#import <XCTest/XCTest.h>
#import "../Library/GBLibraryStore.h"
#import "../Runtime/GBRuntimeController.h"

@interface GBApplicationTests : XCTestCase
@end

@implementation GBApplicationTests
- (UIView *)findView:(UIView *)root matching:(BOOL (^)(UIView *))predicate {
    if (predicate(root)) return root;
    for (UIView *child in root.subviews) { UIView *match = [self findView:child matching:predicate]; if (match) return match; }
    return nil;
}
- (BOOL)waitFor:(BOOL (^)(void))condition {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while (!condition() && deadline.timeIntervalSinceNow > 0) [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    return condition();
}
- (void)testImportedApkExecutesWithNativeControls {
    NSURL *fixture = [[NSBundle bundleForClass:self.class] URLForResource:@"conformance" withExtension:@"apk"];
    XCTAssertNotNil(fixture);
    NSURL *directory = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    GBLibraryStore *store = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTestExpectation *imported = [self expectationWithDescription:@"apk imported"];
    __block GBInstalledApp *app;
    [store importURL:fixture completion:^(GBInstalledApp *result, NSError *error) { XCTAssertNil(error); app = result; [imported fulfill]; }];
    [self waitForExpectations:@[imported] timeout:15];
    XCTAssertEqualObjects(app.packageName, @"org.gutenberg.conformance");
    XCTAssertEqual(store.apps.count, 1U);
    GBLibraryStore *reopened = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTAssertEqual(reopened.apps.count, 1U);
    UIWindow *window = UIApplication.sharedApplication.delegate.window;
    UIViewController *previous = window.rootViewController;
    UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:[UIViewController new]];
    window.rootViewController = navigation;
    GBRuntimeController *runtime = [[GBRuntimeController alloc] initWithApp:app];
    [navigation pushViewController:runtime animated:NO];
    BOOL (^hasText)(NSString *) = ^BOOL(NSString *text) {
        return [self findView:runtime.view matching:^BOOL(UIView *view) { return [view isKindOfClass:UILabel.class] && [((UILabel *)view).text isEqualToString:text]; }] != nil;
    };
    XCTAssertTrue([self waitFor:^BOOL { return hasText(@"Count: 0"); }]);
    UITextField *input = (UITextField *)[self findView:runtime.view matching:^BOOL(UIView *view) { return [view isKindOfClass:UITextField.class]; }];
    UIButton *button = (UIButton *)[self findView:runtime.view matching:^BOOL(UIView *view) { return [view isKindOfClass:UIButton.class] && [((UIButton *)view).currentTitle isEqualToString:@"Increment"]; }];
    XCTAssertNotNil(input); XCTAssertNotNil(button);
    input.text = @"Gutenberg"; [input sendActionsForControlEvents:UIControlEventEditingChanged];
    [button sendActionsForControlEvents:UIControlEventTouchUpInside];
    XCTAssertTrue([self waitFor:^BOOL { return hasText(@"Gutenberg: 1"); }]);
    XCTAssertTrue([self waitFor:^BOOL { return [runtime.title isEqualToString:@"Handler ready"]; }]);
    [runtime.view layoutIfNeeded];
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
    UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) { [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES]; }];
    XCTAttachment *attachment = [XCTAttachment attachmentWithImage:image]; attachment.lifetime = XCTAttachmentLifetimeKeepAlways; attachment.name = @"android-activity"; [self addAttachment:attachment];
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    [UIImagePNGRepresentation(image) writeToURL:[documents URLByAppendingPathComponent:@"activity-validation.png"] atomically:YES];
    [navigation popViewControllerAnimated:NO]; window.rootViewController = previous;
    NSError *error = nil; XCTAssertTrue([store removeApp:app error:&error]); XCTAssertNil(error);
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
}

- (void)testMalformedApkDoesNotEnterLibrary {
    NSURL *directory = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    GBLibraryStore *store = [[GBLibraryStore alloc] initWithDirectory:directory];
    NSURL *file = [directory URLByAppendingPathComponent:@"broken.apk"];
    [@"invalid archive" writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:nil];
    XCTestExpectation *finished = [self expectationWithDescription:@"invalid apk rejected"];
    [store importURL:file completion:^(GBInstalledApp *app, NSError *error) { XCTAssertNil(app); XCTAssertNotNil(error); [finished fulfill]; }];
    [self waitForExpectations:@[finished] timeout:15]; XCTAssertEqual(store.apps.count, 0U);
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
}
@end
