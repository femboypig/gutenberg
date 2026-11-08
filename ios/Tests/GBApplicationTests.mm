#import "../Library/GBLibraryStore.h"
#import "../Runtime/GBRuntimeController.h"
#import <XCTest/XCTest.h>

@interface GBApplicationTests : XCTestCase
@end

@implementation GBApplicationTests
- (UIView*)findView:(UIView*)root matching:(BOOL (^)(UIView*))predicate {
    if (predicate(root))
        return root;
    for (UIView* child in root.subviews) {
        UIView* match = [self findView:child matching:predicate];
        if (match)
            return match;
    }
    return nil;
}
- (BOOL)waitFor:(BOOL (^)(void))condition {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while (!condition() && deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    return condition();
}
- (void)collectInputs:(UIView*)view into:(NSMutableArray<UITextField*>*)inputs {
    if ([view isKindOfClass:UITextField.class])
        [inputs addObject:(UITextField*)view];
    for (UIView* child in view.subviews)
        [self collectInputs:child into:inputs];
}
- (void)testUnchangedThirdPartyApkGeneratesNumbers {
    NSURL* fixture = [[NSBundle bundleForClass:self.class] URLForResource:@"randnum" withExtension:@"apk"];
    XCTAssertNotNil(fixture);
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                   isDirectory:YES];
    GBLibraryStore* store = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTestExpectation* imported = [self expectationWithDescription:@"third party apk imported"];
    __block GBInstalledApp* app;
    [store importURL:fixture
          completion:^(GBInstalledApp* result, NSError* error) {
              XCTAssertNil(error);
              app = result;
              [imported fulfill];
          }];
    [self waitForExpectations:@[ imported ] timeout:15];
    XCTAssertEqualObjects(app.packageName, @"org.andrew.randomnumber");
    UIWindow* window = UIApplication.sharedApplication.delegate.window;
    UIViewController* previous = window.rootViewController;
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:[UIViewController new]];
    window.rootViewController = navigation;
    GBRuntimeController* runtime = [[GBRuntimeController alloc] initWithApp:app];
    [navigation pushViewController:runtime animated:NO];
    __block UIButton* button;
    XCTAssertTrue([self waitFor:^BOOL {
        button = (UIButton*)[self findView:runtime.view
                                  matching:^BOOL(UIView* view) {
                                      return [view isKindOfClass:UIButton.class] &&
                                             [((UIButton*)view).currentTitle isEqualToString:@"Generate"];
                                  }];
        return button != nil;
    }]);
    NSMutableArray<UITextField*>* inputs = [NSMutableArray array];
    [self collectInputs:runtime.view into:inputs];
    [inputs sortUsingComparator:^NSComparisonResult(UITextField* a, UITextField* b) {
        return a.tag < b.tag ? NSOrderedAscending : a.tag > b.tag ? NSOrderedDescending : NSOrderedSame;
    }];
    XCTAssertEqual(inputs.count, 3U);
    if (inputs.count == 3) {
        inputs[0].text = @"7";
        inputs[1].text = @"7";
        inputs[2].text = @"3";
        for (UITextField* input in inputs)
            [input sendActionsForControlEvents:UIControlEventEditingChanged];
        [button sendActionsForControlEvents:UIControlEventTouchUpInside];
        XCTAssertTrue([self waitFor:^BOOL {
            return [self findView:runtime.view
                         matching:^BOOL(UIView* view) {
                             return [view isKindOfClass:UILabel.class] &&
                                    [((UILabel*)view).text isEqualToString:@"7\n7\n7"];
                         }] != nil;
        }]);
        [runtime.view layoutIfNeeded];
        UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
        UIImage* image = [renderer imageWithActions:^(UIGraphicsImageRendererContext* context) {
            [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
        }];
        XCTAttachment* attachment = [XCTAttachment attachmentWithImage:image];
        attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
        attachment.name = @"third-party-activity";
        [self addAttachment:attachment];
        NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                                inDomains:NSUserDomainMask]
                               .firstObject;
        [UIImagePNGRepresentation(image)
            writeToURL:[documents URLByAppendingPathComponent:@"external-validation.png"]
            atomically:YES];
    }
    [navigation popViewControllerAnimated:NO];
    window.rootViewController = previous;
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
}
- (void)testImportedApkExecutesWithNativeControls {
    NSURL* fixture = [[NSBundle bundleForClass:self.class] URLForResource:@"conformance"
                                                            withExtension:@"apk"];
    XCTAssertNotNil(fixture);
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                   isDirectory:YES];
    GBLibraryStore* store = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTestExpectation* imported = [self expectationWithDescription:@"apk imported"];
    __block GBInstalledApp* app;
    [store importURL:fixture
          completion:^(GBInstalledApp* result, NSError* error) {
              XCTAssertNil(error);
              app = result;
              [imported fulfill];
          }];
    [self waitForExpectations:@[ imported ] timeout:15];
    XCTAssertEqualObjects(app.packageName, @"org.gutenberg.conformance");
    XCTAssertEqual(store.apps.count, 1U);
    GBLibraryStore* reopened = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTAssertEqual(reopened.apps.count, 1U);
    UIWindow* window = UIApplication.sharedApplication.delegate.window;
    UIViewController* previous = window.rootViewController;
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:[UIViewController new]];
    window.rootViewController = navigation;
    GBRuntimeController* runtime = [[GBRuntimeController alloc] initWithApp:app];
    [navigation pushViewController:runtime animated:NO];
    BOOL (^hasText)(NSString*) = ^BOOL(NSString* text) {
        return [self findView:runtime.view
                     matching:^BOOL(UIView* view) {
                         return [view isKindOfClass:UILabel.class] &&
                                [((UILabel*)view).text isEqualToString:text];
                     }] != nil;
    };
    XCTAssertTrue([self waitFor:^BOOL {
        return hasText(@"Count: 0");
    }]);
    UITextField* input = (UITextField*)[self findView:runtime.view
                                             matching:^BOOL(UIView* view) {
                                                 return [view isKindOfClass:UITextField.class];
                                             }];
    UIButton* button =
        (UIButton*)[self findView:runtime.view
                         matching:^BOOL(UIView* view) {
                             return [view isKindOfClass:UIButton.class] &&
                                    [((UIButton*)view).currentTitle isEqualToString:@"Increment"];
                         }];
    XCTAssertNotNil(input);
    XCTAssertNotNil(button);
    input.text = @"Gutenberg";
    [input sendActionsForControlEvents:UIControlEventEditingChanged];
    [button sendActionsForControlEvents:UIControlEventTouchUpInside];
    XCTAssertTrue([self waitFor:^BOOL {
        return hasText(@"Gutenberg: 1");
    }]);
    XCTAssertTrue([self waitFor:^BOOL {
        return [runtime.title isEqualToString:@"Handler ready"];
    }]);
    [runtime.view layoutIfNeeded];
    UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
    UIImage* image = [renderer imageWithActions:^(UIGraphicsImageRendererContext* context) {
        [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
    }];
    XCTAttachment* attachment = [XCTAttachment attachmentWithImage:image];
    attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
    attachment.name = @"android-activity";
    [self addAttachment:attachment];
    NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                            inDomains:NSUserDomainMask]
                           .firstObject;
    [UIImagePNGRepresentation(image)
        writeToURL:[documents URLByAppendingPathComponent:@"activity-validation.png"]
        atomically:YES];
    [navigation popViewControllerAnimated:NO];
    window.rootViewController = previous;
    NSError* error = nil;
    XCTAssertTrue([store removeApp:app error:&error]);
    XCTAssertNil(error);
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
}

- (void)testMalformedApkDoesNotEnterLibrary {
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                   isDirectory:YES];
    GBLibraryStore* store = [[GBLibraryStore alloc] initWithDirectory:directory];
    NSURL* file = [directory URLByAppendingPathComponent:@"broken.apk"];
    [@"invalid archive" writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:nil];
    XCTestExpectation* finished = [self expectationWithDescription:@"invalid apk rejected"];
    [store importURL:file
          completion:^(GBInstalledApp* app, NSError* error) {
              XCTAssertNil(app);
              XCTAssertNotNil(error);
              [finished fulfill];
          }];
    [self waitForExpectations:@[ finished ] timeout:15];
    XCTAssertEqual(store.apps.count, 0U);
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
}
@end
