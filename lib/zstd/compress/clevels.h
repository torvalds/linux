/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_CLEVELS_H
#define ZSTD_CLEVELS_H

#define ZSTD_STATIC_LINKING_ONLY  /* ZSTD_compressionParameters  */
#include <linux/zstd.h>

/*-=====  Pre-defined compression levels  =====-*/

#define ZSTD_MAX_CLEVEL     22

__attribute__((__unused__))

static const ZSTD_compressionParameters ZSTD_defaultCParameters[4][ZSTD_MAX_CLEVEL+1] = {
{   /* "default" - for any srcSize > 256 KB */
    /* W,  C,  H,  S,  L, TL, strat */
    { 19, 12, 13,  1,  6,  1, ZSTD_fast    },  /* base for negative levels */
    { 19, 13, 14,  1,  7,  0, ZSTD_fast    },  /* level  1 */
    { 20, 15, 16,  1,  6,  0, ZSTD_fast    },  /* level  2 */
    { 21, 16, 17,  1,  5,  0, ZSTD_dfast   },  /* level  3 */
    { 21, 18, 18,  1,  5,  0, ZSTD_dfast   },  /* level  4 */
    { 21, 18, 19,  3,  5,  2, ZSTD_greedy  },  /* level  5 */
    { 21, 18, 19,  3,  5,  4, ZSTD_lazy    },  /* level  6 */
    { 21, 19, 20,  4,  5,  8, ZSTD_lazy    },  /* level  7 */
    { 21, 19, 20,  4,  5, 16, ZSTD_lazy2   },  /* level  8 */
    { 22, 20, 21,  4,  5, 16, ZSTD_lazy2   },  /* level  9 */
    { 22, 21, 22,  5,  5, 16, ZSTD_lazy2   },  /* level 10 */
    { 22, 21, 22,  6,  5, 16, ZSTD_lazy2   },  /* level 11 */
    { 22, 22, 23,  6,  5, 32, ZSTD_lazy2   },  /* level 12 */
    { 22, 22, 22,  4,  5, 32, ZSTD_btlazy2 },  /* level 13 */
    { 22, 22, 23,  5,  5, 32, ZSTD_btlazy2 },  /* level 14 */
    { 22, 23, 23,  6,  5, 32, ZSTD_btlazy2 },  /* level 15 */
    { 22, 22, 22,  5,  5, 48, ZSTD_btopt   },  /* level 16 */
    { 23, 23, 22,  5,  4, 64, ZSTD_btopt   },  /* level 17 */
    { 23, 23, 22,  6,  3, 64, ZSTD_btultra },  /* level 18 */
    { 23, 24, 22,  7,  3,256, ZSTD_btultra2},  /* level 19 */
    { 25, 25, 23,  7,  3,256, ZSTD_btultra2},  /* level 20 */
    { 26, 26, 24,  7,  3,512, ZSTD_btultra2},  /* level 21 */
    { 27, 27, 25,  9,  3,999, ZSTD_btultra2},  /* level 22 */
},
{   /* for srcSize <= 256 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 18, 12, 13,  1,  5,  1, ZSTD_fast    },  /* base for negative levels */
    { 18, 13, 14,  1,  6,  0, ZSTD_fast    },  /* level  1 */
    { 18, 14, 14,  1,  5,  0, ZSTD_dfast   },  /* level  2 */
    { 18, 16, 16,  1,  4,  0, ZSTD_dfast   },  /* level  3 */
    { 18, 16, 17,  3,  5,  2, ZSTD_greedy  },  /* level  4.*/
    { 18, 17, 18,  5,  5,  2, ZSTD_greedy  },  /* level  5.*/
    { 18, 18, 19,  3,  5,  4, ZSTD_lazy    },  /* level  6.*/
    { 18, 18, 19,  4,  4,  4, ZSTD_lazy    },  /* level  7 */
    { 18, 18, 19,  4,  4,  8, ZSTD_lazy2   },  /* level  8 */
    { 18, 18, 19,  5,  4,  8, ZSTD_lazy2   },  /* level  9 */
    { 18, 18, 19,  6,  4,  8, ZSTD_lazy2   },  /* level 10 */
    { 18, 18, 19,  5,  4, 12, ZSTD_btlazy2 },  /* level 11.*/
    { 18, 19, 19,  7,  4, 12, ZSTD_btlazy2 },  /* level 12.*/
    { 18, 18, 19,  4,  4, 16, ZSTD_btopt   },  /* level 13 */
    { 18, 18, 19,  4,  3, 32, ZSTD_btopt   },  /* level 14.*/
    { 18, 18, 19,  6,  3,128, ZSTD_btopt   },  /* level 15.*/
    { 18, 19, 19,  6,  3,128, ZSTD_btultra },  /* level 16.*/
    { 18, 19, 19,  8,  3,256, ZSTD_btultra },  /* level 17.*/
    { 18, 19, 19,  6,  3,128, ZSTD_btultra2},  /* level 18.*/
    { 18, 19, 19,  8,  3,256, ZSTD_btultra2},  /* level 19.*/
    { 18, 19, 19, 10,  3,512, ZSTD_btultra2},  /* level 20.*/
    { 18, 19, 19, 12,  3,512, ZSTD_btultra2},  /* level 21.*/
    { 18, 19, 19, 13,  3,999, ZSTD_btultra2},  /* level 22.*/
},
{   /* for srcSize <= 128 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 17, 12, 12,  1,  5,  1, ZSTD_fast    },  /* base for negative levels */
    { 17, 12, 13,  1,  6,  0, ZSTD_fast    },  /* level  1 */
    { 17, 13, 15,  1,  5,  0, ZSTD_fast    },  /* level  2 */
    { 17, 15, 16,  2,  5,  0, ZSTD_dfast   },  /* level  3 */
    { 17, 17, 17,  2,  4,  0, ZSTD_dfast   },  /* level  4 */
    { 17, 16, 17,  3,  4,  2, ZSTD_greedy  },  /* level  5 */
    { 17, 16, 17,  3,  4,  4, ZSTD_lazy    },  /* level  6 */
    { 17, 16, 17,  3,  4,  8, ZSTD_lazy2   },  /* level  7 */
    { 17, 16, 17,  4,  4,  8, ZSTD_lazy2   },  /* level  8 */
    { 17, 16, 17,  5,  4,  8, ZSTD_lazy2   },  /* level  9 */
    { 17, 16, 17,  6,  4,  8, ZSTD_lazy2   },  /* level 10 */
    { 17, 17, 17,  5,  4,  8, ZSTD_btlazy2 },  /* level 11 */
    { 17, 18, 17,  7,  4, 12, ZSTD_btlazy2 },  /* level 12 */
    { 17, 18, 17,  3,  4, 12, ZSTD_btopt   },  /* level 13.*/
    { 17, 18, 17,  4,  3, 32, ZSTD_btopt   },  /* level 14.*/
    { 17, 18, 17,  6,  3,256, ZSTD_btopt   },  /* level 15.*/
    { 17, 18, 17,  6,  3,128, ZSTD_btultra },  /* level 16.*/
    { 17, 18, 17,  8,  3,256, ZSTD_btultra },  /* level 17.*/
    { 17, 18, 17, 10,  3,512, ZSTD_btultra },  /* level 18.*/
    { 17, 18, 17,  5,  3,256, ZSTD_btultra2},  /* level 19.*/
    { 17, 18, 17,  7,  3,512, ZSTD_btultra2},  /* level 20.*/
    { 17, 18, 17,  9,  3,512, ZSTD_btultra2},  /* level 21.*/
    { 17, 18, 17, 11,  3,999, ZSTD_btultra2},  /* level 22.*/
},
{   /* for srcSize <= 16 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 14, 12, 13,  1,  5,  1, ZSTD_fast    },  /* base for negative levels */
    { 14, 14, 15,  1,  5,  0, ZSTD_fast    },  /* level  1 */
    { 14, 14, 15,  1,  4,  0, ZSTD_fast    },  /* level  2 */
    { 14, 14, 15,  2,  4,  0, ZSTD_dfast   },  /* level  3 */
    { 14, 14, 14,  4,  4,  2, ZSTD_greedy  },  /* level  4 */
    { 14, 14, 14,  3,  4,  4, ZSTD_lazy    },  /* level  5.*/
    { 14, 14, 14,  4,  4,  8, ZSTD_lazy2   },  /* level  6 */
    { 14, 14, 14,  6,  4,  8, ZSTD_lazy2   },  /* level  7 */
    { 14, 14, 14,  8,  4,  8, ZSTD_lazy2   },  /* level  8.*/
    { 14, 15, 14,  5,  4,  8, ZSTD_btlazy2 },  /* level  9.*/
    { 14, 15, 14,  9,  4,  8, ZSTD_btlazy2 },  /* level 10.*/
    { 14, 15, 14,  3,  4, 12, ZSTD_btopt   },  /* level 11.*/
    { 14, 15, 14,  4,  3, 24, ZSTD_btopt   },  /* level 12.*/
    { 14, 15, 14,  5,  3, 32, ZSTD_btultra },  /* level 13.*/
    { 14, 15, 15,  6,  3, 64, ZSTD_btultra },  /* level 14.*/
    { 14, 15, 15,  7,  3,256, ZSTD_btultra },  /* level 15.*/
    { 14, 15, 15,  5,  3, 48, ZSTD_btultra2},  /* level 16.*/
    { 14, 15, 15,  6,  3,128, ZSTD_btultra2},  /* level 17.*/
    { 14, 15, 15,  7,  3,256, ZSTD_btultra2},  /* level 18.*/
    { 14, 15, 15,  8,  3,256, ZSTD_btultra2},  /* level 19.*/
    { 14, 15, 15,  8,  3,512, ZSTD_btultra2},  /* level 20.*/
    { 14, 15, 15,  9,  3,512, ZSTD_btultra2},  /* level 21.*/
    { 14, 15, 15, 10,  3,999, ZSTD_btultra2},  /* level 22.*/
},
};



#endif  /* ZSTD_CLEVELS_H */
