#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

@interface NativeWindowDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (strong) NSWindow *window;
@property (strong) WKWebView *webView;
@end

@implementation NativeWindowDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSRect screenRect = [[NSScreen mainScreen] visibleFrame];
    CGFloat width = 1200;
    CGFloat height = 800;
    CGFloat x = screenRect.origin.x + (screenRect.size.width - width) / 2.0;
    CGFloat y = screenRect.origin.y + (screenRect.size.height - height) / 2.0;
    NSRect frame = NSMakeRect(x, y, width, height);

    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    [self.window setTitle:@"System Log Analyzer - Professional Edition"];
    [self.window setDelegate:self];

    WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
    self.webView = [[WKWebView alloc] initWithFrame:[[self.window contentView] bounds] configuration:config];
    [self.webView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [[self.window contentView] addSubview:self.webView];

    NSString *urlString = @"http://127.0.0.1:8765";
    NSArray *args = [[NSProcessInfo processInfo] arguments];
    if (args.count > 1) {
        urlString = args[1];
    }
    NSURL *url = [NSURL URLWithString:urlString];
    NSURLRequest *req = [NSURLRequest requestWithURL:url];
    [self.webView loadRequest:req];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
@end

int main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        NativeWindowDelegate *delegate = [[NativeWindowDelegate alloc] init];
        [app setDelegate:delegate];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
