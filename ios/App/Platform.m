#import <UIKit/UIKit.h>
#import "ImportViewController.h"
#import "DlcViewController.h"
#include "Platform.h"
#include <SDL_syswm.h>

/* While a UIKit screen is up, the game loop waits by running the main run loop instead of sleeping:
   SDL_Delay blocks the thread UIKit draws, scrolls and handles touches on, so the screen stutters. */
static void run_ui(double seconds) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
}

bool ios_paths(char *files, size_t files_len, char *saves, size_t saves_len) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *root = [fm URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
                    appropriateForURL:nil create:YES error:nil];
    NSURL *assets = [root URLByAppendingPathComponent:@"Imported" isDirectory:YES];
    NSURL *save = [root URLByAppendingPathComponent:@"Saves" isDirectory:YES];
    if (!root || ![fm createDirectoryAtURL:assets withIntermediateDirectories:YES attributes:nil error:nil] ||
        ![fm createDirectoryAtURL:save withIntermediateDirectories:YES attributes:nil error:nil]) return false;
    if (![assets setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil]) return false;
    return [assets getFileSystemRepresentation:files maxLength:files_len] &&
           [save getFileSystemRepresentation:saves maxLength:saves_len];
}

bool ios_import_screen(SDL_Window *window, const char *files_dir, int mode) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!files_dir || !SDL_GetWindowWMInfo(window, &info)) return false;
    UIViewController *presenter = info.info.uikit.window.rootViewController;
    if (!presenter) return false;

    NSString *dir = [NSString stringWithUTF8String:files_dir];
    MFImportViewController *vc = [[MFImportViewController alloc] initWithFilesDirectory:dir update:mode == IOS_IMPORT_UPDATE];
    vc.addDlc = mode == IOS_IMPORT_DLC;
    [presenter presentViewController:vc animated:NO completion:nil];

    bool ok = false;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) goto done;
        }
        if (vc.succeeded) {
            ok = true;
            break;
        }
        if (vc.declined) break;   // Not Now (update) or Cancel (DLC page)
        run_ui(0.016);
    }

done:
    if (vc.presentingViewController) {
        __block BOOL dismissed = NO;
        [presenter dismissViewControllerAnimated:YES completion:^{ dismissed = YES; }];
        Uint32 deadline = SDL_GetTicks() + 600;
        while (!dismissed && SDL_TICKS_PASSED(deadline, SDL_GetTicks())) {
            SDL_PumpEvents();
            run_ui(0.008);
        }
    }
    return ok;
}

bool ios_dlc_screen(SDL_Window *window, IosDlcPage *page) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!page || !SDL_GetWindowWMInfo(window, &info)) return false;
    UIViewController *presenter = info.info.uikit.window.rootViewController;
    if (!presenter) return false;

    MFDlcViewController *vc = [[MFDlcViewController alloc] initWithPage:page];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:vc];
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    nav.navigationBar.prefersLargeTitles = YES;
    nav.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [presenter presentViewController:nav animated:YES completion:nil];

    for (;;) {
        SDL_Event e;
        bool quit = false;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) quit = true;
        }
        if (page->tick) page->tick(page);   /* song list reload, counts, saving the page's settings */
        if (quit || vc.finished) break;
        run_ui(0.016);
    }
    bool import = vc.wantsImport;
    if (nav.presentingViewController) {
        __block BOOL dismissed = NO;
        [presenter dismissViewControllerAnimated:YES completion:^{ dismissed = YES; }];
        /* the game runs on while the page slides away; only an import, whose screen is presented next,
           has to wait for the end of the animation */
        Uint32 deadline = SDL_GetTicks() + 800;
        while (import && !dismissed && SDL_TICKS_PASSED(deadline, SDL_GetTicks())) {
            SDL_PumpEvents();
            run_ui(0.008);
        }
    }
    return import;
}

/* DLC page downloads (port/src/net.c): bytes [off, off + len) of url with a range request, on the
   caller's (worker) thread. Returns 1 ok, 0 retry, -1 do not retry; final_url gets the URL after
   redirects so net.c can reuse it. */
int ios_http_range(const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *final_url,
                   size_t final_cap, char *err, size_t errlen) {
    @autoreleasepool {
        final_url[0] = 0;
        NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
        if (!u) { snprintf(err, errlen, "Unsupported link."); return -1; }
        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:u cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                       timeoutInterval:60];
        [req setValue:[NSString stringWithFormat:@"bytes=%llu-%llu", (unsigned long long)off,
                                                 (unsigned long long)(off + len - 1)]
            forHTTPHeaderField:@"Range"];
        [req setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
        __block NSData *body = nil;
        __block NSHTTPURLResponse *resp = nil;
        __block NSError *error = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask *task = [NSURLSession.sharedSession dataTaskWithRequest:req
            completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
                body = d;
                resp = [r isKindOfClass:NSHTTPURLResponse.class] ? (NSHTTPURLResponse *)r : nil;
                error = e;
                dispatch_semaphore_signal(done);
            }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (error || !resp) {
            snprintf(err, errlen, "Network error: %s", error ? error.localizedDescription.UTF8String : "no response");
            return 0;
        }
        long status = (long)resp.statusCode;
        if (status == 200) {
            snprintf(err, errlen, "This link does not lead to an IPA file the app can read in parts (the server sent the whole file).");
            return -1;
        }
        if (status != 206) {
            snprintf(err, errlen, "The server answered HTTP %ld.", status);
            return status >= 400 && status < 500 ? -1 : 0;
        }
        NSString *cr = [resp valueForHTTPHeaderField:@"Content-Range"];
        unsigned long long a, b, t;
        if (!cr || sscanf(cr.UTF8String, "bytes %llu-%llu/%llu", &a, &b, &t) != 3 || a != off || b != off + len - 1) {
            snprintf(err, errlen, "The server sent the wrong part of the file.");
            return -1;
        }
        if (body.length != len) { snprintf(err, errlen, "Download interrupted."); return 0; }
        memcpy(dst, body.bytes, len);
        *total = t;
        NSString *fin = resp.URL.absoluteString;
        if (fin && ![fin isEqualToString:u.absoluteString]) snprintf(final_url, final_cap, "%s", fin.UTF8String);
        return 1;
    }
}
