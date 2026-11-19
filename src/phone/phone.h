/* The phone: the app's screens laid out for a phone held upright, in the app's own look (its cards, glass,
   accent, placeholders and motion) with the structure phone apps have: a bar of tabs at the bottom (the TV
   button always among them), pages that scroll under a finger, sheets that rise from the bottom. The screen
   manager, the backdrop, the accent, the toasts and the hold-OK menu are the TV app's (core/app.c); the
   screens here take the TV screens' places. The layout is 720 units wide (1 unit ≈ 0.57 dp) and as tall as
   the screen's shape makes it. */
#ifndef SB_PHONE_H
#define SB_PHONE_H
#include <SDL2/SDL.h>
#include "core/app.h"
#include "core/hit.h"
#include "core/stremio.h"
#include "ui/gfx.h"

extern float INS_T, INS_B, phDt;  /* the status bar and the gesture bar, in units; this frame's step */
extern float phMiniH; /* the cast mini player's room over the bar (0 when none) */
#define BAR_H (124.0f + phMiniH)   /* the floating bar and its margin, above INS_B (and the cast mini player's room when it is there) */
#define PAD 28.0f
#define TXT RGB(0xe9, 0xe9, 0xee)
#define TXT2 WHITE_A(168)
#define TXT3 WHITE_A(118)
#define SURF RGB(0x1f, 0x1c, 0x28)
#define SURF2 RGB(0x2a, 0x26, 0x36)
#define F_LOGO 44
#define F_H1 46
#define F_H2 32
#define F_BODY 25
#define F_SMALL 21
#define F_TINY 18
#define POSTER_W 200.0f
#define POSTER_H 300.0f
#define WIDE_CW 320.0f
#define WIDE_CH 180.0f

/* ---- the shell ---- */
void phone_layout(int dw, int dh);
void phone_finger(const SDL_Event *e, int w, int h);
void phone_tick(float dt);
void phone_bar_draw(void);           /* the bar at the bottom (from core/app.c in place of the top bar) */
/* the row of the page's own choices, just above the bar (filters, chips): filled while the page draws */
void  ctx_row(HitFn act);            /* act(i, 0) when choice i is tapped */
void  ctx_add(const char *label, int on);
void  ctx_add_k(const char *label, int on, float k); /* k: how far it is in (0..1: it grows or shrinks in its place) */
void  ctx_slide(HitFn set, int n);   /* the first n chips: a held finger sliding across picks them */
void  ph_scrub(float x, float y, float w, float h, int n, const float *cx, const float *cw, HitFn set); /* a row of choices: held and slid, the one under the finger is picked */
void  ctx_scrolled(float pos);       /* the page's scroll: the row slides away going down, comes back going up */
void  ctx_pinned(void);
float ctx_room(void);
void phone_register(void);           /* the phone's screens into the app */
void phone_keyboard(int on);         /* the system keyboard */
void phone_out(void (*draw)(void), float k); /* a page leaving by Back (its Screen.out) */
extern Scr phonePrevScr;             /* the screen before the one being entered */

/* ---- scrolling and pressing ---- */
typedef struct { float pos, vel, max; int drag, free; int toTop; float from, to; double at; } Scroll;   /* toTop: gliding to a place (its top: the tab tapped again; a category picked) */
void scroll_top(Scroll *s);
void scroll_to(Scroll *s, float to);   /* free: no resistance past the ends (a sheet pulled down) */
void ph_drag(float x, float y, float w, float h, Scroll *s, int axis);
void ph_drag_pull(float x, float y, float w, float h, Scroll *s); /* a sheet pulled down: it takes a downward drag from a list already at its top */   /* a region the finger scrolls (axis 0 sideways, 1 up and down) */
void scroll_tick(Scroll *s);
int  ph_pressed(float x, float y, float w, float h);
int  ph_finger(float *x, float *y, float *x0, float *y0, int *held); /* the finger now: 1 while down; held: resting (a hold) */    /* the finger rests on it */
float press_k(float x, float y, float w, float h);     /* 0..1: pressed now, or just tapped (it springs back over 0.3 s) */
int  ph_dragging(void);

/* ---- sheets (one at a time, over the page, Back closes) ---- */
typedef struct { void (*draw)(float a); int (*key)(int k); void (*gone)(void); float wide; } PhSheet; /* wide: sideways, as wide as that (else a phone's width) */
void  ph_sheet_open(const PhSheet *s);
void  ph_sheet_close(void);
int   ph_sheet_on(void);
float sheet_panel(float h, float a);
void  phone_col_push(float maxW);   /* the page drawn in a column of at most maxW, in the middle (sideways, a tablet) */
void  phone_col_pop(void);
int   ph_tabw(void);               /* a tablet held sideways (the pages, not the player): laid out as a hub */
#define TABW_H 1050.0f              /* (a 7" tablet sideways in units: from here on, things grow with the screen) */
float sheet_panel_bare(float h, float a); /* (the sheet draws its own card) */
void   ph_press_cancel(void);        /* things appeared under a finger still down: none of them shows pressed */
void   ph_logo_ahead(const Item *it);  /* the title page's logo asked now (a card touched): it is there when the page opens */
double ph_last_press(void);
/* haptics that fit the motion: a hold's rising ticks (call each frame with how far it is, 0..1), its pop when it fires,
   a click on a line crossed, a thud when something goes, a light tick on a pick */
enum { HK_TICK = 1, HK_POP = 2, HK_CLICK = 3, HK_THUD = 4, HK_PICK = 5 };
void pplat_haptic_k(int kind, float s);
void ph_hold_feel(float prog);
/* downloads (downloads.c) */
enum { DL_QUEUED, DL_RUNNING, DL_DONE, DL_FAILED, DL_PAUSED, DL_CHOOSING, DL_NOSTREAM, DL_INPACK }; /* CHOOSING: its stream being found; INPACK: its file is another one's (a season pack) */
typedef struct { char id[64], type[16], vid[96], name[256], ep[200], poster[512], bg[512], url[SB_URL_MAX], file[600], group[160], gid[48]; long long size, got, speed; int state, conns; double userAt; char info[900]; int held, wifi, seq; long long rg[24][2]; int nrg; } Dl; /* rg: the parts of the file it has (a torrent: from anywhere, the film reading ahead adds its own) */ /* seq: downloaded in order (one connection): playable from the phone while it comes */ /* wifi: 1 waits for Wi-Fi, 2 held while the user is asked */ /* gid: the engine's task; speed (bytes a second), conns: now, not kept; userAt: when paused or resumed by hand (the engine catches up); info: the stream as its addon described it */
void ph_download(const Item *it, const char *vid, const char *ep);   /* as Play: the best fit at once when on, else the streams */
void ph_fly_ready(const Item *it); /* the flying bubble's poster made ahead (where Library or Download can be tapped) */
void ph_dl_add(const Item *it, const char *vid, const char *ep, const Stream *s);
int ph_dl_toggle(const char *id, const char *vid); int ph_dl_play_any(int i); /* (still coming: from its stream) */ int ph_dl_auto(void); void ph_dl_prefs(double *mbps, int *res, int *hdr); int ph_dl_waiting_wifi(void);
int  ph_dl_add_quiet(const Item *it, const char *vid, const char *ep, const Stream *s); /* (no poster flying: from the player) */
void ph_download_many(const Item *it, const char *const *vids, const char *const *eps, int n); /* each one's best fit, in turn */
void ph_episodes_download(const Item *it, const char *preselect);
void ph_download_group(const Item *it, const char *const *vids, const char *const *eps, int n, const char *group); /* several, kept as one (a season) */
void ph_download_items(const Item *its, int n, const char *group); /* several titles (a collection's films), each its best fit, kept as one */
void ph_dl_group_next(const char *id, const char *group); /* the next download of this title (its streams picked by hand) joins this group */
void ph_coll_group(const char *key, const char *name, char *out, size_t cap); /* a collection's group: "coll:<key>" and its name */
const char *ph_dl_group_name(const Dl *d); /* a group's shown name (a collection's own name) */
int ph_dl_group_is_coll(const Dl *d);
int  ph_dl_choosing(int *done, int *total, int *finished); /* a series: the sheet to pick seasons and episodes */
int  ph_dl_count(void); const Dl *ph_dl_get(int i); int ph_dl_active(void);
int  ph_dl_state(const char *id, const char *vid, float *prog);     /* -1 none, else DL_* */
void ph_dl_retry(int i); void ph_dl_tick(void); void ph_dl_remove(int i); void ph_dl_pause(int i); void ph_dl_play(int i);
void ph_streams_download(const Item *it, const char *vid, const char *ep); /* the streams sheet, a pick downloads */
void ph_fly(const char *poster, int slot);  /* a poster flies from the finger to a place of the bar (2 Library, 3 Downloads) */         /* when the last finger touched: a sheet opened under a finger skips its lift */  /* the dim, the panel, its handle; returns the panel's top */
void  choices_open(const char *title, const char **lines, int n, int cur, void (*pick)(int i));

/* ---- the kit ---- */
void  top_bar(const char *title, int back, float scrolled);
void  phone_top_glass(float h, float k);
void  ph_glass_picture(const unsigned char *rgba, int w, int h); /* glass from a picture of the screen (the player's film) */
void  ph_glass_off(void);
void  ph_glass_grab(void);   /* the page drawn so far, blurred, for ph_glass (as the bar's pills) */
void  ph_glass(float x, float y, float w, float h, float r, uint32_t tint); /* a shape of that glass (plain tint when there is none) */   /* the glass behind a page's top bar, k 0..1 */
float row_draw(const char *title, const ItemList *l, int wide, float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx);
float row_draw_k(const char *title, const ItemList *l, int wide, float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx, float *k); /* the same, opening and closing as it fills and empties */
void  card_at(const Item *it, float x, float y, float w, float h, int wide, int label, unsigned long long key, int pressed);
int   button(float x, float y, float w, float h, const char *icon, const char *label, int kind, HitFn act, int a, int b); /* kind 0 glass, 1 accent, 2 white, 3 plain */
float chip(float x, float y, const char *label, int on, HitFn act, int a, int b);
void  spinner(float cx, float cy, float r);
float ph_fit_px(const char *t, float px, int weight, float track, float maxW); /* a title's size that fits maxW (any language: shrinks, never under 60 %) */
void  text_field(float x, float y, float w, float h, const char *value, const char *hint, int focused, int secret);
/* a text being edited: the caret where you tap, dragged along; a double tap or a hold selects a word (dragging on
   from the hold, more); a small menu cuts, copies, pastes, selects all. s: its buffer (cap bytes) */
typedef struct { char *s; int cap, cur, anc, len, menu, hold, drag, ws, we, tapped; float scroll, lx, lpx, lw; double tapAt, movedAt; float tapX; int px, weight, secret, focused, lower; } TEd; /* lower: what goes in, in small letters (an email) */
int   ted_field(TEd *e, float x, float y, float w, float h, int px, int weight, uint32_t col, const char *hint, uint32_t hintCol, int focused, int secret); /* 1: tapped (focus it) */
void  ted_insert(TEd *e, const char *t);
void  ted_backspace(TEd *e);
int   ted_key(TEd *e, int k);    /* left / right: the caret */
void  ted_reset(TEd *e);         /* its text changed by other means: the caret at its end */
float poster_grid(const ItemList *l, float y, HitFn focus, int tag, int focusedIdx);
void  section_title(const char *t, float x, float y);
void  switch_draw(float x, float y, int on);
void  meta_line_draw(const Item *it, float x, float y, int px, uint32_t color);
void  text_backspace(char *s);

/* ---- the screens ---- */
extern Screen phHome, phDl, phLib, phSearch, phDetail, phSet, phLogin;
void ph_menu_show(const Item *it);                                   /* hold on a card */
void ph_streams_show(const Item *it, const char *vid, const char *epLabel);
void  ph_streams_pick(const Item *it, const char *vid, const char *epLabel); /* the list itself, even with a download of it here */
void ph_settings_open_about(void);

/* ---- the TV ---- */
void cast_init(void);
void cast_open(void);
void cast_pump(void);
int  cast_connected(void);
int  cast_icon_state(void);   /* 0 no TV seen (dim), 1 seen or remembered, 2 connecting (pulse), 3 connected (glow) */
const char *cast_tv_name(void);
int  cast_play(const Stream *s, const char *type, const char *id, const char *vid, double startSec);

/* ---- the platform's phone part ---- */
float pplat_density(void);
void  pplat_insets(float *top, float *bottom);
float pplat_keyboard(void);          /* the keyboard's height in pixels (0 = down) */
extern float KB_H;                   /* the keyboard's height in units: what sits at the bottom rides above it */
int   pplat_open_player(const char *url, const char *title, const char *headers, double startSec, const char *key);
int   pplat_open_external(const char *url, const char *title);
int   pplat_open_url(const char *url);
int   pplat_open_external_with(const char *url, const char *title, const char *pkg); /* pkg "" : the phone's chooser */
int   pplat_video_players(char *out, int cap);  /* "label\tpackage\n" lines */
void  ph_play_external(const Stream *s, const char *title); /* in the chosen app (Settings), a torrent through the engine's own address */
int   ph_ext_player_on(void);   /* a web address: its app (IMDb's, when installed) or the browser */
void  pplat_multicast(int on);
int   pplat_atv_open(const char *ip, const char *link);      /* an Android TV's remote: open the link (0), pairing needed (1), not reachable (-1) */
int   pplat_atv_pair_start(const char *ip, const char *name); /* 0: the TV shows a 6-character code */
int   pplat_atv_pair_finish(const char *code);                /* 0 paired, 1 not that code, -1 failed */
void  pplat_atv_pair_close(void);
int   pplat_progress(char *key, int cap, double *pos, double *dur, int *state);
#endif
