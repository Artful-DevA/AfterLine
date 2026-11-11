// SPDX-License-Identifier: MIT
#include "app/Settings.h"

#include "app/ModelCatalog.h"

#include <QCoreApplication>
#include <QLocale>

namespace afterline {

QString Settings::speechModel() const { return m_s.value("models/speech", recommendedModel(ModelKind::Speech)).toString(); }
void Settings::setSpeechModel(const QString &id) { m_s.setValue("models/speech", id); }
QString Settings::notesModel() const { return m_s.value("models/notes", recommendedModel(ModelKind::Notes)).toString(); }
void Settings::setNotesModel(const QString &id) { m_s.setValue("models/notes", id); }

QString Settings::language() const
{
    if (m_s.contains("processing/language"))
        return m_s.value("processing/language").toString();
    const QString system = QLocale::system().name().section('_', 0, 0);
    return supportedLanguages().contains(system) ? system : QStringLiteral("auto");
}
void Settings::setLanguage(const QString &code) { m_s.setValue("processing/language", code); }

bool Settings::autoTranscribe() const { return m_s.value("processing/autoTranscribe", true).toBool(); }
void Settings::setAutoTranscribe(bool on) { m_s.setValue("processing/autoTranscribe", on); }
bool Settings::autoNotes() const { return m_s.value("processing/autoNotes", true).toBool(); }
void Settings::setAutoNotes(bool on) { m_s.setValue("processing/autoNotes", on); }
QString Settings::quality() const { return m_s.value("processing/quality", "balanced").toString(); }
void Settings::setQuality(const QString &q) { m_s.setValue("processing/quality", q); }
int Settings::threads() const { return m_s.value("processing/threads", 0).toInt(); }
void Settings::setThreads(int n) { m_s.setValue("processing/threads", n); }
QString Settings::theme() const { return m_s.value("ui/theme", "system").toString(); }
void Settings::setTheme(const QString &t) { m_s.setValue("ui/theme", t); }
bool Settings::closeToTray() const { return m_s.value("ui/closeToTray", true).toBool(); }
void Settings::setCloseToTray(bool on) { m_s.setValue("ui/closeToTray", on); }
bool Settings::liveTranscript() const { return m_s.value("processing/liveTranscript", true).toBool(); }
void Settings::setLiveTranscript(bool on) { m_s.setValue("processing/liveTranscript", on); }
bool Settings::includeMicrophone() const { return m_s.value("recording/includeMicrophone", true).toBool(); }
void Settings::setIncludeMicrophone(bool on) { m_s.setValue("recording/includeMicrophone", on); }
QString Settings::lastSource() const { return m_s.value("recording/source").toString(); }
void Settings::setLastSource(const QString &key) { m_s.setValue("recording/source", key); }
QString Settings::lastMicrophone() const { return m_s.value("recording/microphone").toString(); }
void Settings::setLastMicrophone(const QString &key) { m_s.setValue("recording/microphone", key); }
bool Settings::setupDone() const { return m_s.value("setup/done", false).toBool(); }
void Settings::setSetupDone(bool done) { m_s.setValue("setup/done", done); }
QByteArray Settings::windowGeometry() const { return m_s.value("ui/geometry").toByteArray(); }
void Settings::setWindowGeometry(const QByteArray &g) { m_s.setValue("ui/geometry", g); }

QStringList Settings::supportedLanguages()
{
    return {"auto", "en", "ro", "de", "fr", "es", "it", "pt", "nl", "pl", "hu", "cs", "uk", "ru", "sv",
            "da", "fi", "el", "bg", "tr", "ja", "zh", "ko"};
}

QString Settings::languageName(const QString &code)
{
    if (code == "auto")
        return QCoreApplication::translate("Settings", "Detect automatically");
    const QLocale l(code);
    QString name = l.nativeLanguageName();
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    const QString english = QLocale::languageToString(l.language());
    return name.isEmpty() || name == english ? english : QString("%1 (%2)").arg(name, english);
}

}  // namespace afterline
