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
 * FC transport API
 *
 */

#include "ocs.h"
#include "ocs_device.h"

static void ocs_xport_link_stats_cb(int32_t status, uint32_t num_counters, ocs_hw_link_stat_counts_t *counters, void *arg);
static void ocs_xport_host_stats_cb(int32_t status, uint32_t num_counters, ocs_hw_host_stat_counts_t *counters, void *arg);
/**
 * @brief Post node event callback argument.
 */
typedef struct {
	ocs_sem_t sem;
	ocs_node_t *node;
	ocs_sm_event_t evt;
	void *context;
} ocs_xport_post_node_event_t;

/**
 * @brief Allocate a transport object.
 *
 * @par Description
 * A transport object is allocated, and associated with a device instance.
 *
 * @param ocs Pointer to device instance.
 *
 * @return Returns the pointer to the allocated transport object, or NULL if failed.
 */
ocs_xport_t *
ocs_xport_alloc(ocs_t *ocs)
{
	ocs_xport_t *xport;

	ocs_assert(ocs, NULL);
	xport = ocs_malloc(ocs, sizeof(*xport), OCS_M_ZERO);
	if (xport != NULL) {
		xport->ocs = ocs;
	}
	return xport;
}

/**
 * @brief Create the RQ threads and the circular buffers used to pass sequences.
 *
 * @par Description
 * Creates the circular buffers and the servicing threads for RQ processing.
 *
 * @param xport Pointer to transport object
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static void
ocs_xport_rq_threads_teardown(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;
	uint32_t i;

	if (xport->num_rq_threads == 0 ||
	    xport->rq_thread_info == NULL) {
		return;
	}

	/* Abort any threads */
	for (i = 0; i < xport->num_rq_threads; i++) {
		if (xport->rq_thread_info[i].thread_started) {
			ocs_thread_terminate(&xport->rq_thread_info[i].thread);
			/* wait for the thread to exit */
			ocs_log_debug(ocs, "wait for thread %d to exit\n", i);
			while (xport->rq_thread_info[i].thread_started) {
				ocs_udelay(10000);
			}
			ocs_log_debug(ocs, "thread %d to exited\n", i);
		}
		if (xport->rq_thread_info[i].seq_cbuf != NULL) {
			ocs_cbuf_free(xport->rq_thread_info[i].seq_cbuf);
			xport->rq_thread_info[i].seq_cbuf = NULL;
		}
	}
}

/**
 * @brief Create the RQ threads and the circular buffers used to pass sequences.
 *
 * @par Description
 * Creates the circular buffers and the servicing threads for RQ processing.
 *
 * @param xport Pointer to transport object.
 * @param num_rq_threads Number of RQ processing threads that the
 * driver creates.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
ocs_xport_rq_threads_create(ocs_xport_t *xport, uint32_t num_rq_threads)
{
	ocs_t *ocs = xport->ocs;
	int32_t rc = 0;
	uint32_t i;

	xport->num_rq_threads = num_rq_threads;
	ocs_log_debug(ocs, "number of RQ threads %d\n", num_rq_threads);
	if (num_rq_threads == 0) {
		return 0;
	}

	/* Allocate the space for the thread objects */
	xport->rq_thread_info = ocs_malloc(ocs, sizeof(ocs_xport_rq_thread_info_t) * num_rq_threads, OCS_M_ZERO);
	if (xport->rq_thread_info == NULL) {
		ocs_log_err(ocs, "memory allocation failure\n");
		return -1;
	}

	/* Create the circular buffers and threads. */
	for (i = 0; i < num_rq_threads; i++) {
		xport->rq_thread_info[i].ocs = ocs;
		xport->rq_thread_info[i].seq_cbuf = ocs_cbuf_alloc(ocs, OCS_HW_RQ_NUM_HDR);
		if (xport->rq_thread_info[i].seq_cbuf == NULL) {
			goto ocs_xport_rq_threads_create_error;
		}

		ocs_snprintf(xport->rq_thread_info[i].thread_name,
			     sizeof(xport->rq_thread_info[i].thread_name),
			     "ocs_unsol_rq:%d:%d", ocs->instance_index, i);
		rc = ocs_thread_create(ocs, &xport->rq_thread_info[i].thread, ocs_unsol_rq_thread,
				       xport->rq_thread_info[i].thread_name,
				       &xport->rq_thread_info[i], OCS_THREAD_RUN);
		if (rc) {
			ocs_log_err(ocs, "ocs_thread_create failed: %d\n", rc);
			goto ocs_xport_rq_threads_create_error;
		}
		xport->rq_thread_info[i].thread_started = TRUE;
	}
	return 0;

ocs_xport_rq_threads_create_error:
	ocs_xport_rq_threads_teardown(xport);
	return -1;
}

/**
 * @brief Do as much allocation as possible, but do not initialization the device.
 *
 * @par Description
 * Performs the functions required to get a device ready to run.
 *
 * @param xport Pointer to transport object.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
ocs_xport_attach(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;
	int32_t rc;
	uint32_t max_sgl;
	uint32_t n_sgl;
	uint32_t i;
	uint32_t value;
	uint32_t max_remote_nodes;

	/* booleans used for cleanup if initialization fails */
	uint8_t io_pool_created = FALSE;
	uint8_t node_pool_created = FALSE;
	uint8_t rq_threads_created = FALSE;

	ocs_list_init(&ocs->domain_list, ocs_domain_t, link);

	for (i = 0; i < SLI4_MAX_FCFI; i++) {
		xport->fcfi[i].hold_frames = 1;
		ocs_lock_init(ocs, &xport->fcfi[i].pend_frames_lock, "xport pend_frames[%d]", i);
		ocs_list_init(&xport->fcfi[i].pend_frames, ocs_hw_sequence_t, link);
	}

	rc = ocs_hw_set_ptr(&ocs->hw, OCS_HW_WAR_VERSION, ocs->hw_war_version);
	if (rc) {
		ocs_log_test(ocs, "can't set OCS_HW_WAR_VERSION\n");
		return -1;
	}

	rc = ocs_hw_setup(&ocs->hw, ocs, SLI4_PORT_TYPE_FC);
	if (rc) {
		ocs_log_err(ocs, "%s: Can't setup hardware\n", ocs->desc);
		return -1;
	} else if (ocs->ctrlmask & OCS_CTRLMASK_CRASH_RESET) {
		ocs_log_debug(ocs, "stopping after ocs_hw_setup\n");
		return -1;
	}

	ocs_hw_set(&ocs->hw, OCS_HW_BOUNCE, ocs->hw_bounce);
	ocs_log_debug(ocs, "HW bounce: %d\n", ocs->hw_bounce);

	ocs_hw_set(&ocs->hw, OCS_HW_RQ_SELECTION_POLICY, ocs->rq_selection_policy);
	ocs_hw_set(&ocs->hw, OCS_HW_RR_QUANTA, ocs->rr_quanta);
	ocs_hw_get(&ocs->hw, OCS_HW_RQ_SELECTION_POLICY, &value);
	ocs_log_debug(ocs, "RQ Selection Policy: %d\n", value);

	ocs_hw_set_ptr(&ocs->hw, OCS_HW_FILTER_DEF, (void*) ocs->filter_def);

	ocs_hw_get(&ocs->hw, OCS_HW_MAX_SGL, &max_sgl);
	max_sgl -= SLI4_SGE_MAX_RESERVED;
	n_sgl = MIN(OCS_FC_MAX_SGL, max_sgl);

	/* EVT: For chained SGL testing */
	if (ocs->ctrlmask & OCS_CTRLMASK_TEST_CHAINED_SGLS) {
		n_sgl = 4;
	}

	/* Note: number of SGLs must be set for ocs_node_create_pool */
	if (ocs_hw_set(&ocs->hw, OCS_HW_N_SGL, n_sgl) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set number of SGLs\n", ocs->desc);
		return -1;
	} else {
		ocs_log_debug(ocs, "%s: Configured for %d SGLs\n", ocs->desc, n_sgl);
	}

	ocs_hw_get(&ocs->hw, OCS_HW_MAX_NODES, &max_remote_nodes);

	if (!ocs->max_remote_nodes)
		ocs->max_remote_nodes = max_remote_nodes;

	rc = ocs_node_create_pool(ocs, ocs->max_remote_nodes);
	if (rc) {
		ocs_log_err(ocs, "Can't allocate node pool\n");
		goto ocs_xport_attach_cleanup;
	} else {
		node_pool_created = TRUE;
	}

	/* EVT: if testing chained SGLs allocate OCS_FC_MAX_SGL SGE's in the IO */
	xport->io_pool = ocs_io_pool_create(ocs, ocs->num_scsi_ios,
		(ocs->ctrlmask & OCS_CTRLMASK_TEST_CHAINED_SGLS) ? OCS_FC_MAX_SGL : n_sgl);
	if (xport->io_pool == NULL) {
		ocs_log_err(ocs, "Can't allocate IO pool\n");
		goto ocs_xport_attach_cleanup;
	} else {
		io_pool_created = TRUE;
	}

	/*
	 * setup the RQ processing threads
	 */
	if (ocs_xport_rq_threads_create(xport, ocs->rq_threads) != 0) {
		ocs_log_err(ocs, "failure creating RQ threads\n");
		goto ocs_xport_attach_cleanup;
	}
	rq_threads_created = TRUE;

	return 0;

ocs_xport_attach_cleanup:
	if (io_pool_created) {
		ocs_io_pool_free(xport->io_pool);
	}

	if (node_pool_created) {
		ocs_node_free_pool(ocs);
	}

	return -1;
}

/**
 * @brief Determines how to setup auto Xfer ready.
 *
 * @par Description
 * @param xport Pointer to transport object.
 *
 * @return Returns 0 on success or a non-zero value on failure.
 */
static int32_t
ocs_xport_initialize_auto_xfer_ready(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;
	uint32_t auto_xfer_rdy;
	char prop_buf[32];
	uint32_t ramdisc_blocksize = 512;
	uint8_t p_type = 0;

	ocs_hw_get(&ocs->hw, OCS_HW_AUTO_XFER_RDY_CAPABLE, &auto_xfer_rdy);
	if (!auto_xfer_rdy) {
		ocs->auto_xfer_rdy_size = 0;
		ocs_log_test(ocs, "Cannot enable auto xfer rdy for this port\n");
		return 0;
	}

	if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_SIZE, ocs->auto_xfer_rdy_size)) {
		ocs_log_test(ocs, "%s: Can't set auto xfer rdy mode\n", ocs->desc);
		return -1;
	}

	/*
	 * Determine if we are doing protection in the backend. We are looking
	 * at the modules parameters here. The backend cannot allow a format
	 * command to change the protection mode when using this feature,
	 * otherwise the firmware will not do the proper thing.
	 */
	if (ocs_get_property("p_type", prop_buf, sizeof(prop_buf)) == 0) {
		p_type = ocs_strtoul(prop_buf, 0, 0);
	}
	if (ocs_get_property("ramdisc_blocksize", prop_buf, sizeof(prop_buf)) == 0) {
		ramdisc_blocksize = ocs_strtoul(prop_buf, 0, 0);
	}
	if (ocs_get_property("external_dif", prop_buf, sizeof(prop_buf)) == 0) {
		if(ocs_strlen(prop_buf)) {
			if (p_type == 0) {
				p_type = 1;
			}
		}
	}

	if (p_type != 0) {
		if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_T10_ENABLE, TRUE)) {
			ocs_log_test(ocs, "%s: Can't set auto xfer rdy mode\n", ocs->desc);
			return -1;
		}
		if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_BLK_SIZE, ramdisc_blocksize)) {
			ocs_log_test(ocs, "%s: Can't set auto xfer rdy blk size\n", ocs->desc);
			return -1;
		}
		if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_P_TYPE, p_type)) {
			ocs_log_test(ocs, "%s: Can't set auto xfer rdy mode\n", ocs->desc);
			return -1;
		}
		if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_REF_TAG_IS_LBA, TRUE)) {
			ocs_log_test(ocs, "%s: Can't set auto xfer rdy ref tag\n", ocs->desc);
			return -1;
		}
		if (ocs_hw_set(&ocs->hw, OCS_HW_AUTO_XFER_RDY_APP_TAG_VALID, FALSE)) {
			ocs_log_test(ocs, "%s: Can't set auto xfer rdy app tag valid\n", ocs->desc);
			return -1;
		}
	}
	ocs_log_debug(ocs, "Auto xfer rdy is enabled, p_type=%d, blksize=%d\n",
		p_type, ramdisc_blocksize);
	return 0;
}

/**
 * @brief Initializes the device.
 *
 * @par Description
 * Performs the functions required to make a device functional.
 *
 * @param xport Pointer to transport object.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
ocs_xport_initialize(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;
	int32_t rc;
	uint32_t i;
	uint32_t max_hw_io;
	uint32_t max_sgl;
	uint32_t hlm;
	uint32_t rq_limit;
	uint32_t dif_capable;
	uint8_t dif_separate = 0;
	char prop_buf[32];

	/* booleans used for cleanup if initialization fails */
	uint8_t ini_device_set = FALSE;
	uint8_t tgt_device_set = FALSE;
	uint8_t hw_initialized = FALSE;

	ocs_hw_get(&ocs->hw, OCS_HW_MAX_IO, &max_hw_io);
	if (ocs_hw_set(&ocs->hw, OCS_HW_N_IO, max_hw_io) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set number of IOs\n", ocs->desc);
		return -1;
	}

	ocs_hw_get(&ocs->hw, OCS_HW_MAX_SGL, &max_sgl);
	max_sgl -= SLI4_SGE_MAX_RESERVED;

	if (ocs->enable_hlm) {
		ocs_hw_get(&ocs->hw, OCS_HW_HIGH_LOGIN_MODE, &hlm);
		if (!hlm) {
			ocs->enable_hlm = FALSE;
			ocs_log_err(ocs, "Cannot enable high login mode for this port\n");
		} else {
                        ocs_log_debug(ocs, "High login mode is enabled\n");
			if (ocs_hw_set(&ocs->hw, OCS_HW_HIGH_LOGIN_MODE, TRUE)) {
				ocs_log_err(ocs, "%s: Can't set high login mode\n", ocs->desc);
				return -1;
			}
		}
	}

	/* validate the auto xfer_rdy size */
	if (ocs->auto_xfer_rdy_size > 0 &&
	    (ocs->auto_xfer_rdy_size < 2048 ||
	     ocs->auto_xfer_rdy_size > 65536)) {
		ocs_log_err(ocs, "Auto XFER_RDY size is out of range (2K-64K)\n");
		return -1;
	}

	ocs_hw_get(&ocs->hw, OCS_HW_MAX_IO, &max_hw_io);

	if (ocs->auto_xfer_rdy_size > 0) {
		if (ocs_xport_initialize_auto_xfer_ready(xport)) {
			ocs_log_err(ocs, "%s: Failed auto xfer ready setup\n", ocs->desc);
			return -1;
		}
		if (ocs->esoc){
			ocs_hw_set(&ocs->hw, OCS_ESOC, TRUE);
		}
	}

	if (ocs->explicit_buffer_list) {
		/* Are pre-registered SGL's required? */
		ocs_hw_get(&ocs->hw, OCS_HW_PREREGISTER_SGL, &i);
		if (i == TRUE) {
			ocs_log_err(ocs, "Explicit Buffer List not supported on this device, not enabled\n");
		} else {
			ocs_hw_set(&ocs->hw, OCS_HW_PREREGISTER_SGL, FALSE);
		}
	}

	if (ocs_hw_set(&ocs->hw, OCS_HW_TOPOLOGY, ocs->topology) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set the toplogy\n", ocs->desc);
		return -1;
	}
	ocs_hw_set(&ocs->hw, OCS_HW_RQ_DEFAULT_BUFFER_SIZE, OCS_FC_RQ_SIZE_DEFAULT);

	if (ocs_hw_set(&ocs->hw, OCS_HW_LINK_SPEED, ocs->speed) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set the link speed\n", ocs->desc);
		return -1;
	}

	if (ocs_hw_set(&ocs->hw, OCS_HW_ETH_LICENSE, ocs->ethernet_license) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set the ethernet license\n", ocs->desc);
		return -1;
	}

	/* currently only lancer support setting the CRC seed value */
	if (ocs->hw.sli.asic_type == SLI4_ASIC_TYPE_LANCER) {
		if (ocs_hw_set(&ocs->hw, OCS_HW_DIF_SEED, OCS_FC_DIF_SEED) != OCS_HW_RTN_SUCCESS) {
			ocs_log_err(ocs, "%s: Can't set the DIF seed\n", ocs->desc);
			return -1;
		}
	}

	/* Set the Dif mode */
	if (0 == ocs_hw_get(&ocs->hw, OCS_HW_DIF_CAPABLE, &dif_capable)) {
		if (dif_capable) {
			if (ocs_get_property("dif_separate", prop_buf, sizeof(prop_buf)) == 0) {
				dif_separate = ocs_strtoul(prop_buf, 0, 0);
			}

			if ((rc = ocs_hw_set(&ocs->hw, OCS_HW_DIF_MODE,
			      (dif_separate == 0 ? OCS_HW_DIF_MODE_INLINE : OCS_HW_DIF_MODE_SEPARATE)))) {
				ocs_log_err(ocs, "Requested DIF MODE not supported\n");
			}
		}
	}

	if (ocs->target_io_timer_sec) {
		ocs_log_debug(ocs, "setting target io timer=%d\n", ocs->target_io_timer_sec);
		ocs_hw_set(&ocs->hw, OCS_HW_EMULATE_TARGET_WQE_TIMEOUT, TRUE);
	}

	ocs_hw_callback(&ocs->hw, OCS_HW_CB_DOMAIN, ocs_domain_cb, ocs);
	ocs_hw_callback(&ocs->hw, OCS_HW_CB_REMOTE_NODE, ocs_remote_node_cb, ocs);
	ocs_hw_callback(&ocs->hw, OCS_HW_CB_UNSOLICITED, ocs_unsolicited_cb, ocs);
	ocs_hw_callback(&ocs->hw, OCS_HW_CB_PORT, ocs_port_cb, ocs);

	ocs->fw_version = (const char*) ocs_hw_get_ptr(&ocs->hw, OCS_HW_FW_REV);

	/* Initialize vport list */
	ocs_list_init(&xport->vport_list, ocs_vport_spec_t, link);
	ocs_lock_init(ocs, &xport->io_pending_lock, "io_pending_lock[%d]", ocs->instance_index);
	ocs_list_init(&xport->io_pending_list, ocs_io_t, io_pending_link);
	ocs_atomic_init(&xport->io_active_count, 0);
	ocs_atomic_init(&xport->io_pending_count, 0);
	ocs_atomic_init(&xport->io_total_free, 0);
	ocs_atomic_init(&xport->io_total_pending, 0);
	ocs_atomic_init(&xport->io_alloc_failed_count, 0);
	ocs_atomic_init(&xport->io_pending_recursing, 0);
	ocs_lock_init(ocs, &ocs->hw.watchdog_lock, " Watchdog Lock[%d]", ocs_instance(ocs));
	rc = ocs_hw_init(&ocs->hw);
	if (rc) {
		ocs_log_err(ocs, "ocs_hw_init failure\n");
		goto ocs_xport_init_cleanup;
	} else {
		hw_initialized = TRUE;
	}

	rq_limit = max_hw_io/2;
	if (ocs_hw_set(&ocs->hw, OCS_HW_RQ_PROCESS_LIMIT, rq_limit) != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(ocs, "%s: Can't set the RQ process limit\n", ocs->desc);
	}

	if (ocs->config_tgt) {
		rc = ocs_scsi_tgt_new_device(ocs);
		if (rc) {
			ocs_log_err(ocs, "failed to initialize target\n");
			goto ocs_xport_init_cleanup;
		} else {
			tgt_device_set = TRUE;
		}
	}

	if (ocs->enable_ini) {
		rc = ocs_scsi_ini_new_device(ocs);
		if (rc) {
			ocs_log_err(ocs, "failed to initialize initiator\n");
			goto ocs_xport_init_cleanup;
		} else {
			ini_device_set = TRUE;
		}

	}

	/* Add vports */
	if (ocs->num_vports != 0) {

		uint32_t max_vports;
		ocs_hw_get(&ocs->hw, OCS_HW_MAX_VPORTS, &max_vports);

		if (ocs->num_vports < max_vports) {
			ocs_log_debug(ocs, "Provisioning %d vports\n", ocs->num_vports);
			for (i = 0; i < ocs->num_vports; i++) {
				ocs_vport_create_spec(ocs, 0, 0, UINT32_MAX, ocs->enable_ini, ocs->enable_tgt, NULL, NULL);
			}
		} else {
			ocs_log_err(ocs, "failed to create vports. num_vports range should be (1-%d) \n", max_vports-1);
			goto ocs_xport_init_cleanup;
		}
	}

	return 0;

ocs_xport_init_cleanup:
	if (ini_device_set) {
		ocs_scsi_ini_del_device(ocs);
	}

	if (tgt_device_set) {
		ocs_scsi_tgt_del_device(ocs);
	}

	if (hw_initialized) {
		/* ocs_hw_teardown can only execute after ocs_hw_init */
		ocs_hw_teardown(&ocs->hw);
	}

	return -1;
}

/**
 * @brief Detaches the transport from the device.
 *
 * @par Description
 * Performs the functions required to shut down a device.
 *
 * @param xport Pointer to transport object.
 *
 * @return Returns 0 on success or a non-zero value on failure.
 */
int32_t
ocs_xport_detach(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;

	/* free resources associated with target-server and initiator-client */
	if (ocs->config_tgt)
		ocs_scsi_tgt_del_device(ocs);

	if (ocs->enable_ini) {
		ocs_scsi_ini_del_device(ocs);

		/*Shutdown FC Statistics timer*/
		if (ocs_timer_pending(&ocs->xport->stats_timer))
			ocs_del_timer(&ocs->xport->stats_timer);
	}

	ocs_hw_teardown(&ocs->hw);

	return 0;
}

/**
 * @brief domain list empty callback
 *
 * @par Description
 * Function is invoked when the device domain list goes empty. By convention
 * @c arg points to an ocs_sem_t instance, that is incremented.
 *
 * @param ocs Pointer to device object.
 * @param arg Pointer to semaphore instance.
 *
 * @return None.
 */

static void
ocs_xport_domain_list_empty_cb(ocs_t *ocs, void *arg)
{
	ocs_sem_t *sem = arg;

	ocs_assert(ocs);
	ocs_assert(sem);

	ocs_sem_v(sem);
}

/**
 * @brief post node event callback
 *
 * @par Description
 * This function is called from the mailbox completion interrupt context to post an
 * event to a node object. By doing this in the interrupt context, it has
 * the benefit of only posting events in the interrupt context, deferring the need to
 * create a per event node lock.
 *
 * @param hw Pointer to HW structure.
 * @param status Completion status for mailbox command.
 * @param mqe Mailbox queue completion entry.
 * @param arg Callback argument.
 *
 * @return Returns 0 on success, a negative error code value on failure.
 */

static int32_t
ocs_xport_post_node_event_cb(ocs_hw_t *hw, int32_t status, uint8_t *mqe, void *arg)
{
	ocs_xport_post_node_event_t *payload = arg;

	if (payload != NULL) {
		ocs_node_post_event(payload->node, payload->evt, payload->context);
		ocs_sem_v(&payload->sem);
	}

        return 0;
}

/**
 * @brief Initiate force free.
 *
 * @par Description
 * Perform force free of OCS.
 *
 * @param xport Pointer to transport object.
 *
 * @return None.
 */

static void
ocs_xport_force_free(ocs_xport_t *xport)
{
	ocs_t *ocs = xport->ocs;
	ocs_domain_t *domain;
	ocs_domain_t *next;

	ocs_log_debug(ocs, "reset required, do force shutdown\n");
	ocs_device_lock(ocs);
		ocs_list_foreach_safe(&ocs->domain_list, domain, next) {
			ocs_domain_force_free(domain);
		}
	ocs_device_unlock(ocs);
}

/**
 * @brief Perform transport attach function.
 *
 * @par Description
 * Perform the attach function, which for the FC transport makes a HW call
 * to bring up the link.
 *
 * @param xport pointer to transport object.
 * @param cmd command to execute.
 *
 * ocs_xport_control(ocs_xport_t *xport, OCS_XPORT_PORT_ONLINE)
 * ocs_xport_control(ocs_xport_t *xport, OCS_XPORT_PORT_OFFLINE)
 * ocs_xport_control(ocs_xport_t *xport, OCS_XPORT_PORT_SHUTDOWN)
 * ocs_xport_control(ocs_xport_t *xport, OCS_XPORT_POST_NODE_EVENT, ocs_node_t *node, ocs_sm_event_t, void *context)
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

int32_t
ocs_xport_control(ocs_xport_t *xport, ocs_xport_ctrl_e cmd, ...)
{
	uint32_t rc = 0;
	ocs_t *ocs = NULL;
	va_list argp;

	ocs_assert(xport, -1);
	ocs_assert(xport->ocs, -1);
	ocs = xport->ocs;

	switch (cmd) {
	case OCS_XPORT_PORT_ONLINE: {
		/* Bring the port on-line */
		rc = ocs_hw_port_control(&ocs->hw, OCS_HW_PORT_INIT, 0, NULL, NULL);
		if (rc) {
			ocs_log_err(ocs, "%s: Can't init port\n", ocs->desc);
		} else {
			xport->configured_link_state = cmd;
		}
		break;
	}
	case OCS_XPORT_PORT_OFFLINE: {
		if (ocs_hw_port_control(&ocs->hw, OCS_HW_PORT_SHUTDOWN, 0, NULL, NULL)) {
			ocs_log_err(ocs, "port shutdown failed\n");
		} else {
			xport->configured_link_state = cmd;
		}
		break;
	}

	case OCS_XPORT_SHUTDOWN: {
		ocs_sem_t sem;
		uint32_t reset_required;

		/* if a PHYSDEV reset was performed (e.g. hw dump), will affect
		 * all PCI functions; orderly shutdown won't work, just force free
		 */
		/* TODO: need to poll this regularly... */
		if (ocs_hw_get(&ocs->hw, OCS_HW_RESET_REQUIRED, &reset_required) != OCS_HW_RTN_SUCCESS) {
			reset_required = 0;
		}

		if (reset_required) {
			ocs_log_debug(ocs, "reset required, do force shutdown\n");
			ocs_xport_force_free(xport);
			break;
		}
		ocs_sem_init(&sem, 0, "domain_list_sem");
		ocs_register_domain_list_empty_cb(ocs, ocs_xport_domain_list_empty_cb, &sem);

		if (ocs_hw_port_control(&ocs->hw, OCS_HW_PORT_SHUTDOWN, 0, NULL, NULL)) {
			ocs_log_debug(ocs, "port shutdown failed, do force shutdown\n");
			ocs_xport_force_free(xport);
		} else {
			ocs_log_debug(ocs, "Waiting %d seconds for domain shutdown.\n", (OCS_FC_DOMAIN_SHUTDOWN_TIMEOUT_USEC/1000000));

			rc = ocs_sem_p(&sem, OCS_FC_DOMAIN_SHUTDOWN_TIMEOUT_USEC);
			if (rc) {
				ocs_log_debug(ocs, "Note: Domain shutdown timed out\n");
				ocs_xport_force_free(xport);
			}
		}

		ocs_register_domain_list_empty_cb(ocs, NULL, NULL);

		/* Free up any saved virtual ports */
		ocs_vport_del_all(ocs);
		break;
	}

	/*
	 * POST_NODE_EVENT:  post an event to a node object
	 *
	 * This transport function is used to post an event to a node object. It does
	 * this by submitting a NOP mailbox command to defer execution to the
	 * interrupt context (thereby enforcing the serialized execution of event posting
	 * to the node state machine instances)
	 *
	 * A counting semaphore is used to make the call synchronous (we wait until
	 * the callback increments the semaphore before returning (or times out)
	 */
	case OCS_XPORT_POST_NODE_EVENT: {
		ocs_node_t *node;
		ocs_sm_event_t evt;
		void *context;
		ocs_xport_post_node_event_t payload;
		ocs_t *ocs;
		ocs_hw_t *hw;

		/* Retrieve arguments */
		va_start(argp, cmd);
		node = va_arg(argp, ocs_node_t*);
		evt = va_arg(argp, ocs_sm_event_t);
		context = va_arg(argp, void *);
		va_end(argp);

		ocs_assert(node, -1);
		ocs_assert(node->ocs, -1);

		ocs = node->ocs;
		hw = &ocs->hw;

		/* if node's state machine is disabled, don't bother continuing */
		if (!node->sm.current_state) {
			ocs_log_test(ocs, "node %p state machine disabled\n", node);
			return -1;
		}

		/* Setup payload */
		ocs_memset(&payload, 0, sizeof(payload));
		ocs_sem_init(&payload.sem, 0, "xport_post_node_Event");
		payload.node = node;
		payload.evt = evt;
		payload.context = context;

		if (ocs_hw_async_call(hw, ocs_xport_post_node_event_cb, &payload)) {
			ocs_log_test(ocs, "ocs_hw_async_call failed\n");
			rc = -1;
			break;
		}

		/* Wait for completion */
		if (ocs_sem_p(&payload.sem, OCS_SEM_FOREVER)) {
			ocs_log_test(ocs, "POST_NODE_EVENT: sem wait failed\n");
			rc = -1;
		}

		break;
	}
	/*
	 * Set wwnn for the port.  This will be used instead of the default provided by FW.
	 */
	case OCS_XPORT_WWNN_SET: {
		uint64_t wwnn;

		/* Retrieve arguments */
		va_start(argp, cmd);
		wwnn = va_arg(argp, uint64_t);
		va_end(argp);

		ocs_log_debug(ocs, " WWNN %016" PRIx64 "\n", wwnn);
		xport->req_wwnn = wwnn;

		break;
	}
	/*
	 * Set wwpn for the port.  This will be used instead of the default provided by FW.
	 */
	case OCS_XPORT_WWPN_SET: {
		uint64_t wwpn;

		/* Retrieve arguments */
		va_start(argp, cmd);
		wwpn = va_arg(argp, uint64_t);
		va_end(argp);

		ocs_log_debug(ocs, " WWPN %016" PRIx64 "\n", wwpn);
		xport->req_wwpn = wwpn;

		break;
	}


	default:
		break;
	}
	return rc;
}

/**
 * @brief Return status on a link.
 *
 * @par Description
 * Returns status information about a link.
 *
 * @param xport Pointer to transport object.
 * @param cmd Command to execute.
 * @param result Pointer to result value.
 *
 * ocs_xport_status(ocs_xport_t *xport, OCS_XPORT_PORT_STATUS)
 * ocs_xport_status(ocs_xport_t *xport, OCS_XPORT_LINK_SPEED, ocs_xport_stats_t *result)
 *	return link speed in MB/sec
 * ocs_xport_status(ocs_xport_t *xport, OCS_XPORT_IS_SUPPORTED_LINK_SPEED, ocs_xport_stats_t *result)
 *	[in] *result is speed to check in MB/s
 *	returns 1 if supported, 0 if not
 * ocs_xport_status(ocs_xport_t *xport, OCS_XPORT_LINK_STATISTICS, ocs_xport_stats_t *result)
 *	return link/host port stats
 * ocs_xport_status(ocs_xport_t *xport, OCS_XPORT_LINK_STAT_RESET, ocs_xport_stats_t *result)
 *	resets link/host stats
 *
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

int32_t
ocs_xport_status(ocs_xport_t *xport, ocs_xport_status_e cmd, ocs_xport_stats_t *result)
{
	uint32_t rc = 0;
	ocs_t *ocs = NULL;
	ocs_xport_stats_t value;
	ocs_hw_rtn_e hw_rc;

	ocs_assert(xport, -1);
	ocs_assert(xport->ocs, -1);

	ocs = xport->ocs;

	switch (cmd) {
	case OCS_XPORT_CONFIG_PORT_STATUS:
		ocs_assert(result, -1);
		if (xport->configured_link_state == 0) {
			/* Initial state is offline. configured_link_state is    */
			/* set to online explicitly when port is brought online. */
			xport->configured_link_state = OCS_XPORT_PORT_OFFLINE;
		}
		result->value = xport->configured_link_state;
		break;

	case OCS_XPORT_PORT_STATUS:
		ocs_assert(result, -1);
		/* Determine port status based on link speed. */
		hw_rc = ocs_hw_get(&(ocs->hw), OCS_HW_LINK_SPEED, &value.value);
		if (hw_rc == OCS_HW_RTN_SUCCESS) {
			if (value.value == 0) {
				result->value = 0;
			} else {
				result->value = 1;
			}
			rc = 0;
		} else {
			rc = -1;
		}
		break;

	case OCS_XPORT_LINK_SPEED: {
		uint32_t speed;

		ocs_assert(result, -1);
		result->value = 0;

		rc = ocs_hw_get(&ocs->hw, OCS_HW_LINK_SPEED, &speed);
		if (rc == 0) {
			result->value = speed;
		}
		break;
	}

	case OCS_XPORT_IS_SUPPORTED_LINK_SPEED: {
		uint32_t speed;
		uint32_t link_module_type;

		ocs_assert(result, -1);
		speed = result->value;

		rc = ocs_hw_get(&ocs->hw, OCS_HW_LINK_MODULE_TYPE, &link_module_type);
		if (rc == 0) {
			switch(speed) {
			case 1000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_1GB) != 0; break;
			case 2000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_2GB) != 0; break;
			case 4000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_4GB) != 0; break;
			case 8000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_8GB) != 0; break;
			case 10000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_10GB) != 0; break;
			case 16000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_16GB) != 0; break;
			case 32000:	rc = (link_module_type & OCS_HW_LINK_MODULE_TYPE_32GB) != 0; break;
			default:	rc = 0; break;
			}
		} else {
			rc = 0;
		}
		break;
	}
	case OCS_XPORT_LINK_STATISTICS: 
		ocs_device_lock(ocs);
			ocs_memcpy((void *)result, &ocs->xport->fc_xport_stats, sizeof(ocs_xport_stats_t));
		ocs_device_unlock(ocs);
		break;
	case OCS_XPORT_LINK_STAT_RESET: {
		/* Create a semaphore to synchronize the stat reset process. */
		ocs_sem_init(&(result->stats.semaphore), 0, "fc_stats_reset");

		/* First reset the link stats */
		if ((rc = ocs_hw_get_link_stats(&ocs->hw, 0, 1, 1, ocs_xport_link_stats_cb, result)) != 0) {
			ocs_log_err(ocs, "%s: Failed to reset link statistics\n", __func__);
			break;
		}

		/* Wait for semaphore to be signaled when the command completes */
		/* TODO:  Should there be a timeout on this?  If so, how long? */
		if (ocs_sem_p(&(result->stats.semaphore), OCS_SEM_FOREVER) != 0) {
			/* Undefined failure */
			ocs_log_test(ocs, "ocs_sem_p failed\n");
			rc = -ENXIO;
			break;
		}

		/* Next reset the host stats */
		if ((rc = ocs_hw_get_host_stats(&ocs->hw, 1,  ocs_xport_host_stats_cb, result)) != 0) {
			ocs_log_err(ocs, "%s: Failed to reset host statistics\n", __func__);
			break;
		}

		/* Wait for semaphore to be signaled when the command completes */
		if (ocs_sem_p(&(result->stats.semaphore), OCS_SEM_FOREVER) != 0) {
			/* Undefined failure */
			ocs_log_test(ocs, "ocs_sem_p failed\n");
			rc = -ENXIO;
			break;
		}
		break;
	}
	case OCS_XPORT_IS_QUIESCED:
		ocs_device_lock(ocs);
			result->value = ocs_list_empty(&ocs->domain_list);
		ocs_device_unlock(ocs);
		break;
	default:
		rc = -1;
		break;
	}

	return rc;

}

static void
ocs_xport_link_stats_cb(int32_t status, uint32_t num_counters, ocs_hw_link_stat_counts_t *counters, void *arg)
{
        ocs_xport_stats_t *result = arg;

        result->stats.link_stats.link_failure_error_count = counters[OCS_HW_LINK_STAT_LINK_FAILURE_COUNT].counter;
        result->stats.link_stats.loss_of_sync_error_count = counters[OCS_HW_LINK_STAT_LOSS_OF_SYNC_COUNT].counter;
        result->stats.link_stats.primitive_sequence_error_count = counters[OCS_HW_LINK_STAT_PRIMITIVE_SEQ_COUNT].counter;
        result->stats.link_stats.invalid_transmission_word_error_count = counters[OCS_HW_LINK_STAT_INVALID_XMIT_WORD_COUNT].counter;
        result->stats.link_stats.crc_error_count = counters[OCS_HW_LINK_STAT_CRC_COUNT].counter;

        ocs_sem_v(&(result->stats.semaphore));
}


static void
ocs_xport_host_stats_cb(int32_t status, uint32_t num_counters, ocs_hw_host_stat_counts_t *counters, void *arg)
{
        ocs_xport_stats_t *result = arg;

        result->stats.host_stats.transmit_kbyte_count = counters[OCS_HW_HOST_STAT_TX_KBYTE_COUNT].counter;
        result->stats.host_stats.receive_kbyte_count = counters[OCS_HW_HOST_STAT_RX_KBYTE_COUNT].counter;
        result->stats.host_stats.transmit_frame_count = counters[OCS_HW_HOST_STAT_TX_FRAME_COUNT].counter;
        result->stats.host_stats.receive_frame_count = counters[OCS_HW_HOST_STAT_RX_FRAME_COUNT].counter;

        ocs_sem_v(&(result->stats.semaphore));
}


/**
 * @brief Free a transport object.
 *
 * @par Description
 * The transport object is freed.
 *
 * @param xport Pointer to transport object.
 *
 * @return None.
 */

void
ocs_xport_free(ocs_xport_t *xport)
{
	ocs_t *ocs;
	uint32_t i;

	if (xport) {
		ocs = xport->ocs;
		ocs_io_pool_free(xport->io_pool);
		ocs_node_free_pool(ocs);
		if(mtx_initialized(&xport->io_pending_lock.lock))
			ocs_lock_free(&xport->io_pending_lock);

		for (i = 0; i < SLI4_MAX_FCFI; i++) {
			ocs_lock_free(&xport->fcfi[i].pend_frames_lock);
		}

		ocs_xport_rq_threads_teardown(xport);

		ocs_free(ocs, xport, sizeof(*xport));
	}
}
