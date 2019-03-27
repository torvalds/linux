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
 * Header file for the hotplug APIs
 *
 * <hr>$Revision:  $<hr>
 */

#ifndef __CVMX_APP_HOTPLUG_H__
#define __CVMX_APP_HOTPLUG_H__

#ifdef    __cplusplus
extern "C" {
#endif

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-bootmem.h>
#include <asm/octeon/cvmx-spinlock.h>
#else
#include "cvmx.h"
#include "cvmx-coremask.h"
#include "cvmx-interrupt.h"
#include "cvmx-bootmem.h"
#include "cvmx-spinlock.h"
#endif

#define CVMX_APP_HOTPLUG_MAX_APPS 32
#define CVMX_APP_HOTPLUG_MAX_APPNAME_LEN 256

/**
* hotplug_start          is the entry  point for hot plugged cores.
* cores_added_callback   is callback which in invoked when new cores are added
*                        to the application. This is invoked on all the old core
*                        that existed before the current set of cores were
*                        added.
* cores_removed_callback is callback which in invoked when cores are removed
*                        an application. This is invoked on  all the cores that
*                        exist after the set of cores being requesed are
*                        removed.
* shutdown_done_callback before the application is shutdown this callback is
*                        invoked on all the cores that are part of the app.
* unplug_callback        before the cores are unplugged this callback is invoked
*                        only on the cores that are being unlpuuged.
*/
typedef struct cvmx_app_hotplug_callbacks
{
    void (*hotplug_start)(void *ptr);
    void (*cores_added_callback) (uint32_t  ,void *ptr);
    void (*cores_removed_callback) (uint32_t,void *ptr);
    void (*shutdown_callback) (void *ptr);
    void (*unplug_core_callback) (void *ptr);
} cvmx_app_hotplug_callbacks_t;

/* The size of this struct should be a fixed size of 1024 bytes.
   Additional members should be added towards the end of the
   strcuture by adjusting the size of padding */
typedef struct cvmx_app_hotplug_info
{
    char app_name[CVMX_APP_HOTPLUG_MAX_APPNAME_LEN];
    uint32_t coremask;
    uint32_t volatile hotplug_activated_coremask;
    int32_t  valid;
    int32_t volatile shutdown_done;
    uint64_t shutdown_callback;
    uint64_t unplug_callback;
    uint64_t cores_added_callback;
    uint64_t cores_removed_callback;
    uint64_t hotplug_start;
    uint64_t data;
    uint32_t volatile hplugged_cores;
    uint32_t shutdown_cores;
    uint32_t app_shutdown;
    uint32_t unplug_cores;
    uint32_t padding[172];
} cvmx_app_hotplug_info_t;

struct cvmx_app_hotplug_global
{
   uint32_t avail_coremask;
   cvmx_app_hotplug_info_t hotplug_info_array[CVMX_APP_HOTPLUG_MAX_APPS];
   uint32_t version;
   cvmx_spinlock_t hotplug_global_lock;
   int app_under_boot;
   int app_under_shutdown;
};
typedef struct cvmx_app_hotplug_global cvmx_app_hotplug_global_t;

int is_core_being_hot_plugged(void);
int is_app_being_booted_or_shutdown(void);
void set_app_unber_boot(int val);
void set_app_under_shutdown(int val);
int cvmx_app_hotplug_shutdown_request(uint32_t, int);
int cvmx_app_hotplug_unplug_cores(int index, uint32_t coremask, int wait);
cvmx_app_hotplug_info_t* cvmx_app_hotplug_get_info(uint32_t);
int cvmx_app_hotplug_get_index(uint32_t coremask);
cvmx_app_hotplug_info_t* cvmx_app_hotplug_get_info_at_index(int index);
int is_app_hotpluggable(int index);
int cvmx_app_hotplug_call_add_cores_callback(int index);
#ifndef CVMX_BUILD_FOR_LINUX_USER
int cvmx_app_hotplug_register(void(*)(void*), void*);
int cvmx_app_hotplug_register_cb(cvmx_app_hotplug_callbacks_t *, void*, int);
int cvmx_app_hotplug_activate(void);
void cvmx_app_hotplug_core_shutdown(void);
void cvmx_app_hotplug_shutdown_disable(void);
void cvmx_app_hotplug_shutdown_enable(void);
#endif

#define CVMX_APP_HOTPLUG_INFO_REGION_SIZE  sizeof(cvmx_app_hotplug_global_t)
#define CVMX_APP_HOTPLUG_INFO_REGION_NAME  "cvmx-app-hotplug-block"

#ifdef  __cplusplus
}
#endif

#endif  /* __CVMX_APP_HOTPLUG_H__ */
