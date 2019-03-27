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








#ifndef __OCTEON_PCI_CONSOLE_H__
#define __OCTEON_PCI_CONSOLE_H__

#ifndef CVMX_BUILD_FOR_LINUX_KERNEL
#include "cvmx-platform.h"
#endif

/* Current versions */
#define OCTEON_PCI_CONSOLE_MAJOR_VERSION    1
#define OCTEON_PCI_CONSOLE_MINOR_VERSION    0

#define OCTEON_PCI_CONSOLE_BLOCK_NAME   "__pci_console"


/* Structure that defines a single console.


* Note: when read_index == write_index, the buffer is empty.  The actual usable size
*       of each console is console_buf_size -1;
*/
typedef struct {
    uint64_t input_base_addr;
    uint32_t input_read_index;
    uint32_t input_write_index;
    uint64_t output_base_addr;
    uint32_t output_read_index;
    uint32_t output_write_index;
    uint32_t lock;
    uint32_t buf_size;
} octeon_pci_console_t;


/* This is the main container structure that contains all the information
about all PCI consoles.  The address of this structure is passed to various
routines that operation on PCI consoles.
*/
typedef struct {
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t lock;
    uint32_t flags;
    uint32_t num_consoles;
    uint32_t pad;
    /* must be 64 bit aligned here... */
    uint64_t console_addr_array[0];  /* Array of addresses of octeon_pci_console_t structures */
    /* Implicit storage for console_addr_array */
} octeon_pci_console_desc_t;


/* Flag definitions for octeon_pci_console_desc_t */
enum {
    OCT_PCI_CON_DESC_FLAG_PERCPU = 1 << 0,  /* If set, output from core N will be sent to console N */
};

#if defined(OCTEON_TARGET) && !defined(__linux__)
/**
 * This is an internal-only function that is called from within the simple executive
 * C library, and is not intended for any other use.
 *
 * @param fd
 * @param buf
 * @param nbytes
 *
 * @return
 */
int  __cvmx_pci_console_write (int fd, char *buf, int nbytes);
#endif


#ifdef CVMX_BUILD_FOR_UBOOT
uint64_t octeon_pci_console_init(int num_consoles, int buffer_size);
#endif

/* Flag definitions for read/write functions */
enum {
    OCT_PCI_CON_FLAG_NONBLOCK = 1 << 0,  /* If set, read/write functions won't block waiting for space or data.
                                          * For reads, 0 bytes may be read, and for writes not all of the
                                          * supplied data may be written.*/
};

#if !defined(__linux__) || defined(__KERNEL__)
int octeon_pci_console_write(uint64_t console_desc_addr, unsigned int console_num, const char * buffer, int bytes_to_write, uint32_t flags);
int octeon_pci_console_write_avail(uint64_t console_desc_addr, unsigned int console_num);

int octeon_pci_console_read(uint64_t console_desc_addr, unsigned int console_num, char * buffer, int buffer_size, uint32_t flags);
int octeon_pci_console_read_avail(uint64_t console_desc_addr, unsigned int console_num);
#endif

#if !defined(OCTEON_TARGET) && defined(__linux__) && !defined(__KERNEL__)
int octeon_pci_console_host_write(uint64_t console_desc_addr, unsigned int console_num, const char * buffer, int write_reqest_size, uint32_t flags);
int octeon_pci_console_host_write_avail(uint64_t console_desc_addr, unsigned int console_num);

int octeon_pci_console_host_read(uint64_t console_desc_addr, unsigned int console_num, char * buffer, int buf_size, uint32_t flags);
int octeon_pci_console_host_read_avail(uint64_t console_desc_addr, unsigned int console_num);
#endif
#endif
