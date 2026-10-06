#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QTime>
#include <QMap>
#include <QColor>
#include <QPalette>

namespace {
// Mau nen theo trang thai, dung chung cho bang node va banner tong quan.
const QColor kColorOk{200, 230, 201};      // xanh - on
const QColor kColorWarn{255, 236, 179};    // vang - online nhung co canh bao
const QColor kColorOffline{255, 205, 210}; // do - khong phan hoi
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    buildUi();

    m_controller = new OtaController(this);
    m_serial = new SerialManager(); // khong co parent - chuyen sang thread rieng ben duoi
    m_serial->moveToThread(&m_serialThread);
    m_serialThread.start();
    m_controller->setTransport(m_serial);

    connect(m_serial, &SerialManager::frameReceived, m_controller, &OtaController::onFrameReceived);
    connect(m_serial, &SerialManager::errorOccurred, this, &MainWindow::onSerialError);
    connect(m_serial, &SerialManager::portOpened, this, &MainWindow::onPortOpened);

    connect(m_controller, &OtaController::nodeStatusChanged, this, &MainWindow::onNodeStatusChanged);
    connect(m_controller, &OtaController::logMessage, this, &MainWindow::onLogMessage);
    connect(m_controller, &OtaController::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_controller, &OtaController::countdownTick, this, &MainWindow::onCountdownTick);
    connect(m_controller, &OtaController::stepChanged, this, &MainWindow::onStepChanged);
    connect(m_controller, &OtaController::allFinished, this, &MainWindow::onAllFinished);
    connect(m_controller, &OtaController::manualQueryResult, this, &MainWindow::onManualQueryResult);

    connect(m_interFrameDelayMs, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        QMetaObject::invokeMethod(m_serial, &SerialManager::setInterFrameDelayMs, Qt::QueuedConnection, v);
    });
    QMetaObject::invokeMethod(m_serial, &SerialManager::setInterFrameDelayMs,
                              Qt::QueuedConnection, m_interFrameDelayMs->value());

    onRefreshPorts();
    onStepChanged(OtaController::Step::Idle);
}

MainWindow::~MainWindow()
{
    m_serialThread.quit();
    m_serialThread.wait();
    delete m_serial;
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("OTA FPGA qua RS485"));

    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    // ===== Khu vuc dung chung phia tren (ca 2 tab deu can) =====

    auto *portGroup = new QGroupBox(tr("Kết nối RS485"));
    auto *portLayout = new QHBoxLayout(portGroup);
    m_portCombo = new QComboBox();
    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200", "230400"});
    m_baudCombo->setCurrentText("115200");
    m_btnRefreshPorts = new QPushButton(tr("Làm mới"));
    m_btnOpenPort = new QPushButton(tr("Mở cổng"));
    m_interFrameDelayMs = new QSpinBox();
    m_interFrameDelayMs->setRange(0, 1000);
    m_interFrameDelayMs->setValue(20);
    m_interFrameDelayMs->setSuffix(tr(" ms"));
    portLayout->addWidget(new QLabel(tr("Cổng:")));
    portLayout->addWidget(m_portCombo);
    portLayout->addWidget(m_btnRefreshPorts);
    portLayout->addWidget(new QLabel(tr("Baudrate:")));
    portLayout->addWidget(m_baudCombo);
    portLayout->addWidget(m_btnOpenPort);
    portLayout->addWidget(new QLabel(tr("Delay giữa khung:")));
    portLayout->addWidget(m_interFrameDelayMs);
    portLayout->addStretch();

    auto *addrGroup = new QGroupBox(tr("Chế độ & địa chỉ node"));
    auto *addrLayout = new QVBoxLayout(addrGroup);
    auto *modeRow = new QHBoxLayout();
    m_radioBroadcast = new QRadioButton(tr("Broadcast (nhiều motherboard/TRB)"));
    m_radioUnicast = new QRadioButton(tr("Unicast (1 địa chỉ)"));
    m_radioBroadcast->setChecked(true);
    modeRow->addWidget(m_radioBroadcast);
    modeRow->addWidget(m_radioUnicast);
    modeRow->addStretch();
    addrLayout->addLayout(modeRow);

    m_addrStack = new QStackedWidget();

    auto *rangePage = new QWidget();
    auto *rangePageLayout = new QVBoxLayout(rangePage);
    m_rangeTable = new QTableWidget(0, 3);
    m_rangeTable->setHorizontalHeaderLabels({tr("Motherboard"), tr("TRB từ"), tr("TRB đến")});
    m_rangeTable->horizontalHeader()->setStretchLastSection(true);
    m_rangeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rangeTable->setMaximumHeight(120);
    auto *rangeBtnRow = new QHBoxLayout();
    m_btnAddRange = new QPushButton(tr("+ Thêm dải"));
    m_btnRemoveRange = new QPushButton(tr("- Xóa dòng"));
    rangeBtnRow->addWidget(m_btnAddRange);
    rangeBtnRow->addWidget(m_btnRemoveRange);
    rangeBtnRow->addStretch();
    rangePageLayout->addWidget(m_rangeTable);
    rangePageLayout->addLayout(rangeBtnRow);
    m_addrStack->addWidget(rangePage);

    auto *unicastPage = new QWidget();
    auto *unicastLayout = new QHBoxLayout(unicastPage);
    m_unicastMb = new QSpinBox(); m_unicastMb->setRange(0, 255);
    m_unicastMb->setValue(10);
    m_unicastTrb = new QSpinBox(); m_unicastTrb->setRange(1, 254);
    m_unicastTrb->setValue(1);
    unicastLayout->addWidget(new QLabel(tr("Motherboard:")));
    unicastLayout->addWidget(m_unicastMb);
    unicastLayout->addWidget(new QLabel(tr("TRB:")));
    unicastLayout->addWidget(m_unicastTrb);
    unicastLayout->addStretch();
    m_addrStack->addWidget(unicastPage);

    addrLayout->addWidget(m_addrStack);

    mainLayout->addWidget(portGroup);
    mainLayout->addWidget(addrGroup);

    // ===== Tab "Giam sat trang thai" =====
    auto *monitorTab = new QWidget();
    auto *monitorLayout = new QVBoxLayout(monitorTab);

    auto *monitorBtnRow = new QHBoxLayout();
    m_btnConnect = new QPushButton(tr("Kết nối"));
    m_btnCheckAll = new QPushButton(tr("Hỏi tất cả 1 lần"));
    monitorBtnRow->addWidget(m_btnConnect);
    monitorBtnRow->addWidget(m_btnCheckAll);
    monitorBtnRow->addStretch();
    monitorLayout->addLayout(monitorBtnRow);

    auto *manualGroup = new QGroupBox(tr("Hỏi lỗi thủ công (0x88)"));
    auto *manualLayout = new QHBoxLayout(manualGroup);
    m_manualMb = new QSpinBox(); m_manualMb->setRange(0, 255);
    m_manualMb->setValue(10);
    m_manualTrb = new QSpinBox(); m_manualTrb->setRange(1, 254);
    m_manualTrb->setValue(1);
    m_btnManualQuery = new QPushButton(tr("Hỏi lỗi"));
    manualLayout->addWidget(new QLabel(tr("Motherboard:")));
    manualLayout->addWidget(m_manualMb);
    manualLayout->addWidget(new QLabel(tr("TRB:")));
    manualLayout->addWidget(m_manualTrb);
    manualLayout->addWidget(m_btnManualQuery);
    manualLayout->addStretch();
    monitorLayout->addWidget(manualGroup);
    monitorLayout->addStretch();

    // ===== Tab "Nap image & Boot" =====
    auto *loadTab = new QWidget();
    auto *loadLayout = new QVBoxLayout(loadTab);

    auto *fileGroup = new QGroupBox(tr("1. File image (.bin)"));
    auto *fileLayout = new QHBoxLayout(fileGroup);
    m_btnChooseFile = new QPushButton(tr("Chọn file..."));
    m_fileLabel = new QLabel(tr("(chưa chọn file)"));
    fileLayout->addWidget(m_btnChooseFile);
    fileLayout->addWidget(m_fileLabel, 1);
    loadLayout->addWidget(fileGroup);

    auto *paramGroup = new QGroupBox(tr("2. Tham số"));
    auto *paramLayout = new QHBoxLayout(paramGroup);
    m_eraseDelayMin = new QSpinBox(); m_eraseDelayMin->setRange(1, 30);
    m_eraseDelayMin->setValue(7); m_eraseDelayMin->setSuffix(tr(" phút"));
    m_rebootDelaySec = new QSpinBox(); m_rebootDelaySec->setRange(1, 120);
    m_rebootDelaySec->setValue(15); m_rebootDelaySec->setSuffix(tr(" giây"));
    m_maxRetry = new QSpinBox(); m_maxRetry->setRange(0, 10); m_maxRetry->setValue(3);
    paramLayout->addWidget(new QLabel(tr("Chờ sau xóa Flash:")));
    paramLayout->addWidget(m_eraseDelayMin);
    paramLayout->addWidget(new QLabel(tr("Chờ sau Boot:")));
    paramLayout->addWidget(m_rebootDelaySec);
    paramLayout->addWidget(new QLabel(tr("Số lần thử lại:")));
    paramLayout->addWidget(m_maxRetry);
    m_chkDummyFirstPacket = new QCheckBox(tr("Gửi gói ID 0 (256 byte 0xFF) trước khi nạp"));
    paramLayout->addWidget(m_chkDummyFirstPacket);
    paramLayout->addStretch();
    loadLayout->addWidget(paramGroup);

    auto *stepsGroup = new QGroupBox(tr("3. Các bước"));
    auto *stepsLayout = new QHBoxLayout(stepsGroup);
    m_btnErase = new QPushButton(tr("Xóa Flash"));
    m_btnLoad = new QPushButton(tr("Nạp code"));
    m_btnBoot = new QPushButton(tr("Yêu cầu Boot"));
    m_btnCheckLoad = new QPushButton(tr("Kiểm tra nạp"));
    stepsLayout->addWidget(m_btnErase);
    stepsLayout->addWidget(m_btnLoad);
    stepsLayout->addWidget(m_btnBoot);
    stepsLayout->addWidget(m_btnCheckLoad);
    stepsLayout->addStretch();
    loadLayout->addWidget(stepsGroup);
    loadLayout->addStretch();

    m_tabs = new QTabWidget();
    m_tabs->addTab(monitorTab, tr("Giám sát trạng thái"));
    m_tabs->addTab(loadTab, tr("Nạp image && Boot"));
    mainLayout->addWidget(m_tabs);

    // ===== Thanh trang thai dung chung (luon hien du dang o tab nao) =====
    auto *statusRow = new QHBoxLayout();
    m_stepLabel = new QLabel(tr("Trạng thái: Idle"));
    m_countdownLabel = new QLabel(tr("--:--"));
    m_btnCancelWait = new QPushButton(tr("Bỏ qua chờ"));
    m_btnStop = new QPushButton(tr("Dừng"));
    statusRow->addWidget(m_stepLabel);
    statusRow->addWidget(m_countdownLabel);
    statusRow->addWidget(m_btnCancelWait);
    statusRow->addWidget(m_btnStop);
    statusRow->addStretch();
    mainLayout->addLayout(statusRow);

    m_progressBar = new QProgressBar();
    mainLayout->addWidget(m_progressBar);

    // ===== Banner tong quan + bang trang thai node + log, dung chung =====
    m_summaryLabel = new QLabel(tr("Chưa có dữ liệu"));
    m_summaryLabel->setAutoFillBackground(true);
    m_summaryLabel->setStyleSheet(QStringLiteral("font-weight:bold; padding:6px;"));
    mainLayout->addWidget(m_summaryLabel);

    m_nodeTable = new QTableWidget(0, 7);
    m_nodeTable->setHorizontalHeaderLabels({tr("Địa chỉ (mb:trb)"), tr("Online"), tr("Lỗi nạp"),
                                            tr("CODE"), tr("STAT"), tr("BOOTSTS"), tr("WBSTAR")});
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeTable->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(m_nodeTable, 2);

    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_logView->setFont(mono);
    mainLayout->addWidget(m_logView, 1);

    setCentralWidget(central);
    resize(900, 860);

    connect(m_btnRefreshPorts, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(m_btnOpenPort, &QPushButton::clicked, this, &MainWindow::onOpenPortClicked);
    connect(m_radioUnicast, &QRadioButton::toggled, this, &MainWindow::onModeToggled);
    connect(m_btnAddRange, &QPushButton::clicked, this, &MainWindow::onAddRangeRow);
    connect(m_btnRemoveRange, &QPushButton::clicked, this, &MainWindow::onRemoveRangeRow);

    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_btnCheckAll, &QPushButton::clicked, this, &MainWindow::onCheckLoadClicked);
    connect(m_btnManualQuery, &QPushButton::clicked, this, &MainWindow::onManualQueryClicked);

    connect(m_btnChooseFile, &QPushButton::clicked, this, &MainWindow::onChooseFileClicked);
    connect(m_btnErase, &QPushButton::clicked, this, &MainWindow::onEraseClicked);
    connect(m_btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadClicked);
    connect(m_btnBoot, &QPushButton::clicked, this, &MainWindow::onBootClicked);
    connect(m_btnCheckLoad, &QPushButton::clicked, this, &MainWindow::onCheckLoadClicked);

    connect(m_btnCancelWait, &QPushButton::clicked, this, &MainWindow::onCancelWaitClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);

    onAddRangeRow(); // 1 dong mac dinh de bang khong trong khi khoi dong
}

void MainWindow::onRefreshPorts()
{
    m_portCombo->clear();
    for (const auto &info : QSerialPortInfo::availablePorts())
        m_portCombo->addItem(info.portName());
}

void MainWindow::onOpenPortClicked()
{
    if (m_portOpen) {
        QMetaObject::invokeMethod(m_serial, &SerialManager::closePort, Qt::QueuedConnection);
        m_portOpen = false;
        m_btnOpenPort->setText(tr("Mở cổng"));
        updateControlButtons();
        return;
    }
    const QString port = m_portCombo->currentText();
    const qint32 baud = m_baudCombo->currentText().toInt();
    if (port.isEmpty()) {
        QMessageBox::warning(this, tr("Thiếu cổng"), tr("Hãy chọn cổng COM trước."));
        return;
    }
    QMetaObject::invokeMethod(m_serial, &SerialManager::openPort, Qt::QueuedConnection, port, baud);
}

void MainWindow::onPortOpened(bool ok)
{
    m_portOpen = ok;
    m_btnOpenPort->setText(ok ? tr("Đóng cổng") : tr("Mở cổng"));
    if (!ok)
        QMessageBox::warning(this, tr("Lỗi"), tr("Không mở được cổng RS485."));
    updateControlButtons();
}

void MainWindow::onSerialError(const QString &msg)
{
    onLogMessage(tr("[Lỗi RS485] %1").arg(msg));
}

void MainWindow::onModeToggled(bool unicast)
{
    m_addrStack->setCurrentIndex(unicast ? 1 : 0);
}

void MainWindow::onChooseFileClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Chọn file firmware"),
                                                      QString(), tr("Binary files (*.bin)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Lỗi"), tr("Không mở được file: %1").arg(path));
        return;
    }
    m_firmwareData = f.readAll();
    m_fileLabel->setText(tr("%1 (%2 byte)").arg(QFileInfo(path).fileName()).arg(m_firmwareData.size()));
}

void MainWindow::onAddRangeRow()
{
    int row = m_rangeTable->rowCount();
    m_rangeTable->insertRow(row);
    auto *mbBox = new QSpinBox(); mbBox->setRange(0, 255); mbBox->setValue(10);
    auto *fromBox = new QSpinBox(); fromBox->setRange(1, 254); fromBox->setValue(1);
    auto *toBox = new QSpinBox(); toBox->setRange(1, 254); toBox->setValue(8);
    m_rangeTable->setCellWidget(row, 0, mbBox);
    m_rangeTable->setCellWidget(row, 1, fromBox);
    m_rangeTable->setCellWidget(row, 2, toBox);
}

void MainWindow::onRemoveRangeRow()
{
    int row = m_rangeTable->currentRow();
    if (row < 0)
        row = m_rangeTable->rowCount() - 1;
    if (row >= 0)
        m_rangeTable->removeRow(row);
}

QVector<Ota::Address> MainWindow::expandRangeTable() const
{
    QVector<Ota::Address> result;
    for (int row = 0; row < m_rangeTable->rowCount(); ++row) {
        auto *mbBox = qobject_cast<QSpinBox *>(m_rangeTable->cellWidget(row, 0));
        auto *fromBox = qobject_cast<QSpinBox *>(m_rangeTable->cellWidget(row, 1));
        auto *toBox = qobject_cast<QSpinBox *>(m_rangeTable->cellWidget(row, 2));
        if (!mbBox || !fromBox || !toBox)
            continue;
        const int mb = mbBox->value(), from = fromBox->value(), to = toBox->value();
        if (from > to)
            continue;
        for (int trb = from; trb <= to; ++trb)
            result.append(Ota::Address{uint8_t(mb), uint8_t(trb)});
    }
    return result;
}

void MainWindow::setupNodeTable(const QVector<Ota::Address> &addrs)
{
    m_nodeTable->setRowCount(0);
    for (const auto &a : addrs) {
        int row = m_nodeTable->rowCount();
        m_nodeTable->insertRow(row);
        m_nodeTable->setItem(row, 0, new QTableWidgetItem(Ota::addressToString(a)));
        for (int col = 1; col <= 6; ++col)
            m_nodeTable->setItem(row, col, new QTableWidgetItem(tr("-")));
    }
    updateSummaryLabel();
}

void MainWindow::applyTargetConfig()
{
    if (m_radioUnicast->isChecked()) {
        Ota::Address a{uint8_t(m_unicastMb->value()), uint8_t(m_unicastTrb->value())};
        m_controller->setTargetMode(OtaController::TargetMode::Unicast, a);
        m_controller->setNodes({a});
        setupNodeTable({a});
    } else {
        m_controller->setTargetMode(OtaController::TargetMode::Broadcast);
        const QVector<Ota::Address> addrs = expandRangeTable();
        m_controller->setNodes(addrs);
        setupNodeTable(addrs);
    }
}

void MainWindow::onConnectClicked()
{
    m_controller->setMaxRetry(m_maxRetry->value());
    applyTargetConfig();
    m_controller->startConnect();
}

void MainWindow::onEraseClicked()
{
    m_controller->setMaxRetry(m_maxRetry->value());
    m_controller->setEraseDelaySeconds(m_eraseDelayMin->value() * 60);
    m_controller->startErase();
}

void MainWindow::onLoadClicked()
{
    if (m_firmwareData.isEmpty()) {
        QMessageBox::warning(this, tr("Thiếu file"), tr("Hãy chọn file .bin trước."));
        return;
    }
    m_controller->setMaxRetry(m_maxRetry->value());
    m_controller->setSendDummyFirstPacket(m_chkDummyFirstPacket->isChecked());
    m_controller->setFirmware(m_firmwareData);
    m_controller->startLoad();
}

void MainWindow::onCancelWaitClicked()
{
    m_controller->cancelWait();
}

void MainWindow::onBootClicked()
{
    m_controller->setMaxRetry(m_maxRetry->value());
    m_controller->setRebootDelaySeconds(m_rebootDelaySec->value());
    m_controller->startBoot();
}

void MainWindow::onCheckLoadClicked()
{
    m_controller->checkAllNodes();
}

void MainWindow::onStopClicked()
{
    m_controller->stop();
}

void MainWindow::onNodeStatusChanged(int index, bool online, bool loadError)
{
    if (index < 0 || index >= m_nodeTable->rowCount())
        return;
    const auto &node = m_controller->nodes().at(index);
    m_nodeTable->item(index, 1)->setText(online ? tr("Có") : tr("Không"));
    m_nodeTable->item(index, 2)->setText(loadError ? tr("Có") : tr("-"));
    m_nodeTable->item(index, 3)->setText(QString::number(node.lastReply.code, 16).rightJustified(2, '0'));
    m_nodeTable->item(index, 4)->setText(QString::number(node.lastReply.stat, 16).rightJustified(8, '0'));
    m_nodeTable->item(index, 5)->setText(QString::number(node.lastReply.bootsts, 16).rightJustified(8, '0'));
    m_nodeTable->item(index, 6)->setText(QString::number(node.lastReply.wbstar, 16).rightJustified(8, '0'));

    QColor bg = !online ? kColorOffline
                : (loadError || node.lastReply.code != 0) ? kColorWarn
                                                          : kColorOk;
    for (int col = 0; col < m_nodeTable->columnCount(); ++col)
        m_nodeTable->item(index, col)->setBackground(bg);

    updateSummaryLabel();
}

void MainWindow::updateSummaryLabel()
{
    const auto &nodes = m_controller->nodes();
    int offline = 0, warn = 0, ok = 0;
    for (const auto &n : nodes) {
        if (!n.online) ++offline;
        else if (n.loadError || n.lastReply.code != 0) ++warn;
        else ++ok;
    }
    m_summaryLabel->setText(tr("Tổng %1 node — OK: %2, Cảnh báo: %3, Không phản hồi: %4")
                                .arg(nodes.size()).arg(ok).arg(warn).arg(offline));
    QColor bg = offline > 0 ? kColorOffline : (warn > 0 ? kColorWarn : (ok > 0 ? kColorOk : QColor(Qt::lightGray)));
    QPalette pal = m_summaryLabel->palette();
    pal.setColor(QPalette::Window, bg);
    m_summaryLabel->setPalette(pal);
}

void MainWindow::onLogMessage(const QString &msg)
{
    m_logView->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + " " + msg);
}

void MainWindow::onProgressChanged(int percent)
{
    m_progressBar->setValue(percent);
}

void MainWindow::onCountdownTick(int secondsLeft)
{
    m_countdownLabel->setText(QStringLiteral("%1:%2")
                                  .arg(secondsLeft / 60, 2, 10, QChar('0'))
                                  .arg(secondsLeft % 60, 2, 10, QChar('0')));
}

void MainWindow::onStepChanged(OtaController::Step step)
{
    static const QMap<OtaController::Step, QString> names = {
                                                              {OtaController::Step::Idle, tr("Idle")},
                                                              {OtaController::Step::Connecting, tr("Đang kết nối...")},
                                                              {OtaController::Step::ErasingFlash, tr("Đang xóa Flash...")},
                                                              {OtaController::Step::WaitingErase, tr("Đang chờ xóa Flash...")},
                                                              {OtaController::Step::LoadingCode, tr("Đang nạp code...")},
                                                              {OtaController::Step::RequestingBoot, tr("Đang yêu cầu boot...")},
                                                              {OtaController::Step::WaitingReboot, tr("Đang chờ reboot...")},
                                                              {OtaController::Step::VerifyingBoot, tr("Đang xác nhận boot...")},
                                                              {OtaController::Step::CheckingLoad, tr("Đang kiểm tra nạp...")},
                                                              {OtaController::Step::Done, tr("Hoàn tất")},
                                                              };
    m_stepLabel->setText(tr("Trạng thái: %1").arg(names.value(step)));

    m_lastStep = step;
    updateControlButtons();
}

void MainWindow::updateControlButtons()
{
    const bool busy = (m_lastStep != OtaController::Step::Idle && m_lastStep != OtaController::Step::Done);
    const bool ready = m_portOpen && !busy;
    m_btnConnect->setEnabled(ready);
    m_btnCheckAll->setEnabled(ready);
    m_btnErase->setEnabled(ready);
    m_btnLoad->setEnabled(ready);
    m_btnBoot->setEnabled(ready);
    m_btnCheckLoad->setEnabled(ready);
    m_btnCancelWait->setEnabled(m_lastStep == OtaController::Step::WaitingErase ||
                                m_lastStep == OtaController::Step::WaitingReboot);
    m_btnStop->setEnabled(busy);
    // hoi loi thu cong la cong cu chan doan doc lap, chi can cong da mo la dung duoc
    // (OtaController tu chan neu dang co thao tac tu dong khac cho phan hoi)
    m_btnManualQuery->setEnabled(m_portOpen);
}

void MainWindow::onManualQueryClicked()
{
    Ota::Address a{uint8_t(m_manualMb->value()), uint8_t(m_manualTrb->value())};
    m_controller->queryNodeStatus(a);
}

void MainWindow::onManualQueryResult(quint8 motherboard, quint8 trb, bool ok,
                                     quint8 code, quint32 stat, quint32 bootsts, quint32 wbstar)
{
    const QString addr = Ota::addressToString(Ota::Address{motherboard, trb});
    if (!ok) {
        onLogMessage(tr("Node %1: không phản hồi hoặc khung lỗi").arg(addr));
        return;
    }
    onLogMessage(tr("Node %1: CODE=0x%2 STAT=0x%3 BOOTSTS=0x%4 WBSTAR=0x%5")
                     .arg(addr)
                     .arg(code, 2, 16, QChar('0'))
                     .arg(stat, 8, 16, QChar('0'))
                     .arg(bootsts, 8, 16, QChar('0'))
                     .arg(wbstar, 8, 16, QChar('0')));
}

void MainWindow::onAllFinished(bool success)
{
    onLogMessage(success ? tr("Hoàn tất toàn bộ quy trình.") : tr("Quy trình kết thúc với lỗi."));
}