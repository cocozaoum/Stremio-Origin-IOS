/* Settings: the account, the addons, how films play, subtitles, audio, the look, the language, and About
   (version, updates, what's new, the log code). The same keys the TV changes, so an account's phone and TV agree
   where the setting is the account's; the rest is this phone's. */
void board_invalidate(void); /* (the phone's Home: rows built again on its next visit) */
#include "core/traktrows.h"
#include "core/traktwatch.h"
#include "core/prevspike.h"
#include "ui/subfonts.h"
#include "core/scrobble.h"
#include "core/trakt.h"
#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include "phone/phone.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/store.h"
#include "core/update.h"
#include "core/logseal.h"
#include "core/tmdb.h"
#include "ui/sound.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/img.h"
#include "ui/widgets.h"
#include "platform/plat.h"
#include "screens/welcome.h"
#include "core/best.h"

typedef struct { const char *v, *lb; } Opt;
const uint32_t *settings_accent_rgb(int i); /* core/app.c */
static const Opt LANGS[] = { { "", "Off" }, { "eng", "English" }, { "ara", "Arabic" }, { "fre", "French" }, { "spa", "Spanish" }, { "spl", "Spanish (LA)" }, { "ger", "German" }, { "ita", "Italian" }, { "por", "Portuguese" }, { "pob", "Portuguese (BR)" }, { "rus", "Russian" }, { "tur", "Turkish" }, { "pol", "Polish" }, { "dut", "Dutch" }, { "gre", "Greek" }, { "rum", "Romanian" }, { "swe", "Swedish" }, { "heb", "Hebrew" }, { "hin", "Hindi" }, { "jpn", "Japanese" }, { "kor", "Korean" }, { "chi", "Chinese" }, { "zht", "Chinese (traditional)" },
  /* every other language the subtitles know, by name */
  { "alb", "Albanian" }, { "arm", "Armenian" }, { "baq", "Basque" }, { "ben", "Bengali" }, { "bos", "Bosnian" }, { "bul", "Bulgarian" }, { "cat", "Catalan" }, { "hrv", "Croatian" }, { "cze", "Czech" }, { "dan", "Danish" }, { "est", "Estonian" }, { "fin", "Finnish" },
  { "glg", "Galician" }, { "geo", "Georgian" }, { "hun", "Hungarian" }, { "ice", "Icelandic" }, { "ind", "Indonesian" }, { "kur", "Kurdish" }, { "lav", "Latvian" }, { "lit", "Lithuanian" }, { "mac", "Macedonian" }, { "may", "Malay" }, { "mal", "Malayalam" },
  { "mon", "Mongolian" }, { "nor", "Norwegian" }, { "per", "Persian" }, { "srp", "Serbian" }, { "sin", "Sinhala" }, { "slo", "Slovak" }, { "slv", "Slovenian" }, { "tgl", "Tagalog" }, { "tam", "Tamil" }, { "tel", "Telugu" }, { "tha", "Thai" }, { "ukr", "Ukrainian" }, { "urd", "Urdu" }, { "vie", "Vietnamese" } }; /* (every language the subtitles know, as the TV) */
static const Opt SEEK[] = { { "5", "5 s" }, { "10", "10 s" }, { "15", "15 s" }, { "20", "20 s" }, { "30", "30 s" } };
static const Opt DLQ[] __attribute__((unused)) = { { "small", "Smaller files" }, { "balanced", "Balanced" }, { "best", "Best quality" } };
static const Opt ASPECT[] = { { "fit", "Original" }, { "fill", "Fill the screen" }, { "zoom", "Zoom" } };
static const Opt SIZES[] = { { "0", "72%" }, { "1", "80%" }, { "2", "100%" }, { "3", "120%" }, { "4", "140%" }, { "5", "160%" }, { "6", "180%" } };
static const Opt COLORS[] = { { "#ffffff", "White" }, { "#ffe600", "Yellow" }, { "#00e5ff", "Cyan" } };
static const Opt OUTL[] = { { "#000000", "Black" }, { "#ffffff", "White" } };
static const Opt BGS[] = { { "0", "None" }, { "1", "Solid" }, { ".6", "Transparent" } };
static const Opt SPOS[] = { { "3", "3%" }, { "5", "5%" }, { "8", "8%" }, { "10", "10%" }, { "13", "13%" }, { "15", "15%" }, { "18", "18%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" } }; /* how high the subtitles sit (the player's Position card goes a percent at a time) */
static const Opt SKIPO[] = { { "auto", "Auto" }, { "show", "Show" }, { "off", "Off" } }; /* intro and recap: skipped by themselves, a button, or nothing */
static const Opt SPOILO[] = { { "off", "Off" }, { "blur", "Blur pictures" }, { "hide", "Blur pictures and text" } }; /* episodes not watched yet */
static const Opt ASSO[] = { { "styled", "Styled" }, { "plain", "Plain" } };
static const Opt VOL[] __attribute__((unused)) = { { "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } };
static const Opt ACCENTS[] = { { "logo", "From the logo" }, { "adaptive", "From the artwork" }, { "purple", "Purple" }, { "violet", "Violet" }, { "fuchsia", "Fuchsia" }, { "pink", "Pink" }, { "rose", "Rose" }, { "red", "Red" }, { "orange", "Orange" }, { "amber", "Amber" }, { "gold", "Gold" }, { "lime", "Lime" }, { "green", "Green" }, { "emerald", "Emerald" }, { "teal", "Teal" }, { "cyan", "Cyan" }, { "sky", "Sky" }, { "blue", "Blue" }, { "indigo", "Indigo" }, { "lavender", "Lavender" }, { "plum", "Plum" }, { "magenta", "Magenta" }, { "coral", "Coral" }, { "peach", "Peach" }, { "sand", "Sand" }, { "olive", "Olive" }, { "mint", "Mint" }, { "ocean", "Ocean" }, { "slate", "Slate" }, { "graphite", "Graphite" } };
#define N(a) ((int)(sizeof(a) / sizeof(a)[0]))

enum { P_MAIN = 0, P_PLAY, P_SUBS, P_AUDIO, P_LOOK, P_LANG, P_ADDONS, P_ABOUT, P_DL, P_TRAKT, P_N };
typedef struct { int page; Scroll v[P_N]; int pickKey; const Opt *pickOpts; int pickN; char code[8]; double codeAt; int syncing; } St;
static St S;
static float rowY;

/* ---- rows: grouped cards (one rounded card per group, rows inside with a fine line between them), each row's icon
   on a small tile of its own colour, the value or a switch on the right ---- */
#define TINT_GREEN RGB(0x2f, 0xb9, 0x6a)
#define TINT_BLUE RGB(0x3a, 0x86, 0xf0)
#define TINT_ORANGE RGB(0xf2, 0x8c, 0x28)
#define TINT_PURPLE RGB(0x8e, 0x5c, 0xf2)
#define TINT_TEAL RGB(0x1f, 0xb5, 0xb0)
#define TINT_PINK RGB(0xec, 0x4f, 0x8c)
#define TINT_GREY RGB(0x6c, 0x6a, 0x7a)
#define TINT_RED RGB(0xe5, 0x48, 0x48)
#define TINT_GOLD RGB(0xe8, 0xb3, 0x2a)
#define RH 100.0f
static float gx, gw, gTop, gH, rres = 200; /* rres: room kept on the right (a value, a switch, a chevron) */ static int gRow, gRows;
static void tk_link(int a, int b);
static int tkLinking;
static void page_act(int p, int b) { (void)b; S.page = p;
  if (p == P_TRAKT && !tkLinking && scrobble_state() != TRAKT_LINKED && trakt_client_id()[0]) tk_link(0, 0); } /* Trakt not connected: its code comes at once (no Connect step first) */
static void card(float x, float y, float w, float h) { ph_glass(x, y, w, h, 28, RGBA(16, 14, 22, 92)); } /* glass, as the bar's pills: the page blurred under it, a light rim */
static float grp_h(float y, float h) { gx = PAD; gw = SB_W - 2 * PAD; gTop = y; gRows = 1; gRow = 0; gH = h; card(gx, y, gw, gH); return y; }
static float grp(float y, int rows) { gx = PAD; gw = SB_W - 2 * PAD; gTop = y; gRows = rows; gRow = 0; gH = rows * RH;
  card(gx, y, gw, gH); return y; }
static float grp_end(void) { return gTop + gH + 30; }
static int rowSubOk; /* the next row may show its line under its name (asked for: Original's) */
static float row_base(float y, const char *icon, uint32_t tint, const char *label, const char *sub, int rh) { if (!rowSubOk) sub = NULL; rowSubOk = 0; /* (names alone: no descriptions, unless asked) */
  gfx_clip_push(gx, gTop, gw, gH); { float pk = press_k(gx, y, gw, rh); if (pk > 0) gfx_rect(gx, y, gw, rh, WHITE_A((int)(16 * pk))); } gfx_clip_pop();
  if (gRow > 0) gfx_rect(gx + (icon ? 96 : 26), y, gw - (icon ? 96 : 26), 1, WHITE_A(14)); gRow++;
  float x = gx + 26; if (icon) { (void)tint; icon_draw(icon, x, y + rh / 2 - 22, 44, tint == TINT_RED ? RGB(0xf0, 0x7a, 0x7a) : WHITE_A(215)); x += 44 + 26; /* the icon alone, quiet, in the words' colour */ }
  float ty = sub && *sub ? y + rh / 2 - 33 : y + rh / 2 - text_line_height(F_BODY) / 2.0f;
  { float av = gx + gw - x - rres, w = text_width(label, F_BODY, 600, 0) + 10; /* (+10: the drawn line is a little wider than measured: it was shortened by a hair) */ /* a long name (some languages): a little smaller, then on two lines, never cut */
    if (w <= av) text_draw(label, x, ty, F_BODY, 600, TXT, 0, av, ALIGN_START);
    else if (w * 0.88f <= av) { int f = (int)(F_BODY * av / w); text_draw(label, x, y + rh / 2 - text_line_height(f) / 2.0f, f, 600, TXT, 0, av, ALIGN_START); }
    else { int f = (int)(F_BODY * 0.88f); while (f > (int)(F_BODY * 0.6f) && text_wrap_count(label, av - 6, 3, f, 600, 0) > 2) f--; int lh = text_line_height(f) - 2, nl = text_wrap_count(label, av, 2, f, 600, 0); /* (two lines, a little smaller still when needed: never cut) */ text_draw_wrapped(label, x, y + rh / 2 - nl * lh / 2.0f, av, 2, f, 600, TXT, 0, ALIGN_START, lh); } }
  rres = 200;
  if (sub && *sub) text_draw(sub, x, y + rh / 2 + 4, F_SMALL - 1, 500, TXT3, 0, gx + gw - x - 120, ALIGN_START);
  return x;
}
static void nav_row(float y, const char *icon, uint32_t tint, const char *label, const char *sub, int page) { rres = 80; row_base(y, icon, tint, label, sub, RH); icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100)); hit_act(gx, y, gw, RH, page_act, page, 0); }
/* a switch: the key holds "true" / "false" */
static const char *swKeys[16]; static int nsw;
static void sw_act(int i, int b) { (void)b; const char *k = swKeys[i]; int def = strcmp(k, "nerdStats") != 0 && strcmp(k, "dlWifi") != 0 && strcmp(k, "seekPreviewsTorrent") != 0; /* (the ones off unless switched on) */ store_set(k, store_bool(k, def) ? "false" : "true"); if (!strcmp(k, "accent")) accent_apply(); if (!strcmp(k, "dlHdr")) store_set("dlQuality", "custom");
  if (!strcmp(k, "traktRows") || !strcmp(k, "traktMarks") || !strcmp(k, "traktCw")) board_invalidate(); } /* (Trakt's rows, marks and pauses come and go on Home) */
static void sw_row(float y, const char *label, const char *sub, const char *key, int def) { rres = 124; row_base(y, NULL, 0, label, sub, RH); switch_draw(gx + gw - 26 - 82, y + RH / 2 - 23, store_bool(key, def)); if (nsw < 16) { swKeys[nsw] = key; hit_act(gx, y, gw, RH, sw_act, nsw, 0); nsw++; } }
/* a choice among options: a sheet */
static const char *chKeys[16]; static const Opt *chOpts[16]; static int chN[16], nch;
static double arPickAt; /* the Arabic font being picked (its list up, or just picked): the preview's line in Arabic */
static void ch_pick(int i) { if (i < 0 || i >= S.pickN) return; store_set(chKeys[S.pickKey], S.pickOpts[i].v); if (!strcmp(chKeys[S.pickKey], "p2pTcpOnly")) { extern void p2p_connections_changed(void); p2p_connections_changed(); } if (!strcmp(chKeys[S.pickKey], "defaultAudioTrack")) store_set("enableDefaultAudioTrack", strcmp(S.pickOpts[i].v, "orig") ? "true" : "false"); /* (kept for older TVs and builds) */ if (!strcmp(chKeys[S.pickKey], "subsFontAr")) arPickAt = app_now(); if (!strcmp(chKeys[S.pickKey], "dlMaxRes") || !strcmp(chKeys[S.pickKey], "dlRate")) store_set("dlQuality", "custom"); /* (a choice of its own: the profile is theirs) */ if (!strcmp(chKeys[S.pickKey], "accent")) accent_apply(); }
#define AR_SAMPLE "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7" /* (the Arabic fonts' sample, the same word everywhere: مرحبا) */
static int font_wt(const char *key, const char *v, int w) { if (!strcmp(key, "subsFont")) return text_sub_weight(w, atoi(v), 0); if (!strcmp(key, "subsFontAr")) return text_sub_weight(w, 0, atoi(v)); return w; } /* a font's own name drawn in it */
void choices_weights(const int *wt, int n); int choices_up(void);
static void ch_act(int i, int b) { (void)b; S.pickKey = i; S.pickOpts = chOpts[i]; S.pickN = chN[i]; const char *lines[20]; static char lbuf[20][96]; int wts[20]; const char *cur = store_get(chKeys[i], chOpts[i][0].v); int c = 0, ar = !strcmp(chKeys[i], "subsFontAr");
  for (int k = 0; k < chN[i] && k < 20; k++) { lines[k] = T(chOpts[i][k].lb); wts[k] = font_wt(chKeys[i], chOpts[i][k].v, 600); if (wts[k] == 600) wts[k] = 0; if (ar) { snprintf(lbuf[k], sizeof lbuf[k], "%s  " AR_SAMPLE, lines[k]); lines[k] = lbuf[k]; } if (!strcmp(chOpts[i][k].v, cur)) c = k; } /* (a font: each name in itself, an Arabic one with a word in it) */
  choices_open("", lines, chN[i] < 20 ? chN[i] : 20, c, ch_pick); choices_weights(wts, chN[i] < 20 ? chN[i] : 20); if (ar) arPickAt = app_now(); }
/* a value at a row's right: its room, so the name beside it never runs into it (the name shrinks, then takes two lines;
   a value longer than 40% of the row is shortened instead) */
static int vpx = F_SMALL + 1; /* the value's size: smaller to fit before it is ever shortened */
static float value_room(const char *v, int wt) { vpx = F_SMALL + 1; float vw = text_width(v, vpx, wt, 0), cap = gw * 0.5f; if (vw > cap) { int f = (int)(vpx * cap / vw); if (f < (int)(vpx * 0.6f)) f = (int)(vpx * 0.6f); vpx = f; vw = text_width(v, vpx, wt, 0); if (vw > cap) vw = cap; } rres = 26 + 32 + 8 + 20 + vw; return vw; }
static void ch_row(float y, const char *label, const char *sub, const char *key, const Opt *o, int n, const char *def) {
  const char *cur = store_get(key, def); const char *lb = cur; for (int k = 0; k < n; k++) if (!strcmp(o[k].v, cur)) lb = T(o[k].lb);
  char lb2[120]; if (!strcmp(key, "subsFontAr")) { snprintf(lb2, sizeof lb2, "%s  " AR_SAMPLE, lb); lb = lb2; } /* (a font's value in itself; an Arabic one with a word in it) */
  float vw = value_room(lb, font_wt(key, cur, 600)); row_base(y, NULL, 0, label, sub, RH);
  text_draw(lb, gx + gw - 26 - 32 - 8, y + RH / 2 - text_line_height(vpx) / 2.0f, vpx, font_wt(key, cur, 600), ACC2, 0, vw + 1, ALIGN_END); icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100));
  if (nch < 16) { chKeys[nch] = key; chOpts[nch] = o; chN[nch] = n; hit_act(gx, y, gw, RH, ch_act, nch, 0); nch++; } }
/* the audio language, in Playback: Original (each film's own track, as it starts) first and by default, a brief line under
   it; else a language, picked by itself when a film has it (as the TVs, main b16ba972: the old Auto audio track switch
   folded in, off was Original) */
static void aud_row(float y) { static Opt AUDO[N(LANGS)]; static int made; if (!made) { made = 1; AUDO[0].v = "orig"; AUDO[0].lb = "Original"; for (int i = 1; i < (int)N(LANGS); i++) AUDO[i] = LANGS[i]; }
  if (!store_get("audOrigDone", NULL)) { if (strcmp(store_get("enableDefaultAudioTrack", "false"), "true") || !store_get("defaultAudioTrack", "")[0]) store_set("defaultAudioTrack", "orig"); store_set("audOrigDone", "1"); } /* (once: the switch off, or no language, was the original track) */
  const char *cur = store_get("defaultAudioTrack", "orig"); int orig = !strcmp(cur, "orig"); const char *lb = cur; for (int k = 0; k < (int)N(LANGS); k++) if (!strcmp(AUDO[k].v, cur)) lb = T(AUDO[k].lb);
  float vw = value_room(lb, 600); rowSubOk = orig; row_base(y, "st_audio", TINT_ORANGE, T("Default audio language"), orig ? T("Each film in its own language") : NULL, RH);
  text_draw(lb, gx + gw - 26 - 32 - 8, y + RH / 2 - text_line_height(vpx) / 2.0f, vpx, 600, ACC2, 0, vw + 1, ALIGN_END); icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100));
  if (nch < 16) { chKeys[nch] = "defaultAudioTrack"; chOpts[nch] = AUDO; chN[nch] = N(LANGS); hit_act(gx, y, gw, RH, ch_act, nch, 0); nch++; } }
static float act_row(float y, const char *icon, uint32_t tint, const char *label, const char *sub, HitFn act, int a) { row_base(y, icon, tint, label, sub, RH); if (act) { icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100)); hit_act(gx, y, gw, RH, act, a, 0); } return RH; }
static float head(float y, const char *t) { text_draw(t, PAD + 12, y + 10, F_SMALL, 800, TXT3, 0.6f, 0, ALIGN_START); return 54; }

/* ---- the actions ---- */
static void logout_act(int a, int b) { (void)a; (void)b; if (sb_signed_in()) sb_logout(); store_flush(); app_go(SCR_LOGIN, NULL, 1); }
static void on_sync(int changed, void *ud) { (void)changed; (void)ud; S.syncing = 0; toast(T("Addons updated"), 1800, NULL); }
static void sync_act(int a, int b) { (void)a; (void)b; if (S.syncing) return; S.syncing = 1; sb_sync_addons(on_sync, NULL); }
/* an addon's own sheet: its name and what it does, on or off on this phone, set it up (its page), move it up or
   down, remove it from the account */
static int adSel = -1;
static void ad_done(int ok, void *ud) { (void)ud; toast(ok ? T("Addons updated") : T("Could not change the addons"), 1800, NULL); app_refresh_boards(); }
static void ad_toggle(int a, int b) { (void)a; (void)b; cJSON *list = sb_addons(); const cJSON *ad = cJSON_GetArrayItem(list, adSel); if (!ad) return; char key[200], sk[220]; sb_addon_key(ad, key, sizeof key); snprintf(sk, sizeof sk, "adOn_%s", key); store_set(sk, sb_addon_on(ad) ? "false" : "true"); app_refresh_boards(); }
static void ad_conf(int a, int b) { (void)a; (void)b; const cJSON *ad = cJSON_GetArrayItem(sb_addons(), adSel); char u[1100]; if (ad && sb_addon_configure_url(ad, u, sizeof u)[0] && !pplat_open_url(u)) toast(u, 3000, NULL); }
static void ad_remove(int a, int b) { (void)a; (void)b; int i = adSel; ph_sheet_close(); sb_addon_remove(i, ad_done, NULL); }
static void ad_draw(float a) {
  cJSON *list = sb_addons(); const cJSON *ad = cJSON_GetArrayItem(list, adSel); if (!ad) { ph_sheet_close(); return; } const cJSON *mf = js_obj(ad, "manifest"); int n = cJSON_GetArraySize(list);
  const char *nm = js_str(mf, "name", "?"), *ds = js_str(mf, "description", ""), *ver = js_str(mf, "version", ""); char conf[1100]; sb_addon_configure_url(ad, conf, sizeof conf); int prot = sb_addon_protected(ad);
  int dl = ds[0] ? text_wrap_count(ds, SB_W - 2 * PAD, 5, F_SMALL + 1, 400, 0) : 0; int rows = 1 + (conf[0] ? 1 : 0) + (prot ? 0 : 1);
  float h = 310 + dl * 31 + 30 + rows * RH + INS_B + 40; if (h > SB_H - INS_T - 20) h = SB_H - INS_T - 20; float y = sheet_panel(h, a); /* (every row in: Remove was below the card) */
  { const char *lg = js_str(mf, "logo", ""); float ls = 96; GLuint t = lg[0] ? img_get_shaped(lg, (int)ls, (int)ls, 24, 2) : 0; /* its own logo, else its first letter */
    if (!t || !ph_picture(t, SB_W / 2 - ls / 2, y + 40, ls, ls, 24, WHITE_A(255))) { gfx_rrect(SB_W / 2 - ls / 2, y + 40, ls, ls, 24, col_alpha(ACC, 0.35f)); char c1[4] = { nm[0] ? nm[0] : '?', 0 }; text_upper(c1, sizeof c1); text_draw(c1, SB_W / 2, y + 40 + ls / 2 - text_line_height(F_H1) / 2.0f, F_H1, 900, ACC3, 0, 0, ALIGN_CENTER); } }
  text_draw(nm, SB_W / 2, y + 156, F_H2, 800, TXT, -0.2f, SB_W - 2 * PAD, ALIGN_CENTER); if (ver[0]) { char v[40]; snprintf(v, sizeof v, "v%s", ver); text_draw(v, SB_W / 2, y + 204, F_SMALL, 700, TXT3, 0, 0, ALIGN_CENTER); }
  float ty = y + 250; if (dl) { text_draw_wrapped(ds, SB_W / 2, ty, SB_W - 2 * PAD, 5, F_SMALL + 1, 400, TXT2, 0, ALIGN_CENTER, 31); ty += dl * 31; } ty += 30;
  grp(ty, rows); float ry = ty;
  row_base(ry, NULL, 0, T("On on this phone"), NULL, RH); switch_draw(gx + gw - 26 - 82, ry + RH / 2 - 23, sb_addon_on(ad)); hit_act(gx, ry, gw, RH, ad_toggle, 0, 0); ry += RH;
  if (conf[0]) { act_row(ry, "st_link", TINT_BLUE, T("Configure"), T("Opens its own page"), ad_conf, 0); ry += RH; }
  (void)n;
  if (!prot) { row_base(ry, "st_out", TINT_RED, T("Remove from your account"), NULL, RH); hit_act(gx, ry, gw, RH, ad_remove, 0, 0); }
}
static const PhSheet adSheet = { ad_draw, NULL, NULL };
/* adding one: from a link copied anywhere (the addon's install page, a catalog of addons) */
static void add_paste(int a, int b) { (void)a; (void)b; char *c = SDL_GetClipboardText(); if (!c || !*c) { toast(T("Copy an addon\xe2\x80\x99s link first"), 2200, NULL); SDL_free(c); return; } toast(T("Adding the addon\xe2\x80\xa6"), 1500, NULL); sb_addon_install(c, ad_done, NULL); SDL_free(c); ph_sheet_close(); }
static void add_find(int a, int b) { (void)a; (void)b; if (!pplat_open_url("https://stremio-addons.net")) toast("stremio-addons.net", 2500, NULL); }
static void add_draw(float a) { float h = 520 + INS_B, y = sheet_panel(h, a);
  text_draw(T("Add an addon"), PAD, y + 46, F_H2, 800, TXT, -0.2f, 0, ALIGN_START);
  text_draw_wrapped(T("Copy the link of an addon (its manifest.json, or the stremio:// link of an install button), then paste it here."), PAD, y + 104, SB_W - 2 * PAD, 4, F_SMALL + 1, 500, TXT2, 0, ALIGN_START, 31);
  button(PAD, y + 250, SB_W - 2 * PAD, 84, "ph_plus", T("Paste the link"), 1, add_paste, 0, 0);
  button(PAD, y + 350, SB_W - 2 * PAD, 84, "ph_ext", T("Find addons"), 0, add_find, 0, 0); }
static const PhSheet addSheet = { add_draw, NULL, NULL };
static void add_act(int a, int b) { (void)a; (void)b; ph_sheet_open(&addSheet); }
/* a line under a group that comes and goes as the others in Settings do: it fades in, slides into place and the
   rows under it make room smoothly (0.34 s, the pages' ease) */
static float soft_hint(float y, int on, const char *text, int slot) { static Tween t[4]; static int seen[4]; if (!seen[slot]) { seen[slot] = 1; tween_set(&t[slot], on); }
  tween_to(&t[slot], on ? 1 : 0, 0.34f, EASE_WEB); tween_tick(&t[slot], phDt); float k = t[slot].v; if (k <= 0.005f) return y;
  int nl = text_wrap_count(text, SB_W - 2 * PAD - 24, 3, F_SMALL, 500, 0); float h = (nl * 30 + 14) * k;
  gfx_clip_push(0, y - 20, SB_W, h + 6); gfx_opacity_push(k); gfx_offset_push(0, -14 * (1 - k)); text_draw_wrapped(text, PAD + 12, y - 16, SB_W - 2 * PAD - 24, 3, F_SMALL, 500, TXT3, 0, ALIGN_START, 30); gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
  return y + h; }
/* the external player: Off, ask each time, or one of the phone's video players */
static char xpPk[16][120], xpLb[16][80]; static int xpN;
static void xp_pick(int i) { if (i == 0) { store_set("extPlayer", ""); store_set("extPlayerName", ""); } else if (i == 1) { store_set("extPlayer", "ask"); store_set("extPlayerName", ""); } else if (i - 2 < xpN) { store_set("extPlayer", xpPk[i - 2]); store_set("extPlayerName", xpLb[i - 2]); } store_flush(); }
static void xp_act(int a, int b) { (void)a; (void)b; char buf[4000]; pplat_video_players(buf, sizeof buf); xpN = 0;
  for (char *l = buf; *l && xpN < 12;) { char *nl = strchr(l, '\n'); if (nl) *nl = 0; char *tb = strchr(l, '\t'); if (tb) { *tb = 0; snprintf(xpLb[xpN], sizeof xpLb[0], "%s", l); snprintf(xpPk[xpN], sizeof xpPk[0], "%s", tb + 1); xpN++; } if (!nl) break; l = nl + 1; }
  const char *ls[14]; ls[0] = T("Built-in player"); ls[1] = T("Ask every time"); for (int i = 0; i < xpN; i++) ls[2 + i] = xpLb[i];
  const char *cur = store_get("extPlayer", ""); int ci = !cur[0] ? 0 : !strcmp(cur, "ask") ? 1 : 0; for (int i = 0; i < xpN; i++) if (!strcmp(cur, xpPk[i])) ci = 2 + i;
  choices_open(T("Play with"), ls, 2 + xpN, ci, xp_pick); }
static void addon_open(int i, int b) { (void)b; adSel = i; ph_sheet_open(&adSheet); }
static void check_act(int a, int b) { (void)a; (void)b; update_check(1); }
static void install_act(int a, int b) { (void)a; (void)b; update_install(); } /* downloaded, its SHA-256 checked, handed to the system's installer */
static void news_act(int a, int b) { (void)a; (void)b; void ph_whatsnew_show(void); ph_whatsnew_show(); }
static void url_act(int a, int b) { (void)b; const char *u = a == 2 ? "https://discord.gg/SJEtyV3kW6" : a ? welcome_github_url() : welcome_support_url(); if (u && *u && !pplat_open_url(u)) toast(u, 3000, NULL); }
static void swatch_act(int i, int b) { (void)b; store_set("accent", i == 0 ? "logo" : i == 1 ? "adaptive" : ACCENT_KEYS[i - 2]); accent_apply(); }
/* Share logs, as on the TV: the logs sealed (only the developer can open them) and left on the relay; the 4-letter
   code finds them. A sheet says so, shows the code (the same one for 10 minutes), and opens a new issue with it in */
#include "core/applog.h"
#define SB_ISSUES "https://github.com/stremio-origin/android-phone/issues/new" /* the phone app's own repository */
static struct { int state; char out[16]; } LG;
static void lg_issue(int a, int b) { (void)a; (void)b; char u[600]; snprintf(u, sizeof u, "%s?title=%s&body=Log%%20code%%3A%%20%s%%0AVersion%%3A%%20%s%%20(%s)%%0A%%0AWhat%%20happened%%3A%%20", SB_ISSUES, "Issue%20from%20the%20phone", LG.out, update_current(), "Android"); if (!pplat_open_url(u)) toast(u, 3000, NULL); }
static void lg_close(int a, int b) { (void)a; (void)b; ph_sheet_close(); }
static void lg_draw(float a) {
  if (LG.state == 0) { char o[16]; int st = applog_share_state(o, sizeof o); if (st == 1 && strlen(o) == 4) { LG.state = 1; snprintf(LG.out, sizeof LG.out, "%s", o); snprintf(S.code, sizeof S.code, "%s", o); } else if (st < 0) { LG.state = -1; snprintf(LG.out, sizeof LG.out, "%s", o); } }
  float h = 640 + INS_B, y = sheet_panel(h, a), cw = SB_W - 2 * PAD;
  text_draw(T("Share logs"), SB_W / 2, y + 44, F_H2, 800, TXT, -0.2f, 0, ALIGN_CENTER);
  text_draw_wrapped(T("The app's logs are sealed so only the developer can read them, and a code finds them. Give the code with what happened: nothing personal is in them."), SB_W / 2, y + 104, cw - 20, 4, F_SMALL + 1, 500, TXT2, 0, ALIGN_CENTER, 32);
  float cy = y + 260; gfx_rrect(PAD, cy, cw, 140, 26, SURF2);
  if (LG.state == 1) text_draw(LG.out, SB_W / 2, cy + 70 - text_line_height(72) / 2.0f, 72, 900, ACC2, 18, 0, ALIGN_CENTER);
  else if (LG.state == 0) { spinner(SB_W / 2, cy + 70, 20); }
  else text_draw_wrapped(T(!strcmp(LG.out, "net") || !strcmp(LG.out, "timeout") ? "No internet connection" : "Couldn\xe2\x80\x99t share the logs. Try again in a moment"), SB_W / 2, cy + 46, cw - 40, 2, F_BODY, 600, TXT2, 0, ALIGN_CENTER, 34);
  float by = cy + 170; if (LG.state == 1) { button(PAD, by, cw, 80, "ph_ext", T("Report an issue with this code"), 1, lg_issue, 0, 0); by += 96; }
  button(PAD, by, cw, 80, NULL, T("Close"), 0, lg_close, 0, 0);
}
static const PhSheet lgSheet = { lg_draw, NULL, NULL };
static void logs_act(int a, int b) { (void)a; (void)b; LG.state = 0; LG.out[0] = 0; if (!applog_share_start()) { LG.state = -1; snprintf(LG.out, sizeof LG.out, "seal"); } ph_sheet_open(&lgSheet); }
static void tmdb_act(int a, int b) { (void)a; (void)b; tmdb_key_clear(); toast(T("Titles and descriptions in English"), 2000, NULL); }
static void lang_pick(int i) { if (i < 0 || i >= i18n_lang_count() + 1) return; store_set("uiLang", i == 0 ? "auto" : i18n_lang_code(i - 1)); store_flush(); toast(T("The app restarts in the new language"), 2000, NULL); i18n_init(plat_asset_dir(), store_get("uiLang", "auto")); text_set_rtl(i18n_rtl()); }
static void lang_row(int i, int b) { (void)b; const char *cur = store_get("uiLang", "auto"); const char *code = i ? i18n_lang_code(i - 1) : "auto"; if (strcmp(cur, code)) lang_pick(i); }
__attribute__((unused)) static void lang_act(int a, int b) { (void)a; (void)b; const char *lines[40]; static char buf[40][64]; int n = i18n_lang_count(); if (n > 80) n = 80; snprintf(buf[0], 64, "%s", T("Auto")); lines[0] = buf[0]; const char *cur = store_get("uiLang", "auto"); int c = 0;
  for (int i = 0; i < n; i++) { snprintf(buf[i + 1], 64, "%s", i18n_lang_name(i18n_lang_code(i))); lines[i + 1] = buf[i + 1]; if (!strcmp(cur, i18n_lang_code(i))) c = i + 1; } choices_open(T("Language"), lines, n + 1, c, lang_pick); }

void pplat_haptic(void);
/* ---- Downloads: how much room they take and where, then how good (three steps, said plainly what each means; their
   own values under Customize), then the network, then the place: in the app, a folder of the phone's own choosing, or
   the card ---- */
#include <sys/statvfs.h>
void pplat_pick_folder(void); int pplat_folder_take(char *out, size_t cap); void pplat_dl_dirs(char *out, size_t cap);
long long ph_dl_bytes(void); void ph_dl_remove_all(void);
static int dlAdv; static Tween dlAdvT;
/* a row of choices side by side, the lit one gliding (no sheet to open): a label on the left, the segments on the right */
static const char *segKeys[12]; static const Opt *segOpts[12]; static int nseg; static int segCustom[12];
static void seg_act(int k, int i) { store_set(segKeys[k], segOpts[k][i].v); if (segCustom[k]) store_set("dlQuality", "custom"); pplat_haptic(); }
static void seg_row(float y, const char *label, const char *key, const Opt *o, int n, const char *def, int custom) {
  rres = 0; row_base(y, NULL, 0, label, NULL, RH); const char *cur = store_get(key, def); int ci = 0; for (int i = 0; i < n; i++) if (!strcmp(o[i].v, cur)) ci = i;
  float sw = 0, ws[8]; for (int i = 0; i < n; i++) { ws[i] = text_width(T(o[i].lb), F_SMALL, 700, 0) + 34; if (ws[i] < 70) ws[i] = 70; sw += ws[i]; }
  float sh = 60, sx = gx + gw - 22 - sw, sy = y + RH / 2 - sh / 2; static Tween gl[12][2]; static const char *glKey[12]; int k = -1; /* its glide kept by its setting (not its place: a row hidden above never hands it another's) */
  for (int q = 0; q < 12; q++) if (glKey[q] && !strcmp(glKey[q], key)) { k = q; break; } if (k < 0) for (int q = 0; q < 12; q++) if (!glKey[q]) { glKey[q] = key; k = q; break; } if (k < 0) k = 11;
  float lx = sx; for (int i = 0; i < ci; i++) lx += ws[i]; if (gl[k][1].v <= 0) { tween_set(&gl[k][0], lx); tween_set(&gl[k][1], ws[ci]); } tween_to(&gl[k][0], lx, 0.3f, EASE_WEB); tween_to(&gl[k][1], ws[ci], 0.3f, EASE_WEB); tween_tick(&gl[k][0], phDt); tween_tick(&gl[k][1], phDt);
  gfx_rrect(sx - 4, sy - 4, sw + 8, sh + 8, (sh + 8) / 2, WHITE_A(14)); gfx_rrect(gl[k][0].v, sy, gl[k][1].v, sh, sh / 2, WHITE_A(235));
  float x = sx; for (int i = 0; i < n; i++) { float c = clampf(1 - fabsf(x - gl[k][0].v) / ws[i], 0, 1); text_draw(T(o[i].lb), x + ws[i] / 2, sy + sh / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, col_mix(WHITE_A(210), RGB(14, 12, 20), c), 0, 0, ALIGN_CENTER);
    if (nseg < 12) hit_act(x, sy - 16, ws[i], sh + 32, seg_act, nseg, i); x += ws[i]; }
  if (nseg < 12) { segKeys[nseg] = key; segOpts[nseg] = o; segCustom[nseg] = custom; nseg++; } }
static const Opt DLQ3[] = { { "small", "Smaller" }, { "balanced", "Balanced" }, { "best", "Best" } };
static const Opt DLRES2[] = { { "0", "Auto" }, { "720", "720p" }, { "1080", "1080p" }, { "2160", "4K" } };
static const Opt DLRATE2[] = { { "1.5", "1.5" }, { "2", "2" }, { "4", "4" }, { "8", "8" }, { "0", "\xe2\x88\x9e" } };
static const Opt DLMODE[] = { { "fast", "Fast" }, { "order", "In order" } };
static const Opt DLONCE2[] = { { "1", "1" }, { "2", "2" }, { "3", "3" }, { "0", "All" } };
static void dladv_act(int a, int b) { (void)a; (void)b; dlAdv = !dlAdv; }
static void dlq_act(int i, int b) { (void)b; store_set("dlQuality", DLQ3[i].v); pplat_haptic(); }
static void loc_act(int i, int b) { (void)b; if (i == 1) { pplat_pick_folder(); return; } char all[2000]; pplat_dl_dirs(all, sizeof all);
  if (i == 0) store_set("dlDir", ""); else { char *c = strstr(all, "card\t"); if (c) { char *p = c + 5, *e = strchr(p, '\t'); if (e) *e = 0; store_set("dlDir", p); } } pplat_haptic(); }
static void delall_pick(int i) { if (i == 1) { ph_dl_remove_all(); toast(T("All downloads deleted"), 1800, NULL); } }
static void delall_act(int a, int b) { (void)a; (void)b; const char *l[2] = { T("Keep them"), T("Delete all downloads") }; choices_open(T("Delete every download from this phone?"), l, 2, 0, delall_pick); }
static void gb_s(double b, char *o, size_t c) { if (b >= 1e9) snprintf(o, c, "%.1f GB", b / 1e9); else snprintf(o, c, "%.0f MB", b / 1e6); }
static float dl_page(float y) {
  nseg = 0; { char got[700]; int r = pplat_folder_take(got, sizeof got); if (r > 0) { store_set("dlDir", got); toast(T("Downloads go there now"), 1800, NULL); } else if (r < 0) toast(T("That folder can\xe2\x80\x99t be written to"), 2400, NULL); } /* a folder picked */
  const char *dir = store_get("dlDir", ""); char where[700]; if (*dir) snprintf(where, sizeof where, "%s", dir); else snprintf(where, sizeof where, "%s", store_data_dir());
  static char all[2000]; static double allAt = -9; if (app_now() - allAt > 3) { allAt = app_now(); pplat_dl_dirs(all, sizeof all); } /* (asked of the phone every few seconds, not every frame: it is slow) */ char card[600] = ""; { char *c = strstr(all, "card\t"); if (c) { char *p = c + 5, *e = strchr(p, '\t'); if (e) snprintf(card, sizeof card, "%.*s", (int)(e - p), p); } }
  int loc = !*dir ? 0 : card[0] && !strcmp(dir, card) ? 2 : 1;
  /* the room: downloads, the rest, what is free, on the place they go */
  { static double tot, fr, mine, vAt = -9; if (app_now() - vAt > 2) { vAt = app_now(); struct statvfs v; tot = fr = 0; if (!statvfs(where, &v)) { tot = (double)v.f_blocks * v.f_frsize; fr = (double)v.f_bavail * v.f_frsize; } mine = (double)ph_dl_bytes(); } double other = tot - fr - mine; if (other < 0) other = 0;
    float h = 190; grp_h(y, h); float x = gx + 28, w = gw - 56;
    char a1[32], a2[32], ln[120]; gb_s(mine, a1, sizeof a1); gb_s(fr, a2, sizeof a2);
    text_draw(a1, x, y + 26, F_H2, 800, TXT, -0.3f, 0, ALIGN_START); float aw = text_width(a1, F_H2, 800, -0.3f); text_draw(T("of downloads"), x + aw + 12, y + 26 + text_line_height(F_H2) - text_line_height(F_SMALL) - 4, F_SMALL, 600, TXT2, 0, 0, ALIGN_START);
    snprintf(ln, sizeof ln, "%s %s", a2, T("free"));  text_draw(ln, x + w, y + 26 + text_line_height(F_H2) - text_line_height(F_SMALL) - 4, F_SMALL, 600, TXT2, 0, 0, ALIGN_END);
    float by = y + 100, bh = 18; static Tween m1, m2; float f1 = tot > 0 ? (float)(mine / tot) : 0, f2 = tot > 0 ? (float)(other / tot) : 0; if (f1 > 0 && f1 < 0.012f) f1 = 0.012f;
    tween_to(&m1, f1, 0.6f, EASE_WEB); tween_to(&m2, f2, 0.6f, EASE_WEB); tween_tick(&m1, phDt); tween_tick(&m2, phDt);
    gfx_rrect(x, by, w, bh, bh / 2, WHITE_A(22)); gfx_clip_push(x, by, w, bh); gfx_rrect(x, by, w * (m1.v + m2.v) + bh, bh, bh / 2, WHITE_A(70)); gfx_rrect(x, by, w * m1.v + bh * 0.5f, bh, bh / 2, ACC2 | 0xff000000u); gfx_clip_pop();
    const char *ln2 = loc == 0 ? T("In the app") : loc == 2 ? T("SD card") : dir; const char *em = strstr(ln2, "/emulated/0/"); char sh[700]; if (em) snprintf(sh, sizeof sh, "%s  \xc2\xb7  %s", T("Phone"), em + 12); else snprintf(sh, sizeof sh, "%s", ln2);
    icon_draw("ph_down", x, by + 40, 30, TXT3); text_draw(sh, x + 40, by + 42, F_SMALL, 600, TXT3, 0, w - 40, ALIGN_START); y = grp_end(); }
  /* who chooses the stream of a download: the app (with the quality below), or the user each time (then no quality to set) */
  y += head(y, T("Quality")); grp(y, 1); sw_row(y, T("Choose the stream for me"), NULL, "dlAuto", 1); y = grp_end();
  { static Tween autoT; int au = ph_dl_auto(); static int seenA; if (!seenA) { seenA = 1; tween_set(&autoT, au); } tween_to(&autoT, au ? 1 : 0, 0.34f, EASE_WEB); tween_tick(&autoT, phDt); float ak = autoT.v;
    if (ak < 0.99f) { gfx_opacity_push(1 - ak); text_draw_wrapped(T("You choose the stream each time you download."), PAD + 12, y - 14, SB_W - 2 * PAD - 24, 2, F_SMALL, 500, TXT3, 0, ALIGN_START, 30); gfx_opacity_pop(); }
    float top = y; int hn = hit_count(); gfx_clip_push(0, top - 4, SB_W, 2000 * ak + (1 - ak) * 60); gfx_opacity_push(ak); gfx_offset_push(0, -24 * (1 - ak));
  /* how good: three steps and what the one chosen means */
  { const char *q = store_get("dlQuality", "balanced"); int cur = !strcmp(q, "small") ? 0 : !strcmp(q, "best") ? 2 : !strcmp(q, "custom") ? -1 : 1;
    float h = 210; grp_h(y, h); float sx = gx + 22, sw = gw - 44, sh = 72, sy = y + 22, seg = sw / 3; static Tween gq; float tx = cur < 0 ? -1 : sx + cur * seg;
    if (cur >= 0) { if (gq.v <= 0) tween_set(&gq, tx); tween_to(&gq, tx, 0.32f, EASE_WEB); } tween_tick(&gq, phDt);
    gfx_rrect(sx, sy, sw, sh, sh / 2, WHITE_A(14)); if (cur >= 0) gfx_rrect(gq.v + 4, sy + 4, seg - 8, sh - 8, (sh - 8) / 2, WHITE_A(238));
    for (int i = 0; i < 3; i++) { float x = sx + i * seg, c = cur >= 0 ? clampf(1 - fabsf(x - gq.v) / seg, 0, 1) : 0; text_draw(T(DLQ3[i].lb), x + seg / 2, sy + sh / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, col_mix(WHITE_A(220), RGB(14, 12, 20), c), 0, 0, ALIGN_CENTER); hit_act(x, sy, seg, sh, dlq_act, i, 0); }
    double mb; int res, hdr; ph_dl_prefs(&mb, &res, &hdr); char ep[40], line[200], rs[24]; /* what it means: a 45-minute episode's size, the picture at most, HDR */
    if (mb > 0) { gb_s(mb * 1e6 / 8 * 45 * 60, ep, sizeof ep); } else snprintf(ep, sizeof ep, "%s", T("as big as your internet speed allows"));
    snprintf(rs, sizeof rs, "%s", res == 720 ? "720p" : res == 1080 ? "1080p" : res == 2160 ? "4K" : T("the phone\xe2\x80\x99s best"));
    snprintf(line, sizeof line, "%s %s", T("A 45-minute episode:"), mb > 0 ? ep : T("as big as your internet speed allows"));
    text_draw(line, sx + 8, sy + sh + 26, F_SMALL, 700, TXT, 0, sw - 16, ALIGN_START);
    char l2[200]; snprintf(l2, sizeof l2, "%s %s  \xc2\xb7  HDR %s%s", T("Up to"), rs, hdr == 0 ? T("off") : T("when the phone shows it"), cur < 0 ? "  \xc2\xb7  " : ""); if (cur < 0) { size_t L = strlen(l2); snprintf(l2 + L, sizeof l2 - L, "%s", T("your own values")); }
    text_draw(l2, sx + 8, sy + sh + 66, F_SMALL - 1, 600, TXT3, 0, sw - 16, ALIGN_START); y = grp_end(); }
  /* their own values */
  tween_to(&dlAdvT, dlAdv ? 1 : 0, 0.34f, EASE_WEB); tween_tick(&dlAdvT, phDt); float av = dlAdvT.v;
  grp_h(y, RH + 3 * RH * av); { row_base(y, "st_look", TINT_GREY, T("Customize"), NULL, RH); icon_draw_rot("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(140), 90 * av); hit_act(gx, y, gw, RH, dladv_act, 0, 0); }
  if (av > 0.01f) { float top = y + RH; gfx_clip_push(gx, top, gw, 3 * RH * av); gfx_opacity_push(av); gfx_offset_push(0, -20 * (1 - av)); float yy = top; int hn = hit_count();
    seg_row(yy, T("Picture"), "dlMaxRes", DLRES2, N(DLRES2), "0", 1); yy += RH;
    seg_row(yy, T("GB an hour"), "dlRate", DLRATE2, N(DLRATE2), "4", 1); yy += RH;
    sw_row(yy, T("HDR"), NULL, "dlHdr", 1); gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop(); if (av < 0.95f) hit_trim(hn); } /* (taps only once open) */
  y = grp_end();
    gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop(); if (ak < 0.95f) hit_trim(hn);
    y = top + (y - top) * ak + (1 - ak) * 60; }
  /* the network */
  y += head(y, T("Network")); grp(y, 2);
  sw_row(y, T("Wi-Fi only"), NULL, "dlWifi", 0); y += RH;
  seg_row(y, T("At once"), "dlAtOnce", DLONCE2, N(DLONCE2), "0", 0); y = grp_end(); /* (connections: the engine's own business) */
  /* how a file comes: fast (in pieces, at once) or in order (from its start: playable from the phone while it comes) */
  y += head(y, T("Mode")); grp(y, 1); seg_row(y, T("Download mode"), "dlMode", DLMODE, N(DLMODE), "fast", 0); y = grp_end();
  { int ord = !strcmp(store_get("dlMode", "fast"), "order"); int nl = text_draw_wrapped(ord ? T("Watch while it downloads.") : T("Quickest. Watching before it finishes plays online."), PAD + 12, y - 16, SB_W - 2 * PAD - 24, 3, F_SMALL, 500, TXT3, 0, ALIGN_START, 30); y += nl * 30 + 10; }
  /* where: one of them lit */
  y += head(y, T("Location")); int rows = card[0] ? 3 : 2; grp(y, rows);
  for (int i = 0; i < rows; i++) { const char *lb = i == 0 ? T("In the app") : i == 1 ? T("A folder on the phone") : T("SD card"); char sub[700] = "";
    if (i == 1) { const char *em = loc == 1 ? strstr(dir, "/emulated/0/") : NULL; if (loc == 1) snprintf(sub, sizeof sub, "%s", em ? em + 12 : dir); else snprintf(sub, sizeof sub, "%s", T("Choose a folder")); }
    rres = 90; row_base(y, i == 0 ? "st_about" : i == 1 ? "lib_sort" : "ph_down", TINT_GREY, lb, NULL, RH);
    if (sub[0]) { text_draw(sub, gx + gw - 26 - 60, y + RH / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 600, i == loc ? ACC2 : TXT3, 0, gw * 0.42f, ALIGN_END); }
    float cx = gx + gw - 26 - 18, cy = y + RH / 2; gfx_ring(cx - 15, cy - 15, 30, 30, 15, 2, i == loc ? ACC2 : WHITE_A(70)); if (i == loc) gfx_circle(cx, cy, 8, ACC2 | 0xff000000u);
    hit_act(gx, y, gw, RH, loc_act, i, 0); y += RH; }
  y = grp_end();
  grp(y, 1); { row_base(y, NULL, 0, "", NULL, RH); text_draw(T("Delete all downloads"), gx + 26, y + RH / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 700, RGB(0xef, 0x6b, 0x6b), 0, 0, ALIGN_START); hit_act(gx, y, gw, RH, delall_act, 0, 0); } y = grp_end();
  return y; }
int phSettingsPage = -1; /* opened from elsewhere at one of its pages (Downloads' own button) */
static int direct; /* opened at a page from elsewhere: Back and a swipe go back there, not to the list */
static float sbKNow; float ph_settings_back_k(void) { return sbKNow; } /* (how far it is swiped back to where it was opened from: the bar follows) */
static void s_enter(const void *p) { (void)p; bg_clear(); if (phSettingsPage >= 0) { S.page = phSettingsPage; direct = 1; } else { S.page = P_MAIN; direct = 0; } phSettingsPage = -1; S.code[0] = 0; }
void ph_settings_downloads(void) { phSettingsPage = P_DL; }
/* the header: the page's name large in the middle; scrolled, it gathers into the slim glass bar (as the Library's) */
static void s_head(const char *title, float pos) {
  float k = clampf(pos / 140, 0, 1); k = k * k * (3 - 2 * k); phone_top_glass(INS_T + 150 - 60 * k, k);
  float px = F_H1 + 10 - (F_H1 + 10 - F_H2) * k, mw = SB_W - 2 * PAD, fp = ph_fit_px(title, px, 900, -1.0f + 0.6f * k, mw); text_draw(title, SB_W / 2, INS_T + 34 - 18 * k + (text_line_height((int)px) - text_line_height((int)fp)) / 2.0f, (int)fp, 900, TXT, -1.0f + 0.6f * k, mw, ALIGN_CENTER); }
/* the previews: on a real still (the app's own backdrop), centred, as the page and the player will look */
/* the previews' pictures: always the same, Blender's open films (Sintel, Big Buck Bunny, Tears of Steel), never what Home
   happens to show (it changed every visit) */
static const char *const DEMO_ID[3] = { "tt1727587", "tt1254207", "tt2285752" }, *const DEMO_NM[3] = { "Sintel", "Big Buck Bunny", "Tears of Steel" };
static int demo_item(int i, Item *out, char *bg, size_t cap) { i = ((i % 3) + 3) % 3; memset(out, 0, sizeof *out); snprintf(out->id, sizeof out->id, "%s", DEMO_ID[i]); snprintf(out->type, sizeof out->type, "movie"); snprintf(out->name, sizeof out->name, "%s", DEMO_NM[i]);
  snprintf(out->poster, sizeof out->poster, "https://images.metahub.space/poster/medium/%s/img", DEMO_ID[i]); snprintf(out->logo, sizeof out->logo, "https://images.metahub.space/logo/medium/%s/img", DEMO_ID[i]);
  snprintf(bg, cap, "https://images.metahub.space/background/medium/%s/img", DEMO_ID[i]); snprintf(out->background, sizeof out->background, "%s", bg); return 3; }
static GLuint still(float w, float h) { static char u[200]; if (!u[0]) { Item it; demo_item(0, &it, u, sizeof u); } return img_get_shaped(u, (int)w, (int)h, 28, 1); } /* Sintel's still, every time */
static uint32_t hexcol(const char *h) { unsigned v = 0xffffff; if (h && h[0] == '#') sscanf(h + 1, "%x", &v); return 0xff000000u | v; }
static float subPrevY; static int subPrevOn; static void sub_preview_draw(float y);
static float sub_preview(float y) { float w = SB_W - 2 * PAD; subPrevY = y; subPrevOn = 1; return y + w * 9 / 16 + 34; } /* (its place kept; drawn over the rows once they are, pinned under the bar as they scroll: it stays in view) */
static void sub_preview_draw(float y) {
  static const int SZ[] = { 72, 80, 100, 120, 140, 160, 180 }; float w = SB_W - 2 * PAD, h = w * 9 / 16, x = PAD; static Tween ts, tb; static int seen; static uint32_t shown;
  int si = atoi(store_get("subtitleSize", "2")); if (si < 0 || si > 6) si = 2; float sz = 30 * SZ[si] / 100.0f; const char *bgv = store_get("subsBgStyle", "0"); float bga = !strcmp(bgv, "1") ? 1 : !strcmp(bgv, ".6") ? 0.6f : 0;
  uint32_t col = hexcol(store_get("subsColor", "#ffffff")), out = hexcol(store_get("subsOutlineColor", "#000000"));
  { float bk = atoi(store_get("subsBright", "100")) / 100.0f; if (bk > 0 && bk < 1) { col = col_mix(0xff000000u, col, bk); out = col_mix(0xff000000u, out, bk); } } /* (as the player dims them) */
  if (!seen) { seen = 1; tween_set(&ts, sz); tween_set(&tb, bga); shown = col; } tween_to(&ts, sz, 0.32f, EASE_OUT_BACK); tween_to(&tb, bga, 0.3f, EASE_CSS_EASE); tween_tick(&ts, phDt); tween_tick(&tb, phDt); shown = col_mix(shown, col, clampf(phDt * 10, 0, 1));
  gfx_shadow(x, y + 10, w, h, 28, 30, BLACK_A(120)); GLuint t = still(w, h); if (!t || !ph_picture(t, x, y, w, h, 28, WHITE_A(255))) gfx_rrect_v(x, y, w, h, 28, RGB(0x2a, 0x3a, 0x55), RGB(0x10, 0x14, 0x22));
  gfx_rrect_v(x, y + h * 0.45f, w, h * 0.55f, 28, BLACK_A(0), BLACK_A(160)); /* (the shade ends with the picture, its rounded corners: square ones stuck out of the picture's) */ gfx_ring(x, y, w, h, 28, 1.5f, WHITE_A(30));
  int ar = (choices_up() && S.pickKey >= 0 && S.pickKey < nch && !strcmp(chKeys[S.pickKey], "subsFontAr")) || app_now() - arPickAt < 2.5; /* (the Arabic font being set: the line in Arabic) */
  const char *line = ar ? "\xd8\xb9\xd8\xaf\xd8\xaa\xd9\x8f\x20\xd9\x84\xd8\xa3\xd8\xae\xd8\xa8\xd8\xb1\xd9\x83\x20\xd8\xa8\xd8\xb4\xd9\x8a\xd8\xa1\x2e" : T("Where were you last night?"); float px = ts.v, lh = text_line_height((int)px), tw = text_width(line, (int)px, text_sub_weight(700, subfont_lat(), subfont_ar()), 0), ty;
  { int pp = atoi(store_get("subsPosPct", "8")); if (pp < 2 || pp > 30) pp = 8; static Tween pg; static int pgSet; float want = h * pp / 100.0f + 8; /* the Position: the same share of the picture's height as in the player, gliding there */
    if (!pgSet) { tween_set(&pg, want); pgSet = 1; } else if (pg.to != want) tween_to(&pg, want, 0.25f, EASE_OUT); tween_tick(&pg, phDt); ty = y + h - pg.v - lh; if (ty < y + 8) ty = y + 8; }
  int fw = text_sub_weight(700, subfont_lat(), subfont_ar()); float maxW = w - 60; int nl = text_wrap_count(line, maxW, 2, (int)px, fw, 0); if (nl < 1) nl = 1; if (nl > 1) { ty -= (nl - 1) * lh; if (ty < y + 8) ty = y + 8; tw = maxW; } /* (in the subtitle font chosen; a big size: two lines inside the picture, as the player wraps them) */
  if (tb.v > 0.01f) gfx_rrect(SB_W / 2 - tw / 2 - 14, ty - 6, tw + 28, nl * lh + 12, 10, BLACK_A((int)(190 * tb.v)));
  for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2) if (dx || dy) text_draw_wrapped(line, SB_W / 2 + dx, ty + dy, maxW, 2, (int)px, fw, col_alpha(out, 0.85f), 0, ALIGN_CENTER, lh);
  text_draw_wrapped(line, SB_W / 2, ty, maxW, 2, (int)px, fw, shown, 0, ALIGN_CENTER, lh);
  hit_shield(x, y, w, h); } /* (over the rows scrolling under it: a tap on it reaches none of them) */
/* Appearance's preview: a title's page in small. A fixed colour: the app's own picture and name in it. From the logo
   or the artwork: real titles from Home's spotlight in turn (every 3 s, crossfading), each with its own picture, logo
   and colour, and a label saying so (with the colours it took): what will happen on every title */
static float look_preview(float y) {
  float w = SB_W - 2 * PAD, h = w * 0.62f, x = PAD, cx = SB_W / 2; const char *mode = store_get("accent", "logo"); int dyn = !strcmp(mode, "logo") || !strcmp(mode, "adaptive");
  static int curI = -1, prevI = -1; static uint32_t pc[4], cc[4]; static Tween xf; Item it; char bg[1100]; int n = 0;
  if (dyn) { int want = (int)(app_now() / 3.2); n = demo_item(0, &it, bg, sizeof bg); if (n) { want %= n; if (want != curI) { prevI = curI; curI = want; tween_set(&xf, 0); tween_to(&xf, 1, 0.6f, EASE_CSS_EASE); memcpy(pc, cc, sizeof pc); } } }
  tween_tick(&xf, phDt);
  uint32_t ac[4] = { ACC | 0xff000000u, ACC2 | 0xff000000u, ACC3 | 0xff000000u, ACC_D | 0xff000000u }; Item ci; char cbg[1100] = ""; int have = 0;
  if (dyn && n) { have = 1; demo_item(curI, &ci, cbg, sizeof cbg); uint32_t t4[4]; if (accent_tint_of(ci.id, ci.poster, ci.logo, t4)) { for (int k = 0; k < 4; k++) cc[k] = 0xff000000u | t4[k]; } else accent_tint_fetch(ci.id, ci.poster, ci.logo);
    for (int k = 0; k < 4; k++) ac[k] = prevI >= 0 && pc[k] ? col_mix(pc[k], cc[k] ? cc[k] : ac[k], xf.v) : (cc[k] ? cc[k] : ac[k]); }
  gfx_shadow(x, y + 10, w, h, 28, 30, BLACK_A(120));
  if (have) { if (prevI >= 0 && xf.v < 1) { Item pi; char pbg[1100]; demo_item(prevI, &pi, pbg, sizeof pbg); GLuint pt = pbg[0] ? img_get_shaped(pbg, (int)w, (int)h, 28, 1) : 0; if (!pt || !ph_picture(pt, x, y, w, h, 28, WHITE_A(255))) gfx_rrect(x, y, w, h, 28, SURF); }
    GLuint t = cbg[0] ? img_get_shaped(cbg, (int)w, (int)h, 28, 1) : 0; gfx_opacity_push(prevI >= 0 ? xf.v : 1); if (!t || !ph_picture(t, x, y, w, h, 28, WHITE_A(255))) gfx_rrect(x, y, w, h, 28, SURF); gfx_opacity_pop(); }
  else { GLuint t = still(w, h); if (!t || !ph_picture(t, x, y, w, h, 28, WHITE_A(255))) gfx_rrect(x, y, w, h, 28, SURF); }
  gfx_rrect(x, y, w, h, 28, BLACK_A(90)); gfx_rrect_v(x, y, w, h, 28, BLACK_A(60), col_alpha(PAGE | 0xff000000u, 0.92f)); gfx_glow_round(x - w * 0.1f, y + h * 0.35f, w * 1.2f, h, col_alpha(ac[0], 0.35f), x, y, w, h, 28); gfx_ring(x, y, w, h, 28, 1.5f, WHITE_A(30));
  /* its name: the title's logo when it shows a title */
  { float ly = y + h * 0.18f; int drew = 0; if (have && ci.logo[0]) { float lw = w * 0.5f, lh = 84, asp = 0; GLuint lt = img_get(ci.logo, (int)lw, (int)lh, &asp); if (lt && asp > 0) { float ww = lh * asp, hh2 = lh; if (ww > lw) { ww = lw; hh2 = ww / asp; } gfx_opacity_push(prevI >= 0 ? xf.v : 1); gfx_image(lt, cx - ww / 2, ly + 40 - hh2 / 2, ww, hh2, 0, WHITE_A(255), 0, 0, 1, 1); gfx_opacity_pop(); drew = 1; } }
    if (!drew) text_draw(have ? ci.name : "Origin", cx, ly, F_H1, 900, TXT, -0.8f, w - 60, ALIGN_CENTER); }
  uint32_t fg = (((ac[0] >> 16) & 255) * 0.299f + ((ac[0] >> 8) & 255) * 0.587f + (ac[0] & 255) * 0.114f) > 170 ? RGB(14, 12, 20) : WHITE_A(255);
  float py = y + h * 0.18f + 86, pw = 190, ph = 66, gap = 14, rowW = pw + gap + ph + gap + 150, rx = cx - rowW / 2;
  gfx_rrect(rx, py, pw, ph, ph / 2, WHITE_A(245)); icon_draw("play", rx + 26, py + ph / 2 - 17, 34, RGB(14, 12, 20)); text_draw(T("Play"), rx + 72, py + ph / 2 - text_line_height(F_SMALL + 1) / 2.0f, F_SMALL + 1, 800, RGB(14, 12, 20), 0, 0, ALIGN_LEFT);
  rx += pw + gap; gfx_circle(rx + ph / 2, py + ph / 2, ph / 2, col_alpha(ac[0], 0.38f)); icon_draw("ph_check", rx + ph / 2 - 18, py + ph / 2 - 18, 36, ac[2]);
  rx += ph + gap; gfx_rrect(rx, py + 6, 150, ph - 12, (ph - 12) / 2, col_alpha(ac[0], 0.9f)); text_draw(T("Series"), rx + 75, py + ph / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 800, fg, 0, 0, ALIGN_CENTER);
  float bw = w * 0.7f, bx = cx - bw / 2, by = py + ph + 46; gfx_rrect(bx, by, bw, 8, 4, WHITE_A(40)); gfx_rrect(bx, by, bw * 0.62f, 8, 4, ac[0]); gfx_circle(bx + bw * 0.62f, by + 4, 12, WHITE_A(250));
  /* the label: where the colour comes from, the colours it took */
  if (dyn) { const char *lb = !strcmp(mode, "logo") ? T("Colours from each title\xe2\x80\x99s logo") : T("Colours from each title\xe2\x80\x99s artwork"); float tw = text_width(lb, F_TINY, 700, 0), chw = 44 + tw + 18 + 2 * 22 + 8 + 18, chh = 44, chx = x + 16, chy = y + 16, fk = chw > w - 32 ? (w - 32) / chw : 1; gfx_scale_push(fk, fk, chx, chy); /* (its words and dots inside it, with room: never cut by its edge) */
    gfx_rrect(chx, chy, chw, chh, chh / 2, RGBA(10, 10, 16, 170)); gfx_ring(chx, chy, chw, chh, chh / 2, 1, WHITE_A(40)); icon_draw(!strcmp(mode, "logo") ? "st_look" : "ph_lib", chx + 12, chy + chh / 2 - 12, 24, WHITE_A(220));
    text_draw(lb, chx + 44, chy + chh / 2 - text_line_height(F_TINY) / 2.0f, F_TINY, 700, WHITE_A(235), 0, 0, ALIGN_START);
    for (int k = 0; k < 3; k++) { float dx = chx + 44 + tw + 18 + k * 22; gfx_circle(dx, chy + chh / 2, 8, ac[k]); gfx_ring(dx - 8, chy + chh / 2 - 8, 16, 16, 8, 1, WHITE_A(80)); } gfx_scale_pop(); }
  return y + h + 34; }
/* the first page: the account, then the groups */
static float main_list(float y) {
      const cJSON *u = store_node("user"); int in = sb_signed_in(); (void)u;
      y += head(y, T("Watch")); grp(y, 2);
      nav_row(y, "st_play", TINT_GREEN, T("Playback"), T("Best stream, next episode, seek step"), P_PLAY); y += RH;
      nav_row(y, "ph_down", TINT_BLUE, T("Downloads"), NULL, P_DL); y = grp_end();
      y += head(y, T("Screens")); grp(y, 2);
      nav_row(y, "st_look", TINT_PURPLE, T("Appearance"), T("Accent colour"), P_LOOK); y += RH;
      { const char *cur = store_get("uiLang", "auto"); nav_row(y, "st_lang", TINT_TEAL, T("Language"), !strcmp(cur, "auto") ? T("Auto") : i18n_lang_name(cur), P_LANG); } y = grp_end();
      int sup = welcome_support_url()[0] != 0, gh = welcome_github_url()[0] != 0; y += head(y, T("Account and system")); grp(y, 3);
      { char n[16]; snprintf(n, sizeof n, "%d", sb_addon_on_count()); nav_row(y, "st_addon", TINT_PINK, T("Addons"), T1("{n} on", "n", n), P_ADDONS); y += RH; }
      { int ts = scrobble_state(); nav_row(y, "trakt", RGB(0x64, 0x38, 0xd0), "Trakt", ts == TRAKT_LINKED ? (scrobble_user()[0] ? scrobble_user() : T("Connected")) : NULL, P_TRAKT); y += RH; }
      { const char *av = update_avail(); nav_row(y, "st_about", av ? TINT_GREEN : TINT_GREY, T("About"), av ? T1("Version {v} is available", "v", av) : T("Version, updates, logs and support"), P_ABOUT); if (av) gfx_circle(gx + gw - 26 - 32 - 22, y + RH / 2, 8, RGB(0x34, 0xc7, 0x59)); } /* (a newer version out: its green dot, beside the row that installs it) */
      y = grp_end();
      { y += head(y, T("Support the project")); grp(y, 1 + sup + gh); /* under About, on the first page, with its own heading */
        act_row(y, "discord", RGB(0x58, 0x65, 0xf2), T("Join our Discord"), NULL, url_act, 2); y += RH;
        if (sup) { act_row(y, "ph_heart", TINT_PINK, T("Support on Ko-fi"), NULL, url_act, 0); y += RH; }
        if (gh) { act_row(y, "ph_star", TINT_GOLD, T("Star on GitHub"), NULL, url_act, 1); y += RH; } y = grp_end(); }
      /* the account at the foot: the address, and on its right the way out (or in) */
      { grp(y, 1); const char *em = in ? sb_user_email() : T("Not signed in"); float x = gx + 26; icon_draw("st_user", x, y + RH / 2 - 22, 44, WHITE_A(215));
        text_draw(em, x + 70, y + RH / 2 - text_line_height(F_BODY) / 2.0f, F_BODY, 700, TXT, 0, gw - 26 - 80 - 110, ALIGN_START);
        float bx = gx + gw - 26 - 34, by = y + RH / 2, pk = press_k(bx - 40, by - 40, 80, 80); gfx_circle(bx, by, 34, in ? col_alpha(RGB(0xe5, 0x48, 0x48) | 0xff000000u, 0.2f + 0.2f * pk) : col_alpha(ACC, 0.35f + 0.2f * pk));
        icon_draw(in ? "st_out" : "ph_chev", bx - 20, by - 20, 40, in ? RGB(0xf0, 0x7a, 0x7a) : WHITE_A(240)); hit_act(bx - 44, by - 44, 88, 88, logout_act, 0, 0); y = grp_end(); }
  return y; }
/* the first page as a picture under a sub-page being swiped away (nothing in it takes the finger) */
static int page_parent(int p) { return p == P_SUBS || p == P_AUDIO ? P_PLAY : P_MAIN; } /* Subtitles and Audio live inside Playback */
static void s_list(void) { float y = INS_T + 150 - S.v[P_MAIN].pos; nsw = nch = 0; phone_col_push(1100); main_list(y); phone_col_pop(); s_head(T("Settings"), S.v[P_MAIN].pos); } /* (in its column, as when shown: no jump as it lands) */
/* a page's own rows (the list, Playback, Subtitles...): drawn where it is, or under the one swiped back from */
/* Trakt: connected once with its device code (the code shown big, Trakt's page opened with it copied), then a switch
   for what you watch and a way out */
static double tkAt;
static void tk_ready(int ok, void *ud) { (void)ud; if (!ok) { tkLinking = 0; toast(T("Couldn\xe2\x80\x99t reach Trakt"), 2400, NULL); return; } tkLinking = 2; tkAt = app_now(); SDL_SetClipboardText(scrobble_link_code()); }
static void tk_link(int a, int b) { (void)a; (void)b; tkLinking = 1; scrobble_link_start(tk_ready, NULL); }
static void tk_open(int a, int b) { (void)a; (void)b; SDL_SetClipboardText(scrobble_link_code()); if (!pplat_open_url(scrobble_link_url())) toast(scrobble_link_url(), 3000, NULL); }
static int sbGo; /* (defined with the swipe back below) */
static void tk_off(int a, int b) { (void)a; (void)b; if (tkLinking) { scrobble_link_cancel(); tkLinking = 0; sbGo = 1; return; } /* (cancelled: back to Settings, as it came) */ scrobble_unlink(); board_invalidate(); toast(T("Trakt is disconnected"), 2000, NULL); }
static float trakt_page(float y) { int st = scrobble_state();
  if (tkLinking == 2) { int r = scrobble_link_poll(); if (r == 1) { tkLinking = 0; traktrows_forget(); tw_forget(); board_invalidate(); pplat_haptic_k(HK_POP, 1); } /* (connected: Trakt's rows on Home, never another account's kept copy first) */ else if (r < 0) { tkLinking = 0; toast(scrobble_link_error(), 3000, NULL); } }
  if (!trakt_client_id()[0]) { grp(y, 1); act_row(y, "trakt", RGB(0x64, 0x38, 0xd0), T("Connect Trakt"), NULL, NULL, 0); return grp_end(); }
  if (tkLinking) { /* the code, big, and Trakt's page with it */
    float h = 300; grp_h(y, h); const char *code = tkLinking == 2 ? scrobble_link_code() : "";
    if (code[0]) text_draw(code, SB_W / 2, y + 60, F_H1 + 16, 900, TXT, 4, 0, ALIGN_CENTER); else spinner(SB_W / 2, y + 90, 18);
    button(PAD + 40, y + h - 120, SB_W - 2 * PAD - 80, 80, "trakt", T("Open Trakt"), 1, tk_open, 0, 0); y += h + 30;
    grp(y, 1); act_row(y, NULL, 0, T("Cancel"), NULL, tk_off, 0); return grp_end(); }
  if (st == TRAKT_LINKED) { int rows = !traktrows_official(); /* (the account's own Trakt addon gives the rows: no second set, no switch) */
    grp(y, 4 + rows); sw_row(y, T("Track what you watch"), scrobble_user()[0] ? scrobble_user() : NULL, "traktScrobble", 1); y += RH;
    sw_row(y, T("Watched marks from Trakt"), NULL, "traktMarks", 1); y += RH; sw_row(y, T("Continue Watching from Trakt"), NULL, "traktCw", 1); y += RH;
    if (rows) { sw_row(y, T("Trakt rows on Home"), NULL, "traktRows", 1); y += RH; }
    act_row(y, NULL, 0, T("Disconnect Trakt"), NULL, tk_off, 0); return grp_end(); }
  grp(y, 1); act_row(y, "trakt", RGB(0x64, 0x38, 0xd0), st == TRAKT_RELINK ? T("Connect Trakt again") : T("Connect Trakt"), NULL, tk_link, 0); return grp_end(); }
static float page_body(int pg, float y, const char **titleOut) { const char *title = *titleOut;
  switch (pg) {
    case P_MAIN: y = main_list(y);
      break;
    case P_PLAY: title = T("Playback"); {
      double mb = best_net_mbps(); char cs[120] = ""; if (mb > 0) { char n[16]; snprintf(n, sizeof n, "%.0f", mb); snprintf(cs, sizeof cs, "%s", T1("Your connection: {n} Mbps. Play now starts the best stream", "n", n)); }
      grp(y, 1); sw_row(y, T("Play the best stream automatically"), cs[0] ? cs : T("Chosen by itself when a film starts"), "autoBest", 1); y = grp_end();
      y = soft_hint(y, !store_bool("autoBest", 1), T("The app picks the best stream for you, using your languages and your connection."), 0); /* (off: what it would do, briefly) */
      int pv = prev_available() || getenv("SB_PVFAKE"); /* (the seek previews: only where this app carries their decoder) */
      int pvt = pv && store_bool("seekPreviews", 1); /* (its torrent switch: a part of it, opening under it while it is on) */
      static Tween tsub, tlim; static int tSeen; int limOn = pvt && store_bool("seekPreviewsTorrent", 0); if (!tSeen) { tSeen = 1; tween_set(&tsub, pvt); tween_set(&tlim, limOn); }
      tween_to(&tsub, pvt ? 1 : 0, 0.36f, EASE_WEB); tween_tick(&tsub, phDt); tween_to(&tlim, limOn ? 1 : 0, 0.34f, EASE_WEB); tween_tick(&tlim, phDt);
      const float SRH = RH * 0.84f, subX = PAD + 44 + 30; const char *limT = T("Slower than debrid, up to 500 MB extra per stream.");
      float limW = SB_W - PAD - 26 - subX, subH = pv ? SRH * tsub.v : 0, limH = pv && tlim.v > 0.005f ? (text_wrap_count(limT, limW, 3, F_SMALL - 2, 500, 0) * 28 + 16) * tlim.v * tsub.v : 0;
      const char *dataT = T("Uses more data, on mobile data too."); float dataH = pv && tsub.v > 0.005f ? (text_wrap_count(dataT, SB_W - 2 * PAD - 52, 2, F_SMALL - 2, 500, 0) * 28 + 6) * tsub.v : 0; /* (said under Previews while on: the data it takes) */
      grp_h(y, (5 + pv) * RH + dataH + subH + limH);
      sw_row(y, T("Auto-play next episode"), NULL, "enableNextVideo", 1); y += RH;
      ch_row(y, T("Skip intro and recap"), NULL, "skipIntro", SKIPO, N(SKIPO), "auto"); y += RH;
      ch_row(y, T("Seek step"), NULL, "seekStep", SEEK, N(SEEK), "10"); y += RH;
      if (pv) { sw_row(y, T("Previews on the seek bar"), NULL, "seekPreviews", 1); y += RH; }
      if (dataH > 0.5f) { gfx_clip_push(gx, y - 14, gw, dataH + 14); gfx_opacity_push(tsub.v); text_draw_wrapped(dataT, gx + 26, y - 14, gw - 52, 2, F_SMALL - 2, 500, TXT3, 0, ALIGN_START, 28); gfx_opacity_pop(); gfx_clip_pop(); y += dataH; }
      if (subH > 0.5f) { /* "Previews for torrents": its parent's own sub-switch, indented, smaller, joined to it by a thin line, sliding out from under it */
        float k = tsub.v, yy = y + subH - SRH, bx = gx + 44, swX = gx + gw - 26 - 82, cy = yy + SRH / 2;
        gfx_clip_push(gx, y, gw, subH + limH); gfx_opacity_push(k);
        { float pk = press_k(gx, y, gw, subH); if (pk > 0) gfx_rect(gx, y, gw, subH, WHITE_A((int)(16 * pk))); }
        gfx_rect(bx, yy - 6, 2, SRH / 2 + 7, WHITE_A(40)); gfx_rect(bx, cy - 1, 18, 2, WHITE_A(40));
        text_draw(T("Previews for torrents"), subX, cy - text_line_height(F_SMALL) / 2.0f, F_SMALL, 600, TXT, 0, swX - subX - 16, ALIGN_START);
        gfx_scale_push(0.84f, 0.84f, swX + 82, cy); switch_draw(swX, cy - 23, store_bool("seekPreviewsTorrent", 0)); gfx_scale_pop();
        if (limH > 0.5f) { float lk = tlim.v; gfx_opacity_push(lk); gfx_offset_push(0, -8 * (1 - lk)); text_draw_wrapped(limT, subX, y + subH - 4, limW, 3, F_SMALL - 2, 500, TXT3, 0, ALIGN_START, 28); gfx_offset_pop(); gfx_opacity_pop(); }
        gfx_opacity_pop(); gfx_clip_pop();
        if (k > 0.6f && nsw < 16) { swKeys[nsw] = "seekPreviewsTorrent"; hit_act(gx, y, gw, subH, sw_act, nsw, 0); nsw++; }
        y += subH + limH; }
      ch_row(y, T("Picture size"), NULL, "aspectMode", ASPECT, N(ASPECT), "fit"); y += RH;
      { const char *xp = store_get("extPlayer", ""), *xn = !xp[0] ? T("Built-in player") : !strcmp(xp, "ask") ? T("Ask every time") : store_get("extPlayerName", xp); float xw = value_room(xn, 600); row_base(y, NULL, 0, T("External player"), NULL, RH);
        text_draw(xn, gx + gw - 26 - 32 - 8, y + RH / 2 - text_line_height(vpx) / 2.0f, vpx, 600, xp[0] ? ACC2 : TXT3, 0, xw + 1, ALIGN_END); icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100)); hit_act(gx, y, gw, RH, xp_act, 0, 0); y = grp_end();
        /* what goes with another app: said once, plainly */
        y = soft_hint(y, xp[0] != 0, T("Skip intro, recap and resume work only in our player."), 1); }
      grp(y, 2); /* their own pages, inside Playback */
      nav_row(y, "st_subs", TINT_BLUE, T("Subtitles"), NULL, P_SUBS); y += RH;
      aud_row(y); y = grp_end(); /* (the audio language itself, no page of its own) */
      { static const Opt TC[] = { { "auto", "Automatic" }, { "tcp", "TCP only" } }; grp(y, 1); ch_row(y, T("Torrent connections"), NULL, "p2pTcpOnly", TC, N(TC), "auto"); y = grp_end(); } /* (TCP only: torrents on some mobile 4G / 5G routers) */
      break; }
    case P_SUBS: title = T("Subtitles"); y = sub_preview(y); grp(y, 11);
      ch_row(y, T("Default subtitles"), NULL, "subsLang", LANGS, N(LANGS), ""); y += RH;
      { static const Opt SM[] = { { "all", "All subtitles" }, { "forced", "Forced only" } }; ch_row(y, T("Subtitle preference"), NULL, "subsMode", SM, N(SM), "all"); y += RH; } /* (PR #50, from LG: a forced track only, the lines in another language and the signs) */
      ch_row(y, T("Size"), NULL, "subtitleSize", SIZES, N(SIZES), "2"); y += RH;
      { static Opt FL[SUBF_N], FA[SUBF_N]; static const char *const NV[SUBF_N] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }; /* the subtitle fonts (Latin, Arabic): ten each and the default */
        for (int i = 0; i < SUBF_N; i++) { FL[i].v = FA[i].v = NV[i]; FL[i].lb = i ? SUBF_LAT[i] : T("Default"); FA[i].lb = i ? SUBF_AR[i] : T("Default"); }
        ch_row(y, T("Font"), NULL, "subsFont", FL, SUBF_N, "0"); y += RH; ch_row(y, T("Arabic font"), NULL, "subsFontAr", FA, SUBF_N, "0"); y += RH; }
      ch_row(y, T("Position"), NULL, "subsPosPct", SPOS, N(SPOS), "8"); y += RH;
      ch_row(y, T("Colour"), NULL, "subsColor", COLORS, N(COLORS), "#ffffff"); y += RH;
      ch_row(y, T("Outline"), NULL, "subsOutlineColor", OUTL, N(OUTL), "#000000"); y += RH;
      ch_row(y, T("Background"), NULL, "subsBgStyle", BGS, N(BGS), "0"); y += RH;
      { static const Opt BR[] = { { "100", "100%" }, { "80", "80%" }, { "60", "60%" }, { "40", "40%" } }; ch_row(y, T("Brightness"), NULL, "subsBright", BR, N(BR), "100"); y += RH; } /* (softer on an OLED in the dark) */
      ch_row(y, T("Styled subtitles"), NULL, "subsAssStyle", ASSO, N(ASSO), "styled"); y = grp_end();
      break;
    case P_DL: title = T("Downloads"); y = dl_page(y); break;
    case P_TRAKT: title = "Trakt"; y = trakt_page(y); break;
    case P_AUDIO: title = T("Audio"); break; /* (gone: its one row is in Playback) */
    case P_LOOK: title = T("Appearance"); y = look_preview(y); grp(y, 2);
      ch_row(y, T("Accent colour"), NULL, "accent", ACCENTS, N(ACCENTS), "logo"); y += RH;
      ch_row(y, T("Spoilers"), NULL, "spoilers", SPOILO, N(SPOILO), "off"); y = grp_end();
      { /* the colours: the two that follow the title (its logo, its artwork), then the twenty-eight (graphite last), in a centred grid of fives; held, a bubble of glass lifts over the colour under the finger and follows it across, picking as it goes */
        int cols = 5; float d = (SB_W - 2 * PAD - 52 - (cols - 1) * 16) / cols; if (d > 70) d = 70; int total = 30, rowsN = (total + cols - 1) / cols; float h = 26 * 2 + rowsN * d + (rowsN - 1) * 16 + 30; grp_h(y, h);
        const char *cur = store_get("accent", "logo");
        for (int k = 0; k < total; k++) { float xx = gx + (gw - (cols * d + (cols - 1) * 16)) / 2 + (k % cols) * (d + 16), yy = y + 26 + (k / cols) * (d + 16); const char *key = k == 0 ? "logo" : k == 1 ? "adaptive" : ACCENT_KEYS[k - 2]; int on = !strcmp(cur, key);
          float pk = press_k(xx, yy, d, d), sc = 1 - 0.08f * pk; gfx_scale_push(sc, sc, xx + d / 2, yy + d / 2);
          if (k < 2) { /* a title's own colours: a sweep of them, with its mark */ for (int q = 0; q < 6; q++) { const uint32_t *c = settings_accent_rgb(q * 3); float a0 = q * 60 * (float)M_PI / 180; gfx_circle(xx + d / 2 + d * 0.18f * sinf(a0), yy + d / 2 - d * 0.18f * cosf(a0), d * 0.3f, 0xff000000u | c[0]); }
            gfx_circle(xx + d / 2, yy + d / 2, d * 0.26f, RGBA(16, 14, 22, 220)); if (k == 0) text_draw("Aa", xx + d / 2, yy + d / 2 - text_line_height(F_TINY) / 2.0f, F_TINY, 900, WHITE_A(240), 0, 0, ALIGN_CENTER); else icon_draw("st_look", xx + d / 2 - 12, yy + d / 2 - 12, 24, WHITE_A(240)); }
          else if (k == 29) { gfx_rrect_v(xx, yy, d, d, d / 2, RGB(0xd8, 0xdd, 0xe6), RGB(0x6a, 0x72, 0x85)); gfx_rrect_v(xx + d * 0.1f, yy + d * 0.05f, d * 0.8f, d * 0.45f, d * 0.22f, WHITE_A(90), WHITE_A(0)); gfx_ring(xx, yy, d, d, d / 2, 1, WHITE_A(110)); } /* graphite: a little glass */
          else { const uint32_t *c = settings_accent_rgb(k - 2); gfx_circle(xx + d / 2, yy + d / 2, d / 2, 0xff000000u | c[0]); gfx_rrect_v(xx, yy, d, d, d / 2, WHITE_A(50), WHITE_A(0)); }
          if (on) { gfx_ring(xx - 6, yy - 6, d + 12, d + 12, (d + 12) / 2, 3, WHITE_A(235)); if (k >= 2) icon_draw("ph_check", xx + d / 2 - 15, yy + d / 2 - 15, 30, WHITE_A(255)); }
          gfx_scale_pop(); hit_act(xx, yy, d, d, swatch_act, k, 0); }
        { int ph_finger(float *x, float *y, float *x0, float *y0, int *held); void ph_liquid_bubble(float x, float y, float w, float h, float r, float press); float fx, fy, fx0, fy0; int held;
          float g0x = gx + (gw - (cols * d + (cols - 1) * 16)) / 2, g0y = y + 26, gW = cols * d + (cols - 1) * 16, gH2 = rowsN * d + (rowsN - 1) * 16; static Tween bub; static int lastK = -1; static float bx, by;
          static Scroll gS; gS.free = 1; gS.max = 0; gS.pos = 0; ph_drag(g0x - 8, g0y - 8, gW + 16, gH2 + 16, &gS, 0); /* (a sideways slide on the colours is theirs: the bubble at once, no hold first; up and down still scrolls the page) */
          int down = ph_finger(&fx, &fy, &fx0, &fy0, &held), inG = fx0 >= g0x - 8 && fx0 <= g0x + gW + 8 && fy0 >= g0y - 8 && fy0 <= g0y + gH2 + 8, on2 = down && inG && (held || gS.drag);
          tween_to(&bub, on2 ? 1 : 0, on2 ? 0.28f : 0.2f, on2 ? EASE_OUT_BACK : EASE_CSS_EASE); tween_tick(&bub, phDt);
          int k = -1; if (on2) { int cx2 = (int)((fx - g0x + 8) / (d + 16)), cy2 = (int)((fy - g0y + 8) / (d + 16)); if (cx2 < 0) cx2 = 0; if (cx2 >= cols) cx2 = cols - 1; if (cy2 < 0) cy2 = 0; if (cy2 >= rowsN) cy2 = rowsN - 1; k = cy2 * cols + cx2; if (k >= total) k = total - 1;
            if (k != lastK) { lastK = k; swatch_act(k, 0); pplat_haptic_k(HK_TICK, 0.5f); } } /* across the colours: each one picked as the finger reaches it, a tick each */
          else lastK = -1;
          if (bub.v > 0.01f) { int kk = k >= 0 ? k : lastK; if (kk < 0) { const char *c0 = store_get("accent", "logo"); kk = !strcmp(c0, "logo") ? 0 : !strcmp(c0, "adaptive") ? 1 : 2; for (int q = 0; q < 28; q++) if (!strcmp(c0, ACCENT_KEYS[q])) kk = q + 2; }
            float tx = g0x + (kk % cols) * (d + 16) + d / 2, ty = g0y + (kk / cols) * (d + 16) + d / 2 - 92; if (bub.v < 0.05f || (bx == 0 && by == 0)) { bx = tx; by = ty; } bx += (tx - bx) * clampf(phDt * 18, 0, 1); by += (ty - by) * clampf(phDt * 18, 0, 1);
            float R = (d * 0.74f) * bub.v, cx3 = bx, cy3 = by + 30 * (1 - bub.v); /* a drop of glass lifted above the finger, holding the colour */
            { void ph_glass_grab_liquid(void); ph_glass_grab_liquid(); } /* the page as it is now (the colours drawn): the bubble's glass bends them, as the bar's pills bend what is under them */
            ph_liquid_bubble(cx3 - R, cy3 - R, 2 * R, 2 * R, R, 0.2f); gfx_opacity_push(bub.v);
            if (kk >= 2 && kk < 29) { const uint32_t *c = settings_accent_rgb(kk - 2); gfx_circle(cx3, cy3, R * 0.74f, 0xff000000u | c[0]); gfx_rrect_v(cx3 - R * 0.74f, cy3 - R * 0.74f, R * 1.48f, R * 1.48f, R * 0.74f, WHITE_A(60), WHITE_A(0)); gfx_ring(cx3 - R * 0.74f, cy3 - R * 0.74f, R * 1.48f, R * 1.48f, R * 0.74f, 1, WHITE_A(90)); }
            else if (kk == 29) { gfx_rrect_v(cx3 - R * 0.74f, cy3 - R * 0.74f, R * 1.48f, R * 1.48f, R * 0.74f, RGB(0xd8, 0xdd, 0xe6), RGB(0x6a, 0x72, 0x85)); }
            else { for (int q = 0; q < 6; q++) { const uint32_t *c = settings_accent_rgb(q * 4); float a0 = q * 60 * (float)M_PI / 180; gfx_circle(cx3 + R * 0.3f * sinf(a0), cy3 - R * 0.3f * cosf(a0), R * 0.44f, 0xff000000u | c[0]); } }
            gfx_opacity_pop(); } }
        text_draw(!strcmp(cur, "logo") ? T("From the logo") : !strcmp(cur, "adaptive") ? T("From the artwork") : T("One colour everywhere"), SB_W / 2, y + h - 40, F_TINY, 700, TXT3, 0, 0, ALIGN_CENTER);
        y += h + 30; }
      break;
    case P_LANG: title = T("Language"); { int keyRow = !tmdb_builtin();
      { const char *cur = store_get("uiLang", "auto"); int n = i18n_lang_count(); if (n > 39) n = 39; /* every language at once, the one in use ticked: a tap picks it */
        grp(y, n + 1); for (int i = 0; i <= n; i++) { const char *code = i ? i18n_lang_code(i - 1) : "auto"; int on = !strcmp(cur, code); rres = 90; row_base(y, NULL, 0, i ? i18n_lang_name(code) : T("Auto"), NULL, RH);
          if (on) icon_draw("ph_check", gx + gw - 26 - 40, y + RH / 2 - 20, 40, ACC2); hit_act(gx, y, gw, RH, lang_row, i, 0); y += RH; } y = grp_end(); }
      if (keyRow) { grp(y, 1 + (tmdb_has_key() ? 1 : 0)); }
      if (keyRow) { int on = tmdb_has_key(); act_row(y, NULL, 0, T("Titles and descriptions"), on ? T("In the app\xe2\x80\x99s language, from TMDB") : T("Only in English. Add a free TMDB key in Settings on the TV"), NULL, 0); y += RH;
        if (on) { act_row(y, NULL, 0, T("Remove the TMDB key"), T("Titles and descriptions go back to English"), tmdb_act, 0); y += RH; } }
      if (keyRow) y = gTop + gH + 30; break; }
    case P_ADDONS: { title = T("Addons"); cJSON *list = sb_addons(); int n = cJSON_GetArraySize(list), i = 0; const cJSON *ad;
      grp(y, 2); act_row(y, "st_add", TINT_GREEN, T("Add an addon"), T("From its link"), add_act, 0); y += RH; act_row(y, "st_addon", TINT_PINK, S.syncing ? T("Syncing…") : T("Sync addons now"), T("Fetch your addon list from your account"), sync_act, 0); y = grp_end();
      if (n) { grp(y, n); cJSON_ArrayForEach(ad, list) { const cJSON *mf = js_obj(ad, "manifest"); const char *nm = js_str(mf, "name", js_str(ad, "transportUrl", "?")); char ds[300]; snprintf(ds, sizeof ds, "%s", js_str(mf, "description", ""));
          if (js_bool(js_obj(mf, "behaviorHints"), "adult", 0)) { size_t L = strlen(ds); snprintf(ds + L, sizeof ds - L, "%s%s", L ? "   " : "", T("Adult content")); } /* the manifest's own warning */
          rres = 150; row_base(y, NULL, 0, nm, ds, RH); if (!sb_addon_on(ad)) text_draw(T("Off"), gx + gw - 26 - 32 - 14, y + RH / 2 - text_line_height(F_SMALL) / 2.0f, F_SMALL, 700, TXT3, 0, 0, ALIGN_END); icon_draw("ph_chev", gx + gw - 26 - 32, y + RH / 2 - 16, 32, WHITE_A(100)); hit_act(gx, y, gw, RH, addon_open, i, 0); y += RH; i++; } /* a tap: its own sheet */ y = grp_end(); }
      else text_draw_wrapped(T("No addons. Sign in, or add them from the Stremio site on this phone."), PAD, y + 20, SB_W - 2 * PAD, 3, F_BODY, 500, TXT2, 0, ALIGN_START, 34);
      break; }
    case P_ABOUT: { title = T("About"); const char *av = update_avail(); int st = update_state();
      /* the app: its mark and version */
      { float cx = SB_W / 2; gfx_rrect_v(cx - 56, y, 112, 112, 56, RGB(0x6a, 0x45, 0xe0), RGB(0x2f, 0x1a, 0x9e)); icon_draw("o_play", cx - 40, y + 14, 84, WHITE_A(255));
        text_draw("Stremio Origin", cx, y + 132, F_H2, 800, TXT, -0.3f, 0, ALIGN_CENTER); text_draw(T1("Version {v}", "v", update_current()), cx, y + 180, F_SMALL, 600, TXT3, 0, 0, ALIGN_CENTER); y += 240; }
      int nr = 2 + (av ? 1 : 0) + (update_note_count() || 1 ? 1 : 0); grp(y, nr);
      if (av) { act_row(y, "st_update", TINT_GREEN, st == UPD_INSTALLING ? T1("Installing version {v}…", "v", av) : T1("Install version {v}", "v", av), update_msg()[0] ? update_msg() : T("Downloads, checks and installs it; the app opens again by itself"), install_act, 0); y += RH; }
      act_row(y, "st_update", TINT_BLUE, T("Check for updates"), st == UPD_CHECKING ? T("Checking for updates…") : st == UPD_ERROR ? T("Could not check for updates") : av ? T1("Version {v} is available", "v", av) : st == UPD_NONE ? T("You have the latest version") : "", check_act, 0); y += RH;
      act_row(y, "st_new", TINT_PURPLE, T("What\xe2\x80\x99s new"), T1("Version {v}", "v", update_current()), news_act, 0); y += RH;
      act_row(y, "st_logs", TINT_GREY, T("Share logs"), S.code[0] ? T1("Code {c}: give it with what happened", "c", S.code) : T("Share the code and describe the issue"), logs_act, 0); y = grp_end();
      { /* the fine print, in short points: what it is, what it is not, TMDB's credit (their logo and their words) */
        static const char *PTS[] = { "Unofficial client for Stremio addons. Stremio is a trademark of its owner; this app is not affiliated with it.",
          "This product uses the TMDB API but is not endorsed or certified by TMDB",
          "It hosts, stores and shares no films or series: it plays what your addons return." };
        float tx = PAD + 34, tw = SB_W - 2 * PAD - 44;
        for (int i = 0; i < 3; i++) { gfx_circle(PAD + 16, y + 15, 3.5f, TXT3); int nl = text_draw_wrapped(T(PTS[i]), tx, y, tw, 4, F_SMALL - 1, 500, TXT3, 0, ALIGN_START, 28); y += nl * 28 + 14; }
        y += 90; }
      break; }
  }  *titleOut = title; return y; }
static Scroll sb; static int sbWas, sbGo, sbLanded; static float sbX; /* sb: a swipe back on a sub-page (its pos goes negative as the finger moves right); Back runs the same slide */
static void s_draw(void) {
  nsw = nch = 0; Scroll *v = &S.v[S.page]; ph_drag(0, 0, SB_W, SB_H - INS_B - BAR_H, v, 1); scroll_tick(v);
  /* a sub-page comes in from the right, the list goes back to the left */
  if (S.page != P_MAIN && !sbGo) { ph_drag(0, INS_T, SB_W, SB_H - INS_T - INS_B - BAR_H, &sb, 0); sb.free = 1; sb.max = 0; } /* (opened from elsewhere: the same slide, that page under it) */
  if (sb.drag) { sbWas = 1; sbX = -sb.pos < 0 ? 0 : -sb.pos; }
  else { if (sbWas) { sbWas = 0; sbGo = sbX > SB_W * 0.28f || sb.vel < -900; } sb.vel = 0; float to = sbGo ? SB_W : 0; sbX += (to - sbX) * clampf(phDt * 14, 0, 1); if (fabsf(to - sbX) < 1) sbX = to;
    if (sbGo && sbX >= SB_W - 1) { int toPage = page_parent(S.page); sbGo = 0; sbX = 0; sb.pos = 0; if (direct) { extern int phoneSwipeBack; void app_ambient_cut(void); extern Screen phDl; phoneSwipeBack = 1; S.page = P_MAIN; app_back(); app_ambient_cut(); gfx_rect(0, 0, SB_W, SB_H, PAGE | 0xff000000u); phDl.draw(); return; } /* (the glows went with the page: not faded out again over Downloads; Downloads drawn in this frame too, never an empty one) */ /* (its Back sees it was opened from there and lets the app go back) */ /* back to where it was opened from, already in view */ S.page = toPage; sbLanded = 1; } else if (!sbGo) sb.pos = -sbX; }
  v = &S.v[S.page]; /* (landed this frame: the page now shown is drawn at its own scroll, never the one it left (a frame of it drawn at the other's place)) */
  int under = sbX > 1 && S.page != P_MAIN; /* the list under the page being swiped away */ sbKNow = direct && S.page != P_MAIN ? sbX / SB_W : 0;
  static int lastPage = -1, dir = 1; static Tween pa; if (lastPage != S.page) { if (sbLanded) { sbLanded = 0; lastPage = S.page; tween_set(&pa, 1); } else { dir = S.page > lastPage ? 1 : -1; if (lastPage >= 0) { tween_set(&pa, 0); tween_to(&pa, 1, 0.32f, EASE_WEB); } else tween_set(&pa, 1); lastPage = S.page; } } tween_tick(&pa, phDt);
  if (under) { int was = S.page, hn = hit_count(); S.page = P_MAIN; extern int phGhost, phGhostGlass; if (!direct) { ph_glass_grab(); phGhostGlass = 1; } /* (the list's own glass: the page and its glows, grabbed before the list: it looks as it will once there) */ phGhost = 1; gfx_offset_push(-(SB_W - sbX) / SB_W * 0.25f * fminf(SB_W, 1100), 0); /* (a quarter of what it shows: on a tablet its column stays in view) */
    if (direct) { extern Screen phDl; Scr sv = SCR; SCR = SCR_DISC; gfx_rect(0, 0, SB_W, SB_H, PAGE | 0xff000000u); phDl.draw(); SCR = sv; } else if (page_parent(was) != P_MAIN) { int pp = page_parent(was); S.page = pp; nsw = nch = 0; const char *tt = T("Settings"); float yy = INS_T + 150 - S.v[pp].pos; phone_col_push(1100); page_body(pp, yy, &tt); phone_col_pop(); s_head(tt, S.v[pp].pos); } else s_list(); /* under it: its parent page (Playback under Subtitles and Audio) */ /* under it: the page it was opened from (Downloads), else the list */
    gfx_offset_pop(); phGhost = 0; phGhostGlass = 0; S.page = was; hit_trim(hn);
    gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(40 * (1 - sbX / SB_W)))); /* (a light shade only: no brightness jump as it lands) */ gfx_rect(sbX, 0, SB_W, SB_H, PAGE | 0xff000000u); { void app_ambient_at(float dx); gfx_clip_push(sbX, 0, SB_W, SB_H); app_ambient_at(sbX); gfx_clip_pop(); } gfx_offset_push(sbX, 0); } /* the page being swiped away keeps its glows (they go with it) */
  gfx_offset_push(dir * 90 * (1 - pa.v), 0); gfx_opacity_push(0.2f + 0.8f * pa.v);
  ph_glass_grab(); /* (the page under the cards, for their glass) */
  float y = INS_T + 150 - v->pos; const char *title = T("Settings");
  subPrevOn = 0; phone_col_push(1100); y = page_body(S.page, y, &title);
  if (subPrevOn) { float ph = (SB_W - 2 * PAD) * 9 / 16, pin = INS_T + 96; sub_preview_draw(ph < SB_H * 0.42f && subPrevY < pin ? pin : subPrevY); } /* (Subtitles: its preview pinned under the bar while the rows under it scroll, where it is short enough to) */
  phone_col_pop(); /* (sideways, a tablet: a readable width, in the middle) */

  gfx_opacity_pop(); gfx_offset_pop();
  v->max = y + v->pos - (SB_H - INS_B - BAR_H) + 30; rowY = y;
  s_head(title, v->pos);
  if (under) gfx_offset_pop();
}
static int s_key(int k) { if (k == K_BACK) { if (ph_sheet_on()) return 0; if (direct && S.page == P_MAIN) { direct = 0; return 0; } /* back where it was opened from */ if (S.page != P_MAIN) { if (!sb.drag) sbGo = 1; return 1; } return 0; } /* (Back: the page slides away as the swipe takes it, onto the one under it) */ if (k == K_CH_UP) { S.v[S.page].pos = 0; return 1; } return 0; }
static void phSet_out(float k) { phone_out(s_draw, k); }
static void s_leave(void) { direct = 0; }
int ph_settings_direct(void) { return direct; }
Screen phSet = { s_enter, s_leave, s_key, NULL, s_draw, NULL, NULL, NULL, NULL, phSet_out };
void ph_settings_open_about(void) { app_go(SCR_SET, NULL, 0); S.page = P_ABOUT; }
void ph_settings_dev(const char *a) { ph_settings_open_about(); if (!strcmp(a, ":logs")) logs_act(0, 0); else if (!strcmp(a, ":check")) check_act(0, 0); else if (!strcmp(a, ":news")) news_act(0, 0); } /* (development: SB_OPEN="about", "about:logs", "about:check") */
void settings_open_about(void) { ph_settings_open_about(); }
void settings_open_usb(void) { app_go(SCR_SET, NULL, 0); }
