// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <wglobal.h>
#include <qwglobal.h>

#include <QDeadlineTimer>
#include <QFuture>
#include <QObject>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

QW_BEGIN_NAMESPACE
class QWDisplay;
QW_END_NAMESPACE

struct wl_global;

WAYLIB_SERVER_BEGIN_NAMESPACE

typedef bool (*GlobalFilterFunc)(const wl_client *client,
                                 const wl_global *global,
                                 void *data);

class WServer;
class WServerInterface
{
public:
    virtual ~WServerInterface() {}
    inline void *handle() const {
        return m_handle;
    }
    inline bool isValid() const {
        return m_handle;
    }

    template<typename DNativeInterface>
    DNativeInterface *nativeInterface() const {
        return reinterpret_cast<DNativeInterface*>(handle());
    }

    inline WServer *server() const {
        return m_server;
    }

protected:
    void *m_handle = nullptr;
    WServer *m_server = nullptr;

    virtual void create(WServer *server) = 0;
    virtual void destroy(WServer *server) = 0;
    friend class WServer;
    friend class WServerPrivate;
};

class WSocket;
class WServerPrivate;
class WServer : public QObject, public WObject
{
    Q_OBJECT
    W_DECLARE_PRIVATE(WServer)
    friend class WShellInterface;

public:
    explicit WServer(QObject *parent = nullptr);

    QW_NAMESPACE::QWDisplay *handle() const;

    void attach(WServerInterface *interface);
    template<typename Interface, typename... Args>
    Interface *attach(Args&&...args) {
        static_assert(std::is_base_of<WServerInterface, Interface>::value,
                "Not base of WServerInterface");
        auto interface = new Interface(std::forward<Args...>(args...));
        attach(interface);
        return interface;
    }
    template<typename Interface>
    Interface *attach() {
        static_assert(std::is_base_of<WServerInterface, Interface>::value,
                "Not base of WServerInterface");
        auto interface = new Interface();
        attach(interface);
        return interface;
    }
    bool detach(WServerInterface *interface);

    QVector<WServerInterface*> interfaceList() const;
    QVector<WServerInterface*> findInterfaces(void *handle) const;
    WServerInterface *findInterface(void *handle) const;
    template<typename Interface>
    QVector<Interface*> findInterfaces() const {
        QVector<Interface*> list;
        Q_FOREACH(auto i, interfaceList()) {
            if (auto ii = dynamic_cast<Interface*>(i))
                list << ii;
        }

        return list;
    }
    template<typename Interface>
    Interface *findInterface() const {
        Q_FOREACH(auto i, interfaceList()) {
            if (auto ii = dynamic_cast<Interface*>(i))
                return ii;
        }

        return nullptr;
    }

    static WServer *from(WServerInterface *interface);

    void start();
    void stop();
    static void initializeQPA(bool master = true, const QStringList &parameters = {});
    void initializeProxyQPA(int &argc, char **argv, const QStringList &proxyPlatformPlugins = {}, const QStringList &parameters = {});

    bool isRunning() const;
    void addSocket(WSocket *socket);

    QObject *slotOwner() const;

    void setGlobalFilter(GlobalFilterFunc filter, void *data);

Q_SIGNALS:
    void started();

protected:
    WServer(WServerPrivate &dd, QObject *parent = nullptr);
};

WAYLIB_SERVER_END_NAMESPACE
