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
    uLong level_mask;                  /* Levels on which to use DFLTCC */
    uLong block_size;                  /* New block each X bytes */
    uLong block_threshold;             /* New block after total_in > X */
    uLong dht_threshold;               /* New block only if avail_in >= X */
    char msg[64];                      /* Buffer for strm->msg */
};

/* Resides right after inflate_state or deflate_state */
#define GET_DFLTCC_STATE(state) ((struct dfltcc_state *)((state) + 1))

/* External functions */
int dfltcc_can_deflate(z_streamp strm);
int dfltcc_deflate(z_streamp strm,
                   int flush,
                   block_state *result);
void dfltcc_reset(z_streamp strm, uInt size);
int dfltcc_can_inflate(z_streamp strm);
typedef enum {
    DFLTCC_INFLATE_CONTINUE,
    DFLTCC_INFLATE_BREAK,
    DFLTCC_INFLATE_SOFTWARE,
} dfltcc_inflate_action;
dfltcc_inflate_action dfltcc_inflate(z_streamp strm,
                                     int flush, int *ret);
static inline int is_dfltcc_enabled(void)
{
return (zlib_dfltcc_support != ZLIB_DFLTCC_DISABLED &&
        test_facility(DFLTCC_FACILITY));
}

#define DEFLATE_RESET_HOOK(strm) \
    dfltcc_reset((strm), sizeof(deflate_state))

#define DEFLATE_HOOK dfltcc_deflate

#define DEFLATE_NEED_CHECKSUM(strm) (!dfltcc_can_deflate((strm)))

#define DEFLATE_DFLTCC_ENABLED() is_dfltcc_enabled()

#define INFLATE_RESET_HOOK(strm) \
    dfltcc_reset((strm), sizeof(struct inflate_state))

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

#endif /* DFLTCC_H */
