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
 * This is file defines ASM primitives for sim magic functions.

 * <hr>$Revision: 70030 $<hr>
 *
 *
 */
#ifndef __CVMX_SIM_MAGIC_H__
#define __CVMX_SIM_MAGIC_H__

#ifdef  __cplusplus
extern "C" {
#endif

/* Note, the following Magic function are only useful in the simulator
 * environment. Typical simple executive application should not use
 * these functions. Their access functions are defined and implemented in
 * the newlib
 *  SIM_MAGIC_PUTS
 *  SIM_MAGIC_WRITE
 *  SIM_MAGIC_READ
 *  SIM_MAGIC_OPEN
 *  SIM_MAGIC_CLOSE
 *  SIM_MAGIC_STAT
 *  SIM_MAGIC_FSTAT
 *  SIM_MAGIC_LSEEK
 *  SIM_MAGIC_ALLOC_MEM
 */

/* Assembler macros for accessing simulator magic functions */
#define OCTEON_SIM_MAGIC_TRAP_ADDRESS 0x8000000feffe0000ull

/* Reg t9 (r25) specifies the actual magic function*/
#define SIM_MAGIC_PUTS        0x05
#define SIM_MAGIC_SIMPRINTF   0x06
#define SIM_MAGIC_WRITE       0x07
#define SIM_MAGIC_READ        0x08
#define SIM_MAGIC_OPEN        0x09
#define SIM_MAGIC_CLOSE       0x0A
#define SIM_MAGIC_STAT        0x0B
#define SIM_MAGIC_FSTAT       0x0C
#define SIM_MAGIC_LSEEK       0x0D
#define SIM_MAGIC_ALLOC_MEM   0x20
#define SIM_MAGIC_GET_CPUFREQ 0x31      /* SDK 1.9 release and after */
#define SIM_MAGIC_GET_MEMFREQ 0x32      /* SDK 1.9 release and after */
#define SIM_MAGIC_GET_IOFREQ  0x33      /* SDK 2.0 release and after, only set in Octeon2 */

/**
 * @INTERNAL
 * sim_magci implementation function with return code.
 *
 * @param func_no   SIM magic function to invoke
 *
 * @return Result of the SIM magic function
 */
static inline int __cvmx_sim_magic_return(unsigned long long func_no)
{
    register unsigned long long  magic_addr asm ("$15");
    register unsigned long long  magic_func asm ("$25");  /* t9 */
    int ret;

    magic_addr = OCTEON_SIM_MAGIC_TRAP_ADDRESS;
    magic_func = func_no;
    asm volatile (
        "dadd $24, $31, $0 \n"
        "jalr  $15 \n"
        "dadd $31, $24, $0 \n"
        "move %0, $2"
        : "=r" (ret)
        : "r" (magic_addr), "r" (magic_func)
	: "$2", "$24" );


    return ret;
}

/**
 * @INTERNAL
 * sim_magci implementation function without return code.
 *
 * @param func_no   SIM magic function to invoke
 */
static inline void __cvmx_sim_magic_no_return(unsigned long long func_no)
{
    register unsigned long long  magic_addr asm ("$15");
    register unsigned long long  magic_func asm ("$25");  /* t9 */

    magic_addr = OCTEON_SIM_MAGIC_TRAP_ADDRESS;
    magic_func = func_no;
    asm volatile (
        "dadd $24, $31, $0 \n"
        "jalr  $15 \n"
        "dadd $31, $24, $0 \n"
        :
        : "r" (magic_addr), "r" (magic_func)
	: "$24" );

}

/**
 * @INTERNAL
 * SIM magic printf function, only support up to 8 parameters
 *
 * @param format
 */
static inline void __cvmx_sim_magic_simprintf(const char *format, ...)
{
    CVMX_SYNC;

    __cvmx_sim_magic_no_return( SIM_MAGIC_SIMPRINTF);
}

/**
 * Retrive cpu core clock frequency from the simulator
 *
 * @return simulating core frequency
 */
static inline int cvmx_sim_magic_get_cpufreq(void)
{
    CVMX_SYNC;

    return  __cvmx_sim_magic_return(SIM_MAGIC_GET_CPUFREQ);
}

/**
 * Retrive DDR clock frequency from the simulator
 *
 * @return simulating DDR frequency
 */
static inline int cvmx_sim_magic_get_memfreq(void)
{
    CVMX_SYNC;

    return __cvmx_sim_magic_return(SIM_MAGIC_GET_MEMFREQ);
}

/**
 * Retrive io core clock frequency from the simulator
 *
 * @return simulating core frequency
 */
static inline int cvmx_sim_magic_get_iofreq(void)
{
    CVMX_SYNC;

    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
       return  __cvmx_sim_magic_return(SIM_MAGIC_GET_IOFREQ);
    else
       return 0;
}

#ifdef  __cplusplus
}
#endif

#endif /* __CVMX_SIM_MAGIC_H__ */
