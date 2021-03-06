// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com
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
#include "drillmodel.h"
#include "drillform.h"
#include "tooldatabase/tool.h"

#include <QBitmap>
#include <QDebug>

DrillModel::DrillModel(int type, int rowCount, QObject* parent)
    : QAbstractTableModel(parent)
    , m_type(type)
{
    m_data.reserve(rowCount);
}

Row& DrillModel::appendRow(const QString& name, const QIcon& icon, int id)
{
    m_data.append(Row(name, icon, id));
    return m_data.last();
}

void DrillModel::setToolId(int row, int id)
{
    if (m_data[row].toolId != id)
        m_data[row].useForCalc = id > -1;
    m_data[row].toolId = id;
    set(row, id != -1);
    dataChanged(createIndex(row, 0), createIndex(row, 1));
}

int DrillModel::toolId(int row) { return m_data[row].toolId; }

void DrillModel::setSlot(int row, bool slot) { m_data[row].isSlot = slot; }

bool DrillModel::isSlot(int row) { return m_data[row].isSlot; }

int DrillModel::apertureId(int row) { return m_data[row].apertureId; }

bool DrillModel::useForCalc(int row) const { return m_data[row].useForCalc; }

void DrillModel::setCreate(int row, bool create)
{
    if (m_data[row].toolId == -1)
        return;
    m_data[row].useForCalc = create;
    dataChanged(createIndex(row, 0), createIndex(row, 1));
    headerDataChanged(Qt::Vertical, row, row);
}

void DrillModel::setCreate(bool create)
{
    for (int row = 0; row < rowCount(); ++row) {
        m_data[row].useForCalc = create && m_data[row].toolId != -1;
    }
    dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 1));
}

int DrillModel::rowCount(const QModelIndex& /*parent*/) const { return m_data.size(); }

int DrillModel::columnCount(const QModelIndex& /*parent*/) const { return 2; }

QVariant DrillModel::data(const QModelIndex& index, int role) const
{
    int row = index.row();
    if (!index.column())
        switch (role) {
        case Qt::DisplayRole:
            if (m_data[row].isSlot)
                return QString(m_data[row].name).replace(tr("Tool"), tr("Slot"));
            else
                return m_data[row].name;
        case Qt::DecorationRole: {
            if (m_data[index.row()].toolId > -1 && m_data[row].isSlot) {
                QImage image(m_data[row].icon.pixmap(24, 24).toImage());
                for (int x = 0; x < 24; ++x)
                    for (int y = 0; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(255, 0, 0, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            } else if (m_data[index.row()].toolId > -1) {
                return m_data[row].icon;
            } else if (m_data[row].isSlot) {
                QImage image(m_data[row].icon.pixmap(24, 24).toImage());
                for (int x = 0; x < 24; ++x)
                    for (int y = 0; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(255, 100, 100, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            } else {
                QImage image(m_data[row].icon.pixmap(24, 24).toImage());
                for (int x = 0; x < 24; ++x)
                    for (int y = 0; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(100, 100, 100, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            }
        }
        case Qt::UserRole:
            return m_data[row].apertureId;
        default:
            break;
        }
    else {
        if (m_data[row].toolId == -1)
            switch (role) {
            case Qt::DisplayRole:
                return tr("Select Tool");
            case Qt::TextAlignmentRole:
                return Qt::AlignCenter;
            case Qt::UserRole:
                return m_data[row].toolId;
            default:
                break;
            }
        else
            switch (role) {
            case Qt::DisplayRole:
                return ToolHolder::tool(m_data[row].toolId).name();
            case Qt::DecorationRole:
                return ToolHolder::tool(m_data[row].toolId).icon();
            case Qt::UserRole:
                return m_data[row].toolId;
            default:
                break;
            }
    }
    return QVariant();
}

QVariant DrillModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        if (orientation == Qt::Horizontal) {
            switch (section) {
            case 0:
                switch (m_type) {
                case tAperture:
                    return tr("Aperture");
                case tTool:
                    return tr("Tool");
                }
            case 1:
                return tr("Tool");
            }

        } else {
            switch (m_type) {
            case tAperture:
                return QString("D%1").arg(m_data[section].apertureId);
            case tTool:
                return QString("T%1").arg(m_data[section].apertureId);
            }
        }
        return QVariant();
    case Qt::SizeHintRole:
        if (orientation == Qt::Vertical)
            return QFontMetrics(QFont()).boundingRect(QString("T999")).size() + QSize(Header::DelegateSize + 10, 1);
        return QVariant();
    case Qt::TextAlignmentRole:
        if (orientation == Qt::Vertical)
            return Qt::AlignRight + Qt::AlignVCenter;
        return Qt::AlignCenter;
    default:
        return QVariant();
    }
}

Qt::ItemFlags DrillModel::flags(const QModelIndex& index) const
{
    if (!index.column())
        return /*(m_data[index.row()].toolId > -1 ? Qt::ItemIsEnabled : Qt::NoItemFlags)*/ Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}
