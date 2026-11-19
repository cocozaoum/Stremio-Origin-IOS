/* The player on a phone, drawn over the C base's player (its picture plane, its loading art, its subtitles, its
   prompts) in the app's look, made for a hand:
   - at the top: Back, the title and its episode, the TV and the lock;
   - in the middle: play / pause and a jump back or ahead;
   - at the bottom: a thick bar the finger drags (a bubble tells the time), and a row of pills: Episodes, Next episode,
     Audio & subtitles, Sources, Speed, the picture's size;
   - each pill opens a panel of glass on the right, scrolled by the finger, built from the player's own lists and
     choosing through them (the same choices as on the TV);
   - on the picture: a tap shows or hides all this, a double tap on a side jumps 10 s, a slide across scrubs, a
     slide up or down sets the brightness (left) or the volume (right); locked, only the lock answers.
   The same layer is meant for Android TV later, the remote's keys going to the same calls. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "phone/phone.h"
#include "core/p2p.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/store.h"
#include "ui/text.h"
#include "ui/subfonts.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "screens/player.h"
#include "core/rating.h"
#include "core/parental.h"
#include "core/best.h"
#include "core/video.h"
#include "core/live.h"
#include "ui/spoil.h"
extern int player_waiting_picture(void);
#include <stdint.h>
int player_live(void); int player_live_broadcast(void); int player_live_channel(void);
static int live_now(void) { return player_live_broadcast() || player_live_channel(); } /* a live broadcast, or a channel (between two of its streams too) */

extern void player_toggle_play(void); extern void player_seek_dir(int dir); extern int player_paused(void);
extern double player_time(void); extern double player_dur(void); extern void player_seek_to(double t); extern void player_next_aspect(void);
extern const char *player_aspect(void); extern void player_back(void); extern const char *player_name(void); extern const char *player_logo(void);
extern void player_sub_line(char *out, size_t cap); extern int player_has_eps(void); extern int player_has_next(void); extern void player_next(void);
extern int player_is_trailer(void); extern double player_buffered(void);
extern void player_tray_open(int kind); extern void player_tray_close(void); extern int player_tray_kind(void); extern const char *player_tray_title(void);
extern int player_tray_ntabs(void); extern const char *player_tray_tabname(int i); extern int player_tray_tab(void); extern void player_tray_set_tab(int i); extern void player_tray_back(void);
extern int player_tray_ncols(void); extern const char *player_tray_coltitle(int c); extern const char *player_tray_colvalue(int c); extern int player_tray_count(int c);
extern int player_tray_item(int c, int i, const char **lb, const char **sub, const char **vl, const char **thumb, int *on, int *spin, int *tool); extern void player_tray_pick(int c, int i);
extern int player_src_count(void); extern int player_src_detail(int i, char *info, size_t cap, const char **addon, double *size); extern int player_src_row(int i, const char **name, const char **title, int *on, int *open); extern void player_src_pick(int i);
float pplat_brightness(float set); float pplat_volume(float set); void pplat_rate(float r); void pplat_haptic(void);

/* ---- what can be tapped this frame (the last one registered wins) ---- */
typedef void (*PFn)(int a, int b);
typedef struct { float x, y, w, h; PFn fn; int a, b; } PHit;
static PHit hits[160]; static int nhit; static float chTabY = -1; /* the Channels panel: where its pills are (a sideways drag there moves them) */
static float hcY0 = -1e9f, hcY1 = 1e9f; /* a scrolled list: only its shown part takes taps (a row scrolled under the switch above it never does) */
static void phit(float x, float y, float w, float h, PFn fn, int a, int b) { if (y < hcY0) { h -= hcY0 - y; y = hcY0; } if (y + h > hcY1) h = hcY1 - y; if (h <= 0) return; if (nhit < 160) hits[nhit++] = (PHit){ x, y, w, h, fn, a, b }; }
static int phit_at(float x, float y) { for (int i = nhit - 1; i >= 0; i--) { PHit *p = &hits[i]; if (x >= p->x && y >= p->y && x < p->x + p->w && y < p->y + p->h) { p->fn(p->a, p->b); return 1; } } return 0; }

#include "core/seekprev.h"
static void prev_pic(double t, float cx, float bottom, float a); static void prev_focus(void); static int prev_strip(double t, double from, int showDelta, float a); static Tween seekK; static double seekStartAt; static double stripT = -1, stripFrom; static int stripDelta; /* (seek previews, below; when this seek began: the hint shows a moment, then goes) */
static Tween uiA, lockA, bubA, lvlA, jumpA, panA; static int locked, lvlKind, jumpDir, jumps, panel; static float lvl, scrubT; static double lockShownAt, jumpAt, hideAt;
static SbRect pillR[10], orgR; static int shownKind, srcF = -1, infoRow = -1; static Tween infoA; static double downAt; /* pillR: where each pill was drawn (a panel grows from it); srcF: the addon shown (-1 all); infoRow: the stream whose details are up */
static struct { float x, y, w, h; int i; } holds[64]; static int nholds;
static Scroll panV, panV2, tabV; static float splitX = -1; static Scroll *dragV; /* panV2: the right one of two lists side by side (each scrolls on its own); splitX: where they part */ static float speed = 1; static int subStyle; /* the subtitles' "Sync & style" tab */
static int seekHeld; static Tween seekDim; /* the film paused by a seek (to play on once it lands), the shade over it meanwhile */
extern int player_now(Item *it, char *vid, size_t vcap, const Stream **st);
static int playing_local(void) { Item it; char v[96]; const Stream *st; return player_now(&it, v, sizeof v, &st) && !strncmp(st->url, "file:", 5); } /* a file on the phone: seeks land at once */
static void hold_for_seek(void) { if (playing_local()) return; /* (nothing to wait for: it plays on, no shade) */ if (!player_paused() && player_live()) { video_pause(); seekHeld = 1; } tween_to(&seekDim, 1, 0.15f, EASE_CSS_EASE); }
static float cF, cV, cDX, cTX, cTY, cS = 1, cR; static int cMode, cDir = 1; /* cDir: swiped down (1) or up (-1) */ /* swiped down: how far (0..1, the screen's 0.8), its speed, the finger across; the card's offset, scale, corners; 0 idle, 1 held, 2 home, 3 out, 4 gone */
static float closeShade; /* how far the film has given way to the app's colour (the card's own motion: cF above) */
void video_drag(float tx, float ty, float k);
static Tween waitA, btnA; static int btnDir, btnJumps; static double btnAt; /* the ±10 buttons: how many taps in a row, which way */
static double seekAt, heldT = -1, heldUntil; /* the last seek made here; the time the bar keeps showing until the film is there */ static double lockHold; static int swallowUp; /* the finger that unlocked: its lifting does nothing */ /* the finger on the lock since (0: not) */
static struct { SDL_FingerID id, id2; int two; float x2, y2, d0; } Z; /* a second finger: the pinch that sizes the picture */
static struct { int on, mode; float x0, y0, x, y, lx, ly, v0; double t0, lt; double scrub0; float vel; } G; /* mode: 0 undecided, 1 scrub, 2 brightness, 3 volume, 4 the bar, 5 the panel */

static double openAt; /* when the player opened (its loading's hint waits a moment) */ static double dlHoldAt; /* the finger down on the download button since */
static int sentVol = -1; static float cVol0 = 1, cF0; /* the film's sound as the player is swiped: the volume sent last; where the fade out starts */
static float vBoost = 1; static int lvlDirty; static double lvlAt; static float lvlSet = -1; /* the level set last (brightness or sound), and when */
static void lvl_apply(int force) { if (!lvlDirty) return; if (!force && (app_now() - lvlAt < 0.04 || fabsf(lvl - lvlSet) < 0.004f)) return; lvlAt = app_now(); lvlSet = lvl; lvlDirty = 0; if (lvlKind == 2) pplat_brightness(lvl); else if (lvl <= 1) pplat_volume(lvl); } /* the sound over the phone's top: libVLC's gain, 1 = none, up to 2 (200 %) */
void video_set_volume(int pct, int easeMs);
static Tween pressA; static float pressX = -1, pressY = -1; /* the finger down on a control (the player has its own fingers) */
static float ppress(float x, float y, float w, float h) { return pressX >= x && pressX < x + w && pressY >= y && pressY < y + h ? pressA.v : 0; }
static int seek_step(void) { int s = atoi(store_get("seekStep", "10")); return s > 0 ? s : 10; } /* the settings' step */
static void keep(void) { hideAt = app_now() + 4.5; }
static int ui_on(void) { return uiA.to > 0.5f; }
static void ui_show(int on) { tween_to(&uiA, on ? 1 : 0, on ? 0.2f : 0.3f, EASE_CSS_EASE); if (on) keep(); }
static void fmt(double t, char *o, size_t cap) { int s = (int)(t < 0 ? 0 : t); if (s >= 3600) snprintf(o, cap, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf(o, cap, "%d:%02d", s / 60, s % 60); }

/* ---- the pieces ---- */
/* every surface of the player: the film's own liquid glass (bent as the picture moves behind it) with a tint for its
   words; before the picture runs (no copy of it yet) the plain glass it had */
int ph_liquid_pic(float x, float y, float w, float h, float r, float press);
static void pglass(float x, float y, float w, float h, float r, uint32_t live, uint32_t plain, int plainGlass, float press) {
  if (ph_liquid_pic(x, y, w, h, r, press)) { if (live & 0xff000000u) gfx_rrect(x, y, w, h, r, live); return; }
  if (plainGlass) ph_glass(x, y, w, h, r, plain); else gfx_rrect(x, y, w, h, r, plain); }
static void circle(float cx, float cy, float r, const char *icon, int lit, PFn fn, int a) {
  float pk = ppress(cx - r, cy - r, 2 * r, 2 * r), k = 1 - 0.1f * pk; gfx_scale_push(k, k, cx, cy);
  pglass(cx - r, cy - r, 2 * r, 2 * r, r, lit ? col_alpha(ACC, 0.72f) : BLACK_A(62), lit ? col_alpha(ACC, 0.85f) : BLACK_A(115), 0, pk); gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 1.5f, WHITE_A(lit ? 0 : 40));
  if (pk > 0) gfx_circle(cx, cy, r, WHITE_A((int)(40 * pk))); icon_draw(icon, cx - r * 0.55f, cy - r * 0.55f, r * 1.1f, lit ? ACC_FG : WHITE_A(240)); gfx_scale_pop();
  if (fn) phit(cx - r - 14, cy - r - 14, 2 * r + 28, 2 * r + 28, fn, a, 0);
}
/* a pill's icons: one, or two joined by a slash ("p_cc/p_spk": subtitles and sound in one) */
static float icons_w(const char *icon) { return !icon ? 0 : strchr(icon, '/') ? 42 + 22 + 42 : 42; }
static void icons_draw(const char *icon, float x, float cy, uint32_t c) { const char *sl = strchr(icon, '/'); char a[32]; snprintf(a, sizeof a, "%.*s", (int)(sl ? sl - icon : (long)strlen(icon)), icon);
  icon_draw(a, x, cy - 21, 42, c); if (sl) { text_draw("/", x + 42 + 11, cy - text_line_height(30) / 2.0f, 30, 600, col_alpha(c, 0.6f), 0, 0, ALIGN_CENTER); icon_draw(sl + 1, x + 42 + 22, cy - 21, 42, c); } }
static int pillPulseK = -1; static double pillPulseAt; /* a tray just gone back into its pill: it bounces */
static float pillLK = 1; /* how much of its words a pill shows (the size pill unfolding its name): set for one call */
static float pill(float x, float y, const char *icon, const char *label, int lit, PFn fn, int a) {
  float lk = pillLK; pillLK = 1; float h = 78, tw = label ? text_width(label, 27, 700, 0) : 0, w0 = icons_w(icon) + 58, w = label ? tw + (icon ? icons_w(icon) + 12 : 0) + 56 : w0; if (label && lk < 1) w = w0 + (w - w0) * lk; /* no label: a short pill, the icon alone */ float pk = ppress(x, y, w, h), k = 1 - 0.05f * pk; { float q = (float)((app_now() - pillPulseAt) / 0.5); if (a == pillPulseK && q >= 0 && q < 1) k *= 1 + 0.16f * sinf(q * (float)M_PI * 1.5f) * expf(-q * 3); } gfx_scale_push(k, k, x + w / 2, y + h / 2); /* (a tray just gone back into it: a little bounce) */
  pglass(x, y, w, h, h / 2, lit ? col_alpha(ACC, 0.68f) : BLACK_A(64), lit ? col_alpha(ACC, 0.8f) : BLACK_A(120), 0, pk); gfx_ring(x, y, w, h, h / 2, 1.5f, WHITE_A(lit ? 30 : 40));
  if (pk > 0) gfx_rrect(x, y, w, h, h / 2, WHITE_A((int)(30 * pk)));
  float cx = label ? x + 28 + (1 - lk) * (w0 / 2 - icons_w(icon) / 2 - 28) : x + w / 2 - icons_w(icon) / 2; if (icon) { icons_draw(icon, cx, y + h / 2, lit ? ACC_FG : WHITE_A(235)); cx += icons_w(icon) + 12; }
  if (label && lk > 0.01f) { gfx_clip_push(x, y, w - 20, h); gfx_opacity_push(lk); text_draw(label, cx, y + h / 2 - text_line_height(27) / 2.0f, 27, 700, lit ? ACC_FG : WHITE_A(240), 0, 0, ALIGN_LEFT); gfx_opacity_pop(); gfx_clip_pop(); }
  gfx_scale_pop(); if (fn) phit(x, y - 10, w, h + 20, fn, a, 0); return w;
}

/* ---- the actions ---- */
static void a_keep(int a, int b) { (void)a; (void)b; keep(); }
static void a_back(int a, int b) { (void)a; (void)b; if (infoRow >= 0) { infoRow = -1; tween_to(&infoA, 0, 0.2f, EASE_CSS_EASE); return; } if (panel) { void ph_live_panel_state(int on); ph_live_panel_state(0); player_tray_close(); panel = 0; tween_to(&panA, 0, 0.3f, EASE_WEB); ui_show(1); return; } player_back(); } /* the panel shrinks back into its pill; the controls come back */
/* the cast button: what plays goes to the TV, from here (connected: at once, the phone's player shut; else the TVs, then
   there once one is connected); a download played from the phone: its own link (the TV can't reach the phone's file) */
static void a_tv(int a, int b) { (void)a; (void)b; keep(); Item it; char vid[96]; const Stream *st; if (!player_now(&it, vid, sizeof vid, &st)) { cast_open(); return; }
  Stream s2 = *st; if (!strncmp(s2.url, "file:", 5) || !strncmp(s2.url, "http://127.0.0.1", 16)) { extern const Dl *ph_dl_find(const char *id, const char *vid); const Dl *d = ph_dl_find(it.id, vid); if (!d || !d->url[0]) { cast_open(); return; } snprintf(s2.url, sizeof s2.url, "%s", d->url); s2.p2p = !strncmp(d->url, "p2p://", 6); }
  pplat_haptic_k(HK_TICK, 0.6f); int was = cast_connected(); cast_play(&s2, it.type, it.id, vid, player_time()); if (was) player_back(); }
extern int player_now(Item *it, char *vid, size_t vcap, const Stream **st);
static int dlMode; /* the download button: 0 none (a torrent played: it downloads from Downloads only), 1 all, 2 a torrent download played: its card on a tap, nothing else */
static int dlOpen; static Tween dlOpenT;
static void a_dl(int a, int b) { (void)a; (void)b; if (dlMode == 2) { dlOpen = !dlOpen; pplat_haptic_k(HK_TICK, 0.5f); keep(); return; } Item it; char vid[96]; const Stream *st; if (!player_now(&it, vid, sizeof vid, &st)) return; keep(); /* what plays, kept for later: lock the phone, watch it offline */
  int s0 = ph_dl_state(it.id, vid, NULL); if (s0 == DL_DONE) { toast(T("Already downloaded"), 1600, NULL); return; }
  if (s0 >= 0) { ph_dl_toggle(it.id, vid); pplat_haptic_k(HK_PICK, 0.7f); return; } /* (its button shows it: no words) */ /* again: pause or go on (held: Downloads) */
  char ep[200] = ""; { int se = 0, e = 0; const char *c = strchr(vid, ':'); if (c && sscanf(c, ":%d:%d", &se, &e) == 2) snprintf(ep, sizeof ep, "S%d E%d", se, e); }
  if (ph_dl_add_quiet(&it, vid, ep, st)) pplat_haptic(); } /* (a torrent playing: it keeps what plays from now on, the film as it is) */
static void a_zap(int sd, int b) { (void)b; keep(); live_zap(sd > 0 ? 1 : -1); pplat_haptic_k(HK_TICK, 0.6f); } /* a channel: the next one, or the one before */
static void a_todl(int a, int b) { (void)a; (void)b; player_back(); app_go(SCR_DISC, NULL, 1); }
#define TOPY 74.0f
#define TOPR 48.0f
#define DLX (SB_W - 320)
/* the download button held: it opens into a card with everything about the download (how far, how much, how fast,
   how long left, its peers); a tap on the card folds it back; a tap on the button pauses or goes on */
static void a_dlfold(int a, int b) { (void)a; (void)b; dlOpen = 0; keep(); pplat_haptic_k(HK_TICK, 0.5f); }
static void bytes_s2(double b, char *o, size_t c) { if (b >= 1e9) snprintf(o, c, "%.1f GB", b / 1e9); else snprintf(o, c, "%.0f MB", b / 1e6); }
/* paused: the download sign dimmed with a small amber pause badge (never the film's own pause sign) */
static void dl_paused_badge(float cx, float cy, float r) { float bx = cx + r * 0.62f, by = cy + r * 0.62f, br = 15; gfx_circle(bx, by, br + 2.5f, BLACK_A(170)); gfx_circle(bx, by, br, RGB(0xf5, 0xb4, 0x2a)); gfx_rrect(bx - 6, by - 7, 4, 14, 1.5f, RGB(20, 16, 8)); gfx_rrect(bx + 2, by - 7, 4, 14, 1.5f, RGB(20, 16, 8)); }
static float dcX, dcY, dcW, dcH; /* the open card, for a tap away */
static int dlReading; /* the film plays from this very download (as it comes): no pause for it */
static void a_dlcardpause(int a, int b) { (void)a; (void)b; Item it; char vid[96]; const Stream *st; if (!player_now(&it, vid, sizeof vid, &st)) return; ph_dl_toggle(it.id, vid); pplat_haptic_k(HK_PICK, 0.7f); keep(); }
static void dl_card(const Item *it, const char *vid, int ds, float pr) {
  if (!dlOpen && dlOpenT.v <= 0.001f) return; extern const Dl *ph_dl_find(const char *id, const char *vid); const Dl *d = ph_dl_find(it->id, vid); if (!d) { dlOpen = 0; return; }
  if (dlOpen) keep(); /* open: the controls stay until it is folded */
  float k = dlOpenT.v, W = 430, H = 132, w = 2 * TOPR + (W - 2 * TOPR) * k, h = 2 * TOPR + (H - 2 * TOPR) * k, r = TOPR + (30 - TOPR) * k, x1 = DLX + TOPR, x = x1 - w, y = TOPY - TOPR; dcX = x; dcY = y; dcW = w; dcH = h;
  gfx_shadow(x, y + 10, w, h, r, 30, BLACK_A((int)(120 * k))); pglass(x, y, w, h, r, BLACK_A(90 + (int)(40 * k)), BLACK_A(150 + (int)(40 * k)), 0, 0); gfx_ring(x, y, w, h, r, 1.5f, WHITE_A(44)); phit(x, y, w, h, a_dlfold, 0, 0);
  float ck = clampf((k - 0.45f) / 0.55f, 0, 1); if (ck <= 0.01f) return; gfx_opacity_push(ck); gfx_clip_push(x, y, w, h); float px = x + 24, py = y + 18, cw = w - 48;
  if (!strncmp(d->url, "p2p://", 6) && !dlReading && ds != DL_DONE && ds >= 0) { float br = 30, bx = x + w - 24 - br, by = y + h / 2; int paused = ds == DL_PAUSED; /* a torrent's: its pause here (a tap on the button opened this card) */
    gfx_circle(bx, by, br, WHITE_A(30)); if (paused) icon_draw("play", bx - 16, by - 16, 32, WHITE_A(240)); else { gfx_rrect(bx - 8, by - 10, 5, 20, 2, WHITE_A(240)); gfx_rrect(bx + 3, by - 10, 5, 20, 2, WHITE_A(240)); }
    phit(bx - br - 8, y, 2 * br + 24, h, a_dlcardpause, 0, 0); cw -= 2 * br + 20; }
  extern int player_torrent(void); int ph_dl_peers(const Dl *d, char *out, int cap); int tor = !strncmp(d->url, "p2p://", 6); char pl[160] = ""; int np = 0; if (tor && ph_dl_peers(d, pl, sizeof pl)) np = atoi(pl); /* its own peers (its torrent's, not the film's) */
  const char *st = ds == DL_DONE ? T("Downloaded") : ds == DL_PAUSED ? T("Paused") : ds == DL_RUNNING ? T("Downloading") : ds == DL_FAILED ? T("Stopped") : player_torrent() ? T("After the film") : T("Waiting");
  uint32_t sc = ds == DL_PAUSED || (ds == DL_QUEUED) ? RGB(0xf5, 0xb4, 0x2a) : ds == DL_FAILED ? RGB(0xef, 0x8f, 0x8a) : ACC2 | 0xff000000u;
  char l1[120]; if (np > 0 && ds != DL_DONE) { char n[16]; snprintf(n, sizeof n, "%d", np); snprintf(l1, sizeof l1, "%s  \xc2\xb7  %s", st, T1("{n} peers", "n", n)); } else snprintf(l1, sizeof l1, "%s", st);
  gfx_circle(px + 6, py + text_line_height(F_SMALL) / 2.0f, 6, sc); char pc[16]; snprintf(pc, sizeof pc, "%d%%", (int)(pr * 100 + 0.5f)); float pw = text_draw(pc, px + cw, py, F_SMALL + 1, 800, TXT, 0, 0, ALIGN_END);
  text_draw(l1, px + 20, py, F_SMALL, 700, sc, 0, cw - 20 - pw - 12, ALIGN_START);
  char a[32], b2[32], l2[160]; bytes_s2((double)d->got, a, sizeof a); bytes_s2((double)d->size, b2, sizeof b2); size_t L = d->size > 0 ? (size_t)snprintf(l2, sizeof l2, "%s / %s", a, b2) : (size_t)snprintf(l2, sizeof l2, "%s", a);
  if (ds == DL_RUNNING && d->speed > 0 && d->size > d->got) { char sp[32], eta[32]; if (d->speed >= 1000000) snprintf(sp, sizeof sp, "%.1f MB/s", d->speed / 1e6); else snprintf(sp, sizeof sp, "%.0f KB/s", d->speed / 1e3); long long left = (d->size - d->got) / d->speed;
    if (left >= 3600) snprintf(eta, sizeof eta, "%lld h %02lld", left / 3600, (left % 3600) / 60); else if (left >= 60) snprintf(eta, sizeof eta, "%lld min", left / 60); else snprintf(eta, sizeof eta, "<1 min");
    snprintf(l2 + L, sizeof l2 - L, "  \xc2\xb7  %s  \xc2\xb7  %s", sp, eta); }
  text_draw(l2, px, py + 42, F_TINY + 1, 600, TXT2, 0, cw, ALIGN_START);
  float by = y + h - 26; gfx_rrect(px, by, cw, 6, 3, WHITE_A(40)); static Tween bar; tween_to(&bar, pr, 0.8f, EASE_CSS_EASE); tween_tick(&bar, phDt); gfx_rrect(px, by, cw * clampf(bar.v, 0, 1), 6, 3, sc);
  gfx_clip_pop(); gfx_opacity_pop(); }
static void a_lock(int a, int b) { (void)a; (void)b; locked = 1; lockShownAt = app_now(); ui_show(0); }
static double playTapAt; /* the play button pressed by hand (a seek's wait ends with it) */
static void a_play(int a, int b) { (void)a; (void)b; playTapAt = app_now(); player_toggle_play(); keep(); }
static int can_seek(void);
static void a_jump(int dir, int b) { (void)b; if (!can_seek()) return; hold_for_seek(); player_seek_dir(dir); seekAt = app_now(); keep(); btnJumps = btnDir == dir && app_now() - btnAt < 1.2 ? btnJumps + 1 : 1; btnDir = dir; btnAt = app_now(); tween_set(&btnA, 0); tween_to(&btnA, 1, 0.45f, EASE_OUT); pplat_haptic(); }
static double aspAt = -9; static Tween aspK; /* the size just changed: its name shown a moment in its pill */
static void a_size(int a, int b) { (void)a; (void)b; player_next_aspect(); keep(); aspAt = app_now(); }
static void a_next(int a, int b) { (void)a; (void)b; player_next(); }
static void a_panel(int kind, int b) { (void)b; if (panel == kind) { a_back(0, 0); return; } if (!panel) { orgR = pillR[kind == 3 ? 4 : kind]; srcF = -1; } infoRow = -1; tween_set(&infoA, 0); panel = kind; shownKind = kind; memset(&panV, 0, sizeof panV); memset(&panV2, 0, sizeof panV2); memset(&tabV, 0, sizeof tabV); subStyle = 0;
  if (kind >= 1 && kind <= 4) player_tray_open(kind); else player_tray_close();
  { void ph_live_panel_state(int on); void ph_live_panel_open(Scroll *list); ph_live_panel_state(kind == 9); if (kind == 9) ph_live_panel_open(&panV); }
  tween_to(&panA, 1, 0.36f, EASE_WEB); keep(); }
static const float SPEEDS[6] = { 0.5f, 0.75f, 1, 1.25f, 1.5f, 2 };
static void a_speed(int i, int b) { (void)b; speed = SPEEDS[i]; pplat_rate(speed); }
static void a_tab(int i, int b) { (void)b; if (i < 0) subStyle = 1; else { subStyle = 0; player_tray_set_tab(i); } panV.pos = 0; panV2.pos = 0; }
static void a_item(int c, int i) { player_tray_pick(c, i); keep(); if (player_tray_kind() == 3 || player_tray_kind() == 4) return; if (player_tray_kind() != 5 && panel == 1) {} }
static void a_src(int i, int b) { (void)b; player_src_pick(i); }
static void a_epback(int a, int b) { (void)a; (void)b; player_tray_back(); panV.pos = 0; panV2.pos = 0; }

/* ---- the panel: a column of glass on the right ---- */
static int rowShown = -1; /* a language row looked at (lit, no tick): set for one call */
static int rowVwt; /* the next row's value weight (a font row's: its value in that font), then the usual again */
static float row_item(float x, float y, float w, const char *lb, const char *sub, const char *vl, int on, int spin, PFn fn, int a, int b) {
  int shown = rowShown; rowShown = -1; int vwt = rowVwt ? rowVwt : 700; rowVwt = 0; int lit = shown >= 0 ? shown : on;
  int nl = sub && *sub ? text_wrap_count(sub, w - 130, 2, 26, 500, 0) : 0; float h = 92 + nl * 32; if (y + h < 0 || y > SB_H) return h + 10;
  float pk = ppress(x, y, w, h); gfx_rrect(x, y, w, h, 18, lit ? col_alpha(ACC, 0.26f) : WHITE_A(pk > 0 ? (int)(14 + 20 * pk) : 8));
  float vw = vl && *vl ? text_width(vl, 28, vwt, 0) : 0, room = 150 + vw + (on && vw > 0 ? 56 : 0);
  if (vw > 0) { float lbw = text_width(lb, 34, lit ? 800 : 600, 0); /* (a narrow tray: the name whole, the value shortened, never the name gone to "..."; room kept for a tick or a spinner beside the value) */
    if (lbw > w - room) { float ex = spin ? 56 : on ? 80 : 0, avail = w - 84 - ex; if (lbw + vw > avail) { float vmax = fmaxf(avail - lbw, avail * 0.4f); if (vmax < vw) vw = vmax; } room = 84 + vw + ex; } }
  text_draw(lb, x + 30, y + 24, 34, lit ? 800 : 600, WHITE_A(lit ? 255 : 230), 0, w - room, ALIGN_START);
  if (nl) text_draw_wrapped(sub, x + 30, y + 70, w - 130, 2, 26, 500, WHITE_A(150), 0, ALIGN_START, 32);
  if (vl && *vl) text_draw(vl, x + w - 30, y + h / 2 - text_line_height(28) / 2.0f, 28, vwt, ACC2, 0, vw + 1, ALIGN_END);
  if (spin) spinner(x + w - 52 - (vw > 0 ? vw + 20 : 0), y + h / 2, 16); else if (on) icon_draw("ph_check", x + w - 70 - (vw > 0 ? vw + 16 : 0), y + h / 2 - 24, 48, ACC2); /* (beside its count, never on it) */
  if (fn) phit(x, y, w, h, fn, a, b); return h + 10;
}
/* the subtitles' delay, stepped by taps: earlier on the left, later on the right, the value between */
extern double player_sub_delay(void); extern void player_sub_delay_add(double d);
static Tween dlyA; static double dlyAt; /* the delay card: its showing, the last touch on it */
static void a_back(int a, int b);
static void a_dlyopen(int a, int b) { (void)a; (void)b; a_back(0, 0); dlyAt = app_now(); tween_to(&dlyA, 1, 0.3f, EASE_WEB); ui_show(0); }
static void a_dlyclose(int a, int b) { (void)a; (void)b; dlyAt = 0; tween_to(&dlyA, 0, 0.22f, EASE_CSS_EASE); }
static void a_dlystep(int a, int b) { (void)b; player_sub_delay_add(a / 100.0); dlyAt = app_now(); pplat_haptic(); }
static SbRect dlyR; static float dlyDY; /* the card's place (taps and the swipe start on it), how far the finger has pushed it up */
static void a_dlyreset(int a, int b) { (void)a; (void)b; player_sub_delay_add(0); dlyAt = app_now(); pplat_haptic(); }
static void delay_card(void) {
  if (dlyA.to > 0.5f && app_now() - dlyAt > 6 && G.mode != 9) a_dlyclose(0, 0); /* left alone a while: it goes */
  tween_tick(&dlyA, phDt); float k = dlyA.v; if (k <= 0.01f) { dlyR.w = 0; return; }
  if (G.mode != 9 && dlyDY < 0) { dlyDY *= 0.75f; if (dlyDY > -0.5f) dlyDY = 0; } /* let go without swiping it away: it settles back */
  float w = 1180 < SB_W - 80 ? 1180 : SB_W - 80, h = 150, x = SB_W / 2 - w / 2, y = 26 - (1 - k) * 60 + dlyDY; dlyR = rect(x, y, w, h);
  gfx_opacity_push(k * clampf(1 + dlyDY / 160, 0, 1)); gfx_shadow(x, y + 8, w, h, h / 2, 30, BLACK_A(110)); pglass(x, y, w, h, h / 2, RGBA(14, 12, 20, 120), RGBA(16, 14, 24, 165), 1, 0); phit(x, y, w, h, a_keep, 0, 0);
  double d = player_sub_delay(); char v[32]; if (d == 0) snprintf(v, sizeof v, "0.00 s"); else snprintf(v, sizeof v, "%s%.2f s", d > 0 ? "+" : "\xe2\x88\x92", fabs(d));
  float bw = 112, bh = 80, by = y + (h - bh) / 2, gap = 12; static const int S[4] = { -100, -25, 25, 100 }; static const char *L[4] = { "\xe2\x88\x92" "1 s", "\xe2\x88\x92" "0.25", "+0.25", "+1 s" };
  const char *rl = T("Reset"); float rw = text_width(rl, 26, 700, 0) + 52, right = x + w - 30 - 96 - 16 - rw - 24; /* the reset pill and the close button on the right */
  float bx[4] = { x + 34, x + 34 + bw + gap, right - 2 * bw - gap, right - bw };
  for (int i = 0; i < 4; i++) { float pk = ppress(bx[i], by, bw, bh), s2 = 1 - 0.08f * pk; gfx_scale_push(s2, s2, bx[i] + bw / 2, by + bh / 2); gfx_rrect(bx[i], by, bw, bh, bh / 2, WHITE_A(pk > 0 ? (int)(34 + 50 * pk) : 26)); text_draw(L[i], bx[i] + bw / 2, by + bh / 2 - text_line_height(27) / 2.0f, 27, 800, WHITE_A(240), 0, 0, ALIGN_CENTER); gfx_scale_pop();
    phit(bx[i] - gap / 2, y, bw + gap, h, a_dlystep, S[i], 0); } /* the whole height of the card: an easy target */
  float mx = (bx[1] + bw + bx[2]) / 2; text_draw(v, mx, y + 30, 40, 800, WHITE_A(250), 0, 0, ALIGN_CENTER);
  text_draw(d > 0 ? T("Subtitles later") : d < 0 ? T("Subtitles earlier") : T("Subtitle delay"), mx, y + 86, 24, 600, WHITE_A(160), 0, 0, ALIGN_CENTER);
  { float rx = right + 24, pk = ppress(rx, by, rw, bh); gfx_rrect(rx, by, rw, bh, bh / 2, d != 0 ? WHITE_A(pk > 0 ? (int)(34 + 50 * pk) : 26) : WHITE_A(10)); text_draw(rl, rx + rw / 2, by + bh / 2 - text_line_height(26) / 2.0f, 26, 700, WHITE_A(d != 0 ? 235 : 110), 0, 0, ALIGN_CENTER); if (d != 0) phit(rx, y, rw, h, a_dlyreset, 0, 0); }
  circle(x + w - 30 - 48, y + h / 2, 48, "ph_x", 0, a_dlyclose, 0);
  gfx_rrect(x + w / 2 - 30, y + h - 14, 60, 5, 2.5f, WHITE_A(70)); /* a handle: it swipes up and away */
  gfx_opacity_pop();
}
/* the subtitles' Position: out of the tray into a card at the top, the controls away, the subtitles exactly where they
   will rest (a faint line marks it when no line is on screen); a percent at a time with the arrows, or anywhere on the
   picture with a finger up and down */
extern float player_sub_pos(void); extern void player_sub_pos_set(float pct);
static Tween posA; static double posAt; static SbRect posR; static int posSwiped; static SbRect posX; /* posX: the close button */
static int posMode; static SbRect szR[7]; static const char *ST_SZ[7] = { "0", "1", "2", "3", "4", "5", "6" }; /* posMode 1: the same bar for the size, its seven sizes across it (szR) */
int phone_sub_preview(void) { return posA.to > 0.5f; }
static double styleAt; /* a size, colour or background just chosen: the sample line shows it a moment */
float phone_sub_sample_k(void) { float k = posA.v; double t = app_now() - styleAt; if (styleAt > 0 && t < 3) { float s = t < 2.4 ? 1 : (float)(1 - (t - 2.4) / 0.6); if (s > k) k = s; } return k; }
static void a_posopen(int a, int b) { (void)b; posMode = a; a_back(0, 0); posSwiped = 0; posAt = app_now(); tween_to(&posA, 1, 0.3f, EASE_WEB); ui_show(0); }
static void a_posclose(int a, int b) { (void)a; (void)b; posAt = 0; tween_to(&posA, 0, 0.22f, EASE_CSS_EASE); }
static void a_posstep(int d, int b) { (void)b; float v = roundf(player_sub_pos()) + d; if (v < 2) v = 2; if (v > 30) v = 30; player_sub_pos_set(v); posAt = app_now(); pplat_haptic_k(HK_TICK, 0.6f); }
/* the Position bar: it stays until its ✕ is pressed (never with the controls); a swipe up or down on it moves the
   subtitles a percent at a time, the percent on its left, its ✕ on the right; until the first swipe a finger sliding up and down on it
   shows how, softly */
static void pos_card(void) {
  tween_tick(&posA, phDt); float k = posA.v; if (k <= 0.01f) { posR.w = 0; return; }
  float pct = player_sub_pos(), ly = SB_H - SB_H * pct / 100.0f; int drag = G.mode == 21 && !posMode;
  if (posMode) { /* the size: its seven sizes across the bar, the subtitles below at the size chosen, its ✕ on the right */
    float w = 900 < SB_W - 80 ? 900 : SB_W - 80, h = 132, x = SB_W / 2 - w / 2, y = 26 - (1 - k) * 60; posR = rect(x, y, w, h);
    gfx_opacity_push(k); gfx_shadow(x, y + 8, w, h, h / 2, 30, BLACK_A(110)); pglass(x, y, w, h, h / 2, RGBA(14, 12, 20, 120), RGBA(16, 14, 24, 165), 1, 0);
    float cr = 44, cx = x + w - 22 - cr, cy = y + h / 2; posX = rect(cx - cr - 10, y, 2 * cr + 20, h); circle(cx, cy, cr, "ph_x", 0, NULL, 0);
    const char *cur = store_get("subtitleSize", "2"); float ax = x + 40, aw = cx - cr - 24 - ax, cw = aw / 7;
    for (int i = 0; i < 7; i++) { float sx = ax + cw * i + cw / 2; int on = !strcmp(cur, ST_SZ[i]), px = 24 + i * 7; szR[i] = rect(sx - cw / 2, y, cw, h);
      if (on) gfx_circle(sx, cy, fminf(cw / 2 - 4, 46), col_alpha(ACC, 0.45f));
      text_draw("A", sx, cy - text_line_height(px) / 2.0f, px, 800, on ? WHITE_A(255) : WHITE_A(170), 0, 0, ALIGN_CENTER); }
    gfx_opacity_pop(); return; }
  gfx_opacity_push(k * (drag ? 0.9f : 0.4f)); for (float xx = SB_W * 0.18f; xx < SB_W * 0.82f; xx += 28) gfx_rect(xx, ly, 16, 2, WHITE_A(200)); gfx_opacity_pop(); /* where they rest, dashed */
  float w = 900 < SB_W - 80 ? 900 : SB_W - 80, h = 132, x = SB_W / 2 - w / 2, y = 26 - (1 - k) * 60; posR = rect(x, y, w, h);
  float pk = drag ? 1 : 0; static Tween dk; tween_to(&dk, pk, 0.18f, EASE_CSS_EASE); tween_tick(&dk, phDt);
  gfx_opacity_push(k); gfx_shadow(x, y + 8, w, h, h / 2, 30, BLACK_A(110)); pglass(x, y, w, h, h / 2, RGBA(14, 12, 20, 120 + (int)(30 * dk.v)), RGBA(16, 14, 24, 165), 1, 0);
  if (dk.v > 0.01f) gfx_ring(x, y, w, h, h / 2, 2, col_alpha(ACC2, 0.7f * dk.v));
  float cr = 44, cx = x + w - 22 - cr, cy = y + h / 2; posX = rect(cx - cr - 10, y, 2 * cr + 20, h); circle(cx, cy, cr, "ph_x", 0, NULL, 0); /* the close button on the right */
  char v[16]; snprintf(v, sizeof v, "%.0f%%", pct); text_draw(v, x + 44, cy - text_line_height(44) / 2.0f, 44, 800, WHITE_A(250), 0, 0, ALIGN_START); /* the percent, on the left */
  { float mx = (x + 44 + 120 + cx - cr) / 2; const char *lb = T("Swipe up or down"); float t = (float)fmod(app_now(), 1.8) / 1.8f, sw = sinf(t * 2 * (float)M_PI); /* a finger sliding up and down, and its words */
    float a = posSwiped ? 0.55f : 0.75f + 0.25f * sinf((float)app_now() * 3.2f); float gx2 = mx - text_width(lb, 26, 700, 0) / 2 - 46;
    gfx_rrect(gx2 - 12, cy - 30, 24, 60, 12, WHITE_A((int)(40 * a))); gfx_circle(gx2, cy + 18 * sw, 9, WHITE_A((int)(230 * a)));
    text_draw(lb, mx + 4, cy - text_line_height(26) / 2.0f, 26, 700, WHITE_A((int)(225 * a)), 0, 0, ALIGN_CENTER); }
  gfx_opacity_pop(); }
/* the subtitles' look, chosen by seeing it: sizes as letters of those sizes, colours as their colour, backgrounds as a
   word on each; the one in use ringed */
static const char *ST_COL[3] = { "#ffffff", "#ffe600", "#00e5ff" }, *ST_BG[3] = { "0", "1", ".6" }, *ST_BR[4] = { "100", "80", "60", "40" };
void player_tray_refresh_subs(void);
static void a_style(int kind, int i) { if (kind == 0) store_set("subtitleSize", ST_SZ[i]); else if (kind == 1) store_set("subsColor", ST_COL[i]); else if (kind == 3) store_set("subsBright", ST_BR[i]); else store_set("subsBgStyle", ST_BG[i]); styleAt = app_now(); pplat_haptic_k(HK_PICK, 0.8f); keep(); player_tray_refresh_subs(); } /* (a size, a colour, a background chosen: felt, clearly; the tray's values told again) */
static float style_row(float x, float y, float w, const char *key, const char *lb) { /* its name on the left, its choices on the right: one line each */
  int kind = !strcmp(key, "size") ? 0 : !strcmp(key, "color") ? 1 : !strcmp(key, "bright") ? 3 : 2, n = kind == 3 ? 4 : kind ? 3 : 7; float h = 96; if (y + h < 0 || y > SB_H) return h + 8;
  const char *cur = kind == 0 ? store_get("subtitleSize", "2") : kind == 1 ? store_get("subsColor", "#ffffff") : kind == 3 ? store_get("subsBright", "100") : store_get("subsBgStyle", "0");
  uint32_t tc = 0xff000000u | (uint32_t)strtoul(store_get("subsColor", "#ffffff") + 1, NULL, 16);
  gfx_rrect(x, y, w, h, 18, WHITE_A(8)); float lw = w * 0.36f; { float fp = ph_fit_px(lb, 28, 600, 0, lw - 34); text_draw(lb, x + 26, y + h / 2 - text_line_height((int)fp) / 2.0f, (int)fp, 600, WHITE_A(230), 0, lw - 30, ALIGN_START); } /* (a long name in some languages: a little smaller, never cut) */
  float ax = x + lw, aw = w - lw - 10, cw = aw / n, cy = y + h / 2;
  for (int i = 0; i < n; i++) { float cx = ax + cw * i + cw / 2; const char *v = kind == 0 ? ST_SZ[i] : kind == 1 ? ST_COL[i] : kind == 3 ? ST_BR[i] : ST_BG[i]; int on = kind == 1 ? !strcasecmp(cur, v) : !strcmp(cur, v); float pk = ppress(cx - cw / 2, y, cw, h);
    if (kind == 0) { int px = 16 + i * 4; float rr = fminf(cw / 2 - 2, 28); if (on) gfx_circle(cx, cy, rr, col_alpha(ACC, 0.4f)); text_draw("A", cx, cy - text_line_height(px) / 2.0f, px, 800, on ? WHITE_A(255) : WHITE_A(pk > 0 ? 230 : 160), 0, 0, ALIGN_CENTER); }
    else if (kind == 1) { gfx_circle(cx, cy, 20, 0xff000000u | (uint32_t)strtoul(v + 1, NULL, 16)); gfx_ring(cx - 20, cy - 20, 40, 40, 20, 1.5f, BLACK_A(120)); if (on) gfx_ring(cx - 28, cy - 28, 56, 56, 28, 3, WHITE_A(235)); }
    else if (kind == 3) { float bw2 = fminf(cw - 14, 110), bh2 = 50, bx = cx - bw2 / 2, by = cy - bh2 / 2, bk = atoi(v) / 100.0f; uint32_t dc = col_mix(0xff000000u, tc, bk); /* the brightness: the word over the picture at that brightness */
      gfx_rrect(bx, by, bw2, bh2, 12, RGB(0x4a, 0x5a, 0x6e));
      for (int d = 0; d < 4; d++) { static const float ox[4] = { -1.5f, 1.5f, 0, 0 }, oy[4] = { 0, 0, -1.5f, 1.5f }; text_draw("Aa", cx + ox[d], cy - text_line_height(24) / 2.0f + oy[d], 24, 700, BLACK_A(255), 0, 0, ALIGN_CENTER); }
      text_draw("Aa", cx, cy - text_line_height(24) / 2.0f, 24, 700, dc, 0, 0, ALIGN_CENTER); if (on) gfx_ring(bx - 5, by - 5, bw2 + 10, bh2 + 10, 16, 3, WHITE_A(235)); }
    else { float bw2 = fminf(cw - 16, 110), bh2 = 50, bx = cx - bw2 / 2, by = cy - bh2 / 2; float bo = (float)atof(v); /* the word over the picture as it will look */
      gfx_rrect(bx, by, bw2, bh2, 12, RGB(0x4a, 0x5a, 0x6e)); if (bo > 0) gfx_rrect(bx + 10, by + 8, bw2 - 20, bh2 - 16, 6, BLACK_A((int)(bo * 255)));
      for (int d = 0; d < 4 && bo == 0; d++) { static const float ox[4] = { -1.5f, 1.5f, 0, 0 }, oy[4] = { 0, 0, -1.5f, 1.5f }; text_draw("Aa", cx + ox[d], cy - text_line_height(24) / 2.0f + oy[d], 24, 700, BLACK_A(255), 0, 0, ALIGN_CENTER); }
      text_draw("Aa", cx, cy - text_line_height(24) / 2.0f, 24, 700, tc, 0, 0, ALIGN_CENTER); if (on) gfx_ring(bx - 5, by - 5, bw2 + 10, bh2 + 10, 16, 3, WHITE_A(235)); }
    phit(cx - cw / 2, y, cw, h, a_style, kind, i); }
  return h + 8; }
/* a style row the subtitles shown can't take: greyed, not pressable (pictures take none; styled lines keep their colours and boxes) */
static int sty_off(const char *ky) { extern int player_sub_scope(void); int sc = player_sub_scope(); int look = !strcmp(ky, "color") || !strcmp(ky, "bg") || !strcmp(ky, "font") || !strcmp(ky, "fontar"), any = look || !strcmp(ky, "pos") || !strcmp(ky, "size") || !strcmp(ky, "bright");
  return (sc == 2 && any) || (sc == 1 && look); }
/* the subtitle fonts: the row opens its list under it, each name in its own font (an Arabic one with a word in it) */
static int fontOpen; /* 1 the Latin list open, 2 the Arabic one */
static void a_fontopen(int kind, int b) { (void)b; fontOpen = fontOpen == kind ? 0 : kind; pplat_haptic_k(HK_TICK, 0.6f); keep(); }
static void a_font(int kind, int i) { char v[8]; snprintf(v, sizeof v, "%d", i); store_set(kind == 2 ? "subsFontAr" : "subsFont", v); styleAt = app_now(); pplat_haptic_k(HK_PICK, 0.8f); keep(); player_tray_refresh_subs(); }
static float font_list(float x, float y, float w, int kind) { int cur = kind == 2 ? subfont_ar() : subfont_lat(); float h = 64, y0 = y;
  for (int i = 0; i < SUBF_N; i++) { if (y + h > 0 && y < SB_H) { float pk = ppress(x, y, w, h); int on = i == cur; gfx_rrect(x + 24, y + 2, w - 24, h - 4, 14, on ? col_alpha(ACC, 0.22f) : WHITE_A(pk > 0 ? (int)(14 + 20 * pk) : 5));
      const char *nm = i ? (kind == 2 ? SUBF_AR[i] : SUBF_LAT[i]) : T("Default"); int wt = kind == 2 ? text_sub_weight(600, 0, i) : text_sub_weight(600, i, 0);
      text_draw(nm, x + 48, y + h / 2 - text_line_height(26) / 2.0f, 26, wt, WHITE_A(on ? 255 : 220), 0, w * 0.55f, ALIGN_START);
      if (kind == 2) text_draw("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", x + w - (on ? 80 : 30), y + h / 2 - text_line_height(28) / 2.0f, 28, wt, WHITE_A(200), 0, 0, ALIGN_END); /* (a word in it) */
      if (on) icon_draw("ph_check", x + w - 60, y + h / 2 - 20, 40, ACC2);
      phit(x, y, w, h, a_font, kind, i); }
    y += h; }
  return y - y0 + 8; }
/* one of the tray's columns, from y down: its title, then its rows; returns where it ends */
static float col_draw(int c, float x, float y, float w, float lt) {
  if (c >= 0) { const char *ct = player_tray_coltitle(c); if (ct[0]) { y += 12; text_draw(ct, x + 8, y + 12, 28, 800, ACC2, 0.5f, w - 120, ALIGN_START); const char *cv = player_tray_colvalue(c); if (cv[0]) text_draw(cv, x + w - 12, y + 12, 28, 700, ACC2, 0, 0, ALIGN_END); y += 60; } }
  int n = player_tray_count(c); extern const char *player_tray_itemkey(int c, int i);
  for (int i = 0; i < n; i++) if (!strcmp(player_tray_itemkey(c, i), "pos")) { const char *lb, *sub, *vl, *th; int on, spin, tool; player_tray_item(c, i, &lb, &sub, &vl, &th, &on, &spin, &tool); int off = sty_off("pos"), h0 = nhit; if (off) gfx_opacity_push(0.35f); y += row_item(x, y, w, lb, sub, vl, 0, 0, a_posopen, 0, 0); if (off) { gfx_opacity_pop(); nhit = h0; } } /* Position first in its column: seen at once */
  for (int i = 0; i < n; i++) { const char *lb, *sub, *vl, *th; int on, spin, tool; int k = player_tray_item(c, i, &lb, &sub, &vl, &th, &on, &spin, &tool);
    if (!strcmp(player_tray_itemkey(c, i), "dlyopen")) { y += row_item(x, y, w, lb, sub, vl, 0, 0, a_dlyopen, 0, 0); continue; }
    { const char *ky = player_tray_itemkey(c, i); int off = sty_off(ky), h0 = nhit; /* (greyed and not pressable where the subtitles shown can't take it) */
      if (!strcmp(ky, "font") || !strcmp(ky, "fontar")) { int kind = ky[4] ? 2 : 1; if (off) { gfx_opacity_push(0.35f); if (fontOpen == kind) fontOpen = 0; } /* (a font: its list under its row) */
        { extern int player_tray_item_vwt(int c, int i); rowVwt = player_tray_item_vwt(c, i); } y += row_item(x, y, w, lb, sub, vl, 0, 0, a_fontopen, kind, 0); if (off) { gfx_opacity_pop(); nhit = h0; } else if (fontOpen == kind) y += font_list(x, y, w, kind); continue; }
      if (!strcmp(ky, "size")) { if (off) gfx_opacity_push(0.35f); y += row_item(x, y, w, lb, sub, vl, 0, 0, a_posopen, 1, 0); if (off) { gfx_opacity_pop(); nhit = h0; } continue; } /* the size: its bar at the top, the subtitles seen at each size */
      if (!strcmp(ky, "color") || !strcmp(ky, "bg") || !strcmp(ky, "bright")) { if (off) gfx_opacity_push(0.35f); y += style_row(x, y, w, ky, lb); if (off) { gfx_opacity_pop(); nhit = h0; } continue; } } /* (brightness too: on the phone the TV's arrow card had no way to change it) */ /* each choice shown as what it does */
    if (!strcmp(player_tray_itemkey(c, i), "pos")) continue; /* (drawn first, above) */ /* Position: its own card, the subtitles seen where they rest */ /* the card at the top, stepped by the finger (the TV's is driven by arrows) */
    if (k == 1) { ph_box(x, y, w, 74, 18); y += 82; continue; }
    if (k == 0) { text_draw_wrapped(lb, x + 10, y + 8, w - 20, 3, 24, 500, WHITE_A(150), 0, ALIGN_START, 30); y += 60; continue; }
    if (k == 2) { /* an episode: its still, its name, its line */ float tw = 260, thh = 146, h = thh + 18; if (y + h > lt - 10 && y < SB_H) { float pk = ppress(x, y, w, h); gfx_rrect(x, y, w, h, 18, on ? col_alpha(ACC, 0.22f) : WHITE_A(pk > 0 ? (int)(14 + 20 * pk) : 6));
        { extern int player_tray_item_hid(int c, int i); extern const char *player_tray_item_vid(int c, int i); int hid = player_tray_item_hid(c, i); const char *ev = player_tray_item_vid(c, i);
          extern int player_tray_item_unaired(int c, int i); extern const char *player_backdrop(void); const char *bg = player_backdrop(); int unrel = player_tray_item_unaired(c, i);
          int noStill = !th || !*th || img_shaped_failed(th, (int)tw, (int)thh, 12, 1), upBlur = unrel && noStill && bg[0] && spoil_blurable(bg); float sx = x + 8, sy = y + 8;
          if (hid && !noStill && spoil_blurable(th)) { spoil_picture(th, sx, sy, tw, thh, 12); spoil_mark(sx + tw / 2, sy + thh / 2, 52, 0, 0); } /* (as the TV's tray: soft, the crossed eye on it) */
          else if (upBlur) spoil_picture(bg, sx, sy, tw, thh, 12); /* not out yet, no still: the show's backdrop blurred (as the TV) */
          else if (noStill || hid) { GLuint bt = bg[0] ? img_get_shaped(bg, (int)tw, (int)thh, 12, 1) : 0; if (!bt || !ph_picture(bt, sx, sy, tw, thh, 12, WHITE_A(hid ? 110 : 255))) ph_box(sx, sy, tw, thh, 12); } /* (none of its own: the show's backdrop; hidden: dimmed) */
          else { GLuint t = img_get_shaped(th, (int)tw, (int)thh, 12, 1); if (!t || !ph_picture(t, sx, sy, tw, thh, 12, WHITE_A(255))) { ph_box(sx, sy, tw, thh, 12); spoil_reveal_wait(ev); } else spoil_reveal_over(ev, th, sx, sy, tw, thh, 12, sx + tw / 2, sy + thh / 2, 52); }
          if (upBlur) { const char *u = T("Upcoming"); float ph = 30, pw = text_width(u, 20, 600, 0.2f) + 46, px = sx + 10, py = sy + 10; gfx_rrect(px, py, pw, ph, ph / 2, RGBA(14, 12, 20, 178)); gfx_rrect_v(px, py, pw, ph, ph / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(px, py, pw, ph, ph / 2, 1, WHITE_A(46));
            gfx_circle(px + 19, py + ph / 2, 4.5f, ACC2); text_draw(u, px + 32, py + ph / 2 - text_line_height(20) / 2.0f, 20, 600, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); } } /* (the TV's tag, as on the title page) */
        { extern const char *player_tray_item_vid(int c, int i); extern const char *player_title_id(void); float pr = 0; int ds = ph_dl_state(player_title_id(), player_tray_item_vid(c, i), &pr); /* on the phone: its mark on the still, as on the pages; coming: its ring */
          if (ds == DL_DONE) { float d = 38, bx = x + 16, by = y + 16; gfx_shadow(bx, by + 3, d, d, d / 2, 10, BLACK_A(120)); gfx_circle(bx + d / 2, by + d / 2, d / 2, ACC | 0xff000000u); gfx_rrect_v(bx, by, d, d, d / 2, WHITE_A(70), WHITE_A(0)); icon_draw("ph_down", bx + 8, by + 8, 22, WHITE_A(255)); }
          else if (ds >= 0) { float cx2 = x + 35, cy2 = y + 35; gfx_circle(cx2, cy2, 19, BLACK_A(140)); gfx_ring(cx2 - 15, cy2 - 15, 30, 30, 15, 3, WHITE_A(50)); for (int q = 0; q <= 30; q++) { if (q > 30 * pr) break; float an = q * 12 * (float)M_PI / 180; gfx_circle(cx2 + 13.5f * sinf(an), cy2 - 13.5f * cosf(an), 1.6f, ACC2 | 0xff000000u); } } }
        int nl = text_draw_wrapped(lb, x + tw + 30, y + 16, w - tw - 48, 2, 32, 700, WHITE_A(240), 0, ALIGN_START, 38); if (sub && *sub) text_draw(sub, x + tw + 30, y + 24 + nl * 38, 26, 500, WHITE_A(140), 0, w - tw - 48, ALIGN_START);
        phit(x, y, w, h, a_item, c, i); } y += h + 8; continue; }
    { extern int player_tray_item_lang(int c, int i, int *shown, int *ticked); int sh2, tk2; if (player_tray_item_lang(c, i, &sh2, &tk2)) { rowShown = sh2; on = tk2; } } /* a language: lit when looked at, ticked only when its subtitle is on */
    y += row_item(x, y, w, lb, sub, vl, on, spin, a_item, c, i); }
  return y;
}
/* ---- the sources: a row of addons to pick from, then one card per stream ---- */
static int srcG[16], srcGn[16], srcGon[16], nsrcG; static const char *srcGname[16];
static void a_srcf(int g, int b) { (void)b; srcF = g; panV.pos = 0; }
static float src_chips(float px, float top, float pw) { /* a tab per addon (the one playing first shown), the lit pill gliding */
  nsrcG = 0; int n = player_src_count(), playing = -1;
  for (int i = 0; i < n; i++) { const char *nm, *tt; int on, open; int k = player_src_row(i, &nm, &tt, &on, &open);
    if (k == 5 && nsrcG < 16) { srcG[nsrcG] = i; srcGname[nsrcG] = nm; srcGon[nsrcG] = on; srcGn[nsrcG++] = 0; } else if (k == 3 && nsrcG) { srcGn[nsrcG - 1]++; if (on) playing = nsrcG - 1; } }
  if (!nsrcG) return top; if (srcF < 0 || srcF >= nsrcG) srcF = playing >= 0 ? playing : 0;
  static Scroll sv; static Tween tX, tW; float x0 = px + 28 - sv.pos, x = x0, tw[16], onX = 0, onW = 0; char l[16][128]; gfx_clip_push(px, top, pw, 80);
  for (int g = 0; g < nsrcG; g++) { snprintf(l[g], 128, "%s  %d", srcGname[g], srcGn[g]); tw[g] = text_width(l[g], 26, 700, 0) + 48 + (srcGon[g] ? 30 : 0);
    gfx_rrect(x, top, tw[g], 62, 31, WHITE_A(16)); if (g == srcF) { onX = x - x0; onW = tw[g]; } phit(x, top, tw[g], 62, a_srcf, g, 0); x += tw[g] + 10; }
  if (panA.v < 0.9f || tW.v <= 0) { tween_set(&tX, onX); tween_set(&tW, onW); } else { tween_to(&tX, onX, 0.3f, EASE_WEB); tween_to(&tW, onW, 0.3f, EASE_WEB); } tween_tick(&tX, phDt); tween_tick(&tW, phDt);
  gfx_rrect(x0 + tX.v, top, tW.v, 62, 31, col_alpha(ACC, 0.8f));
  x = x0; for (int g = 0; g < nsrcG; g++) { float c = clampf(1 - fabsf((x - x0) - tX.v) / tw[g], 0, 1); text_draw(l[g], x + 24, top + 31 - text_line_height(26) / 2.0f, 26, 700, col_mix(WHITE_A(220), ACC_FG, c), 0, 0, ALIGN_LEFT); if (srcGon[g]) spinner(x + tw[g] - 30, top + 31, 10); x += tw[g] + 10; }
  sv.max = x - x0 + 56 - pw; scroll_tick(&sv); gfx_clip_pop(); return top + 80;
}
/* a stream's card: its name on the left and its description on the right, line for line as the addon wrote them
   (people shape that text in the addon's own settings: nothing is read out of it or rearranged) */
static int lines_of(const char *t, const char **st, int *len, int max) { int n = 0; while (t && *t && n < max) { const char *e = strchr(t, '\n'); int l = e ? (int)(e - t) : (int)strlen(t); while (l && (t[l - 1] == ' ' || t[l - 1] == '\r')) l--; st[n] = t; len[n++] = l; if (!e) break; t = e + 1; } while (n && !len[n - 1]) n--; return n; }
static float src_card(float x, float y, float w, int i, const char *nm, const char *tt, int on, float lt) {
  const char *ns[6], *ds[14]; int nl[6], dl[14], nn = lines_of(nm, ns, nl, 6), nd = lines_of(tt, ds, dl, 14);
  float lh = 30, h = (nd > nn ? nd : nn) * lh + 32; if (h < 104) h = 104; if (y + h < lt - 20 || y > SB_H) return h + 10;
  float pk = ppress(x, y, w, h), k = 1 - 0.015f * pk; gfx_scale_push(k, k, x + w / 2, y + h / 2);
  gfx_rrect(x, y, w, h, 22, on ? col_alpha(ACC, 0.24f) : WHITE_A(pk > 0 ? (int)(12 + 22 * pk) : 9)); if (on) gfx_ring(x, y, w, h, 22, 2, col_alpha(ACC, 0.9f));
  float lw = 230, ny = y + h / 2 - nn * lh / 2; char b[400];
  for (int q = 0; q < nn; q++) { snprintf(b, sizeof b, "%.*s", nl[q], ns[q]); text_draw(b, x + 22, ny + q * lh + 2, 25, 800, WHITE_A(on ? 255 : 240), 0, lw - 30, ALIGN_START); }
  gfx_rect(x + lw, y + 18, 1.5f, h - 36, WHITE_A(30));
  float tx = x + lw + 20, tw = x + w - tx - (on ? 70 : 20), dy = y + h / 2 - nd * lh / 2;
  for (int q = 0; q < nd; q++) { snprintf(b, sizeof b, "%.*s", dl[q], ds[q]); text_draw(b, tx, dy + q * lh + 3, 22, 500, WHITE_A(on ? 235 : 200), 0, tw, ALIGN_START); }
  if (on) icon_draw("ph_check", x + w - 62, y + h / 2 - 22, 44, ACC2);
  gfx_scale_pop(); phit(x, y, w, h, a_src, i, 0); if (nholds < 64 && y + h > hcY0 && y < hcY1) { holds[nholds].x = x; holds[nholds].y = y; holds[nholds].w = w; holds[nholds].h = h; holds[nholds++].i = i; }
  return h + 10;
}
/* a stream held: all its text and what it is, over the list, with a button to play it */
static void a_infoplay(int a, int b) { (void)b; infoRow = -1; tween_set(&infoA, 0); player_src_pick(a); }
static void a_infoclose(int a, int b) { (void)a; (void)b; infoRow = -1; tween_to(&infoA, 0, 0.2f, EASE_CSS_EASE); }
static void info_draw(float px, float py, float pw, float ph) {
  static int shown = -1; if (infoRow >= 0) shown = infoRow; tween_tick(&infoA, phDt); float k = infoA.v; if (k <= 0.01f || shown < 0) return;
  static char info[1200]; const char *addon = ""; double sz = 0; if (!player_src_detail(shown, info, sizeof info, &addon, &sz)) { a_infoclose(0, 0); return; }
  float x = px + 16, y = py + 16 + (1 - k) * 40, w = pw - 32, h = ph - 32; gfx_opacity_push(k);
  pglass(x, y, w, h, 30, RGBA(12, 10, 18, 175), RGBA(12, 10, 18, 215), 1, 0); phit(x, y, w, h, a_keep, 0, 0);
  text_draw(T("Stream details"), x + 30, y + 52 - text_line_height(34) / 2.0f, 34, 800, WHITE_A(250), -0.2f, w - 160, ALIGN_START); circle(x + w - 60, y + 52, 40, "ph_x", 0, a_infoclose, 0);
  char line[200]; snprintf(line, sizeof line, "%s", addon); (void)sz; text_draw(line, x + 30, y + 98, 24, 700, ACC2, 0, w - 60, ALIGN_START);
  /* the stream's own lines (its name, then its description), each trimmed, the divider lines left out */
  { const char *nm, *tt; int on, op; static char raw[1600]; extern const char *player_src_file(int i); if (player_src_row(shown, &nm, &tt, &on, &op) == 3) { const char *fn = player_src_file(shown); snprintf(raw, sizeof raw, "%s\n%s%s%s", nm, tt, fn[0] && !strstr(tt, fn) ? "\n" : "", fn[0] && !strstr(tt, fn) ? fn : ""); } else snprintf(raw, sizeof raw, "%s", info);
    gfx_clip_push(x, y + 136, w, h - 136 - 110); float ly = y + 140; char *p = raw;
    while (*p && ly < y + h - 110) { char *e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p); char ln[400]; size_t o = 0; int sp = 0, real = 0;
      for (size_t q = 0; q < n && o < sizeof ln - 4; q++) { unsigned char c = (unsigned char)p[q]; if (c == 0xC2 && q + 1 < n && (unsigned char)p[q + 1] == 0xA0) { q += 1; c = ' '; } else if (c == 0xE2 && q + 2 < n && (unsigned char)p[q + 1] == 0x80 && (unsigned char)p[q + 2] <= 0x8B) { q += 2; c = ' '; } /* wide and fixed spaces */
        if (c == ' ' || c == '\t') { if (o && !sp) ln[o++] = ' '; sp = 1; continue; } sp = 0;
        if (c == 0xE2 && q + 2 < n && (unsigned char)p[q + 1] == 0x94) { q += 2; continue; } /* box-drawing dashes */ if (c != '-' && c != '_' && c != '=') real = 1; ln[o++] = (char)c; }
      while (o && ln[o - 1] == ' ') o--; ln[o] = 0;
      if (real && o) { int nl = text_draw_wrapped(ln, x + 30, ly, w - 60, 4, 25, ly == y + 140 ? 700 : 500, WHITE_A(ly == y + 140 ? 245 : 205), 0, ALIGN_START, 33); ly += nl * 33 + 8; }
      if (!e) break; p = e + 1; }
    gfx_clip_pop(); }
  float bw = w - 60, bh = 80, bx = x + 30, by = y + h - bh - 24, pk = ppress(bx, by, bw, bh); gfx_rrect(bx, by, bw, bh, bh / 2, col_alpha(ACC, 0.85f + 0.15f * pk));
  icon_draw("play", bx + bw / 2 - text_width(T("Play this stream"), 28, 800, 0) / 2 - 40, by + bh / 2 - 18, 36, ACC_FG); text_draw(T("Play this stream"), bx + bw / 2 + 18, by + bh / 2 - text_line_height(28) / 2.0f, 28, 800, ACC_FG, 0, 0, ALIGN_CENTER);
  phit(bx, by, bw, bh, a_infoplay, shown, 0); gfx_opacity_pop();
}
/* the tray, taken by the finger: it follows it any way (freely right and down, against a growing pull up and left),
   shrinking a little about the point held so that point stays under the finger, its corners rounding as it comes
   away. Let go: where the throw would carry it decides; far enough, it flies on the way it was thrown, smaller and
   fading; else a damped spring brings it home with a breath of overshoot */
static struct { int on; float ox, oy, vx, vy, gx, gy, bx, by; double flyAt; } TD; /* on: 1 held, 2 springing home, 3 flying away; gx, gy: the point taken */
static float rubber(float d, float lim) { return d >= 0 ? d : -lim * (1 - 1 / (1 + (-d) * 0.55f / lim)); }
static float td_pull(float pw) { float p = hypotf(TD.ox, TD.oy) / (pw * 0.9f); return p > 1 ? 1 : p; }
static void a_back(int a, int b);
static void td_tick(float pw) { float dt = phDt > 0.05f ? 0.05f : phDt;
  if (TD.on == 2) { const float k = 520, c = 2 * sqrtf(520) * 0.72f; for (int q = 0; q < 4; q++) { float h = dt / 4; float ax = -k * TD.ox - c * TD.vx, ay = -k * TD.oy - c * TD.vy; TD.vx += ax * h; TD.vy += ay * h; TD.ox += TD.vx * h; TD.oy += TD.vy * h; }
    if (hypotf(TD.ox, TD.oy) < 0.4f && hypotf(TD.vx, TD.vy) < 6) { TD.on = 0; TD.ox = TD.oy = TD.vx = TD.vy = 0; } }
  else if (TD.on == 3) { TD.vx *= 1 + 1.6f * dt; TD.vy *= 1 + 1.6f * dt; TD.ox += TD.vx * dt; TD.oy += TD.vy * dt; /* away, a little faster as it goes */
    if (app_now() - TD.flyAt > 0.3) { TD.on = 0; TD.ox = TD.oy = TD.vx = TD.vy = 0; a_back(0, 0); tween_set(&panA, 0); } } (void)pw; }
static void panel_draw(void) {
  float a = panA.v; if (a <= 0.01f) return; int kind = panel ? panel : shownKind;
  td_tick(SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f); float pull = TD.on ? td_pull(SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f) : 0, fly = TD.on == 3 ? clampf((float)((app_now() - TD.flyAt) / 0.3), 0, 1) : 0;
  float tsc = 1 - 0.16f * pull - 0.22f * fly, trad = 46 * pull; /* smaller as it comes away, rounder */
  gfx_offset_push(TD.ox, TD.oy); gfx_scale_push(tsc, tsc, TD.gx, TD.gy); gfx_opacity_push(1 - 0.25f * pull - 0.75f * fly * fly);
  float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f, px = SB_W - pw - 20, py = 20, ph = SB_H - 40;
  /* the glass grows out of the pill that opened it (and shrinks back into it); the contents come in once it has room */
  SbRect o = orgR.w > 0 ? orgR : rect(px + pw / 2 - 100, SB_H - 112, 200, 78); float e = clampf(a, 0, 1);
  float gx = o.x + (px - o.x) * e, gy = o.y + (py - o.y) * e, gw = o.w + (pw - o.w) * e, gh = o.h + (ph - o.h) * e, gr = 39 + (36 - 39) * e;
  gfx_opacity_push(clampf(a * 3, 0, 1)); gr += trad; gfx_shadow(gx, gy + 10 + 20 * pull, gw, gh, gr, 40 + 30 * pull, BLACK_A((int)(110 + 50 * pull))); pglass(gx, gy, gw, gh, gr, RGBA(14, 12, 20, 168), RGBA(16, 14, 24, 170), 1, 0); /* (a tray: darker, its lists read over any picture) */ gfx_opacity_pop(); /* glass: the film blurred behind it */
  gfx_clip_push(gx, gy, gw, gh); gfx_opacity_push(clampf((a - 0.45f) / 0.55f, 0, 1));
  phit(px, py, pw, ph, a_keep, 0, 0); /* the panel itself: taps on it do not reach the picture */
  int tk = player_tray_kind(); const char *title = kind == 9 ? T("Channels") : kind == 6 ? T("Speed") : kind == 2 ? T("Sources") : (kind == 3 || kind == 4) ? T("Audio & subtitles") : tk == 5 ? player_tray_title() : T("Episodes");
  float tx = px + 36; if (tk == 5) { circle(px + 72, py + 70, 42, "ph_back", 0, a_epback, 0); tx = px + 130; }
  text_draw(title, tx, py + 70 - text_line_height(40) / 2.0f, 40, 800, WHITE_A(250), -0.3f, pw - (tx - px) - 120, ALIGN_START);
  circle(px + pw - 72, py + 70, 42, "ph_x", 0, a_back, 0);
  float top = py + 136;
  /* Audio & subtitles: two segments; subtitles have their own tabs (the file's, the addons') */
  if (kind == 3 || kind == 4) { float sx = px + 28, sw = (pw - 56) / 2; static Tween segX; tween_to(&segX, tk == 4 ? 1 : 0, 0.3f, EASE_WEB); tween_tick(&segX, phDt);
    gfx_rrect(sx, top, 2 * sw - 8, 76, 38, WHITE_A(18)); gfx_rrect(sx + segX.v * sw, top, sw - 8, 76, 38, WHITE_A(240)); /* the white pill glides to the one chosen */
    for (int i = 0; i < 2; i++) { int on = (i == 0 && tk == 3) || (i == 1 && tk == 4); float x = sx + i * sw, k = i ? segX.v : 1 - segX.v;
      text_draw(i ? T("Subtitles") : T("Audio"), x + (sw - 8) / 2, top + 38 - text_line_height(30) / 2.0f, 30, 800, col_mix(WHITE_A(230), RGB(14, 12, 20), k), 0, 0, ALIGN_CENTER);
      phit(x, top, sw - 8, 76, on ? a_keep : a_panel, i ? 4 : 3, 0); } top += 96; }
  if (kind == 9) { float ph_live_panel_draw(float px, float top, float pw, float bot, Scroll *tabs, Scroll *list, void (*hit)(float, float, float, float, void (*)(int, int), int, int));
    chTabY = top; scroll_tick(&panV); ph_live_panel_draw(px, top, pw, py + ph - 24, &tabV, &panV, phit);
    gfx_opacity_pop(); gfx_clip_pop(); gfx_opacity_pop(); gfx_scale_pop(); gfx_offset_pop(); return; } /* the channels: their pills and rows (phone/live_phone.c) */
  int ntabs = (tk == 1 || tk == 4) ? player_tray_ntabs() : 0, tabs = tk == 4 ? (ntabs > 1 ? ntabs : 2) : ntabs; /* subtitles: the player's own tabs (Subtitles, Addons, Style/Delay); a trailer's one list: it and Sync & style */ if (tk == 4 && ntabs > 1) subStyle = 0;
  if (tabs > 1) { float x0 = px + 28 - tabV.pos, x = x0, tot = 0, onX = -1, onW = 0; gfx_clip_push(px, top, pw, 80); static Tween tX, tW; static int tSeen;
    float tw[12]; const char *tn[12]; int ton[12], nt = tabs < 12 ? tabs : 12;
    for (int i = 0; i < nt; i++) { int st = tk == 4 && ntabs <= 1 && i == tabs - 1; tn[i] = st ? T("Sync & style") : ntabs > 1 ? player_tray_tabname(i) : T("Subtitles"); tw[i] = text_width(tn[i], 28, 700, 0) + 52;
      ton[i] = tk == 4 ? (st ? subStyle : !subStyle && (ntabs > 1 ? i == player_tray_tab() : 1)) : i == player_tray_tab();
      gfx_rrect(x, top, tw[i], 66, 33, WHITE_A(16)); if (ton[i]) { onX = x - x0; onW = tw[i]; } phit(x, top, tw[i], 66, a_tab, st ? -1 : i, 0); x += tw[i] + 12; tot += tw[i] + 12; }
    if (onW > 0) { if (!tSeen || panA.v < 0.5f) { tween_set(&tX, onX); tween_set(&tW, onW); tSeen = 1; } else { tween_to(&tX, onX, 0.3f, EASE_WEB); tween_to(&tW, onW, 0.3f, EASE_WEB); } tween_tick(&tX, phDt); tween_tick(&tW, phDt);
      gfx_rrect(x0 + tX.v, top, tW.v, 66, 33, col_alpha(ACC, 0.8f)); } /* the lit pill glides to the tab chosen */
    x = x0; for (int i = 0; i < nt; i++) { float c = onW > 0 ? clampf(1 - fabsf((x - x0) - tX.v) / (tw[i] > 1 ? tw[i] : 1), 0, 1) : 0; text_draw(tn[i], x + tw[i] / 2, top + 33 - text_line_height(28) / 2.0f, 28, 700, col_mix(WHITE_A(220), ACC_FG, c), 0, 0, ALIGN_CENTER); x += tw[i] + 12; }
    gfx_clip_pop(); tabV.max = tot + 56 - pw; scroll_tick(&tabV); top += 88; }
  if (tk == 2 || tk == 5) top = src_chips(px, top, pw);
  float lt = top, lh = py + ph - 24 - lt, x = px + 22, w = pw - 44; gfx_clip_push(px, lt, pw, lh); scroll_tick(&panV); scroll_tick(&panV2); hcY0 = lt; hcY1 = lt + lh;
  { static int lastKey = -1, dir = 1; static Tween navA; int key = kind * 100000 + tk * 1000 + (tk == 4 || tk == 1 ? player_tray_tab() * 10 : 0) + subStyle;
    if (key != lastKey) { dir = key > lastKey ? 1 : -1; if (lastKey >= 0 && panA.v > 0.5f) { tween_set(&navA, 0); tween_to(&navA, 1, 0.3f, EASE_WEB); } else tween_set(&navA, 1); lastKey = key; }
    tween_tick(&navA, phDt); gfx_offset_push(dir * (1 - navA.v) * 70, 0); gfx_opacity_push(0.15f + 0.85f * navA.v); } /* the new list comes in from its side */
  float y = lt - panV.pos; int two = 0; splitX = -1;
  if (kind == 6) { for (int i = 0; i < 6; i++) { char l[16]; snprintf(l, sizeof l, "%g\xc3\x97", SPEEDS[i]); y += row_item(x, y, w, i == 2 ? T("Normal") : l, NULL, i == 2 ? l : NULL, fabsf(speed - SPEEDS[i]) < 0.01f, 0, a_speed, i, 0); } }
  else if (tk == 2 || tk == 5) { int n = player_src_count(), gi = -1, shown = 0; if (!n) { spinner(px + pw / 2, y + 60, 20); y += 120; }
    for (int i = 0; i < n; i++) { const char *nm, *tt; int on, open; int k = player_src_row(i, &nm, &tt, &on, &open);
      if (k == 5) { gi++; continue; } if (nsrcG && gi != srcF) continue; /* the chosen addon's tab */
      if (k == 3) { y += src_card(x, y, w, i, nm, tt, on, lt); shown++; }
      else if (k == 1) { ph_box(x, y, w, 110, 22); y += 120; }
      else if (k == 0 && (srcF >= 0 || !shown)) { text_draw_wrapped(nm, x + 10, y + 10, w - 20, 3, 24, 500, WHITE_A(150), 0, ALIGN_START, 30); y += 80; } } }
  else if (tk) { int nc = player_tray_ncols();
    if (tk == 4 && nc >= 2) { float cw = (w - 16) / 2; int l = -1, r = -1;
      if (nc == 2) { l = 0; r = 1; } /* each tab's two columns side by side: the lists, or the style and the delay */
      else if (subStyle) { l = nc - 2; r = nc - 1; } /* (a trailer) style and delay side by side */
      else if (nc == 4) { l = 0; r = 1; } /* the file's and OpenSubtitles' (or the languages and theirs) side by side */
      if (l >= 0) { float y0 = col_draw(l, x, lt - panV.pos, cw, lt), y1 = col_draw(r, x + cw + 16, lt - panV2.pos, cw, lt); /* each scrolls on its own */
        panV.max = y0 + panV.pos - (lt + lh) + 20; panV2.max = y1 + panV2.pos - (lt + lh) + 20; two = 1; splitX = x + cw + 8; }
      else y = col_draw(0, x, y, w, lt); }
    else for (int c = nc ? 0 : -1; c < (nc ? nc : 0); c++) y = col_draw(c, x, y, w, lt); }
  if (!two) { panV.max = y + panV.pos - (lt + lh) + 20; panV2.pos = 0; } gfx_opacity_pop(); gfx_offset_pop(); gfx_clip_pop(); hcY0 = -1e9f; hcY1 = 1e9f;
  info_draw(px, py, pw, ph);
  gfx_opacity_pop(); gfx_clip_pop(); gfx_opacity_pop(); gfx_scale_pop(); gfx_offset_pop();
  if (panA.to > 0.5f && !player_tray_kind() && panel >= 1 && panel <= 4) { panel = 0; tween_to(&panA, 0, 0.22f, EASE_CSS_EASE); } /* the player put its list away (a source chosen, the film over) */
}

/* ---- the viewer notice, as on the TV: as the picture first runs, IMDb's Parents Guide read as an age and what for,
   for 6 s, controls or not: one small glass pill, the age in an accent badge, each reason with three dots for how much
   (amber when severe), coming in one after the other; top left, under the title while the controls show ---- */
static struct { ParGuide g; int age; unsigned sess; double at; char id[96]; Tween a; } PGN;
static void on_pgn(const ParGuide *g, void *ud) { if ((unsigned)(uintptr_t)ud != PGN.sess || !g || !g->n || !player_live()) return;
  int sev = 0, mod = 0; for (int i = 0; i < g->n; i++) { sev += g->lvl[i] == PG_SEVERE; mod += g->lvl[i] == PG_MODERATE; }
  PGN.g = *g; PGN.age = sev >= 3 ? 18 : sev ? 16 : mod ? 13 : 7; PGN.at = app_now(); }
static void notice_draw(float osd) {
  extern const char *player_title_id(void); const char *id = player_title_id();
  if (strcmp(id, PGN.id)) { snprintf(PGN.id, sizeof PGN.id, "%s", id); PGN.at = 0; PGN.g.n = 0; PGN.sess++; tween_set(&PGN.a, 0); }
  static unsigned asked; if (asked != PGN.sess && player_live() && !player_is_trailer() && !strncmp(id, "tt", 2)) { asked = PGN.sess; parental_get(id, on_pgn, (void *)(uintptr_t)PGN.sess); } /* asked as the picture comes */
  int on = PGN.at > 0 && app_now() - PGN.at < 6.0 && !panel && posA.to < 0.5f; /* (never under the Position or Size bar) */ tween_to(&PGN.a, on ? 1 : 0, on ? 0.5f : 0.55f, EASE_CSS_EASE); tween_tick(&PGN.a, phDt); if (PGN.a.v < 0.004f) return;
  float t = (float)(app_now() - PGN.at), h = 50, x = 40 + (1 - PGN.a.v) * -14, y = 24 + osd * (96 + 8 + 36 + 22); /* the controls up: under the title's logo and its line, out of their way */ char ag[8]; snprintf(ag, sizeof ag, "%d+", PGN.age);
  float nw[PG_CATS]; for (int c = 0; c < PG_CATS; c++) nw[c] = text_width(parental_cat_name(c), 21, 600, 0);
  float bw = text_width(ag, 24, 800, 0) + 22, w = 8 + bw + 14; for (int i = 0; i < PGN.g.n; i++) w += nw[PGN.g.cat[i]] + 8 + 2 * 10 + 7 + (i + 1 < PGN.g.n ? 18 : 16);
  float fitK = w > SB_W - 80 - (osd > 0.5f ? 0 : 0) ? (SB_W - 80) / w : 1; gfx_scale_push(fitK, fitK, x, y); /* never cut by the screen's edge: smaller when long */
  gfx_opacity_push(PGN.a.v); gfx_rrect(x, y, w, h, h / 2, RGBA(10, 12, 22, 160)); gfx_ring(x, y, w, h, h / 2, 1.5f, WHITE_A(46));
  float bx = x + 7, by = y + 7; gfx_rrect(bx, by, bw, h - 14, (h - 14) / 2, col_alpha(ACC | 0xff000000u, 0.45f)); text_draw(ag, bx + bw / 2, by + (h - 14 - text_line_height(24)) / 2, 24, 800, WHITE_A(255), 0, 0, ALIGN_CENTER);
  float rx = bx + bw + 14, ry = y + (h - text_line_height(21)) / 2, cy = y + h / 2;
  for (int i = 0; i < PGN.g.n; i++) { float k = clampf((t - 0.25f - i * 0.12f) / 0.35f, 0, 1); k = 1 - (1 - k) * (1 - k); int c = PGN.g.cat[i], lv = PGN.g.lvl[i];
    if (k > 0.004f) { gfx_opacity_push(k); text_draw(parental_cat_name(c), rx, ry, 21, 600, WHITE_A(225), 0, 0, ALIGN_LEFT);
      for (int d = 0; d < 3; d++) gfx_circle(rx + nw[c] + 8 + 3.5f + d * 10, cy, 3.5f, d < lv ? (lv == PG_SEVERE ? RGB(0xf0, 0xb9, 0x4a) : WHITE_A(220)) : WHITE_A(55));
      gfx_opacity_pop(); }
    rx += nw[c] + 8 + 2 * 10 + 7 + 18; }
  gfx_opacity_pop(); gfx_scale_pop();
}
/* ---- the frame ---- */
#define TOPY 74.0f
#define TOPR 48.0f
#define LOCKX (SB_W - 204)
#define TVX (SB_W - 88)
#define DLX (SB_W - 320)
/* ---- the title, top left: its logo (else its name), and under it smaller the year, genre and age (or S:E and the
   episode's name) ---- */
static char ageFor[96], ageLbl[32]; static unsigned ageSess;
static void on_age(const Rating *r, void *ud) { if ((unsigned)(uintptr_t)ud != ageSess) return; char b[32]; rating_label(r, b, sizeof b); snprintf(ageLbl, sizeof ageLbl, "%s", r->rating[0] ? b : ""); }
static void title_draw(float x, float maxW) {
  extern const char *player_title_type(void); extern const char *player_title_id(void); extern void player_facts(char *out, size_t cap);
  const char *id = player_title_id(); if (strcmp(id, ageFor)) { snprintf(ageFor, sizeof ageFor, "%s", id); ageLbl[0] = 0; ageSess++; if (*id && !player_is_trailer()) rating_get(player_title_type(), id, on_age, (void *)(uintptr_t)ageSess); }
  float ta; { const LiveCard *c = live_card(); static Tween tA; static int init; if (!init) { init = 1; tween_set(&tA, 1); } tween_to(&tA, c->on && c->tuning && c->it && strcmp(c->it->id, id) ? 0 : 1, 0.22f, EASE_CSS_EASE); tween_tick(&tA, phDt); ta = tA.v; } /* switching channels: the one left fades away, the new one comes in when it plays */
  if (ta < 0.01f) return;
  const char *lg = player_logo(); float lh = 96, lw = 560 < maxW ? 560 : maxW, asp = 0, ty = TOPY - 50; /* the logo, big enough to read at a glance */ GLuint t = lg && *lg ? img_get(lg, (int)lw, (int)lh, &asp) : 0;
  if (t && asp > 0) { float w = lh * asp, h = lh; if (w > lw) { w = lw; h = w / asp; } gfx_image(t, x, ty + (lh - h) / 2, w, h, 0, WHITE_A((int)(255 * ta)), 0, 0, 1, 1); }
  else text_draw(player_name(), x, ty + lh / 2 - text_line_height(36) / 2.0f, 36, 800, WHITE_A((int)(250 * ta)), -0.4f, maxW, ALIGN_START);
  char f[300]; player_facts(f, sizeof f); float fy = ty + lh + 8, fx = x;
  if (f[0]) { text_draw(f, fx + 1, fy + 2, 27, 700, BLACK_A((int)(120 * ta)), 0, maxW - 90, ALIGN_START); text_draw(f, fx, fy, 27, 700, WHITE_A((int)(235 * ta)), 0, maxW - 90, ALIGN_START); fx += text_width(f, 27, 700, 0) + 16; } /* (a faint shadow: readable on bright pictures) */
  if (ageLbl[0] && fx < x + maxW - 60) { float w = text_width(ageLbl, 20, 800, 0) + 18, h = text_line_height(27) + 2; gfx_ring(fx, fy - 1, w, h, 7, 1.5f, WHITE_A((int)(170 * ta))); text_draw(ageLbl, fx + 9, fy + h / 2 - text_line_height(20) / 2.0f - 1, 20, 800, WHITE_A((int)(220 * ta)), 0, 0, ALIGN_LEFT); } /* the age: a small outlined badge */
}

static float barY(void) { return SB_H - 168; } /* (the pills under it at SB_H - 118) */
static float barX0(void) { return 160; }
static float barX1(void) { return SB_W - 160; }
/* ---- skip intro / recap, and at the credits: watch them or the next episode, bottom right (over the bar when the
   controls are up, near the edge when not); the one that acts by itself fills up as it counts down ---- */
extern int player_prompt(const char **b0, const char **b1, float *fill); extern void player_prompt_go(int i);
static SbRect promptR[2]; static int promptN;
static void prompt_draw(float osd) {
  static Tween prA; static char l0[48], l1[48]; static int kk; static float fill = -1; const char *b0, *b1; float f;
  int k = player_prompt(&b0, &b1, &f); if (k) { kk = k; snprintf(l0, sizeof l0, "%s", b0); snprintf(l1, sizeof l1, "%s", b1); fill = f; }
  tween_to(&prA, k && !panel && !locked ? 1 : 0, 0.32f, EASE_WEB); tween_tick(&prA, phDt); promptN = 0; float a = prA.v; if (a <= 0.01f) return;
  float h = 84, y = (SB_H - 44 - h) + (barY() - 70 - h - (SB_H - 44 - h)) * osd, right = SB_W - 40 + (1 - a) * 120; gfx_opacity_push(a);
  int nb = kk == 3 && l1[0] ? 2 : 1; for (int i = nb - 1; i >= 0; i--) { const char *l = i == 0 ? l0 : l1; int main = nb == 1 || i == 1; float tw = text_width(l, 28, 800, 0), w = tw + 56 + (main ? 52 : 0), x = right - w;
    float pk = ppress(x, y, w, h), sc = 1 - 0.05f * pk; gfx_scale_push(sc, sc, x + w / 2, y + h / 2);
    if (main) { gfx_shadow(x, y + 6, w, h, h / 2, 24, BLACK_A(120)); gfx_rrect(x, y, w, h, h / 2, WHITE_A(242)); float fl = (nb == 1 || i == 1) ? fill : -1; if (fl >= 0) { gfx_clip_push(x, y, w * fl, h); gfx_rrect(x, y, w, h, h / 2, col_alpha(ACC2 | 0xff000000u, 0.35f)); gfx_clip_pop(); } /* it fills up: then it acts */
      icon_draw("p_next", x + 24, y + h / 2 - 20, 40, RGB(14, 12, 20)); text_draw(l, x + 74, y + h / 2 - text_line_height(28) / 2.0f, 28, 800, RGB(14, 12, 20), 0, 0, ALIGN_LEFT); }
    else { pglass(x, y, w, h, h / 2, RGBA(14, 12, 20, 96), RGBA(16, 14, 24, 150), 1, 0); text_draw(l, x + w / 2, y + h / 2 - text_line_height(28) / 2.0f, 28, 800, WHITE_A(240), 0, 0, ALIGN_CENTER); }
    gfx_scale_pop(); if (promptN < 2) promptR[promptN++] = rect(x, y, w, h); right = x - 16; (void)i; }
  gfx_opacity_pop();
}
void phone_player_draw(void) {
  nhit = 0; nholds = 0; { static char last[16]; const char *m = player_aspect(); if (strcmp(m, last)) { snprintf(last, sizeof last, "%s", m); void video_size_mode(const char *mode); video_size_mode(m); } } /* the picture's size: libVLC's own scaling */ int play = !player_paused() || seekHeld; /* (a seek holding the film: it plays on after) */
  { extern int player_failed(void); int lv = player_live();
    if (!lv && !locked) { if (player_failed()) { if (!ui_on()) ui_show(1); keep(); } else if (ui_on() && !panel) ui_show(0); } /* the first loading is the title's art, its logo and line alone: no controls over it (a failure brings them back, for Back and Sources) */ }
  if (ui_on() && play && !panel && !G.on && app_now() > hideAt) ui_show(0);
  tween_tick(&uiA, phDt); tween_tick(&panA, phDt);
  tween_to(&lockA, locked && app_now() - lockShownAt < 2.5 ? 1 : 0, 0.2f, EASE_CSS_EASE); tween_tick(&lockA, phDt);
  tween_tick(&bubA, phDt); tween_tick(&pressA, phDt); tween_tick(&btnA, phDt); tween_tick(&lvlA, phDt); tween_tick(&jumpA, phDt);
  { extern int player_waiting_picture(void); static double waitAt; static double lastT, movedAt; double tn = player_time(); if (fabs(tn - lastT) > 0.001) movedAt = app_now(); lastT = tn;
    int seeking = !playing_local() && app_now() - seekAt < 6 && app_now() - movedAt > 0.12; /* (a file on the phone: never a wait shown for a seek) */ /* a seek on its way: the wait shows at once */
    /* a seek on a stream is under way from the tap until the film moves on from there (however long the source takes):
       the wait shows at once and stays, never the play sign of the hold it is paused in; a pause by hand ends it */
    static double lastSeek, mvFrom; static int pend; if (seekAt != lastSeek) { lastSeek = seekAt; pend = !playing_local(); mvFrom = 0; }
    if (pend) { if (playTapAt > seekAt) pend = 0; /* (paused or played by hand meanwhile; never the player's own pause-and-go around the seek) */
      else if (app_now() - movedAt < 0.6 && app_now() - seekAt > 0.6 && !seekHeld) { if (!mvFrom) mvFrom = app_now(); else if (app_now() - mvFrom > 0.7) pend = 0; } else mvFrom = 0; if (app_now() - seekAt > 90) pend = 0; } /* (the time may move a few times a second: gaps under 0.6 s are still moving; never held past 90 s) */ /* (moving again for a moment: landed) */
    int w = player_live() && (play || pend) && (pend || (seeking && app_now() - seekAt > 0.25) || (player_waiting_picture() && app_now() - movedAt > 0.3)); if (!w) waitAt = 0; else if (seeking || pend) waitAt = app_now() - 1; /* after a seek: as soon as the picture is seen waiting */ /* the time moving again: gone at once, whatever the player still says */ else if (!waitAt) waitAt = app_now();
    tween_to(&waitA, w && app_now() - waitAt > 0.4 ? 1 : 0, w ? 0.15f : 0.12f, EASE_CSS_EASE); tween_tick(&waitA, phDt); } /* a wait that lasts (a quick one shows nothing) */
  if (seekDim.v > 0.01f) gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(80 * seekDim.v))); /* a seek on its way: the film a little dimmed */
  { static int was; int on = G.mode == 4 || G.mode == 1; if (on && !was) seekStartAt = app_now(); was = on; }
  { int sk = (G.mode == 4 || G.mode == 1) && sp_active(); tween_to(&seekK, sk, sk ? 0.18f : 0.45f, EASE_CSS_EASE); tween_tick(&seekK, phDt); } /* (seeking with previews: everything but the bar steps back) */
  float a = locked ? 0 : uiA.v * (1 - panA.v); /* a panel up: the controls slide away behind it */
  if (a > 0.01f) { gfx_opacity_push(a);
    gfx_rrect_v(0, 0, SB_W, 300, 0, BLACK_A(185), BLACK_A(0)); { float rw = SB_W * 0.62f, rh = 300; gfx_alpha_tex(gfx_radial_tex(), -rw, -rh, 2 * rw, 2 * rh, BLACK_A(95)); } /* a deeper shade at the top left, fading every way from the corner (a band with a hard edge at the middle before): the logo stands out on bright pictures */ gfx_rrect_v(0, SB_H - 380, SB_W, 380, 0, BLACK_A(0), BLACK_A(185));
    /* the top: slides up as it goes */
    gfx_offset_push(0, -(1 - a) * 150); gfx_opacity_push(1 - seekK.v);
    gfx_rrect(SB_W / 2 - 36, 14, 72, 8, 4, WHITE_A(130)); /* a handle at the top: a swipe down closes it */
    title_draw(48, SB_W - 680); /* (no back button: a swipe down or the phone's Back closes it) */
    { int cs = cast_icon_state(); if (cs == 0) gfx_opacity_push(0.5f); circle(TVX, TOPY, TOPR, cs == 3 ? "ph_cast_on" : "ph_cast", 0, a_tv, 0); /* (connected: the TV-connected icon, nothing else) */ if (cs == 0) gfx_opacity_pop();
      if (cs == 2) { float ph = fmodf((float)app_now(), 1.2f) / 1.2f, rr = TOPR + 4 + 14 * ph; gfx_ring(TVX - rr, TOPY - rr, 2 * rr, 2 * rr, rr, 2.5f, col_alpha(ACC2 | 0xff000000u, 0.7f * (1 - ph))); } } circle(LOCKX, TOPY, TOPR, "lock", 0, a_lock, 0);
    { Item it_; char v_[96]; const Stream *s_; if (player_now(&it_, v_, sizeof v_, &s_) && !strncmp(s_->url, "file:", 5)) { /* playing a download: where the download button would be, a quiet glass capsule saying it plays from the phone (no connection needed); a tap opens Downloads */
        dlMode = 0; const char *lb = T("On this phone"); float th = 2 * TOPR * 0.78f, tw2 = text_width(lb, 24, 700, 0), cw2 = 22 + 30 + 10 + tw2 + 24, cx2 = DLX + TOPR - cw2, cy2 = TOPY - th / 2;
        pglass(cx2, cy2, cw2, th, th / 2, BLACK_A(70), BLACK_A(115), 0, 0); gfx_ring(cx2, cy2, cw2, th, th / 2, 1.5f, WHITE_A(40));
        gfx_circle(cx2 + 22 + 15, TOPY, 15, ACC | 0xff000000u); icon_draw("check", cx2 + 22 + 4, TOPY - 11, 22, WHITE_A(255));
        text_draw(lb, cx2 + 22 + 30 + 10, TOPY - text_line_height(24) / 2.0f, 24, 700, WHITE_A(235), 0, 0, ALIGN_LEFT); phit(cx2, cy2, cw2, th, a_todl, 0, 0); }
      else if (player_now(&it_, v_, sizeof v_, &s_) && !s_->p2p && !strncmp(s_->url, "http", 4) && !live_now()) { float pr_ = 0; int ds = ph_dl_state(it_.id, v_, &pr_); /* the download: this, for later */
        { extern const Dl *ph_dl_find(const char *id, const char *vid); const Dl *d_ = ph_dl_find(it_.id, v_); dlMode = d_ && !strncmp(d_->url, "p2p://", 6) ? 2 : 1; dlReading = !strncmp(s_->url, "http://127.0.0.1", 16); } /* (a torrent's download: a tap opens its card, its pause in it unless the film reads from it; a link's: a tap pauses, a hold opens it, as on Downloads) */
        tween_to(&dlOpenT, dlOpen && ds >= 0 ? 1 : 0, dlOpen ? 0.34f : 0.24f, dlOpen ? EASE_OUT_BACK : EASE_CSS_EASE); tween_tick(&dlOpenT, phDt); if (ds < 0) dlOpen = 0;
        if (dlOpen || dlOpenT.v > 0.001f) dl_card(&it_, v_, ds, pr_);
        else { int going = ds >= 0 && ds != DL_DONE && ds != DL_PAUSED; if (ds == DL_PAUSED) gfx_opacity_push(0.75f); circle(DLX, TOPY, TOPR, going ? "" : ds == DL_DONE ? "ph_check" : "ph_down", 0, a_dl, 0);
          if (going) { float ph = fmodf((float)app_now(), 1.6f) / 1.6f, bob = 4 * sinf(ph * 2 * (float)M_PI), a = 0.8f + 0.2f * cosf(ph * 2 * (float)M_PI), isz = TOPR * 1.1f; /* downloading: its arrow in the accent, easing down and back */
            icon_draw("ph_down", DLX - isz / 2, TOPY - isz / 2 + bob, isz, col_alpha(ACC2 | 0xff000000u, a)); } /* (plain glass as its neighbours: its ring says how far; a lit disc stayed a red blob as the controls faded) */ if (ds == DL_PAUSED) { gfx_opacity_pop(); dl_paused_badge(DLX, TOPY, TOPR); } } /* (paused: its sign dimmed, an amber pause badge; going: lit, its ring filling) */
        if (0) {} } /* (below: its ring) */
      else { dlMode = 0; dlOpen = 0; extern int player_choosing(void); if (player_choosing() && !(player_now(&it_, v_, sizeof v_, &s_) && s_->p2p)) { gfx_opacity_push(0.45f); circle(DLX, TOPY, TOPR, "ph_down", 0, NULL, 0); gfx_opacity_pop(); } } /* the stream still being chosen: the button is there already, quiet, so it never pops in */
      if (dlMode && player_now(&it_, v_, sizeof v_, &s_) && !s_->p2p && !strncmp(s_->url, "http", 4)) { float pr_ = 0; int ds = ph_dl_state(it_.id, v_, &pr_);
        if (ds >= 0 && ds != DL_DONE && pr_ > 0 && dlOpenT.v < 0.05f) { float r = TOPR + 6; for (int q = 0; q <= 60; q++) { if (q > 60 * pr_) break; float an = q * 6 * (float)M_PI / 180; gfx_circle(DLX + r * sinf(an), TOPY - r * cosf(an), 3, ds == DL_PAUSED ? RGB(0xf5, 0xb4, 0x2a) : ACC2 | 0xff000000u); } } } } /* its ring fills as it comes (amber while paused) */
    { Item it_; char v_[96]; const Stream *s_; if (player_now(&it_, v_, sizeof v_, &s_) && s_->p2p) { extern const char *player_p2p_line(void); const char *pl = player_p2p_line(); /* a torrent: its peers and speed, small, under the buttons, at the right */
        static Tween pA; tween_to(&pA, pl[0] ? 1 : 0, 0.3f, EASE_CSS_EASE); tween_tick(&pA, phDt); static char kept[160]; if (pl[0]) snprintf(kept, sizeof kept, "%s", pl);
        if (pA.v > 0.01f && kept[0]) { float th = 2 * TOPR * 0.78f, tw2 = text_width(kept, 24, 700, 0), cw2 = 22 + 12 + 12 + tw2 + 24, cx2 = SB_W - 36 - cw2, cy2 = TOPY + TOPR + 22; float my = cy2 + th / 2; gfx_opacity_push(pA.v);
          pglass(cx2, cy2, cw2, th, th / 2, BLACK_A(70), BLACK_A(115), 0, 0); gfx_ring(cx2, cy2, cw2, th, th / 2, 1.5f, WHITE_A(40));
          float pulse = 0.75f + 0.25f * sinf((float)app_now() * 3); gfx_circle(cx2 + 22 + 6, my, 6, col_alpha(RGB(0x4a, 0xde, 0x80), pulse));
          text_draw(kept, cx2 + 22 + 12 + 12, my - text_line_height(24) / 2.0f, 24, 700, WHITE_A(235), 0, 0, ALIGN_LEFT); gfx_opacity_pop(); } } }
    gfx_opacity_pop(); gfx_offset_pop();
    /* the middle */
    float cx = SB_W / 2, cy = SB_H / 2 - 20, gap = 290; static Tween midA; tween_to(&midA, player_live() ? 1 : 0, 0.3f, EASE_CSS_EASE); tween_tick(&midA, phDt); /* not before the picture runs: the title's logo and its line show the loading */
    int midHits = nhit; gfx_opacity_push(midA.v * (1 - seekK.v)); { float k = 0.86f + 0.14f * a; gfx_scale_push(k, k, cx, cy); }
    extern int player_live_channel(void); int chz = player_live_channel() && live_lineup_n() > 1, lvb = live_now(); /* a channel: the one before and the next one in place of the jumps; another live broadcast: neither */
    if (chz) for (int sd = -1; sd <= 1; sd += 2) { float bx = cx + sd * gap; circle(bx, cy, 64, "", 0, a_zap, sd); icon_draw_rot("p_next", bx - 34, cy - 34, 68, WHITE_A(240), sd < 0 ? 180 : 0); }
    for (int sd = -1; sd <= 1 && !lvb; sd += 2) { float bx = cx + sd * gap, bk = btnDir == sd && btnA.v < 1 ? 1 + 0.14f * sinf(btnA.v * (float)M_PI) : 1; gfx_scale_push(bk, bk, bx, cy); circle(bx, cy, 64, "", 0, a_jump, sd);
      { float r = btnDir == sd && btnA.v < 1 ? sd * 40 * sinf(btnA.v * (float)M_PI) : 0; /* a short turn its way and back */ icon_draw_rot(sd < 0 ? "p_rw" : "p_ff", bx - 46, cy - 46, 92, WHITE_A(240), r); /* the arrow turns a little its way */
        char n[8]; snprintf(n, sizeof n, "%d", seek_step()); text_draw(n, bx, cy - text_line_height(19) / 2.0f + 5, 19, 800, WHITE_A(240), 0, 0, ALIGN_CENTER); } /* the step, inside the arrow */
      gfx_scale_pop();
      float la = btnDir == sd ? clampf((float)(1.1 - (app_now() - btnAt)) / 0.3f, 0, 1) : 0; /* how far, over the button, for a moment */
      if (la > 0) { char t[16]; snprintf(t, sizeof t, "%s%d s", sd < 0 ? "\xe2\x88\x92" : "+", seek_step() * btnJumps); gfx_opacity_push(la); text_draw(t, bx, cy - 64 - 54 - 10 * (1 - clampf(btnA.v * 2, 0, 1)), 32, 800, WHITE_A(250), 0, 0, ALIGN_CENTER); gfx_opacity_pop(); } }
    { int h0 = nhit; gfx_opacity_push(1 - waitA.v); circle(cx, cy, 86, play ? "p_pause" : "p_play", 0, a_play, 0); gfx_opacity_pop(); if (waitA.v > 0.5f) nhit = h0; } /* a wait takes the play button's place */
    gfx_scale_pop(); gfx_opacity_pop(); if (midA.v < 0.5f) nhit = midHits; /* hidden: not tappable either */
    /* the bottom: slides down as it goes */
    gfx_offset_push(0, (1 - a) * 190);
    double d = player_dur(), pt = player_time(); if (heldT >= 0 && (fabs(pt - heldT) < 1.5 || app_now() > heldUntil)) heldT = -1;
    double t = G.mode == 4 || G.mode == 1 ? scrubT : heldT >= 0 ? heldT : pt; /* (no jump back while the seek lands) */ float x0 = barX0(), x1 = barX1(), by = barY(), bw = x1 - x0;
    int lv = live_now(); /* a live broadcast: LIVE in red where the time left was, the bar full, no knob */
    char tl[24], tr[24]; fmt(t, tl, sizeof tl); fmt(d > t ? d - t : 0, tr + 1, sizeof tr - 1); tr[0] = '-'; if (d <= 0) snprintf(tr, sizeof tr, "--:--");
    if (lv) { snprintf(tr, sizeof tr, "%s", T("LIVE")); text_draw(tr, x1 + 24, by - text_line_height(26) / 2.0f + 2, 26, 800, RGB(0xff, 0x4d, 0x4d), 0.5f, 0, ALIGN_START); }
    else { text_draw(tl, x0 - 24, by - text_line_height(26) / 2.0f + 2, 26, 700, WHITE_A(235), 0, 0, ALIGN_END); text_draw(tr, x1 + 24, by - text_line_height(26) / 2.0f + 2, 26, 700, WHITE_A(200), 0, 0, ALIGN_START); }
    float pd = d > 0 ? clampf((float)(t / d), 0, 1) : 0;
    if (lv) { gfx_rrect(x0, by - 5, bw, 10, 5, RGB(0xff, 0x4d, 0x4d)); } else {
    float pb = d > 0 ? (float)(player_buffered() / d) : 0; pb = clampf(pb, 0, 1); float th = G.mode == 4 || G.mode == 1 ? 14 : 10;
    gfx_rrect(x0, by - th / 2, bw, th, th / 2, WHITE_A(55));
    { Item it_; char v_[96]; const Stream *s_; float ph_dl_fraction(const char *id, const char *vid); float fd = player_now(&it_, v_, sizeof v_, &s_) ? ph_dl_fraction(it_.id, v_) : -1; /* a download of it, in order: the part on the phone */
      if (fd > 0 && fd < 1) gfx_rrect(x0, by - th / 2, bw * fd, th, th / 2, col_alpha(ACC2 | 0xff000000u, 0.45f)); }
    gfx_rrect(x0, by - th / 2, bw * pb, th, th / 2, WHITE_A(90)); gfx_rrect(x0, by - th / 2, bw * pd, th, th / 2, ACC);
    if (d > 0) { extern int player_marks(double *s, double *e, int max); double ms[3], me[3]; int nm = player_marks(ms, me, 3); /* the intro, the recap, the credits: marked on the bar */
      for (int q = 0; q < nm; q++) { float mx0 = x0 + bw * (float)clampf((float)(ms[q] / d), 0, 1), mx1 = x0 + bw * (float)clampf((float)(me[q] / d), 0, 1); if (mx1 - mx0 < 6) mx1 = mx0 + 6;
        gfx_rrect(mx0, by - th / 2 - 1, mx1 - mx0, th + 2, (th + 2) / 2, col_alpha(ACC2 | 0xff000000u, 0.55f)); gfx_rect(mx0, by - th / 2 - 5, 2.5f, th + 10, WHITE_A(200)); } }
    float kr = G.mode == 4 || G.mode == 1 ? 24 : 16; gfx_shadow(x0 + bw * pd - kr, by - kr + 3, 2 * kr, 2 * kr, kr, 10, BLACK_A(120)); gfx_circle(x0 + bw * pd, by, kr, WHITE_A(250));
    } /* (not live) */
    if (G.mode == 4 && !lv) { stripT = t; stripFrom = 0; stripDelta = 0; }
    if (G.mode != 4 && G.mode != 1 && !lv && stripT >= 0 && seekK.v > 0.01f) prev_strip(stripT, stripFrom, stripDelta, seekK.v); /* (let go: the strip fades where it was, not gone at once) */
    int stripped = G.mode == 4 && !lv && prev_strip(t, 0, 0, seekK.v > 0.01f ? 1 : 0); /* (the picture in the middle, its neighbours either side) */
    if (G.mode == 4 && !lv && !stripped) { char bt[24]; fmt(t, bt, sizeof bt); float w = text_width(bt, 34, 800, 0) + 44, bx = clampf(x0 + bw * pd - w / 2, 20, SB_W - 20 - w); pglass(bx, by - 112, w, 64, 32, BLACK_A(100), BLACK_A(190), 0, 0); gfx_ring(bx, by - 112, w, 64, 32, 1.5f, WHITE_A(60)); text_draw(bt, bx + w / 2, by - 80 - text_line_height(34) / 2.0f, 34, 800, WHITE_A(250), 0, 0, ALIGN_CENTER); }
    /* the pills */
    gfx_opacity_push(1 - seekK.v);
    { const char *L[7]; const char *I[7]; int K[7], n = 0; int tr2 = player_is_trailer(); /* the next episode first; each says what it holds: the speed as a number, the subtitles' language */
      { extern int player_live_channel(void); if (player_live_channel() && live_catalogs() > 0) { L[n] = T("Channels"); I[n] = "wn_live"; K[n++] = 9; } } /* a channel: the others, to switch */
      if (player_has_next()) { L[n] = NULL; I[n] = "p_next"; K[n++] = 7; }
      if (player_has_eps()) { L[n] = NULL; I[n] = "p_eps"; K[n++] = 1; }
      L[n] = NULL; I[n] = "p_cc/p_spk"; K[n++] = 4; /* subtitles and sound: both inside */
      if (!tr2) { L[n] = NULL; I[n] = "p_src"; K[n++] = 2; }
      static char sp[16]; snprintf(sp, sizeof sp, "%g\xc3\x97", speed); if (!lv) { L[n] = sp; I[n] = NULL; K[n++] = 6; } /* (a live broadcast: no speed) */
      { static char an[64]; const char *m = player_aspect(); snprintf(an, sizeof an, "%s", !strcmp(m, "fill") ? T("Fill the screen") : !strcmp(m, "zoom") ? T("Zoom") : T("Original")); tween_to(&aspK, app_now() - aspAt < 1.6 ? 1 : 0, app_now() - aspAt < 1.6 ? 0.3f : 0.35f, EASE_WEB); tween_tick(&aspK, phDt);
        L[n] = aspK.v > 0.01f ? an : NULL; I[n] = "p_asp"; K[n++] = 8; } /* its name unfolds a moment after a change, then it folds back to its sign */
      float tot = 0, ws[7]; for (int i = 0; i < n; i++) { ws[i] = L[i] ? text_width(L[i], 27, 700, 0) + (I[i] ? icons_w(I[i]) + 12 : 0) + 56 : icons_w(I[i]) + 58; if (K[i] == 8 && L[i]) { float w0 = icons_w(I[i]) + 58; ws[i] = w0 + (ws[i] - w0) * aspK.v; } tot += ws[i] + 14; } tot -= 14; /* (the size pill unfolding: the others make room as it does) */
      float x = SB_W - 40 - tot, y = SB_H - 112; if (x < 30) x = 30; /* pushed right: the stream's specs on the left */
      { extern int player_specs(const char **t, const char **icon, int *bold, int max); const char *st[8], *si[8]; int sb[8], ns = player_specs(st, si, sb, 8); float sx = 44, cy = y + 39, lim = x - 28, fs = 23;
        for (int i = 0; i < ns && sx < lim - 60; i++) { int b = sb[i]; float iw = si[i] ? 34 : 0, tw = text_width(st[i], fs, b ? 800 : 600, b ? 0.5f : 0), w = b ? tw + 22 : tw + iw, gap = i ? (si[i] && !si[i - 1] ? 26 : 20) : 0;
          if (si[i] && !strcmp(st[i], "|")) continue;
          char cut[100]; if (si[i] && !strcmp(si[i], "p_cc")) { snprintf(cut, sizeof cut, "%s", st[i]); char *d = strstr(cut, " \xc2\xb7 "); if (d) *d = 0; st[i] = cut; tw = text_width(st[i], fs, 600, 0); w = tw + iw; } /* the subtitle: its language alone */
          if (i && !si[i]) { text_draw("\xc2\xb7", sx + gap / 2, cy - text_line_height(fs) / 2.0f, fs, 700, WHITE_A(110), 0, 0, ALIGN_CENTER); } sx += gap;
          if (b) { gfx_rrect(sx, cy - 19, w, 38, 9, WHITE_A(30)); gfx_ring(sx, cy - 19, w, 38, 9, 1.5f, WHITE_A(150)); text_draw(st[i], sx + 11, cy - text_line_height(fs) / 2.0f, fs, 800, WHITE_A(245), 0.5f, 0, ALIGN_LEFT); }
          else { if (si[i]) icon_draw(si[i], sx, cy - 14, 28, WHITE_A(200)); float mw = lim - sx - iw; if (mw < 40) break; text_draw(st[i], sx + iw, cy - text_line_height(fs) / 2.0f, fs, 600, WHITE_A(si[i] ? 230 : 200), 0, mw, ALIGN_LEFT); if (tw > mw) tw = mw; w = tw + iw; }
          sx += w; } }
      for (int i = 0; i < n; i++) { int lit = (K[i] == panel) || (K[i] == 4 && panel == 3); PFn fn = K[i] == 7 ? a_next : K[i] == 8 ? a_size : a_panel; { if (K[i] == 8) pillLK = aspK.v; float pw_ = pill(x, y, I[i], L[i], lit || (K[i] == 6 && fabsf(speed - 1) > 0.01f) || (K[i] == 8 && strcmp(player_aspect(), "fit")), fn, K[i]); /* lit too: a speed or a size other than the usual */ if (K[i] >= 0 && K[i] < 10) pillR[K[i]] = rect(x, y, pw_, 78); x += pw_ + 14; } } }
    gfx_opacity_pop();
    gfx_offset_pop(); gfx_opacity_pop(); }
  if (waitA.v * (1 - seekK.v) > 0.01f) { float cx = SB_W / 2, cy = SB_H / 2 - 20; gfx_opacity_push(waitA.v * (1 - seekK.v)); /* (seeking with previews: the picture has the middle) */ gfx_circle(cx, cy, 86, BLACK_A((int)(115 * fmaxf(a, 0.6f)))); spinner(cx, cy, 52); /* (its disc never thinner than without the controls: no gap as they fade in) */ gfx_opacity_pop(); } /* on its own: with the controls or without */
  { void ph_glass_picture_live(const unsigned char *rgba, int w, int h); int frost = panA.v > 0.01f || dlyA.v > 0.01f; /* the glass: the film copied a quarter size as often as the copies come back, while anything of the player's shows */
    if ((a > 0.01f || frost) && player_live()) { void video_snap_ask(int w, int h); int video_snap_take(unsigned char *rgba, int cap, int *w, int *h);
      static unsigned char px[512 * 256 * 4]; int sw = 0, sh = 0; extern float ph_vid_rect[4]; int W = (int)((ph_vid_rect[2] > 1 ? ph_vid_rect[2] : SB_W) / 4), H = (int)((ph_vid_rect[3] > 1 ? ph_vid_rect[3] : SB_H) / 4); if (W > 512) W = 512; if (H > 256) H = 256; /* (the film's own view, a quarter size) */
      video_snap_ask(W, H); /* (one at a time: a new one once the last has come back) */
      if (video_snap_take(px, sizeof px, &sw, &sh)) { ph_glass_picture_live(px, sw, sh); if (frost) ph_glass_picture(px, sw, sh); } } }
  /* swiped down: the picture drops a little and the whole player fades to nothing (its controls, its words, the
     film); let go far enough and it fades the rest of the way, then closes behind the dark; else it comes back */
  { float dt = phDt > 0.05f ? 0.05f : phDt; /* the card's motion: held, it is where the finger put it; let go, home on a spring or on its way out */
    if (cMode == 1 && !G.on) cMode = 2; /* (a finger whose lift was missed: it comes back, never left dark) */
    if (cMode == 2) { const float k = 380, c = 2 * sqrtf(380) * 0.86f; for (int q = 0; q < 4; q++) { float h2 = dt / 4, acc = -k * cF - c * cV; cV += acc * h2; cF += cV * h2; cDX += (0 - cDX) * clampf(h2 * 14, 0, 1); }
      if (cF < 0.0015f && fabsf(cV) < 0.02f) { cF = 0; cV = 0; cDX = 0; cMode = 0; } }
    else if (cMode == 3) { if (cV < 2.4f) cV = 2.4f; cV *= 1 + 2.5f * dt; cF += cV * dt; if (cF >= 1) { cF = 1; cMode = 4; extern int player_from_preview(void); int fromPv = player_from_preview(); if (!fromPv) { video_set_volume(0, 0); sentVol = 0; } /* (silent before it stops; a channel going back into its preview plays on) */ void app_swipe_cover(void); if (!fromPv) app_swipe_cover(); void player_close_swiped(void); player_close_swiped(); return; } } /* (gone: the app's cover over the change, then the page) */
    float e = cF * cF * (3 - 2 * cF); cS = 1 - 0.34f * e; cTY = SB_H * 0.42f * cF * cDir; cTX = cDX * 0.35f; cR = 46 * fminf(cF * 6, 1); /* smaller as it comes down, its corners rounding at once */
    closeShade = clampf((cF - 0.04f) / 0.62f, 0, 1); closeShade = closeShade * closeShade * (3 - 2 * closeShade); /* the film giving way to the app's own colour */
    /* the sound goes with it: held, it dims as the player comes down (to 40 %), back up as it springs home; let go far
       enough, it fades on from there to silence as the player leaves, so nothing is heard once the page is back (the
       film's own stop comes after, unheard) */
    { static int was; if (cMode >= 3 && was < 3) { cF0 = cF; cVol0 = 1 - 0.6f * e; } was = cMode;
      float f = cMode >= 3 ? cVol0 * powf(clampf((1 - cF) / fmaxf(1 - cF0, 0.001f), 0, 1), 1.4f) : 1 - 0.6f * e;
      int want = (int)(100 * vBoost * f + 0.5f); if (cMode >= 4) want = 0; { extern int player_from_preview(void); if (player_from_preview()) want = (int)(100 * vBoost + 0.5f); } /* (a channel shrinking into its preview: its sound stays) */
      if (want != sentVol && (abs(want - sentVol) >= 2 || want == 0 || cMode == 0)) { sentVol = want; video_set_volume(want, 0); } }
    (void)cTX; } /* (the film itself never moves: the app's colour comes over it, app.c) */
  { int landing = G.mode == 4 || G.mode == 1 || app_now() - seekAt < 0.6 || (heldT >= 0 && fabs(player_time() - heldT) > 1.5); /* chosen, sent, or the film not there yet */
    if (seekHeld && !landing && !player_waiting_picture()) { seekHeld = 0; video_play(); }
    if (!landing && !seekHeld) tween_to(&seekDim, 0, 0.3f, EASE_CSS_EASE); tween_tick(&seekDim, phDt); }
  prompt_draw(locked ? 0 : uiA.v * (1 - panA.v)); notice_draw(locked ? 0 : uiA.v * (1 - panA.v)); panel_draw(); delay_card(); pos_card();
  if (G.on && G.mode == 5 && infoRow < 0 && hypotf(G.x - G.x0, G.y - G.y0) < 20) { int onRow = 0; for (int i = 0; i < nholds; i++) if (G.x0 >= holds[i].x && G.x0 < holds[i].x + holds[i].w && G.y0 >= holds[i].y && G.y0 < holds[i].y + holds[i].h) onRow = 1; if (onRow && app_now() - downAt < 0.45) ph_hold_feel((float)((app_now() - downAt - 0.08) / 0.37)); }
  if (G.on && G.mode == 5 && infoRow < 0 && hypotf(G.x - G.x0, G.y - G.y0) < 20 && app_now() - downAt > 0.45) /* a stream held: its details */
    for (int i = nholds - 1; i >= 0; i--) if (G.x0 >= holds[i].x && G.x0 < holds[i].x + holds[i].w && G.y0 >= holds[i].y && G.y0 < holds[i].y + holds[i].h) { infoRow = holds[i].i; tween_to(&infoA, 1, 0.28f, EASE_WEB); G.mode = 8; pplat_haptic_k(HK_POP, 1); dragV->drag = 0; dragV->vel = 0; break; }
  { static int fired; if (G.on && dlHoldAt > 0 && hypotf(G.x - G.x0, G.y - G.y0) < 24) { float hp = (float)((app_now() - dlHoldAt - 0.1) / 0.45); if (hp < 1) ph_hold_feel(hp); /* the download button held: to Downloads */
      else if (!fired) { fired = 1; dlHoldAt = 0; swallowUp = 1; pplat_haptic_k(HK_POP, 1); dlOpen = !dlOpen; keep(); } } else fired = 0; } /* (held: its card opens, held again: folds) */
  if (lockA.v > 0.01f) { float k = lockHold > 0 ? clampf((float)((app_now() - lockHold) / 0.9), 0, 1) : 0; if (lockHold > 0) lockShownAt = app_now();
    gfx_opacity_push(lockA.v); { float sc = 1 + 0.12f * k; gfx_scale_push(sc, sc, LOCKX, TOPY); circle(LOCKX, TOPY, TOPR, "lock", 1, NULL, 0); gfx_scale_pop(); }
    if (k > 0 && k < 1) ph_hold_feel(k); if (k > 0) { float r = TOPR * (1 + 0.12f * k) + 9; for (int q = 0; q <= 60; q++) { if (q > 60 * k) break; float an = q * 6 * (float)M_PI / 180; gfx_circle(LOCKX + r * sinf(an), TOPY - r * cosf(an), 4, WHITE_A(240)); } } /* the ring fills as the finger stays */
    text_draw(k > 0 ? T("Keep holding…") : T("Hold to unlock"), LOCKX - TOPR - 26, TOPY - text_line_height(26) / 2.0f, 26, 700, WHITE_A(235), 0, 0, ALIGN_RIGHT); gfx_opacity_pop();
    if (k >= 1) { lockHold = 0; locked = 0; swallowUp = 1; ui_show(1); pplat_haptic_k(HK_POP, 1); toast(T("Unlocked"), 1200, NULL); } }
  if (bubA.v > 0.01f) { char tt[24], dd[24], line[64]; fmt(scrubT, tt, sizeof tt); fmt(player_dur(), dd, sizeof dd); snprintf(line, sizeof line, "%s / %s", tt, dd); double dt = scrubT - G.scrub0; char dl[24]; fmt(fabs(dt), dl, sizeof dl);
    float w = text_width(line, 44, 800, 0) + 80, h = 120, x = SB_W / 2 - w / 2, y = SB_H * 0.22f; if (G.mode == 1) { stripT = scrubT; stripFrom = G.scrub0; stripDelta = 1; } if (G.mode == 1 && prev_strip(scrubT, G.scrub0, 1, bubA.v)) goto bub_done; /* (a picture: the same strip as the bar's drag, its time and how far under it) */ gfx_opacity_push(bubA.v);
    pglass(x, y, w, h, 30, BLACK_A(120), BLACK_A(175), 0, 0); gfx_ring(x, y, w, h, 30, 1.5f, WHITE_A(44)); text_draw(line, SB_W / 2, y + 18, 44, 800, WHITE_A(250), 0, 0, ALIGN_CENTER);
    char sg[32]; snprintf(sg, sizeof sg, "%s%s", dt < 0 ? "\xe2\x88\x92" : "+", dl); text_draw(sg, SB_W / 2, y + 72, 26, 700, ACC2, 0, 0, ALIGN_CENTER); gfx_opacity_pop();  bub_done:; }
  prev_focus(); /* (the seek previews told where the finger is) */
  lvl_apply(0); /* (each frame: at most every 40 ms) */
  if (lvlA.v > 0.01f) { float h = SB_H * 0.42f, w = 70, x = lvlKind == 2 ? 120 : SB_W - 120 - w, y = SB_H / 2 - h / 2; gfx_opacity_push(lvlA.v);
    float bst = lvlKind == 3 && lvl > 1 ? lvl - 1 : 0; static Tween redT; tween_to(&redT, bst > 0 ? 1 : 0, 0.25f, EASE_CSS_EASE); tween_tick(&redT, phDt);
    pglass(x, y, w, h, w / 2, BLACK_A(96), BLACK_A(150), 0, 0); gfx_ring(x, y, w, h, w / 2, 1.5f, col_mix(WHITE_A(50), RGBA(0xff, 0x4d, 0x4d, 200), redT.v)); float f = (h - 16) * (lvl > 1 ? 1 : lvl); if (f > 0) gfx_rrect(x + 8, y + h - 8 - f, w - 16, f, (w - 16) / 2, col_mix(WHITE_A(235), WHITE_A(120), redT.v));
    if (bst > 0) { float fb = (h - 16) * bst; gfx_rrect(x + 8, y + h - 8 - fb, w - 16, fb, (w - 16) / 2, RGB(0xff, 0x4d, 0x4d)); } /* the boost: red, filling the meter again from the bottom */
    icon_draw(lvlKind == 2 ? "sun" : "p_spk", x + w / 2 - 22, y - 60, 44, col_mix(WHITE_A(230), RGB(0xff, 0x6b, 0x6b), redT.v));
    { char pc[16]; snprintf(pc, sizeof pc, "%d%%", (int)(lvl * 100 + 0.5f)); text_draw(pc, x + w / 2, y + h + 16, 26, 800, col_mix(WHITE_A(235), RGB(0xff, 0x6b, 0x6b), redT.v), 0, 0, ALIGN_CENTER); } gfx_opacity_pop(); }
  if (jumpA.v > 0.01f) { float cx = jumpDir < 0 ? SB_W * 0.2f : SB_W * 0.8f, cy = SB_H / 2, r = 120 + 40 * (1 - jumpA.v); gfx_opacity_push(jumpA.v);
    gfx_circle(cx, cy, r, WHITE_A(36)); char t[16]; snprintf(t, sizeof t, "%s%d s", jumpDir < 0 ? "\xe2\x88\x92" : "+", seek_step() * jumps); text_draw(t, cx, cy - text_line_height(40) / 2.0f, 40, 800, WHITE_A(250), 0, 0, ALIGN_CENTER); gfx_opacity_pop();
    if (app_now() - jumpAt > 0.6) { tween_to(&jumpA, 0, 0.3f, EASE_CSS_EASE); if (jumpA.v < 0.02f) jumps = 0; } }
  { extern int player_failed(void); static Tween hintA; int on = !player_live() && !player_failed() && !locked && app_now() - openAt > 1.2 && G.mode != 10 && !player_live_channel(); /* the loading: how to leave it, softly */
    tween_to(&hintA, on ? 1 : 0, on ? 0.5f : 0.2f, EASE_CSS_EASE); tween_tick(&hintA, phDt);
    if (hintA.v > 0.01f) { const char *ht = T("Swipe down to close"); float t = (float)app_now(), cx = SB_W / 2, y = 30 + 10 * (1 - hintA.v), ph = fmodf(t, 1.8f) / 1.8f; /* top middle, bare: a grabber that keeps pulling down, the words under it */
      float pull = ph < 0.55f ? sinf(ph / 0.55f * (float)M_PI * 0.5f) : 1 - (ph - 0.55f) / 0.45f; pull = pull * pull * (3 - 2 * pull);
      gfx_opacity_push(hintA.v); gfx_rrect(cx - 34, y + 2 + 12 * pull, 68, 7, 3.5f, BLACK_A(60)); gfx_rrect(cx - 34, y + 12 * pull, 68, 7, 3.5f, WHITE_A((int)(150 + 80 * (1 - pull))));
      text_draw(ht, cx + 1, y + 34 + 1, 22, 600, BLACK_A(90), 0.8f, 0, ALIGN_CENTER); text_draw(ht, cx, y + 34, 22, 600, WHITE_A(200), 0.8f, 0, ALIGN_CENTER); gfx_opacity_pop(); } }
  /* (swiped away: the page under comes up through it: app.c draws it over the film as closeShade grows) */
}
int phone_player_tap(float x, float y) { (void)x; (void)y; return 0; }

float phone_subs_alpha(void) { return 1 - closeShade; } /* the subtitles go with the rest */
/* ---- the finger ---- */
/* the seek preview over a scrub's time: its own keyframe sharp, the nearest one made soft, none: the bubble alone */
#include "core/seekprev.h"
/* "Previews ready": said once a video (the scrub it is first seen at 100 %), as the TVs; later scrubs show the pictures alone */
extern int player_ready_seen(const char *vid); extern void player_ready_mark(const char *vid); extern const char *player_vid(void);
static int ready_hidden(int pc) { static double scrubAt = -1; static int done; if (scrubAt != seekStartAt) { scrubAt = seekStartAt; done = pc >= 100 && player_ready_seen(player_vid()); } if (pc >= 100 && !done) player_ready_mark(player_vid()); return done; }
static void prev_badge(float cx, float y, float a) { int pc = sp_progress(); if (pc >= 100 && ready_hidden(pc)) return; if (pc >= 0 && pc >= 90) a *= clampf(1 - (float)(app_now() - seekStartAt - 3.1) / 0.6f, 0, 1); if (pc < 0 || a <= 0.01f) return; char t[48]; if (pc >= 100) snprintf(t, sizeof t, "%s", T("Previews ready")); else { char n[8]; snprintf(n, sizeof n, "%d", pc); snprintf(t, sizeof t, "%s", T1("Previews {n}%", "n", n)); } /* (how much of the film has its pictures, while seeking) */
  float w = text_width(t, 22, 700, 0) + 28, h = 36, x = clampf(cx - w / 2, 20, SB_W - 20 - w); gfx_opacity_push(a); gfx_rrect(x, y, w, h, h / 2, BLACK_A(140)); if (pc < 100) gfx_rrect(x, y + h - 3, w * pc / 100.0f, 3, 1.5f, ACC2 | 0xff000000u); text_draw(t, x + w / 2, y + h / 2 - text_line_height(22) / 2.0f, 22, 700, WHITE_A(pc >= 100 ? 200 : 235), 0, 0, ALIGN_CENTER); gfx_opacity_pop(); }
static void prev_pic(double t, float cx, float bottom, float a) { SpPic p; if (a <= 0.01f || !sp_get(t, &p)) { prev_badge(cx, bottom - 50, a); return; } float w = SB_W > SB_H ? 420 : 360, h = w * p.h / p.w, x = clampf(cx - w / 2, 20, SB_W - 20 - w), y = bottom - h - 14;
  int under = 0; if (y < 20) { y = bottom + 120 + 14; under = 1; } /* (no room above, the slide's bubble near the top: under it) */
  gfx_opacity_push(a); gfx_shadow(x, y + 10, w, h, 16, 34, BLACK_A(150)); gfx_image(p.tex, x, y, w, h, 16, WHITE_A(255), 0, p.v0, p.u1, p.v1); gfx_ring(x, y, w, h, 16, 1, WHITE_A(26)); gfx_opacity_pop(); prev_badge(x + w / 2, under ? y + h + 10 : y - 44, a); } /* (the same brightness whether it is the exact picture or the nearest: no flicker as the finger moves; a hairline edge, a soft shadow) */
/* seeking with previews: the controls step back, the picture in the middle of the screen, its neighbours either side
   smaller and fainter, the time under it (and how far from where it was, sliding); the same for the bar and the slide.
   0: no picture yet (the plain bubble stays) */
static int prev_strip(double t, double from, int showDelta, float a) {
  SpPic m; if (a <= 0.01f || !sp_get_at(t, 1, -1, &m)) return 0; double sl = sp_slot(); float land = SB_W > SB_H, k = ease_apply(EASE_OUT, clampf(seekK.v, 0, 1));
  float cw = land ? fminf(SB_W * 0.34f, 680) : SB_W * 0.64f, ch = cw * m.h / m.w; if (ch > cw * 0.62f) ch = cw * 0.62f; /* (never taller than its 16:9 room) */ float cx = SB_W / 2, cy = SB_H * (land ? 0.40f : 0.36f) + 18 * (1 - k), nw = cw * 0.7f, nh = ch * 0.7f, gap = 30, rc = 30, rn = 24;
  gfx_opacity_push(a);
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(70 * k))); /* (the film a little darker behind: the pictures stand out) */
  for (int sd = -1; sd <= 1; sd += 2) { SpPic n; if (!sp_get_at(m.t + sd * sl * 1.2, sd < 0 ? 0 : 2, m.k, &n)) continue; /* its neighbours: smaller, fainter, sliding out from behind it as it opens */
    float off = (cw / 2 + gap + nw / 2) * (0.75f + 0.25f * k), nx = cx + sd * off - nw / 2, ny = cy - nh / 2; if (nx + nw < 0 || nx > SB_W) continue;
    gfx_opacity_push(0.42f * k); gfx_image(n.tex, nx, ny, nw, nh, rn, WHITE_A(255), 0, n.v0, n.u1, n.v1); gfx_opacity_pop(); }
  float sc = 0.94f + 0.06f * k; gfx_scale_push(sc, sc, cx, cy);
  float x = cx - cw / 2, y = cy - ch / 2; gfx_shadow(x, y + 16, cw, ch, rc, 56, BLACK_A(170)); gfx_image(m.tex, x, y, cw, ch, rc, WHITE_A(255), 0, m.v0, m.u1, m.v1); gfx_ring(x, y, cw, ch, rc, 1.2f, WHITE_A(34));
  gfx_scale_pop();
  char tt[24]; fmt(t, tt, sizeof tt); float ty = y + ch + 20; text_draw(tt, cx, ty, 42, 800, WHITE_A(250), -0.5f, 0, ALIGN_CENTER); ty += text_line_height(42);
  if (showDelta) { double dt = t - from; char dl[24], sg[32]; fmt(fabs(dt), dl, sizeof dl); snprintf(sg, sizeof sg, "%s%s", dt < 0 ? "\xe2\x88\x92" : "+", dl); text_draw(sg, cx, ty + 2, 24, 700, WHITE_A(170), 0, 0, ALIGN_CENTER); ty += text_line_height(24) + 4; }
  { int pc = sp_progress(); if (pc >= 100 && ready_hidden(pc)) pc = -1; float ha = pc >= 0 && pc < 90 ? 1 : clampf(1 - (float)(app_now() - seekStartAt - 3.5) / 0.6f, 0, 1); /* (under 90 %: it stays while the scrub goes on; from 90 % it fades after a moment; it always leaves with the strip) */ if (pc >= 0 && ha > 0.01f) { /* how much of the film has its pictures: "Previews 64%", a ring filling beside it, "Previews ready" once all are made; a moment as a seek starts */
      char n[48], pn[8]; snprintf(pn, sizeof pn, "%d", pc); snprintf(n, sizeof n, "%s", pc >= 100 ? T("Previews ready") : T1("Previews {n}%", "n", pn)); float tw = text_width(n, 28, 700, 0), r = 13, w = 2 * r + 12 + tw + 36, h = 50, gx = cx - w / 2, gy = ty + 12; gfx_opacity_push(ha);
      gfx_rrect(gx, gy, w, h, h / 2, BLACK_A(120)); gfx_ring(gx, gy, w, h, h / 2, 1, WHITE_A(30)); float rx = gx + 18, ry = gy + h / 2;
      gfx_ring(rx, ry - r, 2 * r, 2 * r, r, 3, WHITE_A(45)); int seg = 48; for (int q = 0; q < seg * pc / 100; q++) { float an = -1.5708f + 6.2832f * q / seg; gfx_circle(rx + r + cosf(an) * (r - 1.5f), ry + sinf(an) * (r - 1.5f), 1.8f, ACC2 | 0xff000000u); }
      text_draw(n, rx + 2 * r + 12, ry - text_line_height(28) / 2.0f, 28, 700, WHITE_A(235), 0, 0, ALIGN_LEFT); gfx_opacity_pop(); } }
  gfx_opacity_pop(); return 1; }
static void prev_focus(void) { static double last = -9; static int lastMode; int on = G.mode == 4 || G.mode == 1; double f = on ? scrubT : -1; /* (told only as it moves: the threads woken a few times a second, not every frame) */
  if (on != (lastMode != 0) || (on && fabs(f - last) > 0.4) || (on && G.mode != lastMode)) { sp_focus(f, G.mode == 1); last = f; lastMode = on ? G.mode : 0; } }
static int can_seek(void) { extern int player_failed(void); return player_live() && !player_failed(); } /* a seek only once the film is up: never while its loading card shows */
static void seek_here(double t) { if (!can_seek()) return; hold_for_seek(); player_seek_to(t); seekAt = app_now(); heldT = t; heldUntil = app_now() + 5; }
static void pinch_set(const char *m) { extern void player_set_aspect(const char *m); if (!*m) return; int changed = strcmp(m, player_aspect()) != 0; player_set_aspect(m); { void video_size_mode(const char *mode); video_size_mode(m); }
  if (changed) { toast(!strcmp(m, "fill") ? T("Fill the screen") : !strcmp(m, "zoom") ? T("Zoom") : T("Original"), 900, NULL); pplat_haptic(); } }
float phone_subs_room(void) { float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f, px = SB_W - pw - 20, k = clampf(panA.v, 0, 1), full = SB_W - 80, free = px - 60; return full + (free - full) * k; } /* the width the subtitles may take: the screen, or the room left of an open tray */
void phone_subs_ass_fit(float *k, float *ox) { float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f, px = SB_W - pw - 20, t = clampf(panA.v, 0, 1); *k = 1 + ((px - 60) / SB_W - 1) * t; *ox = 40 * t; } /* the tray open: a styled frame shrunk whole into the room left of it (its signs keep their places in it, no line cut) */
float phone_subs_dx(void) { float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f, px = SB_W - pw - 20, k = clampf(panA.v, 0, 1); return (px / 2 - SB_W / 2) * k; } /* the tray open: the subtitles (and the style's sample) centred in the room left of it, so a style change is seen */
static int in_panel(float x, float y) { if (panA.v < 0.5f) return 0; float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f; return x >= SB_W - pw - 20 && y >= 20 && y <= SB_H - 20; }
static int on_bar(float x, float y) { return ui_on() && !panel && !live_now() && fabsf(y - barY()) < 54 && x >= barX0() - 40 && x <= barX1() + 40; }
int phone_player_finger(const SDL_Event *e) {
  float x = e->tfinger.x * SB_W, y = e->tfinger.y * SB_H; double now = e->tfinger.timestamp / 1000.0;
  if (G.on && e->tfinger.fingerId != Z.id) { /* another finger: a pinch (its own events) */
    if (e->type == SDL_FINGERDOWN && !Z.two && !locked && !panel && player_live()) { Z.two = 1; Z.id2 = e->tfinger.fingerId; Z.x2 = x; Z.y2 = y; Z.d0 = hypotf(Z.x2 - G.x, Z.y2 - G.y); if (Z.d0 < 40) Z.d0 = 40;
      if (G.mode == 1) tween_to(&bubA, 0, 0.2f, EASE_CSS_EASE); if (G.mode == 2 || G.mode == 3) tween_to(&lvlA, 0, 0.2f, EASE_CSS_EASE); G.mode = 6; return 1; }
    if (Z.two && e->tfinger.fingerId == Z.id2) {
      if (e->type == SDL_FINGERMOTION) { Z.x2 = x; Z.y2 = y; void video_pinch(float k); video_pinch(hypotf(Z.x2 - G.x, Z.y2 - G.y) / Z.d0); }
      else if (e->type == SDL_FINGERUP) { Z.two = 0; char m[16]; void video_pinch_end(float k, char *out, size_t cap); video_pinch_end(hypotf(x - G.x, y - G.y) / Z.d0, m, sizeof m); pinch_set(m); } }
    return 1; }
  if (e->type == SDL_FINGERDOWN) { dlHoldAt = ui_on() && !locked && dlMode == 1 && hypotf(e->tfinger.x * SB_W - DLX, e->tfinger.y * SB_H - TOPY) < TOPR + 20 ? app_now() : 0; if (dlOpen) { float fx = e->tfinger.x * SB_W, fy = e->tfinger.y * SB_H; if (!(fx >= dcX && fx < dcX + dcW && fy >= dcY && fy < dcY + dcH)) { dlOpen = 0; dlHoldAt = 0; swallowUp = 1; memset(&G, 0, sizeof G); return 1; } } /* open: a tap away folds it (and only that) */ memset(&G, 0, sizeof G); Z.id = e->tfinger.fingerId; Z.two = 0; pressX = x; pressY = y; tween_to(&pressA, 1, 0.08f, EASE_OUT);
    if (posA.to > 0.5f) { G.on = 1; G.mode = 21; G.x0 = G.x = G.lx = x; G.y0 = G.y = G.ly = y; G.v0 = player_sub_pos(); posAt = app_now(); return 1; } /* the Position card open: a finger anywhere on the picture moves them */
    if (dlyR.w > 0 && dlyA.to > 0.5f && x >= dlyR.x && x < dlyR.x + dlyR.w && y >= dlyR.y && y < dlyR.y + dlyR.h + 20) { G.on = 1; G.mode = 9; G.x0 = G.x = G.lx = x; G.y0 = G.y = G.ly = y; dlyAt = app_now(); return 1; }
    if (locked) { lockHold = hypotf(x - LOCKX, y - TOPY) < TOPR + 30 && lockA.v > 0.5f ? app_now() : 0; if (!lockHold) lockShownAt = app_now(); if (lockHold) pplat_haptic(); } G.on = 1; G.x0 = G.x = G.lx = x; G.y0 = G.y = G.ly = y; G.t0 = G.lt = now; keep();
    if (!locked && on_bar(x, y) && can_seek()) { G.mode = 4; hold_for_seek(); double d = player_dur(); scrubT = (float)(d > 0 ? clampf((x - barX0()) / (barX1() - barX0()), 0, 1) * d : 0); }
    else if (!locked && in_panel(x, y) && TD.on != 3 && y < 20 + 120 && !(x > SB_W - 20 - 72 - 60)) { G.mode = 12; } /* the tray's title: taken any way once the finger moves (the x stays a tap) */
    else if (!locked && in_panel(x, y) && panel == 9 && chTabY > 0 && y >= chTabY && y < chTabY + 66) { G.mode = 31; tabV.drag = 1; tabV.vel = 0; downAt = app_now(); } /* the channels' pills: sideways */
    else if (!locked && in_panel(x, y)) { G.mode = 5; dragV = splitX > 0 && x > splitX ? &panV2 : &panV; dragV->drag = 1; dragV->vel = 0; downAt = app_now(); }
    return 1; }
  if (!G.on) return 1;
  if (e->type == SDL_FINGERMOTION) { float dx = x - G.x0, dy = y - G.y0; G.x = x; G.y = y; if (hypotf(dx, dy) > 24) tween_to(&pressA, 0, 0.12f, EASE_CSS_EASE); if (locked) return 1;
    if (G.mode == 8) return 1;
    if (G.mode == 31) { float d = G.lx - x; if (tabV.pos < 0 || tabV.pos > tabV.max) d *= 0.4f; tabV.pos += d; float dt = (float)(now - G.lt); if (dt > 0.001f) G.vel = G.vel * 0.3f + d / dt * 0.7f; G.lx = x; G.ly = y; G.lt = now; return 1; }
    if (G.mode == 30) { G.lx = x; G.ly = y; G.lt = now; return 1; } /* a channel swiped sideways: decided as it is let go */
    if (G.mode == 5 && TD.on != 3 && dx > 22 && dx > fabsf(dy) * 1.3f) { dragV->drag = 0; dragV->vel = 0; G.mode = 12; } /* the list pushed sideways: the tray is taken instead */
    if (G.mode == 12 && hypotf(dx, dy) > 14) { G.mode = 11; int caught = TD.on == 2; TD.bx = caught ? TD.ox : 0; TD.by = caught ? TD.oy : 0; /* caught on its way home: from where it is */
      TD.gx = G.x0 - TD.bx; TD.gy = G.y0 - TD.by; TD.on = 1; TD.vx = TD.vy = 0; G.x0 = x; G.y0 = y; dx = dy = 0; pplat_haptic_k(HK_TICK, 0.35f); } /* (from here: no jump by the distance that started it) */
    if (G.mode == 11) { float rx = rubber(TD.bx + dx, 140), ry = rubber(TD.by + dy, 140), dt = (float)(now - G.lt); float nx = rx, ny = ry; /* it hugs the finger: right and down freely, up and left against a pull */
      if (dt > 0.001f) { TD.vx = TD.vx * 0.35f + (nx - TD.ox) / dt * 0.65f; TD.vy = TD.vy * 0.35f + (ny - TD.oy) / dt * 0.65f; } TD.ox = nx; TD.oy = ny; G.lx = x; G.ly = y; G.lt = now; return 1; }
    if (G.mode == 10) { float d = (y - G.y0) * cDir / (SB_H * 0.8f), f = d >= 0 ? d : d * 0.15f, dt = (float)(now - G.lt); if (f > 1) f = 1 + (f - 1) * 0.2f; if (f < -0.03f) f = -0.03f; if (f > 1.05f) f = 1.05f; /* one to one with the finger (a little give past the ends) */
      if (dt > 0.001f) cV = cV * 0.35f + ((f - cF) / dt) * 0.65f; cF = f; cDX = x - G.x0; G.ly = y; G.lt = now; return 1; }
    if (G.mode == 21 && posMode) { posAt = app_now(); return 1; } /* (the size bar: taps only) */
    if (G.mode == 21) { float nv = G.v0 + (G.y0 - y) / (SB_H * 0.45f) * 28.0f; nv = roundf(nv < 2 ? 2 : nv > 30 ? 30 : nv); if (nv != roundf(player_sub_pos())) { player_sub_pos_set(nv); posSwiped = 1; pplat_haptic_k(HK_TICK, 0.35f); } posAt = app_now(); return 1; } /* up and down: a percent per step (the whole range in about half the screen's height) */ /* up and down: a percent per tick */
    if (G.mode == 9) { float dy = y - G.y0; dlyDY = dy < 0 ? dy : dy * 0.15f; dlyAt = app_now(); return 1; } /* the card follows the finger up */
    if (G.mode == 6) { if (Z.two) { void video_pinch(float k); video_pinch(hypotf(Z.x2 - G.x, Z.y2 - G.y) / Z.d0); } return 1; }
    if (G.mode == 4) { double d = player_dur(); scrubT = (float)(d > 0 ? clampf((x - barX0()) / (barX1() - barX0()), 0, 1) * d : 0); keep(); }
    else if (G.mode == 5) { float d = G.ly - y; if (dragV->pos < 0 || dragV->pos > dragV->max) d *= 0.4f; dragV->pos += d; float dt = (float)(now - G.lt); if (dt > 0.001f) G.vel = G.vel * 0.3f + d / dt * 0.7f; }
    else if (!G.mode && hypotf(dx, dy) > 24 && !panel) {
      { extern int player_live_channel(void); if (fabsf(dx) > fabsf(dy) && player_live_channel() && player_live() && live_lineup_n() > 1) { G.mode = 30; G.lx = x; G.lt = now; return 1; } } /* a channel: sideways goes to the next or the one before */
      if (fabsf(dx) > fabsf(dy) && !can_seek()) G.mode = 8; /* (loading: a sideways slide does nothing) */
      else if (fabsf(dx) > fabsf(dy)) { G.mode = 1; hold_for_seek(); G.scrub0 = player_time(); scrubT = (float)G.scrub0; tween_to(&bubA, 1, 0.15f, EASE_CSS_EASE); } /* (the controls stay as they were: hidden, the bubble alone says where) */
      else if (dy < 0 && G.x0 > SB_W * 0.25f && G.x0 < SB_W * 0.75f) G.mode = 8; /* up from the middle: nothing (only a swipe down closes the player) */
      else if (dy > 0 && (!player_live() || (G.x0 > SB_W * 0.25f && G.x0 < SB_W * 0.75f))) { { /* down closes it (loading: from anywhere; playing: from the middle, the sides being brightness and sound) */ cDir = 1; G.mode = 10; cMode = 1; cV = 0; G.y0 = y; G.lt = now; pplat_haptic_k(HK_TICK, 0.3f); } } /* from the middle (loading: anywhere), downwards: the player as a card under the finger */
      else { G.mode = G.x0 < SB_W / 2 ? 2 : 3; lvlKind = G.mode; G.v0 = G.mode == 2 ? pplat_brightness(-1) : vBoost > 1.001f ? vBoost : pplat_volume(-1); lvl = G.v0; tween_to(&lvlA, 1, 0.15f, EASE_CSS_EASE); } }
    if (G.mode == 1) { double d = player_dur(), span = d > 0 ? (d < 600 ? d : 600) : 0; scrubT = (float)(G.scrub0 + dx / SB_W * span); if (scrubT < 0) scrubT = 0; if (d > 0 && scrubT > d) scrubT = (float)d; }
    else if (G.mode == 2) { lvl = clampf(G.v0 - dy / (SB_H * 0.6f), 0, 1); lvlDirty = 1; } /* (the meter follows at once; the screen's brightness is set a few times a second: each set lays the window out again) */
    else if (G.mode == 3) { float was = lvl; lvl = clampf(G.v0 - dy / (SB_H * 0.6f), 0, 2); /* past the top: the boost, on the same meter */
      if (lvl <= 1) { if (vBoost > 1) { vBoost = 1; video_set_volume(100, 0); sentVol = 100; } lvlDirty = 1; } else { if (was <= 1) { pplat_volume(1); pplat_haptic_k(HK_CLICK, 1); } vBoost = lvl; video_set_volume((int)(100 * lvl + 0.5f), 0); sentVol = (int)(100 * lvl + 0.5f); } }
    G.lx = x; G.ly = y; G.lt = now; return 1; }
  if (e->type == SDL_FINGERUP) { G.on = 0; lockHold = 0; tween_to(&pressA, 0, 0.2f, EASE_CSS_EASE); if (swallowUp) { swallowUp = 0; G.mode = 0; return 1; } int moved = hypotf(x - G.x0, y - G.y0) > 24;
    if (G.mode == 10) { G.mode = 0; if (now - G.lt > 0.09) cV = 0; float proj = cF + cV * 0.22f; /* where the throw would carry it */
      if (proj > 0.34f && cF > 0.04f) { cMode = 3; pplat_haptic_k(HK_CLICK, 0.6f); } else cMode = 2; return 1; } /* far enough: on its way out; else home on a spring */ /* far enough (or flung): it fades out and closes; else it comes back */ /* all the way (or flung): closed; else it settles back */
    if (G.mode == 8) { G.mode = 0; return 1; }
    if (G.mode == 11) { G.mode = 0; float pw = SB_W * 0.52f > 1200 ? 1200 : SB_W * 0.52f; if (now - G.lt > 0.08) { TD.vx = TD.vy = 0; } /* (held still before letting go: no throw) */
      float px2 = TD.ox + TD.vx * 0.18f, py2 = TD.oy + TD.vy * 0.18f; /* where the throw would carry it */
      if (px2 > pw * 0.36f || py2 > SB_H * 0.28f) { int kind = panel ? panel : shownKind; TD.on = 2; a_back(0, 0); tween_to(&panA, 0, 0.42f, EASE_WEB); /* far enough: it goes home into its pill, shrinking and rounding as it goes (the controls coming back with it), the pill taking it with a little bounce */
        pillPulseK = kind == 3 ? 4 : kind; pillPulseAt = app_now() + 0.3; pplat_haptic_k(HK_CLICK, 0.6f); }
      else { TD.on = 2; } return 1; } /* else home on a spring */
    if (G.mode == 12) G.mode = 0; /* a tap on the title row */
    if (G.mode == 21) { G.mode = 0; G.on = 0; posAt = app_now(); if (hypotf(x - G.x0, y - G.y0) < 16 && x >= posX.x && x < posX.x + posX.w && y >= posX.y && y < posX.y + posX.h) a_posclose(0, 0); else if (posMode && hypotf(x - G.x0, y - G.y0) < 24) for (int i = 0; i < 7; i++) if (x >= szR[i].x && x < szR[i].x + szR[i].w && y >= szR[i].y && y < szR[i].y + szR[i].h) a_style(0, i); return 1; } /* (a tap on ✕: it closes; nothing else does) */
    if (G.mode == 9) { G.mode = 0; float dy = y - G.y0; if (dy < -50) { a_dlyclose(0, 0); dlyDY = 0; tween_set(&dlyA, 0); return 1; } /* swiped up: back to the film */
      if (hypotf(x - G.x0, dy) < 24) phit_at(x, y); return 1; } /* a tap on its buttons, whatever the controls are doing */ /* the hold that showed a stream's details */
    if (G.mode == 6) { if (Z.two) { Z.two = 0; char m[16]; void video_pinch_end(float k, char *out, size_t cap); video_pinch_end(hypotf(Z.x2 - x, Z.y2 - y) / Z.d0, m, sizeof m); pinch_set(m); } G.mode = 0; return 1; }
    if (G.mode == 4) { seek_here(sp_snap(scrubT)); G.mode = 0; keep(); return 1; } /* (lands on the picture shown, its keyframe: libVLC's fast seek goes straight there) */
    if (G.mode == 31) { tabV.drag = 0; tabV.vel = now - G.lt > 0.08 ? 0 : G.vel; G.mode = 0; if (moved) return 1; }
    if (G.mode == 30) { G.mode = 0; float dx = x - G.x0, v = (float)(now - G.lt) < 0.08f && now > G.t0 ? dx / (float)(now - G.t0) : 0; if (fabsf(dx) > SB_W * 0.12f || fabsf(v) > 900) { live_zap(dx < 0 ? 1 : -1); pplat_haptic_k(HK_CLICK, 0.7f); } return 1; } /* far or fast enough: right to left the next one */
    if (G.mode == 5) { dragV->drag = 0; dragV->vel = now - G.lt > 0.08 ? 0 : G.vel; G.mode = 0; if (moved) return 1; }
    if (G.mode == 1) { seek_here(sp_snap(scrubT)); tween_to(&bubA, 0, 0.25f, EASE_CSS_EASE); G.mode = 0; keep(); return 1; }
    if (G.mode >= 2) { lvl_apply(1); tween_to(&lvlA, 0, 0.4f, EASE_CSS_EASE); G.mode = 0; return 1; }
    if (moved) return 1;
    { extern int player_failed(void); if (!player_live() && !player_failed() && !panel) return 1; } /* the first loading: nothing to show yet (Back still leaves) */
    if (locked) { lockHold = 0; return 1; }
    for (int q = 0; q < promptN; q++) if (x >= promptR[q].x && x < promptR[q].x + promptR[q].w && y >= promptR[q].y && y < promptR[q].y + promptR[q].h) { player_prompt_go(promptN == 2 ? 1 - q : 0); pplat_haptic(); return 1; } /* (only a hold on the lock opens it) */
    if ((ui_on() || panel) && phit_at(x, y)) return 1;
    if (panel) { a_back(0, 0); return 1; } /* a tap beside the panel puts it away */
    { extern int player_failed(void); extern void player_try_next(void); if (player_failed()) { player_try_next(); return 1; } }
    /* a double tap on a side: 10 s that way (taps that follow add 10 more) */
    static double lastUp; static float lastX; int side = x < SB_W * 0.35f ? -1 : x > SB_W * 0.65f ? 1 : 0;
    { if (live_now()) side = 0; } /* (live: nothing to jump) */
    if (side && can_seek() && now - lastUp < 0.3 && (lastX < SB_W * 0.35f ? -1 : lastX > SB_W * 0.65f ? 1 : 0) == side) {
      hold_for_seek(); player_seek_dir(side); seekAt = app_now(); jumpDir = side; jumps = jumps && jumpDir == side ? jumps + 1 : 1; jumpAt = app_now(); tween_to(&jumpA, 1, 0.12f, EASE_OUT); lastUp = 0; return 1; }
    lastUp = now; lastX = x;
    ui_show(!ui_on()); return 1; }
  return 1;
}
/* the Channels panel (phone/live_phone.c answers the live hub's calls with it) */
void phone_player_channels(void) { if (panel != 9) { ui_show(1); a_panel(9, 0); } }
void phone_player_panel_close(void) { if (panel) a_back(0, 0); }
void phone_player_list_top(void) { panV.pos = 0; panV.vel = 0; }
/* Back (the phone's): a panel first, then the player */
int phone_player_back(void) { if (locked) { lockShownAt = app_now(); return 1; } if (panel) { a_back(0, 0); return 1; } return 0; }
float phone_player_close_k(void) { return closeShade; }
/* the card's place now (for app.c: the room around it, its corners, the controls moved with it): 0 when not pulled */
float phone_player_close_geo(float *tx, float *ty, float *sc, float *r) { *tx = cTX; *ty = cTY; *sc = cS; *r = cR; return cF; }
int phone_player_leaving(void) { return cMode >= 3; } /* (let go far enough: on its way out) */
int phone_player_close_dir(void) { return cDir; }
int phone_player_closing(void) { extern int player_from_preview(void); return cF > 0.0005f && player_from_preview(); } /* (the player fades to nothing: no page drawn under it) */ /* the page under it shows while it is swiped away */
void phone_player_reset(void) { { void pplat_fullscreen(int on); pplat_fullscreen(1); } /* sideways from the first frame: the loading screen too, never upright first */ { extern int phPlayerLayout; void phone_relayout(void); phPlayerLayout = 1; phone_relayout(); } /* (opening sideways already: the player's rows at once) */ seekHeld = 0; tween_set(&seekDim, 0); closeShade = 0; cF = cV = cDX = cTX = cTY = cR = 0; cS = 1; cMode = 0; sentVol = -1; /* (a new film: its sound full again, whatever the last close left it at) */ video_drag(0, 0, 1); void pplat_fullscreen(int on); pplat_fullscreen(1); { void video_size_mode(const char *mode); video_size_mode(player_aspect()); } /* sideways as the player opens: its loading too (a trailer is found before it loads) */ locked = 0; panel = 0; tween_set(&panA, 0); ui_show(0); speed = 1; vBoost = 1; video_set_volume(100, 0); memset(&TD, 0, sizeof TD); openAt = app_now(); }
int phone_player_ui(void) { return !locked && uiA.to > 0.5f; } /* the bar and pills are up: the subtitles step above them */
