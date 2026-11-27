#include "MainWindow.h"

#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QLinearGradient>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QShortcut>
#include <QKeySequence>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCloseEvent>
#include <QPixmap>
#include <cmath>
#include <algorithm>

// ==================== VmLogoWidget ====================
VmLogoWidget::VmLogoWidget(QWidget* parent) : QWidget(parent) {
    setFixedSize(142, 34);
}

void VmLogoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(4, 4, 28, 30);
    grad.setColorAt(0.0, QColor("#FFB347"));
    grad.setColorAt(1.0, QColor("#FF6A00"));
    QPainterPath path;
    path.moveTo(4, 6);
    path.lineTo(12, 6);
    path.lineTo(18, 22);
    path.lineTo(24, 6);
    path.lineTo(31, 6);
    path.lineTo(21, 30);
    path.lineTo(15, 30);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(grad));
    p.drawPath(path);

    p.setPen(QColor("#F8FAFC"));
    p.setFont(QFont("Segoe UI", 11, QFont::Bold));
    p.drawText(QRectF(36, 1, 102, 17), Qt::AlignVCenter | Qt::AlignLeft, "vision");
    p.setPen(QColor("#9CA3AF"));
    p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
    p.drawText(QRectF(37, 16, 102, 16), Qt::AlignVCenter | Qt::AlignLeft, "master C++");
}

// ==================== CategoryRailButton ====================
CategoryRailButton::CategoryRailButton(ToolCategory cat, QWidget* parent)
    : QPushButton(QString(), parent), m_cat(cat), m_accent(categoryColorHex(cat)) {
    setCheckable(true);
    setFixedSize(42, 46);
    setToolTip(categoryToChinese(cat) + "（点击展开悬浮算子抽屉）");
    setCursor(Qt::PointingHandCursor);
}

void CategoryRailButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    bool checked = isChecked();
    bool hovered = underMouse();
    QRectF r(3, 3, width() - 6, height() - 6);
    if (checked) {
        QColor bg = m_accent; bg.setAlpha(42);
        p.setPen(QPen(m_accent, 1.3));
        p.setBrush(QBrush(bg));
        p.drawRoundedRect(r, 7, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(m_accent));
        p.drawRoundedRect(QRectF(0, 10, 3, height() - 20), 1.5, 1.5);
    } else if (hovered) {
        p.setPen(QPen(QColor(255, 255, 255, 36), 1.0));
        p.setBrush(QBrush(QColor(255, 255, 255, 18)));
        p.drawRoundedRect(r, 7, 7);
    }

    p.setPen((checked || hovered) ? m_accent : QColor("#CBD5E1"));
    p.setFont(QFont("Consolas", 9, QFont::Bold));
    p.drawText(QRectF(0, 4, width(), 22), Qt::AlignCenter, categoryIconText(m_cat));

    p.setPen(checked ? QColor("#FFFFFF") : QColor("#9CA3AF"));
    p.setFont(QFont("Microsoft YaHei", 7, checked ? QFont::Bold : QFont::Normal));
    p.drawText(QRectF(0, 26, width(), 16), Qt::AlignCenter, categoryShortName(m_cat));
}

// ==================== GlassOperatorDrawer ====================
GlassOperatorDrawer::GlassOperatorDrawer(QWidget* parent) : QWidget(parent) {
    setFixedWidth(252);
    hide();

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QHBoxLayout();
    m_lblTitle = new QLabel("算子库");
    m_lblTitle->setStyleSheet("color: #FFFFFF; font-size: 13px; font-weight: bold; background: transparent;");
    auto* btnClose = new QPushButton("✕");
    btnClose->setFixedSize(24, 24);
    btnClose->setProperty("accent", "tool");
    connect(btnClose, &QPushButton::clicked, this, &GlassOperatorDrawer::closeDrawer);
    hdr->addWidget(m_lblTitle);
    hdr->addStretch();
    hdr->addWidget(btnClose);
    vl->addLayout(hdr);

    auto* sub = new QLabel("点击任意算子卡片串联到当前流程图");
    sub->setStyleSheet("color: #9CA3AF; font-size: 10px; background: transparent;");
    vl->addWidget(sub);

    m_cardsHost = new QWidget();
    m_cardsHost->setStyleSheet("background: transparent;");
    m_cardsLayout = new QVBoxLayout(m_cardsHost);
    m_cardsLayout->setContentsMargins(0, 4, 0, 0);
    m_cardsLayout->setSpacing(7);
    vl->addWidget(m_cardsHost, 1);

    m_anim = new QPropertyAnimation(this, "geometry", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void GlassOperatorDrawer::openCategory(ToolCategory cat, int anchorHeight) {
    if (isVisible() && m_activeCat == cat) {
        closeDrawer();
        return;
    }
    m_activeCat = cat;
    QString colHex = categoryColorHex(cat);
    m_accent = QColor(colHex);
    m_lblTitle->setText(QString("◆  %1").arg(categoryToChinese(cat)));
    m_lblTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold; background: transparent;").arg(colHex));

    while (QLayoutItem* ch = m_cardsLayout->takeAt(0)) {
        if (ch->widget()) ch->widget()->deleteLater();
        delete ch;
    }

    for (const auto& meta : VisionOperatorRegistry::allTools()) {
        if (meta.category != cat) continue;
        auto* btn = new QPushButton(QString("  +  %1").arg(meta.displayName));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(
            "QPushButton { background: rgba(48, 54, 66, 215); color: #F1F5F9; "
            "border: 1px solid rgba(255, 255, 255, 26); border-left: 3px solid %1; "
            "border-radius: 7px; padding: 9px 10px; text-align: left; font-size: 12px; }"
            "QPushButton:hover { background: rgba(68, 76, 94, 240); border-color: %1; color: #FFFFFF; }"
        ).arg(colHex));
        QString key = meta.typeKey;
        connect(btn, &QPushButton::clicked, [this, key]() { emit toolSelected(key); });
        m_cardsLayout->addWidget(btn);
    }
    m_cardsLayout->addStretch();

    int h = std::max(260, std::min(anchorHeight - 20, 500));
    show();
    raise();
    m_anim->stop();
    m_anim->setStartValue(QRect(-252, 10, 252, h));
    m_anim->setEndValue(QRect(8, 10, 252, h));
    m_anim->start();
    update();
}

void GlassOperatorDrawer::closeDrawer() {
    hide();
    emit closed();
}

void GlassOperatorDrawer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF rect(0.5, 0.5, width() - 1, height() - 1);
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(42, 47, 58, 236));
    grad.setColorAt(1.0, QColor(22, 25, 32, 242));
    p.setPen(QPen(QColor(255, 255, 255, 44), 1.2));
    p.setBrush(QBrush(grad));
    p.drawRoundedRect(rect, 11, 11);

    QLinearGradient sheen(0, 0, 0, 42);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 28));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(sheen));
    p.drawRoundedRect(QRectF(2, 2, width() - 4, 40), 10, 10);

    p.setBrush(QBrush(m_accent));
    p.drawRoundedRect(QRectF(18, height() - 4, width() - 36, 2.5), 1, 1);
}

// ==================== YieldRingWidget & LatencyChartWidget ====================
YieldRingWidget::YieldRingWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(140, 140);
}

void YieldRingWidget::setMetrics(int total, int ok, int ng) {
    (void)ng;
    m_total = total;
    m_ok = ok;
    m_pct = (total > 0) ? (ok * 100.0 / total) : 0.0;
    update();
}

void YieldRingWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double side = std::min(width(), height()) - 18.0;
    QRectF r((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    p.setPen(QPen(QColor(255, 255, 255, 22), 10, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 0, 360 * 16);
    QColor col = (m_pct >= 90.0) ? QColor("#22C55E") : ((m_pct >= 70.0 || m_total == 0) ? QColor("#FF7A00") : QColor("#EF4444"));
    int span = static_cast<int>(std::lround(-m_pct / 100.0 * 360.0 * 16.0));
    p.setPen(QPen(col, 9.5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 90 * 16, span);
    p.setPen(QColor("#F8FAFC"));
    p.setFont(QFont("Consolas", 15, QFont::Bold));
    p.drawText(r.adjusted(0, -10, 0, -4), Qt::AlignCenter, QString("%1%").arg(m_pct, 0, 'f', 1));
    p.setPen(QColor("#9CA3AF"));
    p.setFont(QFont("Microsoft YaHei", 8));
    p.drawText(r.adjusted(0, 22, 0, 12), Qt::AlignCenter, QString("良率 %1/%2").arg(m_ok).arg(m_total));
}

LatencyChartWidget::LatencyChartWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(96);
}

void LatencyChartWidget::pushSample(double ms, bool ok) {
    if (m_samples.size() >= 40) m_samples.pop_front();
    m_samples.push_back({ms, ok});
    update();
}

void LatencyChartWidget::clearSamples() {
    m_samples.clear();
    update();
}

void LatencyChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.setBrush(QBrush(QColor(20, 23, 30, 220)));
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 7, 7);

    double sum = 0.0;
    for (const auto& s : m_samples) sum += s.first;
    double avg = m_samples.empty() ? 0.0 : (sum / m_samples.size());
    p.setPen(QColor("#9CA3AF"));
    p.setFont(QFont("Microsoft YaHei", 8, QFont::Bold));
    p.drawText(10, 17, QString("节拍耗时波形 · 均值 %1 ms").arg(avg, 0, 'f', 1));
    if (m_samples.size() < 2) return;

    double pl = 10, pr = 10, pt = 24, pb = 10;
    double cw = std::max(10.0, width() - pl - pr);
    double ch = std::max(10.0, height() - pt - pb);
    double maxV = 10.0;
    for (const auto& s : m_samples) maxV = std::max(maxV, s.first * 1.2);

    QPainterPath path;
    for (size_t i = 0; i < m_samples.size(); ++i) {
        double x = pl + cw * i / (m_samples.size() - 1);
        double y = pt + ch - (m_samples[i].first / maxV) * ch;
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }
    p.setPen(QPen(QColor("#FF9E3D"), 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

// ==================== FreeAddressPlcEditor ====================
FreeAddressPlcEditor::FreeAddressPlcEditor(FlowCapsuleItem* node, FlowCanvasView* canvas,
                                           const QString& protocolName, QWidget* parent)
    : QWidget(parent), m_node(node), m_canvas(canvas), m_protocol(protocolName) {
    if (m_node->data.plcBindings.isEmpty()) {
        m_node->data.plcBindings = VisionOperatorRegistry::defaultPlcBindings();
    }

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 4, 0, 4);
    vl->setSpacing(6);

    auto* bar = new QHBoxLayout();
    auto* lbl = new QLabel("PLC 自由地址映射表 (每行可设任意独立目标地址):");
    lbl->setObjectName("SectionTitle");
    auto* btnAdd = new QPushButton("+ 添加发送项");
    btnAdd->setProperty("accent", "primary");
    auto* btnDel = new QPushButton("- 删除");
    auto* btnUp = new QPushButton("↑"); btnUp->setFixedWidth(26);
    auto* btnDn = new QPushButton("↓"); btnDn->setFixedWidth(26);
    auto* btnRst = new QPushButton("↺ 默认");
    bar->addWidget(lbl);
    bar->addStretch();
    bar->addWidget(btnAdd);
    bar->addWidget(btnDel);
    bar->addWidget(btnUp);
    bar->addWidget(btnDn);
    bar->addWidget(btnRst);
    vl->addLayout(bar);

    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"目标PLC地址", "数据来源模块", "输出字段", "数据类型", "倍率/常量值"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setMinimumHeight(165);
    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Fixed);
    hdr->setSectionResizeMode(1, QHeaderView::Stretch);
    hdr->setSectionResizeMode(2, QHeaderView::Fixed);
    hdr->setSectionResizeMode(3, QHeaderView::Fixed);
    hdr->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 92);
    m_table->setColumnWidth(2, 86);
    m_table->setColumnWidth(3, 92);
    m_table->setColumnWidth(4, 78);
    vl->addWidget(m_table);

    m_lblPreview = new QLabel();
    m_lblPreview->setWordWrap(true);
    m_lblPreview->setStyleSheet(
        "background: rgba(16,19,26,225); color: #86EFAC; border: 1px solid rgba(255,255,255,26); "
        "border-radius: 5px; padding: 5px 8px; font-family: Consolas; font-size: 11px;"
    );
    vl->addWidget(m_lblPreview);

    connect(btnAdd, &QPushButton::clicked, [this]() {
        int nextAddr = m_node->data.plcBindings.isEmpty() ? 100 : (m_node->data.plcBindings.last().targetAddr + 10);
        int srcId = 0;
        for (auto* n : m_canvas->orderedNodes()) {
            if (n->data.id != m_node->data.id) srcId = n->data.id;
        }
        m_node->data.plcBindings.push_back({nextAddr, srcId, "x", "INT16", 1.0, 0.0});
        rebuildTable(m_node->data.plcBindings.size() - 1);
        emit bindingsChanged();
    });
    connect(btnDel, &QPushButton::clicked, [this]() {
        if (m_node->data.plcBindings.isEmpty()) return;
        int r = m_table->currentRow();
        if (r < 0 || r >= m_node->data.plcBindings.size()) r = m_node->data.plcBindings.size() - 1;
        m_node->data.plcBindings.removeAt(r);
        rebuildTable(std::max(0, r - 1));
        emit bindingsChanged();
    });
    connect(btnUp, &QPushButton::clicked, [this]() {
        int r = m_table->currentRow();
        if (r > 0 && r < m_node->data.plcBindings.size()) {
            std::swap(m_node->data.plcBindings[r], m_node->data.plcBindings[r - 1]);
            rebuildTable(r - 1);
            emit bindingsChanged();
        }
    });
    connect(btnDn, &QPushButton::clicked, [this]() {
        int r = m_table->currentRow();
        if (r >= 0 && r + 1 < m_node->data.plcBindings.size()) {
            std::swap(m_node->data.plcBindings[r], m_node->data.plcBindings[r + 1]);
            rebuildTable(r + 1);
            emit bindingsChanged();
        }
    });
    connect(btnRst, &QPushButton::clicked, [this]() {
        m_node->data.plcBindings = VisionOperatorRegistry::defaultPlcBindings();
        rebuildTable(0);
        emit bindingsChanged();
    });

    rebuildTable();
}

void FreeAddressPlcEditor::rebuildTable(int selectRow) {
    auto& list = m_node->data.plcBindings;
    m_table->setRowCount(list.size());

    QVector<QPair<int, QString>> srcChoices = {{0, "紧邻上游模块"}, {-1, "固定常量值"}};
    for (auto* n : m_canvas->orderedNodes()) {
        if (n->data.id != m_node->data.id) {
            srcChoices.push_back({n->data.id, QString("%1 %2").arg(n->data.id - 1).arg(n->data.title)});
        }
    }

    QString prefix = (m_protocol == "Siemens S7") ? "DB " : "D";
    for (int r = 0; r < list.size(); ++r) {
        m_table->setRowHeight(r, 29);
        const auto& item = list[r];

        auto* spAddr = new QSpinBox();
        spAddr->setPrefix(prefix);
        spAddr->setRange(0, 65535);
        spAddr->setValue(item.targetAddr);
        spAddr->setStyleSheet("color: #FF9E3D; font-weight: bold;");

        auto* cbSrc = new QComboBox();
        for (const auto& ch : srcChoices) cbSrc->addItem(ch.second, ch.first);
        int idxS = cbSrc->findData(item.sourceNodeId);
        if (idxS >= 0) cbSrc->setCurrentIndex(idxS);

        auto* cbField = new QComboBox();
        cbField->addItem("状态(OK/NG)", "ok");
        cbField->addItem("坐标 X", "x");
        cbField->addItem("坐标 Y", "y");
        cbField->addItem("附加值/角度", "extra");
        cbField->addItem("目标数量", "count");
        cbField->addItem("文本长度", "text_len");
        int idxF = cbField->findData(item.field);
        if (idxF >= 0) cbField->setCurrentIndex(idxF);

        auto* cbType = new QComboBox();
        cbType->addItem("INT16(1字)", "INT16");
        cbType->addItem("UINT16(1字)", "UINT16");
        cbType->addItem("INT32(2字)", "INT32");
        cbType->addItem("FLOAT32(2字)", "FLOAT32");
        int idxT = cbType->findData(item.dataType);
        if (idxT >= 0) cbType->setCurrentIndex(idxT);

        auto* spVal = new QDoubleSpinBox();
        spVal->setRange(-100000.0, 100000.0);
        spVal->setDecimals(2);
        if (item.sourceNodeId == -1) {
            cbField->setEnabled(false);
            spVal->setValue(item.constVal);
        } else {
            cbField->setEnabled(true);
            spVal->setValue(item.scale);
        }

        connect(spAddr, QOverload<int>::of(&QSpinBox::valueChanged), [this, r](int v) {
            m_node->data.plcBindings[r].targetAddr = v;
            updatePreview();
            emit bindingsChanged();
        });
        connect(cbSrc, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, r, cbSrc, cbField, spVal](int) {
            int nid = cbSrc->currentData().toInt();
            m_node->data.plcBindings[r].sourceNodeId = nid;
            spVal->blockSignals(true);
            if (nid == -1) {
                cbField->setEnabled(false);
                spVal->setValue(m_node->data.plcBindings[r].constVal);
            } else {
                cbField->setEnabled(true);
                spVal->setValue(m_node->data.plcBindings[r].scale);
            }
            spVal->blockSignals(false);
            updatePreview();
            emit bindingsChanged();
        });
        connect(cbField, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, r, cbField](int) {
            m_node->data.plcBindings[r].field = cbField->currentData().toString();
            updatePreview();
            emit bindingsChanged();
        });
        connect(cbType, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, r, cbType](int) {
            m_node->data.plcBindings[r].dataType = cbType->currentData().toString();
            updatePreview();
            emit bindingsChanged();
        });
        connect(spVal, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, r](double v) {
            auto& b = m_node->data.plcBindings[r];
            if (b.sourceNodeId == -1) b.constVal = v;
            else b.scale = v;
            updatePreview();
            emit bindingsChanged();
        });

        m_table->setCellWidget(r, 0, spAddr);
        m_table->setCellWidget(r, 1, cbSrc);
        m_table->setCellWidget(r, 2, cbField);
        m_table->setCellWidget(r, 3, cbType);
        m_table->setCellWidget(r, 4, spVal);
    }
    if (selectRow >= 0 && selectRow < list.size()) m_table->selectRow(selectRow);
    updatePreview();
}

void FreeAddressPlcEditor::updatePreview() {
    QStringList parts;
    int totalWords = 0;
    QString prefix = (m_protocol == "Siemens S7") ? "DB." : "D";
    for (const auto& b : m_node->data.plcBindings) {
        totalWords += PlcClient::dtypeWordCount(b.dataType);
        QString srcExpr = (b.sourceNodeId == -1)
            ? QString("常量(%1)").arg(b.constVal)
            : QString("%1.%2").arg(b.sourceNodeId == 0 ? "上游" : QString("#%1").arg(b.sourceNodeId - 1)).arg(b.field);
        parts << QString("%1%2 ← %3[%4]").arg(prefix).arg(b.targetAddr).arg(srcExpr).arg(b.dataType);
    }
    m_lblPreview->setText(QString("独立地址映射 (%1项, 共%2字):  %3")
                              .arg(m_node->data.plcBindings.size()).arg(totalWords).arg(parts.join("  |  ")));
}

// ==================== MainWindow ====================
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("VisionMaster C++ v4.7 — 仿海康威视工业视觉与高精度模板学习工作站");
    resize(1520, 910);
    setStyleSheet(buildGlassStyleSheet(false));

    m_engine = new ContinuousFlowEngine(&m_plc, &m_camCtrl, this);
    connect(m_engine, &ContinuousFlowEngine::cycleCompleted, this, &MainWindow::onCycleCompleted);
    connect(m_engine, &ContinuousFlowEngine::runStateChanged, this, &MainWindow::onRunStateChanged);
    connect(m_engine, &ContinuousFlowEngine::errorOccurred, this, &MainWindow::onEngineError);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPlcPoll);

    m_plcStatTimer = new QTimer(this);
    connect(m_plcStatTimer, &QTimer::timeout, this, &MainWindow::onRefreshPlcStatus);
    m_plcStatTimer->start(400);

    buildUi();
    applyCameraSettings(true);
    loadPresetSolution(m_cbPreset->currentText());

    QString defaultTestDir = QCoreApplication::applicationDirPath() + "/test_images";
    if (QDir(defaultTestDir).exists()) loadFolderPath(defaultTestDir);

    new QShortcut(QKeySequence("F5"), this, [this]() { runSingleFrame(); });
    new QShortcut(QKeySequence("Space"), this, [this]() { triggerManualPlcSend(); });
}

MainWindow::~MainWindow() {
    m_pollTimer->stop();
    m_plcStatTimer->stop();
    m_engine->stopContinuous();
    m_camCtrl.release();
    m_plc.disconnectPlc();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    m_pollTimer->stop();
    m_plcStatTimer->stop();
    m_engine->stopContinuous();
    m_camCtrl.release();
    m_plc.disconnectPlc();
    QMainWindow::closeEvent(e);
}

QString MainWindow::buildGlassStyleSheet(bool cyberBlue) const {
    QString accent = cyberBlue ? "#00D8FF" : "#FF7A00";
    QString accentL = cyberBlue ? "#5CE7FF" : "#FF9E3D";
    QString bgWin = cyberBlue ? "#0B0F17" : "#17191E";
    QString bgTop = cyberBlue ? "#101624" : "#1E2128";
    return QString(R"(
QMainWindow, QWidget {
    background-color: %1;
    color: #E5E7EB;
    font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
    font-size: 12px;
}
QFrame#TopTitleBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #262A33, stop:1 %2);
    border-bottom: 1px solid rgba(255,255,255,28);
}
QFrame#ActionRibbon {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(42,46,56,238), stop:1 rgba(28,31,38,242));
    border-bottom: 1px solid rgba(255,255,255,28);
}
QFrame#CategoryRail {
    background-color: #1B1E24;
    border-right: 1px solid rgba(255,255,255,26);
}
QFrame#GlassPanel {
    background-color: rgba(34, 38, 46, 215);
    border: 1px solid rgba(255, 255, 255, 28);
    border-top: 1px solid rgba(255, 255, 255, 42);
    border-radius: 8px;
}
QFrame#GlassSubCard {
    background-color: rgba(44, 49, 60, 198);
    border: 1px solid rgba(255, 255, 255, 28);
    border-top: 1px solid rgba(255, 255, 255, 42);
    border-radius: 7px;
}
QFrame#FlowSubHeader {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(48,52,62,225), stop:1 rgba(34,38,46,225));
    border-bottom: 1px solid rgba(255,255,255,26);
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
}
QFrame#BottomStatusBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #20242C, stop:1 #16181E);
    border-top: 1px solid rgba(255,255,255,26);
}
QLabel#SectionTitle { color: %4; font-size: 12px; font-weight: bold; }
QLabel#Breadcrumb { color: #9CA3AF; font-size: 11px; padding: 2px 8px; }
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(62,68,82,215), stop:1 rgba(42,47,58,220));
    color: #E5E7EB;
    border: 1px solid rgba(255,255,255,28);
    border-radius: 5px;
    padding: 4px 10px;
    font-weight: bold;
    min-height: 20px;
}
QPushButton:hover { border-color: %3; color: #FFFFFF; }
QPushButton[accent="primary"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %4, stop:1 %3);
    border: 1px solid %4;
    color: #101216;
}
QPushButton[accent="success"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34D399, stop:1 #16A34A);
    border: 1px solid #4ADE80;
    color: #071A0E;
}
QPushButton[accent="danger"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F87171, stop:1 #DC2626);
    border: 1px solid #FCA5A5;
    color: #FFFFFF;
}
QPushButton[accent="tool"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 4px 7px;
    color: #D1D5DB;
}
QPushButton[accent="tool"]:hover { background: rgba(255,255,255,22); border-color: rgba(255,255,255,30); color: #FFFFFF; }
QPushButton[accent="tool"]:checked { background: rgba(255,122,0,55); border: 1px solid %3; color: %4; }
QPushButton[accent="menu"] { background: transparent; border: none; color: #CBD5E1; padding: 4px 10px; font-weight: normal; }
QPushButton[accent="menu"]:hover { background: rgba(255,255,255,18); color: #FFFFFF; border-radius: 4px; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: rgba(18, 20, 26, 232);
    color: #E5E7EB;
    border: 1px solid rgba(255,255,255,28);
    border-radius: 5px;
    padding: 3px 7px;
    min-height: 20px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %3; }
QComboBox QAbstractItemView { background-color: #242832; color: #E5E7EB; border: 1px solid %3; selection-background-color: %3; selection-color: #111318; }
QTableWidget, QListWidget, QTextEdit {
    background-color: rgba(22, 25, 31, 228);
    color: #E5E7EB;
    border: 1px solid rgba(255,255,255,26);
    border-radius: 6px;
    alternate-background-color: rgba(32, 36, 45, 220);
    gridline-color: rgba(255,255,255,14);
}
QHeaderView::section {
    background-color: rgba(42, 46, 56, 238);
    color: #9CA3AF;
    border: none;
    border-right: 1px solid rgba(255,255,255,16);
    border-bottom: 1px solid rgba(255,255,255,22);
    padding: 5px 6px;
    font-weight: bold;
}
QTabWidget::pane { border: none; background: transparent; }
QTabBar::tab {
    background: rgba(34, 38, 46, 175);
    color: #9CA3AF;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 7px 16px;
    margin-right: 2px;
    font-weight: bold;
}
QTabBar::tab:selected { background: rgba(50, 55, 68, 228); color: #FFFFFF; border-bottom: 2px solid %3; }
QTabBar::tab:hover { color: #FFFFFF; }
QMenu { background-color: rgba(30, 34, 42, 246); border: 1px solid %3; border-radius: 6px; color: #E5E7EB; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 4px; }
QMenu::item:selected { background-color: %3; color: #111318; }
QSplitter::handle { background-color: #14161B; }
QSplitter::handle:hover { background-color: %3; }
)").arg(bgWin, bgTop, accent, accentL);
}

void MainWindow::buildUi() {
    auto* central = new QWidget();
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildTopTitleBar());
    root->addWidget(buildActionRibbon());

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    m_catRail = buildCategoryRail();
    body->addWidget(m_catRail);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setContentsMargins(6, 6, 6, 6);

    m_flowPanel = buildFlowPanel();
    m_rightSplitter = qobject_cast<QSplitter*>(buildRightWorkspace());
    m_prodSidePanel = buildProdSidePanel();
    m_prodSidePanel->hide();

    m_mainSplitter->addWidget(m_flowPanel);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->addWidget(m_prodSidePanel);
    m_mainSplitter->setSizes({640, 810, 0});

    body->addWidget(m_mainSplitter, 1);
    root->addLayout(body, 1);
    root->addWidget(buildBottomStatusBar());
}

QWidget* MainWindow::buildTopTitleBar() {
    auto* bar = new QFrame();
    bar->setObjectName("TopTitleBar");
    bar->setFixedHeight(40);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(10, 2, 12, 2);
    hl->setSpacing(6);

    hl->addWidget(new VmLogoWidget());

    auto* btnFile = new QPushButton("文件"); btnFile->setProperty("accent", "menu");
    auto* mFile = new QMenu(this);
    connect(mFile->addAction("📂 导入视觉方案 (.json)"), &QAction::triggered, this, &MainWindow::openSolutionJson);
    connect(mFile->addAction("💾 保存视觉方案 (.json)"), &QAction::triggered, this, &MainWindow::saveSolutionJson);
    mFile->addSeparator();
    connect(mFile->addAction("🖼 打开本地图片文件夹"), &QAction::triggered, this, &MainWindow::openImageFolder);
    connect(mFile->addAction("🎞 打开本地视频文件"), &QAction::triggered, this, &MainWindow::openVideoFile);
    btnFile->setMenu(mFile);

    auto* btnSet = new QPushButton("设置"); btnSet->setProperty("accent", "menu");
    auto* mSet = new QMenu(this);
    connect(mSet->addAction("📷 4K 相机与曝光设置"), &QAction::triggered, [this]() { m_rightTopTabs->setCurrentIndex(1); });
    connect(mSet->addAction("⚙ 打开 Windows 相机底层属性"), &QAction::triggered, this, &MainWindow::openNativeCameraProps);
    connect(mSet->addAction("🔌 打开 PLC 硬件联调中心"), &QAction::triggered, [this]() { m_rightTopTabs->setCurrentIndex(3); });
    btnSet->setMenu(mSet);

    auto* btnTool = new QPushButton("工具"); btnTool->setProperty("accent", "menu");
    auto* mTool = new QMenu(this);
    connect(mTool->addAction("⇕ 自动整理流程图排版"), &QAction::triggered, m_flowCanvas, &FlowCanvasView::autoLayout);
    connect(mTool->addAction("↶ 撤销 (Ctrl+Z)"), &QAction::triggered, m_flowCanvas, &FlowCanvasView::undo);
    connect(mTool->addAction("↷ 重做 (Ctrl+Y)"), &QAction::triggered, m_flowCanvas, &FlowCanvasView::redo);
    btnTool->setMenu(mTool);

    auto* btnHelp = new QPushButton("帮助"); btnHelp->setProperty("accent", "menu");
    connect(btnHelp, &QPushButton::clicked, [this]() {
        QMessageBox::information(this, "关于 VisionMaster C++ v4.7",
            "VisionMaster C++ 原生工业视觉工作站 (C++17 + Qt6 + OpenCV4)\n\n"
            "• 1:1 仿海康威视 VisionMaster 4.4 工业架构与磨砂玻璃动态UI\n"
            "• 高精度模板匹配：支持学习模板方式、图像金字塔多尺度加速、360°全角度旋转搜索、边缘特征融合与 0.01px 亚像素精定位\n"
            "• 真实工业 PLC 自由地址通信：每行发送数据均可独立定义目标寄存器地址");
    });

    for (auto* b : {btnFile, btnSet, btnTool, btnHelp}) hl->addWidget(b);
    hl->addStretch();

    m_lblPlcBadge = new QLabel("● PLC 未连接");
    m_lblPlcBadge->setStyleSheet("background: rgba(42,24,28,220); color: #FCA5A5; border: 1px solid #EF4444; border-radius: 11px; padding: 3px 10px; font-weight: bold;");
    hl->addWidget(m_lblPlcBadge);

    auto* btnTheme = new QPushButton("🎨 换肤");
    btnTheme->setProperty("accent", "tool");
    connect(btnTheme, &QPushButton::clicked, [this]() {
        m_cyberTheme = !m_cyberTheme;
        setStyleSheet(buildGlassStyleSheet(m_cyberTheme));
    });
    hl->addWidget(btnTheme);

    m_btnRunMode = new QPushButton("🖥 运行模式");
    m_btnRunMode->setProperty("accent", "tool");
    m_btnRunMode->setCheckable(true);
    connect(m_btnRunMode, &QPushButton::toggled, this, &MainWindow::toggleRunMode);
    hl->addWidget(m_btnRunMode);
    return bar;
}

QWidget* MainWindow::buildActionRibbon() {
    auto* rib = new QFrame();
    rib->setObjectName("ActionRibbon");
    rib->setFixedHeight(42);
    auto* hl = new QHBoxLayout(rib);
    hl->setContentsMargins(10, 4, 12, 4);
    hl->setSpacing(6);

    auto* bSave = new QPushButton("💾"); bSave->setProperty("accent", "tool"); connect(bSave, &QPushButton::clicked, this, &MainWindow::saveSolutionJson);
    auto* bOpen = new QPushButton("📂"); bOpen->setProperty("accent", "tool"); connect(bOpen, &QPushButton::clicked, this, &MainWindow::openSolutionJson);
    auto* bDir  = new QPushButton("🖼"); bDir->setProperty("accent", "tool"); connect(bDir, &QPushButton::clicked, this, &MainWindow::openImageFolder);
    auto* bCam  = new QPushButton("📷 相机/曝光"); bCam->setProperty("accent", "tool");
    connect(bCam, &QPushButton::clicked, [this]() { m_rightTopTabs->setCurrentIndex(1); });

    m_cbSource = new QComboBox();
    m_cbSource->addItems({"USB相机", "本地图库", "视频文件 (MP4)", "仿真测试"});
    m_cbSource->setMinimumWidth(114);
    connect(m_cbSource, &QComboBox::currentTextChanged, [this](const QString& txt) {
        m_spCamId->setVisible(txt.startsWith("USB"));
        m_cbRes->setVisible(txt.startsWith("USB") || txt.startsWith("仿真"));
        m_engine->setSourceMode(txt.startsWith("视频") ? "视频文件" : txt, m_videoPath);
    });

    m_spCamId = new QSpinBox();
    m_spCamId->setPrefix("CAM "); m_spCamId->setRange(0, 8); m_spCamId->setFixedWidth(72);
    connect(m_spCamId, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) { applyCameraSettings(); });

    m_cbRes = new QComboBox();
    m_cbRes->addItems({"640×480 (VGA)", "1280×720 (720P)", "1920×1080 (1080P)", "2560×1440 (2K)", "3840×2160 (4K UHD)"});
    m_cbRes->setCurrentText("1920×1080 (1080P)");
    m_cbRes->setMinimumWidth(142);
    connect(m_cbRes, &QComboBox::currentTextChanged, [this](const QString&) { applyCameraSettings(); });

    auto* bOnce = new QPushButton("▶ 单次运行");
    bOnce->setProperty("accent", "tool");
    connect(bOnce, &QPushButton::clicked, this, &MainWindow::runSingleFrame);

    m_btnContRun = new QPushButton("▶▶ 连续运行");
    m_btnContRun->setProperty("accent", "primary");
    m_btnContRun->setCheckable(true);
    connect(m_btnContRun, &QPushButton::clicked, this, &MainWindow::toggleContinuousRun);

    m_spCycleMs = new QSpinBox();
    m_spCycleMs->setPrefix("间隔 "); m_spCycleMs->setSuffix("ms");
    m_spCycleMs->setRange(0, 3000); m_spCycleMs->setSingleStep(20); m_spCycleMs->setValue(0);
    m_spCycleMs->setFixedWidth(96);
    connect(m_spCycleMs, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) { m_engine->setCycleInterval(v); });

    auto* bSend = new QPushButton("📤 触发发PLC"); bSend->setProperty("accent", "tool"); connect(bSend, &QPushButton::clicked, this, &MainWindow::triggerManualPlcSend);
    auto* bPlcTab = new QPushButton("🔌 PLC设置"); bPlcTab->setProperty("accent", "tool");
    connect(bPlcTab, &QPushButton::clicked, [this]() { m_rightTopTabs->setCurrentIndex(3); });
    auto* bLay  = new QPushButton("⇕ 排版"); bLay->setProperty("accent", "tool"); connect(bLay, &QPushButton::clicked, m_flowCanvas, &FlowCanvasView::autoLayout);

    hl->addWidget(bSave); hl->addWidget(bOpen); hl->addWidget(bDir); hl->addWidget(bCam);
    hl->addWidget(m_cbSource); hl->addWidget(m_spCamId); hl->addWidget(m_cbRes);
    hl->addWidget(bOnce); hl->addWidget(m_btnContRun); hl->addWidget(m_spCycleMs);
    hl->addWidget(bSend); hl->addWidget(bPlcTab); hl->addWidget(bLay);
    hl->addStretch();
    return rib;
}

QWidget* MainWindow::buildCategoryRail() {
    auto* rail = new QFrame();
    rail->setObjectName("CategoryRail");
    rail->setFixedWidth(48);
    auto* vl = new QVBoxLayout(rail);
    vl->setContentsMargins(3, 8, 3, 8);
    vl->setSpacing(5);

    ToolCategory cats[] = {
        ToolCategory::Acquisition, ToolCategory::Preprocess, ToolCategory::Location,
        ToolCategory::Measurement, ToolCategory::Recognition, ToolCategory::Calibration,
        ToolCategory::Logic, ToolCategory::Communication
    };
    for (ToolCategory c : cats) {
        auto* btn = new CategoryRailButton(c);
        connect(btn, &QPushButton::clicked, [this, c]() { onRailCategoryClicked(c); });
        vl->addWidget(btn);
        m_railBtns[c] = btn;
    }
    vl->addStretch();
    return rail;
}

void MainWindow::onRailCategoryClicked(ToolCategory cat) {
    bool wasSame = m_drawer->hasCategory(cat);
    for (auto it = m_railBtns.begin(); it != m_railBtns.end(); ++it) {
        it.value()->setChecked(it.key() == cat && !wasSame);
    }
    m_drawer->openCategory(cat, m_flowPanel->height());
}

void MainWindow::onDrawerClosed() {
    for (auto it = m_railBtns.begin(); it != m_railBtns.end(); ++it) it.value()->setChecked(false);
}

QWidget* MainWindow::buildFlowPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("GlassPanel");
    auto* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* sub = new QFrame();
    sub->setObjectName("FlowSubHeader");
    sub->setFixedHeight(36);
    auto* hl = new QHBoxLayout(sub);
    hl->setContentsMargins(10, 3, 8, 3);
    hl->setSpacing(6);

    auto* lblFlow = new QLabel("流程1");
    lblFlow->setStyleSheet("color: #FFFFFF; font-weight: bold; padding: 2px 8px; border-bottom: 2px solid #FF7A00;");
    auto* b1 = new QPushButton("▶"); b1->setProperty("accent", "tool"); connect(b1, &QPushButton::clicked, this, &MainWindow::runSingleFrame);
    auto* b2 = new QPushButton("▶▶"); b2->setProperty("accent", "tool"); connect(b2, &QPushButton::clicked, this, &MainWindow::toggleContinuousRun);

    m_cbPreset = new QComboBox();
    m_cbPreset->addItems({
        "流程1 · 灰度转换 + 高精度模板匹配(学习模板) + 自由地址PLC",
        "流程2 · 灰度转换 + 斑点定位 + 公差判定 + OK/NG分支",
        "流程3 · HSV颜色分选 + 红/蓝双路独立PLC地址",
        "流程4 · CLAHE灰度 + 圆形定位 + 九点标定(FLOAT32)"
    });
    m_cbPreset->setMinimumWidth(260);
    connect(m_cbPreset, &QComboBox::currentTextChanged, this, &MainWindow::loadPresetSolution);

    auto* bDel = new QPushButton("✕"); bDel->setProperty("accent", "tool");
    connect(bDel, &QPushButton::clicked, m_flowCanvas, &FlowCanvasView::deleteSelected);

    hl->addWidget(lblFlow); hl->addWidget(b1); hl->addWidget(b2);
    hl->addSpacing(8); hl->addWidget(m_cbPreset); hl->addStretch(); hl->addWidget(bDel);
    vl->addWidget(sub);

    m_lblBreadcrumb = new QLabel("流程1 > 0 USB相机采集");
    m_lblBreadcrumb->setObjectName("Breadcrumb");
    vl->addWidget(m_lblBreadcrumb);

    m_flowCanvas = new FlowCanvasView();
    connect(m_flowCanvas, &FlowCanvasView::nodeSelected, this, &MainWindow::onNodeSelected);
    connect(m_flowCanvas, &FlowCanvasView::nodeDoubleClicked, this, &MainWindow::onNodeDoubleClicked);
    connect(m_flowCanvas, &FlowCanvasView::flowChanged, this, &MainWindow::onFlowChanged);
    connect(m_flowCanvas, &FlowCanvasView::zoomChanged, [this](int pct) {
        m_slFlowZoom->blockSignals(true);
        m_slFlowZoom->setValue(std::max(40, std::min(180, pct)));
        m_slFlowZoom->blockSignals(false);
        m_lblFlowZoom->setText(QString("%1%").arg(pct));
    });
    connect(m_flowCanvas, &FlowCanvasView::runUpToRequested, this, &MainWindow::runSingleUpTo);
    vl->addWidget(m_flowCanvas, 1);

    m_drawer = new GlassOperatorDrawer(panel);
    connect(m_drawer, &GlassOperatorDrawer::toolSelected, this, &MainWindow::onAppendTool);
    connect(m_drawer, &GlassOperatorDrawer::closed, this, &MainWindow::onDrawerClosed);
    return panel;
}

QWidget* MainWindow::buildRightWorkspace() {
    auto* vsplit = new QSplitter(Qt::Vertical);

    auto* topCard = new QFrame();
    topCard->setObjectName("GlassPanel");
    auto* tl = new QVBoxLayout(topCard);
    tl->setContentsMargins(0, 0, 0, 0);
    m_rightTopTabs = new QTabWidget();

    m_imagePanel = new VmImageDisplayPanel();
    m_imagePanel->btnRawOnly->setChecked(false);
    connect(m_imagePanel, &VmImageDisplayPanel::outputNodeChanged, [this](int id) {
        m_engine->setInspectNode(id);
        if (!m_engine->isRunning()) runSingleFrame();
    });
    connect(m_imagePanel, &VmImageDisplayPanel::rawOnlyToggled, [this](bool c) {
        m_rawOnly = c;
        if (!m_lastRawImg.empty()) {
            m_imagePanel->canvas->updateFrames(m_lastRawImg, c ? m_lastRawImg : m_lastVisImg, m_engine->isRunning(), false, -1);
        }
    });
    connect(m_imagePanel, &VmImageDisplayPanel::pauseToggled, [this](bool p) { m_engine->setPaused(p); });
    connect(m_imagePanel, &VmImageDisplayPanel::snapshotRequested, this, &MainWindow::saveSnapshot);
    connect(m_imagePanel->canvas, &VmImageCanvasWidget::roiSelected, this, &MainWindow::onRoiDrawn);
    connect(m_imagePanel->canvas, &VmImageCanvasWidget::stepImageRequested, this, &MainWindow::stepImage);

    m_rightTopTabs->addTab(m_imagePanel, "图像");
    m_rightTopTabs->addTab(buildCameraTab(), "相机与曝光 (4K)");
    m_rightTopTabs->addTab(buildDashboardTab(), "产线看板");
    m_rightTopTabs->addTab(buildPlcTab(), "PLC 联调");
    tl->addWidget(m_rightTopTabs);
    vsplit->addWidget(topCard);

    auto* botCard = new QFrame();
    botCard->setObjectName("GlassPanel");
    auto* bl = new QVBoxLayout(botCard);
    bl->setContentsMargins(0, 0, 0, 0);
    m_rightBotTabs = new QTabWidget();

    m_resultTable = new QTableWidget(0, 3);
    m_resultTable->setHorizontalHeaderLabels({"执行序号", "时间", "模块数据"});
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setAlternatingRowColors(true);
    auto* rh = m_resultTable->horizontalHeader();
    rh->setSectionResizeMode(0, QHeaderView::Fixed);
    rh->setSectionResizeMode(1, QHeaderView::Fixed);
    rh->setSectionResizeMode(2, QHeaderView::Stretch);
    m_resultTable->setColumnWidth(0, 74);
    m_resultTable->setColumnWidth(1, 152);

    auto* libW = new QWidget();
    auto* ll = new QVBoxLayout(libW);
    ll->setContentsMargins(8, 8, 8, 8);
    auto* hbL = new QHBoxLayout();
    auto* bFd = new QPushButton("📂 选择图片文件夹"); connect(bFd, &QPushButton::clicked, this, &MainWindow::openImageFolder);
    auto* bPr = new QPushButton("◀ 上一张"); connect(bPr, &QPushButton::clicked, [this]() { stepImage(-1); });
    auto* bNx = new QPushButton("下一张 ▶"); connect(bNx, &QPushButton::clicked, [this]() { stepImage(1); });
    hbL->addWidget(bFd); hbL->addWidget(bPr); hbL->addWidget(bNx); hbL->addStretch();
    ll->addLayout(hbL);
    m_fileList = new QListWidget();
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::selectImageRow);
    ll->addWidget(m_fileList, 1);

    m_logBox = new QTextEdit();
    m_logBox->setReadOnly(true);

    m_rightBotTabs->addTab(m_resultTable, "运行结果");
    m_rightBotTabs->addTab(buildParamPanel(), "模块参数 (热调)");
    m_rightBotTabs->addTab(libW, "本地图库");
    m_rightBotTabs->addTab(m_logBox, "运行日志");
    bl->addWidget(m_rightBotTabs);
    vsplit->addWidget(botCard);
    vsplit->setSizes({500, 310});
    return vsplit;
}

QWidget* MainWindow::buildCameraTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget();
    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(10, 10, 10, 10);
    hl->setSpacing(10);

    auto* left = new QFrame(); left->setObjectName("GlassSubCard");
    auto* g1 = new QGridLayout(left); g1->setContentsMargins(12, 12, 12, 12); g1->setSpacing(8);
    auto* lbl1 = new QLabel("📷 4K 相机分辨率与驱动设置"); lbl1->setObjectName("SectionTitle");

    m_cbCamFmt = new QComboBox();
    m_cbCamFmt->addItems({"自动 (4K用MJPG/低分不改)", "默认 (不改驱动防变暗)", "MJPG (4K高帧率推荐)", "YUY2 (无压缩高亮度)"});
    connect(m_cbCamFmt, &QComboBox::currentTextChanged, [this](const QString&) { applyCameraSettings(); });

    m_cbCamBackend = new QComboBox();
    m_cbCamBackend->addItems({"DSHOW", "MSMF", "AUTO"});
    connect(m_cbCamBackend, &QComboBox::currentTextChanged, [this](const QString&) { applyCameraSettings(); });

    m_lblActualRes = new QLabel("实际输出分辨率：尚未打开相机");
    m_lblActualRes->setStyleSheet("color: #22C55E; font-family: Consolas; font-weight: bold;");

    auto* btnNative = new QPushButton("⚙ 打开 Windows 相机底层驱动属性 (调曝光/低光补偿)");
    btnNative->setProperty("accent", "primary");
    connect(btnNative, &QPushButton::clicked, this, &MainWindow::openNativeCameraProps);

    auto* btnReopen = new QPushButton("🔄 应用参数并重连相机");
    connect(btnReopen, &QPushButton::clicked, this, &MainWindow::reopenCamera);

    g1->addWidget(lbl1, 0, 0, 1, 2);
    g1->addWidget(new QLabel("视频编码格式:"), 1, 0); g1->addWidget(m_cbCamFmt, 1, 1);
    g1->addWidget(new QLabel("采集驱动后端:"), 2, 0); g1->addWidget(m_cbCamBackend, 2, 1);
    g1->addWidget(m_lblActualRes, 3, 0, 1, 2);
    g1->addWidget(btnNative, 4, 0, 1, 2);
    g1->addWidget(btnReopen, 5, 0, 1, 2);
    g1->setRowStretch(6, 1);
    hl->addWidget(left, 1);

    auto* right = new QFrame(); right->setObjectName("GlassSubCard");
    auto* g2 = new QGridLayout(right); g2->setContentsMargins(12, 12, 12, 12); g2->setSpacing(8);
    auto* lbl2 = new QLabel("☀ 曝光与软件 Gamma 提亮 (单次/连续运行完全一致)"); lbl2->setObjectName("SectionTitle");

    m_chkManualExp = new QCheckBox("启用手动曝光 (取消勾选则使用相机自动曝光)");
    connect(m_chkManualExp, &QCheckBox::toggled, [this](bool) { applyCameraSettings(); });

    auto* expRow = new QHBoxLayout();
    m_slExp = new QSlider(Qt::Horizontal); m_slExp->setRange(-13, -1); m_slExp->setValue(-5);
    m_spExp = new QSpinBox(); m_spExp->setRange(-13, -1); m_spExp->setValue(-5);
    connect(m_slExp, &QSlider::valueChanged, m_spExp, &QSpinBox::setValue);
    connect(m_spExp, QOverload<int>::of(&QSpinBox::valueChanged), m_slExp, &QSlider::setValue);
    connect(m_spExp, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) { applyCameraSettings(); });
    expRow->addWidget(m_slExp, 1); expRow->addWidget(m_spExp);

    m_spGain = new QSpinBox(); m_spGain->setRange(-1, 255); m_spGain->setValue(-1); m_spGain->setSpecialValueText("相机默认");
    connect(m_spGain, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) { applyCameraSettings(); });

    m_spGamma = new QDoubleSpinBox(); m_spGamma->setRange(0.3, 3.5); m_spGamma->setSingleStep(0.1); m_spGamma->setValue(1.0);
    connect(m_spGamma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double) { applyCameraSettings(); });

    m_spBias = new QSpinBox(); m_spBias->setRange(-80, 100); m_spBias->setValue(0);
    connect(m_spBias, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) { applyCameraSettings(); });

    g2->addWidget(lbl2, 0, 0, 1, 2);
    g2->addWidget(m_chkManualExp, 1, 0, 1, 2);
    g2->addWidget(new QLabel("手动曝光值 (-13暗 ~ -1亮):"), 2, 0); g2->addLayout(expRow, 2, 1);
    g2->addWidget(new QLabel("硬件增益 Gain:"), 3, 0); g2->addWidget(m_spGain, 3, 1);
    g2->addWidget(new QLabel("软件 Gamma 提亮 (>1变亮):"), 4, 0); g2->addWidget(m_spGamma, 4, 1);
    g2->addWidget(new QLabel("软件亮度偏移:"), 5, 0); g2->addWidget(m_spBias, 5, 1);
    g2->setRowStretch(6, 1);
    hl->addWidget(right, 1);

    scroll->setWidget(w);
    return scroll;
}

QWidget* MainWindow::buildDashboardTab() {
    auto* w = new QWidget();
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 10, 12, 10);
    vl->setSpacing(10);

    auto* top = new QHBoxLayout();
    m_yieldRing = new YieldRingWidget();
    top->addWidget(m_yieldRing);

    auto makeCard = [](const QString& title, const QString& color, QLabel*& valOut) {
        auto* f = new QFrame(); f->setObjectName("GlassSubCard");
        auto* l = new QVBoxLayout(f); l->setContentsMargins(12, 8, 12, 8);
        auto* t = new QLabel(title); t->setStyleSheet("color: #9CA3AF; font-size: 11px; font-weight: bold;");
        valOut = new QLabel("0"); valOut->setStyleSheet(QString("color: %1; font-family: Consolas; font-size: 20px; font-weight: bold;").arg(color));
        l->addWidget(t); l->addWidget(valOut);
        return f;
    };
    auto* g = new QGridLayout();
    g->addWidget(makeCard("节拍 FPS", "#FF9E3D", m_lblKpiFps), 0, 0);
    g->addWidget(makeCard("检测总数", "#F8FAFC", m_lblKpiTot), 0, 1);
    g->addWidget(makeCard("良品 OK", "#22C55E", m_lblKpiOk), 1, 0);
    g->addWidget(makeCard("不良 NG", "#EF4444", m_lblKpiNg), 1, 1);
    top->addLayout(g, 1);
    vl->addLayout(top);

    m_latencyChart = new LatencyChartWidget();
    vl->addWidget(m_latencyChart, 1);
    return w;
}

QWidget* MainWindow::buildProdSidePanel() {
    auto* card = new QFrame(); card->setObjectName("GlassPanel");
    card->setMinimumWidth(360); card->setMaximumWidth(420);
    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(14, 14, 14, 14);
    vl->setSpacing(10);
    auto* title = new QLabel("📊 产线实时运行大屏"); title->setObjectName("SectionTitle");
    vl->addWidget(title);
    m_prodRing = new YieldRingWidget(); vl->addWidget(m_prodRing);

    auto makeCard = [](const QString& t, const QString& c, QLabel*& out) {
        auto* f = new QFrame(); f->setObjectName("GlassSubCard");
        auto* l = new QVBoxLayout(f); l->setContentsMargins(10, 8, 10, 8);
        auto* lb = new QLabel(t); lb->setStyleSheet("color: #9CA3AF; font-size: 11px;");
        out = new QLabel("0"); out->setStyleSheet(QString("color: %1; font-family: Consolas; font-size: 18px; font-weight: bold;").arg(c));
        l->addWidget(lb); l->addWidget(out);
        return f;
    };
    auto* g = new QGridLayout();
    g->addWidget(makeCard("FPS", "#FF9E3D", m_lblProdFps), 0, 0);
    g->addWidget(makeCard("总数", "#F8FAFC", m_lblProdTot), 0, 1);
    g->addWidget(makeCard("OK", "#22C55E", m_lblProdOk), 1, 0);
    g->addWidget(makeCard("NG", "#EF4444", m_lblProdNg), 1, 1);
    vl->addLayout(g);
    m_prodChart = new LatencyChartWidget(); vl->addWidget(m_prodChart);
    m_lblProdMsg = new QLabel("等待运行...");
    m_lblProdMsg->setWordWrap(true);
    m_lblProdMsg->setStyleSheet("background: rgba(16,19,26,220); padding: 10px; border-radius: 6px; font-family: Consolas;");
    vl->addWidget(m_lblProdMsg);
    vl->addStretch();
    return card;
}

// ==================== 纯真实工业 PLC 硬件联调中心（已完全去除模拟/虚拟PLC） ====================
QWidget* MainWindow::buildPlcTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget();
    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(10, 10, 10, 10);
    hl->setSpacing(10);

    // 左卡片：PLC 硬件连接参数
    auto* left = new QFrame(); left->setObjectName("GlassSubCard");
    auto* g = new QGridLayout(left); g->setContentsMargins(12, 12, 12, 12); g->setSpacing(7);
    auto* lblL = new QLabel("🔌 工业 PLC 硬件连接设置"); lblL->setObjectName("SectionTitle");

    m_cbProto = new QComboBox();
    m_cbProto->addItems({"Modbus TCP", "Modbus RTU", "Siemens S7", "TCP Socket"});
    m_edHost = new QLineEdit("192.168.1.10");
    m_spPort = new QSpinBox(); m_spPort->setRange(1, 65535); m_spPort->setValue(502);
    m_spUnit = new QSpinBox(); m_spUnit->setRange(0, 255); m_spUnit->setValue(1);
    m_spBaud = new QSpinBox(); m_spBaud->setRange(1200, 115200); m_spBaud->setValue(9600);
    m_spDb   = new QSpinBox(); m_spDb->setRange(1, 9999); m_spDb->setValue(1);
    m_spRack = new QSpinBox(); m_spRack->setRange(0, 7); m_spRack->setValue(0);
    m_spSlot = new QSpinBox(); m_spSlot->setRange(0, 31); m_spSlot->setValue(1);

    m_btnConn = new QPushButton("连接 PLC");
    m_btnConn->setProperty("accent", "primary");
    connect(m_btnConn, &QPushButton::clicked, this, &MainWindow::toggleConnectPlc);

    g->addWidget(lblL, 0, 0, 1, 2);
    g->addWidget(new QLabel("通信协议:"), 1, 0); g->addWidget(m_cbProto, 1, 1);
    g->addWidget(new QLabel("PLC IP / 串口:"), 2, 0); g->addWidget(m_edHost, 2, 1);
    g->addWidget(new QLabel("端口 / 波特率:"), 3, 0);
    auto* hb1 = new QHBoxLayout(); hb1->addWidget(m_spPort); hb1->addWidget(m_spBaud); g->addLayout(hb1, 3, 1);
    g->addWidget(new QLabel("站号 / S7 DB块:"), 4, 0);
    auto* hb2 = new QHBoxLayout(); hb2->addWidget(m_spUnit); hb2->addWidget(m_spDb); g->addLayout(hb2, 4, 1);
    g->addWidget(new QLabel("S7 机架 / 槽号:"), 5, 0);
    auto* hb3 = new QHBoxLayout(); hb3->addWidget(m_spRack); hb3->addWidget(m_spSlot); g->addLayout(hb3, 5, 1);
    g->addWidget(m_btnConn, 6, 0, 1, 2);
    g->setRowStretch(7, 1);
    hl->addWidget(left, 1);

    // 右卡片：触发策略与真实寄存器在线读写测试
    auto* right = new QFrame(); right->setObjectName("GlassSubCard");
    auto* g2 = new QGridLayout(right); g2->setContentsMargins(12, 12, 12, 12); g2->setSpacing(7);
    auto* lblR = new QLabel("📡 触发策略与在线寄存器读写诊断"); lblR->setObjectName("SectionTitle");

    m_cbPlcPolicy = new QComboBox();
    m_cbPlcPolicy->addItems({"每周期自动发送", "仅外部/手动触发发送", "按定时间隔发送", "仅检测不发PLC"});
    m_cbPlcPolicy->setCurrentIndex(1);
    connect(m_cbPlcPolicy, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) { applyPlcPolicy(); });

    m_spPlcInterval = new QSpinBox(); m_spPlcInterval->setRange(50, 5000); m_spPlcInterval->setValue(200); m_spPlcInterval->setSuffix(" ms");
    connect(m_spPlcInterval, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) { applyPlcPolicy(); });

    m_spTrigAddr = new QSpinBox(); m_spTrigAddr->setRange(0, 65535); m_spTrigAddr->setValue(200);
    m_btnPlcTrig = new QPushButton("🔗 开启外部PLC触发监听 (0→1上升沿)");
    connect(m_btnPlcTrig, &QPushButton::clicked, this, &MainWindow::togglePlcTriggerWatch);

    m_lblPlcTxRx = new QLabel("通信统计: TX 0 帧  |  RX 0 帧");
    m_lblPlcTxRx->setStyleSheet("background: rgba(16,19,26,220); color: #9CA3AF; padding: 6px 10px; border-radius: 5px; font-family: Consolas;");

    auto* hbrw = new QHBoxLayout();
    m_spTestAddr = new QSpinBox(); m_spTestAddr->setPrefix("@"); m_spTestAddr->setRange(0, 65535); m_spTestAddr->setValue(100);
    m_edTestVal = new QLineEdit("1,320,240,0");
    auto* bRd = new QPushButton("在线读取"); connect(bRd, &QPushButton::clicked, this, &MainWindow::manualReadPlc);
    auto* bWr = new QPushButton("在线写入"); bWr->setProperty("accent", "primary"); connect(bWr, &QPushButton::clicked, this, &MainWindow::manualWritePlc);
    hbrw->addWidget(m_spTestAddr); hbrw->addWidget(m_edTestVal, 1); hbrw->addWidget(bRd); hbrw->addWidget(bWr);

    g2->addWidget(lblR, 0, 0, 1, 2);
    g2->addWidget(new QLabel("连续运行发包策略:"), 1, 0); g2->addWidget(m_cbPlcPolicy, 1, 1);
    g2->addWidget(new QLabel("定时发送间隔:"), 2, 0); g2->addWidget(m_spPlcInterval, 2, 1);
    g2->addWidget(new QLabel("外部触发寄存器地址:"), 3, 0); g2->addWidget(m_spTrigAddr, 3, 1);
    g2->addWidget(m_btnPlcTrig, 4, 0, 1, 2);
    g2->addWidget(m_lblPlcTxRx, 5, 0, 1, 2);
    g2->addWidget(new QLabel("单地址读写测试:"), 6, 0); g2->addLayout(hbrw, 6, 1);
    g2->setRowStretch(7, 1);
    hl->addWidget(right, 1);

    scroll->setWidget(w);
    return scroll;
}

QWidget* MainWindow::buildParamPanel() {
    auto* w = new QWidget();
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(6);

    auto* hdr = new QHBoxLayout();
    m_lblParamHeader = new QLabel("请在左侧流程图点击任意胶囊节点调参（支持学习模板与PLC自由地址配置）");
    m_lblParamHeader->setStyleSheet("background: rgba(18,20,26,215); color: #FF9E3D; padding: 5px 10px; border-left: 3px solid #FF7A00; border-radius: 4px; font-weight: bold;");
    auto* bStep = new QPushButton("▶ 单步执行到此节点");
    bStep->setProperty("accent", "primary");
    connect(bStep, &QPushButton::clicked, [this]() {
        if (m_activeNode) runSingleUpTo(m_activeNode->data.id);
    });
    hdr->addWidget(m_lblParamHeader, 1);
    hdr->addWidget(bStep);
    vl->addLayout(hdr);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_paramInner = new QWidget();
    m_paramForm = new QGridLayout(m_paramInner);
    m_paramForm->setAlignment(Qt::AlignTop);
    scroll->setWidget(m_paramInner);
    vl->addWidget(scroll, 1);
    return w;
}

QWidget* MainWindow::buildBottomStatusBar() {
    auto* bar = new QFrame();
    bar->setObjectName("BottomStatusBar");
    bar->setFixedHeight(28);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(10, 0, 12, 0);
    hl->setSpacing(12);

    m_lblRunState = new QLabel("● 就绪");
    m_lblRunState->setStyleSheet("color: #22C55E; font-weight: bold;");
    m_lblFlowMs = new QLabel("流程 0.00ms");
    m_lblModMs = new QLabel("模块 0.00ms");
    m_lblFpsBar = new QLabel("节拍 0.0 FPS");
    m_lblFpsBar->setStyleSheet("color: #FF9E3D; font-family: Consolas;");

    m_slFlowZoom = new QSlider(Qt::Horizontal);
    m_slFlowZoom->setRange(40, 180); m_slFlowZoom->setValue(100); m_slFlowZoom->setFixedWidth(96);
    m_lblFlowZoom = new QLabel("100%"); m_lblFlowZoom->setFixedWidth(36);
    connect(m_slFlowZoom, &QSlider::valueChanged, [this](int v) {
        m_lblFlowZoom->setText(QString("%1%").arg(v));
        m_flowCanvas->setZoomPercent(v);
    });

    m_lblTicker = new QLabel("▲ C++17 原生引擎就绪 · 高精度模板学习匹配 · 真实PLC自由地址映射");
    m_lblTicker->setStyleSheet("color: #9CA3AF;");
    auto* lblVer = new QLabel("V4.7.0 C++17");
    lblVer->setStyleSheet("color: #6B7280; font-family: Consolas;");

    hl->addWidget(m_lblRunState); hl->addWidget(m_lblFlowMs); hl->addWidget(m_lblModMs);
    hl->addWidget(m_lblFpsBar); hl->addWidget(m_slFlowZoom); hl->addWidget(m_lblFlowZoom);
    hl->addWidget(m_lblTicker, 1);
    hl->addWidget(lblVer);
    return bar;
}

// ==================== 预设方案加载 ====================
void MainWindow::loadPresetSolution(const QString& name) {
    QVector<FlowNodeData> nodes;
    QVector<FlowEdgeData> edges;

    auto mk = [](int id, const QString& type, double x, double y) {
        FlowNodeData d;
        d.id = id; d.typeKey = type;
        d.title = VisionOperatorRegistry::findTool(type).displayName;
        d.pos = QPointF(x, y);
        d.params = VisionOperatorRegistry::defaultParams(type);
        if (type == "calib_9pt") d.calibPoints = VisionOperatorRegistry::defaultCalibPoints();
        return d;
    };

    if (name.startsWith("流程1")) {
        auto n1 = mk(1, "camera_source", 180, 28);
        auto n2 = mk(2, "color_convert", 180, 114);
        auto n3 = mk(3, "high_prec_match", 180, 200);
        auto n4 = mk(4, "comm_plc_send", 180, 288);
        // 每一个数据都可自由定义独立PLC地址: D100(状态), D205(亚像素X), D310(亚像素Y), D500(旋转角度)
        n4.plcBindings = {
            {100, 3, "ok",    "INT16", 1.0, 0.0},
            {205, 3, "x",     "INT16", 1.0, 0.0},
            {310, 3, "y",     "INT16", 1.0, 0.0},
            {500, 3, "extra", "INT16", 1.0, 0.0},
        };
        nodes = {n1, n2, n3, n4};
        edges = {{1, 2, EdgeCondition::Always}, {2, 3, EdgeCondition::Always}, {3, 4, EdgeCondition::Always}};
    } else if (name.startsWith("流程2")) {
        auto n1 = mk(1, "camera_source", 180, 24);
        auto n2 = mk(2, "color_convert", 180, 106);
        auto n3 = mk(3, "img_filter", 180, 188);
        auto n4 = mk(4, "locate_blob", 180, 270);
        auto n5 = mk(5, "logic_judge", 180, 352);
        auto n6 = mk(6, "comm_plc_send", 65, 442);
        n6.plcBindings = {
            {100, 5, "ok",    "INT16", 1.0, 0.0},
            {205, 4, "x",     "INT16", 1.0, 0.0},
            {310, 4, "y",     "INT16", 1.0, 0.0},
            {500, 4, "extra", "INT16", 1.0, 0.0},
        };
        auto n7 = mk(7, "io_save_image", 295, 442);
        nodes = {n1, n2, n3, n4, n5, n6, n7};
        edges = {
            {1, 2, EdgeCondition::Always}, {2, 3, EdgeCondition::Always},
            {3, 4, EdgeCondition::Always}, {4, 5, EdgeCondition::Always},
            {5, 6, EdgeCondition::OnOk},   {5, 7, EdgeCondition::OnNg}
        };
    } else if (name.startsWith("流程3")) {
        auto n1 = mk(1, "camera_source", 180, 28);
        auto n2 = mk(2, "detect_color", 180, 116);
        auto n3 = mk(3, "logic_branch", 180, 204);
        auto n4 = mk(4, "comm_plc_send", 65, 300);
        n4.plcBindings = {{100, -1, "ok", "INT16", 1.0, 1.0}, {102, 2, "x", "INT16", 1.0, 0.0}, {104, 2, "y", "INT16", 1.0, 0.0}};
        auto n5 = mk(5, "comm_plc_send", 295, 300);
        n5.plcBindings = {{200, -1, "ok", "INT16", 1.0, 2.0}, {202, 2, "x", "INT16", 1.0, 0.0}, {204, 2, "y", "INT16", 1.0, 0.0}};
        nodes = {n1, n2, n3, n4, n5};
        edges = {{1, 2, EdgeCondition::Always}, {2, 3, EdgeCondition::Always}, {3, 4, EdgeCondition::OnOk}, {3, 5, EdgeCondition::OnNg}};
    } else {
        auto n1 = mk(1, "camera_source", 180, 24);
        auto n2 = mk(2, "color_convert", 180, 108);
        n2.params["mode"] = "CLAHE增强灰度 (暗光提亮)";
        auto n3 = mk(3, "locate_circle", 180, 192);
        auto n4 = mk(4, "calib_9pt", 180, 276);
        auto n5 = mk(5, "comm_plc_send", 180, 360);
        n5.plcBindings = {
            {100, 4, "ok",    "INT16",   1.0, 0.0},
            {102, 4, "x",     "FLOAT32", 1.0, 0.0},
            {104, 4, "y",     "FLOAT32", 1.0, 0.0},
            {106, 3, "extra", "INT16",   1.0, 0.0},
        };
        nodes = {n1, n2, n3, n4, n5};
        edges = {{1, 2, EdgeCondition::Always}, {2, 3, EdgeCondition::Always}, {3, 4, EdgeCondition::Always}, {4, 5, EdgeCondition::Always}};
    }

    m_flowCanvas->loadFromSpecs(nodes, edges, true);
    onFlowChanged();
    appendLog("已加载方案: " + name, "INFO");
}

void MainWindow::onAppendTool(const QString& typeKey) {
    FlowCapsuleItem* anchor = m_flowCanvas->selectedNode();
    if (!anchor && !m_flowCanvas->orderedNodes().isEmpty()) {
        anchor = m_flowCanvas->orderedNodes().last();
    }
    auto* item = m_flowCanvas->addNode(typeKey, QPointF(-9999, -9999), -1, anchor, true);
    onFlowChanged();
    appendLog(QString("已添加算子: %1 %2").arg(item->data.id - 1).arg(item->data.title), "INFO");
}

void MainWindow::onFlowChanged() {
    QVector<FlowNodeData> nodes;
    QVector<FlowEdgeData> edges;
    m_flowCanvas->exportSpecs(nodes, edges);
    m_engine->updateFlowSnapshot(nodes, edges);

    QVector<QPair<int, QString>> items;
    for (const auto& n : nodes) items.push_back({n.id, n.title});
    m_imagePanel->refreshOutputNodes(items);
    if (m_activePlcEditor && m_activeNode) m_activePlcEditor->rebuildTable();
}

void MainWindow::onNodeDoubleClicked(FlowCapsuleItem* node) {
    onNodeSelected(node);
    m_rightBotTabs->setCurrentIndex(1);
}

void MainWindow::onNodeSelected(FlowCapsuleItem* node) {
    m_activeNode = node;
    m_activePlcEditor = nullptr;
    while (QLayoutItem* ch = m_paramForm->takeAt(0)) {
        if (ch->widget()) ch->widget()->deleteLater();
        delete ch;
    }
    if (!node) {
        m_lblBreadcrumb->setText("流程1");
        m_lblParamHeader->setText("请在左侧流程图点击任意胶囊节点调参");
        m_imagePanel->canvas->setRoiRect(0, 0, 0, 0, false);
        return;
    }

    ToolMeta meta = VisionOperatorRegistry::findTool(node->data.typeKey);
    m_lblBreadcrumb->setText(QString("流程1 > %1 %2").arg(node->data.id - 1).arg(node->data.title));
    m_lblParamHeader->setText(QString("%1 %2  [%3]").arg(node->data.id - 1).arg(node->data.title).arg(categoryToChinese(meta.category)));

    if (meta.supportsRoi && node->data.params.value("use_search_roi", false).toBool()) {
        m_imagePanel->canvas->setRoiRect(
            node->data.params.value("sroi_x", 50).toInt(),
            node->data.params.value("sroi_y", 50).toInt(),
            node->data.params.value("sroi_w", 400).toInt(),
            node->data.params.value("sroi_h", 400).toInt(), true
        );
    } else {
        m_imagePanel->canvas->setRoiRect(0, 0, 0, 0, false);
    }

    const QString& t = node->data.typeKey;
    int row = 0;

    // ==================== 学习模板方式 UI 卡片 ====================
    if (meta.supportsTemplateLearn) {
        auto* card = new QFrame();
        card->setObjectName("GlassSubCard");
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(10, 8, 10, 8);
        cl->setSpacing(10);

        auto* lblThumb = new QLabel();
        lblThumb->setFixedSize(88, 88);
        lblThumb->setAlignment(Qt::AlignCenter);
        lblThumb->setStyleSheet("background: #14171D; border: 1px solid rgba(255,122,0,140); border-radius: 6px; color: #9CA3AF; font-size: 11px;");
        if (node->data.learnedModel.isLearned && !node->data.learnedModel.grayPatch.empty()) {
            const cv::Mat& gp = node->data.learnedModel.grayPatch;
            QImage qi(gp.data, gp.cols, gp.rows, static_cast<int>(gp.step), QImage::Format_Grayscale8);
            lblThumb->setPixmap(QPixmap::fromImage(qi.copy()).scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            lblThumb->setText("尚未学习\n模板模型");
        }
        cl->addWidget(lblThumb);

        auto* rightBox = new QVBoxLayout();
        rightBox->setSpacing(5);
        QString statusTxt = node->data.learnedModel.isLearned
            ? QString("✔ 已学习灰度特征模型: %1 × %2 px  (学习于 %3)")
                  .arg(node->data.learnedModel.width).arg(node->data.learnedModel.height).arg(node->data.learnedModel.learnedTime)
            : QString("⚠ 未学习模板：请先在上方图像框选目标，再点下方学习按钮");
        auto* lblInfo = new QLabel(statusTxt);
        lblInfo->setStyleSheet(node->data.learnedModel.isLearned ? "color: #4ADE80; font-weight: bold;" : "color: #FBBF24; font-weight: bold;");
        rightBox->addWidget(lblInfo);

        auto* btnRow = new QHBoxLayout();
        auto* btnDrawLearnRoi = new QPushButton("⬚ 1. 框选学习区域 (ROI)");
        connect(btnDrawLearnRoi, &QPushButton::clicked, [this]() {
            m_imagePanel->btnRoi->setChecked(true);
            appendLog("请在上方图像上拖拽框选要学习的目标模型区域", "INFO");
        });
        auto* btnDoLearn = new QPushButton("🎓 2. 学习当前区域为模板");
        btnDoLearn->setProperty("accent", "success");
        connect(btnDoLearn, &QPushButton::clicked, [this, node]() {
            learnTemplateFromCurrentRoi(node);
        });
        btnRow->addWidget(btnDrawLearnRoi);
        btnRow->addWidget(btnDoLearn);
        rightBox->addLayout(btnRow);
        cl->addLayout(rightBox, 1);

        m_paramForm->addWidget(card, row++, 0, 1, 2);
    }

    if (t == "comm_plc_send") {
        auto* cbOrder = new QComboBox(); cbOrder->addItems({"ABCD", "CDAB"});
        cbOrder->setCurrentText(node->data.params.value("word_order", "ABCD").toString());
        connect(cbOrder, &QComboBox::currentTextChanged, [this, node](const QString& v) {
            node->data.params["word_order"] = v; onFlowChanged();
        });
        m_paramForm->addWidget(new QLabel("32位字节序:"), row, 0);
        m_paramForm->addWidget(cbOrder, row++, 1);

        m_activePlcEditor = new FreeAddressPlcEditor(node, m_flowCanvas, m_cbProto->currentText());
        connect(m_activePlcEditor, &FreeAddressPlcEditor::bindingsChanged, this, &MainWindow::onFlowChanged);
        m_paramForm->addWidget(m_activePlcEditor, row++, 0, 1, 2);
        return;
    }

    if (t == "calib_9pt") {
        auto* tbl = new QTableWidget(node->data.calibPoints.size(), 4);
        tbl->setHorizontalHeaderLabels({"像素 X", "像素 Y", "物理 X(mm)", "物理 Y(mm)"});
        tbl->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tbl->setMinimumHeight(165);
        for (int r = 0; r < node->data.calibPoints.size(); ++r) {
            const auto& cp = node->data.calibPoints[r];
            double vals[4] = {cp.px, cp.py, cp.wx, cp.wy};
            for (int c = 0; c < 4; ++c) {
                auto* it = new QTableWidgetItem(QString::number(vals[c], 'f', 2));
                it->setTextAlignment(Qt::AlignCenter);
                tbl->setItem(r, c, it);
            }
        }
        connect(tbl, &QTableWidget::itemChanged, [this, node](QTableWidgetItem* it) {
            bool ok = false; double v = it->text().toDouble(&ok);
            if (!ok) return;
            auto& cp = node->data.calibPoints[it->row()];
            if (it->column() == 0) cp.px = v;
            else if (it->column() == 1) cp.py = v;
            else if (it->column() == 2) cp.wx = v;
            else if (it->column() == 3) cp.wy = v;
            onFlowChanged();
        });
        m_paramForm->addWidget(tbl, row++, 0, 1, 2);
        return;
    }

    for (auto it = node->data.params.begin(); it != node->data.params.end(); ++it) {
        QString key = it.key();
        if (key == "use_search_roi" || key.startsWith("sroi_")) continue;
        QVariant val = it.value();
        m_paramForm->addWidget(new QLabel(key + ":"), row, 0);

        if (key == "feature_mode" && t == "high_prec_match") {
            auto* cb = new QComboBox();
            cb->addItems({"灰度+边缘特征融合 (高精抗光)", "纯灰度高精NCC"});
            cb->setCurrentText(val.toString());
            connect(cb, &QComboBox::currentTextChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(cb, row++, 1);
        } else if (key == "mode" && t == "color_convert") {
            auto* cb = new QComboBox();
            cb->addItems({"彩色转灰度 (Gray)", "CLAHE增强灰度 (暗光提亮)", "提取 R 通道 (红色分量)", "提取 G 通道 (绿色分量)",
                          "提取 B 通道 (蓝色分量)", "提取 HSV-H (色相通道)", "提取 HSV-S (饱和度通道)", "提取 HSV-V (明度通道)"});
            cb->setCurrentText(val.toString());
            connect(cb, &QComboBox::currentTextChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(cb, row++, 1);
        } else if (key == "method" && t == "img_filter") {
            auto* cb = new QComboBox();
            cb->addItems({"高斯滤波", "中值滤波", "均值滤波", "双边滤波", "锐化增强"});
            cb->setCurrentText(val.toString());
            connect(cb, &QComboBox::currentTextChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(cb, row++, 1);
        } else if (key == "op" && t == "img_morph") {
            auto* cb = new QComboBox();
            cb->addItems({"开运算(去噪)", "闭运算(填孔)", "腐蚀", "膨胀", "顶帽", "黑帽"});
            cb->setCurrentText(val.toString());
            connect(cb, &QComboBox::currentTextChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(cb, row++, 1);
        } else if (key == "mode" && t == "img_transform") {
            auto* cb = new QComboBox();
            cb->addItems({"顺时针90度", "逆时针90度", "旋转180度", "水平镜像", "垂直镜像"});
            cb->setCurrentText(val.toString());
            connect(cb, &QComboBox::currentTextChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(cb, row++, 1);
        } else if (val.typeId() == QMetaType::Bool) {
            auto* chk = new QCheckBox(); chk->setChecked(val.toBool());
            connect(chk, &QCheckBox::toggled, [this, node, key](bool b) { node->data.params[key] = b; onFlowChanged(); });
            m_paramForm->addWidget(chk, row++, 1);
        } else if (val.typeId() == QMetaType::Double) {
            auto* sp = new QDoubleSpinBox(); sp->setRange(-100000.0, 100000.0); sp->setDecimals(3); sp->setValue(val.toDouble());
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, node, key](double v) { node->data.params[key] = v; onFlowChanged(); });
            m_paramForm->addWidget(sp, row++, 1);
        } else if (val.typeId() == QMetaType::Int) {
            auto* sp = new QSpinBox(); sp->setRange(-100000, 10000000); sp->setValue(val.toInt());
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), [this, node, key](int v) { node->data.params[key] = v; onFlowChanged(); });
            m_paramForm->addWidget(sp, row++, 1);
        } else {
            auto* ed = new QLineEdit(val.toString());
            connect(ed, &QLineEdit::textChanged, [this, node, key](const QString& s) { node->data.params[key] = s; onFlowChanged(); });
            m_paramForm->addWidget(ed, row++, 1);
        }
    }

    if (meta.supportsRoi && !meta.supportsTemplateLearn) {
        auto* bRoi = new QPushButton("⬚ 在上方图像上框选 / 调整 ROI 检测区");
        connect(bRoi, &QPushButton::clicked, [this]() { m_imagePanel->btnRoi->setChecked(true); });
        m_paramForm->addWidget(bRoi, row++, 0, 1, 2);
    }
}

void MainWindow::onRoiDrawn(int x, int y, int w, int h) {
    m_imagePanel->btnRoi->setChecked(false);
    if (!m_activeNode) return;
    ToolMeta meta = VisionOperatorRegistry::findTool(m_activeNode->data.typeKey);
    if (meta.supportsTemplateLearn) {
        // 对于学习模板节点，画ROI后提示可直接点击“学习当前区域为模板”
        appendLog(QString("已框选学习区域 (%1,%2, %3×%4)，请点击「🎓 2. 学习当前区域为模板」完成建模").arg(x).arg(y).arg(w).arg(h), "INFO");
        return;
    }
    if (!meta.supportsRoi) return;
    m_activeNode->data.params["use_search_roi"] = true;
    m_activeNode->data.params["sroi_x"] = x;
    m_activeNode->data.params["sroi_y"] = y;
    m_activeNode->data.params["sroi_w"] = w;
    m_activeNode->data.params["sroi_h"] = h;
    onNodeSelected(m_activeNode);
    onFlowChanged();
}

void MainWindow::learnTemplateFromCurrentRoi(FlowCapsuleItem* node) {
    cv::Mat src = !m_lastRawImg.empty() ? m_lastRawImg : m_staticImg;
    if (src.empty()) {
        QMessageBox::information(this, "提示", "当前无图像，请先点击「▶ 单次运行」或「▶▶ 连续运行」采集一帧图像。");
        return;
    }
    if (!m_imagePanel->canvas->hasRoiRect()) {
        QMessageBox::information(this, "提示", "请先点击「⬚ 1. 框选学习区域 (ROI)」并在上方图像中拖拽框选要学习的模板物体。");
        return;
    }
    QRect qr = m_imagePanel->canvas->currentRoiRect();
    cv::Rect cvRoi(qr.x(), qr.y(), qr.width(), qr.height());
    if (VisionOperatorRegistry::learnTemplateFromRoi(src, cvRoi, node->data.learnedModel)) {
        // 学习完成后清除学习框，使全图可自由搜索该学习模型
        m_imagePanel->canvas->setRoiRect(0, 0, 0, 0, false);
        node->data.params["use_search_roi"] = false;
        onNodeSelected(node);
        onFlowChanged();
        appendLog(QString("高精度模板学习成功！模型尺寸: %1×%2 px (已构建金字塔与边缘特征)").arg(qr.width()).arg(qr.height()), "OK");
        if (!m_engine->isRunning()) runSingleFrame();
    } else {
        QMessageBox::warning(this, "学习失败", "所选学习区域过小（需至少 12×12 像素），请重新框选。");
    }
}

// ==================== 相机、连续运行与单次运行 ====================
void MainWindow::applyCameraSettings(bool silent) {
    QString resTxt = m_cbRes->currentText();
    int w = 1920, h = 1080;
    if (resTxt.startsWith("640")) { w = 640; h = 480; }
    else if (resTxt.startsWith("1280")) { w = 1280; h = 720; }
    else if (resTxt.startsWith("2560")) { w = 2560; h = 1440; }
    else if (resTxt.startsWith("3840")) { w = 3840; h = 2160; }

    m_camCtrl.configure(
        m_spCamId->value(), w, h,
        m_cbCamFmt->currentText(), m_cbCamBackend->currentText(),
        m_chkManualExp->isChecked(), static_cast<double>(m_spExp->value()),
        m_spGain->value(), m_spGamma->value(), m_spBias->value()
    );
    if (!silent) {
        appendLog(QString("相机配置已同步: CAM#%1 %2×%3 | Gamma:%4").arg(m_spCamId->value()).arg(w).arg(h).arg(m_spGamma->value(), 0, 'f', 2), "INFO");
    }
}

void MainWindow::openNativeCameraProps() {
    applyCameraSettings(true);
    if (!m_camCtrl.openDriverSettings()) {
        QMessageBox::warning(this, "提示", "未能调出相机底层驱动属性页，请确认USB相机已连接。");
    }
}

void MainWindow::reopenCamera() {
    m_camCtrl.release();
    applyCameraSettings(true);
    cv::Mat f = m_camCtrl.grabFrame();
    if (!f.empty()) {
        m_lblActualRes->setText(QString("实际输出分辨率：%1 × %2").arg(f.cols).arg(f.rows));
        m_imagePanel->canvas->updateFrames(f, f, false, false, -1);
        appendLog(QString("相机重连成功: %1×%2").arg(f.cols).arg(f.rows), "OK");
    } else {
        appendLog("相机重连失败，请检查相机索引号或驱动后端", "ERR");
    }
}

void MainWindow::runSingleFrame() { runSingleUpTo(-1); }

void MainWindow::runSingleUpTo(int stopNodeId) {
    if (m_engine->isRunning()) {
        triggerManualPlcSend();
        return;
    }
    applyCameraSettings(true);
    onFlowChanged();
    QString src = m_cbSource->currentText();
    m_engine->setSourceMode(src.startsWith("视频") ? "视频文件" : src, m_videoPath);
    m_engine->runSingleCycle(m_staticImg, m_staticName, stopNodeId);
}

void MainWindow::toggleContinuousRun() {
    if (m_engine->isRunning()) {
        m_engine->stopContinuous();
    } else {
        applyCameraSettings(true);
        onFlowChanged();
        applyPlcPolicy();
        QString src = m_cbSource->currentText();
        m_engine->setSourceMode(src.startsWith("视频") ? "视频文件" : src, m_videoPath);
        m_engine->setCycleInterval(m_spCycleMs->value());
        m_engine->startContinuous();
    }
}

void MainWindow::onRunStateChanged(bool running, const QString& text) {
    m_btnContRun->blockSignals(true);
    m_btnContRun->setChecked(running);
    m_btnContRun->setText(running ? "⏹ 停止运行" : "▶▶ 连续运行");
    m_btnContRun->setProperty("accent", running ? "danger" : "primary");
    m_btnContRun->style()->unpolish(m_btnContRun);
    m_btnContRun->style()->polish(m_btnContRun);
    m_btnContRun->blockSignals(false);
    m_lblRunState->setText(running ? "● 连续运行中" : "● 运行完成");
    appendLog(text, running ? "OK" : "WARN");
}

void MainWindow::onEngineError(const QString& msg) {
    appendLog(msg, "ERR");
    if (QMessageBox::question(this, "采集提示", msg + "\n\n是否切换为内置【仿真测试】源继续测试？") == QMessageBox::Yes) {
        m_cbSource->setCurrentText("仿真测试");
        toggleContinuousRun();
    }
}

void MainWindow::onCycleCompleted(const cv::Mat& rawBgr, const cv::Mat& visBgr, const FlowCycleSummary& summary) {
    m_lastRawImg = rawBgr;
    m_lastVisImg = visBgr;
    if (m_camCtrl.actualWidth() > 0) {
        m_lblActualRes->setText(QString("实际输出分辨率：%1 × %2").arg(m_camCtrl.actualWidth()).arg(m_camCtrl.actualHeight()));
    }
    m_imagePanel->canvas->updateFrames(rawBgr, m_rawOnly ? rawBgr : visBgr,
                                       m_engine->isRunning(), false, summary.finalOk ? 1 : 0);

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_engine->isRunning() || (nowMs - m_lastUiUpdateMs >= 140)) {
        m_lastUiUpdateMs = nowMs;
        m_flowCanvas->applyTelemetry(summary);
        m_lblFlowMs->setText(QString("流程 %1ms").arg(summary.totalMs, 0, 'f', 2));
        m_lblFpsBar->setText(summary.fps > 0 ? QString("节拍 %1 FPS").arg(summary.fps, 0, 'f', 1) : "单次完成");
        if (m_activeNode && summary.nodeStates.contains(m_activeNode->data.id)) {
            m_lblModMs->setText(QString("模块 %1ms").arg(summary.nodeStates[m_activeNode->data.id].elapsedMs, 0, 'f', 2));
        }

        m_statTotal++;
        if (summary.finalOk) m_statOk++;
        else m_statNg++;
        m_yieldRing->setMetrics(m_statTotal, m_statOk, m_statNg);
        m_prodRing->setMetrics(m_statTotal, m_statOk, m_statNg);
        m_latencyChart->pushSample(summary.totalMs, summary.finalOk);
        m_prodChart->pushSample(summary.totalMs, summary.finalOk);

        m_lblKpiFps->setText(QString::number(summary.fps, 'f', 1));
        m_lblKpiTot->setText(QString::number(m_statTotal));
        m_lblKpiOk->setText(QString::number(m_statOk));
        m_lblKpiNg->setText(QString::number(m_statNg));
        m_lblProdFps->setText(QString::number(summary.fps, 'f', 1));
        m_lblProdTot->setText(QString::number(m_statTotal));
        m_lblProdOk->setText(QString::number(m_statOk));
        m_lblProdNg->setText(QString::number(m_statNg));
        m_lblProdMsg->setText(QString("[%1] %2ms\n%3").arg(summary.finalOk ? "OK" : "NG").arg(summary.totalMs, 0, 'f', 1).arg(summary.finalMsg));

        QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        m_resultTable->setRowCount(summary.executionOrder.size());
        for (int r = 0; r < summary.executionOrder.size(); ++r) {
            int nid = summary.executionOrder[r];
            const auto& st = summary.nodeStates[nid];
            auto* nItem = m_flowCanvas->nodeById(nid);
            QString title = nItem ? nItem->data.title : QString::number(nid);
            m_resultTable->setRowHeight(r, 25);
            auto* c0 = new QTableWidgetItem(QString::number(r + 1)); c0->setTextAlignment(Qt::AlignCenter);
            auto* c1 = new QTableWidgetItem(ts); c1->setTextAlignment(Qt::AlignCenter);
            auto* c2 = new QTableWidgetItem(QString("[%1] %2 %3: %4 (%5ms)").arg(st.status).arg(nid - 1).arg(title).arg(st.result.msg).arg(st.elapsedMs, 0, 'f', 1));
            c2->setForeground(st.status == "OK" ? QColor("#22C55E") : QColor("#EF4444"));
            m_resultTable->setItem(r, 0, c0);
            m_resultTable->setItem(r, 1, c1);
            m_resultTable->setItem(r, 2, c2);
        }
    }
}

void MainWindow::toggleRunMode(bool checked) {
    m_runMode = checked;
    m_catRail->setVisible(!checked);
    m_flowPanel->setVisible(!checked);
    m_rightBotTabs->parentWidget()->setVisible(!checked);
    m_prodSidePanel->setVisible(checked);
    if (checked) {
        m_rightTopTabs->setCurrentIndex(0);
        m_btnRunMode->setText("🛠 编辑模式");
        m_mainSplitter->setSizes({0, 1080, 390});
    } else {
        m_btnRunMode->setText("🖥 运行模式");
        m_mainSplitter->setSizes({640, 810, 0});
    }
    QTimer::singleShot(50, m_imagePanel->canvas, &VmImageCanvasWidget::fitInView);
}

// ==================== 方案保存/加载（含学习模板模型 Base64 序列化） ====================
void MainWindow::saveSolutionJson() {
    QString p = QFileDialog::getSaveFileName(this, "保存视觉方案", "visionmaster_cpp.json", "JSON (*.json)");
    if (p.isEmpty()) return;
    QVector<FlowNodeData> nodes; QVector<FlowEdgeData> edges;
    m_flowCanvas->exportSpecs(nodes, edges);
    QJsonArray nArr;
    for (const auto& n : nodes) {
        QJsonObject obj;
        obj["id"] = n.id; obj["type"] = n.typeKey; obj["x"] = n.pos.x(); obj["y"] = n.pos.y(); obj["enabled"] = n.enabled;
        obj["params"] = QJsonObject::fromVariantMap(n.params);
        if (n.learnedModel.isLearned && !n.learnedModel.grayPatch.empty()) {
            std::vector<uchar> pngBuf;
            cv::imencode(".png", n.learnedModel.grayPatch, pngBuf);
            QByteArray ba(reinterpret_cast<const char*>(pngBuf.data()), static_cast<int>(pngBuf.size()));
            obj["learned_template_b64"] = QString::fromLatin1(ba.toBase64());
        }
        QJsonArray bArr;
        for (const auto& b : n.plcBindings) {
            QJsonObject bo;
            bo["target_addr"] = b.targetAddr; bo["source_node_id"] = b.sourceNodeId;
            bo["field"] = b.field; bo["dtype"] = b.dataType; bo["scale"] = b.scale; bo["const_val"] = b.constVal;
            bArr.append(bo);
        }
        obj["plc_bindings"] = bArr;
        nArr.append(obj);
    }
    QJsonArray eArr;
    for (const auto& e : edges) {
        QJsonObject eo;
        eo["src"] = e.srcId; eo["dst"] = e.dstId; eo["cond"] = static_cast<int>(e.condition);
        eArr.append(eo);
    }
    QJsonObject root; root["nodes"] = nArr; root["edges"] = eArr;
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        appendLog("方案(含已学习模板)已保存: " + p, "OK");
    }
}

void MainWindow::openSolutionJson() {
    QString p = QFileDialog::getOpenFileName(this, "导入视觉方案", "", "JSON (*.json)");
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QVector<FlowNodeData> nodes; QVector<FlowEdgeData> edges;
    for (const auto& v : root["nodes"].toArray()) {
        QJsonObject o = v.toObject();
        FlowNodeData d;
        d.id = o["id"].toInt(); d.typeKey = o["type"].toString();
        d.title = VisionOperatorRegistry::findTool(d.typeKey).displayName;
        d.pos = QPointF(o["x"].toDouble(), o["y"].toDouble());
        d.enabled = o["enabled"].toBool(true);
        d.params = o["params"].toObject().toVariantMap();
        QString b64 = o["learned_template_b64"].toString();
        if (!b64.isEmpty()) {
            QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
            std::vector<uchar> buf(raw.begin(), raw.end());
            d.learnedModel.grayPatch = cv::imdecode(buf, cv::IMREAD_GRAYSCALE);
            d.learnedModel.learnedTime = "已从方案恢复";
            VisionOperatorRegistry::rebuildModelPyramids(d.learnedModel);
        }
        for (const auto& bv : o["plc_bindings"].toArray()) {
            QJsonObject bo = bv.toObject();
            d.plcBindings.push_back({
                bo["target_addr"].toInt(100), bo["source_node_id"].toInt(0),
                bo["field"].toString("ok"), bo["dtype"].toString("INT16"),
                bo["scale"].toDouble(1.0), bo["const_val"].toDouble(0.0)
            });
        }
        nodes.push_back(d);
    }
    for (const auto& v : root["edges"].toArray()) {
        QJsonObject o = v.toObject();
        edges.push_back({o["src"].toInt(), o["dst"].toInt(), static_cast<EdgeCondition>(o["cond"].toInt(0))});
    }
    m_flowCanvas->loadFromSpecs(nodes, edges, true);
    onFlowChanged();
    appendLog("已导入方案: " + p, "OK");
}

void MainWindow::openImageFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择本地图片文件夹");
    if (!dir.isEmpty()) {
        loadFolderPath(dir);
        m_cbSource->setCurrentText("本地图库");
    }
}

void MainWindow::openVideoFile() {
    QString p = QFileDialog::getOpenFileName(this, "选择视频文件", "", "Video (*.mp4 *.avi *.mkv *.mov)");
    if (!p.isEmpty()) {
        m_videoPath = p;
        m_cbSource->setCurrentText("视频文件 (MP4)");
        appendLog("已加载视频: " + p, "OK");
    }
}

void MainWindow::loadFolderPath(const QString& folder) {
    QDir d(folder);
    QStringList files = d.entryList({"*.png", "*.jpg", "*.jpeg", "*.bmp"}, QDir::Files, QDir::Name);
    m_imagePaths.clear();
    m_fileList->clear();
    for (const QString& fn : files) {
        m_imagePaths << d.absoluteFilePath(fn);
        m_fileList->addItem(fn);
    }
    m_engine->setFolderImages(m_imagePaths, 0);
    if (!m_imagePaths.isEmpty()) m_fileList->setCurrentRow(0);
}

void MainWindow::selectImageRow(int row) {
    if (row < 0 || row >= m_imagePaths.size()) return;
    QFile f(m_imagePaths[row]);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray ba = f.readAll();
        std::vector<uchar> buf(ba.begin(), ba.end());
        cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
        if (!img.empty()) {
            m_staticImg = img;
            m_staticName = QFileInfo(m_imagePaths[row]).fileName();
            m_engine->setFolderImages(m_imagePaths, row);
            if (!m_engine->isRunning()) m_imagePanel->canvas->updateFrames(img, img, false, false, -1);
        }
    }
}

void MainWindow::stepImage(int delta) {
    if (m_imagePaths.isEmpty()) return;
    int next = (m_fileList->currentRow() + delta + m_imagePaths.size()) % m_imagePaths.size();
    m_fileList->setCurrentRow(next);
}

void MainWindow::saveSnapshot() {
    cv::Mat img = !m_lastVisImg.empty() ? m_lastVisImg : m_staticImg;
    if (img.empty()) return;
    QString p = QFileDialog::getSaveFileName(this, "保存抓拍图像", "snapshot.png", "PNG (*.png);;JPG (*.jpg)");
    if (!p.isEmpty()) {
        std::vector<uchar> buf;
        cv::imencode(".png", img, buf);
        QFile f(p);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size()));
            appendLog("抓拍已保存: " + p, "OK");
        }
    }
}

// ==================== 真实 PLC 硬件通信（无模拟PLC） ====================
void MainWindow::applyPlcPolicy() {
    QString modes[] = {"EVERY_CYCLE", "MANUAL_TRIG", "INTERVAL", "OFF"};
    m_engine->setPlcSendPolicy(modes[m_cbPlcPolicy->currentIndex()], m_spPlcInterval->value());
}

void MainWindow::triggerManualPlcSend() {
    if (m_engine->isRunning()) m_engine->requestOnePlcSend();
    else runSingleFrame();
}

void MainWindow::onRefreshPlcStatus() {
    if (m_lblPlcTxRx) {
        m_lblPlcTxRx->setText(QString("通信统计: TX %1 帧  |  RX %2 帧  |  状态: %3")
                                  .arg(m_plc.txCount()).arg(m_plc.rxCount())
                                  .arg(m_plc.isConnected() ? "已连接" : "未连接"));
    }
}

void MainWindow::toggleConnectPlc() {
    if (m_plc.isConnected()) {
        m_pollTimer->stop();
        m_plc.disconnectPlc();
        m_btnConn->setText("连接 PLC");
        m_lblPlcBadge->setText("● PLC 未连接");
        m_lblPlcBadge->setStyleSheet("background: rgba(42,24,28,220); color: #FCA5A5; border: 1px solid #EF4444; border-radius: 11px; padding: 3px 10px; font-weight: bold;");
        appendLog("PLC 已断开", "WARN");
        return;
    }
    QString err;
    QString proto = m_cbProto->currentText();
    if (m_plc.connectPlc(proto, m_edHost->text(), m_spPort->value(), m_spUnit->value(),
                         m_spBaud->value(), m_spDb->value(), m_spRack->value(), m_spSlot->value(), &err)) {
        m_btnConn->setText("断开 PLC");
        m_lblPlcBadge->setText("● " + proto);
        m_lblPlcBadge->setStyleSheet("background: rgba(18,42,30,220); color: #86EFAC; border: 1px solid #22C55E; border-radius: 11px; padding: 3px 10px; font-weight: bold;");
        appendLog("真实 PLC 连接成功: " + proto, "OK");
        if (m_activePlcEditor) m_activePlcEditor->rebuildTable();
    } else {
        QMessageBox::warning(this, "PLC 连接失败", err);
        appendLog("PLC 连接失败: " + err, "ERR");
    }
}

void MainWindow::manualReadPlc() {
    QVector<int16_t> vals; QString err;
    if (m_plc.readRegisters(m_spTestAddr->value(), 6, vals, &err)) {
        QStringList s; for (int16_t v : vals) s << QString::number(v);
        appendLog(QString("读取 @%1 返回: [%2]").arg(m_spTestAddr->value()).arg(s.join(", ")), "OK");
    } else {
        appendLog("读取失败: " + err, "ERR");
    }
}

void MainWindow::manualWritePlc() {
    QString err;
    if (m_plc.protocol() == "TCP Socket") {
        if (m_plc.sendRawText(m_edTestVal->text(), nullptr, &err)) appendLog("TCP自由口发送成功", "OK");
        else appendLog("发送失败: " + err, "ERR");
        return;
    }
    QVector<uint16_t> words;
    for (const QString& part : m_edTestVal->text().split(",", Qt::SkipEmptyParts)) {
        words.push_back(static_cast<uint16_t>(part.trimmed().toInt()));
    }
    if (m_plc.writeRegisters(m_spTestAddr->value(), words, &err)) {
        appendLog(QString("写入 @%1 成功").arg(m_spTestAddr->value()), "OK");
    } else {
        appendLog("写入失败: " + err, "ERR");
    }
}

void MainWindow::togglePlcTriggerWatch() {
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
        m_btnPlcTrig->setText("🔗 开启外部PLC触发监听 (0→1上升沿)");
    } else {
        if (!m_plc.isConnected()) {
            QMessageBox::information(this, "提示", "请先连接真实工业 PLC 硬件。");
            return;
        }
        m_lastTrigVal = 0;
        m_pollTimer->start(80);
        m_btnPlcTrig->setText("⏹ 停止外部PLC触发监听");
        appendLog(QString("已开启外部 PLC 触发监听 (@%1)").arg(m_spTrigAddr->value()), "OK");
    }
}

void MainWindow::onPlcPoll() {
    QVector<int16_t> vals; QString err;
    int addr = m_spTrigAddr->value();
    if (!m_plc.readRegisters(addr, 1, vals, &err) || vals.isEmpty()) return;
    int cur = (vals[0] == 1) ? 1 : 0;
    if (cur == 1 && m_lastTrigVal == 0) {
        if (m_engine->isRunning()) m_engine->requestOnePlcSend();
        else runSingleFrame();
        m_plc.writeRegisters(addr, {0}, nullptr);
    }
    m_lastTrigVal = cur;
}

void MainWindow::appendLog(const QString& msg, const QString& level) {
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString col = (level == "OK") ? "#4ADE80" : (level == "ERR" ? "#F87171" : (level == "WARN" ? "#FBBF24" : "#CBD5E1"));
    m_logBox->append(QString("<span style='color:#6B7280;'>[%1]</span> <span style='color:%2;'>%3</span>").arg(ts, col, msg.toHtmlEscaped()));
    m_lblTicker->setText(QString("▲ %1  %2").arg(ts, msg.left(72)));
}
