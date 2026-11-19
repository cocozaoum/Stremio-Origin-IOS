/* The pieces the phone's pages share: the bar at the top of a page, rows of the app's cards that slide under a
   finger, grids, buttons, chips, switches, fields, the waiting ring. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"

static void back_act(int a, int b) { (void)a; (void)b; app_back(); }
void top_bar(const char *title, int back, float scrolled) {
  float h = INS_T + 88, k = scrolled <= 0 ? 0 : scrolled > 120 ? 1 : scrolled / 120; k = k * k * (3 - 2 * k); /* eased: it comes in softly with the scroll */
  phone_top_glass(h, k);
  float x = PAD;
  (void)back; if (0) { float bx = 10, by = INS_T + 10; /* (no back arrow: a page goes back by a swipe from its left edge, or the bar's Back) */ float pk = press_k(0, INS_T, 90, 88); if (pk > 0) gfx_circle(bx + 36, by + 34, 32, WHITE_A((int)(30 * pk)));
    icon_draw("ph_back", bx + 16, by + 14, 40, WHITE_A(255)); hit_act(0, INS_T, 90, 88, back_act, 0, 0); x = 92; }
  if (title && *title) { float ta = title[0] == 1 ? k : 1; const char *t = title[0] == 1 ? title + 1 : title; /* a leading \001: the title comes in with the glass */
    if (ta > 0.01f) { gfx_opacity_push(ta); text_draw(t, x, INS_T + 44 - text_line_height(F_H2) / 2.0f + 8 * (1 - ta), F_H2, 800, TXT, -0.3f, SB_W - x - PAD - 80, ALIGN_START); gfx_opacity_pop(); } }
}

/* a card going away (removed from the Library, or off Continue Watching once watched): it shrinks and fades while
   the ones after it slide into its place; the removal itself is done once it has gone (no jump) */
static struct { char id[64]; double at; int scope; Item it; int act; } LV[8]; /* scope 1: the Library's grid, 2: Continue Watching; act: what to do once gone */
#define LEAVE_S 0.34
void ph_leave(const Item *it, int scope, int act) { int f = 0; for (int i = 0; i < 8; i++) if (!LV[i].id[0] || app_now() - LV[i].at > 2) { f = i; break; } snprintf(LV[f].id, sizeof LV[f].id, "%s", it->id); LV[f].at = app_now(); LV[f].scope = scope; LV[f].it = *it; LV[f].act = act; }
static float leave_k(const Item *it, int scope) { for (int i = 0; i < 8; i++) if (LV[i].id[0] && LV[i].scope == scope && !strcmp(LV[i].id, it->id)) { float k = (float)((app_now() - LV[i].at) / LEAVE_S); k = k < 0 ? 0 : k > 1 ? 1 : k; return k * k * (3 - 2 * k); } return 0; }
void ph_leave_tick(void) { extern void ph_leave_done(const Item *it, int act); for (int i = 0; i < 8; i++) if (LV[i].id[0] && LV[i].act && app_now() - LV[i].at >= LEAVE_S) { int a = LV[i].act; LV[i].act = 0; ph_leave_done(&LV[i].it, a); }
  for (int i = 0; i < 8; i++) if (LV[i].id[0] && app_now() - LV[i].at > 1.5) LV[i].id[0] = 0; } /* (kept a moment after: the list rebuilt without it, no jump back) */
void card_at(const Item *it, float x, float y, float w, float h, int wide, int label, unsigned long long key, int pressed) {
  if (x > SB_W || x + w < 0 || y > SB_H || y + h < 0) return;
  int coll = !strcmp(it->type, "collection"); float hold = pressed && !coll ? app_hold_progress() : 0, r = wide ? 12 : 14; if (!pressed && press_k(x, y, w, h) > 0.2f) pressed = 1; /* (a collection: no hold's glow, it has no menu) */
  if (coll && pressed) x += app_card_shake_dx(); /* (held anyway: a short soft shake) */
  lift_begin_z(key, pressed, hold, x, y, w, h, r, 0.04f);
  card_draw_wh(it, x, y, w, h, wide, label, 1);
  lift_end(key, x, y, w, h, r);
  if (hold > 0) hold_trace_draw(x, y, w, h, r, hold);
}

/* a row that comes and goes (Continue Watching, Upcoming): emptied (its last card leaving), it closes, the rows under
   it gliding up; a new one opens, the rows under it making room; *k is its own (kept by the caller, -1 at first) */
float row_draw_k(const char *title, const ItemList *l, int wide, float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx, float *k) {
  int n = l ? l->n : 0, alive = 0; for (int i = 0; i < n; i++) if (!((l->items[i].cw || l->items[i].airAt > 0) && leave_k(&l->items[i], 2) > 0)) { alive = 1; break; }
  float want = alive ? 1 : 0, full = (wide ? WIDE_CH : POSTER_H) + 52 + 36; if (*k < 0) *k = want; else { *k += (want - *k) * fminf(1, phDt * 7); if (fabsf(*k - want) < 0.003f) *k = want; }
  if (*k <= 0.001f) return 0; float e = *k * *k * (3 - 2 * *k);
  if (!n) return full * e; /* (gone: only its room, closing) */
  if (e >= 0.999f) return row_draw(title, l, wide, y, s, rowIdx, focus, focusedIdx);
  gfx_clip_push(0, y, SB_W, full * e); gfx_opacity_push(e); row_draw(title, l, wide, y - (1 - e) * 30, s, rowIdx, focus, focusedIdx); gfx_opacity_pop(); gfx_clip_pop(); return full * e; }
float row_draw(const char *title, const ItemList *l, int wide, float y, Scroll *s, int rowIdx, HitFn focus, int focusedIdx) {
  float cw = wide ? WIDE_CW : POSTER_W, ch = wide ? WIDE_CH : POSTER_H, gap = 14, top = 52;
  if (!l || l->n <= 0) return 0;
  if (y + top + ch < -10 || y > SB_H + 10) { scroll_tick(s); return top + ch + 36; }
  text_draw(title, PAD, y + 6, F_H2 - 2, 800, TXT, -0.3f, SB_W - 2 * PAD, ALIGN_START);
  s->max = l->n * (cw + gap) - gap + 2 * PAD - SB_W; scroll_tick(s); ph_drag(0, y + top, SB_W, ch + 8, s, 0);
  int rtl = app_rtl();
  float acc = 0; for (int i = 0; i < l->n; i++) { float lk = l->items[i].cw || l->items[i].airAt > 0 ? leave_k(&l->items[i], 2) : 0, x = PAD + acc - s->pos; acc += (cw + gap) * (1 - lk); if (rtl) x = SB_W - x - cw; if (x > SB_W || x + cw < 0) continue;
    if (lk >= 0.999f) continue; if (lk > 0) { float k2 = 1 - 0.25f * lk; gfx_opacity_push(1 - lk); gfx_scale_push(k2, k2, x + cw / 2, y + top + ch / 2); } /* going: smaller and fainter, the next ones closing in */
    int pr = ph_pressed(x, y + top, cw, ch) || (focusedIdx == i && app_hold_progress() > 0);
    card_at(&l->items[i], x, y + top, cw, ch, wide, wide, lift_key(l, i), pr);
    if (lk > 0) { gfx_scale_pop(); gfx_opacity_pop(); } else hit_add(x, y + top, cw, ch, focus, NULL, rowIdx, i); }
  return top + ch + 36;
}

float poster_grid(const ItemList *l, float y, HitFn focus, int tag, int focusedIdx) {
  int cols = SB_W >= 1300 ? 6 : SB_W >= 1050 ? 5 : SB_W >= 860 ? 4 : 3; if (ph_tabw()) { int c = (int)((SB_W - 2 * PAD + 14) / (220 + 14) + 0.5f); cols = c > cols ? c : cols; } /* (a tablet sideways: posters of about 220, as many as fit) */ float gap = 14, w = (SB_W - 2 * PAD - gap * (cols - 1)) / cols, h = w * 1.5f; /* more across on a wider screen */
  float shift = 0; int lib = SCR == SCR_LIB; for (int i = 0; i < l->n; i++) { float lk = lib ? leave_k(&l->items[i], 1) : 0, f = i - shift; shift += lk; /* (a card going: the ones after slide back a place, along the grid) */
    int c0 = (int)floorf(f); float fr = f - c0; float x0 = PAD + (c0 % cols) * (w + gap), y0 = y + (c0 / cols) * (h + gap), x1 = PAD + ((c0 + 1) % cols) * (w + gap), y1 = y + ((c0 + 1) / cols) * (h + gap);
    float x = x0 + (x1 - x0) * fr, yy = y0 + (y1 - y0) * fr;
    if (yy + h < 0 || yy > SB_H || lk >= 0.999f) continue;
    if (lk > 0) { float k2 = 1 - 0.25f * lk; gfx_opacity_push(1 - lk); gfx_scale_push(k2, k2, x + w / 2, yy + h / 2); }
    int pr = ph_pressed(x, yy, w, h) || (focusedIdx == i && app_hold_progress() > 0);
    card_at(&l->items[i], x, yy, w, h, 0, 0, lift_key(l, i), pr); if (lk > 0) { gfx_scale_pop(); gfx_opacity_pop(); } else hit_add(x, yy, w, h, focus, NULL, tag, i); }
  return l->n ? ((l->n + cols - 1) / cols) * (h + gap) : 0;
}

int button(float x, float y, float w, float h, const char *icon, const char *label, int kind, HitFn act, int a, int b) {
  float pk = press_k(x, y, w, h); int pr = pk > 0.3f; float k = 1 - 0.05f * pk; gfx_scale_push(k, k, x + w / 2, y + h / 2); float r = 12;
  if (kind == 1) { gfx_rrect(x, y, w, h, r, ACC); gfx_rrect_v(x, y, w, h, r, WHITE_A(50), WHITE_A(0)); }
  else if (kind == 2) gfx_rrect(x, y, w, h, r, pr ? WHITE_A(215) : WHITE_A(245));
  else if (kind == 3) { if (pr) gfx_rrect(x, y, w, h, r, WHITE_A(14)); }
  else glass_rrect(x, y, w, h, r, pr ? RGBA(60, 56, 72, 230) : RGBA(46, 42, 58, 220), 0.14f, 0.02f, 0.5f);
  uint32_t fg = kind == 1 ? ACC_FG : kind == 2 ? RGB(14, 12, 20) : WHITE_A(240);
  float tw = label && *label ? text_width(label, F_BODY, 700, 0) : 0, iw = icon ? 36 : 0, g = icon && tw ? 12 : 0, cx = x + w / 2 - (iw + g + tw) / 2;
  if (icon) icon_draw(icon, cx, y + h / 2 - 18, 36, fg);
  if (tw) text_draw(label, cx + iw + g, y + h / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 700, fg, 0, w - 20, ALIGN_LEFT);
  if (pk > 0 && kind != 3) gfx_rrect(x, y, w, h, r, kind == 2 ? BLACK_A((int)(30 * pk)) : WHITE_A((int)(26 * pk)));
  gfx_scale_pop(); if (act) hit_act(x, y, w, h, act, a, b); return pr;
}

float chip(float x, float y, const char *label, int on, HitFn act, int a, int b) {
  float h = 56, w = text_width(label, F_SMALL + 1, 700, 0) + 44; float pk = press_k(x, y, w, h); int pr = pk > 0.3f;
  if (on) gfx_rrect(x, y, w, h, h / 2, WHITE_A(240)); else { gfx_rrect(x, y, w, h, h / 2, pr ? WHITE_A(40) : WHITE_A(18)); gfx_ring(x, y, w, h, h / 2, 1, WHITE_A(40)); }
  text_draw(label, x + w / 2, y + h / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 700, on ? RGB(14, 12, 20) : WHITE_A(230), 0, 0, ALIGN_CENTER);
  if (act) hit_act(x, y, w, h, act, a, b); return w;
}

void spinner(float cx, float cy, float r) {
  gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 4, WHITE_A(36)); float a0 = fmodf((float)app_now() / 0.8f, 1) * 360;
  for (int k = 0; k <= 14; k++) { float an = (a0 + k * 6.5f - 45) * (float)M_PI / 180; gfx_circle(cx + (r - 2) * sinf(an), cy - (r - 2) * cosf(an), 2.4f, ACC3); }
}

void text_backspace(char *s) { size_t l = strlen(s); while (l && (s[l - 1] & 0xc0) == 0x80) l--; if (l) l--; s[l] = 0; }
void text_field(float x, float y, float w, float h, const char *value, const char *hint, int focused, int secret) {
  gfx_rrect(x, y, w, h, 16, SURF2); gfx_ring(x, y, w, h, 16, focused ? 2 : 1, focused ? col_alpha(ACC2, 0.85f) : WHITE_A(22));
  char shown[400]; if (secret) { int n = 0; for (const char *p = value; *p && n < 80; p++) if ((*p & 0xc0) != 0x80) { memcpy(shown + n * 3, "\xe2\x80\xa2", 3); n++; } shown[n * 3] = 0; } else snprintf(shown, sizeof shown, "%s", value);
  float ty = y + h / 2 - text_line_height(F_BODY + 2) / 2.0f, tw = 0;
  if (shown[0]) tw = text_draw(shown, x + 24, ty, F_BODY + 2, 500, TXT, 0, w - 48, ALIGN_LEFT); else text_draw(hint, x + 24, ty, F_BODY + 2, 500, WHITE_A(105), 0, w - 48, ALIGN_LEFT);
  if (focused && fmodf((float)app_now(), 1.0f) < 0.55f) gfx_rect(x + 24 + tw + 2, y + h / 2 - 17, 3, 34, ACC2);
}
/* ---- a text you edit ---- */
static int u8p(const char *s, int i) { if (i <= 0) return 0; i--; while (i > 0 && (s[i] & 0xc0) == 0x80) i--; return i; }
static int u8n(const char *s, int i) { int l = (int)strlen(s); if (i >= l) return l; i++; while (i < l && (s[i] & 0xc0) == 0x80) i++; return i; }
static int nchars(const char *s, int i) { int k = 0; for (int j = 0; j < i && s[j]; j++) if ((s[j] & 0xc0) != 0x80) k++; return k; }
static void ted_disp(const TEd *e, char *out, int cap) { if (!e->secret) { snprintf(out, cap, "%s", e->s); return; } int n = nchars(e->s, e->len), k = 0; for (; k < n && k * 3 + 4 < cap; k++) memcpy(out + k * 3, "\xe2\x80\xa2", 3); out[k * 3] = 0; }
static int to_d(const TEd *e, int i) { return e->secret ? 3 * nchars(e->s, i) : i; }
static float x_of(const TEd *e, const char *d, int i) { char t[600]; int di = to_d(e, i); if (di > (int)sizeof t - 1) di = sizeof t - 1; memcpy(t, d, di); t[di] = 0; return text_width(t, e->px, e->weight, 0); }
/* a mirrored page (right to left): left-to-right words in a field (an English title typed in Arabic) run the other way
   round in its box, so a place in them is measured from the box's other end; right-to-left words keep the plain measure */
static int ted_flip(const char *d) { if (!gfx_mirrored()) return 0; for (const unsigned char *c = (const unsigned char *)d; *c; c++) { if (*c >= 0xD6 && *c <= 0xDD) return 0; if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) return 1; } return 1; } /* (its first letter decides) */
static float pos_of(const TEd *e, const char *d, int i, int flip, float total) { float p = x_of(e, d, i); return flip ? total - p : p; }
static int idx_at(const TEd *e, float fx) { char d[600]; ted_disp(e, d, sizeof d); float want = fx - e->lx + e->scroll; int best = 0; float bd = 1e9f, total = text_width(d, e->px, e->weight, 0); int flip = ted_flip(d);
  for (int i = 0;; i = u8n(e->s, i)) { float dd = fabsf(pos_of(e, d, i, flip, total) - want); if (dd < bd) { bd = dd; best = i; } if (i >= e->len) break; } return best; }
static int wsp(char c) { return c == ' ' || c == '@' || c == '.' || c == '-' || c == '_'; }
static void sel_word(TEd *e, int i) { if (e->secret) { e->anc = 0; e->cur = e->len; e->ws = 0; e->we = e->len; return; }
  int a = i, b = i; while (a > 0 && !wsp(e->s[a - 1])) a = u8p(e->s, a); while (b < e->len && !wsp(e->s[b])) b = u8n(e->s, b);
  if (a == b) { b = u8n(e->s, b); } e->anc = a; e->cur = b; e->ws = a; e->we = b; }
static void del_sel(TEd *e) { int a = e->cur < e->anc ? e->cur : e->anc, b = e->cur < e->anc ? e->anc : e->cur; if (a == b) return; memmove(e->s + a, e->s + b, e->len - b + 1); e->len -= b - a; e->cur = e->anc = a; }
void ted_reset(TEd *e) { e->len = (int)strlen(e->s); e->cur = e->anc = e->len; e->menu = 0; }
void ted_insert(TEd *e, const char *t) { del_sel(e); int n = (int)strlen(t); if (e->len + n >= e->cap) n = e->cap - 1 - e->len; if (n <= 0) return; while (n > 0 && (t[n] & 0xc0) == 0x80) n--;
  memmove(e->s + e->cur + n, e->s + e->cur, e->len - e->cur + 1); memcpy(e->s + e->cur, t, n); if (e->lower) for (int i = 0; i < n; i++) { char *c = e->s + e->cur + i; if (*c >= 'A' && *c <= 'Z') *c += 32; } e->len += n; e->cur += n; e->anc = e->cur; e->menu = 0; e->movedAt = app_now(); }
void ted_backspace(TEd *e) { if (e->cur != e->anc) del_sel(e); else if (e->cur > 0) { e->anc = u8p(e->s, e->cur); del_sel(e); } e->menu = 0; e->movedAt = app_now(); }
int ted_key(TEd *e, int k) { if (k == K_LEFT) { e->cur = e->cur != e->anc ? (e->cur < e->anc ? e->cur : e->anc) : u8p(e->s, e->cur); } else if (k == K_RIGHT) { e->cur = e->cur != e->anc ? (e->cur > e->anc ? e->cur : e->anc) : u8n(e->s, e->cur); } else return 0; e->anc = e->cur; e->menu = 0; e->movedAt = app_now(); return 1; }
static TEd *teds[8]; static int ted_slot(TEd *e) { for (int i = 0; i < 8; i++) if (teds[i] == e) return i; for (int i = 0; i < 8; i++) if (!teds[i]) { teds[i] = e; return i; } teds[7] = e; return 7; }
static void ted_tap(int slot, int b) { (void)b; TEd *e = teds[slot]; if (!e) return; if (e->hold) { e->hold = 0; e->menu = 1; return; } /* (the end of a hold: its selection stays) */ float px, py; app_pointer_xy(&px, &py); double now = app_now(); int i = idx_at(e, px);
  if (!e->focused) { e->cur = e->anc = i; e->menu = 0; }
  else if (now - e->tapAt < 0.32 && fabsf(px - e->tapX) < 40) { sel_word(e, i); e->menu = 1; pplat_haptic_k(HK_TICK, 0.6f); } /* a double tap: the word */
  else if (e->cur == e->anc && i == e->cur && !e->menu) e->menu = 1; /* on the caret: Paste */
  else { e->cur = e->anc = i; e->menu = 0; }
  e->tapAt = now; e->tapX = px; e->movedAt = now; e->tapped = 1; }
enum { TM_CUT, TM_COPY, TM_PASTE, TM_ALL };
static void ted_menu_act(int slot, int m) { TEd *e = teds[slot]; if (!e) return; int a = e->cur < e->anc ? e->cur : e->anc, b = e->cur < e->anc ? e->anc : e->cur;
  if ((m == TM_CUT || m == TM_COPY) && b > a) { char t[600]; int n = b - a < 599 ? b - a : 599; memcpy(t, e->s + a, n); t[n] = 0; SDL_SetClipboardText(t); if (m == TM_CUT) del_sel(e); else e->anc = e->cur = b; }
  else if (m == TM_PASTE) { char *c = SDL_GetClipboardText(); if (c) { for (char *p = c; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' '; ted_insert(e, c); SDL_free(c); } }
  else if (m == TM_ALL) { e->anc = 0; e->cur = e->len; pplat_haptic_k(HK_TICK, 0.5f); if (e->len) { e->menu = 1; return; } }
  e->menu = 0; e->movedAt = app_now(); }
int ted_field(TEd *e, float x, float y, float w, float h, int px, int weight, uint32_t col, const char *hint, uint32_t hintCol, int focused, int secret) {
  int slot = ted_slot(e); e->px = px; e->weight = weight; e->secret = secret; e->lx = x; e->lw = w;
  { int l = (int)strlen(e->s); if (l != e->len) ted_reset(e); } if (e->cur > e->len) e->cur = e->len; if (e->anc > e->len) e->anc = e->len;
  if (!focused) { e->menu = 0; e->hold = 0; } e->focused = focused;
  char d[600]; ted_disp(e, d, sizeof d); float lh = text_line_height(px), ty = y + h / 2 - lh / 2.0f, total = text_width(d, px, weight, 0), room = w - 6;
  /* the finger: a hold selects (and drags the selection on), a slide moves the caret */
  { float fx, fy, fx0, fy0; int held, down = ph_finger(&fx, &fy, &fx0, &fy0, &held);
    if (down && focused && fx0 >= x - 12 && fx0 < x + w + 12 && fy0 >= y && fy0 < y + h) {
      if (held && !e->hold && !e->drag) { e->hold = 1; sel_word(e, idx_at(e, fx0)); e->menu = 0; pplat_haptic_k(HK_POP, 0.7f); }
      else if (e->hold) { int i = idx_at(e, fx); if (i < e->ws) { e->anc = e->we; e->cur = i; } else if (i > e->we) { e->anc = e->ws; e->cur = i; } else { e->anc = e->ws; e->cur = e->we; } e->movedAt = app_now(); }
      else if (e->drag || fabsf(fx - fx0) > 14) { if (!e->drag) e->menu = 0; e->drag = 1; int i = idx_at(e, fx); if (i != e->cur) { e->cur = e->anc = i; e->movedAt = app_now(); pplat_haptic_k(HK_TICK, 0.25f); } } }
    if (!down) { if (e->hold) { e->hold = 0; e->menu = 1; } e->drag = 0; } }
  int flip = ted_flip(d); float cx = pos_of(e, d, e->cur, flip, total); if (cx - e->scroll > room) e->scroll = cx - room; if (cx - e->scroll < 0) e->scroll = cx; if (e->scroll > total - room) e->scroll = total - room; if (e->scroll < 0) e->scroll = 0;
  gfx_clip_push(x - 4, y, w + 8, h);
  if (e->cur != e->anc && focused) { float xa = pos_of(e, d, e->cur < e->anc ? e->cur : e->anc, flip, total), xb = pos_of(e, d, e->cur < e->anc ? e->anc : e->cur, flip, total); if (xa > xb) { float t2 = xa; xa = xb; xb = t2; } gfx_rrect(x + xa - e->scroll - 2, ty - 4, xb - xa + 4, lh + 8, 8, col_alpha(0xffb794ffu, 0.38f)); }
  if (e->len) text_draw(d, x - e->scroll, ty, px, weight, col, 0, 0, ALIGN_LEFT); else { int hp = px; float hw = text_width(hint, hp, weight, 0); if (hw > w - 6) { hp = (int)(px * (w - 6) / hw); if (hp < (int)(px * 0.72f)) hp = (int)(px * 0.72f); } text_draw(hint, x, ty + (lh - text_line_height(hp)) / 2.0f, hp, weight, hintCol, 0, w, ALIGN_LEFT); } /* (a long hint in some languages: smaller, not cut) */
  if (focused && e->cur == e->anc) { double t = app_now() - e->movedAt; if (t < 0.5 || fmod(t, 1.0) < 0.55) gfx_rrect(x + cx - e->scroll + (flip ? -4 : 1), ty - 2, 3, lh + 4, 1.5f, 0xffb794ffu); /* (mirrored words: the caret on their outer side, as unmirrored) */ }
  gfx_clip_pop();
  hit_act(x - 12, y, w + 24, h, ted_tap, slot, 0);
  /* the menu: over the field (under it near the top), at the selection */
  if (e->menu && focused) { int has = e->cur != e->anc, all = has && ((e->cur == 0 && e->anc == e->len) || (e->anc == 0 && e->cur == e->len)); char *clip = SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL; int cp = clip && *clip; if (clip) SDL_free(clip);
    const char *lb[4]; int id[4], n = 0; if (has && !secret) { lb[n] = T("Cut"); id[n++] = TM_CUT; lb[n] = T("Copy"); id[n++] = TM_COPY; } if (cp) { lb[n] = T("Paste"); id[n++] = TM_PASTE; } if (!all && e->len) { lb[n] = T("Select all"); id[n++] = TM_ALL; }
    if (!n) e->menu = 0; else { float mh = 68, pad = 26, ws[4], mw = 0; for (int i = 0; i < n; i++) { ws[i] = text_width(lb[i], F_SMALL, 650, 0) + 2 * pad; mw += ws[i]; }
      float ax = has ? (pos_of(e, d, e->cur, flip, total) + pos_of(e, d, e->anc, flip, total)) / 2 : cx, mx = x + ax - e->scroll - mw / 2, my = y - mh - 10; if (my < INS_T + 8) my = y + h + 10;
      if (mx < 16) mx = 16; if (mx + mw > SB_W - 16) mx = SB_W - 16 - mw;
      float k = ease_apply(EASE_OUT, (float)(app_now() - e->movedAt) / 0.18f); gfx_opacity_push(clampf(k * 1.2f, 0, 1)); gfx_scale_push(0.94f + 0.06f * k, 0.94f + 0.06f * k, mx + mw / 2, my + mh / 2);
      extern void ph_glass_grab_liquid(void); extern void ph_liquid_bubble(float x, float y, float w, float h, float r, float press); gfx_scale_pop(); ph_glass_grab_liquid(); gfx_scale_push(0.94f + 0.06f * k, 0.94f + 0.06f * k, mx + mw / 2, my + mh / 2); /* the page under it, for the bar's liquid glass */
      ph_liquid_bubble(mx, my, mw, mh, mh / 2, 0); gfx_rrect(mx, my, mw, mh, mh / 2, RGBA(14, 12, 20, 70)); /* (a touch darker: its words read over any page) */
      float ix = mx; for (int i = 0; i < n; i++) { int pr = press_k(ix, my, ws[i], mh) > 0.3f; if (pr) gfx_rrect(ix + 4, my + 4, ws[i] - 8, mh - 8, (mh - 8) / 2, WHITE_A(22));
        text_draw(lb[i], ix + ws[i] / 2, my + mh / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 650, WHITE_A(240), 0, 0, ALIGN_CENTER);
        if (i) gfx_rect(ix, my + 20, 1, mh - 40, WHITE_A(30)); hit_act(ix, my, ws[i], mh, ted_menu_act, slot, id[i]); ix += ws[i]; }
      gfx_scale_pop(); gfx_opacity_pop(); } }
  int t = e->tapped; e->tapped = 0; return t; }
void section_title(const char *t, float x, float y) { text_draw(t, x, y, F_SMALL, 800, ACC2, 1.2f, SB_W - 2 * PAD, ALIGN_START); }
/* a switch: its knob glides across, the track fills with the accent, the knob stretches a little on the way (each
   switch kept by its place on the page, so it animates when its value changes) */
void switch_draw(float x, float y, int on) {
  static struct { float x, y; Tween t; int seen; unsigned frame; } sw[24]; static int n; int k = -1;
  for (int i = 0; i < n; i++) if (fabsf(sw[i].x - x) < 4 && fabsf(sw[i].y - y) < 200 && sw[i].frame != app_frame()) { k = i; break; }
  if (k < 0) { k = n < 24 ? n++ : (int)(app_frame() % 24); sw[k].x = x; sw[k].y = y; sw[k].seen = 0; }
  if (!sw[k].seen || fabsf(sw[k].y - y) > 120) { tween_set(&sw[k].t, on); sw[k].seen = 1; } sw[k].x = x; sw[k].y = y; sw[k].frame = app_frame();
  tween_to(&sw[k].t, on, 0.28f, EASE_OUT_BACK); tween_tick(&sw[k].t, phDt); float v = sw[k].t.v, c = clampf(v, 0, 1);
  float w = 82, h = 46, d = h - 8, mid = 4 * c * (1 - c), kw = d + 14 * mid, kx = x + 4 + (w - 8 - kw) * clampf(v, -0.05f, 1.05f);
  gfx_rrect(x, y, w, h, h / 2, col_mix(WHITE_A(50), ACC | 0xff000000u, c)); gfx_shadow(kx, y + 6, kw, d, d / 2, 8, BLACK_A(90)); gfx_rrect(kx, y + 4, kw, d, d / 2, WHITE_A(255)); }
void meta_line_draw(const Item *it, float x, float y, int px, uint32_t color) { MetaParts mp; meta_prep(&mp, it->metaLine, NULL, NULL, px); meta_draw_p(&mp, x, y, color, ALIGN_START); }

float ph_fit_px(const char *t, float px, int weight, float track, float maxW) { maxW *= 0.96f; /* (a little room: the drawn line is a few pixels wider than measured, and was then shortened) */ float w = text_width(t, (int)px, weight, track); if (w <= maxW || w <= 0) return px; float f = px * maxW / w; return f < px * 0.45f ? px * 0.45f : f; } /* (a long title in some languages: smaller, down to 45 %, before ever being cut) */
