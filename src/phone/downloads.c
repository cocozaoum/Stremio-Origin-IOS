#include <stdint.h>
/* Downloads: a film or an episode kept on the phone to play without a connection. Chosen as Play chooses (the best
   fit at once when that is on, else from its streams), fetched one at a time on a worker into the app's own folder,
   picking up where it stopped after a cut or a restart (a ranged request). The list is kept in the store; a finished
   one plays from its file. Torrents are not downloaded here (they need the engine running). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include "phone/phone.h"
#include "core/store.h"
#include "core/net.h"
#include "core/json.h"
#include "core/i18n.h"
#include "core/best.h"
#include "phone/sbdl.h"
#include <dlfcn.h>

/* the profile: small about 1.5 GB an hour, balanced about 4, best as the line allows; custom: the user's own */
void ph_dl_prefs(double *mbps, int *res, int *hdr) { const char *q = store_get("dlQuality", "balanced"); *res = 0; *hdr = -1;
  if (!strcmp(q, "small")) { *mbps = 3.5; *res = 1080; } else if (!strcmp(q, "best")) *mbps = 0; else if (strcmp(q, "custom")) *mbps = 9;
  else { double gbh = atof(store_get("dlRate", "4")); *mbps = gbh > 0 ? gbh * 8000 / 3600 : 0; *res = atoi(store_get("dlMaxRes", "0")); *hdr = store_bool("dlHdr", 1) ? -1 : 0; } }
static void done_later(Dl *d); static void pv_tick(void); static void dl_dir(char *dir, size_t c); static int net_ok(void); int ph_dl_in_order(void); static void sv_update(void); static void notify_now(void); static void notify_tick(void); static void notify_done(const Dl *d); static int local_stream(const char *id, const char *vid, Stream *out);
/* on mobile data, a new download asks first: now, or when back on Wi-Fi (those wait; the setting stays as it is) */
int pplat_on_wifi(void); static int asking; static double askAt; static void choices_cb(int i);
static int wifi_now(void) { static double at = -9; static int w = 1; if (app_now() - at > 2) { at = app_now(); w = pplat_on_wifi(); } return w; } /* (asked every couple of seconds)
 */
static void ask_data(void); static int find(const char *id, const char *vid); static int is_p2p(const Dl *d); int p2p_unsteady(void);
#define MAXD 64
#define NEWWAIT(d) do { if (!store_bool("dlWifi", 0) && !wifi_now()) (d)->wifi = 2; } while (0) /* (held until answered) */
static Dl D[MAXD]; static int nd, loaded;
static void q_push(int i); static void q_next(void);
/* lanes: as many files at once as the settings say (torrents and links together); each lane its worker, and for a
   torrent its engine address. W, TD, tdUrl... below are the lane being looked at (cl) */
#define NL 6
typedef struct { volatile int busy, stop, done, status; volatile long long got, total; int idx; char url[SB_URL_MAX], file[600]; long long from, to; FILE *wf; int hop206; } Wk;
static Wk WK[NL]; static int cl;
#define W (WK[cl])
static double savedAt;
/* the engine (Gopeed, libsbdl): several connections per file, real pause and resume, its own store; the plain worker
   below when it is not there */
static const sbdl_api *E_; static int eTried; static double eAt; /* when it started: it reloads its tasks for a moment */
static const sbdl_api *eng(void) { if (eTried) return E_; eTried = 1;
  const char *names[] = { "libsbdl.so", "build/phone-mac/libsbdl.dylib", "libsbdl.dylib" }; void *h = NULL; for (int i = 0; i < 3 && !h; i++) h = dlopen(names[i], RTLD_NOW);
  if (!h) { fprintf(stderr, "[dl] no engine (%s): the plain downloader\n", dlerror()); return NULL; }
  sbdl_get_fn g = (sbdl_get_fn)dlsym(h, "sbdl_get"); const sbdl_api *a = g ? g() : NULL; if (!a || a->api() != SBDL_API_VERSION) { fprintf(stderr, "[dl] engine of another version\n"); return NULL; }
  char dir[700]; snprintf(dir, sizeof dir, "%s/dlengine", store_data_dir()); mkdir(dir, 0755); int cn = ph_dl_in_order() ? 1 : 8; /* connections a file: one in order (playable while it comes), else eight */ if (a->start(dir, cn)) { fprintf(stderr, "[dl] engine did not start\n"); return NULL; }
  fprintf(stderr, "[dl] engine %s, %d connections a file\n", a->version(), cn); E_ = a; eAt = app_now(); return E_; }

static long long rg_cov(const Dl *d);
static void rg_add(Dl *d, long long a, long long b);
static void save(void) {
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < nd; i++) { Dl kept = D[i]; Dl *d = &kept; cJSON *o = cJSON_CreateObject(); /* (a copy: the parts in progress saved with it, the download itself untouched) */
    for (int k = 0; k < NL; k++) if (WK[k].busy && WK[k].idx == i && WK[k].to >= 0 && WK[k].got > 0) { long long a = WK[k].from, b = WK[k].from + WK[k].got; if (b > WK[k].to + 1) b = WK[k].to + 1; rg_add(d, a, b); d->got = rg_cov(d); } /* the part a lane is fetching: what is already on disk (written unbuffered) counts after a restart too */
    cJSON_AddStringToObject(o, "id", d->id); cJSON_AddStringToObject(o, "type", d->type); cJSON_AddStringToObject(o, "vid", d->vid); cJSON_AddStringToObject(o, "name", d->name);
    cJSON_AddStringToObject(o, "ep", d->ep); cJSON_AddStringToObject(o, "group", d->group); cJSON_AddStringToObject(o, "gid", d->gid); cJSON_AddStringToObject(o, "poster", d->poster); cJSON_AddStringToObject(o, "bg", d->bg); cJSON_AddStringToObject(o, "url", d->url); cJSON_AddStringToObject(o, "file", d->file);
    cJSON_AddStringToObject(o, "info", d->info); cJSON_AddNumberToObject(o, "seq", d->seq); cJSON_AddNumberToObject(o, "wifi", d->wifi == 2 ? 1 : d->wifi); cJSON_AddNumberToObject(o, "size", (double)d->size); cJSON_AddNumberToObject(o, "got", (double)d->got); if (d->nrg) { cJSON *r = cJSON_AddArrayToObject(o, "rg"); for (int k = 0; k < d->nrg; k++) { cJSON *p2 = cJSON_CreateArray(); cJSON_AddItemToArray(p2, cJSON_CreateNumber((double)d->rg[k][0])); cJSON_AddItemToArray(p2, cJSON_CreateNumber((double)d->rg[k][1])); cJSON_AddItemToArray(r, p2); } } cJSON_AddNumberToObject(o, "state", d->state == DL_RUNNING ? DL_QUEUED : d->state);
    cJSON_AddItemToArray(a, o); }
  store_set_node("downloads", a); store_flush(); savedAt = app_now();
}
static void load(void) {
  if (loaded) return; loaded = 1; sb_local_stream = local_stream; const cJSON *a = store_node("downloads"), *o; nd = 0;
  cJSON_ArrayForEach(o, a) { if (nd >= MAXD) break; Dl *d = &D[nd++]; memset(d, 0, sizeof *d);
    snprintf(d->id, sizeof d->id, "%s", js_str(o, "id", "")); snprintf(d->type, sizeof d->type, "%s", js_str(o, "type", "movie")); snprintf(d->vid, sizeof d->vid, "%s", js_str(o, "vid", ""));
    snprintf(d->name, sizeof d->name, "%s", js_str(o, "name", "")); snprintf(d->ep, sizeof d->ep, "%s", js_str(o, "ep", "")); snprintf(d->group, sizeof d->group, "%s", js_str(o, "group", "")); snprintf(d->gid, sizeof d->gid, "%s", js_str(o, "gid", "")); snprintf(d->poster, sizeof d->poster, "%s", js_str(o, "poster", ""));
    snprintf(d->bg, sizeof d->bg, "%s", js_str(o, "bg", "")); snprintf(d->url, sizeof d->url, "%s", js_str(o, "url", "")); snprintf(d->file, sizeof d->file, "%s", js_str(o, "file", "")); snprintf(d->info, sizeof d->info, "%s", js_str(o, "info", "")); d->wifi = (int)js_num(o, "wifi", 0); d->seq = (int)js_num(o, "seq", 0);
    d->size = (long long)js_num(o, "size", 0); d->got = (long long)js_num(o, "got", 0); d->state = (int)js_num(o, "state", DL_QUEUED);
    { const cJSON *r = js_arr(o, "rg"), *p2; if (r) cJSON_ArrayForEach(p2, r) { if (d->nrg >= 24) break; d->rg[d->nrg][0] = (long long)cJSON_GetArrayItem(p2, 0)->valuedouble; d->rg[d->nrg][1] = (long long)cJSON_GetArrayItem(p2, 1)->valuedouble; d->nrg++; } if (d->nrg) d->got = rg_cov(d); } }
  for (int i = 0; i < nd; i++) if (D[i].state == DL_CHOOSING) q_push(i); /* (stopped while finding its stream: found again) */
  if (p2p_unsteady()) { int n = 0; for (int i = 0; i < nd; i++) if (is_p2p(&D[i]) && (D[i].state == DL_QUEUED || D[i].state == DL_RUNNING)) { D[i].state = DL_PAUSED; n++; } if (n) fprintf(stderr, "[dl] the engine went down with the app twice: %d torrent(s) wait for a hand\n", n); } /* (never a crash at every start) */
}
int ph_dl_count(void) { load(); return nd; }
const Dl *ph_dl_get(int i) { load(); return i >= 0 && i < nd ? &D[i] : NULL; }
int ph_dl_active(void) { load(); int n = 0; for (int i = 0; i < nd; i++) if (D[i].state == DL_QUEUED || D[i].state == DL_RUNNING) n++; return n; }

/* ---- the parts of a file it has: ranges [start, end), kept in order and joined ---- */
static void rg_add(Dl *d, long long a, long long b) { if (b <= a) return; long long r[26][2]; int n = 0, put = 0;
  for (int i = 0; i < d->nrg; i++) { long long s0 = d->rg[i][0], e0 = d->rg[i][1]; if (e0 < a) { r[n][0] = s0; r[n][1] = e0; n++; } else if (s0 > b) { if (!put) { r[n][0] = a; r[n][1] = b; n++; put = 1; } r[n][0] = s0; r[n][1] = e0; n++; } else { if (s0 < a) a = s0; if (e0 > b) b = e0; } }
  if (!put) { r[n][0] = a; r[n][1] = b; n++; } if (n > 24) { r[22][1] = r[n - 2][1]; r[23][0] = r[n - 1][0]; r[23][1] = r[n - 1][1]; n = 24; } /* (too many: two nearest joined, the gap fetched again) */
  memcpy(d->rg, r, sizeof(long long) * 2 * n); d->nrg = n; }
static long long rg_cov(const Dl *d) { long long c = 0; for (int i = 0; i < d->nrg; i++) c += d->rg[i][1] - d->rg[i][0]; return c; }
static long long rg_end_at(const long long (*rg)[2], int n, long long pos) { for (int i = 0; i < n; i++) if (rg[i][0] <= pos && pos < rg[i][1]) return rg[i][1]; return -1; } /* the end of the part it has at pos, -1 when it has not got pos */
/* a part cut out of what it has (found bad: fetched again) */
static void rg_cut(long long (*rg)[2], int *n, long long a, long long b) { long long r[26][2]; int m = 0; for (int i = 0; i < *n && m < 24; i++) { long long s0 = rg[i][0], e0 = rg[i][1]; if (e0 <= a || s0 >= b) { r[m][0] = s0; r[m][1] = e0; m++; continue; } if (s0 < a && m < 24) { r[m][0] = s0; r[m][1] = a; m++; } if (e0 > b && m < 24) { r[m][0] = b; r[m][1] = e0; m++; } }
  memcpy(rg, r, sizeof(long long) * 2 * m); *n = m; }
static int rg_gap(const Dl *d, long long *gs, long long *ge) { long long p = 0; for (int i = 0; i <= d->nrg; i++) { long long s0 = i < d->nrg ? d->rg[i][0] : d->size; if (s0 > p) { *gs = p; *ge = s0; return 1; } if (i < d->nrg && d->rg[i][1] > p) p = d->rg[i][1]; } return 0; } /* the first part it has not got */
/* ---- the worker: one file at a time, from where it stopped ---- */
static void on_hdr(const char *line, void *ud) { Wk *w = ud;
  if (!strncmp(line, "HTTP/", 5)) { const char *sp = strchr(line, ' '); w->hop206 = sp && atoi(sp + 1) == 206; return; }
  if (!strncasecmp(line, "Content-Range:", 14)) { const char *sl = strrchr(line, '/'); if (sl && sl[1] != '*') w->total = atoll(sl + 1); }
  else if (!strncasecmp(line, "Content-Length:", 15) && !w->hop206) w->total = atoll(line + 15); }
static int on_body(const unsigned char *p, size_t n, void *ud) { Wk *w = ud; if (w->stop) return 0; if (!p || !n) return 1;
  if (w->to >= 0) { if (!w->hop206) { w->status = -3; return 0; } if (fwrite(p, 1, n, w->wf) != n) { w->status = -2; return 0; } w->got += (long long)n; return 1; } /* (a part: written where it goes; a server that sends it all is no good here) */
  if (!w->hop206 && w->got > 0) { fflush(w->wf); if (ftruncate(fileno(w->wf), 0) == 0) { fseek(w->wf, 0, SEEK_SET); w->got = 0; } w->hop206 = 1; } /* the server sent it whole: start the file over */
  if (fwrite(p, 1, n, w->wf) != n) { w->status = -2; return 0; } w->got += (long long)n; return 1; }
static void *worker(void *arg) { Wk *w = arg;
  if (w->to >= 0) { w->hop206 = 0; w->got = 0; w->wf = fopen(w->file, "r+b"); if (!w->wf) w->wf = fopen(w->file, "w+b"); if (w->wf) setvbuf(w->wf, NULL, _IONBF, 0); /* (unbuffered: what it counts is on disk, the player may read it at once) */ if (!w->wf || fseeko(w->wf, w->from, SEEK_SET)) { if (w->wf) fclose(w->wf); w->wf = NULL; w->status = -2; w->done = 1; return NULL; } /* a part of it: from its first byte to its last */
    char hr[96]; snprintf(hr, sizeof hr, "Range: bytes=%lld-%lld", w->from, w->to); int r = net_pipe_sync(w->url, hr, 0, on_hdr, on_body, w, 30); fclose(w->wf); w->wf = NULL; if (w->status >= 0) w->status = r; w->done = 1; return NULL; }
  struct stat st; long long have = stat(w->file, &st) == 0 ? (long long)st.st_size : 0; w->got = have; w->hop206 = 0;
  w->wf = fopen(w->file, have > 0 ? "ab" : "wb"); if (!w->wf) { w->status = -2; w->done = 1; return NULL; } setvbuf(w->wf, NULL, _IONBF, 0);
  char hdr[128] = ""; snprintf(hdr, sizeof hdr, "Range: bytes=%lld-", have); /* always a range, from 0 too: the torrent engine waits for pieces it has not got only when asked for a range (without one it hands back whatever its file holds) */
  int r = net_pipe_sync(w->url, hdr, 0, on_hdr, on_body, w, 30); fclose(w->wf); w->wf = NULL;
  if (w->status != -2) w->status = r; w->done = 1; return NULL;
}
static void start_from(int i, const char *url);
static void start(int i) { start_from(i, D[i].url); }
static int is_p2p(const Dl *d);
/* a file's first bytes as a video's: Matroska, MP4 (ftyp), AVI (RIFF), MPEG-TS (0x47) */
static int looks_video(const char *path) { FILE *f = fopen(path, "rb"); if (!f) return 1; unsigned char b[12] = { 0 }; size_t n = fread(b, 1, sizeof b, f); fclose(f); if (n < 12) return 1;
  return (b[0] == 0x1a && b[1] == 0x45 && b[2] == 0xdf && b[3] == 0xa3) || !memcmp(b + 4, "ftyp", 4) || !memcmp(b, "RIFF", 4) || b[0] == 0x47; }
static void dl_file_of(Dl *d) { char dir[600]; dl_dir(dir, sizeof dir); if (d->file[0]) return; unsigned h = 2166136261u; for (const char *c = d->id; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u; for (const char *c = d->vid; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
  const char *ext = strstr(d->url, ".mp4") ? "mp4" : strstr(d->url, ".avi") ? "avi" : "mkv"; snprintf(d->file, sizeof d->file, "%s/%08x.%s", dir, h, ext); }
static void start_from(int i, const char *url) {
  Dl *d = &D[i]; char dir[600]; dl_dir(dir, sizeof dir);
  if (d->file[0] && is_p2p(d) && (d->nrg ? d->rg[0][0] == 0 : d->got > 0) && !looks_video(d->file)) { fprintf(stderr, "[dl] %s %s: its file does not begin as a video (an older fetch): downloaded again from the start\n", d->id, d->ep); FILE *z = fopen(d->file, "wb"); if (z) fclose(z); d->got = 0; d->nrg = 0; } /* (its start here and not a video: junk; a start not here yet is no sign) */
  if (!d->file[0]) { unsigned h = 2166136261u; for (const char *c = d->id; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u; for (const char *c = d->vid; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    const char *ext = strstr(d->url, ".mp4") ? "mp4" : strstr(d->url, ".avi") ? "avi" : "mkv"; snprintf(d->file, sizeof d->file, "%s/%08x.%s", dir, h, ext); }
  W.from = 0; W.to = -1;
  if (is_p2p(d)) { if (!d->nrg && d->got > 0) rg_add(d, 0, d->got); long long gs, ge; if (d->size > 0 && !rg_gap(d, &gs, &ge)) { d->state = DL_DONE; d->got = d->size; save(); return; } /* (all of it here) */
    if (d->size > 0) { W.from = gs; W.to = ge - 1; } }
  W.idx = i; snprintf(W.url, sizeof W.url, "%s", url); d->seq = 1; /* (in order: it plays from the phone as it comes) */ snprintf(W.file, sizeof W.file, "%s", d->file); W.stop = 0; W.done = 0; W.status = 0; W.total = d->size; W.got = d->got; W.busy = 1;
  if (W.to >= 0) W.got = 0;
  d->state = DL_RUNNING; pthread_t t; if (pthread_create(&t, NULL, worker, &W)) { W.busy = 0; d->state = DL_FAILED; return; } pthread_detach(t);
  fprintf(stderr, "[dl] start %s %s%s\n", d->id, d->ep, W.to >= 0 ? " (the part it has not got)" : "");
}
/* ---- torrents: through the app's own engine, as many at once as "At once" says (links counted with them) and while
   no film plays. The engine fetches each one's file whole (files of one pack too) and serves it on the phone's own
   address as its pieces come; the worker reads it in order into the file, and picks up where it stopped (a film
   played in between, the app closed) ---- */
#include "core/p2p.h"
static int is_p2p(const Dl *d) { return !strncmp(d->url, "p2p://", 6); }
typedef struct { int on, idx, tok, tries, byPlayer, yield, ahead, res, fx; double at; char url[600], key[200]; } Td; static Td TDL[NL]; /* fx: its engine fetch (p2p_fetch) */
#define TD (TDL[cl])
#define tdUrl (TDL[cl].url)
#define tdKey (TDL[cl].key)
#define tdAhead (TDL[cl].ahead)
static char tdWant[200]; /* tdWant: a torrent download being played: it goes first (id|vid) */ static double tdWait;
static int px_busy(void); /* tdAhead: the film plays ahead and its torrent is the film's for now: the download still shows as downloading, its own fetch goes on after */ /* the engine's address of the last torrent fetched, and whose: the film reading ahead keeps using it */
static int px_busy_key(const char *key); /* (the film reads that one through the phone's server just now: below) */
static int lane_of(int i) { for (int k = 0; k < NL; k++) if ((TDL[k].on && TDL[k].idx == i) || (WK[k].busy && WK[k].idx == i)) return k; return -1; }
static int lanes_on(void) { int n = 0; for (int k = 0; k < NL; k++) n += TDL[k].on || WK[k].busy; return n; }
static int eng_running(void) { int n = 0; for (int i = 0; i < nd; i++) n += D[i].gid[0] && D[i].state == DL_RUNNING && !D[i].held; return n; }
static int at_once(void);
static void td_end(int retry) { extern int player_torrent(void); int i = TD.idx; TD.yield = 0; TD.res = 0; fprintf(stderr, "[dl] torrent lane %d free (%s)\n", cl, retry ? "again in 20 s" : "done"); TD.on = 0; TD.byPlayer = 0;
  if (!px_busy_key(tdKey)) { p2p_fetch_end(TD.fx); TD.fx = 0; tdUrl[0] = 0; } /* its fetch goes (the file stays; the other lanes' and the film's stay); not while the film reads from it */
  if (retry && i >= 0 && i < nd) { tdWait = app_now() + 20; } }
static void td_cb_lane(const char *url, const char *err, uint64_t size) {
  int i = TD.idx; TD.res = 0; if (i < 0 || i >= nd) { TD.on = 0; return; } Dl *d = &D[i];
  if (d->state != DL_RUNNING) { td_end(0); return; } /* paused or gone while it looked for peers */
  if (!url) { fprintf(stderr, "[dl] torrent %s %s: %s\n", d->id, d->ep, err ? err : "?"); if (++TD.tries < 3) { d->state = DL_QUEUED; td_end(1); } else { d->state = DL_FAILED; td_end(0); toast(err && *err ? err : T("Couldn\xe2\x80\x99t download this torrent"), 2600, NULL); } save(); return; }
  if (size) d->size = (long long)size; fprintf(stderr, "[dl] torrent %s %s: from %s (lane %d)\n", d->id, d->ep, url, cl); snprintf(tdUrl, sizeof tdUrl, "%s", url); snprintf(tdKey, sizeof tdKey, "%s|%s", d->id, d->vid); start_from(i, url); }
static void td_cb(void *ud, const char *url, const char *err, uint64_t size) { int tok = (int)(intptr_t)ud; for (int k = 0; k < NL; k++) if (TDL[k].on && TDL[k].tok == tok) { int sv = cl; cl = k; td_cb_lane(url, err, size); cl = sv; return; } }
static volatile double pxAt; static volatile int pxK = -1; static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; } /* pxAt: the player read ahead of the download just now (through the server's slot pxK) */
static int px_busy(void) { return mono_s() - pxAt < 12; } /* the film reads from a torrent ahead of its download: the engine is the film's while it does */
static void key_of(int i, char *k2, size_t c) { snprintf(k2, c, "%s|%s", D[i].id, D[i].vid); }
/* one lane's torrent: the film ahead of it, stepping aside, no peers, a film starting */
static void td_lane(void) { extern int player_torrent(void);
  if (!tdAhead && tdUrl[0] && !W.busy && !TD.on && px_busy_key(tdKey)) for (int j = 0; j < nd; j++) { char k2[200]; key_of(j, k2, sizeof k2); if (D[j].state == DL_QUEUED && !strcmp(k2, tdKey)) { D[j].state = DL_RUNNING; tdAhead = 1; } } /* (resumed while the film plays ahead: downloading, as it is) */
  if (tdAhead) { int i = -1; for (int j = 0; j < nd; j++) { char k2[200]; key_of(j, k2, sizeof k2); if (!strcmp(k2, tdKey)) i = j; }
    if (i < 0 || D[i].state != DL_RUNNING) tdAhead = 0; /* (paused or gone meanwhile) */
    else if (px_busy_key(tdKey) && !W.busy) { uint32_t pe; double r = 0; D[i].speed = TD.fx && p2p_fetch_stats(TD.fx, &pe, &r) ? (long long)r : (long long)p2p_rate(); } /* its file's own pace while the film takes it */
    else if (!px_busy_key(tdKey) && !W.busy) { tdAhead = 0; D[i].state = DL_QUEUED; snprintf(tdWant, sizeof tdWant, "%s", tdKey); tdWait = 0; } } /* the film no longer ahead: its own fetch goes on, first */
  if (TD.on && W.busy && W.idx == TD.idx && TD.idx >= 0 && TD.idx < nd && !TD.yield) { char k2[200]; key_of(TD.idx, k2, sizeof k2); if (px_busy_key(k2)) { TD.yield = 1; W.stop = 1; fprintf(stderr, "[dl] torrent download steps aside: the film plays ahead of it\n"); } }
  if (TD.on && !W.busy && (TD.idx < 0 || TD.idx >= nd || D[TD.idx].state != DL_RUNNING || app_now() - TD.at > 240)) { /* paused while it looked for peers, or no peers for 4 minutes: let go (again in a while) */
    int i = TD.idx; if (i >= 0 && i < nd && D[i].state == DL_RUNNING) { D[i].state = ++TD.tries < 3 ? DL_QUEUED : DL_FAILED; save(); } td_end(1); return; }
  if (TD.on && player_torrent() && !TD.byPlayer) { TD.byPlayer = 1; fprintf(stderr, "[dl] torrent download waits: a torrent film plays\n"); if (W.busy && W.idx == TD.idx) W.stop = 1; else { if (TD.idx >= 0 && TD.idx < nd && D[TD.idx].state == DL_RUNNING) D[TD.idx].state = DL_QUEUED; TD.on = 0; TD.byPlayer = 0; TD.res = 0; } } } /* a film: it waits, then goes on */
static void td_tick(void) { extern int player_torrent(void); static int tok;
  for (int k = 0; k < NL; k++) if (TDL[k].fx && !TDL[k].on && !TDL[k].url[0]) { p2p_fetch_end(TDL[k].fx); TDL[k].fx = 0; } /* a fetch nothing reads any more */
  for (cl = 0; cl < NL; cl++) td_lane(); cl = 0;
  { static double lg; int q = 0; for (int i = 0; i < nd; i++) q += D[i].state == DL_QUEUED && is_p2p(&D[i]); if (q && app_now() - lg > 15) { lg = app_now(); fprintf(stderr, "[dl] %d torrent(s) wait: film %d, %d of %d running, net %d, retry in %.0f s\n", q, player_torrent(), lanes_on() + eng_running(), at_once(), net_ok(), tdWait - app_now()); } } /* why, in the log */
  if (player_torrent() || !net_ok() || app_now() < tdWait) return; /* (a film from a link: torrents download meanwhile) */
  if (lanes_on() + eng_running() >= at_once()) return; /* as many at once as the settings say, torrents and links together */
  int k = -1; for (int q = 0; q < NL; q++) if (!TDL[q].on && !WK[q].busy && !TDL[q].ahead && (k < 0 || !TDL[q].url[0])) { k = q; if (!TDL[q].url[0]) break; } if (k < 0) return; /* (one the film reads from last) */
  int first = -1; if (tdWant[0]) for (int i = 0; i < nd; i++) { char k2[200]; key_of(i, k2, sizeof k2); if (!strcmp(k2, tdWant) && D[i].state == DL_QUEUED) first = i; }
  for (int j = -1; j < nd; j++) { int i = j < 0 ? first : j; if (i < 0 || (j >= 0 && i == first)) continue; Dl *d = &D[i]; if (d->state != DL_QUEUED || !is_p2p(d) || d->wifi == 2 || (d->wifi == 1 && !wifi_now())) continue;
    { char k2[200]; key_of(i, k2, sizeof k2); if (px_busy_key(k2)) continue; } /* (the film reads that one itself just now) */ if (i == first) tdWant[0] = 0;
    if (!p2p_available()) { d->state = DL_FAILED; toast(T("Torrents need the app\xe2\x80\x99s engine: pick a direct link"), 2600, NULL); save(); return; }
    cl = k; if (TD.idx != i) TD.tries = 0; p2p_fetch_end(TD.fx); TD.on = 1; TD.res = 1; TD.idx = i; TD.byPlayer = 0; TD.tok = ++tok; TD.at = app_now(); tdUrl[0] = 0; d->state = DL_RUNNING; d->speed = 0;
    TD.fx = p2p_fetch(d->url, td_cb, (void *)(intptr_t)tok); cl = 0; fprintf(stderr, "[dl] torrent %s %s: finding peers (lane %d)\n", d->id, d->ep, k);
    if (!TDL[k].fx) { cl = k; td_cb_lane(NULL, T("Couldn\xe2\x80\x99t start the torrent engine"), 0); cl = 0; } return; } }
/* a torrent downloading: "12 peers · 2.4 MB/s" (the player's line), "Finding peers…" before */
float ph_dl_fraction(const char *id, const char *vid) { load(); int i = find(id, vid && *vid ? vid : id); if (i < 0 || !D[i].seq || D[i].size <= 0) return -1; return D[i].state == DL_DONE ? 1 : (float)((double)D[i].got / D[i].size); } /* the part on the phone, from the start */
const char *ph_dl_wait_word(const Dl *d) { (void)d; return NULL; } /* (no words for why it waits: its state alone) */
int ph_dl_after_film(const Dl *d) { extern int player_torrent(void); return d->state == DL_QUEUED && is_p2p(d) && player_torrent(); } /* a torrent waiting for the film's torrent to end */
int ph_dl_peers(const Dl *d, char *out, int cap) { int i = (int)(d - D), L = i >= 0 && i < nd && is_p2p(d) ? lane_of(i) : -1; if (L < 0 && i >= 0 && i < nd && is_p2p(d)) { char k2[200]; key_of(i, k2, sizeof k2); for (int k = 0; k < NL; k++) if (TDL[k].ahead && !strcmp(TDL[k].key, k2)) L = k; } if (L < 0) return 0; /* (the film taking it: its lane still) */ /* its peers alone ("12 peers"; its speed is its own, beside) */
  uint32_t pe = 0; double r; char ln[160]; if (TDL[L].fx ? p2p_fetch_stats(TDL[L].fx, &pe, &r) : (p2p_peer_line(ln, sizeof ln) && (pe = (uint32_t)atoi(ln)) > 0)) { if (pe) { char n[16]; snprintf(n, sizeof n, "%u", pe); snprintf(out, cap, "%s", T1("{n} peers", "n", n)); return 1; } }
  snprintf(out, cap, "%s", T("Finding peers\xe2\x80\xa6")); return 1; }
/* an episode whose file turns out to be a whole season (a pack served as one file): far above what one episode
   weighs at the chosen quality (best: 15 GB), so it is dropped and another stream looked for */
static int too_big(const Dl *d) { if (strcmp(d->type, "series") || d->size <= 0) return 0; double mbps; int r_, h_; ph_dl_prefs(&mbps, &r_, &h_); double lim = mbps > 0 ? mbps * 1e6 / 8 * 60 * 60 * 3 : 15e9; /* 3 times an hour's worth */
  return (double)d->size > lim; }
static void drop_pack(int i) { Dl *d = &D[i]; fprintf(stderr, "[dl] %s %s is %lld bytes: a season pack, another stream\n", d->id, d->ep, d->size);
  best_skip(d->url); if (eng() && d->gid[0]) eng()->remove(d->gid, 1); d->gid[0] = 0; if (d->file[0]) remove(d->file); d->file[0] = 0; d->url[0] = 0; d->size = d->got = 0; d->state = DL_CHOOSING; save(); q_push(i); q_next(); }
/* the settings: where files go, how many at once, Wi-Fi only */
int pplat_on_wifi(void);
static void dl_dir(char *dir, size_t c) { const char *u = store_get("dlDir", ""); if (*u && (mkdir(u, 0755) == 0 || errno == EEXIST)) { snprintf(dir, c, "%s", u); return; } snprintf(dir, c, "%s/downloads", store_data_dir()); mkdir(dir, 0755); }
static int at_once(void) { int n = atoi(store_get("dlAtOnce", "0")); return n > 0 ? n : NL; } /* (all: as many lanes as there are) */
static int net_ok(void) { static double at; static int ok = 1; if (!store_bool("dlWifi", 0)) return 1; if (app_now() - at > 3) { at = app_now(); ok = pplat_on_wifi(); } return ok; } /* (asked every few seconds) */
int ph_dl_waiting_wifi(void) { if (!net_ok()) return 1; if (wifi_now()) return 0; for (int i = 0; i < nd; i++) if (D[i].wifi) return 1; return 0; }
int ph_dl_in_order(void) { return !strcmp(store_get("dlMode", "fast"), "order"); }
static void eng_tick(const sbdl_api *e) {
  { static int lastMode = -1; int m = ph_dl_in_order(); if (m != lastMode) { if (lastMode >= 0) { char sd[700]; snprintf(sd, sizeof sd, "%s/dlengine", store_data_dir()); e->start(sd, m ? 1 : 8); } lastMode = m; } } /* (the mode changed: new downloads take it) */
  int changed = 0; char dir[600]; dl_dir(dir, sizeof dir); int ok = net_ok(), lim = at_once() - lanes_on(), run = 0; /* (the torrents running count too) */ for (int i = 0; i < nd; i++) run += D[i].gid[0] && D[i].state == DL_RUNNING && !D[i].held;
  for (int i = 0; i < nd; i++) { Dl *d = &D[i]; /* off Wi-Fi when only Wi-Fi is allowed: what runs waits, and goes on once back */
    if (!ok && d->gid[0] && d->state == DL_RUNNING && !d->held) { e->pause(d->gid); d->held = 1; d->state = DL_QUEUED; run--; changed = 1; }
    else if (ok && d->held) { if (run < lim) { e->resume(d->gid); d->held = 0; d->state = DL_RUNNING; d->userAt = app_now(); run++; changed = 1; } } }
  for (int i = 0; i < nd; i++) { Dl *d = &D[i];
    if (d->held) continue;
    if (d->state == DL_QUEUED && !d->gid[0] && d->url[0] && !is_p2p(d)) { if (!ok || run >= lim || d->wifi == 2 || (d->wifi == 1 && !wifi_now())) continue; run++; /* (asked about, or waiting for Wi-Fi) */ char id[48] = ""; if (e->add(d->url, dir, "", "User-Agent: Mozilla/5.0 (Linux; Android 14) StremioOrigin/0.1\n", id, sizeof id) == 0 && id[0]) { d->seq = ph_dl_in_order(); snprintf(d->gid, sizeof d->gid, "%s", id); d->state = DL_RUNNING; fprintf(stderr, "[dl] engine took %s %s\n", d->id, d->ep); } else d->state = DL_FAILED; changed = 1; continue; }
    if (!d->gid[0] || d->state == DL_DONE) continue;
    sbdl_stat st; memset(&st, 0, sizeof st); if (e->stat(d->gid, &st)) { if (app_now() - eAt < 15) continue; /* still reloading its tasks */ d->gid[0] = 0; if (d->state != DL_PAUSED) d->state = DL_QUEUED; changed = 1; continue; } /* the engine lost it: handed over again */
    d->got = st.done; if (st.total > 0) d->size = st.total; if (st.total > 0 && too_big(d)) { drop_pack(i); return; } d->speed = st.speed; d->conns = st.connections; if (st.path[0] && strcmp(d->file, st.path)) { snprintf(d->file, sizeof d->file, "%s", st.path); changed = 1; }
    int ns = st.state == SBDL_DONE ? DL_DONE : st.state == SBDL_PAUSED ? DL_PAUSED : st.state == SBDL_ERROR ? DL_FAILED : st.state == SBDL_WAIT ? DL_QUEUED : DL_RUNNING;
    if (d->state == DL_PAUSED && ns == DL_QUEUED) continue; /* paused by hand: the engine's waiting is still paused */
    if (app_now() - d->userAt < 4 && ns != DL_DONE && ((d->state == DL_PAUSED) != (ns == DL_PAUSED))) continue; /* a pause or resume just asked: the engine takes a moment to follow */
    if (ns != d->state) { if (ns == DL_DONE) { done_later(d); fprintf(stderr, "[dl] done %s %s (%lld bytes)\n", d->id, d->ep, d->size); } if (ns == DL_FAILED) fprintf(stderr, "[dl] stopped %s: %s\n", d->id, st.err); d->state = ns; changed = 1; } }
  if (changed || app_now() - savedAt > 5) save(); }
/* the phone's notification while something downloads: one title (or how many), how far all of it is, how fast and
   how long left; asked once a second, gone when nothing downloads any more */
void pplat_dl_notify2(const char *title, const char *text, int pct, const char *sub, const char *lines, const char *art); void pplat_dl_finished2(const char *title, const char *text, const char *art);
/* the show's logo (else its poster), for the notification's tile */
static void art_of(const Dl *d, char *out, size_t cap) { out[0] = 0; const cJSON *m = sb_meta_cached(d->type, d->id); if (m) { Item it; sb_fill_item(&it, m, d->type); char b[600]; const char *lg = sb_logo_of(&it, b, sizeof b); if (lg && *lg) { snprintf(out, cap, "%s", lg); return; } }
  snprintf(out, cap, "%s", d->poster); }
static void ep_short(const Dl *d, char *out, size_t cap) { int se = 0, e = 0; const char *c = strchr(d->vid, ':'); if (c && sscanf(c, ":%d:%d", &se, &e) == 2) snprintf(out, cap, "S%d E%d", se, e); else out[0] = 0; }
/* finished: its seek previews made from the file first (an MKV, previews on), "Downloaded" said only then */
#include "core/seekprev.h"
#include "core/prevspike.h"
static int find(const char *id, const char *vid);
static struct { char id[64], vid[96]; int on; } PV[8]; static int pvBusy = -1;
static void say_done(const Dl *d);
static void done_later(Dl *d) { int ext = strlen(d->file) > 4 && !strcasecmp(d->file + strlen(d->file) - 4, ".mkv");
  if (!ext || !prev_enabled() || !prev_available()) { say_done(d); return; }
  for (int i = 0; i < 8; i++) if (!PV[i].on) { PV[i].on = 1; snprintf(PV[i].id, sizeof PV[i].id, "%s", d->id); snprintf(PV[i].vid, sizeof PV[i].vid, "%s", d->vid); fprintf(stderr, "[dl] %s %s: making its previews first\n", d->id, d->ep); return; }
  say_done(d); }
static void pv_tick(void) { if (pvBusy >= 0) { if (sp_make_busy()) return; int i = pvBusy; pvBusy = -1; PV[i].on = 0; int k = find(PV[i].id, PV[i].vid); if (k >= 0) say_done(&D[k]); }
  for (int i = 0; i < 8; i++) if (PV[i].on) { int k = find(PV[i].id, PV[i].vid); if (k < 0) { PV[i].on = 0; continue; } char u[700], key[300]; snprintf(u, sizeof u, "file://%s", D[k].file); snprintf(key, sizeof key, "%s|%s|0|0", D[k].id, D[k].vid);
      if (sp_make_file(u, key)) pvBusy = i; else { PV[i].on = 0; say_done(&D[k]); } return; } }
static void notify_done(const Dl *d) { char art[600], ep[32]; art_of(d, art, sizeof art); ep_short(d, ep, sizeof ep); char x[200]; snprintf(x, sizeof x, "%s%s%s", ep, ep[0] ? "  \xc2\xb7  " : "", T("Downloaded")); pplat_dl_finished2(d->name, x, art); }
static void say_done(const Dl *d) { toast(T("Downloaded"), 1800, NULL); notify_done(d); }
static void fmt_left(long long left, char *eta, size_t cap) { char m[16]; if (left >= 3600) { snprintf(m, sizeof m, "%lld", left / 3600); snprintf(eta, cap, "%s", T1("{n} h left", "n", m)); } else if (left >= 60) { snprintf(m, sizeof m, "%lld", left / 60); snprintf(eta, cap, "%s", T1("{m} min left", "m", m)); } else snprintf(eta, cap, "%s", T("Less than a minute left")); }
/* while something downloads: the show's name, its episode, how far, how fast, how long left; several: how many and one
   line each; the show's logo on its tile; once a second; gone when nothing downloads any more */
static double notifyAt; static void notify_now(void) { notifyAt = -9; } /* (a download just added: said at once, "finding a stream" too) */
static void notify_tick(void) { if (app_now() - notifyAt < 1.0) return; notifyAt = app_now();
  int run = 0, wait = 0, one = -1, act = 0; long long got = 0, size = 0, spd = 0; char lines[1200] = ""; size_t L = 0;
  for (int i = 0; i < nd; i++) { Dl *d = &D[i]; int on = d->state == DL_RUNNING, w = d->state == DL_QUEUED || d->state == DL_CHOOSING; if (!on && !w) continue; act++; if (one < 0 || (on && D[one].state != DL_RUNNING)) one = i;
    if (on) { run++; got += d->got; size += d->size; spd += d->speed; }
    if (L < sizeof lines - 200) { char ep[32]; ep_short(d, ep, sizeof ep); char st[96]; if (on && d->size > 0) { char a1[24], b1[24]; if (d->size >= 1000000000) { snprintf(a1, sizeof a1, "%.1f", d->got / 1e9); snprintf(b1, sizeof b1, "%.1f GB", d->size / 1e9); } else { snprintf(a1, sizeof a1, "%.0f", d->got / 1e6); snprintf(b1, sizeof b1, "%.0f MB", d->size / 1e6); } snprintf(st, sizeof st, "%d%%  (%s / %s)", (int)(100.0 * d->got / d->size), a1, b1); } else snprintf(st, sizeof st, "%s", d->state == DL_CHOOSING ? T("Finding a stream") : T("Waiting"));
      L += snprintf(lines + L, sizeof lines - L, "%s%s%s  \xe2\x80\x94  %s\n", d->name, ep[0] ? "  \xc2\xb7  " : "", ep, st); } }
  int pz = -1, npz = 0; for (int i = 0; i < nd; i++) if (D[i].state == DL_PAUSED) { npz++; if (pz < 0) pz = i; }
  if (!act && npz) { /* only paused ones: said so, how far, no spinner */ char t2[300], x2[240], ep2[32], art2[600]; art_of(&D[pz], art2, sizeof art2); ep_short(&D[pz], ep2, sizeof ep2);
    if (npz == 1) { snprintf(t2, sizeof t2, "%s", D[pz].name); int pc = D[pz].size > 0 ? (int)(100.0 * D[pz].got / D[pz].size) : -1; char pcs[16] = ""; if (pc >= 0) snprintf(pcs, sizeof pcs, "%d%%", pc); snprintf(x2, sizeof x2, "%s%s%s", ep2, ep2[0] && pcs[0] ? "  \xc2\xb7  " : "", pcs); }
    else { char n[16]; snprintf(n, sizeof n, "%d", npz); snprintf(t2, sizeof t2, "%s", T1("{n} downloads", "n", n)); x2[0] = 0; }
    pplat_dl_notify2(t2, x2, npz == 1 && D[pz].size > 0 ? (int)(100.0 * D[pz].got / D[pz].size) : -2, T("Paused"), "", art2); return; }
  if (!act) { pplat_dl_notify2(NULL, NULL, -1, "", "", ""); return; }
  char t[300], x[240], sub[64], art[600], ep[32]; art_of(&D[one], art, sizeof art); ep_short(&D[one], ep, sizeof ep);
  char sp[32] = "", eta[48] = ""; if (spd > 0) { if (spd >= 1000000) snprintf(sp, sizeof sp, "%.1f MB/s", spd / 1e6); else snprintf(sp, sizeof sp, "%.0f KB/s", spd / 1e3); if (size > got) fmt_left((size - got) / spd, eta, sizeof eta); }
  if (act == 1) { snprintf(t, sizeof t, "%s", D[one].name); snprintf(sub, sizeof sub, "%s", run ? T("Downloading") : T("Waiting"));
    if (run && size > 0) { char sz[64]; if (size >= 1000000000) snprintf(sz, sizeof sz, "%.1f / %.1f GB", got / 1e9, size / 1e9); else snprintf(sz, sizeof sz, "%.0f / %.0f MB", got / 1e6, size / 1e6);
      snprintf(x, sizeof x, "%s%s%s%s%s%s%s", ep, ep[0] ? "  \xc2\xb7  " : "", sz, sp[0] ? "  \xc2\xb7  " : "", sp, eta[0] ? "  \xc2\xb7  " : "", eta); }
    else snprintf(x, sizeof x, "%s%s%s", ep, ep[0] ? "  \xc2\xb7  " : "", D[one].state == DL_CHOOSING ? T("Finding a stream") : ph_dl_waiting_wifi() ? T("Waiting for Wi-Fi") : T("Waiting")); lines[0] = 0; }
  else { char n[16]; snprintf(n, sizeof n, "%d", act); snprintf(t, sizeof t, "%s", T1("{n} downloads", "n", n)); snprintf(sub, sizeof sub, "%s", T("Downloading"));
    char sz[64] = ""; if (size > 0) { if (size >= 1000000000) snprintf(sz, sizeof sz, "%.1f / %.1f GB", got / 1e9, size / 1e9); else snprintf(sz, sizeof sz, "%.0f / %.0f MB", got / 1e6, size / 1e6); }
    snprintf(x, sizeof x, "%s%s%s%s%s", sz, sz[0] && sp[0] ? "  \xc2\xb7  " : "", sp, eta[0] ? "  \xc2\xb7  " : "", eta); }
  { static int wasAct; if (!wasAct) fprintf(stderr, "[dl] notification: %s | %s\n", t, x); wasAct = 1; } pplat_dl_notify2(t, x, run && size > 0 ? (int)(100.0 * got / size) : run ? -1 : -2, sub, lines, art); }
/* one lane's file: how far, how fast, done or stopped, its next missing part */
static void lane_tick(void) {
  if (W.busy && W.done && (W.idx < 0 || W.idx >= nd)) { W.busy = 0; if (TD.on && TD.idx < 0) TD.on = 0; fprintf(stderr, "[dl] the worker of a removed download ended\n"); } /* (its download removed meanwhile: nothing else would let it go) */
  if (W.busy && W.idx >= 0 && W.idx < nd) { Dl *d = &D[W.idx]; int part = W.to >= 0; d->got = part ? rg_cov(d) + W.got : W.got; if (W.total > 0 && !part) d->size = W.total; int tor = TD.on && TD.idx == W.idx;
    { static double wt_[NL], sp_[NL]; static long long wg_[NL]; static int wi_[NL] = { -1, -1, -1, -1, -1, -1 }; double t = app_now(); /* how fast: what came into its file, smoothed over a few seconds (a torrent comes in bursts) */
      if (wi_[cl] != W.idx) { wi_[cl] = W.idx; wt_[cl] = t; wg_[cl] = W.got; sp_[cl] = d->speed > 0 ? (double)d->speed : 0; }
      else if (t - wt_[cl] >= 0.5) { long long dg = W.got - wg_[cl]; double inst = dg > 0 ? dg / (t - wt_[cl]) : 0; sp_[cl] = sp_[cl] <= 0 ? inst : sp_[cl] * 0.8 + inst * 0.2; wt_[cl] = t; wg_[cl] = W.got; d->speed = (long long)sp_[cl]; } } /* how fast, for the page and the notification */
    if (W.total > 0 && d->state == DL_RUNNING && too_big(d)) { W.stop = 1; if (tor) td_end(0); drop_pack(W.idx); return; } /* the worker stops; its end is then ignored */
    if (d->state == DL_CHOOSING) { d->got = d->size = 0; if (W.done) W.busy = 0; return; }
    if (W.done && part) { rg_add(d, W.from, W.from + W.got); d->got = rg_cov(d); long long gs, ge; int all = d->size > 0 && !rg_gap(d, &gs, &ge), whole = W.got >= W.to - W.from + 1; /* a part done: its place kept; the next missing part next, on the same address */
      if (!all && whole && tor && !W.stop && d->state == DL_RUNNING) { W.busy = 0; d->speed = 0; save(); start_from(W.idx, tdUrl); return; }
      if (all) { W.status = 206; d->got = d->size; } }
    if (W.done) { W.busy = 0; int ok = (W.status == 200 || W.status == 206) && d->got > 0 && (!d->size || d->got >= d->size); d->speed = 0;
      if (tor) { int byPl = TD.byPlayer; int ahead = TD.yield && !ok; if (ahead) tdAhead = 1; if (d->state == DL_RUNNING && !ahead) d->state = ok ? DL_DONE : byPl ? DL_QUEUED : W.stop ? DL_PAUSED : ++TD.tries < 4 ? DL_QUEUED : DL_FAILED; td_end(!ok && !byPl && d->state == DL_QUEUED); } /* a film came: it goes on after; the swarm went quiet: again in a while */
      else if (d->state == DL_RUNNING) d->state = ok ? DL_DONE : W.stop ? DL_PAUSED : DL_FAILED;
      fprintf(stderr, "[dl] %s: status %d, %lld of %lld bytes\n", ok ? "done" : "stopped", W.status, d->got, d->size);
      if (ok) done_later(d); save(); }
    else if (app_now() - savedAt > 5) save(); }
}
/* files left behind: once a start, a minute in (the engine's tasks have told their files by then), a video in the app's
   own downloads folder that no download names is deleted. Only the app's private folder (never a folder the user chose),
   only video files, never one written to in the last 10 minutes, and not at all while a download's file is not settled */
#include <dirent.h>
static void sweep_orphans(void) { if (store_get("dlDir", "")[0]) return; char dir[600]; snprintf(dir, sizeof dir, "%s/downloads", store_data_dir());
  for (int i = 0; i < nd; i++) { struct stat st; if (D[i].gid[0] && D[i].state != DL_DONE && D[i].file[0] && stat(D[i].file, &st) != 0) { fprintf(stderr, "[dl] orphans: not now (%s %s's file not on disk yet)\n", D[i].name, D[i].ep); return; } }
  DIR *dd = opendir(dir); if (!dd) return; struct dirent *e; int n = 0; long long b = 0; time_t now = time(NULL);
  while ((e = readdir(dd))) { const char *nm = e->d_name; size_t l = strlen(nm); if (l < 5 || (strcasecmp(nm + l - 4, ".mkv") && strcasecmp(nm + l - 4, ".mp4") && strcasecmp(nm + l - 4, ".avi"))) continue;
    char path[1200]; snprintf(path, sizeof path, "%s/%s", dir, nm); int used = 0; for (int i = 0; i < nd && !used; i++) { const char *f = D[i].file, *fs = strrchr(f, '/'); used = !strcmp(f, path) || (fs && !strcmp(fs + 1, nm)); }
    struct stat st; if (used || stat(path, &st) != 0 || now - st.st_mtime < 600) continue;
    if (!remove(path)) { n++; b += st.st_size; fprintf(stderr, "[dl] orphans: deleted %s (%lld MB, no download names it)\n", nm, (long long)st.st_size / 1000000); } }
  closedir(dd); if (n) fprintf(stderr, "[dl] orphans: %d files, %lld MB freed\n", n, b / 1000000); }
void ph_dl_tick(void) { pv_tick(); /* (a finished download's previews, then its "Downloaded") */
  { static int swept; if (!swept && app_now() > 60) { swept = 1; load(); sweep_orphans(); } }
  notify_tick(); sv_update();
  load(); if (asking && !ph_sheet_on() && app_now() - askAt > 0.8) choices_cb(1); /* put away unanswered: they wait for Wi-Fi */
  for (cl = 0; cl < NL; cl++) lane_tick(); cl = 0;
  td_tick();
  { const sbdl_api *e = eng(); if (e) { eng_tick(e); return; } }
  if (net_ok() && lanes_on() < at_once()) { int k = -1; for (int q = 0; q < NL; q++) if (!TDL[q].on && !WK[q].busy && !TDL[q].ahead) { k = q; break; } /* (no engine: the plain worker, in a free lane) */
    if (k >= 0) for (int i = 0; i < nd; i++) if (D[i].state == DL_QUEUED && !is_p2p(&D[i]) && D[i].wifi != 2 && !(D[i].wifi == 1 && !wifi_now()) && lane_of(i) < 0) { cl = k; start(i); cl = 0; break; } }
}
/* ---- what the pages ask ---- */
static int find(const char *id, const char *vid) { for (int i = 0; i < nd; i++) if (!strcmp(D[i].id, id) && !strcmp(D[i].vid, vid)) return i; return -1; }
long long ph_dl_bytes(void) { load(); long long b = 0; for (int i = 0; i < nd; i++) b += D[i].state == DL_DONE ? D[i].size : D[i].got; return b; }
void ph_dl_remove_all(void) { load(); while (nd > 0) ph_dl_remove(nd - 1); }
int ph_dl_toggle(const char *id, const char *vid) { load(); int i = find(id, vid); if (i < 0) return -1; ph_dl_pause(i); return D[i].state; } /* (the player's button again: pause or go on) */
/* resuming (Continue Watching, Resume, from the start): a finished download of that film or episode plays from the phone,
   no stream looked for, and says so */
static int sv_serve(int i); static int svPort;
static int local_stream(const char *id, const char *vid, Stream *out) { load(); int i = find(id, vid && *vid ? vid : id); if (i < 0 && vid && *vid) i = find(id, id);
  if (i >= 0 && D[i].state != DL_DONE && D[i].seq && D[i].size > 0 && D[i].got > 1024 * 1024) { int k = sv_serve(i); /* still coming, in order: from the phone at once, the player waits at its edge */
    if (k >= 0) { memset(out, 0, sizeof *out); snprintf(out->url, sizeof out->url, "http://127.0.0.1:%d/%d", svPort, k); snprintf(out->name, sizeof out->name, "%s", T("Downloaded")); snprintf(out->title, sizeof out->title, "%s", D[i].ep[0] ? D[i].ep : D[i].name);
      snprintf(out->addon, sizeof out->addon, "%s", T("On this phone")); if (D[i].state == DL_PAUSED || D[i].state == DL_FAILED) ph_dl_pause(i);
      if (is_p2p(&D[i]) && lane_of(i) < 0) { snprintf(tdWant, sizeof tdWant, "%s|%s", D[i].id, D[i].vid); tdWait = 0; if (lanes_on() + eng_running() >= at_once()) for (int k = NL - 1; k >= 0; k--) if (TDL[k].on && WK[k].busy && WK[k].idx == TDL[k].idx) { TDL[k].yield = 1; WK[k].stop = 1; break; } } /* the one being watched first: another torrent steps aside when all lanes are taken */
      toast(T("Playing from your phone as it downloads"), 1800, NULL); return 1; } }
  if (i < 0 || D[i].state != DL_DONE || !D[i].file[0]) return 0; struct stat sb2; if (stat(D[i].file, &sb2) != 0) return 0; /* (the file still there) */
  memset(out, 0, sizeof *out); snprintf(out->url, sizeof out->url, "file://%s", D[i].file); snprintf(out->name, sizeof out->name, "%s", T("Downloaded")); snprintf(out->title, sizeof out->title, "%s", D[i].ep[0] ? D[i].ep : D[i].name);
  snprintf(out->addon, sizeof out->addon, "%s", T("On this phone")); toast(T("Playing from your phone"), 1800, NULL); return 1; }
int ph_dl_local_has(const char *id, const char *vid) { load(); int i = find(id, vid && *vid ? vid : id); return i >= 0 && D[i].state == DL_DONE; } /* (a card's mark) */
int ph_dl_auto(void) { return store_bool("dlAuto", 1); } /* downloads choose their stream (with the quality chosen), or the user always does */
int ph_dl_done_any(const char *id) { load(); for (int i = 0; i < nd; i++) if (D[i].state == DL_DONE && !strcmp(D[i].id, id)) return 1; return 0; } /* (a poster's mark) */
const Dl *ph_dl_find(const char *id, const char *vid) { load(); int i = find(id, vid); return i >= 0 ? &D[i] : NULL; }
int ph_dl_state(const char *id, const char *vid, float *prog) { load(); int i = find(id, vid); if (i < 0) return -1; if (prog) *prog = D[i].size > 0 ? (float)((double)D[i].got / D[i].size) : 0; return D[i].state; }
static void choices_cb(int i) { asking = 0; for (int j = 0; j < nd; j++) if (D[j].wifi == 2) D[j].wifi = i == 1 ? 1 : 0; save(); if (i == 1) toast(T("They start once you\xe2\x80\x99re on Wi-Fi"), 2200, NULL); }
static void ask_data(void) { extern int player_open(void); if (store_bool("dlWifi", 0) || wifi_now()) return; if (player_open()) { for (int j = 0; j < nd; j++) if (D[j].wifi == 2) D[j].wifi = 0; toast(T("Downloading on mobile data"), 2200, NULL); return; } /* (in the player a question would be under the film) */ if (asking) return; asking = 1; askAt = app_now();
  const char *l[2] = { T("Download now on mobile data"), T("Wait for Wi-Fi") }; choices_open(T("You\xe2\x80\x99re not on Wi-Fi"), l, 2, 0, choices_cb); }
static int dl_add_(const Item *it, const char *vid, const char *ep, const Stream *s, const char *group, int quiet);
void ph_dl_add(const Item *it, const char *vid, const char *ep, const Stream *s) { dl_add_(it, vid, ep, s, "", 0); }
int ph_dl_add_quiet(const Item *it, const char *vid, const char *ep, const Stream *s) { return dl_add_(it, vid, ep, s, "", 1); }
/* the stream as its addon gave it: the addon, its name, its lines, the file */
static void info_of(const Stream *s, char *o, size_t c) { snprintf(o, c, "%s\n%s\n%s%s%s", s->addon, s->name, s->title, s->filename[0] ? "\n" : "", s->filename); }
static char pgId[64], pgGroup[160]; static double pgAt; /* (a title's streams being picked by hand: the group it joins) */
void ph_dl_group_next(const char *id, const char *group) { snprintf(pgId, sizeof pgId, "%s", id ? id : ""); snprintf(pgGroup, sizeof pgGroup, "%s", group ? group : ""); pgAt = app_now(); }
void ph_coll_group(const char *key, const char *name, char *out, size_t cap) { char nm[120]; snprintf(nm, sizeof nm, "%s", name ? name : ""); for (int k = (int)strlen(nm); k > 0 && (nm[k] & 0xC0) == 0x80; k--) nm[k] = 0; /* (never cut inside a letter) */ snprintf(out, cap, "coll:%s\x1f%s", key, nm); }
int ph_dl_group_is_coll(const Dl *d) { return !strncmp(d->group, "coll:", 5); }
const char *ph_dl_group_name(const Dl *d) { if (!ph_dl_group_is_coll(d)) return d->group; const char *u = strchr(d->group, '\x1f'); return u ? u + 1 : d->group + 5; }
static int dl_add_(const Item *it, const char *vid, const char *ep, const Stream *s, const char *group, int quiet) {
  if ((!group || !*group) && pgId[0] && !strcmp(pgId, it->id) && app_now() - pgAt < 600) { group = pgGroup; pgId[0] = 0; } /* (picked from a collection's page) */
  load(); if (s->p2p ? !p2p_available() : strncmp(s->url, "http", 4) != 0) { if (!quiet) toast(s->p2p ? T("Torrents need the app\xe2\x80\x99s engine: pick a direct link") : T("This stream can\xe2\x80\x99t be downloaded: pick another"), 2600, NULL); return 0; }
  int i = find(it->id, vid); if (i >= 0 && D[i].state == DL_DONE) { if (!quiet) toast(T("Already downloaded"), 1600, NULL); return 0; }
  if (i < 0) { if (nd >= MAXD) { toast(T("Too many downloads: remove some first"), 2200, NULL); return 0; } i = nd++; }
  Dl *d = &D[i]; memset(d, 0, sizeof *d); snprintf(d->id, sizeof d->id, "%s", it->id); snprintf(d->type, sizeof d->type, "%s", it->type); snprintf(d->vid, sizeof d->vid, "%s", vid);
  snprintf(d->name, sizeof d->name, "%s", it->name); snprintf(d->ep, sizeof d->ep, "%s", ep ? ep : ""); char pb[600]; snprintf(d->poster, sizeof d->poster, "%s", sb_poster_of(it, pb, sizeof pb)); snprintf(d->bg, sizeof d->bg, "%s", it->background);
  snprintf(d->url, sizeof d->url, "%s", s->url); info_of(s, d->info, sizeof d->info); snprintf(d->group, sizeof d->group, "%s", group ? group : ""); d->size = s->size > 0 ? (long long)s->size : 0; d->state = DL_QUEUED; NEWWAIT(d); save(); notify_now(); notify_tick(); ask_data(); { void pplat_ask_notifications(void); pplat_ask_notifications(); }
  if (!quiet) ph_fly(d->poster, 3); /* the poster flies to the Downloads tab */
  return 1;
}
static void sv_drop(const char *id, const char *vid);
void ph_dl_remove(int i) { load(); if (i < 0 || i >= nd) return;
  if (is_p2p(&D[i])) { char k2[200]; key_of(i, k2, sizeof k2); for (int k = 0; k < NL; k++) if (!strcmp(k2, TDL[k].key)) { TDL[k].ahead = 0; TDL[k].url[0] = 0; TDL[k].key[0] = 0; TDL[k].tries = 0; } if (!strcmp(k2, tdWant)) tdWant[0] = 0; pxAt = 0; tdWait = 0; } /* (a torrent gone: the next one at once) */
  sv_drop(D[i].id, D[i].vid); /* (anything reading it stops) */ if (eng() && D[i].gid[0]) eng()->remove(D[i].gid, 1); remove(D[i].file);
  for (int k = 0; k < NL; k++) { Wk *w = &WK[k]; Td *t = &TDL[k]; if (w->busy && w->idx == i) { w->stop = 1; D[i].state = DL_PAUSED; } if (w->busy && w->idx > i) w->idx--; else if (w->busy && w->idx == i) w->idx = -1;
    if (t->on && t->idx == i) { t->on = 0; t->res = 0; t->idx = -1; p2p_fetch_end(t->fx); t->fx = 0; t->url[0] = 0; } else if (t->idx > i) t->idx--; }
  memmove(&D[i], &D[i + 1], (size_t)(nd - i - 1) * sizeof *D); nd--; save(); }
void ph_dl_pause(int i) { load(); if (i < 0 || i >= nd) return; if (eng() && D[i].gid[0]) { if (D[i].state == DL_RUNNING || D[i].state == DL_QUEUED) { eng()->pause(D[i].gid); D[i].state = DL_PAUSED; } else if (D[i].state == DL_PAUSED || D[i].state == DL_FAILED) { eng()->resume(D[i].gid); D[i].state = DL_RUNNING; } D[i].userAt = app_now(); fprintf(stderr, "[dl] %s %s by hand\n", D[i].name, D[i].state == DL_PAUSED ? "paused" : "resumed"); save(); return; } if (D[i].state == DL_RUNNING) { int k = lane_of(i); if (k >= 0) WK[k].stop = 1; D[i].state = DL_PAUSED; } else if (D[i].state == DL_QUEUED) D[i].state = DL_PAUSED; else if (D[i].state == DL_PAUSED || D[i].state == DL_FAILED) D[i].state = DL_QUEUED; save(); }
/* ---- playing a download that is still coming, from the phone: a tiny server on the phone's own address feeds the
   player the part already downloaded (in order, so it is all there from the start) and waits at its edge until more
   has come; the player sees the whole file (its full size), seeks and all ---- */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
static struct { char path[600], id[64], vid[96], up[SB_URL_MAX]; long long size, got, rg[24][2], pend[16][2]; int nrg, npend, done, live, wr, tee; char teeUp[600], teeSrc[200]; long long badA, badB, hFrom[8]; double hAt[8]; int hN; } SV[4]; /* hFrom/hAt: where the player opened it lately */ /* tee: a torrent film played through here (its engine address teeUp): what passes is kept once it is a download */ static pthread_mutex_t svMu = PTHREAD_MUTEX_INITIALIZER; static int svPort, svSock = -1;
/* the server's view of each download played: its parts (a torrent's, else from the start to what has come), where a part
   not here yet comes from (the torrent engine, the link), and the parts the film fetched itself (written into the file,
   then counted by the download: what is watched ahead is downloaded too) */
static void sv_drop(const char *id, const char *vid) { pthread_mutex_lock(&svMu); for (int q = 0; q < 4; q++) if (SV[q].live && !strcmp(SV[q].id, id) && !strcmp(SV[q].vid, vid)) { SV[q].live = 0; SV[q].tee = 0; } pthread_mutex_unlock(&svMu); }
static void sv_update(void) { extern int player_open(void); pthread_mutex_lock(&svMu); for (int k = 0; k < 4; k++) if (SV[k].live) { if (SV[k].tee && !player_open()) { for (int L = 0; L < NL; L++) if (TDL[L].url[0] && !strcmp(TDL[L].url, SV[k].teeUp)) { char k2[200]; snprintf(k2, sizeof k2, "%s|%s", SV[k].id, SV[k].vid); TDL[L].url[0] = 0; pxAt = 0;
        for (int j = 0; j < nd; j++) if (!strcmp(D[j].id, SV[k].id) && !strcmp(D[j].vid, SV[k].vid) && D[j].state == DL_RUNNING && lane_of(j) < 0) { D[j].state = DL_QUEUED; TDL[L].ahead = 0; snprintf(tdWant, sizeof tdWant, "%s", k2); tdWait = 0; } }
      SV[k].tee = 0; SV[k].teeUp[0] = 0; SV[k].up[0] = 0; } /* (the film gone: its engine address too; the download on its own at once, first) */
    int i = find(SV[k].id, SV[k].vid); if (i >= 0 && SV[k].tee && strncmp(D[i].url, SV[k].teeSrc, strlen(SV[k].teeSrc))) i = -1; /* (a download of another torrent of it: nothing kept from this one) */
    if (i < 0 && SV[k].tee) { SV[k].nrg = 0; SV[k].wr = 0; SV[k].done = 0; snprintf(SV[k].up, sizeof SV[k].up, "%s", SV[k].teeUp); continue; } /* (no download of it: only passed on) */
    if (i < 0) { SV[k].live = 0; continue; } Dl *d = &D[i]; if (SV[k].tee && is_p2p(d) && !d->file[0]) { dl_file_of(d); FILE *f = fopen(d->file, "ab"); if (f) fclose(f); d->seq = 1; }
    if (SV[k].tee && is_p2p(d) && SV[k].size > 0 && d->size != SV[k].size) { d->size = SV[k].size; d->nrg = 0; d->got = 0; } /* (the torrent's own size, not the addon's guess: the film sees the same file) */
    if (SV[k].tee && is_p2p(d)) { int L = -1; char k2[200]; snprintf(k2, sizeof k2, "%s|%s", d->id, d->vid); for (int q = 0; q < NL; q++) if (!strcmp(TDL[q].key, k2)) L = q; if (L < 0) for (int q = 0; q < NL; q++) if (!TDL[q].on && !WK[q].busy && !TDL[q].ahead && !TDL[q].url[0]) { L = q; break; } if (L >= 0) { snprintf(TDL[L].url, sizeof TDL[L].url, "%s", SV[k].teeUp); snprintf(TDL[L].key, sizeof TDL[L].key, "%s", k2); } } /* (downloading as the film plays it; on from there after) */ /* (a download asked for while it plays: its file, at once) */
    if (SV[k].badB > SV[k].badA) { rg_cut(d->rg, &d->nrg, SV[k].badA, SV[k].badB); d->got = rg_cov(d); if (d->state == DL_DONE) d->state = DL_QUEUED; SV[k].badA = SV[k].badB = 0; save(); } /* (a bad part found by the film: missing again) */
    if (is_p2p(d)) { for (int q = 0; q < SV[k].npend; q++) rg_add(d, SV[k].pend[q][0], SV[k].pend[q][1]); int L = lane_of(i); Wk *w = L >= 0 ? &WK[L] : NULL; if (SV[k].npend) { d->got = rg_cov(d) + (w && w->busy && w->idx == i && w->to >= 0 ? w->got : 0); } SV[k].npend = 0;
      memcpy(SV[k].rg, d->rg, sizeof d->rg); SV[k].nrg = d->nrg; if (w && w->busy && w->idx == i && w->to >= 0 && w->got > 0 && SV[k].nrg < 24) { SV[k].rg[SV[k].nrg][0] = w->from; SV[k].rg[SV[k].nrg][1] = w->from + w->got; SV[k].nrg++; } SV[k].wr = 1; }
    else { SV[k].rg[0][0] = 0; SV[k].rg[0][1] = d->got; SV[k].nrg = d->got > 0; SV[k].wr = 0; } /* (a link's download: in order, from the start) */
    SV[k].got = d->got; if (d->size > 0) SV[k].size = d->size; SV[k].done = d->state == DL_DONE; if (d->file[0]) snprintf(SV[k].path, sizeof SV[k].path, "%s", d->file);
    if (is_p2p(d)) { char k2[200]; snprintf(k2, sizeof k2, "%s|%s", d->id, d->vid); const char *lu = ""; for (int q = 0; q < NL; q++) if (TDL[q].url[0] && !strcmp(k2, TDL[q].key)) lu = TDL[q].url; snprintf(SV[k].up, sizeof SV[k].up, "%s", SV[k].tee && SV[k].teeUp[0] ? SV[k].teeUp : lu); } else snprintf(SV[k].up, sizeof SV[k].up, "%s", !strncmp(d->url, "http", 4) ? d->url : ""); } pthread_mutex_unlock(&svMu); }
static int px_busy_key(const char *key) { if (!px_busy() || !key || !key[0]) return 0; int k = pxK; if (k < 0 || k >= 4) return 0; char k2[200]; pthread_mutex_lock(&svMu); snprintf(k2, sizeof k2, "%s|%s", SV[k].id, SV[k].vid); pthread_mutex_unlock(&svMu); return !strcmp(k2, key); }
/* a part not here yet: from its source to the player, written into the file on the way (a torrent's) */
typedef struct { int c, ok, k, wr; long long at, mark; FILE *f; int chk; } Px;
static void px_hdr(const char *line, void *ud) { Px *x = ud; if (!strncmp(line, "HTTP/", 5)) { const char *sp = strchr(line, ' '); x->ok = sp && atoi(sp + 1) == 206; } }
static void px_push(Px *x) { if (!x->wr || x->at <= x->mark) return; pthread_mutex_lock(&svMu); if (SV[x->k].npend < 16) { SV[x->k].pend[SV[x->k].npend][0] = x->mark; SV[x->k].pend[SV[x->k].npend][1] = x->at; SV[x->k].npend++; x->mark = x->at; } pthread_mutex_unlock(&svMu); }
static int px_body(const unsigned char *p, size_t n, void *ud) { Px *x = ud;
  if (!p || !n) { char t; return !(recv(x->c, &t, 1, MSG_PEEK | MSG_DONTWAIT) == 0); } /* (the player gone: this fetch ends at once) */
  { int lv; pthread_mutex_lock(&svMu); lv = SV[x->k].live; pthread_mutex_unlock(&svMu); if (!lv) return 0; } /* (its download deleted: this read ends at once) */
  pxAt = mono_s(); pxK = x->k; if (!x->ok) return 0;
  if (!x->wr && ++x->chk % 16 == 0) { char pa[600] = ""; pthread_mutex_lock(&svMu); if (SV[x->k].wr && SV[x->k].path[0]) snprintf(pa, sizeof pa, "%s", SV[x->k].path); pthread_mutex_unlock(&svMu); if (pa[0] && (x->f = fopen(pa, "r+b"))) { x->wr = 1; x->mark = x->at; } } /* (a download asked for while this plays: kept from here on) */
  int kept = x->f && fseeko(x->f, x->at, SEEK_SET) == 0 && fwrite(p, 1, n, x->f) == n; if (!kept && x->f) { px_push(x); fclose(x->f); x->f = NULL; } /* (kept in the file; a write that fails: kept no further) */
  if (send(x->c, p, n, MSG_NOSIGNAL) <= 0) { if (kept) { x->at += (long long)n; px_push(x); } return 0; } x->at += (long long)n; if (!kept) x->mark = x->at;
  if (x->f && x->at - x->mark >= 1 << 20) { fflush(x->f); px_push(x); } return 1; } /* (counted every MB) */
static void *sv_client(void *arg) { int c = (int)(intptr_t)arg; char req[2048]; int n = (int)recv(c, req, sizeof req - 1, 0); if (n <= 0) { close(c); return NULL; } req[n] = 0;
  int k = -1; sscanf(req, "%*s /%d", &k); long long from = 0; const char *rg = strcasestr(req, "\nRange: bytes="); if (rg) from = atoll(rg + 14);
  if (k < 0 || k >= 4) { const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"; send(c, nf, strlen(nf), 0); close(c); return NULL; }
  pthread_mutex_lock(&svMu); long long size = SV[k].size; char path[600]; snprintf(path, sizeof path, "%s", SV[k].path);
  { double t = mono_s(); int seen = 0; if (rg && from > 0) { for (int q = 0; q < 8; q++) { long long d0 = from - SV[k].hFrom[q]; double age = t - SV[k].hAt[q]; if (SV[k].hAt[q] > 0 && age > 5 && age < 45 && d0 <= (64ll << 10) && d0 >= -(4ll << 20)) seen++; }
      SV[k].hFrom[SV[k].hN % 8] = from; SV[k].hAt[SV[k].hN % 8] = t; SV[k].hN++; } /* (the player opened it at the same place again and again, seconds apart: stuck there) */
    if (seen >= 2 && SV[k].wr && !SV[k].done) { long long a = from > (2ll << 20) ? from - (2ll << 20) : 0, b = from + (48ll << 20); SV[k].badA = a; SV[k].badB = b; rg_cut(SV[k].rg, &SV[k].nrg, a, b); memset(SV[k].hAt, 0, sizeof SV[k].hAt); fprintf(stderr, "[dl] the film came back to %lld MB again: that part fetched afresh\n", from >> 20); } } /* (the player stuck at the same place twice: what the file holds there is bad; from the torrent, kept anew) */
  pthread_mutex_unlock(&svMu);
  if (size <= 0 || from >= size) { const char *nr = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\n\r\n"; send(c, nr, strlen(nr), 0); close(c); return NULL; }
  char h[400]; int hl = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\nContent-Length: %lld\r\n%s%lld-%lld/%lld\r\nConnection: close\r\n\r\n", rg ? "206 Partial Content" : "200 OK", size - from, rg ? "Content-Range: bytes " : "X-Range: ", from, size - 1, size);
  if (send(c, h, hl, 0) <= 0) { close(c); return NULL; }
  FILE *f = NULL; char *b = malloc(1 << 16); long long pos = from; int idle = 0;
  while (pos < size && b) { pthread_mutex_lock(&svMu); long long end = SV[k].done ? size : rg_end_at((const long long (*)[2])SV[k].rg, SV[k].nrg, pos); int live = SV[k].live, wr = SV[k].wr; char up[SB_URL_MAX]; snprintf(up, sizeof up, "%s", SV[k].up); if (!path[0] && SV[k].path[0]) snprintf(path, sizeof path, "%s", SV[k].path); pthread_mutex_unlock(&svMu);
    if (!live) break;
    if (end > pos) { idle = 0; if (!f) { f = path[0] ? fopen(path, "rb") : NULL; if (!f) { usleep(200000); continue; } } long long want = end - pos; if (want > (1 << 16)) want = 1 << 16;
      if (fseeko(f, pos, SEEK_SET)) break; size_t r = fread(b, 1, (size_t)want, f); if (r == 0) { clearerr(f); usleep(100000); continue; } if (send(c, b, r, MSG_NOSIGNAL) <= 0) break; pos += (long long)r; continue; }
    /* not here yet: from its source, at once (kept on the way); without one, a moment for it to come (two minutes at most) */
    if (up[0]) { Px x = { c, 0, k, wr, pos, pos, NULL, 0 }; if (wr && path[0]) { x.f = fopen(path, "r+b"); } char hd[64]; snprintf(hd, sizeof hd, "Range: bytes=%lld-", pos);
      pxAt = mono_s(); pxK = k; net_pipe_sync(up, hd, 0, px_hdr, px_body, &x, 30); if (x.f) { fflush(x.f); fclose(x.f); } px_push(&x); break; }
    usleep(200000); if (++idle > 600) break; }
  free(b); if (f) fclose(f); close(c); return NULL; }
static void *sv_accept(void *arg) { (void)arg; for (;;) { int c = accept(svSock, NULL, NULL); if (c < 0) { usleep(100000); continue; } pthread_t t; if (pthread_create(&t, NULL, sv_client, (void *)(intptr_t)c) == 0) pthread_detach(t); else close(c); } return NULL; }
static int sv_start(void) { if (svSock >= 0) return 1; int s0 = socket(AF_INET, SOCK_STREAM, 0); if (s0 < 0) return 0; struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  if (bind(s0, (struct sockaddr *)&a, sizeof a) || listen(s0, 8)) { close(s0); return 0; } socklen_t al = sizeof a; getsockname(s0, (struct sockaddr *)&a, &al); svPort = ntohs(a.sin_port); svSock = s0; pthread_t t; pthread_create(&t, NULL, sv_accept, NULL); pthread_detach(t); return 1; }
static int sv_serve(int i) { if (!sv_start()) return -1; pthread_mutex_lock(&svMu); int k = -1; for (int q = 0; q < 4; q++) if (SV[q].live && !strcmp(SV[q].id, D[i].id) && !strcmp(SV[q].vid, D[i].vid)) k = q; if (k < 0) for (int q = 0; q < 4; q++) if (!SV[q].live) { k = q; break; } if (k < 0) k = 0;
  snprintf(SV[k].id, sizeof SV[k].id, "%s", D[i].id); snprintf(SV[k].vid, sizeof SV[k].vid, "%s", D[i].vid); snprintf(SV[k].path, sizeof SV[k].path, "%s", D[i].file); SV[k].size = D[i].size; SV[k].got = D[i].got; SV[k].done = 0; SV[k].live = 1; pthread_mutex_unlock(&svMu); return k; }
/* a torrent film: played through the phone's own server (its engine address behind), so a download asked for while it
   plays starts at once without the film ever changing source (what passes is kept); the address for the player */
int ph_tee_url(const char *id, const char *vid, const char *src, const char *up, double size, char *out, int cap) { load(); if (!sv_start() || size <= 0) return 0; pthread_mutex_lock(&svMu); int k = -1;
  for (int q = 0; q < 4; q++) if (SV[q].live && !strcmp(SV[q].id, id) && !strcmp(SV[q].vid, vid)) k = q; if (k < 0) for (int q = 0; q < 4; q++) if (!SV[q].live) { k = q; break; } if (k < 0) k = 3;
  if (!SV[k].live || strcmp(SV[k].id, id) || strcmp(SV[k].vid, vid)) { memset(&SV[k], 0, sizeof SV[k]); snprintf(SV[k].id, sizeof SV[k].id, "%s", id); snprintf(SV[k].vid, sizeof SV[k].vid, "%s", vid); }
  SV[k].tee = 1; { const char *q = strchr(src, '?'); snprintf(SV[k].teeSrc, sizeof SV[k].teeSrc, "%.*s", (int)(q ? q - src : (long)strlen(src)), src); } snprintf(SV[k].teeUp, sizeof SV[k].teeUp, "%s", up); snprintf(SV[k].up, sizeof SV[k].up, "%s", up); SV[k].size = (long long)size; SV[k].live = 1; pthread_mutex_unlock(&svMu);
  sv_update(); snprintf(out, cap, "http://127.0.0.1:%d/%d", svPort, k); return 1; }
/* one still coming, played now: in order (from the phone, through the server above), else from its stream online */
int ph_dl_play_any(int i) { load(); if (i < 0 || i >= nd) return 0; if (D[i].state == DL_DONE) { ph_dl_play(i); return 1; } Dl *d = &D[i]; if (!d->url[0]) return 0;
  if (d->seq && d->size > 0 && d->got > 0) { int k = sv_serve(i); if (k >= 0) { Stream st; memset(&st, 0, sizeof st); snprintf(st.url, sizeof st.url, "http://127.0.0.1:%d/%d", svPort, k); snprintf(st.name, sizeof st.name, "%s", T("Downloaded")); snprintf(st.title, sizeof st.title, "%s", d->ep[0] ? d->ep : d->name); snprintf(st.addon, sizeof st.addon, "%s", T("On this phone"));
      if (d->state == DL_PAUSED) ph_dl_pause(i); /* (paused: it goes on, or the film would wait at its edge for ever) */ toast(T("Playing from your phone as it downloads"), 2000, NULL); app_play(&st, d->type, d->id, d->vid, 0); return 1; } }
  else toast(T("Playing online: choose \xe2\x80\x9cIn order\xe2\x80\x9d in download settings to play from your phone"), 2800, NULL);
  Stream st; memset(&st, 0, sizeof st); snprintf(st.url, sizeof st.url, "%s", d->url); snprintf(st.title, sizeof st.title, "%s", d->ep[0] ? d->ep : d->name); const char *nl = strchr(d->info, '\n'); snprintf(st.addon, sizeof st.addon, "%.*s", nl ? (int)(nl - d->info) : 0, d->info);
  if (nl) { const char *n2 = strchr(nl + 1, '\n'); snprintf(st.name, sizeof st.name, "%.*s", n2 ? (int)(n2 - nl - 1) : (int)strlen(nl + 1), nl + 1); }
  app_play(&st, d->type, d->id, d->vid, 0); return 1; }
void ph_dl_play(int i) { load(); if (i < 0 || i >= nd || D[i].state != DL_DONE) return; Dl *d = &D[i];
  Stream st; memset(&st, 0, sizeof st); snprintf(st.url, sizeof st.url, "file://%s", d->file); snprintf(st.name, sizeof st.name, "%s", T("Downloaded")); snprintf(st.title, sizeof st.title, "%s", d->ep[0] ? d->ep : d->name);
  app_play(&st, d->type, d->id, d->vid, 0); }
/* download as Play would play: the best fit at once when that is on, else the streams to choose from. Several
   episodes at once: each one's best fit in turn (a queue of choices, one at a time) */
/* Several at once (or one with the best fit on): each goes to Downloads at once, "finding a stream", the poster
   flying there once; their streams are then found one after the other on their own, each entry going on to
   download as its stream comes (or saying none suits) */
#define MAXQ 200
static Item qIt; static int qIdx[MAXQ], qN, qAt, qBusy;
static void q_next(void);
static void on_best_status(const char *t, void *ud) { (void)t; (void)ud; }
static void on_best_done(const Stream *s, const BestRate *r, void *ud) { (void)r; (void)ud; int i = qAt > 0 ? qIdx[qAt - 1] : -1; qBusy = 0;
  if (i >= 0 && i < nd && D[i].state == DL_CHOOSING) {
    if (!s || (s->p2p ? !p2p_available() : strncmp(s->url, "http", 4) != 0)) { D[i].state = DL_NOSTREAM; }
    else { int dup = -1; for (int j = 0; j < nd; j++) if (j != i && !strcmp(D[j].url, s->url)) dup = j; /* the same file as another: a pack, kept once */
      if (dup >= 0) { D[i].state = DL_NOSTREAM; snprintf(D[i].url, sizeof D[i].url, "%s", s->url); D[i].state = DL_INPACK; }
      else { snprintf(D[i].url, sizeof D[i].url, "%s", s->url); info_of(s, D[i].info, sizeof D[i].info); D[i].size = s->size > 0 ? (long long)s->size : 0; D[i].state = DL_QUEUED; } }
    save(); }
  q_next(); }
static void q_next(void) {
  if (qBusy) return; while (qAt < qN && (qIdx[qAt] >= nd || D[qIdx[qAt]].state != DL_CHOOSING)) qAt++;
  if (qAt >= qN) { qN = qAt = 0; return; }
  int i = qIdx[qAt++]; qBusy = 1; { double mb; int res, hdr; ph_dl_prefs(&mb, &res, &hdr); best_cap_next(mb); best_limits_next(res, hdr); best_for_download_next(); } /* the settings' profile (or their own choices): its size an hour, its picture */
  best_choose(D[i].type, D[i].vid, strcmp(D[i].type, "series") ? 100 : 45, on_best_status, on_best_done, NULL); }
static void q_push(int i) { if (qN < MAXQ) qIdx[qN++] = i; }
int ph_dl_choosing(int *done, int *total, int *finished) { if (done) *done = qAt; if (total) *total = qN; if (finished) *finished = 0; return qN > 0; }
void ph_download_group(const Item *it, const char *const *vids, const char *const *eps, int n, const char *group) {
  load(); if (n <= 0) return; int added = 0; char pb[600]; sb_poster_of(it, pb, sizeof pb);
  for (int k = 0; k < n; k++) { int i = find(it->id, vids[k]); if (i >= 0 && D[i].state != DL_NOSTREAM && D[i].state != DL_FAILED) continue; if (i < 0) { if (nd >= MAXD) break; i = nd++; }
    Dl *d = &D[i]; memset(d, 0, sizeof *d); snprintf(d->id, sizeof d->id, "%s", it->id); snprintf(d->type, sizeof d->type, "%s", it->type); snprintf(d->vid, sizeof d->vid, "%s", vids[k]);
    snprintf(d->name, sizeof d->name, "%s", it->name); snprintf(d->ep, sizeof d->ep, "%s", eps && eps[k] ? eps[k] : ""); snprintf(d->poster, sizeof d->poster, "%s", pb); snprintf(d->bg, sizeof d->bg, "%s", it->background);
    snprintf(d->group, sizeof d->group, "%s", group ? group : ""); d->state = DL_CHOOSING; NEWWAIT(d); q_push(i); added++; }
  if (!added) { toast(T("Already downloaded"), 1600, NULL); return; }
  save(); ph_fly(pb, 3); notify_now(); notify_tick(); qIt = *it; q_next(); ask_data(); { void pplat_ask_notifications(void); pplat_ask_notifications(); } }
/* a collection's films, each its own title: added together, each its best fit in turn, shown as one in Downloads (as a season) */
void ph_download_items(const Item *its, int n, const char *group) {
  load(); if (n <= 0) return; int added = 0; char pb[600] = "";
  for (int k = 0; k < n; k++) { const Item *it = &its[k]; int i = find(it->id, it->id); if (i >= 0 && D[i].state != DL_NOSTREAM && D[i].state != DL_FAILED) { if (group && *group && strcmp(D[i].group, group)) { snprintf(D[i].group, sizeof D[i].group, "%s", group); added++; } continue; } /* (one already here: it joins the group) */
    if (i < 0) { if (nd >= MAXD) break; i = nd++; }
    Dl *d = &D[i]; memset(d, 0, sizeof *d); snprintf(d->id, sizeof d->id, "%s", it->id); snprintf(d->type, sizeof d->type, "%s", it->type); snprintf(d->vid, sizeof d->vid, "%s", it->id);
    snprintf(d->name, sizeof d->name, "%s", it->name); char b[600]; snprintf(d->poster, sizeof d->poster, "%s", sb_poster_of(it, b, sizeof b)); if (!pb[0]) snprintf(pb, sizeof pb, "%s", d->poster); snprintf(d->bg, sizeof d->bg, "%s", it->background);
    snprintf(d->group, sizeof d->group, "%s", group ? group : ""); d->state = DL_CHOOSING; NEWWAIT(d); q_push(i); added++; }
  if (!added) { toast(T("Already downloaded"), 1600, NULL); return; }
  save(); if (pb[0]) ph_fly(pb, 3); notify_now(); notify_tick(); q_next(); ask_data(); { void pplat_ask_notifications(void); pplat_ask_notifications(); } }
void ph_download_many(const Item *it, const char *const *vids, const char *const *eps, int n) { ph_download_group(it, vids, eps, n, ""); }
void ph_dl_retry(int i) { load(); if (i < 0 || i >= nd) return; if (!D[i].url[0] || D[i].state == DL_NOSTREAM) { D[i].state = DL_CHOOSING; q_push(i); q_next(); save(); } else ph_dl_pause(i); }
void ph_download(const Item *it, const char *vid, const char *ep) {
  if (ph_dl_auto()) { const char *v[1] = { vid }, *e[1] = { ep }; ph_download_many(it, v, e, 1); return; } /* (its own setting, apart from playing's best stream) */
  ph_streams_download(it, vid, ep);
}
