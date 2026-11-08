#import "GBLibraryStore.h"
#include "gutenberg/package.hpp"

@implementation GBInstalledApp
@end

@implementation GBLibraryStore {
    NSURL *_directory;
    NSMutableArray<GBInstalledApp *> *_mutableApps;
    dispatch_queue_t _importQueue;
}

- (instancetype)initWithDirectory:(NSURL *)directory {
    if ((self = [super init])) {
        _directory = directory;
        _mutableApps = [NSMutableArray array];
        _importQueue = dispatch_queue_create("app.gutenberg.import", DISPATCH_QUEUE_SERIAL);
        [NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:nil];
        for (NSURL *folder in [NSFileManager.defaultManager contentsOfDirectoryAtURL:directory includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:nil]) {
            NSData *data = [NSData dataWithContentsOfURL:[folder URLByAppendingPathComponent:@"metadata.json"]];
            NSDictionary *metadata = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
            if (![metadata isKindOfClass:NSDictionary.class]) continue;
            NSURL *apk = [folder URLByAppendingPathComponent:@"application.apk"];
            if (![NSFileManager.defaultManager fileExistsAtPath:apk.path]) continue;
            GBInstalledApp *app = [self appFromMetadata:metadata file:apk];
            if (app) [_mutableApps addObject:app];
        }
        [_mutableApps sortUsingComparator:^NSComparisonResult(GBInstalledApp *a, GBInstalledApp *b) { return [b.importedAt compare:a.importedAt]; }];
    }
    return self;
}

- (NSArray<GBInstalledApp *> *)apps { return [_mutableApps copy]; }

- (GBInstalledApp *)appFromMetadata:(NSDictionary *)metadata file:(NSURL *)url {
    for (NSString *key in @[@"identifier", @"package", @"name", @"version", @"launcher"])
        if (![metadata[key] isKindOfClass:NSString.class]) return nil;
    GBInstalledApp *app = [GBInstalledApp new];
    app.identifier = metadata[@"identifier"]; app.packageName = metadata[@"package"]; app.name = metadata[@"name"];
    app.version = metadata[@"version"]; app.launcher = metadata[@"launcher"]; app.fileURL = url;
    app.importedAt = [NSDate dateWithTimeIntervalSince1970:[metadata[@"importedAt"] doubleValue]];
    app.classCount = [metadata[@"classCount"] unsignedIntegerValue]; app.nativeLibraryCount = [metadata[@"nativeLibraries"] unsignedIntegerValue];
    app.permissions = [metadata[@"permissions"] isKindOfClass:NSArray.class] ? metadata[@"permissions"] : @[];
    return app;
}

- (void)importURL:(NSURL *)url completion:(void (^)(GBInstalledApp *, NSError *))completion {
    dispatch_async(_importQueue, ^{
        const BOOL scoped = [url startAccessingSecurityScopedResource];
        NSURL *staging = [self->_directory URLByAppendingPathComponent:[@".import-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
        NSError *failure = nil;
        GBInstalledApp *installed = nil;
        @try {
            if (![NSFileManager.defaultManager createDirectoryAtURL:staging withIntermediateDirectories:YES attributes:nil error:&failure]) return;
            NSURL *apk = [staging URLByAppendingPathComponent:@"application.apk"];
            NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            __block NSError *copyError = nil;
            [coordinator coordinateReadingItemAtURL:url options:0 error:&failure byAccessor:^(NSURL *readable) {
                [NSFileManager.defaultManager copyItemAtURL:readable toURL:apk error:&copyError];
            }];
            if (!failure) failure = copyError;
            if (!failure) {
                try {
                    gutenberg::Package package(gutenberg::readFile(apk.fileSystemRepresentation));
                    const auto& info = package.info();
                    if (package.dexFiles().empty()) throw gutenberg::Error(gutenberg::ErrorCode::unsupported, "This package contains no DEX code. Split APK sets must be combined before importing.");
                    auto string = [](const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @""; };
                    NSUInteger count = 0;
                    for (const auto& dex : package.dexFiles()) count += dex->classes().size();
                    NSMutableArray *permissions = [NSMutableArray array];
                    for (const auto& permission : info.permissions) [permissions addObject:string(permission)];
                    NSString *identifier = NSUUID.UUID.UUIDString;
                    NSDictionary *metadata = @{@"identifier": identifier, @"package": string(info.id), @"name": string(info.label),
                        @"version": string(info.version), @"launcher": string(info.launcher), @"importedAt": @(NSDate.date.timeIntervalSince1970),
                        @"classCount": @(count), @"nativeLibraries": @(info.nativeLibraries.size()), @"permissions": permissions};
                    NSData *data = [NSJSONSerialization dataWithJSONObject:metadata options:NSJSONWritingSortedKeys error:&failure];
                    if (data && [data writeToURL:[staging URLByAppendingPathComponent:@"metadata.json"] options:NSDataWritingAtomic error:&failure]) {
                        NSURL *destination = [self->_directory URLByAppendingPathComponent:identifier isDirectory:YES];
                        if ([NSFileManager.defaultManager moveItemAtURL:staging toURL:destination error:&failure])
                            installed = [self appFromMetadata:metadata file:[destination URLByAppendingPathComponent:@"application.apk"]];
                    }
                } catch (const std::exception& error) {
                    failure = [NSError errorWithDomain:@"Gutenberg" code:1 userInfo:@{NSLocalizedDescriptionKey: @(error.what())}];
                }
            }
        } @finally {
            [NSFileManager.defaultManager removeItemAtURL:staging error:nil];
            if (scoped) [url stopAccessingSecurityScopedResource];
            if (!installed && !failure) failure = [NSError errorWithDomain:@"Gutenberg" code:2 userInfo:@{NSLocalizedDescriptionKey: @"The package could not be imported."}];
            GBInstalledApp *result = installed; NSError *error = failure;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (result) [self->_mutableApps insertObject:result atIndex:0];
                completion(result, error);
            });
        }
    });
}

- (BOOL)removeApp:(GBInstalledApp *)app error:(NSError **)error {
    if (![NSFileManager.defaultManager removeItemAtURL:app.fileURL.URLByDeletingLastPathComponent error:error]) return NO;
    [_mutableApps removeObject:app]; return YES;
}
@end
