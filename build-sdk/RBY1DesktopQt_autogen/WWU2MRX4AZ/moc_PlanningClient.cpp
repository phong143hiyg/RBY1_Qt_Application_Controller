/****************************************************************************
** Meta object code from reading C++ file 'PlanningClient.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/planning/PlanningClient.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PlanningClient.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14PlanningClientE_t {};
} // unnamed namespace

template <> constexpr inline auto PlanningClient::qt_create_metaobjectdata<qt_meta_tag_ZN14PlanningClientE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "PlanningClient",
        "changed",
        "",
        "message",
        "text",
        "responseReceived",
        "id",
        "command",
        "QJsonObject",
        "envelope",
        "eventReceived",
        "requestTimedOut",
        "State",
        "Idle",
        "Planning",
        "PlanReady",
        "Failed",
        "Cancelling",
        "Cancelled",
        "Disconnected"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'changed'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'message'
        QtMocHelpers::SignalData<void(const QString &)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'responseReceived'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QJsonObject &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 }, { QMetaType::QString, 7 }, { 0x80000000 | 8, 9 },
        }}),
        // Signal 'eventReceived'
        QtMocHelpers::SignalData<void(const QJsonObject &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Signal 'requestTimedOut'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 }, { QMetaType::QString, 7 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'State'
        QtMocHelpers::EnumData<enum State>(12, 12, QMC::EnumIsScoped).add({
            {   13, State::Idle },
            {   14, State::Planning },
            {   15, State::PlanReady },
            {   16, State::Failed },
            {   17, State::Cancelling },
            {   18, State::Cancelled },
            {   19, State::Disconnected },
        }),
    };
    return QtMocHelpers::metaObjectData<PlanningClient, qt_meta_tag_ZN14PlanningClientE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject PlanningClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PlanningClientE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PlanningClientE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14PlanningClientE_t>.metaTypes,
    nullptr
} };

void PlanningClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PlanningClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->changed(); break;
        case 1: _t->message((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->responseReceived((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[3]))); break;
        case 3: _t->eventReceived((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 4: _t->requestTimedOut((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PlanningClient::*)()>(_a, &PlanningClient::changed, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlanningClient::*)(const QString & )>(_a, &PlanningClient::message, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlanningClient::*)(const QString & , const QString & , const QJsonObject & )>(_a, &PlanningClient::responseReceived, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlanningClient::*)(const QJsonObject & )>(_a, &PlanningClient::eventReceived, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlanningClient::*)(const QString & , const QString & )>(_a, &PlanningClient::requestTimedOut, 4))
            return;
    }
}

const QMetaObject *PlanningClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PlanningClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PlanningClientE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int PlanningClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void PlanningClient::changed()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void PlanningClient::message(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void PlanningClient::responseReceived(const QString & _t1, const QString & _t2, const QJsonObject & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2, _t3);
}

// SIGNAL 3
void PlanningClient::eventReceived(const QJsonObject & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void PlanningClient::requestTimedOut(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}
QT_WARNING_POP
