// SPDX-License-Identifier: MIT
#pragma once

#include <QSettings>
#include <QString>

namespace afterline {

// Typed access to ~/.config/AfterLine/AfterLine.conf.
class Settings {
public:
    QString speechModel() const;
    void setSpeechModel(const QString &id);
    QString notesModel() const;
    void setNotesModel(const QString &id);
    // "auto" or an ISO 639-1 code. Defaults to the desktop language when
    // supported: telling the recogniser the language is far more reliable
    // than letting it guess from the first seconds.
    QString language() const;
    void setLanguage(const QString &code);
    bool autoTranscribe() const;
    void setAutoTranscribe(bool on);
    bool autoNotes() const;
    void setAutoNotes(bool on);
    QString quality() const;  // fast | balanced | accurate
    void setQuality(const QString &q);
    int threads() const;      // 0 = automatic
    void setThreads(int n);
    QString theme() const;    // system | light | dark
    void setTheme(const QString &t);
    bool liveTranscript() const;  // preliminary transcript while recording
    void setLiveTranscript(bool on);
    bool closeToTray() const;
    void setCloseToTray(bool on);
    bool includeMicrophone() const;
    void setIncludeMicrophone(bool on);
    QString lastSource() const;
    void setLastSource(const QString &key);
    QString lastMicrophone() const;
    void setLastMicrophone(const QString &key);
    bool setupDone() const;
    void setSetupDone(bool done);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &g);

    static QStringList supportedLanguages();  // codes offered in the UI
    static QString languageName(const QString &code);

private:
    mutable QSettings m_s{"AfterLine", "AfterLine"};
};

}  // namespace afterline
