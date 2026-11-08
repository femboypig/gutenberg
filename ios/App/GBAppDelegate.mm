#import "GBAppDelegate.h"
#import "../Library/GBLibraryController.h"

@implementation GBAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    GBLibraryController *library = [GBLibraryController new];
    UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:library];
    navigation.view.tintColor = [UIColor colorWithRed:0.10 green:0.53 blue:0.40 alpha:1];
    self.window.rootViewController = navigation; [self.window makeKeyAndVisible];
    return YES;
}
- (BOOL)application:(UIApplication *)application openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options {
    if (!url.isFileURL) return NO;
    UINavigationController *navigation = (UINavigationController *)self.window.rootViewController;
    [navigation popToRootViewControllerAnimated:NO];
    [(GBLibraryController *)navigation.viewControllers.firstObject importURL:url];
    return YES;
}
@end
