/* The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user. */
/* The shell: the layout's shape, the finger (a tap is the pointer's click, a finger that rests is OK held, a
   swipe scrolls the region under it with momentum), the bar of tabs, the sheets, and the play hook that sends
   a stream to the connected TV. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "phone/phone.h"
#include "ui/img.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "screens/player.h"

float sb_layout_w = 720, sb_layout_h = 1600, INS_T, INS_B, KB_H, phDt = 1 / 60.0f;
static float pxPerUnit = 1; static double insAt;
#include "core/video.h"
#include "core/p2p.h"
#include "core/store.h"
#include "core/json.h"
#include "core/stremio.h"
static int lastDw, lastDh; int phPlayerLayout; /* the player is up: its own sideways rows */
int ph_tabw(void) { return !phPlayerLayout && SB_W > SB_H && SB_H >= 950; } /* a tablet (or an unfolded phone) held sideways: the pages laid out as a hub; a phone sideways is never this tall */
static double turnAt; /* the player opened on an upright screen: the phone is turning sideways */
int phone_player_turning(void) { return phPlayerLayout && lastDh > lastDw && app_now() - turnAt < 1.2; } /* (until the turn lands: the page stays, the player never drawn upright; a screen that never turns: shown anyway after 1.2 s) */
void phone_relayout(void) { if (phPlayerLayout && lastDh > lastDw) turnAt = app_now(); if (lastDw > 0) { phone_layout(lastDw, lastDh); gfx_resize(lastDw, lastDh); } } /* the player opened or closed: its own layout or the pages', and the drawing's scale with it (the screen's shape may not change, so no resize comes to do it: the pages drawn at the player's scale, small in a corner, touches off) */
static float colX; static int colOn; /* (a sheet in its column: below) */
#define FULLW (SB_W + (colOn ? 2 * colX : 0)) /* the whole screen's width, a sheet in its column or not */
void phone_layout(int dw, int dh) { lastDw = dw; lastDh = dh;
  if (dw > dh && !phPlayerLayout) { /* sideways, the pages: the same size of things as upright (1.75 units per dp), more room across */
    float d = pplat_density(); pxPerUnit = d > 0 ? d / 1.75f : 1; if (pxPerUnit < 0.5f) pxPerUnit = 0.5f;
    { float uh = dh / pxPerUnit; if (uh > TABW_H) { float th = TABW_H + (uh - TABW_H) * 0.4f; pxPerUnit = dh / th; } } /* a tablet held sideways: things grow with the screen (held further off than a phone), 7" as it was, a 10" a fifth bigger, a 13" a third */
    sb_layout_w = floorf(dw / pxPerUnit); sb_layout_h = floorf(dh / pxPerUnit); }
  else if (dw > dh) { sb_layout_h = 900; sb_layout_w = floorf(900.0f * dw / dh); pxPerUnit = dh / 900.0f; } /* sideways (a film): the TV player's layout, in 900 rows (a fifth bigger than the TV's 1080: a phone is held close but it is small) */
  else { /* upright: 1.75 units per dp, so every phone shows things at the same physical size (720 on a 411 dp phone); a
            tablet or an unfolded phone gets more room, not bigger things */
    float d = pplat_density(); float dp = d > 0 ? dw / d : 411; sb_layout_w = floorf(clampf(dp * 1.75f, 640, 1800)); pxPerUnit = dw / sb_layout_w; sb_layout_h = floorf(dh / pxPerUnit); }
  float t, b; pplat_insets(&t, &b); INS_T = t / pxPerUnit; INS_B = b / pxPerUnit; insAt = app_now();
  video_set_surface(dw, dh); extern void player_layout_changed(void); player_layout_changed(); /* the picture placed for the new shape (the plane reads it in the new pixels) */
  fprintf(stderr, "[phone] layout %.0fx%.0f for %dx%d, bars %.0f / %.0f\n", sb_layout_w, sb_layout_h, dw, dh, INS_T, INS_B);
}

/* ---- the finger ---- */
#define HOLD_AFTER 0.28
typedef struct { float x, y, w, h; Scroll *s; int axis, pull; } DragR; static int dragFloor[2]; /* (the first drag a sheet asked for this frame: with a sheet open, nothing under it moves) */
static DragR drags[2][64]; static int nDrag[2], cur;
extern int phOut;
int phGhostGlass; /* a page drawn under another whose own glass was grabbed for it (Settings' list): real glass */
int phGhost; /* a page drawn as a picture only (under one being swiped away): it moves nothing, takes no finger */
static void scr_rect(float *x, float *y, float *w, float *h) { float x1 = *x + *w, y1 = *y + *h; gfx_to_screen(x, y); gfx_to_screen(&x1, &y1); *w = x1 - *x; *h = y1 - *y; if (colOn && *x <= colX + 1 && *x + *w >= colX + SB_W - 1) { *x = 0; *w = FULLW; } } /* (a page's whole width in a column: the whole screen still scrolls it) */ /* (a box where it lands: a sheet in its column, a page moved) */
void ph_drag(float x, float y, float w, float h, Scroll *s, int axis) { if (phGhost) return; scr_rect(&x, &y, &w, &h); if (!phOut && nDrag[cur] < 64) drags[cur][nDrag[cur]++] = (DragR){ x, y, w, h, s, axis, 0 }; }
void ph_drag_pull(float x, float y, float w, float h, Scroll *s) { scr_rect(&x, &y, &w, &h); if (!phOut && nDrag[cur] < 64) drags[cur][nDrag[cur]++] = (DragR){ x, y, w, h, s, 1, 1 }; }
typedef struct { float x, y, w, h; int n; float cx[8], cw[8]; HitFn set; } ScrubR;
static ScrubR scrubs[2][4]; static int nScrub[2];
void ph_scrub(float x, float y, float w, float h, int n, const float *cx, const float *cw, HitFn set) { if (nScrub[cur] >= 4 || n <= 0) return; ScrubR *r = &scrubs[cur][nScrub[cur]++]; r->x = x; r->y = y; r->w = w; r->h = h; r->n = n > 8 ? 8 : n; for (int i = 0; i < r->n; i++) { r->cx[i] = cx[i]; r->cw[i] = cw[i]; } r->set = set; }
static int inside(float px, float py, float x, float y, float w, float h);
static int scrub_cell(const ScrubR *r, float x) { int best = 0; float bd = 1e9f; for (int i = 0; i < r->n; i++) { float m = r->cx[i] + r->cw[i] / 2, d = x > m ? x - m : m - x; if (d < bd) { bd = d; best = i; } } return best; }
static int inside(float px, float py, float x, float y, float w, float h) { return px >= x && py >= y && px < x + w && py < y + h; }
static struct { int edge, tabs; int down, drag, pressed; float x0, y0, x, y, lx, ly; double t0, lt; float vel; Scroll *s; int axis; float hp[8]; double ht[8]; int hn; ScrubR sc; int scOn, scCell; Scroll *caught; } F; /* caught: a moving list stopped by this finger (the gesture not decided yet) */
void phone_tap_point(float *x, float *y);
static Timer1 flyT; static char flyPic[512]; static float flyX, flyY; static int flySlot; static double flyPulse = -9;
/* the bubble waits for its round copy of the poster (made from the poster already there: a moment), at most 0.4 s, so
   it never flies empty */
static double flyWaitAt;
void ph_fly(const char *poster, int slot) { snprintf(flyPic, sizeof flyPic, "%s", poster ? poster : ""); phone_tap_point(&flyX, &flyY); flySlot = slot;
  fprintf(stderr, "[fly] poster %s\n", !flyPic[0] ? "none" : img_get_shaped(flyPic, 160, 160, 80, 2) ? "ready" : "waiting");
  if (flyPic[0] && !img_get_shaped(flyPic, 160, 160, 80, 2)) { flyWaitAt = app_now(); flyT.on = 0; } else { flyWaitAt = 0; timer_start(&flyT, 0.7f); } }
/* the round poster the bubble carries, made while the page or sheet that can send it is open: ready before the tap (made at the tap, it often missed the wait) */
void ph_fly_ready(const Item *it) { char pb[600]; const char *u = sb_poster_of(it, pb, sizeof pb); if (u && *u) img_get_shaped(u, 160, 160, 80, 2); }
static void fly_wait(void) { if (!flyWaitAt) return; if (img_get_shaped(flyPic, 160, 160, 80, 2) || img_shaped_failed(flyPic, 160, 160, 80, 2) || app_now() - flyWaitAt > 0.4) { fprintf(stderr, "[fly] waited %.2f s: %s\n", app_now() - flyWaitAt, img_get_shaped(flyPic, 160, 160, 80, 2) ? "in" : "not in"); flyWaitAt = 0; timer_start(&flyT, 0.7f); } }
static float backK; static Tween backT; static int backGoing; static float backVel; static float backFX, backFY, backLx, backLy, backLk = 1; static Tween backGo; /* a swipe back: how far, its settling, going, the finger's speed */
static const PhSheet *sheet; /* the sheet up */
static const Scr STRIP[4] = { SCR_SET, SCR_HOME, SCR_LIB, SCR_DISC };
static float tabDx; static Tween tabT; static int tabGoing, tabTo = -1; static float tabVel;
static int strip_at(Scr s) { for (int i = 0; i < 4; i++) if (STRIP[i] == s) return i; return -1; }
/* the finger's speed from its last 80 ms of movement (each event's own time: several come within one frame) */
static void hist_add(float p, double t) { int i = F.hn % 8; F.hp[i] = p; F.ht[i] = t; F.hn++; }
static float hist_vel(double now) { int n = F.hn < 8 ? F.hn : 8; if (n < 2) return 0; int last = (F.hn - 1) % 8; double t1 = F.ht[last]; float p1 = F.hp[last]; if (now - t1 > 0.06) return 0;
  for (int k = 1; k < n; k++) { int i = (F.hn - 1 - k) % 8; if (t1 - F.ht[i] >= 0.08 || k == n - 1) { double dt = t1 - F.ht[i]; return dt > 0.005 ? (float)((p1 - F.hp[i]) / dt) : 0; } } return 0; }
/* a hold's feel: soft ticks growing as it fills (with the swell drawn), nothing once it has fired (its pop is the firer's) */
static double feelAt; static int feelStage;
void ph_hold_feel(float prog) { if (app_now() - feelAt > 0.12) feelStage = 0; feelAt = app_now(); if (prog <= 0.02f) { feelStage = 0; return; }
  static const float at[3] = { 0.12f, 0.45f, 0.78f }, sc[3] = { 0.22f, 0.38f, 0.6f }; while (feelStage < 3 && prog >= at[feelStage]) { pplat_haptic_k(HK_TICK, sc[feelStage]); feelStage++; } }
static Tween holdK; static float holdVel, holdLastX, holdRx = -1, holdRy = -1; static int holdOn;
/* held on a row of choices (still a moment, or sliding): that row's highlight swells, lifts and follows the finger;
   the other rows stay as they are */
static float pillLight(float want) { static float v; float k = clampf(phDt * (want > v ? 14 : 6), 0, 1); v += (want - v) * k; return v; } /* (the pill's light eases in and fades out: never gone at once as the finger starts to slide) */
static int under_bar(float x, float y, float h);
static float held_k(float x, float y, float w, float h) { return inside(holdRx + 1, holdRy + 1, x, y, w, h) && !under_bar(x, y, h) ? holdK.v : 0; } /* (held on the bar: a page's row reaching under it is not held) */
static void hold_tick(void) {
  int was = holdOn; holdOn = F.down && F.scOn && (F.drag == 3 || (!F.drag && app_now() - F.t0 > 0.16));
  if (holdOn && !was) { holdRx = F.sc.x; holdRy = F.sc.y; pplat_haptic_k(HK_TICK, 0.45f); } /* a short tick: the hold has started */
  tween_to(&holdK, holdOn ? 1 : 0, holdOn ? 0.2f : 0.32f, holdOn ? EASE_OUT_BACK : EASE_WEB); tween_tick(&holdK, phDt);
  float v = phDt > 0 ? (F.x - holdLastX) / phDt : 0; holdLastX = F.x; holdVel += ((holdOn ? v : 0) - holdVel) * (phDt * 10 > 1 ? 1 : phDt * 10);
}
static int pressStale; static float barTop = 1e9f; /* (last frame's) */
/* a press under the bar: only the bar's own things (wholly inside its band) take it */
static int barDrawing, sheetDrawing, sheetShown; /* (a sheet open: only what it draws reacts to a finger) */ /* (the bar, its row of choices and the mini player being drawn: theirs the presses that start on them) */
static int under_bar(float x, float y, float h) { (void)x; (void)y; (void)h; if (sheetShown && !sheetDrawing) return 1; return F.y0 >= barTop && !barDrawing && !sheet && !app_overlay; } /* (a sheet open: the page and the bar under it never light up under the finger) */ /* (started on the bar: nothing of the page under it is pressed or held, even a card wholly inside its band) */
int ph_pressed(float x, float y, float w, float h) { scr_rect(&x, &y, &w, &h); return F.down && !F.drag && !pressStale && !under_bar(x, y, h) && app_now() - F.t0 > 0.03 && inside(F.x0, F.y0, x, y, w, h); }
int ph_dragging(void) { return F.down && F.drag; }
static float relX = -1, relY = -1; static double relAt;
void phone_tap_point(float *x, float *y) { if (relX >= 0) { *x = relX; *y = relY; } else { *x = SB_W / 2; *y = SB_H / 2; } }
/* a page leaving by Back: drawn once more, shrinking toward where it was opened and fading */
int phOut;
void phone_out(void (*draw)(void), float k) {
  extern void app_nav_origin(float *x, float *y); float ox, oy; app_nav_origin(&ox, &oy); float e = ease_apply(EASE_CSS_EASEOUT, k), sc = 1 - 0.14f * e;
  phOut = 1; gfx_opacity_push(1 - e); gfx_scale_push(sc, sc, ox, oy); draw(); gfx_scale_pop(); gfx_opacity_pop(); phOut = 0;
}
/* the press: only the thing the finger came down on, never what shows up under it afterwards (a menu opened by a
   hold, the next page, a row that moved in): the pressed thing is the smallest box asking while the finger is down,
   and only that same box fades after the lift */
static SbRect pressR, pressNow; static int pressFrame = -1; static int frameNo; unsigned app_frame_no(void) { return (unsigned)frameNo; }
void ph_press_cancel(void) { pressStale = 1; relAt = -10; pressR.w = 0; } /* new things under a finger still down: none of them pressed */
float press_k(float x, float y, float w, float h) { scr_rect(&x, &y, &w, &h);
  if (F.down && !F.drag && !pressStale && !under_bar(x, y, h) && inside(F.x0, F.y0, x, y, w, h)) {
    if (pressFrame != frameNo) { pressFrame = frameNo; pressR = pressNow; pressNow = rect(x, y, w, h); } else if (w * h < pressNow.w * pressNow.h) pressNow = rect(x, y, w, h);
    float t = (float)(app_now() - F.t0) / 0.08f; return t > 1 ? 1 : t; }
  if (!F.down && pressR.w > 0 && fabsf(x - pressR.x) < 2 && fabsf(y - pressR.y) < 2 && fabsf(w - pressR.w) < 2 && fabsf(h - pressR.h) < 2) { float t = (float)(app_now() - relAt) / 0.3f; if (t < 1) return (1 - t) * (1 - t); }
  return 0;
}
/* where the free room ends at the bottom: above the bar (and its row of choices when it shows), above the player's controls */
float phone_free_bottom(void) { if (player_open()) return SB_H - 210; if (SCR == SCR_LOGIN || !app_splash_done) return SB_H - INS_B - 40; return barTop < SB_H ? barTop : SB_H - INS_B - 130; }
/* the finger now, for a grid that is held and slid across (Appearance's colours): where, where it touched, whether it
   rests long enough to be a hold (then the page no longer scrolls under it) */
int ph_finger(float *x, float *y, float *x0, float *y0, int *held) { *x = F.x; *y = F.y; *x0 = F.x0; *y0 = F.y0; *held = F.pressed && !F.drag; return F.down; }
/* right to left (Arabic): every page is drawn mirrored about the middle, its words and pictures kept their way round; the player keeps its own way */
int ph_rtl_mirror(void) { return i18n_rtl() && !player_open(); }
int ph_settings_direct(void); /* (Settings opened at a page from elsewhere: the swipe back goes there) */
static Screen sheetHost; /* (below) */
void phone_finger(const SDL_Event *e, int w, int h) { (void)w; (void)h; { extern void app_touch(void); app_touch(); } /* (a finger is activity: what waits for a still screen waits for it too) */
  float x = e->tfinger.x * SB_W, y = e->tfinger.y * SB_H; double now = e->tfinger.timestamp / 1000.0; /* the event's own time (the frame's clock is the same for every event in it) */
  if (ph_rtl_mirror()) x = SB_W - x; /* (right to left: the pages are drawn mirrored, the finger is read the same way) */
  if (e->type == SDL_FINGERDOWN) { if (F.down) return; pressStale = 0; pressR.w = 0; pressNow.w = 0; pressFrame = -1; memset(&F, 0, sizeof F); F.down = 1; F.x0 = F.x = F.lx = x; F.y0 = F.y = F.ly = y; F.t0 = app_now(); F.lt = now;
    if (player_open() && app_overlay != &sheetHost) { extern int phone_player_finger(const SDL_Event *e); phone_player_finger(e); F.down = 0; return; } /* the player has its own touch (src/phone/player_phone.c) */
    { extern int app_can_back(void); F.edge = !sheet && !app_overlay && !backGoing && app_can_back() && SCR != SCR_HOME && SCR != SCR_DISC && SCR != SCR_LIB && SCR != SCR_LOGIN && SCR != SCR_SET; (void)ph_settings_direct; F.tabs = !sheet && !app_overlay && !tabGoing && strip_at(SCR) >= 0 && y < SB_H - INS_B - BAR_H - 100; /* (not on the bar or its choices) */ } /* from the left edge of an inner page: maybe a swipe back */
    { int p = cur ^ 1; for (int i = nScrub[p] - 1; i >= 0; i--) if (inside(x, y, scrubs[p][i].x, scrubs[p][i].y, scrubs[p][i].w, scrubs[p][i].h)) { F.sc = scrubs[p][i]; F.scOn = 1; F.scCell = -1; break; } } /* held on a row of choices: sliding picks */
    /* a moving list stops under the finger */
    int p = cur ^ 1; for (int i = nDrag[p] - 1; i >= (sheet ? dragFloor[p] : 0); i--) if (inside(x, y, drags[p][i].x, drags[p][i].y, drags[p][i].w, drags[p][i].h) && fabsf(drags[p][i].s->vel) > 30) { drags[p][i].s->vel = 0; drags[p][i].s->drag = 1; F.caught = drags[p][i].s; break; } /* (held still; which way the finger goes decides: a scroll, a page swipe, a swipe back) */
    return; }
  if (player_open() && app_overlay != &sheetHost) { extern int phone_player_finger(const SDL_Event *e); phone_player_finger(e); return; }
  if (!F.down) return;
  if (e->type == SDL_FINGERMOTION) { F.x = x; F.y = y;
    if (F.tabs && !F.drag && hypotf(x - F.x0, y - F.y0) > 12) { int side = 0, p = cur ^ 1; for (int i = 0; i < nDrag[p]; i++) if (drags[p][i].axis == 0 && inside(F.x0, F.y0, drags[p][i].x, drags[p][i].y, drags[p][i].w, drags[p][i].h)) side = 1;
      int i = strip_at(SCR), j = x < F.x0 ? i + 1 : i - 1; if (!side && fabsf(x - F.x0) > fabsf(y - F.y0) * 1.3f && j >= 0 && j < 4) { F.drag = 8; tabVel = 0; if (F.caught) { F.caught->drag = 0; F.caught = NULL; } } else F.tabs = 0; } /* sideways where no row scrolls sideways: the next page */
    if (F.drag == 8) { int i = strip_at(SCR); float dx = x - F.x0; if ((dx < 0 && i == 3) || (dx > 0 && i == 0)) dx *= 0.25f; tabDx = dx; double dt = now - F.lt; if (dt > 0.001) tabVel = tabVel * 0.4f + (float)((x - F.lx) / dt) * 0.6f; F.lx = x; F.ly = y; F.lt = now; return; }
    if (F.edge && !F.drag && hypotf(x - F.x0, y - F.y0) > 9) { int side = 0, p = cur ^ 1; for (int i = 0; i < nDrag[p]; i++) if (drags[p][i].axis == 0 && inside(F.x0, F.y0, drags[p][i].x, drags[p][i].y, drags[p][i].w, drags[p][i].h)) side = 1;
      if (x - F.x0 > 0 && fabsf(x - F.x0) > fabsf(y - F.y0) * 1.2f && (!side || F.x0 < SB_W * 0.12f)) { F.drag = 7; backVel = 0; if (F.caught) { F.caught->drag = 0; F.caught = NULL; } } else F.edge = 0; } /* sideways to the right, where no row scrolls sideways (or from the edge): the page goes */ /* the page goes with the finger */
    if (F.drag == 7) { backFX = x; backFY = y; backK = clampf(hypotf(x - F.x0, y - F.y0) / SB_W, 0, 1); double dt = now - F.lt; if (dt > 0.001) backVel = backVel * 0.4f + (float)((x - F.lx) / SB_W / dt) * 0.6f; F.lx = x; F.ly = y; F.lt = now; return; }
    if (F.scOn && (F.drag == 3 || (!F.drag && fabsf(x - F.x0) > 10))) { F.drag = 3; int c = scrub_cell(&F.sc, x); if (c != F.scCell) { if (F.scCell >= 0 || c != scrub_cell(&F.sc, F.x0)) F.sc.set(c, 0); F.scCell = c; } F.lx = x; F.ly = y; F.lt = now; return; }
    if (!F.drag && !F.pressed && hypotf(x - F.x0, y - F.y0) > 12) { int axis = fabsf(x - F.x0) > fabsf(y - F.y0) ? 0 : 1; int p = cur ^ 1; F.drag = 1;
      for (int pass = 0; pass < 2 && !F.s; pass++) for (int i = nDrag[p] - 1; i >= (sheet ? dragFloor[p] : 0); i--) { DragR *d = &drags[p][i]; if ((pass == 0 ? d->axis == axis : 1) && inside(F.x0, F.y0, d->x, d->y, d->w, d->h)) { F.s = d->s; F.axis = d->axis; break; } }
      if (F.s && F.axis == 1 && y > F.y0 && F.s->pos <= 0.5f) for (int i = 0; i < nDrag[p]; i++) { DragR *d = &drags[p][i]; if (d->pull && d->s != F.s && inside(F.x0, F.y0, d->x, d->y, d->w, d->h)) { F.s = d->s; break; } } /* a list at its top pulled down: the sheet goes with the finger */
      if (F.caught && F.caught != F.s) F.caught->drag = 0; F.caught = NULL; /* (another list, or none: the stopped one is free again) */
      if (F.s) { F.s->drag = 1; F.s->vel = 0; F.lx = F.x0; F.ly = F.y0; hist_add(F.axis ? F.y0 : F.x0, F.lt); } } /* from where the finger went down: the content keeps up with it */
    if (F.s) { float d = F.axis ? F.ly - y : F.lx - x; Scroll *s = F.s; if (!s->free && (s->pos < 0 || s->pos > s->max)) d *= 0.4f; s->pos += d; hist_add(F.axis ? y : x, now); }
    else if (F.pressed) app_pointer_move(x, y);
    F.lx = x; F.ly = y; F.lt = now; return; }
  if (e->type == SDL_FINGERUP) { F.down = 0;
    if (F.drag == 8) { int i = strip_at(SCR), j = tabDx < 0 ? i + 1 : i - 1; tween_set(&tabT, tabDx);
      if (j >= 0 && j < 4 && (fabsf(tabDx) > SB_W * 0.3f || (fabsf(tabVel) > 900 && (tabVel < 0) == (tabDx < 0)))) { tabTo = STRIP[j]; tabGoing = 1; tween_to(&tabT, tabDx < 0 ? -SB_W : SB_W, 0.24f, EASE_OUT); }
      else { tabTo = -1; tabGoing = 1; tween_to(&tabT, 0, 0.28f, EASE_WEB); } return; } /* far enough or flung: the next page; else back in place */
    if (F.drag == 7) { tween_set(&backT, backK); if (backK > 0.28f || (backVel > 1.1f && backK > 0.05f)) { backGoing = 1; accent_scope(NULL, NULL, NULL); /* the page it goes back to has its own colour: it starts fading in now, not after */ tween_set(&backGo, 0); tween_to(&backGo, 1, 0.26f, EASE_WEB); } else tween_to(&backT, 0, 0.3f, EASE_WEB); return; } /* far enough or flung: into where it came from; else it settles back */ /* far enough or flung: gone; else back in place */
    if (F.caught) { F.caught->drag = 0; F.caught = NULL; if (!F.drag) { F.pressed = 0; return; } } /* a touch that only stopped a moving list: nothing under it is pressed */
    if (F.s) { hist_add(F.axis ? y : x, now); F.s->drag = 0; F.s->vel = -hist_vel(now); F.s = NULL; return; } /* a fling: the content carries on at the finger's speed */
    if (F.pressed) { app_pointer_button(0, x, y); F.pressed = 0; return; }
    if (F.drag) return;
    relX = F.x0; relY = F.y0; relAt = app_now(); if (pressFrame >= 0) pressR = pressNow; /* the box it pressed: only it fades */
    app_pointer_move(x, y); app_pointer_button(1, x, y); app_pointer_button(0, x, y); }
}
void scroll_to(Scroll *s, float to) { s->vel = 0; if (fabsf(s->pos - to) <= 0.5f) { s->pos = to; s->toTop = 0; return; } s->toTop = 1; s->from = s->pos; s->to = to; s->at = app_now(); }
void scroll_top(Scroll *s) { scroll_to(s, 0); }
void scroll_tick(Scroll *s) { if (phGhost) return; float dt = phDt;
  if (s->max < 0) s->max = 0; if (s->drag) { s->toTop = 0; return; } /* (a finger takes it from there) */
  if (s->toTop) { float k = (float)((app_now() - s->at) / 0.5); float to = s->to > s->max ? s->max : s->to < 0 ? 0 : s->to; if (k >= 1) { s->toTop = 0; s->pos = to; } else { float e = k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; s->pos = s->from + (to - s->from) * e; } s->vel = 0; return; } /* a smooth glide to a place (the top, a category), 0.5 s */
  if (fabsf(s->vel) > 8) { s->pos += s->vel * dt; s->vel *= expf(-dt * 2.1f); } else s->vel = 0; /* a long, soft glide (iOS-like) */
  float t = s->pos < 0 ? 0 : s->pos > s->max ? s->max : s->pos;
  if (t != s->pos) { s->vel *= expf(-dt * 22); float k = dt * 12 > 1 ? 1 : dt * 12; s->pos += (t - s->pos) * k; if (fabsf(t - s->pos) < 0.5f) s->pos = t; } /* past an end: it stretches, then springs back */
}
/* the keyboard: asked for again on every tap of a field (it may have been put away by the phone's own Back, which the
   app never hears); its height read while it moves, so the field rides on top of it; put away by the phone, the
   field lets go of it too */
static int kbOn; static double kbAt; static int kbSeen;
void (*phKeyboardGone)(void);
void phone_keyboard(int on) { if (on && kbOn && KB_H > 0 && SDL_IsTextInputActive()) return; /* already up: not again */ if (on) { if (SDL_IsTextInputActive()) SDL_StopTextInput(); SDL_StartTextInput(); kbAt = app_now(); kbSeen = 0; } else if (kbOn) SDL_StopTextInput(); kbOn = on; }
static void keyboard_tick(void) {
  static int n; if (!kbOn && KB_H <= 0.5f && (n++ % 30)) return; /* still: now and then only */
  float px = pplat_keyboard(), u = px / pxPerUnit; KB_H += (u - KB_H) * (phDt * 22 > 1 ? 1 : phDt * 22); if (fabsf(u - KB_H) < 0.5f) KB_H = u;
  if (kbOn && px > 0) kbSeen = 1;
  if (kbOn && kbSeen && px <= 0 && app_now() - kbAt > 0.5) { kbOn = 0; SDL_StopTextInput(); if (phKeyboardGone) phKeyboardGone(); }
}

/* ---- sheets ---- */
static const PhSheet *closing, *queued; static Tween sheetA; /* queued: the next sheet, opened once the one going has gone */ static Scroll pull;
static int host_key(int k) { if (sheet && sheet->key && sheet->key(k)) return 1; if (k == K_BACK) { ph_sheet_close(); return 1; } return 1; }
static void host_tick(float dt) { tween_tick(&sheetA, dt);
  if (sheet && !pull.drag && (pull.pos < -170 || (pull.pos < -30 && pull.vel < -900))) ph_sheet_close(); /* let go far enough down, or flicked down: it goes */
  if (sheet) scroll_tick(&pull);
  if (!sheet && sheetA.v <= 0.001f) { sheetShown = 0; if (closing && closing->gone) closing->gone(); closing = NULL; if (app_overlay && app_overlay->key == host_key) app_overlay = NULL;
    if (queued) { const PhSheet *q = queued; queued = NULL; ph_sheet_open(q); } } }
/* sideways or on a tablet: a sheet keeps a phone's width, in the middle (its dim, its touch shield and the glass it reads
   still the whole screen) */
static int colLift; /* the column lifted for a moment (a dim, a grab of the whole screen): nests inside whatever was pushed since */
static void col_begin(void) { if (!colOn && colX > 0) { colOn = 1; colLift = 0; sb_layout_w -= 2 * colX; gfx_offset_push(colX, 0); } }
static void col_end(void) { if (colOn) { colOn = 0; gfx_offset_pop(); sb_layout_w += 2 * colX; } colX = 0; }
static void col_off(void) { if (colOn) { colOn = 0; colLift = 1; gfx_offset_push(-colX, 0); sb_layout_w += 2 * colX; } }
static void col_on(void) { if (colLift) { colLift = 0; colOn = 1; gfx_offset_pop(); sb_layout_w -= 2 * colX; } }
void phone_col_push(float maxW) { if (colOn || SB_W <= maxW + 40) return; colX = (SB_W - maxW) / 2; col_begin(); } /* a page kept to a readable width in the middle (sideways, a tablet) */
void phone_col_pop(void) { col_end(); }
static void host_draw(void) { const PhSheet *s = sheet ? sheet : closing; sheetShown = s && sheetA.v > 0.001f; if (!sheetShown) return; float cw = SB_W > 900 ? 760 : SB_W; if (s->wide > 0 && SB_W > SB_H && SB_W > 900) cw = fminf(SB_W - 40, s->wide); if (s->wide < 0 && SB_W > 900) cw = fminf(SB_W - 40, -s->wide); /* (wide < 0: a small sheet, no wider than that on a wide screen) */ colX = (SB_W - cw) / 2; col_begin(); sheetDrawing = 1; s->draw(sheetA.v); sheetDrawing = 0; col_end(); }
static int host_origin(float *x, float *y) { *x = SB_W / 2; *y = SB_H; return 1; }
static Screen sheetHost = { NULL, NULL, host_key, host_tick, host_draw, NULL, NULL, NULL, host_origin, NULL };
void ph_sheet_open(const PhSheet *s) { if (F.down) ph_press_cancel(); if (app_overlay && app_overlay != &sheetHost) return;
  if (!sheet && closing && closing != s && sheetA.v > 0.001f) { queued = s; return; } /* another sheet still going: this one rises after it */ queued = NULL; closing = NULL; sheet = s; app_overlay = &sheetHost; memset(&pull, 0, sizeof pull); pull.free = 1; tween_to(&sheetA, 1, 0.34f, EASE_WEB); phone_keyboard(0); }
void ph_sheet_close(void) { if (!sheet) { queued = NULL; return; } closing = sheet; sheet = NULL; pull.drag = 0; pull.vel = 0; tween_to(&sheetA, 0, 0.24f, EASE_CSS_EASE); }
int ph_sheet_on(void) { return sheet != NULL || queued != NULL; }
static void dim_act(int a, int b) { (void)a; (void)b; ph_sheet_close(); }
static void glass_grab(float top); static void lglass(float x, float y, float w, float h, float r, uint32_t tint); static void lglass_old(float x, float y, float w, float h, float r, uint32_t tint); static void glass_uv(float x, float y, float w, float h, float *u0, float *v0, float *u1, float *v1);
/* swiping back: from the left edge of an inner page, the page follows the finger off to the right; let go past a third
   (or flung) and it goes, else it settles back */
/* the pages side by side: Settings, Home, Library, Downloads; a sideways swipe where nothing scrolls sideways moves
   between them, the page following the finger, its neighbour coming in beside it */
float phone_tab_dx(int *nb) { int i = strip_at(SCR); float dx = F.down && F.drag == 8 ? tabDx : tabT.v; *nb = -1; if (i < 0 || fabsf(dx) < 0.5f) return 0;
  int j = dx < 0 ? i + 1 : i - 1; if (j >= 0 && j < 4) *nb = STRIP[j]; return dx; }
static void tab_tick(void) { { int i = strip_at(SCR); if (i >= 0 && SCR != SCR_HOME && ((i > 0 && STRIP[i - 1] == SCR_HOME) || (i < 3 && STRIP[i + 1] == SCR_HOME))) { void ph_home_keep(void); ph_home_keep(); } } /* (Home beside: its spotlight's picture kept ready) */ tween_tick(&tabT, phDt); if (tabGoing && fabsf(tabT.v - tabT.to) < 0.5f) { extern int phoneSwipeBack; tabGoing = 0; tween_set(&tabT, 0); if (tabTo >= 0) { phoneSwipeBack = 1; app_go((Scr)tabTo, NULL, 1); } tabTo = -1; } }
float phone_back_k(void) { return F.down && F.drag == 7 ? backK : backT.v; }
/* the page as a card under the finger: how big (k) and moved by (tx, ty), the spot first touched staying under it;
   gone > 0: let go far enough, shrinking toward where the page was opened from (ox, oy) */
void phone_back_card(float *k, float *tx, float *ty, float *alpha, float ox, float oy) {
  float b = phone_back_k(); float e = 1 - (1 - b) * (1 - b); float kk = 1 - 0.32f * e, cx = SB_W / 2, cy = SB_H / 2;
  if (F.down && F.drag == 7) { backLx = (backFX - cx) - kk * (F.x0 - cx); backLy = (backFY - cy) - kk * (F.y0 - cy); backLk = kk; *k = kk; *tx = backLx; *ty = backLy; *alpha = 1; return; }
  if (backGoing) { float g = backGo.v, k1 = 0.18f; *k = backLk + (k1 - backLk) * g; *tx = backLx + ((ox - cx) - backLx) * g; *ty = backLy + ((oy - cy) - backLy) * g; *alpha = 1 - g * g; return; } /* into where it came from */
  float q = backLk < 1 ? b / (1 - backLk + 0.0001f) * 0.32f : 0; q = b > 0 ? clampf(b / clampf(backK, 0.001f, 1), 0, 1) : 0; *k = 1 - (1 - backLk) * q; *tx = backLx * q; *ty = backLy * q; *alpha = 1; /* settling back */
}
static void back_tick(void) { tween_tick(&backT, phDt); tween_tick(&backGo, phDt); if (backGoing && backGo.v > 0.995f) { extern int phoneSwipeBack; backGoing = 0; phoneSwipeBack = 1; tween_set(&backT, 0); app_back(); } }
/* the panel: a card of glass floating over the page, its handle at the top; it rises in, follows the finger down */
double ph_last_press(void) { return F.t0; } /* when the last (or current) finger touched */
/* a sheet that draws its own card (one shape, its own ground): the dim, the pull, the touch shield; no glass of ours
   under it (whose rim would show round its card). Gives its top; its card is the same place (12 in, h - INS_B + 10 tall) */
int ph_sheet_shown(void) { return sheet || (closing && sheetA.v > 0.001f); } /* (a sheet drawn this frame: open, or still going) */
float ph_sheet_pulled(void) { return (sheet || sheetA.v > 0.001f) && pull.pos < 0 ? -pull.pos : 0; } /* (also while it goes: the frame it is let go) */ /* (how far the open sheet is pulled down by the finger) */
float sheet_panel_bare(float h, float a) {
  float m = 12, e = 1 - powf(1 - a, 3), pulled = pull.pos < 0 ? -pull.pos : 0, ch = h - INS_B + 10, y = SB_H - INS_B - m - ch * e + pulled + (1 - e) * (INS_B + m);
  float dimK = a * (1 - (pulled / (ch + 1)) * 0.8f); { int c = colOn; col_off(); gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(130 * dimK))); hit_act(0, 0, SB_W, SB_H, dim_act, 0, 0); if (c) col_on(); }
  hit_shield(m, y, SB_W - 2 * m, ch); dragFloor[cur] = nDrag[cur]; ph_drag_pull(0, 0, SB_W, SB_H, &pull); ph_drag_pull(m, y, SB_W - 2 * m, ch, &pull); return y; } /* (a finger anywhere while it is open moves the sheet, never the page under it) */
float sheet_panel(float h, float a) {
  float m = 12, e = 1 - powf(1 - a, 3), pulled = pull.pos < 0 ? -pull.pos : 0, ch = h - INS_B + 10, y = SB_H - INS_B - m - ch * e + pulled + (1 - e) * (INS_B + m);
  float dimK = a * (1 - (pulled / (ch + 1)) * 0.8f); { int c = colOn; col_off(); gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(130 * dimK))); hit_act(0, 0, SB_W, SB_H, dim_act, 0, 0); glass_grab(y > 0 ? y : 0); if (c) col_on(); } lglass_old(m, y, SB_W - 2 * m, ch, 44, RGBA(20, 18, 28, 196)); hit_shield(m, y, SB_W - 2 * m, ch);
  dragFloor[cur] = nDrag[cur]; ph_drag_pull(0, 0, SB_W, SB_H, &pull); ph_drag_pull(m, y, SB_W - 2 * m, ch, &pull);
  gfx_rrect(SB_W / 2 - 30, y + 14, 60, 6, 3, WHITE_A(70));
  return y;
}
/* a list of choices */
static struct { char title[128]; char l[14][128]; int n, cur, wt[14]; void (*pick)(int); Scroll v; } CH; /* wt: a row's own weight (a font's name in that font), 0 the list's */
static void ch_act(int i, int b) { (void)b; ph_sheet_close(); if (CH.pick) CH.pick(i); }
static void ch_draw(float a) {
  float rowH = 92, listH = CH.n * rowH, maxList = SB_H * 0.6f; if (listH > maxList) listH = maxList; float h = 116 + listH + INS_B + 24, y = sheet_panel(h, a), x0 = 12 + 20, xw = SB_W - 2 * x0;
  text_draw(CH.title, SB_W / 2, y + 40, F_BODY + 2, 800, TXT, -0.2f, SB_W - 2 * PAD, ALIGN_CENTER);
  float top = y + 100; gfx_clip_push(0, top, SB_W, listH); CH.v.max = CH.n * rowH - listH; scroll_tick(&CH.v); ph_drag(0, top, SB_W, listH, &CH.v, 1);
  gfx_rrect(x0, top - CH.v.pos, xw, CH.n * rowH, 26, WHITE_A(10)); /* the list's own group, as on a phone's settings */
  for (int i = 0; i < CH.n; i++) { float ry = top + i * rowH - CH.v.pos; if (ry + rowH < top || ry > top + listH) continue; int on = i == CH.cur;
    { float pk = press_k(x0, ry, xw, rowH); if (pk > 0) gfx_rrect(x0, ry, xw, rowH, i == 0 || i == CH.n - 1 ? 26 : 0, WHITE_A((int)(22 * pk))); }
    if (i) gfx_rect(x0 + 28, ry, xw - 28, 1, WHITE_A(16));
    text_draw(CH.l[i], x0 + 28, ry + rowH / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, CH.wt[i] ? CH.wt[i] : on ? 700 : 500, on ? TXT : WHITE_A(215), 0, xw - 110, ALIGN_START);
    if (on) icon_draw("ph_check", x0 + xw - 64, ry + rowH / 2 - 20, 40, ACC2);
    hit_act(x0, ry, xw, rowH, ch_act, i, 0); }
  gfx_clip_pop();
}
static const PhSheet chSheet = { ch_draw, NULL, NULL };
void choices_open(const char *title, const char **lines, int n, int cur, void (*pick)(int i)) {
  snprintf(CH.title, sizeof CH.title, "%s", title); CH.n = n > 14 ? 14 : n; for (int i = 0; i < CH.n; i++) snprintf(CH.l[i], sizeof CH.l[i], "%s", lines[i]); CH.cur = cur; CH.pick = pick; memset(&CH.v, 0, sizeof CH.v); memset(CH.wt, 0, sizeof CH.wt); ph_sheet_open(&chSheet); }
void choices_weights(const int *wt, int n) { for (int i = 0; i < n && i < 14; i++) CH.wt[i] = wt[i]; } /* (after choices_open: each row in its own font) */
int choices_up(void) { return sheet == &chSheet; }

/* ---- the bar at the bottom: the profile (or Back) on the left, the three places in a glass pill, Search and
   the TV on the right; a row of the page's own choices floats just above it and slides away while the page
   scrolls down. Everything a thumb needs is down here. ---- */
static const Scr TABS[3] = { SCR_HOME, SCR_LIB, SCR_DISC }; /* Home, Library, Downloads (Downloads lives in the screen slot Discover had) */
/* the pill's four places: Home, Library, Downloads, and Search (a layer over the page it is opened on) */
extern int ph_search_on(void); extern void ph_search_open(void), ph_search_close(void);
static int pill_at(void) { if (ph_search_on()) return 3; for (int i = 0; i < 3; i++) if (SCR == TABS[i]) return i; return -1; }
static void tab_act(int i, int b) { (void)b;
  if (i == 3) { ph_sheet_close(); ph_search_open(); return; } /* (again: its field and keyboard, as before) */
  if (ph_search_on()) { ph_search_close(); if (SCR != TABS[i]) { ph_sheet_close(); app_go(TABS[i], NULL, 1); } return; } /* (from search: the place, as it was) */
  if (SCR == TABS[i]) { if (screens[SCR]->key) screens[SCR]->key(K_CH_UP); return; } /* the tab again: back to its top */
  ph_sheet_close(); app_go(TABS[i], NULL, 1); }
static void tab_set(int i, int b) { (void)b; if (i == pill_at()) return; tab_act(i, 0); }
static int is_root(void) { return SCR == SCR_HOME || SCR == SCR_DISC || SCR == SCR_LIB; }
static void left_act(int a, int b) { (void)a; (void)b; if (is_root()) app_go(SCR_SET, NULL, 0); else app_back(); }
static void tv_act(int a, int b) { (void)a; (void)b; cast_open(); }
/* the row of choices above the bar: a page fills it while it draws (ctx_row_*), the bar shows it */
typedef struct { char l[8][64]; int on[8], n; float ap[8]; /* ap: how far each chip is in (0..1: coming or going, it grows or shrinks in its place) */ HitFn act, set; int nset; Tween show; float lastPos; int seen; } CxRow;
static CxRow CX, CXN; /* CXN: the page next to it, drawn while it is swiped across (its own row: never mixed into this one) */
#define CXW (phGhost ? &CXN : &CX)
void ctx_row(HitFn act) { CxRow *c = CXW; c->n = 0; c->act = act; c->set = NULL; c->nset = 0; c->seen = 1; }
void ctx_slide(HitFn set, int n) { CxRow *c = CXW; c->set = set; c->nset = n; } /* the first n chips: a held finger sliding across picks them (set(i)) */
void ctx_add_k(const char *label, int on, float k) { CxRow *c = CXW; if (c->n < 8) { snprintf(c->l[c->n], 64, "%s", label); c->on[c->n] = on; c->ap[c->n] = clampf(k, 0, 1); c->n++; } }
void ctx_add(const char *label, int on) { ctx_add_k(label, on, 1); }
void ctx_scrolled(float pos) { if (phGhost) return; float d = pos - CX.lastPos; CX.lastPos = pos; if (pos < 40 || d < -4) tween_to(&CX.show, 1, 0.22f, EASE_WEB); else if (d > 4) tween_to(&CX.show, 0, 0.22f, EASE_WEB); }
void ctx_pinned(void) { if (phGhost) return; tween_to(&CX.show, 1, 0.22f, EASE_WEB); } /* a page whose choices always stay (the Library's) */
static float bar_w(void) { float m = ph_tabw() ? 640 : 760; return SB_W - 36 < m ? SB_W - 36 : m; } /* (a tablet sideways: a phone's own bar width, so the page's choices fit beside it) */
static float cx_tot(const CxRow *C) { float tot = 0; for (int i = 0; i < C->n; i++) tot += (text_width(C->l[i], F_BODY, 700, 0) + 56 + 10) * C->ap[i]; return tot; }
static float cx_avail(void) { return SB_W / 2 - bar_w() / 2 - PAD - 24; }
static int cx_beside(const CxRow *C) { return ph_tabw() && C->n && cx_avail() >= 320; } /* a tablet sideways: the choices on the bar's own line, left of it, never over the page (more than fit: they scroll there) */
float ctx_room(void) { return CX.n && !cx_beside(&CX) ? 92 : 0; }
/* Liquid glass: the strip of the page under the bar is copied each frame, taken down to an eighth and blurred
   (two small targets, six passes: well under a millisecond), then shown inside each control's shape with a light
   tint, a bright rim, light from the top and a soft shadow, so what scrolls under the bar shows through it. */
static int needGm; /* the next grab is for liquid glass (the bar, a bubble): its sharp backdrop too (full size, heavy: only then) */
static GLuint grabTex, picTex; static int grabW, grabH; static GfxTarget gm[2]; static int gmOk; /* gm: the page under the bar lightly blurred (the liquid glass's backdrop, 6 px) */ /* picTex: the player's glass (glassOk 2) */ static GfxTarget gb[2]; static float glassTop, glassH; static int glassOk;
static void glass_band(float top, float bottom);
static void glass_grab(float top) { int c = colOn; col_off(); glass_band(top, SB_H); if (c) col_on(); } /* (the whole screen, a column on or not) */
double ph_grab_ms; /* (development: the glass's copy and blurs, read by the slow-frame log) */
static void glass_band_(float top, float bottom);
static void glass_band(float top, float bottom) { Uint64 tq = SDL_GetPerformanceCounter(); glass_band_(top, bottom); ph_grab_ms += (double)(SDL_GetPerformanceCounter() - tq) * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
static void glass_band_(float top, float bottom) {
  extern int gfx_in_target(void); if (phGhost || gfx_in_target()) return; glassOk = 0; /* (a page drawn under a swipe, or into a picture: no grab in the middle of it; what was grabbed for it stays) */ /* (a page drawn under a swipe, or into a picture: no grab in the middle of it, its glass plain) */ float sc = gfx_scale(); int W = (int)(SB_W * sc + 0.5f), H = (int)((bottom - top) * sc + 0.5f), Y = (int)((SB_H - bottom) * sc + 0.5f); if (W < 8 || H < 8) return;
  gfx_flush();
  if (!grabTex || grabW != W || grabH != H) { if (grabTex) gfx_tex_free(grabTex); unsigned char *z = calloc((size_t)W * H, 4); grabTex = gfx_tex_create(z, W, H, 4, 1); free(z); grabW = W; grabH = H; }
  glBindTexture(GL_TEXTURE_2D, grabTex); glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, Y, W, H); gfx_invalidate_state();
  int bw = W / 4 > 4 ? W / 4 : 4, bh = H / 4 > 4 ? H / 4 : 4; /* (a quarter of the screen's pixels: smooth under the glass, no blocks) */
  for (int i = 0; i < 2; i++) { if (gb[i].fbo && (gb[i].w != bw || gb[i].h != bh)) gfx_target_free(&gb[i]); if (!gb[i].fbo && !gfx_target_create(&gb[i], bw, bh)) return; }
  gfx_target_begin(&gb[0], 0xff000000u); gfx_image(grabTex, 0, 0, SB_W, bh * SB_W / bw, 0, WHITE_A(255), 0, 1, 1, 0); gfx_target_end();
  static const float spread[3] = { 2.0f, 3.6f, 5.2f }; /* (twice the steps at twice the resolution: the same softness) */
  for (int i = 0; i < 3; i++) { gfx_blur_pass(&gb[1], gb[0].tex, bw, bh, 0, spread[i]); gfx_blur_pass(&gb[0], gb[1].tex, bw, bh, 1, spread[i]); }
  gmOk = 0; if (needGm) { needGm = 0; int mw = W, mh = H; gmOk = 1; /* at the screen's own resolution: no texel ever shows through the glass */ for (int i = 0; i < 2; i++) { if (gm[i].fbo && (gm[i].w != mw || gm[i].h != mh)) gfx_target_free(&gm[i]); if (!gm[i].fbo && !gfx_target_create(&gm[i], mw, mh)) gmOk = 0; }
    if (gmOk) { gfx_target_begin(&gm[0], 0xff000000u); gfx_image(grabTex, 0, 0, SB_W, mh * SB_W / mw, 0, WHITE_A(255), 0, 1, 1, 0); gfx_target_end(); float sp = 6.0f * 1.75f * sc / 1.7f / 2.0f; /* (the library's 6 px in screen pixels, over four passes: a 9-tap pass is about 1.7 of its steps wide; four of them smooth out its taps) */
      for (int k = 0; k < 4; k++) { gfx_blur_pass(&gm[1], gm[0].tex, mw, mh, 0, sp); gfx_blur_pass(&gm[0], gm[1].tex, mw, mh, 1, sp); } } }
  glassTop = top; glassH = bottom - top; glassOk = 1;
}
static void lglass_old(float x, float y, float w, float h, float r, uint32_t tint);
/* the bar's pills alone: liquid-glass-react's glass (its shadow 0 12 40 at 25 %, then the glass); everything else keeps
   the plain glass below. press: 0..1 (its highlights) */
static void lglass_p(float x, float y, float w, float h, float r, uint32_t tint, float press) {
  if (glassOk == 1 && gmOk && !getenv("SB_OLDGLASS")) { float u0, v0, u1, v1; glass_uv(x, y, w, h, &u0, &v0, &u1, &v1); (void)tint;
    { float n = x * 0.013f, tm = (float)app_now() * (1 + 0.13f * n) + 17.0f * n, ph = 2.39f * n; /* a lens drifting along it on its own pace, as the main build's bar */
      float su = 0.5f + 0.42f * (0.55f * sinf(tm * 0.27f + ph) + 0.3f * sinf(tm * 0.43f + 2.0f + 1.7f * ph) + 0.15f * sinf(tm * 0.71f + 4.4f + 2.3f * ph)); gfx_lens_spot(su, 0.9f); }
    gfx_shadow(x, y + 21, w, h, r, 70, BLACK_A(64)); gfx_liquid_lens(gm[0].tex, gb[0].tex, x, y, w, h, r, 95 * 1.75f, press, u0, v0, u1, v1); return; } /* (bent a little harder than liquid-glass-react's 70) */
  lglass_old(x, y, w, h, r, tint); }
__attribute__((unused)) static void lglass(float x, float y, float w, float h, float r, uint32_t tint) { lglass_p(x, y, w, h, r, tint, 0); }
/* its "elasticity" (0.15): near the finger (within 200 of its edges) it leans towards it and stretches along that way;
   pressed, 0.96; each eased over 0.2 s */
typedef struct { float tx, ty, sx, sy; } Elx;
static void elastic(Elx *e, float x, float y, float w, float h, int pressed) { float tx = 0, ty = 0, sx = 1, sy = 1, cx = x + w / 2, cy = y + h / 2;
  if (F.down) { float dx = F.x - cx, dy = F.y - cy, ex = fmaxf(0, fabsf(dx) - w / 2), ey = fmaxf(0, fabsf(dy) - h / 2), ed = sqrtf(ex * ex + ey * ey), zone = 200 * 1.75f;
    if (ed < zone) { float fade = 1 - ed / zone, cd = sqrtf(dx * dx + dy * dy); tx = dx * 0.15f * 0.1f * fade; ty = dy * 0.15f * 0.1f * fade;
      if (cd > 0) { float nx = dx / cd, ny = dy / cd, si = fminf(cd / (300 * 1.75f), 1) * 0.15f * fade; sx = fmaxf(0.8f, 1 + fabsf(nx) * si * 0.3f - fabsf(ny) * si * 0.15f); sy = fmaxf(0.8f, 1 + fabsf(ny) * si * 0.3f - fabsf(nx) * si * 0.15f); } } }
  if (pressed) { sx = sy = 0.96f; }
  float k = clampf(phDt / 0.2f * 2.2f, 0, 1); e->tx += (tx - e->tx) * k; e->ty += (ty - e->ty) * k; e->sx += (sx - e->sx) * k; e->sy += (sy - e->sy) * k; if (e->sx == 0) { e->sx = e->sy = 1; } }
static void el_push(const Elx *e, float cx, float cy) { gfx_offset_push(e->tx, e->ty); gfx_scale_push(e->sx ? e->sx : 1, e->sy ? e->sy : 1, cx, cy); }
static void el_pop(void) { gfx_scale_pop(); gfx_offset_pop(); }
/* the words and signs on it: the library's text shadow (0 2 12, 40 % black) */
static void soft_shadow(float x, float y, float w, float h) { gfx_shadow(x, y + 3.5f, w, h, h / 2, 21, BLACK_A(102)); }
static void glass_uv(float x, float y, float w, float h, float *u0, float *v0, float *u1, float *v1) { float ax = x, ay = y, bx = x + w, by = y + h; gfx_to_screen(&ax, &ay); gfx_to_screen(&bx, &by); gfx_mirror_span(&ax, &bx); /* where it really is (a page sliding, a pill leaning): the page under it there */
  *u0 = ax / FULLW; *u1 = bx / FULLW; *v0 = 1 - (ay - glassTop) / glassH; *v1 = 1 - (by - glassTop) / glassH; }
static void lglass_old(float x, float y, float w, float h, float r, uint32_t tint) {
  if (phGhost && !phGhostGlass) { gfx_rrect(x, y, w, h, r, WHITE_A(10)); gfx_rrect(x, y, w, h, r, tint); gfx_rrect_v(x, y, w, h, r, WHITE_A(28), WHITE_A(4)); gfx_ring(x, y, w, h, r, 1.5f, WHITE_A(46)); return; } /* a page drawn under one being swiped: its glass would read a copy of another page (light blocks); plain, the same look */
  if (glassOk) { float u0, v0, u1, v1; glass_uv(x, y, w, h, &u0, &v0, &u1, &v1);
    gfx_image(glassOk == 2 ? picTex : gb[0].tex, x, y, w, h, r, WHITE_A(255), u0, v0, u1, v1); gfx_rrect(x, y, w, h, r, tint); }
  else gfx_rrect(x, y, w, h, r, RGBA(30, 28, 40, 235));
  gfx_rrect_v(x, y, w, h, r, WHITE_A(28), WHITE_A(4));                /* light from the top, over the whole shape (a part of it with this corner breaks the curve) */
  gfx_ring(x, y, w, h, r, 1.5f, WHITE_A(46));                          /* the rim (one line: no second outline) */
}
/* the glass from a picture of the whole screen (the player's film, copied small): blurred once, then each panel shows
   its part (ph_glass) until the next one */
void ph_glass_picture(const unsigned char *rgba, int w, int h) {
  static GLuint t; static int tw, th; if (!t || tw != w || th != h) { if (t) gfx_tex_free(t); t = gfx_tex_create(rgba, w, h, 4, 1); tw = w; th = h; } else gfx_tex_update(t, rgba, 0, 0, w, h, 4);
  for (int i = 0; i < 2; i++) { if (gb[i].fbo && (gb[i].w != w || gb[i].h != h)) gfx_target_free(&gb[i]); if (!gb[i].fbo && !gfx_target_create(&gb[i], w, h)) { glassOk = 0; return; } }
  gfx_flush(); gfx_target_begin(&gb[0], 0xff000000u); gfx_image(t, 0, 0, SB_W, h * SB_W / w, 0, WHITE_A(255), 0, 0, 1, 1); gfx_target_end();
  static const float spread[3] = { 1.2f, 2.0f, 2.8f };
  for (int i = 0; i < 3; i++) { gfx_blur_pass(&gb[1], gb[0].tex, w, h, 0, spread[i]); gfx_blur_pass(&gb[0], gb[1].tex, w, h, 1, spread[i]); }
  /* each new copy blends over the ones before (a frame or two behind the film, but smooth, not stepping) */
  static GfxTarget acc[2]; static int cur, have; for (int i = 0; i < 2; i++) { if (acc[i].fbo && (acc[i].w != w || acc[i].h != h)) { gfx_target_free(&acc[i]); have = 0; } if (!acc[i].fbo && !gfx_target_create(&acc[i], w, h)) { glassOk = 0; return; } }
  int nx = cur ^ 1; float bh = h * SB_W / w; gfx_target_begin(&acc[nx], 0xff000000u);
  if (have) { gfx_image(acc[cur].tex, 0, 0, SB_W, bh, 0, WHITE_A(255), 0, 1, 1, 0); gfx_image(gb[0].tex, 0, 0, SB_W, bh, 0, WHITE_A(110), 0, 1, 1, 0); }
  else gfx_image(gb[0].tex, 0, 0, SB_W, bh, 0, WHITE_A(255), 0, 1, 1, 0);
  gfx_target_end(); cur = nx; have = 1; picTex = acc[cur].tex;
  glassTop = 0; glassH = SB_H; glassOk = 2;
}
void ph_glass_off(void) { glassOk = 0; }
/* the player's liquid glass: the film itself, copied a quarter size as often as the copies come back, lightly
   softened, and bent by the shapes over it as it moves (the bar's glass, made of the picture) */
static GfxTarget pm[2]; static GLuint pmSrc; static int pmW, pmH, pmOk; static double pmAt;
void ph_glass_picture_live(const unsigned char *rgba, int w, int h) {
  extern float ph_vid_rect[4]; float vx = ph_vid_rect[0], vy = ph_vid_rect[1], vw = ph_vid_rect[2] > 1 ? ph_vid_rect[2] : SB_W, vh = ph_vid_rect[3] > 1 ? ph_vid_rect[3] : SB_H;
  if (!pmSrc || pmW != w || pmH != h) { if (pmSrc) gfx_tex_free(pmSrc); pmSrc = gfx_tex_create(rgba, w, h, 4, 1); pmW = w; pmH = h; } else gfx_tex_update(pmSrc, rgba, 0, 0, w, h, 4);
  int tw = (int)(SB_W / 4), th = (int)(SB_H / 4); /* the whole screen a quarter size, the film where it is on it (black around it, as the bars are) */
  for (int i = 0; i < 2; i++) { if (pm[i].fbo && (pm[i].w != tw || pm[i].h != th)) gfx_target_free(&pm[i]); if (!pm[i].fbo && !gfx_target_create(&pm[i], tw, th)) { pmOk = 0; return; } }
  gfx_flush(); gfx_target_begin(&pm[0], 0xff000000u); gfx_image(pmSrc, vx, vy, vw, vh, 0, WHITE_A(255), 0, 0, 1, 1); gfx_target_end();
  for (int k = 0; k < 2; k++) { gfx_blur_pass(&pm[1], pm[0].tex, tw, th, 0, 1.4f); gfx_blur_pass(&pm[0], pm[1].tex, tw, th, 1, 1.4f); } /* (soft enough that its pixels never show: the glass's own frost) */
  pmOk = 1; pmAt = app_now(); }
void ph_glass_picture_reset(void) { pmOk = 0; }
/* a shape of it; 0 when there is no fresh copy of the film (the caller's plain glass then) */
int ph_liquid_pic(float x, float y, float w, float h, float r, float press) {
  if (!pmOk || app_now() - pmAt > 0.7) return 0;
  float ax = x, ay = y, bx = x + w, by = y + h; gfx_to_screen(&ax, &ay); gfx_to_screen(&bx, &by); gfx_mirror_span(&ax, &bx);
  float u0 = ax / FULLW, u1 = bx / FULLW, v0 = 1 - ay / SB_H, v1 = 1 - by / SB_H;
  gfx_liquid_lens(pm[0].tex, pm[0].tex, x, y, w, h, r, fminf(60.0f * 1.75f, fminf(w, h) * 0.9f), press, u0, v0, u1, v1); return 1; }
/* a bubble of the bar's liquid glass anywhere on a page that grabbed its glass (ph_glass_grab) */
void ph_liquid_bubble(float x, float y, float w, float h, float r, float press) { if (glassOk == 1 && gmOk) { float u0, v0, u1, v1; glass_uv(x, y, w, h, &u0, &v0, &u1, &v1); gfx_shadow(x, y + 18, w, h, r, 50, BLACK_A(80)); gfx_liquid_lens(gm[0].tex, gb[0].tex, x, y, w, h, r, 60 * 1.75f, press, u0, v0, u1, v1); } else { gfx_shadow(x, y + 18, w, h, r, 50, BLACK_A(80)); gfx_rrect(x, y, w, h, r, WHITE_A(40)); gfx_ring(x, y, w, h, r, 1.5f, WHITE_A(90)); } }

void ph_glass(float x, float y, float w, float h, float r, uint32_t tint) { lglass_old(x, y, w, h, r, tint); } /* (pages' glass: the plain one) */
void ph_glass_grab(void) { glass_grab(0); }
void ph_glass_grab_liquid(void) { needGm = 1; glass_grab(0); } /* (for a bubble of liquid glass on a page) */
int ph_glass_full(void) { return glassOk == 1 && glassTop < 0.5f && glassH > SB_H - 1; } /* (the whole page grabbed just now) */ /* the page so far, blurred: what ph_glass shows */
static void circle_btn(float cx, float cy, float r, const char *icon, uint32_t tint, int lit, HitFn act) {
  static Elx E[8]; static int ne; static float ex0[8]; int ei = -1; for (int i = 0; i < ne; i++) if (ex0[i] == cx) ei = i; if (ei < 0 && ne < 8) { ei = ne++; ex0[ei] = cx; memset(&E[ei], 0, sizeof E[ei]); } if (ei < 0) ei = 0; /* (each button's own, by where it is) */
  float pk = press_k(cx - r, cy - r, 2 * r, 2 * r); elastic(&E[ei], cx - r, cy - r, 2 * r, 2 * r, pk > 0.5f); el_push(&E[ei], cx, cy);
  lglass_p(cx - r, cy - r, 2 * r, 2 * r, r, lit ? col_alpha(ACC, 0.75f) : RGBA(16, 14, 22, 92), pk); if (lit) gfx_circle(cx, cy, r - 3, col_alpha(ACC, 0.45f));
  soft_shadow(cx - 26, cy - 26, 52, 52); icon_draw(icon, cx - 26, cy - 26, 52, tint); el_pop(); hit_act(cx - r, cy - r, 2 * r, 2 * r, act, 0, 0);
}
/* one row of choices: slot 0 the page's own (pressable, scrollable), 1 the page swiped in, 2 a row going away; moved
   across by xoff; k its showing (it rises from below). The chosen one's white pill rides a spring and stretches the
   way it goes, as the bar's drop does */
static void cx_draw(CxRow *C, int slot, float xoff, float k, int live) {
  if (k <= 0.01f || !C->n) return; float cy = SB_H - INS_B - 18 - 44, r = 44, ry = cy - r - 24 - 72 - phMiniH + (1 - k) * 100, h = 72, tot = cx_tot(C); /* (over the cast mini player when it is there) */
  int beside = cx_beside(C); if (beside) ry = cy - h / 2 + (1 - k) * 100; /* (a tablet sideways: on the bar's line) */
  float cs = 1; int fpx = F_BODY; { float vw0 = beside ? cx_avail() : SB_W - 36, t0 = tot - 10; if (t0 > vw0 && t0 * 0.8f <= vw0) { cs = vw0 / t0 * 0.99f; fpx = (int)(F_BODY * cs); tot = 0; for (int i = 0; i < C->n; i++) tot += (text_width(C->l[i], fpx, 700, 0) + 56 * cs + 10) * C->ap[i]; } } /* (a little too many for the width, a tablet upright: a little smaller so all fit; far too many, a phone: they scroll as ever) */
  static Scroll cxS[3]; static int lastSel[3] = { -2, -2, -2 }, cxGo[3]; static float cxTo[3]; Scroll *S = &cxS[slot]; float vw = beside ? cx_avail() : SB_W - 36, over = tot - 10 - vw; S->max = over > 0 ? over : 0;
  { int se = -1; float sx = 18, want = 0; for (int i = 0; i < C->n; i++) { float w = (text_width(C->l[i], fpx, 700, 0) + 56 * cs) * C->ap[i]; if (C->on[i]) { se = i; want = beside ? sx - 18 + w / 2 - vw / 2 : sx + w / 2 - SB_W / 2; } sx += w + 10 * C->ap[i]; }
    if (live && over > 0) { if (beside) ph_drag(PAD, ry - 6, vw, 84, S, 0); else ph_drag(0, ry - 6, SB_W, 84, S, 0); }
    if (se != lastSel[slot]) { if (lastSel[slot] != -2 && !S->drag) { S->vel = 0; S->free = 0; } lastSel[slot] = se; cxTo[slot] = clampf(want, 0, S->max); cxGo[slot] = 1; if (!live) S->pos = cxTo[slot]; }
    if (S->drag) cxGo[slot] = 0; if (cxGo[slot]) { S->pos += (cxTo[slot] - S->pos) * clampf(phDt * 10, 0, 1); if (fabsf(cxTo[slot] - S->pos) < 0.5f) cxGo[slot] = 0; } else scroll_tick(S); }
  float x = (beside ? PAD - S->pos : tot < SB_W - 36 ? (SB_W - tot + 10) / 2 : 18 - S->pos) + xoff; gfx_opacity_push(k); int clip = beside && over > 0; if (clip) gfx_clip_push(PAD - 14 + xoff, ry - 40, vw + 14, h + 80); /* (more than fit beside the bar: they scroll in their own strip) */
  float cxs[8], cws[8]; int sel = -1; for (int i = 0; i < C->n; i++) { cws[i] = (text_width(C->l[i], fpx, 700, 0) + 56 * cs) * C->ap[i]; cxs[i] = x; x += cws[i] + 10 * C->ap[i]; if (C->on[i]) sel = i; }
  static Elx CE[3][8]; for (int i = 0; i < C->n; i++) { float pk = live ? press_k(cxs[i], ry, cws[i], h) : 0; elastic(&CE[slot][i], cxs[i], ry, cws[i], h, pk > 0.5f); el_push(&CE[slot][i], cxs[i] + cws[i] / 2, ry + h / 2); float a = C->ap[i]; if (a < 0.999f) { if (a < 0.01f) { el_pop(); continue; } float fw = cws[i] / a; gfx_opacity_push(a); gfx_scale_push(a, a, cxs[i] + cws[i] / 2, ry + h / 2); lglass_p(cxs[i] + cws[i] / 2 - fw / 2, ry, fw, h, h / 2, RGBA(16, 14, 22, 92), pk); gfx_scale_pop(); gfx_opacity_pop(); } else lglass_p(cxs[i], ry, cws[i], h, h / 2, RGBA(16, 14, 22, 92), pk); el_pop(); } /* (coming or going: the whole chip grows or shrinks in its place) */
  /* the chosen one: on a spring, in the row's own frame (so a row moving across carries it exactly) */
  static float sx0[3], sv[3], sw0[3]; static int seenS[3]; float tx = sel >= 0 ? cxs[sel] - xoff : 0, tw = sel >= 0 ? cws[sel] : 0;
  if (sel >= 0) { if (!seenS[slot] || !live) { sx0[slot] = tx; sv[slot] = 0; sw0[slot] = tw; seenS[slot] = 1; }
    float dt = phDt > 0.05f ? 0.05f : phDt; const float kk = 340, c = 2 * sqrtf(340) * 0.8f; for (int q = 0; q < 4; q++) { float h2 = dt / 4, acc = -kk * (sx0[slot] - tx) - c * sv[slot]; sv[slot] += acc * h2; sx0[slot] += sv[slot] * h2; }
    sw0[slot] += (tw - sw0[slot]) * clampf(phDt * 14, 0, 1);
    float st = tanhf(fabsf(sv[slot]) / 900), w0 = sw0[slot] * (1 + 0.28f * st), h0 = h * (1 - 0.12f * st) * C->ap[sel], x0 = sx0[slot] + xoff + sw0[slot] / 2 - w0 / 2, y0 = ry + h / 2 - h0 / 2;
    gfx_rrect(x0, y0, w0, h0, h0 / 2, WHITE_A(242)); }
  else seenS[slot] = 0;
  for (int i = 0; i < C->n; i++) { float mid = cxs[i] + cws[i] / 2; int under = sel >= 0 && mid > sx0[slot] + xoff && mid < sx0[slot] + xoff + sw0[slot];
    float a = C->ap[i]; if (a < 0.01f) continue; if (a < 0.999f) { gfx_opacity_push(a); gfx_scale_push(a, a, mid, ry + h / 2); }
    if (!under) { float tw2 = text_width(C->l[i], fpx, 700, 0); soft_shadow(mid - tw2 / 2, ry + h / 2 - 16, tw2, 32); }
    text_draw(C->l[i], mid, ry + h / 2 - text_line_height(fpx) / 2.0f, fpx, 700, under ? RGB(14, 12, 20) : WHITE_A(240), 0, 0, ALIGN_CENTER);
    if (a < 0.999f) { gfx_scale_pop(); gfx_opacity_pop(); }
    if (live && k > 0.5f && (!clip || (cxs[i] + cws[i] > PAD && cxs[i] < PAD + vw))) hit_act(cxs[i], ry, cws[i], h, C->act, i, 0); }
  if (clip) gfx_clip_pop(); gfx_opacity_pop(); }
static void bar_draw_in(void);
float phMiniH; static Tween miniT; /* the cast mini player: there while a title plays on the TV, on every page but Settings */
int cast_mini_want(void); void cast_mini_content(float x, float y, float w, float h); void cast_mini_fly(float x, float y, float h); void cast_mini_place(float x, float y, float w, float h); float cast_mini_fly_k(void);
void phone_bar_draw(void) { extern float ph_live_full(void); float lf = SCR == SCR_LIVE ? ph_live_full() : 0; if (lf > 0.98f) return; if (lf > 0.002f) gfx_opacity_push(1 - lf); barDrawing = 1; bar_draw_in(); barDrawing = 0; if (lf > 0.002f) gfx_opacity_pop(); } /* (All channels' preview filling the screen: the bar fades with the page) */
static void bar_draw_in(void) {
  extern int ph_search_on(void); if (SCR == SCR_LOGIN || !app_splash_done || player_open()) { CX.seen = 0; return; }
  float cy = SB_H - INS_B - 18 - 44, r = 44; /* the bar's middle line: 88 tall, 18 above the gesture bar */
  float barW = bar_w(), barX = SB_W / 2 - barW / 2; /* sideways or on a tablet: the bar keeps a phone's width, in the middle */
#define SLOT(i) (barX + barW / 6.0f * ((i) + 0.5f)) /* six equal places: profile, the four in the pill (Home, Library, Downloads, Search), the TV */
  { int mw = cast_mini_want() && SCR != SCR_SET && !ph_search_on(); /* (not in Settings, not while searching: the bar is the search field then) */ tween_to(&miniT, mw, 0.32f, EASE_WEB); tween_tick(&miniT, phDt); phMiniH = 118 * miniT.v; }
  float rowR = CX.seen && CX.n && CX.show.to > 0.5f && !cx_beside(&CX) ? 24 + 72 : 0, miniY = cy - r - 12 - 106; /* (the mini player: right over the bar; the page's row of choices over it) */
  barTop = cy - r - rowR - phMiniH - 8; /* the bar, its row of choices and the mini player: what is under them is never pressed */
  needGm = 1; glass_grab(flyT.on ? 0 : cy - r - 24 - 72 - phMiniH - 24); /* the page under the bar and its row of choices, before anything is drawn over it (a bubble flying: the whole page, for its glass all the way) */
  { void pplat_tilt(float *x, float *y); float tx, ty; pplat_tilt(&tx, &ty); static float lx, ly; float wx = tx * 35, wy = ty * 35; lx += (wx - lx) * clampf(phDt * 6, 0, 1); ly += (wy - ly) * clampf(phDt * 6, 0, 1); gfx_liquid_light(lx, ly); } /* the light moves as the phone tilts */
  gfx_rrect_v(0, cy - 40, SB_W, SB_H - cy + 40, 0, col_alpha(PAGE | 0xff000000u, 0), col_alpha(PAGE | 0xff000000u, 0.5f)); /* a little shade under the bar */
  /* the row of choices */
  /* the row belongs to its page: swiped across, it goes with it and the next page's comes in from the other side (as
     their pages do); landing, the one there stays; a page entered otherwise brings its row up; a page with none lets
     the last one go softly */
  { float pdx = F.down && F.drag == 8 ? tabDx : tabT.v; static float lastPdx; static Scr cxScr = (Scr)-1; static int cxLive; static CxRow cxLast; static int nbWas;
    int landed = SCR != cxScr && (fabsf(lastPdx) > SB_W * 0.4f || nbWas); /* (arrived by a swipe: its row was already in view) */
    if (CX.seen && CX.n) { if (SCR != cxScr) { cxScr = SCR; if (landed) tween_set(&CX.show, 1); else { tween_set(&CX.show, 0); tween_to(&CX.show, 1, 0.34f, EASE_WEB); } } cxLast = CX; cxLive = 1; }
    else if (cxLive) { cxLive = 0; cxScr = (Scr)-1; if (landed || fabsf(lastPdx) > SB_W * 0.4f) tween_set(&CX.show, 0); else tween_to(&CX.show, 0, 0.22f, EASE_CSS_EASE); }
    int nb = CXN.seen && CXN.n && fabsf(pdx) > 0.5f; nbWas = nb; lastPdx = pdx;
    if (CX.seen && CX.n) { tween_tick(&CX.show, phDt); cx_draw(&CX, 0, pdx, CX.show.v, fabsf(pdx) < 1); }
    else if (CX.show.v > 0.01f && cxLast.n) { tween_tick(&CX.show, phDt); cx_draw(&cxLast, 2, 0, CX.show.v, 0); }
    if (nb) cx_draw(&CXN, 1, pdx < 0 ? pdx + SB_W : pdx - SB_W, 1, 0);
    CXN.seen = 0; }
  CX.seen = 0;
  cast_mini_place(barX, miniY, barW, 106); float fk = cast_mini_fly_k(); if (fk >= 0) cast_mini_fly(0, 0, 0); /* (the remote shrinking into it: drawn where it lands) */
  if (miniT.v > 0.01f) { float k = fk >= 0 ? clampf((fk - 0.7f) / 0.3f, 0, 1) : miniT.v, h = 106, my = fk >= 0 ? miniY : miniY + (1 - k) * 60, pk = press_k(barX, my, barW - 170, h), s = 1 - 0.03f * pk; /* (pressed on its picture or name: it gives a little; on a button: only the button) */ /* the cast mini player: comes up over the bar, a tap opens the remote */
    gfx_opacity_push(k); gfx_scale_push(s, s, barX + barW / 2, my + h / 2); lglass_p(barX, my, barW, h, 34, RGBA(16, 14, 22, 92), pk); cast_mini_content(barX, my, barW, h); gfx_ring(barX, my, barW, h, 34, 1, WHITE_A(44)); gfx_scale_pop(); gfx_opacity_pop(); } /* (its outline over its picture too) */
  if (ph_dl_active()) { float dx = SLOT(3) + 22, dy = cy - 26; gfx_circle(dx, dy, 9, ACC2 | 0xff000000u); } /* downloading: a dot on its place */
  /* left: the profile, or Back on an inner page */
  { /* the left button: Profile on the tabs, Back inside; swiping back to a tab, one turns into the other under the finger */
    extern int app_back_target(void); int to = app_back_target(); float ph_settings_back_k(void); float sk = SCR == SCR_SET ? ph_settings_back_k() : 0, bkk = fmaxf(phone_back_k(), sk); float want = is_root() ? 1 : (to == SCR_HOME || to == SCR_DISC || to == SCR_LIB) ? bkk : 0;
    static Tween rootT; static int seen; if (!seen) { seen = 1; tween_set(&rootT, want); } if (F.drag == 7 || backGoing || sk > 0.001f) tween_set(&rootT, want); else tween_to(&rootT, want, 0.25f, EASE_WEB); tween_tick(&rootT, phDt);
    float u = rootT.v, c0 = SLOT(0); circle_btn(c0, cy, r, "", WHITE_A(240), 0, left_act);
    if (u < 0.99f) icon_draw_rot("ph_back", c0 - 26, cy - 26, 52, WHITE_A((int)(240 * (1 - u))), -90 * u);
    if (u > 0.01f) icon_draw_rot("ph_sliders", c0 - 26, cy - 26, 52, WHITE_A((int)(240 * u)), 90 * (1 - u)); }
  { extern const char *update_avail(void); if (update_avail() && is_root()) gfx_circle(SLOT(0) + 30, cy - 30, 9, RGB(0x34, 0xc7, 0x59)); } /* a newer version out: a green dot on Settings (About installs it), as on the TV */
  /* right: Search and the TV */
  { int cs = cast_icon_state(), on = cs == 3; float ccx = SLOT(5); /* the cast button: dim with no TV around, a pulse while connecting, lit when connected */
    if (cs == 0) gfx_opacity_push(0.5f); circle_btn(ccx, cy, r, on ? "ph_cast_on" : "ph_cast", WHITE_A(240), 0, tv_act); /* (connected: the TV-connected icon, nothing else) */ if (cs == 0) gfx_opacity_pop();
    if (cs == 2) { float ph = fmodf((float)app_now(), 1.2f) / 1.2f, rr = r + 4 + 14 * ph; gfx_ring(ccx - rr, cy - rr, 2 * rr, 2 * rr, rr, 2.5f, col_alpha(ACC2 | 0xff000000u, 0.7f * (1 - ph))); }
  }
  /* the pill with the three places, between them */
  float px = 18 + 2 * r + 12, pw = SB_W - 2 * (18 + 2 * r + 12) - 2 * r - 12 + 0, ph = 2 * r; pw = (SB_W - 18 - 2 * r - 12 - 2 * r - 12) - px - 0;
  px = SLOT(1) - r; pw = SLOT(4) + r - px; /* the four places: icons alone, each as big as the round buttons, on the same steps */
  static Elx TE; float tpk = press_k(px, cy - r, pw, ph), tHold = held_k(px, cy - r, pw, ph); elastic(&TE, px, cy - r, pw, ph, tpk > 0.5f && tHold < 0.05f); /* (held: the drop lifts, the bar stays) */ el_push(&TE, px + pw / 2, cy);
  /* a bubble swallowed at one of its places: the pill swells there and settles, on a spring (a breath past, then home) */
  float swq = (float)((app_now() - flyPulse) / 0.55), sw = swq >= 0 && swq < 1 ? sinf(swq * (float)M_PI * 1.6f) * expf(-swq * 3.2f) : 0; float swx = SLOT(flySlot);
  int swOn = fabsf(sw) > 0.002f && flySlot >= 1 && flySlot <= 3; if (swOn) gfx_scale_push(1 + 0.045f * sw, 1 + 0.10f * sw, swx, cy);
  lglass_p(px, cy - r, pw, ph, r, RGBA(16, 14, 22, 92), tpk);
  if (swOn && sw > 0) { gfx_clip_push(px, cy - r, pw, ph); float gr = r * (0.6f + 1.6f * swq); gfx_glow_round(swx - gr, cy - gr, 2 * gr, 2 * gr, col_alpha(ACC2 | 0xff000000u, 0.32f * sw), px, cy - r, pw, ph, r); gfx_clip_pop(); } /* its light spreading inside the pill */
  float w = SLOT(2) - SLOT(1); int tab = pill_at();
  /* a page being swiped back to one of the places (Settings opened from Downloads, a page swiped away): the drop comes
     in there with the finger, so landing changes nothing */
  float tabA = 1; static int lastTab = -9, lensSnap; { extern int app_back_target(void); float ph_settings_back_k(void); if (tab < 0) { int to = app_back_target(); float k = fmaxf(phone_back_k(), SCR == SCR_SET ? ph_settings_back_k() : 0); for (int j = 0; j < 3; j++) if (to == TABS[j] && k > 0.001f) { tab = j; tabA = k; } } }
  if (tab >= 0 && lastTab < 0) lensSnap = 1; lastTab = tab;
  /* the place you are in: a drop of the same glass inside the pill. It rides a spring (never a jump, whatever comes
     next), follows a page being swiped across under the finger, follows a held finger; its speed stretches it the
     way it goes and thins it, smoothly (from the spring's own speed) */
  static float lx = -1, lv; static int seenTab = -2;
  if (tab >= 0) { float target = (float)tab;
    static Tween swT; int swiping = 0; { float pdx = F.down && F.drag == 8 ? tabDx : tabT.v; if (fabsf(pdx) > 0.5f) { target = clampf(tab - pdx / SB_W, 0, 3); float dt0 = phDt > 0.001f ? phDt : 0.016f; lv += ((target - lx) / dt0 - lv) * clampf(phDt * 20, 0, 1); lx = target; swiping = 1; } } /* the page as it is shown (under the finger, then settling): the drop exactly with it, never drifting; lifted as when held, stretching with its own speed */
    tween_to(&swT, swiping ? 1 : 0, 0.25f, EASE_WEB); tween_tick(&swT, phDt);
    float hk = held_k(px, cy - r, pw, ph); if (hk > 0.01f && holdOn) target = clampf((F.x - SLOT(1)) / w, 0, 3);
    if (seenTab == -2 || lx < 0 || lensSnap) { lx = target; lv = 0; } seenTab = tab; if (tabA < 0.999f) gfx_opacity_push(tabA);
    { float dt = phDt > 0.05f ? 0.05f : phDt; const float k = 300, c = 2 * sqrtf(300) * 0.8f; for (int q = 0; q < 4; q++) { float h2 = dt / 4, acc = -k * (lx - target) - c * lv; lv += acc * h2; lx += lv * h2; } }
    /* held: it lifts out of the bar, bigger, a lens over the places. Swiped across pages: no lifting; it flows like
       a drop of liquid (as the spotlight's dots do): its head with the page, its tail following on a softer spring,
       stretched between the two and a little thinner the longer it is, a round drop again when the page lands */
    static float tail = -1, tailV; if (tail < 0) tail = lx; { float dt = phDt > 0.05f ? 0.05f : phDt; const float k2 = 140, c2 = 2 * sqrtf(140) * 0.92f; for (int q = 0; q < 4; q++) { float h2 = dt / 4, acc = -k2 * (tail - lx) - c2 * tailV; tailV += acc * h2; tail += tailV * h2; } }
    if ((!swT.v && !swiping) || lensSnap) { tail = lx; tailV = 0; lensSnap = 0; }
    float lift = tHold, base = ph - 12, hh = base * (1 + 0.32f * lift), hw = hh, sp = fabsf(lv), st = tanhf(sp * 0.9f) * (1 - swT.v), sw = 1 + 0.5f * st * (1 - 0.5f * lift), sh = 1 - 0.14f * st * (1 - 0.5f * lift);
    float lc = SLOT(1) + lx * w, my = cy; hw *= sw; hh *= sh; float hx = lc - hw / 2, hy = my - hh / 2;
    if (swT.v > 0.001f) { float a0 = SLOT(1) + lx * w, a1 = SLOT(1) + tail * w, len = fabsf(a0 - a1), thin = 1 - 0.16f * tanhf(len / (w * 0.6f)) * swT.v; /* the liquid drop between head and tail */
      float x0 = fminf(a0, a1) - base / 2, x1 = fmaxf(a0, a1) + base / 2; float nh = base * thin; hx = hx + (x0 - hx) * swT.v; hw = hw + ((x1 - x0) - hw) * swT.v; hh = hh + (nh - hh) * swT.v; hy = my - hh / 2; }
    if (glassOk == 1 && gmOk) { float u0, v0, u1, v1; glass_uv(hx, hy, hw, hh, &u0, &v0, &u1, &v1); if (lift > 0.01f) gfx_shadow(hx, hy + 10 * lift, hw, hh, hh / 2, 30 * lift, BLACK_A((int)(90 * lift))); gfx_liquid_lens(gm[0].tex, gb[0].tex, hx, hy, hw, hh, hh / 2, (40 + 40 * lift) * 1.75f, pillLight(0.3f * tpk * (1 - fminf(lift, 1)) + 0.16f * fminf(lift, 1)), u0, v0, u1, v1); /* (its light: pressed and held blend into each other, softly; it switched between them at once) */ gfx_rrect(hx, hy, hw, hh, hh / 2, col_alpha(ACC, 0.16f * (1 - 0.6f * lift))); }
    else { gfx_rrect(hx, hy, hw, hh, hh / 2, WHITE_A(34)); gfx_ring(hx, hy, hw, hh, hh / 2, 1.5f, WHITE_A(60)); } if (tabA < 0.999f) gfx_opacity_pop(); }
  { float cx3[4], cw3[4]; for (int i = 0; i < 4; i++) { cx3[i] = SLOT(1 + i) - w / 2; cw3[i] = w; } ph_scrub(px, cy - r, pw, ph, 4, cx3, cw3, tab_set); }
  static const char *IC[4] = { "ph_home", "ph_lib", "ph_down", "ph_search" }; const char *LB[4] = { T("Home"), T("Library"), T("Downloads"), T("Search") };
  (void)LB; for (int i = 0; i < 4; i++) { float x = SLOT(1 + i) - w / 2; int at = i == tab; uint32_t c = at ? col_mix(WHITE_A(160), WHITE_A(255), tabA) : WHITE_A(160); float k = 1 - 0.12f * press_k(x, cy - r, w, ph);
    if (swOn && i + 1 == flySlot) k *= 1 + 0.32f * fmaxf(sw, 0); /* the place that took it: its sign pops */
    gfx_scale_push(k, k, x + w / 2, cy); soft_shadow(x + w / 2 - 26, cy - 26, 52, 52); icon_draw(IC[i], x + w / 2 - 26, cy - 26, 52, c); gfx_scale_pop();
    hit_act(x, cy - r, w, ph, tab_act, i, 0); }
  if (swOn) gfx_scale_pop();
  el_pop();
  /* added to the Library or downloading: a drop of the same glass, holding a round cut of the poster, flies from the finger
     along an arc to its place, smaller as it goes, stretching with its speed; near the end the poster melts into it, and
     the pill swallows it (above: it swells there, its sign pops, a click) */
  fly_wait(); if (flyT.on) { float k = timer_x(&flyT), e = ease_apply(EASE_IN_OUT, k); float tx = SLOT(flySlot), ty = cy;
    float x = flyX + (tx - flyX) * e, y = flyY + (ty - flyY) * e - sinf(e * (float)M_PI) * 200 * (1 - 0.3f * e);
    static float lx2, ly2, vs; float sp = phDt > 0 ? hypotf(x - lx2, y - ly2) / phDt : 0; lx2 = x; ly2 = y; vs += (sp - vs) * clampf(phDt * 12, 0, 1);
    float R = 86 + (r * 0.55f - 86) * e, st = tanhf(vs / 2600) * 0.16f * (1 - k), Rx = R * (1 + st), Ry = R * (1 - st * 0.7f); /* (a drop: longer the faster) */
    float a2 = k < 0.88f ? 1 : (1 - k) / 0.12f; gfx_opacity_push(a2);
    ph_liquid_bubble(x - Rx, y - Ry, 2 * Rx, 2 * Ry, Ry, 0.15f);
    { float pr2 = R * 0.66f, pa = 1 - e * e; GLuint t = flyPic[0] ? img_get_shaped(flyPic, 160, 160, 80, 2) : 0; if (t && pa > 0.02f) { gfx_opacity_push(pa); ph_picture(t, x - pr2, y - pr2, 2 * pr2, 2 * pr2, pr2, WHITE_A(255)); gfx_opacity_pop(); } } /* the poster inside, melting into the glass */
    gfx_ring(x - Rx, y - Ry, 2 * Rx, 2 * Ry, Ry, 1.4f, WHITE_A(70)); gfx_opacity_pop();
    timer_tick(&flyT, phDt); if (!flyT.on) { flyPulse = app_now(); pplat_haptic_k(HK_CLICK, 0.8f); } }
}
void phone_tick(float dt) { phDt = dt; frameNo++;
  { extern int player_from_preview(void); extern int player_to_preview(int anim); int pplat_held(void); static int seenSide, wasFrom; static double upAt; int from = player_from_preview();
    if (from != wasFrom) { wasFrom = from; seenSide = 0; upAt = 0; }
    if (from && frameNo % 6 == 0) { int h = pplat_held(); if (h == 0) { seenSide = 1; upAt = 0; } else if (h == 1 && seenSide) { if (!upAt) upAt = app_now(); else if (app_now() - upAt > 0.35) { upAt = 0; player_to_preview(1); } } else upAt = 0; } } /* a channel from the page's preview, the phone turned upright again: back into the preview */
  { int pplat_fullscreen_on(void); void pplat_fullscreen(int on); extern int player_open(void); static double offAt; if (pplat_fullscreen_on() && !player_open()) { if (!offAt) offAt = app_now(); else if (app_now() - offAt > 0.4) { fprintf(stderr, "[phone] sideways with no player: upright again\n"); pplat_fullscreen(0); offAt = 0; } } else offAt = 0; } /* (sideways belongs to the player alone: whatever asked for it, no player, the phone turns freely again) */
  { static int told; if (sb_session_lost() && !told && app_splash_done && !player_open() && SCR != SCR_LOGIN) { told = 1; toast(T("Signed out on this phone: sign in again"), 3200, NULL); app_go(SCR_LOGIN, NULL, 0); } if (!sb_session_lost()) told = 0; } /* (the account dropped this sign-in: Continue Watching and the Library stop syncing until it is back) */ { int pplat_take_open_downloads(void); static int want; if (frameNo % 4 == 0 && pplat_take_open_downloads()) want = 1; if (want && app_splash_done && !player_open()) { want = 0; ph_sheet_close(); if (SCR != SCR_DISC) app_go(SCR_DISC, NULL, 1); } } /* (its notification tapped: straight to Downloads, as soon as the app is up) */ { void ph_leave_tick(void); ph_leave_tick(); } if (frameNo == 3) { void pplat_first_frame(void); pplat_first_frame(); } /* (two frames of its own drawn: Android's cover over them can go) */ ph_dl_tick(); back_tick(); tab_tick(); hold_tick(); { extern float app_hold_progress(void); float hp = app_hold_progress(); if (hp > 0) ph_hold_feel(hp); } /* a card held: its ticks */ cur ^= 1; nDrag[cur] = 0; dragFloor[cur] = 0; nScrub[cur] = 0;
  if (F.down && !F.drag && !F.pressed && !F.caught && app_now() - F.t0 >= HOLD_AFTER) { F.pressed = 1; if (sheet || app_overlay || F.y0 < barTop) app_pointer_button(1, F.x, F.y); } /* (on the bar, its row of choices or the mini player: the page under them never held) */ /* a finger that rests: OK held */
  if (app_now() - insAt < 1.5 || !(frameNo % 30)) { static int n; if (!(n++ % 6) || app_now() - insAt >= 1.5) { float t, b; pplat_insets(&t, &b); INS_T = t / pxPerUnit; INS_B = b / pxPerUnit; } } /* after a turn the system bars come back a moment later */
  keyboard_tick(); cast_pump();
}

/* ---- playing: the connected TV first ---- */
/* another app plays it (Settings > Playback > External player): a link as it is; a torrent through the engine, which
   serves its file on the phone's own address (other apps reach it too) */
int ph_ext_player_on(void) { return store_get("extPlayer", "")[0] != 0; }
static char extTitle[300]; static int extTok;
static void ext_p2p(void *ud, const char *url, const char *err, uint64_t size) { (void)size; if ((int)(intptr_t)ud != extTok) return;
  if (!url) { toast(err && *err ? err : T("No peers found for this torrent"), 2600, NULL); return; }
  const char *pk = store_get("extPlayer", ""); if (!pplat_open_external_with(url, extTitle, strcmp(pk, "ask") ? pk : "")) toast(T("No video player app on this phone."), 2600, NULL); }
void ph_play_external(const Stream *s, const char *title) { const char *pk = store_get("extPlayer", ""); if (!strcmp(pk, "ask")) pk = "";
  snprintf(extTitle, sizeof extTitle, "%s", title ? title : "");
  if (s->p2p) { if (!p2p_available()) { toast(T("Torrents play in the app, not in another one."), 2600, NULL); return; } p2p_player_closed(); extTok++; p2p_resolve(s->url, ext_p2p, (void *)(intptr_t)extTok); toast(T("Finding peers\xe2\x80\xa6"), 2400, NULL); return; }
  if (strncmp(s->url, "http", 4)) { toast(T("This stream plays in the app only"), 2400, NULL); return; }
  if (!pplat_open_external_with(s->url, extTitle, pk)) toast(T("No video player app on this phone."), 2600, NULL); }
static void (*tvPlay)(const Stream *s, const char *type, const char *id, const char *vid, int fromStart);
static void phone_play(const Stream *s, const char *type, const char *id, const char *vid, int fromStart) {
  if (cast_connected() && s->url[0] && cast_play(s, type, id, vid, fromStart ? 0 : -1)) return;
  if (ph_ext_player_on() && (s->p2p || !strncmp(s->url, "http", 4))) { char t[300]; const cJSON *m = sb_meta_cached(type, id); int se = 0, ep = 0; const char *c = vid ? strchr(vid, ':') : NULL; /* the External player setting: that app plays it */
    if (c && sscanf(c, ":%d:%d", &se, &ep) == 2) snprintf(t, sizeof t, "%s  S%d E%d", js_str(m, "name", ""), se, ep); else snprintf(t, sizeof t, "%s", js_str(m, "name", ""));
    sb_remember_stream(id, vid, s); ph_play_external(s, t); return; }
  if (s->p2p && !p2p_available()) { toast(T("Torrents play on a TV for now: pick one with the TV button."), 3000, NULL); return; } /* no engine in this build */
  if (tvPlay) tvPlay(s, type, id, vid, fromStart);
}
void phone_register(void) {
  app_register(SCR_LOGIN, &phLogin); app_register(SCR_HOME, &phHome); app_register(SCR_DISC, &phDl); /* (the slot Discover had: Downloads) */ app_register(SCR_LIB, &phLib);
  app_register(SCR_DETAIL, &phDetail); { extern Screen simScreen; app_register(SCR_SIM, &simScreen); } app_register(SCR_SEARCH, &phSearch); app_register(SCR_SET, &phSet); { extern Screen phLive; app_register(SCR_LIVE, &phLive); } app_card_menu_hook = ph_menu_show;
  tvPlay = app_play_hook; app_play_hook = phone_play; cast_init(); /* (previews: the same on mobile data as on Wi-Fi; Settings says they use more data) */ { extern int prev_available(void); prev_available(); } /* (the preview library looked for at start: Settings' Previews row never comes in under a page being swiped) */
}


/* the bar at the top of a page, once it has moved: the same glass as the bottom bar, from the screen's edge down, fading
   in with the scroll (k 0..1); its lower edge melts into the page */
void phone_top_glass(float h, float k) {
  if (k <= 0.01f) return; glass_band(0, h + 24); gfx_opacity_push(k); int gok = glassOk && !(phGhost && !phGhostGlass); /* (under a swipe with nothing grabbed for it: plain) */
  /* the glass, then one continuous shade of the page across its lower edge: deepening over its last part, easing off
     over what is under it, so the edge never reads as a line */
  if (gok) gfx_image(gb[0].tex, 0, 0, SB_W, h, 0, WHITE_A(255), 0, 1, 1, 1 - h / glassH); else gfx_rect(0, 0, SB_W, h, RGBA(26, 24, 34, 255));
  gfx_rect(0, 0, SB_W, h, RGBA(14, 12, 20, 110)); gfx_rrect_v(0, 0, SB_W, h, 0, WHITE_A(10), WHITE_A(0));
  { uint32_t pg = PAGE | 0xff000000u; float a = 0.62f; gfx_rrect_v(0, h - 40, SB_W, 40, 0, col_alpha(pg, 0), col_alpha(pg, a));
    for (int q = 0; q < 4; q++) { float u0 = q / 4.0f, u1 = (q + 1) / 4.0f; gfx_rrect_v(0, h + 56 * u0, SB_W, 56 / 4.0f + 0.5f, 0, col_alpha(pg, a * (1 - u0) * (1 - u0)), col_alpha(pg, a * (1 - u1) * (1 - u1))); } }
  gfx_opacity_pop();
}
