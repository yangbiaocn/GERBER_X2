/*******************************************************************************
*                                                                              *
* Author    :  Damir Bakiev                                                    *
* Version   :  na                                                              *
* Date      :  01 February 2020                                                *
* Website   :  na                                                              *
* Copyright :  Damir Bakiev 2016-2020                                          *
*                                                                              *
* License:                                                                     *
* Use, modification & distribution is subject to Boost Software License Ver 1. *
* http://www.boost.org/LICENSE_1_0.txt                                         *
*                                                                              *
*******************************************************************************/
#pragma once
#include "gcode.h"
#include "graphicsitem.h"
#include <QObject>

class GraphicsView;

class BridgeItem final : public GraphicsItem {
    Q_OBJECT
    friend class ProfileForm;

public:
    explicit BridgeItem(double& lenght, double& size, GCode::SideOfMilling& side, BridgeItem*& ptr);
    ~BridgeItem() override { m_ptr = nullptr; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    int type() const override;
    void setNewPos(const QPointF& pos);
    // GraphicsItem interface
    Paths paths() const override;

    bool ok() const;
    double lenght() const;
    double angle() const;

    void update();

    Point64 getPoint(const int side) const;
    QLineF getPath() const;

    void setOk(bool ok);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void changeColor() override { }

private:
    BridgeItem*& m_ptr;
    GCode::SideOfMilling& m_side;
    QPainterPath m_path;
    QPointF calculate(const QPointF& pos);
    QPointF m_lastPos;
    bool m_ok = false;
    double m_angle = 0.0;
    double& m_lenght;
    double& m_size;
};
