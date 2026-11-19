/* The download engine library's interface (libsbdl: Gopeed's downloader, github.com/GopeedLab/gopeed, GPL-3.0,
   built by tools/gopeed/build.sh and loaded on demand by downloads.c). HTTP(S) files in several connections at
   once, paused and resumed where they stopped (its own task store survives restarts). Every call returns at once;
   the app reads the state each frame. Ids are the library's own (kept across restarts). */
#ifndef SB_DL_H
#define SB_DL_H
#include <stdint.h>
#define SBDL_API_VERSION 1
enum { SBDL_WAIT = 0, SBDL_RUNNING = 1, SBDL_PAUSED = 2, SBDL_ERROR = 3, SBDL_DONE = 4, SBDL_GONE = -1 };
typedef struct {
  int state;                 /* SBDL_* */
  int64_t total, done;       /* bytes; total 0 = not known yet */
  int64_t speed;             /* bytes a second, now */
  int connections;           /* connections open now */
  char name[256];            /* the file's name as saved */
  char path[1024];           /* the file's full path once known */
  char err[200];             /* why it stopped, when SBDL_ERROR */
} sbdl_stat;
typedef struct {
  int         (*api)(void);
  const char *(*version)(void);
  /* storageDir: the engine's own store (tasks kept across restarts). 0 = running */
  int         (*start)(const char *storageDir, int connections);
  /* a download into dir (as name, "" = the server's name); headers: "Name: value" lines, may be NULL.
     id gets the task id; 0 = added (it starts by itself) */
  int         (*add)(const char *url, const char *dir, const char *name, const char *headers, char *id, int idCap);
  int         (*pause)(const char *id);
  int         (*resume)(const char *id);
  int         (*remove)(const char *id, int deleteFile);
  int         (*stat)(const char *id, sbdl_stat *out);   /* 0 = found, -1 = no such task */
} sbdl_api;
/* the one exported symbol */
typedef const sbdl_api *(*sbdl_get_fn)(void);
#endif
