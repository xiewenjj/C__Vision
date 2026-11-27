#include "VisionEngine.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <cmath>
#include <deque>
#include <algorithm>

namespace {

cv::Mat ensureBgr(const cv::Mat& src) {
    if (src.empty()) return cv::Mat();
    if (src.channels() == 1) {
        cv::Mat dst;
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
        return dst;
    }
    if (src.channels() == 4) {
        cv::Mat dst;
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
        return dst;
    }
    return src.clone();
}

cv::Mat toGray(const cv::Mat& src) {
    if (src.empty()) return cv::Mat();
    if (src.channels() == 1) return src.clone();
    cv::Mat g;
    cv::cvtColor(ensureBgr(src), g, cv::COLOR_BGR2GRAY);
    return g;
}

cv::Mat computeEdgeGradient(const cv::Mat& gray) {
    cv::Mat gx, gy, mag;
    cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    double maxV = 0.0;
    cv::minMaxLoc(mag, nullptr, &maxV);
    cv::Mat out8U;
    if (maxV > 1e-5) {
        mag.convertTo(out8U, CV_8U, 255.0 / maxV);
    } else {
        out8U = cv::Mat::zeros(gray.size(), CV_8U);
    }
    return out8U;
}

// 旋转模板并保留原始尺寸（边缘用均值填充，避免旋转黑边产生伪特征）
cv::Mat rotateTemplateKeepSize(const cv::Mat& patch, double angleDeg, double borderFill) {
    cv::Point2f center(patch.cols / 2.0f, patch.rows / 2.0f);
    cv::Mat rotM = cv::getRotationMatrix2D(center, angleDeg, 1.0);
    cv::Mat rotated;
    cv::warpAffine(patch, rotated, rotM, patch.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(borderFill));
    return rotated;
}

// 2D抛物线亚像素峰值精定位（达 0.01px 精度）
void refineSubpixelPeak(const cv::Mat& scoreMap, const cv::Point& peak, double& outDx, double& outDy) {
    outDx = 0.0;
    outDy = 0.0;
    int x = peak.x, y = peak.y;
    if (x <= 0 || x >= scoreMap.cols - 1 || y <= 0 || y >= scoreMap.rows - 1) return;

    double vL = scoreMap.at<float>(y, x - 1);
    double vC = scoreMap.at<float>(y, x);
    double vR = scoreMap.at<float>(y, x + 1);
    double denomX = 2.0 * (vL - 2.0 * vC + vR);
    if (std::abs(denomX) > 1e-7) {
        double dx = (vL - vR) / denomX;
        if (std::abs(dx) <= 1.0) outDx = dx;
    }

    double vT = scoreMap.at<float>(y - 1, x);
    double vB = scoreMap.at<float>(y + 1, x);
    double denomY = 2.0 * (vT - 2.0 * vC + vB);
    if (std::abs(denomY) > 1e-7) {
        double dy = (vT - vB) / denomY;
        if (std::abs(dy) <= 1.0) outDy = dy;
    }
}

bool imwriteUnicodeCpp(const QString& path, const cv::Mat& img) {
    if (path.isEmpty() || img.empty()) return false;
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    std::string ext = "." + fi.suffix().toStdString();
    if (ext == ".") ext = ".png";
    std::vector<uchar> buf;
    if (!cv::imencode(ext, img, buf)) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size())) == static_cast<qint64>(buf.size());
}

struct DrawStyle {
    double scale = 1.0;
    int thick = 2;
    double fontScale = 0.62;
    int markerSize = 20;
};

DrawStyle getDrawStyle(const cv::Mat& img) {
    DrawStyle ds;
    ds.scale = std::max(1.0, img.cols / 800.0);
    ds.thick = std::max(2, static_cast<int>(std::lround(2.0 * ds.scale)));
    ds.fontScale = 0.62 * ds.scale;
    ds.markerSize = static_cast<int>(std::lround(20.0 * ds.scale));
    return ds;
}

cv::Mat extractSearchRoi(const cv::Mat& src, const QVariantMap& params, int& ox, int& oy, cv::Rect& roiRect, bool& hasRoi) {
    ox = 0; oy = 0; hasRoi = false;
    if (!params.value("use_search_roi", false).toBool() || src.empty()) {
        return src;
    }
    int w = src.cols, h = src.rows;
    int rx = std::max(0, std::min(w - 2, params.value("sroi_x", 50).toInt()));
    int ry = std::max(0, std::min(h - 2, params.value("sroi_y", 50).toInt()));
    int rw = std::max(4, std::min(w - rx, params.value("sroi_w", 400).toInt()));
    int rh = std::max(4, std::min(h - ry, params.value("sroi_h", 400).toInt()));
    ox = rx; oy = ry;
    roiRect = cv::Rect(rx, ry, rw, rh);
    hasRoi = true;
    return src(roiRect);
}

void drawRoiBox(cv::Mat& vis, const cv::Rect& r, bool hasRoi, const char* tag = "SearchROI") {
    if (!hasRoi || vis.empty()) return;
    DrawStyle ds = getDrawStyle(vis);
    cv::rectangle(vis, r, cv::Scalar(0, 140, 255), ds.thick);
    std::string lbl = cv::format("%s(%d,%d,%dx%d)", tag, r.x, r.y, r.width, r.height);
    cv::putText(vis, lbl, cv::Point(r.x + 4, std::max(static_cast<int>(20 * ds.scale), r.y - static_cast<int>(8 * ds.scale))),
                cv::FONT_HERSHEY_SIMPLEX, ds.fontScale * 0.78, cv::Scalar(0, 140, 255), std::max(1, ds.thick - 1), cv::LINE_AA);
}

struct ExecContext {
    cv::Mat streamFrame;
    cv::Mat sourceImage;
    QString sourceName;
    cv::Mat rawImage;
    cv::Mat image;
    cv::Mat grayImage;
    cv::Mat displayImage;
    StepResult lastResult;
    QMap<int, StepResult> results;
    PlcClient* plcClient = nullptr;
    UnifiedCameraController* camCtrl = nullptr;
    bool allowPlcSend = true;
    bool allowSaveIo = true;
};

} // namespace

// ==================== UnifiedCameraController ====================
UnifiedCameraController::UnifiedCameraController() = default;
UnifiedCameraController::~UnifiedCameraController() { release(); }

void UnifiedCameraController::configure(int camIndex, int reqW, int reqH,
                                        const QString& pixelFormat, const QString& backend,
                                        bool manualExposure, double exposureVal,
                                        int hwGain, double swGamma, int swBias) {
    std::lock_guard<std::mutex> lk(m_mutex);
    bool reopen = false;
    if (camIndex != m_camIndex) { m_camIndex = camIndex; reopen = true; }
    if (reqW != m_reqW) { m_reqW = reqW; reopen = true; }
    if (reqH != m_reqH) { m_reqH = reqH; reopen = true; }
    if (pixelFormat != m_pixelFormat) { m_pixelFormat = pixelFormat; reopen = true; }
    if (backend != m_backend) { m_backend = backend; reopen = true; }

    m_manualExposure = manualExposure;
    m_exposureVal = exposureVal;
    m_hwGain = hwGain;
    m_swGamma = swGamma;
    m_swBias = swBias;

    if (reopen && m_cap.isOpened()) {
        releaseUnlocked();
    } else if (m_cap.isOpened()) {
        applyExposureUnlocked();
    }
}

void UnifiedCameraController::release() {
    std::lock_guard<std::mutex> lk(m_mutex);
    releaseUnlocked();
}

void UnifiedCameraController::releaseUnlocked() {
    if (m_cap.isOpened()) m_cap.release();
}

void UnifiedCameraController::applyExposureUnlocked() {
    if (!m_cap.isOpened()) return;
    if (m_manualExposure) {
        m_cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);
        m_cap.set(cv::CAP_PROP_EXPOSURE, m_exposureVal);
    } else {
        m_cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.75);
    }
    if (m_hwGain >= 0) m_cap.set(cv::CAP_PROP_GAIN, static_cast<double>(m_hwGain));
}

bool UnifiedCameraController::openUnlocked() {
    if (m_cap.isOpened()) return true;
    int api = cv::CAP_DSHOW;
    if (m_backend == "MSMF") api = cv::CAP_MSMF;
    else if (m_backend == "AUTO") api = cv::CAP_ANY;

    if (!m_cap.open(m_camIndex, api)) {
        if (!m_cap.open(m_camIndex)) return false;
    }

    bool useMjpg = m_pixelFormat.startsWith("MJPG") || (m_pixelFormat.startsWith("自动") && m_reqW >= 1920);
    bool useYuy2 = m_pixelFormat.startsWith("YUY2");
    if (useMjpg) {
        m_cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    } else if (useYuy2) {
        m_cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', '2'));
    }
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_reqW);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_reqH);
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    applyExposureUnlocked();

    cv::Mat dummy;
    for (int i = 0; i < 5; ++i) m_cap.read(dummy);
    m_actualW.store(static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_WIDTH)));
    m_actualH.store(static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_HEIGHT)));
    return true;
}

bool UnifiedCameraController::openDriverSettings() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!openUnlocked()) return false;
    return m_cap.set(cv::CAP_PROP_SETTINGS, 1.0);
}

cv::Mat UnifiedCameraController::grabFrame() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!openUnlocked()) return cv::Mat();
    cv::Mat frame;
    if (!m_cap.read(frame) || frame.empty()) return cv::Mat();
    m_actualW.store(frame.cols);
    m_actualH.store(frame.rows);

    if (std::abs(m_swGamma - 1.0) > 0.01) {
        double invG = 1.0 / std::max(0.25, m_swGamma);
        cv::Mat lut(1, 256, CV_8U);
        uchar* p = lut.ptr();
        for (int i = 0; i < 256; ++i) p[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, invG) * 255.0);
        cv::LUT(frame, lut, frame);
    }
    if (m_swBias != 0) frame.convertTo(frame, -1, 1.0, m_swBias);
    return frame;
}

// ==================== VisionOperatorRegistry & Template Learning ====================
const QVector<ToolMeta>& VisionOperatorRegistry::allTools() {
    static const QVector<ToolMeta> list = {
        // 1. 图像采集 (3)
        {"image_source",        "本地图像源",            ToolCategory::Acquisition,   false, false},
        {"camera_source",       "USB相机采集(4K)",       ToolCategory::Acquisition,   false, false},
        {"sim_source",          "仿真产线图像源",         ToolCategory::Acquisition,   false, false},
        // 2. 图像预处理 (6)
        {"color_convert",       "颜色转换 (灰度)",        ToolCategory::Preprocess,    false, false},
        {"img_enhance",         "亮度/Gamma增强",        ToolCategory::Preprocess,    false, false},
        {"img_filter",          "图像滤波",              ToolCategory::Preprocess,    false, false},
        {"img_binary",          "二值化分割",            ToolCategory::Preprocess,    false, false},
        {"img_morph",           "形态学处理",            ToolCategory::Preprocess,    false, false},
        {"img_transform",       "图像旋转镜像",          ToolCategory::Preprocess,    false, false},
        // 3. 定位工具 (6)
        {"high_prec_match",     "高精度模板匹配",        ToolCategory::Location,      true,  true},
        {"locate_template",     "快速灰度匹配",          ToolCategory::Location,      true,  true},
        {"locate_shape",        "轮廓形状匹配",          ToolCategory::Location,      true,  true},
        {"locate_blob",         "斑点轮廓定位",          ToolCategory::Location,      true,  false},
        {"locate_circle",       "霍夫圆形定位",          ToolCategory::Location,      true,  false},
        {"locate_line",         "直线边缘查找",          ToolCategory::Location,      true,  false},
        // 4. 测量工具 (4)
        {"measure_dist",        "基准点距离测量",        ToolCategory::Measurement,   false, false},
        {"measure_cgap",        "双圆心距测量",          ToolCategory::Measurement,   true,  false},
        {"measure_bright",      "ROI亮度/有无料检测",    ToolCategory::Measurement,   true,  false},
        {"measure_area",        "像素面积统计",          ToolCategory::Measurement,   true,  false},
        // 5. 识别工具 (2)
        {"detect_color",        "HSV颜色识别",           ToolCategory::Recognition,   true,  false},
        {"detect_qrcode",       "二维码/条码识别",       ToolCategory::Recognition,   true,  false},
        // 6. 标定转换 (1)
        {"calib_9pt",           "九点手眼标定",          ToolCategory::Calibration,   false, false},
        // 7. 逻辑工具 (2)
        {"logic_judge",         "条件公差判定",          ToolCategory::Logic,         false, false},
        {"logic_branch",        "条件值分流器",          ToolCategory::Logic,         false, false},
        // 8. 通信与IO (2)
        {"comm_plc_send",       "PLC自由地址发送",       ToolCategory::Communication, false, false},
        {"io_save_image",       "自动存图归档",          ToolCategory::Communication, false, false},
    };
    return list;
}

ToolMeta VisionOperatorRegistry::findTool(const QString& typeKey) {
    for (const auto& m : allTools()) {
        if (m.typeKey == typeKey) return m;
    }
    return {"unknown", typeKey, ToolCategory::Preprocess, false, false};
}

QVariantMap VisionOperatorRegistry::defaultParams(const QString& t) {
    QVariantMap p;
    if (t == "camera_source") {
        p["cam_index"] = 0;
    } else if (t == "color_convert") {
        p["mode"] = "彩色转灰度 (Gray)";
        p["clahe_clip"] = 2.5;
        p["use_as_display"] = true;
    } else if (t == "img_enhance") {
        p["gamma"] = 1.5; p["contrast"] = 1.0; p["brightness"] = 0; p["auto_clahe"] = false;
    } else if (t == "img_filter") {
        p["method"] = "高斯滤波"; p["ksize"] = 5;
    } else if (t == "img_binary") {
        p["thresh"] = 120; p["use_otsu"] = false; p["invert"] = false;
    } else if (t == "img_morph") {
        p["op"] = "开运算(去噪)"; p["ksize"] = 5; p["iterations"] = 1;
    } else if (t == "img_transform") {
        p["mode"] = "顺时针90度";
    } else if (t == "high_prec_match") {
        p["min_score"] = 0.75;
        p["angle_start"] = -180;
        p["angle_end"] = 180;
        p["angle_step"] = 5;
        p["feature_mode"] = "灰度+边缘特征融合 (高精抗光)";
        p["subpixel"] = true;
    } else if (t == "locate_template") {
        p["score_thresh"] = 0.75;
    } else if (t == "locate_shape") {
        p["thresh"] = 120; p["invert"] = false; p["min_score"] = 70;
    } else if (t == "locate_blob") {
        p["thresh"] = 120; p["invert"] = false; p["min_area"] = 500; p["max_area"] = 8000000;
    } else if (t == "locate_circle") {
        p["min_r"] = 15; p["max_r"] = 300; p["param2"] = 35;
    } else if (t == "locate_line") {
        p["canny_low"] = 50; p["canny_high"] = 150; p["min_length"] = 60;
    } else if (t == "measure_dist") {
        p["ref_x"] = 320; p["ref_y"] = 240; p["mm_per_px"] = 1.0;
    } else if (t == "measure_cgap") {
        p["min_r"] = 15; p["max_r"] = 300; p["param2"] = 35; p["mm_per_px"] = 1.0;
    } else if (t == "measure_bright") {
        p["min_bright"] = 60; p["max_bright"] = 220;
    } else if (t == "measure_area") {
        p["thresh"] = 120; p["invert"] = false; p["min_area"] = 500;
    } else if (t == "detect_color") {
        p["min_area"] = 800;
    } else if (t == "logic_judge") {
        p["field"] = "x"; p["min_val"] = 0.0; p["max_val"] = 3840.0; p["require_prev_ok"] = true;
    } else if (t == "logic_branch") {
        p["field"] = "extra"; p["operator"] = "=="; p["target_val"] = 1.0;
    } else if (t == "comm_plc_send") {
        p["word_order"] = "ABCD"; p["delimiter"] = ",";
    } else if (t == "io_save_image") {
        p["save_dir"] = ""; p["prefix"] = "NG"; p["save_raw"] = false;
    }
    p["use_search_roi"] = false;
    p["sroi_x"] = 50; p["sroi_y"] = 50; p["sroi_w"] = 400; p["sroi_h"] = 400;
    return p;
}

QVector<PlcBindingItem> VisionOperatorRegistry::defaultPlcBindings() {
    return {
        {100, 0, "ok",    "INT16", 1.0, 0.0},
        {101, 0, "x",     "INT16", 1.0, 0.0},
        {102, 0, "y",     "INT16", 1.0, 0.0},
        {103, 0, "extra", "INT16", 1.0, 0.0},
    };
}

QVector<CalibPoint> VisionOperatorRegistry::defaultCalibPoints() {
    return {
        {100, 100, 10, 10}, {320, 100, 32, 10}, {540, 100, 54, 10},
        {100, 240, 10, 24}, {320, 240, 32, 24}, {540, 240, 54, 24},
        {100, 380, 10, 38}, {320, 380, 32, 38}, {540, 380, 54, 38},
    };
}

void VisionOperatorRegistry::rebuildModelPyramids(LearnedTemplateModel& model) {
    model.pyrGray.clear();
    model.pyrEdge.clear();
    if (model.grayPatch.empty()) {
        model.isLearned = false;
        return;
    }
    model.width = model.grayPatch.cols;
    model.height = model.grayPatch.rows;
    model.edgePatch = computeEdgeGradient(model.grayPatch);

    cv::Mat curG = model.grayPatch.clone();
    cv::Mat curE = model.edgePatch.clone();
    model.pyrGray.push_back(curG);
    model.pyrEdge.push_back(curE);

    while (curG.cols >= 48 && curG.rows >= 48 && model.pyrGray.size() < 4) {
        cv::Mat nxtG, nxtE;
        cv::pyrDown(curG, nxtG);
        cv::pyrDown(curE, nxtE);
        model.pyrGray.push_back(nxtG);
        model.pyrEdge.push_back(nxtE);
        curG = nxtG;
        curE = nxtE;
    }
    model.isLearned = true;
}

bool VisionOperatorRegistry::learnTemplateFromRoi(const cv::Mat& srcImg, const cv::Rect& roi, LearnedTemplateModel& outModel) {
    if (srcImg.empty()) return false;
    cv::Rect validR = roi & cv::Rect(0, 0, srcImg.cols, srcImg.rows);
    if (validR.width < 12 || validR.height < 12) return false;

    cv::Mat patch = srcImg(validR);
    outModel.grayPatch = toGray(patch);
    outModel.learnedTime = QDateTime::currentDateTime().toString("HH:mm:ss");
    rebuildModelPyramids(outModel);
    return outModel.isLearned;
}

bool VisionOperatorRegistry::solveNinePointAffine(const QVector<CalibPoint>& pts, cv::Mat& outM, double& outRmse) {
    if (pts.size() < 3) return false;
    std::vector<cv::Point2f> src, dst;
    for (const auto& pt : pts) {
        src.emplace_back(static_cast<float>(pt.px), static_cast<float>(pt.py));
        dst.emplace_back(static_cast<float>(pt.wx), static_cast<float>(pt.wy));
    }
    cv::Mat M = cv::estimateAffine2D(src, dst, cv::noArray(), cv::LMEDS);
    if (M.empty()) return false;
    double sumSq = 0.0;
    for (size_t i = 0; i < src.size(); ++i) {
        double px = M.at<double>(0, 0) * src[i].x + M.at<double>(0, 1) * src[i].y + M.at<double>(0, 2);
        double py = M.at<double>(1, 0) * src[i].x + M.at<double>(1, 1) * src[i].y + M.at<double>(1, 2);
        double dx = px - dst[i].x, dy = py - dst[i].y;
        sumSq += dx * dx + dy * dy;
    }
    outM = M;
    outRmse = std::sqrt(sumSq / static_cast<double>(src.size()));
    return true;
}

// ==================== 高精度金字塔旋转亚像素模板匹配核心算法 ====================
static StepResult runHighPrecisionTemplateMatch(const FlowNodeData& node, ExecContext& ctx, cv::Mat& outVis) {
    StepResult res;
    cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
    DrawStyle ds = getDrawStyle(vis);
    int ox = 0, oy = 0; cv::Rect sroiR; bool hasSroi = false;
    cv::Mat sub = extractSearchRoi(ctx.image, node.params, ox, oy, sroiR, hasSroi);
    drawRoiBox(vis, sroiR, hasSroi, "SearchROI");
    outVis = vis;

    const LearnedTemplateModel& model = node.learnedModel;
    if (!model.isLearned || model.grayPatch.empty()) {
        res.summary = "未学习模板";
        res.msg = "请先在图像上框选目标区域，然后点击「🎓 学习当前区域为模板」";
        return res;
    }

    cv::Mat searchGray = toGray(sub);
    if (model.width > searchGray.cols || model.height > searchGray.rows) {
        res.summary = "模板超限";
        res.msg = "学习模板尺寸大于当前搜索区域";
        return res;
    }

    double minScore = node.params.value("min_score", 0.75).toDouble();
    int angStart = node.params.value("angle_start", -180).toInt();
    int angEnd = node.params.value("angle_end", 180).toInt();
    int angStep = std::max(1, node.params.value("angle_step", 5).toInt());
    if (angStart > angEnd) std::swap(angStart, angEnd);
    bool useEdgeFusion = node.params.value("feature_mode", "灰度+边缘特征融合 (高精抗光)").toString().contains("边缘");
    bool doSubpixel = node.params.value("subpixel", true).toBool();

    // 构建搜索图像金字塔
    int topLevel = std::max(0, static_cast<int>(model.pyrGray.size()) - 1);
    QVector<cv::Mat> searchPyrG, searchPyrE;
    cv::Mat curSG = searchGray;
    cv::Mat curSE = useEdgeFusion ? computeEdgeGradient(searchGray) : cv::Mat();
    searchPyrG.push_back(curSG);
    searchPyrE.push_back(curSE);
    for (int lv = 1; lv <= topLevel; ++lv) {
        cv::Mat nxtG; cv::pyrDown(curSG, nxtG);
        searchPyrG.push_back(nxtG);
        curSG = nxtG;
        if (useEdgeFusion) {
            cv::Mat nxtE; cv::pyrDown(curSE, nxtE);
            searchPyrE.push_back(nxtE);
            curSE = nxtE;
        } else {
            searchPyrE.push_back(cv::Mat());
        }
    }

    // 阶段1：顶层金字塔全角度粗搜索，筛选Top-3候选角度
    struct AngleCand { double angle; double score; cv::Point loc; };
    std::vector<AngleCand> coarseCands;
    const cv::Mat& topSG = searchPyrG[topLevel];
    const cv::Mat& topSE = searchPyrE[topLevel];
    const cv::Mat& topTG = model.pyrGray[topLevel];
    const cv::Mat& topTE = model.pyrEdge[topLevel];
    double meanTG = cv::mean(topTG)[0];

    if (topTG.cols < topSG.cols && topTG.rows < topSG.rows) {
        for (int a = angStart; a <= angEnd; a += angStep) {
            cv::Mat rotG = (a == 0) ? topTG : rotateTemplateKeepSize(topTG, a, meanTG);
            cv::Mat resG;
            cv::matchTemplate(topSG, rotG, resG, cv::TM_CCOEFF_NORMED);
            double score = 0.0;
            cv::Point maxLoc;
            if (useEdgeFusion && !topSE.empty() && !topTE.empty()) {
                cv::Mat rotE = (a == 0) ? topTE : rotateTemplateKeepSize(topTE, a, 0.0);
                cv::Mat resE;
                cv::matchTemplate(topSE, rotE, resE, cv::TM_CCOEFF_NORMED);
                cv::Mat fused;
                cv::addWeighted(resG, 0.65, resE, 0.35, 0.0, fused);
                cv::minMaxLoc(fused, nullptr, &score, nullptr, &maxLoc);
            } else {
                cv::minMaxLoc(resG, nullptr, &score, nullptr, &maxLoc);
            }
            coarseCands.push_back({static_cast<double>(a), score, maxLoc});
        }
    } else {
        coarseCands.push_back({0.0, 0.0, cv::Point(0, 0)});
    }

    std::sort(coarseCands.begin(), coarseCands.end(), [](const AngleCand& a, const AngleCand& b) {
        return a.score > b.score;
    });
    if (coarseCands.size() > 3) coarseCands.resize(3);

    // 阶段2：全分辨率 (Level 0) 局部精细角度搜索 + 2D抛物线亚像素精定位
    double bestScore = -1.0;
    double bestAngle = 0.0;
    cv::Point bestIntLoc(0, 0);
    cv::Mat bestScoreMap;

    const cv::Mat& fullTG = model.pyrGray[0];
    const cv::Mat& fullTE = model.pyrEdge[0];
    const cv::Mat& fullSE = searchPyrE[0];
    double fullMeanG = cv::mean(fullTG)[0];

    std::vector<double> fineAngles;
    for (const auto& c : coarseCands) {
        double fineSpan = (angStart == angEnd) ? 0.0 : static_cast<double>(angStep);
        double fineStep = (angStart == angEnd) ? 1.0 : std::max(0.5, angStep / 5.0);
        for (double a = c.angle - fineSpan; a <= c.angle + fineSpan + 1e-4; a += fineStep) {
            if (a < angStart || a > angEnd) continue;
            bool dup = false;
            for (double ex : fineAngles) {
                if (std::abs(ex - a) < 0.15) { dup = true; break; }
            }
            if (!dup) fineAngles.push_back(a);
        }
    }
    if (fineAngles.empty()) fineAngles.push_back(0.0);

    for (double a : fineAngles) {
        cv::Mat rotG = (std::abs(a) < 1e-4) ? fullTG : rotateTemplateKeepSize(fullTG, a, fullMeanG);
        cv::Mat resG;
        cv::matchTemplate(searchGray, rotG, resG, cv::TM_CCOEFF_NORMED);
        cv::Mat targetMap = resG;
        if (useEdgeFusion && !fullSE.empty() && !fullTE.empty()) {
            cv::Mat rotE = (std::abs(a) < 1e-4) ? fullTE : rotateTemplateKeepSize(fullTE, a, 0.0);
            cv::Mat resE;
            cv::matchTemplate(fullSE, rotE, resE, cv::TM_CCOEFF_NORMED);
            cv::addWeighted(resG, 0.65, resE, 0.35, 0.0, targetMap);
        }
        double sc = 0.0;
        cv::Point loc;
        cv::minMaxLoc(targetMap, nullptr, &sc, nullptr, &loc);
        if (sc > bestScore) {
            bestScore = sc;
            bestAngle = a;
            bestIntLoc = loc;
            bestScoreMap = targetMap;
        }
    }

    double subDx = 0.0, subDy = 0.0;
    if (doSubpixel && !bestScoreMap.empty()) {
        refineSubpixelPeak(bestScoreMap, bestIntLoc, subDx, subDy);
    }

    double centerX = ox + bestIntLoc.x + subDx + model.width / 2.0;
    double centerY = oy + bestIntLoc.y + subDy + model.height / 2.0;
    double scorePct = bestScore * 100.0;

    if (bestScore < minScore) {
        res.extra = std::round(bestAngle * 10.0) / 10.0;
        res.summary = QString("NG %1%").arg(scorePct, 0, 'f', 1);
        res.msg = QString("高精度匹配NG 最高分:%1% < 阈值:%2%").arg(scorePct, 0, 'f', 1).arg(minScore * 100.0, 0, 'f', 0);
        return res;
    }

    // 绘制精确旋转包围盒 + 亚像素十字中心 + 角度方向矢量
    cv::RotatedRect rr(cv::Point2f(static_cast<float>(centerX), static_cast<float>(centerY)),
                       cv::Size2f(static_cast<float>(model.width), static_cast<float>(model.height)),
                       static_cast<float>(-bestAngle));
    cv::Point2f pts[4];
    rr.points(pts);
    std::vector<cv::Point> poly;
    for (int i = 0; i < 4; ++i) {
        poly.emplace_back(static_cast<int>(std::lround(pts[i].x)), static_cast<int>(std::lround(pts[i].y)));
    }
    cv::polylines(vis, poly, true, cv::Scalar(46, 204, 113), ds.thick + 1, cv::LINE_AA);

    cv::Point cPt(static_cast<int>(std::lround(centerX)), static_cast<int>(std::lround(centerY)));
    cv::drawMarker(vis, cPt, cv::Scalar(60, 76, 231), cv::MARKER_CROSS, ds.markerSize, ds.thick);

    double rad = -bestAngle * CV_PI / 180.0;
    double arrowLen = std::min(model.width, model.height) * 0.42;
    cv::Point tipPt(static_cast<int>(std::lround(centerX + arrowLen * std::cos(rad))),
                    static_cast<int>(std::lround(centerY + arrowLen * std::sin(rad))));
    cv::arrowedLine(vis, cPt, tipPt, cv::Scalar(0, 215, 255), ds.thick, cv::LINE_AA, 0, 0.25);

    std::string hud = cv::format("HiMatch %.1f%% (%.2f,%.2f) A:%.1f", scorePct, centerX, centerY, bestAngle);
    cv::putText(vis, hud, cv::Point(std::max(10, poly[1].x), std::max(static_cast<int>(28 * ds.scale), poly[1].y - static_cast<int>(10 * ds.scale))),
                cv::FONT_HERSHEY_SIMPLEX, ds.fontScale, cv::Scalar(46, 204, 113), ds.thick, cv::LINE_AA);

    res.ok = 1;
    res.x = std::round(centerX * 100.0) / 100.0;
    res.y = std::round(centerY * 100.0) / 100.0;
    res.extra = std::round(bestAngle * 100.0) / 100.0;
    res.count = 1;
    res.summary = QString("%1% %2°").arg(scorePct, 0, 'f', 1).arg(bestAngle, 0, 'f', 1);
    res.msg = QString("高精度匹配OK 得分:%1% 亚像素中心:(%2, %3) 角度:%4°")
                  .arg(scorePct, 0, 'f', 1).arg(res.x, 0, 'f', 2).arg(res.y, 0, 'f', 2).arg(res.extra, 0, 'f', 2);
    return res;
}

// ==================== 单个算子执行分派 ====================
static StepResult executeSingleOperator(const FlowNodeData& node, ExecContext& ctx, cv::Mat& outVis) {
    StepResult res;
    const QString& t = node.typeKey;
    const QVariantMap& p = node.params;

    if (t == "image_source" || t == "sim_source") {
        cv::Mat img = !ctx.streamFrame.empty() ? ctx.streamFrame : ctx.sourceImage;
        if (img.empty()) {
            res.msg = "未加载图像，请打开本地图片文件夹或启动连续运行";
            res.summary = "无图像";
            return res;
        }
        cv::Mat bgr = ensureBgr(img);
        ctx.rawImage = bgr.clone();
        ctx.image = bgr.clone();
        ctx.grayImage.release();
        outVis = bgr;
        res.ok = 1; res.x = bgr.cols; res.y = bgr.rows; res.count = 1;
        res.text = ctx.sourceName;
        res.summary = QString("%1x%2").arg(bgr.cols).arg(bgr.rows);
        res.msg = QString("图像输入:%1 (%2×%3)").arg(ctx.sourceName).arg(bgr.cols).arg(bgr.rows);
        return res;
    }

    if (t == "camera_source") {
        cv::Mat frame = !ctx.streamFrame.empty() ? ctx.streamFrame
                       : (ctx.camCtrl ? ctx.camCtrl->grabFrame() : ctx.sourceImage);
        if (frame.empty()) {
            res.msg = "相机取图失败，请检查USB连接或分辨率";
            res.summary = "取图失败";
            return res;
        }
        cv::Mat bgr = ensureBgr(frame);
        ctx.rawImage = bgr.clone();
        ctx.image = bgr.clone();
        ctx.grayImage.release();
        outVis = bgr;
        res.ok = 1; res.x = bgr.cols; res.y = bgr.rows; res.count = 1;
        res.summary = QString("%1x%2").arg(bgr.cols).arg(bgr.rows);
        res.msg = QString("相机采集成功 (%1×%2)").arg(bgr.cols).arg(bgr.rows);
        return res;
    }

    if (ctx.image.empty()) {
        outVis = ctx.displayImage;
        res.msg = "上游无输入图像";
        res.summary = "无输入";
        return res;
    }

    if (t == "color_convert") {
        QString mode = p.value("mode", "彩色转灰度 (Gray)").toString();
        double clip = p.value("clahe_clip", 2.5).toDouble();
        cv::Mat bgr = ensureBgr(ctx.image);
        cv::Mat gray;
        if (mode.startsWith("CLAHE")) {
            cv::Mat g0;
            cv::cvtColor(bgr, g0, cv::COLOR_BGR2GRAY);
            cv::createCLAHE(std::max(0.5, clip), cv::Size(8, 8))->apply(g0, gray);
        } else if (mode.contains("R 通道")) {
            std::vector<cv::Mat> ch; cv::split(bgr, ch); gray = ch[2];
        } else if (mode.contains("G 通道")) {
            std::vector<cv::Mat> ch; cv::split(bgr, ch); gray = ch[1];
        } else if (mode.contains("B 通道")) {
            std::vector<cv::Mat> ch; cv::split(bgr, ch); gray = ch[0];
        } else if (mode.contains("HSV-H")) {
            cv::Mat hsv; cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch; cv::split(hsv, ch); gray = ch[0];
        } else if (mode.contains("HSV-S")) {
            cv::Mat hsv; cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch; cv::split(hsv, ch); gray = ch[1];
        } else if (mode.contains("HSV-V")) {
            cv::Mat hsv; cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch; cv::split(hsv, ch); gray = ch[2];
        } else {
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        }

        cv::Mat outBgr;
        cv::cvtColor(gray, outBgr, cv::COLOR_GRAY2BGR);
        ctx.grayImage = gray.clone();
        ctx.image = outBgr.clone();
        if (p.value("use_as_display", true).toBool()) ctx.rawImage = outBgr.clone();
        outVis = outBgr;
        int meanG = static_cast<int>(std::lround(cv::mean(gray)[0]));
        res.ok = 1; res.x = gray.cols; res.y = gray.rows; res.extra = meanG; res.count = 1;
        res.summary = QString("灰度 均:%1").arg(meanG);
        res.msg = QString("颜色转换[%1]完成 平均灰度:%2").arg(mode).arg(meanG);
        return res;
    }

    if (t == "img_enhance") {
        double gamma = std::max(0.25, std::min(4.0, p.value("gamma", 1.5).toDouble()));
        double alpha = std::max(0.2, std::min(3.0, p.value("contrast", 1.0).toDouble()));
        int beta = p.value("brightness", 0).toInt();
        cv::Mat out = ensureBgr(ctx.image);
        if (std::abs(gamma - 1.0) > 1e-3) {
            double invG = 1.0 / gamma;
            cv::Mat lut(1, 256, CV_8U);
            uchar* ptr = lut.ptr();
            for (int i = 0; i < 256; ++i) ptr[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, invG) * 255.0);
            cv::LUT(out, lut, out);
        }
        if (std::abs(alpha - 1.0) > 1e-3 || beta != 0) out.convertTo(out, -1, alpha, beta);
        if (p.value("auto_clahe", false).toBool()) {
            cv::Mat hsv; cv::cvtColor(out, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch; cv::split(hsv, ch);
            cv::createCLAHE(2.2, cv::Size(8, 8))->apply(ch[2], ch[2]);
            cv::merge(ch, hsv); cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
        }
        ctx.image = out.clone();
        ctx.rawImage = out.clone();
        if (!ctx.grayImage.empty()) ctx.grayImage = toGray(out);
        outVis = out;
        int m = static_cast<int>(std::lround(cv::mean(toGray(out))[0]));
        res.ok = 1; res.extra = m; res.count = 1;
        res.summary = QString("G:%1 均:%2").arg(gamma, 0, 'f', 1).arg(m);
        res.msg = QString("亮度增强完成 Gamma:%1 均值:%2").arg(gamma, 0, 'f', 2).arg(m);
        return res;
    }

    if (t == "img_filter") {
        QString m = p.value("method", "高斯滤波").toString();
        int k = p.value("ksize", 5).toInt();
        if (k % 2 == 0) k += 1;
        k = std::max(3, std::min(31, k));
        cv::Mat out;
        if (m == "中值滤波") cv::medianBlur(ctx.image, out, k);
        else if (m == "均值滤波") cv::blur(ctx.image, out, cv::Size(k, k));
        else if (m == "双边滤波") cv::bilateralFilter(ctx.image, out, k, 75, 75);
        else if (m == "锐化增强") {
            cv::Mat b; cv::GaussianBlur(ctx.image, b, cv::Size(k, k), 0);
            cv::addWeighted(ctx.image, 1.5, b, -0.5, 0, out);
        } else cv::GaussianBlur(ctx.image, out, cv::Size(k, k), 0);
        ctx.image = out.clone();
        if (!ctx.grayImage.empty()) ctx.grayImage = toGray(out);
        outVis = out;
        res.ok = 1; res.extra = k; res.count = 1;
        res.summary = QString("%1 %2x%2").arg(m).arg(k);
        res.msg = QString("%1完成 (核:%2×%2)").arg(m).arg(k);
        return res;
    }

    if (t == "img_binary") {
        cv::Mat g = !ctx.grayImage.empty() ? ctx.grayImage : toGray(ctx.image);
        int th = p.value("thresh", 120).toInt();
        bool otsu = p.value("use_otsu", false).toBool();
        bool inv = p.value("invert", false).toBool();
        int flag = inv ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
        if (otsu) flag |= cv::THRESH_OTSU;
        cv::Mat bin;
        double actual = cv::threshold(g, bin, th, 255, flag);
        cv::Mat out; cv::cvtColor(bin, out, cv::COLOR_GRAY2BGR);
        ctx.image = out.clone();
        ctx.grayImage = bin.clone();
        outVis = out;
        res.ok = 1; res.extra = actual; res.count = 1;
        res.summary = QString("T=%1").arg(static_cast<int>(actual));
        res.msg = QString("二值化完成 实际阈值:%1").arg(static_cast<int>(actual));
        return res;
    }

    if (t == "img_morph") {
        QString op = p.value("op", "开运算(去噪)").toString();
        int k = std::max(3, std::min(31, p.value("ksize", 5).toInt()));
        int iters = std::max(1, std::min(10, p.value("iterations", 1).toInt()));
        cv::Mat ker = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));
        cv::Mat out;
        if (op == "腐蚀") cv::erode(ctx.image, out, ker, cv::Point(-1, -1), iters);
        else if (op == "膨胀") cv::dilate(ctx.image, out, ker, cv::Point(-1, -1), iters);
        else if (op.startsWith("闭运算")) cv::morphologyEx(ctx.image, out, cv::MORPH_CLOSE, ker, cv::Point(-1, -1), iters);
        else if (op.startsWith("顶帽")) cv::morphologyEx(ctx.image, out, cv::MORPH_TOPHAT, ker, cv::Point(-1, -1), iters);
        else if (op.startsWith("黑帽")) cv::morphologyEx(ctx.image, out, cv::MORPH_BLACKHAT, ker, cv::Point(-1, -1), iters);
        else cv::morphologyEx(ctx.image, out, cv::MORPH_OPEN, ker, cv::Point(-1, -1), iters);
        ctx.image = out.clone();
        if (!ctx.grayImage.empty()) ctx.grayImage = toGray(out);
        outVis = out;
        res.ok = 1; res.extra = k; res.count = 1;
        res.summary = QString("%1 K=%2").arg(op.left(3)).arg(k);
        res.msg = QString("形态学[%1]完成 核:%2").arg(op).arg(k);
        return res;
    }

    if (t == "img_transform") {
        QString mode = p.value("mode", "顺时针90度").toString();
        cv::Mat out;
        if (mode == "逆时针90度") cv::rotate(ctx.image, out, cv::ROTATE_90_COUNTERCLOCKWISE);
        else if (mode == "旋转180度") cv::rotate(ctx.image, out, cv::ROTATE_180);
        else if (mode == "水平镜像") cv::flip(ctx.image, out, 1);
        else if (mode == "垂直镜像") cv::flip(ctx.image, out, 0);
        else cv::rotate(ctx.image, out, cv::ROTATE_90_CLOCKWISE);
        ctx.image = out.clone();
        ctx.rawImage = out.clone();
        if (!ctx.grayImage.empty()) ctx.grayImage = toGray(out);
        outVis = out;
        res.ok = 1; res.x = out.cols; res.y = out.rows; res.count = 1;
        res.summary = mode;
        res.msg = QString("图像变换[%1]完成 (%2×%3)").arg(mode).arg(out.cols).arg(out.rows);
        return res;
    }

    if (t == "high_prec_match") {
        return runHighPrecisionTemplateMatch(node, ctx, outVis);
    }

    if (t == "locate_template") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect sroiR; bool hasSroi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, sroiR, hasSroi);
        drawRoiBox(vis, sroiR, hasSroi);
        outVis = vis;

        if (!node.learnedModel.isLearned || node.learnedModel.grayPatch.empty()) {
            res.summary = "未学习模板"; res.msg = "请先在图像上框选区域并点击「学习模板」";
            return res;
        }
        cv::Mat gImg = toGray(sub);
        const cv::Mat& gTpl = node.learnedModel.grayPatch;
        if (gTpl.rows > gImg.rows || gTpl.cols > gImg.cols) {
            res.summary = "尺寸超限"; res.msg = "模板尺寸大于搜索区域";
            return res;
        }
        double thresh = p.value("score_thresh", 0.75).toDouble();
        cv::Mat matchRes;
        cv::matchTemplate(gImg, gTpl, matchRes, cv::TM_CCOEFF_NORMED);
        double maxVal = 0; cv::Point maxLoc;
        cv::minMaxLoc(matchRes, nullptr, &maxVal, nullptr, &maxLoc);
        int scorePct = static_cast<int>(std::lround(maxVal * 100.0));
        if (maxVal < thresh) {
            res.extra = scorePct; res.summary = QString("NG %1%").arg(scorePct);
            res.msg = QString("快速灰度匹配NG 得分:%1% < 阈值:%2%").arg(scorePct).arg(static_cast<int>(thresh * 100));
            return res;
        }
        cv::Rect box(maxLoc.x + ox, maxLoc.y + oy, gTpl.cols, gTpl.rows);
        int cx = box.x + box.width / 2, cy = box.y + box.height / 2;
        cv::rectangle(vis, box, cv::Scalar(46, 204, 113), ds.thick + 1);
        cv::drawMarker(vis, cv::Point(cx, cy), cv::Scalar(60, 76, 231), cv::MARKER_CROSS, ds.markerSize, ds.thick);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = scorePct; res.count = 1;
        res.summary = QString("(%1,%2) %3%").arg(cx).arg(cy).arg(scorePct);
        res.msg = QString("快速灰度匹配OK 得分:%1% 中心:(%2,%3)").arg(scorePct).arg(cx).arg(cy);
        return res;
    }

    if (t == "locate_shape") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect sroiR; bool hasSroi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, sroiR, hasSroi);
        drawRoiBox(vis, sroiR, hasSroi);
        outVis = vis;

        if (!node.learnedModel.isLearned || node.learnedModel.grayPatch.empty()) {
            res.summary = "未学习模板"; res.msg = "请先在图像上框选区域并点击「学习模板」";
            return res;
        }
        int th = p.value("thresh", 120).toInt();
        bool inv = p.value("invert", false).toBool();
        int minScore = p.value("min_score", 70).toInt();

        cv::Mat tplBin, imgBin;
        cv::threshold(node.learnedModel.grayPatch, tplBin, th, 255, inv ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);
        cv::threshold(toGray(sub), imgBin, th, 255, inv ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> tCnts, iCnts;
        cv::findContours(tplBin, tCnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        cv::findContours(imgBin, iCnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (tCnts.empty() || iCnts.empty()) { res.summary = "无轮廓"; res.msg = "未提取到有效形状轮廓"; return res; }

        auto maxIt = std::max_element(tCnts.begin(), tCnts.end(), [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
        double bestDist = 1e9; int bestIdx = -1;
        for (int i = 0; i < static_cast<int>(iCnts.size()); ++i) {
            if (cv::contourArea(iCnts[i]) < 200) continue;
            double d = cv::matchShapes(*maxIt, iCnts[i], cv::CONTOURS_MATCH_I1, 0.0);
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        int simPct = (bestIdx >= 0) ? std::max(0, std::min(100, static_cast<int>(std::lround((1.0 - std::min(1.0, bestDist)) * 100.0)))) : 0;
        if (bestIdx < 0 || simPct < minScore) {
            res.extra = simPct; res.summary = QString("NG %1%").arg(simPct);
            res.msg = QString("轮廓形状匹配NG 相似度:%1% < %2%").arg(simPct).arg(minScore);
            return res;
        }
        cv::Moments m = cv::moments(iCnts[bestIdx]);
        int cx = (m.m00 > 1e-5 ? static_cast<int>(std::lround(m.m10 / m.m00)) : 0) + ox;
        int cy = (m.m00 > 1e-5 ? static_cast<int>(std::lround(m.m01 / m.m00)) : 0) + oy;
        std::vector<cv::Point> shifted;
        for (const auto& pt : iCnts[bestIdx]) shifted.emplace_back(pt.x + ox, pt.y + oy);
        cv::polylines(vis, shifted, true, cv::Scalar(46, 204, 113), ds.thick + 1);
        cv::drawMarker(vis, cv::Point(cx, cy), cv::Scalar(60, 76, 231), cv::MARKER_CROSS, ds.markerSize, ds.thick);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = simPct; res.count = 1;
        res.summary = QString("(%1,%2) %3%").arg(cx).arg(cy).arg(simPct);
        res.msg = QString("轮廓形状匹配OK 相似度:%1% 中心:(%2,%3)").arg(simPct).arg(cx).arg(cy);
        return res;
    }

    if (t == "locate_blob") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");

        cv::Mat g = toGray(sub);
        int th = p.value("thresh", 120).toInt();
        bool inv = p.value("invert", false).toBool();
        double minA = p.value("min_area", 500).toDouble();
        double maxA = p.value("max_area", 8000000).toDouble();
        cv::Mat bin;
        cv::threshold(g, bin, th, 255, inv ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        struct BlobItem { double area; cv::RotatedRect rr; };
        std::vector<BlobItem> valid;
        for (const auto& c : contours) {
            double a = cv::contourArea(c);
            if (a >= minA && a <= maxA) valid.push_back({a, cv::minAreaRect(c)});
        }
        outVis = vis;
        if (valid.empty()) {
            res.summary = "未找目标"; res.msg = "未检测到符合面积条件的斑点轮廓";
            return res;
        }
        std::sort(valid.begin(), valid.end(), [](const BlobItem& a, const BlobItem& b) { return a.area > b.area; });
        for (size_t i = 0; i < valid.size(); ++i) {
            cv::Point2f pts[4]; valid[i].rr.points(pts);
            std::vector<cv::Point> poly;
            for (int k = 0; k < 4; ++k) poly.emplace_back(static_cast<int>(pts[k].x) + ox, static_cast<int>(pts[k].y) + oy);
            cv::Scalar col = (i == 0) ? cv::Scalar(46, 204, 113) : cv::Scalar(243, 156, 18);
            cv::polylines(vis, poly, true, col, ds.thick);
        }
        int cx = static_cast<int>(std::lround(valid[0].rr.center.x)) + ox;
        int cy = static_cast<int>(std::lround(valid[0].rr.center.y)) + oy;
        int ang = static_cast<int>(std::lround(valid[0].rr.angle));
        cv::drawMarker(vis, cv::Point(cx, cy), cv::Scalar(60, 76, 231), cv::MARKER_CROSS, ds.markerSize, ds.thick);
        cv::putText(vis, cv::format("Blob(%d,%d) A:%d N:%d", cx, cy, ang, static_cast<int>(valid.size())),
                    cv::Point(cx + static_cast<int>(10 * ds.scale), std::max(static_cast<int>(26 * ds.scale), cy - static_cast<int>(10 * ds.scale))),
                    cv::FONT_HERSHEY_SIMPLEX, ds.fontScale, cv::Scalar(46, 204, 113), ds.thick, cv::LINE_AA);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = ang; res.count = static_cast<int>(valid.size());
        res.summary = QString("(%1,%2) %3°").arg(cx).arg(cy).arg(ang);
        res.msg = QString("目标数:%1 最大面积:%2 中心:(%3,%4) 角度:%5°").arg(valid.size()).arg(static_cast<long long>(valid[0].area)).arg(cx).arg(cy).arg(ang);
        return res;
    }

    if (t == "locate_circle") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;

        cv::Mat g; cv::medianBlur(toGray(sub), g, 5);
        int minR = p.value("min_r", 15).toInt();
        int maxR = p.value("max_r", 300).toInt();
        int p2 = p.value("param2", 35).toInt();
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(g, circles, cv::HOUGH_GRADIENT, 1.2, std::max(20.0, 30.0 * ds.scale), 100, p2, minR, maxR);
        if (circles.empty()) {
            res.summary = "未找到圆"; res.msg = "未检测到圆形目标";
            return res;
        }
        for (size_t i = 0; i < circles.size(); ++i) {
            int gx = static_cast<int>(std::lround(circles[i][0])) + ox;
            int gy = static_cast<int>(std::lround(circles[i][1])) + oy;
            int r = static_cast<int>(std::lround(circles[i][2]));
            cv::Scalar col = (i == 0) ? cv::Scalar(46, 204, 113) : cv::Scalar(243, 156, 18);
            cv::circle(vis, cv::Point(gx, gy), r, col, ds.thick);
            cv::circle(vis, cv::Point(gx, gy), std::max(3, static_cast<int>(3 * ds.scale)), cv::Scalar(60, 76, 231), -1);
        }
        int cx = static_cast<int>(std::lround(circles[0][0])) + ox;
        int cy = static_cast<int>(std::lround(circles[0][1])) + oy;
        int r = static_cast<int>(std::lround(circles[0][2]));
        cv::putText(vis, cv::format("C(%d,%d) R:%d", cx, cy, r),
                    cv::Point(cx + static_cast<int>(10 * ds.scale), std::max(static_cast<int>(26 * ds.scale), cy - static_cast<int>(10 * ds.scale))),
                    cv::FONT_HERSHEY_SIMPLEX, ds.fontScale, cv::Scalar(46, 204, 113), ds.thick, cv::LINE_AA);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = r; res.count = static_cast<int>(circles.size());
        res.summary = QString("(%1,%2) R=%3").arg(cx).arg(cy).arg(r);
        res.msg = QString("找到 %1 个圆，主圆心:(%2,%3) 半径:%4px").arg(circles.size()).arg(cx).arg(cy).arg(r);
        return res;
    }

    if (t == "locate_line") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;

        cv::Mat edges;
        cv::Canny(toGray(sub), edges, p.value("canny_low", 50).toInt(), p.value("canny_high", 150).toInt());
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180.0, 50, p.value("min_length", 60).toInt(), 15);
        if (lines.empty()) {
            res.summary = "无直线"; res.msg = "未查找到有效直线边缘";
            return res;
        }
        double bestLen = -1.0;
        cv::Vec4i bestL;
        for (const auto& l : lines) {
            cv::Point p1(l[0] + ox, l[1] + oy), p2(l[2] + ox, l[3] + oy);
            cv::line(vis, p1, p2, cv::Scalar(243, 156, 18), std::max(1, ds.thick - 1));
            double len = std::hypot(p2.x - p1.x, p2.y - p1.y);
            if (len > bestLen) { bestLen = len; bestL = cv::Vec4i(p1.x, p1.y, p2.x, p2.y); }
        }
        int cx = (bestL[0] + bestL[2]) / 2;
        int cy = (bestL[1] + bestL[3]) / 2;
        int ang = static_cast<int>(std::lround(std::atan2(bestL[3] - bestL[1], bestL[2] - bestL[0]) * 180.0 / CV_PI));
        cv::line(vis, cv::Point(bestL[0], bestL[1]), cv::Point(bestL[2], bestL[3]), cv::Scalar(46, 204, 113), ds.thick + 1);
        cv::drawMarker(vis, cv::Point(cx, cy), cv::Scalar(60, 76, 231), cv::MARKER_CROSS, ds.markerSize, ds.thick);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = ang; res.count = static_cast<int>(lines.size());
        res.summary = QString("L=%1 %2°").arg(static_cast<int>(bestLen)).arg(ang);
        res.msg = QString("直线数:%1 主线长:%2px 中点:(%3,%4) 倾角:%5°").arg(lines.size()).arg(static_cast<int>(bestLen)).arg(cx).arg(cy).arg(ang);
        return res;
    }

    if (t == "measure_dist") {
        cv::Mat vis = (!ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage).clone();
        outVis = vis;
        if (!ctx.lastResult.ok) { res.summary = "无上游坐标"; res.msg = "上游无有效定位坐标"; return res; }
        DrawStyle ds = getDrawStyle(vis);
        int tx = static_cast<int>(std::lround(ctx.lastResult.x));
        int ty = static_cast<int>(std::lround(ctx.lastResult.y));
        int rx = p.value("ref_x", 320).toInt();
        int ry = p.value("ref_y", 240).toInt();
        double mmPerPx = p.value("mm_per_px", 1.0).toDouble();
        double dReal = std::hypot(tx - rx, ty - ry) * mmPerPx;
        cv::circle(vis, cv::Point(rx, ry), static_cast<int>(6 * ds.scale), cv::Scalar(255, 200, 0), ds.thick);
        cv::line(vis, cv::Point(rx, ry), cv::Point(tx, ty), cv::Scalar(0, 220, 255), ds.thick);
        res.ok = 1; res.x = tx - rx; res.y = ty - ry; res.extra = std::round(dReal * 100.0) / 100.0; res.count = 1;
        res.summary = QString("D=%1").arg(dReal, 0, 'f', 1);
        res.msg = QString("距基准(%1,%2) 偏移:(%3,%4) 距离:%5").arg(rx).arg(ry).arg(tx - rx).arg(ty - ry).arg(dReal, 0, 'f', 2);
        return res;
    }

    if (t == "measure_cgap") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;
        cv::Mat g; cv::medianBlur(toGray(sub), g, 5);
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(g, circles, cv::HOUGH_GRADIENT, 1.2, 30, 100, p.value("param2", 35).toInt(),
                         p.value("min_r", 15).toInt(), p.value("max_r", 300).toInt());
        if (circles.size() < 2) { res.summary = "不足2圆"; res.msg = "双圆心距测量需要检测到至少 2 个圆"; return res; }
        cv::Point c1(static_cast<int>(circles[0][0]) + ox, static_cast<int>(circles[0][1]) + oy);
        cv::Point c2(static_cast<int>(circles[1][0]) + ox, static_cast<int>(circles[1][1]) + oy);
        double d = std::hypot(c2.x - c1.x, c2.y - c1.y) * p.value("mm_per_px", 1.0).toDouble();
        cv::circle(vis, c1, static_cast<int>(circles[0][2]), cv::Scalar(46, 204, 113), ds.thick);
        cv::circle(vis, c2, static_cast<int>(circles[1][2]), cv::Scalar(46, 204, 113), ds.thick);
        cv::line(vis, c1, c2, cv::Scalar(0, 220, 255), ds.thick);
        res.ok = 1; res.x = (c1.x + c2.x) / 2; res.y = (c1.y + c2.y) / 2; res.extra = std::round(d * 100.0) / 100.0; res.count = 2;
        res.summary = QString("间距=%1").arg(d, 0, 'f', 1);
        res.msg = QString("双圆心距:%1 中点:(%2,%3)").arg(d, 0, 'f', 2).arg(static_cast<int>(res.x)).arg(static_cast<int>(res.y));
        return res;
    }

    if (t == "measure_bright") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;

        cv::Scalar m, sd;
        cv::meanStdDev(toGray(sub), m, sd);
        double minB = p.value("min_bright", 60).toDouble();
        double maxB = p.value("max_bright", 220).toDouble();
        bool ok = (m[0] >= minB && m[0] <= maxB);
        cv::putText(vis, cv::format("Mean:%.1f Std:%.1f [%s]", m[0], sd[0], ok ? "OK" : "NG"),
                    cv::Point(ox + static_cast<int>(10 * ds.scale), std::max(static_cast<int>(28 * ds.scale), oy + static_cast<int>(28 * ds.scale))),
                    cv::FONT_HERSHEY_SIMPLEX, ds.fontScale, ok ? cv::Scalar(46, 204, 113) : cv::Scalar(60, 76, 231), ds.thick, cv::LINE_AA);
        res.ok = ok ? 1 : 0; res.x = std::round(m[0] * 10) / 10.0; res.y = std::round(sd[0] * 10) / 10.0;
        res.extra = static_cast<int>(std::lround(m[0])); res.count = ok ? 1 : 0;
        res.summary = QString("亮度:%1").arg(m[0], 0, 'f', 1);
        res.msg = QString("ROI平均亮度:%1 标准差:%2 范围[%3,%4]").arg(m[0], 0, 'f', 1).arg(sd[0], 0, 'f', 1).arg(minB).arg(maxB);
        return res;
    }

    if (t == "measure_area") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;
        cv::Mat bin;
        cv::threshold(toGray(sub), bin, p.value("thresh", 120).toInt(), 255,
                      p.value("invert", false).toBool() ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);
        int nz = cv::countNonZero(bin);
        int minA = p.value("min_area", 500).toInt();
        bool ok = nz >= minA;
        res.ok = ok ? 1 : 0; res.x = nz; res.y = static_cast<int>(std::lround(nz * 100.0 / std::max(1, bin.rows * bin.cols)));
        res.extra = nz; res.count = ok ? 1 : 0;
        res.summary = QString("面积:%1px").arg(nz);
        res.msg = QString("前景像素面积:%1 px (占比 %2%)").arg(nz).arg(static_cast<int>(res.y));
        return res;
    }

    if (t == "detect_color") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;

        double minA = p.value("min_area", 800).toDouble();
        cv::Mat hsv; cv::cvtColor(ensureBgr(sub), hsv, cv::COLOR_BGR2HSV);
        struct ColorDef { int code; const char* name; cv::Scalar l1, u1, l2, u2; bool two; cv::Scalar draw; };
        ColorDef defs[] = {
            {1, "Red",    {0, 100, 80},   {10, 255, 255},  {170, 100, 80}, {180, 255, 255}, true,  {60, 76, 231}},
            {2, "Blue",   {100, 100, 80}, {130, 255, 255}, {},             {},              false, {235, 136, 30}},
            {3, "Green",  {35, 80, 70},   {85, 255, 255},  {},             {},              false, {113, 204, 46}},
            {4, "Yellow", {20, 100, 100}, {34, 255, 255},  {},             {},              false, {18, 200, 240}},
        };
        double bestArea = -1.0; int bestIdx = -1; cv::Rect bestBox;
        for (int i = 0; i < 4; ++i) {
            cv::Mat m; cv::inRange(hsv, defs[i].l1, defs[i].u1, m);
            if (defs[i].two) { cv::Mat m2; cv::inRange(hsv, defs[i].l2, defs[i].u2, m2); cv::bitwise_or(m, m2, m); }
            cv::morphologyEx(m, m, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
            std::vector<std::vector<cv::Point>> cnts;
            cv::findContours(m, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            for (const auto& c : cnts) {
                double a = cv::contourArea(c);
                if (a >= minA && a > bestArea) { bestArea = a; bestIdx = i; bestBox = cv::boundingRect(c); }
            }
        }
        if (bestIdx < 0) { res.summary = "无匹配色"; res.msg = "未识别到设定颜色"; return res; }
        cv::Rect gb(bestBox.x + ox, bestBox.y + oy, bestBox.width, bestBox.height);
        int cx = gb.x + gb.width / 2, cy = gb.y + gb.height / 2;
        cv::rectangle(vis, gb, defs[bestIdx].draw, ds.thick + 1);
        cv::drawMarker(vis, cv::Point(cx, cy), defs[bestIdx].draw, cv::MARKER_CROSS, ds.markerSize, ds.thick);
        cv::putText(vis, cv::format("%s(ID:%d) (%d,%d)", defs[bestIdx].name, defs[bestIdx].code, cx, cy),
                    cv::Point(gb.x, std::max(static_cast<int>(26 * ds.scale), gb.y - static_cast<int>(8 * ds.scale))),
                    cv::FONT_HERSHEY_SIMPLEX, ds.fontScale, defs[bestIdx].draw, ds.thick, cv::LINE_AA);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = defs[bestIdx].code; res.count = 1;
        res.text = defs[bestIdx].name;
        res.summary = QString("%1 ID:%2").arg(defs[bestIdx].name).arg(defs[bestIdx].code);
        res.msg = QString("识别颜色:%1(代码%2) 面积:%3 中心:(%4,%5)").arg(defs[bestIdx].name).arg(defs[bestIdx].code).arg(static_cast<long long>(bestArea)).arg(cx).arg(cy);
        return res;
    }

    if (t == "detect_qrcode") {
        cv::Mat vis = (!ctx.rawImage.empty() ? ctx.rawImage : ctx.image).clone();
        DrawStyle ds = getDrawStyle(vis);
        int ox = 0, oy = 0; cv::Rect roiR; bool hasRoi = false;
        cv::Mat sub = extractSearchRoi(ctx.image, p, ox, oy, roiR, hasRoi);
        drawRoiBox(vis, roiR, hasRoi, "ROI");
        outVis = vis;

        cv::QRCodeDetector det;
        std::vector<cv::Point> pts;
        std::string txt = det.detectAndDecode(sub, pts);
        if (txt.empty() || pts.size() < 4) { res.summary = "无二维码"; res.msg = "未识别到二维码"; return res; }
        int sx = 0, sy = 0;
        for (auto& pt : pts) { pt.x += ox; pt.y += oy; sx += pt.x; sy += pt.y; }
        cv::polylines(vis, pts, true, cv::Scalar(46, 204, 113), ds.thick + 1);
        int cx = sx / static_cast<int>(pts.size()), cy = sy / static_cast<int>(pts.size());
        QString qtxt = QString::fromStdString(txt);
        res.ok = 1; res.x = cx; res.y = cy; res.extra = qtxt.length(); res.count = 1; res.text = qtxt;
        res.summary = QString("QR:%1").arg(qtxt.left(10));
        res.msg = QString("二维码:%1 中心:(%2,%3)").arg(qtxt).arg(cx).arg(cy);
        return res;
    }

    if (t == "calib_9pt") {
        outVis = !ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage;
        if (!ctx.lastResult.ok) { res.summary = "无上游坐标"; res.msg = "上游无有效像素坐标可转换"; return res; }
        cv::Mat M; double rmse = 0.0;
        if (!VisionOperatorRegistry::solveNinePointAffine(node.calibPoints, M, rmse)) {
            res.summary = "标定失效"; res.msg = "九点标定矩阵求解失败";
            return res;
        }
        double px = ctx.lastResult.x, py = ctx.lastResult.y;
        double wx = M.at<double>(0, 0) * px + M.at<double>(0, 1) * py + M.at<double>(0, 2);
        double wy = M.at<double>(1, 0) * px + M.at<double>(1, 1) * py + M.at<double>(1, 2);
        res.ok = 1; res.x = std::round(wx * 1000.0) / 1000.0; res.y = std::round(wy * 1000.0) / 1000.0;
        res.extra = ctx.lastResult.extra; res.count = node.calibPoints.size();
        res.summary = QString("(%1,%2)mm").arg(wx, 0, 'f', 1).arg(wy, 0, 'f', 1);
        res.msg = QString("手眼标定 (%1,%2)px → (%3,%4)mm (RMSE:%5mm)").arg(px, 0, 'f', 1).arg(py, 0, 'f', 1).arg(wx, 0, 'f', 2).arg(wy, 0, 'f', 2).arg(rmse, 0, 'f', 3);
        return res;
    }

    if (t == "logic_judge") {
        outVis = !ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage;
        QString f = p.value("field", "x").toString();
        double minV = p.value("min_val", 0.0).toDouble();
        double maxV = p.value("max_val", 3840.0).toDouble();
        bool reqOk = p.value("require_prev_ok", true).toBool();
        double v = (f == "y") ? ctx.lastResult.y : (f == "extra" ? ctx.lastResult.extra : (f == "count" ? ctx.lastResult.count : ctx.lastResult.x));
        bool ok = (v >= minV && v <= maxV) && (!reqOk || ctx.lastResult.ok != 0);
        res.ok = ok ? 1 : 0; res.x = ctx.lastResult.x; res.y = ctx.lastResult.y;
        res.extra = ctx.lastResult.extra; res.count = ctx.lastResult.count; res.text = ctx.lastResult.text;
        res.summary = QString("%1 %2=%3").arg(ok ? "OK" : "NG").arg(f).arg(v);
        res.msg = QString("公差判定[%1] %2=%3 范围[%4,%5]").arg(ok ? "OK" : "NG").arg(f).arg(v).arg(minV).arg(maxV);
        return res;
    }

    if (t == "logic_branch") {
        outVis = !ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage;
        QString f = p.value("field", "extra").toString();
        QString op = p.value("operator", "==").toString();
        double tgt = p.value("target_val", 1.0).toDouble();
        double v = (f == "x") ? ctx.lastResult.x : ((f == "y") ? ctx.lastResult.y : ((f == "count") ? ctx.lastResult.count : ((f == "ok") ? ctx.lastResult.ok : ctx.lastResult.extra)));
        bool m = false;
        if (op == "==") m = std::abs(v - tgt) < 1e-6;
        else if (op == "!=") m = std::abs(v - tgt) >= 1e-6;
        else if (op == ">") m = v > tgt;
        else if (op == ">=") m = v >= tgt;
        else if (op == "<") m = v < tgt;
        else m = v <= tgt;
        res.ok = m ? 1 : 0; res.x = ctx.lastResult.x; res.y = ctx.lastResult.y;
        res.extra = ctx.lastResult.extra; res.count = ctx.lastResult.count; res.text = ctx.lastResult.text;
        res.summary = QString("%1%2%3:%4").arg(f).arg(op).arg(tgt).arg(m ? "OK" : "NG");
        res.msg = QString("条件分支 %1(%2) %3 %4 → %5").arg(f).arg(v).arg(op).arg(tgt).arg(m ? "走OK分支" : "走NG分支");
        return res;
    }

    if (t == "io_save_image") {
        outVis = !ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage;
        if (!ctx.allowSaveIo) {
            res.ok = 1; res.summary = "存图节流"; res.msg = "连续运行防刷盘节流中";
            return res;
        }
        QString dir = p.value("save_dir", "").toString().trimmed();
        if (dir.isEmpty()) dir = QDir::currentPath() + "/saved_images";
        QString prefix = p.value("prefix", "NG").toString();
        bool rawFlag = p.value("save_raw", false).toBool();
        cv::Mat target = (rawFlag && !ctx.rawImage.empty()) ? ctx.rawImage : outVis;
        QString fn = QString("%1/%2_%3.png").arg(dir, prefix, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
        bool ok = imwriteUnicodeCpp(fn, target);
        res.ok = ok ? 1 : 0; res.count = ok ? 1 : 0; res.text = fn;
        res.summary = QString("已存 %1").arg(prefix);
        res.msg = ok ? QString("存图成功:%1").arg(fn) : QString("存图失败:%1").arg(fn);
        return res;
    }

    if (t == "comm_plc_send") {
        outVis = !ctx.displayImage.empty() ? ctx.displayImage : ctx.rawImage;
        QString wordOrder = p.value("word_order", "ABCD").toString();
        QVector<AddrWordWrite> mappedWrites;
        QStringList previewList;

        for (const auto& item : node.plcBindings) {
            int nid = item.sourceNodeId;
            double rawVal = 0.0;
            if (nid == -1) {
                rawVal = item.constVal;
            } else {
                const StepResult* sr = nullptr;
                if (nid == 0) sr = &ctx.lastResult;
                else if (ctx.results.contains(nid)) sr = &ctx.results[nid];
                if (sr) {
                    if (item.field == "ok") rawVal = sr->ok;
                    else if (item.field == "x") rawVal = sr->x;
                    else if (item.field == "y") rawVal = sr->y;
                    else if (item.field == "extra") rawVal = sr->extra;
                    else if (item.field == "count") rawVal = sr->count;
                    else if (item.field == "text_len") rawVal = sr->text.length();
                }
            }
            double scaled = (nid == -1) ? rawVal : (rawVal * item.scale);
            QVector<uint16_t> words = PlcClient::encodeValueToWords(scaled, item.dataType, wordOrder);
            int step = (ctx.plcClient && ctx.plcClient->protocol() == "Siemens S7") ? 2 : 1;
            for (int wIdx = 0; wIdx < words.size(); ++wIdx) {
                mappedWrites.push_back({ item.targetAddr + wIdx * step, words[wIdx] });
            }
            QString valStr = (item.dataType.toUpper() == "FLOAT32") ? QString::number(scaled, 'f', 2) : QString::number(std::lround(scaled));
            previewList << QString("@%1=%2").arg(item.targetAddr).arg(valStr);
        }

        if (mappedWrites.isEmpty()) {
            res.summary = "空发送表"; res.msg = "未配置任何PLC发送地址项";
            return res;
        }
        QString joined = previewList.join(", ");
        if (!ctx.plcClient || !ctx.plcClient->isConnected()) {
            res.ok = 0;
            res.count = mappedWrites.size();
            res.summary = "PLC未连接";
            res.msg = QString("PLC未连接，未发送 (配置目标: %1)").arg(joined);
            return res;
        }
        if (!ctx.allowPlcSend) {
            res.ok = ctx.lastResult.ok;
            res.count = mappedWrites.size();
            res.summary = QString("待触发(%1项)").arg(node.plcBindings.size());
            res.msg = QString("连续运行中(等待外部/手动触发发送: %1)").arg(joined);
            return res;
        }
        QString err;
        if (ctx.plcClient->writeMappedWords(mappedWrites, &err)) {
            res.ok = 1; res.count = mappedWrites.size();
            res.summary = QString("已发%1项").arg(node.plcBindings.size());
            res.msg = QString("PLC自由地址写入成功: %1").arg(joined);
        } else {
            res.ok = 0; res.summary = "发送失败";
            res.msg = QString("PLC发送失败: %1").arg(err);
        }
        return res;
    }

    outVis = ctx.displayImage;
    res.ok = 1;
    res.msg = "算子执行完成";
    return res;
}

// ==================== FlowExecutionRunner ====================
FlowCycleSummary FlowExecutionRunner::execute(
    const QVector<FlowNodeData>& nodes,
    const QVector<FlowEdgeData>& edges,
    const cv::Mat& inputFrame,
    const QString& sourceName,
    PlcClient* plcClient,
    UnifiedCameraController* camCtrl,
    int stopNodeId,
    int inspectNodeId,
    bool isLiveStream,
    bool allowPlcSend,
    bool allowSaveIo)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    QMap<int, QVector<QPair<int, EdgeCondition>>> incoming;
    for (const auto& n : nodes) incoming[n.id] = {};
    for (const auto& e : edges) {
        if (incoming.contains(e.dstId)) incoming[e.dstId].push_back({e.srcId, e.condition});
    }

    ExecContext ctx;
    if (isLiveStream && !inputFrame.empty()) ctx.streamFrame = inputFrame.clone();
    ctx.sourceImage = inputFrame;
    ctx.sourceName = sourceName;
    ctx.plcClient = plcClient;
    ctx.camCtrl = camCtrl;
    ctx.allowPlcSend = allowPlcSend;
    ctx.allowSaveIo = allowSaveIo;

    FlowCycleSummary summary;
    summary.sourceName = sourceName;
    summary.finalOk = true;
    cv::Mat inspectedMat;

    for (const auto& n : nodes) {
        if (!n.enabled) {
            NodeRuntimeState st; st.status = "DISABLED"; st.summary = "已禁用";
            summary.nodeStates[n.id] = st;
            continue;
        }

        const auto& inc = incoming[n.id];
        if (!inc.isEmpty()) {
            QVector<int> activeParents;
            for (const auto& pair : inc) {
                int srcId = pair.first;
                EdgeCondition cond = pair.second;
                if (!summary.nodeStates.contains(srcId)) continue;
                QString srcSt = summary.nodeStates[srcId].status;
                if (srcSt != "OK" && srcSt != "NG") continue;
                bool pass = (cond == EdgeCondition::Always)
                         || (cond == EdgeCondition::OnOk && srcSt == "OK")
                         || (cond == EdgeCondition::OnNg && srcSt == "NG");
                if (pass) {
                    activeParents.push_back(srcId);
                    summary.activeEdges.push_back({srcId, n.id});
                }
            }
            if (activeParents.isEmpty()) {
                NodeRuntimeState st; st.status = "SKIP"; st.summary = "分支跳过";
                summary.nodeStates[n.id] = st;
                continue;
            }
            int lastPid = activeParents.last();
            if (ctx.results.contains(lastPid)) ctx.lastResult = ctx.results[lastPid];
        }

        QElapsedTimer opTimer;
        opTimer.start();
        cv::Mat vis;
        StepResult sr = executeSingleOperator(n, ctx, vis);
        double dtMs = opTimer.nsecsElapsed() / 1e6;

        NodeRuntimeState st;
        st.status = (sr.ok != 0) ? "OK" : "NG";
        st.elapsedMs = dtMs;
        st.summary = sr.summary;
        st.result = sr;
        if (!vis.empty()) {
            st.outputImage = vis.clone();
            ctx.displayImage = vis;
            if (inspectNodeId > 0 && n.id == inspectNodeId) inspectedMat = vis.clone();
        }
        summary.nodeStates[n.id] = st;
        summary.executionOrder.push_back(n.id);

        ctx.lastResult = sr;
        ctx.results[n.id] = sr;
        if (n.typeKey != "logic_branch" && sr.ok == 0) summary.finalOk = false;
        summary.finalMsg = sr.msg;

        if (stopNodeId > 0 && n.id == stopNodeId) break;
    }

    summary.totalMs = totalTimer.nsecsElapsed() / 1e6;
    summary.rawImage = !ctx.rawImage.empty() ? ctx.rawImage : inputFrame;
    summary.displayImage = !inspectedMat.empty() ? inspectedMat : ctx.displayImage;
    return summary;
}

// ==================== ContinuousFlowEngine ====================
ContinuousFlowEngine::ContinuousFlowEngine(PlcClient* plc, UnifiedCameraController* cam, QObject* parent)
    : QObject(parent), m_plc(plc), m_cam(cam) {}

ContinuousFlowEngine::~ContinuousFlowEngine() {
    stopContinuous();
    if (m_videoCap.isOpened()) m_videoCap.release();
}

void ContinuousFlowEngine::setSourceMode(const QString& mode, const QString& videoPath) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (mode != m_sourceMode) {
        m_sourceMode = mode;
        if (m_videoCap.isOpened()) m_videoCap.release();
    }
    if (!videoPath.isEmpty()) m_videoPath = videoPath;
}

void ContinuousFlowEngine::setCycleInterval(int ms) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_cycleIntervalMs = std::max(0, ms);
}

void ContinuousFlowEngine::setPaused(bool paused) { m_paused.store(paused); }

void ContinuousFlowEngine::setInspectNode(int nodeId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_inspectNodeId = nodeId;
}

void ContinuousFlowEngine::setPlcSendPolicy(const QString& policyMode, int intervalMs) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_plcPolicy = policyMode;
    m_plcIntervalMs = std::max(30, intervalMs);
}

void ContinuousFlowEngine::requestOnePlcSend() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_pendingPlcTrig = true;
}

void ContinuousFlowEngine::setFolderImages(const QStringList& paths, int startIdx) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_folderImages = paths;
    m_folderIdx = std::max(0, startIdx);
}

void ContinuousFlowEngine::updateFlowSnapshot(const QVector<FlowNodeData>& nodes, const QVector<FlowEdgeData>& edges) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_nodes = nodes;
    m_edges = edges;
}

cv::Mat ContinuousFlowEngine::generateSimFrame(int w, int h) {
    m_simTick++;
    double t = m_simTick * 0.033;
    int cycle = static_cast<int>(t / 7.0) % 3;
    double sx = w / 640.0, sy = h / 480.0;
    cv::Mat frame;
    if (cycle == 0) {
        frame = cv::Mat(h, w, CV_8UC3, cv::Scalar(24, 28, 36));
        int cx = static_cast<int>((150 + 340 * (0.5 + 0.5 * std::sin(t * 1.1))) * sx);
        int cy = static_cast<int>((240 + 45 * std::cos(t * 0.9)) * sy);
        float ang = static_cast<float>(std::fmod(t * 24.0, 360.0));
        cv::RotatedRect rr(cv::Point2f(static_cast<float>(cx), static_cast<float>(cy)),
                           cv::Size2f(static_cast<float>(185 * sx), static_cast<float>(118 * sy)), ang);
        cv::Point2f pts[4]; rr.points(pts);
        std::vector<cv::Point> poly;
        for (int i = 0; i < 4; ++i) poly.emplace_back(static_cast<int>(pts[i].x), static_cast<int>(pts[i].y));
        cv::fillPoly(frame, poly, cv::Scalar(232, 236, 242));
        cv::circle(frame, cv::Point(cx, cy), static_cast<int>(18 * sx), cv::Scalar(30, 34, 42), -1);
    } else if (cycle == 1) {
        frame = cv::Mat(h, w, CV_8UC3, cv::Scalar(168, 172, 178));
        int cx = static_cast<int>((140 + 360 * std::fmod(t, 7.0) / 7.0) * sx);
        int cy = static_cast<int>((240 + 35 * std::sin(t * 2.0)) * sy);
        cv::Scalar cols[3] = {{30, 35, 225}, {225, 95, 25}, {45, 195, 55}};
        cv::Scalar c = cols[static_cast<int>(t / 2.3) % 3];
        cv::rectangle(frame, cv::Rect(cx - static_cast<int>(85 * sx), cy - static_cast<int>(70 * sy), static_cast<int>(170 * sx), static_cast<int>(140 * sy)), c, -1);
    } else {
        frame = cv::Mat(h, w, CV_8UC3, cv::Scalar(234, 237, 242));
        int cx = static_cast<int>((320 + 150 * std::sin(t * 1.25)) * sx);
        int cy = static_cast<int>((240 + 75 * std::cos(t * 0.95)) * sy);
        cv::circle(frame, cv::Point(cx, cy), static_cast<int>(78 * sx), cv::Scalar(50, 54, 62), -1);
        cv::circle(frame, cv::Point(cx, cy), static_cast<int>(24 * sx), cv::Scalar(220, 224, 230), -1);
    }
    return frame;
}

cv::Mat ContinuousFlowEngine::acquireFrame(bool advanceFolder, QString& outName) {
    QString mode; QString vidPath; QStringList imgs; int idx = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        mode = m_sourceMode; vidPath = m_videoPath; imgs = m_folderImages; idx = m_folderIdx;
        if (!imgs.isEmpty() && advanceFolder) m_folderIdx = (idx + 1) % imgs.size();
    }

    if (mode == "USB相机") {
        cv::Mat f = m_cam ? m_cam->grabFrame() : cv::Mat();
        outName = QString("USB#%1(%2x%3)").arg(m_cam ? m_cam->camIndex() : 0).arg(f.cols).arg(f.rows);
        return f;
    }
    if (mode == "仿真测试") {
        outName = "SimConveyor";
        return generateSimFrame(1280, 720);
    }
    if (mode == "视频文件") {
        if (vidPath.isEmpty()) return cv::Mat();
        if (!m_videoCap.isOpened()) m_videoCap.open(vidPath.toStdString());
        if (!m_videoCap.isOpened()) return cv::Mat();
        cv::Mat f;
        if (!m_videoCap.read(f) || f.empty()) {
            m_videoCap.set(cv::CAP_PROP_POS_FRAMES, 0);
            m_videoCap.read(f);
        }
        outName = QFileInfo(vidPath).fileName();
        return f;
    }
    if (imgs.isEmpty()) return cv::Mat();
    QString p = imgs[idx % imgs.size()];
    outName = QFileInfo(p).fileName();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return cv::Mat();
    QByteArray ba = f.readAll();
    std::vector<uchar> buf(ba.begin(), ba.end());
    return cv::imdecode(buf, cv::IMREAD_COLOR);
}

void ContinuousFlowEngine::runSingleCycle(const cv::Mat& staticImg, const QString& staticName, int stopNodeId) {
    cv::Mat raw; QString name; QString mode;
    QVector<FlowNodeData> nodes; QVector<FlowEdgeData> edges; int inspectId = -1;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        mode = m_sourceMode; nodes = m_nodes; edges = m_edges; inspectId = m_inspectNodeId;
    }
    if (mode == "本地图库" && !staticImg.empty()) {
        raw = staticImg.clone();
        name = staticName;
    } else {
        raw = acquireFrame(false, name);
    }
    if (raw.empty()) {
        emit errorOccurred(QString("单次运行取图失败（当前源：%1），请检查相机连接或选择本地图库。").arg(mode));
        return;
    }
    FlowCycleSummary sum = FlowExecutionRunner::execute(
        nodes, edges, raw, name, m_plc, m_cam, stopNodeId, inspectId, true, true, true
    );
    sum.fps = 0.0; sum.plcSent = true;
    cv::Mat vis = !sum.displayImage.empty() ? sum.displayImage : raw;
    emit cycleCompleted(raw, vis, sum);
}

void ContinuousFlowEngine::startContinuous() {
    if (m_running.load()) return;
    m_running.store(true);
    m_paused.store(false);
    m_thread = std::thread(&ContinuousFlowEngine::continuousLoop, this);
    emit runStateChanged(true, QString("连续运行中 (%1)").arg(m_sourceMode));
}

void ContinuousFlowEngine::stopContinuous() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    emit runStateChanged(false, "连续运行已停止");
}

void ContinuousFlowEngine::continuousLoop() {
    QElapsedTimer fpsTimer; fpsTimer.start();
    int fpsCount = 0; double curFps = 0.0;
    qint64 lastPlcMs = 0; qint64 lastSaveMs = 0;
    QElapsedTimer monoClock; monoClock.start();

    while (m_running.load()) {
        QElapsedTimer loopTimer; loopTimer.start();
        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        QString name;
        cv::Mat raw = acquireFrame(true, name);
        if (raw.empty()) {
            m_running.store(false);
            emit errorOccurred(QString("连续运行取图失败（当前源：%1），请检查相机连接。").arg(m_sourceMode));
            emit runStateChanged(false, "取图失败已停止");
            return;
        }

        QVector<FlowNodeData> nodes; QVector<FlowEdgeData> edges;
        int inspectId = -1; QString plcPolicy; int plcInterval = 200;
        int cycleInterval = 0; bool pendingTrig = false; QString srcMode;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            nodes = m_nodes; edges = m_edges; inspectId = m_inspectNodeId;
            plcPolicy = m_plcPolicy; plcInterval = m_plcIntervalMs;
            cycleInterval = m_cycleIntervalMs; srcMode = m_sourceMode;
            pendingTrig = m_pendingPlcTrig; m_pendingPlcTrig = false;
        }

        qint64 nowMs = monoClock.elapsed();
        bool allowPlc = false;
        if (plcPolicy == "EVERY_CYCLE") allowPlc = true;
        else if (plcPolicy == "INTERVAL" && (nowMs - lastPlcMs) >= plcInterval) { allowPlc = true; lastPlcMs = nowMs; }
        else if (plcPolicy == "MANUAL_TRIG" && pendingTrig) { allowPlc = true; lastPlcMs = nowMs; }

        bool allowSave = (nowMs - lastSaveMs) >= 1200;
        FlowCycleSummary sum = FlowExecutionRunner::execute(
            nodes, edges, raw, name, m_plc, m_cam, -1, inspectId, true, allowPlc, allowSave
        );
        if (allowSave) lastSaveMs = nowMs;

        fpsCount++;
        if (fpsTimer.elapsed() >= 500) {
            curFps = fpsCount * 1000.0 / fpsTimer.elapsed();
            fpsCount = 0; fpsTimer.restart();
        }
        sum.fps = curFps; sum.plcSent = allowPlc;
        cv::Mat vis = !sum.displayImage.empty() ? sum.displayImage : raw;
        emit cycleCompleted(raw, vis, sum);

        int minGap = (srcMode == "本地图库") ? std::max(350, cycleInterval) : cycleInterval;
        if (minGap > 0) {
            qint64 rem = minGap - loopTimer.elapsed();
            if (rem > 2) std::this_thread::sleep_for(std::chrono::milliseconds(rem));
        } else if (srcMode == "仿真测试") {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
}
