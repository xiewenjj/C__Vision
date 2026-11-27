#include "FlowCanvas.h"
#include "VisionEngine.h"
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMenu>
#include <cmath>
#include <deque>
#include <algorithm>

FlowEdgeItem::FlowEdgeItem(FlowCapsuleItem* src, FlowCapsuleItem* dst, EdgeCondition cond)
    : srcNode(src), dstNode(dst), condition(cond) {
    setZValue(1.0);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    updatePath();
}

void FlowEdgeItem::updatePath() {
    if (!srcNode || !dstNode) return;
    QPointF p1 = srcNode->outPortScenePos();
    QPointF p2 = dstNode->inPortScenePos();
    QPainterPath path(p1);
    double dy = std::max(32.0, std::abs(p2.y() - p1.y()) * 0.5);
    path.cubicTo(QPointF(p1.x(), p1.y() + dy), QPointF(p2.x(), p2.y() - dy), p2);
    setPath(path);
}

void FlowEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QColor baseCol("#FF9E3D");
    if (condition == EdgeCondition::OnOk) baseCol = QColor("#22C55E");
    else if (condition == EdgeCondition::OnNg) baseCol = QColor("#EF4444");
    if (isSelected()) baseCol = QColor("#FFB347");

    QColor lineCol = baseCol;
    if (!active && srcNode && (srcNode->status == "OK" || srcNode->status == "NG" || srcNode->status == "SKIP")) {
        lineCol.setAlpha(95);
    }

    QPainterPath p = path();
    painter->setPen(QPen(lineCol, (isSelected() || active) ? 2.8 : 1.8));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(p);

    if (active) {
        for (double off : {0.0, 0.5}) {
            QPointF pt = p.pointAtPercent(std::fmod(flowPhase + off, 1.0));
            QColor glow = baseCol; glow.setAlpha(125);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QBrush(glow));
            painter->drawEllipse(pt, 5.5, 5.5);
            painter->setBrush(QBrush(QColor("#FFFFFF")));
            painter->drawEllipse(pt, 2.4, 2.4);
        }
    }

    if (dstNode) {
        QPointF p2 = dstNode->inPortScenePos();
        QPointF tip(p2.x(), p2.y() - 2.0);
        QPolygonF arrow;
        arrow << tip << QPointF(tip.x() - 4.5, tip.y() - 8.5) << QPointF(tip.x() + 4.5, tip.y() - 8.5);
        painter->setBrush(QBrush(lineCol));
        painter->setPen(Qt::NoPen);
        painter->drawPolygon(arrow);
    }

    if (condition != EdgeCondition::Always) {
        QPointF mid = p.pointAtPercent(0.5);
        QRectF badge(mid.x() - 15, mid.y() - 9, 30, 18);
        painter->setPen(QPen(baseCol, 1.2));
        painter->setBrush(QBrush(QColor(24, 27, 34, 235)));
        painter->drawRoundedRect(badge, 9, 9);
        painter->setPen(baseCol);
        painter->setFont(QFont("Microsoft YaHei", 7, QFont::Bold));
        painter->drawText(badge, Qt::AlignCenter, condition == EdgeCondition::OnOk ? "OK" : "NG");
    }
}

FlowCapsuleItem::FlowCapsuleItem(const FlowNodeData& d) : data(d) {
    setZValue(5.0);
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setPos(d.pos);
}

QRectF FlowCapsuleItem::boundingRect() const {
    return QRectF(-8.0, -kPortR - 8.0, kWidth + 16.0, kHeight + 2.0 * kPortR + 16.0);
}

bool FlowCapsuleItem::isOnOutPort(const QPointF& scenePos) const {
    QPointF d = outPortScenePos() - scenePos;
    return std::hypot(d.x(), d.y()) <= 15.0;
}

bool FlowCapsuleItem::isOnInPort(const QPointF& scenePos) const {
    QPointF d = inPortScenePos() - scenePos;
    return std::hypot(d.x(), d.y()) <= 16.0;
}

void FlowCapsuleItem::hoverEnterEvent(QGraphicsSceneHoverEvent* e) {
    m_hovered = true; update(); QGraphicsItem::hoverEnterEvent(e);
}
void FlowCapsuleItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e) {
    m_hovered = false; update(); QGraphicsItem::hoverLeaveEvent(e);
}

QVariant FlowCapsuleItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionHasChanged) {
        data.pos = pos();
        for (auto* e : edges) {
            if (e) e->updatePath();
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

void FlowCapsuleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect(0, 0, kWidth, kHeight);
    double radius = kHeight / 2.0;
    bool selected = isSelected();

    if (selected) {
        QColor glow("#FF7A00");
        glow.setAlpha(68);
        painter->setPen(QPen(glow, 6.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(-1.5, -1.5, 1.5, 1.5), radius + 1.5, radius + 1.5);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(QColor(0, 0, 0, 105)));
    painter->drawRoundedRect(rect.translated(0, 3), radius, radius);

    QLinearGradient grad(0, 0, 0, kHeight);
    QColor txtCol("#14171F");
    if (!data.enabled || status == "SKIP" || status == "DISABLED") {
        grad.setColorAt(0.0, QColor(58, 63, 74, 185));
        grad.setColorAt(1.0, QColor(38, 42, 50, 200));
        txtCol = QColor("#8E97A8");
    } else {
        grad.setColorAt(0.0, selected ? QColor(246, 248, 252, 240) : QColor(232, 237, 245, 226));
        grad.setColorAt(0.55, selected ? QColor(220, 226, 236, 235) : QColor(204, 212, 224, 220));
        grad.setColorAt(1.0, selected ? QColor(205, 213, 226, 238) : QColor(186, 195, 210, 224));
    }

    QColor bdCol = selected ? QColor("#FF7A00") : (m_hovered ? QColor("#FFB347") : QColor(255, 255, 255, 115));
    painter->setPen(QPen(bdCol, selected ? 2.3 : 1.3));
    painter->setBrush(QBrush(grad));
    painter->drawRoundedRect(rect, radius, radius);

    QLinearGradient sheen(0, 2, 0, kHeight * 0.45);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 125));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(sheen));
    painter->drawRoundedRect(QRectF(10, 2, kWidth - 20, kHeight * 0.42), radius * 0.6, radius * 0.6);

    ToolMeta meta = VisionOperatorRegistry::findTool(data.typeKey);
    QColor badgeCol(categoryColorHex(meta.category));
    if (!data.enabled || status == "DISABLED") badgeCol = QColor("#4B5563");
    else if (status == "OK") badgeCol = QColor("#22C55E");
    else if (status == "NG") badgeCol = QColor("#EF4444");

    QRectF iconR(7, 7, 34, 34);
    painter->setPen(QPen(QColor(255, 255, 255, 165), 1.2));
    painter->setBrush(QBrush(badgeCol));
    painter->drawEllipse(iconR);
    painter->setPen(QColor("#FFFFFF"));
    painter->setFont(QFont("Consolas", 8, QFont::Bold));
    painter->drawText(iconR, Qt::AlignCenter, categoryIconText(meta.category));

    painter->setPen(txtCol);
    painter->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    painter->drawText(QRectF(47, 5, kWidth - 58, 22), Qt::AlignVCenter | Qt::AlignLeft,
                      QString("%1 %2").arg(data.id - 1).arg(data.title));

    QString sub = !summaryText.isEmpty()
        ? QString("%1 · %2ms").arg(summaryText).arg(elapsedMs, 0, 'f', 1)
        : ((status == "OK" || status == "NG") ? QString("%1 ms").arg(elapsedMs, 0, 'f', 1) : categoryToChinese(meta.category));
    painter->setPen(data.enabled ? QColor("#475569") : QColor("#6B7280"));
    painter->setFont(QFont("Consolas", 7));
    painter->drawText(QRectF(47, 25, kWidth - 58, 16), Qt::AlignVCenter | Qt::AlignLeft, sub.left(22));

    if (selected || m_hovered) {
        painter->setPen(QPen(QColor("#FF7A00"), 1.5));
        painter->setBrush(QBrush(QColor("#1E2128")));
        painter->drawEllipse(inPortLocal(), kPortR, kPortR);
        painter->setBrush(QBrush(QColor("#FF7A00")));
        painter->drawEllipse(outPortLocal(), kPortR, kPortR);
    }
}

FlowMinimapWidget::FlowMinimapWidget(FlowCanvasView* canvas, QWidget* parent)
    : QWidget(parent), m_canvas(canvas) {
    setFixedSize(168, 118);
    setCursor(Qt::PointingHandCursor);
}

QRectF FlowMinimapWidget::worldBounds() const {
    if (!m_canvas || m_canvas->nodesMap().isEmpty()) {
        return QRectF(-50, -50, 700, 560);
    }
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (auto it = m_canvas->nodesMap().begin(); it != m_canvas->nodesMap().end(); ++it) { auto* n = it.value();
        minX = std::min(minX, n->pos().x());
        maxX = std::max(maxX, n->pos().x() + FlowCapsuleItem::kWidth);
        minY = std::min(minY, n->pos().y());
        maxY = std::max(maxY, n->pos().y() + FlowCapsuleItem::kHeight);
    }
    minX -= 160; maxX += 160; minY -= 120; maxY += 140;
    return QRectF(minX, minY, std::max(720.0, maxX - minX), std::max(520.0, maxY - minY));
}

void FlowMinimapWidget::panToPos(const QPointF& pos) {
    if (!m_canvas) return;
    QRectF wb = worldBounds();
    double rx = std::max(0.0, std::min(1.0, (pos.x() - 8.0) / std::max(1.0, width() - 16.0)));
    double ry = std::max(0.0, std::min(1.0, (pos.y() - 8.0) / std::max(1.0, height() - 16.0)));
    m_canvas->centerOn(QPointF(wb.left() + rx * wb.width(), wb.top() + ry * wb.height()));
    update();
}

void FlowMinimapWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) { m_dragging = true; panToPos(e->position()); e->accept(); }
}
void FlowMinimapWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) { panToPos(e->position()); e->accept(); }
}
void FlowMinimapWidget::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void FlowMinimapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r(0.5, 0.5, width() - 1, height() - 1);
    p.setPen(QPen(QColor(255, 255, 255, 38), 1.0));
    p.setBrush(QBrush(QColor(24, 27, 34, 215)));
    p.drawRoundedRect(r, 6, 6);

    QRectF wb = worldBounds();
    double pad = 8.0;
    double sc = std::min((width() - 2 * pad) / std::max(1.0, wb.width()),
                         (height() - 2 * pad) / std::max(1.0, wb.height()));
    double ox = pad + ((width() - 2 * pad) - wb.width() * sc) / 2.0;
    double oy = pad + ((height() - 2 * pad) - wb.height() * sc) / 2.0;
    auto mapPt = [&](double wx, double wy) {
        return QPointF(ox + (wx - wb.left()) * sc, oy + (wy - wb.top()) * sc);
    };

    for (auto* e : m_canvas->edgesList()) {
        QPointF p1 = mapPt(e->srcNode->pos().x() + FlowCapsuleItem::kWidth / 2.0, e->srcNode->pos().y() + FlowCapsuleItem::kHeight);
        QPointF p2 = mapPt(e->dstNode->pos().x() + FlowCapsuleItem::kWidth / 2.0, e->dstNode->pos().y());
        p.setPen(QPen(QColor(255, 158, 61, 175), 1.0));
        p.drawLine(p1, p2);
    }

    for (auto it = m_canvas->nodesMap().begin(); it != m_canvas->nodesMap().end(); ++it) { auto* n = it.value();
        QPointF tl = mapPt(n->pos().x(), n->pos().y());
        double nw = std::max(14.0, FlowCapsuleItem::kWidth * sc);
        double nh = std::max(6.0, FlowCapsuleItem::kHeight * sc);
        QColor c = n->isSelected() ? QColor("#FF7A00") : (n->status == "OK" ? QColor("#22C55E") : (n->status == "NG" ? QColor("#EF4444") : QColor("#CBD5E1")));
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(c));
        p.drawRoundedRect(QRectF(tl.x(), tl.y(), nw, nh), nh / 2.0, nh / 2.0);
    }

    QRectF vpRect = m_canvas->mapToScene(m_canvas->viewport()->rect()).boundingRect();
    QRectF vr = QRectF(mapPt(vpRect.left(), vpRect.top()), mapPt(vpRect.right(), vpRect.bottom())).intersected(r.adjusted(2, 2, -2, -2));
    p.setPen(QPen(QColor("#FF7A00"), 1.6));
    p.setBrush(QBrush(QColor(255, 122, 0, 26)));
    p.drawRect(vr);
}

FlowCanvasView::FlowCanvasView(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(-600, -400, 2600, 2200, this);
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFrameShape(QFrame::NoFrame);

    m_minimap = new FlowMinimapWidget(this, viewport());
    m_minimap->show();

    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, &FlowCanvasView::tickAnimations);
    m_animTimer->start(45);

    connect(this, &QGraphicsView::customContextMenuRequested, this, &FlowCanvasView::onContextMenu);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &FlowCanvasView::onSelectionChanged);
}

void FlowCanvasView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    placeMinimap();
}

void FlowCanvasView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::placeMinimap() {
    if (!m_minimap) return;
    m_minimap->move(std::max(8, viewport()->width() - m_minimap->width() - 12),
                    std::max(8, viewport()->height() - m_minimap->height() - 12));
    m_minimap->update();
}

void FlowCanvasView::tickAnimations() {
    bool dirty = false;
    for (auto* e : m_edges) {
        if (e && e->active) {
            e->flowPhase = std::fmod(e->flowPhase + 0.036, 1.0);
            dirty = true;
        }
    }
    if (dirty) viewport()->update();
}

void FlowCanvasView::setZoomPercent(int pct) {
    pct = std::max(35, std::min(200, pct));
    m_zoom = pct / 100.0;
    resetTransform();
    scale(m_zoom, m_zoom);
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor("#252830"));
    int small = 18, big = 90;
    int left = static_cast<int>(rect.left()) - (static_cast<int>(rect.left()) % small);
    int top = static_cast<int>(rect.top()) - (static_cast<int>(rect.top()) % small);
    painter->setPen(QPen(QColor("#2C3039"), 1));
    for (int x = left; x < static_cast<int>(rect.right()); x += small)
        painter->drawLine(x, static_cast<int>(rect.top()), x, static_cast<int>(rect.bottom()));
    for (int y = top; y < static_cast<int>(rect.bottom()); y += small)
        painter->drawLine(static_cast<int>(rect.left()), y, static_cast<int>(rect.right()), y);

    int lb = static_cast<int>(rect.left()) - (static_cast<int>(rect.left()) % big);
    int tb = static_cast<int>(rect.top()) - (static_cast<int>(rect.top()) % big);
    painter->setPen(QPen(QColor("#333843"), 1));
    for (int x = lb; x < static_cast<int>(rect.right()); x += big)
        painter->drawLine(x, static_cast<int>(rect.top()), x, static_cast<int>(rect.bottom()));
    for (int y = tb; y < static_cast<int>(rect.bottom()); y += big)
        painter->drawLine(static_cast<int>(rect.left()), y, static_cast<int>(rect.right()), y);
}

FlowCapsuleItem* FlowCanvasView::addNode(const QString& typeKey, const QPointF& pos,
                                         int explicitId, FlowCapsuleItem* autoConnectFrom,
                                         bool recordHistory) {
    int nid = (explicitId > 0) ? explicitId : m_nextId;
    m_nextId = std::max(m_nextId, nid + 1);

    ToolMeta meta = VisionOperatorRegistry::findTool(typeKey);
    FlowNodeData d;
    d.id = nid;
    d.typeKey = typeKey;
    d.title = meta.displayName;
    d.params = VisionOperatorRegistry::defaultParams(typeKey);
    if (typeKey == "comm_plc_send") d.plcBindings = VisionOperatorRegistry::defaultPlcBindings();
    if (typeKey == "calib_9pt") d.calibPoints = VisionOperatorRegistry::defaultCalibPoints();

    if (pos.x() < -5000) {
        if (autoConnectFrom) d.pos = QPointF(autoConnectFrom->pos().x(), autoConnectFrom->pos().y() + 86.0);
        else d.pos = QPointF(180.0, 36.0 + m_nodes.size() * 86.0);
    } else {
        d.pos = pos;
    }

    auto* item = new FlowCapsuleItem(d);
    m_scene->addItem(item);
    m_nodes[nid] = item;

    if (autoConnectFrom && m_nodes.contains(autoConnectFrom->data.id)) {
        addEdge(autoConnectFrom, item, EdgeCondition::Always, false);
    }

    m_scene->clearSelection();
    item->setSelected(true);
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit flowChanged();
    return item;
}

FlowEdgeItem* FlowCanvasView::addEdge(FlowCapsuleItem* src, FlowCapsuleItem* dst, EdgeCondition cond, bool recordHistory) {
    if (!src || !dst || src == dst) return nullptr;
    for (auto* e : m_edges) {
        if (e->srcNode == src && e->dstNode == dst) return nullptr;
    }
    auto* edge = new FlowEdgeItem(src, dst, cond);
    src->edges.push_back(edge);
    dst->edges.push_back(edge);
    m_edges.push_back(edge);
    m_scene->addItem(edge);
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit flowChanged();
    return edge;
}

void FlowCanvasView::removeEdge(FlowEdgeItem* edge, bool recordHistory) {
    if (!edge) return;
    m_edges.removeAll(edge);
    if (edge->srcNode) edge->srcNode->edges.removeAll(edge);
    if (edge->dstNode) edge->dstNode->edges.removeAll(edge);
    m_scene->removeItem(edge);
    delete edge;
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit flowChanged();
}

void FlowCanvasView::removeNode(FlowCapsuleItem* node, bool recordHistory) {
    if (!node) return;
    QVector<FlowEdgeItem*> copyEdges = node->edges;
    for (auto* e : copyEdges) removeEdge(e, false);
    m_nodes.remove(node->data.id);
    m_scene->removeItem(node);
    delete node;
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit flowChanged();
    emit nodeSelected(nullptr);
}

void FlowCanvasView::deleteSelected() {
    QList<QGraphicsItem*> sel = m_scene->selectedItems();
    if (sel.isEmpty()) return;
    for (auto* it : sel) {
        if (auto* e = dynamic_cast<FlowEdgeItem*>(it)) removeEdge(e, false);
    }
    for (auto* it : sel) {
        if (auto* n = dynamic_cast<FlowCapsuleItem*>(it)) removeNode(n, false);
    }
    pushUndoSnapshot();
}

void FlowCanvasView::clearFlow(bool recordHistory) {
    cancelTempLine();
    m_nodes.clear();
    m_edges.clear();
    m_nextId = 1;
    m_scene->clear();
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit nodeSelected(nullptr);
    emit flowChanged();
}

FlowCapsuleItem* FlowCanvasView::selectedNode() const {
    for (auto* it : m_scene->selectedItems()) {
        if (auto* n = dynamic_cast<FlowCapsuleItem*>(it)) return n;
    }
    return nullptr;
}

QVector<FlowCapsuleItem*> FlowCanvasView::orderedNodes() const {
    QMap<int, int> indeg;
    QMap<int, QVector<int>> adj;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        indeg[it.key()] = 0;
        adj[it.key()] = {};
    }
    for (auto* e : m_edges) {
        int s = e->srcNode->data.id, d = e->dstNode->data.id;
        if (adj.contains(s) && indeg.contains(d)) {
            adj[s].push_back(d);
            indeg[d]++;
        }
    }
    QVector<int> roots;
    for (auto it = indeg.begin(); it != indeg.end(); ++it) {
        if (it.value() == 0) roots.push_back(it.key());
    }
    std::sort(roots.begin(), roots.end(), [&](int a, int b) {
        return m_nodes[a]->pos().y() < m_nodes[b]->pos().y();
    });

    std::deque<int> q(roots.begin(), roots.end());
    QVector<FlowCapsuleItem*> order;
    QSet<int> visited;
    while (!q.empty()) {
        int u = q.front(); q.pop_front();
        if (visited.contains(u)) continue;
        visited.insert(u);
        order.push_back(m_nodes[u]);
        QVector<int> nbrs = adj[u];
        std::sort(nbrs.begin(), nbrs.end(), [&](int a, int b) {
            return m_nodes[a]->pos().x() < m_nodes[b]->pos().x();
        });
        for (int v : nbrs) {
            indeg[v]--;
            if (indeg[v] <= 0) q.push_back(v);
        }
    }
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (!visited.contains(it.key())) order.push_back(it.value());
    }
    return order;
}

void FlowCanvasView::exportSpecs(QVector<FlowNodeData>& outNodes, QVector<FlowEdgeData>& outEdges) const {
    outNodes.clear();
    outEdges.clear();
    for (auto* n : orderedNodes()) {
        FlowNodeData d = n->data;
        d.pos = n->pos();
        outNodes.push_back(d);
    }
    for (auto* e : m_edges) {
        outEdges.push_back({e->srcNode->data.id, e->dstNode->data.id, e->condition});
    }
}

void FlowCanvasView::loadFromSpecs(const QVector<FlowNodeData>& nodes, const QVector<FlowEdgeData>& edges, bool recordHistory) {
    clearFlow(false);
    for (const auto& nd : nodes) {
        auto* item = addNode(nd.typeKey, nd.pos, nd.id, nullptr, false);
        item->data = nd;
        item->setPos(nd.pos);
        item->update();
    }
    for (const auto& ed : edges) {
        auto* s = nodeById(ed.srcId);
        auto* d = nodeById(ed.dstId);
        if (s && d) addEdge(s, d, ed.condition, false);
    }
    if (recordHistory) pushUndoSnapshot();
    if (m_minimap) m_minimap->update();
    emit flowChanged();
}

void FlowCanvasView::autoLayout() {
    QVector<FlowCapsuleItem*> order = orderedNodes();
    if (order.isEmpty()) return;
    QMap<int, int> depth;
    for (auto* n : order) depth[n->data.id] = 0;
    for (int iter = 0; iter < order.size(); ++iter) {
        for (auto* e : m_edges) {
            int s = e->srcNode->data.id, d = e->dstNode->data.id;
            depth[d] = std::max(depth[d], depth[s] + 1);
        }
    }
    QMap<int, QVector<FlowCapsuleItem*>> layers;
    for (auto* n : order) layers[depth[n->data.id]].push_back(n);

    double cx = 230.0;
    for (auto it = layers.begin(); it != layers.end(); ++it) {
        auto& row = it.value();
        double sp = 228.0;
        double sx = cx - (row.size() - 1) * sp / 2.0;
        for (int i = 0; i < row.size(); ++i) {
            row[i]->setPos(QPointF(sx + i * sp, 32.0 + it.key() * 88.0));
        }
    }
    for (auto* e : m_edges) e->updatePath();
    centerOn(order[order.size() / 2]);
    if (m_minimap) m_minimap->update();
    pushUndoSnapshot();
}

void FlowCanvasView::applyTelemetry(const FlowCycleSummary& summary) {
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        int nid = it.key();
        auto* node = it.value();
        if (summary.nodeStates.contains(nid)) {
            const auto& st = summary.nodeStates[nid];
            node->status = st.status;
            node->elapsedMs = st.elapsedMs;
            node->summaryText = st.summary;
            node->update();
        }
    }
    for (auto* e : m_edges) {
        e->active = summary.activeEdges.contains({e->srcNode->data.id, e->dstNode->data.id});
        e->update();
    }
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::pushUndoSnapshot() {
    if (m_suppressHistory) return;
    Snapshot s;
    exportSpecs(s.nodes, s.edges);
    m_undoStack.push_back(s);
    if (m_undoStack.size() > 30) m_undoStack.removeFirst();
    m_redoStack.clear();
}

bool FlowCanvasView::undo() {
    if (m_undoStack.size() <= 1) return false;
    m_redoStack.push_back(m_undoStack.takeLast());
    m_suppressHistory = true;
    loadFromSpecs(m_undoStack.last().nodes, m_undoStack.last().edges, false);
    m_suppressHistory = false;
    return true;
}

bool FlowCanvasView::redo() {
    if (m_redoStack.isEmpty()) return false;
    Snapshot nxt = m_redoStack.takeLast();
    m_undoStack.push_back(nxt);
    m_suppressHistory = true;
    loadFromSpecs(nxt.nodes, nxt.edges, false);
    m_suppressHistory = false;
    return true;
}

void FlowCanvasView::mouseDoubleClickEvent(QMouseEvent* event) {
    QGraphicsItem* it = itemAt(event->pos());
    if (auto* n = dynamic_cast<FlowCapsuleItem*>(it)) emit nodeDoubleClicked(n);
    QGraphicsView::mouseDoubleClickEvent(event);
}

void FlowCanvasView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF sp = mapToScene(event->pos());
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) { auto* n = it.value();
            if (n->isOnOutPort(sp)) {
                m_draggingSrc = n;
                m_tempLine = new QGraphicsPathItem();
                m_tempLine->setPen(QPen(QColor("#FF7A00"), 2.4, Qt::DashLine));
                m_tempLine->setZValue(20);
                m_scene->addItem(m_tempLine);
                updateTempLine(sp);
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void FlowCanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggingSrc) {
        updateTempLine(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
    if (event->buttons() & Qt::LeftButton && m_minimap) m_minimap->update();
}

void FlowCanvasView::mouseReleaseEvent(QMouseEvent* event) {
    if (m_draggingSrc && event->button() == Qt::LeftButton) {
        QPointF sp = mapToScene(event->pos());
        FlowCapsuleItem* target = nullptr;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) { auto* n = it.value();
            if (n != m_draggingSrc && (n->isOnInPort(sp) || n->sceneBoundingRect().contains(sp))) {
                target = n;
                break;
            }
        }
        FlowCapsuleItem* src = m_draggingSrc;
        cancelTempLine();
        if (target) addEdge(src, target);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::updateTempLine(const QPointF& endScene) {
    if (!m_tempLine || !m_draggingSrc) return;
    QPointF p1 = m_draggingSrc->outPortScenePos();
    QPainterPath path(p1);
    double dy = std::max(28.0, std::abs(endScene.y() - p1.y()) * 0.5);
    path.cubicTo(QPointF(p1.x(), p1.y() + dy), QPointF(endScene.x(), endScene.y() - dy), endScene);
    m_tempLine->setPath(path);
}

void FlowCanvasView::cancelTempLine() {
    m_draggingSrc = nullptr;
    if (m_tempLine) {
        m_scene->removeItem(m_tempLine);
        delete m_tempLine;
        m_tempLine = nullptr;
    }
}

void FlowCanvasView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void FlowCanvasView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double f = (event->angleDelta().y() > 0) ? 1.12 : (1.0 / 1.12);
        int newPct = std::max(35, std::min(200, static_cast<int>(std::lround(m_zoom * f * 100.0))));
        setZoomPercent(newPct);
        emit zoomChanged(newPct);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::onSelectionChanged() {
    emit nodeSelected(selectedNode());
    if (m_minimap) m_minimap->update();
}

void FlowCanvasView::onContextMenu(const QPoint& pos) {
    QGraphicsItem* item = itemAt(pos);
    QMenu menu(this);
    if (auto* n = dynamic_cast<FlowCapsuleItem*>(item)) {
        connect(menu.addAction("⚙ 打开参数配置"), &QAction::triggered, [this, n]() { emit nodeDoubleClicked(n); });
        connect(menu.addAction("▶ 单步执行到此节点"), &QAction::triggered, [this, n]() { emit runUpToRequested(n->data.id); });
        connect(menu.addAction(n->data.enabled ? "禁用该节点" : "启用该节点"), &QAction::triggered, [this, n]() {
            n->data.enabled = !n->data.enabled; n->update(); pushUndoSnapshot(); emit flowChanged();
        });
        menu.addSeparator();
        connect(menu.addAction("✕ 删除节点"), &QAction::triggered, [this, n]() { removeNode(n); });
    } else if (auto* e = dynamic_cast<FlowEdgeItem*>(item)) {
        auto setCond = [this, e](EdgeCondition c) { e->condition = c; e->update(); pushUndoSnapshot(); emit flowChanged(); };
        connect(menu.addAction("● 连线模式：始终执行 (ALL)"), &QAction::triggered, [=]() { setCond(EdgeCondition::Always); });
        connect(menu.addAction("● 连线模式：仅上游 OK 执行 (绿色分支)"), &QAction::triggered, [=]() { setCond(EdgeCondition::OnOk); });
        connect(menu.addAction("● 连线模式：仅上游 NG 执行 (红色分支)"), &QAction::triggered, [=]() { setCond(EdgeCondition::OnNg); });
        menu.addSeparator();
        connect(menu.addAction("✕ 删除连线"), &QAction::triggered, [this, e]() { removeEdge(e); });
    } else {
        connect(menu.addAction("⇕ 自动分层排版"), &QAction::triggered, this, &FlowCanvasView::autoLayout);
        connect(menu.addAction("↶ 撤销 (Ctrl+Z)"), &QAction::triggered, this, &FlowCanvasView::undo);
        connect(menu.addAction("↷ 重做 (Ctrl+Y)"), &QAction::triggered, this, &FlowCanvasView::redo);
    }
    menu.exec(mapToGlobal(pos));
}
