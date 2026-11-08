#import "GBDrawingView.h"
#include <algorithm>
#include <numbers>

using namespace gutenberg;
namespace {
UIColor* color(std::uint32_t argb) {
    return [UIColor colorWithRed:((argb >> 16) & 255) / 255.0
                           green:((argb >> 8) & 255) / 255.0
                            blue:(argb & 255) / 255.0
                           alpha:(argb >> 24) / 255.0];
}
void appendPath(CGContextRef context, const std::vector<PathElement>& path) {
    CGContextBeginPath(context);
    for (const auto& element : path) {
        const auto& p = element.coordinates;
        switch (element.verb) {
        case PathVerb::move:
            CGContextMoveToPoint(context, p[0], p[1]);
            break;
        case PathVerb::line:
            CGContextAddLineToPoint(context, p[0], p[1]);
            break;
        case PathVerb::quad:
            CGContextAddQuadCurveToPoint(context, p[0], p[1], p[2], p[3]);
            break;
        case PathVerb::cubic:
            CGContextAddCurveToPoint(context, p[0], p[1], p[2], p[3], p[4], p[5]);
            break;
        case PathVerb::close:
            CGContextClosePath(context);
            break;
        }
    }
}
void applyPaint(CGContextRef context, const Paint& paint) {
    CGContextSetFillColorWithColor(context, color(paint.color).CGColor);
    CGContextSetStrokeColorWithColor(context, color(paint.color).CGColor);
    CGContextSetLineWidth(context, paint.strokeWidth == 0 ? 1 : paint.strokeWidth);
    CGContextSetLineCap(context, paint.cap == 1   ? kCGLineCapRound
                                 : paint.cap == 2 ? kCGLineCapSquare
                                                  : kCGLineCapButt);
    CGContextSetLineJoin(context, paint.join == 1   ? kCGLineJoinRound
                                  : paint.join == 2 ? kCGLineJoinBevel
                                                    : kCGLineJoinMiter);
    CGContextSetMiterLimit(context, MAX(1, paint.miter));
    CGContextSetShouldAntialias(context, paint.antiAlias);
}
void fillOrStroke(CGContextRef context, const DrawCommand& command) {
    if (command.paint.style == PaintStyle::stroke)
        CGContextStrokePath(context);
    else if (command.paint.style == PaintStyle::fillAndStroke)
        CGContextDrawPath(context, command.evenOdd ? kCGPathEOFillStroke : kCGPathFillStroke);
    else if (command.evenOdd)
        CGContextEOFillPath(context);
    else
        CGContextFillPath(context);
}
void draw(CGContextRef context, const DisplayList& display) {
    unsigned saves = 0;
    CGContextSaveGState(context);
    for (const auto& command : display.commands) {
        const auto& p = command.coordinates;
        switch (command.operation) {
        case DrawOperation::save:
            CGContextSaveGState(context);
            ++saves;
            continue;
        case DrawOperation::restore:
            if (saves) {
                CGContextRestoreGState(context);
                --saves;
            }
            continue;
        case DrawOperation::translate:
            CGContextTranslateCTM(context, p[0], p[1]);
            continue;
        case DrawOperation::scale:
            CGContextScaleCTM(context, p[0], p[1]);
            continue;
        case DrawOperation::rotate:
            CGContextRotateCTM(context, p[0] * std::numbers::pi / 180);
            continue;
        case DrawOperation::skew:
            CGContextConcatCTM(context, CGAffineTransformMake(1, p[1], p[0], 1, 0, 0));
            continue;
        case DrawOperation::clipRect:
            CGContextClipToRect(context, CGRectMake(p[0], p[1], MAX(0, p[2] - p[0]), MAX(0, p[3] - p[1])));
            continue;
        case DrawOperation::clipPath:
            appendPath(context, command.path);
            if (command.evenOdd)
                CGContextEOClip(context);
            else
                CGContextClip(context);
            continue;
        default:
            break;
        }
        applyPaint(context, command.paint);
        if (command.operation == DrawOperation::color) {
            CGContextFillRect(context, CGContextGetClipBoundingBox(context));
        } else if (command.operation == DrawOperation::line) {
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, p[0], p[1]);
            CGContextAddLineToPoint(context, p[2], p[3]);
            CGContextStrokePath(context);
        } else if (command.operation == DrawOperation::point) {
            const auto width = command.paint.strokeWidth == 0 ? 1 : command.paint.strokeWidth;
            const auto box = CGRectMake(p[0] - width / 2, p[1] - width / 2, width, width);
            if (command.paint.cap == 1)
                CGContextFillEllipseInRect(context, box);
            else
                CGContextFillRect(context, box);
        } else if (command.operation == DrawOperation::text) {
            if (command.paint.textSize <= 0)
                continue;
            NSString* text =
                [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar*>(command.text.data())
                                              length:command.text.size()];
            UIFont* font = [UIFont systemFontOfSize:std::min<CGFloat>(4096, command.paint.textSize)];
            NSMutableDictionary* attributes =
                [@{NSFontAttributeName : font, NSForegroundColorAttributeName : color(command.paint.color)}
                    mutableCopy];
            if (command.paint.style != PaintStyle::fill) {
                const auto width = MAX(0.01, command.paint.strokeWidth) / command.paint.textSize * 100;
                attributes[NSStrokeWidthAttributeName] =
                    @(command.paint.style == PaintStyle::stroke ? width : -width);
                attributes[NSStrokeColorAttributeName] = color(command.paint.color);
            }
            const auto width = [text sizeWithAttributes:attributes].width;
            const auto x = p[0] - (command.paint.textAlign == 1   ? width / 2
                                   : command.paint.textAlign == 2 ? width
                                                                  : 0);
            [text drawAtPoint:CGPointMake(x, p[1] - font.ascender) withAttributes:attributes];
        } else {
            if (command.operation == DrawOperation::path)
                appendPath(context, command.path);
            else {
                if (p[0] >= p[2] || p[1] >= p[3])
                    continue;
                const auto rect = CGRectMake(p[0], p[1], p[2] - p[0], p[3] - p[1]);
                CGContextBeginPath(context);
                if (command.operation == DrawOperation::oval)
                    CGContextAddEllipseInRect(context, rect);
                else if (command.operation == DrawOperation::roundRect) {
                    CGPathRef path = CGPathCreateWithRoundedRect(rect, MAX(0, p[4]), MAX(0, p[5]), nullptr);
                    CGContextAddPath(context, path);
                    CGPathRelease(path);
                } else
                    CGContextAddRect(context, rect);
            }
            fillOrStroke(context, command);
        }
    }
    while (saves) {
        CGContextRestoreGState(context);
        --saves;
    }
    CGContextRestoreGState(context);
}
} // namespace

@implementation GBDrawingView {
    std::shared_ptr<const DisplayList> _display;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.opaque = NO;
        self.clipsToBounds = YES;
        self.contentMode = UIViewContentModeRedraw;
        self.accessibilityIdentifier = @"guest.canvas";
    }
    return self;
}
- (void)setDisplayList:(std::shared_ptr<const DisplayList>)displayList {
    if (_display != displayList) {
        _display = std::move(displayList);
        [self setNeedsDisplay];
    }
}
- (void)drawRect:(CGRect)rect {
    if (_display)
        draw(UIGraphicsGetCurrentContext(), *_display);
}
- (void)sendTouches:(NSSet<UITouch*>*)touches action:(int)action {
    UITouch* touch = touches.anyObject;
    if (!touch || !self.onTouch)
        return;
    const auto point = [touch locationInView:self];
    self.onTouch(action, static_cast<float>(point.x), static_cast<float>(point.y),
                 touch.maximumPossibleForce > 0 ? static_cast<float>(touch.force / touch.maximumPossibleForce)
                                                : 1,
                 static_cast<int64_t>(touch.timestamp * 1000));
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self sendTouches:touches action:0];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self sendTouches:touches action:2];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self sendTouches:touches action:1];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self sendTouches:touches action:3];
}
@end
