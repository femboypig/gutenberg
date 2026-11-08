#include "gutenberg/graphics.hpp"
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN
@interface GBDrawingView : UIView
@property(nonatomic, copy, nullable) void (^onRenderError)(NSString* message);
@property(nonatomic, copy, nullable) void (^onTouch)
    (int action, float x, float y, float pressure, int64_t timeMs);
- (void)setDisplayList:(std::shared_ptr<const gutenberg::DisplayList>)displayList;
@end
NS_ASSUME_NONNULL_END
