#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <functional>

#include "ota_frame.h"
#include "ota_transport.h"

class OtaController : public QObject
{
    Q_OBJECT
public:
    enum class Step {
        Idle, Connecting, ErasingFlash, WaitingErase, LoadingCode,
        RequestingBoot, WaitingReboot, VerifyingBoot, CheckingLoad, Done
    };
    Q_ENUM(Step)

    enum class TargetMode { Broadcast, Unicast };

    struct NodeInfo {
        Ota::Address addr;
        bool online = false;
        bool loadError = false;
        Ota::StatusReply lastReply;
    };

    explicit OtaController(QObject *parent = nullptr);

    void setTransport(IOtaTransport *transport);
    void setNodes(const QVector<Ota::Address> &addrs);
    void setTargetMode(TargetMode mode, Ota::Address unicastAddr = {});
    void setEraseDelaySeconds(int seconds);
    void setRebootDelaySeconds(int seconds);
    void setMaxRetry(int retries);
    void setFirmware(const QByteArray &binData);
    const QVector<NodeInfo> &nodes() const { return m_nodes; }

public slots:
    void startConnect();
    void startEraseAndLoad();
    void startBoot();
    void checkAllNodes();     // "Kiem tra nap": hoi lai toan bo node hien tai bang 0x88
    void cancelWait();        // bo qua dem nguoc dang cho (sau xoa flash HOAC sau boot)
    void stop();              // huy ngay, khong doi phan hoi/timeout cua node dang cho
    void queryNodeStatus(Ota::Address addr); // hoi loi thu cong 1 dia chi, doc lap voi luong tu dong
    void onFrameReceived(const QByteArray &frame);

signals:
    void nodeStatusChanged(int index, bool online, bool loadError);
    void logMessage(const QString &msg);
    void progressChanged(int percent);
    void countdownTick(int secondsLeft);
    void stepChanged(OtaController::Step step);
    void allFinished(bool success);
    void manualQueryResult(quint8 motherboard, quint8 trb, bool ok,
                           quint8 code, quint32 stat, quint32 bootsts, quint32 wbstar);

private:
    enum class PendingAction { None, ConnectQuery, LoadQuery, BootQuery, CheckQuery, ManualQuery };

    Ota::Address currentTarget() const; // Broadcast -> 0xFFFF, Unicast -> dia chi da chon

    void connectNextNode();
    void loadNextPacket();
    void loadCheckNextNode();
    void resendCurrentPacket();
    void beginBootVerify();
    void verifyNextNode();
    void checkAllNext();
    void sendAndArm(const QByteArray &frame, PendingAction action, int timeoutMs = 300);
    void onTimeout();

    IOtaTransport *m_transport = nullptr;
    QTimer m_timeoutTimer;
    QTimer m_countdownTimer;
    std::function<void()> m_countdownContinuation; // hanh dong tiep theo khi dem nguoc ve 0 (hoac bi bo qua)

    QVector<NodeInfo> m_nodes;
    int m_curNode = 0;
    int m_retryCount = 0;

    TargetMode m_targetMode = TargetMode::Broadcast;
    Ota::Address m_unicastAddr;

    QByteArray m_firmware;
    int m_packetIndex = 0;
    int m_totalPackets = 0;
    QByteArray m_curChunk;

    int m_eraseDelaySec = 420; // mac dinh 7 phut
    int m_rebootDelaySec = 15; // mac dinh 15s
    int m_countdownLeft = 0;
    int m_maxRetry = 3;

    PendingAction m_pending = PendingAction::None;
    Step m_step = Step::Idle;
    Ota::Address m_manualQueryAddr;
    bool m_bootHadError = false;
};