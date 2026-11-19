/* Home and Discover: the wordmark and the Settings gear, chips to narrow the rows (Series, Films, a list of the
   catalogs), a big card for one title, Continue Watching, then the catalogs of the installed addons as rows.
   Discover has the same rows without the big card. */
#include "core/notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/palette.h"
#include "core/live.h"
float ph_live_row(float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx); const Item *ph_live_band_item(int i); int ph_live_band(const Item **items);

#define MAXR 40
typedef struct { CatalogDef def; ItemList list; int got; Scroll s; int retries; double retryAt; } HRow; /* retryAt: no answer, asked again then (8, 20, 45 s) */
typedef struct { HRow rows[MAXR]; int n; ItemList cw, up; Scroll cws, ups, v; unsigned lib, gen; int filter, fr, fi; float lastY[MAXR + 2]; int heroRow; Timer1 in; Timer1 sw; int swDir; } Board;
static Board B[2]; /* Home, Discover */
/* Discover's spotlight: this year's films and the trending series, alternating, eight in all; it slides on by itself
   every 5 s (and by the finger), dots under it */
#define NFEAT 8
#include <time.h>
static struct { Item it[NFEAT]; int n, load, gotM, gotS; ItemList m, sr; Scroll s; int idx, dragged; double touchAt; Tween dotW[NFEAT]; } F;
/* the spotlight's picture kept while a page beside this one is shown (Settings, Library): swiped back to, it is there at
   once, never loaded again as it comes in (the pages' many posters would have pushed it out of the budget) */
static char keepUrl[600]; static int keepW, keepH;
void ph_home_keep(void) { if (keepUrl[0] && keepW > 0) img_get_shaped(keepUrl, keepW, keepH, 0, 1); }
static int feat_keep(const Item *it) { return it->id[0] && (it->background[0] || it->poster[0]); }
static void feat_from_rows(Board *b) { /* the spotlight from Home's own catalogs: the first titles of its rows, in turn */
  if (F.n) return; int at[MAXR] = { 0 }, any = 1;
  while (F.n < NFEAT && any) { any = 0; for (int i = 0; i < b->n && F.n < NFEAT; i++) { HRow *r = &b->rows[i]; if (!r->got) continue; if (b->filter && strcmp(r->def.type, b->filter == 1 ? "series" : "movie")) continue; /* (Series or Movies chosen: only those) */
      while (at[i] < r->list.n && !feat_keep(&r->list.items[at[i]])) at[i]++;
      if (at[i] < r->list.n) { const Item *c = &r->list.items[at[i]++]; int dup = 0; for (int k = 0; k < F.n; k++) if (!strcmp(F.it[k].id, c->id)) dup = 1; if (!dup) F.it[F.n++] = *c; any = 1; } } }
  if (F.n) { F.touchAt = app_now(); for (int i = 0; i < NFEAT; i++) tween_set(&F.dotW[i], i == 0 ? 34 : 12); } }
static void feat_act(int a, int b2) { if (b2 < 0 || b2 >= F.n) return; Item it = F.it[b2];
  if (a == 1) { DetailParams p; memset(&p, 0, sizeof p); p.it = it; p.openStreams = 1; app_go(SCR_DETAIL, &p, 0); }
  else if (a == 2) { lib_toggle(&it); toast(lib_has(it.id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
  else app_open_item(&it); }
/* the spotlight, drawn at y: each title a page of the strip; returns its height */
/* a film's backdrop for other pages to show (Settings' previews): the spotlight's one, "" before it has any */
const char *ph_home_backdrop(char *out, size_t cap) { out[0] = 0; if (!F.n) return out; const Item *hi = &F.it[F.idx]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) sb_fill_item(&full, hm, hi->type); else full = *hi; const char *b = sb_bg_mid(&full, out, cap); if (b != out) snprintf(out, cap, "%s", b ? b : ""); return out; }
/* the titles of the spotlight, for Appearance's preview of "from the logo / artwork": i-th one's id, backdrop, logo, poster and name; the count */
int ph_home_demo(int i, Item *out, char *bg, size_t cap) { if (!F.n) return 0; i %= F.n; const Item *hi = &F.it[i]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
  *out = full; const char *b = sb_bg_mid(&full, bg, cap); if (b != bg) snprintf(bg, cap, "%s", b ? b : ""); return F.n; }
static uint32_t disc_page(void) { return col_mix(PAGE | 0xff000000u, ACC | 0xff000000u, 0.18f); } /* Discover's page: tinted by the shown title's colour */
/* Series or Movies chosen (or All again): the spotlight's titles change; the picture shown stays until the new one's is
   in, then the new one comes over it, its words with it (no empty box, nothing popping in) */
static char swOld[600]; static double swReadyAt; static int swOn;
static void feat_switch(void) { if (F.n && keepUrl[0]) { snprintf(swOld, sizeof swOld, "%s", keepUrl); swOn = 1; swReadyAt = 0; } F.n = 0; F.idx = 0; F.s.pos = 0; F.s.vel = 0; F.touchAt = app_now(); }
static float sw_k(void) { if (!swOn) return 1; if (!swReadyAt) return 0; float k = (float)((app_now() - swReadyAt) / 0.4); if (k >= 1) { swOn = 0; swOld[0] = 0; return 1; } return k * k * (3 - 2 * k); }
static float feat_draw(float y) {
  float hh = SB_W * 1.02f + INS_T, W = SB_W; int wide = SB_W > SB_H, tab = ph_tabw(); if (wide) hh = SB_H * 0.74f; if (tab) hh = SB_H * 0.62f; /* (a tablet sideways: a big picture at the top; going down it slides away slower and dims, the rows come up over it) */
  float bh = tab ? W * 9 / 16 : hh, boff = tab ? clampf(bh * 0.35f - hh / 2, 0, bh - hh) : 0, py = tab && y < 0 ? y * 0.5f : y, gone = tab ? clampf(-y / (hh * 0.8f), 0, 1) : 0; /* (the band: the picture whole, framed a third of the way down where faces are, never cut through the middle) */ /* (sideways: shorter, its words up out of the bar's way) */ /* (sideways: the screen's height, not its width) */
  if (!F.n) { GLuint ot = swOn && swOld[0] ? img_get_shaped(swOld, (int)W, (int)bh, 0, 1) : 0; gfx_clip_push(0, y, W, hh); if (ot) gfx_image(ot, 0, y - boff, W, bh, 0, WHITE_A(255), 0, 0, 1, 1); else ph_box(0, y, W, hh, 0); gfx_clip_pop(); return hh; } /* (the new titles not known yet: the picture shown before stays) */
  /* the strip: the finger moves it; let go, it settles on the page it was nearest (a flick goes one on); alone 5 s, it goes on */
  { static double drawnAt; if (drawnAt > 0 && app_now() - drawnAt > 0.3) F.touchAt += app_now() - drawnAt; drawnAt = app_now(); } /* (out of sight, its clock stops: back to it, as it was left, the one a page came from or goes back into still there) */
  ph_drag(0, y, W, hh - 120, &F.s, 0);
  if (F.s.drag) { F.dragged = 1; F.touchAt = app_now(); }
  else { if (F.dragged) { F.dragged = 0; float v = -F.s.vel; int to = (int)floorf(F.s.pos / W + 0.5f); if (v < -600 && F.s.pos / W < F.idx + 0.5f) to = F.idx + 1; else if (v > 600 && F.s.pos / W > F.idx - 0.5f) to = F.idx - 1; F.idx = to < 0 ? 0 : to >= F.n ? F.n - 1 : to; F.s.vel = 0; F.touchAt = app_now(); }
    if (app_now() - F.touchAt > 5) { F.idx = (F.idx + 1) % F.n; F.touchAt = app_now(); }
    float tgt = F.idx * W; F.s.pos += (tgt - F.s.pos) * clampf(phDt * 9, 0, 1); if (fabsf(tgt - F.s.pos) < 0.5f) F.s.pos = tgt; }
  F.s.max = (F.n - 1) * W;
  { const Item *c = &F.it[F.idx]; accent_scope(c->id, c->poster, c->logo); } /* (not while drawn as a picture under another page: that page keeps its colour) */ /* the shown title's colour, for the whole page (as the user's colour setting allows; it fades over) */
  /* the titles crossfade as they change (a picture moves a little, never cut by its neighbour's edge): the pictures, the nearest last */
  int ord[NFEAT], no = 0; for (int i = 0; i < F.n; i++) { float d = fabsf((i * W - F.s.pos) / W); if (d < 1) ord[no++] = i; } for (int a1 = 1; a1 < no; a1++) for (int b1 = a1; b1 > 0 && fabsf(ord[b1] * W - F.s.pos) > fabsf(ord[b1 - 1] * W - F.s.pos); b1--) { int tq = ord[b1]; ord[b1] = ord[b1 - 1]; ord[b1 - 1] = tq; }
  gfx_clip_push(0, y < 0 ? 0 : y, W, y < 0 ? hh + y : hh); /* (scrolled down: it ends where the spotlight ends, never below it) */
  float swk = 1; if (swOn) { /* the picture before, under the new one until it has come in */
    GLuint ot = swOld[0] ? img_get_shaped(swOld, (int)W, (int)bh, 0, 1) : 0; if (ot) gfx_image(ot, 0, y - boff, W, bh, 0, WHITE_A(255), 0, 0, 1, 1);
    if (!swReadyAt) { const Item *hi = &F.it[F.idx]; const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) sb_fill_item(&full, hm, hi->type); else full = *hi; char nb[600]; const char *bg = sb_bg_mid(&full, nb, sizeof nb);
      if (!bg || !*bg || img_get_shaped(bg, (int)W, (int)bh, 0, 1) || img_shaped_failed(bg, (int)W, (int)bh, 0, 1)) swReadyAt = app_now(); }
    swk = sw_k(); }
  gfx_opacity_push(swk);
  { float f = clampf(F.s.pos / W, 0, (float)(F.n - 1)); int i0 = (int)floorf(f); if (i0 > F.n - 1) i0 = F.n - 1; float t = f - i0; t = t * t * (3 - 2 * t); /* the one going under, the next over it fading in: never an edge between them */
    for (int q = 0; q < 2; q++) { int i = i0 + q; if (i >= F.n || (q == 1 && t < 0.003f)) continue; const Item *hi = &F.it[i]; char buf[600];
      const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
      const char *bg = sb_bg_mid(&full, buf, sizeof buf); GLuint tx = bg && *bg ? img_get_shaped(bg, (int)W, (int)bh, 0, 1) : 0; if (i == F.idx && bg) { snprintf(keepUrl, sizeof keepUrl, "%s", bg); keepW = (int)W; keepH = (int)bh; } float k = q ? 1.05f - 0.05f * t : 1 + 0.03f * t; /* the old one drifts a little closer, the new settles in */
      gfx_opacity_push(q ? t : 1); gfx_scale_push(k, k, W / 2, y + hh / 2); if (!tx || !ph_picture(tx, 0, py - boff, W, bh, 0, WHITE_A((int)(255 * (1 - 0.7f * gone))))) ph_box(0, y, W, hh, 0); gfx_scale_pop(); gfx_opacity_pop(); } }
  { int nx = (F.idx + 1) % F.n; if (nx != F.idx) { const Item *ni = &F.it[nx]; const cJSON *nm = sb_meta_cached(ni->type, ni->id); Item nf; if (nm) sb_fill_item(&nf, nm, ni->type); else nf = *ni; char nb[600]; const char *nbg = sb_bg_mid(&nf, nb, sizeof nb); if (nbg && *nbg) img_get_shaped(nbg, (int)W, (int)bh, 0, 1); } } /* the next title's picture asked ahead: on a big screen it takes a moment, and it is there when its turn comes */
  gfx_opacity_pop();
  { int i = 0; float px = 0; (void)i; (void)px;
    gfx_rrect_v(0, y, W, INS_T + 120, 0, BLACK_A(120), BLACK_A(0)); { uint32_t pg = disc_page(); float g0 = y + hh * 0.30f, gh = hh * 0.62f; /* into the page: eased in eight steps (no edge where a straight fade ends, whatever the picture), solid for the last part */
      for (int q = 0; q < 8; q++) { float u0 = q / 8.0f, u1 = (q + 1) / 8.0f, e0 = u0 * u0 * (3 - 2 * u0), e1 = u1 * u1 * (3 - 2 * u1); gfx_rrect_v(0, g0 + gh * u0, W, gh * (u1 - u0), 0, col_alpha(pg, e0), col_alpha(pg, e1)); /* (edge to edge: an overlap would show as a line) */ }
      gfx_rect(0, g0 + gh, W, hh - (g0 + gh - y) + 2, pg); }
    if (wide) { uint32_t pg2 = disc_page(); for (int q = 0; q < 8; q++) { float u0 = q / 8.0f, u1 = (q + 1) / 8.0f, e0 = 1 - u0 * u0 * (3 - 2 * u0), e1 = 1 - u1 * u1 * (3 - 2 * u1); gfx_rrect_h(W * 0.62f * u0, y, W * 0.62f * (u1 - u0), hh, 0, col_alpha(pg2, 0.62f * e0), col_alpha(pg2, 0.62f * e1)); } } /* in the page's own colour: where it meets the page there is nothing to see */ /* sideways, as on the TV: a shade from the left that its words sit on */
  }
  /* its words: they move a little more and fade quicker */
  for (int q = 0; q < no; q++) { int i = ord[q]; float px = i * W - F.s.pos, al = clampf(1 - fabsf(px) / W * 1.6f, 0, 1); if (al <= 0.01f) continue; const Item *hi = &F.it[i]; char buf[600]; (void)buf;
    const cJSON *hm = sb_meta_cached(hi->type, hi->id); Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else full = *hi;
    gfx_opacity_push(al * swk); gfx_offset_push(px * 0.4f, 0);
    float lw = wide ? W * 0.34f : W * 0.64f, lh = wide ? 170 : 130, ly = wide ? y + hh - 96 - (lh + 60 + 3 * 32) : y + hh - 390, tw0 = wide ? W * 0.42f : W - 2 * PAD, cx = wide ? PAD + 20 : W / 2;
    if (tab) { lw = W * 0.30f; lh = 190; tw0 = W * 0.40f; cx = PAD + 36; ly = y + hh - 70 - (lh + 14 + 34 + 12 + 3 * 34); } /* a tablet sideways: a column of its own on the left, its words larger */ int al0 = wide ? ALIGN_START : ALIGN_CENTER; /* sideways: bottom left over the picture, as on the TV; upright: centred */ char lb[600]; const char *logo = sb_logo_of(&full, lb, sizeof lb); float asp = 0;
    GLuint lt = logo && *logo ? img_get(logo, (int)lw, (int)lh, &asp) : 0;
    if (lt && asp > 0) { float w = lw, h = w / asp; if (h > lh) { h = lh; w = h * asp; } gfx_image(lt, wide ? cx : W / 2 - w / 2, ly + lh - h, w, h, 0, WHITE_A(255), 0, 0, 1, 1); }
    else if (!(logo && *logo) || img_failed(logo)) text_draw_wrapped(hi->name, cx, ly + lh - 108, wide ? tw0 : W - 2 * PAD, 2, F_H1, 800, TXT, -0.6f, al0, 54); /* (the name only when there is no logo) */
    { char g[200]; const char *kind = !strcmp(hi->type, "series") ? T("Series") : T("Film"); snprintf(g, sizeof g, "%s  \xc2\xb7  %s", T("Trending now"), kind); const cJSON *gg; int k = 0; cJSON_ArrayForEach(gg, js_arr(hm, "genres")) if (cJSON_IsString(gg) && k < 2) { size_t l = strlen(g); snprintf(g + l, sizeof g - l, "  \xc2\xb7  %s", gg->valuestring); k++; }
      text_draw(g, cx, ly + lh + 14, tab ? F_SMALL + 3 : F_SMALL, 600, WHITE_A(205), 0.2f, tw0, al0); }
    { const char *d = js_str(hm, "description", hi->description); if (d && *d) { if (tab) text_draw_wrapped(d, cx, ly + lh + 14 + 34 + 12, tw0 - 20, 3, F_BODY - 1, 400, WHITE_A(210), 0, al0, 34); else text_draw_wrapped(d, cx, ly + lh + 60, tw0 - 20, 3, F_SMALL + 1, 400, WHITE_A(200), 0, al0, 32); } } /* its story, under it */
    if (i == F.idx) hit_act(0, y + INS_T + 60, W, hh - 120 - INS_T, feat_act, 0, i);
    gfx_offset_pop(); gfx_opacity_pop(); }
  gfx_clip_pop();
  /* the dots: the one shown long, the others small, gliding */
  { float dy = y + hh - 34, tot = 0; for (int i = 0; i < F.n; i++) { tween_to(&F.dotW[i], i == F.idx ? 34 : 12, 0.3f, EASE_WEB); tween_tick(&F.dotW[i], phDt); tot += F.dotW[i].v + 10; } tot -= 10;
    float dx = W / 2 - tot / 2; for (int i = 0; i < F.n; i++) { float w = F.dotW[i].v, on = clampf((w - 12) / 22, 0, 1); gfx_rrect(dx, dy, w, 12, 6, col_mix(WHITE_A(80), WHITE_A(240), on)); if (i == F.idx && app_now() - F.touchAt < 5 && !F.s.drag) { float f = (float)((app_now() - F.touchAt) / 5); gfx_clip_push(dx, dy - 1, w * f, 14); gfx_rrect(dx, dy, w, 12, 6, ACC2 | 0xff000000u); gfx_clip_pop(); } /* (the whole dot revealed from its start: never a rounded shape narrower than its rounding) */ dx += w + 10; } } /* (the one shown fills up until it moves on) */
  return hh;
}
static Board *me(void) { return &B[0]; } /* (one board: Home) */
static void lib_rows(Board *b) { itemlist_clear(&b->cw); lib_cw_items(&b->cw); itemlist_clear(&b->up); notify_upcoming(&b->up); b->lib = lib_version(); } /* Continue Watching (new episodes too) and Upcoming: from the library */

static void got_row(ItemList *l, int cached, void *ud) { (void)cached; unsigned tag = (unsigned)(uintptr_t)ud; Board *b = &B[tag >> 24]; int i = (tag >> 8) & 255; if ((tag & 255) != (b->gen & 255) || i >= b->n || !l) { if (l) itemlist_free(l); return; }
  HRow *r = &b->rows[i]; if (!l->n && l->total < 0 && !r->list.n && r->retries < 3) { static const double W[3] = { 8, 20, 45 }; r->retryAt = app_now() + W[r->retries]; r->retries++; itemlist_free(l); fprintf(stderr, "[home] %s: no answer, asked again in %.0f s\n", r->def.title, W[r->retries - 1]); return; } /* (an addon that makes its lists on demand: a slow first answer is not the end of the row) */
  if (l->n > 0 || !r->got) { itemlist_clear(&r->list); r->list = *l; free(l); } else itemlist_free(l); r->got = 1; }
static void load(Board *b, int disc) {
  CatalogDef defs[MAXR]; int n = disc ? sb_disc_defs(defs, MAXR) : sb_home_defs(defs, MAXR);
  live_refresh(0); if (live_catalogs() > 0) { int k = 0; for (int i = 0; i < n; i++) if (!live_type(defs[i].type)) defs[k++] = defs[i]; n = k; } /* the live catalogs: in the Live TV row's All channels, not as rows of posters */
  for (int i = 0; i < b->n; i++) itemlist_clear(&b->rows[i].list); memset(b->rows, 0, sizeof b->rows); b->n = n; b->gen++;
  for (int i = 0; i < n; i++) { b->rows[i].def = defs[i]; b->rows[i].retries = 0; b->rows[i].retryAt = 0; sb_catalog(&b->rows[i].def, got_row, (void *)(uintptr_t)((disc << 24) | (i << 8) | (b->gen & 255))); }
  lib_rows(b); b->fr = -1; (void)disc; F.n = 0;
}
static const Item *hero_item(Board *b) { for (int i = 0; i < b->n; i++) if (b->rows[i].list.n > 0 && (b->filter == 0 || !strcmp(b->rows[i].def.type, b->filter == 1 ? "series" : "movie"))) { b->heroRow = i; return &b->rows[i].list.items[0]; } return NULL; }
static const Item *focused_item(void) { Board *b = me(); if (b->fr == -5) return ph_live_band_item(b->fi); if (b->fr == -2) return b->cw.n > b->fi ? &b->cw.items[b->fi] : NULL; if (b->fr == -4) return b->up.n > b->fi ? &b->up.items[b->fi] : NULL; if (b->fr == -3) return hero_item(b); if (b->fr >= 0 && b->fr < b->n && b->fi < b->rows[b->fr].list.n) return &b->rows[b->fr].list.items[b->fi]; return NULL; }
static void focus(int r, int i) { Board *b = me(); b->fr = r; b->fi = i; const Item *it = focused_item(); if (it) { accent_tint_fetch(it->id, it->poster, it->logo); ph_logo_ahead(it); } } /* the title's colour is on its way before its page opens */
static void hero_act(int a, int b2) { (void)b2; Board *b = me(); const Item *it = hero_item(b); if (!it) return;
  if (a == 1) { if (!strcmp(it->type, "series")) { DetailParams p; memset(&p, 0, sizeof p); p.it = *it; p.openStreams = 1; app_go(SCR_DETAIL, &p, 0); } else ph_streams_show(it, it->id, NULL); }
  else if (a == 2) { lib_toggle(it); toast(lib_has(it->id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
  else { b->fr = -3; b->fi = 0; app_open_item(it); } }
static void filter_act(int a, int b2) { (void)b2; Board *b = me(); int nf = b->filter == a ? 0 : a; b->swDir = nf > b->filter ? 1 : -1; b->filter = nf; timer_start(&b->sw, 0.34f); } /* the rows slide in from the chip's side */
static void cat_pick(int i) { Board *b = me(); if (i >= 0 && i < b->n) { scroll_to(&b->v, b->lastY[i + 2]); } /* (it glides there; the end of the page is as far as it goes) */ }
static void cats_act(int a, int b2);
static int fAll; /* (Series or Movies chosen: an "All" chip first, back to everything) */
static void filter_set(int i, int b2) { (void)b2; Board *b = me(); int nf = fAll ? i : i + 1; if (nf == b->filter) return; b->swDir = nf > b->filter ? 1 : -1; b->filter = nf; timer_start(&b->sw, 0.34f); }
static void ctx_act(int i, int b2) { if (fAll) { if (i == 0) { filter_set(0, b2); return; } i--; } if (i == 2) cats_act(0, 0); else filter_act(i + 1, b2); }
static void cats_act(int a, int b2) { (void)a; (void)b2; Board *b = me(); const char *names[14]; static char buf[14][96]; int n = 0; for (int i = 0; i < b->n && n < 14; i++) { snprintf(buf[n], sizeof buf[n], "%s", T(b->rows[i].def.title)); names[n] = buf[n]; n++; } choices_open(T("Categories"), names, n, -1, cat_pick); }

static void enter(const void *params) { (void)params; Board *b = me(); bg_clear(); if (!b->n || b->lib != lib_version()) load(b, 0); else lib_rows(b); { extern int app_entered_by_swipe; if (app_entered_by_swipe) b->in.on = 0; else timer_start(&b->in, 0.4f); } } /* swiped back to: already in place */
static void leave(void) {}
static void tick(float dt) { (void)dt; { double now = app_now(); for (int k = 0; k < 2; k++) { Board *bb = &B[k]; for (int i = 0; i < bb->n; i++) { HRow *r = &bb->rows[i]; if (r->retryAt > 0 && now >= r->retryAt) { r->retryAt = 0; sb_catalog(&r->def, got_row, (void *)(uintptr_t)((k << 24) | (i << 8) | (bb->gen & 255))); } } } } Board *b = me(); timer_tick(&b->sw, dt); if (b->lib != lib_version()) lib_rows(b); timer_tick(&b->in, dt); }
static void draw(void) {
  Board *b = me(); int disc = 1; float in = ease_apply(EASE_OUT, timer_x(&b->in));
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &b->v, 1); scroll_tick(&b->v);
  /* Home: the cover of one title across the top (under the status bar), its logo, its genres, Play and My List on it;
     Discover: its rows from the top. No wordmark: the page is the content. */
  float y = -b->v.pos; if (disc) gfx_rect(0, 0, SB_W, SB_H, disc_page()); /* the page in the shown title's colour */
  { static Tween allT; static int seen; if (!seen) { seen = 1; tween_set(&allT, b->filter != 0); } /* the All chip: grows in beside Series when one is chosen, shrinks away (lit, as chosen) when All is */
    tween_to(&allT, b->filter != 0 ? 1 : 0, 0.3f, EASE_WEB); tween_tick(&allT, phDt);
    fAll = b->filter != 0 || allT.v > 0.01f; ctx_row(ctx_act); if (fAll) ctx_add_k(T("All"), b->filter == 0, allT.v); }
  ctx_add(T("Series"), b->filter == 1); ctx_add(T("Movies"), b->filter == 2); if (b->n) ctx_add(T("Categories"), 0); ctx_slide(filter_set, fAll ? 3 : 2); ctx_scrolled(b->v.pos);
  const Item *hi = disc ? NULL : hero_item(b);
  if (!disc) {
    float hh = SB_W * 0.86f + INS_T; char buf[600];
    const cJSON *hm = hi ? sb_meta_cached(hi->type, hi->id) : NULL; Item full; if (hm) { sb_fill_item(&full, hm, hi->type); if (!full.poster[0]) snprintf(full.poster, sizeof full.poster, "%s", hi->poster); } else if (hi) full = *hi;
    const char *bg = hi ? sb_bg_mid(&full, buf, sizeof buf) : NULL; GLuint t = bg && *bg ? img_get_shaped(bg, (int)SB_W, (int)hh, 0, 1) : 0;
    float py = y < 0 ? y * 0.5f : y; /* the cover moves at half the speed */
    /* as the page goes up, the cover dims, darkens at its edges and draws back a little; it fades in when it arrives */
    float gone = clampf(b->v.pos / (hh * 0.8f), 0, 1); static GLuint lastT; static Tween heroIn; if (t && t != lastT) { lastT = t; tween_set(&heroIn, 0); tween_to(&heroIn, 1, 0.6f, EASE_OUT); } tween_tick(&heroIn, phDt);
    float zs = 1.06f - 0.06f * ease_apply(EASE_OUT, heroIn.v) + 0.05f * gone; gfx_scale_push(zs, zs, SB_W / 2, py + hh * 0.45f);
    if (!t || !ph_picture(t, 0, py, SB_W, hh, 0, WHITE_A((int)(255 * heroIn.v * (1 - 0.85f * gone))))) ph_box(0, py, SB_W, hh, 0);
    gfx_scale_pop();
    if (gone > 0) { uint32_t pg = PAGE | 0xff000000u; float e = SB_W * 0.35f; /* the vignette closes in from the sides as it goes */
      gfx_rrect_h(0, py, e, hh, 0, col_alpha(pg, 0.9f * gone), col_alpha(pg, 0)); gfx_rrect_h(SB_W - e, py, e, hh, 0, col_alpha(pg, 0), col_alpha(pg, 0.9f * gone)); gfx_rect(0, py, SB_W, hh, col_alpha(pg, 0.45f * gone)); }
    if (hi) hit_add(0, y, SB_W, hh - 150, focus, NULL, -3, 0);
    gfx_rrect_v(0, py, SB_W, INS_T + 90, 0, BLACK_A(110), BLACK_A(0));
    gfx_rrect_v(0, y + hh * 0.38f, SB_W, hh * 0.62f + 2, 0, col_alpha(PAGE | 0xff000000u, 0), PAGE | 0xff000000u);
    float ly = y + hh - 250; char lb[600]; const char *logo = hi ? sb_logo_of(&full, lb, sizeof lb) : NULL; float lw = SB_W * 0.62f, lh = 120, asp = 0;
    GLuint lt = logo && *logo ? img_get(logo, (int)lw, (int)lh, &asp) : 0;
    if (lt && asp > 0) { float w = lw, h = w / asp; if (h > lh) { h = lh; w = h * asp; } gfx_image(lt, SB_W / 2 - w / 2, ly + lh - h, w, h, 0, WHITE_A(255), 0, 0, 1, 1); }
    else if (hi) text_draw_wrapped(hi->name, SB_W / 2, ly + 20, SB_W - 2 * PAD, 2, F_H1, 800, TXT, -0.6f, ALIGN_CENTER, 54);
    if (hi) { char g[200] = ""; const cJSON *gg; int k = 0; cJSON_ArrayForEach(gg, js_arr(hm, "genres")) if (cJSON_IsString(gg) && k < 3) { size_t l = strlen(g); snprintf(g + l, sizeof g - l, "%s%s", k ? "  \xc2\xb7  " : "", gg->valuestring); k++; }
      if (g[0]) text_draw(g, SB_W / 2, ly + lh + 12, F_SMALL, 600, WHITE_A(200), 0.2f, SB_W - 2 * PAD, ALIGN_CENTER); }
    float by = y + hh - 88, bw = (SB_W - 2 * PAD - 16) / 2;
    button(PAD, by, bw, 76, "play", T("Play"), 2, hero_act, 1, 0); button(PAD + bw + 16, by, bw, 76, hi && lib_has(hi->id) ? "ph_lib" : "ph_plus", T("My List"), 0, hero_act, 2, 0);
    y += hh + 30; }
  else { int ready = 0, total = 0; for (int i = 0; i < b->n; i++) { total++; ready += b->rows[i].got; }
    { static int fWas; if (fWas != b->filter) { fWas = b->filter; feat_switch(); } } /* (another choice: the spotlight's titles taken again, of that kind) */
    if (!F.n && (ready >= 2 || (total && ready == total))) feat_from_rows(b); y += feat_draw(y) + 24; } /* the spotlight across the top, from the first rows that came */
  float sk = b->sw.on ? ease_apply(EASE_OUT, timer_x(&b->sw)) : 1; gfx_opacity_push((0.3f + 0.7f * in) * (0.2f + 0.8f * sk)); gfx_offset_push(110 * b->swDir * (1 - sk), 0);
  { /* Continue Watching, always: with Series or Films picked, only that kind */
    static ItemList cwF; static int fKey = -1, fN = -1; static const Item *fP;
    if (fKey != b->filter || fN != b->cw.n || fP != b->cw.items) { fKey = b->filter; fN = b->cw.n; fP = b->cw.items; itemlist_clear(&cwF); cwF.items = calloc(b->cw.n ? b->cw.n : 1, sizeof(Item)); cwF.n = 0;
      for (int i = 0; i < b->cw.n; i++) if (b->filter == 0 || !strcmp(b->cw.items[i].type, b->filter == 1 ? "series" : "movie")) cwF.items[cwF.n++] = b->cw.items[i]; }
    { static float cwK = -1; if (cwF.n > 0) b->lastY[0] = y + b->v.pos; y += row_draw_k(T("Continue Watching"), b->filter ? &cwF : &b->cw, 1, y, &b->cws, -2, focus, b->fr == -2 ? b->fi : -1, &cwK); } }
  { static Scroll lvS; if (!b->filter) y += ph_live_row(y, &lvS, -5, focus, b->fr == -5 ? b->fi : -1); } /* a live addon on: its channels, All channels first */
  { static float upK = -1; if (b->filter != 2) y += row_draw_k(T("Upcoming episodes"), &b->up, 1, y, &b->ups, -4, focus, b->fr == -4 ? b->fi : -1, &upK); } /* the library's series airing in the next two weeks: wide cards as the TV's, the episode named on it and when it airs in a chip (Today, Tomorrow, In 3 days) */
  int waiting = 0;
  for (int i = 0; i < b->n; i++) { HRow *r = &b->rows[i]; b->lastY[i + 2] = y + b->v.pos - INS_T - 20;
    if (b->filter && strcmp(r->def.type, b->filter == 1 ? "series" : "movie")) continue;
    if (!r->got) { waiting++; continue; }
    char t[200]; snprintf(t, sizeof t, "%s", T(r->def.title)); y += row_draw(t, &r->list, 0, y, &r->s, i, focus, b->fr == i ? b->fi : -1); }
  gfx_offset_pop();
  if (waiting) for (int k = 0; k < 2 && y < SB_H; k++) { ph_text(PAD, y + 10, 260, 1, F_H2 - 2, 40, ALIGN_START); for (int c = 0; c < 4; c++) ph_box(PAD + c * (POSTER_W + 14), y + 52, POSTER_W, POSTER_H, 14); y += 52 + POSTER_H + 36; }
  gfx_opacity_pop();
  b->v.max = y + b->v.pos - (SB_H - INS_B - BAR_H - ctx_room()) + 24;
  /* the top: a shade under the status bar once the page has moved; Discover's name */
  float hk = b->v.pos > 120 ? 1 : b->v.pos / 120; hk = hk * hk * (3 - 2 * hk); phone_top_glass(INS_T, hk);
}
static int key(int k) { Board *b = me();
  if (k == K_OK) { const Item *it = focused_item(); if (it) app_open_item(it); return 1; }
  if (k == K_CH_UP) { scroll_top(&b->v); return 1; }
  if (k == K_BACK) { if (b->v.pos > 20) { scroll_top(&b->v); return 1; } return 0; }
  return 0;
}
static const Item *focused(void) { return focused_item(); }
static int origin(float *x, float *y) { *x = SB_W / 2; *y = SB_H / 2; return 0; }
Screen phHome = { enter, leave, key, tick, draw, focused, NULL, NULL, origin };
/* the TV's boards' hooks the core calls when the library changed */
void board_invalidate(void) { B[0].lib = B[1].lib = ~0u; }
/* the row a card is in (live TV: the channels beside the one played, for the next and the previous) */
int board_row_items(const char *id, const Item **items) { Board *b = me(); *items = NULL;
  { const Item *bi; int n = ph_live_band(&bi); for (int i = 0; i < n; i++) if (!strcmp(bi[i].id, id)) { *items = bi; return n; } } /* a channel of the Live TV row: that row is its line-up */
  for (int r = 0; r < b->n; r++) for (int i = 0; i < b->rows[r].list.n; i++) if (!strcmp(b->rows[r].list.items[i].id, id)) { *items = b->rows[r].list.items; return b->rows[r].list.n; }
  return 0; }
void board_rebuild(void) { lib_rows(me()); }
void board_refresh_quiet(void) { board_rebuild(); }
void board_from_bar(void) {}
void lib_from_bar(void) {}
void tabs_warm_tick(void) {}
/* the splash waits for Home's first rows; the screensaver and the background trailer ask the boards */
int board_home_ready(void) { Board *b = &B[0]; for (int i = 0; i < b->n; i++) if (b->rows[i].got) return 1; return b->n == 0; }
int board_spot_up(void) { return 0; }
int board_saver_items(Item *out, int max) { Board *b = &B[0]; int n = 0; for (int i = 0; i < b->n && n < max; i++) for (int k = 0; k < b->rows[i].list.n && n < max && k < 8; k++) out[n++] = b->rows[i].list.items[k]; return n; }
/* titles from Home's first poster rows (Discover's when Home has none) and the first row's name: the guide's and
   Settings' pictures */
int board_sample(Item *out, int max, char *rowTitle, int cap) { for (int d = 0; d < 2; d++) { Board *b = &B[d]; for (int i = 0; i < b->n; i++) if (b->rows[i].list.n > 0) {
      int n = 0; for (int k = 0; k < b->rows[i].list.n && n < max; k++) out[n++] = b->rows[i].list.items[k]; if (rowTitle && cap > 0) snprintf(rowTitle, cap, "%s", T(b->rows[i].def.title)); return n; } } return 0; }
