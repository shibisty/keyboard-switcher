#include "icons.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSettings>

namespace ui {

namespace {

void drawKeyboard(QPainter &p, const QRectF &r, const QColor &c)
{
    const qreal s = r.width();
    const QRectF body(r.left() + s * 0.06, r.top() + s * 0.22, s * 0.88, s * 0.56);
    p.setPen(QPen(c, qMax<qreal>(1.0, s * 0.08)));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, s * 0.1, s * 0.1);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal k = s * 0.09;
    const qreal gap = (body.width() - s * 0.2 - 4 * k) / 3.0;
    for (int row = 0; row < 2; ++row) {
        const qreal y = body.top() + s * 0.12 + row * (k + s * 0.05);
        for (int i = 0; i < 4; ++i)
            p.drawRect(QRectF(body.left() + s * 0.1 + i * (k + gap), y, k, k));
    }
    p.drawRect(QRectF(body.left() + s * 0.24, body.bottom() - s * 0.17, body.width() - s * 0.48, k));
}

void draw(QPainter &p, Icon kind, int s, const QColor &c)
{
    const qreal w = qMax<qreal>(1.2, s / 10.0);
    QPen pen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRectF r(0, 0, s, s);
    const QPointF ctr = r.center();

    switch (kind) {
    case Icon::Add:
        p.drawLine(QPointF(s * 0.5, s * 0.2), QPointF(s * 0.5, s * 0.8));
        p.drawLine(QPointF(s * 0.2, s * 0.5), QPointF(s * 0.8, s * 0.5));
        break;
    case Icon::Delete: {
        p.drawLine(QPointF(s * 0.18, s * 0.27), QPointF(s * 0.82, s * 0.27));
        p.drawLine(QPointF(s * 0.4, s * 0.15), QPointF(s * 0.6, s * 0.15));
        QPainterPath body;
        body.moveTo(s * 0.27, s * 0.27);
        body.lineTo(s * 0.32, s * 0.85);
        body.lineTo(s * 0.68, s * 0.85);
        body.lineTo(s * 0.73, s * 0.27);
        p.drawPath(body);
        p.drawLine(QPointF(s * 0.45, s * 0.42), QPointF(s * 0.45, s * 0.7));
        p.drawLine(QPointF(s * 0.55, s * 0.42), QPointF(s * 0.55, s * 0.7));
        break;
    }
    case Icon::Pause:
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(s * 0.26, s * 0.2, s * 0.16, s * 0.6), s * 0.04, s * 0.04);
        p.drawRoundedRect(QRectF(s * 0.58, s * 0.2, s * 0.16, s * 0.6), s * 0.04, s * 0.04);
        break;
    case Icon::Play: {
        QPainterPath tri;
        tri.moveTo(s * 0.3, s * 0.2);
        tri.lineTo(s * 0.78, s * 0.5);
        tri.lineTo(s * 0.3, s * 0.8);
        tri.closeSubpath();
        p.setBrush(c);
        p.drawPath(tri);
        break;
    }
    case Icon::Refresh: {
        const QRectF arc(s * 0.2, s * 0.2, s * 0.6, s * 0.6);
        p.drawArc(arc, 60 * 16, 290 * 16);
        // arrow head at the arc start (60 degrees)
        const QPointF tip(ctr.x() + s * 0.3 * 0.5, ctr.y() - s * 0.3 * 0.866);
        p.drawLine(tip, tip + QPointF(-s * 0.2, -s * 0.03));
        p.drawLine(tip, tip + QPointF(-s * 0.02, s * 0.2));
        break;
    }
    case Icon::Globe: {
        const QRectF g(s * 0.15, s * 0.15, s * 0.7, s * 0.7);
        p.drawEllipse(g);
        p.drawEllipse(QRectF(s * 0.35, s * 0.15, s * 0.3, s * 0.7));
        p.drawLine(QPointF(s * 0.15, s * 0.5), QPointF(s * 0.85, s * 0.5));
        break;
    }
    case Icon::Stack:
        p.drawRoundedRect(QRectF(s * 0.14, s * 0.46, s * 0.72, s * 0.36), s * 0.06, s * 0.06);
        p.drawLine(QPointF(s * 0.22, s * 0.34), QPointF(s * 0.78, s * 0.34));
        p.drawLine(QPointF(s * 0.3, s * 0.2), QPointF(s * 0.7, s * 0.2));
        break;
    case Icon::App:
        p.drawRoundedRect(QRectF(s * 0.15, s * 0.2, s * 0.7, s * 0.6), s * 0.08, s * 0.08);
        p.drawLine(QPointF(s * 0.15, s * 0.36), QPointF(s * 0.85, s * 0.36));
        break;
    case Icon::Keyboard:
        drawKeyboard(p, r, c);
        break;
    }
}

QColor taskbarColor()
{
#if defined(Q_OS_WIN)
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                  QSettings::NativeFormat);
    const bool light = reg.value(QStringLiteral("SystemUsesLightTheme"), 0).toInt() == 1;
    return light ? QColor(0x1f, 0x1f, 0x1f) : QColor(Qt::white);
#elif defined(Q_OS_MACOS)
    return QColor(Qt::black); // template image, macOS recolours it
#else
    return QGuiApplication::palette().color(QPalette::WindowText);
#endif
}

} // namespace

QIcon icon(Icon kind, const QColor &color)
{
    QIcon result;
    for (int s : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p, kind, s, color);
        p.end();
        result.addPixmap(pm);
    }
    return result;
}

QIcon icon(Icon kind)
{
    return icon(kind, QGuiApplication::palette().color(QPalette::WindowText));
}

QIcon trayIcon(bool paused)
{
    const QColor fg = taskbarColor();
    QIcon result;
    for (int s : {16, 20, 22, 24, 32, 40, 44, 48, 64}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(paused ? 0.45 : 1.0);
        drawKeyboard(p, QRectF(0, 0, s, s), fg);
        if (paused) {
            p.setOpacity(1.0);
            const qreal d = s * 0.5;
            const QRectF badge(s - d, s - d, d, d);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xF7, 0xA2, 0x1B));
            p.drawEllipse(badge);
            p.setBrush(QColor(0x1f, 0x1f, 0x1f));
            const qreal bw = d * 0.16, bh = d * 0.5, y = badge.center().y() - bh / 2;
            p.drawRect(QRectF(badge.center().x() - bw * 1.5, y, bw, bh));
            p.drawRect(QRectF(badge.center().x() + bw * 0.5, y, bw, bh));
        }
        p.end();
        result.addPixmap(pm);
    }
#if defined(Q_OS_MACOS)
    result.setIsMask(true);
#endif
    return result;
}

} // namespace ui
