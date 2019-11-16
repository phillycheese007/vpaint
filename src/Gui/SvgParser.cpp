// Copyright (C) 2012-2016 The VPaint Developers.
// See the COPYRIGHT file at the top-level directory of this distribution
// and at https://github.com/dalboris/vpaint/blob/master/COPYRIGHT
//
// This file is part of VPaint, a vector graphics editor. It is subject to the
// license terms and conditions in the LICENSE.MIT file found in the top-level
// directory of this distribution and at http://opensource.org/licenses/MIT

#include "SvgParser.h"

#include <QApplication>
#include <QDebug>
#include <QRegExp>
#include <QStack>
#include <QString>
#include <QStringRef>
#include <QVector>

#include "Global.h"
#include "Scene.h"
#include "VectorAnimationComplex/EdgeGeometry.h"
#include "VectorAnimationComplex/EdgeSample.h"
#include "VectorAnimationComplex/KeyEdge.h"
#include "VectorAnimationComplex/KeyFace.h"
#include "VectorAnimationComplex/KeyVertex.h"
#include "VectorAnimationComplex/SculptCurve.h"
#include "VectorAnimationComplex/VAC.h"
#include "View.h"
#include "XmlStreamReader.h"

SvgParser::SvgParser()
{

}

// Parses color from string, will probably be moved to a class like CSSColor
// This implements the most of the W3 specifications
// found at https://www.w3.org/TR/SVG11/types.html#DataTypeColor
// It also extends the specifications in a few minor ways
// This includes more flexible whitespace and some CSS3 features (hsl)
QColor SvgParser::parseColor_(QString s)
{
    // Remove excess whitespace
    s = s.trimmed();
    if(s == "none") {
        return Qt::transparent;
    }
    if(s.startsWith("rgba") && s.endsWith(")") && s.contains("("))
    {
        // Remove rgba()
        s.remove(0, 4).chop(1);
        s = s.trimmed().remove(0, 1);

        // Split into elements with comma separating them
        QStringList sSplit = s.split(',');

        // If it doesn't have exactly four elements, return an invalid color
        if(sSplit.size() != 4)
        {
            return QColor();
        }

        int colors[3];

        // Handle rgb channels
        for(int i = 0; i < 3; i++)
        {
            QString element(sSplit[i]);

            // More trimming
            element = element.trimmed();

            // Determine if it is *% or * format
            if(element.endsWith("%"))
            {
                // Remove % sign
                element.chop(1);

                colors[i] = qRound(qBound(0.0, element.toDouble(), 100.0) * 2.55);
            }
            else
            {
                colors[i] = qBound(0, qRound(element.toDouble()), 255);
            }
        }
        // Alpha channel is a double from 0.0 to 1.0 inclusive
        double alpha = qBound(0.0, sSplit[3].toDouble(), 1.0);

        // Return result
        QColor color = QColor(colors[0], colors[1], colors[2]);
        color.setAlphaF(alpha);
        return color;
    }
    else if(s.startsWith("rgb") && s.endsWith(")") && s.contains("("))
    {
        // Remove rgb()
        s.remove(0, 3).chop(1);
        s = s.trimmed().remove(0, 1);

        // Split into elements with comma separating them
        QStringList sSplit = s.split(',');

        // If it doesn't have exactly three elements, return an invalid color
        if(sSplit.size() != 3)
        {
            return QColor();
        }

        int colors[3];

        for(int i = 0; i < 3; i++)
        {
            QString element(sSplit[i]);

            // More trimming
            s = s.trimmed();

            // Determine if it is *% or * format
            if(element.endsWith("%"))
            {
                // Remove % sign
                element.chop(1);

                // Convert to number and add element to list (100% -> 255)
                colors[i] = qRound(qBound(0.0, element.toDouble(), 100.0) * 2.55);
            }
            else
            {
                colors[i] = qBound(0, qRound(element.toDouble()), 255);
            }
        }

        // Return result
        return QColor(colors[0], colors[1], colors[2]);
    }
    else if(s.startsWith("hsla") && s.endsWith(")") && s.contains("("))
    {
        // Remove hsla()
        s.remove(0, 4).chop(1);
        s = s.trimmed().remove(0, 1);

        // Split into elements with comma separating them
        QStringList sSplit = s.split(',');

        // If it doesn't have exactly three elements, return an invalid color
        // If saturation and lightness are not percentages, also return invalid
        if(sSplit.size() != 4 || !sSplit[1].endsWith("%") || !sSplit[2].endsWith("%"))
        {
            return QColor();
        }

        // Hue is an angle from 0-359 inclusive
        int hue = qRound(sSplit[0].toDouble());
        // As an angle, hue loops around
        hue = ((hue % 360) + 360) % 360;

        // Saturation and lightness are read as percentages and mapped to the range 0-255
        // Remove percentage signs
        sSplit[1].chop(1);
        sSplit[2].chop(1);
        int saturation = qRound(qBound(0.0, sSplit[1].toDouble(), 100.0) * 2.55);
        int lightness = qRound(qBound(0.0, sSplit[2].toDouble(), 100.0) * 2.55);

        // Alpha channel is a double from 0.0 to 1.0 inclusive
        double alpha = qBound(0.0, sSplit[3].toDouble(), 1.0);

        // Return result
        QColor color = QColor();
        color.setHsl(hue, saturation, lightness);
        color.setAlphaF(alpha);
        return color;
    }
    else if(s.startsWith("hsl") && s.endsWith(")") && s.contains("("))
    {
        // Remove hsl()
        s.remove(0, 3).chop(1);
        s = s.trimmed().remove(0, 1);

        // Split into elements with comma separating them
        QStringList sSplit = s.split(',');

        // If it doesn't have exactly three elements, return an invalid color
        // If saturation and lightness are not percentages, also return invalid
        if(sSplit.size() != 3 || !sSplit[1].endsWith("%") || !sSplit[2].endsWith("%"))
        {
            return QColor();
        }

        // Hue is an angle from 0-359 inclusive
        int hue = qRound(sSplit[0].toDouble());
        // As an angle, hue loops around
        hue = ((hue % 360) + 360) % 360;

        // Saturation and lightness are read as percentages and mapped to the range 0-255
        // Remove percentage signs
        sSplit[1].chop(1);
        sSplit[2].chop(1);
        int saturation = qRound(qBound(0.0, sSplit[1].toDouble(), 100.0) * 2.55);
        int lightness = qRound(qBound(0.0, sSplit[2].toDouble(), 100.0) * 2.55);

        // Return result
        QColor color = QColor();
        color.setHsl(hue, saturation, lightness);
        return color;
    }
    else
    {
        // This handles named constants and #* formats
        return QColor(s);
    }
}

// Reads a <rect> object
// https://www.w3.org/TR/SVG11/shapes.html#RectElement
// @return true on success, false on failure
bool SvgParser::readRect_(XmlStreamReader & xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a rect object
    if(xml.name() != "rect") return true;

    bool okay = true;

    // Get attributes

    // X position
    double x = xml.attributes().hasAttribute("x") ? xml.attributes().value("x").toDouble(&okay) : 0;
    if(!okay) x = 0;

    // Y position
    double y = xml.attributes().hasAttribute("y") ? xml.attributes().value("y").toDouble(&okay) : 0;
    if(!okay) y = 0;

    // Width
    double width = xml.attributes().value("width").toDouble(&okay);
    // Error, width isn't a real number
    if(!okay) return false;

    // Height
    double height = xml.attributes().value("height").toDouble(&okay);
    // Error, height isn't a real number
    if(!okay) return false;

    // Negative width or height results in an error
    if(width < 0 || height < 0) return false;

    // A width or height of 0 does not result in an error, but disables rendering of the object
    if(width == 0 || height == 0) return true;

    // The rx and ry attributes have a slightly more advanced default value, see W3 specifications for details
    double rx, ry;
    bool rxOkay = false, ryOkay = false;
    if(xml.attributes().hasAttribute("rx"))
    {
        rx = xml.attributes().value("rx").toDouble(&rxOkay);
    }
    if(xml.attributes().hasAttribute("ry"))
    {
        ry = xml.attributes().value("ry").toDouble(&ryOkay);
    }
    if(!rxOkay && !ryOkay)
    {
        rx = ry = 0;
    }
    else if(rxOkay && !ryOkay)
    {
        ry = rx;
    }
    else if(!rxOkay && ryOkay)
    {
        rx = ry;
    }
    rx = qBound(0.0, rx, width / 2);
    ry = qBound(0.0, ry, height / 2);

    // Build verticies and edges
    VectorAnimationComplex::KeyVertex * v1 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x, y));
    VectorAnimationComplex::KeyVertex * v2 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x + width, y));
    VectorAnimationComplex::KeyVertex * v3 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x + width, y + height));
    VectorAnimationComplex::KeyVertex * v4 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x, y + height));
    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> c1(VectorAnimationComplex::EdgeSample(v1->pos()[0], v1->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(v2->pos()[0], v2->pos()[1], pa.strokeWidth));
    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> c2(VectorAnimationComplex::EdgeSample(v2->pos()[0], v2->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(v3->pos()[0], v3->pos()[1], pa.strokeWidth));
    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> c3(VectorAnimationComplex::EdgeSample(v3->pos()[0], v3->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(v4->pos()[0], v4->pos()[1], pa.strokeWidth));
    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> c4(VectorAnimationComplex::EdgeSample(v4->pos()[0], v4->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(v1->pos()[0], v1->pos()[1], pa.strokeWidth));
    VectorAnimationComplex::KeyEdge * e1 = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v1, v2, (new VectorAnimationComplex::LinearSpline(c1)), pa.strokeWidth);
    VectorAnimationComplex::KeyEdge * e2 = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v2, v3, (new VectorAnimationComplex::LinearSpline(c2)), pa.strokeWidth);
    VectorAnimationComplex::KeyEdge * e3 = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v3, v4, (new VectorAnimationComplex::LinearSpline(c3)), pa.strokeWidth);
    VectorAnimationComplex::KeyEdge * e4 = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v4, v1, (new VectorAnimationComplex::LinearSpline(c4)), pa.strokeWidth);

    // Apply stroke color
    v1->setColor(pa.stroke);
    v2->setColor(pa.stroke);
    v3->setColor(pa.stroke);
    v4->setColor(pa.stroke);
    e1->setColor(pa.stroke);
    e2->setColor(pa.stroke);
    e3->setColor(pa.stroke);
    e4->setColor(pa.stroke);

    // Add fill
    if(xml.attributes().value("fill").trimmed() != "none")
    {
        QList<VectorAnimationComplex::KeyHalfedge> edges;
        edges.append(VectorAnimationComplex::KeyHalfedge(e1, true));
        edges.append(VectorAnimationComplex::KeyHalfedge(e2, true));
        edges.append(VectorAnimationComplex::KeyHalfedge(e3, true));
        edges.append(VectorAnimationComplex::KeyHalfedge(e4, true));
        VectorAnimationComplex::Cycle cycle(edges);
        VectorAnimationComplex::KeyFace * face = global()->scene()->activeVAC()->newKeyFace(cycle);
        face->setColor(pa.fill);
    }

    return true;
}

bool SvgParser::readLine_(XmlStreamReader & xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a line object
    if(xml.name() != "line") return true;

    bool okay = true;

    // Get attributes

    // X position 1
    double x1 = xml.attributes().hasAttribute("x1") ? xml.attributes().value("x1").toDouble(&okay) : 0;
    if(!okay) x1 = 0;

    // Y position 1
    double y1 = xml.attributes().hasAttribute("y1") ? xml.attributes().value("y1").toDouble(&okay) : 0;
    if(!okay) y1 = 0;

    // X position 2
    double x2 = xml.attributes().hasAttribute("x2") ? xml.attributes().value("x2").toDouble(&okay) : 0;
    if(!okay) x2 = 0;

    // Y position 2
    double y2 = xml.attributes().hasAttribute("y2") ? xml.attributes().value("y2").toDouble(&okay) : 0;
    if(!okay) y2 = 0;

    // Build verticies and edges
    VectorAnimationComplex::KeyVertex * v1 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x1, y1));
    VectorAnimationComplex::KeyVertex * v2 = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x2, y2));
    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> c(VectorAnimationComplex::EdgeSample(v1->pos()[0], v1->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(v2->pos()[0], v2->pos()[1], pa.strokeWidth));
    VectorAnimationComplex::KeyEdge * e = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v1, v2, (new VectorAnimationComplex::LinearSpline(c)), pa.strokeWidth);

    // Apply stroke color
    v1->setColor(pa.stroke);
    v2->setColor(pa.stroke);
    e->setColor(pa.stroke);

    return true;
}

bool SvgParser::readPolyline_(XmlStreamReader &xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a polyline object
    if(xml.name() != "polyline" || !xml.attributes().hasAttribute("points")) return true;

    bool okay = true;

    // Read and split points
    // Technically the parsing of separators is a bit more complicated,
    // but this will suffice as it correctly handles all standard-conforming svgs
    QStringList points = xml.attributes().value("points").toString().split(QRegExp("[\\s,]+"), QString::SkipEmptyParts);

    // Don't render isn't at least one complete coordinate
    if(points.size() < 2) return true;

    QVector<VectorAnimationComplex::KeyVertex *> verticies(points.size() / 2);

    // Parse points
    for(int i = 0; i < verticies.size(); i++) {
        // X
        double x = points[i * 2].toDouble(&okay);
        if(!okay) return false;

        // Y
        double y = points[i * 2 + 1].toDouble(&okay);
        if(!okay) return false;

        verticies[i] = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x, y));
        verticies[i]->setColor(pa.stroke);
    }

    // Create edges
    for(int i = 1; i < verticies.size(); i++) {
        VectorAnimationComplex::KeyEdge * e = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), verticies[i-1], verticies[i], (new VectorAnimationComplex::LinearSpline(SculptCurve::Curve<VectorAnimationComplex::EdgeSample>(VectorAnimationComplex::EdgeSample(verticies[i-1]->pos()[0], verticies[i-1]->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(verticies[i]->pos()[0], verticies[i]->pos()[1], pa.strokeWidth)))), pa.strokeWidth);
        e->setColor(pa.stroke);
    }

    return true;
}

bool SvgParser::readPolygon_(XmlStreamReader &xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a polygon object
    if(xml.name() != "polygon" || !xml.attributes().hasAttribute("points")) return true;

    bool okay = true;

    // Read and split points
    // Technically the parsing of separators is a bit more complicated,
    // but this will suffice as it correctly handles all standard-conforming svgs
    QStringList points = xml.attributes().value("points").toString().split(QRegExp("[\\s,]+"), QString::SkipEmptyParts);

    // Fail if there isn't at least one complete coordinate
    if(points.size() < 2) return false;

    QVector<VectorAnimationComplex::KeyVertex *> verticies(points.size() / 2);

    // Parse points
    for(int i = 0; i < verticies.size(); i++) {
        // X
        double x = points[i * 2].toDouble(&okay);
        if(!okay) return false;

        // Y
        double y = points[i * 2 + 1].toDouble(&okay);
        if(!okay) return false;

        verticies[i] = global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(x, y));
        verticies[i]->setColor(pa.stroke);
    }

    // Create Edges
    QVector<VectorAnimationComplex::KeyEdge *> edges(verticies.size() - 1);
    for(int i = 1; i < verticies.size(); i++) {
        VectorAnimationComplex::KeyEdge * e = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), verticies[i-1], verticies[i], (new VectorAnimationComplex::LinearSpline(SculptCurve::Curve<VectorAnimationComplex::EdgeSample>(VectorAnimationComplex::EdgeSample(verticies[i-1]->pos()[0], verticies[i-1]->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(verticies[i]->pos()[0], verticies[i]->pos()[1], pa.strokeWidth)))), pa.strokeWidth);
        e->setColor(pa.stroke);
        edges[i-1] = e;
    }

    // Close the loop if it isn't yet closed
    if(verticies.first()->pos() != verticies.last()->pos()) {
        VectorAnimationComplex::KeyEdge * e = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), verticies.last(), verticies[0], (new VectorAnimationComplex::LinearSpline(SculptCurve::Curve<VectorAnimationComplex::EdgeSample>(VectorAnimationComplex::EdgeSample(verticies.last()->pos()[0], verticies.last()->pos()[1], pa.strokeWidth), VectorAnimationComplex::EdgeSample(verticies[0]->pos()[0], verticies[0]->pos()[1], pa.strokeWidth)))), pa.strokeWidth);
        e->setColor(pa.stroke);
        edges.push_back(e);
    }

    // Add fill
    if(xml.attributes().value("fill").trimmed() != "none")
    {
        QList<VectorAnimationComplex::KeyHalfedge> halfEdges;
        for(int i = 0; i < edges.size(); i++) {
            halfEdges.append(VectorAnimationComplex::KeyHalfedge(edges[i], true));
        }
        VectorAnimationComplex::Cycle cycle(halfEdges);
        VectorAnimationComplex::KeyFace * face = global()->scene()->activeVAC()->newKeyFace(cycle);
        face->setColor(pa.fill);
    }

    return true;
}

bool SvgParser::readCircle_(XmlStreamReader &xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a circle object
    if(xml.name() != "circle") return true;

    bool okay = true;

    // Get attributes

    // Center X position
    double cx = xml.attributes().hasAttribute("cx") ? xml.attributes().value("cx").toDouble(&okay) : 0;
    if(!okay) cx = 0;

    // Center Y position
    double cy = xml.attributes().hasAttribute("cy") ? xml.attributes().value("cy").toDouble(&okay) : 0;
    if(!okay) cy = 0;

    // Radius
    double r = xml.attributes().value("r").toDouble(&okay);
    // Error, radius isn't a real number
    if(!okay) return false;

    // Negative radius results in an error
    if(r < 0) return false;
    // A radius of 0 does not result in an error, but disables rendering of the object
    if(r == 0) return true;

    // Build verticies and edges
    QVector<VectorAnimationComplex::KeyVertex *> v;
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx + r, cy)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx, cy + r)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx - r, cy)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx, cy - r)));
    QVector<VectorAnimationComplex::KeyEdge *> e(4);

    for(int i = 0; i < 4; i++) {
        QList<PotentialPoint> es;
        es.push_back(PotentialPoint(v[i]->pos(), pa.strokeWidth));
        es.push_back(PotentialPoint(v[(i+1)%4]->pos(), pa.strokeWidth));
        SculptCurve::Curve<VectorAnimationComplex::EdgeSample> newC;

        populateSamplesRecursive((i + 0.5) * (M_PI / 2), M_PI / 2, es, es.begin(), pa.strokeWidth, newC.ds(), [&] (const double t) -> Eigen::Vector2d { return Eigen::Vector2d(r * qCos(t) + cx, r * qSin(t) + cy); });

        newC.beginSketch(es.first().getEdgeSample());
        for(int j = 1; j < es.size(); j++) {
            newC.continueSketch(es[j].getEdgeSample());
        }
        newC.endSketch();

        e[i] = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v[i], v[(i+1)%4], (new VectorAnimationComplex::LinearSpline(newC)), pa.strokeWidth);
        e[i]->setColor(pa.stroke);
    }

    // Add fill
    if(xml.attributes().value("fill").trimmed() != "none")
    {
        QList<VectorAnimationComplex::KeyHalfedge> edges;
        for(VectorAnimationComplex::KeyEdge * edge : e) {
            edges.append(VectorAnimationComplex::KeyHalfedge(edge, true));
        }
        VectorAnimationComplex::Cycle cycle(edges);
        VectorAnimationComplex::KeyFace * face = global()->scene()->activeVAC()->newKeyFace(cycle);
        face->setColor(pa.fill);
    }

    return true;
}

bool SvgParser::readEllipse_(XmlStreamReader &xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading an ellipse object
    if(xml.name() != "ellipse") return true;

    bool okay = true;

    // Get attributes

    // Center X position
    double cx = xml.attributes().hasAttribute("cx") ? xml.attributes().value("cx").toDouble(&okay) : 0;
    if(!okay) cx = 0;

    // Center Y position
    double cy = xml.attributes().hasAttribute("cy") ? xml.attributes().value("cy").toDouble(&okay) : 0;
    if(!okay) cy = 0;

    // X radius
    double rx = xml.attributes().value("rx").toDouble(&okay);
    // Error, x radius isn't a real number
    if(!okay) return false;

    // Y radius
    double ry = xml.attributes().value("ry").toDouble(&okay);
    // Error, y radius isn't a real number
    if(!okay) return false;

    // Negative x or y radius results in an error
    if(rx < 0 || ry < 0) return false;
    // A x or y radius of 0 does not result in an error, but disables rendering of the object
    if(rx == 0 || ry == 0) return true;

    // Build verticies and edges
    QVector<VectorAnimationComplex::KeyVertex *> v;
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx + rx, cy)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx, cy + ry)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx - rx, cy)));
    v.push_back(global()->scene()->activeVAC()->newKeyVertex(global()->activeTime(), Eigen::Vector2d(cx, cy - ry)));
    QVector<VectorAnimationComplex::KeyEdge *> e(4);

    for(int i = 0; i < 4; i++) {
        QList<PotentialPoint> es;
        es.push_back(PotentialPoint(v[i]->pos(), pa.strokeWidth));
        es.push_back(PotentialPoint(v[(i+1)%4]->pos(), pa.strokeWidth));
        SculptCurve::Curve<VectorAnimationComplex::EdgeSample> newC;

        populateSamplesRecursive((i + 0.5) * (M_PI / 2), M_PI / 2, es, es.begin(), pa.strokeWidth, newC.ds(), [&] (const double t) -> Eigen::Vector2d { return Eigen::Vector2d(rx * qCos(t) + cx, ry * qSin(t) + cy); });

        newC.beginSketch(es.first().getEdgeSample());
        for(int j = 1; j < es.size(); j++) {
            newC.continueSketch(es[j].getEdgeSample());
        }
        newC.endSketch();

        e[i] = global()->scene()->activeVAC()->newKeyEdge(global()->activeTime(), v[i], v[(i+1)%4], (new VectorAnimationComplex::LinearSpline(newC)), pa.strokeWidth);
        e[i]->setColor(pa.stroke);
    }

    // Add fill
    if(pa.hasFill())
    {
        QList<VectorAnimationComplex::KeyHalfedge> edges;
        for(VectorAnimationComplex::KeyEdge * edge : e)
        {
            edges.append(VectorAnimationComplex::KeyHalfedge(edge, true));
        }
        VectorAnimationComplex::Cycle cycle(edges);
        VectorAnimationComplex::KeyFace * face = global()->scene()->activeVAC()->newKeyFace(cycle);
        face->setColor(pa.fill);
    }

    return true;
}

bool SvgParser::readPath_(XmlStreamReader &xml, SvgPresentationAttributes &pa)
{
    // Check to make sure we are reading a path object
    if(xml.name() != "path" || !xml.attributes().hasAttribute("d")) return true;

    // Get attributes

    QString d = xml.attributes().value("d").toString();

    return parsePath(d, pa);
}

// TODO refactor tail recursion
bool SvgParser::parsePath(QString &data, const SvgPresentationAttributes &pa, const Eigen::Vector2d startPos) {
    QList<PotentialPoint> samples;

    // Remove whitespace characters from the beginning
    trimFront(data);

    // Add startPos as the first point if the first command is not a move
    if(data[0] != 'M' && data[0] != 'm')
    {
        samples.append(PotentialPoint(startPos, pa.strokeWidth));
    }

    bool ok = true;
    char prevMode = 'M';
    while(!data.isEmpty())
    {
        bool relative = true;
        char c = data[0].cell();
        data.remove(0, 1);

        switch(c) {
        // Ignore whitespace characters
        case 0x20:
        case 0x9:
        case 0xD:
        case 0xA:
            continue;
        // Move
        case 'M':
            relative = false;
        case 'm':
        {
            trimFront(data);
            Eigen::Vector2d start = getNextCoordinatePair(data, &ok);
            //qDebug() << "pair Parsed " << start[0] << "," << start[1] << endl;
            if(!ok) return false;
            if(samples.isEmpty())
            {
                // If this is the first command add it as the first point
                if(relative) {
                    start += startPos;
                }
                samples.append(PotentialPoint(start[0], start[1], pa.strokeWidth));
            }
            else
            {
                // If this is not the first command, finish the existing path and start a new subpath
                Eigen::Vector2d end = finishPath(samples, pa);
                if(relative) {
                    start += end;
                }
                return parsePath(data, pa, start);
            }
            break;
        }
        case 'S':
        case 'T':
        case 'L':
            relative = false;
        case 's':
        case 't':
        case 'l':
            ok = addLineTo(samples, data, pa, relative);
            break;
        case 'V':
            relative = false;
        case 'v':
            ok = addVerticalLineTo(samples, data, pa, relative);
            break;
        case 'H':
            relative = false;
        case 'h':
            ok = addHorizontalLineTo(samples, data, pa, relative);
            break;
        case 'C':
            relative = false;
        case 'c':
            ok = addCurveTo(samples, data, pa, relative);
            break;
        /*case 'S': // Temporarily using line instead of this
            relative = false;
        case 's':
            ok = addSmoothCurveTo(samples, data, relative);
            break;*/
        case 'Q':
            relative = false;
        case 'q':
            ok = addQuadraticBezierCurveTo(samples, data, pa, relative);
            break;
        /*case 'T':
            relative = false;
        case 't':
            ok = addSmoothQuadraticBezierCurveTo(samples, data, relative);
            break;*/
        case 'A':
            relative = false;
        case 'a':
            ok = addEllipticalArc(samples, data, pa, relative);
            break;
        case 'Z':
        case 'z':
            return parsePath(data, pa, finishPath(samples, pa, true));
            break;
        default:
            data.prepend(c);
            data.prepend(prevMode);
            continue;
        }
        if(!ok) return false;

        prevMode = c;
    }
    finishPath(samples, pa);
    return true;
}

bool SvgParser::addLineTo(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    bool hasLooped = false;
    bool ok;
    Eigen::Vector2d pos;
    while(true)
    {
        trimFront(data);
        pos = getNextCoordinatePair(data, &ok);
        if(!ok) return hasLooped;
        hasLooped = true;

        // Don't bother drawing anything if the length is 0
        if((relative && pos == Eigen::Vector2d(0, 0)) || (!relative && samplingPoints.last().getX() == pos[0] && samplingPoints.last().getY() == pos[1])) continue;

        double x, y, deltaX, deltaY;
        if(relative) {
            x = samplingPoints.last().getX() + pos[0];
            y = samplingPoints.last().getY() + pos[1];
            deltaX = pos[0];
            deltaY = pos[1];
        }
        else {
            x = pos[0];
            y = pos[1];
            deltaX = x - samplingPoints.last().getX();
            deltaY = y - samplingPoints.last().getY();
        }

        samplingPoints.last().setRightTangent(qAtan2(deltaY, deltaX));
        samplingPoints.append(PotentialPoint(x, y, pa.strokeWidth));
        samplingPoints.last().setLeftTangent(qAtan2(-deltaY, -deltaX));
    }
}

bool SvgParser::addVerticalLineTo(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    bool hasLooped = false, ok;
    double dist;
    while(true) {
        // Try to find number
        dist = getNextDouble(data, &ok);
        if(!ok) break;

        if(!addLineTo(samplingPoints, QString::number(relative ? 0 : samplingPoints.last().getX()).append(",").append(QString::number(dist)), pa, relative))
        {
            return false;
        }
        hasLooped = true;
    }
    return hasLooped;
}

bool SvgParser::addHorizontalLineTo(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    bool hasLooped = false, ok;
    double dist;
    while(true)
    {
        // Try to find number
        dist = getNextDouble(data, &ok);
        if(!ok) break;

        if(!addLineTo(samplingPoints, QString::number(dist).append(",").append(QString::number(relative ? 0 : samplingPoints.last().getY())), pa, relative))
        {
            return false;
        }
        hasLooped = true;
    }
    return hasLooped;
}

bool SvgParser::addCurveTo(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    bool hasLooped = false, ok;

    //qDebug() << "First startloc for adCurveTo" << samplingPoints.last().getX() << samplingPoints.last().getY();
    while(true)
    {
        trimFront(data);
        PotentialPoint startLoc = samplingPoints.last();

        Eigen::Vector2d cp1 = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        trimCommaWspFront(data);
        Eigen::Vector2d cp2 = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        trimCommaWspFront(data);
        Eigen::Vector2d endLoc = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        if(relative)
        {
            cp1[0] += startLoc.getX();
            cp1[1] += startLoc.getY();
            cp2[0] += startLoc.getX();
            cp2[1] += startLoc.getY();
            endLoc[0] += startLoc.getX();
            endLoc[1] += startLoc.getY();
        }

        SculptCurve::Curve<VectorAnimationComplex::EdgeSample> curve;
        samplingPoints.push_back(PotentialPoint(endLoc, pa.strokeWidth));

        populateSamplesRecursive(0.5, 1.0, samplingPoints, samplingPoints.end()-2, pa.strokeWidth, curve.ds(), [&] (const double t) -> Eigen::Vector2d { const double ti = 1-t; return Eigen::Vector2d(ti*ti*ti*startLoc.getX()+3*ti*ti*t*cp1[0]+3*ti*t*t*cp2[0]+t*t*t*endLoc[0], ti*ti*ti*startLoc.getY()+3*ti*ti*t*cp1[1]+3*ti*t*t*cp2[1]+t*t*t*endLoc[1]); });

        hasLooped = true;
    }
    return hasLooped;
}

// TODO Implement this
bool SvgParser::addSmoothCurveTo(QList<PotentialPoint> & samplingPoints, QString & data, bool relative) {}

bool SvgParser::addQuadraticBezierCurveTo(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    trimFront(data);
    bool hasLooped = false, ok;
    while(true)
    {
        PotentialPoint startLoc = samplingPoints.last();

        Eigen::Vector2d cp = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        trimCommaWspFront(data);
        Eigen::Vector2d endLoc = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        if(relative)
        {
            cp[0] += startLoc.getX();
            cp[1] += startLoc.getY();
            endLoc[0] += startLoc.getX();
            endLoc[1] += startLoc.getY();
        }

        SculptCurve::Curve<VectorAnimationComplex::EdgeSample> curve;
        samplingPoints.push_back(PotentialPoint(endLoc, pa.strokeWidth));

        populateSamplesRecursive(0.5, 1.0, samplingPoints, samplingPoints.end()-2, pa.strokeWidth, curve.ds(), [&] (const double t) -> Eigen::Vector2d { const double ti = 1-t; return Eigen::Vector2d(ti*ti*startLoc.getX()+2*ti*t*cp[0]+t*t*endLoc[0], ti*ti*startLoc.getY()+2*ti*t*cp[1]+t*t*endLoc[1]); });

        hasLooped = true;
    }
    return hasLooped;
}

// TODO Implement this
bool SvgParser::addSmoothQuadraticBezierCurveTo(QList<PotentialPoint> & samplingPoints, QString & data, bool relative) {}

void SvgParser::printVec(const Eigen::Vector2d & v, QString name)
{
    qDebug() << name << "=" << v[0] << "," << v[1];
}

bool SvgParser::addEllipticalArc(QList<PotentialPoint> & samplingPoints, QString & data, const SvgPresentationAttributes & pa, bool relative)
{
    trimFront(data);
    bool hasLooped = false, ok;
    while(true)
    {
        Eigen::Vector2d startLoc(samplingPoints.last().getX(), samplingPoints.last().getY());

        double rx = qAbs(getNextDouble(data, &ok));
        if(!ok) break;
        trimCommaWspFront(data);
        double ry = qAbs(getNextDouble(data, &ok));
        if(!ok) break;

        trimCommaWspFront(data);
        double rot = getNextDouble(data, &ok);
        if(!ok) break;

        // Comma-wsp is required so run check before trimming
        if(!(data.at(0) == ',' || data.at(0) == 0x20 || data.at(0) == 0x9 || data.at(0) == 0xD || data.at(0) == 0xA)) return false;
        trimCommaWspFront(data);

        bool largeArc = getNextFlag(data, &ok);
        if(!ok) break;
        trimCommaWspFront(data);
        bool positiveSweep = getNextFlag(data, &ok);
        if(!ok) break;
        trimCommaWspFront(data);

        Eigen::Vector2d endLoc = getNextCoordinatePair(data, &ok);
        if(!ok) break;
        if(relative)
        {
            endLoc += startLoc;
        }
        if(startLoc == endLoc) return true;

        // TODO use Eigen for applying transformation matricies
        Eigen::Matrix2d rotationForward, rotationBackwards;
        rotationForward << qCos(rot), qSin(rot), -qSin(rot), qCos(rot);
        rotationBackwards << qCos(rot), -qSin(rot), qSin(rot), qCos(rot);

        Eigen::Vector2d midLoc((startLoc[0] - endLoc[0]) / 2, (startLoc[1] - endLoc[1]) / 2);
        midLoc = rotationForward * midLoc;
        double sFactor = midLoc[0]*midLoc[0]/(rx*rx) + midLoc[1]*midLoc[1]/(ry*ry);
        if(sFactor > 1)
        {
            rx *= qSqrt(sFactor);
            ry *= qSqrt(sFactor);
        }
        // TODO figure out why cCoeff can be NaN in some cases
        double cCoeff = qSqrt((rx*rx*ry*ry-rx*rx*midLoc[1]*midLoc[1]-ry*ry*midLoc[0]*midLoc[0]) / (rx*rx*midLoc[1]*midLoc[1]+ry*ry*midLoc[0]*midLoc[0]));
        if(largeArc == positiveSweep) cCoeff *= -1;
        Eigen::Vector2d center(cCoeff * rx * midLoc[1] / ry, cCoeff * -ry * midLoc[0] / rx);
        Eigen::Vector2d centerRot(qCos(rot) * center[0] - qSin(rot) * center[1] + (startLoc[0] + endLoc[0]) / 2, qSin(rot) * center[0] + qCos(rot) * center[1] + (startLoc[1] + endLoc[1]) / 2);
        Eigen::Vector2d u((midLoc[0] - center[0]) / rx, (midLoc[1] - center[1]) / ry), v((-midLoc[0] - center[0]) / rx, (-midLoc[1] - center[1]) / ry);
        double startAngle = qAcos(((midLoc[0] - center[0]) / rx) / u.norm());
        if((midLoc[1] - center[1]) / ry < 0) startAngle *= -1;
        double angleDelta = std::fmod(qAcos(u.dot(v) / (u.norm() * v.norm())), 2 * M_PI);
        if(u[0] * v[1] - u[1] * v[0] < 0) angleDelta *= -1;
        if(!positiveSweep && angleDelta > 0) angleDelta -= 2 * M_PI;
        else if (positiveSweep && angleDelta < 0) angleDelta += 2 * M_PI;

        printVec(startLoc, "startLoc");
        printVec(endLoc, "endLoc");
        printVec(midLoc, "midLoc");
        qDebug() << "cCoeff = " << cCoeff;
        printVec(center, "center");
        printVec(centerRot, "centerRot");
        qDebug() << "startAngle = " << startAngle << endl << "angleDelta = " << angleDelta;
        qDebug() << (midLoc[1] - center[1]) / ry;

        //qDebug() << midLocRot[0] << "," << midLocRot[1] << "-" << center[0] << "," << center[1] << "-" << startAngle << "-" << angleDelta;
        //qDebug() << qSqrt(qPow((midLocRot[0] - center[0]) / rx, 2) + qPow((midLocRot[1] - center[1]) / ry, 2));

        SculptCurve::Curve<VectorAnimationComplex::EdgeSample> curve;
        samplingPoints.push_back(PotentialPoint(endLoc, pa.strokeWidth));

        populateSamplesRecursive(startAngle + angleDelta / 2, qAbs(angleDelta), samplingPoints, samplingPoints.end()-2, pa.strokeWidth, curve.ds(), [&] (const double t) -> Eigen::Vector2d
        {
            double newT = t;
            if(angleDelta < 0) newT = startAngle + angleDelta - (t - startAngle);
            double baseX = rx * qCos(newT), baseY = ry * qSin(newT);
            return Eigen::Vector2d(qCos(rot) * baseX - qSin(rot) * baseY + centerRot[0], qSin(rot) * baseX + qCos(rot) * baseY + centerRot[1]);
        });

        hasLooped = true;
    }
    return hasLooped;
}

/** Finishes a path (or subpath), closing and creating faces as necessary
 *
 * @returns Position of end point
 */
Eigen::Vector2d SvgParser::finishPath(QList<PotentialPoint> & samplingPoints, const SvgPresentationAttributes pa, bool closed)
{
    if(samplingPoints.size() < 2) {
        return Eigen::Vector2d(0, 0);
    }

    VectorAnimationComplex::VAC *vac = global()->scene()->activeVAC();

    // Reverse points
    /*QList<PotentialPoint> samplingPoints;
    while(!samplingPointsR.empty()) {
        samplingPoints.append(samplingPointsR.first());
        samplingPointsR.removeFirst();
    }*/

    // If the endpoints are not the same, join endpoints on a closed path or a path with a face
    // TODO consider making EdgeSample equality operators
    if((closed || pa.hasFill()) && samplingPoints.first().distanceTo(samplingPoints.last()) == 0) {
        if(!addLineTo(samplingPoints, QString::number(samplingPoints.first().getX()).append(",").append(QString::number(samplingPoints.first().getY())), pa, false)) {
            return Eigen::Vector2d(0, 0);
        }
    }

    QVector<VectorAnimationComplex::KeyVertex *> v;
    QVector<VectorAnimationComplex::KeyEdge *> e;

    SculptCurve::Curve<VectorAnimationComplex::EdgeSample> curC;
    for(PotentialPoint point : samplingPoints)
    {
        if(!point.isSmooth()) {
            // The point is non-smooth, so close off the current curve if necessary and make a new vertex
            VectorAnimationComplex::KeyVertex * newV = vac->newKeyVertex(global()->activeTime(), Eigen::Vector2d(point.getX(), point.getY()));
            if(curC.size() != 0)
            {
                curC.continueSketch(point.getEdgeSample());
                curC.endSketch();
                e.push_back(vac->newKeyEdge(global()->activeTime(), v.last(), newV, (new VectorAnimationComplex::LinearSpline(curC)), pa.strokeWidth));
                e.last()->setColor(pa.stroke);
            }
            v.push_back(newV);
            curC.beginSketch(point.getEdgeSample());
        }
        else {
            curC.continueSketch(point.getEdgeSample());
        }
    }

    if(closed) {
        e.push_back(vac->newKeyEdge(global()->activeTime(), v.last(), v.first(), new VectorAnimationComplex::LinearSpline(SculptCurve::Curve<VectorAnimationComplex::EdgeSample>(samplingPoints.last().getEdgeSample(), samplingPoints.first().getEdgeSample())), pa.strokeWidth));
        e.last()->setColor(pa.stroke);
    }
    else if(pa.hasFill()) {
        VectorAnimationComplex::EdgeSample start(samplingPoints.last().getEdgeSample()), end(samplingPoints.first().getEdgeSample());
        start.setWidth(0);
        end.setWidth(0);

        e.push_back(vac->newKeyEdge(global()->activeTime(), v.last(), v.first(), new VectorAnimationComplex::LinearSpline(SculptCurve::Curve<VectorAnimationComplex::EdgeSample>(start, end)), 0));
        e.last()->setColor(QColor());
    }

    if(pa.hasFill()) {
        QList<VectorAnimationComplex::KeyHalfedge> halfEdges;
        for(auto edge : e) {
            halfEdges.append(VectorAnimationComplex::KeyHalfedge(edge, true));
        }
        VectorAnimationComplex::Cycle cycle(halfEdges);
        VectorAnimationComplex::KeyFace * face = vac->newKeyFace(cycle);
        face->setColor(pa.fill);
    }

    return Eigen::Vector2d(samplingPoints.last().getX(), samplingPoints.last().getY());
}

void SvgParser::readElement_(XmlStreamReader &xml, QStack<SvgPresentationAttributes> attributeStack)
{
    if(xml.isStartElement())
    {
        SvgPresentationAttributes pa(attributeStack.top());
        pa.init(xml, *this);
        attributeStack.push(pa);
        qDebug() << "Pushing" << pa;

        qDebug() << xml.name();

        if(xml.name() == "rect")
        {
            if(!readRect_(xml, pa)) return;
        }
        else if(xml.name() == "line")
        {
            if(!readLine_(xml, pa)) return;
        }
        else if(xml.name() == "polyline")
        {
            if(!readPolyline_(xml, pa)) return;
        }
        else if(xml.name() == "polygon")
        {
            if(!readPolygon_(xml, pa)) return;
        }
        else if(xml.name() == "circle")
        {
            if(!readCircle_(xml, pa)) return;
        }
        else if(xml.name() == "ellipse")
        {
            if(!readEllipse_(xml, pa)) return;
        }
        else if(xml.name() == "path")
        {
            if(!readPath_(xml, pa)) return;
        }
        else if(xml.name() == "g") {
            // We don't have to do anything more here
        }
        else {
            // Warning
        }
    }

    if(xml.isEndElement()) {
        SvgPresentationAttributes rmPa = attributeStack.pop();
        qDebug() << "Popping" << rmPa;
    }

    if(!xml.atEnd()) {
        xml.readNext();
        readElement_(xml, attributeStack);
    }
}

void SvgParser::readSvg_(XmlStreamReader & xml)
{
    xml.readNextStartElement();
    if(xml.name() != "svg") {
        // Error
    }
    xml.readNextStartElement();
    SvgPresentationAttributes pa;
    QStack<SvgPresentationAttributes> attributeStack;
    SvgPresentationAttributes baseStyle;
    attributeStack.push(baseStyle);
    readElement_(xml, attributeStack);
}

SvgPresentationAttributes::SvgPresentationAttributes() {
    reset();
}

SvgPresentationAttributes::SvgPresentationAttributes(XmlStreamReader &xml, SvgParser & parser) {
    reset();
    init(xml, parser);
}

void SvgPresentationAttributes::reset() {
    strokeWidth = 1;
    fill = Qt::black;
    stroke = Qt::transparent;
}

void SvgPresentationAttributes::init(XmlStreamReader &xml, SvgParser &parser) {
    bool okay = true;

    // Stroke width
    if(xml.attributes().hasAttribute("stroke-width")) {
        double tempStrokeWidth = qMax(0.0, xml.attributes().value("stroke-width").toDouble(&okay));
        if(okay) strokeWidth = tempStrokeWidth;
    }

    // Fill (color)
    if(xml.attributes().hasAttribute("fill")) {
        QColor tempFill = parser.parseColor_(xml.attributes().value("fill").toString());
        if(tempFill.isValid()) fill = tempFill;
    }

    // Stroke (color)
    if(xml.attributes().hasAttribute("stroke")) {
        QColor tempStroke = parser.parseColor_(xml.attributes().value("stroke").toString());
        if(tempStroke.isValid()) stroke = tempStroke;
    }

    // Opacity (whole object)
    double opacity = xml.attributes().hasAttribute("opacity") ? qBound(0.0, xml.attributes().value("opacity").toDouble(&okay), 1.0) : 1;
    if(!okay) opacity = 1;

    // Fill opacity
    double tempFillOpacity = xml.attributes().hasAttribute("fill-opacity") ? qBound(0.0, xml.attributes().value("fill-opacity").toDouble(&okay), 1.0) : 1;
    if(!okay) tempFillOpacity = 1;
    fill.setAlphaF(fill.alphaF() * tempFillOpacity * opacity);

    // Stroke opacity
    double tempStrokeOpacity = xml.attributes().hasAttribute("stroke-opacity") ? qBound(0.0, xml.attributes().value("stroke-opacity").toDouble(&okay), 1.0) : 1;
    if(!okay) tempStrokeOpacity = 1;
    stroke.setAlphaF(stroke.alphaF() * tempStrokeOpacity * opacity);
}

SvgPresentationAttributes::operator QString() const
{
    return QString("SvgPresentationAttribute(Fill = %1, Stroke = %2 @ %3 px)").arg(fill.name(QColor::HexArgb), stroke.name(QColor::HexArgb), QString::number(strokeWidth));
}

bool SvgParser::getNextFlag(QString & source, bool * ok)
{
    if(source.isEmpty())
    {
        *ok = false;
        return false;
    }
    bool res = source.at(0) != '0';
    source.remove(0, 1);
    return res;
}

double SvgParser::getNextDouble(QString & source, bool * ok)
{
    QRegExp realNumberExp("[+\\-]?(([0-9]*\\.?[0-9]*)|(\\.[0-9]+))([Ee][0-9]+)?");
    if(realNumberExp.indexIn(source) != 0)
    {
        *ok = false;
        return 1;
    }

    double res = realNumberExp.cap(0).toDouble(ok);
    if(!*ok)
    {
        return 1;
    }

    source.remove(0, realNumberExp.cap(0).length());

    return res;
}

// For coordinate pair detection *with optional comma-wsp* which applies only to path elements, not polylines/polygons
Eigen::Vector2d SvgParser::getNextCoordinatePair(QString & source, bool * ok)
{
    QString s(static_cast<const QString>(source));

    // Find first number
    double x = getNextDouble(s, ok);
    if(!*ok) return Eigen::Vector2d(0, 0);

    // Remove /\s*,\s/
    trimCommaWspFront(s);

    // Find second number
    double y = getNextDouble(s, ok);
    if(!*ok) return Eigen::Vector2d(0, 0);

    source = s;
    *ok = true;
    return Eigen::Vector2d(x, y);
}

void SvgParser::trimFront(QString & string, QVector<QChar> charactersToRemove) {
    int i = 0;
    while(!string.isEmpty() && charactersToRemove.contains(string.at(i))) i++;
    if(i > 0) string.remove(0, i);
}

void SvgParser::trimCommaWspFront(QString & string) {
    trimFront(string);
    if(!string.isEmpty() && string.at(0) == ',')
    {
        string.remove(0, 1);
        trimFront(string);
    }
}

QList<PotentialPoint>::iterator SvgParser::populateSamplesRecursive(double paramVal, double paramSpan, QList<PotentialPoint> & edgeSamples, QList<PotentialPoint>::iterator pointLoc, double strokeWidth, double ds, std::function<Eigen::Vector2d (double)> getPoint)
{
    //if((*pointLoc).distanceTo(*(pointLoc+1)) <= ds) return;

    Eigen::Vector2d newPoint = getPoint(paramVal);
    VectorAnimationComplex::EdgeSample newSample(newPoint[0], newPoint[1], strokeWidth);
    if(newSample.distanceTo(pointLoc->getEdgeSample()) < ds / 2 || newSample.distanceTo((pointLoc+1)->getEdgeSample()) < ds / 2) return pointLoc;
    pointLoc = edgeSamples.insert(pointLoc+1, newSample);

    pointLoc = populateSamplesRecursive(paramVal + paramSpan / 4, paramSpan / 2, edgeSamples, pointLoc, strokeWidth, ds, getPoint);
    pointLoc = populateSamplesRecursive(paramVal - paramSpan / 4, paramSpan / 2, edgeSamples, pointLoc-1, strokeWidth, ds, getPoint);
    return pointLoc;
}
