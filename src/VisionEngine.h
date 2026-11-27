#pragma once

#include "VisionTypes.h"
#include "PlcClient.h"
#include <QObject>
#include <QMap>
#include <mutex>
#include <thread>
#include <atomic>

class UnifiedCameraController {
public:
    UnifiedCameraController();
    ~UnifiedCameraController();

    void configure(int camIndex, int reqW, int reqH,
                   const QString& pixelFormat, const QString& backend,
                   bool manualExposure, double exposureVal,
                   int hwGain, double swGamma, int swBias);

    bool openDriverSettings();
    cv::Mat grabFrame();
    void release();

    int actualWidth() const { return m_actualW.load(); }
    int actualHeight() const { return m_actualH.load(); }
    int camIndex() const { return m_camIndex; }
    int reqWidth() const { return m_reqW; }
    int reqHeight() const { return m_reqH; }

private:
    bool openUnlocked();
    void applyExposureUnlocked();
    void releaseUnlocked();

    mutable std::mutex m_mutex;
    cv::VideoCapture m_cap;
    int m_camIndex = 0;
    int m_reqW = 1920;
    int m_reqH = 1080;
    std::atomic<int> m_actualW{0};
    std::atomic<int> m_actualH{0};
    QString m_pixelFormat = "自动 (4K用MJPG/低分不改)";
    QString m_backend = "DSHOW";

    bool m_manualExposure = false;
    double m_exposureVal = -5.0;
    int m_hwGain = -1;
    double m_swGamma = 1.0;
    int m_swBias = 0;
};

class VisionOperatorRegistry {
public:
    static const QVector<ToolMeta>& allTools();
    static ToolMeta findTool(const QString& typeKey);
    static QVariantMap defaultParams(const QString& typeKey);
    static QVector<PlcBindingItem> defaultPlcBindings();
    static QVector<CalibPoint> defaultCalibPoints();
    static bool solveNinePointAffine(const QVector<CalibPoint>& pts, cv::Mat& outM, double& outRmse);

    // 学习模板方式：从指定图像区域学习并构建多尺度金字塔+边缘特征高精度模板模型
    static bool learnTemplateFromRoi(const cv::Mat& srcImg, const cv::Rect& roi, LearnedTemplateModel& outModel);
    static void rebuildModelPyramids(LearnedTemplateModel& model);
};

class FlowExecutionRunner {
public:
    static FlowCycleSummary execute(
        const QVector<FlowNodeData>& nodes,
        const QVector<FlowEdgeData>& edges,
        const cv::Mat& inputFrame,
        const QString& sourceName,
        PlcClient* plcClient,
        UnifiedCameraController* camCtrl,
        int stopNodeId = -1,
        int inspectNodeId = -1,
        bool isLiveStream = false,
        bool allowPlcSend = true,
        bool allowSaveIo = true
    );
};

class ContinuousFlowEngine : public QObject {
    Q_OBJECT
public:
    explicit ContinuousFlowEngine(PlcClient* plc, UnifiedCameraController* cam, QObject* parent = nullptr);
    ~ContinuousFlowEngine() override;

    bool isRunning() const { return m_running.load(); }
    void setSourceMode(const QString& mode, const QString& videoPath = QString());
    void setCycleInterval(int ms);
    void setPaused(bool paused);
    void setInspectNode(int nodeId);
    void setPlcSendPolicy(const QString& policyMode, int intervalMs = 200);
    void requestOnePlcSend();
    void setFolderImages(const QStringList& paths, int startIdx = 0);
    void updateFlowSnapshot(const QVector<FlowNodeData>& nodes, const QVector<FlowEdgeData>& edges);

    void runSingleCycle(const cv::Mat& staticImg, const QString& staticName, int stopNodeId = -1);
    void startContinuous();
    void stopContinuous();

signals:
    void cycleCompleted(const cv::Mat& rawBgr, const cv::Mat& visBgr, const FlowCycleSummary& summary);
    void runStateChanged(bool running, const QString& statusText);
    void errorOccurred(const QString& errorMsg);

private:
    cv::Mat acquireFrame(bool advanceFolder, QString& outName);
    cv::Mat generateSimFrame(int w, int h);
    void continuousLoop();

    PlcClient* m_plc = nullptr;
    UnifiedCameraController* m_cam = nullptr;

    mutable std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::thread m_thread;

    QString m_sourceMode = "USB相机";
    QString m_videoPath;
    cv::VideoCapture m_videoCap;
    int m_cycleIntervalMs = 0;
    int m_inspectNodeId = -1;

    QString m_plcPolicy = "MANUAL_TRIG";
    int m_plcIntervalMs = 200;
    bool m_pendingPlcTrig = false;

    QStringList m_folderImages;
    int m_folderIdx = 0;
    int64_t m_simTick = 0;

    QVector<FlowNodeData> m_nodes;
    QVector<FlowEdgeData> m_edges;
};
