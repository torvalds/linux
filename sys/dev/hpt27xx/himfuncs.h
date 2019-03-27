/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 HighPoint Technologies, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <dev/hpt27xx/hpt27xx_config.h>
/* 
 * define _HIM_INTERFACE before include this file, and
 * undef it after include this file.
 */


#ifndef _HIM_INTERFACE
#error "you must define _HIM_INTERFACE before this file"
#endif

_HIM_INTERFACE(HPT_BOOL, get_supported_device_id, (int index, PCI_ID *id))

_HIM_INTERFACE(HPT_U8, get_controller_count, (PCI_ID *id, HPT_U8 *reached, PCI_ADDRESS *addr))


_HIM_INTERFACE(HPT_UINT, get_adapter_size, (const PCI_ID *id))


_HIM_INTERFACE(HPT_BOOL, create_adapter, (const PCI_ID *id, PCI_ADDRESS pciAddress, void *adapter, void *osext))

_HIM_INTERFACE(void, get_adapter_config, (void *adapter, HIM_ADAPTER_CONFIG *config))

_HIM_INTERFACE(HPT_BOOL, get_meminfo, (void *adapter))


_HIM_INTERFACE(HPT_BOOL, adapter_on_same_vbus, (void *adapter1, void *adapter2))
_HIM_INTERFACE(void, route_irq, (void *adapter, HPT_BOOL enable))


_HIM_INTERFACE(HPT_BOOL, initialize, (void *adapter))


_HIM_INTERFACE(HPT_UINT, get_device_size, (void *adapter))


_HIM_INTERFACE(HPT_BOOL, probe_device, (void *adapter, int index, void *devhandle, PROBE_CALLBACK done, void *arg))
_HIM_INTERFACE(void *, get_device, (void *adapter, int index))
_HIM_INTERFACE(void, get_device_config, (void *dev, HIM_DEVICE_CONFIG *config))
_HIM_INTERFACE(void, remove_device, (void *dev))

_HIM_INTERFACE(void, reset_device, (void * dev, void (*done)(void *arg), void *arg))


_HIM_INTERFACE(HPT_U32, get_cmdext_size, (void))

_HIM_INTERFACE(void, queue_cmd, (void *dev, struct _COMMAND *cmd))


_HIM_INTERFACE(int, read_write, (void *dev,HPT_LBA lba, HPT_U16 nsector, HPT_U8 *buffer, HPT_BOOL read))

_HIM_INTERFACE(HPT_BOOL, intr_handler, (void *adapter))
_HIM_INTERFACE(HPT_BOOL, intr_control, (void * adapter, HPT_BOOL enable))


_HIM_INTERFACE(int, get_channel_config, (void * adapter, int index, PHIM_CHANNEL_CONFIG pInfo))
_HIM_INTERFACE(int, set_device_info, (void * dev, PHIM_ALTERABLE_DEV_INFO pInfo))
_HIM_INTERFACE(void, unplug_device, (void * dev))


_HIM_INTERFACE(void, shutdown, (void *adapter))
_HIM_INTERFACE(void, suspend, (void *adapter))
_HIM_INTERFACE(void, resume, (void *adapter))
_HIM_INTERFACE(void, release_adapter, (void *adapter))

/*called after ldm_register_adapter*/
_HIM_INTERFACE(HPT_BOOL, verify_adapter, (void *adapter))

/* (optional) */
_HIM_INTERFACE(void, ioctl, (void * adapter, struct _IOCTL_ARG *arg))
_HIM_INTERFACE(int, compare_slot_seq, (void *adapter1, void *adapter2))
_HIM_INTERFACE(int, get_enclosure_count, (void *adapter))
_HIM_INTERFACE(int, get_enclosure_info, (void *adapter, int id, void *pinfo))
_HIM_INTERFACE(int, get_enclosure_info_v2, (void *adapter, int id, void *pinfo))
_HIM_INTERFACE(int, get_enclosure_info_v3, (void *adapter, int id, void *pinfo))
_HIM_INTERFACE(int, get_enclosure_info_v4, (void *adapter, int enc_id, int ele_id, void *pinfo, void *pstatus))

_HIM_INTERFACE(HPT_BOOL, flash_access, (void *adapter, HPT_U32 offset, void *value, int size, HPT_BOOL reading))

#undef _HIM_INTERFACE
