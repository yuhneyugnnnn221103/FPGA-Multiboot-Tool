#pragma once

#include <QObject>
#include <QByteArray>
#include <QSerialPort>
#include <QQueue>
#include <QTimer>

#include "ota_frame.h"
#include "ota_transport.h"

// Giao tiep RS485 qua QSerialPort. Du kien chay tren QThread rieng (moveToThread),
// tach khoi UI thread, dung chung mo hinh 2-thread nhu cac project Qt khac cua du an.
// QSerialPort duoc tao trong openPort() (khong phai constructor) de dam bao no thuoc
// dung thread noi SerialManager dang chay tai thoi diem goi, ke ca khi instance
// duoc tao o main thread roi moveToThread() sau do.
// RS485 song cong, day rieng TX/RX -> khong can dieu khien chan DE/RE qua RTS.
class SerialManager : public QObject, public IOtaTransport
{
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager() override;

public slots:
    bool openPort(const QString &portName, qint32 baudRate);
    void closePort();
    void setInterFrameDelayMs(int ms); // khoang nghi toi thieu giua 2 lan ghi cong lien tiep
    void sendFrame(const QByteArray &frame) override; // IOtaTransport

signals:
    void frameReceived(const QByteArray &frame);
    void frameSent(const QByteArray &frame); // phat ngay khi ghi xuong cong, de log debug
    void errorOccurred(const QString &message);
    void portOpened(bool ok);

private slots:
    void onReadyRead();
    void onPortError(QSerialPort::SerialPortError error);
    void pumpQueue();

private:
    void tryExtractFrames();

    QSerialPort *m_port = nullptr;
    QByteArray m_rxBuffer;

    // Hang doi gui: moi khung (dieu khien lan hoi trang thai) deu di qua day de
    // dam bao co khoang nghi toi thieu giua 2 lan ghi cong lien tiep tren bus RS485.
    QQueue<QByteArray> m_txQueue;
    QTimer m_txTimer;
    int m_interFrameDelayMs = 20;
};