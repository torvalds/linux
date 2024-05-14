// SPDX-License-Identifier: Zlib
#ifndef DFLTCC_UTIL_H
#define DFLTCC_UTIL_H

#include <linux/zutil.h>

/*
 * C wrapper for the DEFLATE CONVERSION CALL instruction.
 */
typedef enum {
    DFLTCC_CC_OK = 0,
    DFLTCC_CC_OP1_TOO_SHORT = 1,
    DFLTCC_CC_OP2_TOO_SHORT = 2,
    DFLTCC_CC_OP2_CORRUPT = 2,
    DFLTCC_CC_AGAIN = 3,
} dfltcc_cc;

#define DFLTCC_QAF 0
#define DFLTCC_GDHT 1
#define DFLTCC_CMPR 2
#define DFLTCC_XPND 4
#define HBT_CIRCULAR (1 << 7)
#define HB_BITS 15
#define HB_SIZE (1 << HB_BITS)

static inline dfltcc_cc dfltcc(
    int fn,
    void *param,
    Byte **op1,
    size_t *len1,
    const Byte **op2,
    size_t *len2,
    void *hist
)
{
    Byte *t2 = op1 ? *op1 : NULL;
    size_t t3 = len1 ? *len1 : 0;
    const Byte *t4 = op2 ? *op2 : NULL;
    size_t t5 = len2 ? *len2 : 0;
    register int r0 __asm__("r0") = fn;
    register void *r1 __asm__("r1") = param;
    register Byte *r2 __asm__("r2") = t2;
    register size_t r3 __asm__("r3") = t3;
    register const Byte *r4 __asm__("r4") = t4;
    register size_t r5 __asm__("r5") = t5;
    int cc;

    __asm__ volatile(
                     ".insn rrf,0xb9390000,%[r2],%[r4],%[hist],0\n"
                     "ipm %[cc]\n"
                     : [r2] "+r" (r2)
                     , [r3] "+r" (r3)
                     , [r4] "+r" (r4)
                     , [r5] "+r" (r5)
                     , [cc] "=r" (cc)
                     : [r0] "r" (r0)
                     , [r1] "r" (r1)
                     , [hist] "r" (hist)
                     : "cc", "memory");
    t2 = r2; t3 = r3; t4 = r4; t5 = r5;

    if (op1)
        *op1 = t2;
    if (len1)
        *len1 = t3;
    if (op2)
        *op2 = t4;
    if (len2)
        *len2 = t5;
    return (cc >> 28) & 3;
}

static inline int is_bit_set(
    const char *bits,
    int n
)
{
    return bits[n / 8] & (1 << (7 - (n % 8)));
}

static inline void turn_bit_off(
    char *bits,
    int n
)
{
    bits[n / 8] &= ~(1 << (7 - (n % 8)));
}

static inline int dfltcc_are_params_ok(
    int level,
    uInt window_bits,
    int strategy,
    uLong level_mask
)
{
    return (level_mask & (1 << level)) != 0 &&
        (window_bits == HB_BITS) &&
        (strategy == Z_DEFAULT_STRATEGY);
}

char *oesc_msg(char *buf, int oesc);

#endif /* DFLTCC_UTIL_H */
