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
 * Provides APIs for applications to register for hotplug. It also provides 
 * APIs for requesting shutdown of a running target application. 
 *
 * <hr>$Revision: $<hr>
 */

#include "cvmx-app-hotplug.h"
#include "cvmx-spinlock.h"
#include "cvmx-debug.h"

//#define DEBUG 1

static cvmx_app_hotplug_global_t *hotplug_global_ptr = 0;

#ifndef CVMX_BUILD_FOR_LINUX_USER

static CVMX_SHARED cvmx_spinlock_t cvmx_app_hotplug_sync_lock = { CVMX_SPINLOCK_UNLOCKED_VAL };
static CVMX_SHARED cvmx_spinlock_t cvmx_app_hotplug_lock = { CVMX_SPINLOCK_UNLOCKED_VAL };
static CVMX_SHARED cvmx_app_hotplug_info_t *cvmx_app_hotplug_info_ptr = NULL;

static void __cvmx_app_hotplug_shutdown(int irq_number, uint64_t registers[32], void *user_arg);
static void __cvmx_app_hotplug_sync(void);
static void __cvmx_app_hotplug_reset(void);

/* Declaring this array here is a compile time check to ensure that the
   size of  cvmx_app_hotplug_info_t is 1024. If the size is not 1024
   the size of the array will be -1 and this results in a compilation
   error */
char __hotplug_info_check[(sizeof(cvmx_app_hotplug_info_t) == 1024) ? 1 : -1];
/**
 * This routine registers an application for hotplug. It installs a handler for
 * any incoming shutdown request. It also registers a callback routine from the
 * application. This callback is invoked when the application receives a 
 * shutdown notification. 
 *
 * This routine only needs to be called once per application. 
 *
 * @param fn      Callback routine from the application. 
 * @param arg     Argument to the application callback routine. 
 * @return        Return 0 on success, -1 on failure
 *
 */
int cvmx_app_hotplug_register(void(*fn)(void*), void* arg)
{
    /* Find the list of applications launched by bootoct utility. */

    if (!(cvmx_app_hotplug_info_ptr = cvmx_app_hotplug_get_info(cvmx_sysinfo_get()->core_mask)))
    {
        /* Application not launched by bootoct? */
        printf("ERROR: cmvx_app_hotplug_register() failed\n");
        return -1;
    }

    /* Register the callback */
    cvmx_app_hotplug_info_ptr->data = CAST64(arg);
    cvmx_app_hotplug_info_ptr->shutdown_callback = CAST64(fn);

#ifdef DEBUG
    printf("cvmx_app_hotplug_register(): coremask 0x%x valid %d\n", 
                  cvmx_app_hotplug_info_ptr->coremask, cvmx_app_hotplug_info_ptr->valid);
#endif

    cvmx_interrupt_register(CVMX_IRQ_MBOX0, __cvmx_app_hotplug_shutdown, NULL);

    return 0;
}

/**
 * This routine deprecates the the cvmx_app_hotplug_register method. This
 * registers application for hotplug and the application will have CPU
 * hotplug callbacks. Various callbacks are specified in cb.
 * cvmx_app_hotplug_callbacks_t documents the callbacks
 *
 * This routine only needs to be called once per application.
 *
 * @param cb      Callback routine from the application.
 * @param arg     Argument to the application callback routins
 * @param app_shutdown   When set to 1 the application will invoke core_shutdown
                         on each core. When set to 0 core shutdown will be
                         called invoked automatically after invoking the
                         application callback.
 * @return        Return index of app on success, -1 on failure
 *
 */
int cvmx_app_hotplug_register_cb(cvmx_app_hotplug_callbacks_t *cb, void* arg,
                                 int app_shutdown)
{
    cvmx_app_hotplug_info_t *app_info;

    /* Find the list of applications launched by bootoct utility. */
    app_info = cvmx_app_hotplug_get_info(cvmx_sysinfo_get()->core_mask);
    cvmx_app_hotplug_info_ptr = app_info;
    if (!app_info)
    {
        /* Application not launched by bootoct? */
        printf("ERROR: cmvx_app_hotplug_register() failed\n");
        return -1;
    }
    /* Register the callback */
    app_info->data = CAST64(arg);
    app_info->shutdown_callback  = CAST64(cb->shutdown_callback);
    app_info->cores_added_callback = CAST64(cb->cores_added_callback);
    app_info->cores_removed_callback = CAST64(cb->cores_removed_callback);
    app_info->unplug_callback = CAST64(cb->unplug_core_callback);
    app_info->hotplug_start = CAST64(cb->hotplug_start);
    app_info->app_shutdown = app_shutdown;
#ifdef DEBUG
    printf("cvmx_app_hotplug_register(): coremask 0x%x valid %d\n",
           app_info->coremask, app_info->valid);
#endif

    cvmx_interrupt_register(CVMX_IRQ_MBOX0, __cvmx_app_hotplug_shutdown, NULL);
    return 0;

}

void cvmx_app_hotplug_remove_self_from_core_mask(void)
{
    int core = cvmx_get_core_num();
    uint32_t core_mask = 1ull << core;

    cvmx_spinlock_lock(&cvmx_app_hotplug_lock);
    cvmx_app_hotplug_info_ptr->coremask = cvmx_app_hotplug_info_ptr->coremask & ~core_mask ;
    cvmx_app_hotplug_info_ptr->hotplug_activated_coremask =
        cvmx_app_hotplug_info_ptr->hotplug_activated_coremask & ~core_mask ;
    cvmx_spinlock_unlock(&cvmx_app_hotplug_lock);
}



/**
*  Returns 1 if the running core is being unplugged, else it returns 0.
*/
int is_core_being_unplugged(void)
{
    if (cvmx_app_hotplug_info_ptr->unplug_cores &
        (1ull << cvmx_get_core_num()))
        return 1;
    return 0;
}


/**
 * Activate the current application core for receiving hotplug shutdown requests.
 *
 * This routine makes sure that each core belonging to the application is enabled 
 * to receive the shutdown notification and also provides a barrier sync to make
 * sure that all cores are ready. 
 */
int cvmx_app_hotplug_activate(void)
{
    uint64_t cnt = 0;
    uint64_t cnt_interval = 10000000;

    while (!cvmx_app_hotplug_info_ptr) 
    {
        cnt++;
        if ((cnt % cnt_interval) == 0)
            printf("waiting for cnt=%lld\n", (unsigned long long)cnt);
    }

    if (cvmx_app_hotplug_info_ptr->hplugged_cores & (1ull << cvmx_get_core_num()))
    {
#ifdef DEBUG
        printf("core=%d : is being hotplugged \n", cvmx_get_core_num());
#endif
        cvmx_sysinfo_t *sys_info_ptr = cvmx_sysinfo_get();
        sys_info_ptr->core_mask |= 1ull << cvmx_get_core_num();
    }
    else
    {
        __cvmx_app_hotplug_sync();
    }
    cvmx_spinlock_lock(&cvmx_app_hotplug_lock);
    if (!cvmx_app_hotplug_info_ptr)
    {
        cvmx_spinlock_unlock(&cvmx_app_hotplug_lock);
        printf("ERROR: This application is not registered for hotplug\n");
        return -1;
    }
    /* Enable the interrupt before we mark the core as activated */
    cvmx_interrupt_unmask_irq(CVMX_IRQ_MBOX0);
    cvmx_app_hotplug_info_ptr->hotplug_activated_coremask |= (1ull<<cvmx_get_core_num());

#ifdef DEBUG
    printf("cvmx_app_hotplug_activate(): coremask 0x%x valid %d sizeof %d\n", 
                 cvmx_app_hotplug_info_ptr->coremask, cvmx_app_hotplug_info_ptr->valid, 
                 sizeof(*cvmx_app_hotplug_info_ptr));
#endif

    cvmx_spinlock_unlock(&cvmx_app_hotplug_lock);

    return 0;
}

/**
 * This routine is only required if cvmx_app_hotplug_shutdown_request() was called
 * with wait=0. This routine waits for the application shutdown to complete. 
 *
 * @param coremask     Coremask the application is running on. 
 * @return             0 on success, -1 on error
 *
 */
int cvmx_app_hotplug_shutdown_complete(uint32_t coremask)
{
    cvmx_app_hotplug_info_t *hotplug_info_ptr;

    if (!(hotplug_info_ptr = cvmx_app_hotplug_get_info(coremask)))
    {
        printf("\nERROR: Failed to get hotplug info for coremask: 0x%x\n", (unsigned int)coremask);
        return -1;
    }

    while(!hotplug_info_ptr->shutdown_done);

    /* Clean up the hotplug info region for this app */
    bzero(hotplug_info_ptr, sizeof(*hotplug_info_ptr));

    return 0;
}

/**
 * Disable recognition of any incoming shutdown request. 
 */

void cvmx_app_hotplug_shutdown_disable(void)
{
    cvmx_interrupt_mask_irq(CVMX_IRQ_MBOX0);
}

/**
 * Re-enable recognition of incoming shutdown requests.
 */

void cvmx_app_hotplug_shutdown_enable(void)
{
    cvmx_interrupt_unmask_irq(CVMX_IRQ_MBOX0);
}

/**
*  Request shutdown of the currently running core. Should be
*  called by the application when it has been registered with
*  app_shutdown option set to 1.
*/
void cvmx_app_hotplug_core_shutdown(void)
{
    uint32_t flags;
    if (cvmx_app_hotplug_info_ptr->shutdown_cores)
    {
        cvmx_sysinfo_t *sys_info_ptr = cvmx_sysinfo_get();
       __cvmx_app_hotplug_sync();
        if (cvmx_coremask_first_core(sys_info_ptr->core_mask))
        {
            bzero(cvmx_app_hotplug_info_ptr,
                  sizeof(*cvmx_app_hotplug_info_ptr));
            #ifdef DEBUG
            printf("__cvmx_app_hotplug_shutdown(): setting shutdown done! \n");
            #endif
            cvmx_app_hotplug_info_ptr->shutdown_done = 1;
        }
        /* Tell the debugger that this application is finishing.  */
        cvmx_debug_finish ();
        flags = cvmx_interrupt_disable_save();
        __cvmx_app_hotplug_sync();
        /* Reset the core */
        __cvmx_app_hotplug_reset();
    }
    else
    {
        cvmx_sysinfo_remove_self_from_core_mask();
        cvmx_app_hotplug_remove_self_from_core_mask();
        flags = cvmx_interrupt_disable_save();
        __cvmx_app_hotplug_reset();
    }
}

/*
 * ISR for the incoming shutdown request interrupt.
 */
static void __cvmx_app_hotplug_shutdown(int irq_number, uint64_t registers[32],
                                        void *user_arg)
{
    cvmx_sysinfo_t *sys_info_ptr = cvmx_sysinfo_get();
    uint64_t mbox;
    cvmx_app_hotplug_info_t *ai = cvmx_app_hotplug_info_ptr;
    int dbg = 0;

#ifdef DEBUG
    dbg = 1;
#endif
    cvmx_interrupt_mask_irq(CVMX_IRQ_MBOX0);

    mbox = cvmx_read_csr(CVMX_CIU_MBOX_CLRX(cvmx_get_core_num()));
    /* Clear the interrupt */
    cvmx_write_csr(CVMX_CIU_MBOX_CLRX(cvmx_get_core_num()), mbox);

    /* Make sure the write above completes */
    cvmx_read_csr(CVMX_CIU_MBOX_CLRX(cvmx_get_core_num()));


    if (!cvmx_app_hotplug_info_ptr)
    {
        printf("ERROR: Application is not registered for hotplug!\n");
        return;
    }

    if (ai->hotplug_activated_coremask != sys_info_ptr->core_mask)
    {
        printf("ERROR: Shutdown requested when not all app cores have "
               "activated hotplug\n" "Application coremask: 0x%x Hotplug "
               "coremask: 0x%x\n", (unsigned int)sys_info_ptr->core_mask,
               (unsigned int)ai->hotplug_activated_coremask);
        return;
    }

    if (mbox & 1ull)
    {
        int core = cvmx_get_core_num();
        if (dbg)
            printf("Shutting down application .\n");
        /* Call the application's own callback function */
        if (ai->shutdown_callback)
        {
            ((void(*)(void*))(long)ai->shutdown_callback)(CASTPTR(void *, ai->data));
        }
        else
        {
            printf("ERROR : Shutdown callback has not been registered\n");
        }
        if (!ai->app_shutdown)
        {
            if (dbg) 
                printf("%s : core = %d Invoking app shutdown\n", __FUNCTION__, core);
            cvmx_app_hotplug_core_shutdown();
        }
    }
    else if (mbox & 2ull)
    {
        int core = cvmx_get_core_num();
        int unplug = is_core_being_unplugged();
        if (dbg) printf("%s : core=%d Unplug event \n", __FUNCTION__, core);

        if (unplug)
        {
            /* Call the application's own callback function */
            if (ai->unplug_callback)
            {
                if (dbg) printf("%s : core=%d Calling unplug callback\n",
                                __FUNCTION__, core);
                ((void(*)(void*))(long)ai->unplug_callback)(CASTPTR(void *,
                                                           ai->data));
            }
            if (!ai->app_shutdown)
            {
                if (dbg) printf("%s : core = %d Invoking app shutdown\n",
                                __FUNCTION__, core);
                cvmx_app_hotplug_core_shutdown();
            }
        }
        else
        {
            if (ai->cores_removed_callback)
            {
                if (dbg) printf("%s : core=%d Calling cores removed callback\n",
                                __FUNCTION__, core);
                ((void(*)(uint32_t, void*))(long)ai->cores_removed_callback)
                    (ai->unplug_cores, CASTPTR(void *, ai->data));
            }
            cvmx_interrupt_unmask_irq(CVMX_IRQ_MBOX0);
        }
    }
    else if (mbox & 4ull)
    {
        int core = cvmx_get_core_num();
        if (dbg) printf("%s : core=%d Add cores event \n", __FUNCTION__, core);

        if (ai->cores_added_callback)
        {
            if (dbg) printf("%s : core=%d Calling cores added callback\n",
                            __FUNCTION__, core);
            ((void(*)(uint32_t, void*))(long)ai->cores_added_callback)
                (ai->hplugged_cores, CASTPTR(void *, ai->data));
        }
        cvmx_interrupt_unmask_irq(CVMX_IRQ_MBOX0);
    }
    else
    {
        printf("ERROR: unexpected mbox=%llx\n", (unsigned long long)mbox);
    }

}

void __cvmx_app_hotplug_reset(void)
{
#define IDLE_CORE_BLOCK_NAME    "idle-core-loop"
#define HPLUG_MAKE_XKPHYS(x)       ((1ULL << 63) | (x))
    uint64_t reset_addr;
    const cvmx_bootmem_named_block_desc_t *block_desc;

    block_desc = cvmx_bootmem_find_named_block(IDLE_CORE_BLOCK_NAME);
    if (!block_desc) {
        cvmx_dprintf("Named block(%s) is not created\n", IDLE_CORE_BLOCK_NAME);
        /* loop here, should not happen */
        __asm__ volatile (
                          ".set noreorder      \n"
                          "\tsync               \n"
                          "\tnop               \n"
                          "1:\twait            \n"
                          "\tb 1b              \n"
                          "\tnop               \n"
                          ".set reorder        \n"
                          ::
                          );
    }

    reset_addr = HPLUG_MAKE_XKPHYS(block_desc->base_addr);
    asm volatile ("       .set push                \n"
                  "       .set mips64              \n"
                  "       .set noreorder           \n"
                  "       move  $2, %[addr]        \n"
                  "       jr    $2                 \n"
                  "       nop                      \n"
                  "       .set pop "
                  :: [addr] "r"(reset_addr)
                  : "$2");

    /*Should never reach here*/
    while (1) ;

}

/* 
 * We need a separate sync operation from cvmx_coremask_barrier_sync() to
 * avoid a deadlock on state.lock, since the application itself maybe doing a
 * cvmx_coremask_barrier_sync(). 
 */
static void __cvmx_app_hotplug_sync(void)
{
    static CVMX_SHARED volatile uint32_t sync_coremask = 0;
    cvmx_sysinfo_t *sys_info_ptr = cvmx_sysinfo_get();

    cvmx_spinlock_lock(&cvmx_app_hotplug_sync_lock);
    
    sync_coremask |= cvmx_coremask_core(cvmx_get_core_num());

    cvmx_spinlock_unlock(&cvmx_app_hotplug_sync_lock);

    while (sync_coremask != sys_info_ptr->core_mask);

    cvmx_spinlock_lock(&cvmx_app_hotplug_sync_lock);
    sync_coremask = 0;
    cvmx_spinlock_unlock(&cvmx_app_hotplug_sync_lock);


}

#endif /* CVMX_BUILD_FOR_LINUX_USER */

/**
*  Returns 1 if the running core is being hotplugged, else it returns 0.
*/
int is_core_being_hot_plugged(void)
{

#ifndef CVMX_BUILD_FOR_LINUX_USER
    if (!cvmx_app_hotplug_info_ptr) return 0;
    if (cvmx_app_hotplug_info_ptr->hplugged_cores &
        (1ull << cvmx_get_core_num()))
        return 1;
    return 0;
#else
    return 0;
#endif
}

static cvmx_app_hotplug_global_t *cvmx_app_get_hotplug_global_ptr(void)
{
    const struct cvmx_bootmem_named_block_desc *block_desc;
    cvmx_app_hotplug_global_t *hgp;

    if(hotplug_global_ptr != 0) return hotplug_global_ptr;

    block_desc = cvmx_bootmem_find_named_block(CVMX_APP_HOTPLUG_INFO_REGION_NAME);
    if (!block_desc)
    {
        printf("ERROR: Hotplug info region is not setup\n");
        return NULL;
    }
    else
#ifdef CVMX_BUILD_FOR_LINUX_USER
    {
        size_t pg_sz = sysconf(_SC_PAGESIZE), size;
        off_t offset;
        char *vaddr;
        int fd;

        if ((fd = open("/dev/mem", O_RDWR)) == -1) {
            perror("open");
            return NULL;
        }

        /*
         * We need to mmap() this memory, since this was allocated from the 
         * kernel bootup code and does not reside in the RESERVE32 region.
         */
        size = CVMX_APP_HOTPLUG_INFO_REGION_SIZE + pg_sz-1;
        offset = block_desc->base_addr & ~(pg_sz-1);
        if ((vaddr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset)) 
	    == MAP_FAILED) 
        {
            perror("mmap");
            return NULL;
        }

        hgp = (cvmx_app_hotplug_global_t *)(vaddr + ( block_desc->base_addr & (pg_sz-1)));
    }
#else
    hgp = CASTPTR(void, CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS, block_desc->base_addr));
#endif
    hotplug_global_ptr = hgp;
    return hgp;

}

/**
 * Return the hotplug info structure (cvmx_app_hotplug_info_t) pointer for the 
 * application running on the given coremask. 
 *
 * @param coremask     Coremask of application. 
 * @return             Returns hotplug info struct on success, NULL on failure
 *
 */
cvmx_app_hotplug_info_t* cvmx_app_hotplug_get_info(uint32_t coremask)
{
    cvmx_app_hotplug_info_t *hip;
    cvmx_app_hotplug_global_t *hgp;
    int i;
    int dbg = 0;

#ifdef DEBUG
    dbg = 1;
#endif
    hgp = cvmx_app_get_hotplug_global_ptr();
    if (!hgp) return NULL;
    hip = hgp->hotplug_info_array;

    /* Look for the current app's info */
    for (i=0; i<CVMX_APP_HOTPLUG_MAX_APPS; i++)
    {
        if (hip[i].coremask == coremask)
        {
	    if (dbg)
                printf("cvmx_app_hotplug_get_info(): coremask match %d -- coremask 0x%x, valid %d\n", i, (unsigned int)hip[i].coremask, (unsigned int)hip[i].valid);
            return &hip[i];
        }
    }
    return NULL;
}

/**
 * Return the hotplug application index structure for the application running on the 
 * given coremask. 
 *
 * @param coremask     Coremask of application. 
 * @return             Returns hotplug application index on success. -1 on failure
 *                     
 */
int cvmx_app_hotplug_get_index(uint32_t coremask)
{
    cvmx_app_hotplug_info_t *hip;
    cvmx_app_hotplug_global_t *hgp;
    int i;
    int dbg = 0;

#ifdef DEBUG
    dbg = 1;
#endif
    hgp = cvmx_app_get_hotplug_global_ptr();
    if (!hgp) return -1;
    hip = hgp->hotplug_info_array;

    /* Look for the current app's info */
    for (i=0; i<CVMX_APP_HOTPLUG_MAX_APPS; i++)
    {
        if (hip[i].coremask == coremask)
        {
	    if (dbg)
                printf("cvmx_app_hotplug_get_info(): coremask match %d -- coremask 0x%x valid %d\n", i, (unsigned int)hip[i].coremask, (unsigned int)hip[i].valid);
	    return i;
        }
    }
    return -1;
}

void print_hot_plug_info(cvmx_app_hotplug_info_t* hpinfo)
{
    printf("name=%s coremask=%08x hotplugged coremask=%08x valid=%d\n", hpinfo->app_name,
           (unsigned int)hpinfo->coremask, (unsigned int)hpinfo->hotplug_activated_coremask, (unsigned int)hpinfo->valid);
}

/**
 * Return the hotplug info structure (cvmx_app_hotplug_info_t) pointer for the 
 * application with the specified index.
 *
 * @param index        index of application. 
 * @return             Returns hotplug info struct on success, NULL on failure
 *
 */
cvmx_app_hotplug_info_t* cvmx_app_hotplug_get_info_at_index(int index)
{
    cvmx_app_hotplug_info_t *hip;
    cvmx_app_hotplug_global_t *hgp;

    hgp = cvmx_app_get_hotplug_global_ptr();
    if (!hgp) return NULL;
    hip = hgp->hotplug_info_array;

#ifdef DEBUG
    printf("cvmx_app_hotplug_get_info(): hotplug_info phy addr 0x%llx ptr %p\n", 
                  block_desc->base_addr, hgp);
#endif
    if (index < CVMX_APP_HOTPLUG_MAX_APPS)
    {
        if (hip[index].valid) 
        {
            //print_hot_plug_info( &hip[index] );
            return &hip[index];
        }
    }
    return NULL;
}

/**
 * Determines if SE application at the index specified is hotpluggable.
 *
 * @param index        index of application.
 * @return             Returns -1  on error.
 *                     0 -> The application is not hotpluggable
 *                     1 -> The application is hotpluggable
*/
int is_app_hotpluggable(int index)
{
    cvmx_app_hotplug_info_t *ai;

    if (!(ai = cvmx_app_hotplug_get_info_at_index(index)))
    {
        printf("\nERROR: Failed to get hotplug info for app at index=%d\n", index);
        return -1;
    }
    if (ai->hotplug_activated_coremask) return 1;
    return 0;
}

/**
 * This routine sends a shutdown request to a running target application. 
 *
 * @param coremask     Coremask the application is running on. 
 * @param wait         1 - Wait for shutdown completion
 *                     0 - Do not wait
 * @return             0 on success, -1 on error
 *
 */

int cvmx_app_hotplug_shutdown_request(uint32_t coremask, int wait) 
{
    int i;
    cvmx_app_hotplug_info_t *hotplug_info_ptr;

    if (!(hotplug_info_ptr = cvmx_app_hotplug_get_info(coremask)))
    {
        printf("\nERROR: Failed to get hotplug info for coremask: 0x%x\n", (unsigned int)coremask);
        return -1;
    }
    hotplug_info_ptr->shutdown_cores = coremask;
    if (!hotplug_info_ptr->shutdown_callback)
    {
        printf("\nERROR: Target application has not registered for hotplug!\n");
        return -1;
    }

    if (hotplug_info_ptr->hotplug_activated_coremask != coremask)
    {
        printf("\nERROR: Not all application cores have activated hotplug\n");
        return -1;
    }

    /* Send IPIs to all application cores to request shutdown */
    for (i=0; i<CVMX_MAX_CORES; i++) {
            if (coremask & (1ull<<i))
                cvmx_write_csr(CVMX_CIU_MBOX_SETX(i), 1);
    }

    if (wait)
    {
        while (!hotplug_info_ptr->shutdown_done);    

        /* Clean up the hotplug info region for this application */
        bzero(hotplug_info_ptr, sizeof(*hotplug_info_ptr));
    }

    return 0;
}



/**
 * This routine invokes the invoked the cores_added callbacks.
 */
int cvmx_app_hotplug_call_add_cores_callback(int index)
{
    cvmx_app_hotplug_info_t *ai;
    int i;
    if (!(ai = cvmx_app_hotplug_get_info_at_index(index)))
    {
        printf("\nERROR: Failed to get hotplug info for app at index=%d\n", index);
        return -1;
    }
   /* Send IPIs to all application cores to request add_cores callback*/
    for (i=0; i<CVMX_MAX_CORES; i++) {
            if (ai->coremask & (1ull<<i))
                cvmx_write_csr(CVMX_CIU_MBOX_SETX(i), 4);
    }
    return 0;
}

/**
 * This routine sends a request to a running target application
 * to unplug a specified set cores
 * @param index        is the index of the target application
 * @param coremask     Coremask of the cores to be unplugged from the app.
 * @param wait         1 - Wait for shutdown completion
 *                     0 - Do not wait
 * @return             0 on success, -1 on error
 *
 */ 
int cvmx_app_hotplug_unplug_cores(int index, uint32_t coremask, int wait) 
{
    cvmx_app_hotplug_info_t *ai;
    int i;

    if (!(ai = cvmx_app_hotplug_get_info_at_index(index)))
    {
        printf("\nERROR: Failed to get hotplug info for app at index=%d\n", index);
        return -1;
    }
    ai->unplug_cores = coremask;
#if 0
    if (!ai->shutdown_callback)
    {
        printf("\nERROR: Target application has not registered for hotplug!\n");
        return -1;
    }
#endif
    if ( (ai->coremask | coremask ) != ai->coremask)
    {
        printf("\nERROR: Not all cores requested are a part of the app "
               "r=%08x:%08x\n", (unsigned int)coremask, (unsigned int)ai->coremask);
        return -1;
    }
    if (ai->coremask == coremask)
    {
        printf("\nERROR: Trying to remove all cores in app. "
               "r=%08x:%08x\n", (unsigned int)coremask, (unsigned int)ai->coremask);
        return -1;
    }
    /* Send IPIs to all application cores to request unplug/remove_cores
       callback */
    for (i=0; i<CVMX_MAX_CORES; i++) {
            if (ai->coremask & (1ull<<i))
                cvmx_write_csr(CVMX_CIU_MBOX_SETX(i), 2);
    }

#if 0
    if (wait)
    {
        while (!ai->shutdown_done);    

        /* Clean up the hotplug info region for this application */
        bzero(ai, sizeof(*ai));
    }
#endif
    return 0;
}

/**
 * Returns 1 if any app is currently being currently booted , hotplugged or
 * shutdown. Only one app can be under a boot, hotplug or shutdown condition.
 * Before booting an app this methods should be used to check whether boot or
 * shutdown activity is in progress and proceed with the boot or shutdown only
 * when there is no other activity.
 *
 */
int is_app_under_boot_or_shutdown(void)
{
    int ret=0;
    cvmx_app_hotplug_global_t *hgp;

    hgp = cvmx_app_get_hotplug_global_ptr();
    cvmx_spinlock_lock(&hgp->hotplug_global_lock);
    if (hgp->app_under_boot || hgp->app_under_shutdown) ret=1;
    cvmx_spinlock_unlock(&hgp->hotplug_global_lock);
    return ret;

}

/**
 * Sets or clear the app_under_boot value. This when set signifies that an app
 * is being currently booted or hotplugged with a new core.
 *
 *
 * @param val     sets the app_under_boot to the specified value. This should be
 *                set to 1 while app any is being booted and cleared after the
 *                application has booted up.
 *
 */
void set_app_unber_boot(int val)
{
    cvmx_app_hotplug_global_t *hgp;

    hgp = cvmx_app_get_hotplug_global_ptr();
    cvmx_spinlock_lock(&hgp->hotplug_global_lock);
    hgp->app_under_boot = val;
    cvmx_spinlock_unlock(&hgp->hotplug_global_lock);
}

/**
 * Sets or clear the app_under_shutdown value. This when set signifies that an
 * app is being currently shutdown or some cores of an app are being shutdown.
 *
 * @param val     sets the app_under_shutdown to the specified value. This
 *                should be set to 1 while any app is being shutdown and cleared
 *                after the shutdown of the app is complete.
 *
 */
void set_app_under_shutdown(int val)
{
    cvmx_app_hotplug_global_t *hgp;

    hgp = cvmx_app_get_hotplug_global_ptr();
    cvmx_spinlock_lock(&hgp->hotplug_global_lock);
    hgp->app_under_shutdown = val;
    cvmx_spinlock_unlock(&hgp->hotplug_global_lock);
}
