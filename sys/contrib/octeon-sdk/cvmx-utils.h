/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * @file
 * Small utility functions and macros to ease programming of Octeon.
 *
 * <hr>$Revision: 38306 $<hr>
*/
#ifndef __CVMX_UTILS_H__
#define __CVMX_UTILS_H__

#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include <stdarg.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef TRUE
#define FALSE   0
#define TRUE    (!(FALSE))
#endif

/*
 * The macros cvmx_likely and cvmx_unlikely use the
 * __builtin_expect GCC operation to control branch
 * probabilities for a conditional. For example, an "if"
 * statement in the code that will almost always be
 * executed should be written as "if (cvmx_likely(...))".
 * If the "else" section of an if statement is more
 * probable, use "if (cvmx_unlikey(...))".
 */
#define cvmx_likely(x)      __builtin_expect(!!(x), 1)
#define cvmx_unlikely(x)    __builtin_expect(!!(x), 0)

#if CVMX_ENABLE_DEBUG_PRINTS
    #ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        #define cvmx_dprintf        printk
        #define cvmx_dvprintf       vprintk
    #elif defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
	void cvmx_dvprintf(const char *, va_list);
	void cvmx_dprintf(const char *, ...) __attribute__ ((format(printf, 1, 2)));
    #else
        #define cvmx_dprintf        printf
        #define cvmx_dvprintf       vprintf
    #endif
#else
    static inline void cvmx_dvprintf(const char *format, va_list ap)
    {
        /* Prints are disbled, do nothing */
    }

    static inline void cvmx_dprintf(const char *format, ...) __attribute__ ((format(printf, 1, 2)));
    static inline void cvmx_dprintf(const char *format, ...)
    {
        /* Prints are disbled, do nothing */
    }
#endif

#define CAST64(v) ((long long)(long)(v)) // use only when 'v' is a pointer
#define CASTPTR(type, v) ((type *)(long)(v))
#define CVMX_CACHE_LINE_SIZE    (128)   // In bytes
#define CVMX_CACHE_LINE_MASK    (CVMX_CACHE_LINE_SIZE - 1)   // In bytes
#define CVMX_CACHE_LINE_ALIGNED __attribute__ ((aligned (CVMX_CACHE_LINE_SIZE)))

/**
 * This macro spins on a field waiting for it to reach a value. It
 * is common in code to need to wait for a specific field in a CSR
 * to match a specific value. Conceptually this macro expands to:
 *
 * 1) read csr at "address" with a csr typedef of "type"
 * 2) Check if ("type".s."field" "op" "value")
 * 3) If #2 isn't true loop to #1 unless too much time has passed.
 */
#define CVMX_WAIT_FOR_FIELD64(address, type, field, op, value, timeout_usec)\
    ({int result;                                                       \
    do {                                                                \
        uint64_t done = cvmx_clock_get_count(CVMX_CLOCK_CORE) + (uint64_t)timeout_usec * \
                        cvmx_clock_get_rate(CVMX_CLOCK_CORE) / 1000000; \
        type c;                                                         \
        while (1)                                                       \
        {                                                               \
            c.u64 = cvmx_read_csr(address);                             \
            if ((c.s.field) op (value)) {                               \
                result = 0;                                             \
                break;                                                  \
            } else if (cvmx_clock_get_count(CVMX_CLOCK_CORE) > done) {  \
                result = -1;                                            \
                break;                                                  \
            } else                                                      \
                cvmx_wait(100);                                         \
        }                                                               \
    } while (0);                                                        \
    result;})

#define CVMX_BUILD_ASSERT_ZERO(e)    (sizeof(struct {int __static_assert:(e)?1:-1;}))
#define CVMX_BUILD_ASSERT(condition) ((void)CVMX_BUILD_ASSERT_ZERO(condition))

/**
 * Builds a bit mask given the required size in bits.
 *
 * @param bits   Number of bits in the mask
 * @return The mask
 */
static inline uint64_t cvmx_build_mask(uint64_t bits)
{
    return ~((~0x0ull) << bits);
}


/**
 * Builds a memory address for I/O based on the Major and Sub DID.
 *
 * @param major_did 5 bit major did
 * @param sub_did   3 bit sub did
 * @return I/O base address
 */
static inline uint64_t cvmx_build_io_address(uint64_t major_did, uint64_t sub_did)
{
    return ((0x1ull << 48) | (major_did << 43) | (sub_did << 40));
}


/**
 * Perform mask and shift to place the supplied value into
 * the supplied bit rage.
 *
 * Example: cvmx_build_bits(39,24,value)
 * <pre>
 * 6       5       4       3       3       2       1
 * 3       5       7       9       1       3       5       7      0
 * +-------+-------+-------+-------+-------+-------+-------+------+
 * 000000000000000000000000___________value000000000000000000000000
 * </pre>
 *
 * @param high_bit Highest bit value can occupy (inclusive) 0-63
 * @param low_bit  Lowest bit value can occupy inclusive 0-high_bit
 * @param value    Value to use
 * @return Value masked and shifted
 */
static inline uint64_t cvmx_build_bits(uint64_t high_bit, uint64_t low_bit, uint64_t value)
{
    return ((value & cvmx_build_mask(high_bit - low_bit + 1)) << low_bit);
}


/**
 * Return the number of cores available in the chip
 *
 * @return
 */
static inline uint32_t cvmx_octeon_num_cores(void)
{
    uint32_t ciu_fuse = (uint32_t)cvmx_read_csr(CVMX_CIU_FUSE) & 0xffffffffull;
    return cvmx_pop(ciu_fuse);
}


/**
 * Return true if Octeon is CN36XX
 *
 * @return
 */
static inline int cvmx_octeon_model_CN36XX(void)
{
    return(OCTEON_IS_MODEL(OCTEON_CN38XX)
           &&cvmx_fuse_read(264));
}


#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_UTILS_H__ */

