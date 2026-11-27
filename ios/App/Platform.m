#import <UIKit/UIKit.h>
#import "ImportViewController.h"
#include "Platform.h"
#include <SDL_syswm.h>

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

bool ios_import_screen(SDL_Window *window, const char *files_dir, bool update) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!files_dir || !SDL_GetWindowWMInfo(window, &info)) return false;
    UIViewController *presenter = info.info.uikit.window.rootViewController;
    if (!presenter) return false;

    NSString *dir = [NSString stringWithUTF8String:files_dir];
    MFImportViewController *vc = [[MFImportViewController alloc] initWithFilesDirectory:dir update:update];
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
        if (vc.declined) break;   // update mode: Not Now keeps the current data
        SDL_Delay(16);
    }

done:
    if (vc.presentingViewController) {
        __block BOOL dismissed = NO;
        [presenter dismissViewControllerAnimated:YES completion:^{ dismissed = YES; }];
        Uint32 deadline = SDL_GetTicks() + 600;
        while (!dismissed && SDL_TICKS_PASSED(deadline, SDL_GetTicks())) {
            SDL_PumpEvents();
            SDL_Delay(8);
        }
    }
    return ok;
}
