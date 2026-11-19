/* Home's live preview: a focused channel plays in a box on the hero's right once the remote rests on it (its
   stream looked for quietly, then loaded on the video plane under a see-through hole, sound up softly); a move
   lets it go at once; OK opens the channel with the stream it already found. One decoder: it waits for a
   background trailer to let go, and the player takes it over. */
#ifndef SB_LIVEPEEK_H
#define SB_LIVEPEEK_H
#include "core/stremio.h"
#include <stdint.h>
#define LPK_W 672.0f
#define LPK_H 378.0f
#define LPK_Y 112.0f
void lpk_want(const Item *it, float x, float y, float w, float h); /* every frame while a channel's preview should play in that box (not called: none) */
void lpk_tick(float dt);                    /* once a frame, from the app */
void lpk_away(void);                        /* behind another app (#69): a channel previewing goes, its sound with it */
void lpk_draw(const char *id, const char *logo, float x, float y, float w, float h, float r, float a, uint32_t top, uint32_t bot); /* the box (its colours from top to bottom), its channel's logo while it comes */
int  lpk_handoff(const char *id, char *src, size_t cap, float *rect); /* OK on the channel previewing: its stream and box, for the player to take as it plays */
int  lpk_shown(const char *id);            /* that channel's picture is up in the box */
int  lpk_can_return(const char *id);        /* the channel playing grew from the preview: it can shrink back into its box */
int  lpk_handed(void);                      /* the player has the preview's playback (whatever channel it is on now) */
void lpk_retarget(const Item *it, const char *src, const Stream *st); /* it switched channels: that one is given back */
int  lpk_take_back(const char *id);         /* and its playback is the preview's again, as it plays */
int  lpk_busy(void);                        /* a channel's preview is wanted: no background trailer */
#endif
