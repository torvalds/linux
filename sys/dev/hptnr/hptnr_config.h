/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 HighPoint Technologies, Inc.
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

#ifndef hptnr_CONFIG_H
#define hptnr_CONFIG_H
#define SUPPORT_ARRAY
#define __KERNEL__ 1
#define DRIVER_MINOR 16
#define TARGETNAME hptnr
#define __dummy_reg hptnr___dummy_reg
#define __ldm_alloc_cmd hptnr___ldm_alloc_cmd
#define debug_flag hptnr_debug_flag
#define delay_between_spinup hptnr_delay_between_spinup
#define dmapool_active hptnr_dmapool_active
#define dmapool_get_page hptnr_dmapool_get_page
#define dmapool_get_page_at hptnr_dmapool_get_page_at
#define dmapool_init hptnr_dmapool_init
#define dmapool_make_order hptnr_dmapool_make_order
#define dmapool_max_class_pages hptnr_dmapool_max_class_pages
#define dmapool_put_page hptnr_dmapool_put_page
#define dmapool_register_client hptnr_dmapool_register_client
#define driver_name hptnr_driver_name
#define driver_name_long hptnr_driver_name_long
#define driver_ver hptnr_driver_ver
#define freelist_get hptnr_freelist_get
#define freelist_get_dma hptnr_freelist_get_dma
#define freelist_put hptnr_freelist_put
#define freelist_put_dma hptnr_freelist_put_dma
#define freelist_reserve hptnr_freelist_reserve
#define freelist_reserve_dma hptnr_freelist_reserve_dma
#define gGlobalNcqFlag hptnr_gGlobalNcqFlag
#define gProbeInInitializing hptnr_gProbeInInitializing
#define gSGPIOPartSupport hptnr_gSGPIOPartSupport
#define gSpinupOneDevEachTime hptnr_gSpinupOneDevEachTime
#define g_legacy_mode hptnr_g_legacy_mode
#define gautorebuild hptnr_gautorebuild
#define grebuildpriority hptnr_grebuildpriority
#define him_handle_to_vbus hptnr_him_handle_to_vbus
#define him_list hptnr_him_list
#define init_config hptnr_init_config
#define init_module_him_dc7280 hptnr_init_module_him_dc7280
#define init_module_him_r750 hptnr_init_module_him_r750
#define init_module_vdev_raw hptnr_init_module_vdev_raw
#define ldm_acquire_lock hptnr_ldm_acquire_lock
#define ldm_add_spare_to_array hptnr_ldm_add_spare_to_array
#define ldm_alloc_cmds_R_6_55_75_46_64 hptnr_ldm_alloc_cmds_R_6_55_75_46_64
#define ldm_alloc_cmds_from_list hptnr_ldm_alloc_cmds_from_list
#define ldm_check_array_online hptnr_ldm_check_array_online
#define ldm_create_vbus hptnr_ldm_create_vbus
#define ldm_create_vdev hptnr_ldm_create_vdev
#define ldm_event_notify hptnr_ldm_event_notify
#define ldm_find_stamp hptnr_ldm_find_stamp
#define ldm_find_target hptnr_ldm_find_target
#define ldm_finish_cmd hptnr_ldm_finish_cmd
#define ldm_free_cmds hptnr_ldm_free_cmds
#define ldm_free_cmds_to_list hptnr_ldm_free_cmds_to_list
#define ldm_generic_member_failed hptnr_ldm_generic_member_failed
#define ldm_get_cmd_size hptnr_ldm_get_cmd_size
#define ldm_get_device_id hptnr_ldm_get_device_id
#define ldm_get_mem_info hptnr_ldm_get_mem_info
#define ldm_get_next_vbus hptnr_ldm_get_next_vbus
#define ldm_get_vbus_ext hptnr_ldm_get_vbus_ext
#define ldm_get_vbus_size hptnr_ldm_get_vbus_size
#define ldm_ide_fixstring hptnr_ldm_ide_fixstring
#define ldm_idle hptnr_ldm_idle
#define ldm_initialize_vbus_async hptnr_ldm_initialize_vbus_async
#define ldm_intr hptnr_ldm_intr
#define ldm_ioctl hptnr_ldm_ioctl
#define ldm_on_timer hptnr_ldm_on_timer
#define ldm_queue_cmd hptnr_ldm_queue_cmd
#define ldm_queue_task hptnr_ldm_queue_task
#define ldm_queue_vbus_dpc hptnr_ldm_queue_vbus_dpc
#define ldm_register_adapter hptnr_ldm_register_adapter
#define ldm_register_device hptnr_ldm_register_device
#define ldm_register_him_R_6_55_75_46_64 hptnr_ldm_register_him_R_6_55_75_46_64
#define ldm_register_vdev_class_R_6_55_75_46_64 hptnr_ldm_register_vdev_class_R_6_55_75_46_64
#define ldm_release_lock hptnr_ldm_release_lock
#define ldm_release_vbus hptnr_ldm_release_vbus
#define ldm_release_vdev hptnr_ldm_release_vdev
#define ldm_remove_timer hptnr_ldm_remove_timer
#define ldm_request_timer hptnr_ldm_request_timer
#define ldm_reset_vbus hptnr_ldm_reset_vbus
#define ldm_resume hptnr_ldm_resume
#define ldm_run hptnr_ldm_run
#define ldm_set_autorebuild hptnr_ldm_set_autorebuild
#define ldm_shutdown hptnr_ldm_shutdown
#define ldm_suspend hptnr_ldm_suspend
#define ldm_sync_array_info hptnr_ldm_sync_array_info
#define ldm_sync_array_stamp hptnr_ldm_sync_array_stamp
#define ldm_timer_probe_device hptnr_ldm_timer_probe_device
#define ldm_unregister_device hptnr_ldm_unregister_device
#define log_sector_repair hptnr_log_sector_repair
#define msi hptnr_msi
#define num_drives_per_spinup hptnr_num_drives_per_spinup
#define os_get_stamp hptnr_os_get_stamp
#define os_get_vbus_seq hptnr_os_get_vbus_seq
#define os_inb hptnr_os_inb
#define os_inl hptnr_os_inl
#define os_insw hptnr_os_insw
#define os_inw hptnr_os_inw
#define os_map_pci_bar hptnr_os_map_pci_bar
#define os_max_cache_size hptnr_os_max_cache_size
#define os_outb hptnr_os_outb
#define os_outl hptnr_os_outl
#define os_outsw hptnr_os_outsw
#define os_outw hptnr_os_outw
#define os_pci_readb hptnr_os_pci_readb
#define os_pci_readl hptnr_os_pci_readl
#define os_pci_readw hptnr_os_pci_readw
#define os_pci_writeb hptnr_os_pci_writeb
#define os_pci_writel hptnr_os_pci_writel
#define os_pci_writew hptnr_os_pci_writew
#define os_printk hptnr_os_printk
#define os_query_remove_device hptnr_os_query_remove_device
#define os_query_time hptnr_os_query_time
#define os_request_timer hptnr_os_request_timer
#define os_revalidate_device hptnr_os_revalidate_device
#define os_schedule_task hptnr_os_schedule_task
#define os_stallexec hptnr_os_stallexec
#define os_unmap_pci_bar hptnr_os_unmap_pci_bar
#define osm_max_targets hptnr_osm_max_targets
#define pcicfg_read_dword hptnr_pcicfg_read_dword
#define vbus_list hptnr_vbus_list
#define vdev_queue_cmd hptnr_vdev_queue_cmd
#endif
