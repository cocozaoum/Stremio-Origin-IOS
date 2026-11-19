/* Development only: the phone's seek-preview decoder measured on the device, without the app.
   adb push libsbprev.so prevbench <frames...> /data/local/tmp; adb shell 'cd /data/local/tmp && LD_LIBRARY_PATH=. ./prevbench 256 h264 a.bin b.bin ...'
   Each file is one keyframe with its parameter sets (start codes). Per frame: decode thread CPU ms, wall ms; then the
   resident memory with the decoder open and after it is closed. Width argument: the picture's width (height 16:9). */
#include "core/sbdts.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static double now(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static long rss_kb(void) { FILE *f = fopen("/proc/self/statm", "r"); long a = 0, r = 0; if (f) { if (fscanf(f, "%ld %ld", &a, &r) != 2) r = 0; fclose(f); } return r * (sysconf(_SC_PAGESIZE) / 1024); }
int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "prevbench <width> <h264|hevc> <frame files...>\n"); return 2; }
  int W = atoi(argv[1]), H = W * 9 / 16; const char *codec = argv[2];
  long rss0 = rss_kb(); void *lib = dlopen("libsbprev.so", RTLD_NOW); if (!lib) { fprintf(stderr, "no library: %s\n", dlerror()); return 1; }
  const sbd_prev_api *(*get)(void) = (const sbd_prev_api *(*)(void))dlsym(lib, "sbd_prev_get"); const sbd_prev_api *a = get ? get() : NULL; if (!a) return 1;
  long rssLib = rss_kb(); double t0 = now(CLOCK_MONOTONIC); void *d = a->open(codec, NULL, 0); double tOpen = now(CLOCK_MONOTONIC) - t0; if (!d) { fprintf(stderr, "open failed\n"); return 1; }
  unsigned char *rgb = malloc((size_t)W * H * 3); long rssPeak = 0; double cpuSum = 0, wallSum = 0; int n = 0;
  for (int pass = 0; pass < 2; pass++) for (int i = 3; i < argc; i++) { /* (twice: the first pass warms the caches, the second is reported) */
    FILE *f = fopen(argv[i], "rb"); if (!f) continue; fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = malloc(len); if (fread(b, 1, len, f) != (size_t)len) { fclose(f); free(b); continue; } fclose(f);
    int sw = 0, sh = 0, dark = 0; float dar = 0; double c0 = now(CLOCK_THREAD_CPUTIME_ID), w0 = now(CLOCK_MONOTONIC);
    int r = a->frame(d, b, (int)len, W, H, rgb, &sw, &sh, &dar, &dark); double cpu = now(CLOCK_THREAD_CPUTIME_ID) - c0, wall = now(CLOCK_MONOTONIC) - w0; free(b);
    long rs = rss_kb(); if (rs > rssPeak) rssPeak = rs;
    if (pass) { printf("%s %ld KB  %dx%d  r=%d  cpu %.1f ms  wall %.1f ms\n", argv[i], len / 1024, sw, sh, r, cpu, wall); if (!r) { cpuSum += cpu; wallSum += wall; n++; } } }
  a->close(d); long rssClosed = rss_kb();
  printf("open %.1f ms | mean cpu %.1f ms, wall %.1f ms over %d | rss start %ld KB, lib %ld, peak open %ld, after close %ld\n", tOpen, n ? cpuSum / n : 0, n ? wallSum / n : 0, n, rss0, rssLib, rssPeak, rssClosed);
  return 0; }
