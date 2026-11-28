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
