#pragma once

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>

// Self-contained icon for AppImages and standalone runs: no reliance on
// the host hicolor theme (which doesn't know this application's icon).
inline QIcon makeAppIcon()
{
    QSvgRenderer renderer(QStringLiteral(":/kgitdo/icon.svg"));
    QIcon icon;
    if (!renderer.isValid()) {
        return icon;
    }
    for (const int size : {32, 48, 64, 128, 256}) {
        QPixmap pixels(size, size);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixels);
    }
    return icon;
}
