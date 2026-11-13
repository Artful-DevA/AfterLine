// SPDX-License-Identifier: MIT
//
// afterline-summarizer: local meeting notes and question answering with a
// GGUF language model (llama.cpp).
//
//   afterline-summarizer --model qwen.gguf --transcript transcript.json
//       --output notes.json [--title "Weekly sync"] [--threads N] [--ctx 8192]
//   afterline-summarizer --model qwen.gguf --ask "What did we decide?"
//       --passages passages.json --output answer.json
//
// Notes are built in sections so meetings of any length fit the model's
// context: each section is turned into structured, cited notes, the results
// are checked against the transcript and merged, and a final short pass
// writes the title and overall summary from the section summaries.
#include "Notes.h"
#include "Protocol.h"

#include <ggml-backend.h>
#include <llama.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <functional>
#include <vector>

using namespace engine;

namespace {

void quietLog(ggml_log_level level, const char *text, void *)
{
    if (level >= GGML_LOG_LEVEL_ERROR)
        std::fputs(text, stderr);
}

class Model {
public:
    ~Model()
    {
        if (m_ctx)
            llama_free(m_ctx);
        if (m_model)
            llama_model_free(m_model);
    }

    bool load(const QString &path, int ctx, int threads, QString *error)
    {
        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = 0;
        m_model = llama_model_load_from_file(path.toLocal8Bit().constData(), mp);
        if (!m_model) {
            *error = "The notes model could not be loaded. It may be damaged: remove it in Settings → AI "
                     "models and download it again.";
            return false;
        }
        m_vocab = llama_model_get_vocab(m_model);
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = uint32_t(ctx);
        cp.n_batch = 512;
        cp.n_threads = threads;
        cp.n_threads_batch = threads;
        cp.no_perf = true;
        m_ctx = llama_init_from_model(m_model, cp);
        if (!m_ctx) {
            *error = "Not enough memory for the notes model. Close other applications or choose the "
                     "smaller notes model in Settings → AI models.";
            return false;
        }
        const char *tmpl = llama_model_chat_template(m_model, nullptr);
        m_template = tmpl ? tmpl : "chatml";
        return true;
    }

    int contextSize() const { return int(llama_n_ctx(m_ctx)); }

    int countTokens(const QString &text) const
    {
        const QByteArray utf8 = text.toUtf8();
        return -llama_tokenize(m_vocab, utf8.constData(), int32_t(utf8.size()), nullptr, 0, false, true);
    }

    // Runs one chat turn constrained by a JSON grammar. Returns the parsed
    // object, or an empty object on failure.
    QJsonObject generate(const QString &system, const QString &user, const QString &grammar, int maxTokens,
                         const std::function<void(double)> &onProgress, QString *error)
    {
        const QByteArray sys = system.toUtf8(), usr = user.toUtf8();
        llama_chat_message msgs[2] = {{"system", sys.constData()}, {"user", usr.constData()}};
        std::vector<char> buf(size_t(sys.size() + usr.size()) * 2 + 1024);
        int len = llama_chat_apply_template(m_template.constData(), msgs, 2, true, buf.data(), int(buf.size()));
        if (len > int(buf.size())) {
            buf.resize(size_t(len) + 1);
            len = llama_chat_apply_template(m_template.constData(), msgs, 2, true, buf.data(), int(buf.size()));
        }
        if (len < 0) {
            *error = "The notes model's chat format is not supported.";
            return {};
        }
        std::vector<llama_token> tokens(size_t(len) + 16);
        const int n = llama_tokenize(m_vocab, buf.data(), len, tokens.data(), int32_t(tokens.size()), true, true);
        if (n < 0) {
            *error = "Could not prepare the transcript for the notes model.";
            return {};
        }
        tokens.resize(size_t(n));
        if (n + maxTokens > contextSize()) {
            *error = "A transcript section is too long for the notes model.";
            return {};
        }
        llama_memory_clear(llama_get_memory(m_ctx), true);
        const int batch = int(llama_n_batch(m_ctx));
        for (int i = 0; i < n; i += batch) {
            if (g_abort)
                return {};
            const int k = std::min(batch, n - i);
            if (llama_decode(m_ctx, llama_batch_get_one(tokens.data() + i, k)) != 0) {
                *error = "The notes model failed while reading the transcript.";
                return {};
            }
            // Reading the transcript is most of the work; count it as 70%.
            onProgress(0.7 * double(i + k) / n);
        }

        const QByteArray g = grammar.toUtf8();
        llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler *gs = llama_sampler_init_grammar(m_vocab, g.constData(), "root");
        if (!gs) {
            llama_sampler_free(chain);
            *error = "Internal error: invalid output grammar.";
            return {};
        }
        llama_sampler_chain_add(chain, gs);
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(chain, llama_sampler_init_penalties(llama_vocab_n_tokens(m_vocab), 256, 1.1f, 0.0f, 0.0f));
        llama_sampler_chain_add(chain, llama_sampler_init_temp(0.2f));
        llama_sampler_chain_add(chain, llama_sampler_init_dist(42));

        QByteArray out;
        bool complete = false;
        for (int i = 0; i < maxTokens; ++i) {
            if (g_abort)
                break;
            llama_token tok = llama_sampler_sample(chain, m_ctx, -1);
            if (llama_vocab_is_eog(m_vocab, tok)) {
                complete = true;
                break;
            }
            char piece[256];
            const int pn = llama_token_to_piece(m_vocab, tok, piece, sizeof(piece), 0, false);
            if (pn > 0)
                out.append(piece, pn);
            if (llama_decode(m_ctx, llama_batch_get_one(&tok, 1)) != 0)
                break;
            if (i % 16 == 0)
                onProgress(0.7 + 0.3 * double(i) / maxTokens);
            // The grammar ends at the closing brace of the root object.
            QJsonParseError pe;
            if (out.endsWith('}') && QJsonDocument::fromJson(out, &pe).isObject()) {
                complete = true;
                break;
            }
        }
        llama_sampler_free(chain);
        if (g_abort)
            return {};
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &pe);
        if (!complete || !doc.isObject()) {
            *error = "The notes model gave an incomplete answer.";
            return {};
        }
        return doc.object();
    }

private:
    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    const llama_vocab *m_vocab = nullptr;
    QByteArray m_template;
};

void usage()
{
    std::fputs("usage: afterline-summarizer --model FILE --transcript FILE --output FILE [--title T]\n"
               "       afterline-summarizer --model FILE --ask QUESTION --passages FILE --output FILE\n"
               "       afterline-summarizer --model FILE --classify NOTES --folders FILE [--title T] --output FILE\n"
               "       [--threads N] [--ctx TOKENS] [--language CODE]\n", stderr);
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail("Cannot read " + path);
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject() && !doc.isArray())
        fail("Damaged file: " + path);
    return doc.isObject() ? doc.object() : QJsonObject{{"items", doc.array()}};
}

int runAsk(Model &model, const QString &question, const QString &passagesPath, const QString &output)
{
    const QJsonArray passages = readJson(passagesPath)["items"].toArray();
    QString error;
    QJsonObject answer;
    if (passages.isEmpty()) {
        answer = {{"found", false}, {"answer", "There is nothing in your meetings about this yet."},
                  {"sources", QJsonArray()}};
    } else {
        const QJsonObject raw = model.generate(notes::askSystemPrompt(), notes::askUserPrompt(question, passages),
                                               notes::askGrammar(), 600,
                                               [](double f) { progress(f, "Thinking"); }, &error);
        if (g_abort) {
            emitEvent({{"type", "cancelled"}});
            return 3;
        }
        if (raw.isEmpty())
            fail(error);
        answer = notes::validateAnswer(raw, int(passages.size()));
    }
    answer["question"] = question;
    if (!writeJsonFile(output, answer, &error))
        fail("Could not save the answer: " + error);
    emitEvent({{"type", "done"}});
    return 0;
}

int runClassify(Model &model, const QString &notesPath, const QString &foldersPath, const QString &title,
                const QString &output)
{
    const QJsonObject notes = readJson(notesPath);
    QStringList folders;
    for (const auto v : readJson(foldersPath)["items"].toArray())
        folders << v.toString();
    QString error;
    const QJsonObject raw = model.generate(notes::classifySystemPrompt(),
                                           notes::classifyUserPrompt(notes, title, folders),
                                           notes::classifyGrammar(folders), 120,
                                           [](double f) { progress(f, "Choosing a folder"); }, &error);
    if (g_abort) {
        emitEvent({{"type", "cancelled"}});
        return 3;
    }
    if (raw.isEmpty())
        fail(error);
    const QJsonObject result = notes::validateClassification(raw, folders);
    if (!writeJsonFile(output, result, &error))
        fail("Could not save the result: " + error);
    emitEvent({{"type", "done"}});
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    installSignalHandlers();
    lowerPriority();

    QString modelPath, transcriptPath, output, title, question, passagesPath, language, classifyNotes, foldersPath;
    int threads = defaultThreads(), ctx = 8192;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&]() -> QString {
            if (i + 1 >= args.size()) {
                usage();
                std::exit(2);
            }
            return args[++i];
        };
        if (a == "--model") modelPath = next();
        else if (a == "--transcript") transcriptPath = next();
        else if (a == "--output") output = next();
        else if (a == "--title") title = next();
        else if (a == "--ask") question = next();
        else if (a == "--passages") passagesPath = next();
        else if (a == "--classify") classifyNotes = next();
        else if (a == "--folders") foldersPath = next();
        else if (a == "--language") language = next();
        else if (a == "--threads") threads = std::max(1, next().toInt());
        else if (a == "--ctx") ctx = std::max(2048, next().toInt());
        else if (a == "--version") {
            std::printf("afterline-summarizer %s\n", AFTERLINE_VERSION);
            return 0;
        } else if (a == "--stdin-control") {
            // handled by installSignalHandlers()
        } else { usage(); return 2; }
    }
    const bool ask = !question.isEmpty();
    const bool classify = !classifyNotes.isEmpty();
    if (modelPath.isEmpty() || output.isEmpty() ||
        (classify ? foldersPath.isEmpty() : ask ? passagesPath.isEmpty() : transcriptPath.isEmpty())) {
        usage();
        return 2;
    }

    llama_log_set(quietLog, nullptr);
    ggml_backend_load_all_from_path(executableDir().toLocal8Bit().constData());
    llama_backend_init();
    progress(0, "Loading the notes model");
    Model model;
    QString error;
    if (!model.load(modelPath, ctx, threads, &error))
        fail(error);

    if (ask)
        return runAsk(model, question, passagesPath, output);
    if (classify)
        return runClassify(model, classifyNotes, foldersPath, title, output);

    const QJsonObject transcript = readJson(transcriptPath);
    const std::vector<notes::Line> lines = notes::loadTranscript(transcript);
    if (language.isEmpty())
        language = transcript["language"].toString("en");
    if (lines.empty())
        fail("The transcript is empty, so there is nothing to write notes about.");

    // Room for the instructions and the answer; the rest is transcript.
    const int budget = model.contextSize() - 2600;
    const auto sections = notes::chunk(lines, [&](const QString &t) { return model.countTokens(t); }, budget);
    const int parts = int(sections.size());
    const int steps = parts + (parts > 0 ? 1 : 0);

    std::vector<QJsonObject> cleaned;
    notes::Stats stats;
    for (int i = 0; i < parts; ++i) {
        const QString stage = parts > 1 ? QString("Writing notes (part %1 of %2)").arg(i + 1).arg(parts)
                                        : QStringLiteral("Writing notes");
        const QJsonObject raw = model.generate(
            notes::extractionSystemPrompt(language), notes::extractionUserPrompt(title, i + 1, parts, sections[size_t(i)]),
            notes::extractionGrammar(), 1800, [&](double f) { progress((i + f) / steps, stage); }, &error);
        if (g_abort) {
            emitEvent({{"type", "cancelled"}});
            return 3;
        }
        if (raw.isEmpty()) {
            // One bad section should not cost the whole meeting its notes.
            std::fprintf(stderr, "section %d: %s\n", i + 1, qPrintable(error));
            stats.dropped++;
            continue;
        }
        cleaned.push_back(notes::validateSection(raw, sections[size_t(i)], lines, &stats));
    }
    if (cleaned.empty())
        fail(error.isEmpty() ? QStringLiteral("No notes could be generated.") : error);

    QJsonObject merged = notes::mergeSections(cleaned);
    QStringList summaries, points;
    for (const auto v : merged["section_summaries"].toArray())
        summaries << v.toString();
    for (const auto v : merged["key_points"].toArray())
        if (points.size() < 12)
            points << v.toObject()["text"].toString();
    const QJsonObject overview = model.generate(
        notes::overviewSystemPrompt(language), notes::overviewUserPrompt(title, summaries, points),
        notes::overviewGrammar(), 400, [&](double f) { progress((parts + f) / steps, "Summarising"); }, &error);
    if (g_abort) {
        emitEvent({{"type", "cancelled"}});
        return 3;
    }
    merged["title"] = overview["title"].toString().trimmed();
    merged["summary"] = overview["summary"].toString().trimmed();
    if (merged["summary"].toString().isEmpty())
        merged["summary"] = summaries.join(' ');
    merged.remove("section_summaries");
    notes::attachTimes(merged, lines);
    merged["version"] = 1;
    merged["model"] = QFileInfo(modelPath).completeBaseName();
    merged["language"] = language;
    merged["created"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    merged["sections"] = parts;
    merged["checks"] = QJsonObject{{"unsupportedItemsRemoved", stats.dropped},
                                   {"ownersCleared", stats.ownersCleared}, {"notFollowUps", stats.notFollowUps},
                                   {"deadlinesCleared", stats.datesCleared}};
    if (!writeJsonFile(output, merged, &error))
        fail("Could not save the notes: " + error);
    emitEvent({{"type", "done"}});
    return 0;
}
