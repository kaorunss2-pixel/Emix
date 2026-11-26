// EasyMix (macOS): Cocoa implementation of the small platform layer.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include "PlatformApi.h"

#include <string>
#include <vector>

namespace
{
    // "Desc\0*.wav;*.mp3\0..." -> { "wav", "mp3" } (first entry only, "*.*" -> empty = anything)
    std::vector<std::string> ExtensionsFromFilter(const char* filter)
    {
        std::vector<std::string> exts;
        if (!filter)
            return exts;

        const char* desc = filter;
        if (!*desc)
            return exts;
        const char* pattern = desc + strlen(desc) + 1;

        std::string p(pattern);
        size_t start = 0;
        while (start <= p.size())
        {
            size_t end = p.find(';', start);
            if (end == std::string::npos)
                end = p.size();
            std::string item = p.substr(start, end - start);
            if (item.rfind("*.", 0) == 0)
            {
                std::string e = item.substr(2);
                if (e == "*")
                {
                    exts.clear();
                    return exts;
                }
                if (!e.empty())
                    exts.push_back(e);
            }
            start = end + 1;
        }
        return exts;
    }

    NSArray<UTType*>* TypesFromExtensions(const std::vector<std::string>& exts)
    {
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (const auto& e : exts)
        {
            UTType* t = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:e.c_str()]];
            if (t)
                [types addObject:t];
        }
        return types;
    }

    const void* kDelegateKey = &kDelegateKey;
}

@interface EasyMixEditorDelegate : NSObject <NSWindowDelegate>
{
@public
    void (*onClose)(void*);
    void* user;
}
@end

@implementation EasyMixEditorDelegate
- (BOOL)windowShouldClose:(id)sender
{
    // Closing must not happen inside this callback: let the host tear the editor down on the next loop turn.
    void (*cb)(void*) = onClose;
    void* u = user;
    if (cb)
        dispatch_async(dispatch_get_main_queue(), ^{ cb(u); });
    return NO;
}
@end

namespace plat
{
    bool OpenFileDialog(void*, const char* filter, std::string& outPath)
    {
        @autoreleasepool
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            panel.allowsMultipleSelection = NO;

            NSArray<UTType*>* types = TypesFromExtensions(ExtensionsFromFilter(filter));
            if (types.count > 0)
                panel.allowedContentTypes = types;

            if ([panel runModal] != NSModalResponseOK || panel.URL == nil)
                return false;

            outPath = std::string(panel.URL.path.UTF8String);
            return true;
        }
    }

    bool SaveFileDialog(void*, const char* filter, const char* defExt, std::string& outPath)
    {
        @autoreleasepool
        {
            NSSavePanel* panel = [NSSavePanel savePanel];
            panel.canCreateDirectories = YES;

            std::vector<std::string> exts = ExtensionsFromFilter(filter);
            if (exts.empty() && defExt && *defExt)
                exts.push_back(defExt);

            NSArray<UTType*>* types = TypesFromExtensions(exts);
            if (types.count > 0)
                panel.allowedContentTypes = types;

            if ([panel runModal] != NSModalResponseOK || panel.URL == nil)
                return false;

            outPath = std::string(panel.URL.path.UTF8String);
            return true;
        }
    }

    int ConfirmSave(const char* title, const char* message)
    {
        @autoreleasepool
        {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = [NSString stringWithUTF8String:title ? title : ""];
            alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];
            alert.alertStyle = NSAlertStyleWarning;
            [alert addButtonWithTitle:@"Save"];
            [alert addButtonWithTitle:@"Cancel"];
            [alert addButtonWithTitle:@"Don't Save"];

            const NSModalResponse r = [alert runModal];
            if (r == NSAlertFirstButtonReturn)  return 1;
            if (r == NSAlertThirdButtonReturn)  return 2;
            return 0;
        }
    }

    void ErrorBox(const char* title, const char* message)
    {
        @autoreleasepool
        {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = [NSString stringWithUTF8String:title ? title : ""];
            alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];
            alert.alertStyle = NSAlertStyleCritical;
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
        }
    }

    std::string SupportDir()
    {
        @autoreleasepool
        {
            NSArray<NSURL*>* urls = [[NSFileManager defaultManager]
                URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
            if (urls.count == 0)
                return std::string();

            NSURL* dir = [urls.firstObject URLByAppendingPathComponent:@"EasyMix" isDirectory:YES];
            [[NSFileManager defaultManager] createDirectoryAtURL:dir
                                     withIntermediateDirectories:YES
                                                      attributes:nil
                                                           error:nil];
            return std::string(dir.path.UTF8String);
        }
    }

    void* CreateEditorWindow(const char* title, int width, int height, void (*onClose)(void*), void* user)
    {
        @autoreleasepool
        {
            const NSWindowStyleMask style =
                NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;

            NSWindow* win = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(0, 0, width, height)
                          styleMask:style
                            backing:NSBackingStoreBuffered
                              defer:NO];
            win.title = [NSString stringWithUTF8String:title ? title : "VST3"];
            win.releasedWhenClosed = NO;
            [win center];

            EasyMixEditorDelegate* del = [[EasyMixEditorDelegate alloc] init];
            del->onClose = onClose;
            del->user = user;
            win.delegate = del;
            objc_setAssociatedObject(win, kDelegateKey, del, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

            return (__bridge_retained void*)win;
        }
    }

    void* EditorContentView(void* window)
    {
        NSWindow* win = (__bridge NSWindow*)window;
        return (__bridge void*)win.contentView;
    }

    void ResizeEditorWindow(void* window, int width, int height)
    {
        NSWindow* win = (__bridge NSWindow*)window;
        [win setContentSize:NSMakeSize(width, height)];
    }

    void ShowEditorWindow(void* window)
    {
        NSWindow* win = (__bridge NSWindow*)window;
        [win makeKeyAndOrderFront:nil];
    }

    void DestroyEditorWindow(void* window)
    {
        if (!window)
            return;

        NSWindow* win = (__bridge_transfer NSWindow*)window;
        win.delegate = nil;
        objc_setAssociatedObject(win, kDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [win orderOut:nil];
        [win close];
    }
}
