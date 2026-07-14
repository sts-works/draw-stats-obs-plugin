#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include <cstdio>

namespace {

    bool pointerDown = false;
    bool pointerMoved = false;
    CFMachPortRef eventTap = nullptr;
    CFRunLoopSourceRef eventTapSource = nullptr;
    NSString *lastPermissionState = nil;

    void emitJSON(NSDictionary *payload)
    {
        NSError *error = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:payload options:0 error:&error];
        if (!data || error)
            return;
        fwrite(data.bytes, 1, data.length, stdout);
        fwrite("\n", 1, 1, stdout);
        fflush(stdout);
    }

    void emitPermission(NSString *state)
    {
        if ([lastPermissionState isEqualToString:state])
            return;
        lastPermissionState = [state copy];
        emitJSON(@ {@"type": @"permission", @"state": state});
    }

    NSDictionary *activeApplication()
    {
        NSRunningApplication *application = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (!application)
            return @ {@"app": @"", @"process": @"", @"bundle": @""};
        return @ {
            @"app": application.localizedName ?: @"",
            @"process": application.executableURL.lastPathComponent ?: @"",
            @"bundle": application.bundleIdentifier ?: @"",
        };
    }

    void emitRunningApplications()
    {
        NSMutableDictionary<NSString *, NSMutableDictionary *> *byPath = [NSMutableDictionary dictionary];
        for (NSRunningApplication *application in NSWorkspace.sharedWorkspace.runningApplications) {
            NSString *name = application.localizedName ?: @"";
            NSString *process = application.executableURL.lastPathComponent ?: @"";
            if (name.length == 0 && process.length == 0)
                continue;
            NSString *path = application.bundleURL.path ?: application.executableURL.path ?: process;
            byPath[path] = [@{
      @"name" : name,
      @"process" : process,
      @"bundle" : application.bundleIdentifier ?: @"",
      @"path" : application.bundleURL.path ?: application.executableURL.path ?: @"",
      @"running" : @YES,
    } mutableCopy];
        }

        NSArray<NSString *> *roots = @[
            @"/Applications", @"/System/Applications",
            [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]
        ];
        NSFileManager *manager = NSFileManager.defaultManager;
        for (NSString *root in roots) {
            NSURL *rootURL = [NSURL fileURLWithPath:root isDirectory:YES];
            NSDirectoryEnumerator<NSURL *> *enumerator =
                [manager enumeratorAtURL:rootURL includingPropertiesForKeys:nil
                                       options:NSDirectoryEnumerationSkipsHiddenFiles |
                                               NSDirectoryEnumerationSkipsPackageDescendants
                                  errorHandler:nil];
            for (NSURL *url in enumerator) {
                if (![url.pathExtension.lowercaseString isEqualToString:@"app"])
                    continue;
                NSBundle *bundle = [NSBundle bundleWithURL:url];
                NSString *name = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"] ?: [bundle objectForInfoDictionaryKey:@"CFBundleName"] ?: url.URLByDeletingPathExtension.lastPathComponent;
                NSString *process = [bundle objectForInfoDictionaryKey:@"CFBundleExecutable"] ?: @"";
                NSMutableDictionary *item = byPath[url.path];
                if (!item) {
                    item = [@{
                        @"name": name ?: @"",
                        @"process": process,
                        @"bundle": bundle.bundleIdentifier ?: @"",
                        @"path": url.path,
                        @"running": @NO,
                    } mutableCopy];
                    byPath[url.path] = item;
                }
            }
        }
        NSArray *items =
            [byPath.allValues sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *left, NSDictionary *right) {
                return [left[@"name"] localizedCaseInsensitiveCompare:right[@"name"]];
            }];
        emitJSON(@ {@"type": @"apps", @"items": items});
    }

    bool isTextKey(CGKeyCode keyCode, CGEventFlags flags)
    {
        const CGEventFlags commandFlags = kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate;
        if ((flags & commandFlags) != 0)
            return false;
        if (keyCode <= 50)
            return keyCode != 36 && keyCode != 48 && keyCode != 51;
        return false;
    }

    void emitInput(NSString *kind)
    {
        NSMutableDictionary *payload = [NSMutableDictionary dictionaryWithDictionary:activeApplication()];
        payload[@"type"] = @"input";
        payload[@"kind"] = kind;
        emitJSON(payload);
    }

    CGEventRef eventCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void *)
    {
        switch (type) {
            case kCGEventKeyDown: {
                const CGKeyCode keyCode =
                    static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
                emitInput(isTextKey(keyCode, CGEventGetFlags(event)) ? @"text" : @"keyboard");
                break;
            }
            case kCGEventLeftMouseDown:
            case kCGEventRightMouseDown:
            case kCGEventOtherMouseDown:
                pointerDown = true;
                pointerMoved = false;
                break;
            case kCGEventLeftMouseDragged:
            case kCGEventRightMouseDragged:
            case kCGEventOtherMouseDragged:
                if (pointerDown)
                    pointerMoved = true;
                break;
            case kCGEventLeftMouseUp:
            case kCGEventRightMouseUp:
            case kCGEventOtherMouseUp:
                if (pointerDown)
                    emitInput(pointerMoved ? @"drag" : @"click");
                pointerDown = false;
                pointerMoved = false;
                break;
            case kCGEventScrollWheel:
                emitInput(@"wheel");
                break;
            case kCGEventTapDisabledByTimeout:
            case kCGEventTapDisabledByUserInput:
                emitJSON(@ {@"type": @"status", @"state": @"event_tap_disabled"});
                if (eventTap)
                    CGEventTapEnable(eventTap, true);
                break;
            default:
                break;
        }
        return event;
    }

    CGEventMask inputEventMask()
    {
        return CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventLeftMouseDown) |
               CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventLeftMouseDragged) |
               CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
               CGEventMaskBit(kCGEventRightMouseDragged) | CGEventMaskBit(kCGEventOtherMouseDown) |
               CGEventMaskBit(kCGEventOtherMouseUp) | CGEventMaskBit(kCGEventOtherMouseDragged) |
               CGEventMaskBit(kCGEventScrollWheel);
    }

    void removeEventTap()
    {
        if (eventTapSource) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), eventTapSource, kCFRunLoopCommonModes);
            CFRelease(eventTapSource);
            eventTapSource = nullptr;
        }
        if (eventTap) {
            CFRelease(eventTap);
            eventTap = nullptr;
        }
    }

    bool installEventTap()
    {
        if (eventTap)
            return true;
        if (!CGPreflightListenEventAccess()) {
            emitPermission(@"required");
            return false;
        }
        eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                                    inputEventMask(), eventCallback, nullptr);
        if (!eventTap) {
            emitPermission(@"required");
            return false;
        }
        eventTapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
        if (!eventTapSource) {
            removeEventTap();
            emitPermission(@"unavailable");
            return false;
        }
        CFRunLoopAddSource(CFRunLoopGetCurrent(), eventTapSource, kCFRunLoopCommonModes);
        CGEventTapEnable(eventTap, true);
        emitPermission(@"allowed");
        return true;
    }

}  // namespace

int main()
{
    @autoreleasepool {
        bool allowed = CGPreflightListenEventAccess();
        if (!allowed)
            allowed = CGRequestListenEventAccess();
        if (allowed)
            installEventTap();
        else
            emitPermission(@"required");
        emitRunningApplications();

        [NSTimer scheduledTimerWithTimeInterval:5.0 repeats:YES block:^(__unused NSTimer *timer) {
            emitRunningApplications();
        }];
        [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(__unused NSTimer *timer) {
            if (!CGPreflightListenEventAccess()) {
                removeEventTap();
                emitPermission(@"required");
            } else if (!installEventTap() || !CGEventTapIsEnabled(eventTap)) {
                if (eventTap)
                    CGEventTapEnable(eventTap, true);
            }
        }];
        CFRunLoopRun();
        removeEventTap();
    }
    return 0;
}
