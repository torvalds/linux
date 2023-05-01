// SPDX-License-Identifier: Zlib
#ifndef DFLTCC_H
#define DFLTCC_H

#include "../zlib_deflate/defutil.h"
#include <asm/facility.h>
#include <asm/setup.h>

/*
 * Tuning parameters.
 */
#define DFLTCC_LEVEL_MASK 0x2 /* DFLTCC compression for level 1 only */
#define DFLTCC_LEVEL_MASK_DEBUG 0x3fe /* DFLTCC compression for all levels */
#define DFLTCC_BLOCK_SIZE 1048576
#define DFLTCC_FIRST_FHT_BLOCK_SIZE 4096
#define DFLTCC_DHT_MIN_SAMPLE_SIZE 4096
#define DFLTCC_RIBM 0

#define DFLTCC_FACILITY 151

/*
 * Parameter Block for Query Available Functions.
 */
struct dfltcc_qaf_param {
    char fns[16];
    char reserved1[8];
    char fmts[2];
    char reserved2[6];
};

static_assert(sizeof(struct dfltcc_qaf_param) == 32);

#define DFLTCC_FMT0 0

/*
 * Parameter Block for Generate Dynamic-Huffman Table, Compress and Expand.
 */
struct dfltcc_param_v0 {
    uint16_t pbvn;                     /* Parameter-Block-Version Number */
    uint8_t mvn;                       /* Model-Version Number */
    uint8_t ribm;                      /* Reserved for IBM use */
    unsigned reserved32 : 31;
    unsigned cf : 1;                   /* Continuation Flag */
    uint8_t reserved64[8];
    unsigned nt : 1;                   /* New Task */
    unsigned reserved129 : 1;
    unsigned cvt : 1;                  /* Check Value Type */
    unsigned reserved131 : 1;
    unsigned htt : 1;                  /* Huffman-Table Type */
    unsigned bcf : 1;                  /* Block-Continuation Flag */
    unsigned bcc : 1;                  /* Block Closing Control */
    unsigned bhf : 1;                  /* Block Header Final */
    unsigned reserved136 : 1;
    unsigned reserved137 : 1;
    unsigned dhtgc : 1;                /* DHT Generation Control */
    unsigned reserved139 : 5;
    unsigned reserved144 : 5;
    unsigned sbb : 3;                  /* Sub-Byte Boundary */
    uint8_t oesc;                      /* Operation-Ending-Supplemental Code */
    unsigned reserved160 : 12;
    unsigned ifs : 4;                  /* Incomplete-Function Status */
    uint16_t ifl;                      /* Incomplete-Function Length */
    uint8_t reserved192[8];
    uint8_t reserved256[8];
    uint8_t reserved320[4];
    uint16_t hl;                       /* History Length */
    unsigned reserved368 : 1;
    uint16_t ho : 15;                  /* History Offset */
    uint32_t cv;                       /* Check Value */
    unsigned eobs : 15;                /* End-of-block Symbol */
    unsigned reserved431: 1;
    uint8_t eobl : 4;                  /* End-of-block Length */
    unsigned reserved436 : 12;
    unsigned reserved448 : 4;
    uint16_t cdhtl : 12;               /* Compressed-Dynamic-Huffman Table
                                          Length */
    uint8_t reserved464[6];
    uint8_t cdht[288];
    uint8_t reserved[32];
    uint8_t csb[1152];
};

static_assert(sizeof(struct dfltcc_param_v0) == 1536);

#define CVT_CRC32 0
#define CVT_ADLER32 1
#define HTT_FIXED 0
#define HTT_DYNAMIC 1

/*
 *  Extension of inflate_state and deflate_state for DFLTCC.
 */
struct dfltcc_state {
    struct dfltcc_param_v0 param;      /* Parameter block */
    struct dfltcc_qaf_param af;        /* Available functions */
    char msg[64];                      /* Buffer for strm->msg */
};

/*
 *  Extension of inflate_state and deflate_state for DFLTCC.
 */
struct dfltcc_deflate_state {
    struct dfltcc_state common;        /* Parameter block */
    uLong level_mask;                  /* Levels on which to use DFLTCC */
    uLong block_size;                  /* New block each X bytes */
    uLong block_threshold;             /* New block after total_in > X */
    uLong dht_threshold;               /* New block only if avail_in >= X */
};

#define ALIGN_UP(p, size) (__typeof__(p))(((uintptr_t)(p) + ((size) - 1)) & ~((size) - 1))
/* Resides right after inflate_state or deflate_state */
#define GET_DFLTCC_STATE(state) ((struct dfltcc_state *)((char *)(state) + ALIGN_UP(sizeof(*state), 8)))

void dfltcc_reset_state(struct dfltcc_state *dfltcc_state);

static inline int is_dfltcc_enabled(void)
{
return (zlib_dfltcc_support != ZLIB_DFLTCC_DISABLED &&
        test_facility(DFLTCC_FACILITY));
}

#define DEFLATE_DFLTCC_ENABLED() is_dfltcc_enabled()

#endif /* DFLTCC_H */
