#pragma once

#include "VisionTypes.h"
#include "PlcClient.h"
#include "VisionEngine.h"
#include "FlowCanvas.h"
#include "ImageViewport.h"

#include <QMainWindow>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QSlider>
#include <QSplitter>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QTimer>
#include <deque>

class VmLogoWidget : public QWidget {
public:
    explicit VmLogoWidget(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
};

class CategoryRailButton : public QPushButton {
public:
    CategoryRailButton(ToolCategory cat, QWidget* parent = nullptr);
    ToolCategory category() const { return m_cat; }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    ToolCategory m_cat;
    QColor m_accent;
};

class GlassOperatorDrawer : public QWidget {
    Q_OBJECT
public:
    explicit GlassOperatorDrawer(QWidget* parent = nullptr);
    void openCategory(ToolCategory cat, int anchorHeight);
    void closeDrawer();
    bool hasCategory(ToolCategory cat) const { return isVisible() && m_activeCat == cat; }

signals:
    void toolSelected(const QString& typeKey);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ToolCategory m_activeCat = ToolCategory::Acquisition;
    QColor m_accent{"#FF7A00"};
    QLabel* m_lblTitle = nullptr;
    QWidget* m_cardsHost = nullptr;
    QVBoxLayout* m_cardsLayout = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

class YieldRingWidget : public QWidget {
public:
    explicit YieldRingWidget(QWidget* parent = nullptr);
    void setMetrics(int total, int ok, int ng);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    int m_total = 0;
    int m_ok = 0;
    double m_pct = 0.0;
};

class LatencyChartWidget : public QWidget {
public:
    explicit LatencyChartWidget(QWidget* parent = nullptr);
    void pushSample(double ms, bool ok);
    void clearSamples();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    std::deque<QPair<double, bool>> m_samples;
};

class FreeAddressPlcEditor : public QWidget {
    Q_OBJECT
public:
    FreeAddressPlcEditor(FlowCapsuleItem* node, FlowCanvasView* canvas,
                         const QString& protocolName, QWidget* parent = nullptr);
    void rebuildTable(int selectRow = -1);
    void updatePreview();

signals:
    void bindingsChanged();

private:
    FlowCapsuleItem* m_node = nullptr;
    FlowCanvasView* m_canvas = nullptr;
    QString m_protocol;
    QTableWidget* m_table = nullptr;
    QLabel* m_lblPreview = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onRailCategoryClicked(ToolCategory cat);
    void onDrawerClosed();
    void onAppendTool(const QString& typeKey);
    void onNodeSelected(FlowCapsuleItem* node);
    void onNodeDoubleClicked(FlowCapsuleItem* node);
    void onFlowChanged();
    void onCycleCompleted(const cv::Mat& rawBgr, const cv::Mat& visBgr, const FlowCycleSummary& summary);
    void onRunStateChanged(bool running, const QString& text);
    void onEngineError(const QString& msg);
    void onRoiDrawn(int x, int y, int w, int h);
    void onPlcPoll();
    void onRefreshPlcStatus();

private:
    void buildUi();
    QWidget* buildTopTitleBar();
    QWidget* buildActionRibbon();
    QWidget* buildCategoryRail();
    QWidget* buildFlowPanel();
    QWidget* buildRightWorkspace();
    QWidget* buildCameraTab();
    QWidget* buildDashboardTab();
    QWidget* buildPlcTab();
    QWidget* buildParamPanel();
    QWidget* buildProdSidePanel();
    QWidget* buildBottomStatusBar();
    QString buildGlassStyleSheet(bool cyberBlue) const;

    void loadPresetSolution(const QString& name);
    void applyCameraSettings(bool silent = false);
    void openNativeCameraProps();
    void reopenCamera();
    void runSingleFrame();
    void runSingleUpTo(int stopNodeId);
    void toggleContinuousRun();
    void learnTemplateFromCurrentRoi(FlowCapsuleItem* node);

    void saveSolutionJson();
    void openSolutionJson();
    void openImageFolder();
    void openVideoFile();
    void loadFolderPath(const QString& folder);
    void selectImageRow(int row);
    void stepImage(int delta);
    void saveSnapshot();

    void toggleConnectPlc();
    void togglePlcTriggerWatch();
    void manualReadPlc();
    void manualWritePlc();
    void triggerManualPlcSend();
    void applyPlcPolicy();
    void toggleRunMode(bool checked);
    void appendLog(const QString& msg, const QString& level = "INFO");

    PlcClient m_plc;
    UnifiedCameraController m_camCtrl;
    ContinuousFlowEngine* m_engine = nullptr;

    bool m_cyberTheme = false;
    bool m_runMode = false;
    bool m_rawOnly = false;
    int m_lastTrigVal = 0;

    QStringList m_imagePaths;
    cv::Mat m_staticImg;
    QString m_staticName;
    cv::Mat m_lastRawImg;
    cv::Mat m_lastVisImg;
    QString m_videoPath;

    FlowCapsuleItem* m_activeNode = nullptr;
    FreeAddressPlcEditor* m_activePlcEditor = nullptr;

    int m_statTotal = 0;
    int m_statOk = 0;
    int m_statNg = 0;
    qint64 m_lastUiUpdateMs = 0;
    qint64 m_lastFrameUiUpdateMs = 0;

    QTimer* m_pollTimer = nullptr;
    QTimer* m_plcStatTimer = nullptr;

    QSplitter* m_mainSplitter = nullptr;
    QWidget* m_catRail = nullptr;
    QWidget* m_flowPanel = nullptr;
    QSplitter* m_rightSplitter = nullptr;
    QWidget* m_prodSidePanel = nullptr;
    QMap<ToolCategory, CategoryRailButton*> m_railBtns;
    GlassOperatorDrawer* m_drawer = nullptr;

    FlowCanvasView* m_flowCanvas = nullptr;
    VmImageDisplayPanel* m_imagePanel = nullptr;
    QTabWidget* m_rightTopTabs = nullptr;
    QTabWidget* m_rightBotTabs = nullptr;

    QComboBox* m_cbPreset = nullptr;
    QComboBox* m_cbSource = nullptr;
    QSpinBox* m_spCamId = nullptr;
    QComboBox* m_cbRes = nullptr;
    QPushButton* m_btnContRun = nullptr;
    QSpinBox* m_spCycleMs = nullptr;
    QPushButton* m_btnRunMode = nullptr;
    QLabel* m_lblPlcBadge = nullptr;
    QLabel* m_lblBreadcrumb = nullptr;

    QComboBox* m_cbCamFmt = nullptr;
    QComboBox* m_cbCamBackend = nullptr;
    QLabel* m_lblActualRes = nullptr;
    QCheckBox* m_chkManualExp = nullptr;
    QSlider* m_slExp = nullptr;
    QSpinBox* m_spExp = nullptr;
    QSpinBox* m_spGain = nullptr;
    QDoubleSpinBox* m_spGamma = nullptr;
    QSpinBox* m_spBias = nullptr;

    QTableWidget* m_resultTable = nullptr;
    QLabel* m_lblParamHeader = nullptr;
    QWidget* m_paramInner = nullptr;
    QGridLayout* m_paramForm = nullptr;
    QListWidget* m_fileList = nullptr;
    QTextEdit* m_logBox = nullptr;

    YieldRingWidget* m_yieldRing = nullptr;
    YieldRingWidget* m_prodRing = nullptr;
    LatencyChartWidget* m_latencyChart = nullptr;
    LatencyChartWidget* m_prodChart = nullptr;
    QLabel* m_lblKpiFps = nullptr;
    QLabel* m_lblKpiTot = nullptr;
    QLabel* m_lblKpiOk = nullptr;
    QLabel* m_lblKpiNg = nullptr;
    QLabel* m_lblProdFps = nullptr;
    QLabel* m_lblProdTot = nullptr;
    QLabel* m_lblProdOk = nullptr;
    QLabel* m_lblProdNg = nullptr;
    QLabel* m_lblProdMsg = nullptr;

    QComboBox* m_cbProto = nullptr;
    QLineEdit* m_edHost = nullptr;
    QSpinBox* m_spPort = nullptr;
    QSpinBox* m_spUnit = nullptr;
    QSpinBox* m_spBaud = nullptr;
    QSpinBox* m_spDb = nullptr;
    QSpinBox* m_spRack = nullptr;
    QSpinBox* m_spSlot = nullptr;
    QPushButton* m_btnConn = nullptr;
    QComboBox* m_cbPlcPolicy = nullptr;
    QSpinBox* m_spPlcInterval = nullptr;
    QSpinBox* m_spTrigAddr = nullptr;
    QPushButton* m_btnPlcTrig = nullptr;
    QLabel* m_lblPlcTxRx = nullptr;
    QSpinBox* m_spTestAddr = nullptr;
    QLineEdit* m_edTestVal = nullptr;

    QLabel* m_lblRunState = nullptr;
    QLabel* m_lblFlowMs = nullptr;
    QLabel* m_lblModMs = nullptr;
    QLabel* m_lblFpsBar = nullptr;
    QSlider* m_slFlowZoom = nullptr;
    QLabel* m_lblFlowZoom = nullptr;
    QLabel* m_lblTicker = nullptr;
};
