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
 * OCS bsd driver common include file
 */


#if !defined(__OCS_H__)
#define __OCS_H__

#include "ocs_os.h"
#include "ocs_utils.h"

#include "ocs_hw.h"
#include "ocs_scsi.h"
#include "ocs_io.h"

#include "version.h"

#define DRV_NAME			"ocs_fc"
#define DRV_VERSION 							\
	STR_BE_MAJOR "." STR_BE_MINOR "." STR_BE_BUILD "." STR_BE_BRANCH

/**
 * @brief Interrupt context
 */
typedef struct ocs_intr_ctx_s {
	uint32_t	vec;		/** Zero based interrupt vector */
	void		*softc;		/** software context for interrupt */
	char		name[64];	/** label for this context */
} ocs_intr_ctx_t;

typedef struct ocs_fc_rport_db_s {
	uint32_t	node_id;
	uint32_t	state;
	uint8_t		is_target;
	uint8_t		is_initiator;

	uint32_t	port_id;
	uint64_t	wwnn;
	uint64_t	wwpn;
	uint32_t	gone_timer;

} ocs_fc_target_t;

#define OCS_TGT_STATE_NONE		0	/* Empty DB slot */
#define OCS_TGT_STATE_VALID		1	/* Valid*/
#define OCS_TGT_STATE_LOST		2	/* LOST*/

typedef struct ocs_fcport_s {
	ocs_t			*ocs;
	struct cam_sim		*sim;
	struct cam_path		*path;
	uint32_t		role;
	uint32_t                fc_id;

	ocs_fc_target_t	tgt[OCS_MAX_TARGETS];
	int lost_device_time;
	struct callout ldt;     /* device lost timer */
	struct task ltask;

	ocs_tgt_resource_t	targ_rsrc_wildcard;
	ocs_tgt_resource_t	targ_rsrc[OCS_MAX_LUN];
	ocs_vport_spec_t	*vport;
} ocs_fcport;

#define FCPORT(ocs, chan)	(&((ocs_fcport *)(ocs)->fcports)[(chan)])

/**
 * @brief Driver's context
 */

struct ocs_softc {

	device_t		dev;
	struct cdev		*cdev;

	ocs_pci_reg_t		reg[PCI_MAX_BAR];

	uint32_t		instance_index;
	const char		*desc;

	uint32_t		irqid;
	struct resource		*irq;
	void			*tag;

	ocs_intr_ctx_t		intr_ctx;
	uint32_t		n_vec;

	bus_dma_tag_t		dmat;	/** Parent DMA tag */
	bus_dma_tag_t		buf_dmat;/** IO buffer DMA tag */
	char display_name[OCS_DISPLAY_NAME_LENGTH];
	uint16_t		pci_vendor;
	uint16_t		pci_device;
	uint16_t		pci_subsystem_vendor;
	uint16_t		pci_subsystem_device;
	char			businfo[16];
	const char		*driver_version;
	const char		*fw_version;
	const char		*model;

	ocs_hw_t hw;

	ocs_rlock_t lock;	/**< device wide lock */

	ocs_xport_e		ocs_xport;
	ocs_xport_t *xport;	/**< pointer to transport object */
	ocs_domain_t *domain;
	ocs_list_t domain_list;
	uint32_t domain_instance_count;
	void (*domain_list_empty_cb)(ocs_t *ocs, void *arg);		
	void *domain_list_empty_cb_arg;

	uint8_t enable_ini;
	uint8_t enable_tgt;
	uint8_t fc_type;
	int ctrlmask;
	uint8_t explicit_buffer_list;
	uint8_t external_loopback;
	uint8_t skip_hw_teardown;
	int speed;
	int topology;
	int ethernet_license;
	int num_scsi_ios;
	uint8_t enable_hlm;
	uint32_t hlm_group_size;
	uint32_t max_isr_time_msec;	/*>> Maximum ISR time */
	uint32_t auto_xfer_rdy_size; /*>> Max sized write to use auto xfer rdy*/
	uint8_t esoc;
	int logmask;
	char *hw_war_version;
	uint32_t num_vports;
	uint32_t target_io_timer_sec;
	uint32_t hw_bounce;
	uint8_t rq_threads;
	uint8_t rq_selection_policy;
	uint8_t rr_quanta;
	char *filter_def;
	uint32_t max_remote_nodes;

	/*
	 * tgt_rscn_delay - delay in kicking off RSCN processing 
	 * (nameserver queries) after receiving an RSCN on the target. 
	 * This prevents thrashing of nameserver requests due to a huge burst of
	 * RSCNs received in a short period of time.
	 * Note: this is only valid when target RSCN handling is enabled -- see 
	 * ctrlmask.
	 */
	time_t tgt_rscn_delay_msec;	/*>> minimum target RSCN delay */

	/*
	 * tgt_rscn_period - determines maximum frequency when processing 
	 * back-to-back RSCNs; e.g. if this value is 30, there will never be 
	 * any more than 1 RSCN handling per 30s window. This prevents 
	 * initiators on a faulty link generating many RSCN from causing the 
	 * target to continually query the nameserver. 
	 * Note: This is only valid when target RSCN handling is enabled
	 */
	time_t tgt_rscn_period_msec;	/*>> minimum target RSCN period */

	uint32_t		enable_task_set_full;		
	uint32_t		io_in_use;		
	uint32_t		io_high_watermark; /**< used to send task set full */
	struct mtx		sim_lock;
	uint32_t		config_tgt:1,	/**< Configured to support target mode */
				config_ini:1;	/**< Configured to support initiator mode */


	uint32_t nodedb_mask;			/**< Node debugging mask */

	char			modeldesc[64];
	char			serialnum[64];
	char			fwrev[64];
	char			sli_intf[9];

	ocs_ramlog_t		*ramlog;
	ocs_textbuf_t		ddump_saved;

	ocs_mgmt_functions_t	*mgmt_functions;
	ocs_mgmt_functions_t	*tgt_mgmt_functions;
	ocs_mgmt_functions_t	*ini_mgmt_functions;

	ocs_err_injection_e err_injection;
	uint32_t cmd_err_inject;
	time_t delay_value_msec;

	bool			attached;
	struct mtx		dbg_lock;
	
	struct cam_devq		*devq;
	ocs_fcport		*fcports;

	void*			tgt_ocs;
};

static inline void
ocs_device_lock_init(ocs_t *ocs)
{
	ocs_rlock_init(ocs, &ocs->lock, "ocsdevicelock");
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

static inline void
ocs_device_lock_free(ocs_t *ocs)
{
	ocs_rlock_free(&ocs->lock);
}

extern int32_t ocs_device_detach(ocs_t *ocs);

extern int32_t ocs_device_attach(ocs_t *ocs);

#define ocs_is_initiator_enabled()	(ocs->enable_ini)
#define ocs_is_target_enabled()	(ocs->enable_tgt)

#include "ocs_xport.h"
#include "ocs_domain.h"
#include "ocs_sport.h"
#include "ocs_node.h"
#include "ocs_unsol.h"
#include "ocs_scsi.h"
#include "ocs_ioctl.h"

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

#endif /* __OCS_H__ */
