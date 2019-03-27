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
 * interface to the low latency DRAM
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_LLM_H__
#define __CVMX_LLM_H__

#ifdef	__cplusplus
extern "C" {
#endif

#define ENABLE_DEPRECATED   /* Set to enable the old 18/36 bit names */

typedef enum
{
    CVMX_LLM_REPLICATION_NONE = 0,
    CVMX_LLM_REPLICATION_2X = 1, // on both interfaces, or 2x if only one interface
    CVMX_LLM_REPLICATION_4X = 2, // both interfaces, 2x, or 4x if only one interface
    CVMX_LLM_REPLICATION_8X = 3, // both interfaces, 4x,  or 8x if only one interface
} cvmx_llm_replication_t;

/**
 * This structure defines the address used to the low-latency memory.
 * This address format is used for both loads and stores.
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t                mbz     :30;
        cvmx_llm_replication_t  repl    : 2;
        uint64_t                address :32; // address<1:0> mbz, address<31:30> mbz
    } s;
} cvmx_llm_address_t;

/**
 * This structure defines the data format in the low-latency memory
 */
typedef union
{
    uint64_t u64;

    /**
     * this format defines the format returned on a load
     *   a load returns the 32/36-bits in memory, plus xxor = even_parity(dat<35:0>)
     *   typically, dat<35> = parity(dat<34:0>), so the xor bit directly indicates parity error
     *   Note that the data field size is 36 bits on the 36XX/38XX, and 32 bits on the 31XX
     */
    struct
    {
        uint64_t                       mbz1  :27;
        uint64_t                       xxor  : 1;
        uint64_t                       mbz   : 4;
        uint64_t                       dat   :32;
    } cn31xx;

    struct
    {
        uint64_t                       mbz   :27;
        uint64_t                       xxor  : 1;
        uint64_t                       dat   :36;
    } s;

    /**
     *  This format defines what should be used if parity is desired.  Hardware returns
     *  the XOR of all the bits in the 36/32 bit data word, so for parity software must use
     * one of the data field bits as a parity bit.
     */
    struct cn31xx_par_struct
    {
        uint64_t                       mbz   :32;
        uint64_t                       par   : 1;
        uint64_t                       dat   :31;
    } cn31xx_par;
    struct cn38xx_par_struct
    {
        uint64_t                       mbz   :28;
        uint64_t                       par   : 1;
        uint64_t                       dat   :35;
    } cn38xx_par;
#if !OCTEON_IS_COMMON_BINARY()
#if CVMX_COMPILED_FOR(OCTEON_CN31XX)
    struct cn31xx_par_struct spar;
#else
    struct cn38xx_par_struct spar;
#endif
#endif
} cvmx_llm_data_t;

#define CVMX_LLM_NARROW_DATA_WIDTH  ((CVMX_COMPILED_FOR(OCTEON_CN31XX)) ? 32 : 36)

/**
 * Calculate the parity value of a number
 *
 * @param value
 * @return parity value
 */
static inline uint64_t cvmx_llm_parity(uint64_t value)
{
    uint64_t result;
    CVMX_DPOP(result, value);
    return result;
}


/**
 * Calculate the ECC needed for 36b LLM mode
 *
 * @param value
 * @return  ECC value
 */
static inline int cvmx_llm_ecc(uint64_t value)
{
    /* FIXME: This needs a re-write */
    static const uint32_t ecc_code_29[7] = {
        0x08962595,
        0x112a4aaa,
        0x024c934f,
        0x04711c73,
        0x0781e07c,
        0x1801ff80,
        0x1ffe0000};
    uint64_t pop0, pop1, pop2, pop3, pop4, pop5, pop6;

    pop0 = ecc_code_29[0];
    pop1 = ecc_code_29[1];
    pop2 = ecc_code_29[2];
    pop0 &= value;
    pop3 = ecc_code_29[3];
    CVMX_DPOP(pop0, pop0);
    pop4 = ecc_code_29[4];
    pop1 &= value;
    CVMX_DPOP(pop1, pop1);
    pop2 &= value;
    pop5 = ecc_code_29[5];
    CVMX_DPOP(pop2, pop2);
    pop6 = ecc_code_29[6];
    pop3 &= value;
    CVMX_DPOP(pop3, pop3);
    pop4 &= value;
    CVMX_DPOP(pop4, pop4);
    pop5 &= value;
    CVMX_DPOP(pop5, pop5);
    pop6 &= value;
    CVMX_DPOP(pop6, pop6);

    return((pop6&1)<<6) | ((pop5&1)<<5) | ((pop4&1)<<4) | ((pop3&1)<<3) | ((pop2&1)<<2) | ((pop1&1)<<1) | (pop0&1);
}


#ifdef ENABLE_DEPRECATED
/* These macros are provided to provide compatibility with code that uses
** the old names for the llm access functions.  The names were changed
** when support for the 31XX llm was added, as the widths differ between Octeon Models.
** The wide/narrow names are preferred, and should be used in all new code */
#define cvmx_llm_write36 cvmx_llm_write_narrow
#define cvmx_llm_read36  cvmx_llm_read_narrow
#define cvmx_llm_write64 cvmx_llm_write_wide
#define cvmx_llm_read64  cvmx_llm_read_wide
#endif
/**
 * Write to LLM memory - 36 bit
 *
 * @param address Address in LLM to write. Consecutive writes increment the
 *                address by 4. The replication mode is also encoded in this
 *                address.
 * @param value   Value to write to LLM. Only the low 36 bits will be used.
 * @param set     Which of the two coprocessor 2 register sets to use for the
 *                write. May be used to get two outstanding LLM access at once
 *                per core. Range: 0-1
 */
static inline void cvmx_llm_write_narrow(cvmx_llm_address_t address, uint64_t value, int set)
{
    cvmx_llm_data_t data;
    data.s.mbz = 0;

    data.s.dat = value;

    data.s.xxor = 0;

    if (set)
    {
        CVMX_MT_LLM_DATA(1, data.u64);
        CVMX_MT_LLM_WRITE_ADDR_INTERNAL(1, address.u64);
    }
    else
    {
        CVMX_MT_LLM_DATA(0, data.u64);
        CVMX_MT_LLM_WRITE_ADDR_INTERNAL(0, address.u64);
    }
}


/**
 * Write to LLM memory - 64 bit
 *
 * @param address Address in LLM to write. Consecutive writes increment the
 *                address by 8. The replication mode is also encoded in this
 *                address.
 * @param value   Value to write to LLM.
 * @param set     Which of the two coprocessor 2 register sets to use for the
 *                write. May be used to get two outstanding LLM access at once
 *                per core. Range: 0-1
 */
static inline void cvmx_llm_write_wide(cvmx_llm_address_t address, uint64_t value, int set)
{
    if (set)
    {
        CVMX_MT_LLM_DATA(1, value);
        CVMX_MT_LLM_WRITE64_ADDR_INTERNAL(1, address.u64);
    }
    else
    {
        CVMX_MT_LLM_DATA(0, value);
        CVMX_MT_LLM_WRITE64_ADDR_INTERNAL(0, address.u64);
    }
}


/**
 * Read from LLM memory - 36 bit
 *
 * @param address Address in LLM to read. Consecutive reads increment the
 *                address by 4. The replication mode is also encoded in this
 *                address.
 * @param set     Which of the two coprocessor 2 register sets to use for the
 *                write. May be used to get two outstanding LLM access at once
 *                per core. Range: 0-1
 * @return The lower 36 bits contain the result of the read
 */
static inline cvmx_llm_data_t cvmx_llm_read_narrow(cvmx_llm_address_t address, int set)
{
    cvmx_llm_data_t value;
    if (set)
    {
        CVMX_MT_LLM_READ_ADDR(1, address.u64);
        CVMX_MF_LLM_DATA(1, value.u64);
    }
    else
    {
        CVMX_MT_LLM_READ_ADDR(0, address.u64);
        CVMX_MF_LLM_DATA(0, value.u64);
    }
    return value;
}


/**
 * Read from LLM memory - 64 bit
 *
 * @param address Address in LLM to read. Consecutive reads increment the
 *                address by 8. The replication mode is also encoded in this
 *                address.
 * @param set     Which of the two coprocessor 2 register sets to use for the
 *                write. May be used to get two outstanding LLM access at once
 *                per core. Range: 0-1
 * @return The result of the read
 */
static inline uint64_t cvmx_llm_read_wide(cvmx_llm_address_t address, int set)
{
    uint64_t value;
    if (set)
    {
        CVMX_MT_LLM_READ64_ADDR(1, address);
        CVMX_MF_LLM_DATA(1, value);
    }
    else
    {
        CVMX_MT_LLM_READ64_ADDR(0, address);
        CVMX_MF_LLM_DATA(0, value);
    }
    return value;
}


#define RLD_INIT_DELAY  (1<<18)



/* This structure describes the RLDRAM configuration for a board.  This structure
** must be populated with the correct values and passed to the initialization function.
*/
typedef struct
{
    uint32_t cpu_hz;            /* CPU frequency in Hz */
    char addr_rld0_fb_str [100];   /* String describing RLDRAM connections on rld 0 front (0) bunk*/
    char addr_rld0_bb_str [100];   /* String describing RLDRAM connections on rld 0 back (1) bunk*/
    char addr_rld1_fb_str [100];   /* String describing RLDRAM connections on rld 1 front (0) bunk*/
    char addr_rld1_bb_str [100];   /* String describing RLDRAM connections on rld 1 back (1) bunk*/
    uint8_t rld0_bunks;     /* Number of bunks on rld 0 (0 is disabled) */
    uint8_t rld1_bunks;     /* Number of bunks on rld 1 (0 is disabled) */
    uint16_t rld0_mbytes;   /* mbytes on rld 0 */
    uint16_t rld1_mbytes;   /* mbytes on rld 1 */
    uint16_t max_rld_clock_mhz;  /* Maximum RLD clock in MHz, only used for CN58XX */
} llm_descriptor_t;

/**
 * Initialize LLM memory controller.  This must be done
 * before the low latency memory can be used.
 * This is simply a wrapper around cvmx_llm_initialize_desc(),
 * and is deprecated.
 *
 * @return -1 on error
 *         0 on success
 */
int cvmx_llm_initialize(void);


/**
 * Initialize LLM memory controller.  This must be done
 * before the low latency memory can be used.
 *
 * @param llm_desc_ptr
 *              Pointer to descriptor structure. If NULL
 *              is passed, a default setting is used if available.
 *
 * @return -1 on error
 *         Size of llm in bytes on success
 */
int cvmx_llm_initialize_desc(llm_descriptor_t *llm_desc_ptr);



/**
 * Gets the default llm descriptor for the board code is being run on.
 *
 * @param llm_desc_ptr
 *               Pointer to descriptor structure to be filled in.  Contents are only
 *               valid after successful completion.  Must not be NULL.
 *
 * @return -1 on error
 *         0 on success
 */
int cvmx_llm_get_default_descriptor(llm_descriptor_t *llm_desc_ptr);

#ifdef	__cplusplus
}
#endif

#endif /*  __CVM_LLM_H__ */
