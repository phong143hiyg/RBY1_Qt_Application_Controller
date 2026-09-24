/****************************************************************************
** Meta object code from reading C++ file 'RobotClient.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/network/RobotClient.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'RobotClient.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11RobotClientE_t {};
} // unnamed namespace

template <> constexpr inline auto RobotClient::qt_create_metaobjectdata<qt_meta_tag_ZN11RobotClientE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "RobotClient",
        "robotConnected",
        "",
        "robotDisconnected",
        "responseReceived",
        "requestId",
        "operationName",
        "QJsonObject",
        "response",
        "requestTimedOut",
        "clientError",
        "message"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'robotConnected'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'robotDisconnected'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'responseReceived'
        QtMocHelpers::SignalData<void(quint64, const QString &, const QJsonObject &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 5 }, { QMetaType::QString, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Signal 'requestTimedOut'
        QtMocHelpers::SignalData<void(quint64, const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'clientError'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<RobotClient, qt_meta_tag_ZN11RobotClientE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject RobotClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RobotClientE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RobotClientE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11RobotClientE_t>.metaTypes,
    nullptr
} };

void RobotClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RobotClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->robotConnected(); break;
        case 1: _t->robotDisconnected(); break;
        case 2: _t->responseReceived((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[3]))); break;
        case 3: _t->requestTimedOut((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->clientError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (RobotClient::*)()>(_a, &RobotClient::robotConnected, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (RobotClient::*)()>(_a, &RobotClient::robotDisconnected, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (RobotClient::*)(quint64 , const QString & , const QJsonObject & )>(_a, &RobotClient::responseReceived, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (RobotClient::*)(quint64 , const QString & )>(_a, &RobotClient::requestTimedOut, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (RobotClient::*)(const QString & )>(_a, &RobotClient::clientError, 4))
            return;
    }
}

const QMetaObject *RobotClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RobotClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RobotClientE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int RobotClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void RobotClient::robotConnected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void RobotClient::robotDisconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void RobotClient::responseReceived(quint64 _t1, const QString & _t2, const QJsonObject & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2, _t3);
}

// SIGNAL 3
void RobotClient::requestTimedOut(quint64 _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void RobotClient::clientError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}
QT_WARNING_POP
