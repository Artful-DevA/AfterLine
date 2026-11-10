// SPDX-License-Identifier: MIT
#include "ui/Icons.h"

#include "ui/Theme.h"

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace afterline::ui {

namespace {

const QHash<QString, QString> &shapes()
{
    // Stroke paths on a 24×24 grid. "F:" prefix: filled shape.
    static const QHash<QString, QString> s = {
        {"record", "F:<circle cx='12' cy='12' r='7'/>"},
        {"stop", "F:<rect x='6' y='6' width='12' height='12' rx='2'/>"},
        {"pause", "F:<rect x='6' y='5' width='4' height='14' rx='1'/><rect x='14' y='5' width='4' height='14' rx='1'/>"},
        {"play", "F:<path d='M8 5.5v13a1 1 0 0 0 1.5.86l10.5-6.5a1 1 0 0 0 0-1.72L9.5 4.64A1 1 0 0 0 8 5.5z'/>"},
        {"mic", "<rect x='9' y='3' width='6' height='11' rx='3'/><path d='M5 11a7 7 0 0 0 14 0M12 18v3M9 21h6'/>"},
        {"mic-off", "<path d='M9 9v2a3 3 0 0 0 5.1 2.1M15 9.3V6a3 3 0 0 0-5.7-1.3M5 11a7 7 0 0 0 11.6 5.3M19 11a7 7 0 0 1-.6 2.8M12 18v3M9 21h6M3 3l18 18'/>"},
        {"computer", "<rect x='3' y='4' width='18' height='12' rx='2'/><path d='M8 20h8M12 16v4'/>"},
        {"app", "<rect x='3' y='4' width='18' height='16' rx='2'/><path d='M3 9h18M7 6.5h.01M10 6.5h.01'/>"},
        {"speaker", "<path d='M11 5 6 9H3v6h3l5 4V5zM15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13'/>"},
        {"search", "<circle cx='11' cy='11' r='7'/><path d='m20 20-3.5-3.5'/>"},
        {"settings", "<path d='M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0'/><circle cx='16' cy='6' r='2'/><circle cx='10' cy='12' r='2'/><circle cx='18' cy='18' r='2'/>"},
        {"folder", "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z'/>"},
        {"tag", "<path d='M3 12V4a1 1 0 0 1 1-1h8l9 9-9 9-9-9z'/><circle cx='8' cy='8' r='1.5'/>"},
        {"trash", "<path d='M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v5M14 11v5'/>"},
        {"download", "<path d='M12 4v11M7 10l5 5 5-5M5 20h14'/>"},
        {"export", "<path d='M12 15V4M7 8l5-4 5 4M5 13v5a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-5'/>"},
        {"more", "F:<circle cx='5' cy='12' r='1.8'/><circle cx='12' cy='12' r='1.8'/><circle cx='19' cy='12' r='1.8'/>"},
        {"ask", "<path d='M4 5h16v11H9l-5 4V5z'/><path d='M9 9.5h6M9 12.5h4'/>"},
        {"check", "<path d='m5 12 4.5 4.5L19 7'/>"},
        {"alert", "<path d='M12 3 2 20h20L12 3z'/><path d='M12 10v4M12 17h.01'/>"},
        {"back", "<path d='M15 5l-7 7 7 7'/>"},
        {"plus", "<path d='M12 5v14M5 12h14'/>"},
        {"refresh", "<path d='M20 11a8 8 0 0 0-14.3-4.9L4 8M4 4v4h4M4 13a8 8 0 0 0 14.3 4.9L20 16M20 20v-4h-4'/>"},
        {"file", "<path d='M6 3h8l5 5v13H6V3z'/><path d='M14 3v5h5'/>"},
        {"clock", "<circle cx='12' cy='12' r='9'/><path d='M12 7v5l3 2'/>"},
        {"close", "<path d='M6 6l12 12M18 6 6 18'/>"},
        {"copy", "<rect x='8' y='8' width='12' height='12' rx='2'/><path d='M16 8V5a1 1 0 0 0-1-1H5a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3'/>"},
        {"edit", "<path d='M4 20h4L19 9l-4-4L4 16v4zM13.5 6.5l4 4'/>"},
        {"rewind", "<path d='M4 12a8 8 0 1 0 2.3-5.7L4 8.5M4 4v4.5h4.5'/><path d='M10.5 9.5v5M13.5 9.5h2v5'/>"},
        {"forward", "<path d='M20 12a8 8 0 1 1-2.3-5.7L20 8.5M20 4v4.5h-4.5'/><path d='M9 9.5v5M12 9.5h2v5'/>"},
        {"notes", "<path d='M5 4h14v16H5z'/><path d='M8 8h8M8 12h8M8 16h5'/>"},
        {"transcript", "<path d='M4 6h3M10 6h10M4 12h3M10 12h10M4 18h3M10 18h7'/>"},
        {"waveform", "<path d='M3 12h2M7 8v8M11 4v16M15 7v10M19 10v4M21 12h0'/>"},
        {"lock", "<rect x='5' y='11' width='14' height='9' rx='2'/><path d='M8 11V8a4 4 0 0 1 8 0v3'/>"},
    };
    return s;
}

QByteArray svgFor(const QString &name, const QColor &color)
{
    QString body = shapes().value(name, shapes().value("file"));
    const QString c = color.name();
    QString group;
    if (body.startsWith("F:"))
        group = QString("<g fill='%1'>%2</g>").arg(c, body.mid(2));
    else
        group = QString("<g fill='none' stroke='%1' stroke-width='1.8' stroke-linecap='round' "
                        "stroke-linejoin='round'>%2</g>").arg(c, body);
    return QString("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>%1</svg>").arg(group).toUtf8();
}

// Renders crisply at any size and device pixel ratio; disabled state is muted.
class SvgEngine : public QIconEngine {
public:
    SvgEngine(QString name, QColor color) : m_name(std::move(name)), m_color(std::move(color)) {}
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor c = m_color.isValid() ? m_color : Theme::color(Token::Text);
        if (mode == QIcon::Disabled)
            c = Theme::color(Token::Muted);
        QSvgRenderer r(svgFor(m_name, c));
        r.render(p, rect);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }
    QIconEngine *clone() const override { return new SvgEngine(m_name, m_color); }

private:
    QString m_name;
    QColor m_color;
};

}  // namespace

QIcon icon(const QString &name, const QColor &color) { return QIcon(new SvgEngine(name, color)); }

QIcon appIcon(const QString &themeName)
{
    if (!themeName.isEmpty()) {
        QIcon i = QIcon::fromTheme(themeName);
        if (i.isNull())
            i = QIcon::fromTheme(themeName.toLower());
        if (!i.isNull())
            return i;
    }
    if (themeName == "computer")
        return icon("computer");
    if (themeName == "audio-speakers")
        return icon("speaker");
    if (themeName == "audio-input-microphone")
        return icon("mic");
    return icon("app");
}

}  // namespace afterline::ui
