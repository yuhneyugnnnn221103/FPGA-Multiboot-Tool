#include "ota_controller.h"

OtaController::OtaController(QObject *parent) : QObject(parent)
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &OtaController::onTimeout);

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdownLeft <= 0) {
            m_countdownTimer.stop();
            emit countdownTick(0);
            if (m_countdownContinuation)
                m_countdownContinuation();
        } else {
            emit countdownTick(m_countdownLeft);
        }
    });
}

void OtaController::setTransport(IOtaTransport *transport) { m_transport = transport; }

void OtaController::setNodes(const QVector<Ota::Address> &addrs)
{
    m_nodes.clear();
    m_nodes.reserve(addrs.size());
    for (const auto &a : addrs)
        m_nodes.append(NodeInfo{a, false, false, {}});
}

void OtaController::setTargetMode(TargetMode mode, Ota::Address unicastAddr)
{
    m_targetMode = mode;
    m_unicastAddr = unicastAddr;
}

void OtaController::setEraseDelaySeconds(int seconds) { m_eraseDelaySec = seconds; }
void OtaController::setRebootDelaySeconds(int seconds) { m_rebootDelaySec = seconds; }
void OtaController::setMaxRetry(int retries) { m_maxRetry = qMax(0, retries); }

void OtaController::setFirmware(const QByteArray &binData)
{
    m_firmware = binData;
    m_packetIndex = 0;
    m_totalPackets = (m_firmware.size() + Ota::kDataChunkSize - 1) / Ota::kDataChunkSize
                     + (m_dummyFirstPacket ? 1 : 0);
}

void OtaController::setSendDummyFirstPacket(bool enable) { m_dummyFirstPacket = enable; }

Ota::Address OtaController::currentTarget() const
{
    return (m_targetMode == TargetMode::Unicast) ? m_unicastAddr : Ota::kBroadcastAddress;
}

void OtaController::sendAndArm(const QByteArray &frame, PendingAction action, int timeoutMs)
{
    m_pending = action;
    m_transport->sendFrame(frame);
    m_timeoutTimer.start(timeoutMs);
}

// ---------- Buoc 1: ket noi ----------
void OtaController::startConnect()
{
    m_step = Step::Connecting;
    emit stepChanged(m_step);
    m_curNode = 0;
    connectNextNode();
}

void OtaController::connectNextNode()
{
    if (m_curNode >= m_nodes.size()) {
        m_pending = PendingAction::None; // tranh khung den muon truy cap m_nodes[m_curNode] qua gioi han
        m_step = Step::Idle;
        emit stepChanged(m_step);
        return;
    }
    m_retryCount = 0;
    sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::ConnectQuery);
}

// ---------- Buoc 2: xoa flash + nap code (da gop kiem tra loi) ----------
void OtaController::startEraseAndLoad()
{
    setFirmware(m_firmware); // tinh lai so goi theo tuy chon goi ID 0
    m_step = Step::ErasingFlash;
    emit stepChanged(m_step);
    m_transport->sendFrame(Ota::buildEraseFlash(currentTarget()));
    emit logMessage(QStringLiteral("Da gui lenh xoa Flash"));

    m_step = Step::WaitingErase;
    emit stepChanged(m_step);
    m_countdownLeft = m_eraseDelaySec;
    emit countdownTick(m_countdownLeft);
    m_countdownContinuation = [this] { loadNextPacket(); };
    m_countdownTimer.start();
}

void OtaController::cancelWait()
{
    if (m_step == Step::WaitingErase || m_step == Step::WaitingReboot) {
        m_countdownTimer.stop();
        if (m_countdownContinuation)
            m_countdownContinuation();
    }
}

void OtaController::stop()
{
    m_timeoutTimer.stop();
    m_countdownTimer.stop();
    m_countdownContinuation = nullptr;
    m_pending = PendingAction::None;
    m_step = Step::Idle;
    emit stepChanged(m_step);
    emit logMessage(QStringLiteral("Da dung theo yeu cau nguoi dung"));
}

void OtaController::loadNextPacket()
{
    if (m_packetIndex >= m_totalPackets) {
        m_pending = PendingAction::None;
        m_step = Step::Idle;
        emit stepChanged(m_step);
        emit progressChanged(100);
        return;
    }
    m_step = Step::LoadingCode;
    emit stepChanged(m_step);

    if (m_dummyFirstPacket && m_packetIndex == 0) {
        m_curChunk = QByteArray(Ota::kDataChunkSize, char(0xFF));
    } else {
        int offset = (m_packetIndex - (m_dummyFirstPacket ? 1 : 0)) * Ota::kDataChunkSize;
        m_curChunk = m_firmware.mid(offset, Ota::kDataChunkSize);
    }
    m_transport->sendFrame(Ota::buildLoadCode(currentTarget(), quint16(m_packetIndex), m_curChunk));
    emit progressChanged(int(100.0 * m_packetIndex / qMax(1, m_totalPackets)));

    m_curNode = 0;
    loadCheckNextNode();
}

void OtaController::loadCheckNextNode()
{
    while (m_curNode < m_nodes.size() &&
           (!m_nodes[m_curNode].online || m_nodes[m_curNode].loadError))
        ++m_curNode;

    if (m_curNode >= m_nodes.size()) {
        ++m_packetIndex;
        loadNextPacket();
        return;
    }
    m_retryCount = 0;
    sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::LoadQuery);
}

void OtaController::resendCurrentPacket()
{
    NodeInfo &node = m_nodes[m_curNode];
    ++m_retryCount;
    if (m_retryCount > m_maxRetry) {
        node.loadError = true;
        emit nodeStatusChanged(m_curNode, node.online, node.loadError);
        emit logMessage(QStringLiteral("Node %1: loi nap goi %2 sau %3 lan thu")
                            .arg(Ota::addressToString(node.addr)).arg(m_packetIndex).arg(m_maxRetry));
        ++m_curNode;
        loadCheckNextNode();
        return;
    }
    // gui lai dung goi nay cho rieng node loi roi hoi lai trang thai de xac nhan
    m_transport->sendFrame(Ota::buildLoadCode(node.addr, quint16(m_packetIndex), m_curChunk));
    sendAndArm(Ota::buildStatusQuery(node.addr), PendingAction::LoadQuery);
}

// ---------- Buoc 3: boot ----------
void OtaController::startBoot()
{
    m_step = Step::RequestingBoot;
    emit stepChanged(m_step);
    m_transport->sendFrame(Ota::buildBootRequest(currentTarget()));
    emit logMessage(QStringLiteral("Da gui lenh yeu cau boot"));

    m_step = Step::WaitingReboot;
    emit stepChanged(m_step);
    m_countdownLeft = m_rebootDelaySec;
    emit countdownTick(m_countdownLeft);
    m_countdownContinuation = [this] { beginBootVerify(); };
    m_countdownTimer.start();
}

void OtaController::beginBootVerify()
{
    m_step = Step::VerifyingBoot;
    emit stepChanged(m_step);
    m_curNode = 0;
    m_bootHadError = false;
    verifyNextNode();
}

void OtaController::verifyNextNode()
{
    while (m_curNode < m_nodes.size() && !m_nodes[m_curNode].online)
        ++m_curNode;
    if (m_curNode >= m_nodes.size()) {
        m_pending = PendingAction::None;
        m_step = Step::Done;
        emit stepChanged(m_step);
        emit allFinished(!m_bootHadError);
        return;
    }
    m_retryCount = 0;
    sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::BootQuery);
}

// ---------- "Kiem tra nap" doc lap: hoi lai toan bo node hien tai ----------
void OtaController::checkAllNodes()
{
    if (m_pending != PendingAction::None) {
        emit logMessage(QStringLiteral("Dang co thao tac khac cho phan hoi, thu lai sau"));
        return;
    }
    m_step = Step::CheckingLoad;
    emit stepChanged(m_step);
    m_curNode = 0;
    checkAllNext();
}

void OtaController::checkAllNext()
{
    while (m_curNode < m_nodes.size() && !m_nodes[m_curNode].online)
        ++m_curNode;
    if (m_curNode >= m_nodes.size()) {
        m_pending = PendingAction::None;
        m_step = Step::Idle;
        emit stepChanged(m_step);
        return;
    }
    m_retryCount = 0;
    sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::CheckQuery);
}

// ---------- Hoi loi thu cong 1 node (nut rieng tren UI, doc lap voi luong tu dong) ----------
void OtaController::queryNodeStatus(Ota::Address addr)
{
    if (m_pending != PendingAction::None) {
        emit logMessage(QStringLiteral("Dang co thao tac khac cho phan hoi, thu lai sau"));
        return;
    }
    m_manualQueryAddr = addr;
    sendAndArm(Ota::buildStatusQuery(addr), PendingAction::ManualQuery);
}

// ---------- Xu ly phan hoi & timeout ----------
void OtaController::onFrameReceived(const QByteArray &frame)
{
    if (m_pending == PendingAction::None)
        return;

    m_timeoutTimer.stop();
    Ota::StatusReply reply;
    bool ok = Ota::parseStatusReply(frame, &reply);

    switch (m_pending) {
    case PendingAction::ConnectQuery: {
        NodeInfo &node = m_nodes[m_curNode];
        node.online = ok;
        if (ok) node.lastReply = reply;
        emit nodeStatusChanged(m_curNode, node.online, node.loadError);
        ++m_curNode;
        connectNextNode();
        break;
    }
    case PendingAction::LoadQuery: {
        if (!ok) { resendCurrentPacket(); break; }
        NodeInfo &node = m_nodes[m_curNode];
        node.lastReply = reply;
        if (reply.hasError(Ota::ErrUartFrame)) {
            resendCurrentPacket();
        } else {
            ++m_curNode;
            loadCheckNextNode();
        }
        break;
    }
    case PendingAction::BootQuery: {
        NodeInfo &node = m_nodes[m_curNode];
        if (ok) {
            node.lastReply = reply;
            if (reply.code != 0)
                m_bootHadError = true;
        } else {
            m_bootHadError = true;
        }
        emit nodeStatusChanged(m_curNode, node.online, node.loadError);
        ++m_curNode;
        verifyNextNode();
        break;
    }
    case PendingAction::CheckQuery: {
        NodeInfo &node = m_nodes[m_curNode];
        if (ok) {
            node.lastReply = reply;
            node.loadError = reply.hasError(Ota::ErrFlashWrite) || reply.hasError(Ota::ErrFlashErase);
        }
        emit nodeStatusChanged(m_curNode, node.online, node.loadError);
        ++m_curNode;
        checkAllNext();
        break;
    }
    case PendingAction::ManualQuery: {
        emit manualQueryResult(m_manualQueryAddr.motherboard, m_manualQueryAddr.trb, ok,
                               reply.code, reply.stat, reply.bootsts, reply.wbstar);
        m_pending = PendingAction::None;
        break;
    }
    default:
        break;
    }
}

void OtaController::onTimeout()
{
    switch (m_pending) {
    case PendingAction::ConnectQuery: {
        ++m_retryCount;
        NodeInfo &node = m_nodes[m_curNode];
        if (m_retryCount > m_maxRetry) {
            node.online = false;
            emit nodeStatusChanged(m_curNode, node.online, node.loadError);
            emit logMessage(QStringLiteral("Node %1: offline").arg(Ota::addressToString(node.addr)));
            ++m_curNode;
            connectNextNode();
        } else {
            sendAndArm(Ota::buildStatusQuery(node.addr), PendingAction::ConnectQuery);
        }
        break;
    }
    case PendingAction::LoadQuery:
        resendCurrentPacket();
        break;
    case PendingAction::BootQuery: {
        ++m_retryCount;
        if (m_retryCount > m_maxRetry) {
            m_bootHadError = true;
            emit logMessage(QStringLiteral("Node %1: khong phan hoi khi xac nhan boot")
                                .arg(Ota::addressToString(m_nodes[m_curNode].addr)));
            ++m_curNode;
            verifyNextNode();
        } else {
            sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::BootQuery);
        }
        break;
    }
    case PendingAction::CheckQuery: {
        ++m_retryCount;
        if (m_retryCount > m_maxRetry) {
            emit logMessage(QStringLiteral("Node %1: khong phan hoi khi kiem tra nap")
                                .arg(Ota::addressToString(m_nodes[m_curNode].addr)));
            ++m_curNode;
            checkAllNext();
        } else {
            sendAndArm(Ota::buildStatusQuery(m_nodes[m_curNode].addr), PendingAction::CheckQuery);
        }
        break;
    }
    case PendingAction::ManualQuery:
        emit manualQueryResult(m_manualQueryAddr.motherboard, m_manualQueryAddr.trb, false, 0, 0, 0, 0);
        m_pending = PendingAction::None;
        break;
    default:
        break;
    }
}