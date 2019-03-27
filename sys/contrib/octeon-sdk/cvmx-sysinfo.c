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
 * This module provides system/board/application information obtained by the bootloader.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-spinlock.h>
#include <asm/octeon/cvmx-sysinfo.h>
#else
#include "cvmx.h"
#include "cvmx-spinlock.h"
#include "cvmx-sysinfo.h"
#endif


/**
 * This structure defines the private state maintained by sysinfo module.
 *
 */
#if defined(CVMX_BUILD_FOR_UBOOT) && CONFIG_OCTEON_NAND_STAGE2
/* For u-boot, put this in the text section so that we can use this in early
** boot when running from ram(or L2 cache).  This is primarily used for NAND
** access during NAND boot.   The 'data_in_text' section is merged with the
** text section by the linker script to avoid an assembler warning. */
static struct {

    cvmx_sysinfo_t   sysinfo;      /**< system information */
    cvmx_spinlock_t  lock;         /**< mutex spinlock */

} state __attribute__ ((section (".data_in_text"))) = {
    .lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER
};
#else
CVMX_SHARED static struct {

    struct cvmx_sysinfo   sysinfo;      /**< system information */
    cvmx_spinlock_t  lock;         /**< mutex spinlock */

} state = {
    .lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER
};
#endif

#ifdef CVMX_BUILD_FOR_LINUX_USER
/* Global variable with the processor ID since we can't read it directly */
CVMX_SHARED uint32_t cvmx_app_init_processor_id;
#endif

/* Global variables that define the min/max of the memory region set up for 32 bit userspace access */
uint64_t linux_mem32_min = 0;
uint64_t linux_mem32_max = 0;
uint64_t linux_mem32_wired = 0;
uint64_t linux_mem32_offset = 0;

/**
 * This function returns the application information as obtained
 * by the bootloader.  This provides the core mask of the cores
 * running the same application image, as well as the physical
 * memory regions available to the core.
 *
 * @return  Pointer to the boot information structure
 *
 */
struct cvmx_sysinfo *cvmx_sysinfo_get(void)
{
    return &(state.sysinfo);
}

void cvmx_sysinfo_add_self_to_core_mask(void)
{
    int core = cvmx_get_core_num();
    uint32_t core_mask = 1 << core;
    
    cvmx_spinlock_lock(&state.lock);
    state.sysinfo.core_mask = state.sysinfo.core_mask | core_mask;
    cvmx_spinlock_unlock(&state.lock);
}

void cvmx_sysinfo_remove_self_from_core_mask(void)
{
    int core = cvmx_get_core_num();
    uint32_t core_mask = 1 << core;
    
    cvmx_spinlock_lock(&state.lock);
    state.sysinfo.core_mask = state.sysinfo.core_mask & ~core_mask;
    cvmx_spinlock_unlock(&state.lock);
}

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_sysinfo_get);
#endif


/**
 * This function is used in non-simple executive environments (such as Linux kernel, u-boot, etc.)
 * to configure the minimal fields that are required to use
 * simple executive files directly.
 *
 * Locking (if required) must be handled outside of this
 * function
 *
 * @param phy_mem_desc_addr
 *                   Address of the global physical memory descriptor (bootmem
 *                   descriptor)
 * @param board_type Octeon board type enumeration
 *
 * @param board_rev_major
 *                   Board major revision
 * @param board_rev_minor
 *                   Board minor revision
 * @param cpu_clock_hz
 *                   CPU clock freqency in hertz
 *
 * @return 0: Failure
 *         1: success
 */
int cvmx_sysinfo_minimal_initialize(uint64_t phy_mem_desc_addr, uint16_t board_type, uint8_t board_rev_major,
                                    uint8_t board_rev_minor, uint32_t cpu_clock_hz)
{


    memset(&(state.sysinfo), 0x0, sizeof(state.sysinfo));
    state.sysinfo.phy_mem_desc_addr = phy_mem_desc_addr;
    state.sysinfo.board_type = board_type;
    state.sysinfo.board_rev_major = board_rev_major;
    state.sysinfo.board_rev_minor = board_rev_minor;
    state.sysinfo.cpu_clock_hz = cpu_clock_hz;

    return(1);
}

#ifdef CVMX_BUILD_FOR_LINUX_USER
/**
 * Initialize the sysinfo structure when running on
 * Octeon under Linux userspace
 */
void cvmx_sysinfo_linux_userspace_initialize(void)
{
    cvmx_sysinfo_t *system_info = cvmx_sysinfo_get();
    memset(system_info, 0, sizeof(cvmx_sysinfo_t));

    system_info->core_mask = 0;
    system_info->init_core = -1;

    FILE *infile = fopen("/proc/octeon_info", "r");
    if (infile == NULL)
    {
        perror("Error opening /proc/octeon_info");
        exit(-1);
    }

    while (!feof(infile))
    {
        char buffer[80];
        if (fgets(buffer, sizeof(buffer), infile))
        {
            const char *field = strtok(buffer, " ");
            const char *valueS = strtok(NULL, " ");
            if (field == NULL)
                continue;
            if (valueS == NULL)
                continue;
            unsigned long long value;
            sscanf(valueS, "%lli", &value);

            if (strcmp(field, "dram_size:") == 0)
                system_info->system_dram_size = value << 20;
            else if (strcmp(field, "phy_mem_desc_addr:") == 0)
                system_info->phy_mem_desc_addr = value;
            else if (strcmp(field, "eclock_hz:") == 0)
                system_info->cpu_clock_hz = value;
            else if (strcmp(field, "dclock_hz:") == 0)
                system_info->dram_data_rate_hz = value * 2;
            else if (strcmp(field, "board_type:") == 0)
                system_info->board_type = value;
            else if (strcmp(field, "board_rev_major:") == 0)
                system_info->board_rev_major = value;
            else if (strcmp(field, "board_rev_minor:") == 0)
                system_info->board_rev_minor = value;
            else if (strcmp(field, "board_serial_number:") == 0)
                strncpy(system_info->board_serial_number, valueS, sizeof(system_info->board_serial_number));
            else if (strcmp(field, "mac_addr_base:") == 0)
            {
                int i;
                int m[6];
                sscanf(valueS, "%02x:%02x:%02x:%02x:%02x:%02x", m+0, m+1, m+2, m+3, m+4, m+5);
                for (i=0; i<6; i++)
                    system_info->mac_addr_base[i] = m[i];
            }
            else if (strcmp(field, "mac_addr_count:") == 0)
                system_info->mac_addr_count = value;
            else if (strcmp(field, "fdt_addr:") == 0)
                system_info->fdt_addr = UNMAPPED_PTR(value);
            else if (strcmp(field, "32bit_shared_mem_base:") == 0)
                linux_mem32_min = value;
            else if (strcmp(field, "32bit_shared_mem_size:") == 0)
                linux_mem32_max = linux_mem32_min + value - 1;
            else if (strcmp(field, "processor_id:") == 0)
                cvmx_app_init_processor_id = value;
            else if (strcmp(field, "32bit_shared_mem_wired:") == 0)
                linux_mem32_wired = value;
        }
    }

    /*
     * set up the feature map.
     */
    octeon_feature_init();

    system_info->cpu_clock_hz = cvmx_clock_get_rate(CVMX_CLOCK_CORE);
}
#endif
