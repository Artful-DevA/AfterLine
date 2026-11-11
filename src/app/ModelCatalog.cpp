// SPDX-License-Identifier: MIT
#include "app/ModelCatalog.h"

#include "core/Paths.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

namespace afterline {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("ModelCatalog", s); }
const QString kWhisper = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

qint64 meminfo(const char *key)
{
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith(key))
            return line.mid(int(qstrlen(key))).trimmed().split(' ').value(0).toLongLong() * 1024;
    return 0;
}
}  // namespace

QString ModelSpec::path() const { return modelsDir() + "/" + fileName; }

bool ModelSpec::installed() const
{
    const QFileInfo fi(path());
    return fi.exists() && fi.size() == bytes;
}

const QVector<ModelSpec> &modelCatalog()
{
    static const QVector<ModelSpec> catalog = {
        {"whisper-base", ModelKind::Speech, tr("Fast"),
         tr("Quickest transcription. Good for clear English; less accurate for other languages."),
         "ggml-base-q5_1.bin", kWhisper + "ggml-base-q5_1.bin",
         "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898", 59707625, 400, "MIT",
         tr("99 languages"), false},
        {"whisper-small", ModelKind::Speech, tr("Balanced"),
         tr("Good accuracy at about real-time speed on a typical laptop."),
         "ggml-small-q5_1.bin", kWhisper + "ggml-small-q5_1.bin",
         "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb", 190085487, 800, "MIT",
         tr("99 languages"), true},
        {"whisper-large-v3-turbo", ModelKind::Speech, tr("Most accurate"),
         tr("Best accuracy, especially for Romanian and other non-English meetings. Slower on older CPUs."),
         "ggml-large-v3-turbo-q5_0.bin", kWhisper + "ggml-large-v3-turbo-q5_0.bin",
         "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", 574041195, 1800, "MIT",
         tr("99 languages"), false},
        {"silero-vad", ModelKind::SpeechDetector, tr("Speech detection"),
         tr("Finds the parts of a recording that contain speech. Installed with any transcription model."),
         "ggml-silero-v6.2.0.bin", "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin",
         "2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987", 885098, 50, "MIT",
         tr("Any"), true},
        {"qwen3-4b", ModelKind::Notes, tr("Qwen3 4B"),
         tr("Clear, well-structured notes. Needs about 4 GB of free memory while writing notes."),
         "Qwen3-4B-Instruct-2507-Q4_K_M.gguf",
         "https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/Qwen3-4B-Instruct-2507-Q4_K_M.gguf",
         "3605803b982cb64aead44f6c1b2ae36e3acdb41d8e46c8a94c6533bc4c67e597", 2497281120, 4000, "Apache-2.0",
         tr("English, Romanian and 100+ others"), true},
        {"qwen3-1.7b", ModelKind::Notes, tr("Qwen3 1.7B"),
         tr("For computers with 8 GB of memory or less. Faster, with simpler notes."),
         "Qwen3-1.7B-Q4_K_M.gguf",
         "https://huggingface.co/unsloth/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q4_K_M.gguf",
         "b139949c5bd74937ad8ed8c8cf3d9ffb1e99c866c823204dc42c0d91fa181897", 1107409472, 2000, "Apache-2.0",
         tr("English, Romanian and 100+ others"), false},
    };
    return catalog;
}

const ModelSpec *findModel(const QString &id)
{
    for (const auto &m : modelCatalog())
        if (m.id == id)
            return &m;
    return nullptr;
}

QString friendlyModelName(const QString &recorded)
{
    for (const auto &m : modelCatalog()) {
        const QString base = m.fileName.left(m.fileName.lastIndexOf('.'));
        if (recorded == m.id || recorded == m.fileName || recorded == base)
            return m.name;
    }
    return recorded;
}

const ModelSpec *findModelBySha256(const QString &sha256)
{
    for (const auto &m : modelCatalog())
        if (m.sha256.compare(sha256, Qt::CaseInsensitive) == 0)
            return &m;
    return nullptr;
}

QVector<ModelSpec> modelsOfKind(ModelKind kind)
{
    QVector<ModelSpec> out;
    for (const auto &m : modelCatalog())
        if (m.kind == kind)
            out.push_back(m);
    return out;
}

qint64 totalMemoryBytes() { return meminfo("MemTotal:"); }
qint64 availableMemoryBytes() { return meminfo("MemAvailable:"); }

QString recommendedModel(ModelKind kind)
{
    const qint64 gb = totalMemoryBytes() / (1024LL * 1024 * 1024);
    switch (kind) {
    case ModelKind::Speech:
        return gb > 0 && gb < 6 ? "whisper-base" : "whisper-small";
    case ModelKind::SpeechDetector:
        return "silero-vad";
    case ModelKind::Notes:
        return gb > 0 && gb < 12 ? "qwen3-1.7b" : "qwen3-4b";
    }
    return {};
}

QString formatBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

}  // namespace afterline
