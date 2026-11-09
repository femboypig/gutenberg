#import "GBRuntimeController.h"
#import "../Library/GBLibraryStore.h"
#include "GBHttpClient.h"
#import "GBSceneView.h"
#include <atomic>
#include <mutex>

using namespace gutenberg;

@implementation GBRuntimeController {
    GBInstalledApp* _app;
    GBSceneView* _canvas;
    UIActivityIndicatorView* _spinner;
    UILabel* _status;
    NSMutableArray<NSString*>* _logs;
    dispatch_queue_t _queue;
    std::mutex _sessionMutex;
    std::shared_ptr<AndroidSession> _session;
    NSTimer* _timer;
    std::atomic_bool _pumpPending;
    std::atomic_bool _closing;
    UIView* _controls;
    BOOL _savedNavigationState;
    BOOL _navigationBarWasHidden;
    BOOL _interactivePopWasEnabled;
    __weak UINavigationController* _containerNavigation;
    GBExecutionState _executionState;
    NSString* _executionFailure;
    std::atomic_bool _executionFailed;
}
- (instancetype)initWithApp:(GBInstalledApp*)app {
    if ((self = [super init])) {
        _app = app;
        _logs = [NSMutableArray array];
        _queue = dispatch_queue_create("app.gutenberg.runtime", DISPATCH_QUEUE_SERIAL);
        _pumpPending.store(false);
        _closing.store(false);
        _executionFailed.store(false);
    }
    return self;
}
- (GBExecutionState)executionState {
    return _executionState;
}
- (NSString*)executionFailure {
    return _executionFailure;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = _app.name;
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    _canvas = [GBSceneView new];
    _canvas.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_canvas];
    UIScreenEdgePanGestureRecognizer* back =
        [[UIScreenEdgePanGestureRecognizer alloc] initWithTarget:self action:@selector(returnGesture:)];
    back.edges = UIRectEdgeLeft;
    [self.view addGestureRecognizer:back];
    UITapGestureRecognizer* menu =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(showContainerMenu)];
    menu.numberOfTouchesRequired = 2;
    menu.numberOfTapsRequired = 2;
    menu.cancelsTouchesInView = NO;
    [self.view addGestureRecognizer:menu];
    _controls = [self makeControls];
    _controls.translatesAutoresizingMaskIntoConstraints = NO;
    _controls.hidden = YES;
    [self.view addSubview:_controls];
    [NSLayoutConstraint activateConstraints:@[
        [_controls.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor
                                                 constant:-16],
        [_controls.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor
                                               constant:-24],
        [_controls.widthAnchor constraintEqualToConstant:156],
        [_controls.heightAnchor constraintEqualToConstant:156]
    ]];
    [NSLayoutConstraint activateConstraints:@[
        [_canvas.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [_canvas.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [_canvas.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_canvas.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]
    ]];
    _spinner =
        [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge];
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_spinner];
    [_spinner startAnimating];
    _status = [UILabel new];
    _status.text = @"Loading package…";
    _status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    _status.textColor = UIColor.secondaryLabelColor;
    _status.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_status];
    [NSLayoutConstraint activateConstraints:@[
        [_spinner.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [_spinner.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor constant:-20],
        [_status.topAnchor constraintEqualToAnchor:_spinner.bottomAnchor constant:16],
        [_status.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor]
    ]];
    __weak GBRuntimeController* weakSelf = self;
    _canvas.onLayoutError = ^(NSString* message) {
        [weakSelf reportFailure:message];
    };
    _canvas.onClick = ^(uint64_t handle) {
        [weakSelf perform:[handle](AndroidSession& s) { s.click(handle); }];
    };
    _canvas.onTextChanged = ^(uint64_t handle, NSString* text) {
        const std::string value(text.UTF8String ?: "");
        [weakSelf perform:[handle, value](AndroidSession& s) { s.changeText(handle, value); }];
    };
    _canvas.onCheckedChanged = ^(uint64_t handle, BOOL checked) {
        [weakSelf perform:[handle, checked](AndroidSession& s) { s.changeChecked(handle, checked); }];
    };
    _canvas.onGeometry = ^(const std::vector<ViewSize>& sizes) {
        [weakSelf perform:[sizes](AndroidSession& s) { s.resizeViews(sizes); }];
    };
    _canvas.onTouch = ^(uint64_t handle, int action, float x, float y, float pressure, int64_t timeMs) {
        [weakSelf perform:[handle, action, x, y, pressure, timeMs](AndroidSession& s) {
            s.touch(handle, action, x, y, pressure, timeMs);
        }];
    };
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(backgrounded)
                                               name:UIApplicationDidEnterBackgroundNotification
                                             object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(foregrounded)
                                               name:UIApplicationWillEnterForegroundNotification
                                             object:nil];
    [self start];
    _timer = [NSTimer
        scheduledTimerWithTimeInterval:0.05
                               repeats:YES
                                 block:^(NSTimer* timer) {
                                     GBRuntimeController* owner = weakSelf;
                                     if (!owner) {
                                         [timer invalidate];
                                         return;
                                     }
                                     if (owner->_closing.load() || owner->_pumpPending.exchange(true))
                                         return;
                                     dispatch_async(owner->_queue, ^{
                                         [owner
                                             performNow:[](AndroidSession& session) { session.pumpTasks(); }];
                                         owner->_pumpPending.store(false);
                                     });
                                 }];
}
- (BOOL)canBecomeFirstResponder {
    return YES;
}
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    _containerNavigation = self.navigationController;
    if (!_savedNavigationState) {
        _navigationBarWasHidden = self.navigationController.navigationBarHidden;
        _interactivePopWasEnabled = self.navigationController.interactivePopGestureRecognizer.enabled;
        _savedNavigationState = YES;
    }
    [self.navigationController setNavigationBarHidden:YES animated:animated];
    self.navigationController.interactivePopGestureRecognizer.enabled = NO;
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self becomeFirstResponder];
}
- (NSArray<UIKeyCommand*>*)keyCommands {
    NSMutableArray<UIKeyCommand*>* commands = [NSMutableArray array];
    for (NSString* input in
         @[ UIKeyInputUpArrow, UIKeyInputDownArrow, UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\r", @" " ])
        [commands addObject:[UIKeyCommand keyCommandWithInput:input
                                                modifierFlags:0
                                                       action:@selector(keyCommand:)]];
    UIKeyCommand* menu = [UIKeyCommand keyCommandWithInput:@"\x1b"
                                             modifierFlags:0
                                                    action:@selector(showContainerMenu)];
    menu.discoverabilityTitle = @"Gutenberg menu";
    [commands addObject:menu];
    UIKeyCommand* controls = [UIKeyCommand keyCommandWithInput:@"g"
                                                 modifierFlags:UIKeyModifierControl | UIKeyModifierShift
                                                        action:@selector(toggleControls)];
    controls.discoverabilityTitle = @"Toggle directional controls";
    [commands addObject:controls];
    return commands;
}
- (void)keyCommand:(UIKeyCommand*)command {
    const int code = [command.input isEqualToString:UIKeyInputUpArrow]      ? 19
                     : [command.input isEqualToString:UIKeyInputDownArrow]  ? 20
                     : [command.input isEqualToString:UIKeyInputLeftArrow]  ? 21
                     : [command.input isEqualToString:UIKeyInputRightArrow] ? 22
                     : [command.input isEqualToString:@" "]                 ? 62
                                                                            : 66;
    [self perform:[code](AndroidSession& session) {
        session.key(code, true);
        session.key(code, false);
    }];
}
- (UIView*)makeControls {
    UIStackView* vertical = [UIStackView new];
    vertical.axis = UILayoutConstraintAxisVertical;
    vertical.distribution = UIStackViewDistributionFillEqually;
    vertical.spacing = 4;
    NSArray<NSArray*>* rows = @[ @[ @0, @19, @0 ], @[ @21, @23, @22 ], @[ @0, @20, @0 ] ];
    NSDictionary* labels = @{@19 : @"↑", @20 : @"↓", @21 : @"←", @22 : @"→", @23 : @"●"};
    NSDictionary* names = @{@19 : @"Up", @20 : @"Down", @21 : @"Left", @22 : @"Right", @23 : @"Select"};
    for (NSArray<NSNumber*>* codes in rows) {
        UIStackView* row = [UIStackView new];
        row.distribution = UIStackViewDistributionFillEqually;
        row.spacing = 4;
        for (NSNumber* code in codes) {
            if (!code.intValue) {
                [row addArrangedSubview:[UIView new]];
                continue;
            }
            UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
            button.configuration = UIButtonConfiguration.borderedProminentButtonConfiguration;
            button.tag = code.integerValue;
            [button setTitle:labels[code] forState:UIControlStateNormal];
            button.accessibilityLabel = names[code];
            button.accessibilityIdentifier = [NSString stringWithFormat:@"gutenberg.key.%@", code];
            [button addTarget:self action:@selector(keyDown:) forControlEvents:UIControlEventTouchDown];
            [button addTarget:self
                          action:@selector(keyUp:)
                forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                                 UIControlEventTouchCancel];
            [row addArrangedSubview:button];
        }
        [vertical addArrangedSubview:row];
    }
    return vertical;
}
- (void)toggleControls {
    _controls.hidden = !_controls.hidden;
    if (!_controls.hidden)
        [self.view bringSubviewToFront:_controls];
}
- (void)returnGesture:(UIScreenEdgePanGestureRecognizer*)gesture {
    if (gesture.state != UIGestureRecognizerStateEnded)
        return;
    const auto distance = [gesture translationInView:self.view].x;
    const auto speed = [gesture velocityInView:self.view].x;
    if (distance > MAX(72, self.view.bounds.size.width * 0.2) || (distance > 16 && speed > 600))
        [self.navigationController popViewControllerAnimated:YES];
}
- (void)showContainerMenu {
    if (self.presentedViewController)
        return;
    UIAlertController* menu = [UIAlertController alertControllerWithTitle:_app.name
                                                                  message:nil
                                                           preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GBRuntimeController* weakSelf = self;
    [menu addAction:[UIAlertAction actionWithTitle:@"Back"
                                             style:UIAlertActionStyleDefault
                                           handler:^(__unused UIAlertAction* action) {
                                               [weakSelf
                                                   perform:[](AndroidSession& session) { session.back(); }];
                                           }]];
    [menu addAction:[UIAlertAction actionWithTitle:_controls.hidden ? @"Show directional controls"
                                                                    : @"Hide directional controls"
                                             style:UIAlertActionStyleDefault
                                           handler:^(__unused UIAlertAction* action) {
                                               [weakSelf toggleControls];
                                           }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"View logs"
                                             style:UIAlertActionStyleDefault
                                           handler:^(__unused UIAlertAction* action) {
                                               [weakSelf showLogs];
                                           }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Return to library"
                                             style:UIAlertActionStyleDefault
                                           handler:^(__unused UIAlertAction* action) {
                                               [weakSelf.navigationController popViewControllerAnimated:YES];
                                           }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    menu.popoverPresentationController.sourceView = self.view;
    menu.popoverPresentationController.sourceRect =
        CGRectMake(self.view.bounds.size.width / 2, self.view.bounds.size.height / 2, 1, 1);
    menu.popoverPresentationController.permittedArrowDirections = 0;
    [self presentViewController:menu animated:YES completion:nil];
}
- (void)keyDown:(UIButton*)button {
    const int code = static_cast<int>(button.tag);
    [self perform:[code](AndroidSession& s) { s.key(code, true); }];
}
- (void)keyUp:(UIButton*)button {
    const int code = static_cast<int>(button.tag);
    [self perform:[code](AndroidSession& s) { s.key(code, false); }];
}
- (std::shared_ptr<AndroidSession>)session {
    std::lock_guard lock(_sessionMutex);
    return _session;
}
- (void)start {
    const CGSize size = self.view.bounds.size;
    __weak GBRuntimeController* weakSelf = self;
    NSString* namespaceKey = [@"guest." stringByAppendingString:_app.identifier];
    NSURL* file = _app.fileURL;
    dispatch_async(_queue, ^{
        if (self->_closing.load())
            return;
        try {
            auto package = std::make_shared<Package>(readFile(file.fileSystemRepresentation));
            AndroidHost host;
            host.http = performHttpRequest;
            host.dataDirectory = [file.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"data"
                                                                                      isDirectory:YES]
                                     .fileSystemRepresentation;
            host.screenWidth = static_cast<float>(size.width);
            host.screenHeight = static_cast<float>(size.height);
            host.present = [weakSelf](const Scene& scene) {
                auto snapshot = std::make_shared<Scene>(scene);
                dispatch_async(dispatch_get_main_queue(), ^{
                    GBRuntimeController* owner = weakSelf;
                    if (!owner || owner->_closing.load())
                        return;
                    [owner->_spinner stopAnimating];
                    owner->_status.hidden = YES;
                    [owner->_canvas displayScene:*snapshot];
                    if (!snapshot->title.empty())
                        owner.title = @(snapshot->title.c_str());
                    if (snapshot->finished)
                        [owner.navigationController popViewControllerAnimated:YES];
                });
            };
            host.log = [weakSelf](std::string_view line) {
                NSString* message = [[NSString alloc] initWithBytes:line.data()
                                                             length:line.size()
                                                           encoding:NSUTF8StringEncoding]
                                        ?: @"";
                dispatch_async(dispatch_get_main_queue(), ^{
                    [weakSelf appendLog:message];
                });
            };
            host.toast = [weakSelf](std::string_view text) {
                NSString* message = [[NSString alloc] initWithBytes:text.data()
                                                             length:text.size()
                                                           encoding:NSUTF8StringEncoding]
                                        ?: @"";
                dispatch_async(dispatch_get_main_queue(), ^{
                    [weakSelf showToast:message];
                });
            };
            host.openUrl = [](std::string_view text) {
                NSString* string = [[NSString alloc] initWithBytes:text.data()
                                                            length:text.size()
                                                          encoding:NSUTF8StringEncoding];
                NSURL* url = [NSURL URLWithString:string];
                if (![@[ @"http", @"https", @"mailto" ] containsObject:url.scheme.lowercaseString])
                    return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    [UIApplication.sharedApplication openURL:url options:@{} completionHandler:nil];
                });
            };
            host.readPreference = [namespaceKey](std::string_view key) {
                NSString* name = [[NSString alloc] initWithBytes:key.data()
                                                          length:key.size()
                                                        encoding:NSUTF8StringEncoding];
                NSString* value = [NSUserDefaults.standardUserDefaults
                    stringForKey:[namespaceKey stringByAppendingFormat:@".%@", name]];
                return std::string(value.UTF8String ?: "");
            };
            host.writePreference = [namespaceKey](std::string_view key, std::string_view value) {
                NSString* name = [[NSString alloc] initWithBytes:key.data()
                                                          length:key.size()
                                                        encoding:NSUTF8StringEncoding];
                NSString* text = [[NSString alloc] initWithBytes:value.data()
                                                          length:value.size()
                                                        encoding:NSUTF8StringEncoding];
                [NSUserDefaults.standardUserDefaults
                    setObject:text
                       forKey:[namespaceKey stringByAppendingFormat:@".%@", name]];
            };
            auto session = std::make_shared<AndroidSession>(package, std::move(host));
            {
                std::lock_guard lock(self->_sessionMutex);
                self->_session = session;
            }
            session->launch();
            const bool finished = session->scene().finished;
            dispatch_async(dispatch_get_main_queue(), ^{
                GBRuntimeController* owner = weakSelf;
                if (owner && !owner->_closing.load() && !owner->_executionFailed.load())
                    owner->_executionState = finished ? GBExecutionStateStopped : GBExecutionStateRunning;
            });
        } catch (const GuestException& error) {
            [self reportException:error];
        } catch (const std::exception& error) {
            [self reportFailure:@(error.what())];
        }
    });
}
- (void)perform:(std::function<void(AndroidSession&)>)action {
    if (_closing.load())
        return;
    dispatch_async(_queue, ^{
        [self performNow:action];
    });
}
- (void)performNow:(const std::function<void(AndroidSession&)>&)action {
    if (_closing.load() || _executionFailed.load())
        return;
    auto session = [self session];
    if (!session)
        return;
    try {
        action(*session);
    } catch (const GuestException& error) {
        [self reportException:error];
    } catch (const std::exception& error) {
        [self reportFailure:@(error.what())];
    }
}
- (void)reportException:(const GuestException&)error {
    NSMutableString* message = [NSMutableString stringWithUTF8String:error.what()];
    for (const auto& frame : error.frames)
        [message appendFormat:@"\n  at %s", frame.c_str()];
    [self reportFailure:message];
}
- (void)reportFailure:(NSString*)message {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_closing.load() || self->_executionFailed.exchange(true))
            return;
        self->_executionState = GBExecutionStateFailed;
        self->_executionFailure = [message copy];
        if (auto session = [self session])
            session->cancel();
        [self appendLog:message];
        [self->_spinner stopAnimating];
        self->_status.hidden = YES;
        [self->_timer invalidate];
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Execution stopped"
                                                                       message:message
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"View logs"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction* action) {
                                                    [self showLogs];
                                                }]];
        __weak GBRuntimeController* weakSelf = self;
        [alert addAction:[UIAlertAction actionWithTitle:@"Return to library"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(__unused UIAlertAction* action) {
                                                    [weakSelf.navigationController
                                                        popViewControllerAnimated:YES];
                                                }]];
        if (!self.presentedViewController)
            [self presentViewController:alert animated:YES completion:nil];
    });
}
- (void)appendLog:(NSString*)message {
    [_logs addObject:message];
    if (_logs.count > 2000)
        [_logs removeObjectsInRange:NSMakeRange(0, _logs.count - 2000)];
}
- (void)showLogs {
    UIViewController* controller = [UIViewController new];
    controller.title = @"Runtime log";
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView* text = [UITextView new];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    text.text = [_logs componentsJoinedByString:@"\n"];
    text.frame = controller.view.bounds;
    text.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [controller.view addSubview:text];
    UINavigationController* navigation =
        [[UINavigationController alloc] initWithRootViewController:controller];
    controller.navigationItem.leftBarButtonItem =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                      target:self
                                                      action:@selector(dismissLogs)];
    controller.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction
                                                      target:self
                                                      action:@selector(shareLogs)];
    [self presentViewController:navigation animated:YES completion:nil];
}
- (void)dismissLogs {
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)shareLogs {
    UIActivityViewController* share =
        [[UIActivityViewController alloc] initWithActivityItems:@[ [_logs componentsJoinedByString:@"\n"] ]
                                          applicationActivities:nil];
    share.popoverPresentationController.barButtonItem =
        ((UINavigationController*)self.presentedViewController)
            .topViewController.navigationItem.rightBarButtonItem;
    [self.presentedViewController presentViewController:share animated:YES completion:nil];
}
- (void)showToast:(NSString*)message {
    UILabel* label = [UILabel new];
    label.text = message;
    label.textColor = UIColor.whiteColor;
    label.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.85];
    label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    label.numberOfLines = 0;
    label.textAlignment = NSTextAlignmentCenter;
    label.layer.cornerRadius = 12;
    label.clipsToBounds = YES;
    CGSize size = [label sizeThatFits:CGSizeMake(self.view.bounds.size.width - 64, 200)];
    label.frame =
        CGRectMake((self.view.bounds.size.width - size.width - 32) / 2,
                   self.view.bounds.size.height - self.view.safeAreaInsets.bottom - size.height - 60,
                   size.width + 32, size.height + 24);
    [self.view addSubview:label];
    [UIView animateWithDuration:0.25
        delay:2
        options:0
        animations:^{
            label.alpha = 0;
        }
        completion:^(__unused BOOL finished) {
            [label removeFromSuperview];
        }];
}
- (void)backgrounded {
    [self perform:[](AndroidSession& s) { s.pause(); }];
}
- (void)foregrounded {
    [self perform:[](AndroidSession& s) { s.resume(); }];
}
- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_containerNavigation setNavigationBarHidden:_navigationBarWasHidden animated:animated];
    _containerNavigation.interactivePopGestureRecognizer.enabled = _interactivePopWasEnabled;
    if (self.isMovingFromParentViewController || self.navigationController.isBeingDismissed) {
        _closing.store(true);
        _executionState = GBExecutionStateStopped;
        [_timer invalidate];
        auto session = [self session];
        if (session)
            session->cancel();
        dispatch_async(_queue, ^{
            std::lock_guard lock(self->_sessionMutex);
            self->_session.reset();
        });
    }
}
- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    if (_closing.load()) {
        [_containerNavigation setNavigationBarHidden:_navigationBarWasHidden animated:NO];
        _containerNavigation.interactivePopGestureRecognizer.enabled = _interactivePopWasEnabled;
    }
}
- (void)dealloc {
    [_timer invalidate];
    [NSNotificationCenter.defaultCenter removeObserver:self];
}
@end
