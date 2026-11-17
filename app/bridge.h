#ifndef MONITOR_BRIDGE_H
#define MONITOR_BRIDGE_H
#include <QObject>

class SampleBridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void dht(bool ok, int temperature, int humidity, int error, qint64 timestamp);
    void light(bool ok, double lux, int error, qint64 timestamp);
    void pir(int kind, bool high, int error, qint64 timestamp);
    void actuator(bool servo, bool ok, int error, qint64 timestamp);
};
#endif
