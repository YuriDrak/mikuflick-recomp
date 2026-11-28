#import "DlcViewController.h"
#include "dlcdl.h"
#include <string.h>

// Sections: songs toggle, download (link field, Restore Default Link while it differs, import, status), packs.
typedef NS_ENUM(NSInteger, MFDlcSection) {
    MFDlcSectionSongs,
    MFDlcSectionDownload,
    MFDlcSectionPacks,
};

typedef NS_ENUM(NSInteger, MFDlcRow) {
    MFDlcRowLink,
    MFDlcRowRestore,   // only while the link is not DLC_DEFAULT_URL
    MFDlcRowImport,
    MFDlcRowStatus,
};

static UIColor *MikuTeal(void) {
    return [UIColor colorWithRed:0.224 green:0.773 blue:0.733 alpha:1.0]; // #39C5BB
}

static NSString *Str(const char *s) {
    NSString *r = s ? [NSString stringWithUTF8String:s] : nil;
    return r ?: @"";
}

static BOOL Busy(const DlcDlStatus *st) {
    return st->state == DLD_LISTING || st->state == DLD_INSTALLING;
}

@interface MFDlcViewController () <UITextFieldDelegate> {
    DlcDlStatus _st;   // last polled downloader status
}
@property(nonatomic, assign) IosDlcPage *page;
@property(nonatomic, readwrite) BOOL finished;
@property(nonatomic, readwrite) BOOL wantsImport;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) UISwitch *showSwitch;
@property(nonatomic, strong) UITextField *linkField;
@property(nonatomic) int shownPacks;
@property(nonatomic) int shownSongs;
@end

@implementation MFDlcViewController

- (instancetype)initWithPage:(IosDlcPage *)page {
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) {
        _page = page;
        dlcdl_status(&_st);
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"DLC Packs";
    self.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeAlways;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                                                           target:self
                                                                                           action:@selector(doneTapped)];
    self.view.tintColor = MikuTeal();
    [self.tableView registerClass:UITableViewCell.class forCellReuseIdentifier:@"cell"];
    [self.tableView registerClass:UITableViewCell.class forCellReuseIdentifier:@"link"];

    _showSwitch = [[UISwitch alloc] init];
    _showSwitch.onTintColor = MikuTeal();
    [_showSwitch addTarget:self action:@selector(showChanged:) forControlEvents:UIControlEventValueChanged];

    self.shownPacks = self.page->installed_packs;
    self.shownSongs = self.page->installed_songs;
    if (_st.state == DLD_IDLE && self.page->url[0]) dlcdl_list(self.page->url);   // pack list of the saved link
    dlcdl_status(&_st);
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    self.navigationController.view.tintColor = MikuTeal();   // Done button (the bar is outside self.view)
    if (!self.timer) {
        __weak typeof(self) weakSelf = self;
        self.timer = [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *t) {
            [weakSelf refresh];
        }];
    }
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    [self.timer invalidate];
    self.timer = nil;
}

// ---------------------------------------------------------------- state

- (void)refresh {
    DlcDlStatus st;
    dlcdl_status(&st);
    BOOL counts = self.page->installed_packs != self.shownPacks || self.page->installed_songs != self.shownSongs;
    BOOL structural = counts || st.state != _st.state || st.npacks != _st.npacks || st.busy_pack != _st.busy_pack ||
                      st.error != _st.error || strcmp(st.msg, _st.msg) != 0;
    BOOL progress = st.frac != _st.frac;
    BOOL listChanged = st.npacks != _st.npacks;
    _st = st;
    self.shownPacks = self.page->installed_packs;
    self.shownSongs = self.page->installed_songs;
    if (listChanged) {
        [self.tableView reloadData];
        return;
    }
    if (!structural && !(progress && st.state == DLD_INSTALLING)) return;
    /* update the rows in place: reloadData drops the cached row heights and moves the scroll position */
    [UIView performWithoutAnimation:^{
        if (counts) [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:MFDlcSectionSongs]
                                  withRowAnimation:UITableViewRowAnimationNone];   // its footer has the counts
        NSMutableArray<NSIndexPath *> *rows = [NSMutableArray array];
        for (NSIndexPath *ip in self.tableView.indexPathsForVisibleRows)
            if (ip.section != MFDlcSectionSongs && !(ip.section == MFDlcSectionDownload && [self kindOfRow:ip.row] == MFDlcRowLink))
                [rows addObject:ip];   // the link row keeps its field (and the player's typing)
        [self.tableView reconfigureRowsAtIndexPaths:rows];
    }];
}

// Download section rows: Restore Default Link only while the link differs from the built-in one
- (BOOL)customLink {
    return strcmp(self.page->url, DLC_DEFAULT_URL) != 0;
}

- (MFDlcRow)kindOfRow:(NSInteger)row {
    if (row == 0) return MFDlcRowLink;
    if (![self customLink]) row++;
    return row == 1 ? MFDlcRowRestore : row == 2 ? MFDlcRowImport : MFDlcRowStatus;
}

- (NSString *)packName:(int)pack {
    for (int i = 0; i < _st.npacks; i++)
        if (_st.packs[i].pack == pack) return Str(_st.packs[i].name);
    return @"pack";
}

// status line (the row is always there, so the table never changes shape under the finger)
- (nullable NSString *)statusText:(BOOL *)isError {
    *isError = NO;
    switch (_st.state) {
    case DLD_LISTING: return @"Reading the pack list…";
    case DLD_INSTALLING:
        return [NSString stringWithFormat:@"Downloading %@ — %d%%", [self packName:_st.busy_pack], (int)(_st.frac * 100)];
    case DLD_FAILED:
        *isError = YES;
        return [NSString stringWithFormat:@"%@ Tap to try again.", Str(_st.msg)];
    case DLD_READY: {
        *isError = _st.error;
        if (_st.msg[0]) return Str(_st.msg);
        int n = 0;
        for (int i = 0; i < _st.npacks; i++) n += _st.packs[i].available;
        return [NSString stringWithFormat:@"The link has %d of the %d packs.", n, _st.npacks];
    }
    default: return @"Enter a download link.";
    }
}

- (int)installedSongsOfPack:(int)pack {
    return self.page->pack_songs ? self.page->pack_songs(self.page->ud, pack) : 0;
}

// ---------------------------------------------------------------- actions

- (void)doneTapped {
    self.finished = YES;
}

- (void)showChanged:(UISwitch *)sw {
    self.page->show_dlc = sw.on;   // saved by the page's tick (main.c)
}

- (void)alertTitle:(NSString *)title message:(NSString *)message {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}

- (BOOL)refuseWhileBusy {
    if (!Busy(&_st)) return NO;
    [self alertTitle:@"Download in Progress" message:@"Wait for the current download to finish, or cancel it first."];
    return YES;
}

// the link row's field: an empty field means the built-in link; anything else must be an http(s) link
- (BOOL)textFieldShouldBeginEditing:(UITextField *)field {
    return ![self refuseWhileBusy];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

- (void)textFieldDidEndEditing:(UITextField *)field {
    NSString *s = [field.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    const char *url = s.length ? s.UTF8String : DLC_DEFAULT_URL;
    if (!url || !strcmp(url, self.page->url) || Busy(&_st)) {   // unchanged, or a download started meanwhile
        field.text = Str(self.page->url);
        return;
    }
    if (strlen(url) >= sizeof self.page->url || !net_url_ok(url)) {
        field.text = Str(self.page->url);
        [self alertTitle:@"Not a Download Link"
                 message:@"Enter the direct http(s) download link of an IPA that includes the DLC packs. The link was not changed."];
        return;
    }
    [self setLink:url];
}

- (void)setLink:(const char *)url {
    snprintf(self.page->url, sizeof self.page->url, "%s", url);   // saved by the page's tick
    self.linkField.text = Str(self.page->url);
    dlcdl_list(self.page->url);
    [self refresh];
    [self.tableView reloadData];   // Restore Default Link comes or goes
}

- (void)restoreDefaultLink {
    if ([self refuseWhileBusy]) return;
    [self.view endEditing:YES];
    [self setLink:DLC_DEFAULT_URL];
}

- (void)packTapped:(int)index {
    if (index < 0 || index >= _st.npacks) return;
    const DlcPackInfo *p = &_st.packs[index];
    NSString *name = Str(p->name);
    int pack = p->pack;
    if (_st.state == DLD_INSTALLING && _st.busy_pack == pack) {
        UIAlertController *a = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Cancel %@?", name]
                                                                   message:@"The files downloaded so far are removed."
                                                            preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"Cancel Download" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x) {
            dlcdl_cancel();
        }]];
        [a addAction:[UIAlertAction actionWithTitle:@"Keep Downloading" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:a animated:YES completion:nil];
        return;
    }
    if (!p->available) return;   // greyed: not in the linked IPA
    if ([self refuseWhileBusy] || _st.state != DLD_READY) return;
    if (dlcdl_install(pack)) {
        [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
        [self refresh];
    }
}

// ---------------------------------------------------------------- table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return _st.npacks > 0 ? 3 : 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if (section == MFDlcSectionSongs) return 1;
    if (section == MFDlcSectionDownload) return [self customLink] ? 4 : 3;
    return _st.npacks;
}

- (nullable NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if (section == MFDlcSectionSongs) return nil;
    if (section == MFDlcSectionDownload) return @"Download";
    return @"Packs";
}

- (nullable NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if (section == MFDlcSectionSongs) {
        if (self.page->installed_songs <= 0) return @"No DLC songs installed yet.";
        return [NSString stringWithFormat:@"%d pack%@, %d songs installed.", self.page->installed_packs,
                                          self.page->installed_packs == 1 ? @"" : @"s", self.page->installed_songs];
    }
    if (section == MFDlcSectionDownload)
        return @"Packs download from this link. It starts as the archive.org copy of the “All DLC Included” IPA; enter the link of another IPA with the DLC packs to use that one.";
    return @"Tap a pack to download it, or tap it again to cancel.";
}

/* the title as one unbreakable word: Japanese text may otherwise wrap between any two characters */
static NSString *NoBreak(NSString *s) {
    NSMutableString *o = [NSMutableString string];
    [s enumerateSubstringsInRange:NSMakeRange(0, s.length) options:NSStringEnumerationByComposedCharacterSequences
                       usingBlock:^(NSString *ch, NSRange r, NSRange er, BOOL *stop) {
        if (o.length) [o appendString:@"⁠"];
        [o appendString:[ch isEqualToString:@" "] ? @" " : ch];
    }];
    return o;
}

- (NSAttributedString *)songLines:(const DlcPackInfo *)p grey:(BOOL)grey {
    UIColor *c1 = grey ? UIColor.tertiaryLabelColor : UIColor.secondaryLabelColor;
    UIColor *c2 = grey ? UIColor.quaternaryLabelColor : UIColor.tertiaryLabelColor;
    NSMutableAttributedString *s = [[NSMutableAttributedString alloc] init];
    UIFont *font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    int n = p->nsongs < DLC_PACK_SONGS ? p->nsongs : DLC_PACK_SONGS;
    for (int k = 0; k < n; k++) {
        const DlcPackSong *song = &p->songs[k];
        NSString *main = Str(song->latin[0] ? song->latin : song->title);
        [s appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"%@%@", k ? @"\n" : @"", main]
                                                                  attributes:@{NSFontAttributeName: font, NSForegroundColorAttributeName: c1}]];
        if (song->latin[0])
            [s appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"  %@", NoBreak(Str(song->title))]
                                                                      attributes:@{NSFontAttributeName: font, NSForegroundColorAttributeName: c2}]];
    }
    return s;
}

// the link row: a caption over an editable field (built once, kept across reloads)
- (UITableViewCell *)linkCell:(UITableView *)tableView at:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"link" forIndexPath:indexPath];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (!self.linkField) {
        UITextField *f = [[UITextField alloc] init];
        f.delegate = self;
        f.keyboardType = UIKeyboardTypeURL;
        f.textContentType = UITextContentTypeURL;
        f.autocapitalizationType = UITextAutocapitalizationTypeNone;
        f.autocorrectionType = UITextAutocorrectionTypeNo;
        f.spellCheckingType = UITextSpellCheckingTypeNo;
        f.returnKeyType = UIReturnKeyGo;
        f.clearButtonMode = UITextFieldViewModeWhileEditing;
        f.placeholder = @"https://…/file.ipa";
        f.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
        f.adjustsFontForContentSizeCategory = YES;
        self.linkField = f;
    }
    if (self.linkField.superview != cell.contentView) {
        UIImageSymbolConfiguration *sym = [UIImageSymbolConfiguration configurationWithPointSize:17.0 weight:UIImageSymbolWeightMedium];
        UIImageView *icon = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"link" withConfiguration:sym]];
        icon.tintColor = MikuTeal();
        icon.contentMode = UIViewContentModeCenter;
        UILabel *caption = [[UILabel alloc] init];
        caption.text = @"Download Link";
        caption.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        caption.textColor = UIColor.secondaryLabelColor;
        UIStackView *texts = [[UIStackView alloc] initWithArrangedSubviews:@[caption, self.linkField]];
        texts.axis = UILayoutConstraintAxisVertical;
        texts.spacing = 2;
        UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[icon, texts]];
        row.axis = UILayoutConstraintAxisHorizontal;
        row.alignment = UIStackViewAlignmentCenter;
        row.spacing = 16;
        row.translatesAutoresizingMaskIntoConstraints = NO;
        for (UIView *v in cell.contentView.subviews) [v removeFromSuperview];
        [cell.contentView addSubview:row];
        UILayoutGuide *g = cell.contentView.layoutMarginsGuide;
        [NSLayoutConstraint activateConstraints:@[
            [icon.widthAnchor constraintEqualToConstant:24],
            [row.leadingAnchor constraintEqualToAnchor:g.leadingAnchor],
            [row.trailingAnchor constraintEqualToAnchor:g.trailingAnchor],
            [row.topAnchor constraintEqualToAnchor:g.topAnchor],
            [row.bottomAnchor constraintEqualToAnchor:g.bottomAnchor],
        ]];
    }
    if (!self.linkField.isEditing) self.linkField.text = Str(self.page->url);   // never under the player's typing
    return cell;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"cell" forIndexPath:indexPath];
    UIView *oldAccessory = cell.accessoryView;   // a pack row keeps its label: a new one would slide in from the top
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    UIListContentConfiguration *c = [UIListContentConfiguration subtitleCellConfiguration];
    c.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    UIImageSymbolConfiguration *sym = [UIImageSymbolConfiguration configurationWithPointSize:17.0 weight:UIImageSymbolWeightMedium];

    if (indexPath.section == MFDlcSectionSongs) {
        c = [UIListContentConfiguration cellConfiguration];
        c.text = @"Show DLC Songs";
        c.image = [UIImage systemImageNamed:@"music.note.list" withConfiguration:sym];
        BOOL have = self.page->installed_songs > 0;
        self.showSwitch.on = self.page->show_dlc;
        self.showSwitch.enabled = have;
        cell.accessoryView = self.showSwitch;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        if (!have) c.textProperties.color = UIColor.secondaryLabelColor;
    } else if (indexPath.section == MFDlcSectionDownload) {
        switch ([self kindOfRow:indexPath.row]) {
        case MFDlcRowLink:
            return [self linkCell:tableView at:indexPath];
        case MFDlcRowRestore:
            c = [UIListContentConfiguration cellConfiguration];
            c.text = @"Restore Default Link";
            c.image = [UIImage systemImageNamed:@"arrow.counterclockwise" withConfiguration:sym];
            c.textProperties.color = Busy(&_st) ? UIColor.secondaryLabelColor : MikuTeal();
            break;
        case MFDlcRowImport:
            c = [UIListContentConfiguration cellConfiguration];
            c.text = @"Import IPA with DLC…";
            c.image = [UIImage systemImageNamed:@"square.and.arrow.down" withConfiguration:sym];
            c.textProperties.color = Busy(&_st) ? UIColor.secondaryLabelColor : MikuTeal();
            break;
        default: {
            BOOL err = NO;
            NSString *text = [self statusText:&err];
            c = [UIListContentConfiguration cellConfiguration];
            c.text = text ?: @"";
            c.textProperties.numberOfLines = 0;
            c.textProperties.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
            c.textProperties.color = err ? UIColor.systemOrangeColor
                                         : (_st.state == DLD_READY ? MikuTeal() : UIColor.labelColor);
            if (_st.state == DLD_LISTING) {
                UIActivityIndicatorView *spin = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
                [spin startAnimating];
                cell.accessoryView = spin;
            } else if (_st.state == DLD_INSTALLING) {
                UIProgressView *bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
                bar.progressTintColor = MikuTeal();
                bar.frame = CGRectMake(0, 0, 90, 4);
                bar.progress = (float)(_st.frac < 0 ? 0 : _st.frac > 1 ? 1 : _st.frac);
                cell.accessoryView = bar;
            }
            if (_st.state != DLD_FAILED) cell.selectionStyle = UITableViewCellSelectionStyleNone;
            break;
        }
        }
    } else {
        const DlcPackInfo *p = &_st.packs[indexPath.row];
        BOOL installed = [self installedSongsOfPack:p->pack] >= p->nsongs;
        BOOL grey = !p->available && !installed;   // not in the linked IPA (or no link yet)
        c.text = Str(p->name);
        if (grey) c.textProperties.color = UIColor.secondaryLabelColor;
        c.secondaryAttributedText = [self songLines:p grey:grey];   // a line per song: English (or romanized), then the Japanese title dimmer
        c.secondaryTextProperties.numberOfLines = 0;
        UILabel *right = [oldAccessory isKindOfClass:UILabel.class] ? (UILabel *)oldAccessory : [[UILabel alloc] init];
        right.textColor = UIColor.labelColor;
        right.font = [UIFont monospacedDigitSystemFontOfSize:15.0 weight:UIFontWeightSemibold];
        if (_st.state == DLD_INSTALLING && _st.busy_pack == p->pack) {
            right.text = [NSString stringWithFormat:@"%d%%", (int)(_st.frac * 100)];
            right.textColor = MikuTeal();
            c.image = [UIImage systemImageNamed:@"stop.circle" withConfiguration:sym];   // tap again: cancel
            c.imageProperties.tintColor = MikuTeal();
        } else if (installed) {
            right.text = @"Installed";
            right.textColor = MikuTeal();
            c.image = [UIImage systemImageNamed:@"checkmark.circle.fill" withConfiguration:sym];
            c.imageProperties.tintColor = MikuTeal();
        } else if (grey) {
            right.text = @"";
            c.image = [UIImage systemImageNamed:@"arrow.down.circle" withConfiguration:sym];
            c.imageProperties.tintColor = UIColor.tertiaryLabelColor;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        } else {
            right.text = [NSString stringWithFormat:@"%llu MB", (unsigned long long)((p->bytes + (1u << 19)) >> 20)];
            right.textColor = UIColor.secondaryLabelColor;
            c.image = [UIImage systemImageNamed:@"arrow.down.circle" withConfiguration:sym];
        }
        /* one width for every state (the widest text), so a changing percentage never re-wraps the song lines */
        right.textAlignment = NSTextAlignmentRight;
        CGFloat rw = ceil([@"Installed" sizeWithAttributes:@{NSFontAttributeName: right.font}].width);
        CGRect fr = right.frame;   // size only: the origin stays where the cell laid it out
        fr.size = CGSizeMake(rw, ceil(right.font.lineHeight));
        right.frame = fr;
        cell.accessoryView = right;
    }
    cell.contentConfiguration = c;
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (indexPath.section == MFDlcSectionSongs && self.page->installed_songs <= 0) {   // the switch is disabled: say why
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"No DLC Songs Yet"
                                                                   message:@"Download a pack below, or import an IPA that includes the DLC packs."
                                                            preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:a animated:YES completion:nil];
        return;
    }
    if (indexPath.section == MFDlcSectionDownload) {
        switch ([self kindOfRow:indexPath.row]) {
        case MFDlcRowLink: [self.linkField becomeFirstResponder]; break;
        case MFDlcRowRestore: [self restoreDefaultLink]; break;
        case MFDlcRowImport:
            if (![self refuseWhileBusy]) {   // the importer replaces the folder packs are written to
                self.wantsImport = YES;
                self.finished = YES;
            }
            break;
        default:
            if (_st.state == DLD_FAILED && self.page->url[0]) { dlcdl_list(self.page->url); [self refresh]; }
            break;
        }
    } else if (indexPath.section == MFDlcSectionPacks) {
        [self packTapped:(int)indexPath.row];
    }
}

@end
