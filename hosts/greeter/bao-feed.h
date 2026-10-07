/* Preview the wave watcher writes and the bao reads.
   seq is a seqlock: odd while the watcher writes.
   x0 < 0 means there is no palm box. Fractions are
   of the camera frame, origin top left. */
#ifndef DSL_BAO_FEED_H
#define DSL_BAO_FEED_H

#include <stdint.h>

#define BAO_FEED_NAME "/dsl-bao-wave"
#define BAO_FEED_W 160
#define BAO_FEED_H 120
#define BAO_FEED_MOVE 1u
#define BAO_FEED_PALM 2u
#define BAO_FEED_WAVE 4u

typedef struct {
    uint32_t seq;
    int32_t w;
    int32_t h;
    float x0, y0, x1, y1;
    float score;
    uint32_t flags;
    uint8_t rgb[BAO_FEED_W * BAO_FEED_H * 3];
} BaoFeed;

#endif
