/* Library (filters, sort, a grid), Search (the field, suggestions, results), and sign-in. */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "phone/phone.h"
#include "ui/img.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/store.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/orbs.h"
#include "core/coll.h"
#include <math.h>

/* ---- Library ---- */
/* the same as the TV's: All, Movies, Series, In progress, Watched, Not watched (each with its count), four orders
   (recently added, recently watched, A-Z, newest), then Downloads: what is kept on the phone */
#define LNF 7
static const char *LF_LB[LNF] = { "All", "Movies", "Series", "In progress", "Watched", "Not watched", "Collections" };
static const int LF_ORD[LNF] = { 0, 5, 1, 2, 3, 4, 6 }; /* as shown: All, Not watched, then Movies, Series, In progress, Watched */
static void lib_filter(int a, int b);
static void lib_pick(int pos, int b) { lib_filter(LF_ORD[pos], b); }
static const char *LS_KEY[4] = { "added", "watched", "az", "year" };
static const char *LS_LB[4] = { "Recently added", "Recently watched", "A\xe2\x80\x93Z", "Newest" };
typedef struct { Item it; double added, seen; int year, cw, done; } LibIt;
static struct { LibIt *all; int nall; ItemList shown; unsigned lib; int filter, fi, counts[LNF]; Scroll v; Timer1 sw; int swDir; char lbl[LNF][64]; } L;
static double iso_ms(const cJSON *v) {
  if (cJSON_IsNumber(v)) return v->valuedouble;
  if (cJSON_IsString(v) && v->valuestring) { struct tm tm; memset(&tm, 0, sizeof tm); int ms = 0;
    if (sscanf(v->valuestring, "%d-%d-%dT%d:%d:%d.%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) { tm.tm_year -= 1900; tm.tm_mon -= 1; return (double)timegm(&tm) * 1000 + ms; } }
  return 0;
}
static int lib_sort_key(void) { const char *k = store_get("libSort", "added"); for (int i = 0; i < 4; i++) if (!strcmp(k, LS_KEY[i])) return i; return 0; }
static int lpass(const LibIt *o, int f) { switch (f) { case 0: return 1; case 1: return !strcmp(o->it.type, "movie"); case 2: return !strcmp(o->it.type, "series"); case 3: return o->cw; case 4: return o->done; case 5: return !o->done && !o->cw; default: return 0; } } /* (Downloads: none kept yet) */
/* the collections the Library's titles are in: one card each (its poster, its name), once its details are in and it
   has two titles or more; made again whenever more is known (as the TV's) */
static ItemList LC; static unsigned lcVer = ~0u, lcLib = ~0u;
static void lib_colls(void) {
  if (lcVer == coll_version() && lcLib == L.lib) return; lcVer = coll_version(); lcLib = L.lib; itemlist_clear(&LC); LC.items = calloc(64, sizeof(Item)); int asks = 0;
  for (int j = 0; j < L.nall && LC.n < 64; j++) { const Item *o = &L.all[j].it; char key[24]; const char *nm; if (coll_of(o->type, o->id, key, sizeof key, &nm) != 1) continue;
    int dup = 0; char cid[64]; snprintf(cid, sizeof cid, "coll:%s", key); for (int k = 0; k < LC.n && !dup; k++) dup = !strcmp(LC.items[k].id, cid); if (dup) continue;
    int kn = asks < 8 ? coll_known(key) : 0; if (kn == 0) { asks++; continue; } if (kn < 0) continue; const Coll *c = coll_get(key); if (!c || c->n < 2) continue;
    Item *it = &LC.items[LC.n++]; memset(it, 0, sizeof *it); snprintf(it->type, sizeof it->type, "collection"); snprintf(it->id, sizeof it->id, "%s", cid); snprintf(it->name, sizeof it->name, "%s", c->name[0] ? c->name : nm);
    snprintf(it->poster, sizeof it->poster, "%s", c->poster[0] ? c->poster : o->poster); snprintf(it->background, sizeof it->background, "%s", c->backdrop[0] ? c->backdrop : o->background); snprintf(it->videoId, sizeof it->videoId, "%s", o->id); }
  L.counts[6] = LC.n; if (L.filter == 6) { itemlist_clear(&L.shown); L.shown.items = calloc(LC.n ? LC.n : 1, sizeof(Item)); L.shown.n = LC.n; memcpy(L.shown.items, LC.items, sizeof(Item) * LC.n); } }
static int g_lsk;
static int lcmp(const void *a, const void *b) { const LibIt *x = a, *y = b; double d;
  if (g_lsk == 2) return strcasecmp(x->it.name, y->it.name);
  if (g_lsk == 3 && y->year != x->year) return y->year - x->year;
  if (g_lsk == 1 && (d = y->seen - x->seen) != 0) return d > 0 ? 1 : -1;
  d = y->added - x->added; return d > 0 ? 1 : d < 0 ? -1 : 0; }
static void lib_build(void) {
  free(L.all); L.all = NULL; L.nall = 0; L.lib = lib_version(); cJSON *lib = store_node("lib");
  int cap = cJSON_IsObject(lib) ? cJSON_GetArraySize(lib) : 0; L.all = calloc(cap ? cap : 1, sizeof *L.all); const cJSON *x;
  if (cap) cJSON_ArrayForEach(x, lib) {
    if (js_bool(x, "removed", 0) || !js_str(x, "name", "")[0]) continue;
    LibIt *o = &L.all[L.nall++]; Item *it = &o->it; const cJSON *st = js_obj(x, "state");
    snprintf(it->id, sizeof it->id, "%s", js_str(x, "_id", "")); snprintf(it->type, sizeof it->type, "%s", !strcmp(js_str(x, "type", ""), "series") ? "series" : "movie");
    snprintf(it->name, sizeof it->name, "%s", js_str(x, "name", "")); snprintf(it->poster, sizeof it->poster, "%s", js_str(x, "poster", ""));
    snprintf(it->background, sizeof it->background, "%s", js_str(x, "background", "")); snprintf(it->logo, sizeof it->logo, "%s", js_str(x, "logo", ""));
    const char *vid = js_str(st, "video_id", ""); snprintf(it->videoId, sizeof it->videoId, "%s", *vid ? vid : it->id);
    o->year = atoi(js_str(x, "year", "")); if (!o->year) o->year = atoi(js_str(x, "releaseInfo", ""));
    o->added = iso_ms(cJSON_GetObjectItemCaseSensitive(x, "_ctime")); if (!o->added) o->added = iso_ms(cJSON_GetObjectItemCaseSensitive(x, "_mtime"));
    o->seen = iso_ms(cJSON_GetObjectItemCaseSensitive(st, "lastWatched"));
    CwInfo ci; o->cw = lib_cw_info(it, &ci) && ci.pct > 0.01f; if (o->cw) { it->progress = ci.pct; it->left = ci.leftMin; } o->done = lib_item_watched(it); }
  g_lsk = lib_sort_key(); qsort(L.all, L.nall, sizeof *L.all, lcmp);
  for (int f = 0; f < LNF; f++) { L.counts[f] = 0; for (int j = 0; j < L.nall; j++) if (lpass(&L.all[j], f)) L.counts[f]++; }
  itemlist_clear(&L.shown); L.shown.items = calloc(L.nall ? L.nall : 1, sizeof(Item)); L.shown.n = 0;
  for (int j = 0; j < L.nall; j++) if (lpass(&L.all[j], L.filter)) L.shown.items[L.shown.n++] = L.all[j].it;
  lcVer = ~0u; lib_colls(); /* (the collections: their own cards) */
}
static int lf_pos(int f) { for (int p = 0; p < LNF; p++) if (LF_ORD[p] == f) return p; return 0; }
static void lib_filter(int a, int b) { (void)b; if (a == L.filter) return; L.swDir = lf_pos(a) > lf_pos(L.filter) ? 1 : -1; /* (from the chip's side as shown) */ L.filter = a; L.v.pos = 0; lib_build(); timer_start(&L.sw, 0.34f); } /* the grid slides in from the chip's side */
static void sort_pick(int i) { if (i < 0 || i > 3) return; store_set("libSort", LS_KEY[i]); lib_build(); timer_start(&L.sw, 0.34f); L.swDir = 0; }
static void sort_act(int a, int b) { (void)a; (void)b; const char *o[4]; for (int i = 0; i < 4; i++) o[i] = T(LS_LB[i]); choices_open(T("Order"), o, 4, lib_sort_key(), sort_pick); }
static void lib_focus(int a, int i) { (void)a; L.fi = i; if (i >= 0 && i < L.shown.n) { accent_tint_fetch(L.shown.items[i].id, L.shown.items[i].poster, L.shown.items[i].logo); ph_logo_ahead(&L.shown.items[i]); } }
static void lib_enter(const void *p) { (void)p; bg_clear(); extern int app_entered_by_swipe; if (!app_entered_by_swipe || !L.all || L.lib != lib_version()) lib_build(); L.sw.on = 0; } /* (swiped to: already built and on screen as it is) */
static void lib_tick(float dt) { timer_tick(&L.sw, dt); if (L.lib != lib_version()) lib_build(); else if (L.all) lib_colls(); }
static void lib_draw(void) {
  if (!L.all || L.lib != lib_version()) lib_build(); /* (drawn before it is entered: a page swiped in shows its titles, not an empty grid that fills on landing) */
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &L.v, 1); scroll_tick(&L.v);
  float y = INS_T + 200 - L.v.pos; /* (under the header) */
  float sk = L.sw.on ? ease_apply(EASE_OUT, timer_x(&L.sw)) : 1; gfx_opacity_push(0.2f + 0.8f * sk); gfx_offset_push(110 * L.swDir * (1 - sk), L.swDir ? 0 : 30 * (1 - sk));
  if (0) {}
  else if (!sb_signed_in()) text_draw_wrapped(T("Sign in to keep your Library and where you stopped, the same as on your TV."), PAD, y + 20, SB_W - 2 * PAD, 4, F_BODY, 500, TXT2, 0, ALIGN_START, 34);
  else if (!L.shown.n) text_draw_wrapped(L.nall ? T("Nothing here yet. Try another filter") : T("Your library is empty."), PAD, y + 20, SB_W - 2 * PAD, 3, F_BODY, 500, TXT2, 0, ALIGN_START, 34);
  else y += poster_grid(&L.shown, y, lib_focus, 0, L.fi);
  gfx_offset_pop(); gfx_opacity_pop();
  L.v.max = y + L.v.pos - (SB_H - INS_B - BAR_H - ctx_room()) + 30;
  /* the header: the name large in the middle, under it a pill with how many and the order (a tap changes the order);
     scrolled, it gathers into a slim bar of glass with the name alone */
  { float k = clampf(L.v.pos / 140, 0, 1); k = k * k * (3 - 2 * k); float hh = INS_T + 190 - 100 * k;
    phone_top_glass(hh, k); if (k > 0.01f) gfx_rrect_v(0, hh, SB_W, 70, 0, col_alpha(PAGE | 0xff000000u, 0.55f * k), col_alpha(PAGE | 0xff000000u, 0)); /* the glass melts into the posters, no edge */
    float ty = INS_T + 30 - 14 * k, px = F_H1 + 10 - (F_H1 + 10 - F_H2) * k;
    { float mw = SB_W - 2 * (PAD + 60 + 16); float fp = ph_fit_px(T("Library"), px, 900, -1.0f + 0.6f * k, mw); text_draw(T("Library"), SB_W / 2, ty + (text_line_height((int)px) - text_line_height((int)fp)) / 2.0f, (int)fp, 900, TXT, -1.0f + 0.6f * k, mw, ALIGN_CENTER); } /* (clear of the order button beside it, in every language) */
    /* the order: a pill with the count under the name; scrolled, it gathers into a round button at the top right */
    char n[16], line[120]; snprintf(n, sizeof n, "%d", L.counts[0]); snprintf(line, sizeof line, "%s  \xc2\xb7  %s", T1("{n} titles", "n", n), T(LS_LB[lib_sort_key()]));
    float tw = text_width(line, F_SMALL, 700, 0), w0 = tw + 76, h0 = 54, x0 = SB_W / 2 - w0 / 2, y0 = INS_T + 108;
    float d = 60, x1 = SB_W - PAD - d, y1 = ty + text_line_height((int)px) / 2.0f - d / 2;
    float w = w0 + (d - w0) * k, h = h0 + (d - h0) * k, x = x0 + (x1 - x0) * k, py = y0 + (y1 - y0) * k;
    float pk = press_k(x, py, w, h), sc = 1 - 0.05f * pk; gfx_scale_push(sc, sc, x + w / 2, py + h / 2);
    gfx_rrect(x, py, w, h, h / 2, WHITE_A(pk > 0 ? (int)(16 + 24 * pk) : 14)); gfx_ring(x, py, w, h, h / 2, 1, WHITE_A(30));
    icon_draw("lib_sort", x + (18 + (w / 2 - 15 - 18) * k), py + h / 2 - 15, 30, ACC2);
    if (k < 0.6f) { gfx_opacity_push(1 - k / 0.6f); gfx_clip_push(x, py, w, h); text_draw(line, x + 56, py + h / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, WHITE_A(225), 0, 0, ALIGN_LEFT); gfx_clip_pop(); gfx_opacity_pop(); }
    gfx_scale_pop(); hit_act(x, py, w, h, sort_act, 0, 0); }
  ctx_row(lib_pick); for (int p = 0; p < LNF; p++) { int i = LF_ORD[p]; snprintf(L.lbl[p], sizeof L.lbl[p], "%s  %d", T(LF_LB[i]), L.counts[i]); ctx_add(L.lbl[p], i == L.filter); } ctx_pinned(); /* (the Library's filters stay put) */
}
static int lib_key(int k) { if (k == K_OK && L.fi >= 0 && L.fi < L.shown.n) { app_open_item(&L.shown.items[L.fi]); return 1; } if (k == K_CH_UP) { scroll_top(&L.v); return 1; } if (k == K_BACK) { app_go(SCR_HOME, NULL, 1); return 1; } return 0; }
static const Item *lib_focused(void) { return L.fi >= 0 && L.fi < L.shown.n ? &L.shown.items[L.fi] : NULL; }
/* ---- Downloads: what is kept on the phone (the third tab): a card per film or episode alone, or per season (its
   episodes inside when open). A tap plays a finished one, pauses or goes on with one under way; swiped left it is
   deleted (a tick of the phone as it passes the point of no return); held, the stream it came from in full. Select
   picks several (a season's card: all its episodes) to delete at once ---- */
#include "core/best.h"
void pplat_haptic(void);
static Scroll dlV;
static void dl_settings_act(int a, int b);
static double dlIn; /* when the page came (the cards rise in one after the other) */
static void dl_enter(const void *p) { (void)p; bg_clear(); extern int app_entered_by_swipe; dlIn = app_entered_by_swipe ? app_now() - 10 : app_now(); } /* (swiped to: it was already on screen, no second entrance) */
int ph_dl_after_film(const Dl *d); const char *ph_dl_wait_word(const Dl *d);
static const char *st_word(int st) { return st == DL_RUNNING ? T("Downloading") : st == DL_QUEUED ? (ph_dl_waiting_wifi() ? T("Waiting for Wi-Fi") : T("Waiting")) : st == DL_PAUSED ? T("Paused") : st == DL_FAILED ? T("Stopped: tap to try again") : st == DL_CHOOSING ? T("Finding a stream") : st == DL_NOSTREAM ? T("No stream suits this phone: tap to try again") : st == DL_INPACK ? T("In the season\xe2\x80\x99s pack") : st == DL_DONE ? T("Downloaded") : ""; }
static void bytes_s(double b, char *o, size_t c) { if (b >= 1e9) snprintf(o, c, "%.1f GB", b / 1e9); else snprintf(o, c, "%.0f MB", b / 1e6); }
/* per card or row, by its key: its swipe, its shown numbers (read once a second: no digits racing), its eased bar */
typedef struct { char k[260]; Scroll s; float sx, sv; int drag, past, gone; double goneAt, readAt; float svPad; long long got, size, spd; int conns, nconns; Tween bar, paused, sel, pastT, lift; double fx0; int live; } DlRow;
static DlRow DR[96]; static int nDR;
static DlRow *dr(const char *k) { for (int i = 0; i < nDR; i++) if (!strcmp(DR[i].k, k)) { DR[i].live = 1; return &DR[i]; }
  int f = nDR < 96 ? nDR++ : -1; if (f < 0) { for (int i = 0; i < 96; i++) if (!DR[i].live) { f = i; break; } if (f < 0) f = 0; }
  memset(&DR[f], 0, sizeof DR[f]); snprintf(DR[f].k, sizeof DR[f].k, "%s", k); DR[f].live = 1; return &DR[f]; }
static void dr_read(DlRow *r, long long got, long long size, long long spd, int force) { if (force || !r->readAt || app_now() - r->readAt > 1.0) { r->readAt = app_now(); r->got = got; r->size = size; r->spd = spd; r->conns = r->nconns; } }
/* the seasons shown open; what is picked in Select */
static char dlOpen[64][260]; static int nOpen;
static int is_open(const char *k) { for (int i = 0; i < nOpen; i++) if (!strcmp(dlOpen[i], k)) return 1; return 0; }
static void grp_key(const Dl *d, char *k, size_t c) { if (ph_dl_group_is_coll(d)) snprintf(k, c, "%s", d->group); else snprintf(k, c, "%s|%s", d->id, d->group); } /* (a collection's films: one group across their titles) */
static int selMode; static Tween selT; static char selK[128][110]; static int nSel;
static void sel_key(const Dl *d, char *k, size_t c) { snprintf(k, c, "%s|%s", d->id, d->vid); }
static int is_sel(const Dl *d) { char k[110]; sel_key(d, k, sizeof k); for (int i = 0; i < nSel; i++) if (!strcmp(selK[i], k)) return 1; return 0; }
static void sel_set(const Dl *d, int on) { char k[110]; sel_key(d, k, sizeof k); for (int i = 0; i < nSel; i++) if (!strcmp(selK[i], k)) { if (!on) { memmove(selK[i], selK[i + 1], (size_t)(nSel - i - 1) * sizeof selK[0]); nSel--; } return; } if (on && nSel < 128) snprintf(selK[nSel++], 110, "%s", k); }
static int same_grp(const Dl *a, const Dl *b) { return !strcmp(a->group, b->group) && (ph_dl_group_is_coll(a) || !strcmp(a->id, b->id)); }
static int grp_count(const Dl *d) { int n = 0; for (int j = 0; j < ph_dl_count(); j++) if (same_grp(ph_dl_get(j), d)) n++; return n; }
static int grp_all_sel(const Dl *d) { for (int j = 0; j < ph_dl_count(); j++) { const Dl *o = ph_dl_get(j); if (same_grp(o, d) && !is_sel(o)) return 0; } return 1; }
static void grp_sel(const Dl *d, int on) { for (int j = 0; j < ph_dl_count(); j++) { const Dl *o = ph_dl_get(j); if (same_grp(o, d)) sel_set(o, on); } }
/* deleting: the file, the engine's task and its part files, the entry; then "Deleted" */
static void del_vid(const char *id, const char *vid) { for (int j = ph_dl_count() - 1; j >= 0; j--) { const Dl *o = ph_dl_get(j); if (!strcmp(o->id, id) && !strcmp(o->vid, vid)) { ph_dl_remove(j); return; } } }
static void del_grp(const char *id, const char *g) { int cl = !strncmp(g, "coll:", 5); for (int j = ph_dl_count() - 1; j >= 0; j--) { const Dl *o = ph_dl_get(j); if ((cl || !strcmp(o->id, id)) && !strcmp(o->group, g)) ph_dl_remove(j); } }
static void tip_ok(int a, int b) { (void)a; (void)b; store_set("dlTipSeen", "true"); pplat_haptic_k(HK_PICK, 0.6f); }
static int dl_hold_ended(void);
static void dl_pause_act(int i, int b) { (void)b; if (dl_hold_ended()) return; ph_dl_retry(i); pplat_haptic_k(HK_PICK, 0.6f); } /* its own button: pause or go on */
static void dl_tap(int i, int b) { (void)b; const Dl *d = ph_dl_get(i); if (!d || dl_hold_ended()) return;
  if (selMode) { sel_set(d, !is_sel(d)); pplat_haptic_k(HK_PICK, 0.6f); return; }
  if (d->state == DL_DONE) ph_dl_play(i); else if ((d->state == DL_RUNNING || d->state == DL_QUEUED || d->state == DL_PAUSED) && d->url[0]) ph_dl_play_any(i); /* still coming: it plays now, from its stream */ else if (d->state == DL_INPACK) { for (int j = 0; j < ph_dl_count(); j++) { const Dl *o = ph_dl_get(j); if (j != i && !strcmp(o->url, d->url) && o->state == DL_DONE) { ph_dl_play(j); return; } } } else ph_dl_retry(i); }
static void grp_tap(int i, int b) { (void)b; const Dl *d = ph_dl_get(i); if (!d || dl_hold_ended()) return;
  if (selMode && b != 1) { grp_sel(d, !grp_all_sel(d)); pplat_haptic_k(HK_PICK, 0.8f); return; } /* (a season's card in Select: all its episodes) */
  char k[260]; grp_key(d, k, sizeof k); for (int q = 0; q < nOpen; q++) if (!strcmp(dlOpen[q], k)) { memmove(dlOpen[q], dlOpen[q + 1], (size_t)(nOpen - q - 1) * sizeof dlOpen[0]); nOpen--; return; } if (nOpen < 64) snprintf(dlOpen[nOpen++], 260, "%s", k); }
/* held: the stream it came from, in full */
static double dlHoldId = -1, dlHoldFired = -2, dlHoldAt;
static int dl_held(float x, float y, float w, float h) { if (selMode || !ph_pressed(x, y, w, h) || ph_dragging()) return 0; double id = ph_last_press(); if (id != dlHoldId) { dlHoldId = id; dlHoldAt = app_now(); }
  if (dlHoldFired != id) ph_hold_feel((float)((app_now() - dlHoldAt - 0.1) / 0.4)); /* ticks as it fills */
  if (app_now() - dlHoldAt > 0.5 && dlHoldFired != id) { dlHoldFired = id; pplat_haptic_k(HK_POP, 1); store_set("dlHoldSeen", "true"); store_set("dlTipSeen", "true"); return 1; } return 0; }
static int dl_hold_ended(void) { return dlHoldFired == ph_last_press(); }
/* ---- the stream in full: its poster and name, what it is at a glance (chips), then the addon's own words, the file ---- */
static int dlInfo = -1; static Scroll dliV;
/* words laid into a width, broken inside a word when one is longer than the line (file names): nothing runs out */
static int wrap_lines(const char *t, float w, int size, int weight, char out[][300], int max) { int n = 0; char cur[300] = ""; const char *p = t;
  while (*p && n < max) { const char *e = p; while (*e && *e != ' ' && *e != '\n') e++; char word[300]; snprintf(word, sizeof word, "%.*s", (int)(e - p), p);
    char tryb[600]; snprintf(tryb, sizeof tryb, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (text_width(tryb, size, weight, 0) <= w) snprintf(cur, sizeof cur, "%s", tryb);
    else { if (cur[0]) { snprintf(out[n++], 300, "%s", cur); cur[0] = 0; if (n >= max) break; }
      while (text_width(word, size, weight, 0) > w && n < max) { int k = (int)strlen(word); while (k > 1) { char c2[300]; snprintf(c2, sizeof c2, "%.*s", k, word); if (text_width(c2, size, weight, 0) <= w) break; k--; } while (k > 1 && (word[k] & 0xC0) == 0x80) k--; /* (never inside a letter) */
        snprintf(out[n++], 300, "%.*s", k, word); memmove(word, word + k, strlen(word + k) + 1); }
      snprintf(cur, sizeof cur, "%s", word); }
    p = e; if (*p == ' ') p++; else if (*p == '\n') { if (n < max) { snprintf(out[n++], 300, "%s", cur); cur[0] = 0; } p++; } }
  if (cur[0] && n < max) snprintf(out[n++], 300, "%s", cur); return n; }
static float info_box(float x, float y, float w, const char *head, const char *body, int size, int weight, uint32_t col, int maxl, float lh) {
  static char L[40][300]; int n = wrap_lines(body, w - 48, size, weight, L, maxl < 40 ? maxl : 40); float h = (head ? 58 : 22) + n * lh + 20;
  gfx_rrect(x, y, w, h, 22, WHITE_A(10)); gfx_ring(x, y, w, h, 22, 1, WHITE_A(22)); gfx_clip_push(x, y, w, h);
  float yy = y + 22; if (head) { text_draw(head, x + 24, yy, F_TINY, 800, ACC2, 0.5f, w - 48, ALIGN_START); yy += 36; }
  for (int i = 0; i < n; i++) text_draw(L[i], x + 24, yy + i * lh, size, weight, col, 0, w - 48, ALIGN_START); gfx_clip_pop(); return h; }
static void dli_draw(float a) { const Dl *d = ph_dl_get(dlInfo); if (!d) { ph_sheet_close(); return; } float h = SB_H * 0.74f, y0 = sheet_panel(h, a), cw = SB_W - 2 * PAD;
  ph_drag(0, y0 + 30, SB_W, h - 30, &dliV, 1); scroll_tick(&dliV); gfx_clip_push(0, y0 + 24, SB_W, h - 24); float y = y0 + 44 - dliV.pos;
  /* its name and episode; then boxes: the addon and its name for the stream, the stream's own lines, the file */
  int nl = text_draw_wrapped(d->name, PAD, y, cw, 2, F_H2, 800, TXT, -0.3f, ALIGN_START, 44); y += nl * 44 + 4;
  { char st[200]; char sz[32] = ""; if (d->size > 0) bytes_s((double)d->size, sz, sizeof sz); snprintf(st, sizeof st, "%s%s%s%s%s", d->ep, d->ep[0] ? "  \xc2\xb7  " : "", st_word(d->state), sz[0] ? "  \xc2\xb7  " : "", sz); text_draw(st, PAD, y, F_SMALL, 600, TXT2, 0, cw, ALIGN_START); y += 50; }
  if (!d->info[0]) { y += info_box(PAD, y, cw, NULL, T("No details kept for this download."), F_SMALL, 500, TXT2, 3, 32) + 14; }
  else { const char *p = d->info, *e1 = strchr(p, '\n'); char addon[200], rest[900]; snprintf(addon, sizeof addon, "%.*s", e1 ? (int)(e1 - p) : (int)strlen(p), p); p = e1 ? e1 + 1 : p + strlen(p);
    const char *e2 = strchr(p, '\n'); char name[400]; snprintf(name, sizeof name, "%.*s", e2 ? (int)(e2 - p) : (int)strlen(p), p); p = e2 ? e2 + 1 : p + strlen(p); snprintf(rest, sizeof rest, "%s", p);
    char *fl = strrchr(rest, '\n'); char file[300] = ""; if (fl && d->file[0] == 0) { snprintf(file, sizeof file, "%s", fl + 1); }
    if (name[0]) y += info_box(PAD, y, cw, addon[0] ? addon : NULL, name, F_BODY, 700, TXT, 6, 36) + 14;
    if (rest[0]) y += info_box(PAD, y, cw, T("DETAILS"), rest, F_SMALL, 500, WHITE_A(210), 30, 32) + 14; }
  if (d->file[0]) { const char *fn = strrchr(d->file, '/'); y += info_box(PAD, y, cw, T("FILE"), fn ? fn + 1 : d->file, F_SMALL, 500, TXT2, 6, 32) + 14; }
  dliV.max = y + dliV.pos - (y0 + h) + 40; if (dliV.max < 0) dliV.max = 0; gfx_clip_pop(); }
static const PhSheet dliSheet = { dli_draw, NULL, NULL };
static void dl_info_open(int i) { dlInfo = i; memset(&dliV, 0, sizeof dliV); ph_sheet_open(&dliSheet); }
/* a swipe left on a card or a row: it follows the finger, a tick when it is far enough, let go there it slides away and is deleted */
static float swipe(DlRow *r, float x, float y, float w, float h) { /* (its own drag state, kept with the row: the finger's system holds on to it) */
  r->s.free = 1; r->s.max = 0; if (!selMode && !r->gone) ph_drag(x, y, w, h, &r->s, 0);
  float dt = phDt > 0.05f ? 0.05f : phDt;
  if (r->s.drag) { float p = r->s.pos, nx = p < 0 ? -36 * (1 - 1 / (1 - p / 120)) : p; /* left freely; right only a little, against a pull */
      if (dt > 0) r->sv = r->sv * 0.4f + (nx - r->sx) / dt * 0.6f; r->sx = nx; r->drag = 1;
      int past = r->sx > w * 0.42f; if (past != r->past) { r->past = past; pplat_haptic_k(past ? HK_CLICK : HK_TICK, past ? 1 : 0.4f); } } /* past the line: a click; back: a soft tick */
  else { if (r->drag) { r->drag = 0; if (r->past || r->sv > 1800) { r->gone = 1; r->goneAt = app_now(); store_set("dlTipSeen", "true"); if (r->sv < 2600) r->sv = 2600; pplat_haptic_k(HK_THUD, 1); } }
    if (r->gone) { r->sv *= 1 + 2.5f * dt; r->sx += r->sv * dt; if (r->sx > w * 1.3f) r->sx = w * 1.3f; } /* away, faster as it goes */
    else { const float k = 520, c = 2 * sqrtf(520) * 0.68f; for (int q = 0; q < 3; q++) { float h2 = dt / 3, acc = -k * r->sx - c * r->sv; r->sv += acc * h2; r->sx += r->sv * h2; } /* home on a spring, a breath past it */
      if (fabsf(r->sx) < 0.3f && fabsf(r->sv) < 4) { r->sx = 0; r->sv = 0; } r->past = 0; } }
  tween_to(&r->pastT, r->past || r->gone ? 1 : 0, 0.22f, EASE_WEB); tween_tick(&r->pastT, phDt);
  tween_to(&r->lift, r->drag ? 1 : 0, 0.18f, EASE_WEB); tween_tick(&r->lift, phDt);
  r->s.pos = r->sx; r->s.vel = 0; return r->sx; }
/* under a row being swiped: red opening in the gap the finger makes, the bin growing in it; past the line the red
   fills behind the whole row and the bin pops */
static void swipe_under(DlRow *R, float x, float y, float w, float h, float sx, float r) { if (sx < 1 && R->pastT.v < 0.01f) return;
  float gap = sx > 0 ? sx : 0, pt = R->pastT.v, open = clampf(gap / (w * 0.42f), 0, 1);
  float gx = x + w - gap - 24 * pt * 0 , gw = gap + r; float fx = gx + (x - gx) * pt, fw = gw + (w - gw) * pt; /* the gap, then all of it */
  gfx_rrect(fx, y, fw > 0 ? fw : 0, h, r, col_mix(RGB(0xc0, 0x34, 0x34), RGB(0xe5, 0x3e, 0x3e), pt));
  float icx = x + w - (gap > 0 ? gap / 2 : 0) - (1 - open) * 0, icy = y + h / 2; if (pt > 0) icx = icx + (x + w - 60 - icx) * pt;
  float sc = (0.55f + 0.45f * open) * (1 + 0.22f * sinf(pt * (float)M_PI)); gfx_opacity_push(clampf(gap / 50, 0, 1)); gfx_scale_push(sc, sc, icx, icy); icon_draw("ph_trash", icx - 20, icy - 20, 40, WHITE_A(255)); gfx_scale_pop(); gfx_opacity_pop(); }
/* the bar and its words, quiet: one line, read once a second */
/* a download's numbers, quiet and readable, read once a second: its size (how far of how much), then how fast and how
   long left; compact (an episode's row): one line */
int ph_dl_peers(const Dl *d, char *out, int cap);
static char rowEta[48]; /* an episode row's time left, drawn by its row on the line above (across from its name) */
static void prog_line(DlRow *r, const Dl *dd, float x, float y, float w, int st, int compact) { if (compact) rowEta[0] = 0;
  char l1[96] = "", l2[200] = "", a[32], b2[32], pl[160]; int tor = st == DL_RUNNING && dd && ph_dl_peers(dd, pl, sizeof pl); /* a torrent: its peers and speed, as in the player */ long long got = r->got, size = r->size;
  if (size > 0) { bytes_s((double)size, b2, sizeof b2); if (st == DL_DONE || got >= size) snprintf(l1, sizeof l1, "%s", b2); else { bytes_s((double)got, a, sizeof a); if (compact) snprintf(l1, sizeof l1, "%s / %s", a, b2); else snprintf(l1, sizeof l1, "%s %s %s", a, T("of"), b2); } } /* (an episode row: short) */
  if (st == DL_RUNNING && r->spd > 0 && size > got) { char sp[32], m[16], eta[48]; if (r->spd >= 1000000) snprintf(sp, sizeof sp, "%.1f MB/s", r->spd / 1e6); else snprintf(sp, sizeof sp, "%.0f KB/s", r->spd / 1e3);
    long long left = (size - got) / (r->spd > 0 ? r->spd : 1); if (left >= 3600) { snprintf(m, sizeof m, "%lld", left / 3600); snprintf(eta, sizeof eta, "%s", T1("{n} h left", "n", m)); } else if (left >= 60) { snprintf(m, sizeof m, "%lld", left / 60); snprintf(eta, sizeof eta, "%s", T1("{m} min left", "m", m)); } else snprintf(eta, sizeof eta, "%s", T("Less than a minute left"));
    if (compact) { snprintf(l2, sizeof l2, "%s", sp); snprintf(rowEta, sizeof rowEta, "%s", eta); } /* (an episode in its series: its own speed, the series' header the total) */ else if (tor) snprintf(l2, sizeof l2, "%s  \xc2\xb7  %s  \xc2\xb7  %s", pl, sp, eta); else snprintf(l2, sizeof l2, "%s  \xc2\xb7  %s", sp, eta); } /* (a torrent: its peers, its own speed, its time left; the same on every row) */
  else if (tor) snprintf(l2, sizeof l2, "%s", pl); /* (no speed yet: finding peers) */
  if (compact) { char one[200]; snprintf(one, sizeof one, "%s%s%s", l1, l1[0] && l2[0] ? "  \xc2\xb7  " : "", l2); text_draw(one, x, y, F_TINY + 1, 600, TXT3, 0, w, ALIGN_START); return; }
  text_draw(l1, x, y, F_SMALL, 600, TXT2, 0, w, ALIGN_START); if (l2[0]) text_draw(l2, x, y + 32, F_SMALL - 1, 600, TXT3, 0, w, ALIGN_START); }
static float dl_card(int i, float y, float ink) {
  const Dl *d = ph_dl_get(i); int n = ph_dl_count(); float x = PAD, w = SB_W - 2 * PAD; char key[260]; grp_key(d, key, sizeof key);
  int ne = d->group[0] ? grp_count(d) : 1, grp = ne > 1, emin = 9999, emax = 0;
  long long got = 0, size = 0, spd = 0; int conns = 0, cnt = 0, done = 0, run = 0, choose = 0, bad = 0, paused = 0;
  for (int j = 0; j < n; j++) { const Dl *o = ph_dl_get(j); if (grp ? !same_grp(o, d) : j != i) continue; cnt++; long long os = o->size; got += o->state == DL_DONE ? os : o->got; size += os; if (o->state == DL_RUNNING) { spd += o->speed; conns += o->conns; }
    done += o->state == DL_DONE || o->state == DL_INPACK; run += o->state == DL_RUNNING || o->state == DL_QUEUED; choose += o->state == DL_CHOOSING; bad += o->state == DL_NOSTREAM || o->state == DL_FAILED; paused += o->state == DL_PAUSED;
    const char *c = strrchr(o->vid, ':'); int e = c ? atoi(c + 1) : 0; if (e < emin) emin = e; if (e > emax) emax = e; }
  DlRow *R = dr(grp ? key : d->vid); int st = grp ? (done == cnt ? DL_DONE : run ? DL_RUNNING : choose ? DL_CHOOSING : paused ? DL_PAUSED : bad ? DL_FAILED : DL_PAUSED) : d->state;
  static int lastSt[96]; int ri = (int)(R - DR); R->nconns = conns; dr_read(R, got, size, spd, lastSt[ri] != st); lastSt[ri] = st; /* (a change of state: read at once) */
  /* how open: the season's episodes slide down under it */
  int open = grp && is_open(key); static Tween opT[96]; tween_to(&opT[ri], open ? 1 : 0, 0.34f, EASE_WEB); tween_tick(&opT[ri], phDt); float ok = opT[ri].v;
  float h = 236, rowH = 96, extra = grp ? (cnt * rowH + 12) * ok : 0;
  float fold = R->gone ? clampf((float)((app_now() - R->goneAt - 0.18) / 0.28), 0, 1) : 0; fold = fold * fold * (3 - 2 * fold); float H = (h + extra) * (1 - fold);
  if (R->gone && fold >= 1) { if (grp) del_grp(d->id, d->group); else del_vid(d->id, d->vid); toast(T("Deleted"), 1400, NULL); R->gone = 0; R->sx = 0; return 0; }
  gfx_opacity_push(ink * (1 - fold)); gfx_offset_push(0, 40 * (1 - ink));
  if (y + H > -20 && y < SB_H && H > 4) { gfx_clip_push(0, y, SB_W, H);
    tween_to(&R->sel, selMode ? 1 : 0, 0.3f, EASE_WEB); tween_tick(&R->sel, phDt); float sk = R->sel.v, inset = 64 * sk; /* Select: a round tick slides in on the left */
    int picked = grp ? grp_all_sel(d) : is_sel(d);
    float sx = swipe(R, x, y, w, h); swipe_under(R, x, y, w, h + extra, sx, 24); gfx_offset_push(-sx, 0); { float lk = 1 - 0.015f * R->lift.v; gfx_scale_push(lk, lk, x + w / 2, y + h / 2); } if (R->lift.v > 0.01f) gfx_shadow(x, y + 8 * R->lift.v, w, h, 24, 24 * R->lift.v, BLACK_A((int)(110 * R->lift.v))); /* taken: it lifts a little off the page */
    gfx_rrect(x, y, w, h + extra, 24, SURF); gfx_ring(x, y, w, h + extra, 24, 1, picked && selMode ? col_alpha(ACC2 | 0xff000000u, 0.9f) : WHITE_A(12));
    float pk = press_k(x, y, w, h); if (pk > 0 && sx < 2) gfx_rrect(x, y, w, h, 24, WHITE_A((int)(14 * pk)));
    if (sk > 0.01f) { float cx = x + 20 + 22 * sk, cy = y + h / 2; gfx_opacity_push(sk); gfx_ring(cx - 16, cy - 16, 32, 32, 16, 2, picked ? ACC2 : WHITE_A(110)); if (picked) { gfx_circle(cx, cy, 16, ACC2 | 0xff000000u); icon_draw("ph_check", cx - 12, cy - 12, 24, RGB(14, 12, 20)); } gfx_opacity_pop(); }
    float pw = 120, ph = 180, px = x + 15 + inset; int cg = grp && ph_dl_group_is_coll(d); const char *pst = d->poster; /* (a collection: its own poster and name, its films under it) */
    if (cg) { char ck[24]; const char *a0 = d->group + 5, *u = strchr(a0, '\x1f'); snprintf(ck, sizeof ck, "%.*s", u ? (int)(u - a0) : (int)strlen(a0), a0); const Coll *c = coll_get(ck); if (c && c->poster[0]) pst = c->poster; }
    GLuint t = pst[0] ? img_get_shaped(pst, (int)pw, (int)ph, 14, 2) : 0; if (!t || !ph_picture(t, px, y + 15, pw, ph, 14, WHITE_A(255))) ph_box(px, y + 15, pw, ph, 14);
    float tx = px + pw + 22, tw = x + w - tx - (!grp && d->state != DL_DONE && d->url[0] ? 170 : 96); int nl = text_draw_wrapped(cg ? ph_dl_group_name(d) : d->name, tx, y + 22, tw, cg ? 2 : 1 + !d->ep[0], F_BODY + 1, 800, TXT, 0, ALIGN_START, 34);
    char sub[200], c1[16]; if (grp) { snprintf(c1, sizeof c1, "%d", cnt); char r1[16], r2[16]; snprintf(r1, sizeof r1, "%d", emin); snprintf(r2, sizeof r2, "%d", emax);
      if (cg) snprintf(sub, sizeof sub, "%s", T1("{n} films", "n", c1)); else snprintf(sub, sizeof sub, "%s  \xc2\xb7  %s", d->group, emax - emin + 1 == ne && emin > 0 ? T2("Episodes {a}\xe2\x80\x93{b}", "a", r1, "b", r2) : T1("{n} episodes", "n", c1)); } else snprintf(sub, sizeof sub, "%s", d->ep);
    if (sub[0]) text_draw(sub, tx, y + 26 + nl * 34, F_SMALL, 600, TXT2, 0, tw, ALIGN_START);
    const char *stw; char sw[96]; if (grp) { char c2[16]; snprintf(c1, sizeof c1, "%d", done); snprintf(c2, sizeof c2, "%d", cnt); if (done == cnt) stw = T("Downloaded"); else if (choose) stw = T("Finding streams"); else if (run) { snprintf(sw, sizeof sw, "%s", T2("Downloading  {a} of {b}", "a", c1, "b", c2)); stw = sw; } else if (bad) stw = T("Some have no stream: open to try again"); else stw = T("Paused"); } else stw = ph_dl_wait_word(d) ? ph_dl_wait_word(d) : st_word(st);
    int warm = st == DL_CHOOSING; uint32_t scol = st == DL_FAILED || st == DL_NOSTREAM ? RGB(0xef, 0x8f, 0x8a) : st == DL_RUNNING ? ACC2 : st == DL_DONE ? ACC3 : TXT2;
    { float sy0 = y + h - 118, pc = R->size > 0 && st != DL_DONE ? 100.0f * R->got / R->size : -1; char sl[140]; if (pc >= 0 && !grp) snprintf(sl, sizeof sl, "%s  \xc2\xb7  %d%%", stw, (int)pc); else snprintf(sl, sizeof sl, "%s", stw); /* its state, plain to see: a dot of its colour, the word, how far */
      if (warm) spinner(tx + 9, sy0 + text_line_height(F_SMALL) / 2.0f, 9); else gfx_circle(tx + 7, sy0 + text_line_height(F_SMALL) / 2.0f, 6, scol | 0xff000000u);
      text_draw(sl, tx + 24, sy0, F_SMALL, 800, scol, 0, tw + 40, ALIGN_START); }
    prog_line(R, grp ? NULL : d, tx, y + h - 80, tw + 60, st, 0);
    float pr = R->size > 0 ? (float)((double)R->got / R->size) : 0; tween_to(&R->bar, pr, 1.0f, EASE_CSS_EASE); tween_tick(&R->bar, phDt); /* (eased across each second's reading: smooth, never racing) */
    tween_to(&R->paused, st == DL_RUNNING || st == DL_CHOOSING || st == DL_DONE ? 0 : 1, 0.3f, EASE_CSS_EASE); tween_tick(&R->paused, phDt);
    if (st != DL_DONE) { float bw = x + w - 24 - tx; gfx_rrect(tx, y + h - 22, bw, 6, 3, WHITE_A(30)); gfx_rrect(tx, y + h - 22, bw * R->bar.v, 6, 3, col_mix(ACC | 0xff000000u, WHITE_A(140), R->paused.v)); }
    /* the button: play when kept; else pause and go on, the two signs crossing over */
    { float bx = x + w - 66, by = y + h / 2 - 6; if (grp) { icon_draw_rot("ph_chev", bx - 18, by - 18, 36, WHITE_A(200), 90 * ok); hit_act(bx - 40, y, 80, h, grp_tap, i, 1); }
      else if (st == DL_DONE) { gfx_circle(bx, by, 36, WHITE_A(240)); icon_draw("play", bx - 20, by - 20, 40, RGB(14, 12, 20)); }
      else if (!warm && d->url[0] && st != DL_FAILED && st != DL_NOSTREAM) { float pz = R->paused.v, qx = bx - 78; /* coming: play now (white), and pause / go on beside it */
        gfx_circle(bx, by, 32, WHITE_A(240)); icon_draw("play", bx - 17, by - 17, 34, RGB(14, 12, 20)); hit_act(bx - 40, y, 80, h, dl_tap, i, 0);
        gfx_circle(qx, by, 28, WHITE_A(20)); gfx_opacity_push(1 - pz); icon_draw("p_pause", qx - 15, by - 15, 30, WHITE_A(235)); gfx_opacity_pop(); gfx_opacity_push(pz); icon_draw("ph_down", qx - 15, by - 15, 30, WHITE_A(235)); gfx_opacity_pop(); hit_act(qx - 34, by - 40, 68, 80, dl_pause_act, i, 0); }
      else if (!warm) { float pz = R->paused.v; gfx_circle(bx, by, 36, WHITE_A(20)); gfx_opacity_push(1 - pz); icon_draw("p_pause", bx - 18, by - 18, 36, WHITE_A(235)); gfx_opacity_pop(); gfx_opacity_push(pz); icon_draw(st == DL_FAILED || st == DL_NOSTREAM ? "restart" : "play", bx - 18, by - 18, 36, WHITE_A(235)); gfx_opacity_pop(); } }
    if (sx < 2) { int two = !grp && st != DL_DONE && !warm && d->url[0] && st != DL_FAILED && st != DL_NOSTREAM; hit_act(x, y, w - (grp ? 90 : two ? 170 : 0), h, grp ? grp_tap : dl_tap, i, 0); /* (its own two buttons on the right while it comes; else all of it plays) */ if (!grp && dl_held(x, y, w, h)) dl_info_open(i); }
    /* its episodes */
    if (ok > 0.004f) { float ry = y + h; int r = 0; for (int j = 0; j < n; j++) { const Dl *o = ph_dl_get(j); if (!same_grp(o, d)) continue;
        float rk = clampf(ok * 1.5f - r * 0.1f, 0, 1); rk = 1 - (1 - rk) * (1 - rk); r++; DlRow *E = dr(o->vid); E->nconns = o->conns; dr_read(E, o->state == DL_DONE ? o->size : o->got, o->size, o->state == DL_RUNNING ? o->speed : 0, 0);
        float colk = E->gone ? clampf((float)((app_now() - E->goneAt - 0.16) / 0.26), 0, 1) : 0; colk = colk * colk * (3 - 2 * colk); /* gone off the side: its room folds shut, the rows below glide up */
        gfx_opacity_push(rk * (1 - colk)); gfx_offset_push(0, (1 - rk) * -14); float rx = x + 8, rw = w - 16, rh = (rowH - 8) * (1 - colk); gfx_clip_push(rx - 2, ry, rw + 4, rh + 2);
        float esx = ok > 0.9f ? swipe(E, rx, ry, rw, rowH - 8) : 0; swipe_under(E, rx, ry, rw, rh, esx, 18); gfx_offset_push(-esx, 0); { float lk = 1 - 0.02f * E->lift.v; gfx_scale_push(lk, lk, rx + rw / 2, ry + rh / 2); }
        if (esx > 0.5f || E->lift.v > 0.01f) { if (E->lift.v > 0.01f) gfx_shadow(rx, ry + 6 * E->lift.v, rw, rh, 18, 18 * E->lift.v, BLACK_A((int)(110 * E->lift.v))); gfx_rrect(rx, ry, rw, rh, 18, SURF); } /* (solid while it moves: the red never shows through it) */
        int ep = is_sel(o); gfx_rrect(rx, ry, rw, rh, 18, ep && selMode ? col_alpha(ACC, 0.22f) : WHITE_A(8)); float ppk = press_k(rx, ry, rw, rh); if (ppk > 0 && esx < 2) gfx_rrect(rx, ry, rw, rh, 18, WHITE_A((int)(14 * ppk)));
        float ex0 = rx + 24 + inset; if (sk > 0.01f) { float cx = rx + 16 + 22 * sk, cy = ry + rh / 2; gfx_opacity_push(sk); gfx_ring(cx - 13, cy - 13, 26, 26, 13, 2, ep ? ACC2 : WHITE_A(110)); if (ep) gfx_circle(cx, cy, 13, ACC2 | 0xff000000u); gfx_opacity_pop(); }
        rowEta[0] = 0;
        { const char *w2 = ph_dl_wait_word(o) ? ph_dl_wait_word(o) : st_word(o->state); float tw2 = text_draw(w2, ex0, ry + 50, F_TINY, 700, o->state == DL_NOSTREAM || o->state == DL_FAILED ? RGB(0xef, 0x8f, 0x8a) : o->state == DL_RUNNING ? ACC2 : TXT3, 0, rw * 0.5f, ALIGN_START);
          if (E->size > 0) { text_draw("\xc2\xb7", ex0 + tw2 + 10, ry + 50, F_TINY, 700, TXT3, 0, 0, ALIGN_START); prog_line(E, o, ex0 + tw2 + 24, ry + 50, rx + rw - 160 - (ex0 + tw2 + 24), o->state, 1); } }
        { float ew = rowEta[0] ? text_width(rowEta, F_TINY, 600, 0) + 16 : 0, right = rx + rw - 164; /* its name, and across from it its time left (clear of its ring) */
          if (rowEta[0]) text_draw(rowEta, right, ry + 14 + (text_line_height(F_SMALL + 1) - text_line_height(F_TINY)) / 2.0f, F_TINY, 600, TXT3, 0, 0, ALIGN_END);
          text_draw(cg ? o->name : o->ep, ex0, ry + 14, F_SMALL + 1, 700, TXT, 0, right - ew - ex0 - 8, ALIGN_START); } /* (a collection's row: its film's name) */
        /* its ring: fills as it comes; paused, the pause sign comes into it */
        float cx = rx + rw - 44, cy = ry + rh / 2; float opr = E->size > 0 ? (float)((double)E->got / E->size) : 0; tween_to(&E->bar, opr, 1.0f, EASE_CSS_EASE); tween_tick(&E->bar, phDt); tween_to(&E->paused, o->state == DL_PAUSED || o->state == DL_FAILED ? 1 : 0, 0.3f, EASE_CSS_EASE); tween_tick(&E->paused, phDt); float pz = E->paused.v;
        if (o->state != DL_DONE && o->state != DL_CHOOSING && o->url[0] && o->state != DL_FAILED && o->state != DL_NOSTREAM) { float px2 = cx - 64; gfx_circle(px2, cy, 22, WHITE_A(235)); icon_draw("play", px2 - 12, cy - 12, 24, RGB(14, 12, 20)); if (ok > 0.9f && esx < 2) hit_act(px2 - 30, ry, 60, rh, dl_tap, j, 0); } /* coming: play now */
        if (o->state == DL_DONE) { gfx_circle(cx, cy, 24, WHITE_A(235)); icon_draw("play", cx - 14, cy - 14, 28, RGB(14, 12, 20)); } else if (o->state == DL_CHOOSING) spinner(cx, cy, 12);
        else { gfx_ring(cx - 22, cy - 22, 44, 44, 22, 3, WHITE_A(34)); uint32_t rc = col_mix(ACC2 | 0xff000000u, WHITE_A(150), pz); int qn = (int)(60 * E->bar.v); for (int q = 0; q <= qn && q <= 60; q++) { float an = q * 6 * (float)M_PI / 180; gfx_circle(cx + 20.5f * sinf(an), cy - 20.5f * cosf(an), 1.6f, rc); }
          gfx_opacity_push(pz); gfx_rrect(cx - 6, cy - 8, 4, 16, 2, WHITE_A(235)); gfx_rrect(cx + 2, cy - 8, 4, 16, 2, WHITE_A(235)); gfx_opacity_pop(); gfx_opacity_push(1 - pz); icon_draw("ph_down", cx - 11, cy - 11, 22, WHITE_A(200)); gfx_opacity_pop(); }
        gfx_scale_pop(); gfx_offset_pop(); gfx_clip_pop(); gfx_offset_pop(); gfx_opacity_pop();
        if (E->gone && app_now() - E->goneAt > 0.44) { char id[64], vid[96]; snprintf(id, sizeof id, "%s", o->id); snprintf(vid, sizeof vid, "%s", o->vid); E->gone = 0; E->sx = 0; del_vid(id, vid); toast(T("Deleted"), 1400, NULL); break; }
        if (ok > 0.9f && esx < 2) { hit_act(rx, ry, rw - 100, rh, dl_tap, j, 0); hit_act(rx + rw - 76, ry, 76, rh, o->state == DL_DONE ? dl_tap : dl_pause_act, j, 0); if (dl_held(rx, ry, rw, rh)) dl_info_open(j); } ry += rowH * ok * (1 - colk); } }
    gfx_scale_pop(); gfx_offset_pop(); gfx_clip_pop(); }
  gfx_offset_pop(); gfx_opacity_pop(); return H + 16 * (1 - fold); }
/* Select: the button at the top left, the bar over the tabs (all or none, delete what is picked) */
static void sel_toggle(int a, int b) { (void)a; (void)b; selMode = !selMode; nSel = 0; pplat_haptic_k(HK_PICK, 0.6f); }
static void sel_all(int a, int b) { (void)a; (void)b; int all = nSel >= ph_dl_count(); nSel = 0; if (!all) for (int j = 0; j < ph_dl_count(); j++) sel_set(ph_dl_get(j), 1); pplat_haptic(); }
static void sel_del(int a, int b) { (void)a; (void)b; if (!nSel) return; int n = 0; for (int j = ph_dl_count() - 1; j >= 0; j--) { const Dl *o = ph_dl_get(j); if (is_sel(o)) { ph_dl_remove(j); n++; } } nSel = 0; selMode = 0; char c[16]; snprintf(c, sizeof c, "%d", n); toast(n == 1 ? T("Deleted") : T1("{n} deleted", "n", c), 1600, NULL); pplat_haptic(); }
static void dl_draw(void) {
  for (int i = 0; i < nDR; i++) DR[i].live = 0;
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &dlV, 1); scroll_tick(&dlV);
  phone_col_push(1100); /* (sideways, a tablet: a readable width, in the middle) */
  float y = INS_T + 150 - dlV.pos; int n = ph_dl_count(); if (!n && selMode) selMode = 0;
  { /* the first visit: what can be done with a download, shown once (gone on "Got it", or once a download is held or swiped) */
    static Tween tipT; int show = n && !store_bool("dlTipSeen", 0); static int seenT; if (!seenT) { seenT = 1; tween_set(&tipT, show); } tween_to(&tipT, show ? 1 : 0, 0.35f, EASE_WEB); tween_tick(&tipT, phDt);
    if (tipT.v > 0.01f) { float th = 236, k = tipT.v, x = PAD, w = SB_W - 2 * PAD, ty = y - 6; gfx_clip_push(0, ty - 4, SB_W, th * k + 4); gfx_opacity_push(k);
      ph_glass(x, ty, w, th, 26, RGBA(16, 14, 22, 110)); gfx_ring(x, ty, w, th, 26, 1, WHITE_A(30));
      float t = (float)fmod(app_now(), 2.4), sl = t < 0.4f ? 0 : t < 1.3f ? (t - 0.4f) / 0.9f : t < 1.9f ? 1 : 1 - (t - 1.9f) / 0.5f; sl = sl * sl * (3 - 2 * sl); /* a little card swiped left, again and again */
      float mx = x + 26, mw = 92, mh = 56, my = ty + 26; gfx_rrect(mx, my, mw, mh, 14, RGB(0xe5, 0x3e, 0x3e)); icon_draw("ph_trash", mx + mw - 38, my + mh / 2 - 14, 28, WHITE_A((int)(255 * sl)));
      gfx_clip_push(mx, my, mw, mh); gfx_rrect(mx - 44 * sl, my, mw, mh, 14, RGB(0x3a, 0x36, 0x48)); gfx_rrect(mx - 44 * sl + 10, my + 12, 22, 32, 6, WHITE_A(60)); gfx_rrect(mx - 44 * sl + 40, my + 18, 38, 6, 3, WHITE_A(90)); gfx_rrect(mx - 44 * sl + 40, my + 32, 26, 6, 3, WHITE_A(50)); gfx_clip_pop(); /* (inside its own lane) */
      text_draw(T("Swipe left to delete"), mx + mw + 22, my + mh / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, TXT, 0, w - mw - 70, ALIGN_START);
      float hy = my + mh + 22, hp = (float)fmod(app_now(), 2.0) < 1.2 ? (float)(fmod(app_now(), 2.0) / 1.2) : 0; /* a finger held: a ring filling */
      gfx_circle(mx + mw / 2, hy + 28, 24, WHITE_A(20)); for (int q = 0; q <= 30; q++) { if (q > 30 * hp) break; float an = q * 12 * (float)M_PI / 180; gfx_circle(mx + mw / 2 + 22 * sinf(an), hy + 28 - 22 * cosf(an), 2.2f, ACC2 | 0xff000000u); } gfx_circle(mx + mw / 2, hy + 28, 9, WHITE_A(200));
      text_draw(T("Hold one to see its stream"), mx + mw + 22, hy + 28 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, TXT, 0, w - mw - 70, ALIGN_START);
      { const char *gl = T("Got it"); float gw = text_width(gl, F_SMALL, 800, 0) + 44, gx = x + w - 20 - gw, gy = ty + th - 20 - 54; gfx_rrect(gx, gy, gw, 54, 27, WHITE_A(235)); text_draw(gl, gx + gw / 2, gy + 27 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 800, RGB(14, 12, 20), 0, 0, ALIGN_CENTER); if (k > 0.9f) hit_act(gx - 8, gy - 8, gw + 16, 70, tip_ok, 0, 0); }
      gfx_opacity_pop(); gfx_clip_pop(); y += (th + 18) * k; } }
  if (!n) { float cy = INS_T + 88 + (SB_H - INS_T - 88 - INS_B - BAR_H) * 0.38f - dlV.pos;
    { float t = (float)app_now(), bob = sinf(t * 2.2f) * 8, pul = 1 + 0.04f * sinf(t * 2.2f + 1); gfx_circle(SB_W / 2, cy, 84 * pul, col_alpha(ACC, 0.22f)); gfx_ring(SB_W / 2 - 84 * pul, cy - 84 * pul, 168 * pul, 168 * pul, 84 * pul, 1.5f, col_alpha(ACC2 | 0xff000000u, 0.4f)); icon_draw("ph_down", SB_W / 2 - 42, cy - 42 + bob, 84, ACC3); }
    text_draw(T("Nothing downloaded yet"), SB_W / 2, cy + 128, F_H2 - 2, 800, TXT, -0.2f, SB_W - 2 * PAD, ALIGN_CENTER);
    text_draw_wrapped(T("Use Download on a film or an episode: it plays here without a connection."), SB_W / 2, cy + 190, SB_W - 2 * PAD - 40, 4, F_BODY, 500, TXT2, 0, ALIGN_CENTER, 34); y = cy + 300; }
  int shown = 0;
  for (int i = 0; i < n; i++) { const Dl *d = ph_dl_get(i); if (d->group[0] && grp_count(d) > 1) { int first = 1; for (int j = 0; j < i; j++) if (same_grp(ph_dl_get(j), d)) first = 0; if (!first) continue; } /* a season: one card */
    float ink = clampf((float)((app_now() - dlIn - 0.05 - shown * 0.06) / 0.4), 0, 1); ink = 1 - (1 - ink) * (1 - ink) * (1 - ink); shown++;
    y += dl_card(i, y, ink); n = ph_dl_count(); }
  tween_to(&selT, selMode ? 1 : 0, 0.3f, EASE_WEB); tween_tick(&selT, phDt);
  dlV.max = y + dlV.pos - (SB_H - INS_B - BAR_H) + 20 + 120 * selT.v; if (dlV.max < 0) dlV.max = 0;
  { float k = clampf(dlV.pos / 140, 0, 1); k = k * k * (3 - 2 * k); phone_top_glass(INS_T + 150 - 60 * k, k); float px = F_H1 + 10 - (F_H1 + 10 - F_H2) * k, cy = INS_T + 34 - 18 * k + text_line_height((int)px) / 2.0f;
    { float sw = n ? text_width(selMode ? T("Done") : T("Select"), F_SMALL, 700, 0) + 40 : 0, side = (sw > 72 ? sw : 72) + 16, mw = SB_W - 2 * (PAD + side); float fp = ph_fit_px(T("Downloads"), px, 900, -1.0f + 0.6f * k, mw); /* (between Select and its settings, in every language) */
      text_draw(T("Downloads"), SB_W / 2, INS_T + 34 - 18 * k + (text_line_height((int)px) - text_line_height((int)fp)) / 2.0f, (int)fp, 900, TXT, -1.0f + 0.6f * k, mw, ALIGN_CENTER); }
    { float r = 36, cx = SB_W - PAD - r, pk = press_k(cx - r, cy - r, 2 * r, 2 * r), sc = 1 - 0.08f * pk; /* its settings, beside its name */
      gfx_scale_push(sc, sc, cx, cy); ph_glass(cx - r, cy - r, 2 * r, 2 * r, r, RGBA(16, 14, 22, 92)); gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 1, WHITE_A(30)); icon_draw("ph_sliders", cx - 20, cy - 20, 40, WHITE_A(230)); gfx_scale_pop();
      hit_act(cx - r - 8, cy - r - 8, 2 * r + 16, 2 * r + 16, dl_settings_act, 0, 0); }
    if (n) { const char *sl = selMode ? T("Done") : T("Select"); float w = text_width(sl, F_SMALL, 700, 0) + 40, h2 = 72, x = PAD, pk = press_k(x, cy - h2 / 2, w, h2), sc = 1 - 0.06f * pk; /* Select, on the other side */
      gfx_scale_push(sc, sc, x + w / 2, cy); ph_glass(x, cy - h2 / 2, w, h2, h2 / 2, selMode ? col_alpha(ACC, 0.5f) : RGBA(16, 14, 22, 92)); gfx_ring(x, cy - h2 / 2, w, h2, h2 / 2, 1, WHITE_A(30)); text_draw(sl, x + w / 2, cy - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, TXT, 0, 0, ALIGN_CENTER); gfx_scale_pop();
      hit_act(x - 8, cy - h2 / 2 - 8, w + 16, h2 + 16, sel_toggle, 0, 0); } }
  if (selT.v > 0.01f) { float bh = 92, by = SB_H - INS_B - BAR_H - bh - 24 + (1 - selT.v) * 160, bx = PAD, bw = SB_W - 2 * PAD; gfx_opacity_push(selT.v); /* the bar: all or none, delete */
    gfx_shadow(bx, by + 8, bw, bh, bh / 2, 30, BLACK_A(140)); ph_glass(bx, by, bw, bh, bh / 2, RGBA(16, 14, 22, 150)); gfx_ring(bx, by, bw, bh, bh / 2, 1, WHITE_A(30));
    const char *al = nSel >= n && n ? T("Select none") : T("Select all"); text_draw(al, bx + 34, by + bh / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, TXT, 0, 0, ALIGN_START); hit_act(bx, by, bw / 2, bh, sel_all, 0, 0);
    char dlb[48], c[16]; snprintf(c, sizeof c, "%d", nSel); snprintf(dlb, sizeof dlb, "%s", nSel ? T1("Delete {n}", "n", c) : T("Delete")); float dw = text_width(dlb, F_SMALL + 1, 800, 0) + 50, dx = bx + bw - 12 - dw;
    gfx_rrect(dx, by + 12, dw, bh - 24, (bh - 24) / 2, nSel ? RGB(0xe5, 0x3e, 0x3e) : WHITE_A(20)); text_draw(dlb, dx + dw / 2, by + bh / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, nSel ? WHITE_A(255) : TXT3, 0, 0, ALIGN_CENTER); hit_act(dx, by, dw, bh, sel_del, 0, 0);
    gfx_opacity_pop(); }
  phone_col_pop();
}
static void dl_settings_act(int a, int b) { (void)a; (void)b; void ph_settings_downloads(void); ph_settings_downloads(); app_go(SCR_SET, NULL, 0); }
static int dl_key(int k) { if (k == K_BACK && selMode) { selMode = 0; nSel = 0; return 1; } if (k == K_BACK) { app_go(SCR_HOME, NULL, 1); return 1; } if (k == K_CH_UP) { scroll_top(&dlV); return 1; } return 0; }
Screen phDl = { dl_enter, NULL, dl_key, NULL, dl_draw, NULL, NULL, NULL, NULL };
Screen phLib = { lib_enter, NULL, lib_key, lib_tick, lib_draw, lib_focused, NULL, NULL, NULL };

/* ---- Search ---- */
#define MAXQ 12
static struct { char q[160]; double typedAt; int asked, sugAsked, editing; CatalogDef defs[MAXQ]; ItemList res[MAXQ]; int nd, got; char sug[6][128]; int nsug; ItemList sugItems; unsigned gen; Scroll v; int fr, fi; } Q;
static void q_got(ItemList *l, int cached, void *ud) { (void)cached; unsigned t = (unsigned)(uintptr_t)ud; if ((t >> 8) != (Q.gen & 0xffffff) || !l) { if (l) itemlist_free(l); return; } int k = t & 255; if (k < Q.nd) { itemlist_clear(&Q.res[k]); Q.res[k] = *l; free(l); Q.got++; } else itemlist_free(l); }
static void q_sug(ItemList *l, char names[6][128], int nn, void *ud) { if ((unsigned)(uintptr_t)ud != Q.gen) { if (l) itemlist_free(l); return; } Q.nsug = nn > 6 ? 6 : nn; for (int i = 0; i < Q.nsug; i++) snprintf(Q.sug[i], 128, "%s", names[i]); itemlist_clear(&Q.sugItems); if (l) { Q.sugItems = *l; free(l); } }
static void q_ask(void) { for (int i = 0; i < Q.nd; i++) itemlist_clear(&Q.res[i]); Q.nd = Q.got = 0; Q.gen++; if (!Q.q[0]) Q.nsug = 0; itemlist_clear(&Q.sugItems); /* (the suggestions already shown stay) */ if (!Q.q[0]) return;
  Q.nd = sb_search_defs(Q.q, Q.defs, MAXQ); for (int i = 0; i < Q.nd; i++) sb_catalog(&Q.defs[i], q_got, (void *)(uintptr_t)(((Q.gen & 0xffffff) << 8) | i)); if (!Q.sugAsked) { Q.sugAsked = 1; sb_suggest(Q.q, q_sug, (void *)(uintptr_t)Q.gen); } }
/* the suggestions on their own, as soon as the typing pauses a moment: long before the posters */
static unsigned sugGen; static void q_sug_only(ItemList *l, char names[6][128], int nn, void *ud) { if ((unsigned)(uintptr_t)ud != sugGen) { if (l) itemlist_free(l); return; } Q.nsug = nn > 6 ? 6 : nn; for (int i = 0; i < Q.nsug; i++) snprintf(Q.sug[i], 128, "%s", names[i]); itemlist_free(l); }
static TEd qE; static TEd *q_ed(void) { if (!qE.s) { qE.s = Q.q; qE.cap = sizeof Q.q; } return &qE; }
static void q_text(const char *t) { ted_insert(q_ed(), t); Q.typedAt = app_now(); Q.asked = 0; Q.sugAsked = 0; }
static void field_act(int a, int b) { (void)a; (void)b; Q.editing = 1; phone_keyboard(1); }
static void clear_act(int a, int b) { (void)a; (void)b; Q.q[0] = 0; q_ask(); Q.editing = 1; phone_keyboard(1); }
static void sug_act(int i, int b) { (void)b; if (i < Q.nsug) { snprintf(Q.q, sizeof Q.q, "%s", Q.sug[i]); Q.asked = 1; q_ask(); Q.editing = 0; phone_keyboard(0); } }
static void q_focus(int r, int i) { Q.fr = r; Q.fi = i; if (r >= 0 && r < Q.nd && i < Q.res[r].n) accent_tint_fetch(Q.res[r].items[i].id, Q.res[r].items[i].poster, Q.res[r].items[i].logo); }
static void q_enter(const void *p) { (void)p; bg_clear(); Q.fr = -1; if (!Q.q[0]) { Q.editing = 1; phone_keyboard(1); } }
static void q_leave(void) { phone_keyboard(0); Q.editing = 0; }
static void q_tick(float dt) { (void)dt; if (!Q.sugAsked && Q.q[0] && app_now() - Q.typedAt > 0.12) { Q.sugAsked = 1; sugGen++; sb_suggest(Q.q, q_sug_only, (void *)(uintptr_t)sugGen); } /* the words first */
  if (!Q.asked && Q.q[0] && app_now() - Q.typedAt > 0.45) { Q.asked = 1; q_ask(); } }
static void so_key_back(void);
static void close_act(int a, int b) { (void)a; (void)b; so_key_back(); }
static void q_draw(void) {
  float fieldY = SB_H - (KB_H > INS_B ? KB_H : INS_B) - 18 - 88, top = INS_T + 88; /* the field rides on the keyboard */
  ph_drag(0, top, SB_W, fieldY - top, &Q.v, 1); scroll_tick(&Q.v);
  float y = top + 10 - Q.v.pos;
  for (int i = 0; i < Q.nd; i++) if (Q.res[i].n > 0) { char t[200]; snprintf(t, sizeof t, "%s", Q.defs[i].title); for (char *c = t; *c; c++) if (*c != ' ' && *c != '.' && (unsigned char)*c != 0xc2 && (unsigned char)*c != 0xb7 && *c != '-') { text_draw(t, PAD, y, F_H2 - 2, 800, TXT, -0.3f, SB_W - 2 * PAD, ALIGN_START); y += 52; break; } /* no heading when it has no words */ y += poster_grid(&Q.res[i], y, q_focus, i, Q.fr == i ? Q.fi : -1) + 20; }
  if (Q.q[0] && Q.asked && Q.got < Q.nd) spinner(SB_W / 2, y + 50, 20);
  if (!Q.q[0]) { float iy = top + (fieldY - top) * 0.35f; icon_draw("ph_search", SB_W / 2 - 50, iy, 100, WHITE_A(40)); text_draw_wrapped(T("Films, series and people, from your addons."), SB_W / 2, iy + 130, SB_W - 4 * PAD, 3, F_BODY, 500, TXT2, 0, ALIGN_CENTER, 34); }
  else if (Q.asked && Q.got >= Q.nd && Q.nd) { int any = 0; for (int i = 0; i < Q.nd; i++) any += Q.res[i].n; if (!any) text_draw(T("None found"), PAD, y + 10, F_BODY, 500, TXT2, 0, 0, ALIGN_START); }
  Q.v.max = y + Q.v.pos - fieldY + 40;
  { float th = INS_T + 88; gfx_rect(0, 0, SB_W, th, col_alpha(PAGE | 0xff000000u, 0.6f)); text_draw(T("Search"), PAD, INS_T + 44 - text_line_height(F_H2) / 2.0f, F_H2, 800, TXT, -0.3f, 0, ALIGN_START); }
  if (Q.editing && KB_H > 0) hit_act(0, 0, SB_W, SB_H, close_act, 0, 0); /* typing: a tap anywhere else puts the keyboard away and goes back to the page */
  /* the bottom: the page fades under it, suggestions rise above the field, nearest the thumb first */
  gfx_rrect_v(0, fieldY - 100, SB_W, SB_H - fieldY + 100, 0, col_alpha(PAGE | 0xff000000u, 0), col_alpha(PAGE | 0xff000000u, 0.95f));
  if (Q.q[0] && Q.nsug && Q.editing) { float rh = 76, sh = Q.nsug * rh + 16, sy = fieldY - 16 - sh;
    gfx_shadow(PAD - 8, sy, SB_W - 2 * PAD + 16, sh, 26, 30, BLACK_A(130)); glass_rrect(PAD - 8, sy, SB_W - 2 * PAD + 16, sh, 26, RGBA(30, 27, 40, 245), 0.16f, 0.02f, 0.6f); hit_shield(PAD - 8, sy, SB_W - 2 * PAD + 16, sh);
    for (int i = 0; i < Q.nsug; i++) { float ry = sy + 8 + (Q.nsug - 1 - i) * rh; if (ph_pressed(PAD, ry, SB_W - 2 * PAD, rh)) gfx_rrect(PAD, ry, SB_W - 2 * PAD, rh, 18, WHITE_A(16));
      icon_draw("ph_search", PAD + 16, ry + 20, 36, WHITE_A(110)); text_draw(Q.sug[i], PAD + 68, ry + rh / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 500, TXT, 0, SB_W - 2 * PAD - 84, ALIGN_START); hit_act(PAD, ry, SB_W - 2 * PAD, rh, sug_act, i, 0); } }
  float r = 44, cy = fieldY + 44; int pr = ph_pressed(18, fieldY, 2 * r, 2 * r); gfx_shadow(18, fieldY + 6, 2 * r, 2 * r, r, 22, BLACK_A(120)); glass_rrect(18, fieldY, 2 * r, 2 * r, r, pr ? RGBA(60, 56, 74, 240) : RGBA(34, 31, 44, 236), 0.2f, 0.02f, 0.6f);
  icon_draw("ph_back", 18 + r - 22, cy - 22, 44, WHITE_A(240)); hit_act(18, fieldY, 2 * r, 2 * r, close_act, 0, 0);
  float fx = 18 + 2 * r + 12, fw = SB_W - 18 - fx; gfx_shadow(fx, fieldY + 6, fw, 88, 44, 22, BLACK_A(120));
  glass_rrect(fx, fieldY, fw, 88, 44, RGBA(34, 31, 44, 240), 0.2f, 0.02f, 0.6f); if (Q.editing) gfx_ring(fx, fieldY, fw, 88, 44, 2, col_alpha(ACC2, 0.8f));
  icon_draw("ph_search", fx + 22, cy - 20, 40, WHITE_A(150));
  hit_act(fx, fieldY, 76, 88, field_act, 0, 0); if (ted_field(q_ed(), fx + 76, fieldY, fw - 160, 88, F_BODY + 2, 500, TXT, T("Titles, people, genres"), WHITE_A(105), Q.editing, 0)) field_act(0, 0);
  if (Q.q[0]) { icon_draw("ph_x", fx + fw - 62, cy - 20, 40, WHITE_A(160)); hit_act(fx + fw - 84, fieldY, 84, 88, clear_act, 0, 0); }
}
static int q_key(int k) {
  if (Q.editing && ted_key(q_ed(), k)) return 1;
  if (Q.editing && k == K_BACK) { if (Q.q[0]) { ted_backspace(q_ed()); Q.typedAt = app_now(); Q.asked = 0; Q.sugAsked = 0; } else { Q.editing = 0; phone_keyboard(0); } return 1; }
  if (Q.editing && k == K_OK) { Q.editing = 0; phone_keyboard(0); Q.asked = 1; q_ask(); return 1; }
  if (k == K_OK && Q.fr >= 0 && Q.fr < Q.nd && Q.fi < Q.res[Q.fr].n) { app_open_item(&Q.res[Q.fr].items[Q.fi]); return 1; }
  if (k == K_CH_UP) { scroll_top(&Q.v); Q.editing = 1; phone_keyboard(1); return 1; }
  return 0; }
static const Item *q_focused(void) { return Q.fr >= 0 && Q.fr < Q.nd && Q.fi < Q.res[Q.fr].n ? &Q.res[Q.fr].items[Q.fi] : NULL; }
Screen phSearch = { q_enter, q_leave, q_key, q_tick, q_draw, q_focused, NULL, q_text, NULL };

/* ---- sign-in ---- */
static struct { char email[128], pass[128]; int field, busy, errF; double errAt; char err[200]; } G; /* errF: where the error shows: 0 under Email, 1 under Password, 2 under Continue */
static void on_lib(int changed, void *ud) { (void)changed; (void)ud; app_refresh_boards(); }
static void on_addons(int changed, void *ud) { (void)changed; (void)ud; }
static void g_error(int f, const char *msg) { G.errF = f; G.errAt = app_now(); snprintf(G.err, sizeof G.err, "%s", msg); pplat_haptic_k(HK_THUD, 1); }
static int has(const char *s, const char *w) { for (; s && *s; s++) if (!strncasecmp(s, w, strlen(w))) return 1; return 0; }
static void on_login(int ok, const char *err, void *ud) { (void)ud; G.busy = 0;
  if (!ok) { if (has(err, "No connection")) g_error(2, T("No connection. Check your internet and try again."));
    else if (has(err, "pass")) g_error(1, T("Wrong password"));
    else if (has(err, "user") || has(err, "email") || has(err, "found")) g_error(0, T("No account with this email"));
    else g_error(2, err && *err ? err : T("Could not sign in. Try again.")); return; }
  phone_keyboard(0); if (!store_node("lib") || !cJSON_GetArraySize(store_node("lib"))) { store_set("accent", "logo"); accent_apply(); } /* an account signed in: its colours from each title's logo, to begin with */ sb_sync_addons(on_addons, NULL); sb_pull_library(on_lib, NULL); app_go(SCR_HOME, NULL, 1); }
static void go_act(int a, int b) { (void)b; if (a == 1) { phone_keyboard(0); app_go(SCR_HOME, NULL, 1); return; } if (G.busy) return; G.err[0] = 0;
  { const char *at = strchr(G.email, '@'); if (!G.email[0]) { g_error(0, T("Enter your email")); return; } if (!at || at == G.email || !strchr(at, '.')) { g_error(0, T("That email doesn\u2019t look right")); return; } }
  if (!G.pass[0]) { g_error(1, T("Enter your password")); return; } G.busy = 1; phone_keyboard(0); sb_login(G.email, G.pass, on_login, NULL); }
static TEd gE, gP; static TEd *g_ed(void) { if (!gE.s) { gE.s = G.email; gE.cap = sizeof G.email; gE.lower = 1; gP.s = G.pass; gP.cap = sizeof G.pass; } return G.field ? &gP : &gE; }
static void field2_act(int a, int b) { (void)b; G.field = a; phone_keyboard(1); }
static void g_text(const char *t) { char *f = G.field ? G.pass : G.email; if (G.err[0] && (G.errF == G.field || G.errF == 2)) G.err[0] = 0; /* typing again: it goes */ (void)f; ted_insert(g_ed(), t); }
static Orbs gOrbs; static double gT0;
static void g_enter(const void *p) { (void)p; bg_clear(); if (!G.email[0]) { const char *em = js_str(store_node("user"), "email", ""); if (*em) { snprintf(G.email, sizeof G.email, "%s", em); G.field = 1; } } /* (signed in before: its email already there) */ G.busy = 0; memset(&gOrbs, 0, sizeof gOrbs); orbs_place(&gOrbs, 0, 2.2f); gT0 = app_now(); }
static void link_act(int a, int b) { (void)b; phone_keyboard(0); pplat_open_url(a ? "https://www.strem.io/reset-password/" : "https://web.stremio.com/#/intro?form=sign-up"); }
void ph_liquid_bubble(float x, float y, float w, float h, float r, float press); void ph_glass_grab_liquid(void);
/* a field as a line: its words over a hairline that turns the accent while typing in it */
/* an error under its place: a red dot and a short line, coming in; returns the room it takes */
static float g_err_line(float x, float y, float w) { float k = ease_apply(EASE_OUT, (float)(app_now() - G.errAt) / 0.3f);
  gfx_opacity_push(k); gfx_offset_push(0, -8 * (1 - k)); uint32_t c = RGB(0xff, 0x8a, 0x80); float lh = text_line_height(F_SMALL);
  gfx_circle(x + 6, y + lh / 2, 6, c); int n = text_draw_wrapped(G.err, x + 24, y, w - 24, 2, F_SMALL, 600, c, 0, ALIGN_START, lh + 4);
  gfx_offset_pop(); gfx_opacity_pop(); return n * (lh + 4) + 14; }
static void g_field(float x, float y, float w, const char *value, const char *hint, int focused, int secret, int idx) {
  int bad = G.err[0] && G.errF == idx; float st = (float)(app_now() - G.errAt), sh = bad && st < 0.45f ? sinf(st * 46) * 12 * (1 - st / 0.45f) : 0; /* a little shake where it went wrong */
  gfx_offset_push(sh, 0);
  char shown[400]; if (secret) { int n = 0; for (const char *p = value; *p && n < 80; p++) if ((*p & 0xc0) != 0x80) { memcpy(shown + n * 3, "\xe2\x80\xa2", 3); n++; } shown[n * 3] = 0; } else snprintf(shown, sizeof shown, "%s", value);
  float h = 84, ty = y + h / 2 - text_line_height(F_BODY + 2) / 2.0f, tw = 0;
  (void)shown; (void)ty; (void)tw; g_ed(); if (ted_field(idx ? &gP : &gE, x, y, w, h, F_BODY + 2, 500, TXT, hint, WHITE_A(110), focused, secret)) field2_act(idx, 0);
  gfx_rect(x, y + h - 2, w, 2, WHITE_A(40)); if (bad) gfx_rect(x, y + h - 2, w, 2, RGB(0xff, 0x8a, 0x80)); else if (focused) gfx_rect(x, y + h - 2, w, 2, 0xffb794ffu);
  gfx_offset_pop(); }
/* words you can tap, quiet */
static float g_link(float x, float y, const char *label, int px, int align, HitFn act, int a) {
  float w = text_width(label, px, 600, 0), h = 60, x0 = align == ALIGN_END ? x - w : x; int pr = press_k(x0 - 12, y, w + 24, h) > 0.3f;
  text_draw(label, x0, y + h / 2 - text_line_height(px) / 2.0f, px, 600, WHITE_A(pr ? 255 : 175), 0, 0, ALIGN_LEFT); hit_act(x0 - 12, y, w + 24, h, act, a, 0); return w; }
static void kb_off_act(int a, int b) { (void)a; (void)b; phone_keyboard(0); }
static void g_draw(void) {
  float t = (float)(app_now() - gT0), in = ease_apply(EASE_OUT, t / 0.7f);
  gfx_rect(0, 0, SB_W, SB_H, PAGE);
  { orbs_step(&gOrbs, app_dt(), 0, 1, 2.2f); Orbs w = gOrbs; for (int i = 0; i < 2; i++) { float ph = i * 2.1f; w.p[i][0] += sinf(t * 0.55f + ph) * SB_W * 0.07f; w.p[i][1] += cosf(t * 0.43f + ph * 1.3f) * SB_H * 0.05f; } /* the start screen's glows, drifting on */
    uint32_t sv[4] = { ACC, ACC2, ACC3, ACC_D }; ACC = 0xff8c5cffu; ACC2 = 0xffb794ffu; ACC3 = 0xffc9b8ffu; ACC_D = 0xff5b34e6u; orbs_draw(&w, 1); ACC = sv[0]; ACC2 = sv[1]; ACC3 = sv[2]; ACC_D = sv[3]; }
  ph_glass_grab_liquid(); /* the glows under the mark's glass */
  int typing = SDL_IsTextInputActive(); float X = PAD + 8, W = SB_W - 2 * X;
  if (typing) hit_act(0, 0, SB_W, SB_H, kb_off_act, 0, 0); /* a tap outside the fields: the keyboard goes */
  float kb = clampf(KB_H / (SB_H * 0.3f), 0, 1), y = INS_T + SB_H * 0.1f * (1 - kb) + 24 * kb; /* the keyboard up: the mark goes to the top, still there */
  gfx_opacity_push(in);
  /* the mark: the play sign and the name on a bubble of the bar's liquid glass, hugging them */
  { float h = 92, ic = 52, tw = text_width("ORIGIN", 30, 800, 12), w = 30 + ic + 18 + tw + 30 - 12, x = X - 6; gfx_offset_push(0, 16 * (1 - in));
    ph_liquid_bubble(x, y, w, h, h / 2, 0);
    gfx_rrect_v(x + 30, y + (h - ic) / 2, ic, ic, ic / 2, RGB(0x6a, 0x45, 0xe0), RGB(0x2f, 0x1a, 0x9e)); { float S = ic * 512 / 264.0f, cx = x + 30 + ic / 2, cy = y + h / 2; icon_draw("o_play", cx - S * 256 / 512, cy - S * 226 / 512, S, WHITE_A(255)); }
    text_draw("ORIGIN", x + 30 + ic + 18, y + h / 2 - text_line_height(30) / 2.0f, 30, 800, WHITE_A(250), 12, 0, ALIGN_LEFT); gfx_offset_pop(); y += h + SB_H * 0.07f * (1 - kb) + 36 * kb;
    if (KB_H > 0) { float need = 96 + 84 + 108 + 128 + 84 + (G.err[0] ? 60 : 0), over = y + need - (SB_H - KB_H - 24); if (over > 0) y -= over; } } /* the rest above the keyboard (under the mark if it must) */
  { float k = ease_apply(EASE_OUT, (t - 0.08f) / 0.7f); gfx_offset_push(0, 22 * (1 - k));
    text_draw(T("Sign in"), X, y, 76, 800, WHITE_A(255), -2.2f, W, ALIGN_START); y += 96;
    text_draw(T("Use your Stremio account"), X, y, F_BODY, 500, TXT2, 0, W, ALIGN_START); y += 84; gfx_offset_pop(); }
  { float k = ease_apply(EASE_OUT, (t - 0.16f) / 0.7f); gfx_offset_push(0, 22 * (1 - k));
    static float rm[2] = { 24, 44 }; float er = 12 + text_line_height(F_SMALL) + 4 + 14; /* the room under a field opens for its error, smoothly */
    for (int i = 0; i < 2; i++) { float tg = G.err[0] && G.errF == i ? er + (i ? 20 : 0) : (i ? 44 : 24); rm[i] += (tg - rm[i]) * clampf(phDt * 16, 0, 1); }
    g_field(X, y, W, G.email, T("Email"), typing && G.field == 0, 0, 0); y += 84; if (G.err[0] && G.errF == 0) g_err_line(X, y + 12, W); y += rm[0];
    g_field(X, y, W, G.pass, T("Password"), typing && G.field == 1, 1, 1); y += 84; if (G.err[0] && G.errF == 1) g_err_line(X, y + 12, W); y += rm[1];
    /* Sign in: a pill hugging its words, at the end of the line; Forgot password at its start */
    { int ok = G.email[0] && G.pass[0]; const char *lb = G.busy ? T("Signing in…") : T("Continue"); float h = 84, tw = text_width(lb, F_BODY + 1, 750, 0), w = 40 + tw + 14 + 30 + 34, x = X + W - w; float pk = press_k(x, y, w, h);
      gfx_scale_push(1 - 0.04f * pk, 1 - 0.04f * pk, x + w / 2, y + h / 2);
      ph_liquid_bubble(x, y, w, h, h / 2, pk); if (ok) gfx_rrect(x, y, w, h, h / 2, col_alpha(0xff7a4cf5u, 0.5f)); /* the bar's glass; lit, the accent inside it, as the bar's lit pills */
      text_draw(lb, x + 40, y + h / 2 - text_line_height(F_BODY + 1) / 2.0f, F_BODY + 1, 750, WHITE_A(ok ? 255 : 140), 0, 0, ALIGN_LEFT);
      if (G.busy) spinner(x + w - 34 - 15, y + h / 2, 13); else icon_draw("ph_chev", x + w - 34 - 30, y + h / 2 - 15, 30, WHITE_A(ok ? 255 : 140));
      gfx_scale_pop(); hit_act(x, y, w, h, go_act, 0, 0);
      g_link(X, y + 12, T("Forgot password?"), F_SMALL, ALIGN_START, link_act, 1);
      if (G.err[0] && G.errF == 2) g_err_line(X, y + h + 24, W); }
    gfx_offset_pop(); }
  if (!typing) { float ly = SB_H - INS_B - 60 - 36; gfx_rect(X, ly - 22, W, 1, WHITE_A(22));
    g_link(X, ly, T("Create account"), F_SMALL, ALIGN_START, link_act, 0); g_link(X + W, ly, T("Not now"), F_SMALL, ALIGN_END, go_act, 1); }
  gfx_opacity_pop();
}
static int g_key(int k) { char *f = G.field ? G.pass : G.email;
  if (SDL_IsTextInputActive() && ted_key(g_ed(), k)) return 1;
  if (k == K_BACK) { if (SDL_IsTextInputActive()) { if (f[0]) ted_backspace(g_ed()); else phone_keyboard(0); return 1; } app_quit(); return 1; }
  if (k == K_OK) { if (SDL_IsTextInputActive() && !G.field) { G.field = 1; return 1; } go_act(0, 0); return 1; } return 0; }
Screen phLogin = { g_enter, NULL, g_key, NULL, g_draw, NULL, NULL, g_text, NULL };

/* ---- Search as a layer: it fades in over the page you are on, the keyboard comes up with it, Back fades it away ---- */
static Tween sA; static int sOpen;
static int so_key(int k) {
  if (k == K_BACK) { sOpen = 0; Q.editing = 0; phone_keyboard(0); tween_to(&sA, 0, 0.2f, EASE_CSS_EASE); return 1; }
  if (k == K_OK && !Q.editing && Q.fr >= 0 && Q.fr < Q.nd && Q.fi < Q.res[Q.fr].n) { Item it = Q.res[Q.fr].items[Q.fi]; sOpen = 0; phone_keyboard(0); tween_set(&sA, 0); app_overlay = NULL; app_open_item(&it); return 1; }
  return q_key(k); }
static void so_tick(float dt) { tween_tick(&sA, dt); q_tick(dt); if (!sOpen && sA.v <= 0.001f) { extern Screen searchLayer; if (app_overlay == &searchLayer) app_overlay = NULL; } }
static void so_draw(void) {
  float a = sA.v; if (a <= 0.001f) return; hit_shield(0, 0, SB_W, SB_H);
  gfx_rect(0, 0, SB_W, SB_H, col_alpha(PAGE | 0xff000000u, 0.9f * a));
  gfx_opacity_push(a); float dy = 30 * (1 - ease_apply(EASE_OUT, a)); gfx_offset_push(0, dy); q_draw(); gfx_offset_pop(); gfx_opacity_pop();
}
static const Item *so_focused(void) { return q_focused(); }
/* a result tapped (or OK on it): search closes and the title opens, the keyboard up or not */
static void so_open(const Item *it) { Item c = *it; sOpen = 0; Q.editing = 0; phone_keyboard(0); tween_set(&sA, 0); app_overlay = NULL; app_open_item(&c); }
Screen searchLayer = { NULL, NULL, so_key, so_tick, so_draw, so_focused, so_open, q_text, NULL, NULL };
static void so_kb_gone(void) { Q.editing = 0; }
void ph_search_open(void) { extern void (*phKeyboardGone)(void); phKeyboardGone = so_kb_gone; if (app_overlay && app_overlay != &searchLayer) return; app_overlay = &searchLayer; sOpen = 1; tween_to(&sA, 1, 0.24f, EASE_WEB); Q.fr = -1; Q.editing = 1; phone_keyboard(1); }
int ph_search_on(void) { return sOpen; }
static void so_key_back(void);
void ph_search_close(void) { if (sOpen) so_key_back(); } /* (a place in the bar tapped: search goes, the page shows) */
static void so_key_back(void) { sOpen = 0; Q.editing = 0; phone_keyboard(0); tween_to(&sA, 0, 0.2f, EASE_CSS_EASE); }
/* the keyboard's delete key: the field being typed in (the search layer, or the sign-in page) */
void phone_backspace(void) {
  if (sOpen) { if (Q.q[0]) { ted_backspace(q_ed()); Q.typedAt = app_now(); Q.asked = 0; Q.sugAsked = 0; } return; }
  if (SCR == SCR_LOGIN) ted_backspace(g_ed());
}
