#import <UIKit/UIKit.h>
#ifdef __cplusplus
#include "gutenberg/android.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN
@interface GBSceneView : UIView
@property(nonatomic, copy, nullable) void (^onClick)(uint64_t handle);
@property(nonatomic, copy, nullable) void (^onTextChanged)(uint64_t handle, NSString* text);
@property(nonatomic, copy, nullable) void (^onCheckedChanged)(uint64_t handle, BOOL checked);
@property(nonatomic, copy, nullable) void (^onLayoutError)(NSString* message);
@property(nonatomic, copy, nullable) void (^onTouch)
    (uint64_t handle, int action, float x, float y, float pressure, int64_t timeMs);
#ifdef __cplusplus
@property(nonatomic, copy, nullable) void (^onGeometry)(const std::vector<gutenberg::ViewSize>& sizes);
- (void)displayScene:(const gutenberg::Scene&)scene;
#endif
@end
NS_ASSUME_NONNULL_END
