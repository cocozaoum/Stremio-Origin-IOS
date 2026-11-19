/* Stremio Origin on iOS: plat.h and mem.h over SDL's UIKit port (modelled on the Mac preview's plat_mac.c). */
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/plat.h"
#include "platform/mem.h"
#include "core/keys.h"
#include "core/applog.h"
#include "ui/gfx.h"

static char assetDir[1024], dataDir[1024];

void plat_pre_init(const char *appId) { (void)appId;
  @autoreleasepool {
    snprintf(assetDir, sizeof assetDir, "%s/assets", [NSBundle mainBundle].resourcePath.UTF8String);
    NSString *base = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    NSString *d = [base stringByAppendingPathComponent:@"StremioOrigin"];
    [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    snprintf(dataDir, sizeof dataDir, "%s", d.UTF8String);
  }
  applog_start(dataDir);
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
}
/* SDL's drawable on iOS is not framebuffer 0: the app's frames go to the one SDL made */
void plat_post_window(SDL_Window *w) {
  SDL_SysWMinfo i; memset(&i, 0, sizeof i); SDL_VERSION(&i.version);
  if (SDL_GetWindowWMInfo(w, &i) && i.subsystem == SDL_SYSWM_UIKIT) gfx_set_screen_fbo(i.info.uikit.framebuffer);
}
const char *plat_asset_dir(void) { return assetDir; }
const char *plat_data_dir(void) { return dataDir; }
void plat_window_size(int *w, int *h, Uint32 *flags) {
  SDL_DisplayMode m; if (SDL_GetCurrentDisplayMode(0, &m) == 0) { *w = m.w; *h = m.h; } else { *w = 390; *h = 844; }
  *flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
}
int plat_map_key(const SDL_KeyboardEvent *e) {
  switch (e->keysym.sym) {
    case SDLK_LEFT: return K_LEFT; case SDLK_RIGHT: return K_RIGHT; case SDLK_UP: return K_UP; case SDLK_DOWN: return K_DOWN;
    case SDLK_RETURN: case SDLK_KP_ENTER: return K_OK;
    case SDLK_ESCAPE: return K_BACK;
    case SDLK_SPACE: return K_PLAYPAUSE;
    default: return K_NONE;
  }
}
const char *plat_device_name(void) {
  static char n[96]; if (!n[0]) { char mdl[48] = "iPhone"; size_t l = sizeof mdl; sysctlbyname("hw.machine", mdl, &l, NULL, 0); snprintf(n, sizeof n, "iOS %s (%s)", UIDevice.currentDevice.systemVersion.UTF8String, mdl); }
  return n; }
int plat_text_input(void) { return 1; }
void plat_thread_background(int nice) { (void)nice; }
int plat_mem_total_mb(void) { return (int)(NSProcessInfo.processInfo.physicalMemory >> 20); }
int plat_lifecycle(const SDL_Event *e) {
  if (e->type == SDL_APP_DIDENTERFOREGROUND) return 1;
  if (e->type == SDL_APP_DIDENTERBACKGROUND) return -1;
  return 0;
}
const char *plat_ui_locale(void) { static char l[32]; NSString *s = NSLocale.preferredLanguages.firstObject; if (!s) return NULL; snprintf(l, sizeof l, "%s", s.UTF8String); for (char *p = l; *p; p++) if (*p == '-') *p = '_'; return l; }
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) { (void)uri; (void)payload; (void)timeoutMs; return NULL; }
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) { (void)uri; (void)payload; (void)cb; (void)ud; return 0; }
unsigned long plat_luna_subscribe_app(const char *uri, const char *payload, PlatLunaCb cb, void *ud) { (void)uri; (void)payload; (void)cb; (void)ud; return 0; }
int plat_luna_send_app(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
void plat_luna_cancel(unsigned long handle) { (void)handle; }
void plat_luna_pump(void) {}
int plat_luna_send(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
int plat_panel_lines(void) { CGSize s = UIScreen.mainScreen.nativeBounds.size; return (int)(s.width < s.height ? s.width : s.height); }
void plat_pointer_visible(int on) { (void)on; }

/* mem.h */
int plat_mem_free_mb(void) { return (int)(os_proc_available_memory() >> 20); }
void plat_mem_self(int *rssMb, int *swapMb) {
  mach_task_basic_info_data_t i; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  *rssMb = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&i, &n) == KERN_SUCCESS ? (int)(i.resident_size >> 20) : -1; *swapMb = -1;
}
