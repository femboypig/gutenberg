#import "../Library/GBLibraryStore.h"
#import "../Runtime/GBDrawingView.h"
#include "../Runtime/GBHttpClient.h"
#import "../Runtime/GBRuntimeController.h"
#import <XCTest/XCTest.h>
#include <algorithm>
#include <chrono>

@interface GBApplicationTests : XCTestCase
@end

@implementation GBApplicationTests
#if GUTENBERG_COMPATIBILITY_PROBE
- (void)saveCompatibilityReport:(NSDictionary*)report window:(UIWindow*)window {
    NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                            inDomains:NSUserDomainMask]
                           .firstObject;
    NSData* data =
        [NSJSONSerialization dataWithJSONObject:report
                                        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                          error:nil];
    [data writeToURL:[documents URLByAppendingPathComponent:@"probe-report.json"] atomically:YES];
    XCTAttachment* json = [XCTAttachment attachmentWithData:data uniformTypeIdentifier:@"public.json"];
    json.name = @"compatibility-report";
    json.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:json];
    if (window) {
        UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
        UIImage* image = [renderer imageWithActions:^(UIGraphicsImageRendererContext* context) {
            [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
        }];
        [UIImagePNGRepresentation(image)
            writeToURL:[documents URLByAppendingPathComponent:@"probe-validation.png"]
            atomically:YES];
        XCTAttachment* screenshot = [XCTAttachment attachmentWithImage:image];
        screenshot.lifetime = XCTAttachmentLifetimeKeepAlways;
        screenshot.name = @"compatibility-startup";
        [self addAttachment:screenshot];
    }
}
- (void)testCompatibilityApkStartup {
    NSBundle* bundle = [NSBundle bundleForClass:self.class];
    NSURL* fixture = [bundle URLForResource:@"probe" withExtension:@"apk"];
    XCTAssertNotNil(fixture);
    NSData* metadata = [NSData dataWithContentsOfURL:[bundle URLForResource:@"probe" withExtension:@"json"]];
    NSDictionary* source =
        metadata ? [NSJSONSerialization JSONObjectWithData:metadata options:0 error:nil] : nil;
    NSMutableDictionary* report = [NSMutableDictionary dictionaryWithDictionary:source ?: @{}];
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                   isDirectory:YES];
    GBLibraryStore* store = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTestExpectation* imported = [self expectationWithDescription:@"compatibility apk import"];
    __block GBInstalledApp* app = nil;
    __block NSError* failure = nil;
    auto importStart = std::chrono::steady_clock::now();
    [store importURL:fixture
          completion:^(GBInstalledApp* result, NSError* error) {
              app = result;
              failure = error;
              [imported fulfill];
          }];
    [self waitForExpectations:@[ imported ] timeout:90];
    report[@"importSeconds"] =
        @(std::chrono::duration<double>(std::chrono::steady_clock::now() - importStart).count());
    if (!app) {
        report[@"state"] = @"import-failed";
        report[@"failure"] = failure.localizedDescription ?: @"Import did not finish before its deadline.";
        [self saveCompatibilityReport:report window:nil];
        XCTFail(@"compatibility import failed: %@", report[@"failure"]);
        [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
        return;
    }
    report[@"package"] = app.packageName;
    report[@"version"] = app.version;
    report[@"classes"] = @(app.classCount);
    report[@"nativeLibraries"] = @(app.nativeLibraryCount);
    UIWindow* window = UIApplication.sharedApplication.delegate.window;
    UIViewController* previous = window.rootViewController;
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:[UIViewController new]];
    window.rootViewController = navigation;
    GBRuntimeController* runtime = [[GBRuntimeController alloc] initWithApp:app];
    auto startupStart = std::chrono::steady_clock::now();
    [navigation pushViewController:runtime animated:NO];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (runtime.executionState == GBExecutionStateStarting && std::chrono::steady_clock::now() < deadline)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    report[@"startupSeconds"] =
        @(std::chrono::duration<double>(std::chrono::steady_clock::now() - startupStart).count());
    if (runtime.executionState == GBExecutionStateRunning) {
        NSDate* observation = [NSDate dateWithTimeIntervalSinceNow:2];
        while (runtime.executionState == GBExecutionStateRunning && observation.timeIntervalSinceNow > 0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    UIView* content = [self findView:runtime.view
                            matching:^BOOL(UIView* view) {
                                return [view.accessibilityIdentifier hasPrefix:@"guest.view."];
                            }];
    const BOOL running = runtime.executionState == GBExecutionStateRunning && content != nil;
    report[@"state"] = running                                            ? @"running"
                       : runtime.executionState == GBExecutionStateFailed ? @"execution-failed"
                                                                          : @"no-content";
    report[@"failure"] =
        runtime.executionFailure ?: (running ? @"" : @"The activity did not reach a visible content view.");
    [self saveCompatibilityReport:report window:window];
    if (runtime.presentedViewController)
        [runtime dismissViewControllerAnimated:NO completion:nil];
    [navigation popViewControllerAnimated:NO];
    window.rootViewController = previous;
    [NSFileManager.defaultManager removeItemAtURL:directory error:nil];
    XCTAssertTrue(running, @"APK startup did not complete: %@", report[@"failure"]);
}
#endif
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
- (void)testCanvasMatrixResetPreservesClippingAfterZeroScale {
    using namespace gutenberg;
    auto display = std::make_shared<DisplayList>();
    auto append = [&](DrawOperation operation, std::initializer_list<float> coordinates = {},
                      uint32_t color = 0xff000000) {
        DrawCommand command{operation};
        std::copy(coordinates.begin(), coordinates.end(), command.coordinates.begin());
        command.paint.color = color;
        display->commands.push_back(std::move(command));
    };
    append(DrawOperation::color, {}, 0xffffffff);
    append(DrawOperation::save);
    append(DrawOperation::translate, {20, 10});
    append(DrawOperation::clipRect, {0, 0, 20, 20});
    append(DrawOperation::scale, {0, 0});
    append(DrawOperation::rect, {0, 0, 100, 100}, 0xffff0000);
    append(DrawOperation::setMatrix, {1, 0, 0, 0, 1, 0});
    append(DrawOperation::rect, {0, 0, 100, 100}, 0xff00ff00);
    append(DrawOperation::restore);
    append(DrawOperation::concatMatrix, {1, 0, 60, 0, 1, 50});
    append(DrawOperation::rect, {0, 0, 10, 10}, 0xff0000ff);
    GBDrawingView* view = [[GBDrawingView alloc] initWithFrame:CGRectMake(0, 0, 100, 100)];
    view.onRenderError = ^(NSString* message) {
        XCTFail(@"Canvas replay failed: %@", message);
    };
    [view setDisplayList:display];
    // A 1x surface keeps layer resampling out of exact geometry assertions.
    NSMutableData* data = [NSMutableData dataWithLength:100 * 100 * 4];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(data.mutableBytes, 100, 100, 8, 400, space,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    UIGraphicsPushContext(context);
    [view drawRect:view.bounds];
    UIGraphicsPopContext();
    CGContextRelease(context);
    XCTAssertEqual(data.length, 100 * 100 * 4);
    const auto* pixels = static_cast<const uint8_t*>(data.bytes);
    NSUInteger red = 0, green = 0, blue = 0;
    for (NSUInteger n = 0; n < data.length; n += 4) {
        red += pixels[n] > 240 && pixels[n + 1] < 10 && pixels[n + 2] < 10;
        green += pixels[n] < 10 && pixels[n + 1] > 240 && pixels[n + 2] < 10;
        blue += pixels[n] < 10 && pixels[n + 1] < 10 && pixels[n + 2] > 240;
    }
    XCTAssertEqual(red, 0);
    XCTAssertEqual(green, 400);
    XCTAssertEqual(blue, 100);
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
    const auto* pixels = static_cast<const uint8_t*>(beforeTouch.bytes);
    NSUInteger green = 0;
    for (NSUInteger n = 0; n < beforeTouch.length / 4; ++n)
        if (pixels[n * 4] < 80 && pixels[n * 4 + 1] > 150 && pixels[n * 4 + 2] < 80)
            ++green;
    XCTAssertGreaterThan(green, 100u);
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

- (void)testUnchangedCanvasGameAdvancesThroughDirectionalControls {
    NSURL* fixture = [[NSBundle bundleForClass:self.class] URLForResource:@"snake" withExtension:@"apk"];
    XCTAssertNotNil(fixture);
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]
                   isDirectory:YES];
    GBLibraryStore* store = [[GBLibraryStore alloc] initWithDirectory:directory];
    XCTestExpectation* imported = [self expectationWithDescription:@"canvas apk imported"];
    __block GBInstalledApp* app;
    [store importURL:fixture
          completion:^(GBInstalledApp* result, NSError* error) {
              XCTAssertNil(error);
              app = result;
              [imported fulfill];
          }];
    [self waitForExpectations:@[ imported ] timeout:15];
    UIWindow* window = UIApplication.sharedApplication.delegate.window;
    UIViewController* previous = window.rootViewController;
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:[UIViewController new]];
    window.rootViewController = navigation;
    GBRuntimeController* runtime = [[GBRuntimeController alloc] initWithApp:app];
    [navigation pushViewController:runtime animated:NO];
    __block GBDrawingView* drawing = nil;
    XCTAssertTrue([self waitFor:^BOOL {
        drawing = (GBDrawingView*)[self findView:runtime.view
                                        matching:^BOOL(UIView* view) {
                                            return [view isKindOfClass:GBDrawingView.class];
                                        }];
        return drawing != nil && drawing.bounds.size.width > 0;
    }]);
    XCTAssertTrue(navigation.navigationBarHidden);
    UIKeyCommand* controls = nil;
    for (UIKeyCommand* command in runtime.keyCommands)
        if ([command.input isEqualToString:@"g"] &&
            command.modifierFlags == (UIKeyModifierControl | UIKeyModifierShift))
            controls = command;
    XCTAssertNotNil(controls);
    [UIApplication.sharedApplication sendAction:controls.action to:runtime from:controls forEvent:nil];
    UIButton* up =
        (UIButton*)[self findView:runtime.view
                         matching:^BOOL(UIView* view) {
                             return [view.accessibilityIdentifier isEqualToString:@"gutenberg.key.19"];
                         }];
    XCTAssertNotNil(up);
    [self assertVisible:up inController:runtime];
    [up sendActionsForControlEvents:UIControlEventTouchDown];
    [up sendActionsForControlEvents:UIControlEventTouchUpInside];
    __block NSData* firstFrame = nil;
    XCTAssertTrue([self waitFor:^BOOL {
        NSUInteger ignored = 0;
        NSData* frame = [self canvasPixels:drawing bluePixels:&ignored];
        const auto* pixels = static_cast<const uint8_t*>(frame.bytes);
        NSUInteger visible = 0;
        for (NSUInteger n = 0; n < frame.length / 4; ++n)
            if (pixels[n * 4 + 3])
                ++visible;
        if (visible > 1000)
            firstFrame = frame;
        return firstFrame != nil;
    }]);
    XCTAssertTrue([self waitFor:^BOOL {
        NSUInteger ignored = 0;
        NSData* frame = [self canvasPixels:drawing bluePixels:&ignored];
        return frame != nil && ![frame isEqualToData:firstFrame];
    }]);
    UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
    UIImage* image = [renderer imageWithActions:^(UIGraphicsImageRendererContext* context) {
        [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
    }];
    XCTAttachment* attachment = [XCTAttachment attachmentWithImage:image];
    attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
    attachment.name = @"unchanged-canvas-game";
    [self addAttachment:attachment];
    NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                            inDomains:NSUserDomainMask]
                           .firstObject;
    [UIImagePNGRepresentation(image)
        writeToURL:[documents URLByAppendingPathComponent:@"canvas-validation.png"]
        atomically:YES];
    [navigation popViewControllerAnimated:NO];
    XCTAssertTrue([self waitFor:^BOOL {
        return !navigation.navigationBarHidden;
    }]);
    window.rootViewController = previous;
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
