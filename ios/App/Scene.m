#import <UIKit/UIKit.h>
#include "Platform.h"
#include <SDL_syswm.h>

// SDL2's stock UIKit main predates the scene lifecycle required by iOS 27.
#undef main
static int launchArgc;
static char **launchArgv;
static __weak UIWindow *gameWindow;

@interface MFSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

static __weak MFSceneDelegate *gameScene;

bool ios_attach_window(SDL_Window *window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    UIWindow *placeholder = gameScene.window;
    UIWindowScene *windowScene = placeholder.windowScene;
    if (!windowScene || !SDL_GetWindowWMInfo(window, &info)) return false;
    UIWindow *native = info.info.uikit.window;
    native.windowScene = windowScene;
    native.frame = windowScene.effectiveGeometry.coordinateSpace.bounds;
    gameScene.window = native;
    gameWindow = native;
    if (placeholder != native) placeholder.hidden = YES;
    [native makeKeyAndVisible];
    [native layoutIfNeeded];
    return true;
}

@implementation MFSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    gameScene = self;
    if (gameWindow) {
        self.window = gameWindow;
        self.window.windowScene = windowScene;
    } else {
        self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
        self.window.rootViewController = [UIViewController new];
    }
    [self.window makeKeyAndVisible];
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    static BOOL started;
    if (started) return;
    started = YES;
    // Return control to UIKit before entering SDL's event-pumping loop.
    [self performSelector:@selector(runGame) withObject:nil afterDelay:0];
}

- (void)runGame {
    SDL_SetMainReady();
    // SDL's lifecycle observer turns UIApplication notifications (still posted for a
    // single-scene app) into SDL_APP_* events, so the scene callbacks don't forward them.
    SDL_iPhoneSetEventPump(SDL_TRUE);
    int status = SDL_main(launchArgc, launchArgv);
    SDL_iPhoneSetEventPump(SDL_FALSE);
    // SDL_main only returns after a fatal startup error or an explicit quit (Esc on the
    // title screen). It cannot be restarted, so end the process instead of leaving a
    // frozen frame on screen.
    exit(status);
}
@end

@interface MFAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation MFAppDelegate
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        launchArgc = argc;
        launchArgv = argv;
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(MFAppDelegate.class));
    }
}
