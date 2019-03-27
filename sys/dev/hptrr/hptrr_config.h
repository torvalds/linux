/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) HighPoint Technologies, Inc.
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
#ifndef hptrr_CONFIG_H
#define hptrr_CONFIG_H
#define SUPPORT_ARRAY
#define __KERNEL__ 1
#define DRIVER_MINOR 16
#define TARGETNAME hptrr
#define __dummy_reg hptrr___dummy_reg
#define __ldm_alloc_cmd hptrr___ldm_alloc_cmd
#define delay_between_spinup hptrr_delay_between_spinup
#define dmapool_active hptrr_dmapool_active
#define dmapool_get_page hptrr_dmapool_get_page
#define dmapool_get_page_at hptrr_dmapool_get_page_at
#define dmapool_make_order hptrr_dmapool_make_order
#define dmapool_max_class_pages hptrr_dmapool_max_class_pages
#define dmapool_put_page hptrr_dmapool_put_page
#define dmapool_register_client hptrr_dmapool_register_client
#define driver_name hptrr_driver_name
#define driver_name_long hptrr_driver_name_long
#define driver_ver hptrr_driver_ver
#define freelist_get hptrr_freelist_get
#define freelist_get_dma hptrr_freelist_get_dma
#define freelist_put hptrr_freelist_put
#define freelist_put_dma hptrr_freelist_put_dma
#define freelist_reserve hptrr_freelist_reserve
#define freelist_reserve_dma hptrr_freelist_reserve_dma
#define gautorebuild hptrr_gautorebuild
#define grebuildpriority hptrr_grebuildpriority
#define him_handle_to_vbus hptrr_him_handle_to_vbus
#define him_list hptrr_him_list
#define init_config hptrr_init_config
#define init_module_him_rr1720 hptrr_init_module_him_rr1720
#define init_module_him_rr174x_rr2210pm hptrr_init_module_him_rr174x_rr2210pm
#define init_module_him_rr222x_rr2240 hptrr_init_module_him_rr222x_rr2240
#define init_module_him_rr2310pm hptrr_init_module_him_rr2310pm
#define init_module_him_rr232x hptrr_init_module_him_rr232x
#define init_module_him_rr2340 hptrr_init_module_him_rr2340
#define init_module_him_rr2522pm hptrr_init_module_him_rr2522pm
#define init_module_jbod hptrr_init_module_jbod
#define init_module_partition hptrr_init_module_partition
#define init_module_raid0 hptrr_init_module_raid0
#define init_module_raid1 hptrr_init_module_raid1
#define init_module_raid5 hptrr_init_module_raid5
#define init_module_vdev_raw hptrr_init_module_vdev_raw
#define ldm_acquire_lock hptrr_ldm_acquire_lock
#define ldm_add_spare_to_array hptrr_ldm_add_spare_to_array
#define ldm_alloc_cmds_R_6_46_69_43_16 hptrr_ldm_alloc_cmds_R_6_46_69_43_16
#define ldm_alloc_cmds_from_list hptrr_ldm_alloc_cmds_from_list
#define ldm_check_array_online hptrr_ldm_check_array_online
#define ldm_create_vbus hptrr_ldm_create_vbus
#define ldm_create_vdev hptrr_ldm_create_vdev
#define ldm_event_notify hptrr_ldm_event_notify
#define ldm_find_stamp hptrr_ldm_find_stamp
#define ldm_find_target hptrr_ldm_find_target
#define ldm_finish_cmd hptrr_ldm_finish_cmd
#define ldm_free_cmds hptrr_ldm_free_cmds
#define ldm_free_cmds_to_list hptrr_ldm_free_cmds_to_list
#define ldm_generic_member_failed hptrr_ldm_generic_member_failed
#define ldm_get_cmd_size hptrr_ldm_get_cmd_size
#define ldm_get_device_id hptrr_ldm_get_device_id
#define ldm_get_mem_info hptrr_ldm_get_mem_info
#define ldm_get_next_vbus hptrr_ldm_get_next_vbus
#define ldm_get_vbus_ext hptrr_ldm_get_vbus_ext
#define ldm_get_vbus_size hptrr_ldm_get_vbus_size
#define ldm_ide_fixstring hptrr_ldm_ide_fixstring
#define ldm_idle hptrr_ldm_idle
#define ldm_initialize_vbus_async hptrr_ldm_initialize_vbus_async
#define ldm_intr hptrr_ldm_intr
#define ldm_ioctl hptrr_ldm_ioctl
#define ldm_on_timer hptrr_ldm_on_timer
#define ldm_queue_cmd hptrr_ldm_queue_cmd
#define ldm_queue_task hptrr_ldm_queue_task
#define ldm_queue_vbus_dpc hptrr_ldm_queue_vbus_dpc
#define ldm_register_adapter hptrr_ldm_register_adapter
#define ldm_register_device hptrr_ldm_register_device
#define ldm_register_him_R_6_46_69_43_16 hptrr_ldm_register_him_R_6_46_69_43_16
#define ldm_register_vdev_class_R_6_46_69_43_16 hptrr_ldm_register_vdev_class_R_6_46_69_43_16
#define ldm_release_lock hptrr_ldm_release_lock
#define ldm_release_vbus hptrr_ldm_release_vbus
#define ldm_release_vdev hptrr_ldm_release_vdev
#define ldm_remove_timer hptrr_ldm_remove_timer
#define ldm_request_timer hptrr_ldm_request_timer
#define ldm_reset_vbus hptrr_ldm_reset_vbus
#define ldm_resume hptrr_ldm_resume
#define ldm_set_autorebuild hptrr_ldm_set_autorebuild
#define ldm_set_rebuild_priority hptrr_ldm_set_rebuild_priority
#define ldm_shutdown hptrr_ldm_shutdown
#define ldm_suspend hptrr_ldm_suspend
#define ldm_sync_array_info hptrr_ldm_sync_array_info
#define ldm_sync_array_stamp hptrr_ldm_sync_array_stamp
#define ldm_timer_probe_device hptrr_ldm_timer_probe_device
#define ldm_unregister_device hptrr_ldm_unregister_device
#define log_sector_repair hptrr_log_sector_repair
#define num_drives_per_spinup hptrr_num_drives_per_spinup
#define os_get_stamp hptrr_os_get_stamp
#define os_get_vbus_seq hptrr_os_get_vbus_seq
#define os_inb hptrr_os_inb
#define os_inl hptrr_os_inl
#define os_insw hptrr_os_insw
#define os_inw hptrr_os_inw
#define os_map_pci_bar hptrr_os_map_pci_bar
#define os_max_cache_size hptrr_os_max_cache_size
#define os_outb hptrr_os_outb
#define os_outl hptrr_os_outl
#define os_outsw hptrr_os_outsw
#define os_outw hptrr_os_outw
#define os_pci_readb hptrr_os_pci_readb
#define os_pci_readl hptrr_os_pci_readl
#define os_pci_readw hptrr_os_pci_readw
#define os_pci_writeb hptrr_os_pci_writeb
#define os_pci_writel hptrr_os_pci_writel
#define os_pci_writew hptrr_os_pci_writew
#define os_printk hptrr_os_printk
#define os_query_remove_device hptrr_os_query_remove_device
#define os_query_time hptrr_os_query_time
#define os_request_timer hptrr_os_request_timer
#define os_revalidate_device hptrr_os_revalidate_device
#define os_schedule_task hptrr_os_schedule_task
#define os_stallexec hptrr_os_stallexec
#define os_unmap_pci_bar hptrr_os_unmap_pci_bar
#define osm_max_targets hptrr_osm_max_targets
#define vdev_queue_cmd hptrr_vdev_queue_cmd
#endif
