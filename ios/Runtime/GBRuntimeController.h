#import <UIKit/UIKit.h>
@class GBInstalledApp;

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSUInteger, GBExecutionState) {
    GBExecutionStateStarting,
    GBExecutionStateRunning,
    GBExecutionStateFailed,
    GBExecutionStateStopped
};

@interface GBRuntimeController : UIViewController
@property(nonatomic, readonly) GBExecutionState executionState;
@property(nonatomic, copy, readonly, nullable) NSString* executionFailure;
- (instancetype)initWithApp:(GBInstalledApp*)app;
@end
NS_ASSUME_NONNULL_END
