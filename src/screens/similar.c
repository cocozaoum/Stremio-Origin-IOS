/* Similar: a grid of titles of the same genre. Also a person's films and series (OK on a face on a title page),
   the most notable first, from Wikidata (cast, director, writer, with their IMDb ids; ranked by how many wikis
   cover each). */
#include "screens/similar.h"
#ifdef SB_PHONE
#pragma GCC diagnostic ignored "-Wunused-function" /* the phone draws this screen its own way (below): the TV's drawing and keys go unused */
#endif
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "core/stremio.h"
#include "screens/player.h"
#include "screens/sheet.h"
#include <stdio.h>
#include <string.h>
#include "core/hit.h"
#include "core/net.h"
#include "core/coll.h"

#define COLS 6
static struct { DetailParams p; ItemList items; int idx; char title[300], sub[200]; int loading, empty, person, coll, pendTmdb; double pendAt; unsigned collVer; char from[16], ckey[24]; double loadT0; struct { float x, y; int shown; Timer1 pop; } ring; Tween gridY; Timer1 fade; Nudge nudge; unsigned gen; } G;
#define MAXPT 48
typedef struct { char id[16], type[8], name[128]; } PT;
typedef struct { char who[16]; int n; PT t[MAXPT]; } PersonMem;
static PersonMem pmem[8]; static int pmemN;
static void person_show(const PersonMem *m) {
  itemlist_clear(&G.items); G.items.items = calloc(MAXPT, sizeof(Item)); G.items.n = 0;
  for (int i = 0; i < m->n; i++) { Item *it = &G.items.items[G.items.n++]; snprintf(it->id, sizeof it->id, "%s", m->t[i].id); snprintf(it->type, sizeof it->type, "%s", m->t[i].type); snprintf(it->name, sizeof it->name, "%s", m->t[i].name);
    snprintf(it->poster, sizeof it->poster, "https://images.metahub.space/poster/small/%s/img", m->t[i].id); }
  G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = 0; G.ring.shown = 0;
}
static void on_person(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) return;
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *b = js_arr(js_obj(j, "results"), "bindings");
  if (!b) { net_json_free(j); G.loading = 0; G.empty = 1; return; }
  PersonMem *m = &pmem[pmemN++ % 8]; memset(m, 0, sizeof *m); snprintf(m->who, sizeof m->who, "%s", G.p.it.id); const cJSON *r;
  cJSON_ArrayForEach(r, b) { if (m->n >= MAXPT) break; const char *tt = js_str(js_obj(r, "imdb"), "value", ""), *nm = js_str(js_obj(r, "lab"), "value", ""); if (!*tt || !*nm) continue;
    PT *t = &m->t[m->n++]; snprintf(t->id, sizeof t->id, "%s", tt); snprintf(t->name, sizeof t->name, "%s", nm); snprintf(t->type, sizeof t->type, "%s", !strcmp(js_str(js_obj(r, "s"), "value", ""), "1") ? "series" : "movie"); }
  net_json_free(j); person_show(m);
}
static void person_load(void) {
  for (int i = 0; i < 8; i++) if (pmem[i].who[0] && !strcmp(pmem[i].who, G.p.it.id)) { person_show(&pmem[i]); return; } /* kept in memory */
  const char *id = G.p.it.id; if (strncmp(id, "nm", 2)) { G.loading = 0; G.empty = 1; return; } for (const char *q = id + 2; *q; q++) if (*q < '0' || *q > '9') { G.loading = 0; G.empty = 1; return; }
  static const char *FILM = "wd:Q11424 wd:Q24869 wd:Q506240 wd:Q93204 wd:Q202866 wd:Q29168811 wd:Q17123180", *SER = "wd:Q5398426 wd:Q1259759 wd:Q526877 wd:Q581714";
  char q[1600]; snprintf(q, sizeof q, "SELECT ?imdb (SAMPLE(?l) AS ?lab) (MIN(?yy) AS ?yr) (MAX(?ser) AS ?s) (MAX(?sl) AS ?pop) WHERE { ?p wdt:P345 \"%s\" . "
    "{ ?f wdt:P161 ?p } UNION { ?f wdt:P57 ?p } UNION { ?f wdt:P58 ?p } ?f wdt:P345 ?imdb . FILTER(STRSTARTS(?imdb, \"tt\")) "
    "{ ?f wdt:P31 ?t . VALUES ?t { %s } BIND(0 AS ?ser) } UNION { ?f wdt:P31 ?t . VALUES ?t { %s } BIND(1 AS ?ser) } "
    "?f wikibase:sitelinks ?sl . OPTIONAL { ?f wdt:P577 ?d . BIND(YEAR(?d) AS ?yy) } OPTIONAL { ?f rdfs:label ?l . FILTER(LANG(?l) = \"en\") } } GROUP BY ?imdb ORDER BY DESC(?pop) LIMIT 60", id, FILM, SER);
  char u[5200] = "https://query.wikidata.org/sparql?format=json&query="; size_t o = strlen(u);
  for (const char *s = q; *s && o + 4 < sizeof u; s++) { unsigned char c = *s; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') u[o++] = c; else o += snprintf(u + o, sizeof u - o, "%%%02X", c); }
  u[o] = 0; net_get(u, on_person, (void *)(uintptr_t)G.gen);
}
static void on_cat(ItemList *l, int cached, void *ud) {
  (void)cached; if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) { itemlist_free(l); return; }
  itemlist_clear(&G.items); G.items.items = calloc(30, sizeof(Item)); G.items.n = 0;
  for (int i = 0; i < l->n && G.items.n < 30; i++) { if (!strcmp(l->items[i].id, G.p.it.id)) continue; G.items.items[G.items.n++] = l->items[i]; }
  itemlist_free(l); G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = 0; G.ring.shown = 0;
}
static void on_meta(cJSON *m, void *ud) {
  if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) return;
  const char *nm = js_str(m, "name", G.p.it.name); if (*nm) snprintf(G.title, sizeof G.title, "%s", T1("Similar to {name}", "name", nm));
  const cJSON *gs = js_arr(m, "_genresEn"); if (!gs || !cJSON_GetArraySize(gs)) gs = js_arr(m, "genres"); /* Cinemeta's English genre, also when the page shows it translated */
  const char *g = gs && cJSON_GetArraySize(gs) && cJSON_IsString(cJSON_GetArrayItem(gs, 0)) ? cJSON_GetArrayItem(gs, 0)->valuestring : "";
  for (const char *q = g; *q; q++) if ((unsigned char)*q >= 0x80) { g = ""; break; } /* a translated name: the top titles instead */
  char u[512], ge[128] = ""; if (*g) { const char *s = g; char *d = ge; while (*s && d < ge + 120) { if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-') *d++ = *s; else { snprintf(d, 4, "%%%02X", (unsigned char)*s); d += 3; } s++; } *d = 0; }
  if (ge[0]) snprintf(u, sizeof u, CINEMETA "/catalog/%s/top/genre=%s.json", G.p.it.type, ge); else snprintf(u, sizeof u, CINEMETA "/catalog/%s/top.json", G.p.it.type);
  sb_catalog_url(u, G.p.it.type, 31, on_cat, (void *)(uintptr_t)G.gen);
}
/* a collection (from a film's page or the Library's Collections): its films in the order they came out, the one it
   was opened from marked, the focus on the first not watched yet, over the collection's own backdrop */
static void coll_show(void) {
  if (!G.coll || G.collVer == coll_version()) return; G.collVer = coll_version(); int was = !G.loading; /* (shown already: its films' ids filled in place, the focus kept) */
  const Coll *c = coll_get(G.ckey); if (!c) { if (app_now() - G.loadT0 > 30) { G.loading = 0; G.empty = 1; } return; }
  itemlist_clear(&G.items); G.items.items = calloc((size_t)(c->n ? c->n : 1), sizeof(Item)); G.items.n = 0; int first = -1;
  for (int i = 0; i < c->n; i++) { Item *it = &G.items.items[G.items.n++]; if (c->part[i].id[0]) snprintf(it->id, sizeof it->id, "%s", c->part[i].id); else snprintf(it->id, sizeof it->id, "tmdb:%d", c->part[i].tmdb); /* (its IMDb id on its way) */ snprintf(it->type, sizeof it->type, "%s", c->part[i].type[0] ? c->part[i].type : "movie"); snprintf(it->name, sizeof it->name, "%s", c->part[i].name); snprintf(it->poster, sizeof it->poster, "%s", c->part[i].poster);
    if (c->part[i].year) snprintf(it->metaLine, sizeof it->metaLine, "%d", c->part[i].year); if (first < 0 && !lib_item_watched(it)) first = i; }
  if (c->name[0]) snprintf(G.title, sizeof G.title, "%s", c->name);
  { char n[16]; snprintf(n, sizeof n, "%d", c->n); int y0 = c->n ? c->part[0].year : 0, y1 = c->n ? c->part[c->n - 1].year : 0; char yr[24] = ""; if (y0 && y1 && y1 != y0) snprintf(yr, sizeof yr, "  ·  %d\xe2\x80\x93%d", y0, y1); else if (y0) snprintf(yr, sizeof yr, "  ·  %d", y0);
    int nf = 0; for (int i = 0; i < c->n; i++) if (strcmp(c->part[i].type, "series")) nf++; /* (films alone: "8 films"; with shows: "6 titles") */
    snprintf(G.sub, sizeof G.sub, "%s%s", nf == c->n ? T1("{n} films", "n", n) : T1("{n} titles", "n", n), yr); }
  if (was) { if (G.idx >= G.items.n) G.idx = G.items.n ? G.items.n - 1 : 0; return; }
  G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = first >= 0 ? first : 0; G.ring.shown = 0;
#ifndef SB_PHONE
  if (c->backdrop[0]) bg_set(c->backdrop, 0, c->poster); /* (a TV: the collection's backdrop over the page; an upright phone would stretch it: its poster in the page's head instead) */
#endif
}
static void enter(const void *pp) {
  if (pp) G.p = *(const DetailParams *)pp; G.gen++; itemlist_clear(&G.items); G.idx = 0; G.loading = 1; G.loadT0 = app_now(); G.empty = 0; G.ring.shown = 0; tween_set(&G.gridY, 0);
  G.person = !strcmp(G.p.it.type, "person"); G.coll = !strcmp(G.p.it.type, "collection"); G.sub[0] = 0;
  if (G.coll) { snprintf(G.ckey, sizeof G.ckey, "%s", G.p.it.id + 5); G.loadT0 = app_now(); G.collVer = 0; snprintf(G.from, sizeof G.from, "%s", G.p.it.videoId); snprintf(G.title, sizeof G.title, "%s", G.p.it.name[0] ? G.p.it.name : T("Collection")); coll_show(); return; }

  if (G.person) { snprintf(G.title, sizeof G.title, "%s", G.p.it.name); person_load(); return; }
  snprintf(G.title, sizeof G.title, "%s", T("Similar"));
  sb_meta(G.p.it.type, G.p.it.id, on_meta, (void *)(uintptr_t)G.gen);
}
static const Item *focused(void) { return G.idx < G.items.n ? &G.items.items[G.idx] : NULL; }
static void open_it(const Item *it) { if (!strncmp(it->id, "tmdb:", 5)) { G.pendTmdb = atoi(it->id + 5); G.pendAt = app_now(); return; } /* (its IMDb id on its way: opened when it comes, see tick) */
  DetailParams p; memset(&p, 0, sizeof p); p.it = *it; app_go(SCR_DETAIL, &p, 0); }
static int key(int k) {
  if (!G.items.n) { if (k == K_UP) topnav_focus(); return k != K_BACK; } int i0 = G.idx;
  if (k == K_RIGHT) G.idx = G.idx + 1 < G.items.n ? G.idx + 1 : G.idx;
  else if (k == K_LEFT) { if (G.idx > 0) G.idx--; }
  else if (k == K_DOWN) G.idx = G.idx + COLS < G.items.n ? G.idx + COLS : (G.items.n - 1 > G.idx && G.idx / COLS < (G.items.n - 1) / COLS ? G.items.n - 1 : G.idx);
  else if (k == K_UP) { if (G.idx - COLS >= 0) G.idx -= COLS; else { topnav_focus(); return 1; } }
  else if (k == K_OK) { open_it(&G.items.items[G.idx]); return 1; }
  else if (k == K_BACK) return 0;
  if (G.idx == i0) nudge_start(&G.nudge, k);
  return 1;
}
#define ROWH (CARD_H + 26.0f)
/* the text art of the titles around the focus, for their pages (drawn there at 520x175): ready when one opens */
static void logos_ahead(void) { static double at0; double now = app_now(); if (now - at0 < 0.5) return; at0 = now;
  for (int i = G.idx - COLS; i <= G.idx + COLS; i++) { if (i < 0 || i >= G.items.n) continue; char lu[512]; sb_logo_of(&G.items.items[i], lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); } }
static void tick(float dt) {
  coll_show(); logos_ahead();
  if (G.pendTmdb) { if (app_now() - G.pendAt > 15) G.pendTmdb = 0; else for (int i = 0; i < G.items.n; i++) { const Coll *c = coll_get(G.ckey); if (!c) break; for (int k = 0; k < c->n; k++) if (c->part[k].tmdb == G.pendTmdb && c->part[k].id[0]) { Item it = G.items.items[i]; for (int q = 0; q < G.items.n; q++) if (!strcmp(G.items.items[q].id, c->part[k].id)) it = G.items.items[q]; G.pendTmdb = 0; open_it(&it); return; } break; } } /* (a film picked before its id came) */
  timer_tick(&G.ring.pop, dt); tween_tick(&G.gridY, dt); timer_tick(&G.fade, dt); nudge_tick(&G.nudge, dt);
  int row = G.idx / COLS; float y = row > 1 ? (row - 1) * ROWH : 0; tween_to(&G.gridY, -y, 0.3f, EASE_OUT);
  /* the ring jumps to the card and lands with a small pop (never glides across the others) */
  if (G.items.n) { int rtl = app_rtl(); float cx = 48 + 14 + (G.idx % COLS) * (CARD_W + CARD_GAP), cy = 16 + row * ROWH; if (rtl) cx = SB_W - cx - CARD_W;
    if (!G.ring.shown || G.ring.x != cx || G.ring.y != cy) { static double last; int pop = !G.ring.shown || focus_pops(&last); G.ring.x = cx; G.ring.y = cy; G.ring.shown = 1; if (pop) timer_start(&G.ring.pop, 0.2f); else G.ring.pop.on = 0; } } /* held: it jumps, lit */
}
static int origin(float *x, float *y) { if (topNav.active || G.idx >= G.items.n) return 0; int row = G.idx / COLS; float cx = 48 + 14 + (G.idx % COLS) * (CARD_W + CARD_GAP); if (app_rtl()) cx = SB_W - cx - CARD_W; *x = cx + CARD_W / 2; *y = 120 + 104 + G.gridY.v + 16 + row * ROWH + CARD_H / 2; return 1; }
static void sim_focus(int i, int b) { (void)b; int was = topNav.active; topNav.active = 0; if (was || G.idx != i) G.idx = i; }
static void draw(void) {
  int rtl = app_rtl();
  text_draw(G.title, rtl ? SB_W - 48 : 48, 120, 44, 800, WHITE_A(255), -0.8f, SB_W - 96, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  if (G.sub[0]) text_draw(G.sub, rtl ? SB_W - 48 : 48, 120 + 58, 24, 600, WHITE_A(180), 0, SB_W - 96, rtl ? ALIGN_RIGHT : ALIGN_LEFT); /* (a collection: how many films, which years) */
  float top = 120 + 104; gfx_clip_push(34, top - 28, SB_W - 34, SB_H - top + 28); /* (from 28 above the grid: the first line's focused card grows 9% and has its outline; cut at the grid's top, its top edge was lost) */
  float fa = G.fade.on ? 1 - powf(1 - timer_x(&G.fade), 2) : 1; gfx_opacity_push(fa);
  gfx_offset_push(nudge_x(&G.nudge), nudge_y(&G.nudge) + top + G.gridY.v);
  if (G.loading) { /* loading: a thin line under the title, a short accent stroke sweeping along it */
    double t = app_now() - G.loadT0; float in = (float)((t - 0.15) / 0.3); in = in < 0 ? 0 : in > 1 ? 1 : in;
    if (in > 0) { float W = 360, x0 = rtl ? SB_W - 48 - 40 - W : 48 + 40, ph = (float)fmod(t, 1.4) / 1.4f, e = ph * ph * (3 - 2 * ph), sw = W * 0.32f, sx = x0 - sw + (W + sw) * e;
      gfx_opacity_push(in); gfx_rrect(x0, 52, W, 3, 1.5f, WHITE_A(22));
      float l = sx < x0 ? x0 : sx, r = sx + sw > x0 + W ? x0 + W : sx + sw; if (r - l > 1) gfx_rrect(l, 52, r - l, 3, 1.5f, ACC); gfx_opacity_pop(); } } /* the stroke enters and leaves at the line's ends */
  if (G.empty && !G.loading) text_draw(T("Nothing found."), rtl ? SB_W - 48 - 40 : 48 + 40, 40, 27, 400, WHITE_A(191), 0, 0,
 rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  /* the cards; the focused one last, over its neighbours as it grows */
  int fi = !topNav.active && G.idx < G.items.n ? G.idx : -1; float hp = app_hold_progress();
  for (int pass = 0; pass < 2; pass++) for (int i = 0; i < G.items.n; i++) { if ((i == fi) != pass) continue;
    int row = i / COLS; float y = 16 + row * ROWH; if (y + G.gridY.v > SB_H - top || y + CARD_H + G.gridY.v < 0) continue;
    float x = 48 + 14 + (i % COLS) * (CARD_W + CARD_GAP); if (rtl) x = SB_W - x - CARD_W; unsigned long long lk = lift_key(&G, i);
    lift_begin(lk, pass, pass ? hp : 0, x, y, CARD_W, CARD_H, CARD_R);
    card_draw(&G.items.items[i], x, y, 0, 1, abs(row - G.idx / COLS) <= 2);
    if (G.coll && G.from[0] && !strcmp(G.items.items[i].id, G.from)) { /* the one it was opened from: a quiet glass tag in its corner (the app's pill: dark glass, a fine edge, an accent dot) */
      const char *tf = T(strcmp(G.items.items[i].type, "series") ? "This film" : "This show"); float fs = 18, tw = text_width(tf, fs, 700, 0.2f), h = 34, w = 14 + 8 + 9 + tw + 14, px = rtl ? x + CARD_W - 12 - w : x + 12, py = y + 12;
      gfx_shadow(px, py + 4, w, h, h / 2, 14, BLACK_A(90)); gfx_rrect(px, py, w, h, h / 2, BLACK_A(165)); gfx_ring(px, py, w, h, h / 2, 1.2f, WHITE_A(44));
      gfx_circle(px + 14 + 4, py + h / 2, 4, ACC2 | 0xff000000u); text_draw(tf, px + 14 + 8 + 9, py + (h - text_line_height(fs)) / 2, fs, 700, WHITE_A(240), 0.2f, 0, ALIGN_LEFT); }
    if (pass && hp > 0) hold_trace_draw(x, y, CARD_W, CARD_H, CARD_R, ease_apply(EASE_IN_OUT, hp));
    lift_end(lk, x, y, CARD_W, CARD_H, CARD_R); hit_add(x, y, CARD_W, CARD_H, sim_focus, NULL, i, 0); }
  gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
}
#ifdef SB_PHONE
/* the phone: the same titles (like this one, or a person's films and series) as a grid under the finger */
#include "phone/phone.h"
static Scroll pv; static int pfi = -1;
static void p_focus(int a, int i) { (void)a; pfi = i; if (i >= 0 && i < G.items.n) accent_tint_fetch(G.items.items[i].id, G.items.items[i].poster, G.items.items[i].logo); }
/* a collection, as the streaming apps show one: its backdrop across the top (cropped, never stretched, fading into the
   page, slower than the scroll), its poster over the backdrop's foot with its name, how many titles and years, how many
   watched; Start (or Continue: the first not watched yet); its story; then its titles in order, one row each: the
   poster, "Part n", the name and year, a check once watched, the one it was opened from softly lit */
static int cOver; static void c_part(int i, int b) { (void)b; if (i >= 0 && i < G.items.n) open_it(&G.items.items[i]); }
static void c_focus(int a, int i) { (void)a; pfi = i; } /* (a row under the finger: a hold opens its menu, as a card's) */
/* play a title of it as its own page does: a film from where it stopped (its stream kept, else the best one, else its
   streams), from the start when it was never begun; a show (a franchise's): its page, its episodes there */
static void c_play(int i, int b) { (void)b; if (i < 0 || i >= G.items.n) return; Item it = G.items.items[i]; if (!strncmp(it.id, "tmdb:", 5) || !strcmp(it.type, "series")) { open_it(&it); return; }
  extern int best_on(void); CwInfo ci; memset(&ci, 0, sizeof ci); int part = lib_cw_info(&it, &ci) && ci.pct > 0.01f && ci.pct < 0.97f; Stream st;
  if (part && sb_resume_stream(it.id, it.id, &st)) { app_play(&st, it.type, it.id, it.id, 0); return; }
  if (best_on()) { player_choose_best(it.type, it.id, it.id); return; } ph_streams_show(&it, it.id, NULL); }
static void c_seen(int i, int b) { (void)b; if (i < 0 || i >= G.items.n || !strncmp(G.items.items[i].id, "tmdb:", 5)) return; const Item *it = &G.items.items[i]; const cJSON *m = sb_meta_cached(it->type, it->id);
  lib_toggle_watched(it, js_arr(m, "videos")); pplat_haptic_k(HK_TICK, 0.6f); toast(lib_item_watched(it) ? T("Marked as watched") : T("Marked as unwatched"), 1400, NULL); } /* (a tap on its check: watched or not, at a glance) */
static void c_all_lib(int a, int b) { (void)a; (void)b; int all = 1; for (int i = 0; i < G.items.n; i++) if (strncmp(G.items.items[i].id, "tmdb:", 5) && !lib_has(G.items.items[i].id)) all = 0;
  for (int i = 0; i < G.items.n; i++) { const Item *it = &G.items.items[i]; if (strncmp(it->id, "tmdb:", 5) && lib_has(it->id) == all) lib_toggle(it); } toast(all ? T("Removed from Library") : T("Added to Library"), 1400, NULL); pplat_haptic_k(HK_TICK, 0.6f); }
static void c_all_seen(int a, int b) { (void)a; (void)b; int all = 1; for (int i = 0; i < G.items.n; i++) if (strncmp(G.items.items[i].id, "tmdb:", 5) && !lib_item_watched(&G.items.items[i])) all = 0;
  for (int i = 0; i < G.items.n; i++) { const Item *it = &G.items.items[i]; if (strncmp(it->id, "tmdb:", 5) || 1) { if (!strncmp(it->id, "tmdb:", 5)) continue; if (lib_item_watched(it) == all) { const cJSON *m = sb_meta_cached(it->type, it->id); lib_toggle_watched(it, js_arr(m, "videos")); } } }
  toast(all ? T("Marked as unwatched") : T("Marked as watched"), 1400, NULL); pplat_haptic_k(HK_TICK, 0.6f); }
static void c_more(int a, int b) { (void)a; (void)b; cOver = !cOver; }
/* downloads: a film of it, or all its films not kept yet; in Downloads they show as one, as a season does */
static void c_group(char *g, size_t cap) { const Coll *c = coll_get(G.ckey); ph_coll_group(G.ckey, c && c->name[0] ? c->name : G.title, g, cap); }
static int c_film(const Item *it) { return strncmp(it->id, "tmdb:", 5) && strcmp(it->type, "series"); } /* (a film with its id: a show's episodes are picked on its own sheet) */
static void c_dl(int i, int b) { (void)b; if (i < 0 || i >= G.items.n) return; Item it = G.items.items[i]; if (!strncmp(it.id, "tmdb:", 5)) return;
  if (!strcmp(it.type, "series")) { ph_episodes_download(&it, NULL); return; }
  char g[160]; c_group(g, sizeof g); if (ph_dl_auto()) ph_download_items(&it, 1, g); else { ph_dl_group_next(it.id, g); ph_streams_download(&it, it.id, NULL); } pplat_haptic_k(HK_TICK, 0.6f); }
static void c_dl_all(int a, int b) { (void)a; (void)b; Item *v = calloc((size_t)(G.items.n ? G.items.n : 1), sizeof(Item)); int n = 0;
  for (int i = 0; i < G.items.n; i++) { const Item *it = &G.items.items[i]; int st = ph_dl_state(it->id, it->id, NULL); if (c_film(it) && (st < 0 || st == DL_FAILED || st == DL_NOSTREAM)) v[n++] = *it; }
  if (!n) { toast(T("Already downloaded"), 1600, NULL); free(v); return; }
  char g[160]; c_group(g, sizeof g); if (n > 1 && !ph_dl_auto()) toast(T("Best stream for each: download one at a time to choose"), 3200, NULL); ph_download_items(v, n, g); free(v); pplat_haptic_k(HK_TICK, 0.6f); }
static void p_coll(void) {
  const Coll *c = coll_get(G.ckey); ph_drag(0, 0, SB_W, SB_H, &pv, 1); scroll_tick(&pv);
  float y0 = -pv.pos, hh = SB_W * 9 / 16 + INS_T + 20, py = y0 * 0.5f, in = G.fade.on ? ease_apply(EASE_OUT, timer_x(&G.fade)) : 1; if (hh > SB_H * 0.62f) hh = SB_H * 0.62f; if (py > 0) py = 0;
  /* as a title's page: the cover across the top (slower than the scroll, fading into the page), the name over its foot, the facts, one row of round pills */
  { GLuint t = c && c->backdrop[0] ? img_get_shaped(c->backdrop, (int)SB_W, (int)hh, 0, 1) : 0; if (!t || !ph_picture(t, 0, py, SB_W, hh, 0, WHITE_A(255))) ph_box(0, py, SB_W, hh, 0);
    gfx_rrect_v(0, py + hh * 0.3f, SB_W, hh * 0.7f + 2, 0, col_alpha(PAGE | 0xff000000u, 0), PAGE | 0xff000000u); gfx_rrect_v(0, py, SB_W, INS_T + 100, 0, BLACK_A(120), BLACK_A(0)); }
  int seen = 0, first = -1; for (int i = 0; i < G.items.n; i++) { if (lib_item_watched(&G.items.items[i])) seen++; else if (first < 0) first = i; }
  float y = y0 + hh - 70, tw = SB_W - 2 * PAD;
  { int nl = text_wrap_count(G.title, tw, 2, F_H1, 800, -0.6f); float lh = 54; text_draw_wrapped(G.title, PAD, y + 2 * lh - nl * lh - 10, tw, 2, F_H1, 800, TXT, -0.6f, ALIGN_START, lh); y += 2 * lh + 6; }
  if (G.sub[0]) { text_draw(G.sub, PAD, y, F_SMALL + 1, 600, WHITE_A(215), 0, tw, ALIGN_START); y += 36; }
  { static Tween seenA; tween_to(&seenA, seen ? 1 : 0, 0.25f, EASE_WEB); tween_tick(&seenA, phDt); /* its place kept, watched or not: the page never moves as one is marked (it fades in and out where it is) */
    if (seenA.v > 0.01f) { char a2[12], b2[12]; snprintf(a2, sizeof a2, "%d", seen); snprintf(b2, sizeof b2, "%d", G.items.n); float is = F_SMALL + 4; gfx_opacity_push(seenA.v); icon_draw("check", PAD, y + (36 - is) / 2 - 4, is, ACC2 | 0xff000000u);
      text_draw(T2("{n} of {m} watched", "n", a2, "m", b2), PAD + is + 10, y, F_SMALL, 600, ACC2 | 0xff000000u, 0, tw - is - 10, ALIGN_START); gfx_opacity_pop(); }
    if (G.items.n) y += 40; }
  y += 10;
  if (G.items.n) { int st = -1, resume = 0; char lb[120], pn[12]; float rp = 0; /* the title in progress first (Resume, its time left), else the first not watched, else the first */
    for (int i = 0; i < G.items.n && st < 0; i++) { CwInfo ci; memset(&ci, 0, sizeof ci); if (lib_cw_info(&G.items.items[i], &ci) && ci.pct > 0.01f && ci.pct < 0.97f) { st = i; resume = 1; rp = ci.pct; } }
    if (st < 0) st = first < 0 ? 0 : first; snprintf(pn, sizeof pn, "%d", st + 1);
    if (resume) snprintf(lb, sizeof lb, "%s  \xc2\xb7  %s", T("Resume"), T1("Part {n}", "n", pn)); else snprintf(lb, sizeof lb, "%s", seen && first >= 0 ? T1("Continue \xc2\xb7 Part {n}", "n", pn) : st ? T1("Play \xc2\xb7 Part {n}", "n", pn) : T("Start"));
    int allLib = 1, allSeen = 1, allDl = 1, anyF = 0; for (int i = 0; i < G.items.n; i++) { const Item *it = &G.items.items[i]; if (!strncmp(it->id, "tmdb:", 5)) continue; if (!lib_has(it->id)) allLib = 0; if (!lib_item_watched(it)) allSeen = 0; if (c_film(it)) { int ds = ph_dl_state(it->id, it->id, NULL); anyF = 1; if (ds < 0 || ds == DL_FAILED || ds == DL_NOSTREAM) allDl = 0; } } if (!anyF) allDl = 0; /* (every film downloaded or on its way: the download pill lit) */
    static Tween onT[3]; static const void *og; if (og != c) { og = c; tween_set(&onT[0], allLib); tween_set(&onT[1], allSeen); tween_set(&onT[2], allDl); } /* (opened: as they are) */
    float h = 76, gap = 12, x = PAD;
    for (int q = 0; q < 4; q++) { int main = q == 0; float w = main ? text_width(lb, F_SMALL + 1, 800, 0) + 40 + 14 + 48 : h; if (main && w > SB_W - 2 * PAD - 3 * (h + gap)) w = SB_W - 2 * PAD - 3 * (h + gap);
      float pk = press_k(x, y, w, h), ok = 0; gfx_scale_push(1 - 0.05f * pk, 1 - 0.05f * pk, x + w / 2, y + h / 2);
      if (!main) { tween_to(&onT[q - 1], q == 1 ? allLib : q == 2 ? allSeen : allDl, 0.38f, EASE_OUT); tween_tick(&onT[q - 1], phDt); ok = onT[q - 1].v; }
      if (main) gfx_rrect(x, y, w, h, h / 2, WHITE_A(245)); else { gfx_rrect(x, y, w, h, h / 2, WHITE_A(pk > 0 ? (int)(18 + 22 * pk) : 14)); if (ok > 0.01f) gfx_rrect(x, y, w, h, h / 2, col_alpha(ACC, 0.36f * ok)); gfx_ring(x, y, w, h, h / 2, 1, WHITE_A((int)(26 * (1 - ok)))); }
      uint32_t cc = main ? RGB(14, 12, 20) : col_mix(WHITE_A(235), ACC3 | 0xff000000u, ok);
      if (main) { icon_draw("play", x + 24, y + h / 2 - 20, 40, cc); text_draw(lb, x + 24 + 40 + 12, y + h / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, cc, 0, w - 24 - 52 - 24, ALIGN_LEFT);
        if (resume) { float bw = (w - 48) * 0.5f, bx = x + w / 2 - bw / 2; gfx_rrect(bx, y + h - 12, bw, 4, 2, BLACK_A(40)); gfx_rrect(bx, y + h - 12, bw * rp, 4, 2, ACC); } } /* how far, inside the pill */
      else { const char *offI = q == 1 ? "ph_plus" : q == 2 ? "eye" : "ph_down", *onI = q == 1 ? "ph_lib" : q == 2 ? "check" : "ph_check"; /* (in the Library: the bar's sign, never the tick Watched has) */ float icx = x + w / 2 - 20, icy = y + h / 2 - 20, pop = 1 + 0.22f * sinf(ok * (float)M_PI) * (ok > 0.02f && ok < 0.98f); gfx_scale_push(pop, pop, icx + 20, icy + 20);
        if (ok < 0.99f) icon_draw_rot(offI, icx, icy, 40, col_alpha(cc, 1 - ok), 90 * ok); if (ok > 0.01f) icon_draw_rot(onI, icx, icy, 40, col_alpha(cc, ok), -90 * (1 - ok)); gfx_scale_pop(); }
      gfx_scale_pop(); if (main) hit_act(x, y, w, h, c_play, st, 0); else hit_act(x, y, w, h, q == 1 ? c_all_lib : q == 2 ? c_all_seen : c_dl_all, 0, 0); x += w + gap; } /* (the whole collection: to the Library, watched) */
    y += h + 28; }
  if (c && c->overview[0]) { int lines = cOver ? 30 : 3, nl = text_wrap_count(c->overview, SB_W - 2 * PAD, lines, F_BODY - 2, 500, 0); float lh = text_line_height(F_BODY - 2) + 4;
    text_draw_wrapped(c->overview, PAD, y, SB_W - 2 * PAD, lines, F_BODY - 2, 500, TXT2, 0, ALIGN_START, lh);
    if (!cOver && text_wrap_count(c->overview, SB_W - 2 * PAD, 30, F_BODY - 2, 500, 0) > lines) { float fw = (SB_W - 2 * PAD) * 0.35f, fy = y + (nl - 1) * lh; gfx_rrect_h(SB_W - PAD - fw, fy - 2, fw + 4, lh + 4, 0, col_alpha(PAGE | 0xff000000u, 0), PAGE | 0xff000000u); } /* (more than it shows: its last line fades out, a tap opens the rest) */
    hit_act(0, y - 6, SB_W, nl * lh + 12, c_more, 0, 0); y += nl * lh + 28; }
  text_draw(G.sub[0] && strstr(G.sub, T1("{n} films", "n", "")) ? T("Films") : T("Titles"), PAD, y, F_H2 - 4, 800, TXT, -0.3f, 0, ALIGN_START); y += 60;
  if (!G.items.n) { for (int i = 0; i < 4; i++) { ph_box(PAD, y, 96, 144, 12); y += 144 + 18; } }
  gfx_opacity_push(in);
  for (int i = 0; i < G.items.n; i++) { const Item *it = &G.items.items[i]; float rh = 144, rw = SB_W - 2 * PAD; if (y > SB_H || y + rh < 0) { y += rh + 18; continue; }
    int here = G.from[0] && !strcmp(G.from, it->id), done = lib_item_watched(it), pr = ph_pressed(PAD - 10, y - 8, rw + 20, rh + 16);
    float hold = pfi == i && (pr || app_hold_progress() > 0) ? app_hold_progress() : 0, pk2 = press_k(PAD - 10, y - 8, rw + 20 - 70, rh + 16), hs = 1 - 0.03f * pk2 + 0.03f * hold * hold * (3 - 2 * hold); /* held: it swells and the outline comes round it, as a card's */
    gfx_scale_push(hs, hs, PAD + rw / 2, y + rh / 2);
    if (here || pr || hold > 0) gfx_rrect(PAD - 10, y - 8, rw + 20, rh + 16, 20, here ? col_alpha(ACC, pr ? 0.24f : 0.16f) : WHITE_A(18));
    float tw2 = 96; GLuint t = it->poster[0] ? img_get_shaped(it->poster, (int)tw2, (int)rh, 12, 1) : 0; if (!t || !ph_picture(t, PAD, y, tw2, rh, 12, WHITE_A(255))) ph_box(PAD, y, tw2, rh, 12);
    if (done) gfx_rrect(PAD, y, tw2, rh, 12, BLACK_A(70));
    float x2 = PAD + tw2 + 20, w3 = rw - tw2 - 20 - 64 - 70; char pn[12]; snprintf(pn, sizeof pn, "%d", i + 1);
    text_draw(T1("Part {n}", "n", pn), x2, y + 18, F_TINY + 1, 700, TXT3, 0.6f, w3, ALIGN_START);
    int nl = text_wrap_count(it->name, w3, 2, F_BODY, 700, 0); text_draw_wrapped(it->name, x2, y + 18 + text_line_height(F_TINY + 1) + 6, w3, 2, F_BODY, 700, done ? TXT2 : TXT, 0, ALIGN_START, text_line_height(F_BODY));
    if (it->metaLine[0]) text_draw(it->metaLine, x2, y + 18 + text_line_height(F_TINY + 1) + 10 + nl * text_line_height(F_BODY), F_SMALL, 500, TXT3, 0, w3, ALIGN_START);
    { CwInfo ci; memset(&ci, 0, sizeof ci); if (!done && lib_cw_info(it, &ci) && ci.pct > 0.01f && ci.pct < 0.97f) { /* partway: a slim line how far, and the time left beside it, at the foot of its words */
        char r[32], lf[64] = ""; if (ci.leftMin > 0) { sb_fmt_runtime((int)(ci.leftMin + 0.5f) < 1 ? 1 : (int)(ci.leftMin + 0.5f), r, sizeof r); snprintf(lf, sizeof lf, "%s", T1("{t} left", "t", r)); }
        float lw = fminf(120, w3 * 0.4f), ly = y + rh - 22, th = text_line_height(F_TINY + 1); gfx_rrect(x2, ly - 2, lw, 4, 2, WHITE_A(36)); gfx_rrect(x2, ly - 2, fmaxf(4, lw * ci.pct), 4, 2, ACC2 | 0xff000000u);
        if (lf[0]) text_draw(lf, x2 + lw + 12, ly - th / 2, F_TINY + 1, 600, TXT2, 0, w3 - lw - 12, ALIGN_START); } }
    if (hold > 0) hold_trace_draw(PAD - 10, y - 8, rw + 20, rh + 16, 20, ease_apply(EASE_IN_OUT, hold));
    gfx_scale_pop();
    hit_add(PAD - 10, y - 8, rw + 20 - 140, rh + 16, c_focus, NULL, 0, i); /* (a tap opens it, a hold its menu: as any card) */
    if (strncmp(it->id, "tmdb:", 5)) { float pr = 0; int ds = ph_dl_state(it->id, it->id, &pr); float bx = PAD + rw - 26 - 70, by = y + rh / 2, pk = press_k(bx - 30, by - 30, 60, 60); /* its download, beside its check: a button, a ring as it comes, a tick when kept (as an episode's) */
      gfx_scale_push(1 - 0.08f * pk, 1 - 0.08f * pk, bx, by); gfx_circle(bx, by, 22, WHITE_A(pk > 0 ? (int)(18 + 24 * pk) : 12));
      if (ds == DL_DONE || ds == DL_INPACK) icon_draw("ph_check", bx - 14, by - 14, 28, ACC2);
      else if (ds >= 0 && ds != DL_FAILED && ds != DL_NOSTREAM) { gfx_ring(bx - 18, by - 18, 36, 36, 18, 3, WHITE_A(40)); if (ds == DL_CHOOSING) spinner(bx, by, 9); else for (int q = 0; q <= 30; q++) { if (q > 30 * pr) break; float an = q * 12 * (float)M_PI / 180; gfx_circle(bx + 16.5f * sinf(an), by - 16.5f * cosf(an), 1.8f, ACC2 | 0xff000000u); } }
      else icon_draw("ph_down", bx - 14, by - 14, 28, WHITE_A(200));
      gfx_scale_pop(); if (ds < 0 || ds == DL_FAILED || ds == DL_NOSTREAM) hit_act(bx - 34, by - 34, 68, 68, c_dl, i, 0); }
    { float cx = PAD + rw - 26, cy = y + rh / 2, pk = press_k(cx - 30, cy - 30, 60, 60); gfx_scale_push(1 - 0.08f * pk, 1 - 0.08f * pk, cx, cy); /* watched or not: a check to tap, at a glance */
      if (done) { gfx_circle(cx, cy, 22, ACC | 0xff000000u); icon_draw("ph_check", cx - 13, cy - 13, 26, ACC_FG); } else { gfx_ring(cx - 22, cy - 22, 44, 44, 22, 2, WHITE_A(90)); icon_draw("ph_check", cx - 12, cy - 12, 24, WHITE_A(70)); }
      gfx_scale_pop(); hit_act(cx - 36, y - 8, 36 + 26 + 10, rh + 16, c_seen, i, 0); } /* (its whole height at the row's end: a tap a little off still marks it, never opens the film) */
    y += rh + 18; }
  gfx_opacity_pop();
  pv.max = y + pv.pos - (SB_H - INS_B - BAR_H) + 40; if (pv.max < 0) pv.max = 0;
  char tt[200]; snprintf(tt, sizeof tt, "\001%s", G.title); top_bar(tt, 1, pv.pos - (hh - INS_T - 140)); (void)in; }
static void p_draw(void) {
  if (G.coll) { p_coll(); return; }
  ph_drag(0, 0, SB_W, SB_H, &pv, 1); scroll_tick(&pv);
  float y = INS_T + 88 + 10 - pv.pos, in = G.fade.on ? ease_apply(EASE_OUT, timer_x(&G.fade)) : 1;
  if (G.loading) { int cols = 3; float gap = 14, w = (SB_W - 2 * PAD - gap * (cols - 1)) / cols; for (int i = 0; i < 9; i++) ph_box(PAD + (i % cols) * (w + gap), y + (i / cols) * (w * 1.5f + gap), w, w * 1.5f, 14); y += 3 * (w * 1.5f + gap); }
  else if (G.empty) { text_draw_wrapped(G.person ? T("No films found for this person") : T("None found"), PAD, y + 20, SB_W - 2 * PAD, 3, F_BODY, 500, TXT2, 0, ALIGN_START, 34); y += 120; }
  else { gfx_opacity_push(in); gfx_offset_push(0, 30 * (1 - in)); y += poster_grid(&G.items, y, p_focus, 0, pfi); gfx_offset_pop(); gfx_opacity_pop(); }
  pv.max = y + pv.pos - (SB_H - INS_B - BAR_H) + 30;
  top_bar(G.title, 1, pv.pos + 100);
}
static int p_key(int k) { if (k == K_OK && pfi >= 0 && pfi < G.items.n) { open_it(&G.items.items[pfi]); return 1; } if (k == K_CH_UP) { pv.pos = 0; pv.vel = 0; return 1; } return 0; }
static void p_enter(const void *pp) { if (pp) { memset(&pv, 0, sizeof pv); pfi = -1; enter(pp); } }
static const Item *p_focused(void) { return pfi >= 0 && pfi < G.items.n ? &G.items.items[pfi] : NULL; }
static void p_out(float k) { phone_out(p_draw, k); }
Screen simScreen = { p_enter, NULL, p_key, tick, p_draw, p_focused, open_it, NULL, NULL, p_out };
#else
Screen simScreen = { enter, NULL, key, tick, draw, focused, open_it, NULL, origin };
#endif
