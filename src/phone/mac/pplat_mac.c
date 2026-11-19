/* The phone's platform part on the Mac preview: players that only say what they would play. */
#include <stdio.h>
#include <stdlib.h>
#include "phone/phone.h"
float pplat_density(void) { const char *d = getenv("SB_DENSITY"); return d && atof(d) > 0 ? (float)atof(d) : 2; } /* SB_DENSITY: another device in a window that fits the screen (1.4: a 10-inch tablet in a 560x896 window) */
void pplat_insets(float *top, float *bottom) { *top = 0; *bottom = 0; }
int pplat_open_player(const char *url, const char *title, const char *headers, double startSec, const char *key) {
  fprintf(stderr, "[play] phone player: %s (%s) from %.0f s, key %s%s%s\n", url, title, startSec, key, headers && *headers ? ", headers " : "", headers ? headers : ""); return 1; }
int pplat_open_external_with(const char *url, const char *title, const char *pkg) { fprintf(stderr, "[play] another app (%s): %s (%s)\n", pkg && *pkg ? pkg : "ask", url, title); return 1; }
int pplat_video_players(char *out, int cap) { snprintf(out, cap, "VLC\torg.videolan.vlc\nMX Player\tcom.mxtech.videoplayer.ad\n"); return 1; }
float ph_vid_rect[4]; /* (the preview: no copies of the film) */
void pplat_cast_notify(const char *title, const char *sub, const char *art, int playing, double pos, double dur) { (void)title; (void)sub; (void)art; (void)playing; (void)pos; (void)dur; }
int pplat_cast_take(int *kind, double *val) { (void)kind; (void)val; return 0; }
int pplat_open_external(const char *url, const char *title) { fprintf(stderr, "[play] another app: %s (%s)\n", url, title); return 1; }
void pplat_multicast(int on) { (void)on; }
int pplat_progress(char *key, int cap, double *pos, double *dur, int *state) { (void)key; (void)cap; (void)pos; (void)dur; (void)state; return 0; }
int pplat_open_url(const char *url) { fprintf(stderr, "[open] %s\n", url); return 1; }
float pplat_keyboard(void) { return 0; }
float pplat_brightness(float set) { return set >= 0 ? set : 0.5f; }
float pplat_volume(float set) { return set >= 0 ? set : 0.5f; }
void pplat_haptic(void) {}
void pplat_rate(float r) { (void)r; }
void pplat_fullscreen(int on) { (void)on; } int pplat_fullscreen_on(void) { return 0; }
void video_size_mode(const char *mode) { (void)mode; }
void video_pinch(float k) { (void)k; }
void video_pinch_end(float k, char *out, size_t cap) { snprintf(out, cap, "%s", k > 1.1f ? "fill" : "fit"); }
void video_snap_ask(int w, int h) { (void)w; (void)h; }
int video_snap_take(unsigned char *rgba, int cap, int *w, int *h) { (void)rgba; (void)cap; (void)w; (void)h; return 0; }
void video_drag(float tx, float ty, float k) { (void)tx; (void)ty; (void)k; }
int pplat_on_wifi(void) { return 1; }
void pplat_dl_dirs(char *out, size_t cap) { snprintf(out, cap, "phone\t%s/Movies\t0\n", getenv("HOME") ? getenv("HOME") : "/tmp"); }
int pplat_media_caps(void) { return 1 | 32 | 128; }
static int macPick; void pplat_pick_folder(void) { macPick = 1; }
int pplat_folder_take(char *out, size_t cap) { if (!macPick) return 0; macPick = 0; snprintf(out, cap, "%s/Movies/Stremio Origin", getenv("HOME") ? getenv("HOME") : "/tmp"); return 1; }
void pplat_haptic_k(int kind, float s) { (void)kind; (void)s; }
void pplat_app_ended(void) {}
void pplat_tilt(float *x, float *y) { *x = 0; *y = 0; }
void pplat_first_frame(void) {}
void pplat_dl_notify(const char *title, const char *text, int pct) { (void)title; (void)text; (void)pct; }
void pplat_dl_finished(const char *title, const char *text) { (void)title; (void)text; }
void pplat_ask_notifications(void) {}
int pplat_take_open_downloads(void) { return 0; }
void pplat_dl_notify2(const char *title, const char *text, int pct, const char *sub, const char *lines, const char *art) { (void)title; (void)text; (void)pct; (void)sub; (void)lines; (void)art; }
void pplat_dl_finished2(const char *t, const char *x, const char *art) { (void)t; (void)x; (void)art; }

int video_has_picture(void) { return 1; } /* (the Mac preview: always taken as having one) */
int pplat_held(void) { return -1; }
int pplat_atv_open(const char *ip, const char *link) { (void)ip; (void)link; return -1; }
int pplat_atv_pair_start(const char *ip, const char *name) { (void)ip; (void)name; return -1; }
int pplat_atv_pair_finish(const char *code) { (void)code; return -1; }
void pplat_atv_pair_close(void) {}

/* picture in picture: none on the Mac */
int pplat_pip(void) { return 0; }
void pplat_pip_sub(const char *t) { (void)t; }
