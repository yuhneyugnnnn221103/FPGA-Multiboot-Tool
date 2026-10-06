#pragma once

#include <QMainWindow>
#include <QThread>
#include <QByteArray>
#include <QVector>

#include "ota_controller.h"
#include "serial_manager.h"

class QCheckBox;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QTableWidget;
class QPlainTextEdit;
class QRadioButton;
class QStackedWidget;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshPorts();
    void onOpenPortClicked();
    void onPortOpened(bool ok);
    void onSerialError(const QString &msg);

    void onModeToggled(bool unicast);
    void onChooseFileClicked();

    void onAddRangeRow();
    void onRemoveRangeRow();

    void onConnectClicked();
    void onEraseLoadClicked();
    void onCancelWaitClicked();
    void onBootClicked();
    void onCheckLoadClicked();
    void onStopClicked();

    void onNodeStatusChanged(int index, bool online, bool loadError);
    void onLogMessage(const QString &msg);
    void onProgressChanged(int percent);
    void onCountdownTick(int secondsLeft);
    void onStepChanged(OtaController::Step step);
    void onAllFinished(bool success);

    void onManualQueryClicked();
    void onManualQueryResult(quint8 motherboard, quint8 trb, bool ok,
                             quint8 code, quint32 stat, quint32 bootsts, quint32 wbstar);

private:
    void buildUi();
    void applyTargetConfig(); // doc mode + dia chi tu UI, day xuong OtaController va dung de dung bang node
    QVector<Ota::Address> expandRangeTable() const;
    void setupNodeTable(const QVector<Ota::Address> &addrs);
    void updateControlButtons();
    void updateSummaryLabel(); // banner mau tong quan tren bang node

    // giao tiep
    OtaController *m_controller = nullptr;
    SerialManager *m_serial = nullptr;
    QThread m_serialThread;
    bool m_portOpen = false;
    QByteArray m_firmwareData;
    OtaController::Step m_lastStep = OtaController::Step::Idle;

    // widget - ket noi RS485 (dung chung cho ca 2 tab)
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_btnRefreshPorts = nullptr;
    QPushButton *m_btnOpenPort = nullptr;
    QSpinBox *m_interFrameDelayMs = nullptr;

    // widget - che do & dia chi (motherboard + TRB), dung chung cho ca 2 tab
    QRadioButton *m_radioBroadcast = nullptr;
    QRadioButton *m_radioUnicast = nullptr;
    QStackedWidget *m_addrStack = nullptr;
    QTableWidget *m_rangeTable = nullptr; // Broadcast: nhieu dong Motherboard | TRB tu | TRB den
    QPushButton *m_btnAddRange = nullptr;
    QPushButton *m_btnRemoveRange = nullptr;
    QSpinBox *m_unicastMb = nullptr;      // Unicast: 1 dia chi duy nhat
    QSpinBox *m_unicastTrb = nullptr;

    QTabWidget *m_tabs = nullptr;

    // tab "Giam sat trang thai"
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnCheckAll = nullptr; // "Hoi tat ca 1 lan" - alias UI cho checkAllNodes()
    QSpinBox *m_manualMb = nullptr;
    QSpinBox *m_manualTrb = nullptr;
    QPushButton *m_btnManualQuery = nullptr;

    // tab "Nap image & Boot"
    QPushButton *m_btnChooseFile = nullptr;
    QLabel *m_fileLabel = nullptr;
    QSpinBox *m_eraseDelayMin = nullptr;
    QSpinBox *m_rebootDelaySec = nullptr;
    QSpinBox *m_maxRetry = nullptr;
    QPushButton *m_btnEraseLoad = nullptr;
    QPushButton *m_btnBoot = nullptr;
    QPushButton *m_btnCheckLoad = nullptr;
    QCheckBox *m_chkDummyFirstPacket = nullptr;

    // thanh trang thai dung chung (ben duoi tab, luon thay du dang o tab nao)
    QLabel *m_stepLabel = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_btnCancelWait = nullptr;
    QPushButton *m_btnStop = nullptr;

    // bang trang thai node + log, dung chung (ben duoi tab)
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_nodeTable = nullptr;
    QPlainTextEdit *m_logView = nullptr;
};