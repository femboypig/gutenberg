#import "../Library/GBLibraryStore.h"
#import "../Runtime/GBDrawingView.h"
#include "../Runtime/GBHttpClient.h"
#import "../Runtime/GBRuntimeController.h"
#import <XCTest/XCTest.h>

@interface GBApplicationTests : XCTestCase
@end

@implementation GBApplicationTests
- (NSData*)canvasPixels:(UIView*)view bluePixels:(NSUInteger*)bluePixels {
    const auto width = static_cast<size_t>(view.bounds.size.width);
    const auto height = static_cast<size_t>(view.bounds.size.height);
    if (!width || !height || width > 4096 || height > 4096)
        return nil;
    NSMutableData* bytes = [NSMutableData dataWithLength:width * height * 4];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(bytes.mutableBytes, width, height, 8, width * 4, space,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    [view.layer renderInContext:context];
    CGContextRelease(context);
    const auto* pixels = static_cast<const uint8_t*>(bytes.bytes);
    *bluePixels = 0;
    for (size_t n = 0; n < width * height; ++n)
        if (pixels[n * 4] < 60 && pixels[n * 4 + 1] > 60 && pixels[n * 4 + 2] > 140)
            ++*bluePixels;
    return bytes;
}
- (void)testHttpTransportHandlesPostRedirectLimitsAndCancellation {
    XCTestExpectation* finished = [self expectationWithDescription:@"HTTP transport"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        try {
            gutenberg::HttpRequest request;
            request.url = "http://127.0.0.1:8765/echo";
            request.method = "POST";
            request.body = {'a', 'b', 'c'};
            const auto response = gutenberg::performHttpRequest(request, [] { return false; });
            XCTAssertEqual(response.status, 201);
            XCTAssertTrue(response.body == request.body);
            request.url = "http://127.0.0.1:8765/redirect";
            request.method = "GET";
            request.body.clear();
            request.followRedirects = false;
            XCTAssertEqual(gutenberg::performHttpRequest(request, [] { return false; }).status, 302);
            request.followRedirects = true;
            XCTAssertEqual(gutenberg::performHttpRequest(request, [] { return false; }).status, 200);
            request.responseLimit = 4;
            bool limited = false;
            try {
                (void)gutenberg::performHttpRequest(request, [] { return false; });
            } catch (const gutenberg::Error&) {
                limited = true;
            }
            XCTAssertTrue(limited);
            bool cancelled = false;
            try {
                (void)gutenberg::performHttpRequest(request, [] { return true; });
            } catch (const gutenberg::Error& error) {
                cancelled = error.code() == gutenberg::ErrorCode::cancelled;
            }
            XCTAssertTrue(cancelled);
        } catch (const std::exception& error) {
            XCTFail(@"HTTP transport failed: %s", error.what());
        }
        [finished fulfill];
    });
    [self waitForExpectations:@[ finished ] timeout:20];
}
- (void)assertVisible:(UIView*)view inController:(UIViewController*)controller {
    XCTAssertNotNil(view);
    [controller.view layoutIfNeeded];
    const CGRect frame = [view convertRect:view.bounds toView:controller.view];
    const CGRect viewport = CGRectInset(controller.view.safeAreaLayoutGuide.layoutFrame, -1, -1);
    XCTAssertGreaterThan(frame.size.width, 0);
    XCTAssertGreaterThan(frame.size.height, 0);
    XCTAssertTrue(CGRectContainsRect(viewport, frame), @"view outside viewport: %@",
                  NSStringFromCGRect(frame));
}
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
        for (UITextField* input in inputs)
            [self assertVisible:input inController:runtime];
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
        UIView* result = [self findView:runtime.view
                               matching:^BOOL(UIView* view) {
                                   return [view isKindOfClass:UILabel.class] &&
                                          [((UILabel*)view).text isEqualToString:@"7\n7\n7"];
                               }];
        [self assertVisible:result inController:runtime];
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
    __block GBDrawingView* drawing = nil;
    XCTAssertTrue([self waitFor:^BOOL {
        drawing = (GBDrawingView*)[self findView:runtime.view
                                        matching:^BOOL(UIView* view) {
                                            return [view isKindOfClass:GBDrawingView.class];
                                        }];
        NSUInteger count = 0;
        [self canvasPixels:drawing bluePixels:&count];
        return count > 1000;
    }]);
    [self assertVisible:drawing inController:runtime];
    NSUInteger count = 0;
    NSData* beforeTouch = [self canvasPixels:drawing bluePixels:&count];
    XCTAssertNotNil(drawing.onTouch);
    drawing.onTouch(0, 80, 70, 1, 1000);
    XCTAssertTrue([self waitFor:^BOOL {
        NSUInteger countAfter = 0;
        NSData* afterTouch = [self canvasPixels:drawing bluePixels:&countAfter];
        return countAfter > 1000 && ![afterTouch isEqualToData:beforeTouch];
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
    UIButton* fetch =
        (UIButton*)[self findView:runtime.view
                         matching:^BOOL(UIView* view) {
                             return [view isKindOfClass:UIButton.class] &&
                                    [((UIButton*)view).currentTitle isEqualToString:@"Fetch network"];
                         }];
    XCTAssertNotNil(fetch);
    [fetch sendActionsForControlEvents:UIControlEventTouchUpInside];
    XCTAssertTrue([self waitFor:^BOOL {
        return hasText(@"HTTP 200: network message");
    }]);
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
