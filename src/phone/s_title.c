#include "core/traktwatch.h"
#include "ui/spoil.h"
#include <time.h>
/* A title's page: the cover across the top with the logo over it, the facts line, Play (or Resume, with how far
   it is) and Download, the description, the people with their faces, the actions (My List, Trailer, watched,
   on the TV), then Episodes by season or the titles like it. The accent takes the title's own colour. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/trailer.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "screens/faces.h"
#include "core/coll.h"
#include "core/credits.h"
#define CASTN 12 /* the people: TMDB's, in billing order (directors and writers of a film first), as on the TV */
#include "screens/detail.h"
#include "core/rating.h"

typedef struct { const cJSON *v; char id[96], name[256], thumb[512], ov[700], date[32]; int ep, sn; } Ep;
typedef struct { int sn, first, n; } Season;
static struct { DetailParams p; Item it; cJSON *meta; int gotMeta, openStreams; unsigned gen; Ep *eps; int ne; Season seasons[64]; int ns, cur; int tab; ItemList sim; int simGot; Scroll v, sv, fv; char genres[200], facts[2][120]; char cast[CASTN][96]; char face[CASTN][512]; char faceId[CASTN][16]; char role[CASTN][96]; int tid[CASTN]; int ncast, faceGot, credOn, faceAsked; double castAt; Rating rate; int rateGot; int descOpen; int *watched; int fi; Timer1 in; } D;

static int cmp_ep(const void *a, const void *b) { const Ep *x = a, *y = b; if ((x->sn == 0) != (y->sn == 0)) return x->sn == 0 ? 1 : -1; if (x->sn != y->sn) return x->sn - y->sn; return x->ep - y->ep; }
static int is_series(void) { return D.ne > 0; }
/* the watched marks, one per episode as shown (sorted): the library's flags are per the addon's own order of videos */
static int *watched_map(void) { const cJSON *vs = js_arr(D.meta, "videos"); int n = cJSON_GetArraySize(vs); if (n <= 0 || !D.ne) return NULL;
  int *raw = calloc(n, sizeof(int)), *w = calloc(D.ne, sizeof(int)); if (!raw || !w) { free(raw); free(w); return NULL; } cJSON *lit = lib_get(D.it.id); if (lit || tw_on()) lib_ep_watched_map(lit, vs, raw); /* (not in the library: Trakt's marks alone) */
  for (int k = 0; k < D.ne; k++) { int i = 0; const cJSON *v; cJSON_ArrayForEach(v, vs) { if (v == D.eps[k].v) { w[k] = raw[i]; break; } i++; } }
  free(raw); return w; }
static void build_eps(void) {
  free(D.eps); D.eps = NULL; D.ne = 0; D.ns = 0; const cJSON *vs = js_arr(D.meta, "videos"); int n = cJSON_GetArraySize(vs); if (n <= 0 || !strcmp(D.it.type, "movie")) return;
  D.eps = calloc(n, sizeof *D.eps); const cJSON *v; int i = 0;
  cJSON_ArrayForEach(v, vs) { Ep *e = &D.eps[i]; const char *id = js_str(v, "id", ""); if (!*id) continue; e->v = v; snprintf(e->id, sizeof e->id, "%s", id);
    e->sn = (int)js_num(v, "season", 0); e->ep = (int)js_num(v, "episode", js_num(v, "number", i + 1));
    snprintf(e->name, sizeof e->name, "%s", js_str(v, "name", js_str(v, "title", ""))); snprintf(e->thumb, sizeof e->thumb, "%s", js_str(v, "thumbnail", ""));
    if (!e->thumb[0] && sb_is_imdb(D.it.id) && e->sn >= 0) snprintf(e->thumb, sizeof e->thumb, "https://episodes.metahub.space/%s/%d/%d/w780.jpg", D.it.id, e->sn, e->ep); /* (no still of its own: the episode's still from metahub, as the TV does) */
    snprintf(e->ov, sizeof e->ov, "%s", js_str(v, "overview", js_str(v, "description", "")));
    const char *r = js_str(v, "released", js_str(v, "firstAired", "")); if (strlen(r) >= 10) snprintf(e->date, sizeof e->date, "%.10s", r); i++; }
  D.ne = i; qsort(D.eps, D.ne, sizeof *D.eps, cmp_ep);
  for (int k = 0; k < D.ne; k++) { if (!D.ns || D.seasons[D.ns - 1].sn != D.eps[k].sn) { if (D.ns == 64) break; D.seasons[D.ns].sn = D.eps[k].sn; D.seasons[D.ns].first = k; D.seasons[D.ns].n = 0; D.ns++; } D.seasons[D.ns - 1].n++; }
  D.cur = 0; { char pv[96] = ""; const cJSON *st = js_obj(lib_get(D.it.id), "state"); if (st && js_num(st, "timeOffset", 0) > 0) snprintf(pv, sizeof pv, "%s", js_str(st, "video_id", "")); /* the series' place (its latest episode watched, or the next one after the credits), as the TV and the official apps keep it */
    if (!pv[0]) snprintf(pv, sizeof pv, "%s", js_str(st, "video_id", "")); /* (the place even at 0: its season shown, as the TV) */
    if (pv[0]) for (int k = 0; k < D.ne; k++) if (!strcmp(D.eps[k].id, pv)) for (int s = 0; s < D.ns; s++) if (D.seasons[s].sn == D.eps[k].sn) D.cur = s; }
  free(D.watched); D.watched = watched_map(); (void)n;
}
static void on_face(int i, const char *id, const char *img, void *ud) { (void)ud; if (i >= 0 && i < D.ncast && !D.credOn) { snprintf(D.face[i], sizeof D.face[i], "%s", img ? img : ""); snprintf(D.faceId[i], sizeof D.faceId[i], "%s", id ? id : ""); D.faceGot++; } }
/* the people as the TV gets them: TMDB's credits first (every photo, the part each plays, a film's director and
   writers), waited for 2 s at most; with none, the details' names looked up one by one (photos found by name) */
static void cast_tick(void) {
  if (!D.gotMeta || D.credOn) return;
  const Credit *c = NULL; int n = credits_get(D.it.type, D.it.id, &c);
  if (n > 0) { faces_cancel(); int k = 0;
    for (int i = 0; i < n && k < CASTN; i++) { int dup = 0; for (int j = 0; j < k && !dup; j++) dup = !strcmp(D.cast[j], c[i].name); if (dup) continue;
      snprintf(D.cast[k], sizeof D.cast[k], "%s", c[i].name); snprintf(D.face[k], sizeof D.face[k], "%s", c[i].img); D.faceId[k][0] = 0; D.tid[k] = c[i].tid;
      snprintf(D.role[k], sizeof D.role[k], "%s", c[i].kind == CRED_DIRECTOR ? T("Director") : c[i].kind == CRED_WRITER ? T("Writer") : c[i].role); k++; }
    D.ncast = k; D.faceGot = k; D.credOn = 1; return; }
  if (n < 0 && app_now() - D.castAt < 2.0) return; /* (on its way: the row waits for it) */
  if (!D.faceAsked) { D.faceAsked = 1; const char *names[CASTN]; for (int i = 0; i < D.ncast; i++) names[i] = D.cast[i]; if (D.ncast) faces_find(names, D.ncast, D.it.name, on_face, NULL); }
}
static void on_person_id(const char *imdb, void *ud) { /* a TMDB person: their IMDb id came, their films open */
  int i = (int)((uintptr_t)ud & 0xff); if ((unsigned)((uintptr_t)ud >> 8) != (D.gen & 0xffffff) || i >= D.ncast || SCR != SCR_DETAIL) return;
  if (!imdb || !*imdb) { toast(T("No films found for this person"), 2600, NULL); return; } snprintf(D.faceId[i], sizeof D.faceId[i], "%s", imdb);
  DetailParams p; memset(&p, 0, sizeof p); snprintf(p.it.type, sizeof p.it.type, "person"); snprintf(p.it.id, sizeof p.it.id, "%s", imdb); snprintf(p.it.name, sizeof p.it.name, "%s", D.cast[i]); snprintf(p.it.poster, sizeof p.it.poster, "%s", D.face[i]); app_go(SCR_SIM, &p, 0); }
static void on_rating(const Rating *r, void *ud) { if ((unsigned)(uintptr_t)ud != D.gen) return; if (r) D.rate = *r; D.rateGot = 1; }
/* the facts under the genres, as on the TV: the awards in gold (their first phrase), then where it comes from */
static int num_after(const char *s, const char *pre, const char *post) { /* "won 3 oscars" -> 3 */
  size_t pl = strlen(pre); for (const char *q = s; *q; q++) { if (strncasecmp(q, pre, pl)) continue; const char *r = q + pl; while (*r == ' ') r++; if (*r < '0' || *r > '9') continue; int n = atoi(r); while (*r >= '0' && *r <= '9') r++; while (*r == ' ') r++; if (!strncasecmp(r, post, strlen(post))) return n; }
  return -1; }
static void facts_build(const cJSON *m) {
  D.facts[0][0] = D.facts[1][0] = 0; const char *as = js_str(m, "awards", ""); int n; char ns[16];
  if ((n = num_after(as, "won", "oscar")) >= 0) { snprintf(ns, sizeof ns, "%d", n); snprintf(D.facts[0], sizeof D.facts[0], "%s", n == 1 ? T("Won 1 Oscar") : T1("Won {n} Oscars", "n", ns)); }
  else if ((n = num_after(as, "nominated for", "oscar")) >= 0) { snprintf(ns, sizeof ns, "%d", n); snprintf(D.facts[0], sizeof D.facts[0], "%s", n == 1 ? T("Nominated for 1 Oscar") : T1("Nominated for {n} Oscars", "n", ns)); }
  else { for (const char *q = as; *q; q++) if (*q >= '0' && *q <= '9' && (q == as || q[-1] < '0' || q[-1] > '9')) { int v = atoi(q); const char *r = q; while (*r >= '0' && *r <= '9') r++; while (*r == ' ') r++; if (!strncasecmp(r, "win", 3)) { snprintf(ns, sizeof ns, "%d", v); snprintf(D.facts[0], sizeof D.facts[0], "%s", T1("{n} awards", "n", ns)); break; } } }
  const cJSON *cj = cJSON_GetObjectItem(m, "country"); char co[120] = "";
  if (cJSON_IsString(cj)) { char buf[400]; snprintf(buf, sizeof buf, "%s", cj->valuestring); char *sv; int k = 0; for (char *t = strtok_r(buf, ",", &sv); t && k < 2; t = strtok_r(NULL, ",", &sv)) { while (*t == ' ') t++; char *e = t + strlen(t); while (e > t && e[-1] == ' ') *--e = 0; if (!*t) continue; size_t l = strlen(co); snprintf(co + l, sizeof co - l, "%s%s", k ? ", " : "", t); k++; } }
  else if (cJSON_IsArray(cj)) { const cJSON *e; int k = 0; cJSON_ArrayForEach(e, cj) { if (k >= 2 || !cJSON_IsString(e)) break; size_t l = strlen(co); snprintf(co + l, sizeof co - l, "%s%s", k ? ", " : "", e->valuestring); k++; } }
  snprintf(D.facts[1], sizeof D.facts[1], "%s", co); }
static void on_meta(cJSON *m, void *ud) { if ((unsigned)(uintptr_t)ud != D.gen) return; D.gotMeta = 1; if (!m) return; D.meta = m;
  Item keep = D.it; sb_fill_item(&D.it, m, keep.type); if (!D.it.poster[0]) snprintf(D.it.poster, sizeof D.it.poster, "%s", keep.poster); D.it.progress = keep.progress; D.it.cw = keep.cw; snprintf(D.it.videoId, sizeof D.it.videoId, "%s", keep.videoId);
  D.genres[0] = 0; const cJSON *g; int k = 0; cJSON_ArrayForEach(g, js_arr(m, "genres")) if (cJSON_IsString(g) && k < 3) { size_t l = strlen(D.genres); snprintf(D.genres + l, sizeof D.genres - l, "%s%s", k ? "  \xc2\xb7  " : "", g->valuestring); k++; }
  facts_build(m);
  D.ncast = 0; const cJSON *c; cJSON_ArrayForEach(c, js_arr(m, "cast")) if (cJSON_IsString(c) && D.ncast < 9) snprintf(D.cast[D.ncast++], 96, "%s", c->valuestring);
  { memset(D.face, 0, sizeof D.face); memset(D.faceId, 0, sizeof D.faceId); memset(D.role, 0, sizeof D.role); memset(D.tid, 0, sizeof D.tid); memset(&D.rate, 0, sizeof D.rate); D.rateGot = 0; rating_get(D.it.type, D.it.id, on_rating, (void *)(uintptr_t)D.gen); D.faceGot = 0; D.credOn = 0; D.faceAsked = 0; D.castAt = app_now(); cast_tick(); }
  build_eps(); accent_scope(D.it.id, D.it.poster, D.it.logo);
  if (D.openStreams) { D.openStreams = 0; extern void title_play(int a, int b); title_play(0, 0); }
}

/* with the best fit on, a tap plays at once; a hold (half a second) opens the streams to choose from instead, its
   ring filling under the finger; the tap that ends a hold does nothing */
void pplat_haptic(void);
static double holdFired = -1;
static float mainHold, dlHold; /* Play's hold and Download's, 0..1 (last frame's) */
static int held_long(float x, float y, float w, float h, double *since, float *prog) {
  *prog = 0; if (!ph_pressed(x, y, w, h)) { *since = 0; return 0; } if (!*since) *since = app_now();
  float p = (float)((app_now() - *since - 0.12) / 0.45); *prog = p < 0 ? 0 : p > 1 ? 1 : p; if (holdFired != ph_last_press() && *prog < 1) ph_hold_feel(*prog); /* ticks rising with the swell */
  if (*prog >= 1 && holdFired != ph_last_press()) { holdFired = ph_last_press(); pplat_haptic_k(HK_POP, 1); return 1; } return 0; }
static int hold_ended(void) { return holdFired == ph_last_press(); }
/* as the TV's title page (screens/detail.c play_target): what Play plays is the series' place, state.video_id (the latest
   episode watched, or the next one once its credits were reached), else the first episode not yet watched (specials
   last), else the first; it is resumed only past a minute into that very episode, else it starts from its beginning */
static int place_k(void) { const cJSON *st = js_obj(lib_get(D.it.id), "state"); const char *v = js_str(st, "video_id", "");
  if (*v) for (int k = 0; k < D.ne; k++) if (!strcmp(D.eps[k].id, v)) return k;
  for (int k = 0; k < D.ne; k++) if (D.eps[k].sn > 0 && D.watched && !D.watched[k]) return k;
  for (int k = 0; k < D.ne; k++) if (D.eps[k].sn > 0) return k; return D.ne ? 0 : -1; }
static int resumes(const char *vid) { const cJSON *st = js_obj(lib_get(D.it.id), "state"); return st && js_num(st, "timeOffset", 0) > 60000 && (!is_series() || !strcmp(js_str(st, "video_id", ""), vid)); }
static void ep_name(int k, char *nm, size_t c) { snprintf(nm, c, "S%d E%d  %s", D.eps[k].sn, D.eps[k].ep, D.eps[k].name); }
static void open_streams_for_play(void) { if (!is_series()) { ph_streams_pick(&D.it, D.it.id, NULL); return; }
  int k = place_k(); if (k < 0) return; char nm[300]; ep_name(k, nm, sizeof nm); ph_streams_pick(&D.it, D.eps[k].id, nm); }
void title_play(int a, int b) { (void)a; (void)b; if (hold_ended()) return;
  if (!D.gotMeta) { D.openStreams = 1; return; }
  extern int best_on(void); extern void player_choose_best(const char *type, const char *id, const char *vid);
  if (!is_series()) { if (best_on()) player_choose_best(D.it.type, D.it.id, D.it.id); else ph_streams_show(&D.it, D.it.id, NULL); return; } /* the best fit at once when that is on */
  int k = place_k(); if (k < 0) return; char nm[300]; ep_name(k, nm, sizeof nm);
  if (best_on()) player_choose_best(D.it.type, D.it.id, D.eps[k].id); else ph_streams_show(&D.it, D.eps[k].id, nm);
}
static void resume_act(int a, int b) { (void)a; (void)b; if (hold_ended()) return; Item it = D.it; CwInfo ci; memset(&ci, 0, sizeof ci);
  if (is_series()) { int k = place_k(); if (k < 0 || !resumes(D.eps[k].id)) { title_play(0, 0); return; } snprintf(ci.vid, sizeof ci.vid, "%s", D.eps[k].id); } else { if (!resumes(it.id)) { title_play(0, 0); return; } snprintf(ci.vid, sizeof ci.vid, "%s", it.id); }
  Stream st; if (sb_resume_stream(it.id, ci.vid, &st)) { app_play(&st, it.type, it.id, ci.vid, 0); return; } /* the stream it was playing, from where it stopped */
  extern int best_on(void); extern void player_choose_best(const char *type, const char *id, const char *vid);
  if (best_on()) { player_choose_best(it.type, it.id, ci.vid); return; } /* (the player picks up where it stopped) */
  char nm[300] = ""; for (int k = 0; k < D.ne; k++) if (!strcmp(D.eps[k].id, ci.vid)) snprintf(nm, sizeof nm, "S%d E%d  %s", D.eps[k].sn, D.eps[k].ep, D.eps[k].name);
  ph_streams_show(&it, ci.vid, nm[0] ? nm : NULL); } /* no stream kept: its streams, for the same film or episode */
/* Download: as Play (the best fit when on, else the streams), the film or the episode it is at */
/* Download held: its streams, to choose the one to download (as Play held: the streams to play) */
static void dl_streams(void) { if (!D.gotMeta) return; if (!is_series()) { ph_streams_download(&D.it, D.it.id, NULL); return; } int k = place_k(); if (k >= 0) { char nm[300]; ep_name(k, nm, sizeof nm); ph_streams_download(&D.it, D.eps[k].id, nm); } }
static void dl_act(int a, int b) { (void)a; (void)b; if (!D.gotMeta || hold_ended()) return; if (!is_series()) { ph_download(&D.it, D.it.id, NULL); return; }
  { int k = place_k(); ph_episodes_download(&D.it, k >= 0 ? D.eps[k].id : NULL); return; } /* a series: pick its seasons and episodes */
  int k = place_k(); if (k >= 0) { char nm[300]; ep_name(k, nm, sizeof nm); ph_download(&D.it, D.eps[k].id, nm); } }
static void ep_dl(int k, int b) { (void)b; if (k < 0 || k >= D.ne) return; Ep *e = &D.eps[k]; char nm[300]; snprintf(nm, sizeof nm, "S%d E%d  %s", e->sn, e->ep, e->name); ph_download(&D.it, e->id, nm); }
static void list_act(int a, int b) { (void)a; (void)b; lib_toggle(&D.it); if (lib_has(D.it.id)) { char pb[600]; ph_fly(sb_poster_of(&D.it, pb, sizeof pb), 2); } /* it flies to the Library */ toast(lib_has(D.it.id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
static void trailer_act(int a, int b) { (void)a; (void)b; detail_trailer(D.it.type, D.it.id, D.it.name); }
static void seen_act(int a, int b) { (void)a; (void)b; lib_toggle_watched(&D.it, js_arr(D.meta, "videos")); toast(lib_is_marked_watched(&D.it) ? T("Marked as watched") : T("Marked as unwatched"), 1600, NULL); cJSON *lit = lib_get(D.it.id); if (lit && D.watched) lib_ep_watched_map(lit, js_arr(D.meta, "videos"), D.watched); app_refresh_boards(); }
static void face_act(int i, int b) { (void)b; if (i < 0 || i >= D.ncast) return; if (!D.faceId[i][0] && D.tid[i]) { credits_person_imdb(D.tid[i], on_person_id, (void *)(((uintptr_t)(D.gen & 0xffffff) << 8) | (uintptr_t)i)); return; } if (!D.faceId[i][0]) { toast(T("Not sure who this is yet"), 1600, NULL); return; }
  DetailParams p; memset(&p, 0, sizeof p); snprintf(p.it.type, sizeof p.it.type, "person"); snprintf(p.it.id, sizeof p.it.id, "%s", D.faceId[i]); snprintf(p.it.name, sizeof p.it.name, "%s", D.cast[i]); app_go(SCR_SIM, &p, 0); }
static void similar_act(int a, int b) { (void)a; (void)b; DetailParams p; memset(&p, 0, sizeof p); p.it = D.it; app_go(SCR_SIM, &p, 0); }
static void desc_act(int a, int b) { (void)a; (void)b; D.descOpen = !D.descOpen; }
static void reviews_act(int a, int b);
extern int best_on(void);
static float logo_w(void) { return SB_W > SB_H ? SB_W * 0.30f : SB_W * 0.6f; } /* (sideways: the TV's size, on the picture's left) */
void ph_logo_ahead(const Item *it) { char lb[600]; const char *lg = sb_logo_of(it, lb, sizeof lb); float asp; if (lg && *lg) img_get(lg, (int)logo_w(), (int)(logo_w() * 0.36f), &asp); }
/* Ratings and reviews, from every source the C base reaches (core/reviews.c): the main score with how people spread
   their votes, one tile per source, then people's comments. All of it part of the page: each piece comes in (rises
   and fades) as the page brings it into view, as the sources answer. */
#include "core/reviews.h"
#define NPOP 48
static Tween pop[NPOP]; static unsigned popGen; static int spoilOpen[REV_MAXC], showN;
static float pop_k(int i, float y) { if (popGen != D.gen) { popGen = D.gen; for (int k = 0; k < NPOP; k++) tween_set(&pop[k], 0); memset(spoilOpen, 0, sizeof spoilOpen); showN = 6; }
  if (i >= NPOP) return 1; int in = y < SB_H - INS_B - BAR_H - 120 && y > -260; /* well into view, not under the bar */ if (in && pop[i].to == 0) tween_to(&pop[i], 1, 0.5f, EASE_OUT); else if (!in && pop[i].to > 0) tween_set(&pop[i], 0); /* out of view: ready to come in again */
  tween_tick(&pop[i], phDt); return pop[i].v; }
static void pop_begin(float k, float cx, float cy) { gfx_opacity_push(k); float sc = 0.94f + 0.06f * k; gfx_offset_push(0, 26 * (1 - k)); gfx_scale_push(sc, sc, cx, cy); }
static void pop_end(void) { gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop(); }
static uint32_t src_col(const char *s) { if (strstr(s, "IMDb")) return RGB(0xf5, 0xc5, 0x18); if (strstr(s, "Rotten") || strstr(s, "RT")) return RGB(0xfa, 0x32, 0x0a); if (strstr(s, "Meta")) return RGB(0x66, 0xcc, 0x33); if (strstr(s, "Trakt")) return RGB(0xed, 0x1c, 0x24); if (strstr(s, "TMDB")) return RGB(0x01, 0xb4, 0xe4); return ACC2; }
/* a source's mark, drawn in its own colours (no pictures to fetch): the IMDb yellow plate, TMDB's teal-to-blue pill,
   Trakt's red disc with its ring, the tomato, Metacritic's dark disc. Returns its width at height h */
static float src_logo(const char *s, float x, float cy, float h) {
  float y = cy - h / 2; int ts = (int)(h * 0.58f);
  if (strstr(s, "IMDb")) { float w = h * 1.9f; gfx_rrect(x, y, w, h, h * 0.18f, RGB(0xf5, 0xc5, 0x18)); text_draw("IMDb", x + w / 2, cy - text_line_height(ts) / 2.0f, ts, 900, RGB(0, 0, 0), -0.6f, 0, ALIGN_CENTER); return w; }
  if (strstr(s, "TMDB") || strstr(s, "TMDb")) { float w = h * 2.2f; gfx_rrect_h(x, y, w, h, h / 2, RGB(0x90, 0xce, 0xa1), RGB(0x01, 0xb4, 0xe4)); text_draw("TMDB", x + w / 2, cy - text_line_height(ts) / 2.0f, ts, 900, RGB(0x0d, 0x25, 0x3f), 0, 0, ALIGN_CENTER); return w; }
  if (strstr(s, "Trakt")) { float r = h / 2; gfx_circle(x + r, cy, r, RGB(0xed, 0x1c, 0x24)); gfx_ring(x + r * 0.35f, cy - r * 0.65f, r * 1.3f, r * 1.3f, r * 0.65f, h * 0.07f, WHITE_A(255));
    text_draw("trakt", x + h + 8, cy - text_line_height(ts) / 2.0f, ts, 800, WHITE_A(240), -0.3f, 0, ALIGN_START); return h + 8 + text_width("trakt", ts, 800, -0.3f); }
  if (strstr(s, "Rotten") || !strcmp(s, "RT")) { float r = h / 2; gfx_circle(x + r, cy + r * 0.08f, r * 0.92f, RGB(0xfa, 0x32, 0x0a)); gfx_rrect(x + r * 0.62f, y, r * 0.76f, r * 0.42f, r * 0.2f, RGB(0x2e, 0xb8, 0x4b));
    text_draw("Tomatometer", x + h + 8, cy - text_line_height(ts) / 2.0f, ts, 800, WHITE_A(240), 0, 0, ALIGN_START); return h + 8 + text_width("Tomatometer", ts, 800, 0); }
  if (strstr(s, "Meta")) { float r = h / 2; gfx_circle(x + r, cy, r, RGB(0x14, 0x14, 0x14)); gfx_ring(x + 1, y + 1, h - 2, h - 2, r - 1, h * 0.08f, RGB(0xff, 0xcc, 0x34)); text_draw("m", x + r, cy - text_line_height(ts) / 2.0f, ts, 900, WHITE_A(255), 0, 0, ALIGN_CENTER);
    text_draw("Metacritic", x + h + 8, cy - text_line_height(ts) / 2.0f, ts, 800, WHITE_A(240), 0, 0, ALIGN_START); return h + 8 + text_width("Metacritic", ts, 800, 0); }
  uint32_t c = src_col(s); gfx_circle(x + h * 0.3f, cy, h * 0.3f, c); text_draw(s, x + h * 0.7f, cy - text_line_height(ts) / 2.0f, ts, 700, TXT2, 0, 0, ALIGN_START); return h * 0.7f + text_width(s, ts, 700, 0);
}
/* a review read in full: a sheet with its source, its writer and all its words */
static int revOpen = -1;
static void rev_sheet_draw(float a) {
  const Reviews *R = reviews_get(D.it.type, D.it.id, 0, 0); if (revOpen < 0 || revOpen >= R->nc) { ph_sheet_close(); return; } const RevComment *c = &R->c[revOpen];
  float cw = SB_W - 2 * PAD; int nl = text_wrap_count(c->text, cw, 40, F_BODY, 400, 0); float h = 220 + nl * 36 + INS_B + 40; if (h > SB_H - INS_T - 40) h = SB_H - INS_T - 40;
  float y = sheet_panel(h, a), lx = PAD; lx += src_logo(c->from, PAD, y + 60, 40) + 16;
  text_draw(c->user, lx, y + 60 - text_line_height(F_BODY + 2) / 2.0f, F_BODY + 2, 800, TXT, 0, SB_W - PAD - lx - 100, ALIGN_START);
  if (c->score > 0) { char sc[8]; snprintf(sc, sizeof sc, "%d", c->score); float bw = text_width(sc, F_BODY, 800, 0) + 30; gfx_rrect(SB_W - PAD - bw, y + 38, bw, 44, 14, col_alpha(ACC, 0.3f)); text_draw(sc, SB_W - PAD - bw / 2, y + 60 - text_line_height(F_BODY) / 2.0f, F_BODY, 800, ACC3, 0, 0, ALIGN_CENTER); }
  gfx_clip_push(0, y + 110, SB_W, h - 110 - INS_B); text_draw_wrapped(c->text, PAD, y + 120, cw, 40, F_BODY, 400, WHITE_A(225), 0, ALIGN_START, 36); gfx_clip_pop();
}
static const PhSheet revSheet = { rev_sheet_draw, NULL, NULL };
static void rev_open(int i, int b) { (void)b; const Reviews *R = reviews_get(D.it.type, D.it.id, 0, 0); if (i < 0 || i >= R->nc) return; if (R->c[i].spoiler && !spoilOpen[i]) { spoilOpen[i] = 1; return; } revOpen = i; ph_sheet_open(&revSheet); }
static Scroll revS;
/* opened to read its reviews (a hold menu's Reviews): the page comes in as usual, then glides down to them, following
   them while what is above them (episodes, faces) settles */
static int toRev; static double toRevAt;
void ph_title_to_reviews(void) { toRev = 1; toRevAt = app_now(); }
static void to_reviews(float rvY) { if (!toRev) return; double t = app_now() - toRevAt; float want = rvY - (INS_T + 120); if (want < 0) want = 0;
  if (toRev == 1) { if (D.gotMeta && t > 0.45) { scroll_to(&D.v, want); toRev = 2; } else if (t > 6) toRev = 0; return; } /* (once the page is in and laid out) */
  if (D.v.drag || t > 3.5) { toRev = 0; return; } /* (a finger takes it, or it has settled) */
  if (D.v.toTop) D.v.to = want; else if (fabsf(D.v.pos - want) > 2) scroll_to(&D.v, want); }
static float reviews_draw(float y) {
  float y0 = y; const Reviews *R = reviews_get(D.it.type, D.it.id, 0, 0); int pi = 0; float cw = SB_W - 2 * PAD;
  static unsigned rg; static double rAt; if (rg != D.gen) { rg = D.gen; rAt = app_now(); }
  if (R->pending && app_now() - rAt < 4) { text_draw(T("Ratings and reviews"), PAD, y, F_H2 - 2, 800, TXT, -0.3f, 0, ALIGN_START); spinner(SB_W - PAD - 20, y + 20, 12); y += 62;
    ph_box(PAD, y, cw, 230, 28); y += 248; for (int i = 0; i < 3; i++) ph_box(PAD + i * (cw - 24) / 3 + i * 12, y, (cw - 24) / 3, 120, 22); y += 142; return y - y0; } /* every source in together, never one by one */
  text_draw(T("Ratings and reviews"), PAD, y, F_H2 - 2, 800, TXT, -0.3f, 0, ALIGN_START); if (R->pending) spinner(SB_W - PAD - 20, y + 20, 12); y += 62;
  /* the main score and how the votes spread */
  if (R->score > 0) { float k = pop_k(pi++, y), h = 230; pop_begin(k, SB_W / 2, y + h / 2);
    gfx_rrect(PAD, y, cw, h, 28, SURF); gfx_rrect_v(PAD, y, cw, h, 28, WHITE_A(12), WHITE_A(0)); gfx_ring(PAD, y, cw, h, 28, 1, WHITE_A(14));
    char sc[16]; snprintf(sc, sizeof sc, "%.1f", R->score); text_draw(sc, PAD + 30, y + 26, 76, 800, TXT, -1.5f, 0, ALIGN_START);
    float sw = text_width(sc, 76, 800, -1.5f); text_draw("/ 10", PAD + 30 + sw + 10, y + 64, F_BODY, 600, TXT3, 0, 0, ALIGN_START);
    char vs[64] = ""; if (R->votes > 0) { char n[16]; snprintf(n, sizeof n, R->votes >= 1000 ? "%.1fk" : "%.0f", R->votes >= 1000 ? R->votes / 1000.0 : (double)R->votes); snprintf(vs, sizeof vs, "%s", T2("{n} votes on {src}", "n", n, "src", R->scoreSrc)); }
    else vs[0] = 0; { float lw = src_logo(R->scoreSrc, PAD + 30, y + 150, 34); if (vs[0]) text_draw(vs, PAD + 30, y + 178, F_TINY, 600, TXT3, 0, cw / 2 - 30, ALIGN_START); (void)lw; } /* its source, by its mark */
    if (R->hasDist) { int mx = 1; for (int i = 0; i < 10; i++) if (R->dist[i] > mx) mx = R->dist[i]; float bx = PAD + cw * 0.48f, bw = (cw * 0.52f - 30) / 10, bh = 150, by = y + 30;
      for (int i = 0; i < 10; i++) { float fh = bh * R->dist[i] / mx * k; gfx_rrect(bx + i * bw + 3, by + bh - fh, bw - 6, fh > 4 ? fh : 4, 4, i >= 7 ? ACC2 : WHITE_A(70)); char n[4]; snprintf(n, sizeof n, "%d", i + 1); text_draw(n, bx + i * bw + bw / 2, by + bh + 8, F_TINY - 2, 600, TXT3, 0, 0, ALIGN_CENTER); } }
    else { const char *r = js_str(D.meta, "imdbRating", ""); (void)r; float bw = cw - 60; gfx_rrect(PAD + 30, y + 176, bw, 8, 4, WHITE_A(30)); gfx_rrect_h(PAD + 30, y + 176, bw * R->score / 10 * k, 8, 4, RGB(0xf5, 0xc5, 0x18), ACC2); }
    pop_end(); y += h + 18; }
  /* one tile per source */
  if (R->ntiles) { int cols = 3; float gap = 12, tw = (cw - gap * (cols - 1)) / cols, th = 120;
    for (int i = 0; i < R->ntiles; i++) { float tx = PAD + (i % cols) * (tw + gap), ty = y + (i / cols) * (th + gap), k = pop_k(pi++, ty); const RevTile *t = &R->tiles[i]; uint32_t c = src_col(t->src);
      pop_begin(k, tx + tw / 2, ty + th / 2); gfx_rrect(tx, ty, tw, th, 22, SURF); gfx_rrect_v(tx, ty, tw, th, 22, col_alpha(c | 0xff000000u, 0.16f), col_alpha(c | 0xff000000u, 0.02f)); gfx_ring(tx, ty, tw, th, 22, 1, col_alpha(c | 0xff000000u, 0.35f));
      gfx_clip_push(tx, ty, tw, th); src_logo(t->src, tx + 18, ty + 32, 30); gfx_clip_pop(); text_draw(t->val, tx + 20, ty + 58, F_H1 - 4, 800, TXT, -0.6f, tw - 30, ALIGN_START); (void)c; pop_end(); }
    y += ((R->ntiles + cols - 1) / cols) * (th + gap) + 10; }
  /* the awards */
  { const char *aw = js_str(D.meta, "awards", ""); if (*aw) { float k = pop_k(pi++, y); pop_begin(k, SB_W / 2, y + 30); icon_draw("d_award", PAD, y + 4, 34, RGB(0xf5, 0xc5, 0x18)); int n = text_draw_wrapped(aw, PAD + 48, y + 4, cw - 48, 3, F_SMALL + 1, 500, WHITE_A(200), 0, ALIGN_START, 30); pop_end(); y += n * 30 + 24; } }
  /* the comments */
  if (R->nc) { y += 10; text_draw(T("What people say"), PAD, y, F_BODY + 2, 800, TXT, -0.2f, 0, ALIGN_START); y += 50;
    /* side by side, swiped across: each card short (its first lines), a tap reads it in full */
    float cwid = cw * 0.84f, ch = 360, gap = 18, ip = 32; /* ip: the card's inner margin */ ph_drag(0, y, SB_W, ch, &revS, 0); scroll_tick(&revS); revS.max = R->nc * (cwid + gap) - gap - cw; if (revS.max < 0) revS.max = 0;
    gfx_clip_push(0, y - 10, SB_W, ch + 20);
    for (int i = 0; i < R->nc; i++) { const RevComment *c = &R->c[i]; float x = PAD + i * (cwid + gap) - revS.pos; if (x > SB_W || x + cwid < 0) continue;
      int hide = c->spoiler && !spoilOpen[i]; float k = 1; if (i < NPOP - 20) { Tween *t = &pop[20 + i]; int in = y < SB_H - INS_B - BAR_H - 160 && y > -ch && x < SB_W - 40 && x > -cwid + 40; if (in && t->to == 0) tween_to(t, 1, 0.45f, EASE_OUT); else if (!in && t->to > 0) tween_set(t, 0); tween_tick(t, phDt); k = t->v; } /* each comes in as it slides into view */
      gfx_opacity_push(k); gfx_offset_push(40 * (1 - k), 0);
      float pk = press_k(x, y, cwid, ch), sc = 1 - 0.02f * pk; gfx_scale_push(sc, sc, x + cwid / 2, y + ch / 2);
      gfx_rrect(x, y, cwid, ch, 26, SURF); gfx_rrect_v(x, y, cwid, 90, 26, col_alpha(src_col(c->from) | 0xff000000u, 0.12f), WHITE_A(0)); gfx_ring(x, y, cwid, ch, 26, 1, WHITE_A(14));
      float lx = x + ip + src_logo(c->from, x + ip, y + 48, 32) + 14;
      text_draw(c->user, lx, y + 48 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, TXT, 0, x + cwid - lx - 90, ALIGN_START);
      if (c->score > 0) { char s2[8]; snprintf(s2, sizeof s2, "%d", c->score); float bw = text_width(s2, F_SMALL, 800, 0) + 26; gfx_rrect(x + cwid - ip - bw, y + 28, bw, 40, 12, col_alpha(ACC, 0.3f)); text_draw(s2, x + cwid - ip - bw / 2, y + 48 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 800, ACC3, 0, 0, ALIGN_CENTER); }
      char d[32] = ""; if (c->days >= 0) { char n[12]; int dd = c->days; snprintf(n, sizeof n, "%d", dd >= 365 ? dd / 365 : dd >= 30 ? dd / 30 : dd); snprintf(d, sizeof d, "%s", dd == 0 ? T("today") : dd >= 365 ? T1("{n} y ago", "n", n) : dd >= 30 ? T1("{n} mo ago", "n", n) : T1("{n} d ago", "n", n)); } /* (years, months, days) */
      if (d[0]) text_draw(d, x + ip, y + 82, F_TINY, 600, TXT3, 0, cwid - 2 * ip, ALIGN_START);
      if (hide) { gfx_rrect(x + ip, y + 124, cwid - 2 * ip, ch - 124 - ip, 18, WHITE_A(12)); icon_draw("eyeOff", x + cwid / 2 - 20, y + ch / 2 - 30, 40, TXT2); text_draw(T("Spoiler: tap to read"), x + cwid / 2, y + ch / 2 + 20, F_SMALL, 700, TXT2, 0, 0, ALIGN_CENTER); }
      else { int all = text_wrap_count(c->text, cwid - 2 * ip, 40, F_SMALL + 1, 400, 0); text_draw_wrapped(c->text, x + ip, y + 124, cwid - 2 * ip, 5, F_SMALL + 1, 400, WHITE_A(210), 0, ALIGN_START, 30);
        if (all > 5) { gfx_rrect_v(x + 1, y + ch - 100, cwid - 2, 60, 0, col_alpha(SURF | 0xff000000u, 0), SURF | 0xff000000u); text_draw(T("Read more"), x + ip, y + ch - ip - 22, F_SMALL, 800, ACC3, 0, 0, ALIGN_START); } }
      gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop(); hit_act(x, y, cwid, ch, rev_open, i, 0); }
    gfx_clip_pop(); y += ch + 24; }
  else if (!R->pending && R->score <= 0 && !R->ntiles) { text_draw(T("No reviews for this title"), PAD, y, F_BODY, 500, TXT2, 0, 0, ALIGN_START); y += 50; }
  if (sb_is_imdb(D.it.id)) { y += 6; button(PAD, y, cw, 76, "ph_ext", T("Read the reviews on IMDb"), 0, reviews_act, 0, 0); y += 76 + 20; }
  return y - y0;
}
static void reviews_act(int a, int b) { (void)a; (void)b; char u[160]; snprintf(u, sizeof u, "https://www.imdb.com/title/%s/reviews", D.it.id); if (!pplat_open_url(u)) toast(T("No reviews for this title"), 2400, NULL); }
static void season_pick(int i) { if (i >= 0 && i < D.ns) D.cur = i; }
static float seasonK = 1; /* the episodes of a season just picked coming in */
static void season_tap(int i, int b) { (void)b; season_pick(i); }
/* spoilers (as the TV's title page): an episode not watched yet is hidden, its still soft, its words too if asked; the
   series' place and the next to watch stay clear; a tap on a hidden one shows it first (the TV's OK) */
static int ep_unaired(int k) { static char today[11]; static double tAt = -99; if (app_now() - tAt > 60) { tAt = app_now(); time_t t0 = time(NULL); struct tm tm_; localtime_r(&t0, &tm_); strftime(today, sizeof today, "%Y-%m-%d", &tm_); } return D.eps[k].date[0] && strcmp(D.eps[k].date, today) > 0; }
static int ep_hid(int k) { if (!spoil_mode() || k < 0 || k >= D.ne || !D.eps[k].sn) return 0;
  if (ep_unaired(k) && (!*js_str(D.eps[k].v, "thumbnail", "") || !spoil_blurable(D.eps[k].thumb))) return 0; /* (not out yet: hidden only when it has a still of its own, which gives it away as an aired one's does; none: nothing to give away, as the TV) */ int nk = place_k(); return spoil_hidden(D.it.id, D.eps[k].id, D.watched ? D.watched[k] : 0, nk >= 0 ? D.eps[nk].id : NULL); }
static void ep_act(int k, int b) { (void)b; if (k < 0 || k >= D.ne || hold_ended()) return; if (ep_hid(k)) { spoil_reveal(D.eps[k].id); pplat_haptic_k(HK_TICK, 0.6f); return; } Ep *e = &D.eps[k]; char nm[300]; snprintf(nm, sizeof nm, "S%d E%d  %s", e->sn, e->ep, e->name); extern int best_on(void); extern void player_choose_best(const char *type, const char *id, const char *vid); if (best_on()) player_choose_best(D.it.type, D.it.id, e->id); else ph_streams_show(&D.it, e->id, nm); } /* the best fit at once when that is on */

static struct { Item it; float pos; int tab; } H[12]; static int nh, leftPage;
static void load(const Item *it, int openStreams) { D.gen++; D.it = *it; D.meta = NULL; D.gotMeta = 0; D.openStreams = openStreams; D.ne = 0; D.ns = 0; D.tab = 0; D.descOpen = 0; D.ncast = 0; D.simGot = 0; itemlist_clear(&D.sim); memset(&D.v, 0, sizeof D.v); memset(&D.sv, 0, sizeof D.sv); memset(&D.fv, 0, sizeof D.fv); D.fi = -1;
  accent_scope(it->id, it->poster, it->logo); /* the colour now, from what the card had */ sb_meta(it->type, it->id, on_meta, (void *)(uintptr_t)D.gen); }
static void enter(const void *params) {
  const DetailParams *p = params; bg_clear(); timer_start(&D.in, 0.35f);
  if (p) { /* a title opened: from a tab, a new history; from a title (Similar), one more step */
    if (phonePrevScr == SCR_HOME || phonePrevScr == SCR_DISC || phonePrevScr == SCR_LIB) nh = 0;
    if (nh > 0) { H[nh - 1].pos = D.v.pos; H[nh - 1].tab = D.tab; }
    if (nh == 12) { memmove(H, H + 1, 11 * sizeof *H); nh = 11; }
    H[nh].it = p->it; H[nh].pos = 0; H[nh].tab = 0; nh++; D.p = *p; load(&p->it, p->openStreams); }
  else { /* Back: to the title before (when this page was left for another title), or the same one again */
    if (!leftPage && nh > 1) nh--;
    if (nh > 0 && (strcmp(H[nh - 1].it.id, D.it.id) || !D.gotMeta)) { load(&H[nh - 1].it, 0); D.v.pos = H[nh - 1].pos; D.tab = H[nh - 1].tab; }
    else if (nh > 0) accent_scope(D.it.id, D.it.poster, D.it.logo); }
  leftPage = 0;
}
static void leave(void) { spoil_forget(); faces_cancel(); accent_scope(NULL, NULL, NULL); leftPage = 1; if (nh > 0) { H[nh - 1].pos = D.v.pos; H[nh - 1].tab = D.tab; } }
/* a title in a collection (a film series, a franchise): "Part 2 of 4 · Dune Collection" under its facts and a
   Collection pill, both opening the collection's page; known on a later frame (asked, never waited on) */
static struct { char key[24], name[160], line[240]; int n, ix; unsigned ver, gen; Tween a; } CL;
static void coll_open(int a, int b) { (void)a; (void)b; if (!CL.key[0]) return; DetailParams p; memset(&p, 0, sizeof p); snprintf(p.it.type, sizeof p.it.type, "collection"); snprintf(p.it.id, sizeof p.it.id, "coll:%s", CL.key); snprintf(p.it.name, sizeof p.it.name, "%s", CL.name); snprintf(p.it.videoId, sizeof p.it.videoId, "%s", D.it.id); app_go(SCR_SIM, &p, 0); }
static void coll_check(void) {
  if (CL.gen != D.gen) { memset(&CL, 0, sizeof CL); CL.gen = D.gen; CL.ix = -1; }
  if (!D.gotMeta || CL.ver == coll_version() || strncmp(D.it.id, "tt", 2)) return; CL.ver = coll_version();
  { static unsigned hv; if (D.meta && hv != D.gen && !strcmp(D.it.type, "movie")) { hv = D.gen; coll_hint_tmdb(D.it.id, (int)js_num(D.meta, "moviedb_id", 0)); } } /* (a film's TMDB id with its details: no look-up) */
  char key[24]; const char *nm; if (coll_of(D.it.type, D.it.id, key, sizeof key, &nm) != 1) return; const Coll *c = coll_get(key); if (!c || c->n < 2) return;
  int ix = coll_index(c, D.it.id); snprintf(CL.key, sizeof CL.key, "%s", key); snprintf(CL.name, sizeof CL.name, "%s", c->name[0] ? c->name : nm); CL.n = c->n; CL.ix = ix;
  if (ix >= 0) { char a[16], b[16]; snprintf(a, sizeof a, "%d", ix + 1); snprintf(b, sizeof b, "%d", c->n); snprintf(CL.line, sizeof CL.line, "%s \xc2\xb7 %s", T2("Part {n} of {m}", "n", a, "m", b), CL.name); } else snprintf(CL.line, sizeof CL.line, "%s", CL.name); }
static void tick(float dt) { coll_check(); timer_tick(&D.in, dt); faces_tick(); cast_tick();
  { static unsigned lv; unsigned lvNow = lib_version() + (tw_ver() * 2u + (unsigned)tw_on()) * 7919u; /* (Trakt's marks move it too) */ if (lv != lvNow) { lv = lvNow; if (D.ne && D.meta) { int *w = watched_map(); if (w) { free(D.watched); D.watched = w; } } } } } /* (watched marks follow the library: a film finished, the series caught up, earlier ones rewatched) */
static void draw(void) { ph_fly_ready(&D.it);
  ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, &D.v, 1); scroll_tick(&D.v);
  int wide = SB_W > SB_H; /* sideways (a tablet held flat), as on the TV: the picture fills the screen, the name, facts, pills and story on it */
  float y0 = -D.v.pos, coverH = SB_W * 9 / 16 + INS_T + 20; if (coverH > SB_H * 0.62f) coverH = SB_H * 0.62f; if (wide) coverH = SB_H; char buf[600];
  const char *bg = sb_bg_mid(&D.it, buf, sizeof buf); GLuint tex = bg && *bg ? img_get_shaped(bg, (int)SB_W, (int)coverH, 0, 1) : 0;
  float py = y0 * 0.5f; if (py > 0) py = 0;
  if (!tex || !ph_picture(tex, 0, py, SB_W, coverH + (py < 0 ? 0 : 0), 0, WHITE_A(255))) ph_box(0, py, SB_W, coverH, 0);
  gfx_rrect_v(0, py + coverH * 0.3f, SB_W, coverH * 0.7f + 2, 0, col_alpha(PAGE | 0xff000000u, 0), PAGE | 0xff000000u);
  gfx_rrect_v(0, py, SB_W, INS_T + 100, 0, BLACK_A(120), BLACK_A(0));
  if (wide) for (int q = 0; q < 8; q++) { float u0 = q / 8.0f, u1 = (q + 1) / 8.0f, e0 = 1 - u0 * u0 * (3 - 2 * u0), e1 = 1 - u1 * u1 * (3 - 2 * u1); gfx_rrect_h(SB_W * 0.66f * u0, py, SB_W * 0.66f * (u1 - u0), coverH, 0, col_alpha(PAGE | 0xff000000u, e0 * 0.85f), col_alpha(PAGE | 0xff000000u, e1 * 0.85f)); } /* a shade from the left that the words sit on */
  float y = wide ? y0 + INS_T + SB_H * (ph_tabw() ? 0.16f : 0.04f) : y0 + coverH - 70; /* (a phone sideways: from near the top, so its story is above the bar) */
  char lb[600]; const char *logo = sb_logo_of(&D.it, lb, sizeof lb); float lw = logo_w(), lh = lw * 0.36f, asp = 0;
  GLuint lt = logo && *logo ? img_get(logo, (int)lw, (int)lh, &asp) : 0;
  /* (each piece has its room from the start and fades in when it comes: nothing below it moves) */
  { static Tween la, ma; static unsigned g; static double openAt; if (g != D.gen) { g = D.gen; tween_set(&la, 0); tween_set(&ma, 0); openAt = app_now(); }
    int logoReady = lt && asp > 0, wait = logo && *logo && !lt && !img_failed(logo) && app_now() - openAt < 2.5; /* the logo is waited for (never the name first, then the logo over it) */ tween_to(&la, logoReady || !wait ? 1 : 0, 0.35f, EASE_CSS_EASE); tween_tick(&la, phDt);
    gfx_opacity_push(la.v);
    if (logoReady) { float w = lw, h = w / asp; if (h > lh) { h = lh; w = h * asp; } gfx_image(lt, PAD, y + lh - h, w, h, 0, WHITE_A(255), 0, 0, 1, 1); }
    else if (!wait) text_draw_wrapped(D.it.name, PAD, y + lh - 2 * 54, SB_W - 2 * PAD, 2, F_H1, 800, TXT, -0.6f, ALIGN_START, 54);
    gfx_opacity_pop(); y += lh + 16;
    tween_to(&ma, D.it.metaLine[0] || D.genres[0] ? 1 : 0, 0.35f, EASE_CSS_EASE); tween_tick(&ma, phDt); gfx_opacity_push(ma.v);
    if (D.it.metaLine[0]) meta_line_draw(&D.it, PAD, y, F_SMALL + 1, WHITE_A(215)); y += 36;
    if (D.genres[0]) text_draw(D.genres, PAD, y, F_SMALL, 500, WHITE_A(140), 0, SB_W - 2 * PAD, ALIGN_START); y += 36;
    { static Tween fh; static unsigned fg2; int has = D.facts[0][0] || D.facts[1][0]; if (fg2 != D.gen) { fg2 = D.gen; tween_set(&fh, 40); } /* the line's room kept from the start; none after all: it closes softly */
      if (D.gotMeta) tween_to(&fh, has ? 40 : 0, 0.3f, EASE_WEB); tween_tick(&fh, phDt); if (!has) y += fh.v; }
    if (D.facts[0][0] || D.facts[1][0]) { float fx = PAD, is = F_SMALL + 4; /* the awards in gold, the country */
      for (int i = 0; i < 2; i++) { if (!D.facts[i][0]) continue; uint32_t col = i == 0 ? RGB(0xf5, 0xc5, 0x18) : WHITE_A(190);
        icon_draw(i == 0 ? "d_award" : "d_country", fx, y + (36 - is) / 2 - 4, is, i == 0 ? col : WHITE_A(150));
        fx += is + 10 + text_draw(D.facts[i], fx + is + 10, y, F_SMALL, i == 0 ? 600 : 500, col, 0, SB_W - PAD - fx - is - 10, ALIGN_START) + 34; }
      y += 40; }
    { tween_to(&CL.a, CL.line[0] ? 1 : 0, 0.35f, EASE_WEB); tween_tick(&CL.a, phDt); float ck = CL.a.v; /* the collection: its own line, fading in as it is known */
      if (ck > 0.01f && CL.line[0]) { float is = F_SMALL + 4, lw = text_width(CL.line, F_SMALL, 600, 0); gfx_opacity_push(ck); int pr = ph_pressed(PAD - 8, y - 4, lw + is + 30, 40);
        icon_draw("collection", PAD, y + (36 - is) / 2 - 4, is, WHITE_A(pr ? 255 : 190)); text_draw(CL.line, PAD + is + 10, y, F_SMALL, 600, WHITE_A(pr ? 255 : 210), 0, SB_W - 2 * PAD - is - 10, ALIGN_START);
        hit_act(PAD - 8, y - 4, lw + is + 30, 40, coll_open, 0, 0); gfx_opacity_pop(); y += 40 * ck; } }
    gfx_opacity_pop(); }
  y += 10;
  static char resLeft[64]; CwInfo ci; int resume = lib_cw_info(&D.it, &ci) && ci.pct > 0.01f && js_num(js_obj(lib_get(D.it.id), "state"), "timeOffset", 0) > 60000; /* (Resume past a minute in, as the TV) */ /* (known from the Library at once: Resume from the first frame, never Play turning into it) */
  if (resume) { char lf[64]; int lm = (int)ci.leftMin; if (lm >= 60) snprintf(lf, sizeof lf, "%dh%02d", lm / 60, lm % 60); else snprintf(lf, sizeof lf, "%d min", lm < 1 ? 1 : lm); /* short: "1h05", "25 min" */
    snprintf(resLeft, sizeof resLeft, "%s", lf); } else resLeft[0] = 0; /* (shown on the Resume pill itself: nothing above the pills comes and goes) */
  /* everything that can be done, as one row of pills that scrolls sideways: Play (or Resume) first and lit, then
     Download, My List, Trailer, Watched, Similar. All there from the start (no row that grows as things arrive) */
  { static Scroll pv; float h = 76, gap = 12, x = PAD - pv.pos, x0 = x; int hasTr = !D.gotMeta || trailer_available(D.meta, D.it.id);
    const char *ic[7] = { "play", "ph_down", lib_has(D.it.id) ? "ph_lib" : "ph_plus" /* (in the Library: its own sign, the bar's; the tick is Watched's alone) */, "trailer", lib_is_marked_watched(&D.it) ? "check" : "eye", "collection", "similar" };
    char rl[120]; snprintf(rl, sizeof rl, "%s%s%s", T("Resume"), resLeft[0] ? "  \xc2\xb7  " : "", resLeft); const char *lb[7] = { resume ? rl : T("Play"), T("Download"), T("My List"), T("Trailer"), T("Watched"), T("Collection"), T("Similar") };
    HitFn fn[7] = { resume ? resume_act : title_play, dl_act, list_act, trailer_act, seen_act, coll_open, similar_act };
    ph_drag(0, y - 6, SB_W, h + 12, &pv, 0); scroll_tick(&pv);
    static Tween onT[7]; static unsigned onG; if (onG != D.gen) { onG = D.gen; for (int q = 0; q < 7; q++) tween_set(&onT[q], (q == 2 && lib_has(D.it.id)) || (q == 4 && lib_is_marked_watched(&D.it))); } /* (opened: as they are, no animation) */
    for (int i = 0; i < 7; i++) { if (i == 3 && !hasTr) continue; if (i == 5 && !CL.key[0]) continue; /* (the collection: once it is known) */ int main = i == 0, on = (i == 2 && lib_has(D.it.id)) || (i == 4 && lib_is_marked_watched(&D.it));
      tween_to(&onT[i], on, 0.38f, EASE_OUT); tween_tick(&onT[i], phDt); float ok = onT[i].v; /* toggled: the colour fills in, the icon turns into the other one with a little pop */
      int bare = !main && SB_W <= SB_H && SB_W < 1000; /* a phone upright: the obvious ones by their icon alone (with room, sideways or on a tablet, their names too) */
      float w = bare ? h : text_width(lb[i], F_SMALL + 1, 800, 0) + 40 + 14 + 48; if (main) { static Tween mw; static unsigned mg; if (mg != D.gen || mw.v <= 0) { mg = D.gen; tween_set(&mw, w); } tween_to(&mw, w, 0.3f, EASE_WEB); tween_tick(&mw, phDt); w = mw.v; } /* (its words changing: it glides to its new width) */
      float hk = main ? mainHold : i == 1 ? dlHold : 0; float pk = press_k(x, y, w, h), ks = hk > 0 ? 1 - 0.05f * pk + 0.12f * hk * hk * (3 - 2 * hk) + 0.05f * pk : 1 - 0.05f * pk; gfx_scale_push(ks, ks, x + w / 2, y + h / 2); /* Play held: it swells as the hold fills (the streams come at the end) */
      if (hk > 0) gfx_shadow(x, y, w, h, h / 2, 34 * hk, col_alpha(ACC | 0xff000000u, 0.7f * hk)); /* (its glow in the theme) */
      if (main) gfx_rrect(x, y, w, h, h / 2, WHITE_A(245)); else { gfx_rrect(x, y, w, h, h / 2, WHITE_A(pk > 0 ? (int)(18 + 22 * pk) : 14)); if (ok > 0.01f) gfx_rrect(x, y, w, h, h / 2, col_alpha(ACC, 0.36f * ok)); gfx_ring(x, y, w, h, h / 2, 1, WHITE_A((int)(26 * (1 - ok)))); }
      uint32_t c = main ? RGB(14, 12, 20) : col_mix(WHITE_A(235), ACC3 | 0xff000000u, ok); float icx = bare ? x + w / 2 - 20 : x + 24, icy = y + h / 2 - 20;
      if (i == 2 || i == 4) { const char *offI = i == 2 ? "ph_plus" : "eye", *onI = i == 2 ? "ph_lib" : "check"; /* (in the Library: the bar's sign; the tick is Watched's alone) */ float pop = 1 + 0.22f * sinf(ok * (float)M_PI) * (onT[i].to != onT[i].v || (ok > 0.02f && ok < 0.98f)); gfx_scale_push(pop, pop, icx + 20, icy + 20);
        if (ok < 0.99f) icon_draw_rot(offI, icx, icy, 40, col_alpha(c, 1 - ok), 90 * ok); if (ok > 0.01f) icon_draw_rot(onI, icx, icy, 40, col_alpha(c, ok), -90 * (1 - ok)); gfx_scale_pop(); }
      else if (bare) icon_draw(ic[i], icx, icy, 40, c);
      if (!bare) { if (i != 2 && i != 4) icon_draw(ic[i], icx, icy, 40, c); text_draw(lb[i], x + 24 + 40 + 12, y + h / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, c, 0, 0, ALIGN_LEFT); }
      if (main && resume) { float bw = (w - 48) * 0.5f, bx = x + w / 2 - bw / 2; gfx_rrect(bx, y + h - 12, bw, 4, 2, BLACK_A(40)); gfx_rrect(bx, y + h - 12, bw * ci.pct, 4, 2, ACC); } /* how far, inside the pill */
      if (main && !D.gotMeta && D.openStreams) spinner(x + w - 26, y + h / 2, 12);
      if (main && best_on()) { static double hs; float pr; if (held_long(x, y, w, h, &hs, &pr)) open_streams_for_play(); mainHold = hold_ended() ? 0 : pr; /* best fit on: held, the streams */
        if (pr > 0) { float e = pr * pr * (3 - 2 * pr), o = 4 + 3 * e; gfx_ring(x - o, y - o, w + 2 * o, h + 2 * o, h / 2 + o, 2.5f + 1.5f * e, col_alpha(ACC | 0xff000000u, 0.35f + 0.65f * e)); } } /* (held: the theme's outline comes round it as it swells) */
      if (i == 1) { static double hs; float pr; if (held_long(x, y, w, h, &hs, &pr)) dl_streams(); dlHold = hold_ended() ? 0 : pr; /* held: the streams, to choose the one to download (it swells as Play does) */
        if (pr > 0) { float e = pr * pr * (3 - 2 * pr), o = 4 + 3 * e; gfx_ring(x - o, y - o, w + 2 * o, h + 2 * o, h / 2 + o, 2.5f + 1.5f * e, col_alpha(ACC | 0xff000000u, 0.35f + 0.65f * e)); } }
      gfx_scale_pop(); hit_act(x, y, w, h, fn[i], 0, 0); x += w + gap; }
    pv.max = (x - x0) - gap - (SB_W - 2 * PAD); if (pv.max < 0) pv.max = 0; y += h + 28;
    if (best_on()) { const char *hl = T("Plays the best stream. Hold Play to choose one"); float hw = SB_W - 2 * PAD - 36; int nl = text_wrap_count(hl, hw, 2, F_TINY, 600, 0); if (nl < 1) nl = 1; float lh = text_line_height(F_TINY); /* (two lines where a language needs them: never cut) */
      icon_draw("lib_sort", PAD, y - 14, 26, TXT3); text_draw_wrapped(hl, PAD + 36, y - 14 + 13 - lh / 2.0f, hw, 2, F_TINY, 600, TXT3, 0, ALIGN_START, lh); y += 30 + (nl - 1) * lh; } }
  { static Tween da; static unsigned g; if (g != D.gen) { g = D.gen; tween_set(&da, 0); } tween_to(&da, D.it.description[0] ? 1 : 0, 0.4f, EASE_CSS_EASE); tween_tick(&da, phDt);
    float dw = wide ? SB_W * 0.46f : SB_W - 2 * PAD; int nb = wide ? (ph_tabw() ? 3 : 2) : 4, n = nb; /* (a phone sideways: two lines, a tap opens the rest) */ if (D.it.description[0]) { int full = text_wrap_count(D.it.description, dw, 40, F_BODY, 400, 0); gfx_opacity_push(da.v); n = text_draw_wrapped(D.it.description, PAD, y, dw, D.descOpen ? 40 : nb, F_BODY, 400, WHITE_A(210), 0, ALIGN_START, 34); gfx_opacity_pop(); if (full > nb) hit_act(PAD, y, dw, n * 34, desc_act, 0, 0); if (!D.descOpen && n < nb) n = nb; }
    y += n * 34 + 18; } /* (four lines' room from the start: the story fades into it) */
  if (!D.gotMeta) { for (int i = 0; i < 4; i++) ph_circle(PAD + i * 136 + 60, y + 60, 60); y += 170 + 20; } /* (their room while the details come) */
  else if (D.ncast) { /* the people: faces that slide sideways */
    int roles = 0; for (int i = 0; i < D.ncast; i++) if (D.role[i][0]) roles = 1; float fw = 120, fh = roles ? 198 : 170, gap = 16; D.fv.max = D.ncast * (fw + gap) - gap + 2 * PAD - SB_W; scroll_tick(&D.fv); ph_drag(0, y, SB_W, fh, &D.fv, 0);
    for (int i = 0; i < D.ncast; i++) { float x = PAD + i * (fw + gap) - D.fv.pos; if (x > SB_W || x + fw < 0) continue;
      GLuint ft = D.face[i][0] ? img_get_shaped(D.face[i], (int)fw, (int)fw, fw / 2, 2) : 0; if (!ft || !ph_picture(ft, x, y, fw, fw, fw / 2, WHITE_A(255))) { if (D.face[i][0] || D.faceGot < D.ncast) ph_circle(x + fw / 2, y + fw / 2, fw / 2); else { gfx_circle(x + fw / 2, y + fw / 2, fw / 2, SURF2); icon_draw("ph_user", x + fw / 2 - 26, y + fw / 2 - 26, 52, WHITE_A(90)); } }
      { int nl = text_draw_wrapped(D.cast[i], x + fw / 2, y + fw + 10, fw + 10, 2, F_TINY, 600, WHITE_A(200), 0, ALIGN_CENTER, 22); if (D.role[i][0]) text_draw(D.role[i], x + fw / 2, y + fw + 12 + (nl > 1 ? 2 : 1) * 22, F_TINY - 2, 500, TXT3, 0, fw + 10, ALIGN_CENTER); } /* the part they play, faint, one line */
      { float pk = press_k(x, y, fw, fh); if (pk > 0) gfx_circle(x + fw / 2, y + fw / 2, fw / 2, WHITE_A((int)(40 * pk))); } hit_act(x, y, fw, fh, face_act, i, 0); }
    y += fh + 20; }
  if (is_series()) {
    { float ew = text_width(T("Episodes"), F_H2 - 2, 800, -0.3f); text_draw(T("Episodes"), PAD, y, F_H2 - 2, 800, TXT, -0.3f, 0, ALIGN_START); float hw = SB_W - 2 * PAD - ew - 24; /* the hint beside the heading, on two lines when it needs them */
      if (best_on()) { const char *hl = T("Hold an episode to choose its stream"); float lh = text_line_height(F_TINY); int nl = text_wrap_count(hl, hw, 2, F_TINY, 600, 0); text_draw_wrapped(hl, SB_W - PAD, y + (nl > 1 ? 2 : 14), hw, 2, F_TINY, 600, TXT3, 0, ALIGN_END, lh); } } y += 60;
    /* the seasons in a row, swiped across; the lit pill glides to the one picked, its episodes slide in */
    { static Scroll seaS; static Tween pX, pW, epA; static int seen = -1, lastCur = -1; float h = 64, gap = 12, x0 = PAD - seaS.pos, x = x0, onX = 0, onW = 0, ws[64]; char nm[64][40]; int n = D.ns < 64 ? D.ns : 64;
      ph_drag(0, y, SB_W, h, &seaS, 0); scroll_tick(&seaS);
      for (int i = 0; i < n; i++) { char nn[16]; snprintf(nn, sizeof nn, "%d", D.seasons[i].sn); snprintf(nm[i], 40, "%s", D.seasons[i].sn ? T1("Season {n}", "n", nn) : T("Specials")); ws[i] = text_width(nm[i], F_SMALL + 1, 700, 0) + 48;
        float pk = press_k(x, y, ws[i], h); gfx_rrect(x, y, ws[i], h, h / 2, WHITE_A(pk > 0 ? (int)(14 + 20 * pk) : 12)); if (i == D.cur) { onX = x - x0; onW = ws[i]; } hit_act(x, y, ws[i], h, season_tap, i, 0); x += ws[i] + gap; }
      seaS.max = (x - x0) - gap - (SB_W - 2 * PAD); if (seaS.max < 0) seaS.max = 0;
      if (seen != (int)D.gen) { seen = (int)D.gen; tween_set(&pX, onX); tween_set(&pW, onW); tween_set(&epA, 1); lastCur = D.cur; seaS.pos = clampf(onX - 60, 0, seaS.max); seaS.vel = 0; } /* opened: the season on shows */ else { tween_to(&pX, onX, 0.32f, EASE_WEB); tween_to(&pW, onW, 0.32f, EASE_WEB); }
      if (lastCur != D.cur) { lastCur = D.cur; tween_set(&epA, 0); tween_to(&epA, 1, 0.35f, EASE_OUT); } tween_tick(&pX, phDt); tween_tick(&pW, phDt); tween_tick(&epA, phDt); seasonK = epA.v;
      gfx_rrect(x0 + pX.v, y, pW.v, h, h / 2, WHITE_A(240));
      x = x0; for (int i = 0; i < n; i++) { float c = clampf(1 - fabsf((x - x0) - pX.v) / ws[i], 0, 1); text_draw(nm[i], x + ws[i] / 2, y + h / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, col_mix(WHITE_A(220), RGB(14, 12, 20), c), 0, 0, ALIGN_CENTER); x += ws[i] + gap; }
      y += h + 22; }
    Season *se = &D.seasons[D.cur < D.ns ? D.cur : 0];
    gfx_opacity_push(0.2f + 0.8f * seasonK); gfx_offset_push(70 * (1 - seasonK), 0); /* the season's episodes slide in */
    for (int k = se->first; k < se->first + se->n; k++) { Ep *e = &D.eps[k]; float tw = 236, th = 133, rh = th + 16;
      if (y + rh > 0 && y < SB_H) { { float pk = press_k(0, y - 8, SB_W, rh); if (pk > 0) gfx_rect(0, y - 8, SB_W, rh, WHITE_A((int)(16 * pk))); }
        const char *src = e->thumb; GLuint et = src[0] ? img_get_shaped(src, (int)tw, (int)th, 12, 1) : 0; int noStill = !src[0] || img_shaped_failed(src, (int)tw, (int)th, 12, 1);
        char bb[600]; const char *bu = sb_bg_small(&D.it, bb, sizeof bb); int unrel = ep_unaired(k);
        int hid = ep_hid(k), stillOk = hid && !noStill && spoil_blurable(src), bgOk = bu && *bu && spoil_blurable(bu), blurOk = stillOk || (hid && noStill && bgOk), upBlur = !blurOk && unrel && noStill && bgOk, shown = 0;
        if (blurOk) { spoil_picture(stillOk ? src : bu, PAD, y, tw, th, 12); spoil_mark(PAD + tw / 2, y + th / 2, 56, 0, 0); shown = 1; } /* not watched yet: its still soft (none of its own: the show's backdrop), the crossed eye on it (as the TV) */
        else if (upBlur) { spoil_picture(bu, PAD, y, tw, th, 12); spoil_mark(PAD + tw / 2, y + th / 2, 56, 0, 0); shown = 1; } /* not out yet, no still: the show's backdrop blurred, the eye and Upcoming on it (as the TV) */
        else if (!hid && et) { if (ph_picture(et, PAD, y, tw, th, 12, WHITE_A(255))) { shown = 1; spoil_reveal_over(e->id, src, PAD, y, tw, th, 12, PAD + tw / 2, y + th / 2, 56); } else spoil_reveal_wait(e->id); } /* (just shown clear: its blur fading off, the eye going) */
        if (!shown && (noStill || hid)) { GLuint bt = bu && *bu ? img_get_shaped(bu, (int)tw, (int)th, 12, 1) : 0; /* none of its own (or hidden with none to blur): the show's backdrop, dimmed (as the TV) */
          if (!bt || !ph_picture(bt, PAD, y, tw, th, 12, WHITE_A(110))) ph_box(PAD, y, tw, th, 12); }
        else if (!shown) ph_box(PAD, y, tw, th, 12);
        if (unrel) { const char *u = T("Upcoming"); float ph = 34, pw = text_width(u, F_TINY, 700, 0.2f) + 46, px = PAD + 10, py = y + 10; /* not out yet: the TV's tag, top left, a dot of the theme's colour */
          gfx_shadow(px, py + 3, pw, ph, ph / 2, 12, BLACK_A(110)); gfx_rrect(px, py, pw, ph, ph / 2, RGBA(14, 12, 20, 178)); gfx_rrect_v(px, py, pw, ph, ph / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(px, py, pw, ph, ph / 2, 1, WHITE_A(46));
          gfx_circle(px + 19, py + ph / 2, 4.5f, ACC2); text_draw(u, px + 32, py + ph / 2 - text_line_height(F_TINY) / 2.0f, F_TINY, 700, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); }

        if (D.watched && D.watched[e - D.eps]) { gfx_circle(PAD + tw - 22, y + 22, 16, ACC); icon_draw("check", PAD + tw - 32, y + 12, 20, ACC_FG); }
        CwInfo eci; if (lib_cw_info(&D.it, &eci) && !strcmp(eci.vid, e->id) && eci.pct > 0.01f) { gfx_rrect(PAD + 10, y + th - 12, tw - 20, 5, 2.5f, WHITE_A(80)); gfx_rrect(PAD + 10, y + th - 12, (tw - 20) * eci.pct, 5, 2.5f, ACC); }
        char nm[300]; snprintf(nm, sizeof nm, "%d. %s", e->ep, e->name[0] ? e->name : T("Episode")); float tx = PAD + tw + 20;
        int nl = text_draw_wrapped(nm, tx, y + 4, SB_W - PAD - tx - 70, 2, F_BODY, 700, TXT, 0, ALIGN_START, 32);
        if (e->date[0]) text_draw(e->date, tx, y + 8 + nl * 32, F_TINY, 500, WHITE_A(120), 0, 0, ALIGN_START);
        if (best_on()) { static double hs[400]; float pr = 0; if (k < 400 && held_long(0, y - 8, SB_W, rh, &hs[k], &pr)) { char nm[300]; snprintf(nm, sizeof nm, "S%d E%d  %s", e->sn, e->ep, e->name); ph_streams_pick(&D.it, e->id, nm); }
          if (pr > 0) gfx_rrect(PAD, y + th + 2, tw * pr, 4, 2, ACC | 0xff000000u); } /* (held: a line fills under its still) */
        hit_act(0, y - 8, SB_W, rh, ep_act, k, 0);
        { float pr = 0; int st = ph_dl_state(D.it.id, e->id, &pr); float bx = SB_W - PAD - 28, by = y + 30; /* its download: a button, a ring as it comes, a tick when kept */
          float pk = press_k(bx - 34, by - 34, 68, 68); gfx_circle(bx, by, 28, WHITE_A(pk > 0 ? (int)(18 + 24 * pk) : 12));
          if (st == DL_DONE) icon_draw("ph_check", bx - 18, by - 18, 36, ACC2);
          else if (st >= 0) { gfx_ring(bx - 22, by - 22, 44, 44, 22, 3, WHITE_A(40)); for (int q = 0; q <= 30; q++) { if (q > 30 * pr) break; float an = q * 12 * (float)M_PI / 180; gfx_circle(bx + 20.5f * sinf(an), by - 20.5f * cosf(an), 2, ACC2 | 0xff000000u); } }
          else icon_draw("ph_down", bx - 18, by - 18, 36, WHITE_A(200));
          if (st < 0 || st == DL_FAILED) hit_act(bx - 34, by - 34, 68, 68, ep_dl, k, 0); } }
      y += rh;
      { static float ovK[512]; static unsigned ovG; if (ovG != D.gen) { ovG = D.gen; for (int q = 0; q < 512; q++) ovK[q] = -1; } int want = e->ov[0] && !(spoil_mode() == 2 && ep_hid(k)); float *ok = k < 512 ? &ovK[k] : NULL; float kv = want;
        if (ok) { if (*ok < 0) *ok = (float)want; else *ok += (want - *ok) * fminf(1, phDt * 9); if (fabsf(*ok - want) < 0.002f) *ok = (float)want; kv = *ok; } /* (shown clear: its words open and fade in, as its still clears) */
        if (e->ov[0] && kv > 0.002f) { int n = text_wrap_count(e->ov, SB_W - 2 * PAD, 3, F_SMALL, 400, 0); float e3 = kv * kv * (3 - 2 * kv); if (y + n * 29 > 0 && y < SB_H) { gfx_clip_push(0, y, SB_W, (n * 29 + 26) * e3 + 1); gfx_opacity_push(e3); text_draw_wrapped(e->ov, PAD, y, SB_W - 2 * PAD, 3, F_SMALL, 400, WHITE_A(150), 0, ALIGN_START, 29); gfx_opacity_pop(); gfx_clip_pop(); } y += 14 + (n * 29 + 12) * e3; } else y += 14; } }
    gfx_offset_pop(); gfx_opacity_pop();
  }
  y += 30; to_reviews(y + D.v.pos); y += reviews_draw(y);
  D.v.max = y + D.v.pos - (SB_H - INS_B - BAR_H) + 30;
  { char tb[300]; snprintf(tb, sizeof tb, "\001%s", D.it.name); top_bar(tb, 1, D.v.pos - coverH + 220); }
}
static int key(int k) { if (k == K_CH_UP) { scroll_top(&D.v); return 1; } return 0; }
static const Item *focused(void) { return NULL; }
static int origin(float *x, float *y) { *x = SB_W / 2; *y = SB_H * 0.3f; return 1; }
static void phDetail_out(float k) { phone_out(draw, k); }
Screen phDetail = { enter, leave, key, tick, draw, focused, NULL, NULL, origin, phDetail_out };
/* what the card menu and the player ask of the title page */
const char *detail_id(void) { return SCR == SCR_DETAIL ? D.it.id : ""; }
void detail_open_streams(const char *vid) { for (int k = 0; k < D.ne; k++) if (!strcmp(D.eps[k].id, vid)) { ep_act(k, 0); return; } ph_streams_show(&D.it, vid, NULL); }
void detail_refresh_watched(void) { cJSON *lit = lib_get(D.it.id); if ((lit || tw_on()) && D.watched && D.meta) lib_ep_watched_map(lit, js_arr(D.meta, "videos"), D.watched); }
void detail_paint_lib(void) {}

/* ---- a title's trailer in the player (the Trailer action, the held card's menu): as the TV's title page does ---- */
#include "core/trailer.h"
#include "screens/player.h"
static struct { unsigned tok; char type[16], id[64]; int busy; } TR;
static void on_trailer(const Trailer *t, const char *err, void *ud) {
  (void)err; (void)ud; TR.busy = 0; TR.tok = 0;
  fprintf(stderr, "[trailer] found: %s, the player %s\n", t ? "yes" : "no", player_waiting() ? "waits" : "does not wait");
  if (!player_waiting()) return;
  if (!t) { player_close(); toast(T("No trailer available"), 2600, NULL); return; }
  Stream s; memset(&s, 0, sizeof s); snprintf(s.url, sizeof s.url, "%s", t->url); snprintf(s.name, sizeof s.name, "%s", T(t->name[0] ? t->name : "Trailer"));
  if (t->alt[0]) player_fallback(t->alt);
  player_trailer_subs(t->subs, t->nsubs);
  app_play(&s, "other", TR.id, TR.id, 0);
}
static void trailer_go(const cJSON *m) { TR.tok = trailer_find_meta(m, TR.id, 0, on_trailer, NULL); }
static void on_trailer_meta(cJSON *m, void *ud) { if ((unsigned)(uintptr_t)ud != 1 || !TR.busy) return; trailer_go(m); }
void detail_trailer(const char *type, const char *id, const char *name) {
  (void)name; if (TR.busy && TR.tok) trailer_cancel(TR.tok);
  snprintf(TR.type, sizeof TR.type, "%s", type); snprintf(TR.id, sizeof TR.id, "%s", id); TR.busy = 1; TR.tok = 0;
  player_prepare("other", id, id, T("Trailer"));
  cJSON *m = sb_meta_cached(type, id); if (m) trailer_go(m); else sb_meta(type, id, on_trailer_meta, (void *)(uintptr_t)1);
}
