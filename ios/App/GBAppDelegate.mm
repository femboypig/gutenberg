#import "GBAppDelegate.h"
#import "../Library/GBLibraryController.h"
#import "GBSceneDelegate.h"

@implementation GBAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    return YES;
}
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* configuration = [[UISceneConfiguration alloc] initWithName:@"Default"
                                                                         sessionRole:session.role];
    configuration.delegateClass = GBSceneDelegate.class;
    return configuration;
}
- (BOOL)application:(UIApplication*)application
            openURL:(NSURL*)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options {
    if (!url.isFileURL)
        return NO;
    UINavigationController* navigation = (UINavigationController*)self.window.rootViewController;
    [navigation popToRootViewControllerAnimated:NO];
    [(GBLibraryController*)navigation.viewControllers.firstObject importURL:url];
    return YES;
}
@end
