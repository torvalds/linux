/*-
 * Copyright (c) 2018 Microsemi Corporation.
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
 */

/* $FreeBSD$ */

#ifndef _PQI_PROTOTYPES_H
#define _PQI_PROTOTYPES_H

/* Function prototypes */

/*pqi_init.c */
int pqisrc_init(pqisrc_softstate_t *);
void pqisrc_uninit(pqisrc_softstate_t *);
void pqisrc_pqi_uninit(pqisrc_softstate_t *);
int pqisrc_process_config_table(pqisrc_softstate_t *);
int pqisrc_flush_cache(pqisrc_softstate_t *, enum pqisrc_flush_cache_event_type);
int pqisrc_wait_for_pqi_reset_completion(pqisrc_softstate_t *);
int pqisrc_wait_for_cmnd_complete(pqisrc_softstate_t *);
void pqisrc_complete_internal_cmds(pqisrc_softstate_t *);

/* pqi_sis.c*/
int pqisrc_sis_init(pqisrc_softstate_t *);
void pqisrc_sis_uninit(pqisrc_softstate_t *);
int pqisrc_reenable_sis(pqisrc_softstate_t *);
void pqisrc_trigger_nmi_sis(pqisrc_softstate_t *);
void sis_disable_msix(pqisrc_softstate_t *);
void sis_enable_intx(pqisrc_softstate_t *);
void sis_disable_intx(pqisrc_softstate_t *softs);
int pqisrc_force_sis(pqisrc_softstate_t *);
int pqisrc_sis_wait_for_db_bit_to_clear(pqisrc_softstate_t *, uint32_t);
void sis_disable_interrupt(pqisrc_softstate_t*);

/* pqi_queue.c */
int pqisrc_submit_admin_req(pqisrc_softstate_t *,
			    gen_adm_req_iu_t *, gen_adm_resp_iu_t *);
int pqisrc_create_admin_queue(pqisrc_softstate_t *);
int pqisrc_destroy_admin_queue(pqisrc_softstate_t *);
int pqisrc_create_op_queues(pqisrc_softstate_t *);

/* pqi_cmd.c */
int pqisrc_submit_cmnd(pqisrc_softstate_t *,ib_queue_t *,void *);

/* pqi_tag.c */
#ifndef LOCKFREE_STACK
int pqisrc_init_taglist(pqisrc_softstate_t *,pqi_taglist_t *,uint32_t);
void pqisrc_destroy_taglist(pqisrc_softstate_t *,pqi_taglist_t *);
void pqisrc_put_tag(pqi_taglist_t *,uint32_t);
uint32_t pqisrc_get_tag(pqi_taglist_t *);
#else
int pqisrc_init_taglist(pqisrc_softstate_t *, lockless_stack_t *, uint32_t);
void pqisrc_destroy_taglist(pqisrc_softstate_t *, lockless_stack_t *);
void pqisrc_put_tag(lockless_stack_t *,uint32_t);
uint32_t pqisrc_get_tag(lockless_stack_t *);
#endif /* LOCKFREE_STACK */

/* pqi_discovery.c */
void pqisrc_remove_device(pqisrc_softstate_t *, pqi_scsi_dev_t *);
int pqisrc_get_ctrl_fw_version(pqisrc_softstate_t *);
int pqisrc_rescan_devices(pqisrc_softstate_t *);
int pqisrc_scan_devices(pqisrc_softstate_t *);
void pqisrc_process_raid_path_io_response(pqisrc_softstate_t *, uint16_t, struct pqi_io_response *);
void pqisrc_process_io_error_response(pqisrc_softstate_t *, int, uint16_t, struct pqi_io_response *);
void pqisrc_cleanup_devices(pqisrc_softstate_t *);
void pqisrc_device_mem_free(pqisrc_softstate_t *, pqi_scsi_dev_t *);
boolean_t pqisrc_is_external_raid_device(pqi_scsi_dev_t *device);
void pqisrc_free_device(pqisrc_softstate_t * softs,pqi_scsi_dev_t *device);
void pqisrc_init_targetid_pool(pqisrc_softstate_t *softs);
int pqisrc_alloc_tid(pqisrc_softstate_t *softs);
void pqisrc_free_tid(pqisrc_softstate_t *softs, int);

/* pqi_helper.c */
boolean_t pqisrc_ctrl_offline(pqisrc_softstate_t *);
void pqisrc_heartbeat_timer_handler(pqisrc_softstate_t *);
int pqisrc_wait_on_condition(pqisrc_softstate_t *, rcb_t *);
boolean_t pqisrc_device_equal(pqi_scsi_dev_t *, pqi_scsi_dev_t *);
boolean_t pqisrc_is_hba_lunid(uint8_t *);
boolean_t pqisrc_is_logical_device(pqi_scsi_dev_t *);
void pqisrc_sanitize_inquiry_string(unsigned char *, int );
void pqisrc_display_device_info(pqisrc_softstate_t *, char *, pqi_scsi_dev_t *);
boolean_t pqisrc_scsi3addr_equal(uint8_t *, uint8_t *);
void check_struct_sizes(void);
char *pqisrc_raidlevel_to_string(uint8_t);
void pqisrc_configure_legacy_intx(pqisrc_softstate_t*, boolean_t);

/* pqi_response.c */
void pqisrc_signal_event(pqisrc_softstate_t *softs, rcb_t *rcb);
void pqisrc_process_internal_raid_response_success(pqisrc_softstate_t *,
                                          rcb_t *);
void pqisrc_process_internal_raid_response_error(pqisrc_softstate_t *,
                                          rcb_t *, uint16_t);
void pqisrc_process_io_response_success(pqisrc_softstate_t *,
		rcb_t *);
void pqisrc_process_aio_response_error(pqisrc_softstate_t *,
		rcb_t *, uint16_t);
void pqisrc_process_raid_response_error(pqisrc_softstate_t *,
		rcb_t *, uint16_t);
void pqisrc_process_response_queue(pqisrc_softstate_t *, int);


/* pqi_request.c */
int pqisrc_build_send_io(pqisrc_softstate_t *,rcb_t *);

	
int pqisrc_send_scsi_cmd_raidbypass(pqisrc_softstate_t *softs,
				pqi_scsi_dev_t *device, rcb_t *rcb, uint8_t*);
	

int pqisrc_send_tmf(pqisrc_softstate_t *, pqi_scsi_dev_t *,
                    rcb_t *, int, int);
int pqisrc_write_current_time_to_host_wellness(pqisrc_softstate_t *softs);
int pqisrc_write_driver_version_to_host_wellness(pqisrc_softstate_t *softs);

/* pqi_event.c*/
int pqisrc_report_event_config(pqisrc_softstate_t *);
int pqisrc_set_event_config(pqisrc_softstate_t *);
int pqisrc_process_event_intr_src(pqisrc_softstate_t *,int);
void pqisrc_ack_all_events(void *arg);


void pqisrc_event_worker(void *, int);
int pqisrc_scsi_setup(struct pqisrc_softstate *);
void pqisrc_scsi_cleanup(struct pqisrc_softstate *);
boolean_t pqisrc_update_scsi_sense(const uint8_t *, int,
                              struct sense_header_scsi *);
int pqisrc_build_send_raid_request(pqisrc_softstate_t *,  pqisrc_raid_req_t *,
                            void *, size_t, uint8_t, uint16_t, uint8_t *,
                            raid_path_error_info_elem_t *);

int pqisrc_submit_management_req(pqisrc_softstate_t *,
                        pqi_event_config_request_t *);
void pqisrc_take_devices_offline(pqisrc_softstate_t *);
void pqisrc_take_ctrl_offline(pqisrc_softstate_t *);
void pqisrc_free_rcb(pqisrc_softstate_t *, int);
void pqisrc_decide_opq_config(pqisrc_softstate_t *);
int pqisrc_configure_op_queues(pqisrc_softstate_t *);
int pqisrc_pqi_init(pqisrc_softstate_t *);
int pqi_reset(pqisrc_softstate_t *);
int pqisrc_check_pqimode(pqisrc_softstate_t *);
int pqisrc_check_fw_status(pqisrc_softstate_t *);
int pqisrc_init_struct_base(pqisrc_softstate_t *);
int pqisrc_get_sis_pqi_cap(pqisrc_softstate_t *);
int pqisrc_get_preferred_settings(pqisrc_softstate_t *);
int pqisrc_get_adapter_properties(pqisrc_softstate_t *,
                                uint32_t *, uint32_t *);

void pqisrc_get_admin_queue_config(pqisrc_softstate_t *);
void pqisrc_decide_admin_queue_config(pqisrc_softstate_t *);
int pqisrc_allocate_and_init_adminq(pqisrc_softstate_t *);
int pqisrc_create_delete_adminq(pqisrc_softstate_t *, uint32_t);
void pqisrc_print_adminq_config(pqisrc_softstate_t *);
int pqisrc_delete_op_queue(pqisrc_softstate_t *,
                                uint32_t, boolean_t);
void pqisrc_destroy_event_queue(pqisrc_softstate_t *);

void pqisrc_destroy_op_ib_queues(pqisrc_softstate_t *);

void pqisrc_destroy_op_ob_queues(pqisrc_softstate_t *);

int pqisrc_change_op_ibq_queue_prop(pqisrc_softstate_t *,
                                ib_queue_t *, uint32_t);
int pqisrc_create_op_obq(pqisrc_softstate_t *,
                        ob_queue_t *);
int pqisrc_create_op_ibq(pqisrc_softstate_t *,
                        ib_queue_t *);
int pqisrc_create_op_aio_ibq(pqisrc_softstate_t *, ib_queue_t *);
int pqisrc_create_op_raid_ibq(pqisrc_softstate_t *, ib_queue_t *);
int pqisrc_alloc_and_create_event_queue(pqisrc_softstate_t *);
int pqisrc_alloc_and_create_ib_queues(pqisrc_softstate_t *);
int pqisrc_alloc_and_create_ob_queues(pqisrc_softstate_t *);
int pqisrc_process_task_management_response(pqisrc_softstate_t *,
                                pqi_tmf_resp_t *);
void pqisrc_wait_for_rescan_complete(pqisrc_softstate_t *softs);


/* pqi_ioctl.c*/

int
pqisrc_passthru_ioctl(struct pqisrc_softstate *, void *, int);


/* Functions Prototypes */
/* FreeBSD_mem.c */
int os_dma_mem_alloc(pqisrc_softstate_t *,struct dma_mem *);
void os_dma_mem_free(pqisrc_softstate_t *,struct dma_mem *);
void *os_mem_alloc(pqisrc_softstate_t *,size_t);
void os_mem_free(pqisrc_softstate_t *,char *,size_t);
void os_resource_free(pqisrc_softstate_t *);

/* FreeBSD intr.c */
int os_get_intr_config(pqisrc_softstate_t *);
int os_setup_intr(pqisrc_softstate_t *);
int os_destroy_intr(pqisrc_softstate_t *);
int os_get_processor_config(pqisrc_softstate_t *);
void os_free_intr_config(pqisrc_softstate_t *);

/* FreeBSD_ioctl.c */
int os_copy_to_user(struct pqisrc_softstate *, void *,
                void *, int, int);
int os_copy_from_user(struct pqisrc_softstate *, void *,
                void *, int, int);
int create_char_dev(struct pqisrc_softstate *, int);
void destroy_char_dev(struct pqisrc_softstate *);
 
/* FreeBSD_misc.c*/ 
int os_init_spinlock(struct pqisrc_softstate *, struct mtx *, char *);
void os_uninit_spinlock(struct mtx *);
int os_create_semaphore(const char *, int,struct sema *);
int os_destroy_semaphore(struct sema *);
void os_sema_lock(struct sema *);
void os_sema_unlock(struct sema *);

int os_strlcpy(char *dst, char *src, int len);
void os_complete_outstanding_cmds_nodevice(pqisrc_softstate_t *);
void os_stop_heartbeat_timer(pqisrc_softstate_t *);
void os_start_heartbeat_timer(void *);

/* FreeBSD_cam.c */
uint8_t os_get_task_attr(rcb_t *);
void os_wellness_periodic(void *);
void smartpqi_target_rescan(struct pqisrc_softstate *);

/* FreeBSD_intr.c FreeBSD_main.c */
void os_add_device(pqisrc_softstate_t *, pqi_scsi_dev_t *);
void os_remove_device(pqisrc_softstate_t *, pqi_scsi_dev_t *); 
void os_io_response_success(rcb_t *);
void os_aio_response_error(rcb_t *, aio_path_error_info_elem_t *);
void smartpqi_adjust_queue_depth(struct cam_path *, uint32_t );
void os_raid_response_error(rcb_t *, raid_path_error_info_elem_t *);
void os_reset_rcb( rcb_t *);
int register_sim(struct pqisrc_softstate *, int);
void deregister_sim(struct pqisrc_softstate *);
int check_for_scsi_opcode(uint8_t *, boolean_t *, uint64_t *, 
			uint32_t *);
int register_legacy_intr(pqisrc_softstate_t *);
int register_msix_intr(pqisrc_softstate_t *);
void deregister_pqi_intx(pqisrc_softstate_t *);
void deregister_pqi_msix(pqisrc_softstate_t *);
void os_get_time(struct bmic_host_wellness_time *);
void os_eventtaskqueue_enqueue(pqisrc_softstate_t *);
void pqisrc_save_controller_info(struct pqisrc_softstate *);
int smartpqi_shutdown(void *);

#endif // _SMARTPQI_PROTOTYPES_H
