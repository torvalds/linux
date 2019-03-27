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
 * Function prototypes for accessing memory and CSRs on Octeon.
 *
 * <hr>$Revision: 38306 $<hr>
*/
#ifndef __CVMX_ACCESS_H__
#define __CVMX_ACCESS_H__

#ifdef	__cplusplus
extern "C" {
#endif

/* We're going to assume that if we are compiling for Mips then we must be
    running natively on Octoen. It is possible that this code could be
    compiled on a non Octeon Mips that is acting as a PCI/PCIe host. In this
    case this assumption will be wrong and cause issues We can't key off of
    __octeon__ since some people use stock gcc toolchains */
#if defined(__mips__) && !defined(CVMX_BUILD_FOR_LINUX_HOST)
    #define CVMX_FUNCTION static inline
#else
    #define CVMX_FUNCTION extern
#endif

/**
 * simprintf uses simulator tricks to speed up printouts.  The format
 * and args are passed to the simulator and processed natively on the host.
 * Simprintf is limited to 7 arguments, and they all must use %ll (long long)
 * format specifiers to be displayed correctly.
 *
 * @param format
 *
 * @return
 */
EXTERN_ASM void simprintf(const char *format, ...);

/**
 * This function performs some default initialization of the Octeon executive.
 * It initializes the cvmx_bootmem memory allocator with the list of physical
 * memory provided by the bootloader, and creates 1-1 TLB mappings for this
 * memory. This function should be called on all cores that will use either the
 * bootmem allocator or the 1-1 TLB mappings. Applications which require a
 * different configuration can replace this function with a suitable application
 * specific one.
 *
 * @return 0 on success
 *         -1 on failure
 */
extern int cvmx_user_app_init(void);

/**
 * Returns the Octeon processor ID.
 *
 * @return Octeon processor ID from COP0
 */
CVMX_FUNCTION uint32_t cvmx_get_proc_id(void) __attribute__ ((pure));

/**
 * Convert a memory pointer (void*) into a hardware compatable
 * memory address (uint64_t). Octeon hardware widgets don't
 * understand logical addresses.
 *
 * @param ptr    C style memory pointer
 * @return Hardware physical address
 */
CVMX_FUNCTION uint64_t cvmx_ptr_to_phys(void *ptr);

/**
 * Convert a hardware physical address (uint64_t) into a
 * memory pointer (void *).
 *
 * @param physical_address
 *               Hardware physical address to memory
 * @return Pointer to memory
 */
CVMX_FUNCTION void *cvmx_phys_to_ptr(uint64_t physical_address);

CVMX_FUNCTION void cvmx_write64_int64(uint64_t address, int64_t value);
CVMX_FUNCTION void cvmx_write64_uint64(uint64_t address, uint64_t value);
CVMX_FUNCTION void cvmx_write64_int32(uint64_t address, int32_t value);
CVMX_FUNCTION void cvmx_write64_uint32(uint64_t address, uint32_t value);
CVMX_FUNCTION void cvmx_write64_int16(uint64_t address, int16_t value);
CVMX_FUNCTION void cvmx_write64_uint16(uint64_t address, uint16_t value);
CVMX_FUNCTION void cvmx_write64_int8(uint64_t address, int8_t value);
CVMX_FUNCTION void cvmx_write64_uint8(uint64_t address, uint8_t value);
CVMX_FUNCTION void cvmx_write_csr(uint64_t csr_addr, uint64_t val);
CVMX_FUNCTION void cvmx_write_io(uint64_t io_addr, uint64_t val);

CVMX_FUNCTION int64_t cvmx_read64_int64(uint64_t address);
CVMX_FUNCTION uint64_t cvmx_read64_uint64(uint64_t address);
CVMX_FUNCTION int32_t cvmx_read64_int32(uint64_t address);
CVMX_FUNCTION uint32_t cvmx_read64_uint32(uint64_t address);
CVMX_FUNCTION int16_t cvmx_read64_int16(uint64_t address);
CVMX_FUNCTION uint16_t cvmx_read64_uint16(uint64_t address);
CVMX_FUNCTION int8_t cvmx_read64_int8(uint64_t address);
CVMX_FUNCTION uint8_t cvmx_read64_uint8(uint64_t address);
CVMX_FUNCTION uint64_t cvmx_read_csr(uint64_t csr_addr);

CVMX_FUNCTION void cvmx_send_single(uint64_t data);
CVMX_FUNCTION void cvmx_read_csr_async(uint64_t scraddr, uint64_t csr_addr);

/**
 * Number of the Core on which the program is currently running.
 *
 * @return Number of cores
 */
CVMX_FUNCTION unsigned int cvmx_get_core_num(void);

/**
 * Returns the number of bits set in the provided value.
 * Simple wrapper for POP instruction.
 *
 * @param val    32 bit value to count set bits in
 *
 * @return Number of bits set
 */
CVMX_FUNCTION uint32_t cvmx_pop(uint32_t val);

/**
 * Returns the number of bits set in the provided value.
 * Simple wrapper for DPOP instruction.
 *
 * @param val    64 bit value to count set bits in
 *
 * @return Number of bits set
 */
CVMX_FUNCTION int cvmx_dpop(uint64_t val);

/**
 * @deprecated
 * Provide current cycle counter as a return value. Deprecated, use
 * cvmx_clock_get_count(CVMX_CLOCK_CORE) to get cycle counter.
 *
 * @return current cycle counter
 */
CVMX_FUNCTION uint64_t cvmx_get_cycle(void);

/**
 * @deprecated
 * Reads a chip global cycle counter.  This counts SCLK cycles since
 * chip reset.  The counter is 64 bit. This function is deprecated as the rate
 * of the global cycle counter is different between Octeon+ and Octeon2, use
 * cvmx_clock_get_count(CVMX_CLOCK_SCLK) instead. For Octeon2, the clock rate
 * of SCLK may be differnet than the core clock.
 *
 * @return Global chip cycle count since chip reset.
 */
CVMX_FUNCTION uint64_t cvmx_get_cycle_global(void) __attribute__((deprecated));

/**
 * Wait for the specified number of core clock cycles
 *
 * @param cycles
 */
CVMX_FUNCTION void cvmx_wait(uint64_t cycles);

/**
 * Wait for the specified number of micro seconds
 *
 * @param usec   micro seconds to wait
 */
CVMX_FUNCTION void cvmx_wait_usec(uint64_t usec);

/**
 * Wait for the specified number of io clock cycles
 *
 * @param cycles
 */
CVMX_FUNCTION void cvmx_wait_io(uint64_t cycles);

/**
 * Perform a soft reset of Octeon
 *
 * @return
 */
CVMX_FUNCTION void cvmx_reset_octeon(void);

/**
 * Read a byte of fuse data
 * @param byte_addr   address to read
 *
 * @return fuse value: 0 or 1
 */
CVMX_FUNCTION uint8_t cvmx_fuse_read_byte(int byte_addr);

/**
 * Read a single fuse bit
 *
 * @param fuse   Fuse number (0-1024)
 *
 * @return fuse value: 0 or 1
 */
CVMX_FUNCTION int cvmx_fuse_read(int fuse);

#undef CVMX_FUNCTION

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_ACCESS_H__ */

