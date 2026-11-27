#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

// Native iOS 26+ Liquid Glass first-run screen for importing the user's own IPA.
@interface MFImportViewController : UIViewController
@property(nonatomic, readonly) BOOL succeeded;
@property(nonatomic, readonly) BOOL declined;   // update mode: Not Now (keep the current data)
// update: the data exists but an older version imported it (no help text or ending credits)
- (instancetype)initWithFilesDirectory:(NSString *)filesDir update:(BOOL)update;
@end

NS_ASSUME_NONNULL_END
