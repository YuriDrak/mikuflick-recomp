#import <UIKit/UIKit.h>
#include "Platform.h"

NS_ASSUME_NONNULL_BEGIN

// Native DLC page (the title menu's SHOP slot on iOS): SHOW DLC SONGS, the download link, the pack
// list of the IPA behind it and single pack downloads (port/src/dlcdl.c), and importing a whole IPA
// with DLC. Shown inside a UINavigationController by ios_dlc_screen (Platform.m).
@interface MFDlcViewController : UITableViewController
@property(nonatomic, readonly) BOOL finished;      // Done tapped, or wantsImport
@property(nonatomic, readonly) BOOL wantsImport;   // "Import IPA with DLC" tapped
- (instancetype)initWithPage:(IosDlcPage *)page;
@end

NS_ASSUME_NONNULL_END
