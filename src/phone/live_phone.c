/* Live TV on a phone: the TV's live module (core/live.c: the channels of every live catalog, tried in the background,
   started on a stream that sends a picture, the line-up, favourites and recent ones) with the phone's own faces:
   - Home's Live TV row: round badges (All channels, the favourites, the recent, some to discover), a tap tunes in, a
     hold opens the channel's menu;
   - All channels (SCR_LIVE): a page of its own, swiped back like any page: the preview at the top playing the chosen
     channel once the finger rests (core's livepeek: the picture under a see-through box), its state, name and
     catalog, Play and Favorite; the catalogs as pills (All channels, Favorites, Recent, each addon's, A–Z) that stay
     under the top as the page goes up; the channels as tiles in their logos' own colours, LIVE or Off air on each
     once tried; a tap chooses (it previews), a tap on the chosen one or on the preview watches it, a hold its menu;
   - over a channel playing: the player's Channels panel (player_phone.c: its glass, title and close) with the same
     pills and the channels as rows; a tap switches, the star keeps it.
   The live hub's interface (screens/livehub.h) is answered here: the TV's livehub.c is not in a phone build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/live.h"
#include "screens/livehub.h"
#include "screens/livepeek.h"
extern int phGhost; int pplat_held(void);
#include "screens/player.h"
#include "core/video.h"
#include "ui/livewall.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/palette.h"
#include "ui/glass.h"

#define DK RGB(10, 9, 15)
#define GOLD RGB(0xf5, 0xc5, 0x18)

/* ---- the channels' colours and marks (as the TV's tiles) ---- */
typedef struct { char url[512]; uint32_t c1, c2; int ok; double at; } Tint; /* at: when its colours were found (they fade in from then) */
static Tint tints[256];
static void tint_of(const char *url, uint32_t *c1, uint32_t *c2) {
  *c1 = RGB(42, 36, 62); *c2 = RGB(16, 14, 24); if (!url || !*url) return;
  unsigned h = 2166136261u; for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; Tint *t = &tints[h & 255];
  if (t->ok && !strcmp(t->url, url)) { *c1 = t->c1; *c2 = t->c2; return; }
  Palette p; int r = img_palette(url, 64, 64, 1, &p); if (r == 0) return;
  snprintf(t->url, sizeof t->url, "%s", url); t->ok = 1; t->at = app_now();
  if (r < 0) { t->c1 = *c1; t->c2 = *c2; return; }
  const Swatch *v = p.vibrant.ok ? &p.vibrant : p.muted.ok ? &p.muted : p.light.ok ? &p.light : NULL, *d = p.darkMuted.ok ? &p.darkMuted : p.dark.ok ? &p.dark : v;
  if (v) t->c1 = hsl2rgb(v->h, fminf(v->s, 0.62f), 0.30f); if (d) t->c2 = hsl2rgb(d->h, fminf(d->s, 0.5f), 0.11f);
  *c1 = t->c1; *c2 = t->c2;
}
/* how far a logo's colours have come in: 0 not found yet, then up to 1 over a third of a second */
static float tint_k(const char *url) { if (!url || !*url) return 1; unsigned h = 2166136261u; for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; Tint *t = &tints[h & 255];
  if (!t->ok || strcmp(t->url, url)) return 0; float k = (float)((app_now() - t->at) / 0.34); return k >= 1 ? 1 : k <= 0 ? 0 : k * k * (3 - 2 * k); }
static const char *art_of(const LiveCh *c) { return c->logo[0] ? c->logo : c->poster; }
static int fit_logo(const char *url, float mw, float mh, float *w, float *h, GLuint *t) {
  float asp; *t = url && *url ? img_get(url, (int)mw, (int)mh, &asp) : 0; if (!*t || asp <= 0) return 0;
  *w = mw; *h = mw / asp; if (*h > mh) { *h = mh; *w = mh * asp; } return 1; }
/* a channel's face in its box: its logo on its own colours; while the logo comes, the app's waiting shimmer with the
   name soft on it (the logo fading in over it once there); a channel with no logo (or one that would not load): a colour
   of its own from its name, the name on it */
static uint32_t name_hue(const char *n, float light) { unsigned h = 2166136261u; for (const char *p = n; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
  static const uint32_t P[8] = { 0xff5b3f8cu, 0xff2f5d8au, 0xff2c7a6bu, 0xff8a4b2fu, 0xff7a2f55u, 0xff3f6f3au, 0xff6b3f8au, 0xff8a6b2fu }; return col_mix(P[h & 7] | 0xff000000u, 0xff0c0b11u, 1 - light); }
static void ch_face(const LiveCh *c, float x, float y, float w, float h, float r, float lmw, float lmh, float fs) {
  const char *art = art_of(c); int has = art && *art, failed = has && img_failed(art); float lw, lh; GLuint t = 0; int got = has && !failed && fit_logo(art, w * lmw, h * lmh, &lw, &lh, &t);
  float cx = x + w / 2, cy = y + h / 2;
  if (got) { uint32_t c1, c2; tint_of(art, &c1, &c2); float f = fminf(gfx_tex_fade(t), tint_k(art)); /* (the logo and its colours together: never a logo on the waiting colour that then changes) */
    if (f < 1) { ph_box(x, y, w, h, r); if (f < 0.5f) text_draw(c->name, cx, cy - text_line_height(fs) / 2, fs, 700, WHITE_A((int)(110 * (1 - 2 * f))), 0, w - 20, ALIGN_CENTER); }
    if (f > 0.001f) { gfx_opacity_push(f); gfx_rrect_v(x, y, w, h, r, c1, c2); gfx_rrect_v(x, y, w, h * 0.5f, r, WHITE_A(18), WHITE_A(0));
      gfx_scale_push(0.94f + 0.06f * f, 0.94f + 0.06f * f, cx, cy); gfx_image(t, cx - lw / 2, cy - lh / 2, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1); gfx_scale_pop(); gfx_opacity_pop(); }
    return; }
  if (has && !failed) { ph_box(x, y, w, h, r); text_draw(c->name, cx, cy - text_line_height(fs) / 2, fs, 700, WHITE_A(110), 0, w - 20, ALIGN_CENTER); return; } /* (coming) */
  gfx_rrect_v(x, y, w, h, r, name_hue(c->name, 0.62f), name_hue(c->name, 0.28f)); gfx_rrect_v(x, y, w, h * 0.5f, r, WHITE_A(20), WHITE_A(0)); gfx_ring(x, y, w, h, r, 1, WHITE_A(22));
  text_draw(c->name, cx, cy - text_line_height(fs) / 2, fs, 800, WHITE_A(240), 0, w - 20, ALIGN_CENTER); }
static void live_dot(float x, float y, float r) { float pu = 0.5f + 0.5f * sinf((float)app_now() * 3.2f); gfx_circle(x, y, r + 5 * pu, RGBA(0xe5, 0x2b, 0x2b, (int)(70 * (1 - pu)))); gfx_circle(x, y, r, RGB(0xef, 0x3b, 0x3b)); }
static void eq_bars(float x, float y, float h, uint32_t c) { double t = app_now(); for (int i = 0; i < 3; i++) { float k = 0.35f + 0.65f * (0.5f + 0.5f * sinf((float)t * (5.5f + i * 1.7f) + i)); gfx_rrect(x + i * 7, y + h - h * k, 4, h * k, 2, c); } }
static void off_sign(float x, float y, float is, uint32_t under) { /* the broadcast sign struck through */
  icon_draw("wn_live", x, y, is, WHITE_A(150));
  for (int i = 0; i <= 12; i++) { float t = i / 12.f; gfx_circle(x + is * 0.12f + is * 0.76f * t, y + is * 0.88f - is * 0.76f * t, is * 0.09f, under); }
  for (int i = 0; i <= 12; i++) { float t = i / 12.f; gfx_circle(x + is * 0.12f + is * 0.76f * t, y + is * 0.88f - is * 0.76f * t, is * 0.045f, WHITE_A(170)); } }
/* a channel's mark on a tile or row: LIVE (red, breathing) or off air, only once tried, fading in as it comes */
static void state_mark(const char *id, float x, float y, float r, int playing) {
  int st = playing ? 1 : live_reach(id);
  static struct { char id[64]; int st; double at; } MK[256]; unsigned hh = 2166136261u; for (const char *p = id; *p; p++) hh = (hh ^ (unsigned char)*p) * 16777619u;
  int mi = hh & 255; if (strcmp(MK[mi].id, id) || MK[mi].st != st) { snprintf(MK[mi].id, sizeof MK[mi].id, "%s", id); MK[mi].st = st; MK[mi].at = app_now(); }
  float mk = (float)((app_now() - MK[mi].at) / 0.35); mk = mk < 0 ? 0 : mk > 1 ? 1 : mk; mk = mk * mk * (3 - 2 * mk); if (mk <= 0.003f || !st) return;
  gfx_opacity_push(mk);
  if (st > 0) live_dot(x, y, r);
  else { gfx_circle(x, y, r * 1.9f, RGBA(12, 11, 17, 170)); off_sign(x - r * 1.4f, y - r * 1.4f, r * 2.8f, RGBA(12, 11, 17, 255)); }
  gfx_opacity_pop(); }

/* ---- the catalogs as tabs (pointing at core's channels; the favourites and recent ones their own small copies) ---- */
#define MAXTAB 70
typedef struct { char name[96]; const LiveCh **p; LiveCh *own; int n; } Tab;
static Tab tab[MAXTAB]; static int ntab, tabTot; static unsigned tabSig;
static void tab_free(Tab *t) { free(t->p); free(t->own); memset(t, 0, sizeof *t); }
static Tab *tab_new(const char *name, int n) { if (ntab >= MAXTAB || n <= 0) return NULL; Tab *t = &tab[ntab]; memset(t, 0, sizeof *t); t->p = malloc(sizeof(LiveCh *) * (size_t)n); if (!t->p) return NULL; snprintf(t->name, sizeof t->name, "%s", name); ntab++; return t; }
static void tab_items(const char *name, const ItemList *l) {
  Tab *t = tab_new(name, l->n); if (!t) return; t->own = calloc((size_t)l->n, sizeof(LiveCh)); if (!t->own) { ntab--; free(t->p); return; }
  for (int i = 0; i < l->n; i++) { LiveCh *c = &t->own[i]; const Item *it = &l->items[i]; snprintf(c->id, sizeof c->id, "%s", it->id); snprintf(c->type, sizeof c->type, "%s", it->type); snprintf(c->name, sizeof c->name, "%s", it->name);
    snprintf(c->logo, sizeof c->logo, "%s", it->logo); snprintf(c->poster, sizeof c->poster, "%s", it->poster[0] ? it->poster : it->background); t->p[t->n++] = c; } }
static int cmp_name(const void *a, const void *b) { return strcasecmp((*(const LiveCh *const *)a)->name, (*(const LiveCh *const *)b)->name); }
static const char *short_name(const char *t, char *buf, size_t cap) { snprintf(buf, cap, "%s", t); char *c = strstr(buf, " · "); if (c) *c = 0; return buf; } /* "TvVoo • Italia · tv" -> "TvVoo • Italia" */
static int all_total(void) { int tot = 0; for (int c = 0; c < live_catalogs(); c++) { int n = live_catalog(c, NULL, NULL); if (n > 0) tot += n; } return tot; }
static unsigned kept_sig(void) { /* the favourites and recent ones as they are: a change rebuilds their tabs */
  ItemList p, r; live_pins(&p); live_recent(&r); unsigned h = 2166136261u;
  for (int i = 0; i < p.n; i++) for (const char *q = p.items[i].id; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u; h = (h ^ 0xffu) * 16777619u;
  for (int i = 0; i < r.n; i++) for (const char *q = r.items[i].id; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u;
  itemlist_clear(&p); itemlist_clear(&r); return h; }
static void tabs_build(void) {
  for (int i = 0; i < ntab; i++) tab_free(&tab[i]); ntab = 0;
  int tot = all_total(); Tab *all = tab_new(T("All channels"), tot);
  if (all) for (int c = 0; c < live_catalogs(); c++) { const LiveCh *ch; int n = live_catalog(c, NULL, &ch); for (int i = 0; i < n; i++) all->p[all->n++] = &ch[i]; }
  int allAt = all ? ntab - 1 : -1;
  ItemList p; if (live_pins(&p)) tab_items(T("Favorites"), &p); itemlist_clear(&p);
  ItemList r; if (live_recent(&r)) tab_items(T("Recent"), &r); itemlist_clear(&r);
  for (int c = 0; c < live_catalogs(); c++) { const char *tt; const LiveCh *ch; char b[96]; int n = live_catalog(c, &tt, &ch); if (n <= 0) continue; Tab *t = tab_new(short_name(tt, b, sizeof b), n); if (t) for (int i = 0; i < n; i++) t->p[t->n++] = &ch[i]; }
  if (allAt >= 0 && tab[allAt].n > 1) { Tab *az = tab_new("A–Z", tab[allAt].n); if (az) { memcpy(az->p, tab[allAt].p, sizeof(LiveCh *) * (size_t)tab[allAt].n); az->n = tab[allAt].n; qsort(az->p, (size_t)az->n, sizeof(LiveCh *), cmp_name); } }
  tabTot = tot; tabSig = kept_sig();
}
/* the tabs as they should be now: built again when the catalogs grew or a favourite or recent one changed (a tab is
   known by its name: the one shown stays) */
static int tabs_fresh(int *cur) {
  static double at; if (ntab && app_now() - at < 0.5) return 0; at = app_now();
  if (ntab && all_total() == tabTot && kept_sig() == tabSig) return 0;
  char keep[96] = ""; if (cur && *cur >= 0 && *cur < ntab) snprintf(keep, sizeof keep, "%s", tab[*cur].name);
  tabs_build(); if (cur) { *cur = 0; for (int i = 0; i < ntab; i++) if (keep[0] && !strcmp(tab[i].name, keep)) *cur = i; } return 1; }
static int tab_find(int t, const char *id) { if (t < 0 || t >= ntab || !id || !*id) return -1; for (int i = 0; i < tab[t].n; i++) if (!strcmp(tab[t].p[i]->id, id)) return i; return -1; }
static const LiveCh *ch_find(const char *id) { for (int t = 0; t < ntab; t++) { int i = tab_find(t, id); if (i >= 0) return tab[t].p[i]; } return NULL; }
static void fav_toggle(const LiveCh *c) { if (!c) return; Item it; live_item(c, &it); int on = !live_pinned(it.id); live_pin(&it, on); toast(T(on ? "Added to favorites" : "Removed from favorites"), 1600, NULL); pplat_haptic_k(HK_PICK, 0.7f); }
static char playing[64]; /* the channel the player plays (hub_playing) */
static int is_playing(const char *id) { return player_open() && playing[0] && !strcmp(playing, id); }

/* the catalogs' pills: one under the chosen one, gliding to it; each name carries its count, fainter */
typedef struct { Tween x, w; int set; float px[MAXTAB], pw[MAXTAB], ax[MAXTAB], aw[MAXTAB], in[MAXTAB]; char nm[MAXTAB][24]; } Pills; /* ax, aw: where each is drawn, gliding to px, pw (a pill added or gone: the others move over) */
static float pills_draw(Pills *P, float x0, float y, float w, Scroll *s, int cur, HitFn pick, float k) {
  const float th = 66; float x = x0 + 4 - s->pos, tot = 0; ph_drag(x0, y, w, th, s, 0);
  float gk = clampf(phDt * 12, 0, 1); Pills old; memcpy(&old, P, sizeof old); /* (each found by its name among last frame's: none overwritten before it is looked up) */
  for (int i = 0; i < ntab; i++) { char cn[16]; snprintf(cn, sizeof cn, "%d", tab[i].n); float pw = 26 + text_width(tab[i].name, 26, 700, 0) + 10 + text_width(cn, 21, 600, 0.4f) + 26; P->px[i] = x - x0 + s->pos; P->pw[i] = pw; tot += pw + 10; x += pw + 10;
    int same = !strncmp(P->nm[i], tab[i].name, 23); if (!P->set) { P->ax[i] = P->px[i]; P->aw[i] = pw; P->in[i] = 1; } else if (!same) { int from = -1; for (int j = 0; j < MAXTAB; j++) if (!strncmp(old.nm[j], tab[i].name, 23)) { from = j; break; } if (from >= 0 && from != i) { float fx = old.ax[from], fw = old.aw[from], fi = old.in[from]; P->ax[i] = fx; P->aw[i] = fw; P->in[i] = fi; } else { P->ax[i] = P->px[i]; P->aw[i] = 0; P->in[i] = 0; } } /* (a new one: it opens from nothing where it goes, its words coming in as it widens) */
    snprintf(P->nm[i], sizeof P->nm[i], "%.23s", tab[i].name); P->ax[i] += (P->px[i] - P->ax[i]) * gk; P->aw[i] += (pw - P->aw[i]) * gk; P->in[i] += (1 - P->in[i]) * clampf(phDt * 8, 0, 1); } /* (each glides from where it was) */
  s->max = tot - 10 + 8 - w; if (s->max < 0) s->max = 0; scroll_tick(s);
  gfx_clip_push(x0, y - 8, w, th + 16); gfx_opacity_push(k);
  for (int i = 0; i < ntab; i++) { float px = x0 + P->ax[i] - s->pos, aw = P->aw[i] < th * 0.5f ? th * 0.5f : P->aw[i]; gfx_opacity_push(P->in[i]); gfx_rrect(px, y, aw, th, th / 2, WHITE_A(16)); gfx_ring(px, y, aw, th, th / 2, 1, WHITE_A(30)); gfx_opacity_pop(); }
  if (cur >= 0 && cur < ntab) { if (!P->set) { tween_set(&P->x, P->px[cur]); tween_set(&P->w, P->pw[cur]); P->set = 1; }
    else { if (fabsf(P->x.to - P->px[cur]) > 0.5f) tween_to(&P->x, P->px[cur], 0.3f, EASE_WEB); if (fabsf(P->w.to - P->pw[cur]) > 0.5f) tween_to(&P->w, P->pw[cur], 0.3f, EASE_WEB); }
    tween_tick(&P->x, phDt); tween_tick(&P->w, phDt); lglass_accent(x0 + P->x.v - s->pos, y, P->w.v, th, th / 2, 1); }
  for (int i = 0; i < ntab; i++) { float px = x0 + P->ax[i] - s->pos; if (px > x0 + w || px + P->pw[i] < x0) continue;
    float c = cur >= 0 && cur < ntab ? clampf(1 - fabsf(P->px[i] - P->x.v) / (P->pw[i] > 1 ? P->pw[i] : 1), 0, 1) : 0; uint32_t tc = col_mix(WHITE_A(200), ACC_FG, c); char cn[16]; snprintf(cn, sizeof cn, "%d", tab[i].n);
    float ti = P->in[i] * P->in[i] * clampf(P->aw[i] / (P->pw[i] > 1 ? P->pw[i] : 1), 0, 1); gfx_opacity_push(ti); gfx_clip_push(px, y - 4, P->aw[i], th + 8);
    float nw = text_draw(tab[i].name, px + 26, y + th / 2 - text_line_height(26) / 2.0f, 26, 700, tc, 0, 0, ALIGN_LEFT);
    text_draw(cn, px + 26 + nw + 10, y + th / 2 - text_line_height(21) / 2.0f, 21, 600, col_alpha(tc, 0.6f), 0.4f, 0, ALIGN_LEFT); gfx_clip_pop(); gfx_opacity_pop();
    if (pick && k > 0.5f) hit_act(px, y, P->pw[i], th, pick, i, 0); }
  gfx_opacity_pop(); gfx_clip_pop();
  if (cur >= 0 && cur < ntab && !s->drag && fabsf(s->vel) < 1) { static int last = -1; static Pills *lp; if (last != cur || lp != P) { last = cur; lp = P; float l = P->px[cur], r = l + P->pw[cur]; if (r - s->pos > w - 20) s->pos = fminf(s->max, r - w + 40); if (l - s->pos < 0) s->pos = fmaxf(0, l - 30); } } /* (the chosen one in view) */
  return th; }

/* ================= All channels: the page ================= */
static struct { int cur, tapWas, entered; char sel[64]; Scroll v, tv; double openAt, swapAt, selAt; Pills pl; char prevSel[64]; float lastTop; } PG;
static const Tab *pg_tab(void) { return PG.cur >= 0 && PG.cur < ntab ? &tab[PG.cur] : NULL; }
static const LiveCh *pg_sel(void) { const Tab *t = pg_tab(); int i = tab_find(PG.cur, PG.sel); if (t && i >= 0) return t->p[i]; return PG.sel[0] ? ch_find(PG.sel) : NULL; }
static void pg_select(const LiveCh *c) { if (!c || !strcmp(PG.sel, c->id)) return; snprintf(PG.prevSel, sizeof PG.prevSel, "%s", PG.sel); snprintf(PG.sel, sizeof PG.sel, "%s", c->id); PG.selAt = app_now(); }
static int sideArmed; static double returnedAt; /* sideArmed: the phone was upright on the page (turning it sideways opens the player) */
static void pg_watch(void) {
  const LiveCh *c = pg_sel(); if (!c) return; const Tab *t = pg_tab(); int i = tab_find(PG.cur, c->id);
  pplat_haptic_k(HK_TICK, 0.6f);
  if (t && i >= 0) live_start_list(t->p, t->n, i, 0); else { Item it; live_item(c, &it); live_start(&it, NULL, 0, 0); } }
static void pg_tab_pick(int i, int b) { (void)b; if (i < 0 || i >= ntab || i == PG.cur) return; PG.cur = i; PG.swapAt = app_now(); pplat_haptic_k(HK_TICK, 0.4f);
  float gy = PG.lastTop; if (PG.v.pos > gy) PG.v.pos = gy; PG.v.vel = 0; /* (the new list from its top, the pills where they were) */
  if (tab_find(i, PG.sel) < 0 && tab[i].n) pg_select(tab[i].p[0]); }
static char touchId[64]; static double touchAt; /* the tile under the finger: a tap previews it (on lifting), a hold opens its menu and leaves the preview alone */
static void tile_focus(int i, int b) { (void)b; const Tab *t = pg_tab(); if (!t || i < 0 || i >= t->n) return; PG.tapWas = 0; snprintf(touchId, sizeof touchId, "%s", t->p[i]->id); touchAt = app_now(); }
static void pg_grow(void);
static void play_act(int a, int b) { (void)a; (void)b; pg_grow(); }
static void fav_act(int a, int b) { (void)a; (void)b; fav_toggle(pg_sel()); }
/* Play and Favorite: two circles, Play white with its triangle, Favorite glass with its star that fills with the
   theme's colour when on, the star popping */
#define ACTS_W (76 * 2 + 12)
static void acts_row(float x, float y, const char *id, int fav) {
  float h = 76, gap = 12; static Tween onT; static char onId[64]; if (strcmp(onId, id)) { snprintf(onId, sizeof onId, "%s", id); tween_set(&onT, fav); } /* (another channel: as it is, no animation) */
  tween_to(&onT, fav, 0.38f, EASE_OUT); tween_tick(&onT, phDt); float ok = onT.v;
  { float pk = press_k(x, y, h, h), ks = 1 - 0.06f * pk; gfx_scale_push(ks, ks, x + h / 2, y + h / 2);
    gfx_circle(x + h / 2, y + h / 2, h / 2, WHITE_A(245)); icon_draw("play", x + h / 2 - 18, y + h / 2 - 20, 40, RGB(14, 12, 20)); gfx_scale_pop(); hit_act(x, y, h, h, play_act, 0, 0); x += h + gap; }
  { float pk = press_k(x, y, h, h), ks = 1 - 0.06f * pk; gfx_scale_push(ks, ks, x + h / 2, y + h / 2);
    gfx_rrect(x, y, h, h, h / 2, WHITE_A(pk > 0 ? (int)(18 + 22 * pk) : 14)); if (ok > 0.01f) gfx_rrect(x, y, h, h, h / 2, col_alpha(ACC, 0.36f * ok)); gfx_ring(x, y, h, h, h / 2, 1, WHITE_A((int)(26 * (1 - ok))));
    uint32_t c = col_mix(WHITE_A(235), ACC3 | 0xff000000u, ok); float icx = x + h / 2 - 20, icy = y + h / 2 - 20;
    float pop = 1 + 0.22f * sinf(ok * (float)M_PI) * (onT.to != onT.v || (ok > 0.02f && ok < 0.98f)); gfx_scale_push(pop, pop, icx + 20, icy + 20); icon_draw("star", icx, icy, 40, c); gfx_scale_pop();
    gfx_scale_pop(); hit_act(x, y, h, h, fav_act, 0, 0); } }

static void pg_enter(const void *params) { (void)params; extern int app_entered_by_swipe; live_refresh(0);
  if (app_entered_by_swipe && ntab) { PG.entered = 1; return; } /* (back to it: as it was) */
  if (!ntab) tabs_build(); else tabs_fresh(&PG.cur);
  PG.cur = 0; PG.v.pos = 0; PG.v.vel = 0; PG.tv.pos = 0; PG.pl.set = 0; PG.openAt = PG.swapAt = app_now(); PG.entered = 1; PG.sel[0] = 0;
  if (playing[0] && tab_find(0, playing) >= 0) snprintf(PG.sel, sizeof PG.sel, "%s", playing); else if (ntab && tab[0].n) snprintf(PG.sel, sizeof PG.sel, "%s", tab[0].p[0]->id);
  fprintf(stderr, "[live] All channels: %d tabs, %d channels\n", ntab, ntab ? tab[0].n : 0); }
static void pg_leave(void) { PG.entered = 0; }
static float in_k(double delay, double dur) { double t = (app_now() - PG.openAt - delay) / dur; if (t <= 0) return 0; if (t >= 1) return 1; float k = (float)t; return 1 - (1 - k) * (1 - k) * (1 - k); }
static void pg_tile(const LiveCh *c, float x, float y, float w, float h, int sel, float hold) {
  float pk = press_k(x, y, w, h), s = 1 - 0.04f * pk, cx = x + w / 2, cy = y + h / 2, r = 16;
  gfx_scale_push(s, s, cx, cy);
  ch_face(c, x, y, w, h, r, 0.62f, 0.56f, 22);
  state_mark(c->id, x + 16, y + 16, 5, is_playing(c->id));
  if (live_pinned(c->id)) { gfx_circle(x + w - 20, y + 20, 14, RGBA(12, 11, 17, 190)); icon_draw("star", x + w - 29, y + 11, 18, GOLD); }
  if (is_playing(c->id)) { gfx_rrect(x + w - 44, y + h - 32, 32, 22, 8, RGBA(12, 11, 17, 190)); eq_bars(x + w - 38, y + h - 28, 14, ACC2); }
  if (sel) gfx_ring_out(x, y, w, h, r, 3, ACC2);
  if (hold > 0) hold_trace_draw(x, y, w, h, r, ease_apply(EASE_IN_OUT, hold));
  gfx_scale_pop(); }
static int cols_for(float w) { return w >= 1300 ? 6 : w >= 1050 ? 5 : w >= 860 ? 4 : w >= 520 ? 3 : 2; }
/* the grid of channels in a column (x, w), from y (the page's scroll in it), clipped from clipY down */
static float pg_grid(const LiveCh *f, float x0, float w, float y, float clipY, int hits) {
  double now = app_now(); const Tab *t = pg_tab(); int cols = cols_for(w); float gap = 14, tw = (w - gap * (cols - 1)) / cols, th = tw * 9 / 16, pitch = th + 12 + text_line_height(F_SMALL) + 22;
  gfx_clip_push(x0 - 8, clipY, w + 16, SB_H - clipY);
  if (t) { const char *ids[48], *tys[48]; int nv = 0; if (f && !is_playing(f->id)) { ids[nv] = f->id; tys[nv++] = f->type; }
    float hold = app_hold_progress();
    for (int i = 0; i < t->n; i++) { int row = i / cols, col = i % cols; float x = x0 + col * (tw + gap), yy = y + row * pitch;
      if (yy + pitch < clipY || yy > SB_H) continue;
      int vr = (int)((yy - clipY) / pitch); if (vr < 0) vr = 0; float d = (vr * cols + col) * 0.02f; d = d > 0.3f ? 0.3f : d;
      float e = (float)((now - PG.swapAt - d) / 0.28); e = clampf(e, 0, 1); e = ease_apply(EASE_CSS_EASEOUT, e);
      int sel = f && t->p[i] == f; if (!sel && f && !strcmp(t->p[i]->id, f->id)) sel = 1;
      if (nv < 48 && !sel) { ids[nv] = t->p[i]->id; tys[nv++] = t->p[i]->type; }
      gfx_opacity_push(e); gfx_offset_push(0, 14 * (1 - e));
      pg_tile(t->p[i], x, yy, tw, th, sel, touchId[0] && !strcmp(t->p[i]->id, touchId) ? hold : 0); /* (the hold traced on the tile held) */
      text_draw(t->p[i]->name, x + 2, yy + th + 12, F_SMALL, sel ? 700 : 600, sel ? WHITE_A(255) : TXT2, 0, tw - 4, ALIGN_START);
      gfx_offset_pop(); gfx_opacity_pop();
      if (hits && yy + th > clipY) hit_add(x, yy < clipY ? clipY : yy, tw, yy < clipY ? th - (clipY - yy) : th, tile_focus, NULL, i, 0); }
    if (hits && !phGhost && SCR == SCR_LIVE) live_probe_visible(ids, tys, nv);
    y += ((t->n + cols - 1) / cols) * pitch; }
  if (!t || !t->n) { spinner(x0 + w / 2, y + 70, 22); y += 160; }
  gfx_clip_pop(); return y; }
/* the channel's state, small, after its name: LIVE (a red pill, its dot breathing slowly with a soft glow), being
   checked (a small turning arc), or Off air (a faint pill); one fades into the next. Returns its width at full */
static float name_mark(float x, float cy, float *k3, int st) {
  float sp = fminf(1, phDt * 7); k3[0] += ((st == 1) - k3[0]) * sp; k3[1] += ((st == -1) - k3[1]) * sp; k3[2] += ((st == 0) - k3[2]) * sp;
  double now = app_now(); float h = 30, px = 16; float br = 0.5f - 0.5f * cosf((float)fmod(now / 2.2, 1) * 6.2832f); /* (one slow breath every 2.2 s) */
  float lw = text_width(T("LIVE"), px, 800, 1.6f), cw = 14 + 8 + 8 + lw + 12, ty = cy - text_line_height(px) / 2.0f, w = 0;
  if (k3[0] > 0.01f) { gfx_opacity_push(k3[0]); float y = cy - h / 2;
    gfx_shadow(x, y, cw, h, h / 2, 10 + 8 * br, RGBA(0xff, 0x3b, 0x3b, (int)(40 + 50 * br))); /* (its glow rising and falling with the breath) */
    gfx_rrect_v(x, y, cw, h, h / 2, RGB(0xf2, 0x3a, 0x3a), RGB(0xc8, 0x1e, 0x1e));
    float dx = x + 14 + 4; gfx_circle(dx, cy, 4 + 3.5f * br, WHITE_A((int)(70 * (1 - br)))); gfx_circle(dx, cy, 4, WHITE_A((int)(190 + 65 * br)));
    text_draw(T("LIVE"), x + 14 + 8 + 8, ty, px, 800, WHITE_A(255), 1.6f, 0, ALIGN_LEFT); gfx_opacity_pop(); w = fmaxf(w, cw * k3[0]); }
  if (k3[2] > 0.01f) { gfx_opacity_push(k3[2]); float ph = (float)fmod(now / 0.9, 1) * 6.2832f, ccx = x + 12; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(ccx + cosf(an) * 7, cy + sinf(an) * 7, 1.5f, WHITE_A(150)); } gfx_opacity_pop(); w = fmaxf(w, 24 * k3[2]); }
  if (k3[1] > 0.01f) { gfx_opacity_push(k3[1]); const char *lb = T("Off air"); float ow = text_width(lb, px, 700, 0.4f) + 28; gfx_rrect(x, cy - h / 2, ow, h, h / 2, WHITE_A(18)); gfx_ring(x, cy - h / 2, ow, h, h / 2, 1, WHITE_A(30));
    text_draw(lb, x + 14, ty, px, 700, WHITE_A(150), 0.4f, 0, ALIGN_LEFT); gfx_opacity_pop(); w = fmaxf(w, ow * k3[1]); }
  return w; }
/* the chosen channel's words: its name with its state after it, its catalog (crossfading as it changes), Play and Favorite */
static float info_h(void) { return text_line_height(F_H1 - 4) + 4 + text_line_height(F_SMALL + 1) + 8; }
static void pg_info(const LiveCh *f, float IX, float y, float IW) { double now = app_now();
  static float k3[3]; static char kid[64]; if (f && strcmp(kid, f->id)) { snprintf(kid, sizeof kid, "%s", f->id); k3[0] = k3[1] = k3[2] = 0; }
  int st = f ? (is_playing(f->id) ? 1 : live_reach(f->id)) : 0;
  float xf = (float)((now - PG.selAt) / 0.3); xf = clampf(xf, 0, 1); xf = xf * xf * (3 - 2 * xf);
  const LiveCh *pc = PG.prevSel[0] ? ch_find(PG.prevSel) : NULL; float ny = y, NW = IW - ACTS_W - 24, lh = text_line_height(F_H1 - 4); /* (the words beside the two buttons) */
  for (int w = pc && xf < 1 ? 0 : 1; w < 2; w++) { const LiveCh *c = w ? f : pc; float a = w ? xf : 1 - xf; if (!c || a <= 0.003f) continue; float yy = ny + (w ? 8 * (1 - xf) : -5 * xf);
    float mk = 132, tw = text_width(c->name, F_H1 - 4, 800, -0.6f); if (tw > NW - mk) tw = NW - mk; /* (room kept for the mark after the name) */
    gfx_opacity_push(a); text_draw(c->name, IX, yy, F_H1 - 4, 800, WHITE_A(255), -0.6f, NW - mk, ALIGN_START);
    if (w) name_mark(IX + tw + 14, yy + lh * 0.52f, k3, st);
    const char *cat = live_catalog_of(c->id); char cb[96]; text_draw(cat ? short_name(cat, cb, sizeof cb) : "", IX, yy + lh + 4, F_SMALL + 1, 600, TXT2, 0, NW, ALIGN_START); gfx_opacity_pop(); }
  float nh = lh + 4 + text_line_height(F_SMALL + 1);
  acts_row(IX + IW - ACTS_W, ny + nh / 2 - 38, f ? f->id : "", f && live_pinned(f->id)); }
/* the preview to the whole screen and back (fullK 0 its box, 1 the screen): Play grows it, then the player takes it as
   it plays; given back, it shrinks into its box. A phone turned sideways: the screen at once (the turn is the change) */
int phone_player_turning(void); int player_from_preview(void);
static Tween fullT; static int growing, backWait; static double sideAt; static int lastSide = -1;
static void pg_grow(void) { const LiveCh *c = pg_sel(); if (!c || player_open()) return; if (!lpk_shown(c->id)) { pg_watch(); return; } if (growing) return; growing = 1; tween_to(&fullT, 1, SB_W > SB_H ? 0.42f : 0.34f, EASE_WEB); pplat_haptic_k(HK_TICK, 0.6f); }
float ph_live_full(void) { extern int phone_player_turning(void); return player_open() && player_from_preview() && phone_player_turning() ? 1 : fullT.v; }
static float pvWant[4]; static unsigned pvWantF; extern unsigned app_frame_no(void);
/* the page swiped back as a card (drawn into a picture first, then placed): the preview's picture goes where its box is on that card, playing on */
void ph_live_on_card(float cx, float cy, float k, float r) { (void)r; if (SCR != SCR_LIVE || pvWantF != app_frame_no() || pvWant[2] < 1) return; const LiveCh *f = pg_sel(); if (!f) return; Item it; live_item(f, &it);
  float x = cx + pvWant[0] * k, y = cy + pvWant[1] * k, w = pvWant[2] * k, h = pvWant[3] * k; lpk_want(&it, x, y, w, h); gfx_hole(x, y, w, h, 26 * k, 1); }
static float pvBox[4]; int ph_live_box(float *r) { if (pvBox[2] < 1) return 0; memcpy(r, pvBox, sizeof pvBox); return 1; } /* where the preview's box was last drawn (a channel swiped away goes into it) */
void ph_live_returned(int anim) { sideArmed = 0; returnedAt = app_now(); growing = 0; if (anim) { tween_set(&fullT, 1); backWait = 1; } else { tween_set(&fullT, 0); backWait = 0; } }
static void pg_draw(void) {
  int ghost = phGhost; double now = app_now();
  if (PG.entered) tabs_fresh(&PG.cur);
  int phone = fminf(SB_W, SB_H) < 1100, side = SB_W > SB_H, wide = SB_W > SB_H * 1.15f;
  if (side != lastSide) { lastSide = side; sideAt = now; }
  const LiveCh *f = pg_sel(); uint32_t c1, c2; tint_of(f ? art_of(f) : NULL, &c1, &c2);
  /* to the screen and back */
  if (backWait) { int pending = phone && side && pplat_held() == 1 && now - returnedAt < 1.4; if (!pending) { backWait = 0; tween_to(&fullT, 0, 0.42f, EASE_WEB); } } /* (given back on a phone turning upright: it waits for the turn) */
  if (!player_open() && SCR == SCR_LIVE && f && !ghost && PG.entered) {
    if (!side || !phone) sideArmed = side ? 0 : 1;
    else if (sideArmed && lpk_shown(f->id) && now - returnedAt > 0.8) { sideArmed = 0; if (now - sideAt < 0.7) { tween_set(&fullT, 1); growing = 1; } else pg_grow(); } } /* (turned just now: the screen at once) */
  tween_tick(&fullT, phDt); float fk = fullT.v; int turnHold = player_open() && player_from_preview() && phone_player_turning(); if (turnHold) fk = 1; /* (handed to the player, the screen still to turn: the picture stays grown, never the page) */ if (growing && fk >= 0.999f) { growing = 0; pg_watch(); tween_set(&fullT, 0); } /* (handed over: under the player the page is as it was, for a swipe back into it) */
  int hits = fk < 0.02f;
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &PG.v, 1); scroll_tick(&PG.v); if (!hits) PG.v.vel = 0;
  gfx_rect(0, 0, SB_W, SB_H, DK); float sc = PG.v.pos;
  float L = PAD, W = SB_W - 2 * PAD; if (W > 1800) { L = (SB_W - 1800) / 2; W = 1800; }
  float top = INS_T + 26, wordH = 54 * 1.25f + 22, bx, by, bw, bh, y;
  float gui = 1 - fk; /* the page round the preview fades as it fills the screen */
  /* the ground: the channels' wall faint behind the top, the chosen channel's colours glowing slowly */
  gfx_opacity_push(gui);
  { float wb = wide ? SB_H * 0.75f : top + wordH + (W * 9 / 16) + 60 - (wide ? 0 : fminf(sc, 600)); if (wb > 0) { gfx_clip_push(0, 0, wide ? L + W * 0.46f + 28 : SB_W, wb); livewall_backdrop(0.16f * (wide ? 1 : clampf(1 - sc / 600, 0, 1))); gfx_clip_pop(); gfx_rrect_v(0, wb - 160, SB_W, 160, 0, col_alpha(DK, 0), DK); } }
  gfx_rrect_v(0, 0, SB_W, SB_H * 0.7f, 0, col_alpha(c1 | 0xff000000u, 0.45f), col_alpha(c1 | 0xff000000u, 0));
  gfx_rrect_v(0, SB_H * 0.35f, SB_W, SB_H * 0.25f, 0, col_alpha(DK, 0), col_alpha(DK, 0.9f)); gfx_rect(0, SB_H * 0.6f, SB_W, SB_H * 0.4f, col_alpha(DK, 0.9f));
  gfx_opacity_pop();
  if (wide) {
    /* two columns: the preview, its channel and the buttons stay on the left; the catalogs and the channels on the right, scrolling */
    float avail = SB_H - top - INS_B - BAR_H - 10; bh = avail - wordH - 26 - info_h(); bw = bh * 16 / 9; if (bw > W * 0.46f) { bw = W * 0.46f; } if (bw < 360) bw = 360; bh = bw * 9 / 16;
    bx = L; by = top + wordH;
    float RX = bx + bw + 56, RW = L + W - RX, ty = top + 6;
    gfx_opacity_push(gui * in_k(0, 0.4)); livewall_word(L, top, 54, bw, ALIGN_START); gfx_opacity_pop();
    if (hits) gfx_opacity_push(in_k(0.12, 0.45)); else gfx_opacity_push(gui); pg_info(f, L, by + bh + 26, bw); gfx_opacity_pop();
    PG.lastTop = 0; float gy = ty + 66 + 24 - sc;
    gfx_opacity_push(gui); y = pg_grid(f, RX, RW, gy, ty + 66 + 10, hits);
    { float k = in_k(0.16, 0.4); pills_draw(&PG.pl, RX, ty, RW, &PG.tv, PG.cur, pg_tab_pick, k * gui); } gfx_opacity_pop();
    PG.v.max = y + sc - (SB_H - INS_B - 20); if (PG.v.max < 0) PG.v.max = 0;
  } else {
    /* one column: the title goes up, the preview stays at the top once it gets there, the catalogs under it */
    bw = W; bh = bw * 9 / 16; if (bh > SB_H * 0.4f) { bh = SB_H * 0.4f; bw = bh * 16 / 9; }
    bx = L; float pinY = INS_T + 12, natY = top + wordH - sc; by = natY > pinY ? natY : pinY;
    gfx_opacity_push(gui * in_k(0, 0.4)); livewall_word(L, top - sc, 54, W, ALIGN_START); gfx_opacity_pop();
    float under = by + bh + 16; /* (all that scrolls goes under the preview) */
    float iy = natY + bh + 26; gfx_clip_push(0, under, SB_W, SB_H - under); gfx_opacity_push(gui * in_k(0.12, 0.45)); pg_info(f, L, iy, W); gfx_opacity_pop(); gfx_clip_pop();
    float tyN = iy + info_h() + 30, ty = tyN > under ? tyN : under; PG.lastTop = tyN + sc - under;
    gfx_opacity_push(gui); y = pg_grid(f, L, W, tyN + 66 + 24, ty + 66 + 10, hits);
    { float sk = clampf((ty - tyN) / 30, 0, 1); if (sk > 0.003f) { float hb = ty + 66 + 12; gfx_rect(0, 0, SB_W, hb, col_alpha(DK, 0.96f * sk)); gfx_rrect_v(0, hb, SB_W, 26, 0, col_alpha(DK, 0.96f * sk), col_alpha(DK, 0)); } } /* stuck: the page's dark behind the preview and the pills */
    { float k = in_k(0.16, 0.4); pills_draw(&PG.pl, L, ty, W, &PG.tv, PG.cur, pg_tab_pick, k); } gfx_opacity_pop();
    PG.v.max = y + sc - (SB_H - INS_B - BAR_H) + 40; if (PG.v.max < 0) PG.v.max = 0;
  }
  /* the preview, over all of it: growing to the screen as fk goes to 1 */
  if (fk > 0.001f) gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(255 * fminf(fk * 1.6f, 1))));
  { /* from the box straight to where the player will show the picture: fitted to the screen in its own shape (16:9
       until its size is known), so the hand-over moves nothing, whatever the screen's or the channel's shape */
    int vw = video_width(), vh = video_height(); float va = vw > 0 && vh > 0 ? (float)vw / vh : 16.0f / 9, tk = fminf(SB_W / va, SB_H), tw = tk * va, th = tk, tx = (SB_W - tw) / 2, ty = (SB_H - th) / 2;
    float e = fk, x = bx + (tx - bx) * e, yy = by + (ty - by) * e, w = bw + (tw - bw) * e, h = bh + (th - bh) * e;
    float ki = in_k(0.06, 0.45); gfx_opacity_push(ki); float s = (0.97f + 0.03f * ki) * (1 - 0.02f * (hits ? press_k(bx, by, bw, bh) : 0)); gfx_scale_push(s, s, x + w / 2, yy + h / 2);
    if (f && !ghost && PG.entered && SCR == SCR_LIVE) { Item it; live_item(f, &it); float x0 = x, y0 = yy, x1 = x + w, y1 = yy + h; gfx_to_screen(&x0, &y0); gfx_to_screen(&x1, &y1); gfx_mirror_span(&x0, &x1); lpk_want(&it, x0, y0, x1 - x0, y1 - y0); pvWant[0] = x0; pvWant[1] = y0; pvWant[2] = x1 - x0; pvWant[3] = y1 - y0; pvWantF = app_frame_no(); } /* (where the box really is on the screen: the page moved or scaled by a swipe back, the picture with it) */
    pvBox[0] = bx; pvBox[1] = by; pvBox[2] = bw; pvBox[3] = bh;
    lpk_draw(f ? f->id : NULL, f ? art_of(f) : NULL, x, yy, w, h, 26 * (1 - fk), 1, col_mix(c1, DK, 0.2f), c2); if (turnHold) gfx_hole(x, yy, w, h, 0, 1); /* (the player's picture, where the preview grew to) */
    gfx_scale_pop(); gfx_opacity_pop(); if (f && hits) hit_act(bx, by, bw, bh, play_act, 0, 0); }
}
static int pg_key(int k) {
  if (k == K_OK) { if (PG.tapWas) { PG.tapWas = 0; pg_watch(); } return 1; } /* a tap on the chosen one: watch it (the first tap chose it) */
  if (k == K_CH_UP) { scroll_top(&PG.v); return 1; }
  return 0; }
static const Item *pg_focused(void) { static Item one; if (!touchId[0] || app_now() - touchAt > 0.3) return NULL; const LiveCh *c = ch_find(touchId); if (!c) return NULL; live_item(c, &one); return &one; } /* held: the channel's menu */
static void pg_out(float k) { phone_out(pg_draw, k); }
/* the player, opened from the preview, switched to another channel: the preview is on that one when it comes back */
void ph_live_select(const char *id) { const LiveCh *c = id ? ch_find(id) : NULL; if (c) pg_select(c); }
int ph_live_item(const char *id, Item *out) { const LiveCh *c = id ? ch_find(id) : NULL; if (!c) return 0; live_item(c, out); return 1; }
static void pg_open(const Item *it) { const LiveCh *c = it ? ch_find(it->id) : NULL; touchId[0] = 0; if (c && strcmp(PG.sel, c->id)) { pg_select(c); pplat_haptic_k(HK_TICK, 0.35f); } } /* a short tap: the tile chose it for the preview, nothing more (held: its menu) */
Screen phLive = { pg_enter, pg_leave, pg_key, NULL, pg_draw, pg_focused, pg_open, NULL, NULL, pg_out };

/* ================= Home's Live TV row ================= */
static ItemList band; static double bandAt; static unsigned bandSig;
static void band_fresh(void) {
  double now = app_now(); if (band.items && now - bandAt < 1.0) return; bandAt = now;
  ItemList b; int n = live_band(&b); unsigned h = 2166136261u; for (int i = 0; i < n; i++) for (const char *q = b.items[i].id; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u;
  if (band.items && h == bandSig && n == band.n) { itemlist_clear(&b); return; }
  itemlist_clear(&band); band = b; bandSig = h; }
int ph_live_band(const Item **items) { band_fresh(); *items = band.items; return band.n; }
const Item *ph_live_band_item(int i) { band_fresh(); return i >= 0 && i < band.n ? &band.items[i] : NULL; }
/* each badge glides to its place as the row changes (a channel watched goes first), a new one fades in; the row
   itself opens as it first comes (the rows under it move down with it, never a jump) */
static struct { char id[64]; float x, a; int seen; } BG[32];
float ph_live_row(float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx) {
  band_fresh(); static float open; float dt = phDt; open += ((band.n > 0 ? 1.f : 0.f) - open) * clampf(dt * 7, 0, 1); if (open < 0.002f) { open = 0; return 0; } if (open > 0.998f) open = 1;
  const float D = 148, gap = 22, top = 52, nameH = text_line_height(F_SMALL) + 12, h0 = top + D + nameH + 36; float ok = open * open * (3 - 2 * open), h = h0 * ok;
  for (int k = 0; k < 32; k++) BG[k].seen = 0;
  if (y + h < -10 || y > SB_H + 10 || band.n <= 0) { scroll_tick(s); return h; }
  gfx_opacity_push(ok);
  text_draw(T("Live TV"), PAD, y + 6, F_H2 - 2, 800, TXT, -0.3f, SB_W - 2 * PAD, ALIGN_START);
  s->max = band.n * (D + gap) - gap + 2 * PAD - SB_W; if (s->max < 0) s->max = 0; scroll_tick(s); ph_drag(0, y + top, SB_W, D + nameH, s, 0);
  int rtl = app_rtl(); const char *ids[20], *tys[20]; int nv = 0;
  for (int i = 0; i < band.n; i++) { const Item *it = &band.items[i]; float tx = i * (D + gap); int bk = -1, fr = -1;
    for (int k = 0; k < 32; k++) { if (!strcmp(BG[k].id, it->id)) { bk = k; break; } if (fr < 0 && !BG[k].seen && (!BG[k].id[0] || 1)) fr = k; }
    if (bk < 0) { for (int k = 0; k < 32; k++) if (!BG[k].id[0]) { bk = k; break; } if (bk < 0) bk = fr >= 0 ? fr : 0; snprintf(BG[bk].id, sizeof BG[bk].id, "%s", it->id); BG[bk].x = tx; BG[bk].a = 0; }
    BG[bk].seen = 1; BG[bk].x += (tx - BG[bk].x) * clampf(dt * 9, 0, 1); BG[bk].a += (1 - BG[bk].a) * clampf(dt * 7, 0, 1);
    float x = PAD + BG[bk].x - s->pos; if (rtl) x = SB_W - x - D; if (x > SB_W || x + D < 0) continue; gfx_opacity_push(BG[bk].a);
    int all = !strcmp(it->id, LIVE_ALL_ID); float pk = press_k(x, y + top, D, D + nameH), sc = 1 - 0.05f * pk, hold = focusedIdx == i ? app_hold_progress() : 0;
    gfx_scale_push(sc, sc, x + D / 2, y + top + D / 2);
    gfx_shadow(x, y + top + 8, D, D, D / 2, 22, BLACK_A(110)); card_draw_round(it, x, y + top, D);
    if (!all) { state_mark(it->id, x + D * 0.5f - D * 0.5f * 0.7071f, y + top + D * 0.5f - D * 0.5f * 0.7071f, 6, is_playing(it->id)); if (nv < 20) { ids[nv] = it->id; tys[nv++] = it->type; } }
    if (hold > 0) hold_trace_draw(x, y + top, D, D, D / 2, ease_apply(EASE_IN_OUT, hold));
    gfx_scale_pop();
    { const char *nm = all ? T("All channels") : it->name; float mw = D + gap - 6; int f = F_SMALL; float tw = text_width(nm, f, 600, 0); if (tw > mw) { f = (int)(F_SMALL * mw / tw); if (f < (int)(F_SMALL * 0.8f)) f = (int)(F_SMALL * 0.8f); } /* (a long name a little smaller first: shortened only past that) */
      text_draw(nm, x + D / 2, y + top + D + 12 + (text_line_height(F_SMALL) - text_line_height(f)) / 2.0f, f, 600, all ? WHITE_A(240) : TXT2, 0, mw, ALIGN_CENTER); }
    gfx_opacity_pop(); hit_add(x, y + top, D, D + nameH, focus, NULL, rowIdx, i); }
  for (int k = 0; k < 32; k++) if (!BG[k].seen) BG[k].id[0] = 0; /* (one gone from the row: forgotten) */
  gfx_opacity_pop();
  extern int phGhost; if (!phGhost && SCR == SCR_HOME && nv) live_probe_visible(ids, tys, nv);
  return h; }

/* ================= the player's Channels panel ================= */
static struct { int cur; Pills pl; int opened; } PN;
void ph_live_panel_open(Scroll *list) {
  live_refresh(0); if (!ntab) tabs_build(); else tabs_fresh(&PN.cur);
  if (PN.cur < 0 || PN.cur >= ntab || tab_find(PN.cur, playing) < 0) { PN.cur = 0; for (int i = 0; i < ntab; i++) if (tab_find(i, playing) >= 0) { PN.cur = i; break; } }
  PN.pl.set = 0; int at = tab_find(PN.cur, playing); list->pos = at > 2 ? (at - 1) * 104.0f : 0; list->vel = 0; PN.opened = 1; }
static void pn_tab(int i, int b) { (void)b; if (i < 0 || i >= ntab) return; PN.cur = i; pplat_haptic_k(HK_TICK, 0.4f); extern void phone_player_list_top(void); phone_player_list_top(); }
static void pn_pick(int i, int b) { (void)b; if (PN.cur >= ntab || i < 0 || i >= tab[PN.cur].n) return; const Tab *t = &tab[PN.cur]; const LiveCh *c = t->p[i];
  extern void phone_player_panel_close(void); pplat_haptic_k(HK_TICK, 0.6f);
  if (is_playing(c->id) && !live_card()->tuning) { phone_player_panel_close(); return; } /* (the one playing: back to it) */
  live_start_list(t->p, t->n, i, 1); if (live_reach(c->id) >= 0) phone_player_panel_close(); } /* (off air: said, the list stays) */
static void pn_fav(int i, int b) { (void)b; if (PN.cur >= ntab || i < 0 || i >= tab[PN.cur].n) return; fav_toggle(tab[PN.cur].p[i]); }
/* hit: the player's own taps (its list scrolled: only its shown part takes them) */
float ph_live_panel_draw(float px, float top, float pw, float bot, Scroll *tabs, Scroll *list, void (*hit)(float, float, float, float, void (*)(int, int), int, int)) {
  tabs_fresh(&PN.cur); float x = px + 28, w = pw - 56;
  /* the pills (taps through the player's own list of taps) */
  { const float th = 66; float xx = x - tabs->pos; for (int i = 0; i < ntab; i++) { char cn[16]; snprintf(cn, sizeof cn, "%d", tab[i].n); float tw = 26 + text_width(tab[i].name, 26, 700, 0) + 10 + text_width(cn, 21, 600, 0.4f) + 26; if (xx + tw > x && xx < x + w) hit(xx < x ? x : xx, top, tw, th, pn_tab, i, 0); xx += tw + 10; }
    pills_draw(&PN.pl, x, top, w, tabs, PN.cur, NULL, 1); top += th + 20; }
  const Tab *t = PN.cur < ntab ? &tab[PN.cur] : NULL; const float RH = 104, tw = 136, th = tw * 9 / 16; float y = top - list->pos;
  gfx_clip_push(px, top, pw, bot - top);
  if (!t || !t->n) { spinner(px + pw / 2, top + 70, 22); list->max = 0; gfx_clip_pop(); return top; }
  const char *ids[24], *tys[24]; int nv = 0; const LiveCard *lc = live_card();
  for (int i = 0; i < t->n; i++, y += RH) { if (y + RH < top || y > bot) continue; const LiveCh *c = t->p[i]; int pl = is_playing(c->id), tun = lc->tuning && lc->it && !strcmp(lc->it->id, c->id);
    float pk = press_k(px + 12, y, pw - 24, RH); if (pk > 0 || pl) gfx_rrect(px + 14, y + 4, pw - 28, RH - 8, 22, WHITE_A((int)(pl ? 22 : 0) + (int)(18 * pk)));
    float tx = x + 4, ty = y + RH / 2 - th / 2; ch_face(c, tx, ty, tw, th, 12, 0.66f, 0.6f, 18);
    state_mark(c->id, tx + 13, ty + 13, 4.5f, pl);
    float nx = tx + tw + 22, nw = x + w - 84 - nx; text_draw(c->name, nx, y + RH / 2 - 34, 27, 700, WHITE_A(pl ? 255 : 235), 0, nw, ALIGN_START);
    float sy = y + RH / 2 + 4; uint32_t sc2 = WHITE_A(160);
    if (tun) { float ph = (float)fmod(app_now() / 0.9, 1) * 6.2832f; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(nx + 9 + cosf(an) * 7, sy + 14 + sinf(an) * 7, 1.4f, ACC2); } text_draw(T("Tuning in…"), nx + 28, sy, F_SMALL, 600, sc2, 0, nw - 28, ALIGN_START); }
    else if (pl) { eq_bars(nx, sy + 4, 18, ACC2); text_draw(T("Now playing"), nx + 30, sy, F_SMALL, 700, WHITE_A(220), 0, nw - 30, ALIGN_START); }
    else if (live_reach(c->id) < 0) text_draw(T("Off air"), nx, sy, F_SMALL, 600, WHITE_A(120), 0, nw, ALIGN_START);
    else { const char *cat = live_catalog_of(c->id); char cb[96]; text_draw(cat ? short_name(cat, cb, sizeof cb) : "", nx, sy, F_SMALL, 600, sc2, 0, nw, ALIGN_START); }
    int fav = live_pinned(c->id); float sx = x + w - 34, scy = y + RH / 2; float fk = press_k(sx - 34, scy - 34, 68, 68); gfx_circle(sx, scy, 30, fav ? RGBA(0xf5, 0xc5, 0x18, (int)(46 + 30 * fk)) : WHITE_A((int)(14 + 20 * fk)));
    icon_draw("star", sx - 16, scy - 16, 32, fav ? GOLD : WHITE_A(150));
    float hy0 = y < top ? top : y, hy1 = y + RH > bot ? bot : y + RH;
    if (hy1 > hy0) { hit(px + 12, hy0, sx - 40 - px - 12, hy1 - hy0, pn_pick, i, 0); if (scy - 36 >= top && scy + 36 <= bot) hit(sx - 40, scy - 40, 80, 80, pn_fav, i, 0); }
    if (nv < 24 && !pl) { ids[nv] = c->id; tys[nv++] = c->type; } }
  gfx_clip_pop();
  if (nv) live_probe_visible(ids, tys, nv);
  list->max = t->n * RH - (bot - top) + 20; if (list->max < 0) list->max = 0;
  return y; }

/* ================= the live hub's interface, answered by the phone ================= */
static int panelOn;
void hub_open(int inPlayer, const char *playingId) {
  if (playingId && *playingId) snprintf(playing, sizeof playing, "%s", playingId);
  if (inPlayer) { extern void phone_player_channels(void); phone_player_channels(); return; }
  if (SCR != SCR_LIVE) app_go(SCR_LIVE, NULL, 0); }
void hub_close(void) { if (panelOn) { extern void phone_player_panel_close(void); phone_player_panel_close(); } }
int  hub_on(void) { return panelOn || SCR == SCR_LIVE; }
int  hub_key(int k) { if (k == K_BACK && panelOn) { hub_close(); return 1; } return 0; }
void hub_tick(float dt) { (void)dt; if (!player_open()) panelOn = 0; }
void hub_draw(void) {}
void hub_playing(const char *id) { snprintf(playing, sizeof playing, "%s", id ? id : ""); }
int  hub_player_mode(void) { return 0; } /* (the player's panel is drawn by player_phone.c) */
int  hub_back(void) { return 0; }
void hub_ok_up(void) {}
void ph_live_panel_state(int on) { panelOn = on; }

/* the TV's All channels grid (screens/livehub.c, not on a phone): it never covers the page, the frame's clear is the page's */
int hub_covers_page(void) { return 0; }
uint32_t hub_frame_clear(uint32_t page, int live) { (void)live; return page; }
