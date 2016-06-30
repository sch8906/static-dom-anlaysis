/*
 * Copyright (C) 2003, 2006 Apple Computer, Inc.  All rights reserved.
 *                     2006, 2008 Rob Buis <buis@kde.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "Path.h"

#if USE(CG)

#include "AffineTransform.h"
#include "FloatRect.h"
#include "GraphicsContext.h"
#include "IntRect.h"
#include "PlatformString.h"
#include "StrokeStyleApplier.h"
#include <ApplicationServices/ApplicationServices.h>
#include <wtf/MathExtras.h>
#include <wtf/RetainPtr.h>

namespace WebCore {

// A class to provide an isEmpty test that considers a one-element path with only a MoveTo element
// to be empty. This behavior is consistent with other platforms in WebKit, and is needed to prevent
// incorrect (according to the spec) linecap stroking for zero length paths in SVG.
class PathIsEmptyOrSingleMoveTester {
public:
    PathIsEmptyOrSingleMoveTester() : m_moveCount(0) { }

    bool isEmpty() const
    {
        return m_moveCount <= 1;
    }

    static void testPathElement(void* info, const CGPathElement* element)
    {
        PathIsEmptyOrSingleMoveTester* tester = static_cast<PathIsEmptyOrSingleMoveTester*>(info);
        if (element->type == kCGPathElementMoveToPoint)
            ++tester->m_moveCount;
        else {
            // Any non move element implies a non-empty path; set the count to 2 to force
            // isEmpty to return false.
            tester->m_moveCount = 2;
        }
    }

private:
    // Any non-move-to element, or more than one move-to element, will make the count >= 2.
    unsigned m_moveCount;
};

// Paths with only move-to elements do not draw under any circumstances, so their bound should
// be empty. Currently, CoreGraphics returns non-empty bounds for such paths. Radar 10450621
// tracks this. This class reports paths that have only move-to elements, allowing the
// bounding box code to work around the CoreGraphics problem.
class PathHasOnlyMoveToTester {
public:
    PathHasOnlyMoveToTester() : m_hasSeenOnlyMoveTo(true) { }

    bool hasOnlyMoveTo() const
    {
        return m_hasSeenOnlyMoveTo;
    }

    static void testPathElement(void* info, const CGPathElement* element)
    {
        PathHasOnlyMoveToTester* tester = static_cast<PathHasOnlyMoveToTester*>(info);
        if (tester->m_hasSeenOnlyMoveTo && element->type != kCGPathElementMoveToPoint)
            tester->m_hasSeenOnlyMoveTo = false;
    }

private:
    bool m_hasSeenOnlyMoveTo;
};

static size_t putBytesNowhere(void*, const void*, size_t count)
{
    return count;
}

static CGContextRef createScratchContext()
{
    CGDataConsumerCallbacks callbacks = { putBytesNowhere, 0 };
    RetainPtr<CGDataConsumerRef> consumer(AdoptCF, CGDataConsumerCreate(0, &callbacks));
    CGContextRef context = CGPDFContextCreate(consumer.get(), 0, 0);

    CGFloat black[4] = { 0, 0, 0, 1 };
    CGContextSetFillColor(context, black);
    CGContextSetStrokeColor(context, black);

    return context;
}

static inline CGContextRef scratchContext()
{
    static CGContextRef context = createScratchContext();
    return context;
}

Path::Path()
    : m_path(CGPathCreateMutable())
{
}

Path::~Path()
{
    CGPathRelease(m_path);
}

Path::Path(const Path& other)
    : m_path(CGPathCreateMutableCopy(other.m_path))
{
}

Path& Path::operator=(const Path& other)
{
    CGMutablePathRef path = CGPathCreateMutableCopy(other.m_path);
    CGPathRelease(m_path);
    m_path = path;
    return *this;
}

static void copyClosingSubpathsApplierFunction(void* info, const CGPathElement* element)
{
    CGMutablePathRef path = static_cast<CGMutablePathRef>(info);
    CGPoint* points = element->points;
    
    switch (element->type) {
    case kCGPathElementMoveToPoint:
        if (!CGPathIsEmpty(path)) // to silence a warning when trying to close an empty path
            CGPathCloseSubpath(path); // This is the only change from CGPathCreateMutableCopy
        CGPathMoveToPoint(path, 0, points[0].x, points[0].y);
        break;
    case kCGPathElementAddLineToPoint:
        CGPathAddLineToPoint(path, 0, points[0].x, points[0].y);
        break;
    case kCGPathElementAddQuadCurveToPoint:
        CGPathAddQuadCurveToPoint(path, 0, points[0].x, points[0].y, points[1].x, points[1].y);
        break;
    case kCGPathElementAddCurveToPoint:
        CGPathAddCurveToPoint(path, 0, points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y);
        break;
    case kCGPathElementCloseSubpath:
        CGPathCloseSubpath(path);
        break;
    }
}

static CGMutablePathRef copyCGPathClosingSubpaths(CGPathRef originalPath)
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathApply(originalPath, path, copyClosingSubpathsApplierFunction);
    CGPathCloseSubpath(path);
    return path;
}

bool Path::contains(const FloatPoint &point, WindRule rule) const
{
    if (!fastBoundingRect().contains(point))
        return false;

    // CGPathContainsPoint returns false for non-closed paths, as a work-around, we copy and close the path first.  Radar 4758998 asks for a better CG API to use
    RetainPtr<CGMutablePathRef> path(AdoptCF, copyCGPathClosingSubpaths(m_path));
    bool ret = CGPathContainsPoint(path.get(), 0, point, rule == RULE_EVENODD ? true : false);
    return ret;
}

bool Path::strokeContains(StrokeStyleApplier* applier, const FloatPoint& point) const
{
    ASSERT(applier);

    CGContextRef context = scratchContext();

    CGContextSaveGState(context);
    CGContextBeginPath(context);
    CGContextAddPath(context, platformPath());

    GraphicsContext gc(context);
    applier->strokeStyle(&gc);

    bool hitSuccess = CGContextPathContainsPoint(context, point, kCGPathStroke);
    CGContextRestoreGState(context);
    
    return hitSuccess;
}

void Path::translate(const FloatSize& size)
{
    CGAffineTransform translation = CGAffineTransformMake(1, 0, 0, 1, size.width(), size.height());
    CGMutablePathRef newPath = CGPathCreateMutable();
    CGPathAddPath(newPath, &translation, m_path);
    CGPathRelease(m_path);
    m_path = newPath;
}

FloatRect Path::boundingRect() const
{
    // CGPathGetBoundingBox includes the path's control points, CGPathGetPathBoundingBox
    // does not, but only exists on 10.6 and above.
    // A bug in CoreGraphics leads to an incorrect bound on paths containing only move-to elements
    // with a linecap style that is non-butt. All paths with only move-to elements (regardless of
    // linecap) are effectively empty for bounding purposes and here we make it so.
    PathHasOnlyMoveToTester tester;
    CGPathApply(m_path, &tester, PathHasOnlyMoveToTester::testPathElement);
    if (tester.hasOnlyMoveTo())
        return FloatRect(0, 0, 0, 0);

#if !defined(BUILDING_ON_LEOPARD)
    return CGPathGetPathBoundingBox(m_path);
#else
    return CGPathGetBoundingBox(m_path);
#endif
}

FloatRect Path::fastBoundingRect() const
{
    // A bug in CoreGraphics leads to an incorrect bound on paths containing only move-to elements
    // with a linecap style that is non-butt. All paths with only move-to elements (regardless of
    // linecap) are effectively empty for bounding purposes and here we make it so.
    PathHasOnlyMoveToTester tester;
    CGPathApply(m_path, &tester, PathHasOnlyMoveToTester::testPathElement);
    if (tester.hasOnlyMoveTo())
        return FloatRect(0, 0, 0, 0);

    return CGPathGetBoundingBox(m_path);
}

FloatRect Path::strokeBoundingRect(StrokeStyleApplier* applier) const
{
    CGContextRef context = scratchContext();

    CGContextSaveGState(context);
    CGContextBeginPath(context);
    CGContextAddPath(context, platformPath());

    if (applier) {
        GraphicsContext graphicsContext(context);
        applier->strokeStyle(&graphicsContext);
    }

    CGContextReplacePathWithStrokedPath(context);
    CGRect box = CGContextIsPathEmpty(context) ? CGRectZero : CGContextGetPathBoundingBox(context);
    CGContextRestoreGState(context);

    return box;
}

void Path::moveTo(const FloatPoint& point)
{
    CGPathMoveToPoint(m_path, 0, point.x(), point.y());
}

void Path::addLineTo(const FloatPoint& p)
{
    CGPathAddLineToPoint(m_path, 0, p.x(), p.y());
}

void Path::addQuadCurveTo(const FloatPoint& cp, const FloatPoint& p)
{
    CGPathAddQuadCurveToPoint(m_path, 0, cp.x(), cp.y(), p.x(), p.y());
}

void Path::addBezierCurveTo(const FloatPoint& cp1, const FloatPoint& cp2, const FloatPoint& p)
{
    CGPathAddCurveToPoint(m_path, 0, cp1.x(), cp1.y(), cp2.x(), cp2.y(), p.x(), p.y());
}

void Path::addArcTo(const FloatPoint& p1, const FloatPoint& p2, float radius)
{
    CGPathAddArcToPoint(m_path, 0, p1.x(), p1.y(), p2.x(), p2.y(), radius);
}

void Path::closeSubpath()
{
    CGPathCloseSubpath(m_path);
}

void Path::addArc(const FloatPoint& p, float r, float sa, float ea, bool clockwise)
{
    // Workaround for <rdar://problem/5189233> CGPathAddArc hangs or crashes when passed inf as start or end angle
    if (isfinite(sa) && isfinite(ea))
        CGPathAddArc(m_path, 0, p.x(), p.y(), r, sa, ea, clockwise);
}

void Path::addRect(const FloatRect& r)
{
    CGPathAddRect(m_path, 0, r);
}

void Path::addEllipse(const FloatRect& r)
{
    CGPathAddEllipseInRect(m_path, 0, r);
}

void Path::clear()
{
    CGPathRelease(m_path);
    m_path = CGPathCreateMutable();
}

bool Path::isEmpty() const
{
    // The SVG rendering code that uses this method relies on paths with a single move-to
    // element, and nothing else, as being empty. Until that code is refactored to avoid
    // the dependence on isEmpty, we match the behavior of other platforms.
    // When the SVG code is refactored, we could use CGPathIsEmpty(m_path);
    PathIsEmptyOrSingleMoveTester tester;
    CGPathApply(m_path, &tester, PathIsEmptyOrSingleMoveTester::testPathElement);
    return tester.isEmpty();
}

bool Path::hasCurrentPoint() const
{
    return !CGPathIsEmpty(m_path);
}
    
FloatPoint Path::currentPoint() const 
{
    return CGPathGetCurrentPoint(m_path);
}

// MARK: -
// MARK: Path Management

struct PathApplierInfo {
    void* info;
    PathApplierFunction function;
};

static void CGPathApplierToPathApplier(void *info, const CGPathElement *element)
{
    PathApplierInfo* pinfo = (PathApplierInfo*)info;
    FloatPoint points[3];
    PathElement pelement;
    pelement.type = (PathElementType)element->type;
    pelement.points = points;
    CGPoint* cgPoints = element->points;
    switch (element->type) {
    case kCGPathElementMoveToPoint:
    case kCGPathElementAddLineToPoint:
        points[0] = cgPoints[0];
        break;
    case kCGPathElementAddQuadCurveToPoint:
        points[0] = cgPoints[0];
        points[1] = cgPoints[1];
        break;
    case kCGPathElementAddCurveToPoint:
        points[0] = cgPoints[0];
        points[1] = cgPoints[1];
        points[2] = cgPoints[2];
        break;
    case kCGPathElementCloseSubpath:
        break;
    }
    pinfo->function(pinfo->info, &pelement);
}

void Path::apply(void* info, PathApplierFunction function) const
{
    PathApplierInfo pinfo;
    pinfo.info = info;
    pinfo.function = function;
    CGPathApply(m_path, &pinfo, CGPathApplierToPathApplier);
}

void Path::transform(const AffineTransform& transform)
{
    if (transform.isIdentity() || CGPathIsEmpty(m_path))
        return;

    CGMutablePathRef path = CGPathCreateMutable();
    CGAffineTransform transformCG = transform;
    CGPathAddPath(path, &transformCG, m_path);
    CGPathRelease(m_path);
    m_path = path;
}

}

#endif // USE(CG)
