#include "GBHttpClient.h"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <mutex>

using namespace gutenberg;
namespace {
struct RequestState {
    std::mutex mutex;
    HttpResponse response;
    std::string error;
    ErrorCode errorCode = ErrorCode::io;
    dispatch_semaphore_t completion = dispatch_semaphore_create(0);
};
NSString* string(std::string_view value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}
std::string text(NSString* value) {
    return value.UTF8String ?: "";
}
} // namespace

@interface GBHttpDelegate : NSObject <NSURLSessionDataDelegate> {
    HttpRequest _request;
    std::shared_ptr<RequestState> _state;
}
- (instancetype)initWithRequest:(const HttpRequest&)request state:(std::shared_ptr<RequestState>)state;
@end

@implementation GBHttpDelegate
- (instancetype)initWithRequest:(const HttpRequest&)request state:(std::shared_ptr<RequestState>)state {
    if ((self = [super init])) {
        _request = request;
        _state = std::move(state);
    }
    return self;
}
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    if (!_request.followRedirects) {
        completionHandler(nil);
        return;
    }
    NSString* scheme = request.URL.scheme.lowercaseString;
    if (![@[ @"http", @"https" ] containsObject:scheme] ||
        ([scheme isEqualToString:@"http"] && !_request.allowCleartext)) {
        {
            std::lock_guard lock(_state->mutex);
            _state->error = "redirect violates the package network policy";
        }
        completionHandler(nil);
        return;
    }
    completionHandler(request);
}
- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    if (![response isKindOfClass:NSHTTPURLResponse.class]) {
        {
            std::lock_guard lock(_state->mutex);
            _state->error = "response is not HTTP";
        }
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    if (response.expectedContentLength > 0 &&
        static_cast<std::uint64_t>(response.expectedContentLength) > _request.responseLimit) {
        {
            std::lock_guard lock(_state->mutex);
            _state->error = "HTTP response size limit exceeded";
        }
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    {
        std::lock_guard lock(_state->mutex);
        auto& output = _state->response;
        NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
        output.status = static_cast<std::int32_t>(http.statusCode);
        output.finalUrl = text(http.URL.absoluteString);
        for (id name in http.allHeaderFields)
            output.headers[text([name description])] = text([http.allHeaderFields[name] description]);
    }
    completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    bool overflow = false;
    {
        std::lock_guard lock(_state->mutex);
        auto& bytes = _state->response.body;
        if (data.length > _request.responseLimit - bytes.size()) {
            _state->error = "HTTP response size limit exceeded";
            overflow = true;
        } else if (data.length) {
            const auto* begin = static_cast<const std::uint8_t*>(data.bytes);
            bytes.insert(bytes.end(), begin, begin + data.length);
        }
    }
    if (overflow)
        [task cancel];
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    {
        std::lock_guard lock(_state->mutex);
        if (error && _state->error.empty()) {
            _state->error = text(error.localizedDescription);
            if ([error.domain isEqualToString:NSURLErrorDomain])
                switch (error.code) {
                case NSURLErrorTimedOut:
                    _state->errorCode = ErrorCode::network_timeout;
                    break;
                case NSURLErrorCannotFindHost:
                case NSURLErrorDNSLookupFailed:
                    _state->errorCode = ErrorCode::network_dns;
                    break;
                case NSURLErrorCannotConnectToHost:
                    _state->errorCode = ErrorCode::network_connect;
                    break;
                case NSURLErrorSecureConnectionFailed:
                case NSURLErrorServerCertificateHasBadDate:
                case NSURLErrorServerCertificateUntrusted:
                case NSURLErrorServerCertificateHasUnknownRoot:
                case NSURLErrorServerCertificateNotYetValid:
                    _state->errorCode = ErrorCode::network_tls;
                    break;
                }
        }
    }
    dispatch_semaphore_signal(_state->completion);
}
@end

namespace gutenberg {
HttpResponse performHttpRequest(const HttpRequest& request, const std::function<bool()>& cancelled) {
    @autoreleasepool {
        if (cancelled())
            throw Error(ErrorCode::cancelled, "HTTP request cancelled");
        NSURL* url = [NSURL URLWithString:string(request.url)];
        if (!url || ![@[ @"http", @"https" ] containsObject:url.scheme.lowercaseString])
            throw Error(ErrorCode::io, "invalid HTTP URL");
        if ([url.scheme.lowercaseString isEqualToString:@"http"] && !request.allowCleartext)
            throw Error(ErrorCode::io, "cleartext HTTP is not permitted");
        auto state = std::make_shared<RequestState>();
        GBHttpDelegate* delegate = [[GBHttpDelegate alloc] initWithRequest:request state:state];
        NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        configuration.HTTPCookieStorage = nil;
        configuration.URLCredentialStorage = nil;
        configuration.URLCache = nil;
        configuration.connectionProxyDictionary = @{};
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        configuration.timeoutIntervalForRequest =
            request.readTimeoutMs > 0 ? request.readTimeoutMs / 1000.0 : 60;
        configuration.timeoutIntervalForResource =
            std::max(60.0, configuration.timeoutIntervalForRequest * 2);
        NSOperationQueue* queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration
                                                              delegate:delegate
                                                         delegateQueue:queue];
        NSMutableURLRequest* message = [NSMutableURLRequest requestWithURL:url];
        message.HTTPMethod = string(request.method);
        message.timeoutInterval = configuration.timeoutIntervalForRequest;
        for (const auto& [name, value] : request.headers)
            [message setValue:string(value) forHTTPHeaderField:string(name)];
        if (!request.body.empty())
            message.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:message];
        [task resume];
        while (dispatch_semaphore_wait(state->completion,
                                       dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0) {
            if (cancelled()) {
                [session invalidateAndCancel];
                throw Error(ErrorCode::cancelled, "HTTP request cancelled");
            }
        }
        [session finishTasksAndInvalidate];
        std::lock_guard lock(state->mutex);
        if (!state->error.empty())
            throw Error(state->errorCode, state->error);
        return std::move(state->response);
    }
}
} // namespace gutenberg
