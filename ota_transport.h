#pragma once

#include <QByteArray>

// Giao dien gui khung tho. SerialManager (giao tiep that qua QSerialPort) se trien khai lop nay.
// Chieu nhan khung khong di qua interface nay: lop trien khai tu phat signal rieng cua no
// (vi du frameReceived(QByteArray)), va noi khoi tao app se noi signal do voi
// OtaController::onFrameReceived bang connect() thong thuong (queued neu khac thread).
class IOtaTransport
{
public:
    virtual ~IOtaTransport() = default;
    virtual void sendFrame(const QByteArray &frame) = 0;
};