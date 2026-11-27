#pragma once

#include "VisionTypes.h"
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QTimer>
#include <QMap>
#include <QSet>

class FlowCanvasView;
class FlowCapsuleItem;

class FlowEdgeItem : public QGraphicsPathItem {
public:
    FlowEdgeItem(FlowCapsuleItem* src, FlowCapsuleItem* dst, EdgeCondition cond = EdgeCondition::Always);
    void updatePath();
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    FlowCapsuleItem* srcNode = nullptr;
    FlowCapsuleItem* dstNode = nullptr;
    EdgeCondition condition = EdgeCondition::Always;
    double flowPhase = 0.0;
    bool active = false;
};

class FlowCapsuleItem : public QGraphicsItem {
public:
    static constexpr double kWidth = 186.0;
    static constexpr double kHeight = 48.0;
    static constexpr double kPortR = 5.5;

    explicit FlowCapsuleItem(const FlowNodeData& data);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    QPointF inPortLocal() const { return QPointF(kWidth / 2.0, 0.0); }
    QPointF outPortLocal() const { return QPointF(kWidth / 2.0, kHeight); }
    QPointF inPortScenePos() const { return mapToScene(inPortLocal()); }
    QPointF outPortScenePos() const { return mapToScene(outPortLocal()); }
    bool isOnOutPort(const QPointF& scenePos) const;
    bool isOnInPort(const QPointF& scenePos) const;

    FlowNodeData data;
    QString status = "IDLE";
    double elapsedMs = 0.0;
    QString summaryText;
    QVector<FlowEdgeItem*> edges;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    bool m_hovered = false;
};

class FlowMinimapWidget : public QWidget {
    Q_OBJECT
public:
    explicit FlowMinimapWidget(FlowCanvasView* canvas, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF worldBounds() const;
    void panToPos(const QPointF& pos);

    FlowCanvasView* m_canvas = nullptr;
    bool m_dragging = false;
};

class FlowCanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit FlowCanvasView(QWidget* parent = nullptr);

    FlowCapsuleItem* addNode(const QString& typeKey, const QPointF& pos = QPointF(-9999, -9999),
                             int explicitId = -1, FlowCapsuleItem* autoConnectFrom = nullptr,
                             bool recordHistory = true);
    FlowEdgeItem* addEdge(FlowCapsuleItem* src, FlowCapsuleItem* dst,
                          EdgeCondition cond = EdgeCondition::Always, bool recordHistory = true);
    void removeEdge(FlowEdgeItem* edge, bool recordHistory = true);
    void removeNode(FlowCapsuleItem* node, bool recordHistory = true);
    void deleteSelected();
    void clearFlow(bool recordHistory = true);

    FlowCapsuleItem* selectedNode() const;
    FlowCapsuleItem* nodeById(int id) const { return m_nodes.value(id, nullptr); }
    QVector<FlowCapsuleItem*> orderedNodes() const;
    void exportSpecs(QVector<FlowNodeData>& outNodes, QVector<FlowEdgeData>& outEdges) const;
    void loadFromSpecs(const QVector<FlowNodeData>& nodes, const QVector<FlowEdgeData>& edges, bool recordHistory = true);

    void autoLayout();
    void setZoomPercent(int pct);
    void applyTelemetry(const FlowCycleSummary& summary);
    void pushUndoSnapshot();
    bool undo();
    bool redo();

    const QMap<int, FlowCapsuleItem*>& nodesMap() const { return m_nodes; }
    const QVector<FlowEdgeItem*>& edgesList() const { return m_edges; }

signals:
    void nodeSelected(FlowCapsuleItem* node);
    void nodeDoubleClicked(FlowCapsuleItem* node);
    void flowChanged();
    void zoomChanged(int pct);
    void runUpToRequested(int nodeId);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onContextMenu(const QPoint& pos);
    void onSelectionChanged();
    void tickAnimations();

private:
    void placeMinimap();
    void updateTempLine(const QPointF& endScene);
    void cancelTempLine();

    QGraphicsScene* m_scene = nullptr;
    QMap<int, FlowCapsuleItem*> m_nodes;
    QVector<FlowEdgeItem*> m_edges;
    int m_nextId = 1;
    double m_zoom = 1.0;

    FlowCapsuleItem* m_draggingSrc = nullptr;
    QGraphicsPathItem* m_tempLine = nullptr;

    FlowMinimapWidget* m_minimap = nullptr;
    QTimer* m_animTimer = nullptr;

    struct Snapshot {
        QVector<FlowNodeData> nodes;
        QVector<FlowEdgeData> edges;
    };
    QVector<Snapshot> m_undoStack;
    QVector<Snapshot> m_redoStack;
    bool m_suppressHistory = false;
};
