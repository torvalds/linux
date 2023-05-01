// SPDX-License-Identifier: Zlib
#ifndef DFLTCC_DEFLATE_H
#define DFLTCC_DEFLATE_H

#include "dfltcc.h"

/* External functions */
int dfltcc_can_deflate(z_streamp strm);
int dfltcc_deflate(z_streamp strm,
                   int flush,
                   block_state *result);
void dfltcc_reset_deflate_state(z_streamp strm);

#define DEFLATE_RESET_HOOK(strm) \
    dfltcc_reset_deflate_state((strm))

#define DEFLATE_HOOK dfltcc_deflate

#define DEFLATE_NEED_CHECKSUM(strm) (!dfltcc_can_deflate((strm)))

#endif /* DFLTCC_DEFLATE_H */
