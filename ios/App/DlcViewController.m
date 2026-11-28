#import "DlcViewController.h"

// Sections: songs toggle, import.
typedef NS_ENUM(NSInteger, MFDlcSection) {
    MFDlcSectionSongs,
    MFDlcSectionImport,
};

static UIColor *MikuTeal(void) {
    return [UIColor colorWithRed:0.224 green:0.773 blue:0.733 alpha:1.0]; // #39C5BB
}

@interface MFDlcViewController ()
@property(nonatomic, assign) IosDlcPage *page;
@property(nonatomic, readwrite) BOOL finished;
@property(nonatomic, readwrite) BOOL wantsImport;
@property(nonatomic, strong) UISwitch *showSwitch;
@end

@implementation MFDlcViewController

- (instancetype)initWithPage:(IosDlcPage *)page {
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) {
        _page = page;
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

    _showSwitch = [[UISwitch alloc] init];
    _showSwitch.onTintColor = MikuTeal();
    [_showSwitch addTarget:self action:@selector(showChanged:) forControlEvents:UIControlEventValueChanged];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    self.navigationController.view.tintColor = MikuTeal();   // Done button (the bar is outside self.view)
}

// ---------------------------------------------------------------- actions

- (void)doneTapped {
    self.finished = YES;
}

- (void)showChanged:(UISwitch *)sw {
    self.page->show_dlc = sw.on;   // saved by the page's tick (main.c)
}

// ---------------------------------------------------------------- table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return 1;
}

- (nullable NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if (section == MFDlcSectionSongs) {
        if (self.page->installed_songs <= 0) return @"No DLC songs installed yet.";
        return [NSString stringWithFormat:@"%d pack%@, %d songs installed.", self.page->installed_packs,
                                          self.page->installed_packs == 1 ? @"" : @"s", self.page->installed_songs];
    }
    return @"Import an IPA that includes the DLC packs, like the “All DLC Included” one, to add its songs.";
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"cell" forIndexPath:indexPath];
    cell.accessoryView = nil;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    UIListContentConfiguration *c = [UIListContentConfiguration cellConfiguration];
    UIImageSymbolConfiguration *sym = [UIImageSymbolConfiguration configurationWithPointSize:17.0 weight:UIImageSymbolWeightMedium];

    if (indexPath.section == MFDlcSectionSongs) {
        c.text = @"Show DLC Songs";
        c.image = [UIImage systemImageNamed:@"music.note.list" withConfiguration:sym];
        BOOL have = self.page->installed_songs > 0;
        self.showSwitch.on = self.page->show_dlc;
        self.showSwitch.enabled = have;
        cell.accessoryView = self.showSwitch;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        if (!have) c.textProperties.color = UIColor.secondaryLabelColor;
    } else {
        c.text = @"Import IPA with DLC…";
        c.image = [UIImage systemImageNamed:@"square.and.arrow.down" withConfiguration:sym];
        c.textProperties.color = MikuTeal();
    }
    cell.contentConfiguration = c;
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (indexPath.section == MFDlcSectionSongs && self.page->installed_songs <= 0) {   // the switch is disabled: say why
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"No DLC Songs Yet"
                                                                   message:@"DLC songs come from an IPA that includes the DLC packs."
                                                            preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:a animated:YES completion:nil];
        return;
    }
    if (indexPath.section == MFDlcSectionImport) {
        self.wantsImport = YES;
        self.finished = YES;
    }
}

@end
