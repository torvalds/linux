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
 *
 * Interface to the hardware Fetch and Add Unit.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_FAU_H__
#define __CVMX_FAU_H__

#ifndef CVMX_DONT_INCLUDE_CONFIG
#include "cvmx-config.h"
#else
typedef int cvmx_fau_reg_64_t;
typedef int cvmx_fau_reg_32_t;
typedef int cvmx_fau_reg_16_t;
typedef int cvmx_fau_reg_8_t;
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Octeon Fetch and Add Unit (FAU)
 */

#define CVMX_FAU_LOAD_IO_ADDRESS    cvmx_build_io_address(0x1e, 0)
#define CVMX_FAU_BITS_SCRADDR       63,56
#define CVMX_FAU_BITS_LEN           55,48
#define CVMX_FAU_BITS_INEVAL        35,14
#define CVMX_FAU_BITS_TAGWAIT       13,13
#define CVMX_FAU_BITS_NOADD         13,13
#define CVMX_FAU_BITS_SIZE          12,11
#define CVMX_FAU_BITS_REGISTER      10,0


typedef enum {
   CVMX_FAU_OP_SIZE_8  = 0,
   CVMX_FAU_OP_SIZE_16 = 1,
   CVMX_FAU_OP_SIZE_32 = 2,
   CVMX_FAU_OP_SIZE_64 = 3
} cvmx_fau_op_size_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct
{
    uint64_t    error   : 1;
    int64_t     value   : 63;
} cvmx_fau_tagwait64_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct
{
    uint64_t    error   : 1;
    int32_t     value   : 31;
} cvmx_fau_tagwait32_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct
{
    uint64_t    error   : 1;
    int16_t     value   : 15;
} cvmx_fau_tagwait16_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct
{
    uint64_t    error   : 1;
    int8_t     value    : 7;
} cvmx_fau_tagwait8_t;

/**
 * Asynchronous tagwait return definition. If a timeout occurs,
 * the error bit will be set. Otherwise the value of the
 * register before the update will be returned.
 */
typedef union {
   uint64_t        u64;
   struct {
      uint64_t     invalid: 1;
     uint64_t     data   :63; /* unpredictable if invalid is set */
   } s;
} cvmx_fau_async_tagwait_result_t;

/**
 * @INTERNAL
 * Builds a store I/O address for writing to the FAU
 *
 * @param noadd  0 = Store value is atomically added to the current value
 *               1 = Store value is atomically written over the current value
 * @param reg    FAU atomic register to access. 0 <= reg < 2048.
 *               - Step by 2 for 16 bit access.
 *               - Step by 4 for 32 bit access.
 *               - Step by 8 for 64 bit access.
 * @return Address to store for atomic update
 */
static inline uint64_t __cvmx_fau_store_address(uint64_t noadd, uint64_t reg)
{
    return (CVMX_ADD_IO_SEG(CVMX_FAU_LOAD_IO_ADDRESS) |
            cvmx_build_bits(CVMX_FAU_BITS_NOADD, noadd) |
            cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg));
}

/**
 * @INTERNAL
 * Builds a I/O address for accessing the FAU
 *
 * @param tagwait Should the atomic add wait for the current tag switch
 *                operation to complete.
 *                - 0 = Don't wait
 *                - 1 = Wait for tag switch to complete
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 *                - Step by 4 for 32 bit access.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to add.
 *                Note: When performing 32 and 64 bit access, only the low
 *                22 bits are available.
 * @return Address to read from for atomic update
 */
static inline uint64_t __cvmx_fau_atomic_address(uint64_t tagwait, uint64_t reg, int64_t value)
{
    return (CVMX_ADD_IO_SEG(CVMX_FAU_LOAD_IO_ADDRESS) |
            cvmx_build_bits(CVMX_FAU_BITS_INEVAL, value) |
            cvmx_build_bits(CVMX_FAU_BITS_TAGWAIT, tagwait) |
            cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg));
}

/**
 * Perform an atomic 64 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Value of the register before the update
 */
static inline int64_t cvmx_fau_fetch_and_add64(cvmx_fau_reg_64_t reg, int64_t value)
{
    return cvmx_read64_int64(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 32 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 4 for 32 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Value of the register before the update
 */
static inline int32_t cvmx_fau_fetch_and_add32(cvmx_fau_reg_32_t reg, int32_t value)
{
    return cvmx_read64_int32(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 16 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 * @param value   Signed value to add.
 * @return Value of the register before the update
 */
static inline int16_t cvmx_fau_fetch_and_add16(cvmx_fau_reg_16_t reg, int16_t value)
{
    return cvmx_read64_int16(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 8 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 * @param value   Signed value to add.
 * @return Value of the register before the update
 */
static inline int8_t cvmx_fau_fetch_and_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
    return cvmx_read64_int8(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 64 bit add after the current tag switch
 * completes
 *
 * @param reg    FAU atomic register to access. 0 <= reg < 2048.
 *               - Step by 8 for 64 bit access.
 * @param value  Signed value to add.
 *               Note: Only the low 22 bits are available.
 * @return If a timeout occurs, the error bit will be set. Otherwise
 *         the value of the register before the update will be
 *         returned
 */
static inline cvmx_fau_tagwait64_t cvmx_fau_tagwait_fetch_and_add64(cvmx_fau_reg_64_t reg, int64_t value)
{
    union
    {
        uint64_t i64;
        cvmx_fau_tagwait64_t t;
    } result;
    result.i64 = cvmx_read64_int64(__cvmx_fau_atomic_address(1, reg, value));
    return result.t;
}

/**
 * Perform an atomic 32 bit add after the current tag switch
 * completes
 *
 * @param reg    FAU atomic register to access. 0 <= reg < 2048.
 *               - Step by 4 for 32 bit access.
 * @param value  Signed value to add.
 *               Note: Only the low 22 bits are available.
 * @return If a timeout occurs, the error bit will be set. Otherwise
 *         the value of the register before the update will be
 *         returned
 */
static inline cvmx_fau_tagwait32_t cvmx_fau_tagwait_fetch_and_add32(cvmx_fau_reg_32_t reg, int32_t value)
{
    union
    {
        uint64_t i32;
        cvmx_fau_tagwait32_t t;
    } result;
    result.i32 = cvmx_read64_int32(__cvmx_fau_atomic_address(1, reg, value));
    return result.t;
}

/**
 * Perform an atomic 16 bit add after the current tag switch
 * completes
 *
 * @param reg    FAU atomic register to access. 0 <= reg < 2048.
 *               - Step by 2 for 16 bit access.
 * @param value  Signed value to add.
 * @return If a timeout occurs, the error bit will be set. Otherwise
 *         the value of the register before the update will be
 *         returned
 */
static inline cvmx_fau_tagwait16_t cvmx_fau_tagwait_fetch_and_add16(cvmx_fau_reg_16_t reg, int16_t value)
{
    union
    {
        uint64_t i16;
        cvmx_fau_tagwait16_t t;
    } result;
    result.i16 = cvmx_read64_int16(__cvmx_fau_atomic_address(1, reg, value));
    return result.t;
}

/**
 * Perform an atomic 8 bit add after the current tag switch
 * completes
 *
 * @param reg    FAU atomic register to access. 0 <= reg < 2048.
 * @param value  Signed value to add.
 * @return If a timeout occurs, the error bit will be set. Otherwise
 *         the value of the register before the update will be
 *         returned
 */
static inline cvmx_fau_tagwait8_t cvmx_fau_tagwait_fetch_and_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
    union
    {
        uint64_t i8;
        cvmx_fau_tagwait8_t t;
    } result;
    result.i8 = cvmx_read64_int8(__cvmx_fau_atomic_address(1, reg, value));
    return result.t;
}

/**
 * @INTERNAL
 * Builds I/O data for async operations
 *
 * @param scraddr Scratch pad byte addres to write to.  Must be 8 byte aligned
 * @param value   Signed value to add.
 *                Note: When performing 32 and 64 bit access, only the low
 *                22 bits are available.
 * @param tagwait Should the atomic add wait for the current tag switch
 *                operation to complete.
 *                - 0 = Don't wait
 *                - 1 = Wait for tag switch to complete
 * @param size    The size of the operation:
 *                - CVMX_FAU_OP_SIZE_8  (0) = 8 bits
 *                - CVMX_FAU_OP_SIZE_16 (1) = 16 bits
 *                - CVMX_FAU_OP_SIZE_32 (2) = 32 bits
 *                - CVMX_FAU_OP_SIZE_64 (3) = 64 bits
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 *                - Step by 4 for 32 bit access.
 *                - Step by 8 for 64 bit access.
 * @return Data to write using cvmx_send_single
 */
static inline uint64_t __cvmx_fau_iobdma_data(uint64_t scraddr, int64_t value, uint64_t tagwait,
                                          cvmx_fau_op_size_t size, uint64_t reg)
{
    return (CVMX_FAU_LOAD_IO_ADDRESS |
                      cvmx_build_bits(CVMX_FAU_BITS_SCRADDR, scraddr>>3) |
                      cvmx_build_bits(CVMX_FAU_BITS_LEN, 1) |
                      cvmx_build_bits(CVMX_FAU_BITS_INEVAL, value) |
                      cvmx_build_bits(CVMX_FAU_BITS_TAGWAIT, tagwait) |
                      cvmx_build_bits(CVMX_FAU_BITS_SIZE, size) |
                      cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg));
}

/**
 * Perform an async atomic 64 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add64(uint64_t scraddr, cvmx_fau_reg_64_t reg, int64_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 0, CVMX_FAU_OP_SIZE_64, reg));
}

/**
 * Perform an async atomic 32 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 4 for 32 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add32(uint64_t scraddr, cvmx_fau_reg_32_t reg, int32_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 0, CVMX_FAU_OP_SIZE_32, reg));
}

/**
 * Perform an async atomic 16 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 * @param value   Signed value to add.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add16(uint64_t scraddr, cvmx_fau_reg_16_t reg, int16_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 0, CVMX_FAU_OP_SIZE_16, reg));
}

/**
 * Perform an async atomic 8 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 * @param value   Signed value to add.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add8(uint64_t scraddr, cvmx_fau_reg_8_t reg, int8_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 0, CVMX_FAU_OP_SIZE_8, reg));
}

/**
 * Perform an async atomic 64 bit add after the current tag
 * switch completes.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 *                If a timeout occurs, the error bit (63) will be set. Otherwise
 *                the value of the register before the update will be
 *                returned
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add64(uint64_t scraddr, cvmx_fau_reg_64_t reg, int64_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 1, CVMX_FAU_OP_SIZE_64, reg));
}

/**
 * Perform an async atomic 32 bit add after the current tag
 * switch completes.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 *                If a timeout occurs, the error bit (63) will be set. Otherwise
 *                the value of the register before the update will be
 *                returned
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 4 for 32 bit access.
 * @param value   Signed value to add.
 *                Note: Only the low 22 bits are available.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add32(uint64_t scraddr, cvmx_fau_reg_32_t reg, int32_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 1, CVMX_FAU_OP_SIZE_32, reg));
}

/**
 * Perform an async atomic 16 bit add after the current tag
 * switch completes.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 *                If a timeout occurs, the error bit (63) will be set. Otherwise
 *                the value of the register before the update will be
 *                returned
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 * @param value   Signed value to add.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add16(uint64_t scraddr, cvmx_fau_reg_16_t reg, int16_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 1, CVMX_FAU_OP_SIZE_16, reg));
}

/**
 * Perform an async atomic 8 bit add after the current tag
 * switch completes.
 *
 * @param scraddr Scratch memory byte address to put response in.
 *                Must be 8 byte aligned.
 *                If a timeout occurs, the error bit (63) will be set. Otherwise
 *                the value of the register before the update will be
 *                returned
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 * @param value   Signed value to add.
 * @return Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add8(uint64_t scraddr, cvmx_fau_reg_8_t reg, int8_t value)
{
    cvmx_send_single(__cvmx_fau_iobdma_data(scraddr, value, 1, CVMX_FAU_OP_SIZE_8, reg));
}

/**
 * Perform an atomic 64 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to add.
 */
static inline void cvmx_fau_atomic_add64(cvmx_fau_reg_64_t reg, int64_t value)
{
    cvmx_write64_int64(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 32 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 4 for 32 bit access.
 * @param value   Signed value to add.
 */
static inline void cvmx_fau_atomic_add32(cvmx_fau_reg_32_t reg, int32_t value)
{
    cvmx_write64_int32(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 16 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 * @param value   Signed value to add.
 */
static inline void cvmx_fau_atomic_add16(cvmx_fau_reg_16_t reg, int16_t value)
{
    cvmx_write64_int16(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 8 bit add
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 * @param value   Signed value to add.
 */
static inline void cvmx_fau_atomic_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
    cvmx_write64_int8(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 64 bit write
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 8 for 64 bit access.
 * @param value   Signed value to write.
 */
static inline void cvmx_fau_atomic_write64(cvmx_fau_reg_64_t reg, int64_t value)
{
    cvmx_write64_int64(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 32 bit write
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 4 for 32 bit access.
 * @param value   Signed value to write.
 */
static inline void cvmx_fau_atomic_write32(cvmx_fau_reg_32_t reg, int32_t value)
{
    cvmx_write64_int32(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 16 bit write
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 *                - Step by 2 for 16 bit access.
 * @param value   Signed value to write.
 */
static inline void cvmx_fau_atomic_write16(cvmx_fau_reg_16_t reg, int16_t value)
{
    cvmx_write64_int16(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 8 bit write
 *
 * @param reg     FAU atomic register to access. 0 <= reg < 2048.
 * @param value   Signed value to write.
 */
static inline void cvmx_fau_atomic_write8(cvmx_fau_reg_8_t reg, int8_t value)
{
    cvmx_write64_int8(__cvmx_fau_store_address(1, reg), value);
}

#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_FAU_H__ */
