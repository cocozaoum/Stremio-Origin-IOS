#include "core/traktwatch.h"
#include <time.h>
/* The sheets: the streams for a film or an episode (by addon, as they come, the best one marked), and the
   menu a held card opens (Play, from the beginning, Details, Library, watched, Trailer). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/best.h"
#include "core/trailer.h"
#include "core/rating.h"
#include <stdint.h>
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "screens/detail.h"
#include "core/live.h"
#include "core/notify.h"

/* ---- streams ---- */
#define MAXS 400
static struct { Item it; char vid[96], ep[300]; Stream *s; int n; char grp[MAXS][96]; unsigned tok; int pending, count, done; Scroll v; char addons[12][96]; int na, only; int best; } S;
static void part(StreamGroup *g, int gi, void *ud) { (void)gi; (void)ud; for (int i = 0; i < g->n && S.n < MAXS; i++) { S.s[S.n] = g->items[i]; snprintf(S.grp[S.n], 96, "%s", g->name); S.n++; } free(g->items); }
static void done(int total, void *ud) { (void)total; (void)ud; S.done = 1; S.tok = 0; }
static void ans(int pending, int count, void *ud) { (void)ud; S.pending = pending; S.count = count; }
static double rowAt[MAXS]; static int stFresh; /* rowAt: when each stream's card first came (0 not yet, -1 shown with its list: no motion of its own); stFresh: a new list (the tabs' widths set, not glided) */
static int dlMode; /* the sheet opened to download: a pick downloads instead of playing */
static int stPop = -1, stHoldSkip, stPopArmed; /* stPopArmed: the finger that opened the menu has lifted (only then its rows answer) */ static double stHoldAt; /* stHoldSkip: the finger that opened the list (a held Play) is still down */ /* a held card's menu (its stream), when the hold opened it */
static void play_act(int i, int b) { (void)b; if (i < 0 || i >= S.n || stPop >= 0 || app_now() - stHoldAt < 0.6) return; /* (the end of a hold is no tap) */ if (dlMode) { Stream st2 = S.s[i]; ph_sheet_close(); ph_dl_add(&S.it, S.vid, S.ep, &st2); return; } Stream st = S.s[i]; sb_remember_stream(S.it.id, S.vid, &st); ph_sheet_close(); app_play(&st, S.it.type, S.it.id, S.vid, 0); }
static void ext_open(int i) { Stream *st = &S.s[i]; char t[400]; snprintf(t, sizeof t, "%s%s%s", S.it.name, S.ep[0] ? "  " : "", S.ep); sb_remember_stream(S.it.id, S.vid, st);
  if (st->p2p || ph_ext_player_on()) { ph_play_external(st, t); return; } /* (a torrent: through the engine's own address) */
  if (!pplat_open_external(st->url, t)) toast(T("No video player app on this phone."), 2600, NULL); }
static void ext_act(int i, int b) { (void)b; if (i < 0 || i >= S.n || stPop >= 0 || app_now() - stHoldAt < 0.6) return; ext_open(i); }
/* its link, to use anywhere: the address as it is; a torrent as a magnet link (its hash, its trackers) */
static void copy_link(const Stream *st) { char m[3000];
  if (st->p2p) { const char *p = st->url + 6; int n = 0; while (n < 40 && isxdigit((unsigned char)p[n])) n++; int o = snprintf(m, sizeof m, "magnet:?xt=urn:btih:%.*s", n, p);
    const char *q = strchr(st->url, '?'); for (; q && (q = strstr(q, "tr=")) && o < (int)sizeof m - 8; ) { const char *e = strchr(q, '&'); int l = e ? (int)(e - q) : (int)strlen(q); o += snprintf(m + o, sizeof m - o, "&%.*s", l, q); q += l; } }
  else snprintf(m, sizeof m, "%s", st->url);
  SDL_SetClipboardText(m); pplat_haptic_k(HK_TICK, 0.6f); toast(T("Link copied"), 1600, NULL); }
static void pop_act(int k, int b) { (void)b; if (!stPopArmed) return; int i = stPop; stPop = -1; if (i < 0 || i >= S.n) return; Stream st = S.s[i];
  if (k == 0) { stHoldAt = 0; play_act(i, 0); } else if (k == 1) ext_open(i); else if (k == 2) copy_link(&st); else if (k == 3) { ph_sheet_close(); ph_dl_add(&S.it, S.vid, S.ep, &st); } }
static void pop_close(int a, int b) { (void)a; (void)b; if (stPopArmed) stPop = -1; }
static int stPicked; /* a tab tapped: it stays */
static void st_cast_act(int a, int b) { (void)a; (void)b; cast_open(); }
static void only_act(int i, int b) { (void)b; stPicked = 1; if (i == S.only) return; S.only = i; S.v.pos = 0; S.v.vel = 0; }
/* a stream's lines as its addon wrote them (people shape them in the addon's settings) */
/* the streams: the title's logo in the middle, a tab per addon under it (the lit pill gliding), and the chosen
   addon's streams, each a card with its name and its lines as the addon wrote them */
static Item ep_item_of(void) { Item it; memset(&it, 0, sizeof it); snprintf(it.id, sizeof it.id, "%s", S.it.id); snprintf(it.type, sizeof it.type, "%s", S.it.type); snprintf(it.videoId, sizeof it.videoId, "%s", S.vid); it.ep = 1; return it; }
static void ep_seen_act(int a, int b) { (void)a; (void)b; Item it = ep_item_of(); lib_toggle_watched(&it, js_arr(sb_meta_cached(S.it.type, S.it.id), "videos")); }
/* a stream's lines made plain: its name (without the addon's own name), its words, and what the emoji said
   (Torrentio's 👤 seeds, 💾 size, ⚙️ source, flags) as one line of words; emoji the font has not are left out */
typedef struct { char name[160], title[600], stats[200]; } StInfo;
static int u8len(unsigned char c) { return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4; }
static void st_info(const Stream *st, StInfo *o) { memset(o, 0, sizeof *o);
  { const char *t = st->name; char ln[160]; int k = 0; for (;; t++) { if (*t == '\n' || !*t) { ln[k] = 0; while (k && ln[k - 1] == ' ') ln[--k] = 0; if (k && strcmp(ln, st->addon)) { size_t L = strlen(o->name); snprintf(o->name + L, sizeof o->name - L, "%s%s", L ? " " : "", ln); } k = 0; if (!*t) break; } else if (k < 158) ln[k++] = *t; }
    if (!o->name[0]) snprintf(o->name, sizeof o->name, "%s", st->addon); }
  const char *src = st->title[0] ? st->title : st->filename; char seeds[24] = "", size[32] = "", from[48] = "", flags[48] = ""; char *cur = NULL; int cap = 0, cl = 0; size_t tl = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p;) { int n = u8len(*p);
    if (n == 4 && p[0] == 0xf0 && p[1] == 0x9f && p[2] == 0x91 && p[3] == 0xa4) { cur = seeds; cap = sizeof seeds; cl = 0; p += 4; continue; }      /* 👤 */
    if (n == 4 && p[0] == 0xf0 && p[1] == 0x9f && p[2] == 0x92 && p[3] == 0xbe) { cur = size; cap = sizeof size; cl = 0; p += 4; continue; }       /* 💾 */
    if (n == 3 && p[0] == 0xe2 && p[1] == 0x9a && p[2] == 0x99) { cur = from; cap = sizeof from; cl = 0; p += 3; continue; }                      /* ⚙ */
    if (n == 4 && p[0] == 0xf0 && p[1] == 0x9f && p[2] == 0x87 && p[3] >= 0xa6 && p[3] <= 0xbf && p[4] == 0xf0 && p[6] == 0x87) { size_t L = strlen(flags); if (L + 4 < sizeof flags) snprintf(flags + L, sizeof flags - L, "%s%c%c", L ? " " : "", 'A' + p[3] - 0xa6, 'A' + p[7] - 0xa6); p += 8; cur = NULL; continue; } /* a flag: its two letters */
    if (n == 4 || (n == 3 && p[0] == 0xef && p[1] == 0xb8)) { p += n; continue; } /* other emoji, variation selectors: left out */
    if (*p == '\n') { cur = NULL; if (tl && o->title[tl - 1] != ' ') { o->title[tl++] = ' '; o->title[tl] = 0; } p++; continue; }
    if (cur) { if (cl + n < cap - 1 && !(cl == 0 && *p == ' ')) { memcpy(cur + cl, p, n); cl += n; cur[cl] = 0; } p += n; continue; }
    if (tl + n < sizeof o->title - 1) { memcpy(o->title + tl, p, n); tl += n; o->title[tl] = 0; } p += n; }
  for (char *f[3] = { seeds, size, from }, **q = f; q < f + 3; q++) { size_t L = strlen(*q); while (L && (*q)[L - 1] == ' ') (*q)[--L] = 0; }
  while (tl && o->title[tl - 1] == ' ') o->title[--tl] = 0;
  size_t L = 0; if (seeds[0]) L += snprintf(o->stats + L, sizeof o->stats - L, "%s", T1("{n} seeds", "n", seeds));
  if (size[0]) L += snprintf(o->stats + L, sizeof o->stats - L, "%s%s", L ? "  \xc2\xb7  " : "", size);
  if (from[0]) L += snprintf(o->stats + L, sizeof o->stats - L, "%s%s", L ? "  \xc2\xb7  " : "", from);
  if (flags[0] && L < sizeof o->stats) snprintf(o->stats + L, sizeof o->stats - L, "%s%s", L ? "  \xc2\xb7  " : "", flags); }
static void st_draw(float a) { ph_fly_ready(&S.it);
  float h = SB_H - INS_T - 60, y = sheet_panel(h, a), cw = SB_W - 2 * PAD;
  /* the title: its logo, else its name */
  { char lb[600]; const char *lg = sb_logo_of(&S.it, lb, sizeof lb); float lw = SB_W * 0.62f, lh = !dlMode && strchr(S.vid, ':') ? 92 : 120, asp = 0; /* (an episode: a little smaller, its Watched pill under it never touches it) */ GLuint t = lg && *lg ? img_get(lg, (int)lw, (int)lh, &asp) : 0;
    if (t && asp > 0) { float w = lh * asp, hh = lh; if (w > lw) { w = lw; hh = w / asp; } gfx_image(t, SB_W / 2 - w / 2, y + 40 + (lh - hh) / 2, w, hh, 0, WHITE_A(255), 0, 0, 1, 1); }
    else text_draw(S.it.name, SB_W / 2, y + 40 + lh / 2 - text_line_height(F_H1) / 2.0f, F_H1, 800, TXT, -0.4f, cw, ALIGN_CENTER); }
  { static char w[200]; static Tween wA; static double openAt; if (stFresh) { w[0] = 0; tween_set(&wA, 0); openAt = app_now(); } int waitOn = !S.done && S.pending > 0 && app_now() - openAt > 1.2; /* (said once the wait is noticed, as the TV: a quick answer never flashes it) */
    if (waitOn) { char n1[12]; snprintf(n1, 12, "%d", S.pending); snprintf(w, sizeof w, "%s", S.pending == 1 ? T("1 add-on left") : T1("{n} add-ons left", "n", n1)); } tween_to(&wA, waitOn ? 1 : 0, 0.35f, EASE_CSS_EASE); tween_tick(&wA, phDt); if (wA.v < 0.01f) w[0] = 0; /* (the waiting words fade out as the last addon answers; kept as they were meanwhile) */
    char line[400]; snprintf(line, sizeof line, "%s%s%s%s%s", dlMode ? T("Choose what to download") : "", dlMode && (S.ep[0] || w[0]) ? "  \xc2\xb7  " : "", S.ep, S.ep[0] && w[0] ? "  \xc2\xb7  " : "", w); int epPill = !dlMode && strchr(S.vid, ':'); /* an episode: its line at the left, the Watched pill at its right (never over the logo) */
    if (line[0]) { gfx_opacity_push(w[0] && !dlMode && !S.ep[0] ? wA.v : 1); if (epPill) text_draw(line, PAD, y + 178 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 600, TXT2, 0, cw - 300, ALIGN_START); else text_draw(line, SB_W / 2, y + 172, F_SMALL, 600, TXT2, 0, cw, ALIGN_CENTER); gfx_opacity_pop(); } }
  /* the cast button: on a TV instead (lit while connected: what is picked here plays there) */
  { int cs = cast_icon_state(); float r = 34, cx = SB_W - PAD - r, cy = y + 66; int pr = ph_pressed(cx - r, cy - r, 2 * r, 2 * r); if (cs == 0) gfx_opacity_push(0.5f);
    gfx_circle(cx, cy, r, WHITE_A(pr ? 34 : 16)); gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 1, WHITE_A(30)); icon_draw(cs == 3 ? "ph_cast_on" : "ph_cast", cx - 18, cy - 18, 36, WHITE_A(cs == 3 ? 245 : 220)); /* (connected: the TV-connected icon, nothing else) */ if (cs == 0) gfx_opacity_pop();
    if (cs == 2) { float ph = fmodf((float)app_now(), 1.2f) / 1.2f, rr = r + 4 + 12 * ph; gfx_ring(cx - rr, cy - rr, 2 * rr, 2 * rr, rr, 2.5f, col_alpha(ACC2 | 0xff000000u, 0.7f * (1 - ph))); }
    hit_act(cx - r - 10, cy - r - 10, 2 * r + 20, 2 * r + 20, st_cast_act, 0, 0); }
  /* an episode's list: its Watched pill, on the line under the logo, at the right */
  if (!dlMode && strchr(S.vid, ':')) { Item it = ep_item_of(); int on = lib_is_marked_watched(&it); static Tween wt; tween_to(&wt, on, 0.3f, EASE_WEB); tween_tick(&wt, phDt);
    const char *lb = T(on ? "Watched" : "Mark as watched"); float ph = 56, pw = text_width(lb, F_SMALL, 700, 0) + 44 + 34, px = SB_W - PAD - pw, py = y + 150; float pk = press_k(px, py, pw, ph), sc = 1 - 0.05f * pk;
    gfx_scale_push(sc, sc, px + pw / 2, py + ph / 2); gfx_rrect(px, py, pw, ph, ph / 2, col_mix(WHITE_A(22), ACC | 0xff000000u, wt.v)); gfx_ring(px, py, pw, ph, ph / 2, 1, WHITE_A(30));
    icon_draw(on ? "ph_check" : "eye", px + 18, py + ph / 2 - 14, 28, on ? ACC_FG : WHITE_A(230)); text_draw(lb, px + 54, py + ph / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, on ? ACC_FG : WHITE_A(235), 0, 0, ALIGN_START);
    gfx_scale_pop(); hit_act(px, py, pw, ph, ep_seen_act, 0, 0); }
  /* the tabs: every addon, with how many it found */
  int cnt[12] = { 0 }; for (int i = 0; i < S.n; i++) for (int k = 0; k < S.na; k++) if (!strcmp(S.s[i].addon, S.addons[k])) { cnt[k]++; break; }
  if (S.only < 0 || S.only >= S.na) { S.only = 0; if (S.best >= 0) for (int k = 0; k < S.na; k++) if (!strcmp(S.s[S.best].addon, S.addons[k])) S.only = k; } /* the best fit's addon first */
  if (!stPicked && !cnt[S.only]) for (int k = 0; k < S.na; k++) if (cnt[k]) { S.only = k; break; } /* never an empty tab while another has streams */
  float ty = y + 218, th = 64; static Scroll tv; static Tween tX, tW; static int lastTab = -1, dir = 1; static Tween listA;
  { float x0 = PAD - tv.pos, x = x0, ws[12], onX = 0, onW = 0; char l[12][120]; ph_drag(0, ty, SB_W, th, &tv, 0); scroll_tick(&tv); gfx_clip_push(0, ty - 4, SB_W, th + 8);
    static Tween twT[12]; float sp[12];
    for (int k = 0; k < S.na; k++) { snprintf(l[k], 120, "%s  %d", S.addons[k], cnt[k]); float base = text_width(l[k], F_SMALL + 1, 700, 0) + 48, want = base + (!S.done && !cnt[k] ? 30 : 0); /* its width glides as its count comes and its spinner goes */
      if (stFresh || twT[k].v <= 0) tween_set(&twT[k], want); else if (twT[k].to != want) tween_to(&twT[k], want, 0.3f, EASE_WEB); tween_tick(&twT[k], phDt); ws[k] = twT[k].v; sp[k] = clampf((ws[k] - base) / 30, 0, 1);
      gfx_rrect(x, ty, ws[k], th, th / 2, WHITE_A(12)); if (k == S.only) { onX = x - x0; onW = ws[k]; } hit_act(x, ty, ws[k], th, only_act, k, 0); x += ws[k] + 10; }
    tv.max = (x - x0) - 10 - cw; if (tv.max < 0) tv.max = 0;
    if (a < 0.9f || tW.v <= 0) { tween_set(&tX, onX); tween_set(&tW, onW); } else { tween_to(&tX, onX, 0.32f, EASE_WEB); tween_to(&tW, onW, 0.32f, EASE_WEB); } tween_tick(&tX, phDt); tween_tick(&tW, phDt);
    if (onW > 0) gfx_rrect(x0 + tX.v, ty, tW.v, th, th / 2, col_alpha(ACC, 0.85f));
    x = x0; for (int k = 0; k < S.na; k++) { float c = clampf(1 - fabsf((x - x0) - tX.v) / ws[k], 0, 1); text_draw(l[k], x + 24, ty + th / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, col_mix(WHITE_A(220), ACC_FG, c), 0, 0, ALIGN_LEFT); if (sp[k] > 0.01f) { gfx_opacity_push(sp[k]); spinner(x + ws[k] - 30, ty + th / 2, 10); gfx_opacity_pop(); } x += ws[k] + 10; }
    gfx_clip_pop(); }
  if (lastTab != S.only) { dir = S.only > lastTab ? 1 : -1; if (lastTab >= 0) { tween_set(&listA, 0); tween_to(&listA, 1, 0.32f, EASE_WEB); } else tween_set(&listA, 1); lastTab = S.only; } tween_tick(&listA, phDt);
  /* the chosen addon's streams */
  float top = ty + th + 22, bot = SB_H - INS_B - 22;
  gfx_clip_push(0, top, SB_W, bot - top); ph_drag(0, top, SB_W, bot - top, &S.v, 1); scroll_tick(&S.v);
  gfx_offset_push(dir * 80 * (1 - listA.v), 0); gfx_opacity_push(0.15f + 0.85f * listA.v);
  float ry = top - S.v.pos; int shown = 0, newN = 0; const char *want = S.only >= 0 && S.only < S.na ? S.addons[S.only] : ""; double now = app_now();
  int anyHere = 0; for (int i = 0; i < S.n && !anyHere; i++) anyHere = !strcmp(S.s[i].addon, want);
  { static Tween skA; static int lastOnly = -2; int wantSk = !anyHere && !S.done; if (stFresh || lastOnly != S.only) { tween_set(&skA, wantSk); lastOnly = S.only; } else { tween_to(&skA, wantSk, 0.3f, EASE_CSS_EASE); } tween_tick(&skA, phDt);
    if (skA.v > 0.01f) { gfx_opacity_push(skA.v); for (int q = 0; q < 3; q++) ph_box(PAD - 6, top + q * 124 - S.v.pos, cw + 12, 110, 22); gfx_opacity_pop(); } } /* the placeholders fade out under the cards as they come (never gone in one frame) */
  for (int i = 0; i < S.n; i++) { Stream *s2 = &S.s[i]; if (strcmp(s2->addon, want)) continue; shown++;
    if (rowAt[i] == 0) rowAt[i] = stFresh || listA.v < 0.99f || a < 0.99f ? -1 : now + 0.05 * (newN < 8 ? newN++ : 8); /* a card that came while its list was in view: in after the one before it */
    float rk = rowAt[i] < 0 ? 1 : ease_apply(EASE_OUT, clampf((float)((now - rowAt[i]) / 0.34), 0, 1)); if (rk < 0.999f) { gfx_opacity_push(rk); gfx_offset_push(0, 26 * (1 - rk)); }
    StInfo si; st_info(s2, &si); float rx = PAD - 6, rw = cw + 12, iw = rw - 40 - 84; int best = i == S.best;
    int nt = si.title[0] ? text_wrap_count(si.title, iw, 4, F_TINY + 1, 500, 0) : 0; float lh = 30, rh = 22 + 40 + nt * lh + (si.stats[0] ? 40 : 0) + 18; if (rh < 104) rh = 104;
    if (ry + rh > top - 10 && ry < bot + 10) {
      float pk = press_k(rx, ry, rw - 84, rh), sc = 1 - 0.015f * pk; gfx_scale_push(sc, sc, rx + rw / 2, ry + rh / 2);
      gfx_rrect(rx, ry, rw, rh, 22, best ? col_alpha(ACC, 0.18f) : WHITE_A(pk > 0 ? (int)(10 + 20 * pk) : 8)); if (best) gfx_ring(rx, ry, rw, rh, 22, 1.5f, col_alpha(ACC2, 0.7f));
      /* its name, then its tags on the same line (never over the words) */
      float x = rx + 20, y1 = ry + 22, nw = text_draw(si.name, x, y1 + 18 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, WHITE_A(best ? 255 : 240), 0, iw - 200, ALIGN_START); x += nw + 14;
      if (s2->p2p) { float bw = text_width("P2P", F_TINY, 800, 0.5f) + 20; gfx_rrect(x, y1 + 3, bw, 30, 15, col_alpha(ACC, 0.4f)); text_draw("P2P", x + 10, y1 + 18 - text_line_height(F_TINY) / 2.0f, F_TINY, 800, ACC3, 0.5f, 0, ALIGN_LEFT); x += bw + 8; }
      if (best) { const char *bl = T("Best fit"); float bw = text_width(bl, F_TINY, 800, 0) + 20; gfx_rrect(x, y1 + 3, bw, 30, 15, ACC); text_draw(bl, x + 10, y1 + 18 - text_line_height(F_TINY) / 2.0f, F_TINY, 800, ACC_FG, 0, 0, ALIGN_LEFT); }
      float ty2 = y1 + 46; if (nt) { text_draw_wrapped(si.title, rx + 20, ty2, iw, 4, F_TINY + 1, 500, WHITE_A(200), 0, ALIGN_START, lh); ty2 += nt * lh; }
      if (si.stats[0]) text_draw(si.stats, rx + 20, ty2 + 8, F_TINY, 700, TXT3, 0, iw, ALIGN_START);
      gfx_scale_pop(); hit_act(rx, ry, rw - 84, rh, play_act, i, 0);
      { float fx, fy, fx0, fy0; int held; static int fired; int down = ph_finger(&fx, &fy, &fx0, &fy0, &held); if (!down) { fired = 0; stHoldSkip = 0; } if (stHoldSkip) fired = 1; /* a hold on the card: its menu */
        if (down && held && !fired && stPop < 0 && fx0 >= rx && fx0 < rx + rw && fy0 >= ry && fy0 < ry + rh && fy0 > top && fy0 < bot) { fired = 1; stPop = i; stPopArmed = 0; stHoldAt = app_now(); pplat_haptic_k(HK_POP, 0.8f); } }
      float ex = rx + rw - 64; if (ph_pressed(ex - 12, ry, 80, rh)) gfx_circle(ex + 22, ry + rh / 2, 32, WHITE_A(22)); icon_draw("ph_ext", ex, ry + rh / 2 - 22, 44, WHITE_A(170)); hit_act(ex - 14, ry, 84, rh, ext_act, i, 0); }
    if (rk < 0.999f) { gfx_offset_pop(); gfx_opacity_pop(); }
    ry += rh + 10; }
  stFresh = 0;
  if (!shown) { if (!S.done) { ry = top + 380; } else text_draw_wrapped(T("Nothing from this addon."), PAD + 8, top + 30, cw, 3, F_BODY, 500, TXT2, 0, ALIGN_START, 34); }
  gfx_opacity_pop(); gfx_offset_pop(); S.v.max = ry + S.v.pos - bot + 20; gfx_clip_pop();
  /* the held card's menu: over the list, on the bar's liquid glass */
  { static Tween pA; static int last = -1; if (stPop >= 0) last = stPop; tween_to(&pA, stPop >= 0 ? 1 : 0, stPop >= 0 ? 0.24f : 0.16f, EASE_WEB); tween_tick(&pA, phDt);
    { float a1, b1, c1, d1; int h1; if (!ph_finger(&a1, &b1, &c1, &d1, &h1)) stPopArmed = 1; } /* (the hold's own lift is no tap on the menu) */
    if (pA.v > 0.01f && last >= 0 && last < S.n) { const Stream *st = &S.s[last]; StInfo si; st_info(st, &si);
      const char *lb[4] = { T("Play"), ph_ext_player_on() ? T("Play in your player") : T("Play in another app"), st->p2p ? T("Copy magnet link") : T("Copy link"), T("Download") }; const char *ic[4] = { "p_play", "ph_ext", "st_link", "ph_down" }; int n = dlMode ? 3 : 4;
      float mw = SB_W - 2 * PAD - 40, ih = 92, mh = 110 + n * ih + 16, mx = (SB_W - mw) / 2, my = SB_H / 2 - mh / 2, k = pA.v;
      /* the page behind: frosted and dimmed (calm), then the card on the bar's liquid glass over it */
      { extern void ph_glass_grab(void); ph_glass_grab(); gfx_opacity_push(k); ph_glass(0, 0, SB_W, SB_H, 0, RGBA(8, 7, 12, 150)); gfx_opacity_pop(); }
      gfx_opacity_push(k); hit_shield(0, 0, SB_W, SB_H); hit_act(0, 0, SB_W, SB_H, pop_close, 0, 0);
      gfx_scale_push(0.92f + 0.08f * k, 0.92f + 0.08f * k, SB_W / 2, my + mh / 2);
      extern void ph_glass_grab_liquid(void); extern void ph_liquid_bubble(float x, float y, float w, float h, float r, float press); ph_glass_grab_liquid(); ph_liquid_bubble(mx, my, mw, mh, 40, 0); gfx_rrect(mx, my, mw, mh, 40, RGBA(22, 20, 30, 150)); gfx_ring(mx, my, mw, mh, 40, 1, WHITE_A(34));
      text_draw(si.name, mx + 32, my + 30, F_BODY, 800, TXT, 0, mw - 64, ALIGN_START); if (si.title[0]) text_draw(si.title, mx + 32, my + 70, F_TINY + 1, 500, TXT3, 0, mw - 64, ALIGN_START);
      gfx_rect(mx + 28, my + 104, mw - 56, 1, WHITE_A(26)); /* (under its name) */
      for (int q = 0; q < n; q++) { float iy = my + 110 + q * ih; float pk = press_k(mx + 12, iy, mw - 24, ih - 8); if (pk > 0) gfx_rrect(mx + 12, iy, mw - 24, ih - 8, 24, WHITE_A((int)(26 * pk)));
        icon_draw(ic[q], mx + 34, iy + (ih - 8) / 2 - 20, 40, WHITE_A(230)); text_draw(lb[q], mx + 96, iy + (ih - 8) / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 650, TXT, 0, mw - 130, ALIGN_START);
        hit_act(mx + 12, iy, mw - 24, ih - 8, pop_act, q, 0); }
      gfx_scale_pop(); gfx_opacity_pop(); } }
}
static void st_gone(void) { if (S.tok) sb_streams_cancel(S.tok); S.tok = 0; }
static const PhSheet stSheet = { st_draw, NULL, st_gone };
static int stPick; /* the list asked for itself (held Play, Streams, a download): never skipped for a download already here */
void ph_streams_pick(const Item *it, const char *vid, const char *ep) { stPick = 1; ph_streams_show(it, vid, ep); }
void ph_streams_download(const Item *it, const char *vid, const char *ep) { stPick = 1; ph_streams_show(it, vid, ep); dlMode = 1; }
void ph_streams_show(const Item *it, const char *vid, const char *epLabel) { int pick = stPick; stPick = 0;
  if (!pick && sb_local_stream) { Stream ls; if (sb_local_stream(it->id, vid, &ls)) { app_play(&ls, it->type, it->id, vid, 0); return; } } /* a download of it (done, or coming in order): plays at once */
  dlMode = 0;
  if (S.tok) sb_streams_cancel(S.tok); if (!S.s) S.s = malloc(MAXS * sizeof(Stream));
  S.it = *it; snprintf(S.vid, sizeof S.vid, "%s", vid); snprintf(S.ep, sizeof S.ep, "%s", epLabel ? epLabel : ""); S.n = 0; S.done = 0; S.pending = S.count = 0; memset(&S.v, 0, sizeof S.v); S.only = -1; S.best = -1; stPicked = 0; stHoldSkip = 1; stPop = -1; memset(rowAt, 0, sizeof rowAt); stFresh = 1;
  S.na = sb_stream_addons(it->type, vid, S.addons, 12);
  ph_sheet_open(&stSheet); S.tok = sb_streams_ex(it->type, vid, part, done, ans, NULL);
}

/* ---- the held card's menu ---- */
#include "screens/player.h"
/* Play from a card: where it was left (its stream again), else the best fit at once when that is on, else the streams
   to pick from; a series goes on with the episode it was at, else its first */
static void menu_play_(const Item *it, int pick);
static void menu_play(const Item *it) { menu_play_(it, 0); }
static void menu_streams(const Item *it) { menu_play_(it, 1); }
static void menu_play_(const Item *it, int pick) {
  if (it->cw && !pick) { Stream st; if (sb_resume_stream(it->id, it->videoId, &st)) { app_play(&st, it->type, it->id, it->videoId, 0); return; } }
  char vid[96] = "", ep[300] = ""; CwInfo ci;
  if (it->videoId[0]) snprintf(vid, sizeof vid, "%s", it->videoId); else if (lib_cw_info(it, &ci) && ci.vid[0]) snprintf(vid, sizeof vid, "%s", ci.vid);
  else if (!strcmp(it->type, "series")) { const cJSON *m = sb_meta_cached(it->type, it->id), *v; int bs = 1 << 30, be = 1 << 30; cJSON_ArrayForEach(v, js_arr(m, "videos")) { int sn = (int)js_num(v, "season", 0), en = (int)js_num(v, "episode", 0); if (sn > 0 && (sn < bs || (sn == bs && en < be))) { bs = sn; be = en; snprintf(vid, sizeof vid, "%s", js_str(v, "id", "")); snprintf(ep, sizeof ep, "S%d E%d  %s", sn, en, js_str(v, "name", js_str(v, "title", ""))); } }
    if (!vid[0]) { DetailParams p; memset(&p, 0, sizeof p); p.it = *it; app_go(SCR_DETAIL, &p, 0); return; } } /* (its episodes not known yet: its page) */
  else snprintf(vid, sizeof vid, "%s", it->id);
  if (best_on() && !pick) { player_choose_best(it->type, it->id, vid); return; }
  if (pick) ph_streams_pick(it, vid, ep[0] ? ep : (it->epName[0] ? it->epName : NULL)); else ph_streams_show(it, vid, ep[0] ? ep : (it->epName[0] ? it->epName : NULL));
}
static struct { Item it; int n; struct { const char *icon; char label[96]; int what; } a[16]; double openAt; } M; /* openAt: the hold that opened it (its finger's lift picks nothing) */
enum { A_PLAY, A_RESTART, A_DETAILS, A_LIB, A_SEEN, A_TRAILER, A_STREAMS, A_REVIEWS, A_PICK, A_DL, A_DISMISS, A_CHPLAY, A_CHSTREAMS, A_CHPIN, A_CHFORGET };
static void m_add(int what, const char *icon, const char *label) { if (M.n < 16) { M.a[M.n].what = what; M.a[M.n].icon = icon; snprintf(M.a[M.n].label, 96, "%s", label); M.n++; } }
static void m_act(int i, int b) { (void)b; if (i < 0 || i >= M.n) return; if (ph_last_press() < M.openAt) return; /* the finger that held the card, lifting */ Item it = M.it; int what = M.a[i].what; ph_sheet_close();
  switch (what) {
    case A_CHPLAY: app_open_item(&it); break; /* a channel: tuned in (the row it is in, its line-up) */
    case A_CHSTREAMS: ph_streams_pick(&it, it.id, NULL); break;
    case A_CHPIN: { int on = !live_pinned(it.id); live_pin(&it, on); toast(T(on ? "Added to favorites" : "Removed from favorites"), 1600, NULL); break; }
    case A_CHFORGET: live_forget(it.id); toast(T("Removed from Live now"), 1600, NULL); break;
    case A_PLAY: menu_play(&it); break;
    case A_DL: if (it.ep) ph_download(&it, it.videoId, it.epName); else if (!strcmp(it.type, "series")) { CwInfo ci; ph_episodes_download(&it, lib_cw_info(&it, &ci) ? ci.vid : NULL); } else ph_download(&it, it.id, NULL); break; /* a series: its seasons and episodes to pick */
    case A_PICK: { int was = 0; (void)was; menu_streams(&it); break; }
    case A_RESTART: app_play_from_start(&it); break;
    case A_STREAMS: if (SCR == SCR_DETAIL) detail_open_streams(it.videoId); else ph_streams_pick(&it, it.videoId, it.epName); break;
    case A_DETAILS: { DetailParams p; memset(&p, 0, sizeof p); p.it = it; app_go(SCR_DETAIL, &p, 0); break; }
    case A_LIB: if (lib_has(it.id) && SCR == SCR_LIB) { void ph_leave(const Item *it, int scope, int act); ph_leave(&it, 1, 1); break; } /* on the Library: it goes first, then it is removed */ lib_toggle(&it); if (lib_has(it.id)) { char pb[600]; ph_fly(sb_poster_of(&it, pb, sizeof pb), 2); } toast(lib_has(it.id) ? T("Added to Library") : T("Removed from Library"), 1600, NULL); app_refresh_boards(); break;
    case A_DISMISS: { void ph_leave(const Item *it, int scope, int act); ph_leave(&it, 2, 3); break; } /* it shrinks and fades as the next ones close in, then it is dismissed */ /* the new episodes seen: out of Continue Watching until the next one (as the TV) */
    case A_SEEN: if (it.cw && !lib_is_marked_watched(&it)) { void ph_leave(const Item *it, int scope, int act); ph_leave(&it, 2, 2); break; } /* watched from Continue Watching: it leaves the row first */ { const cJSON *m = sb_meta_cached(it.type, it.id); lib_toggle_watched(&it, js_arr(m, "videos")); toast(lib_is_marked_watched(&it) ? T("Marked as watched") : T("Marked as unwatched"), 1600, NULL); card_mark_pop(it.id); app_refresh_boards(); if (SCR == SCR_DETAIL) detail_refresh_watched(); break; }
    case A_TRAILER: detail_trailer(it.type, it.id, it.name); break;
    case A_REVIEWS: { void ph_title_to_reviews(void); DetailParams p; memset(&p, 0, sizeof p); p.it = it; app_go(SCR_DETAIL, &p, 0); ph_title_to_reviews(); break; } } } /* (its page, gliding down to its reviews) */
static void on_mmeta(cJSON *m, void *ud) { (void)m; (void)ud; } /* (cached: the menu reads it as it draws) */
static char mAge[16]; static unsigned mAgeSess;
static void on_mage(const Rating *r, void *ud) { if ((unsigned)(uintptr_t)ud != mAgeSess || !r->rating[0]) return; rating_label(r, mAge, sizeof mAge); }
/* the menu: the poster large on the left; beside it the name, the facts (year, length, rating), the age; under them
   the story; then what can be done, Reviews among them. The same wherever a card is held */
static void m_draw(float a) { ph_fly_ready(&M.it);
  const cJSON *mm = sb_meta_cached(M.it.type, M.it.id); Item full = M.it; if (mm && !full.metaLine[0]) sb_meta_line(mm, full.metaLine, sizeof full.metaLine);
  const char *desc = M.it.description[0] ? M.it.description : js_str(mm, "description", "");
  float cw = SB_W - 2 * PAD, pw = 230, ph = 345, tx = PAD + pw + 26, tw = SB_W - tx - PAD;
  (void)cw; float rowH = 88, top = 46, hmax = SB_H - INS_T - 20, fixed = top + ph + 30 + INS_B + 20; /* (the story beside the poster, not under it) */
  if (M.n > 0 && fixed + M.n * rowH > hmax) { rowH = (hmax - fixed) / M.n; if (rowH < 70) rowH = 70; } /* every option in view: a little closer together when there are many */
  float h = fixed + M.n * rowH; if (h > hmax) h = hmax; static Scroll mv; static double mvAt; if (mvAt != M.openAt) { mvAt = M.openAt; mv.pos = 0; mv.vel = 0; } /* (still too many: they scroll) */
  float y = sheet_panel(h, a); char buf[600];
  const char *pu = sb_poster_of(&M.it, buf, sizeof buf); GLuint t = pu && *pu ? img_get_shaped(pu, (int)pw, (int)ph, 18, 2) : 0;
  if (live_type(M.it.type)) { gfx_shadow(PAD, y + top + 8, pw, pw, pw / 2, 24, BLACK_A(120)); card_draw_round(&M.it, PAD, y + top, pw); } /* a channel: its round badge */
  else { gfx_shadow(PAD, y + top + 8, pw, ph, 18, 24, BLACK_A(120)); if (!t || !ph_picture(t, PAD, y + top, pw, ph, 18, WHITE_A(255))) ph_box(PAD, y + top, pw, ph, 18); }
  float ty = y + top + 6; int nl = text_draw_wrapped(M.it.name, tx, ty, tw, 3, F_H2 - 2, 800, TXT, -0.3f, ALIGN_START, 40); ty += nl * 40 + 14;
  if (M.it.epName[0]) { int n2 = text_draw_wrapped(M.it.epName, tx, ty, tw, 2, F_SMALL + 1, 700, TXT2, 0, ALIGN_START, 32); ty += n2 * 32 + 10; }
  if (full.metaLine[0]) { meta_line_draw(&full, tx, ty, F_SMALL, WHITE_A(210)); ty += 40; }
  { const cJSON *g; char gs[160] = ""; int k = 0; cJSON_ArrayForEach(g, js_arr(mm, "genres")) if (cJSON_IsString(g) && k < 3) { size_t l = strlen(gs); snprintf(gs + l, sizeof gs - l, "%s%s", k ? "  \xc2\xb7  " : "", g->valuestring); k++; }
    if (gs[0]) { int n3 = text_draw_wrapped(gs, tx, ty, tw, 2, F_SMALL, 600, TXT3, 0, ALIGN_START, 30); ty += n3 * 30 + 10; } }
  if (mAge[0]) { float bw = text_width(mAge, F_SMALL, 800, 0) + 24; gfx_ring(tx, ty, bw, 40, 10, 1.5f, WHITE_A(170)); text_draw(mAge, tx + 12, ty + 20 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 800, WHITE_A(225), 0, 0, ALIGN_LEFT); ty += 54; }
  { int fit = (int)((y + top + ph - ty) / 30); if (desc[0] && fit > 0) text_draw_wrapped(desc, tx, ty, tw, fit, F_SMALL, 400, WHITE_A(195), 0, ALIGN_START, 30); } /* the story: beside the poster, as much as fits down to its foot */
  float ry0 = y + top + ph + 30, lh = (y + h - INS_B - 10) - ry0; mv.max = M.n * rowH - lh; if (mv.max < 0) mv.max = 0; ph_drag(0, ry0, SB_W, lh, &mv, 1); scroll_tick(&mv); gfx_clip_push(0, ry0, SB_W, lh + 4); float ry = ry0 - mv.pos;
  for (int i = 0; i < M.n; i++, ry += rowH) { { float pk = press_k(24, ry, SB_W - 48, rowH); if (pk > 0) gfx_rrect(24, ry + 4, SB_W - 48, rowH - 8, 20, WHITE_A((int)(18 * pk))); } /* inside the card, not the screen's edges */
    icon_draw(M.a[i].icon, PAD + 6, ry + rowH / 2 - 20, 40, WHITE_A(230)); text_draw(M.a[i].label, PAD + 72, ry + rowH / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 600, TXT, 0, SB_W - PAD - 72 - PAD, ALIGN_START); if (ry + rowH > ry0 && ry < ry0 + lh) hit_act(24, ry < ry0 ? ry0 : ry, SB_W - 48, ry < ry0 ? rowH - (ry0 - ry) : rowH, m_act, i, 0); }
  gfx_clip_pop();
}
static const PhSheet mSheet = { m_draw, NULL, NULL };
void ph_menu_show(const Item *it) {
  if (!strcmp(it->type, "collection")) { app_card_shake(); pplat_haptic_k(HK_TICK, 0.5f); return; } /* a collection has no menu (a tap opens it): held, the card shakes softly, nothing more (as the TV) */
  pplat_haptic_k(HK_POP, 1); /* the hold fired: its pop, as the menu opens */
  if (live_type(it->type)) { /* a channel: play it, pick its stream, keep it among the favourites, out of Live TV */
    if (!strcmp(it->id, LIVE_ALL_ID)) return; /* (All channels: nothing to choose) */
    M.it = *it; M.n = 0; M.openAt = app_now(); mAge[0] = 0; mAgeSess++;
    m_add(A_CHPLAY, "play", T("Play")); m_add(A_CHSTREAMS, "lib_sort", T("Streams"));
    int pn = live_pinned(it->id); m_add(A_CHPIN, pn ? "minus" : "star", T(pn ? "Remove from favorites" : "Add to favorites"));
    if (live_used()) { ItemList b; int in = 0; if (live_band(&b)) { for (int i = 0; i < b.n; i++) if (!strcmp(b.items[i].id, it->id)) in = 1; itemlist_clear(&b); } if (in) m_add(A_CHFORGET, "eyeOff", T("Remove from Live now")); }
    ph_sheet_open(&mSheet); return; }
  M.it = *it; M.n = 0; M.openAt = app_now(); const cJSON *m = sb_meta_cached(it->type, it->id); if (!m) sb_meta(it->type, it->id, on_mmeta, NULL);
  mAge[0] = 0; mAgeSess++; rating_get(it->type, it->id, on_mage, (void *)(uintptr_t)mAgeSess);
  if (it->ep) m_add(A_STREAMS, "play", T("Streams")); else m_add(A_PLAY, "play", T(it->cw ? "Resume" : "Play"));
  m_add(A_DL, "ph_down", T("Download")); /* right after Play */
  if (!it->ep && best_on()) m_add(A_PICK, "lib_sort", T("Choose a stream")); /* Play takes the best fit: the list is one step away */
  CwInfo ci; if (lib_cw_info(it, &ci) && (!it->ep || !strcmp(ci.vid, it->videoId))) m_add(A_RESTART, "restart", T("Watch from the beginning"));
  m_add(A_LIB, lib_has(it->id) ? "minus" : "plus", T(lib_has(it->id) ? "Remove from Library" : "Add to Library")); /* (among the first, as the TV) */
  if (it->cw && !it->ep) m_add(A_DETAILS, "info", T("Details")); /* (Continue Watching only: a tap on its card may start the player; elsewhere a tap opens the page already) */
  m_add(A_SEEN, lib_is_marked_watched(it) ? "eyeOff" : "eye", T(lib_is_marked_watched(it) ? "Mark as unwatched" : "Mark as watched"));
  if (trailer_available(m, it->id)) m_add(A_TRAILER, "trailer", T("Trailer"));
  if (!it->ep) m_add(A_REVIEWS, "d_award", T("Reviews"));
  if (it->cw || it->airAt > 0 || it->newEp) m_add(A_DISMISS, "close", T("Dismiss")); /* always the last (as the TV): out of Continue Watching until it is watched or marked again or a newer episode is out; out of Upcoming until the next episode is another */
  ph_sheet_open(&mSheet);
}
int cwmenu_open(void) { return ph_sheet_on(); }
void cwmenu_show(const Item *it) { ph_menu_show(it); }

/* ---- What's new: once per version, as a sheet (the items of assets/whatsnew.json, then the thank-you line) ---- */
#include "core/store.h"
#include "screens/welcome.h"
#include "platform/plat.h"
static struct { char ver[24], t[6][128], x[6][400], ic[6][32], st[128], ss[200]; int n; } WN;
static void wn_ok(int a, int b) { (void)a; (void)b; ph_sheet_close(); }
static void wn_url(int a, int b) { (void)b; const char *u = a ? welcome_github_url() : welcome_support_url(); if (u && *u && !pplat_open_url(u)) toast(u, 3000, NULL); }
#include "ui/glass.h"
/* a pill as the sheets' own (Support's): round ends, glass; lit: the accent's glass, its words in the accent's ink */
static void pill_btn(float x, float y, float w, float h, const char *label, int lit, HitFn act, int a, int b) {
  float r = h / 2, pk = press_k(x, y, w, h), sc = 1 - 0.06f * pk; gfx_scale_push(sc, sc, x + w / 2, y + r);
  if (lit) lglass_accent(x, y, w, h, r, 1); else { ph_glass(x, y, w, h, r, RGBA(16, 14, 22, 92)); gfx_ring(x, y, w, h, r, 1, WHITE_A(34)); }
  text_draw(label, x + w / 2, y + r - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, lit ? ACC_FG : TXT, 0, w - 24, ALIGN_CENTER); gfx_scale_pop();
  hit_act(x - 6, y - 6, w + 12, h + 12, act, a, b); }
/* a round glass button with an icon (support, GitHub) */
static void wn_round(float cx, float cy, float r, const char *ic, uint32_t tint, int a) { float pk = press_k(cx - r, cy - r, 2 * r, 2 * r), sc = 1 - 0.08f * pk;
  gfx_scale_push(sc, sc, cx, cy); ph_glass(cx - r, cy - r, 2 * r, 2 * r, r, RGBA(16, 14, 22, 92)); gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 1, WHITE_A(34)); icon_draw(ic, cx - 17, cy - 17, 34, tint); gfx_scale_pop();
  hit_act(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, wn_url, a, 0); }
/* What's new: its name and version, each change as an icon and two short lines, then Continue (support and GitHub
   as two round buttons beside it) */
static void wn_draw(float a) {
  int sup = welcome_support_url()[0] != 0, gh = welcome_github_url()[0] != 0; const float FH = 96;
  float cwT = SB_W - 2 * PAD - 52 - 22, rows = 0; for (int i = 0; i < WN.n; i++) { int nl = WN.x[i][0] ? text_wrap_count(T(WN.x[i]), cwT, 3, F_SMALL, 500, 0) : 0; rows += 52 + (nl > 1 ? 30 * (nl - 1) : 0) + (nl ? 18 : 0) + 30; } /* (each as tall as its words: a line or two, never cut) */
  float h = 160 + rows + 30 + FH + INS_B + 30; if (h > SB_H - INS_T - 40) h = SB_H - INS_T - 40; float y = sheet_panel(h, a), cw = SB_W - 2 * PAD;
  text_draw(T("What\xe2\x80\x99s new"), PAD + 4, y + 52, F_H1, 800, TXT, -0.5f, 0, ALIGN_START);
  { float pw = text_width(WN.ver, F_SMALL, 700, 0) + 32, ph = 44, px = SB_W - PAD - pw, py = y + 52 + text_line_height(F_H1) / 2.0f - ph / 2; /* its version, a quiet pill */
    gfx_rrect(px, py, pw, ph, ph / 2, WHITE_A(16)); gfx_ring(px, py, pw, ph, ph / 2, 1, WHITE_A(30)); text_draw(WN.ver, px + pw / 2, py + ph / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, TXT2, 0, 0, ALIGN_CENTER); }
  static Scroll wnV; float fy = y + h - INS_B - 30 - FH + 8, lt = y + 150, lb = fy - 24, ry = y + 166, ry0 = ry; /* the list: it scrolls when the screen is short (sideways) */
  gfx_clip_push(0, lt, SB_W, lb - lt); gfx_offset_push(0, -wnV.pos);
  for (int i = 0; i < WN.n; i++) { float ts = 52; /* its icon on a small tile, its name, one short line */
    icon_draw(icon_exists(WN.ic[i]) ? WN.ic[i] : "check", PAD + ts / 2 - 18, ry + ts / 2 - 22, 36, TXT); /* (the icon alone, one flat colour, as the app's icons) */
    float tx = PAD + ts + 22, tw = SB_W - PAD - tx;
    text_draw(T(WN.t[i]), tx, ry + 2, F_BODY, 700, TXT, 0, tw, ALIGN_START);
    int nl = WN.x[i][0] ? text_draw_wrapped(T(WN.x[i]), tx, ry + 36, tw, 3, F_SMALL, 500, TXT3, 0, ALIGN_START, 30) : 0; ry += 52 + (nl > 1 ? 30 * (nl - 1) : 0) + (nl ? 18 : 0) + 30; }
  gfx_offset_pop(); gfx_clip_pop(); wnV.max = (ry - ry0) - (lb - ry0); if (wnV.max < 0) wnV.max = 0; scroll_tick(&wnV); ph_drag(0, lt, SB_W, lb - lt, &wnV, 1);
  float bh = 76, by = fy + (FH - bh) / 2, r = bh / 2, bx = PAD; /* one row: Support (a heart and its word), GitHub, and Continue at the right, each as wide as its words */
  if (sup) { const char *l = T("Support"); float w = 28 + 32 + 12 + text_width(l, F_SMALL + 1, 700, 0) + 30, pk = press_k(bx, by, w, bh), sc = 1 - 0.06f * pk;
    gfx_scale_push(sc, sc, bx + w / 2, by + r); ph_glass(bx, by, w, bh, r, RGBA(16, 14, 22, 92)); gfx_ring(bx, by, w, bh, r, 1, WHITE_A(34));
    icon_draw("ph_heart", bx + 28, by + r - 16, 32, RGB(0xff, 0x6b, 0x9a)); text_draw(l, bx + 28 + 32 + 12, by + r - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, TXT, 0, 0, ALIGN_START); gfx_scale_pop();
    hit_act(bx - 6, by - 6, w + 12, bh + 12, wn_url, 0, 0); bx += w + 14; }
  if (gh) wn_round(bx + r, by + r, r, "ph_star", RGB(0xff, 0xc8, 0x57), 1);
  { const char *l = T("Continue"); float w = text_width(l, F_SMALL + 1, 800, 0) + 72; pill_btn(PAD + cw - w, by, w, bh, l, 1, wn_ok, 0, 0); } /* (round, as the pills beside it) */
}

static void wn_gone(void) { store_set("whatsNewSeen", WN.ver); store_set("guideSeen", "1"); }
static const PhSheet wnSheet = { wn_draw, NULL, wn_gone };
static int wn_load(void) {
  char p[1200]; snprintf(p, sizeof p, "%s/whatsnew-phone.json", plat_asset_dir()); FILE *f = fopen(p, "rb"); if (!f) { snprintf(p, sizeof p, "%s/whatsnew.json", plat_asset_dir()); f = fopen(p, "rb"); } if (!f) return 0; /* (the phone's own list first: the TV's speaks of its remote and its screen) */
  char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0; cJSON *j = cJSON_Parse(buf); if (!j) return 0;
  snprintf(WN.ver, sizeof WN.ver, "%s", js_str(j, "version", "")); WN.n = 0; const cJSON *it;
  cJSON_ArrayForEach(it, js_arr(j, "items")) if (WN.n < 6) { snprintf(WN.t[WN.n], 128, "%s", cJSON_IsString(it) ? it->valuestring : js_str(it, "title", "")); /* (a line alone: "And much more.") */ snprintf(WN.x[WN.n], 400, "%s", js_str(it, "text", "")); snprintf(WN.ic[WN.n], 32, "%s", js_str(it, "icon", "")); WN.n++; }
  cJSON_Delete(j); return WN.n; }
void ph_whatsnew_show(void) { if (wn_load()) ph_sheet_open(&wnSheet); } /* Settings > About */
void ph_whatsnew_check(void) {
  char p[1200]; snprintf(p, sizeof p, "%s/whatsnew-phone.json", plat_asset_dir()); FILE *f = fopen(p, "rb"); if (!f) { snprintf(p, sizeof p, "%s/whatsnew.json", plat_asset_dir()); f = fopen(p, "rb"); } if (!f) return;
  char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0; cJSON *j = cJSON_Parse(buf); if (!j) return;
  snprintf(WN.ver, sizeof WN.ver, "%s", js_str(j, "version", "")); WN.n = 0; const cJSON *it;
  cJSON_ArrayForEach(it, js_arr(j, "items")) if (WN.n < 6) { snprintf(WN.t[WN.n], 128, "%s", js_str(it, "title", "")); snprintf(WN.x[WN.n], 400, "%s", js_str(it, "text", "")); snprintf(WN.ic[WN.n], 32, "%s", js_str(it, "icon", "")); WN.n++; }
  cJSON_Delete(j);
  const char *seen = store_get("whatsNewSeen", ""); if (!WN.n || !strcmp(seen, WN.ver)) return;
  if (!seen[0] && !store_get("guideSeen", NULL)) { store_set("whatsNewSeen", WN.ver); store_set("guideSeen", "1"); return; } /* a first start: nothing is new yet */
  ph_sheet_open(&wnSheet);
}

/* ---- a series to download: the show's own picture with its logo at the top, its seasons in a row, a segmented
   helper (the whole season, the unwatched, none), then the season's episodes as cards with their stills (a tick on
   the still); the foot says how many and downloads them as one season. While their streams are found the button
   fills (2 of 8...), then the season goes to Downloads as one ---- */
#define EPMAX 600
static struct { Item it; int ns, cur, n; int sn[60]; struct { char id[96], nm[200], thumb[400]; int s, e, watched; } ep[EPMAX]; unsigned char pick[EPMAX]; Scroll v, sv; Tween tick[EPMAX], segX, curX, curW; char pre[96], bg[600], logo[600]; int ready, busy; double readyAt; } E;
static void ep_build(void) {
  const cJSON *m = sb_meta_cached(E.it.type, E.it.id); if (!m) return; const cJSON *vv; E.n = 0; E.ns = 0;
  { Item full; sb_fill_item(&full, m, E.it.type); char b[600]; const char *bg = sb_bg_mid(&full, b, sizeof b); snprintf(E.bg, sizeof E.bg, "%s", bg ? bg : ""); const char *lg = sb_logo_of(&full, b, sizeof b); snprintf(E.logo, sizeof E.logo, "%s", lg ? lg : ""); }
  int *w = NULL; cJSON *lit = lib_get(E.it.id); const cJSON *vids = js_arr(m, "videos"); int nv = cJSON_GetArraySize(vids); if ((lit || tw_on()) && nv > 0) { w = calloc((size_t)nv, sizeof *w); lib_ep_watched_map(lit, vids, w); }
  char nowIso[40]; { time_t t = time(NULL); struct tm tm; gmtime_r(&t, &tm); strftime(nowIso, sizeof nowIso, "%Y-%m-%dT%H:%M:%S", &tm); }
  int k = 0; cJSON_ArrayForEach(vv, vids) { int sn = (int)js_num(vv, "season", 0), e = (int)js_num(vv, "episode", 0); const char *rel = js_str(vv, "released", ""); if (sn > 0 && E.n < EPMAX && !(rel[0] && strncmp(rel, nowIso, 19) > 0)) { /* (out already: an episode still to come is not offered) */ int i = E.n++; snprintf(E.ep[i].id, 96, "%s", js_str(vv, "id", "")); snprintf(E.ep[i].nm, 200, "%s", js_str(vv, "name", js_str(vv, "title", ""))); snprintf(E.ep[i].thumb, 400, "%s", js_str(vv, "thumbnail", "")); E.ep[i].s = sn; E.ep[i].e = e; E.ep[i].watched = w ? w[k] : 0; } k++; }
  free(w);
  for (int a = 1; a < E.n; a++) for (int b = a; b > 0 && (E.ep[b].s < E.ep[b - 1].s || (E.ep[b].s == E.ep[b - 1].s && E.ep[b].e < E.ep[b - 1].e)); b--) { __typeof__(E.ep[0]) t = E.ep[b]; E.ep[b] = E.ep[b - 1]; E.ep[b - 1] = t; }
  for (int i = 0; i < E.n; i++) { int f = 0; for (int q = 0; q < E.ns; q++) if (E.sn[q] == E.ep[i].s) f = 1; if (!f && E.ns < 60) E.sn[E.ns++] = E.ep[i].s; }
  memset(E.pick, 0, sizeof E.pick); E.cur = 0;
  for (int i = 0; i < E.n; i++) if (E.pre[0] && !strcmp(E.ep[i].id, E.pre)) { E.pick[i] = 1; for (int q = 0; q < E.ns; q++) if (E.sn[q] == E.ep[i].s) E.cur = q; }
  for (int i = 0; i < EPMAX; i++) tween_set(&E.tick[i], E.pick[i]); E.ready = 1; E.readyAt = app_now(); }
static void on_ep_meta(cJSON *m, void *ud) { (void)m; (void)ud; ep_build(); }
static int ep_free(int i) { return ph_dl_state(E.it.id, E.ep[i].id, NULL) < 0; }
static void ep_toggle(int i, int b) { (void)b; if (E.busy || i < 0 || i >= E.n || !ep_free(i)) return; E.pick[i] = !E.pick[i]; extern void pplat_haptic(void); pplat_haptic(); }
static void ep_season(int q, int b) { (void)b; if (q == E.cur) return; E.cur = q; E.v.pos = 0; E.v.vel = 0; }
static int ep_mode(void) { int s = E.sn[E.cur], all = 1, unw = 1, none = 1, any = 0; /* which helper the season is in: 0 whole, 1 unwatched, 2 none, -1 a hand-made choice */
  for (int i = 0; i < E.n; i++) if (E.ep[i].s == s && ep_free(i)) { any = 1; if (!E.pick[i]) all = 0; if (E.pick[i] != !E.ep[i].watched) unw = 0; if (E.pick[i]) none = 0; }
  return !any ? 2 : none ? 2 : all ? 0 : unw ? 1 : -1; }
static void ep_help(int how, int b) { (void)b; if (E.busy) return; int s = E.sn[E.cur]; for (int i = 0; i < E.n; i++) if (E.ep[i].s == s && ep_free(i)) E.pick[i] = how == 0 ? 1 : how == 1 ? !E.ep[i].watched : 0; extern void pplat_haptic(void); pplat_haptic(); }
static void ep_go(int a, int b) { (void)a; (void)b; if (E.busy) return; const char *v[EPMAX], *e[EPMAX]; static char lb[EPMAX][240]; int n = 0, s1 = -1, mixed = 0;
  for (int i = 0; i < E.n; i++) if (E.pick[i]) { snprintf(lb[n], 240, "S%d E%d  %s", E.ep[i].s, E.ep[i].e, E.ep[i].nm); v[n] = E.ep[i].id; e[n] = lb[n]; n++; if (s1 < 0) s1 = E.ep[i].s; else if (s1 != E.ep[i].s) mixed = 1; }
  if (!n) return;
  if (n == 1 && !ph_dl_auto()) { ph_sheet_close(); ph_streams_download(&E.it, v[0], e[0]); return; } /* one, downloads not choosing themselves: its streams */
  char g[64]; char sn[12]; snprintf(sn, sizeof sn, "%d", s1); if (mixed) snprintf(g, sizeof g, "%s", T("Episodes")); else snprintf(g, sizeof g, "%s", T1("Season {n}", "n", sn));
  ph_sheet_close(); if (n > 1 && !ph_dl_auto()) toast(T("Best stream for each: download one at a time to choose"), 3200, NULL); ph_download_group(&E.it, v, e, n, n > 1 ? g : ""); } /* to Downloads at once: their streams are found there */
static void epd_draw(float a) { ph_fly_ready(&E.it);
  if (!E.ready) ep_build(); float cw = SB_W - 2 * PAD;
  if (E.busy) { int fin = 0; ph_dl_choosing(NULL, NULL, &fin); if (fin) { E.busy = 0; ph_sheet_close(); return; } }
  /* the season shown: its episodes, how many picked, whether some are watched */
  int sNow = E.ns ? E.sn[E.cur] : 0, nS = 0, pickS = 0, watchedS = 0, freeS = 0, total = 0; for (int i = 0; i < E.n; i++) { total += E.pick[i]; if (E.ep[i].s != sNow) continue; nS++; pickS += E.pick[i]; watchedS += E.ep[i].watched; freeS += ep_free(i); }
  /* its height hugs what it holds: the head, the seasons (when more than one), the selection row, the episodes, the foot */
  float hh = 236, rh = 128, seasonsH = E.ns > 1 ? 76 : 0, selH = 70, footH = 150 + INS_B, listMax = SB_H - INS_T - 24 - hh - seasonsH - selH - footH;
  int picksAll = total, hint = !ph_dl_auto() && picksAll > 1; static Tween hintT; tween_to(&hintT, hint ? 1 : 0, 0.3f, EASE_WEB); tween_tick(&hintT, phDt);
  float listH = E.ready ? fminf(nS * rh + 8, listMax - 76 * hintT.v) : 300; if (listH < rh) listH = rh;
  static Tween hT; float want = hh + seasonsH + selH + listH + footH + 76 * hintT.v; if (hT.v <= 0) tween_set(&hT, want); tween_to(&hT, want, 0.3f, EASE_WEB); tween_tick(&hT, phDt);
  /* its colours: the show's own, from its poster (locked: not the page's, not the last title's) */
  /* its colours: the app's own as the setting says (the show's when the accent follows the title: it is scoped to it while up) */
  uint32_t pal[4] = { ACC | 0xff000000u, ACC2 | 0xff000000u, ACC3 | 0xff000000u, ACC_D | 0xff000000u };
  uint32_t C0 = pal[0], C1 = pal[1], CD = pal[3], CF = (((C0 >> 16) & 255) * 0.299f + ((C0 >> 8) & 255) * 0.587f + (C0 & 255) * 0.114f) > 165 ? RGB(14, 12, 20) : WHITE_A(255);
  float h = hT.v, y = sheet_panel_bare(h, a); uint32_t sh0 = RGBA(20, 18, 28, 255); float cardH = h - INS_B + 10;
  gfx_shadow(12, y + 10, SB_W - 24, cardH, 44, 40, BLACK_A(120)); gfx_rrect(12, y, SB_W - 24, cardH, 44, sh0); /* one card: its own ground, the same rounded corners all round */
  /* the head: the show's picture in the sheet's own rounded top, fading into it, its logo over it */
  { /* the head: the show's picture, sharp, filling the card's top edge to edge in its rounded shape, a little dimmed for
       the words on it, fading into the card's own colour below */
    float pw = SB_W - 24, ph2 = hh + 40; GLuint bt = E.bg[0] ? img_get_shaped(E.bg, (int)pw, (int)ph2, 44, 1) : 0;
    if (!bt || !ph_picture(bt, 12, y, pw, ph2, 44, WHITE_A(255))) gfx_rrect(12, y, pw, ph2, 44, col_alpha(C0, 0.3f));
    gfx_rrect(12, y, pw, ph2, 44, BLACK_A(70)); gfx_rrect_v(12, y, pw, ph2 * 0.5f, 44, BLACK_A(60), BLACK_A(0));
    gfx_rrect_v(12, y + ph2 * 0.35f, pw, ph2 * 0.65f + 1, 0, col_alpha(sh0, 0), sh0);
    gfx_rrect(SB_W / 2 - 30, y + 14, 60, 6, 3, WHITE_A(170));
    float lw = SB_W * 0.5f, lh = 86, asp = 0; GLuint lt = E.logo[0] ? img_get(E.logo, (int)lw, (int)lh, &asp) : 0; float ly = y + 54;
    if (lt && asp > 0) { float w = lh * asp, h2 = lh; if (w > lw) { w = lw; h2 = w / asp; } gfx_shadow(SB_W / 2 - w / 2, ly + (lh - h2) / 2 + 6, w, h2, 8, 30, BLACK_A(90)); gfx_image(lt, SB_W / 2 - w / 2, ly + (lh - h2) / 2, w, h2, 0, WHITE_A(255), 0, 0, 1, 1); }
    else text_draw_wrapped(E.it.name, SB_W / 2, ly + 14, cw, 2, F_H2, 900, TXT, -0.4f, ALIGN_CENTER, 40);
    text_draw(T("Download episodes"), SB_W / 2, ly + lh + 22, F_SMALL, 700, WHITE_A(190), 0.3f, 0, ALIGN_CENTER); }
  float cy = y + hh;
  if (!E.ready) { spinner(SB_W / 2, cy + 150, 22); return; }
  /* the seasons (more than one): a row, the white pill gliding to the one shown */
  if (E.ns > 1) { float sy = cy + 4, sh = 60, x0 = PAD - E.sv.pos, x = x0, onX = 0, onW = 0, ws[60]; char lab[60][48]; ph_drag(0, sy, SB_W, sh, &E.sv, 0); scroll_tick(&E.sv); gfx_clip_push(12, sy - 4, SB_W - 24, sh + 8);
    for (int q = 0; q < E.ns; q++) { char nn[16]; snprintf(nn, sizeof nn, "%d", E.sn[q]); int cnt = 0; for (int i = 0; i < E.n; i++) if (E.ep[i].s == E.sn[q] && E.pick[i]) cnt++;
      if (cnt) snprintf(lab[q], 48, "%s  \xc2\xb7  %d", T1("Season {n}", "n", nn), cnt); else snprintf(lab[q], 48, "%s", T1("Season {n}", "n", nn)); ws[q] = text_width(lab[q], F_SMALL + 1, 700, 0) + 44;
      gfx_rrect(x, sy, ws[q], sh, sh / 2, WHITE_A(14)); if (q == E.cur) { onX = x - x0; onW = ws[q]; } hit_act(x, sy, ws[q], sh, ep_season, q, 0); x += ws[q] + 10; }
    if (E.curW.v <= 0) { tween_set(&E.curX, onX); tween_set(&E.curW, onW); } tween_to(&E.curX, onX, 0.3f, EASE_WEB); tween_to(&E.curW, onW, 0.3f, EASE_WEB); tween_tick(&E.curX, phDt); tween_tick(&E.curW, phDt);
    gfx_rrect(x0 + E.curX.v, sy, E.curW.v, sh, sh / 2, WHITE_A(240));
    x = x0; for (int q = 0; q < E.ns; q++) { float c = clampf(1 - fabsf((x - x0) - E.curX.v) / ws[q], 0, 1); text_draw(lab[q], x + ws[q] / 2, sy + sh / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, col_mix(WHITE_A(225), RGB(14, 12, 20), c), 0, 0, ALIGN_CENTER); x += ws[q] + 10; }
    E.sv.max = x - x0 - 10 - cw; if (E.sv.max < 0) E.sv.max = 0; gfx_clip_pop(); cy += seasonsH; }
  /* the selection, said plainly: how many of the season are picked; one button for all (or none); "unwatched only" when some are watched */
  { float ry = cy + 6, bh2 = 52; char cnt[64], a1[12], a2[12]; snprintf(a1, sizeof a1, "%d", pickS); snprintf(a2, sizeof a2, "%d", freeS); snprintf(cnt, sizeof cnt, "%s", T2("{a} of {b} selected", "a", a1, "b", a2));
    text_draw(cnt, PAD + 4, ry + bh2 / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, TXT2, 0, cw * 0.45f, ALIGN_START);
    int allOn = freeS && pickS == freeS; const char *bl = allOn ? T("Clear") : T("Select all"); float bw = text_width(bl, F_SMALL, 800, 0) + 40, bx = PAD + cw - bw, pk = press_k(bx, ry, bw, bh2);
    gfx_rrect(bx, ry, bw, bh2, bh2 / 2, allOn ? WHITE_A((int)(18 + 20 * pk)) : col_alpha(C0, 0.9f + 0.1f * pk)); text_draw(bl, bx + bw / 2, ry + bh2 / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 800, allOn ? TXT : CF, 0, 0, ALIGN_CENTER); hit_act(bx, ry, bw, bh2, ep_help, allOn ? 2 : 0, 0);
    if (watchedS && watchedS < nS) { const char *ul = T("Unwatched only"); float uw = text_width(ul, F_SMALL, 700, 0) + 36, ux = bx - 10 - uw, upk = press_k(ux, ry, uw, bh2); int uOn = ep_mode() == 1;
      gfx_rrect(ux, ry, uw, bh2, bh2 / 2, uOn ? col_alpha(C0, 0.3f) : WHITE_A((int)(12 + 18 * upk))); gfx_ring(ux, ry, uw, bh2, bh2 / 2, 1.2f, uOn ? col_alpha(C1, 0.9f) : WHITE_A(40)); text_draw(ul, ux + uw / 2, ry + bh2 / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, TXT, 0, 0, ALIGN_CENTER); hit_act(ux, ry, uw, bh2, ep_help, 1, 0); }
    cy += selH; }
  /* the episodes: their stills (the show's picture when one has none), name, number, a tick that pops */
  float top = cy, bot = top + listH; gfx_clip_push(12, top, SB_W - 24, bot - top); ph_drag(0, top, SB_W, bot - top, &E.v, 1); scroll_tick(&E.v);
  float ry = top + 4 - E.v.pos; int inSeason = 0;
  for (int i = 0; i < E.n; i++) { if (E.ep[i].s != sNow) continue; tween_to(&E.tick[i], E.pick[i], 0.28f, EASE_OUT_BACK); tween_tick(&E.tick[i], phDt);
    float in = clampf((float)((app_now() - E.readyAt - inSeason * 0.035) / 0.35), 0, 1); in = 1 - (1 - in) * (1 - in); inSeason++;
    if (ry + rh > top - 10 && ry < bot + 10) { float pr = 0; int st = ph_dl_state(E.it.id, E.ep[i].id, &pr); float k = E.tick[i].v, pk = press_k(PAD, ry, cw, rh - 12);
      gfx_opacity_push(in); gfx_offset_push(0, 20 * (1 - in));
      gfx_rrect(PAD, ry, cw, rh - 12, 22, WHITE_A(pk > 0 ? (int)(10 + 18 * pk) : 7)); if (k > 0.01f) { gfx_rrect(PAD, ry, cw, rh - 12, 22, col_alpha(C0, 0.16f * clampf(k, 0, 1))); gfx_ring(PAD, ry, cw, rh - 12, 22, 2, col_alpha(C1, 0.85f * clampf(k, 0, 1))); }
      float tw = 168, th = rh - 12 - 20, tx = PAD + 10, ty = ry + 10; const char *src = E.ep[i].thumb[0] ? E.ep[i].thumb : E.bg; GLuint t = src[0] ? img_get_shaped(src, (int)tw, (int)th, 14, 1) : 0; if (!t || !ph_picture(t, tx, ty, tw, th, 14, WHITE_A(st >= 0 ? 140 : 255))) ph_box(tx, ty, tw, th, 14);
      { char en[8]; snprintf(en, sizeof en, "%d", E.ep[i].e); float bw = text_width(en, F_TINY, 900, 0) + 18; gfx_rrect(tx + 8, ty + 8, bw, 30, 9, BLACK_A(160)); text_draw(en, tx + 8 + bw / 2, ty + 23 - text_line_height(F_TINY) / 2.0f, F_TINY, 900, WHITE_A(240), 0, 0, ALIGN_CENTER); }
      float nx = tx + tw + 18, nw = PAD + cw - nx - 76; char nm[220]; if (E.ep[i].nm[0]) snprintf(nm, sizeof nm, "%s", E.ep[i].nm); else { char en[12]; snprintf(en, sizeof en, "%d", E.ep[i].e); snprintf(nm, sizeof nm, "%s", T1("Episode {n}", "n", en)); }
      int enl = text_draw_wrapped(nm, nx, ry + 18, nw, 2, F_BODY, 700, st >= 0 ? TXT3 : TXT, 0, ALIGN_START, 30);
      { char sub[96]; snprintf(sub, sizeof sub, "S%d E%d%s", E.ep[i].s, E.ep[i].e, st == DL_DONE ? "" : ""); float sy2 = ry + 24 + enl * 30; float sw2 = text_draw(sub, nx, sy2, F_TINY, 700, TXT3, 0.3f, 0, ALIGN_START);
        if (st == DL_DONE) text_draw(T("Downloaded"), nx + sw2 + 12, sy2, F_TINY, 700, C1, 0, 0, ALIGN_START); else if (E.ep[i].watched) icon_draw("eye", nx + sw2 + 12, sy2 - 4, 26, TXT3); }
      float ccx = PAD + cw - 42, ccy = ry + (rh - 12) / 2;
      if (st == DL_DONE) { gfx_circle(ccx, ccy, 20, C0); icon_draw("ph_down", ccx - 12, ccy - 12, 24, WHITE_A(255)); }
      else if (st >= 0) { gfx_ring(ccx - 20, ccy - 20, 40, 40, 20, 3, WHITE_A(40)); for (int q = 0; q <= 30; q++) { if (q > 30 * pr) break; float an = q * 12 * (float)M_PI / 180; gfx_circle(ccx + 18.5f * sinf(an), ccy - 18.5f * cosf(an), 2, C1); } }
      else { gfx_ring(ccx - 20, ccy - 20, 40, 40, 20, 2.5f, WHITE_A((int)(120 * (1 - clampf(k, 0, 1))))); if (k > 0.01f) { float r = 20 * clampf(k, 0, 1.15f); gfx_circle(ccx, ccy, r, C0); gfx_opacity_push(clampf(k, 0, 1)); icon_draw("ph_check", ccx - 14, ccy - 14, 28, CF); gfx_opacity_pop(); } }
      gfx_offset_pop(); gfx_opacity_pop(); hit_act(PAD, ry, cw, rh - 12, ep_toggle, i, 0); }
    ry += rh; }
  E.v.max = ry + E.v.pos - bot + 10; if (E.v.max < 0) E.v.max = 0; gfx_clip_pop();
  if (E.v.max > 0) gfx_rrect_v(12, bot - 44, SB_W - 24, 44, 0, col_alpha(sh0, 0), sh0); /* (more under: it fades into the foot) */ /* the foot: its own ground, the list fading under it, the sheet's rounded corners kept */
  /* the foot */
  float fy = bot + 14 + 76 * hintT.v; char line[96]; if (total) { char n[12]; snprintf(n, sizeof n, "%d", total); snprintf(line, sizeof line, "%s", total == 1 ? T("1 episode") : T1("{n} episodes", "n", n)); } else snprintf(line, sizeof line, "%s", T("Pick the episodes to download"));
  if (hintT.v > 0.01f) { float hy2 = bot + 10; gfx_opacity_push(hintT.v); gfx_rrect(PAD, hy2, cw, 64, 20, WHITE_A(10)); icon_draw("st_about", PAD + 18, hy2 + 32 - 15, 30, C1);
    text_draw_wrapped(T("Each one gets its best stream. To choose a stream yourself, download episodes one at a time."), PAD + 62, hy2 + 32 - 26, cw - 80, 2, F_TINY, 600, TXT2, 0, ALIGN_START, 26); gfx_opacity_pop(); }
  text_draw(line, SB_W / 2, fy, F_SMALL, 700, TXT2, 0, cw, ALIGN_CENTER);
  float by = fy + 40, bw = cw, bh = 84;
  if (E.busy) { int dn = 0, tt = 0; ph_dl_choosing(&dn, &tt, NULL); float f = tt ? (float)dn / tt : 0; static Tween ft; tween_to(&ft, f, 0.4f, EASE_OUT); tween_tick(&ft, phDt);
    gfx_rrect(PAD, by, bw, bh, bh / 2, WHITE_A(20)); gfx_clip_push(PAD, by, bw * ft.v, bh); gfx_rrect(PAD, by, bw, bh, bh / 2, C0); gfx_clip_pop();
    char a2[16], b2[16], bl[96]; snprintf(a2, sizeof a2, "%d", dn); snprintf(b2, sizeof b2, "%d", tt); snprintf(bl, sizeof bl, "%s", T2("Finding streams  {a} of {b}", "a", a2, "b", b2)); text_draw(bl, SB_W / 2, by + bh / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 800, WHITE_A(250), 0, 0, ALIGN_CENTER); }
  else { /* the button: a pill in the show's own colour, lit from above, its glow under it; nothing picked, a quiet one */
    float pk = total ? press_k(PAD, by, bw, bh) : 0, sc = 1 - 0.03f * pk; gfx_scale_push(sc, sc, PAD + bw / 2, by + bh / 2);
    if (total) { gfx_rrect_v(PAD, by, bw, bh, bh / 2, col_mix(C0, WHITE_A(255), 0.12f), col_mix(C0, CD, 0.25f)); gfx_rrect_v(PAD, by, bw, bh * 0.5f, bh / 2, WHITE_A(46), WHITE_A(0)); gfx_ring(PAD, by, bw, bh, bh / 2, 1.2f, WHITE_A(50)); }
    else { gfx_rrect(PAD, by, bw, bh, bh / 2, WHITE_A(14)); gfx_ring(PAD, by, bw, bh, bh / 2, 1, WHITE_A(26)); }
    char lbn[64], n2[12]; snprintf(n2, sizeof n2, "%d", total); snprintf(lbn, sizeof lbn, "%s", total > 1 ? T1("Download {n} episodes", "n", n2) : T("Download")); uint32_t fc = total ? CF : WHITE_A(110);
    float lw2 = text_width(lbn, F_BODY + 1, 800, 0), gx = PAD + bw / 2 - (lw2 + 44) / 2; icon_draw("ph_down", gx, by + bh / 2 - 18, 36, fc); text_draw(lbn, gx + 44, by + bh / 2 - text_line_height(F_BODY + 1) / 2.0f, F_BODY + 1, 800, fc, 0, 0, ALIGN_START);
    gfx_scale_pop(); if (total) hit_act(PAD, by, bw, bh, ep_go, 0, 0); }
  gfx_ring(12, y, SB_W - 24, cardH, 44, 1.2f, WHITE_A(22)); /* its outline, one line all round, over everything */
}
/* it takes the show's own colours while it is up (the page it came from gets its own back) */
static void epd_gone(void) { if (SCR != SCR_DETAIL) accent_scope(NULL, NULL, NULL); }
static const PhSheet epdSheet = { epd_draw, NULL, epd_gone };
void ph_episodes_download(const Item *it, const char *preselect) { memset(&E, 0, sizeof E); E.it = *it; snprintf(E.pre, sizeof E.pre, "%s", preselect ? preselect : ""); { char pb[600]; accent_scope(it->id, sb_poster_of(it, pb, sizeof pb), it->logo); }
  if (!sb_meta_cached(it->type, it->id)) sb_meta(it->type, it->id, on_ep_meta, NULL); ph_sheet_open(&epdSheet); }

/* a card that went away (ui.c): now what it went for */
void ph_leave_done(const Item *itp, int act) { Item it = *itp;
  if (act == 1) { lib_toggle(&it); toast(T("Removed from Library"), 1600, NULL); app_refresh_boards(); }
  else if (act == 3) { void board_rebuild(void); if (it.cw || it.newEp) { notify_cw_dismiss(&it); if (it.cw) tw_play_drop(it.id); /* (Trakt's pause of it: deleted there too, while tracking is on) */ } else notify_up_dismiss(&it); board_rebuild(); app_refresh_boards(); } /* (Dismiss: out of Continue Watching or Upcoming, on this phone) */
  else if (act == 2) { const cJSON *m = sb_meta_cached(it.type, it.id); lib_toggle_watched(&it, js_arr(m, "videos")); toast(T("Marked as watched"), 1600, NULL); app_refresh_boards(); } }

/* ---- the support card (src/screens/supportcard.c decides when): one of the phone's sheets, as Share logs: the
   words, the two links as Settings' rows (each opens its page), Maybe later, and Don't show again quietly under it ---- */
void supportcard_choose(int i); void supportcard_sheet_gone(void); const char *welcome_support_url(void); const char *welcome_github_url(void);
static void sup_link(int a, int b) { (void)b; const char *u = a ? welcome_github_url() : welcome_support_url(); if (u && *u && !pplat_open_url(u)) toast(u, 3000, NULL); supportcard_choose(0); }
static void sup_pick(int a, int b) { (void)b; supportcard_choose(a); }
static void sup_draw(float a) {
  const char *sup = welcome_support_url(), *gh = welcome_github_url(); int nr = (sup[0] ? 1 : 0) + (gh[0] ? 1 : 0); const float RH = 96, cw = SB_W - 2 * PAD;
  const char *tt = T("Enjoying Stremio Origin?"), *ln = T("One person makes it in their spare time. Support or a star keeps it going.");
  int n1 = text_wrap_count(tt, cw - 40, 3, F_H2, 800, -0.3f), n2 = text_wrap_count(ln, cw - 60, 4, F_SMALL + 1, 500, 0); if (n1 < 1) n1 = 1; if (n2 < 1) n2 = 1;
  float lw = cw - 60; if (n2 > 1) while (lw > 200 && text_wrap_count(ln, lw - 12, 4, F_SMALL + 1, 500, 0) == n2) lw -= 12; /* (its lines even: never one word alone on the last) */
  float words = 46 + n1 * 42 + 12 + n2 * 32, h = words + 30 + nr * RH + 28 + 84 + 10 + 64 + 18 + INS_B, y = sheet_panel(h, a);
  float ty = y + 46; text_draw_wrapped(tt, SB_W / 2, ty, cw - 40, 3, F_H2, 800, TXT, -0.3f, ALIGN_CENTER, 42); ty += n1 * 42 + 12;
  text_draw_wrapped(ln, SB_W / 2, ty, lw, 4, F_SMALL + 1, 500, TXT2, 0, ALIGN_CENTER, 32); ty += n2 * 32 + 30;
  /* the links: grouped as in Settings, the icon quiet, the page's sign at the end */
  if (nr) { gfx_rrect(PAD, ty, cw, nr * RH, 26, WHITE_A(9)); gfx_ring(PAD, ty, cw, nr * RH, 26, 1, WHITE_A(16)); int r = 0;
    for (int k = 0; k < 2; k++) { if (!(k ? gh[0] : sup[0])) continue; float ry = ty + r * RH, pk = press_k(PAD, ry, cw, RH);
      gfx_clip_push(PAD, ty, cw, nr * RH); if (pk > 0) gfx_rect(PAD, ry, cw, RH, WHITE_A((int)(16 * pk))); gfx_clip_pop(); if (r) gfx_rect(PAD + 92, ry, cw - 92, 1, WHITE_A(14));
      icon_draw(k ? "ph_star" : "ph_heart", PAD + 26, ry + RH / 2 - 22, 44, WHITE_A(215));
      text_draw(T(k ? "Star on GitHub" : "Support on Ko-fi"), PAD + 92, ry + RH / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 600, TXT, 0, cw - 92 - 80, ALIGN_START);
      icon_draw("ph_ext", PAD + cw - 26 - 32, ry + RH / 2 - 16, 32, WHITE_A(110)); hit_act(PAD, ry, cw, RH, sup_link, k, 0); r++; }
    ty += nr * RH + 28; }
  { const char *ml = T("Maybe later"); float bw = fminf(cw, fmaxf(300, text_width(ml, F_BODY, 700, 0) + 120)); pill_btn(SB_W / 2 - bw / 2, ty, bw, 80, ml, 0, sup_pick, 0, 0); ty += 84 + 10; } /* (a pill as wide as its words want, in the middle: never stretched across the sheet) */
  { const char *nv = T("Don't show again"); float nw = text_width(nv, F_SMALL + 1, 600, 0) + 48, nx = SB_W / 2 - nw / 2, pk = press_k(nx, ty, nw, 64); /* (quietly, under it) */
    text_draw(nv, SB_W / 2, ty + 32 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 600, WHITE_A(pk > 0 ? 220 : 140), 0, 0, ALIGN_CENTER); hit_act(nx, ty, nw, 64, sup_pick, 1, 0); }
}
static void sup_gone(void) { supportcard_sheet_gone(); }
static const PhSheet supSheet = { sup_draw, NULL, sup_gone, -620 }; /* (a small sheet: no wider than 620 on a wide screen) */
void ph_supsheet_open(void) { ph_sheet_open(&supSheet); }
void ph_supsheet_close(void) { if (ph_sheet_on()) ph_sheet_close(); }
