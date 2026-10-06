#include "serial_manager.h"

SerialManager::SerialManager(QObject *parent) : QObject(parent)
{
    m_txTimer.setSingleShot(true);
    connect(&m_txTimer, &QTimer::timeout, this, &SerialManager::pumpQueue);
}

SerialManager::~SerialManager()
{
    closePort();
}

bool SerialManager::openPort(const QString &portName, qint32 baudRate)
{
    closePort();

    m_port = new QSerialPort(this);
    m_port->setPortName(portName);
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_port, &QSerialPort::readyRead, this, &SerialManager::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialManager::onPortError);

    bool ok = m_port->open(QIODevice::ReadWrite);
    if (!ok) {
        emit errorOccurred(m_port->errorString());
        m_port->deleteLater();
        m_port = nullptr;
    }
    m_rxBuffer.clear();
    m_txQueue.clear();
    emit portOpened(ok);
    return ok;
}

void SerialManager::closePort()
{
    if (m_port) {
        m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
    }
    m_rxBuffer.clear();
    m_txQueue.clear();
    m_txTimer.stop();
}

void SerialManager::setInterFrameDelayMs(int ms)
{
    m_interFrameDelayMs = qMax(0, ms);
}

void SerialManager::sendFrame(const QByteArray &frame)
{
    // sendFrame() la mot loi goi ham ao thong thuong, co the den tu thread khac
    // (OtaController o main thread). Dua khung vao hang doi va xu ly hoan toan
    // tren thread cua SerialManager bang cach queue lai qua chinh no.
    QMetaObject::invokeMethod(this, [this, frame] {
        m_txQueue.enqueue(frame);
        pumpQueue();
    }, Qt::QueuedConnection);
}

void SerialManager::pumpQueue()
{
    if (m_txQueue.isEmpty() || m_txTimer.isActive())
        return;
    if (!m_port || !m_port->isOpen()) {
        emit errorOccurred(QStringLiteral("Cong RS485 chua mo"));
        m_txQueue.clear();
        return;
    }
    const QByteArray frame = m_txQueue.dequeue();
    m_port->write(frame);
    emit frameSent(frame);
    m_txTimer.start(m_interFrameDelayMs);
}

void SerialManager::onReadyRead()
{
    m_rxBuffer.append(m_port->readAll());
    tryExtractFrames();
}

void SerialManager::onPortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port)
        return;
    emit errorOccurred(m_port->errorString());
}

// Chi co ban tin tra loi 0x99 (25 byte co dinh) di theo chieu FPGA -> PC,
// nen chi can tim header roi cat dung 25 byte va kiem tra ender.
void SerialManager::tryExtractFrames()
{
    for (;;) {
        int start = -1;
        for (int i = 0; i + 1 < m_rxBuffer.size(); ++i) {
            if (uchar(m_rxBuffer[i]) == Ota::kHeaderByte1 && uchar(m_rxBuffer[i + 1]) == Ota::kHeaderByte2) {
                start = i;
                break;
            }
        }
        if (start < 0) {
            if (m_rxBuffer.size() > 1)
                m_rxBuffer.remove(0, m_rxBuffer.size() - 1);
            return;
        }
        if (start > 0)
            m_rxBuffer.remove(0, start);

        if (m_rxBuffer.size() < Ota::kReplyFrameSize)
            return;

        QByteArray frame = m_rxBuffer.left(Ota::kReplyFrameSize);
        if (uchar(frame[Ota::kReplyFrameSize - 2]) == Ota::kEnderByte1 &&
            uchar(frame[Ota::kReplyFrameSize - 1]) == Ota::kEnderByte2) {
            emit frameReceived(frame);
            m_rxBuffer.remove(0, Ota::kReplyFrameSize);
        } else {
            m_rxBuffer.remove(0, 1);
        }
    }
}