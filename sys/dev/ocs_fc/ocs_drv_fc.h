/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * OCS linux driver common include file
 */


#if !defined(__OCS_DRV_FC_H__)
#define __OCS_DRV_FC_H__

#define OCS_INCLUDE_FC

#include "ocs_os.h"
#include "ocs_debug.h"
#include "ocs_common.h"
#include "ocs_hw.h"
#include "ocs_io.h"
#include "ocs_pm.h"
#include "ocs_xport.h"
#include "ocs_stats.h"

struct ocs_s {

	ocs_os_t ocs_os;
	char display_name[OCS_DISPLAY_NAME_LENGTH];
	ocs_rlock_t lock;			/*>> Device wide lock */
	ocs_list_t domain_list;			/*>> linked list of virtual fabric objects */
	ocs_io_pool_t *io_pool;			/**< pointer to IO pool */
	ocs_ramlog_t *ramlog;
	ocs_drv_t drv_ocs;
	ocs_scsi_tgt_t tgt_ocs;
	ocs_scsi_ini_t ini_ocs;
	ocs_xport_e ocs_xport;
	ocs_xport_t *xport;			/*>> Pointer to transport object */
	bool enable_ini;
	bool enable_tgt;
	uint8_t fc_type;
	int ctrlmask;
	int logmask;
	uint32_t max_isr_time_msec;		/*>> Maximum ISR time */
	char *hw_war_version;
	ocs_pm_context_t pm_context;		/*<< power management context */
	ocs_mgmt_functions_t *mgmt_functions;
	ocs_mgmt_functions_t *tgt_mgmt_functions;
	ocs_mgmt_functions_t *ini_mgmt_functions;
	ocs_err_injection_e err_injection;	/**< for error injection testing */
	uint32_t cmd_err_inject;		/**< specific cmd to inject error into */
	time_t delay_value_msec;		/**< for injecting delays */

	const char *desc;
	uint32_t instance_index;
	uint16_t pci_vendor;
	uint16_t pci_device;
	uint16_t pci_subsystem_vendor;
	uint16_t pci_subsystem_device;
	char businfo[OCS_DISPLAY_BUS_INFO_LENGTH];

	const char *model;
	const char *driver_version;
	const char *fw_version;

	ocs_hw_t hw;

	ocs_domain_t *domain;			/*>> pointer to first (physical) domain (also on domain_list) */
	uint32_t domain_instance_count;			/*>> domain instance count */
	void (*domain_list_empty_cb)(ocs_t *ocs, void *arg); /*>> domain list empty callback */
	void *domain_list_empty_cb_arg;                 /*>> domain list empty callback argument */

	bool explicit_buffer_list;
	bool external_loopback;
	uint32_t num_vports;
	uint32_t hw_bounce;
	uint32_t rq_threads;
	uint32_t rq_selection_policy;
	uint32_t rr_quanta;
	char *filter_def;
	uint32_t max_remote_nodes;

	bool soft_wwn_enable;

	/*
	 * tgt_rscn_delay - delay in kicking off RSCN processing (nameserver queries)
	 * after receiving an RSCN on the target. This prevents thrashing of nameserver
	 * requests due to a huge burst of RSCNs received in a short period of time
	 * Note: this is only valid when target RSCN handling is enabled -- see ctrlmask.
	 */
	time_t tgt_rscn_delay_msec;		/*>> minimum target RSCN delay */

	/*
	 * tgt_rscn_period - determines maximum frequency when processing back-to-back
	 * RSCNs; e.g. if this value is 30, there will never be any more than 1 RSCN
	 * handling per 30s window. This prevents initiators on a faulty link generating
	 * many RSCN from causing the target to continually query the nameserver. Note:
	 * this is only valid when target RSCN handling is enabled
	 */
	time_t tgt_rscn_period_msec;		/*>> minimum target RSCN period */

	/*
	 * Target IO timer value:
	 * Zero: target command timeout disabled.
	 * Non-zero: Timeout value, in seconds, for target commands
	 */
	uint32_t target_io_timer_sec;

	int speed;
	int topology;
	int ethernet_license;
	int num_scsi_ios;
	bool enable_hlm;			/*>> high login mode is enabled */
	uint32_t hlm_group_size;		/*>> RPI count for high login mode */
	char *wwn_bump;
	uint32_t nodedb_mask;			/*>> Node debugging mask */

	uint32_t auto_xfer_rdy_size;		/*>> Maximum sized write to use auto xfer rdy */
        bool  esoc;
	uint8_t ocs_req_fw_upgrade;

	ocs_textbuf_t ddump_saved;

};

#define ocs_is_fc_initiator_enabled()	(ocs->enable_ini)
#define ocs_is_fc_target_enabled()	(ocs->enable_tgt)

static inline void
ocs_device_lock_init(ocs_t *ocs)
{
	ocs_rlock_init(ocs, &ocs->lock, "ocsdevicelock");
}
static inline void
ocs_device_lock_free(ocs_t *ocs)
{
	ocs_rlock_free(&ocs->lock);
}
static inline int32_t
ocs_device_lock_try(ocs_t *ocs)
{
	return ocs_rlock_try(&ocs->lock);
}
static inline void
ocs_device_lock(ocs_t *ocs)
{
	ocs_rlock_acquire(&ocs->lock);
}
static inline void
ocs_device_unlock(ocs_t *ocs)
{
	ocs_rlock_release(&ocs->lock);
}

extern ocs_t *ocs_get_instance(uint32_t index);
extern int32_t ocs_get_bus_dev_func(ocs_t *ocs, uint8_t* bus, uint8_t* dev, uint8_t* func);

static inline ocs_io_t *
ocs_io_alloc(ocs_t *ocs)
{
	return ocs_io_pool_io_alloc(ocs->xport->io_pool);
}

static inline void
ocs_io_free(ocs_t *ocs, ocs_io_t *io)
{
	ocs_io_pool_io_free(ocs->xport->io_pool, io);
}

extern void ocs_stop_event_processing(ocs_os_t *ocs_os);
extern int32_t ocs_start_event_processing(ocs_os_t *ocs_os);

#include "ocs_domain.h"
#include "ocs_sport.h"
#include "ocs_node.h"
#include "ocs_io.h"
#include "ocs_unsol.h"
#include "ocs_scsi.h"

#include "ocs_ioctl.h"
#include "ocs_elxu.h"


#endif 
