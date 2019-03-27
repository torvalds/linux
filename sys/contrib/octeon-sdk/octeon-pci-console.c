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







#define CVMX_USE_1_TO_1_TLB_MAPPINGS 0
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/kernel.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-spinlock.h>
#include <asm/octeon/octeon-pci-console.h>

#define MIN(a,b) min((a),(b))

#else
#include "cvmx-platform.h"

#include "cvmx.h"
#include "cvmx-spinlock.h"
#ifndef MIN
# define	MIN(a,b) (((a)<(b))?(a):(b))
#endif

#include "cvmx-bootmem.h"
#include "octeon-pci-console.h"
#endif
#ifdef __U_BOOT__
#include <watchdog.h>
#endif

#if defined(__linux__) && !defined(__KERNEL__) && !defined(OCTEON_TARGET)
#include "octeon-pci.h"
#endif


/* The following code is only used in standalone CVMX applications. It does
    not apply for kernel or Linux programming */
#if defined(OCTEON_TARGET) && !defined(__linux__) && !defined(CVMX_BUILD_FOR_LINUX_KERNEL)

static int cvmx_pci_console_num = 0;
static int per_core_pci_consoles = 0;
static uint64_t pci_console_desc_addr = 0;
/* This function for simple executive internal use only - do not use in any application */
int  __cvmx_pci_console_write (int fd, char *buf, int nbytes)
{
    int console_num;
    if (fd >= 0x10000000)
    {
        console_num = fd & 0xFFFF;
    }
    else if (per_core_pci_consoles)
    {
        console_num = cvmx_get_core_num();
    }
    else
        console_num = cvmx_pci_console_num;

    if (!pci_console_desc_addr)
    {
        const cvmx_bootmem_named_block_desc_t *block_desc = cvmx_bootmem_find_named_block(OCTEON_PCI_CONSOLE_BLOCK_NAME);
        pci_console_desc_addr = block_desc->base_addr;
    }


    return octeon_pci_console_write(pci_console_desc_addr, console_num, buf, nbytes, 0);

}

#endif


#if !defined(CONFIG_OCTEON_U_BOOT) || (defined(CONFIG_OCTEON_U_BOOT) && (defined(CFG_PCI_CONSOLE) || defined(CONFIG_SYS_PCI_CONSOLE)))
static int octeon_pci_console_buffer_free_bytes(uint32_t buffer_size, uint32_t wr_idx, uint32_t rd_idx)
{
    if (rd_idx >= buffer_size || wr_idx >= buffer_size)
        return -1;

    return (((buffer_size -1) - (wr_idx - rd_idx))%buffer_size);
}
static int octeon_pci_console_buffer_avail_bytes(uint32_t buffer_size, uint32_t wr_idx, uint32_t rd_idx)
{
    if (rd_idx >= buffer_size || wr_idx >= buffer_size)
        return -1;

    return (buffer_size - 1 - octeon_pci_console_buffer_free_bytes(buffer_size, wr_idx, rd_idx));
}
#endif



/* The following code is only used under Linux userspace when you are using
    CVMX */
#if defined(__linux__) && !defined(__KERNEL__) && !defined(OCTEON_TARGET)
int octeon_pci_console_host_write(uint64_t console_desc_addr, unsigned int console_num, const char * buffer, int write_reqest_size, uint32_t flags)
{
    if (!console_desc_addr)
        return -1;

    /* Get global pci console information and look up specific console structure. */
    uint32_t num_consoles = octeon_read_mem32(console_desc_addr + offsetof(octeon_pci_console_desc_t, num_consoles));
//    printf("Num consoles: %d, buf size: %d\n", num_consoles, console_buffer_size);
    if (console_num >= num_consoles)
    {
        printf("ERROR: attempting to read non-existant console: %d\n", console_num);
        return(-1);
    }
    uint64_t console_addr = octeon_read_mem64(console_desc_addr + offsetof(octeon_pci_console_desc_t, console_addr_array) + console_num *8);
//    printf("Console %d is at 0x%llx\n", console_num, (long long)console_addr);

    uint32_t console_buffer_size = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, buf_size));
    /* Check to see if any data is available */
    uint32_t rd_idx, wr_idx;
    uint64_t base_addr;

    base_addr = octeon_read_mem64(console_addr + offsetof(octeon_pci_console_t, input_base_addr));
    rd_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, input_read_index));
    wr_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, input_write_index));

//    printf("Input base: 0x%llx, rd: %d(0x%x), wr: %d(0x%x)\n", (long long)base_addr, rd_idx, rd_idx, wr_idx, wr_idx);
    int bytes_to_write = octeon_pci_console_buffer_free_bytes(console_buffer_size, wr_idx, rd_idx);
    if (bytes_to_write <= 0)
        return bytes_to_write;
    bytes_to_write = MIN(bytes_to_write, write_reqest_size);
    /* Check to see if what we want to write is not contiguous, and limit ourselves to the contiguous block*/
    if (wr_idx + bytes_to_write >= console_buffer_size)
        bytes_to_write = console_buffer_size - wr_idx;

//    printf("Attempting to write %d bytes, (buf size: %d)\n", bytes_to_write, write_reqest_size);

    octeon_pci_write_mem(base_addr + wr_idx, buffer, bytes_to_write, OCTEON_PCI_ENDIAN_64BIT_SWAP);
    octeon_write_mem32(console_addr + offsetof(octeon_pci_console_t, input_write_index), (wr_idx + bytes_to_write)%console_buffer_size);

    return bytes_to_write;

}

int octeon_pci_console_host_read(uint64_t console_desc_addr, unsigned int console_num, char * buffer, int buf_size, uint32_t flags)
{
    if (!console_desc_addr)
        return -1;

    /* Get global pci console information and look up specific console structure. */
    uint32_t num_consoles = octeon_read_mem32(console_desc_addr + offsetof(octeon_pci_console_desc_t, num_consoles));
//    printf("Num consoles: %d, buf size: %d\n", num_consoles, console_buffer_size);
    if (console_num >= num_consoles)
    {
        printf("ERROR: attempting to read non-existant console: %d\n", console_num);
        return(-1);
    }
    uint64_t console_addr = octeon_read_mem64(console_desc_addr + offsetof(octeon_pci_console_desc_t, console_addr_array) + console_num *8);
    uint32_t console_buffer_size = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, buf_size));
//    printf("Console %d is at 0x%llx\n", console_num, (long long)console_addr);

    /* Check to see if any data is available */
    uint32_t rd_idx, wr_idx;
    uint64_t base_addr;

    base_addr = octeon_read_mem64(console_addr + offsetof(octeon_pci_console_t, output_base_addr));
    rd_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, output_read_index));
    wr_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, output_write_index));

//    printf("Read buffer base: 0x%llx, rd: %d(0x%x), wr: %d(0x%x)\n", (long long)base_addr, rd_idx, rd_idx, wr_idx, wr_idx);
    int bytes_to_read = octeon_pci_console_buffer_avail_bytes(console_buffer_size, wr_idx, rd_idx);
    if (bytes_to_read <= 0)
        return bytes_to_read;


    bytes_to_read = MIN(bytes_to_read, buf_size);
    /* Check to see if what we want to read is not contiguous, and limit ourselves to the contiguous block*/
    if (rd_idx + bytes_to_read >= console_buffer_size)
        bytes_to_read = console_buffer_size - rd_idx;


    octeon_pci_read_mem(buffer, base_addr + rd_idx, bytes_to_read,OCTEON_PCI_ENDIAN_64BIT_SWAP);
    octeon_write_mem32(console_addr + offsetof(octeon_pci_console_t, output_read_index), (rd_idx + bytes_to_read)%console_buffer_size);

    return bytes_to_read;
}


int octeon_pci_console_host_write_avail(uint64_t console_desc_addr, unsigned int console_num)
{
    if (!console_desc_addr)
        return -1;

    /* Get global pci console information and look up specific console structure. */
    uint32_t num_consoles = octeon_read_mem32(console_desc_addr + offsetof(octeon_pci_console_desc_t, num_consoles));
//    printf("Num consoles: %d, buf size: %d\n", num_consoles, console_buffer_size);
    if (console_num >= num_consoles)
    {
        printf("ERROR: attempting to read non-existant console: %d\n", console_num);
        return -1;
    }
    uint64_t console_addr = octeon_read_mem64(console_desc_addr + offsetof(octeon_pci_console_desc_t, console_addr_array) + console_num *8);
//    printf("Console %d is at 0x%llx\n", console_num, (long long)console_addr);

    uint32_t console_buffer_size = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, buf_size));
    /* Check to see if any data is available */
    uint32_t rd_idx, wr_idx;
    uint64_t base_addr;

    base_addr = octeon_read_mem64(console_addr + offsetof(octeon_pci_console_t, input_base_addr));
    rd_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, input_read_index));
    wr_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, input_write_index));

//    printf("Input base: 0x%llx, rd: %d(0x%x), wr: %d(0x%x)\n", (long long)base_addr, rd_idx, rd_idx, wr_idx, wr_idx);
    return octeon_pci_console_buffer_free_bytes(console_buffer_size, wr_idx, rd_idx);
}


int octeon_pci_console_host_read_avail(uint64_t console_desc_addr, unsigned int console_num)
{
    if (!console_desc_addr)
        return -1;

    /* Get global pci console information and look up specific console structure. */
    uint32_t num_consoles = octeon_read_mem32(console_desc_addr + offsetof(octeon_pci_console_desc_t, num_consoles));
//    printf("Num consoles: %d, buf size: %d\n", num_consoles, console_buffer_size);
    if (console_num >= num_consoles)
    {
        printf("ERROR: attempting to read non-existant console: %d\n", console_num);
        return(-1);
    }
    uint64_t console_addr = octeon_read_mem64(console_desc_addr + offsetof(octeon_pci_console_desc_t, console_addr_array) + console_num *8);
    uint32_t console_buffer_size = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, buf_size));
//    printf("Console %d is at 0x%llx\n", console_num, (long long)console_addr);

    /* Check to see if any data is available */
    uint32_t rd_idx, wr_idx;
    uint64_t base_addr;

    base_addr = octeon_read_mem64(console_addr + offsetof(octeon_pci_console_t, output_base_addr));
    rd_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, output_read_index));
    wr_idx = octeon_read_mem32(console_addr + offsetof(octeon_pci_console_t, output_write_index));

//    printf("Read buffer base: 0x%llx, rd: %d(0x%x), wr: %d(0x%x)\n", (long long)base_addr, rd_idx, rd_idx, wr_idx, wr_idx);
    return octeon_pci_console_buffer_avail_bytes(console_buffer_size, wr_idx, rd_idx);
}


#endif /* TARGET_HOST */






/* This code is only available in a kernel or CVMX standalone. It can't be used
    from userspace */
#if (!defined(CONFIG_OCTEON_U_BOOT) && (!defined(__linux__) || defined(__KERNEL__))) || (defined(CONFIG_OCTEON_U_BOOT) && (defined(CFG_PCI_CONSOLE) || defined(CONFIG_SYS_PCI_CONSOLE))) || defined(CVMX_BUILD_FOR_LINUX_KERNEL)

static octeon_pci_console_t *octeon_pci_console_get_ptr(uint64_t console_desc_addr, unsigned int console_num)
{
    octeon_pci_console_desc_t *cons_desc_ptr;

    if (!console_desc_addr)
        return NULL;

    cons_desc_ptr = (octeon_pci_console_desc_t *)cvmx_phys_to_ptr(console_desc_addr);
    if (console_num >= cons_desc_ptr->num_consoles)
        return NULL;

    return (octeon_pci_console_t *)cvmx_phys_to_ptr(cons_desc_ptr->console_addr_array[console_num]);
}


int octeon_pci_console_write(uint64_t console_desc_addr, unsigned int console_num, const char * buffer, int bytes_to_write, uint32_t flags)
{
    octeon_pci_console_t *cons_ptr;
    cvmx_spinlock_t *lock;
    int bytes_available;
    char *buf_ptr;
    int bytes_written;

    cons_ptr = octeon_pci_console_get_ptr(console_desc_addr, console_num);
    if (!cons_ptr)
        return -1;

    lock = (cvmx_spinlock_t *)&cons_ptr->lock;

    buf_ptr = (char*)cvmx_phys_to_ptr(cons_ptr->output_base_addr);
    bytes_written = 0;
    cvmx_spinlock_lock(lock);
    while (bytes_to_write > 0)
    {
        bytes_available = octeon_pci_console_buffer_free_bytes(cons_ptr->buf_size, cons_ptr->output_write_index, cons_ptr->output_read_index);
//        printf("Console %d has %d bytes available for writes\n", console_num, bytes_available);
        if (bytes_available > 0)
        {
            int write_size = MIN(bytes_available, bytes_to_write);
            /* Limit ourselves to what we can output in a contiguous block */
            if (cons_ptr->output_write_index + write_size >= cons_ptr->buf_size)
                write_size = cons_ptr->buf_size - cons_ptr->output_write_index;

            memcpy(buf_ptr + cons_ptr->output_write_index, buffer + bytes_written, write_size);
            CVMX_SYNCW;  /* Make sure data is visible before changing write index */
            cons_ptr->output_write_index = (cons_ptr->output_write_index + write_size)%cons_ptr->buf_size;
            bytes_to_write -= write_size;
            bytes_written += write_size;
        }
        else if (bytes_available == 0)
        {
            /* Check to see if we should wait for room, or return after a partial write */
            if (flags & OCT_PCI_CON_FLAG_NONBLOCK)
                goto done;

#ifdef __U_BOOT__
            WATCHDOG_RESET();
#endif
            cvmx_wait(1000000);  /* Delay if we are spinning */
        }
        else
        {
            bytes_written = -1;
            goto done;
        }
    }

done:
    cvmx_spinlock_unlock(lock);
    return(bytes_written);
}

int octeon_pci_console_read(uint64_t console_desc_addr, unsigned int console_num, char * buffer, int buffer_size, uint32_t flags)
{
    int bytes_available;
    char *buf_ptr;
    cvmx_spinlock_t *lock;
    int bytes_read;
    int read_size;
    octeon_pci_console_t *cons_ptr = octeon_pci_console_get_ptr(console_desc_addr, console_num);
    if (!cons_ptr)
        return -1;

    buf_ptr = (char*)cvmx_phys_to_ptr(cons_ptr->input_base_addr);

    bytes_available = octeon_pci_console_buffer_avail_bytes(cons_ptr->buf_size, cons_ptr->input_write_index, cons_ptr->input_read_index);
    if (bytes_available < 0)
        return bytes_available;

    lock = (cvmx_spinlock_t *)&cons_ptr->lock;
    cvmx_spinlock_lock(lock);

    if (!(flags & OCT_PCI_CON_FLAG_NONBLOCK))
    {
        /* Wait for some data to be available */
        while (0 == (bytes_available = octeon_pci_console_buffer_avail_bytes(cons_ptr->buf_size, cons_ptr->input_write_index, cons_ptr->input_read_index)))
        {
            cvmx_wait(1000000);
#ifdef __U_BOOT__
            WATCHDOG_RESET();
#endif
        }
    }

    bytes_read = 0;
//        printf("Console %d has %d bytes available for writes\n", console_num, bytes_available);

    /* Don't overflow the buffer passed to us */
    read_size = MIN(bytes_available, buffer_size);

    /* Limit ourselves to what we can input in a contiguous block */
    if (cons_ptr->input_read_index + read_size >= cons_ptr->buf_size)
        read_size = cons_ptr->buf_size - cons_ptr->input_read_index;

    memcpy(buffer, buf_ptr + cons_ptr->input_read_index, read_size);
    cons_ptr->input_read_index = (cons_ptr->input_read_index + read_size)%cons_ptr->buf_size;
    bytes_read += read_size;

    cvmx_spinlock_unlock(lock);
    return(bytes_read);
}


int octeon_pci_console_write_avail(uint64_t console_desc_addr, unsigned int console_num)
{
    int bytes_available;
    octeon_pci_console_t *cons_ptr = octeon_pci_console_get_ptr(console_desc_addr, console_num);
    if (!cons_ptr)
        return -1;

    bytes_available = octeon_pci_console_buffer_free_bytes(cons_ptr->buf_size, cons_ptr->input_write_index, cons_ptr->input_read_index);
    if (bytes_available >= 0)
        return(bytes_available);
    else
        return 0;
}


int octeon_pci_console_read_avail(uint64_t console_desc_addr, unsigned int console_num)
{
    int bytes_available;
    octeon_pci_console_t *cons_ptr = octeon_pci_console_get_ptr(console_desc_addr, console_num);
    if (!cons_ptr)
        return -1;

    bytes_available = octeon_pci_console_buffer_avail_bytes(cons_ptr->buf_size, cons_ptr->input_write_index, cons_ptr->input_read_index);
    if (bytes_available >= 0)
        return(bytes_available);
    else
        return 0;
}

#endif


/* This code can only be used in the bootloader */
#if defined(CONFIG_OCTEON_U_BOOT) && (defined(CFG_PCI_CONSOLE) || defined(CONFIG_SYS_PCI_CONSOLE))
uint64_t  octeon_pci_console_init(int num_consoles, int buffer_size)
{
    octeon_pci_console_desc_t *cons_desc_ptr;
    octeon_pci_console_t *cons_ptr;

    /* Compute size required for pci console structure */
    int alloc_size = num_consoles * (buffer_size * 2 + sizeof(octeon_pci_console_t) + sizeof(uint64_t)) + sizeof(octeon_pci_console_desc_t);

    /* Allocate memory for the consoles.  This must be in the range addresssible by the bootloader.
    ** Try to do so in a manner which minimizes fragmentation.  We try to put it at the top of DDR0 or bottom of
    ** DDR2 first, and only do generic allocation if those fail */
    int64_t console_block_addr = cvmx_bootmem_phy_named_block_alloc(alloc_size, OCTEON_DDR0_SIZE - alloc_size - 128, OCTEON_DDR0_SIZE, 128, OCTEON_PCI_CONSOLE_BLOCK_NAME, CVMX_BOOTMEM_FLAG_END_ALLOC);
    if (console_block_addr < 0)
        console_block_addr = cvmx_bootmem_phy_named_block_alloc(alloc_size, OCTEON_DDR2_BASE + 1, OCTEON_DDR2_BASE + alloc_size + 128, 128, OCTEON_PCI_CONSOLE_BLOCK_NAME, CVMX_BOOTMEM_FLAG_END_ALLOC);
    if (console_block_addr < 0)
        console_block_addr = cvmx_bootmem_phy_named_block_alloc(alloc_size, 0, 0x7fffffff, 128, OCTEON_PCI_CONSOLE_BLOCK_NAME, CVMX_BOOTMEM_FLAG_END_ALLOC);
    if (console_block_addr < 0)
        return 0;

    cons_desc_ptr = (void *)(uint32_t)console_block_addr;

    memset(cons_desc_ptr, 0, alloc_size);  /* Clear entire alloc'ed memory */

    cons_desc_ptr->lock = 1; /* initialize as locked until we are done */
    CVMX_SYNCW;
    cons_desc_ptr->num_consoles = num_consoles;
    cons_desc_ptr->flags = 0;
    cons_desc_ptr->major_version = OCTEON_PCI_CONSOLE_MAJOR_VERSION;
    cons_desc_ptr->minor_version = OCTEON_PCI_CONSOLE_MINOR_VERSION;

    int i;
    uint64_t avail_addr = console_block_addr + sizeof(octeon_pci_console_desc_t) + num_consoles * sizeof(uint64_t);
    for (i = 0; i < num_consoles;i++)
    {
        cons_desc_ptr->console_addr_array[i] = avail_addr;
        cons_ptr = (void *)(uint32_t)cons_desc_ptr->console_addr_array[i];
        avail_addr += sizeof(octeon_pci_console_t);
        cons_ptr->input_base_addr = avail_addr;
        avail_addr += buffer_size;
        cons_ptr->output_base_addr = avail_addr;
        avail_addr += buffer_size;
        cons_ptr->buf_size = buffer_size;
    }
    CVMX_SYNCW;
    cons_desc_ptr->lock = 0;

    return console_block_addr;


}
#endif
