/* The phone on Android: the platform calls (plat.h, mem.h) and the phone's own (pplat_*), over SDL's
   activity. Java copies the bundled assets to the app's files folder before the app starts (fonts and
   dictionaries are read as files); the built-in player runs in its own activity and reports back through
   nativeProgress. The TV's video interface (video.h) is a stand-in here: the phone plays in that activity. */
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <math.h>
#include <sys/system_properties.h>
#include <android/log.h>
#include "cJSON.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include "core/keys.h"
#include "core/video.h"
#include "core/applog.h"
#include <SDL2/SDL_timer.h>
#include "phone/phone.h"

#define JCLS "app/stremioorigin/phone/MainActivity"
static char assetDir[1024], dataDir[1024];

/* stderr to the system log (adb logcat -s StremioOrigin), line by line */
static int logPipe[2];
static void *log_thread(void *a) { (void)a; char buf[2048]; size_t n = 0; ssize_t r;
  while ((r = read(logPipe[0], buf + n, sizeof buf - 1 - n)) > 0) { n += (size_t)r; buf[n] = 0; char *s = buf, *e;
    while ((e = strchr(s, '\n'))) { *e = 0; __android_log_write(ANDROID_LOG_INFO, "StremioOrigin", s); s = e + 1; }
    n = strlen(s); memmove(buf, s, n + 1); if (n >= sizeof buf - 1) { __android_log_write(ANDROID_LOG_INFO, "StremioOrigin", buf); n = 0; } }
  return NULL; }

static JNIEnv *env(void) { return (JNIEnv *)SDL_AndroidGetJNIEnv(); }
static jclass cls(JNIEnv *e) { static jclass c; if (!c) { jobject act = (jobject)SDL_AndroidGetActivity(); jclass k = (*e)->GetObjectClass(e, act); c = (*e)->NewGlobalRef(e, k); (*e)->DeleteLocalRef(e, k); (*e)->DeleteLocalRef(e, act); } return c; }
/* a Java method by name, found once and kept (a lookup by name each call was a few percent of every frame) */
static jmethodID sm(JNIEnv *e, const char *name, const char *sig) {
  static struct { const char *n, *s; jmethodID m; } K[96]; static int nk; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mu); for (int i = 0; i < nk; i++) if ((K[i].n == name || !strcmp(K[i].n, name)) && (K[i].s == sig || !strcmp(K[i].s, sig))) { jmethodID m = K[i].m; pthread_mutex_unlock(&mu); return m; } pthread_mutex_unlock(&mu);
  jmethodID m = (*e)->GetStaticMethodID(e, cls(e), name, sig); if (!m) { (*e)->ExceptionClear(e); fprintf(stderr, "[jni] no %s\n", name); return m; }
  pthread_mutex_lock(&mu); if (nk < 96) { K[nk].n = name; K[nk].s = sig; K[nk].m = m; nk++; } pthread_mutex_unlock(&mu); return m; }
static jstring js(JNIEnv *e, const char *s) { return (*e)->NewStringUTF(e, s ? s : ""); }
static void jstr_copy(JNIEnv *e, jstring s, char *out, size_t cap) { out[0] = 0; if (!s) return; const char *c = (*e)->GetStringUTFChars(e, s, NULL); if (c) { snprintf(out, cap, "%s", c); (*e)->ReleaseStringUTFChars(e, s, c); } (*e)->DeleteLocalRef(e, s); }

void plat_pre_init(const char *appId) { (void)appId;
  setvbuf(stderr, NULL, _IOLBF, 0); if (pipe(logPipe) == 0) { dup2(logPipe[1], 2); pthread_t th; if (!pthread_create(&th, NULL, log_thread, NULL)) pthread_detach(th); }
  const char *d = SDL_AndroidGetInternalStoragePath(); snprintf(dataDir, sizeof dataDir, "%s", d ? d : "/data/local/tmp");
  snprintf(assetDir, sizeof assetDir, "%s/assets", dataDir);
  applog_start(dataDir);
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown"); /* every way round: the pages lay out for the shape they get; films turn sideways in the player */
}
void plat_post_window(SDL_Window *w) { (void)w; }
const char *plat_asset_dir(void) { return assetDir; }
const char *plat_data_dir(void) { return dataDir; }
void plat_window_size(int *w, int *h, Uint32 *flags) { SDL_DisplayMode m; if (SDL_GetCurrentDisplayMode(0, &m) == 0) { *w = m.w; *h = m.h; } else { *w = 1080; *h = 2340; } *flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI; } /* not fullscreen: the status bar and the gesture bar stay, the page is drawn under them (the insets keep it clear) */
int plat_map_key(const SDL_KeyboardEvent *e) { switch (e->keysym.sym) { case SDLK_AC_BACK: return K_BACK; case SDLK_RETURN: return K_OK; default: return K_NONE; } }
int plat_text_input(void) { return 1; }
const char *plat_device_name(void) { static char n[160]; if (!n[0]) { char model[PROP_VALUE_MAX] = "", rel[PROP_VALUE_MAX] = "", man[PROP_VALUE_MAX] = "";
    __system_property_get("ro.product.model", model); __system_property_get("ro.build.version.release", rel); __system_property_get("ro.product.manufacturer", man);
    (void)model; snprintf(n, sizeof n, "Android %s (API %d) %s", rel, SDL_GetAndroidSDKVersion(), man); } return n; } /* (the maker, for its own quirks; never the model: the logs name no device) */
static int meminfo(const char *key) { FILE *f = fopen("/proc/meminfo", "r"); if (!f) return -1; char l[160]; int v = -1; size_t kl = strlen(key);
  while (fgets(l, sizeof l, f)) if (!strncmp(l, key, kl)) { v = atoi(l + kl) / 1024; break; } fclose(f); return v; }
int plat_mem_total_mb(void) { if (getenv("SB_MEMMB")) return atoi(getenv("SB_MEMMB")); int v = meminfo("MemTotal:"); return v > 0 ? v : 0; }
int plat_mem_free_mb(void) { return meminfo("MemAvailable:"); }
void plat_thread_background(int nice) { (void)nice; }
int plat_lifecycle(const SDL_Event *e) { if (e->type == SDL_APP_DIDENTERFOREGROUND) return 1; if (e->type == SDL_APP_DIDENTERBACKGROUND) return -1; return 0; }
const char *plat_ui_locale(void) { static char loc[32]; if (!loc[0]) { JNIEnv *e = env(); jmethodID m = sm(e, "locale", "()Ljava/lang/String;"); if (m) jstr_copy(e, (*e)->CallStaticObjectMethod(e, cls(e), m), loc, sizeof loc); } return loc[0] ? loc : NULL; }
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) { (void)uri; (void)payload; (void)timeoutMs; return NULL; }
/* The one system job the core starts through this call on a phone: the in-app update (core/update.c). Its request
   (the package's address and SHA-256) goes to Java (download, check, the system's package installer), and Java's
   answers come back as webOS's would, taken once a frame: {"statusText","progress"}, {"returnValue":false,"errorText"},
   {"finished":true} */
static struct { unsigned long id; PlatLunaCb cb; void *ud; } updJob;
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) {
  if (!uri || !strstr(uri, "/install")) return 0;
  cJSON *j = cJSON_Parse(payload); const cJSON *u = cJSON_GetObjectItemCaseSensitive(j, "ipkUrl"), *h = cJSON_GetObjectItemCaseSensitive(j, "ipkHash"); unsigned long id = 0;
  if (cJSON_IsString(u) && cJSON_IsString(h)) { JNIEnv *e = env(); jmethodID m = sm(e, "updateStart", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (m) { jstring a = js(e, u->valuestring), b = js(e, h->valuestring); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b);
      if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); else { static unsigned long next = 1; id = next++; updJob.id = id; updJob.cb = cb; updJob.ud = ud; }
      (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); } }
  cJSON_Delete(j); return id;
}
void plat_luna_cancel(unsigned long handle) { if (handle && handle == updJob.id) { updJob.id = 0; JNIEnv *e = env(); jmethodID m = sm(e, "updateCancel", "(I)V"); if (m) { (*e)->CallStaticVoidMethod(e, cls(e), m, (jint)0); if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); } } }
void plat_luna_pump(void) {
  if (!updJob.id) return; JNIEnv *e = env(); static jmethodID take; if (!take) take = sm(e, "updTake", "()Ljava/lang/String;"); if (!take) return;
  for (int i = 0; i < 4 && updJob.id; i++) { char b[2048]; jstr_copy(e, (jstring)(*e)->CallStaticObjectMethod(e, cls(e), take), b, sizeof b); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); break; } if (!b[0]) break; if (updJob.cb) updJob.cb(b, updJob.ud); }
}
int plat_luna_send(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
int plat_panel_lines(void) { SDL_DisplayMode m; if (SDL_GetCurrentDisplayMode(0, &m) == 0) return m.w < m.h ? m.w : m.h; return 0; } /* the screen's short side, in pixels */

/* ---- the phone's part ---- */
float pplat_density(void) { JNIEnv *e = env(); jmethodID m = sm(e, "density", "()F"); return m ? (*e)->CallStaticFloatMethod(e, cls(e), m) : 0; }
static jint ins[5];
static void ins_read(void) { JNIEnv *e = env(); static jmethodID m; if (!m) m = sm(e, "insets", "()[I"); if (!m) return;
  jintArray a = (*e)->CallStaticObjectMethod(e, cls(e), m); if (!a) return; int n = (*e)->GetArrayLength(e, a); (*e)->GetIntArrayRegion(e, a, 0, n < 5 ? n : 5, ins); (*e)->DeleteLocalRef(e, a); }
void pplat_insets(float *top, float *bottom) { ins_read(); *top = ins[0]; *bottom = ins[1]; }
float pplat_keyboard(void) { ins_read(); return ins[4]; }
int pplat_open_player(const char *url, const char *title, const char *headers, double startSec, const char *key) {
  JNIEnv *e = env(); jmethodID m = sm(e, "openPlayer", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JLjava/lang/String;)V"); if (!m) return 0;
  jstring a = js(e, url), b = js(e, title), c = js(e, headers), d = js(e, key); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b, c, (jlong)(startSec * 1000), d);
  (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); (*e)->DeleteLocalRef(e, c); (*e)->DeleteLocalRef(e, d); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } return 1; }
int pplat_open_external(const char *url, const char *title) {
  JNIEnv *e = env(); jmethodID m = sm(e, "openExternal", "(Ljava/lang/String;Ljava/lang/String;)Z"); if (!m) return 0;
  jstring a = js(e, url), b = js(e, title); jboolean ok = (*e)->CallStaticBooleanMethod(e, cls(e), m, a, b); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b);
  if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } return ok; }
int pplat_open_external_with(const char *url, const char *title, const char *pkg) {
  JNIEnv *e = env(); jmethodID m = sm(e, "openExternalWith", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z"); if (!m) return 0;
  jstring a = js(e, url), b = js(e, title), c = js(e, pkg ? pkg : ""); jboolean ok = (*e)->CallStaticBooleanMethod(e, cls(e), m, a, b, c); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); (*e)->DeleteLocalRef(e, c);
  if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } return ok; }
int pplat_video_players(char *out, int cap) { out[0] = 0;
  JNIEnv *e = env(); jmethodID m = sm(e, "videoPlayers", "()Ljava/lang/String;"); if (!m) return 0;
  jstring r = (jstring)(*e)->CallStaticObjectMethod(e, cls(e), m); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } if (!r) return 0;
  const char *s = (*e)->GetStringUTFChars(e, r, NULL); snprintf(out, cap, "%s", s ? s : ""); if (s) (*e)->ReleaseStringUTFChars(e, r, s); (*e)->DeleteLocalRef(e, r); return out[0] != 0; }
/* playing on a TV: its media notification and lock-screen controls (title NULL: gone); what is pressed there */
void pplat_cast_notify(const char *title, const char *sub, const char *art, int playing, double pos, double dur) { JNIEnv *e = env(); jmethodID m = sm(e, "castNotify", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZJJ)V"); if (!m) return;
  jstring a = title ? js(e, title) : NULL, b = js(e, sub ? sub : ""), c = js(e, art ? art : ""); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b, c, (jboolean)(playing != 0), (jlong)(pos * 1000), (jlong)(dur * 1000));
  if (a) (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); (*e)->DeleteLocalRef(e, c); if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); }
int pplat_cast_take(int *kind, double *val) { JNIEnv *e = env(); jmethodID m = sm(e, "castTake", "()[J"); if (!m) return 0; jlongArray r = (jlongArray)(*e)->CallStaticObjectMethod(e, cls(e), m); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } if (!r) return 0;
  jlong v[2] = { 0, 0 }; (*e)->GetLongArrayRegion(e, r, 0, 2, v); (*e)->DeleteLocalRef(e, r); *kind = (int)v[0]; *val = v[1] / 1000.0; return 1; }
int pplat_open_url(const char *url) {
  JNIEnv *e = env(); jmethodID m = sm(e, "openUrl", "(Ljava/lang/String;)Z"); if (!m) return 0;
  jstring a = js(e, url); jboolean ok = (*e)->CallStaticBooleanMethod(e, cls(e), m, a); (*e)->DeleteLocalRef(e, a); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return 0; } return ok; }
void pplat_multicast(int on) { JNIEnv *e = env(); jmethodID m = sm(e, "multicast", "(Z)V"); if (m) { (*e)->CallStaticVoidMethod(e, cls(e), m, (jboolean)(on != 0)); if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); } }

/* the player's reports: from Java's thread, read by the frame loop */
#define NQ 16
static struct { char key[300]; double pos, dur; int state; } Q[NQ]; static int qh, qt; static pthread_mutex_t qmu = PTHREAD_MUTEX_INITIALIZER;
/* a line for the app's log from the Java side (its freeze watchdog): through stderr, as every native line */
JNIEXPORT void JNICALL Java_app_stremioorigin_phone_MainActivity_nativeLog(JNIEnv *e, jclass c, jstring line) { (void)c; char b[8192]; jstr_copy(e, line, b, sizeof b); (void)line; if (b[0]) fprintf(stderr, "%s\n", b); }
JNIEXPORT void JNICALL Java_app_stremioorigin_phone_MainActivity_nativeProgress(JNIEnv *e, jclass c, jstring key, jlong pos, jlong dur, jint state) { (void)c;
  char k[300]; const char *s = (*e)->GetStringUTFChars(e, key, NULL); snprintf(k, sizeof k, "%s", s ? s : ""); if (s) (*e)->ReleaseStringUTFChars(e, key, s);
  pthread_mutex_lock(&qmu); int n = (qt + 1) % NQ; if (n != qh) { snprintf(Q[qt].key, sizeof Q[qt].key, "%s", k); Q[qt].pos = pos / 1000.0; Q[qt].dur = dur / 1000.0; Q[qt].state = state; qt = n; } pthread_mutex_unlock(&qmu); }
int pplat_progress(char *key, int cap, double *pos, double *dur, int *state) {
  pthread_mutex_lock(&qmu); int got = qh != qt; if (got) { snprintf(key, cap, "%s", Q[qh].key); *pos = Q[qh].pos; *dur = Q[qh].dur; *state = Q[qh].state; qh = (qh + 1) % NQ; } pthread_mutex_unlock(&qmu); return got; }

/* ---- video.h: the video plane (Media3 on a SurfaceView under the app's transparent drawing, VideoPlane.java); the
   TV player's own controls draw over it. Its state is read once a frame (video_pump). ---- */
static jmethodID vm(const char *name, const char *sig) { return sm(env(), name, sig); }

static struct { long long st, pos, dur, buf, w, h, live; int stuck; } V; static char vErr[96]; static int surfW = 1, surfH = 1, vHasMedia; static double seekTo = -1; static Uint32 seekAt;
static void vcall(const char *name) { JNIEnv *e = env(); jmethodID m = vm(name, "()V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m); }
int video_init(const char *appId) { (void)appId; return 1; }
void video_load(const char *url) { fprintf(stderr, "[video] load %.160s\n", url); JNIEnv *e = env(); jmethodID m = vm("vLoad", "(Ljava/lang/String;Ljava/lang/String;J)V"); if (!m) return;
  const char *h = ""; /* a stream's own headers go through the app's local proxy (core/hproxy.c), as on the TV */
  jstring a = js(e, url), b = js(e, h); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b, (jlong)0); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b);
  memset(&V, 0, sizeof V); V.st = 1; vErr[0] = 0; vHasMedia = 1; seekTo = -1; }
void video_load_timed(const char *base, double durSec) { video_load(base); (void)durSec; }
void video_set_live(int on) { JNIEnv *e = env(); jmethodID m = vm("vSetLive", "(Z)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jboolean)(on != 0)); } /* a live broadcast: a stall reopens it at the live edge */
void video_live_rejoin(void) { vcall("vLiveRejoin"); } /* a live broadcast after a pause: where it is now */
void plat_pointer_visible(int on) { (void)on; } /* (a TV's pointer: none on a phone) */
void video_unload(void) { vcall("vUnload"); memset(&V, 0, sizeof V); vHasMedia = 0; }
void video_play(void) { vcall("vPlay"); }
void video_pause(void) { vcall("vPause"); }
void video_seek(double sec) { JNIEnv *e = env(); jmethodID m = vm("vSeek", "(J)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jlong)(sec * 1000)); seekTo = sec; seekAt = SDL_GetTicks(); V.pos = (long long)(sec * 1000); }
double video_time(void) { return V.pos / 1000.0; }
double video_duration(void) { return V.dur / 1000.0; }
double video_buffered_end(void) { return V.buf / 1000.0; }
VidState video_state(void) { switch (V.st) { case 1: return VID_LOADING; case 2: return V.stuck ? VID_BUFFERING : VID_PLAYING; case 3: return VID_PAUSED; case 4: return VID_BUFFERING; case 5: return VID_ENDED; case 6: return VID_ERROR; default: return VID_IDLE; } }
const char *video_error(void) { return vErr; }
int video_live(void) { return V.live != 0; }
int video_settling(void) { return seekTo >= 0; }
int video_has_media(void) { return vHasMedia; }
void video_set_volume(int pct, int easeMs) { (void)easeMs; JNIEnv *e = env(); jmethodID m = vm("vVolume", "(F)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jfloat)(pct / 100.0f)); }
int video_width(void) { return (int)V.w; }
int video_height(void) { return (int)V.h; }
double video_fps(void) { return 0; }
/* the shared core's questions to the video layer (core/video.h): libVLC lets go of a dropped load at once, and its
   plane is moved with the frame (a preview grows by the phone's own move, the player takes it as it is) */
double video_decoder_busy_left(void) { return 0; }
int video_load_completed(void) { return V.st >= 2 || V.live != 0; } /* (playing, paused, buffering after its start: it has prepared) */
VidPlaneCaps video_plane_caps(void) { VidPlaneCaps c = { 1, 0, 0, 0 }; return c; }
/* libVLC's tracks: "id|name|selected"; its names read like "Track 2 - [English]" or "English - DTS - [eng]" */
static int tracks(int type, VidTrack *out, int max, int *cur) {
  JNIEnv *e = env(); jmethodID m = vm("vTracks", "(I)[Ljava/lang/String;"); if (!m) return 0; jobjectArray a = (*e)->CallStaticObjectMethod(e, cls(e), m, (jint)type); if (!a) return 0;
  int n = (*e)->GetArrayLength(e, a), k = 0; if (cur) *cur = -1;
  for (int i = 0; i < n && k < max; i++) { jstring js1 = (*e)->GetObjectArrayElement(e, a, i); char line[256]; jstr_copy(e, js1, line, sizeof line);
    char *p1 = strchr(line, '|'), *p2 = p1 ? strchr(p1 + 1, '|') : NULL; if (!p1 || !p2) continue; *p1 = 0; *p2 = 0;
    VidTrack *t = &out[k]; memset(t, 0, sizeof *t); t->idx = atoi(line); snprintf(t->name, sizeof t->name, "%s", p1 + 1);
    const char *lb = strrchr(p1 + 1, '['), *rb = lb ? strchr(lb, ']') : NULL; if (lb && rb && rb - lb - 1 < (int)sizeof t->lang) snprintf(t->lang, sizeof t->lang, "%.*s", (int)(rb - lb - 1), lb + 1);
    char low[96]; snprintf(low, sizeof low, "%s", p1 + 1); for (char *c = low; *c; c++) *c = (char)tolower((unsigned char)*c);
    snprintf(t->codec, sizeof t->codec, "%s", strstr(low, "truehd") ? "truehd" : strstr(low, "dts") ? "dts" : strstr(low, "e-ac3") || strstr(low, "eac3") ? "eac3" : strstr(low, "ac3") || strstr(low, "a52") ? "ac3" : strstr(low, "aac") ? "aac" : "");
    t->channels = strstr(low, "7.1") ? 8 : strstr(low, "5.1") ? 6 : strstr(low, "stereo") ? 2 : 0;
    if (atoi(p2 + 1) && cur) *cur = t->idx; k++; }
  (*e)->DeleteLocalRef(e, a); return k; }
static int audioCur;
int video_audio_tracks(VidTrack *out, int max) { int c; int n = tracks(1, out, max, &c); if (c >= 0) audioCur = c; return n; }
int video_audio_current(void) { return audioCur; }
void video_select_audio(int idx) { JNIEnv *e = env(); jmethodID m = vm("vSelect", "(II)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, 1, (jint)idx); audioCur = idx; }
int video_text_tracks(VidTrack *out, int max) { return tracks(2, out, max, NULL); }
void video_select_text(int idx) { JNIEnv *e = env(); jmethodID m = vm("vSelect", "(II)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, 2, (jint)idx); }
void video_set_surface(int w, int h) { surfW = w > 0 ? w : 1; surfH = h > 0 ? h : 1; }
float ph_vid_rect[4]; /* where the film's view is (layout units): the player's glass places its copies there */
void video_set_window(float x, float y, float w, float h) { ph_vid_rect[0] = x; ph_vid_rect[1] = y; ph_vid_rect[2] = w; ph_vid_rect[3] = h;
  extern float sb_layout_w, sb_layout_h; float kx = surfW / sb_layout_w, ky = surfH / sb_layout_h;
  { static char last[96]; char t[96]; snprintf(t, sizeof t, "%.0f,%.0f %.0fx%.0f of %.0fx%.0f, surface %dx%d", x, y, w, h, sb_layout_w, sb_layout_h, surfW, surfH); if (strcmp(t, last)) { snprintf(last, sizeof last, "%s", t); fprintf(stderr, "[video] window %s\n", t); } }
  JNIEnv *e = env(); jmethodID m = vm("vWindow", "(IIII)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jint)(x * kx), (jint)(y * ky), (jint)(w * kx), (jint)(h * ky)); }
/* Fill and Zoom: libVLC scales the picture (video pixels to screen pixels) so the chosen part covers the screen */
int video_set_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) { (void)sx; (void)sy; (void)sw; (void)sh; (void)dx; (void)dy; (void)dw; (void)dh; return 1; } /* (the size is libVLC's: see video_size_mode) */
/* a small copy of the picture (the panels' glass): asked for, then read when ready into rgba (w*h*4); 1 when there was one */
void video_snap_ask(int w, int h) { JNIEnv *e = env(); jmethodID m = vm("vSnap", "(II)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jint)w, (jint)h); }
int video_snap_take(unsigned char *rgba, int cap, int *w, int *h) { JNIEnv *e = env(); jmethodID m = vm("vSnapTake", "()[I"); if (!m) return 0;
  jintArray a = (jintArray)(*e)->CallStaticObjectMethod(e, cls(e), m); if (!a) return 0; int ok = 0; jsize n = (*e)->GetArrayLength(e, a);
  if (n > 2) { jint *p = (*e)->GetIntArrayElements(e, a, NULL); int W = p[0], H = p[1]; if (W > 0 && H > 0 && (jsize)(2 + W * H) <= n && W * H * 4 <= cap) { for (int i = 0; i < W * H; i++) { unsigned v = (unsigned)p[2 + i]; rgba[i * 4] = (v >> 16) & 255; rgba[i * 4 + 1] = (v >> 8) & 255; rgba[i * 4 + 2] = v & 255; rgba[i * 4 + 3] = 255; } *w = W; *h = H; ok = 1; } (*e)->ReleaseIntArrayElements(e, a, p, JNI_ABORT); }
  (*e)->DeleteLocalRef(e, a); return ok; }
void video_drag(float tx, float ty, float k) { JNIEnv *e = env(); jmethodID m = vm("vDrag", "(FFF)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jfloat)tx, (jfloat)ty, (jfloat)k); }
void video_pinch(float k) { JNIEnv *e = env(); jmethodID m = vm("vPinch", "(F)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jfloat)k); }
void video_pinch_end(float k, char *out, size_t cap) { JNIEnv *e = env(); jmethodID m = vm("vPinchEnd", "(F)Ljava/lang/String;"); out[0] = 0; if (m) jstr_copy(e, (jstring)(*e)->CallStaticObjectMethod(e, cls(e), m, (jfloat)k), out, cap); }
void video_size_mode(const char *mode) { JNIEnv *e = env(); jmethodID m = vm("vAspect", "(Ljava/lang/String;F)V"); if (!m) return; jstring a = js(e, mode); (*e)->CallStaticVoidMethod(e, cls(e), m, a, (jfloat)0); (*e)->DeleteLocalRef(e, a); }
int video_audio_unsupported(void) { return 0; }
static int vPic; int video_has_picture(void) { return vPic; } /* (a video output exists, or its size is known: a stream with sound alone has none) */
int video_partial_plane(void) { return 1; }
void video_pump(void) {
  JNIEnv *e = env(); static jmethodID m; if (!m) m = vm("vPoll", "()[J"); if (!m || !vHasMedia) return;
  jlongArray a = (*e)->CallStaticObjectMethod(e, cls(e), m); if (!a) return; jlong v[9] = { 0 }; jsize an = (*e)->GetArrayLength(e, a); (*e)->GetLongArrayRegion(e, a, 0, an < 9 ? an : 9, v); (*e)->DeleteLocalRef(e, a);
  V.st = v[0]; V.dur = v[2]; V.w = v[4]; V.h = v[5]; V.live = v[6]; vPic = (int)v[8];
  /* the clock: libVLC tells the time a few times a second, a little unevenly; the player sees a smooth clock that follows
     it and never steps back while playing (a seek sets it). A real wait: libVLC's own time not moving for 0.9 s. */
  static long long raw = -1; static Uint32 rawAt, lastTick; static double clk; Uint32 nowT = SDL_GetTicks(); double fdt = lastTick ? (nowT - lastTick) / 1000.0 : 0; lastTick = nowT;
  if (v[1] != raw) { raw = v[1]; rawAt = nowT; }
  int full = v[3] >= 100; /* libVLC's cache full: whatever its clock says, nothing waits (some files' clocks move in jumps of seconds) */
  int stuck = V.st == 2 && V.live && nowT - rawAt > 900 && !full; V.stuck = stuck;
  if (seekTo >= 0) { clk = seekTo; if (llabs(raw - (long long)(seekTo * 1000)) < 1500 && nowT - seekAt > 150) seekTo = -1; else if (nowT - seekAt > 4000) { seekTo = -1; clk = raw / 1000.0; } }
  else if (V.st == 2 && !stuck) { Uint32 cap = full ? 8000 : 1000; double target = (raw + (long long)(nowT - rawAt < cap ? nowT - rawAt : cap)) / 1000.0, err = target - clk; /* (on, smoothly, through a jumpy clock) */
    if (fabs(err) > 0.8) clk = target; else { double n = clk + fdt + err * 0.08; if (n > clk) clk = n; } }
  else clk = raw / 1000.0;
  V.pos = (long long)(clk * 1000);
  V.buf = V.pos + (v[3] >= 100 ? 30000 : (long long)v[3] * 300);
  if (V.st == 6 && !vErr[0]) { jmethodID me = vm("vError", "()Ljava/lang/String;"); if (me) jstr_copy(e, (*e)->CallStaticObjectMethod(e, cls(e), me), vErr, sizeof vErr); }
}
int video_edge_colors(uint32_t *out4) { (void)out4; return 0; } /* (the picture is on its own surface under the app: the channel's colours light its box) */
int video_hdr(char *out, int cap) { if (cap > 0) out[0] = 0; return 0; } /* Media3 picks the HDR path itself */
/* this process: resident and swapped out, from /proc/self/status */
void plat_mem_self(int *rssMb, int *swapMb) { *rssMb = *swapMb = -1; FILE *f = fopen("/proc/self/status", "r"); if (!f) return; char l[160];
  while (fgets(l, sizeof l, f)) { if (!strncmp(l, "VmRSS:", 6)) *rssMb = atoi(l + 6) / 1024; else if (!strncmp(l, "VmSwap:", 7)) *swapMb = atoi(l + 7) / 1024; } fclose(f); }

float pplat_brightness(float set) { JNIEnv *e = env(); jmethodID m = vm("brightness", "(F)F"); return m ? (*e)->CallStaticFloatMethod(e, cls(e), m, (jfloat)set) : 0.5f; }
float pplat_volume(float set) { JNIEnv *e = env(); jmethodID m = vm("volume", "(F)F"); return m ? (*e)->CallStaticFloatMethod(e, cls(e), m, (jfloat)set) : 0.5f; }
/* picture in picture: whether the app is in it, and the subtitle words shown over it (sent only when they change) */
int pplat_pip(void) { JNIEnv *e = env(); jmethodID m = vm("vPip", "()Z"); return m ? (*e)->CallStaticBooleanMethod(e, cls(e), m) : 0; }
void pplat_pip_sub(const char *t) { static char was[1200]; if (!t) { was[0] = 1; was[1] = 0; return; } /* (NULL: forget what was sent: the window's text was cleared there) */ if (!strcmp(was, t)) return; snprintf(was, sizeof was, "%s", t); { static int nLog; if (nLog < 40) { nLog++; fprintf(stderr, "[pip] subtitle: \"%.30s\"\n", t); } } /* (the first ones: proof in the log that they move) */
  JNIEnv *e = env(); jmethodID m = vm("vPipSubB", "([B)V"); if (!m) return; jsize n = (jsize)strlen(t); jbyteArray b = (*e)->NewByteArray(e, n); if (!b) return; (*e)->SetByteArrayRegion(e, b, 0, n, (const jbyte *)t); (*e)->CallStaticVoidMethod(e, cls(e), m, b); (*e)->DeleteLocalRef(e, b); } /* (bytes, read as UTF-8 there: any text a file holds, never a JNI abort on a malformed one) */
void pplat_haptic(void) { JNIEnv *e = env(); jmethodID m = vm("haptic", "()V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m); }
void pplat_rate(float r) { JNIEnv *e = env(); jmethodID m = vm("vRate", "(F)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jfloat)r); }
static int fullOn; int pplat_fullscreen_on(void) { return fullOn; }
int pplat_held(void) { JNIEnv *e = env(); static jmethodID m; if (!m) m = vm("vHeld", "()I"); return m ? (int)(*e)->CallStaticIntMethod(e, cls(e), m) : -1; } /* 1 upright, 0 sideways, -1 not known */
void pplat_fullscreen(int on) { fullOn = on != 0; JNIEnv *e = env(); jmethodID m = vm("vFull", "(Z)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jboolean)(on != 0)); }

/* downloads: the network now, and where files can go ("kind\tpath\tfree bytes" lines) */
int pplat_on_wifi(void) { JNIEnv *e = env(); jmethodID m = sm(e, "onWifi", "()Z"); return m ? (*e)->CallStaticBooleanMethod(e, cls(e), m) : 1; }
void pplat_dl_dirs(char *out, size_t cap) { JNIEnv *e = env(); jmethodID m = sm(e, "dlDirs", "()Ljava/lang/String;"); out[0] = 0; if (m) jstr_copy(e, (*e)->CallStaticObjectMethod(e, cls(e), m), out, cap); }
int pplat_media_caps(void) { static int c = -1; if (c < 0) { JNIEnv *e = env(); jmethodID m = sm(e, "mediaCaps", "()I"); c = m ? (*e)->CallStaticIntMethod(e, cls(e), m) : 1 | 32 | 128; } return c; } /* (asked once) */
void pplat_pick_folder(void) { JNIEnv *e = env(); jmethodID m = sm(e, "pickFolder", "()V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m); }
double app_now(void);
int pplat_folder_take(char *out, size_t cap) { static double at; if (app_now() - at < 0.25) return 0; at = app_now(); JNIEnv *e = env(); jmethodID m = sm(e, "folderTake", "()Ljava/lang/String;"); char b[700] = ""; if (m) jstr_copy(e, (*e)->CallStaticObjectMethod(e, cls(e), m), b, sizeof b);
  if (!b[0]) return 0; if (!strcmp(b, "!")) return -1; snprintf(out, cap, "%s", b); return 1; } /* (asked a few times a second while the page shows) */
void pplat_app_ended(void) { JNIEnv *e = env(); jmethodID m = sm(e, "appEnded", "()V"); if (m) { (*e)->CallStaticVoidMethod(e, cls(e), m); if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); } }
void pplat_haptic_k(int kind, float s) { JNIEnv *e = env(); jmethodID m = vm("haptic2", "(IF)V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m, (jint)kind, (jfloat)s); }
void pplat_tilt(float *x, float *y) { JNIEnv *e = env(); jmethodID m = sm(e, "tilt", "(I)F"); *x = m ? (*e)->CallStaticFloatMethod(e, cls(e), m, 0) : 0; *y = m ? (*e)->CallStaticFloatMethod(e, cls(e), m, 1) : 0; }
void pplat_first_frame(void) { JNIEnv *e = env(); jmethodID m = vm("firstFrame", "()V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m); }
/* downloads' notification (DownloadService) */
void pplat_dl_notify2(const char *title, const char *text, int pct, const char *sub, const char *lines, const char *art) { JNIEnv *e = env(); jmethodID m = vm("dlNotify", "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"); if (!m) return;
  jstring a = title ? js(e, title) : NULL, b = text ? js(e, text) : NULL, c = js(e, sub), d = js(e, lines), f = js(e, art); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b, (jint)pct, c, d, f);
  if (a) (*e)->DeleteLocalRef(e, a); if (b) (*e)->DeleteLocalRef(e, b); (*e)->DeleteLocalRef(e, c); (*e)->DeleteLocalRef(e, d); (*e)->DeleteLocalRef(e, f); }
void pplat_dl_notify(const char *title, const char *text, int pct) { pplat_dl_notify2(title, text, pct, "", "", ""); }
void pplat_dl_finished(const char *title, const char *text) { void pplat_dl_finished2(const char *t, const char *x, const char *art); pplat_dl_finished2(title, text, ""); }
void pplat_dl_finished2(const char *title, const char *text, const char *art) { JNIEnv *e = env(); jmethodID m = vm("dlFinished", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"); if (!m) return; jstring a = js(e, title), b = js(e, text), c = js(e, art); (*e)->CallStaticVoidMethod(e, cls(e), m, a, b, c); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); (*e)->DeleteLocalRef(e, c); }
void pplat_ask_notifications(void) { static int asked; if (asked) return; asked = 1; vcall("askNotifications"); }
int pplat_take_open_downloads(void) { JNIEnv *e = env(); jmethodID m = vm("takeOpenDownloads", "()Z"); return m ? (*e)->CallStaticBooleanMethod(e, cls(e), m) : 0; }

/* an Android TV's own remote (Google's): open our app on it, pair once with the code it shows (AtvRemote.java). They
   block: the cast thread calls them */
int pplat_atv_open(const char *ip, const char *link) { JNIEnv *e = env(); jmethodID m = sm(e, "atvOpen", "(Ljava/lang/String;Ljava/lang/String;)I"); if (!m) return -1; jstring a = js(e, ip), b = js(e, link); int r = (*e)->CallStaticIntMethod(e, cls(e), m, a, b); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return -1; } return r; }
int pplat_atv_pair_start(const char *ip, const char *name) { JNIEnv *e = env(); jmethodID m = sm(e, "atvPairStart", "(Ljava/lang/String;Ljava/lang/String;)I"); if (!m) return -1; jstring a = js(e, ip), b = js(e, name); int r = (*e)->CallStaticIntMethod(e, cls(e), m, a, b); (*e)->DeleteLocalRef(e, a); (*e)->DeleteLocalRef(e, b); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return -1; } return r; }
int pplat_atv_pair_finish(const char *code) { JNIEnv *e = env(); jmethodID m = sm(e, "atvPairFinish", "(Ljava/lang/String;)I"); if (!m) return -1; jstring a = js(e, code); int r = (*e)->CallStaticIntMethod(e, cls(e), m, a); (*e)->DeleteLocalRef(e, a); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return -1; } return r; }
void pplat_atv_pair_close(void) { JNIEnv *e = env(); jmethodID m = sm(e, "atvPairClose", "()V"); if (m) (*e)->CallStaticVoidMethod(e, cls(e), m); }
