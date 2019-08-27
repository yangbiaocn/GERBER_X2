#include "gccreator.h"
#include "forms/gcodepropertiesform.h"
#include "gccreator.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QStack>
#include <algorithm>
#include <filetree/filemodel.h>
#include <gbraperture.h>
#include <gcvoronoi.h>
#include <gi/bridgeitem.h>
#include <limits>
#include <scene.h>
#include <settings.h>

namespace GCode {

Creator* Creator::self = nullptr;
bool Creator::m_cancel = false;
int Creator::m_progressMax = 0;
int Creator::m_progressVal = 0;

void Creator::reset()
{
    self = nullptr;
    m_cancel = false;
    m_progressMax = 0;
    m_progressVal = 0;

    m_file = nullptr;

    m_workingPs.clear();
    m_workingRawPs.clear();
    m_returnPs.clear();
    m_returnPss.clear();
    m_supportPss.clear();
    m_groupedPss.clear();

    m_toolDiameter = 0.0;
    m_dOffset = 0.0;
    m_stepOver = 0.0;
}

Creator::~Creator()
{
    self = nullptr;
}

Pathss& Creator::groupedPaths(Grouping group, cInt k)
{
    PolyTree polyTree;
    Clipper clipper;
    clipper.AddPaths(m_workingPs, ptSubject, true);
    IntRect r(clipper.GetBounds());
    Path outer = {
        IntPoint(r.left - k, r.top - k),
        IntPoint(r.right + k, r.top - k),
        IntPoint(r.right + k, r.bottom + k),
        IntPoint(r.left - k, r.bottom + k),
    };
    // ReversePath(outer);
    clipper.AddPath(outer, ptSubject, true);
    clipper.Execute(ctUnion, polyTree, pftNonZero);
    /****************************/
    std::function<void(PolyNode*, Grouping)> grouping = [&grouping, this](PolyNode* node, Grouping group) {
        Paths paths;
        switch (group) {
        case CutoffPaths:
            if (!node->IsHole()) {
                Path& path = node->Contour;
                paths.push_back(path);
                for (int i = 0, end = node->ChildCount(); i < end; ++i) {
                    path = node->Childs[i]->Contour;
                    paths.push_back(path);
                }
                m_groupedPss.append(paths);
            }
            for (int i = 0, end = node->ChildCount(); i < end; ++i)
                grouping(node->Childs[i], group);
            break;
        case CopperPaths:
            if (node->IsHole()) {
                Path& path = node->Contour;
                paths.push_back(path);
                for (int i = 0, end = node->ChildCount(); i < end; ++i) {
                    path = node->Childs[i]->Contour;
                    paths.push_back(path);
                }
                m_groupedPss.push_back(paths);
            }
            for (int i = 0, end = node->ChildCount(); i < end; ++i)
                grouping(node->Childs[i], group);
            break;
        }
    };
    /*********************************/
    m_groupedPss.clear();
    grouping(polyTree.GetFirst(), group);
    return m_groupedPss;
}
////////////////////////////////////////////////////////////////
/// \brief Creator::addRawPaths
/// \param paths
///
void Creator::addRawPaths(Paths rawPaths)
{
    if (rawPaths.isEmpty())
        return;

    if (m_gcp.side == On) {
        m_workingRawPs.append(rawPaths);
        return;
    }

    for (int i = 0; i < rawPaths.size(); ++i)
        if (rawPaths[i].size() < 2)
            rawPaths.remove(i--);

    const double glueLen = GCodePropertiesForm::glue * uScale;
    Clipper clipper;
    for (int i = 0; i < rawPaths.size(); ++i) {
        Path& path = rawPaths[i];
        if (path.size() > 3 && path.first() == path.last())
            clipper.AddPath(rawPaths.takeAt(i--), ptSubject, true);
        else if (path.size() > 3 && Length(path.first(), path.last()) < glueLen)
            clipper.AddPath(rawPaths.takeAt(i--), ptSubject, true);
    }

    mergeSegments(rawPaths, GCodePropertiesForm::glue * uScale);

    for (Path path : rawPaths) {
        if (path.size() > 3 && path.first() == path.last())
            clipper.AddPath(path, ptSubject, true);
        else if (path.size() > 3 && Length(path.first(), path.last()) < glueLen)
            clipper.AddPath(path, ptSubject, true);
        else
            m_workingRawPs.append(path);
    }

    IntRect r(clipper.GetBounds());
    int k = uScale;
    Path outer = {
        IntPoint(r.left - k, r.bottom + k),
        IntPoint(r.right + k, r.bottom + k),
        IntPoint(r.right + k, r.top - k),
        IntPoint(r.left - k, r.top - k)
    };

    clipper.AddPath(outer, ptClip, true);
    Paths paths;
    clipper.Execute(ctXor, paths, pftEvenOdd);
    //paths.takeFirst();
    m_workingPs.append(paths.mid(1));
}

void Creator::addSupportPaths(Pathss supportPaths) { m_supportPss.append(supportPaths); }

void Creator::addPaths(const Paths& paths) { m_workingPs.append(paths); }

void Creator::createGc(const GCodeParams& gcp)
{
    try {
        qDebug() << "Creator::createGc() started";
        create(gcp);
        qDebug() << "Creator::createGc() ended";
    } catch (bool) {
        m_cancel = false;
        qWarning() << "Creator::createGc() canceled";
    } catch (...) {
        qWarning() << "Creator::createGc() exeption:" << strerror(errno);
    }
}

GCode::File* Creator::file() const
{
    return m_file;
}

QPair<int, int> Creator::getProgress()
{
    if (m_cancel) {
        m_cancel = false;
        throw true;
    }
    return { m_progressMax, m_progressVal };
}

void Creator::stacking(Paths& paths)
{
    QElapsedTimer t;
    t.start();
    PolyTree polyTree;
    {
        Clipper clipper;
        clipper.AddPaths(paths, ptSubject, true);
        IntRect r(clipper.GetBounds());
        int k = uScale;
        Path outer = {
            IntPoint(r.left - k, r.bottom + k),
            IntPoint(r.right + k, r.bottom + k),
            IntPoint(r.right + k, r.top - k),
            IntPoint(r.left - k, r.top - k)
        };
        clipper.AddPath(outer, ptSubject, true);
        clipper.Execute(ctUnion, polyTree, pftEvenOdd);
        paths.clear();
    }
    qDebug() << "polyTree elapsed" << t.elapsed();
    m_returnPss.clear();
    /***********************************************************************************************/
    t.start();
    auto sss = [this](Paths& paths, Path& path, QPair<int, int> idx) -> bool {
        QList<int> list;
        list.append(idx.first);
        for (int i = paths.count() - 1, index = idx.first; i; --i) {
            double d = std::numeric_limits<double>::max();
            IntPoint pt;
            for (const IntPoint& pts : paths[i - 1]) {
                double l = Length(pts, paths[i][index]);
                if (d >= l) {
                    d = l;
                    pt = pts;
                }
            }
            if (d <= m_toolDiameter) {
                list.prepend(paths[i - 1].indexOf(pt));
                index = list.first();
            } else
                return false;
        }
        for (int i = 0; i < paths.count(); ++i)
            std::rotate(paths[i].begin(), paths[i].begin() + list[i], paths[i].end());
        std::rotate(path.begin(), path.begin() + idx.second, path.end());
        return true;
    };
    using Worck = QPair<PolyNode*, bool>;
    std::function<void(Worck)> stacker = [&stacker, &sss, this](Worck w) {
        auto [node, newPaths] = w;
        if (!m_returnPss.isEmpty() || newPaths) {
            Path path(node->Contour);
            if (!(m_gcp.convent ^ !node->IsHole()) ^ (m_gcp.side == Outer))
                ReversePath(path);
            if (Settings::cleanPolygons())
                CleanPolygon(path, uScale * 0.0005);
            if (m_returnPss.isEmpty() || newPaths) {
                m_returnPss.append({ path });
            } else {
                //check distance;
                QPair<int, int> idx;
                double d = std::numeric_limits<double>::max();
                for (int id = 0; id < m_returnPss.last().last().size(); ++id) {
                    const IntPoint& ptd = m_returnPss.last().last()[id];
                    for (int is = 0; is < path.size(); ++is) {
                        const IntPoint& pts = path[is];
                        const double l = Length(ptd, pts);
                        if (d >= l) {
                            d = l;
                            idx.first = id;
                            idx.second = is;
                        }
                    }
                }
                if (d <= m_toolDiameter && sss(m_returnPss.last(), path, idx))
                    m_returnPss.last().append(path);
                else
                    m_returnPss.append({ path });
            }
            for (int i = 0, end = node->ChildCount(); i < end; ++i)
                stacker({ node->Childs[i], static_cast<bool>(i) });
        } else { // Start from here
            for (int i = 0, end = node->ChildCount(); i < end; ++i)
                stacker({ node->Childs[i], true });
        }
        progress();
    };
    /***********************************************************************************************/
    progress(polyTree.Total());
    stacker({ polyTree.GetFirst(), false });
    qDebug() << "stacker elapsed" << t.elapsed();
    for (Paths& paths : m_returnPss) {
        std::reverse(paths.begin(), paths.end());
        for (Path& path : paths)
            path.append(path.first());
    }
    sortB(m_returnPss);
}

void Creator::mergeSegments(Paths& paths, double glue)
{
    for (int i = 0; i < paths.size(); ++i) {
        for (int j = 0; j < paths.size(); ++j) {
            if (i == j)
                continue;
            if (paths[i].last() == paths[j].first()) {
                paths[i].append(paths[j].mid(1));
                paths.remove(j--);
                continue;
            }
            if (paths[i].first() == paths[j].last()) {
                paths[j].append(paths[i].mid(1));
                paths.remove(i--);
                break;
            }
            if (paths[i].last() == paths[j].last()) {
                ReversePath(paths[j]);
                paths[i].append(paths[j].mid(1));
                paths.remove(j--);
                continue;
            }
            if (glue == 0.0)
                continue;
            if (Length(paths[i].last(), paths[j].first()) < glue) {
                paths[i].append(paths[j].mid(1));
                paths.remove(j--);
                continue;
            }
            if (Length(paths[i].first(), paths[j].last()) < glue) {
                paths[j].append(paths[i].mid(1));
                paths.remove(i--);
                break;
            }
            if (Length(paths[i].last(), paths[j].last()) < glue) {
                ReversePath(paths[j]);
                paths[i].append(paths[j].mid(1));
                paths.remove(j--);
                continue;
            }
        }
    }
}

void Creator::progress(int progressMax)
{
    if (self != nullptr)
        m_progressMax += progressMax;
}

void Creator::progress(int progressMax, int progressVal)
{
    if (m_cancel) {
        m_cancel = false;
        throw true;
    }
    m_progressVal = progressVal;
    m_progressMax = progressMax;
}

void Creator::progress()
{
    if (m_cancel) {
        m_cancel = false;
        throw true;
    }
    if (self != nullptr)
        if (m_progressMax < ++m_progressVal) {
            if (m_progressMax == 0)
                m_progressMax = 100;
            else
                m_progressMax *= 2;
        }
}

GCodeParams Creator::getGcp() const
{
    return m_gcp;
}

void Creator::setGcp(const GCodeParams& gcp)
{
    m_gcp = gcp;
    reset();
}

Paths& Creator::sortB(Paths& src)
{
    IntPoint startPt(toIntPoint(GCodePropertiesForm::homePoint->pos() + GCodePropertiesForm::zeroPoint->pos()));
    for (int firstIdx = 0; firstIdx < src.size(); ++firstIdx) {
        int swapIdx = firstIdx;
        double destLen = std::numeric_limits<double>::max();
        for (int secondIdx = firstIdx; secondIdx < src.size(); ++secondIdx) {
            const double length = Length(startPt, src[secondIdx].first());
            if (destLen > length) {
                destLen = length;
                swapIdx = secondIdx;
            }
        }
        startPt = src[swapIdx].last();
        if (swapIdx != firstIdx)
            std::swap(src[firstIdx], src[swapIdx]);
    }
    return src;
}

Paths& Creator::sortBE(Paths& src)
{
    IntPoint startPt(toIntPoint(GCodePropertiesForm::homePoint->pos() + GCodePropertiesForm::zeroPoint->pos()));
    for (int firstIdx = 0; firstIdx < src.size(); ++firstIdx) {
        int swapIdx = firstIdx;
        double destLen = std::numeric_limits<double>::max();
        bool reverse = false;
        for (int secondIdx = firstIdx; secondIdx < src.size(); ++secondIdx) {
            const double lenFirst = Length(startPt, src[secondIdx].first());
            const double lenLast = Length(startPt, src[secondIdx].last());
            if (lenFirst < lenLast) {
                if (destLen > lenFirst) {
                    destLen = lenFirst;
                    swapIdx = secondIdx;
                    reverse = false;
                }
            } else {
                if (destLen > lenLast) {
                    destLen = lenLast;
                    swapIdx = secondIdx;
                    reverse = true;
                }
            }
        }
        if (reverse)
            ReversePath(src[swapIdx]);
        startPt = src[swapIdx].last();
        if (swapIdx != firstIdx)
            std::swap(src[firstIdx], src[swapIdx]);
    }
    return src;
}

Pathss& Creator::sortB(Pathss& src)
{
    IntPoint startPt(toIntPoint(GCodePropertiesForm::homePoint->pos() + GCodePropertiesForm::zeroPoint->pos()));
    for (int firstIdx = 0; firstIdx < src.size(); ++firstIdx) {
        int swapIdx = firstIdx;
        double destLen = std::numeric_limits<double>::max();
        for (int secondIdx = firstIdx; secondIdx < src.size(); ++secondIdx) {
            const double length = Length(startPt, src[secondIdx].first().first());
            if (destLen > length) {
                destLen = length;
                swapIdx = secondIdx;
            }
        }
        startPt = src[swapIdx].last().last();
        if (swapIdx != firstIdx)
            std::swap(src[firstIdx], src[swapIdx]);
    }
    return src;
}

Pathss& Creator::sortBE(Pathss& src)
{
    IntPoint startPt(toIntPoint(GCodePropertiesForm::homePoint->pos() + GCodePropertiesForm::zeroPoint->pos()));
    for (int firstIdx = 0; firstIdx < src.size(); ++firstIdx) {
        int swapIdx = firstIdx;
        double destLen = std::numeric_limits<double>::max();
        bool reverse = false;
        for (int secondIdx = firstIdx; secondIdx < src.size(); ++secondIdx) {
            const double lenFirst = Length(startPt, src[secondIdx].first().first());
            const double lenLast = Length(startPt, src[secondIdx].last().last());
            if (lenFirst < lenLast) {
                if (destLen > lenFirst) {
                    destLen = lenFirst;
                    swapIdx = secondIdx;
                    reverse = false;
                }
            } else {
                if (destLen > lenLast) {
                    destLen = lenLast;
                    swapIdx = secondIdx;
                    reverse = true;
                }
            }
        }
        //        if (reverse)
        //            std::reverse(src[swapIdx].begin(), src[swapIdx].end());
        //        startPt = src[swapIdx].last().last();
        if (swapIdx != firstIdx && !reverse) {
            startPt = src[swapIdx].last().last();
            std::swap(src[firstIdx], src[swapIdx]);
        }
    }
    return src;
}

bool Creator::pointOnPolygon(const QLineF& l2, const Path& path, IntPoint* ret)
{
    int cnt = path.size();
    if (cnt < 3)
        return false;
    QPointF p;
    for (int i = 0; i < cnt; ++i) {
        IntPoint pt1(path[(i + 1) % cnt]);
        IntPoint pt2(/*i == cnt ? path[0] :*/ path[i]);
        QLineF l1(toQPointF(pt1), toQPointF(pt2));
        if (QLineF::BoundedIntersection == l1.intersect(l2, &p)) {
            if (ret)
                *ret = toIntPoint(p);
            return true;
        }
    }
    //    IntPoint pt1 = path[0];
    //    for (int i = 1; i <= cnt; ++i) {
    //        IntPoint pt2(i == cnt ? path[0] : path[i]);
    //        QLineF l1(toQPointF(pt1), toQPointF(pt2));
    //        if (QLineF::BoundedIntersection == l1.intersect(l2, &p)) {
    //            if (ret)
    //                *ret = toIntPoint(p);
    //            return true;
    //        }
    //        pt1 = pt2;
    //    }
    return false;
}
}