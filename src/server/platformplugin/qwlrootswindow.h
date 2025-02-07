// Copyright (C) 2023 JiDe Zhang <zccrs@live.com>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include "wglobal.h"
#include <qwglobal.h>

#include <QPointer>
#include <qpa/qplatformwindow.h>

QW_BEGIN_NAMESPACE
class QWBuffer;
QW_END_NAMESPACE

WAYLIB_SERVER_BEGIN_NAMESPACE

class QWlrootsScreen;
class Q_DECL_HIDDEN QWlrootsOutputWindow : public QPlatformWindow
{
public:
    QWlrootsOutputWindow(QWindow *window);
    ~QWlrootsOutputWindow();

    void initialize() override;

    QWlrootsScreen *qwScreen() const;
    QPlatformScreen *screen() const override;
    void setGeometry(const QRect &rect) override;
    QRect geometry() const override;

    WId winId() const override;
    qreal devicePixelRatio() const override;

    void setBuffer(QW_NAMESPACE::QWBuffer *buffer);
    QW_NAMESPACE::QWBuffer *buffer() const;

    bool attachRenderer();
    void detachRenderer();

private:
    QPointer<QW_NAMESPACE::QWBuffer> renderBuffer;
    bool bufferAttached = false;
    QMetaObject::Connection onScreenChangedConnection;
    QMetaObject::Connection onScreenGeometryConnection;
};

class WCursor;
class Q_DECL_HIDDEN QWlrootsRenderWindow : public QPlatformWindow
{
    friend class QWlrootsCursor;
public:
    QWlrootsRenderWindow(QWindow *window);

    void initialize() override;
    void setGeometry(const QRect &rect) override;

    WId winId() const override;
    qreal devicePixelRatio() const override;
    void setDevicePixelRatio(qreal dpr);
    bool beforeDisposeEventFilter(QEvent *event);
    bool afterDisposeEventFilter(QEvent *event);

private:
    qreal dpr = 1.0;
    QPointer<WCursor> lastActiveCursor;
};

WAYLIB_SERVER_END_NAMESPACE
