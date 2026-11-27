#import "ImportViewController.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <fcntl.h>
#include <unistd.h>

// Declared in port/src/mf.h (avoiding SDL_main macro collisions in Objective-C).
typedef void (*IpaProgress)(void *ud, double frac, const char *file);
extern bool ipa_import(int fd, const char *dir, IpaProgress cb, void *ud, char *err, size_t errlen);

typedef NS_ENUM(NSInteger, MFImportPhase) {
    MFImportPhasePrompt,
    MFImportPhaseImporting,
    MFImportPhaseFailed
};

static UIColor *MikuTeal(void) {
    return [UIColor colorWithRed:0.224 green:0.773 blue:0.733 alpha:1.0]; // #39C5BB
}

static UIColor *MikuDeepCyan(void) {
    return [UIColor colorWithRed:0.106 green:0.710 blue:0.690 alpha:1.0]; // #1BB5B0
}

static void mf_ipa_progress_cb(void *ud, double frac, const char *file);

@interface MFImportViewController () <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
@property(nonatomic, copy) NSString *filesDir;
@property(nonatomic, readwrite) BOOL succeeded;
@property(nonatomic, readwrite) BOOL declined;
@property(nonatomic) BOOL update;
@property(nonatomic) MFImportPhase phase;
@property(nonatomic) BOOL pickerOpen;

@property(nonatomic, strong) UIImageView *statusIcon;
@property(nonatomic, strong) UILabel *cardTitleLabel;
@property(nonatomic, strong) UILabel *bodyLabel;
@property(nonatomic, strong) UIView *storagePill;
@property(nonatomic, strong) UIStackView *progressBox;
@property(nonatomic, strong) UIProgressView *progressView;
@property(nonatomic, strong) UILabel *percentLabel;
@property(nonatomic, strong) UILabel *fileLabel;
@property(nonatomic, strong) UILabel *keepOpenLabel;
@property(nonatomic, strong) UIButton *actionButton;
@property(nonatomic, strong) UIButton *laterButton;
@end

@implementation MFImportViewController

- (instancetype)initWithFilesDirectory:(NSString *)filesDir update:(BOOL)update {
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _filesDir = [filesDir copy];
        _update = update;
        _phase = MFImportPhasePrompt;
        self.modalPresentationStyle = UIModalPresentationOverFullScreen;
        self.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
    }
    return self;
}

- (UIStatusBarStyle)preferredStatusBarStyle {
    return UIStatusBarStyleLightContent;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = [UIColor systemBackgroundColor];

    // Liquid Glass container so the card and action button share glass geometry.
    UIGlassContainerEffect *containerEffect = [[UIGlassContainerEffect alloc] init];
    containerEffect.spacing = 18.0;
    UIVisualEffectView *glassContainer = [[UIVisualEffectView alloc] initWithEffect:containerEffect];
    glassContainer.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:glassContainer];

    UIStackView *rootStack = [[UIStackView alloc] init];
    rootStack.axis = UILayoutConstraintAxisVertical;
    rootStack.alignment = UIStackViewAlignmentFill;
    rootStack.spacing = 22.0;
    rootStack.translatesAutoresizingMaskIntoConstraints = NO;
    [glassContainer.contentView addSubview:rootStack];

    UILabel *appTitle = [[UILabel alloc] init];
    appTitle.text = @"Miku Flick/02";
    appTitle.textColor = [UIColor whiteColor];
    appTitle.textAlignment = NSTextAlignmentCenter;
    UIFontDescriptor *roundedDesc = [[UIFontDescriptor preferredFontDescriptorWithTextStyle:UIFontTextStyleLargeTitle]
        fontDescriptorWithDesign:UIFontDescriptorSystemDesignRounded];
    roundedDesc = [roundedDesc fontDescriptorWithSymbolicTraits:UIFontDescriptorTraitBold] ?: roundedDesc;
    appTitle.font = [UIFont fontWithDescriptor:roundedDesc size:34.0];

    // Main Liquid Glass Card
    UIGlassEffect *cardGlass = [UIGlassEffect effectWithStyle:UIGlassEffectStyleRegular];
    UIVisualEffectView *cardView = [[UIVisualEffectView alloc] initWithEffect:cardGlass];
    cardView.translatesAutoresizingMaskIntoConstraints = NO;
    cardView.layer.cornerRadius = 28.0;
    cardView.layer.cornerCurve = kCACornerCurveContinuous;
    cardView.clipsToBounds = YES;

    UIStackView *cardStack = [[UIStackView alloc] init];
    cardStack.axis = UILayoutConstraintAxisVertical;
    cardStack.alignment = UIStackViewAlignmentFill;
    cardStack.spacing = 14.0;
    cardStack.translatesAutoresizingMaskIntoConstraints = NO;
    [cardView.contentView addSubview:cardStack];
    [NSLayoutConstraint activateConstraints:@[
        [cardStack.topAnchor constraintEqualToAnchor:cardView.contentView.topAnchor constant:24.0],
        [cardStack.bottomAnchor constraintEqualToAnchor:cardView.contentView.bottomAnchor constant:-24.0],
        [cardStack.leadingAnchor constraintEqualToAnchor:cardView.contentView.leadingAnchor constant:22.0],
        [cardStack.trailingAnchor constraintEqualToAnchor:cardView.contentView.trailingAnchor constant:-22.0],
    ]];

    UIStackView *cardHeader = [[UIStackView alloc] init];
    cardHeader.axis = UILayoutConstraintAxisHorizontal;
    cardHeader.alignment = UIStackViewAlignmentCenter;
    cardHeader.spacing = 10.0;

    _statusIcon = [[UIImageView alloc] init];
    _statusIcon.contentMode = UIViewContentModeScaleAspectFit;
    [_statusIcon setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];

    _cardTitleLabel = [[UILabel alloc] init];
    _cardTitleLabel.font = [UIFont systemFontOfSize:20.0 weight:UIFontWeightSemibold];
    _cardTitleLabel.textColor = [UIColor whiteColor];
    _cardTitleLabel.numberOfLines = 0;

    [cardHeader addArrangedSubview:_statusIcon];
    [cardHeader addArrangedSubview:_cardTitleLabel];

    _bodyLabel = [[UILabel alloc] init];
    _bodyLabel.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightRegular];
    _bodyLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.84];
    _bodyLabel.numberOfLines = 0;

    // Subtle local-storage badge inside the card
    UIView *pill = [[UIView alloc] init];
    pill.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.08];
    pill.layer.cornerRadius = 12.0;
    pill.layer.cornerCurve = kCACornerCurveContinuous;
    UIStackView *pillStack = [[UIStackView alloc] init];
    pillStack.axis = UILayoutConstraintAxisHorizontal;
    pillStack.alignment = UIStackViewAlignmentCenter;
    pillStack.spacing = 8.0;
    pillStack.translatesAutoresizingMaskIntoConstraints = NO;
    UIImageSymbolConfiguration *smallSym = [UIImageSymbolConfiguration configurationWithPointSize:13.0 weight:UIImageSymbolWeightMedium];
    UIImageView *driveIcon = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"externaldrive.fill" withConfiguration:smallSym]];
    driveIcon.tintColor = MikuTeal();
    [driveIcon setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    UILabel *pillLabel = [[UILabel alloc] init];
    pillLabel.text = @"Allow ~2 GB free space · Excluded from iCloud Backup";
    pillLabel.font = [UIFont systemFontOfSize:12.5 weight:UIFontWeightMedium];
    pillLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.75];
    pillLabel.numberOfLines = 0;
    [pillStack addArrangedSubview:driveIcon];
    [pillStack addArrangedSubview:pillLabel];
    [pill addSubview:pillStack];
    [NSLayoutConstraint activateConstraints:@[
        [pillStack.topAnchor constraintEqualToAnchor:pill.topAnchor constant:10.0],
        [pillStack.bottomAnchor constraintEqualToAnchor:pill.bottomAnchor constant:-10.0],
        [pillStack.leadingAnchor constraintEqualToAnchor:pill.leadingAnchor constant:12.0],
        [pillStack.trailingAnchor constraintEqualToAnchor:pill.trailingAnchor constant:-12.0],
    ]];
    _storagePill = pill;

    // Progress stack (shown while importing)
    _progressBox = [[UIStackView alloc] init];
    _progressBox.axis = UILayoutConstraintAxisVertical;
    _progressBox.spacing = 10.0;

    _progressView = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    _progressView.progressTintColor = MikuTeal();
    _progressView.trackTintColor = [[UIColor whiteColor] colorWithAlphaComponent:0.16];
    _progressView.layer.cornerRadius = 4.0;
    _progressView.clipsToBounds = YES;
    [_progressView.heightAnchor constraintEqualToConstant:8.0].active = YES;

    UIStackView *metaRow = [[UIStackView alloc] init];
    metaRow.axis = UILayoutConstraintAxisHorizontal;
    metaRow.alignment = UIStackViewAlignmentCenter;
    metaRow.spacing = 8.0;

    _fileLabel = [[UILabel alloc] init];
    _fileLabel.font = [UIFont monospacedSystemFontOfSize:12.5 weight:UIFontWeightRegular];
    _fileLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.72];
    _fileLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;

    _percentLabel = [[UILabel alloc] init];
    _percentLabel.font = [UIFont monospacedDigitSystemFontOfSize:15.0 weight:UIFontWeightSemibold];
    _percentLabel.textColor = MikuTeal();
    _percentLabel.textAlignment = NSTextAlignmentRight;
    [_percentLabel setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [_percentLabel setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];

    [metaRow addArrangedSubview:_fileLabel];
    [metaRow addArrangedSubview:_percentLabel];

    _keepOpenLabel = [[UILabel alloc] init];
    _keepOpenLabel.text = @"Keep the app open while files are verified and extracted.";
    _keepOpenLabel.font = [UIFont systemFontOfSize:13.0 weight:UIFontWeightRegular];
    _keepOpenLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.68];
    _keepOpenLabel.numberOfLines = 0;

    [_progressBox addArrangedSubview:_progressView];
    [_progressBox addArrangedSubview:metaRow];
    [_progressBox addArrangedSubview:_keepOpenLabel];

    [cardStack addArrangedSubview:cardHeader];
    [cardStack addArrangedSubview:_bodyLabel];
    [cardStack addArrangedSubview:_storagePill];
    [cardStack addArrangedSubview:_progressBox];

    // Native Liquid Glass Prominent Action Button
    _actionButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _actionButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_actionButton.heightAnchor constraintEqualToConstant:56.0].active = YES;
    [_actionButton addTarget:self action:@selector(importTapped) forControlEvents:UIControlEventTouchUpInside];

    // update mode only: keep the current data for now
    _laterButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _laterButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_laterButton.heightAnchor constraintEqualToConstant:44.0].active = YES;
    UIButtonConfiguration *laterCfg = [UIButtonConfiguration plainButtonConfiguration];
    laterCfg.title = @"Not Now";
    laterCfg.baseForegroundColor = [UIColor colorWithWhite:1.0 alpha:0.8];
    _laterButton.configuration = laterCfg;
    [_laterButton addTarget:self action:@selector(laterTapped) forControlEvents:UIControlEventTouchUpInside];

    [rootStack addArrangedSubview:appTitle];
    [rootStack addArrangedSubview:cardView];
    [rootStack addArrangedSubview:_actionButton];
    [rootStack addArrangedSubview:_laterButton];
    [rootStack setCustomSpacing:26.0 afterView:appTitle];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [glassContainer.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [glassContainer.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [glassContainer.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [glassContainer.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],

        [rootStack.centerYAnchor constraintEqualToAnchor:glassContainer.contentView.centerYAnchor constant:-12.0],
        [rootStack.leadingAnchor constraintEqualToAnchor:glassContainer.contentView.leadingAnchor constant:24.0],
        [rootStack.trailingAnchor constraintEqualToAnchor:glassContainer.contentView.trailingAnchor constant:-24.0],
        [rootStack.topAnchor constraintGreaterThanOrEqualToAnchor:glassContainer.contentView.topAnchor constant:20.0],
        [rootStack.bottomAnchor constraintLessThanOrEqualToAnchor:glassContainer.contentView.bottomAnchor constant:-20.0],
    ]];

    [self applyPhase:MFImportPhasePrompt message:nil animated:NO];
}

- (void)applyPhase:(MFImportPhase)phase message:(nullable NSString *)message animated:(BOOL)animated {
    self.phase = phase;
    UIImageSymbolConfiguration *iconSym = [UIImageSymbolConfiguration configurationWithPointSize:20.0 weight:UIImageSymbolWeightSemibold];

    void (^update)(void) = ^{
        if (phase == MFImportPhasePrompt) {
            self.statusIcon.image = [UIImage systemImageNamed:@"doc.zipper" withConfiguration:iconSym];
            self.statusIcon.tintColor = MikuTeal();
            self.cardTitleLabel.text = self.update ? @"Game Data Update" : @"Game Data Needed";
            self.bodyLabel.text = self.update
                ? @"This version also reads the help text and the ending credits from your IPA. Import it once more to add them; your scores and options are kept."
                : @"This app contains no SEGA or Crypton game data. Select your own decrypted Miku Flick/02 v1.1.5 IPA from Files to import it locally.";
            self.bodyLabel.hidden = NO;
            self.storagePill.hidden = NO;
            self.progressBox.hidden = YES;
            self.actionButton.hidden = NO;
            self.actionButton.enabled = YES;
            [self configureButtonTitle:@"Import IPA" symbol:@"folder.badge.plus"];
            self.laterButton.hidden = !self.update;
        } else if (phase == MFImportPhaseImporting) {
            self.statusIcon.image = [UIImage systemImageNamed:@"arrow.down.circle.fill" withConfiguration:iconSym];
            self.statusIcon.tintColor = MikuTeal();
            self.cardTitleLabel.text = @"Importing Game Data…";
            self.bodyLabel.hidden = YES;
            self.storagePill.hidden = YES;
            self.progressBox.hidden = NO;
            self.progressView.progress = 0.0f;
            self.percentLabel.text = @"0%";
            self.fileLabel.text = @"Verifying archive…";
            self.actionButton.hidden = YES;
            self.actionButton.enabled = NO;
            self.laterButton.hidden = YES;
        } else {
            self.statusIcon.image = [UIImage systemImageNamed:@"exclamationmark.triangle.fill" withConfiguration:iconSym];
            self.statusIcon.tintColor = [UIColor systemOrangeColor];
            self.cardTitleLabel.text = @"Import Failed";
            self.bodyLabel.text = message.length > 0 ? message : @"Could not import the selected file.";
            self.bodyLabel.hidden = NO;
            self.storagePill.hidden = YES;
            self.progressBox.hidden = YES;
            self.actionButton.hidden = NO;
            self.actionButton.enabled = YES;
            [self configureButtonTitle:@"Try Again" symbol:@"arrow.clockwise"];
            self.laterButton.hidden = !self.update;
        }
        [self.view layoutIfNeeded];
    };

    if (animated) {
        [UIView animateWithDuration:0.25 delay:0 options:UIViewAnimationOptionCurveEaseInOut animations:update completion:nil];
    } else {
        update();
    }
}

- (void)laterTapped {
    if (self.phase == MFImportPhaseImporting) return;
    self.declined = YES;
}

- (void)configureButtonTitle:(NSString *)title symbol:(NSString *)symbolName {
    UIButtonConfiguration *cfg = [UIButtonConfiguration prominentGlassButtonConfiguration];
    cfg.title = title;
    cfg.baseBackgroundColor = MikuDeepCyan();
    cfg.baseForegroundColor = [UIColor whiteColor];
    cfg.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    cfg.image = [UIImage systemImageNamed:symbolName
                       withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:17.0 weight:UIImageSymbolWeightSemibold]];
    cfg.imagePadding = 10.0;
    cfg.titleTextAttributesTransformer = ^NSDictionary<NSAttributedStringKey, id> *(NSDictionary<NSAttributedStringKey, id> *incoming) {
        NSMutableDictionary *out = [incoming mutableCopy];
        out[NSFontAttributeName] = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];
        return out;
    };
    self.actionButton.configuration = cfg;
}

- (void)importTapped {
    if (self.pickerOpen || self.phase == MFImportPhaseImporting) return;
    UIViewController *previous = self.presentedViewController;
    if (previous && !previous.isBeingDismissed) return;

    self.pickerOpen = YES;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate = self;
    picker.presentationController.delegate = self;

    __weak typeof(self) weakSelf = self;
    void (^present)(void) = ^{
        __strong typeof(self) strongSelf = weakSelf;
        if (!strongSelf) return;
        if (strongSelf.presentedViewController) {
            strongSelf.pickerOpen = NO;
            return;
        }
        [strongSelf presentViewController:picker animated:YES completion:nil];
    };

    id<UIViewControllerTransitionCoordinator> transition = previous.transitionCoordinator;
    if (transition) {
        [transition animateAlongsideTransition:nil completion:^(id<UIViewControllerTransitionCoordinatorContext> ctx) {
            present();
        }];
    } else if (previous) {
        dispatch_async(dispatch_get_main_queue(), present);
    } else {
        present();
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    self.pickerOpen = NO;
}

- (void)presentationControllerDidDismiss:(UIPresentationController *)controller {
    self.pickerOpen = NO;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    self.pickerOpen = NO;
    NSURL *copy = urls.firstObject;
    // asCopy:YES supplies a local copy. An open descriptor survives unlink.
    int fd = copy.isFileURL ? open(copy.fileSystemRepresentation, O_RDONLY) : -1;
    if (copy.isFileURL) unlink(copy.fileSystemRepresentation);
    if (fd < 0) {
        [self applyPhase:MFImportPhaseFailed
                 message:@"Could not read the selected file. Try a local copy in Files."
                animated:YES];
        return;
    }

    [self applyPhase:MFImportPhaseImporting message:nil animated:YES];
    NSString *dir = self.filesDir;
    void *ctx = (void *)CFBridgingRetain(self);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char err[320] = "";
        bool ok = ipa_import(fd, dir.fileSystemRepresentation, mf_ipa_progress_cb, ctx, err, sizeof err);
        close(fd);
        MFImportViewController *vc = CFBridgingRelease(ctx);
        NSString *errText = ok ? nil : [NSString stringWithUTF8String:err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) {
                vc.succeeded = YES;
            } else {
                [vc applyPhase:MFImportPhaseFailed message:errText animated:YES];
            }
        });
    });
}

- (void)updateProgress:(double)frac file:(NSString *)file {
    float clamped = (float)(frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac));
    self.progressView.progress = clamped;
    self.percentLabel.text = [NSString stringWithFormat:@"%d%%", (int)(clamped * 100.0f + 0.5f)];
    if (file.length > 0) self.fileLabel.text = file;
}

@end

static void mf_ipa_progress_cb(void *ud, double frac, const char *file) {
    MFImportViewController *vc = (__bridge MFImportViewController *)ud;
    NSString *name = file ? [NSString stringWithUTF8String:file] : @"";
    dispatch_async(dispatch_get_main_queue(), ^{
        [vc updateProgress:frac file:name];
    });
}
