// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <WCursor>
#include <QQmlEngine>
#define protected public
#include <QImage>
#undef protected

QT_BEGIN_NAMESPACE
class QQuickItem;
class QPainter;
QT_END_NAMESPACE

WAYLIB_SERVER_BEGIN_NAMESPACE

class WImageRenderTarget : public QPaintDevice
{
public:
    explicit WImageRenderTarget();

    int devType() const override;
    QPaintEngine *paintEngine() const override;

    inline WImageRenderTarget &operator =(const QImage &other) {
        *image.get() = other;
        return *this;
    }
    inline operator const QImage&() const {
        return *image.get();
    }

    inline const uchar *constBits() const {
        return image->constBits();
    }

    inline bool isNull() const {
        return image->isNull();
    }

    inline QSize size() const {
        return image->size();
    }

    void setDevicePixelRatio(qreal dpr);

private:
    int metric(PaintDeviceMetric metric) const override;
    QPaintDevice *redirected(QPoint *offset) const override;

    mutable std::unique_ptr<QImage> image;
};

class WAYLIB_SERVER_EXPORT WQmlHelper : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WaylibHelper)
    Q_PROPERTY(bool hasXWayland READ hasXWayland CONSTANT)
    QML_SINGLETON

public:
    explicit WQmlHelper(QObject *parent = nullptr);

    bool hasXWayland() const;

public Q_SLOTS:
    void itemStackToTop(QQuickItem *item);
    void setCursorShape(QQuickItem *item, WCursor::CursorShape shape);
    Qt::Edges getEdges(const QRectF &rect, const QPointF &pos, qreal edgeSize);
};

WAYLIB_SERVER_END_NAMESPACE
