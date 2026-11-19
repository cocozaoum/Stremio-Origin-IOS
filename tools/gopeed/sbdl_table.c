/* The table behind libsbdl's one exported symbol (sbdl.h); the functions are the Go exports in sbdl.go. */
#include "sbdl.h"
#include "_cgo_export.h"

static int t_api(void) { return sbdlApi(); }
static const char *t_version(void) { return sbdlVersion(); }
static int t_start(const char *dir, int n) { return sbdlStart((char *)dir, n); }
static int t_add(const char *url, const char *dir, const char *name, const char *headers, char *id, int cap) {
  return sbdlAdd((char *)url, (char *)dir, (char *)name, (char *)headers, id, cap);
}
static int t_pause(const char *id) { return sbdlPause((char *)id); }
static int t_resume(const char *id) { return sbdlResume((char *)id); }
static int t_remove(const char *id, int del) { return sbdlRemove((char *)id, del); }
static int t_stat(const char *id, sbdl_stat *out) { return sbdlStat((char *)id, out); }

static const sbdl_api table = { t_api, t_version, t_start, t_add, t_pause, t_resume, t_remove, t_stat };

__attribute__((visibility("default"))) const sbdl_api *sbdl_get(void) { return &table; }
