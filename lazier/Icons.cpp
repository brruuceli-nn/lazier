#include "Icons.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtSvg/QSvgRenderer>

namespace {

const char *kSvgPath = ":/lazier/image/read.svg";

QSvgRenderer *renderer()
{
    static QSvgRenderer instance(QString::fromUtf8(kSvgPath));
    return &instance;
}

QPixmap renderSvg(int size)
{
    if (!renderer()->isValid() || size <= 0)
        return QPixmap();

    QImage canvas(size, size, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer()->render(&painter, QRectF(0, 0, size, size));
    return QPixmap::fromImage(canvas);
}

QRect opaqueBounds(const QImage &img)
{
    int minX = img.width();
    int minY = img.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 16) {
                if (x < minX)
                    minX = x;
                if (y < minY)
                    minY = y;
                if (x > maxX)
                    maxX = x;
                if (y > maxY)
                    maxY = y;
            }
        }
    }
    if (maxX < 0)
        return img.rect();
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QPixmap packedPixmap(int size)
{
    const QImage src = renderSvg(qMax(size * 2, 64)).toImage().convertToFormat(QImage::Format_ARGB32);
    if (src.isNull())
        return QPixmap();
    const QImage cropped = src.copy(opaqueBounds(src));
    QImage canvas(size, size, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    const QImage fitted = cropped.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage((size - fitted.width()) / 2, (size - fitted.height()) / 2, fitted);
    return QPixmap::fromImage(canvas);
}

} // namespace

QIcon lazierAppIcon()
{
    QIcon icon;
    const int sizes[] = { 16, 20, 24, 32, 48, 64, 128, 256 };
    for (int size : sizes)
        icon.addPixmap(renderSvg(size));
    return icon;
}

QIcon lazierTrayIcon()
{
    QIcon icon;
    const int sizes[] = { 16, 20, 24, 32, 48 };
    for (int size : sizes)
        icon.addPixmap(packedPixmap(size));
    return icon;
}

QPixmap lazierAppPixmap(int size)
{
    return packedPixmap(size);
}
