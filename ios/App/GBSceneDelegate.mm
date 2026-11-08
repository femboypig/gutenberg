#import "GBSceneDelegate.h"
#import "../Library/GBLibraryController.h"
#import "GBAppDelegate.h"

@implementation GBSceneDelegate
- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)options {
    if (![scene isKindOfClass:UIWindowScene.class])
        return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:[GBLibraryController new]];
    navigation.view.tintColor = [UIColor colorWithRed:0.10 green:0.53 blue:0.40 alpha:1];
    self.window.rootViewController = navigation;
    ((GBAppDelegate*)UIApplication.sharedApplication.delegate).window = self.window;
    [self.window makeKeyAndVisible];
    [self scene:scene openURLContexts:options.URLContexts];
}
- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)contexts {
    for (UIOpenURLContext* context in contexts) {
        if (!context.URL.isFileURL)
            continue;
        UINavigationController* navigation = (UINavigationController*)self.window.rootViewController;
        [navigation popToRootViewControllerAnimated:NO];
        [(GBLibraryController*)navigation.viewControllers.firstObject importURL:context.URL];
        break;
    }
}
@end
