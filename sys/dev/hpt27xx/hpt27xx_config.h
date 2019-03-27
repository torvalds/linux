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

#ifndef hpt27xx_CONFIG_H
#define hpt27xx_CONFIG_H
#define SUPPORT_ARRAY
#define __KERNEL__ 1
#define DRIVER_MINOR 16
#define TARGETNAME hpt27xx
#define __dummy_reg hpt27xx___dummy_reg
#define __ldm_alloc_cmd hpt27xx___ldm_alloc_cmd
#define delay_between_spinup hpt27xx_delay_between_spinup
#define dmapool_active hpt27xx_dmapool_active
#define dmapool_get_page hpt27xx_dmapool_get_page
#define dmapool_get_page_at hpt27xx_dmapool_get_page_at
#define dmapool_init hpt27xx_dmapool_init
#define dmapool_make_order hpt27xx_dmapool_make_order
#define dmapool_max_class_pages hpt27xx_dmapool_max_class_pages
#define dmapool_put_page hpt27xx_dmapool_put_page
#define dmapool_register_client hpt27xx_dmapool_register_client
#define driver_name hpt27xx_driver_name
#define driver_name_long hpt27xx_driver_name_long
#define driver_ver hpt27xx_driver_ver
#define freelist_get hpt27xx_freelist_get
#define freelist_get_dma hpt27xx_freelist_get_dma
#define freelist_put hpt27xx_freelist_put
#define freelist_put_dma hpt27xx_freelist_put_dma
#define freelist_reserve hpt27xx_freelist_reserve
#define freelist_reserve_dma hpt27xx_freelist_reserve_dma
#define gGlobalNcqFlag hpt27xx_gGlobalNcqFlag
#define gProbeInInitializing hpt27xx_gProbeInInitializing
#define gSGPIOPartSupport hpt27xx_gSGPIOPartSupport
#define gSpinupOneDevEachTime hpt27xx_gSpinupOneDevEachTime
#define g_legacy_mode hpt27xx_g_legacy_mode
#define gautorebuild hpt27xx_gautorebuild
#define grebuildpriority hpt27xx_grebuildpriority
#define him_handle_to_vbus hpt27xx_him_handle_to_vbus
#define him_list hpt27xx_him_list
#define init_config hpt27xx_init_config
#define init_module_him_rr2720 hpt27xx_init_module_him_rr2720
#define init_module_him_rr273x hpt27xx_init_module_him_rr273x
#define init_module_him_rr276x hpt27xx_init_module_him_rr276x
#define init_module_him_rr278x hpt27xx_init_module_him_rr278x
#define init_module_jbod hpt27xx_init_module_jbod
#define init_module_partition hpt27xx_init_module_partition
#define init_module_raid0 hpt27xx_init_module_raid0
#define init_module_raid1 hpt27xx_init_module_raid1
#define init_module_raid5 hpt27xx_init_module_raid5
#define init_module_vdev_raw hpt27xx_init_module_vdev_raw
#define ldm_acquire_lock hpt27xx_ldm_acquire_lock
#define ldm_add_spare_to_array hpt27xx_ldm_add_spare_to_array
#define ldm_alloc_cmds_R_6_55_75_46_64 hpt27xx_ldm_alloc_cmds_R_6_55_75_46_64
#define ldm_alloc_cmds_from_list hpt27xx_ldm_alloc_cmds_from_list
#define ldm_check_array_online hpt27xx_ldm_check_array_online
#define ldm_create_vbus hpt27xx_ldm_create_vbus
#define ldm_create_vdev hpt27xx_ldm_create_vdev
#define ldm_event_notify hpt27xx_ldm_event_notify
#define ldm_find_stamp hpt27xx_ldm_find_stamp
#define ldm_find_target hpt27xx_ldm_find_target
#define ldm_finish_cmd hpt27xx_ldm_finish_cmd
#define ldm_free_cmds hpt27xx_ldm_free_cmds
#define ldm_free_cmds_to_list hpt27xx_ldm_free_cmds_to_list
#define ldm_generic_member_failed hpt27xx_ldm_generic_member_failed
#define ldm_get_cmd_size hpt27xx_ldm_get_cmd_size
#define ldm_get_device_id hpt27xx_ldm_get_device_id
#define ldm_get_mem_info hpt27xx_ldm_get_mem_info
#define ldm_get_next_vbus hpt27xx_ldm_get_next_vbus
#define ldm_get_vbus_ext hpt27xx_ldm_get_vbus_ext
#define ldm_get_vbus_size hpt27xx_ldm_get_vbus_size
#define ldm_ide_fixstring hpt27xx_ldm_ide_fixstring
#define ldm_idle hpt27xx_ldm_idle
#define ldm_initialize_vbus_async hpt27xx_ldm_initialize_vbus_async
#define ldm_intr hpt27xx_ldm_intr
#define ldm_ioctl hpt27xx_ldm_ioctl
#define ldm_on_timer hpt27xx_ldm_on_timer
#define ldm_queue_cmd hpt27xx_ldm_queue_cmd
#define ldm_queue_task hpt27xx_ldm_queue_task
#define ldm_queue_vbus_dpc hpt27xx_ldm_queue_vbus_dpc
#define ldm_register_adapter hpt27xx_ldm_register_adapter
#define ldm_register_device hpt27xx_ldm_register_device
#define ldm_register_him_R_6_55_75_46_64 hpt27xx_ldm_register_him_R_6_55_75_46_64
#define ldm_register_vdev_class_R_6_55_75_46_64 hpt27xx_ldm_register_vdev_class_R_6_55_75_46_64
#define ldm_release_lock hpt27xx_ldm_release_lock
#define ldm_release_vbus hpt27xx_ldm_release_vbus
#define ldm_release_vdev hpt27xx_ldm_release_vdev
#define ldm_remove_timer hpt27xx_ldm_remove_timer
#define ldm_request_timer hpt27xx_ldm_request_timer
#define ldm_reset_vbus hpt27xx_ldm_reset_vbus
#define ldm_resume hpt27xx_ldm_resume
#define ldm_run hpt27xx_ldm_run
#define ldm_set_autorebuild hpt27xx_ldm_set_autorebuild
#define ldm_shutdown hpt27xx_ldm_shutdown
#define ldm_suspend hpt27xx_ldm_suspend
#define ldm_sync_array_info hpt27xx_ldm_sync_array_info
#define ldm_sync_array_stamp hpt27xx_ldm_sync_array_stamp
#define ldm_timer_probe_device hpt27xx_ldm_timer_probe_device
#define ldm_unregister_device hpt27xx_ldm_unregister_device
#define log_sector_repair hpt27xx_log_sector_repair
#define num_drives_per_spinup hpt27xx_num_drives_per_spinup
#define os_get_stamp hpt27xx_os_get_stamp
#define os_get_vbus_seq hpt27xx_os_get_vbus_seq
#define os_inb hpt27xx_os_inb
#define os_inl hpt27xx_os_inl
#define os_insw hpt27xx_os_insw
#define os_inw hpt27xx_os_inw
#define os_map_pci_bar hpt27xx_os_map_pci_bar
#define os_max_cache_size hpt27xx_os_max_cache_size
#define os_outb hpt27xx_os_outb
#define os_outl hpt27xx_os_outl
#define os_outsw hpt27xx_os_outsw
#define os_outw hpt27xx_os_outw
#define os_pci_readb hpt27xx_os_pci_readb
#define os_pci_readl hpt27xx_os_pci_readl
#define os_pci_readw hpt27xx_os_pci_readw
#define os_pci_writeb hpt27xx_os_pci_writeb
#define os_pci_writel hpt27xx_os_pci_writel
#define os_pci_writew hpt27xx_os_pci_writew
#define os_printk hpt27xx_os_printk
#define os_query_remove_device hpt27xx_os_query_remove_device
#define os_query_time hpt27xx_os_query_time
#define os_request_timer hpt27xx_os_request_timer
#define os_revalidate_device hpt27xx_os_revalidate_device
#define os_schedule_task hpt27xx_os_schedule_task
#define os_stallexec hpt27xx_os_stallexec
#define os_unmap_pci_bar hpt27xx_os_unmap_pci_bar
#define osm_max_targets hpt27xx_osm_max_targets
#define pcicfg_read_byte hpt27xx_pcicfg_read_byte
#define pcicfg_read_dword hpt27xx_pcicfg_read_dword
#define vbus_list hpt27xx_vbus_list
#define vdev_queue_cmd hpt27xx_vdev_queue_cmd
#define get_dmapool_phy_addr hpt27xx_get_dmapool_phy_addr
#endif
