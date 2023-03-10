// SPDX-License-Identifier: Zlib
#ifndef DFLTCC_INFLATE_H
#define DFLTCC_INFLATE_H

#include "dfltcc.h"

/* External functions */
void dfltcc_reset_inflate_state(z_streamp strm);
int dfltcc_can_inflate(z_streamp strm);
typedef enum {
    DFLTCC_INFLATE_CONTINUE,
    DFLTCC_INFLATE_BREAK,
    DFLTCC_INFLATE_SOFTWARE,
} dfltcc_inflate_action;
dfltcc_inflate_action dfltcc_inflate(z_streamp strm,
                                     int flush, int *ret);
#define INFLATE_RESET_HOOK(strm) \
    dfltcc_reset_inflate_state((strm))

#define INFLATE_TYPEDO_HOOK(strm, flush) \
    if (dfltcc_can_inflate((strm))) { \
        dfltcc_inflate_action action; \
\
        RESTORE(); \
        action = dfltcc_inflate((strm), (flush), &ret); \
        LOAD(); \
        if (action == DFLTCC_INFLATE_CONTINUE) \
            break; \
        else if (action == DFLTCC_INFLATE_BREAK) \
            goto inf_leave; \
    }

#define INFLATE_NEED_CHECKSUM(strm) (!dfltcc_can_inflate((strm)))

#define INFLATE_NEED_UPDATEWINDOW(strm) (!dfltcc_can_inflate((strm)))

#endif /* DFLTCC_DEFLATE_H */
