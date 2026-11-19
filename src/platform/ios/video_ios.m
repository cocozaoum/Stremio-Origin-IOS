/* Video on iOS: AVFoundation plays the stream (HLS, MP4, MOV; H.264, HEVC; AAC, AC-3, E-AC-3: no MKV, no DTS), its frames
   come out as pictures the app draws itself under its UI (video_mac_underlay) or in a see-through box (video_mac_hole), the
   way the Mac preview does (platform/mac/video_mac_av.m). Polled once a frame from video_pump on the main thread. */
#define GLES_SILENCE_DEPRECATION 1
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#include "core/video.h"
#include "ui/gfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static AVPlayer *pl; static AVPlayerItem *item; static AVPlayerItemVideoOutput *vout;
static VidState st = VID_IDLE; static int shown, paused, hasMedia, ended, seeking, liveMode; static char err[256];
static GLuint tex; static int tw, th;
static float wx, wy, ww = 1920, wh = 1080; static int crop, cx, cy, cw, ch;
static float vol = 1, volTo = 1, volStep, rate = 1, rad; static double lastPump;
static char tBase[3000]; static double tDur, tOff; static int tOn;      /* video_load_timed: a stream that can't seek: each seek loads it again */
static unsigned char *rowbuf; static size_t rowcap;

static double secs(CMTime t) { return CMTIME_IS_NUMERIC(t) ? CMTimeGetSeconds(t) : 0; }
static void drop(void) { if (pl) [pl pause]; if (item && vout) [item removeOutput:vout]; pl = nil; item = nil; vout = nil; st = VID_IDLE; shown = paused = hasMedia = ended = seeking = 0; err[0] = 0; }
static void start_item(NSURL *url) {
  item = [AVPlayerItem playerItemWithURL:url];
  vout = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:@{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA), (id)kCVPixelBufferOpenGLESCompatibilityKey: @YES }];
  [item addOutput:vout]; pl = [AVPlayer playerWithPlayerItem:item]; pl.volume = vol; [pl play]; if (rate != 1) pl.rate = rate; }
static void open_str(const char *u) {
  NSString *s = [NSString stringWithUTF8String:u]; NSURL *url = s ? [NSURL URLWithString:s] : nil;
  if (!url && s) url = [NSURL URLWithString:[s stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet URLQueryAllowedCharacterSet]]];
  if (!url) { st = VID_ERROR; snprintf(err, sizeof err, "bad address"); return; }
  start_item(url); st = VID_LOADING; fprintf(stderr, "[video] ios: playing %s\n", u); }
static void open_timed(double at) { char u[3200]; snprintf(u, sizeof u, "%s%ct=%d", tBase, strchr(tBase, '?') ? '&' : '?', (int)at); tOff = at; open_str(u); }

int video_init(const char *appId) { (void)appId; NSError *e = nil; [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback error:&e]; [AVAudioSession.sharedInstance setActive:YES error:&e]; return 1; }
void video_set_live(int on) { liveMode = on; }
void video_live_rejoin(void) { if (!item) return; NSValue *v = item.seekableTimeRanges.lastObject; if (v) [pl seekToTime:CMTimeRangeGetEnd(v.CMTimeRangeValue)]; [pl play]; paused = 0; }
void video_load(const char *u) { drop(); tOn = 0; if (!u || !*u) return; open_str(u); }
void video_load_timed(const char *base, double durSec) { drop(); tOn = 1; snprintf(tBase, sizeof tBase, "%s", base); tDur = durSec; open_timed(0); }
void video_unload(void) { drop(); tOn = 0; }
void video_play(void) { if (pl) { [pl play]; if (rate != 1) pl.rate = rate; paused = 0; } }
void video_pause(void) { if (pl) { [pl pause]; paused = 1; st = VID_PAUSED; } }
void video_seek(double s) { if (s < 0) s = 0;
  if (tOn) { int was = paused; drop(); paused = was; open_timed(s); if (was) [pl pause]; seeking = 1; return; }
  if (pl) { seeking = 1; [pl seekToTime:CMTimeMakeWithSeconds(s, 600) completionHandler:^(BOOL f) { (void)f; seeking = 0; }]; } }
double video_time(void) { double c = item ? secs(item.currentTime) : 0; return tOn ? tOff + c : c; }
double video_duration(void) { if (tOn) return tDur; if (!item) return 0; double d = secs(item.duration); return d > 0 && d < 1e7 ? d : 0; }
double video_buffered_end(void) { double e = tOn ? tOff : 0; if (!item) return e; for (NSValue *v in item.loadedTimeRanges) { double x = (tOn ? tOff : 0) + secs(CMTimeRangeGetEnd(v.CMTimeRangeValue)); if (x > e) e = x; } return e; }
VidState video_state(void) { return st; }
const char *video_error(void) { return err; }
int video_live(void) { return shown; }
int video_has_media(void) { return hasMedia; }
int video_settling(void) { return seeking; }
int video_width(void) { return item && shown ? (int)item.presentationSize.width : 0; }
int video_height(void) { return item && shown ? (int)item.presentationSize.height : 0; }
double video_fps(void) { if (!item) return 0; for (AVPlayerItemTrack *t in item.tracks) if ([t.assetTrack.mediaType isEqualToString:AVMediaTypeVideo]) return t.assetTrack.nominalFrameRate; return 0; }
int video_hdr(char *out, int cap) { if (cap > 0) out[0] = 0; return 0; }
void video_set_volume(int pct, int easeMs) { volTo = pct / 100.0f; if (easeMs <= 0) { vol = volTo; volStep = 0; if (pl) pl.volume = vol; } else volStep = (volTo - vol) / (easeMs / 1000.0f); }
void ios_video_rate(float r) { rate = r > 0 ? r : 1; if (pl && !paused && st != VID_IDLE) pl.rate = rate; }

static AVMediaSelectionGroup *group(NSString *kind) { if (!item) return nil; AVAsset *a = item.asset;
  if ([a statusOfValueForKey:@"availableMediaCharacteristicsWithMediaSelectionOptions" error:nil] != AVKeyValueStatusLoaded) return nil;
  return [a mediaSelectionGroupForMediaCharacteristic:kind]; }
int video_audio_tracks(VidTrack *o, int max) {
  AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); int n = 0;
  for (AVMediaSelectionOption *op in g.options) { if (n >= max) break; VidTrack *t = &o[n]; memset(t, 0, sizeof *t);
    NSString *lg = op.extendedLanguageTag ?: op.locale.languageCode ?: @""; snprintf(t->lang, sizeof t->lang, "%s", lg.UTF8String);
    snprintf(t->name, sizeof t->name, "%s", (op.displayName ?: @"").UTF8String); t->idx = n++; t->channels = 2; }
  return n; }
int video_audio_current(void) { AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); if (!g) return 0; AVMediaSelectionOption *s = [item.currentMediaSelection selectedMediaOptionInMediaSelectionGroup:g]; NSUInteger i = s ? [g.options indexOfObject:s] : 0; return i == NSNotFound ? 0 : (int)i; }
void video_select_audio(int idx) { AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); if (g && idx >= 0 && idx < (int)g.options.count) [item selectMediaOption:g.options[idx] inMediaSelectionGroup:g]; }
int video_text_tracks(VidTrack *out, int max) { (void)out; (void)max; return 0; }   /* (the file's own subtitles: not drawn by this backend yet) */
void video_select_text(int idx) { (void)idx; }

void video_set_window(float x, float y, float w, float h) { wx = x; wy = y; ww = w; wh = h; crop = 0; }
int video_set_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) { video_set_window(dx, dy, dw, dh); crop = 1; cx = sx; cy = sy; cw = sw; ch = sh; return 1; }
void video_set_surface(int w, int h) { (void)w; (void)h; }
int video_partial_plane(void) { return 1; }
VidPlaneCaps video_plane_caps(void) { VidPlaneCaps c = { 1, 0, 0, 0 }; return c; }
double video_decoder_busy_left(void) { return 0; }
int video_load_completed(void) { return 1; }
int video_audio_unsupported(void) { return 0; }
int video_edge_colors(uint32_t *out4) { (void)out4; return 0; }
int video_has_picture(void) { return item != nil && shown; }

/* once a frame: the item's state, the volume's ease, and the newest picture into the texture */
void video_pump(void) {
  if (!item) return;
  double now = CACurrentMediaTime(), dt = lastPump > 0 ? now - lastPump : 0; lastPump = now;
  if (volStep != 0) { vol += volStep * (float)dt; if ((volStep > 0 && vol >= volTo) || (volStep < 0 && vol <= volTo)) { vol = volTo; volStep = 0; } if (pl) pl.volume = vol; }
  if (item.status == AVPlayerItemStatusFailed) { if (st != VID_ERROR) { snprintf(err, sizeof err, "%s", (item.error.localizedDescription ?: @"failed").UTF8String); fprintf(stderr, "[video] ios: %s\n", err); } st = VID_ERROR; return; }
  if (item.status == AVPlayerItemStatusReadyToPlay) hasMedia = 1;
  double d = video_duration(); if (d > 0 && video_time() >= d - 0.25 && hasMedia && !paused) { st = VID_ENDED; ended = 1; }
  else if (paused) st = VID_PAUSED;
  else if (pl.timeControlStatus == AVPlayerTimeControlStatusPlaying) st = shown ? VID_PLAYING : VID_LOADING;
  else st = shown ? VID_BUFFERING : VID_LOADING;
  CMTime t = [vout itemTimeForHostTime:now];
  if (![vout hasNewPixelBufferForItemTime:t]) return;
  CVPixelBufferRef pb = [vout copyPixelBufferForItemTime:t itemTimeForDisplay:NULL]; if (!pb) return;
  CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
  int w = (int)CVPixelBufferGetWidth(pb), h = (int)CVPixelBufferGetHeight(pb); size_t stride = CVPixelBufferGetBytesPerRow(pb); const unsigned char *px = CVPixelBufferGetBaseAddress(pb);
  if (stride != (size_t)w * 4) { size_t need = (size_t)w * h * 4; if (rowcap < need) { free(rowbuf); rowbuf = malloc(need); rowcap = rowbuf ? need : 0; }
    if (rowbuf) { for (int y = 0; y < h; y++) memcpy(rowbuf + (size_t)y * w * 4, px + (size_t)y * stride, (size_t)w * 4); px = rowbuf; } else px = NULL; }
  if (px) {
    if (!tex) { tex = gfx_tex_create(NULL, w, h, 4, 1); }
    gfx_flush(); glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px); tw = w; th = h;
    if (!shown) { shown = 1; seeking = tOn ? 0 : seeking; fprintf(stderr, "[video] ios: first picture %dx%d\n", w, h); } }
  CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly); CVPixelBufferRelease(pb);
}

/* the picture where the TV's plane would show it: in its window (a crop: that part of the frame) */
static void frame_uv(float *u0, float *v0, float *u1, float *v1) { *u0 = 0; *v0 = 0; *u1 = 1; *v1 = 1;
  if (crop && tw > 0 && th > 0) { *u0 = (float)cx / tw; *v0 = (float)cy / th; *u1 = (float)(cx + cw) / tw; *v1 = (float)(cy + ch) / th; } }
void video_mac_radius(float r) { rad = r; }
int video_mac_real(void) { return item != nil; }
void video_mac_underlay(void) {
  if (!item || !shown || !tex) return; float u0, v0, u1, v1; frame_uv(&u0, &v0, &u1, &v1);
  gfx_image(tex, wx, wy, ww, wh, rad, 0xffffffffu, u0, v0, u1, v1); }
void video_mac_hole(float x, float y, float w, float h, float r, float a) {
  if (!item || !shown || !tex || ww <= 0 || wh <= 0) return; float u0, v0, u1, v1; frame_uv(&u0, &v0, &u1, &v1);
  float fu = (x - wx) / ww, fv = (y - wy) / wh, gu = (x + w - wx) / ww, gv = (y + h - wy) / wh;
  gfx_image(tex, x, y, w, h, r, ((unsigned)(255 * (a > 1 ? 1 : a)) << 24) | 0xffffffu, u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv, u0 + (u1 - u0) * gu, v0 + (v1 - v0) * gv); }
