// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "wqmldynamiccreator_p.h"

#include <QJSValue>
#include <private/qqmlcomponent_p.h>

WAYLIB_SERVER_BEGIN_NAMESPACE

WAbstractCreatorComponent::WAbstractCreatorComponent(QObject *parent)
    : QObject(parent)
{

}

void WAbstractCreatorComponent::remove(QSharedPointer<WQmlCreatorDelegateData>)
{

}

void WAbstractCreatorComponent::remove(QSharedPointer<WQmlCreatorData>)
{

}

QList<QSharedPointer<WQmlCreatorDelegateData>> WAbstractCreatorComponent::datas() const
{
    return {};
}

WQmlCreator *WAbstractCreatorComponent::creator() const
{
    return m_creator;
}

void WAbstractCreatorComponent::setCreator(WQmlCreator *newCreator)
{
    if (m_creator == newCreator)
        return;
    auto oldCreator = m_creator;
    m_creator = newCreator;
    creatorChange(oldCreator, newCreator);

    Q_EMIT creatorChanged();
}

void WAbstractCreatorComponent::creatorChange(WQmlCreator *oldCreator, WQmlCreator *newCreator)
{
    if (oldCreator)
        oldCreator->removeDelegate(this);

    if (newCreator)
        newCreator->addDelegate(this);
}

void WAbstractCreatorComponent::notifyCreatorObjectAdded(WQmlCreator *creator, QObject *object,
                                                         const QVariantMap &initialProperties)
{
    Q_EMIT creator->objectAdded(this, object, initialProperties);
}

void WAbstractCreatorComponent::notifyCreatorObjectRemoved(WQmlCreator *creator, QObject *object,
                                                           const QVariantMap &initialProperties)
{
    Q_EMIT creator->objectRemoved(this, object, initialProperties);
}

WQmlCreatorDataWatcher::WQmlCreatorDataWatcher(QObject *parent)
    : WAbstractCreatorComponent(parent)
{

}

QSharedPointer<WQmlCreatorDelegateData> WQmlCreatorDataWatcher::add(QSharedPointer<WQmlCreatorData> data)
{
    Q_EMIT added(data->owner, data->properties);
    return nullptr;
}

void WQmlCreatorDataWatcher::remove(QSharedPointer<WQmlCreatorData> data)
{
    Q_EMIT removed(data->owner, data->properties);
}

WQmlCreatorComponent::WQmlCreatorComponent(QObject *parent)
    : WAbstractCreatorComponent(parent)
{

}

WQmlCreatorComponent::~WQmlCreatorComponent()
{
    if (m_creator)
        m_creator->removeDelegate(this);

    clear();
}

bool WQmlCreatorComponent::checkByChooser(const QVariantMap &properties) const
{
    if (m_chooserRole.isEmpty())
        return true;
    return properties[m_chooserRole] == m_chooserRoleValue;
}

QQmlComponent *WQmlCreatorComponent::delegate() const
{
    return m_delegate;
}

void WQmlCreatorComponent::setDelegate(QQmlComponent *component)
{
    if (m_delegate) {
        qmlEngine(this)->throwError(QJSValue::GenericError, "Property 'delegate' can only be assigned once.");
        return;
    }

    m_delegate = component;
}

QSharedPointer<WQmlCreatorDelegateData> WQmlCreatorComponent::add(QSharedPointer<WQmlCreatorData> data)
{
    if (!checkByChooser(data->properties))
        return nullptr;

    QSharedPointer<WQmlCreatorDelegateData> d(new WQmlCreatorDelegateData());
    d->data = data;
    m_datas << d;
    create(d);

    return d;
}

void WQmlCreatorComponent::remove(QSharedPointer<WQmlCreatorData> data)
{
    for (const auto &d : std::as_const(data->delegateDatas)) {
        if (d.first != this)
            continue;
        if (d.second)
            remove(d.second.toStrongRef());
    }
}

void WQmlCreatorComponent::destroy(QSharedPointer<WQmlCreatorDelegateData> data)
{
    if (data->object) {
        auto obj = data->object;
        obj.clear();
        const QVariantMap p = data->data.lock()->properties;
        Q_EMIT objectRemoved(obj, p);
        notifyCreatorObjectRemoved(m_creator, obj, p);

        if (obj)
            obj->deleteLater();
    }
}

void WQmlCreatorComponent::remove(QSharedPointer<WQmlCreatorDelegateData> data)
{
    bool ok = m_datas.removeOne(data);
    Q_ASSERT(ok);
    destroy(data);
}

void WQmlCreatorComponent::clear()
{
    for (auto d : std::as_const(m_datas)) {
        d->data.lock()->delegateDatas.removeOne({this, d});
        destroy(d);
    }

    m_datas.clear();
}

void WQmlCreatorComponent::reset()
{
    clear();

    if (m_creator)
        m_creator->createAll(this);
}

void WQmlCreatorComponent::create(QSharedPointer<WQmlCreatorDelegateData> data)
{
    auto parent = m_parent ? m_parent : QObject::parent();

    if (data->object)
        destroy(data);

    Q_ASSERT(data->data);
    Q_ASSERT(m_delegate);

    auto d = QQmlComponentPrivate::get(m_delegate);
    if (d->state.isCompletePending()) {
        QMetaObject::invokeMethod(this, "create", Qt::QueuedConnection, data, parent, data->data.lock()->properties);
    } else {
        create(data, parent, data->data.lock()->properties);
    }
}

void WQmlCreatorComponent::create(QSharedPointer<WQmlCreatorDelegateData> data, QObject *parent, const QVariantMap &initialProperties)
{
    auto d = QQmlComponentPrivate::get(m_delegate);
    Q_ASSERT(!d->state.isCompletePending());
    data->object = d->createWithProperties(parent, initialProperties, qmlContext(this));

    if (data->object) {
        Q_EMIT objectAdded(data->object, initialProperties);
        notifyCreatorObjectAdded(m_creator, data->object, initialProperties);
    }
}

QObject *WQmlCreatorComponent::parent() const
{
    return m_parent;
}

void WQmlCreatorComponent::setParent(QObject *newParent)
{
    if (m_parent == newParent)
        return;
    m_parent = newParent;
    Q_EMIT parentChanged();
}

QList<QSharedPointer<WQmlCreatorDelegateData> > WQmlCreatorComponent::datas() const
{
    return m_datas;
}

QString WQmlCreatorComponent::chooserRole() const
{
    return m_chooserRole;
}

void WQmlCreatorComponent::setChooserRole(const QString &newChooserRole)
{
    if (m_chooserRole == newChooserRole)
        return;
    m_chooserRole = newChooserRole;
    reset();

    Q_EMIT chooserRoleChanged();
}

QVariant WQmlCreatorComponent::chooserRoleValue() const
{
    return m_chooserRoleValue;
}

void WQmlCreatorComponent::setChooserRoleValue(const QVariant &newChooserRoleValue)
{
    if (m_chooserRoleValue == newChooserRoleValue)
        return;
    m_chooserRoleValue = newChooserRoleValue;
    reset();

    Q_EMIT chooserRoleValueChanged();
}

WQmlCreator::WQmlCreator(QObject *parent)
    : QObject{parent}
{

}

WQmlCreator::~WQmlCreator()
{
    clear(false);

    for (auto delegate : std::as_const(m_delegates)) {
        Q_ASSERT(delegate->creator() == this);
        delegate->setCreator(nullptr);
    }
}

QList<WAbstractCreatorComponent*> WQmlCreator::delegates() const
{
    return m_delegates;
}

int WQmlCreator::count() const
{
    return m_datas.size();
}

void WQmlCreator::add(const QVariantMap &initialProperties)
{
    add(nullptr, initialProperties);
}

void WQmlCreator::add(QObject *owner, const QVariantMap &initialProperties)
{
    QSharedPointer<WQmlCreatorData> data(new WQmlCreatorData());
    data->owner = owner;
    data->properties = initialProperties;

    for (auto delegate : m_delegates) {
        if (auto d = delegate->add(data.toWeakRef()))
            data->delegateDatas.append({delegate, d});
    }

    m_datas << data;

    if (owner) {
        connect(owner, &QObject::destroyed, this, [this] {
            bool ok = removeByOwner(sender());
            Q_ASSERT(ok);
        });
    }

    Q_EMIT countChanged();
}

bool WQmlCreator::removeIf(QJSValue function)
{
    return remove(indexOf(function));
}

bool WQmlCreator::removeByOwner(QObject *owner)
{
    return remove(indexOfOwner(owner));
}

void WQmlCreator::clear(bool notify)
{
    if (m_datas.isEmpty())
        return;

    for (auto data : std::as_const(m_datas))
        destroy(data);

    m_datas.clear();

    if (notify)
        Q_EMIT countChanged();
}

QObject *WQmlCreator::get(int index) const
{
    if (index < 0 || index >= m_datas.size())
        return nullptr;

    auto data = m_datas.at(index);
    if (data->delegateDatas.isEmpty())
        return nullptr;
    return data->delegateDatas.first().second.lock()->object.get();
}

QObject *WQmlCreator::get(WAbstractCreatorComponent *delegate, int index) const
{
    if (index < 0 || index >= m_datas.size())
        return nullptr;

    auto data = m_datas.at(index);
    for (const auto &d : std::as_const(data->delegateDatas)) {
        if (d.first != delegate)
            continue;
        return d.second ? d.second.lock()->object.get() : nullptr;
    }

    return nullptr;
}

QObject *WQmlCreator::getIf(QJSValue function) const
{
    for (auto delegate : m_delegates) {
        auto obj = getIf(delegate, function);
        if (obj)
            return obj;
    }
    return nullptr;
}

QObject *WQmlCreator::getIf(WAbstractCreatorComponent *delegate, QJSValue function) const
{
    return get(delegate, indexOf(function));
}

QObject *WQmlCreator::getByOwner(QObject *owner) const
{
    for (auto delegate : m_delegates) {
        auto obj = getByOwner(delegate, owner);
        if (obj)
            return obj;
    }
    return nullptr;
}

QObject *WQmlCreator::getByOwner(WAbstractCreatorComponent *delegate, QObject *owner) const
{
    return get(delegate, indexOfOwner(owner));
}

void WQmlCreator::destroy(QSharedPointer<WQmlCreatorData> data)
{
    if (data->owner)
        data->owner->disconnect(this);

    for (auto delegate : m_delegates)
        delegate->remove(data);
}

bool WQmlCreator::remove(int index)
{
    if (index < 0 || index >= m_datas.size())
        return false;
    auto data = m_datas.takeAt(index);
    destroy(data);

    Q_EMIT countChanged();

    return true;
}

int WQmlCreator::indexOfOwner(QObject *owner) const
{
    for (int i = 0; i < m_datas.size(); ++i) {
        if (m_datas.at(i)->owner == owner)
            return i;
    }

    return -1;
}

int WQmlCreator::indexOf(QJSValue function) const
{
    auto engine = qmlEngine(this);
    Q_ASSERT(engine);

    for (int i = 0; i < m_datas.size(); ++i) {
        if (function.call({engine->toScriptValue(m_datas.at(i)->properties)}).toBool())
            return i;
    }

    return -1;
}

void WQmlCreator::addDelegate(WAbstractCreatorComponent *delegate)
{
    Q_ASSERT(!m_delegates.contains(delegate));
    m_delegates.append(delegate);
    createAll(delegate);
}

void WQmlCreator::createAll(WAbstractCreatorComponent *delegate)
{
    for (const auto &data : std::as_const(m_datas)) {
        if (auto d = delegate->add(data))
            data->delegateDatas.append({delegate, d});
    }
}

void WQmlCreator::removeDelegate(WAbstractCreatorComponent *delegate)
{
    bool ok = m_delegates.removeOne(delegate);
    Q_ASSERT(ok);

    for (auto d : delegate->datas()) {
        bool ok = d->data.lock()->delegateDatas.removeOne({delegate, d});
        Q_ASSERT(ok);
    }
}

WAYLIB_SERVER_END_NAMESPACE