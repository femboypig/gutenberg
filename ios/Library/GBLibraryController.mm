#import "GBLibraryController.h"
#import "../Runtime/GBRuntimeController.h"
#import "GBLibraryStore.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface GBLibraryController () <UIDocumentPickerDelegate>
@end

@implementation GBLibraryController {
    GBLibraryStore* _store;
    BOOL _importing;
    UIActivityIndicatorView* _progress;
}
- (instancetype)init {
    return [super initWithStyle:UITableViewStyleInsetGrouped];
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Gutenberg";
    self.navigationController.navigationBar.prefersLargeTitles = YES;
    self.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeAlways;
    self.view.tintColor = [UIColor colorWithRed:0.10 green:0.53 blue:0.40 alpha:1];
    self.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"plus"]
                                         style:UIBarButtonItemStylePlain
                                        target:self
                                        action:@selector(choosePackage)];
    self.navigationItem.rightBarButtonItem.accessibilityLabel = @"Import APK";
    NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                            inDomains:NSUserDomainMask]
                           .firstObject;
    _store = [[GBLibraryStore alloc] initWithDirectory:[documents URLByAppendingPathComponent:@"Applications"
                                                                                  isDirectory:YES]];
    [self updateEmptyState];
}
- (void)updateEmptyState {
    if (_store.apps.count) {
        self.contentUnavailableConfiguration = nil;
        return;
    }
    UIContentUnavailableConfiguration* configuration = UIContentUnavailableConfiguration.emptyConfiguration;
    configuration.image = [UIImage systemImageNamed:@"square.stack.3d.up"];
    configuration.text = @"Your Android apps";
    configuration.secondaryText = @"Import an APK from Files to add it to your library.";
    configuration.button.title = @"Import APK";
    __weak GBLibraryController* weakSelf = self;
    configuration.buttonProperties.primaryAction = [UIAction actionWithHandler:^(__unused UIAction* action) {
        [weakSelf choosePackage];
    }];
    self.contentUnavailableConfiguration = configuration;
}
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.navigationController setNavigationBarHidden:NO animated:animated];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
    return _store.apps.count ? 1 : 0;
}
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    return _store.apps.count;
}
- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section {
    return @"Library";
}
- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section {
    return @"Swipe from the left edge to return here. Double-tap with two fingers inside an app to open "
           @"controls.";
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath {
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:@"app"];
    if (!cell)
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"app"];
    GBInstalledApp* app = _store.apps[indexPath.row];
    UIListContentConfiguration* content = cell.defaultContentConfiguration;
    content.text = app.name;
    content.secondaryText = app.version.length
                                ? [NSString stringWithFormat:@"%@ · %@", app.version, app.packageName]
                                : app.packageName;
    content.image = [UIImage systemImageNamed:@"app.fill"];
    content.imageProperties.tintColor = self.view.tintColor;
    content.imageProperties.preferredSymbolConfiguration =
        [UIImageSymbolConfiguration configurationWithPointSize:34 weight:UIImageSymbolWeightRegular];
    content.secondaryTextProperties.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
    cell.contentConfiguration = content;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.accessibilityIdentifier = [@"library.app." stringByAppendingString:app.packageName];
    return cell;
}
- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    [self openApp:_store.apps[indexPath.row]];
}
- (void)openApp:(GBInstalledApp*)app {
    if (!app.launcher.length) {
        [self showError:@"This package has no launcher activity."];
        return;
    }
    GBRuntimeController* runtime = [[GBRuntimeController alloc] initWithApp:app];
    runtime.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeNever;
    [self.navigationController pushViewController:runtime animated:YES];
}
- (UIContextMenuConfiguration*)tableView:(UITableView*)tableView
    contextMenuConfigurationForRowAtIndexPath:(NSIndexPath*)indexPath
                                        point:(CGPoint)point {
    GBInstalledApp* app = _store.apps[indexPath.row];
    __weak GBLibraryController* weakSelf = self;
    return [UIContextMenuConfiguration
        configurationWithIdentifier:nil
                    previewProvider:nil
                     actionProvider:^UIMenu*(__unused NSArray<UIMenuElement*>* suggested) {
                         UIAction* launch = [UIAction actionWithTitle:@"Launch"
                                                                image:[UIImage systemImageNamed:@"play"]
                                                           identifier:nil
                                                              handler:^(__unused UIAction* action) {
                                                                  [weakSelf openApp:app];
                                                              }];
                         UIAction* details =
                             [UIAction actionWithTitle:@"Package details"
                                                 image:[UIImage systemImageNamed:@"info.circle"]
                                            identifier:nil
                                               handler:^(__unused UIAction* action) {
                                                   [weakSelf showDetails:app];
                                               }];
                         UIAction* remove = [UIAction actionWithTitle:@"Delete"
                                                                image:[UIImage systemImageNamed:@"trash"]
                                                           identifier:nil
                                                              handler:^(__unused UIAction* action) {
                                                                  [weakSelf confirmRemoval:app];
                                                              }];
                         remove.attributes = UIMenuElementAttributesDestructive;
                         return [UIMenu menuWithChildren:@[ launch, details, remove ]];
                     }];
}
- (void)showDetails:(GBInstalledApp*)app {
    NSString* message = [NSString
        stringWithFormat:@"%@\nVersion %@\n%lu classes\n%lu native libraries\n\n%@", app.packageName,
                         app.version, (unsigned long)app.classCount, (unsigned long)app.nativeLibraryCount,
                         app.permissions.count ? [app.permissions componentsJoinedByString:@"\n"]
                                               : @"No Android permissions declared."];
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:app.name
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)confirmRemoval:(GBInstalledApp*)app {
    UIAlertController* alert =
        [UIAlertController alertControllerWithTitle:[@"Delete “" stringByAppendingFormat:@"%@”?", app.name]
                                            message:@"The imported APK and its saved data will be removed."
                                     preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Delete"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(__unused UIAlertAction* action) {
                                                NSError* error = nil;
                                                if (![self->_store removeApp:app error:&error]) {
                                                    [self showError:error.localizedDescription];
                                                    return;
                                                }
                                                NSString* prefix =
                                                    [NSString stringWithFormat:@"guest.%@.", app.identifier];
                                                for (NSString* key in NSUserDefaults.standardUserDefaults
                                                         .dictionaryRepresentation)
                                                    if ([key hasPrefix:prefix])
                                                        [NSUserDefaults.standardUserDefaults
                                                            removeObjectForKey:key];
                                                [self.tableView reloadData];
                                                [self updateEmptyState];
                                            }]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)choosePackage {
    if (_importing)
        return;
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    if (urls.firstObject)
        [self importURL:urls.firstObject];
}
- (void)importURL:(NSURL*)url {
    [self loadViewIfNeeded];
    if (_importing) {
        [self showError:@"Another package is being imported."];
        return;
    }
    _importing = YES;
    self.navigationItem.rightBarButtonItem.enabled = NO;
    _progress =
        [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    [_progress startAnimating];
    self.navigationItem.titleView = _progress;
    [_store importURL:url
           completion:^(GBInstalledApp* app, NSError* error) {
               self->_importing = NO;
               self.navigationItem.rightBarButtonItem.enabled = YES;
               self.navigationItem.titleView = nil;
               if (error) {
                   [self showError:error.localizedDescription];
                   return;
               }
               [self.tableView reloadData];
               [self updateEmptyState];
               UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                               [NSString stringWithFormat:@"%@ imported", app.name]);
           }];
}
- (void)showError:(NSString*)message {
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Gutenberg"
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}
@end
