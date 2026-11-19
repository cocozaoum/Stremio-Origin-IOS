/* The phone's platform part on iOS (pplat_*): what the Mac preview stubs, done for real where UIKit makes it cheap. */
#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import <AVFoundation/AVFoundation.h>
#import <UserNotifications/UserNotifications.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phone/phone.h"

extern void ios_video_rate(float r);
float ph_vid_rect[4];

static UIWindow *uiwin(void) {
  SDL_Window *w = SDL_GL_GetCurrentWindow(); if (!w) return nil;
  SDL_SysWMinfo i; memset(&i, 0, sizeof i); SDL_VERSION(&i.version);
  return SDL_GetWindowWMInfo(w, &i) && i.subsystem == SDL_SYSWM_UIKIT ? i.info.uikit.window : nil; }
/* pixels in a point (the drawable over the window) */
float pplat_density(void) {
  SDL_Window *w = SDL_GL_GetCurrentWindow();
  if (w) { int dw, dh, ww, wh; SDL_GL_GetDrawableSize(w, &dw, &dh); SDL_GetWindowSize(w, &ww, &wh); if (ww > 0 && dw > 0) return (float)dw / ww; }
  return (float)UIScreen.mainScreen.nativeScale; }
void pplat_insets(float *top, float *bottom) {
  UIWindow *w = uiwin(); float d = pplat_density();
  if (w) { UIEdgeInsets s = w.safeAreaInsets; *top = s.top * d; *bottom = s.bottom * d; } else { *top = 0; *bottom = 0; } }

static float kbPts;
__attribute__((constructor)) static void kb_watch(void) {
  NSNotificationCenter *c = NSNotificationCenter.defaultCenter;
  [c addObserverForName:UIKeyboardWillShowNotification object:nil queue:nil usingBlock:^(NSNotification *n) { kbPts = (float)[n.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue].size.height; }];
  [c addObserverForName:UIKeyboardWillHideNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; kbPts = 0; }]; }
float pplat_keyboard(void) { return kbPts * pplat_density(); }

/* playing happens in the app itself (video_ios.m); there is no separate player screen */
int pplat_open_player(const char *url, const char *title, const char *headers, double startSec, const char *key) { (void)url; (void)title; (void)headers; (void)startSec; (void)key; return 0; }

/* other apps: "package" is the URL scheme here */
static NSURL *ext_url(const char *scheme, const char *url) {
  NSString *u = [NSString stringWithUTF8String:url], *s = [NSString stringWithUTF8String:scheme];
  NSString *enc = [u stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet alphanumericCharacterSet]];
  if ([s isEqualToString:@"infuse"]) return [NSURL URLWithString:[NSString stringWithFormat:@"infuse://x-callback-url/play?url=%@", enc]];
  if ([s isEqualToString:@"outplayer"]) return [NSURL URLWithString:[NSString stringWithFormat:@"outplayer://%@", u]];
  return [NSURL URLWithString:[NSString stringWithFormat:@"vlc://%@", u]]; }
static int open_ns(NSURL *u) { if (!u) return 0; dispatch_async(dispatch_get_main_queue(), ^{ [UIApplication.sharedApplication openURL:u options:@{} completionHandler:nil]; }); return 1; }
int pplat_open_external_with(const char *url, const char *title, const char *pkg) { (void)title; return open_ns(ext_url(pkg && *pkg ? pkg : "vlc", url)); }
int pplat_open_external(const char *url, const char *title) { return pplat_open_external_with(url, title, "vlc"); }
int pplat_video_players(char *out, int cap) { out[0] = 0; const char *names[][2] = { { "VLC", "vlc" }, { "Infuse", "infuse" }, { "OutPlayer", "outplayer" } }; size_t n = 0;
  for (int i = 0; i < 3; i++) { NSURL *t = [NSURL URLWithString:[NSString stringWithFormat:@"%s://", names[i][1]]]; if (![UIApplication.sharedApplication canOpenURL:t]) continue; n += snprintf(out + n, cap - n, "%s\t%s\n", names[i][0], names[i][1]); }
  return out[0] != 0; }
int pplat_open_url(const char *url) { return open_ns([NSURL URLWithString:[NSString stringWithUTF8String:url]]); }

void pplat_cast_notify(const char *title, const char *sub, const char *art, int playing, double pos, double dur) { (void)title; (void)sub; (void)art; (void)playing; (void)pos; (void)dur; }
int pplat_cast_take(int *kind, double *val) { (void)kind; (void)val; return 0; }
void pplat_multicast(int on) { (void)on; }
int pplat_progress(char *key, int cap, double *pos, double *dur, int *state) { (void)key; (void)cap; (void)pos; (void)dur; (void)state; return 0; }

float pplat_brightness(float set) { if (set >= 0) UIScreen.mainScreen.brightness = set; return (float)UIScreen.mainScreen.brightness; }
static MPVolumeView *mpv;
static UISlider *vol_slider(void) {
  if (!mpv) { UIWindow *w = uiwin(); if (!w) return nil; mpv = [[MPVolumeView alloc] initWithFrame:CGRectMake(-2000, -2000, 10, 10)]; [w addSubview:mpv]; }
  for (UIView *v in mpv.subviews) if ([v isKindOfClass:UISlider.class]) return (UISlider *)v; return nil; }
float pplat_volume(float set) { if (set >= 0) { UISlider *s = vol_slider(); if (s) s.value = set; } return AVAudioSession.sharedInstance.outputVolume; }
void pplat_haptic_k(int kind, float s) { dispatch_async(dispatch_get_main_queue(), ^{ UIImpactFeedbackStyle st = kind == 2 ? UIImpactFeedbackStyleHeavy : kind == 1 ? UIImpactFeedbackStyleMedium : UIImpactFeedbackStyleLight;
  UIImpactFeedbackGenerator *g = [[UIImpactFeedbackGenerator alloc] initWithStyle:st]; [g impactOccurredWithIntensity:s <= 0 ? 1 : (s > 1 ? 1 : s)]; }); }
void pplat_haptic(void) { pplat_haptic_k(0, 1); }
void pplat_rate(float r) { ios_video_rate(r); }
static int fsOn; void pplat_fullscreen(int on) { fsOn = on; } int pplat_fullscreen_on(void) { return fsOn; }
void video_size_mode(const char *mode) { (void)mode; }
void video_pinch(float k) { (void)k; }
void video_pinch_end(float k, char *out, size_t cap) { snprintf(out, cap, "%s", k > 1.1f ? "fill" : "fit"); }
void video_snap_ask(int w, int h) { (void)w; (void)h; }
int video_snap_take(unsigned char *rgba, int cap, int *w, int *h) { (void)rgba; (void)cap; (void)w; (void)h; return 0; }
void video_drag(float tx, float ty, float k) { (void)tx; (void)ty; (void)k; }
int pplat_on_wifi(void) {
  static SCNetworkReachabilityRef r; if (!r) { struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_len = sizeof a; a.sin_family = AF_INET; r = SCNetworkReachabilityCreateWithAddress(NULL, (struct sockaddr *)&a); }
  SCNetworkReachabilityFlags f = 0; if (r && SCNetworkReachabilityGetFlags(r, &f)) return !(f & kSCNetworkReachabilityFlagsIsWWAN); return 1; }
void pplat_dl_dirs(char *out, size_t cap) {
  NSString *d = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Downloads"];
  [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
  snprintf(out, cap, "phone\t%s\t0\n", d.UTF8String); }
int pplat_media_caps(void) { return 1 | 32 | 128; }
void pplat_pick_folder(void) {}
int pplat_folder_take(char *out, size_t cap) { (void)out; (void)cap; return 0; }
void pplat_app_ended(void) {}
void pplat_tilt(float *x, float *y) { *x = 0; *y = 0; }
void pplat_first_frame(void) {}
void pplat_dl_notify(const char *title, const char *text, int pct) { (void)title; (void)text; (void)pct; }
void pplat_dl_finished(const char *title, const char *text) { (void)title; (void)text; }
void pplat_ask_notifications(void) { [UNUserNotificationCenter.currentNotificationCenter requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound completionHandler:^(BOOL g, NSError *e) { (void)g; (void)e; }]; }
int pplat_take_open_downloads(void) { return 0; }
void pplat_dl_notify2(const char *title, const char *text, int pct, const char *sub, const char *lines, const char *art) { (void)title; (void)text; (void)pct; (void)sub; (void)lines; (void)art; }
void pplat_dl_finished2(const char *t, const char *x, const char *art) { (void)t; (void)x; (void)art; }
int pplat_held(void) { return -1; }
int pplat_atv_open(const char *ip, const char *link) { (void)ip; (void)link; return -1; }
int pplat_atv_pair_start(const char *ip, const char *name) { (void)ip; (void)name; return -1; }
int pplat_atv_pair_finish(const char *code) { (void)code; return -1; }
void pplat_atv_pair_close(void) {}
int pplat_pip(void) { return 0; }
void pplat_pip_sub(const char *t) { (void)t; }
