#include "ImageViewport.h"
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

VmImageCanvasWidget::VmImageCanvasWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 220);
    setMouseTracking(true);

    m_btnLeft = new QPushButton("‹", this);
    m_btnRight = new QPushButton("›", this);
    QString arrowCss =
        "QPushButton { background: rgba(20, 23, 30, 150); color: #CBD5E1; "
        "border: 1px solid rgba(255,255,255,28); border-radius: 6px; font-size: 20px; padding: 0; }"
        "QPushButton:hover { background: rgba(255, 122, 0, 195); color: #FFFFFF; border-color: #FF9E3D; }";
    m_btnLeft->setFixedSize(26, 48);
    m_btnRight->setFixedSize(26, 48);
    m_btnLeft->setStyleSheet(arrowCss);
    m_btnRight->setStyleSheet(arrowCss);
    connect(m_btnLeft, &QPushButton::clicked, [this]() { emit stepImageRequested(-1); });
    connect(m_btnRight, &QPushButton::clicked, [this]() { emit stepImageRequested(1); });
}

void VmImageCanvasWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    int cy = std::max(10, (height() - 48) / 2);
    m_btnLeft->move(8, cy);
    m_btnRight->move(std::max(8, width() - 34), cy);
    if (m_autoFit && m_imgW > 0) computeFit();
}

QImage VmImageCanvasWidget::matToQImage(const cv::Mat& bgr) {
    if (bgr.empty()) return QImage();
    cv::Mat rgb;
    if (bgr.channels() == 1) cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB);
    else if (bgr.channels() == 4) cv::cvtColor(bgr, rgb, cv::COLOR_BGRA2RGB);
    else cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    return qimg.copy();
}

void VmImageCanvasWidget::updateFrames(const cv::Mat& rawBgr, const cv::Mat& visBgr,
                                       bool isContRunning, bool isPaused, int okState) {
    m_isRunning = isContRunning;
    m_isPaused = isPaused;
    m_okState = okState;
    if (!rawBgr.empty()) {
        m_rawBgr = rawBgr.clone();
        m_rawQImg = matToQImage(m_rawBgr);
    }
    if (!visBgr.empty()) {
        m_visBgr = visBgr.clone();
        m_visQImg = matToQImage(m_visBgr);
    } else if (!m_rawQImg.isNull()) {
        m_visBgr = m_rawBgr;
        m_visQImg = m_rawQImg;
    }
    const cv::Mat& ref = !m_visBgr.empty() ? m_visBgr : m_rawBgr;
    if (!ref.empty()) {
        bool changed = (ref.cols != m_imgW || ref.rows != m_imgH);
        m_imgW = ref.cols;
        m_imgH = ref.rows;
        if (changed || m_autoFit) computeFit();
    }
    update();
}

void VmImageCanvasWidget::setSplitMode(bool enabled) { m_splitMode = enabled; update(); }
void VmImageCanvasWidget::setCrosshairVisible(bool enabled) { m_showCross = enabled; update(); }
void VmImageCanvasWidget::setRoiMode(bool enabled) {
    m_roiMode = enabled;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void VmImageCanvasWidget::setRoiRect(int x, int y, int w, int h, bool visible) {
    m_roiVisible = visible && w > 2 && h > 2;
    if (m_roiVisible) m_roiRect = QRectF(x, y, w, h);
    update();
}

QRect VmImageCanvasWidget::currentRoiRect() const {
    return QRect(static_cast<int>(std::lround(m_roiRect.x())),
                 static_cast<int>(std::lround(m_roiRect.y())),
                 static_cast<int>(std::lround(m_roiRect.width())),
                 static_cast<int>(std::lround(m_roiRect.height())));
}

void VmImageCanvasWidget::fitInView() { m_autoFit = true; computeFit(); update(); }

void VmImageCanvasWidget::resetZoom100() {
    if (m_imgW <= 0) return;
    m_autoFit = false; m_zoom = 1.0;
    m_pan = QPointF((width() - m_imgW) / 2.0, (height() - m_imgH) / 2.0);
    update();
}

void VmImageCanvasWidget::zoomStep(double factor) {
    if (m_imgW <= 0) return;
    m_autoFit = false;
    double nz = std::max(0.08, std::min(18.0, m_zoom * factor));
    QPointF c(width() / 2.0, height() / 2.0);
    QPointF ip = w2i(c);
    m_zoom = nz;
    m_pan = QPointF(c.x() - ip.x() * m_zoom, c.y() - ip.y() * m_zoom);
    update();
}

void VmImageCanvasWidget::computeFit() {
    if (m_imgW <= 0 || m_imgH <= 0) return;
    double vw = std::max(10, width() - 16);
    double vh = std::max(10, height() - 34);
    m_zoom = std::min(vw / m_imgW, vh / m_imgH);
    m_pan = QPointF((width() - m_imgW * m_zoom) / 2.0, (height() - 26 - m_imgH * m_zoom) / 2.0);
}

QPointF VmImageCanvasWidget::w2i(const QPointF& pt) const {
    return QPointF((pt.x() - m_pan.x()) / std::max(1e-6, m_zoom),
                   (pt.y() - m_pan.y()) / std::max(1e-6, m_zoom));
}

QPointF VmImageCanvasWidget::i2w(const QPointF& pt) const {
    return QPointF(pt.x() * m_zoom + m_pan.x(), pt.y() * m_zoom + m_pan.y());
}

QRectF VmImageCanvasWidget::imgRectW() const {
    return QRectF(m_pan.x(), m_pan.y(), m_imgW * m_zoom, m_imgH * m_zoom);
}

QPointF VmImageCanvasWidget::clampImg(const QPointF& pt) const {
    return QPointF(std::max(0.0, std::min(static_cast<double>(m_imgW - 1), pt.x())),
                   std::max(0.0, std::min(static_cast<double>(m_imgH - 1), pt.y())));
}

QString VmImageCanvasWidget::hitRoiHandle(const QPointF& ipt) const {
    if (!m_roiVisible || m_roiRect.isNull()) return "";
    double tol = std::max(7.0, 10.0 / std::max(0.25, m_zoom));
    double mx = m_roiRect.center().x(), my = m_roiRect.center().y();
    QPair<QString, QPointF> pts[8] = {
        {"tl", {m_roiRect.left(), m_roiRect.top()}}, {"t", {mx, m_roiRect.top()}},
        {"tr", {m_roiRect.right(), m_roiRect.top()}}, {"r", {m_roiRect.right(), my}},
        {"br", {m_roiRect.right(), m_roiRect.bottom()}}, {"b", {mx, m_roiRect.bottom()}},
        {"bl", {m_roiRect.left(), m_roiRect.bottom()}}, {"l", {m_roiRect.left(), my}}
    };
    for (const auto& pair : pts) {
        if (std::abs(ipt.x() - pair.second.x()) <= tol && std::abs(ipt.y() - pair.second.y()) <= tol)
            return pair.first;
    }
    if (m_roiRect.contains(ipt)) return "move";
    return "";
}

void VmImageCanvasWidget::wheelEvent(QWheelEvent* e) {
    if (m_imgW <= 0) return;
    m_autoFit = false;
    double f = (e->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    double nz = std::max(0.08, std::min(18.0, m_zoom * f));
    QPointF cur = e->position();
    QPointF ip = w2i(cur);
    m_zoom = nz;
    m_pan = QPointF(cur.x() - ip.x() * m_zoom, cur.y() - ip.y() * m_zoom);
    update();
}

void VmImageCanvasWidget::mouseDoubleClickEvent(QMouseEvent*) { fitInView(); }

void VmImageCanvasWidget::mousePressEvent(QMouseEvent* e) {
    if (m_imgW <= 0 || e->button() != Qt::LeftButton) return;
    QPointF pos = e->position();
    QRectF ir = imgRectW();
    if (m_splitMode) {
        double sx = ir.left() + ir.width() * m_splitRatio;
        if (std::abs(pos.x() - sx) <= 12.0 && pos.y() >= ir.top() && pos.y() <= ir.bottom()) {
            m_draggingSplit = true;
            return;
        }
    }
    QPointF ipt = clampImg(w2i(pos));
    if (m_roiMode) {
        m_roiAction = "create"; m_roiDragStart = ipt; m_roiRect = QRectF(ipt, ipt); m_roiVisible = true;
        update(); return;
    } else if (m_roiVisible) {
        QString hit = hitRoiHandle(ipt);
        if (!hit.isEmpty()) {
            m_roiAction = hit; m_roiDragStart = ipt; m_roiOrigRect = m_roiRect; return;
        }
    }
    m_autoFit = false; m_panning = true; m_panStart = pos; m_panOrigin = m_pan;
    setCursor(Qt::ClosedHandCursor);
}

void VmImageCanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_imgW <= 0) return;
    QPointF pos = e->position();
    QRectF ir = imgRectW();
    QPointF iptRaw = w2i(pos);
    int ix = static_cast<int>(iptRaw.x()), iy = static_cast<int>(iptRaw.y());
    const cv::Mat& src = !m_rawBgr.empty() ? m_rawBgr : m_visBgr;
    if (!src.empty() && ix >= 0 && ix < m_imgW && iy >= 0 && iy < m_imgH) {
        cv::Vec3b bgr = src.at<cv::Vec3b>(iy, ix);
        m_pixelText = QString::asprintf("R:%03d G:%03d B:%03d  |  X:%04d Y:%04d", bgr[2], bgr[1], bgr[0], ix, iy);
        update();
    }
    if (m_draggingSplit) {
        if (ir.width() > 1.0) {
            m_splitRatio = std::max(0.05, std::min(0.95, (pos.x() - ir.left()) / ir.width()));
            update();
        }
        return;
    }
    if (!m_roiAction.isEmpty()) {
        QPointF cur = clampImg(iptRaw);
        if (m_roiAction == "create") {
            m_roiRect = QRectF(m_roiDragStart, cur).normalized();
        } else if (m_roiAction == "move") {
            double dx = cur.x() - m_roiDragStart.x(), dy = cur.y() - m_roiDragStart.y();
            QRectF nr = m_roiOrigRect;
            nr.moveTo(std::max(0.0, std::min(m_imgW - nr.width(), nr.x() + dx)),
                      std::max(0.0, std::min(m_imgH - nr.height(), nr.y() + dy)));
            m_roiRect = nr;
        } else {
            QRectF nr = m_roiOrigRect;
            if (m_roiAction.contains('l')) nr.setLeft(std::min(nr.right() - 12.0, cur.x()));
            if (m_roiAction.contains('r')) nr.setRight(std::max(nr.left() + 12.0, cur.x()));
            if (m_roiAction.contains('t')) nr.setTop(std::min(nr.bottom() - 12.0, cur.y()));
            if (m_roiAction.contains('b')) nr.setBottom(std::max(nr.top() + 12.0, cur.y()));
            m_roiRect = nr.normalized();
        }
        update(); return;
    }
    if (m_panning) {
        m_pan = m_panOrigin + (pos - m_panStart);
        update(); return;
    }
}

void VmImageCanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (m_draggingSplit) { m_draggingSplit = false; return; }
    if (!m_roiAction.isEmpty()) {
        m_roiAction.clear();
        if (m_roiRect.width() >= 10 && m_roiRect.height() >= 10) {
            emit roiSelected(static_cast<int>(std::lround(m_roiRect.x())),
                             static_cast<int>(std::lround(m_roiRect.y())),
                             static_cast<int>(std::lround(m_roiRect.width())),
                             static_cast<int>(std::lround(m_roiRect.height())));
        }
        if (m_roiMode) setRoiMode(false);
        return;
    }
    if (m_panning) { m_panning = false; setCursor(Qt::ArrowCursor); }
}

void VmImageCanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    int tile = 16;
    p.fillRect(rect(), QColor("#21242B"));
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(QColor("#282C35")));
    for (int y = 0; y < height(); y += tile) {
        int off = ((y / tile) % 2) ? tile : 0;
        for (int x = off; x < width(); x += tile * 2) p.drawRect(x, y, tile, tile);
    }

    if (m_visQImg.isNull()) {
        p.setPen(QColor("#9CA3AF"));
        p.setFont(QFont("Microsoft YaHei", 11, QFont::Bold));
        p.drawText(rect().adjusted(0, -16, 0, 0), Qt::AlignCenter,
                   "点击顶部「▶ 单次运行」或「▶▶ 连续运行」执行视觉流程");
        p.setPen(QColor("#6B7280"));
        p.setFont(QFont("Microsoft YaHei", 9));
        p.drawText(rect().adjusted(0, 22, 0, 0), Qt::AlignCenter,
                   "支持最高 4K USB 相机 · 高精度模板学习匹配 · 自由地址真实PLC通信");
        return;
    }

    QRectF target = imgRectW();
    if (m_splitMode && !m_rawQImg.isNull()) {
        double sx = target.left() + target.width() * m_splitRatio;
        p.save(); p.setClipRect(QRectF(target.left(), target.top(), sx - target.left(), target.height()));
        p.drawImage(target, m_rawQImg); p.restore();
        p.save(); p.setClipRect(QRectF(sx, target.top(), target.right() - sx, target.height()));
        p.drawImage(target, m_visQImg); p.restore();
        p.setPen(QPen(QColor("#FF7A00"), 2.2));
        p.drawLine(QPointF(sx, target.top()), QPointF(sx, target.bottom()));
        double my = target.center().y();
        p.setBrush(QBrush(QColor("#1A1D24"))); p.setPen(QPen(QColor("#FF7A00"), 2));
        p.drawEllipse(QPointF(sx, my), 12, 12);
        p.setPen(QColor("#FFFFFF")); p.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
        p.drawText(QRectF(sx - 12, my - 12, 24, 24), Qt::AlignCenter, "⇄");
    } else {
        p.drawImage(target, m_visQImg);
    }

    if (m_showCross) {
        p.setPen(QPen(QColor(255, 122, 0, 145), 1.2, Qt::DashLine));
        p.drawLine(QPointF(target.center().x(), target.top()), QPointF(target.center().x(), target.bottom()));
        p.drawLine(QPointF(target.left(), target.center().y()), QPointF(target.right(), target.center().y()));
    }

    if (m_roiVisible && !m_roiRect.isNull()) {
        QRectF wr(i2w(m_roiRect.topLeft()), i2w(m_roiRect.bottomRight()));
        p.setPen(QPen(QColor("#FF7A00"), 2.0, Qt::DashLine));
        p.setBrush(QBrush(QColor(255, 122, 0, 30)));
        p.drawRect(wr);
        p.setPen(QPen(QColor("#FFFFFF"), 1.2));
        p.setBrush(QBrush(QColor("#FF7A00")));
        double mx = m_roiRect.center().x(), my = m_roiRect.center().y();
        QPointF hpts[8] = {
            {m_roiRect.left(), m_roiRect.top()}, {mx, m_roiRect.top()}, {m_roiRect.right(), m_roiRect.top()},
            {m_roiRect.right(), my}, {m_roiRect.right(), m_roiRect.bottom()}, {mx, m_roiRect.bottom()},
            {m_roiRect.left(), m_roiRect.bottom()}, {m_roiRect.left(), my}
        };
        for (const auto& hp : hpts) {
            QPointF wp = i2w(hp);
            p.drawRect(QRectF(wp.x() - 4.5, wp.y() - 4.5, 9, 9));
        }
    }

    int hx = 12;
    if (m_isRunning) {
        QRectF lr(hx, 10, 96, 25);
        p.setPen(QPen(QColor("#FF7A00"), 1.1));
        p.setBrush(QBrush(QColor(20, 23, 30, 215)));
        p.drawRoundedRect(lr, 12, 12);
        p.setPen(QColor("#FF9E3D"));
        p.setFont(QFont("Microsoft YaHei", 8, QFont::Bold));
        p.drawText(lr, Qt::AlignCenter, m_isPaused ? "⏸ 已暂停" : "▶▶ 连续运行");
        hx += 104;
    }
    if (m_okState == 1 || m_okState == 0) {
        bool ok = (m_okState == 1);
        QRectF sr(hx, 10, 50, 25);
        p.setPen(QPen(ok ? QColor("#4ADE80") : QColor("#FCA5A5"), 1.2));
        p.setBrush(QBrush(ok ? QColor(22, 163, 74, 225) : QColor(220, 38, 38, 225)));
        p.drawRoundedRect(sr, 12, 12);
        p.setPen(QColor("#FFFFFF"));
        p.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
        p.drawText(sr, Qt::AlignCenter, ok ? "OK" : "NG");
    }

    int barH = 26;
    QRectF barR(0, height() - barH, width(), barH);
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(QColor(18, 20, 26, 210)));
    p.drawRect(barR);
    p.setPen(QPen(QColor(255, 255, 255, 24), 1));
    p.drawLine(QPointF(0, height() - barH), QPointF(width(), height() - barH));
    p.setPen(QColor("#CBD5E1"));
    p.setFont(QFont("Consolas", 8));
    p.drawText(barR.adjusted(10, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight,
               QString("%1 * %2  |  %3").arg(m_imgW).arg(m_imgH).arg(m_pixelText));
}

VmImageDisplayPanel::VmImageDisplayPanel(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* bar = new QFrame();
    bar->setStyleSheet("QFrame { background: rgba(34, 38, 46, 225); border-bottom: 1px solid rgba(255,255,255,22); }");
    bar->setFixedHeight(36);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 3, 8, 3);
    hl->setSpacing(5);

    cbOutput = new QComboBox();
    cbOutput->setMinimumWidth(195);
    cbOutput->addItem("最终输出图像 (整条流程)", -1);

    btnRawOnly = new QPushButton("👁 仅看原图");
    btnRawOnly->setProperty("accent", "tool");
    btnRawOnly->setCheckable(true);

    btnSplit = new QPushButton("◧ 分屏");
    btnSplit->setProperty("accent", "tool");
    btnSplit->setCheckable(true);

    btnRoi = new QPushButton("⬚ ROI");
    btnRoi->setProperty("accent", "tool");
    btnRoi->setCheckable(true);

    btnCross = new QPushButton("✛");
    btnCross->setProperty("accent", "tool");
    btnCross->setCheckable(true);

    btnPause = new QPushButton("⏸");
    btnPause->setProperty("accent", "tool");
    btnPause->setCheckable(true);
    btnPause->setToolTip("暂停/定格当前帧");

    auto* btnZin = new QPushButton("🔍+"); btnZin->setProperty("accent", "tool");
    auto* btnZout = new QPushButton("🔍-"); btnZout->setProperty("accent", "tool");
    auto* btn11 = new QPushButton("1:1"); btn11->setProperty("accent", "tool");
    auto* btnFit = new QPushButton("⛶"); btnFit->setProperty("accent", "tool");
    auto* btnSnap = new QPushButton("📸"); btnSnap->setProperty("accent", "tool");

    hl->addWidget(cbOutput);
    hl->addStretch();
    for (auto* b : {btnRawOnly, btnSplit, btnRoi, btnCross, btnPause, btnZin, btnZout, btn11, btnFit, btnSnap}) {
        hl->addWidget(b);
    }
    vl->addWidget(bar);

    canvas = new VmImageCanvasWidget();
    vl->addWidget(canvas, 1);

    connect(cbOutput, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
        emit outputNodeChanged(selectedOutputNodeId());
    });
    connect(btnRawOnly, &QPushButton::toggled, this, &VmImageDisplayPanel::rawOnlyToggled);
    connect(btnSplit, &QPushButton::toggled, canvas, &VmImageCanvasWidget::setSplitMode);
    connect(btnRoi, &QPushButton::toggled, canvas, &VmImageCanvasWidget::setRoiMode);
    connect(btnCross, &QPushButton::toggled, canvas, &VmImageCanvasWidget::setCrosshairVisible);
    connect(btnPause, &QPushButton::toggled, this, &VmImageDisplayPanel::pauseToggled);
    connect(btnZin, &QPushButton::clicked, [this]() { canvas->zoomStep(1.2); });
    connect(btnZout, &QPushButton::clicked, [this]() { canvas->zoomStep(1.0 / 1.2); });
    connect(btn11, &QPushButton::clicked, canvas, &VmImageCanvasWidget::resetZoom100);
    connect(btnFit, &QPushButton::clicked, canvas, &VmImageCanvasWidget::fitInView);
    connect(btnSnap, &QPushButton::clicked, this, &VmImageDisplayPanel::snapshotRequested);
}

void VmImageDisplayPanel::refreshOutputNodes(const QVector<QPair<int, QString>>& nodeItems) {
    int cur = selectedOutputNodeId();
    cbOutput->blockSignals(true);
    cbOutput->clear();
    cbOutput->addItem("最终输出图像 (整条流程)", -1);
    for (const auto& pair : nodeItems) {
        cbOutput->addItem(QString("%1 %2.输出图像").arg(pair.first - 1).arg(pair.second), pair.first);
    }
    int idx = cbOutput->findData(cur);
    cbOutput->setCurrentIndex(idx >= 0 ? idx : 0);
    cbOutput->blockSignals(false);
}

int VmImageDisplayPanel::selectedOutputNodeId() const {
    return cbOutput->currentData().toInt();
}

void VmImageDisplayPanel::selectOutputNodeId(int nodeId) {
    int idx = cbOutput->findData(nodeId);
    if (idx >= 0) cbOutput->setCurrentIndex(idx);
}
