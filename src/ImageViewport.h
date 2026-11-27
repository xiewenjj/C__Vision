#pragma once

#include <QWidget>
#include <QFrame>
#include <QComboBox>
#include <QPushButton>
#include <QImage>
#include <QRectF>
#include <QPointF>
#include <opencv2/opencv.hpp>

class VmImageCanvasWidget : public QWidget {
    Q_OBJECT
public:
    explicit VmImageCanvasWidget(QWidget* parent = nullptr);

    void updateFrames(const cv::Mat& rawBgr, const cv::Mat& visBgr, bool isContRunning = false, bool isPaused = false, int okState = -1);
    void setSplitMode(bool enabled);
    void setCrosshairVisible(bool enabled);
    void setRoiMode(bool enabled);
    void setRoiRect(int x, int y, int w, int h, bool visible);
    QRect currentRoiRect() const;
    bool hasRoiRect() const { return m_roiVisible && m_roiRect.width() >= 10 && m_roiRect.height() >= 10; }
    void fitInView();
    void resetZoom100();
    void zoomStep(double factor);

signals:
    void roiSelected(int x, int y, int w, int h);
    void stepImageRequested(int delta);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static QImage matToQImage(const cv::Mat& bgr);
    void computeFit();
    QPointF w2i(const QPointF& pt) const;
    QPointF i2w(const QPointF& pt) const;
    QRectF imgRectW() const;
    QPointF clampImg(const QPointF& pt) const;
    QString hitRoiHandle(const QPointF& ipt) const;

    cv::Mat m_rawBgr;
    cv::Mat m_visBgr;
    QImage m_rawQImg;
    QImage m_visQImg;
    int m_imgW = 0;
    int m_imgH = 0;

    double m_zoom = 1.0;
    QPointF m_pan{0, 0};
    bool m_autoFit = true;
    bool m_panning = false;
    QPointF m_panStart;
    QPointF m_panOrigin;

    bool m_splitMode = false;
    double m_splitRatio = 0.5;
    bool m_draggingSplit = false;

    bool m_showCross = false;
    bool m_roiMode = false;
    bool m_roiVisible = false;
    QRectF m_roiRect;
    QString m_roiAction;
    QPointF m_roiDragStart;
    QRectF m_roiOrigRect;

    bool m_isRunning = false;
    bool m_isPaused = false;
    int m_okState = -1;
    QString m_pixelText = "R:000 G:000 B:000  |  X:0000 Y:0000";

    QPushButton* m_btnLeft = nullptr;
    QPushButton* m_btnRight = nullptr;
};

class VmImageDisplayPanel : public QWidget {
    Q_OBJECT
public:
    explicit VmImageDisplayPanel(QWidget* parent = nullptr);

    void refreshOutputNodes(const QVector<QPair<int, QString>>& nodeItems);
    int selectedOutputNodeId() const;
    void selectOutputNodeId(int nodeId);

    QComboBox* cbOutput = nullptr;
    QPushButton* btnRawOnly = nullptr;
    QPushButton* btnSplit = nullptr;
    QPushButton* btnRoi = nullptr;
    QPushButton* btnCross = nullptr;
    QPushButton* btnPause = nullptr;
    VmImageCanvasWidget* canvas = nullptr;

signals:
    void outputNodeChanged(int nodeId);
    void rawOnlyToggled(bool checked);
    void pauseToggled(bool checked);
    void snapshotRequested();
};
