// Copyright (C) 2023 JiDe Zhang <zccrs@live.com>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <QObject>
#define private public
#include <QScreen>
#undef private

#include "qwlrootsintegration.h"
#include "qwlrootscreen.h"
#include "qwlrootswindow.h"
#include "woutput.h"
#include "winputdevice.h"
#include "types.h"

#include <qwoutput.h>
#include <qwrenderer.h>
#include <qwinputdevice.h>
#include <qwdisplay.h>

#include <QOffscreenSurface>
#include <QGuiApplication>

#include <private/qgenericunixfontdatabase_p.h>
#include <private/qgenericunixservices_p.h>
#include <private/qgenericunixeventdispatcher_p.h>
#include <private/qhighdpiscaling_p.h>
#if QT_CONFIG(vulkan)
#include <private/qvulkaninstance_p.h>
#include <private/qbasicvulkanplatforminstance_p.h>
#include <private/qguiapplication_p.h>
#endif
#include <private/qinputdevice_p.h>
#include <qpa/qplatformsurface.h>
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qplatformoffscreensurface.h>

#ifndef QT_NO_OPENGL
#include <qpa/qplatformopenglcontext.h>
#include <private/qeglconvenience_p.h>
#include <EGL/egl.h>

extern "C" {
#include <wlr/render/egl.h>
#define static
#include <wlr/render/gles2.h>
#undef static
}

#endif // QT_NO_OPENGL

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_tablet_pad.h>
}

WAYLIB_SERVER_BEGIN_NAMESPACE

#define CALL_PROXY2(FunName, fallbackValue, ...) m_proxyIntegration ? m_proxyIntegration->FunName(__VA_ARGS__) : fallbackValue
#define CALL_PROXY(FunName, ...) CALL_PROXY2(FunName, QPlatformIntegration::FunName(__VA_ARGS__), __VA_ARGS__)

class OffscreenSurface : public QPlatformOffscreenSurface
{
public:
    OffscreenSurface(QOffscreenSurface *surface)
        : QPlatformOffscreenSurface(surface) {}

    bool isValid() const override {
        return true;
    }

    QPlatformScreen *screen() const override {
        return nullptr;
    }
};

QWlrootsIntegration *QWlrootsIntegration::m_instance = nullptr;
QWlrootsIntegration::QWlrootsIntegration(bool master, const QStringList &parameters, std::function<void ()> onInitialized)
    : m_master(master)
    , m_onInitialized(onInitialized)
{
    Q_UNUSED(parameters);
    Q_ASSERT(!m_instance);
    m_instance = this;
}

QWlrootsIntegration::~QWlrootsIntegration()
{
    if (m_instance == this)
        m_instance = nullptr;
}

QWlrootsIntegration *QWlrootsIntegration::instance()
{
    return m_instance;
}

void QWlrootsIntegration::setProxy(QPlatformIntegration *proxy)
{
    Q_ASSERT(!m_proxyIntegration);
    Q_ASSERT_X(!m_master, "initializeProxyQPA", "Can't set proxy plugin for a master QPA plugin.");
    m_proxyIntegration.reset(proxy);
    if (m_placeholderScreen) {
        QWindowSystemInterface::handleScreenRemoved(m_placeholderScreen.release());
    }
    m_fontDb.reset();
}

QWlrootsScreen *QWlrootsIntegration::addScreen(WOutput *output)
{
    m_screens << new QWlrootsScreen(output);

    if (isMaster()) {
        QWindowSystemInterface::handleScreenAdded(m_screens.last());

        if (m_placeholderScreen) {
            QWindowSystemInterface::handleScreenRemoved(m_placeholderScreen.release());
        }
    } else {
        Q_UNUSED(new QScreen(m_screens.last()))
    }

    m_screens.last()->initialize();
    output->setScreen(m_screens.last());

    return m_screens.last();
}

void QWlrootsIntegration::removeScreen(WOutput *output)
{
    if (auto screen = output->screen()) {
        output->setScreen(nullptr);
        bool ok = m_screens.removeOne(screen);

        if (!isMaster()) {
            delete screen->screen();
            delete screen;
            return;
        }

        if (ok && m_screens.isEmpty()) {
            m_placeholderScreen.reset(new QPlatformPlaceholderScreen);
            QWindowSystemInterface::handleScreenAdded(m_placeholderScreen.get(), true);
        }

        QWindowSystemInterface::handleScreenRemoved(screen);
    }
}

QWlrootsScreen *QWlrootsIntegration::getScreenFrom(const WOutput *output)
{
    return output->screen();
}

QInputDevice *QWlrootsIntegration::addInputDevice(WInputDevice *device, const QString &seatName)
{
    QInputDevice *qtdev = nullptr;
    auto qwDevice = device->handle();
    const QString name = QString::fromUtf8(qwDevice->handle()->name);
    qint64 systemId = reinterpret_cast<qint64>(device);

    switch (qwDevice->handle()->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        qtdev = new QInputDevice(name, systemId, QInputDevice::DeviceType::Keyboard, seatName);
        break;
    }
    case WLR_INPUT_DEVICE_POINTER: {
        qtdev = new QPointingDevice(name, systemId, QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Generic,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Hover
                                        | QInputDevice::Capability::Scroll | QInputDevice::Capability::MouseEmulation,
                                    10, 32, seatName, QPointingDeviceUniqueId());
        break;
    }
    case WLR_INPUT_DEVICE_TOUCH: {
        qtdev = new QPointingDevice(name, systemId, QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Area | QInputDevice::Capability::MouseEmulation,
                                    10, 32, seatName, QPointingDeviceUniqueId());
        break;
    }
    case WLR_INPUT_DEVICE_TABLET_TOOL: {
        qtdev = new QPointingDevice(name, systemId, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                    QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt | QInputDevice::Capability::Pressure,
                                    1, 32, seatName, QPointingDeviceUniqueId());
        break;
    }
    case WLR_INPUT_DEVICE_TABLET_PAD: {
        auto pad = wlr_tablet_pad_from_input_device(qwDevice->handle());
        qtdev = new QPointingDevice(name, systemId, QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Pen,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Hover | QInputDevice::Capability::Pressure,
                                    1, pad->button_count, seatName, QPointingDeviceUniqueId());
        break;
    }
    case WLR_INPUT_DEVICE_SWITCH: {
        qtdev = new QInputDevice(name, systemId, QInputDevice::DeviceType::Keyboard, seatName);
        break;
    }
    }

    if (qtdev) {
        device->setQtDevice(qtdev);
        QWindowSystemInterface::registerInputDevice(qtdev);

        if (qtdev->type() == QInputDevice::DeviceType::Mouse || qtdev->type() == QInputDevice::DeviceType::TouchPad) {
            auto primaryQtDevice = QPointingDevice::primaryPointingDevice();
            if (!WInputDevice::from(primaryQtDevice)) {
                // Ensure the primary pointing device is the WInputDevice
                auto pd = const_cast<QPointingDevice*>(primaryQtDevice);
                pd->setParent(nullptr);
                delete pd;
            }
            Q_ASSERT(WInputDevice::from(QPointingDevice::primaryPointingDevice()));
        } else if (qtdev->type() == QInputDevice::DeviceType::Keyboard) {
            auto primaryQtDevice = QInputDevice::primaryKeyboard();
            if (!WInputDevice::from(primaryQtDevice)) {
                // Ensure the primary keyboard device is the WInputDevice
                auto pd = const_cast<QInputDevice*>(primaryQtDevice);
                pd->setParent(nullptr);
                delete pd;
            }
            Q_ASSERT(WInputDevice::from(QInputDevice::primaryKeyboard()));
        }
    }

    return qtdev;
}

QInputDevice *QWlrootsIntegration::removeInputDevice(WInputDevice *device)
{
    if (auto qdevice = getInputDeviceFrom(device)) {
        device->setQtDevice(nullptr);
        qdevice->deleteLater();
        return qdevice;
    }

    return nullptr;
}

QInputDevice *QWlrootsIntegration::getInputDeviceFrom(WInputDevice *device)
{
    return device->qtDevice();
}

void QWlrootsIntegration::initialize()
{
    if (isMaster()) {
        m_services.reset(new QGenericUnixServices);
    }

    if (m_onInitialized)
        m_onInitialized();

    if (!qGuiApp->primaryScreen()) {
        m_placeholderScreen.reset(new QPlatformPlaceholderScreen);
        QWindowSystemInterface::handleScreenAdded(m_placeholderScreen.get(), true);
    }
}

void QWlrootsIntegration::destroy()
{
    if (m_placeholderScreen)
        QWindowSystemInterface::handleScreenRemoved(m_placeholderScreen.release());

    if (m_proxyIntegration)
        m_proxyIntegration->destroy();
}

bool QWlrootsIntegration::hasCapability(Capability cap) const
{
    if (m_proxyIntegration) {
        return CALL_PROXY(hasCapability, cap);
    }

    switch (cap) {
    case OpenGL:
    case ThreadedOpenGL:
    case BufferQueueingOpenGL:
    case ThreadedPixmaps:
    case WindowMasks:
    case MultipleWindows:
    case ApplicationState:
    case ForeignWindows:
    case NonFullScreenWindows:
    case WindowManagement:
    case WindowActivation:
    case SyncState:
    case RasterGLSurface:
    case AllGLFunctionsQueryable:
    case ApplicationIcon:
    case SwitchableWidgetComposition:
    case OpenGLOnRasterSurface:
    case MaximizeUsingFullscreenGeometry:
    case PaintEvents:
    case RhiBasedRendering:
    case ScreenWindowGrabbing:
        return true;
    default: return false;
    }
}

QPlatformFontDatabase *QWlrootsIntegration::fontDatabase() const
{
    if (m_proxyIntegration)
        return m_proxyIntegration->fontDatabase();

    if (!m_fontDb)
        m_fontDb.reset(new QGenericUnixFontDatabase);
    return m_fontDb.get();
}

QPlatformWindow *QWlrootsIntegration::createPlatformWindow(QWindow *window) const
{
    if (QW::Window::check(window)) {
        Q_ASSERT(window->screen() && (window->screen()->handle() == m_placeholderScreen.get()
                                      || dynamic_cast<QWlrootsScreen*>(window->screen()->handle())));
        return new QWlrootsOutputWindow(window);
    } else if (QW::RenderWindow::check(window)) {
        return new QWlrootsRenderWindow(window);
    }

    return CALL_PROXY2(createPlatformWindow, nullptr, window);
}

QPlatformBackingStore *QWlrootsIntegration::createPlatformBackingStore(QWindow *window) const
{
    return CALL_PROXY2(createPlatformBackingStore, nullptr, window);
}

static inline WOutput *outputFrom(QWlrootsOutputWindow *surface) {
    auto ws = surface->qwScreen();
    return ws ? ws->output() : nullptr;
}

static inline WOutput *outputFrom(QPlatformSurface *surface) {
    auto w = dynamic_cast<QWlrootsOutputWindow*>(surface);
    return w ? outputFrom(w) : nullptr;
}

static inline QWOutput *qoutputFrom(QPlatformSurface *surface) {
    auto o = outputFrom(surface);
    return o ? o->handle() : nullptr;
}

#ifndef QT_NO_OPENGL
class Q_DECL_HIDDEN OpenGLContext : public QPlatformOpenGLContext {
public:
    explicit OpenGLContext(QOpenGLContext *context)
        : m_context(context)
    {}

    void initialize() override {
        m_format.setBlueBufferSize(8);
        m_format.setRedBufferSize(8);
        m_format.setGreenBufferSize(8);
        m_format.setDepthBufferSize(8);
        m_format.setStencilBufferSize(8);
        m_format.setRenderableType(QSurfaceFormat::OpenGLES);

        if (auto c = qobject_cast<QW::OpenGLContext*>(m_context)) {
            auto eglConfig = q_configFromGLFormat(c->eglDisplay(), m_format, false, EGL_WINDOW_BIT);
            if (eglConfig)
                m_format = q_glFormatFromConfig(c->eglDisplay(), eglConfig, m_format);
            Q_ASSERT(m_format.renderableType() == QSurfaceFormat::OpenGLES);
        } else {
            qWarning() << "The QSurfaceFormat is not initialize";
        }
    }

    QSurfaceFormat format() const override {
        return m_format;
    }

    void swapBuffers(QPlatformSurface *surface) override {
        if (QWOutput *output = qoutputFrom(surface))
            output->commit();
    }
    GLuint defaultFramebufferObject(QPlatformSurface *surface) const override {
        if (QWOutput *output = qoutputFrom(surface)) {
            return wlr_gles2_renderer_get_current_fbo(output->handle()->renderer);
        }

        return 0;
    }

    bool makeCurrent(QPlatformSurface *surface) override {
        auto window = dynamic_cast<QWlrootsOutputWindow*>(surface);
        if (window && window->attachRenderer()) {
            m_currentSurface = window;
            return true;
        }

        if (auto c = qobject_cast<QW::OpenGLContext*>(m_context)) {
            return eglMakeCurrent(c->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, c->eglContext());
        }

        return false;
    }
    void doneCurrent() override {
        if (m_currentSurface) {
            m_currentSurface->detachRenderer();
            m_currentSurface = nullptr;
        } else if (auto c = qobject_cast<QW::OpenGLContext*>(m_context)) {
            eglMakeCurrent(c->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    }

    QFunctionPointer getProcAddress(const char *procName) override {
        return eglGetProcAddress(procName);
    }

private:
    QOpenGLContext *m_context = nullptr;
    QWlrootsOutputWindow *m_currentSurface = nullptr;
    QSurfaceFormat m_format;
};

QPlatformOpenGLContext *QWlrootsIntegration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    if (QW::OpenGLContext::check(context))
        return new OpenGLContext(context);

    return CALL_PROXY(createPlatformOpenGLContext, context);
}

QOpenGLContext::OpenGLModuleType QWlrootsIntegration::openGLModuleType()
{
    return QOpenGLContext::LibGLES;
}
#endif

QAbstractEventDispatcher *QWlrootsIntegration::createEventDispatcher() const
{
    return CALL_PROXY2(createEventDispatcher, createUnixEventDispatcher());
}

QPlatformNativeInterface *QWlrootsIntegration::nativeInterface() const
{
    return CALL_PROXY(nativeInterface);
}

QPlatformPixmap *QWlrootsIntegration::createPlatformPixmap(QPlatformPixmap::PixelType type) const
{
    return CALL_PROXY(createPlatformPixmap, type);
}

QPlatformWindow *QWlrootsIntegration::createForeignWindow(QWindow *window, WId id) const
{
    return CALL_PROXY(createForeignWindow, window, id);
}

QPlatformSharedGraphicsCache *QWlrootsIntegration::createPlatformSharedGraphicsCache(const char *cacheId) const
{
    return CALL_PROXY(createPlatformSharedGraphicsCache, cacheId);
}

QPaintEngine *QWlrootsIntegration::createImagePaintEngine(QPaintDevice *paintDevice) const
{
    return CALL_PROXY(createImagePaintEngine, paintDevice);
}

#ifndef QT_NO_CLIPBOARD
QPlatformClipboard *QWlrootsIntegration::clipboard() const
{
    return CALL_PROXY(clipboard);
}
#endif

#if QT_CONFIG(draganddrop)
QPlatformDrag *QWlrootsIntegration::drag() const
{
    return CALL_PROXY(drag);
}
#endif

QPlatformInputContext *QWlrootsIntegration::inputContext() const
{
    return CALL_PROXY(inputContext);
}

#if QT_CONFIG(accessibility)
QPlatformAccessibility *QWlrootsIntegration::accessibility() const
{
    return nullptr;
}
#endif

QPlatformServices *QWlrootsIntegration::services() const
{
    if (isMaster())
        return m_services.get();

    return CALL_PROXY(services);
}

QVariant QWlrootsIntegration::styleHint(StyleHint hint) const
{
    return CALL_PROXY(styleHint, hint);
}

Qt::WindowState QWlrootsIntegration::defaultWindowState(Qt::WindowFlags flags) const
{
    return CALL_PROXY(defaultWindowState, flags);
}

Qt::KeyboardModifiers QWlrootsIntegration::queryKeyboardModifiers() const
{
    return CALL_PROXY(queryKeyboardModifiers);
}

QList<int> QWlrootsIntegration::possibleKeys(const QKeyEvent *event) const
{
    return CALL_PROXY(possibleKeys, event);
}

QStringList QWlrootsIntegration::themeNames() const
{
    return CALL_PROXY(themeNames);
}

QPlatformTheme *QWlrootsIntegration::createPlatformTheme(const QString &name) const
{
    return CALL_PROXY(createPlatformTheme, name);
}

QPlatformOffscreenSurface *QWlrootsIntegration::createPlatformOffscreenSurface(QOffscreenSurface *surface) const
{
    if (QW::OffscreenSurface::check(surface))
        return new OffscreenSurface(surface);

    return CALL_PROXY(createPlatformOffscreenSurface, surface);
}

#ifndef QT_NO_SESSIONMANAGER
QPlatformSessionManager *QWlrootsIntegration::createPlatformSessionManager(const QString &id, const QString &key) const
{
    return CALL_PROXY(createPlatformSessionManager, id, key);
}
#endif

void QWlrootsIntegration::sync()
{
    CALL_PROXY(sync);
}

void QWlrootsIntegration::setApplicationIcon(const QIcon &icon) const
{
    CALL_PROXY(setApplicationIcon, icon);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
void QWlrootsIntegration::setApplicationBadge(qint64 number)
{
    CALL_PROXY(setApplicationBadge, number);
}
#endif

void QWlrootsIntegration::beep() const
{
    CALL_PROXY(beep);
}

void QWlrootsIntegration::quit() const
{
    CALL_PROXY(quit);
}

#if QT_CONFIG(vulkan)
class VulkanInstance : public QBasicPlatformVulkanInstance
{
public:
    VulkanInstance(QVulkanInstance *instance)
        : m_instance(instance)
    {
        loadVulkanLibrary(QStringLiteral("vulkan"));
    }

    void createOrAdoptInstance() override {
        initInstance(m_instance, {});
    }

private:
    QVulkanInstance *m_instance;
};

QPlatformVulkanInstance *QWlrootsIntegration::createPlatformVulkanInstance(QVulkanInstance *instance) const
{
    return m_proxyIntegration ? m_proxyIntegration->createPlatformVulkanInstance(instance) : new VulkanInstance(instance);
}
#endif

WAYLIB_SERVER_END_NAMESPACE
