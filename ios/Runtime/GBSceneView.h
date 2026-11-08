#import <UIKit/UIKit.h>
#ifdef __cplusplus
#include "gutenberg/android.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN
@interface GBSceneView : UIView
@property(nonatomic, copy, nullable) void (^onClick)(uint64_t handle);
@property(nonatomic, copy, nullable) void (^onTextChanged)(uint64_t handle, NSString *text);
@property(nonatomic, copy, nullable) void (^onCheckedChanged)(uint64_t handle, BOOL checked);
#ifdef __cplusplus
- (void)displayScene:(const gutenberg::Scene&)scene;
#endif
@end
NS_ASSUME_NONNULL_END
