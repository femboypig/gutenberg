#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface GBInstalledApp : NSObject
@property(nonatomic, copy) NSString* identifier;
@property(nonatomic, copy) NSString* packageName;
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* version;
@property(nonatomic, copy) NSString* launcher;
@property(nonatomic, strong) NSDate* importedAt;
@property(nonatomic) NSUInteger classCount;
@property(nonatomic) NSUInteger nativeLibraryCount;
@property(nonatomic, copy) NSArray<NSString*>* permissions;
@property(nonatomic, strong) NSURL* fileURL;
@end

@interface GBLibraryStore : NSObject
@property(nonatomic, readonly) NSArray<GBInstalledApp*>* apps;
- (instancetype)initWithDirectory:(NSURL*)directory;
- (void)importURL:(NSURL*)url
       completion:(void (^)(GBInstalledApp* _Nullable app, NSError* _Nullable error))completion;
- (BOOL)removeApp:(GBInstalledApp*)app error:(NSError**)error;
@end
NS_ASSUME_NONNULL_END
