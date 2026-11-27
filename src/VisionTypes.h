#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QPointF>
#include <QMap>
#include <QPair>
#include <opencv2/opencv.hpp>

enum class ToolCategory {
    Acquisition,   // 图像采集
    Preprocess,    // 图像预处理
    Location,      // 定位工具
    Measurement,   // 测量工具
    Recognition,   // 识别工具
    Calibration,   // 标定转换
    Logic,         // 逻辑工具
    Communication  // 通信与IO
};

enum class EdgeCondition {
    Always = 0, // ALL - 始终执行
    OnOk   = 1, // ON_OK - 仅上游OK执行
    OnNg   = 2  // ON_NG - 仅上游NG执行
};

struct PlcBindingItem {
    int targetAddr = 100;       // 每个数据项独立自由定义PLC目标地址
    int sourceNodeId = 0;       // 0=紧邻上游, -1=固定常量, >0=指定前置节点ID
    QString field = "ok";       // ok / x / y / extra / count / text_len
    QString dataType = "INT16"; // INT16 / UINT16 / INT32 / FLOAT32
    double scale = 1.0;
    double constVal = 0.0;
};

struct CalibPoint {
    double px = 0.0;
    double py = 0.0;
    double wx = 0.0;
    double wy = 0.0;
};

// 高精度学习模板模型（内置金字塔多尺度灰度图与边缘特征图，支持序列化保存与加载）
struct LearnedTemplateModel {
    bool isLearned = false;
    cv::Mat grayPatch;           // 原始学习灰度模板
    cv::Mat edgePatch;           // 边缘梯度特征图（抗光照波动）
    QVector<cv::Mat> pyrGray;    // 多层图像金字塔（粗定位加速）
    QVector<cv::Mat> pyrEdge;    // 多层边缘金字塔
    int width = 0;
    int height = 0;
    QString learnedTime;
};

struct StepResult {
    int ok = 0;
    double x = 0.0;
    double y = 0.0;
    double extra = 0.0;
    int count = 0;
    QString text;
    QString summary;
    QString msg;
};

struct ToolMeta {
    QString typeKey;
    QString displayName;
    ToolCategory category;
    bool supportsRoi = false;
    bool supportsTemplateLearn = false;
};

struct FlowNodeData {
    int id = 1;
    QString typeKey;
    QString title;
    QPointF pos{180, 40};
    bool enabled = true;
    QVariantMap params;
    QVector<PlcBindingItem> plcBindings;
    QVector<CalibPoint> calibPoints;
    LearnedTemplateModel learnedModel;
};

struct FlowEdgeData {
    int srcId = 0;
    int dstId = 0;
    EdgeCondition condition = EdgeCondition::Always;
};

struct NodeRuntimeState {
    QString status = "IDLE"; // IDLE / OK / NG / SKIP / DISABLED
    double elapsedMs = 0.0;
    QString summary;
    StepResult result;
    cv::Mat outputImage;
};

struct FlowCycleSummary {
    double totalMs = 0.0;
    double fps = 0.0;
    bool finalOk = true;
    bool plcSent = false;
    QString finalMsg;
    QString sourceName;
    cv::Mat rawImage;
    cv::Mat displayImage;
    QMap<int, NodeRuntimeState> nodeStates;
    QVector<QPair<int, int>> activeEdges;
    QVector<int> executionOrder;
};

inline QString categoryToChinese(ToolCategory c) {
    switch (c) {
        case ToolCategory::Acquisition:   return QStringLiteral("图像采集");
        case ToolCategory::Preprocess:    return QStringLiteral("图像预处理");
        case ToolCategory::Location:      return QStringLiteral("定位工具");
        case ToolCategory::Measurement:   return QStringLiteral("测量工具");
        case ToolCategory::Recognition:   return QStringLiteral("识别工具");
        case ToolCategory::Calibration:   return QStringLiteral("标定转换");
        case ToolCategory::Logic:         return QStringLiteral("逻辑工具");
        case ToolCategory::Communication: return QStringLiteral("通信与IO");
    }
    return QStringLiteral("其他");
}

inline QString categoryShortName(ToolCategory c) {
    switch (c) {
        case ToolCategory::Acquisition:   return QStringLiteral("采集");
        case ToolCategory::Preprocess:    return QStringLiteral("处理");
        case ToolCategory::Location:      return QStringLiteral("定位");
        case ToolCategory::Measurement:   return QStringLiteral("测量");
        case ToolCategory::Recognition:   return QStringLiteral("识别");
        case ToolCategory::Calibration:   return QStringLiteral("标定");
        case ToolCategory::Logic:         return QStringLiteral("逻辑");
        case ToolCategory::Communication: return QStringLiteral("通信");
    }
    return QStringLiteral("工具");
}

inline QString categoryIconText(ToolCategory c) {
    switch (c) {
        case ToolCategory::Acquisition:   return QStringLiteral("CAM");
        case ToolCategory::Preprocess:    return QStringLiteral("IMG");
        case ToolCategory::Location:      return QStringLiteral("POS");
        case ToolCategory::Measurement:   return QStringLiteral("MSR");
        case ToolCategory::Recognition:   return QStringLiteral("ID");
        case ToolCategory::Calibration:   return QStringLiteral("CAL");
        case ToolCategory::Logic:         return QStringLiteral("IF");
        case ToolCategory::Communication: return QStringLiteral("PLC");
    }
    return QStringLiteral("VM");
}

inline QString categoryColorHex(ToolCategory c) {
    switch (c) {
        case ToolCategory::Acquisition:   return QStringLiteral("#22C55E");
        case ToolCategory::Preprocess:    return QStringLiteral("#A855F7");
        case ToolCategory::Location:      return QStringLiteral("#FF7A00");
        case ToolCategory::Measurement:   return QStringLiteral("#06B6D4");
        case ToolCategory::Recognition:   return QStringLiteral("#3B82F6");
        case ToolCategory::Calibration:   return QStringLiteral("#EC4899");
        case ToolCategory::Logic:         return QStringLiteral("#F59E0B");
        case ToolCategory::Communication: return QStringLiteral("#10B981");
    }
    return QStringLiteral("#FF7A00");
}
