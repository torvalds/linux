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
 * Simple executive application initialization for Linux user space. This
 * file should be used instead of cvmx-app-init.c for running simple executive
 * applications under Linux in userspace. The following are some of the key
 * points to remember when writing applications to run both under the
 * standalone simple executive and userspace under Linux.
 *
 * -# Application main must be called "appmain" under Linux. Use and ifdef
 *      based on __linux__ to determine the proper name.
 * -# Be careful to use cvmx_ptr_to_phys() and cvmx_phys_to_ptr. The simple
 *      executive 1-1 TLB mappings allow you to be sloppy and interchange
 *      hardware addresses with virtual address. This isn't true under Linux.
 * -# If you're talking directly to hardware, be careful. The normal Linux
 *      protections are circumvented. If you do something bad, Linux won't
 *      save you.
 * -# Most hardware can only be initialized once. Unless you're very careful,
 *      this also means you Linux application can only run once.
 *
 * <hr>$Revision: 70129 $<hr>
 *
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/sysmips.h>
#include <sched.h>
#include <octeon-app-init.h>

#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-atomic.h"
#include "cvmx-sysinfo.h"
#include "cvmx-coremask.h"
#include "cvmx-spinlock.h"
#include "cvmx-bootmem.h"
#include "cvmx-helper-cfg.h"

int octeon_model_version_check(uint32_t chip_id);

#define OCTEON_ECLOCK_MULT_INPUT_X16    ((int)(33.4*16))

/* Applications using the simple executive libraries under Linux userspace must
    rename their "main" function to match the prototype below. This allows the
    simple executive to perform needed memory initialization and process
    creation before the application runs. */
extern int appmain(int argc, const char *argv[]);

/* These two external addresses provide the beginning and end markers for the
    CVMX_SHARED section. These are defined by the cvmx-shared.ld linker script.
    If they aren't defined, you probably forgot to link using this script. */
extern void __cvmx_shared_start;
extern void __cvmx_shared_end;
extern uint64_t linux_mem32_min;
extern uint64_t linux_mem32_max;
extern uint64_t linux_mem32_wired;
extern uint64_t linux_mem32_offset;

/**
 * This function performs some default initialization of the Octeon executive.  It initializes
 * the cvmx_bootmem memory allocator with the list of physical memory shared by the bootloader.
 * This function should be called on all cores that will use the bootmem allocator.
 * Applications which require a different configuration can replace this function with a suitable application
 * specific one.
 *
 * @return 0 on success
 *         -1 on failure
 */
int cvmx_user_app_init(void)
{
    return 0;
}


/**
 * Simulator magic is not supported in user mode under Linux.
 * This version of simprintf simply calls the underlying C
 * library printf for output. It also makes sure that two
 * calls to simprintf provide atomic output.
 *
 * @param format  Format string in the same format as printf.
 */
void simprintf(const char *format, ...)
{
    CVMX_SHARED static cvmx_spinlock_t simprintf_lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER;
    va_list ap;

    cvmx_spinlock_lock(&simprintf_lock);
    printf("SIMPRINTF(%d): ", (int)cvmx_get_core_num());
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    cvmx_spinlock_unlock(&simprintf_lock);
}


/**
 * Setup the CVMX_SHARED data section to be shared across
 * all processors running this application. A memory mapped
 * region is allocated using shm_open and mmap. The current
 * contents of the CVMX_SHARED section are copied into the
 * region. Then the new region is remapped to replace the
 * existing CVMX_SHARED data.
 *
 * This function will display a message and abort the
 * application under any error conditions. The Linux tmpfs
 * filesystem must be mounted under /dev/shm.
 */
static void setup_cvmx_shared(void)
{
    const char *SHM_NAME = "cvmx_shared";
    unsigned long shared_size = &__cvmx_shared_end - &__cvmx_shared_start;
    int fd;

    /* If there isn't and shared data we can skip all this */
    if (shared_size)
    {
        char shm_name[30];
        printf("CVMX_SHARED: %p-%p\n", &__cvmx_shared_start, &__cvmx_shared_end);

#ifdef __UCLIBC__
	const char *defaultdir = "/dev/shm/";
	struct statfs f;
	int pid;
	/* The canonical place is /dev/shm. */
	if (statfs (defaultdir, &f) == 0)
	{
	    pid = getpid();
	    sprintf (shm_name, "%s%s-%d", defaultdir, SHM_NAME, pid);
	}
	else
	{
	    perror("/dev/shm is not mounted");
	    exit(-1);
	}

	/* shm_open(), shm_unlink() are not implemented in uClibc. Do the
	   same thing using open() and close() system calls.  */
	fd = open (shm_name, O_RDWR | O_CREAT | O_TRUNC, 0);

	if (fd < 0)
	{
	    perror("Failed to open CVMX_SHARED(shm_name)");
	    exit(errno);
	}

	unlink (shm_name);
#else
	sprintf(shm_name, "%s-%d", SHM_NAME, getpid());
        /* Open a new shared memory region for use as CVMX_SHARED */
        fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0);
        if (fd <0)
        {
            perror("Failed to setup CVMX_SHARED(shm_open)");
            exit(errno);
        }

        /* We don't want the file on the filesystem. Immediately unlink it so
            another application can create its own shared region */
        shm_unlink(shm_name);
#endif

        /* Resize the region to match the size of CVMX_SHARED */
        ftruncate(fd, shared_size);

        /* Map the region into some random location temporarily so we can
            copy the shared data to it */
        void *ptr = mmap(NULL, shared_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == NULL)
        {
            perror("Failed to setup CVMX_SHARED(mmap copy)");
            exit(errno);
        }

        /* Copy CVMX_SHARED to the new shared region so we don't lose
            initializers */
        memcpy(ptr, &__cvmx_shared_start, shared_size);
        munmap(ptr, shared_size);

        /* Remap the shared region to replace the old CVMX_SHARED region */
        ptr = mmap(&__cvmx_shared_start, shared_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
        if (ptr == NULL)
        {
            perror("Failed to setup CVMX_SHARED(mmap final)");
            exit(errno);
        }

        /* Once mappings are setup, the file handle isn't needed anymore */
        close(fd);
    }
}


/**
 * Shutdown and free the shared CVMX_SHARED region setup by
 * setup_cvmx_shared.
 */
static void shutdown_cvmx_shared(void)
{
    unsigned long shared_size = &__cvmx_shared_end - &__cvmx_shared_start;
    if (shared_size)
        munmap(&__cvmx_shared_start, shared_size);
}


/**
 * Setup access to the CONFIG_CAVIUM_RESERVE32 memory section
 * created by the kernel. This memory is used for shared
 * hardware buffers with 32 bit userspace applications.
 */
static void setup_reserve32(void)
{
    if (linux_mem32_min && linux_mem32_max)
    {
        int region_size = linux_mem32_max - linux_mem32_min + 1;
        int mmap_flags = MAP_SHARED;
        void *linux_mem32_base_ptr = NULL;

        /* Although not strictly necessary, we are going to mmap() the wired
            TLB region so it is in the process page tables. These pages will
            never fault in, but they will allow GDB to access the wired
            region. We need the mappings to exactly match the wired TLB
            entry. */
        if (linux_mem32_wired)
        {
            mmap_flags |= MAP_FIXED;
            linux_mem32_base_ptr = CASTPTR(void, (1ull<<31) - region_size);
        }

        int fd = open("/dev/mem", O_RDWR);
        if (fd < 0)
        {
            perror("ERROR opening /dev/mem");
            exit(-1);
        }

        linux_mem32_base_ptr = mmap64(linux_mem32_base_ptr,
                                          region_size,
                                          PROT_READ | PROT_WRITE,
                                          mmap_flags,
                                          fd,
                                          linux_mem32_min);
        close(fd);

        if (MAP_FAILED == linux_mem32_base_ptr)
        {
            perror("Error mapping reserve32");
            exit(-1);
        }

        linux_mem32_offset = CAST64(linux_mem32_base_ptr) - linux_mem32_min;
    }
}


/**
 * Main entrypoint of the application. Here we setup shared
 * memory and fork processes for each cpu. This simulates the
 * normal simple executive environment of one process per
 * cpu core.
 *
 * @param argc   Number of command line arguments
 * @param argv   The command line arguments
 * @return Return value for the process
 */
int main(int argc, const char *argv[])
{
    CVMX_SHARED static cvmx_spinlock_t mask_lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER;
    CVMX_SHARED static int32_t pending_fork;
    unsigned long cpumask;
    unsigned long cpu;
    int firstcpu = 0;
    int firstcore = 0;

    cvmx_linux_enable_xkphys_access(0);
    cvmx_sysinfo_linux_userspace_initialize();

    if (sizeof(void*) == 4)
    {
        if (linux_mem32_min)
            setup_reserve32();
        else
        {
            printf("\nFailed to access 32bit shared memory region. Most likely the Kernel\n"
                   "has not been configured for 32bit shared memory access. Check the\n"
                   "kernel configuration.\n"
                   "Aborting...\n\n");
            exit(-1);
        }
    }

    setup_cvmx_shared();
    cvmx_bootmem_init(cvmx_sysinfo_get()->phy_mem_desc_addr);

    /* Check to make sure the Chip version matches the configured version */
    octeon_model_version_check(cvmx_get_proc_id());

    /* Initialize configuration to set bpid, pkind, pko_port for all the 
       available ports connected. */
    __cvmx_helper_cfg_init();

    /* Get the list of logical cpus we should run on */
    if (sched_getaffinity(0, sizeof(cpumask), (cpu_set_t*)&cpumask))
    {
        perror("sched_getaffinity failed");
        exit(errno);
    }

    cvmx_sysinfo_t *system_info = cvmx_sysinfo_get();

    cvmx_atomic_set32(&pending_fork, 1);

    /* Get the lowest logical cpu */
    firstcore = ffsl(cpumask) - 1;
    cpumask ^= (1ull<<(firstcore));
    while (1)
    {
        if (cpumask == 0)
        {
            cpu = firstcore;
            firstcpu = 1;
            break;
        }
        cpu = ffsl(cpumask) - 1;
        /* Turn off the bit for this CPU number. We've counted him */
        cpumask ^= (1ull<<cpu);
        /* Increment the number of CPUs running this app */
        cvmx_atomic_add32(&pending_fork, 1);
        /* Flush all IO streams before the fork. Otherwise any buffered
           data in the C library will be duplicated. This results in
           duplicate output from a single print */
        fflush(NULL);
        /* Fork a process for the new CPU */
        int pid = fork();
        if (pid == 0)
        {
            break;
        }
        else if (pid == -1)
        {
            perror("Fork failed");
            exit(errno);
        }
     }


    /* Set affinity to lock me to the correct CPU */
    cpumask = (1<<cpu);
    if (sched_setaffinity(0, sizeof(cpumask), (cpu_set_t*)&cpumask))
    {
        perror("sched_setaffinity failed");
        exit(errno);
    }

    cvmx_spinlock_lock(&mask_lock);
    system_info->core_mask |= 1<<cvmx_get_core_num();
    cvmx_atomic_add32(&pending_fork, -1);
    if (cvmx_atomic_get32(&pending_fork) == 0)
    {
        cvmx_dprintf("Active coremask = 0x%x\n", system_info->core_mask);
    }
    if (firstcpu)
        system_info->init_core = cvmx_get_core_num();
    cvmx_spinlock_unlock(&mask_lock);

    /* Spinning waiting for forks to complete */
    while (cvmx_atomic_get32(&pending_fork)) {}

    cvmx_coremask_barrier_sync(system_info->core_mask);

    cvmx_linux_enable_xkphys_access(1);

    int result = appmain(argc, argv);

    /* Wait for all forks to complete. This needs to be the core that started
        all of the forks. It may not be the lowest numbered core! */
    if (cvmx_get_core_num() == system_info->init_core)
    {
        int num_waits;
        CVMX_POP(num_waits, system_info->core_mask);
        num_waits--;
        while (num_waits--)
        {
            if (wait(NULL) == -1)
                perror("CVMX: Wait for forked child failed\n");
        }
    }

    shutdown_cvmx_shared();

    return result;
}
