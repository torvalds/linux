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
 * OCS driver remote node handler.  This file contains code that is shared
 * between fabric (ocs_fabric.c) and device (ocs_device.c) nodes.
 */

/*!
 * @defgroup node_common Node common support
 * @defgroup node_alloc Node allocation
 */

#include "ocs.h"
#include "ocs_els.h"
#include "ocs_device.h"

#define SCSI_IOFMT "[%04x][i:%0*x t:%0*x h:%04x]"
#define SCSI_ITT_SIZE(ocs)	((ocs->ocs_xport == OCS_XPORT_FC) ? 4 : 8)

#define SCSI_IOFMT_ARGS(io) io->instance_index, SCSI_ITT_SIZE(io->ocs), io->init_task_tag, SCSI_ITT_SIZE(io->ocs), io->tgt_task_tag, io->hw_tag

#define scsi_io_printf(io, fmt, ...) ocs_log_debug(io->ocs, "[%s]" SCSI_IOFMT fmt, \
	io->node->display_name, SCSI_IOFMT_ARGS(io), ##__VA_ARGS__)

void ocs_mgmt_node_list(ocs_textbuf_t *textbuf, void *node);
void ocs_mgmt_node_get_all(ocs_textbuf_t *textbuf, void *node);
int ocs_mgmt_node_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *node);
int ocs_mgmt_node_set(char *parent, char *name, char *value, void *node);
int ocs_mgmt_node_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
		void *arg_out, uint32_t arg_out_length, void *node);
static ocs_mgmt_functions_t node_mgmt_functions = {
	.get_list_handler	=	ocs_mgmt_node_list,
	.get_handler		=	ocs_mgmt_node_get,
	.get_all_handler	=	ocs_mgmt_node_get_all,
	.set_handler		=	ocs_mgmt_node_set,
	.exec_handler		=	ocs_mgmt_node_exec,
};


/**
 * @ingroup node_common
 * @brief Device node state machine wait for all ELS's to
 *        complete
 *
 * Abort all ELS's for given node.
 *
 * @param node node for which ELS's will be aborted
 */

void
ocs_node_abort_all_els(ocs_node_t *node)
{
	ocs_io_t *els;
	ocs_io_t *els_next;
	ocs_node_cb_t cbdata = {0};

	ocs_node_hold_frames(node);
	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach_safe(&node->els_io_active_list, els, els_next) {
			ocs_log_debug(node->ocs, "[%s] initiate ELS abort %s\n", node->display_name, els->display_name);
			ocs_unlock(&node->active_ios_lock);
			cbdata.els = els;
			ocs_els_post_event(els, OCS_EVT_ABORT_ELS, &cbdata);
			ocs_lock(&node->active_ios_lock);
		}
	ocs_unlock(&node->active_ios_lock);
}

/**
 * @ingroup node_common
 * @brief Handle remote node events from HW
 *
 * Handle remote node events from HW.   Essentially the HW event is translated into
 * a node state machine event that is posted to the affected node.
 *
 * @param arg pointer to ocs
 * @param event HW event to proceoss
 * @param data application specific data (pointer to the affected node)
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_remote_node_cb(void *arg, ocs_hw_remote_node_event_e event, void *data)
{
	ocs_t *ocs = arg;
	ocs_sm_event_t	sm_event = OCS_EVT_LAST;
	ocs_remote_node_t *rnode = data;
	ocs_node_t *node = rnode->node;

	switch (event) {
	case OCS_HW_NODE_ATTACH_OK:
		sm_event = OCS_EVT_NODE_ATTACH_OK;
		break;

	case OCS_HW_NODE_ATTACH_FAIL:
		sm_event = OCS_EVT_NODE_ATTACH_FAIL;
		break;

	case OCS_HW_NODE_FREE_OK:
		sm_event = OCS_EVT_NODE_FREE_OK;
		break;

	case OCS_HW_NODE_FREE_FAIL:
		sm_event = OCS_EVT_NODE_FREE_FAIL;
		break;

	default:
		ocs_log_test(ocs, "unhandled event %#x\n", event);
		return -1;
	}

	/* If we're using HLM, forward the NODE_ATTACH_OK/FAIL event to all nodes in the node group */
	if ((node->node_group != NULL) &&
			((sm_event == OCS_EVT_NODE_ATTACH_OK) || (sm_event == OCS_EVT_NODE_ATTACH_FAIL))) {
		ocs_node_t *n = NULL;
		uint8_t		attach_ok = sm_event == OCS_EVT_NODE_ATTACH_OK;

		ocs_sport_lock(node->sport);
		{
			ocs_list_foreach(&node->sport->node_list, n) {
				if (node == n) {
					continue;
				}
				ocs_node_lock(n);
					if ((!n->rnode.attached) && (node->node_group == n->node_group)) {
						n->rnode.attached = attach_ok;
						node_printf(n, "rpi[%d] deferred HLM node attach %s posted\n",
								n->rnode.index, attach_ok ? "ok" : "fail");
						ocs_node_post_event(n, sm_event, NULL);
					}
				ocs_node_unlock(n);
			}
		}

		ocs_sport_unlock(node->sport);
	}

	ocs_node_post_event(node, sm_event, NULL);

	return 0;
}

/**
 * @ingroup node_alloc
 * @brief Find an FC node structure given the FC port ID
 *
 * @param sport the SPORT to search
 * @param port_id FC port ID
 *
 * @return pointer to the object or NULL if not found
 */
ocs_node_t *
ocs_node_find(ocs_sport_t *sport, uint32_t port_id)
{
	ocs_node_t *node;

	ocs_assert(sport->lookup, NULL);
	ocs_sport_lock(sport);
		node = spv_get(sport->lookup, port_id);
	ocs_sport_unlock(sport);
	return node;
}

/**
 * @ingroup node_alloc
 * @brief Find an FC node structure given the WWPN
 *
 * @param sport the SPORT to search
 * @param wwpn the WWPN to search for (host endian)
 *
 * @return pointer to the object or NULL if not found
 */
ocs_node_t *
ocs_node_find_wwpn(ocs_sport_t *sport, uint64_t wwpn)
{
	ocs_node_t *node = NULL;;

	ocs_assert(sport, NULL);

	ocs_sport_lock(sport);
		ocs_list_foreach(&sport->node_list, node) {
			if (ocs_node_get_wwpn(node) == wwpn) {
				ocs_sport_unlock(sport);
				return node;
			}
		}
	ocs_sport_unlock(sport);
	return NULL;
}

/**
 * @ingroup node_alloc
 * @brief allocate node object pool
 *
 * A pool of ocs_node_t objects is allocated.
 *
 * @param ocs pointer to driver instance context
 * @param node_count count of nodes to allocate
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_node_create_pool(ocs_t *ocs, uint32_t node_count)
{
	ocs_xport_t *xport = ocs->xport;
	uint32_t i;
	ocs_node_t *node;
	uint32_t max_sge;
	uint32_t num_sgl;
	uint64_t max_xfer_size;
	int32_t rc;

	xport->nodes_count = node_count;

	xport->nodes = ocs_malloc(ocs, node_count * sizeof(ocs_node_t *), OCS_M_ZERO | OCS_M_NOWAIT);
	if (xport->nodes == NULL) {
		ocs_log_err(ocs, "node ptrs allocation failed");
		return -1;	
	}

	if (0 == ocs_hw_get(&ocs->hw, OCS_HW_MAX_SGE, &max_sge) &&
	    0 == ocs_hw_get(&ocs->hw, OCS_HW_N_SGL, &num_sgl)) {
		max_xfer_size = (max_sge * (uint64_t)num_sgl);
	} else {
		max_xfer_size = 65536;
	}

	if (max_xfer_size > 65536)
		max_xfer_size = 65536;

	ocs_list_init(&xport->nodes_free_list, ocs_node_t, link);


	for (i = 0; i < node_count; i ++) {
		node = ocs_malloc(ocs, sizeof(ocs_node_t), OCS_M_ZERO | OCS_M_NOWAIT);
		if (node == NULL) {
			ocs_log_err(ocs, "node allocation failed");
			goto error;
		}

		/* Assign any persistent field values */
		node->instance_index = i;
		node->max_wr_xfer_size = max_xfer_size;
		node->rnode.indicator = UINT32_MAX;

		rc = ocs_dma_alloc(ocs, &node->sparm_dma_buf, 256, 16);
		if (rc) {
			ocs_free(ocs, node, sizeof(ocs_node_t));		
			ocs_log_err(ocs, "ocs_dma_alloc failed: %d\n", rc);
			goto error;
		}

		xport->nodes[i] = node;
		ocs_list_add_tail(&xport->nodes_free_list, node);
	}
	return 0;

error:
	ocs_node_free_pool(ocs);
	return -1;	
}

/**
 * @ingroup node_alloc
 * @brief free node object pool
 *
 * The pool of previously allocated node objects is freed
 *
 * @param ocs pointer to driver instance context
 *
 * @return none
 */

void
ocs_node_free_pool(ocs_t *ocs)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_node_t *node;
	uint32_t i;

	if (!xport->nodes)
		return;

	ocs_device_lock(ocs);

	for (i = 0; i < xport->nodes_count; i ++) {
		node = xport->nodes[i];
		if (node) {
			/* free sparam_dma_buf */
			ocs_dma_free(ocs, &node->sparm_dma_buf);
			ocs_free(ocs, node, sizeof(ocs_node_t));
		}
		xport->nodes[i] = NULL;
	}

	ocs_free(ocs, xport->nodes, (xport->nodes_count * sizeof(ocs_node_t *)));

	ocs_device_unlock(ocs);
}

/**
 * @ingroup node_alloc
 * @brief return pointer to node object given instance index
 *
 * A pointer to the node object given by an instance index is returned.
 *
 * @param ocs pointer to driver instance context
 * @param index instance index
 *
 * @return returns pointer to node object, or NULL
 */

ocs_node_t *
ocs_node_get_instance(ocs_t *ocs, uint32_t index)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_node_t *node = NULL;

	if (index >= (xport->nodes_count)) {
		ocs_log_test(ocs, "invalid index: %d\n", index);
		return NULL;
	}
	node = xport->nodes[index];
	return node->attached ? node : NULL;
}

/**
 * @ingroup node_alloc
 * @brief Allocate an fc node structure and add to node list
 *
 * @param sport pointer to the SPORT from which this node is allocated
 * @param port_id FC port ID of new node
 * @param init Port is an inititiator (sent a plogi)
 * @param targ Port is potentially a target
 *
 * @return pointer to the object or NULL if none available
 */

ocs_node_t *
ocs_node_alloc(ocs_sport_t *sport, uint32_t port_id, uint8_t init, uint8_t targ)
{
	int32_t rc;
	ocs_node_t *node = NULL;
	uint32_t instance_index;
	uint32_t max_wr_xfer_size;
	ocs_t *ocs = sport->ocs;
	ocs_xport_t *xport = ocs->xport;
	ocs_dma_t sparm_dma_buf;

	ocs_assert(sport, NULL);

	if (sport->shutting_down) {
		ocs_log_debug(ocs, "node allocation when shutting down %06x", port_id);
		return NULL;
	}

	ocs_device_lock(ocs);
		node = ocs_list_remove_head(&xport->nodes_free_list);
	ocs_device_unlock(ocs);
	if (node == NULL) {
		ocs_log_err(ocs, "node allocation failed %06x", port_id);
		return NULL;
	}

	/* Save persistent values across memset zero */
	instance_index = node->instance_index;
	max_wr_xfer_size = node->max_wr_xfer_size;
	sparm_dma_buf = node->sparm_dma_buf;

	ocs_memset(node, 0, sizeof(*node));
	node->instance_index = instance_index;
	node->max_wr_xfer_size = max_wr_xfer_size;
	node->sparm_dma_buf = sparm_dma_buf;
	node->rnode.indicator = UINT32_MAX;

	node->sport = sport;
	ocs_sport_lock(sport);

		node->ocs = ocs;
		node->init = init;
		node->targ = targ;

		rc = ocs_hw_node_alloc(&ocs->hw, &node->rnode, port_id, sport);
		if (rc) {
			ocs_log_err(ocs, "ocs_hw_node_alloc failed: %d\n", rc);
			ocs_sport_unlock(sport);

			/* Return back to pool. */
			ocs_device_lock(ocs);
			ocs_list_add_tail(&xport->nodes_free_list, node);
			ocs_device_unlock(ocs);

			return NULL;
		}
		ocs_list_add_tail(&sport->node_list, node);

		ocs_node_lock_init(node);
		ocs_lock_init(ocs, &node->pend_frames_lock, "pend_frames_lock[%d]", node->instance_index);
		ocs_list_init(&node->pend_frames, ocs_hw_sequence_t, link);
		ocs_lock_init(ocs, &node->active_ios_lock, "active_ios[%d]", node->instance_index);
		ocs_list_init(&node->active_ios, ocs_io_t, link);
		ocs_list_init(&node->els_io_pend_list, ocs_io_t, link);
		ocs_list_init(&node->els_io_active_list, ocs_io_t, link);
		ocs_scsi_io_alloc_enable(node);

		/* zero the service parameters */
		ocs_memset(node->sparm_dma_buf.virt, 0, node->sparm_dma_buf.size);

		node->rnode.node = node;
		node->sm.app = node;
		node->evtdepth = 0;

		ocs_node_update_display_name(node);

		spv_set(sport->lookup, port_id, node);
	ocs_sport_unlock(sport);
	node->mgmt_functions = &node_mgmt_functions;

	return node;
}

/**
 * @ingroup node_alloc
 * @brief free a node structure
 *
 * The node structure given by 'node' is free'd
 *
 * @param node the node to free
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_node_free(ocs_node_t *node)
{
	ocs_sport_t *sport;
	ocs_t *ocs;
	ocs_xport_t *xport;
	ocs_hw_rtn_e rc = 0;
	ocs_node_t *ns = NULL;
	int post_all_free = FALSE;

	ocs_assert(node, -1);
	ocs_assert(node->sport, -1);
	ocs_assert(node->ocs, -1);
	sport = node->sport;
	ocs_assert(sport, -1);
	ocs = node->ocs;
	ocs_assert(ocs->xport, -1);
	xport = ocs->xport;

	node_printf(node, "Free'd\n");

	if(node->refound) {
		/*
		 * Save the name server node. We will send fake RSCN event at
		 * the end to handle ignored RSCN event during node deletion
		 */
		ns = ocs_node_find(node->sport, FC_ADDR_NAMESERVER);
	}

	/* Remove from node list */
	ocs_sport_lock(sport);
		ocs_list_remove(&sport->node_list, node);

		/* Free HW resources */
		if (OCS_HW_RTN_IS_ERROR((rc = ocs_hw_node_free_resources(&ocs->hw, &node->rnode)))) {
			ocs_log_test(ocs, "ocs_hw_node_free failed: %d\n", rc);
			rc = -1;
		}

		/* if the gidpt_delay_timer is still running, then delete it */
		if (ocs_timer_pending(&node->gidpt_delay_timer)) {
			ocs_del_timer(&node->gidpt_delay_timer);
		}

		if (node->fcp2device) {
			ocs_del_crn(node);
		}

		/* remove entry from sparse vector list */
		if (sport->lookup == NULL) {
			ocs_log_test(node->ocs, "assertion failed: sport lookup is NULL\n");
			ocs_sport_unlock(sport);
			return -1;
		}

		spv_set(sport->lookup, node->rnode.fc_id, NULL);

		/*
		 * If the node_list is empty, then post a ALL_CHILD_NODES_FREE event to the sport,
		 * after the lock is released.  The sport may be free'd as a result of the event.
		 */
		if (ocs_list_empty(&sport->node_list)) {
			post_all_free = TRUE;
		}

	ocs_sport_unlock(sport);

	if (post_all_free) {
		ocs_sm_post_event(&sport->sm, OCS_EVT_ALL_CHILD_NODES_FREE, NULL);
	}

	node->sport = NULL;
	node->sm.current_state = NULL;

	ocs_node_lock_free(node);
	ocs_lock_free(&node->pend_frames_lock);
	ocs_lock_free(&node->active_ios_lock);

	/* return to free list */
	ocs_device_lock(ocs);
		ocs_list_add_tail(&xport->nodes_free_list, node);
	ocs_device_unlock(ocs);

	if(ns != NULL) {
		/* sending fake RSCN event to name server node */
		ocs_node_post_event(ns, OCS_EVT_RSCN_RCVD, NULL);
	}

	return rc;
}

/**
 * @brief free memory resources of a node object
 *
 * The node object's child objects are freed after which the
 * node object is freed.
 *
 * @param node pointer to a node object
 *
 * @return none
 */

void
ocs_node_force_free(ocs_node_t *node)
{
	ocs_io_t *io;
	ocs_io_t *next;
	ocs_io_t *els;
	ocs_io_t *els_next;

	/* shutdown sm processing */
	ocs_sm_disable(&node->sm);
	ocs_strncpy(node->prev_state_name, node->current_state_name, sizeof(node->prev_state_name));
	ocs_strncpy(node->current_state_name, "disabled", sizeof(node->current_state_name));

	/* Let the backend cleanup if needed */
	ocs_scsi_notify_node_force_free(node);

	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach_safe(&node->active_ios, io, next) {
			ocs_list_remove(&io->node->active_ios, io);
			ocs_io_free(node->ocs, io);
		}
	ocs_unlock(&node->active_ios_lock);

	/* free all pending ELS IOs */
	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach_safe(&node->els_io_pend_list, els, els_next) {
			/* can't call ocs_els_io_free() because lock is held; cleanup manually */
			ocs_list_remove(&node->els_io_pend_list, els);

			ocs_io_free(node->ocs, els);
		}
	ocs_unlock(&node->active_ios_lock);

	/* free all active ELS IOs */
	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach_safe(&node->els_io_active_list, els, els_next) {
			/* can't call ocs_els_io_free() because lock is held; cleanup manually */
			ocs_list_remove(&node->els_io_active_list, els);

			ocs_io_free(node->ocs, els);
		}
	ocs_unlock(&node->active_ios_lock);

	/* manually purge pending frames (if any) */
	ocs_node_purge_pending(node);

	ocs_node_free(node);
}

/**
 * @ingroup node_common
 * @brief Perform HW call to attach a remote node
 *
 * @param node pointer to node object
 *
 * @return 0 on success, non-zero otherwise
 */
int32_t
ocs_node_attach(ocs_node_t *node)
{
	int32_t rc = 0;
	ocs_sport_t *sport = node->sport;
	ocs_domain_t *domain = sport->domain;
	ocs_t *ocs = node->ocs;

	if (!domain->attached) {
		ocs_log_test(ocs, "Warning: ocs_node_attach with unattached domain\n");
		return -1;
	}
	/* Update node->wwpn/wwnn */

	ocs_node_build_eui_name(node->wwpn, sizeof(node->wwpn), ocs_node_get_wwpn(node));
	ocs_node_build_eui_name(node->wwnn, sizeof(node->wwnn), ocs_node_get_wwnn(node));

	if (ocs->enable_hlm) {
		ocs_node_group_init(node);
	}

	ocs_dma_copy_in(&node->sparm_dma_buf, node->service_params+4, sizeof(node->service_params)-4);

	/* take lock to protect node->rnode.attached */
	ocs_node_lock(node);
		rc = ocs_hw_node_attach(&ocs->hw, &node->rnode, &node->sparm_dma_buf);
		if (OCS_HW_RTN_IS_ERROR(rc)) {
			ocs_log_test(ocs, "ocs_hw_node_attach failed: %d\n", rc);
		}
	ocs_node_unlock(node);

	return rc;
}

/**
 * @ingroup node_common
 * @brief Generate text for a node's fc_id
 *
 * The text for a nodes fc_id is generated, either as a well known name, or a 6 digit
 * hex value.
 *
 * @param fc_id fc_id
 * @param buffer text buffer
 * @param buffer_length text buffer length in bytes
 *
 * @return none
 */

void
ocs_node_fcid_display(uint32_t fc_id, char *buffer, uint32_t buffer_length)
{
	switch (fc_id) {
	case FC_ADDR_FABRIC:
		ocs_snprintf(buffer, buffer_length, "fabric");
		break;
	case FC_ADDR_CONTROLLER:
		ocs_snprintf(buffer, buffer_length, "fabctl");
		break;
	case FC_ADDR_NAMESERVER:
		ocs_snprintf(buffer, buffer_length, "nserve");
		break;
	default:
		if (FC_ADDR_IS_DOMAIN_CTRL(fc_id)) {
			ocs_snprintf(buffer, buffer_length, "dctl%02x",
				FC_ADDR_GET_DOMAIN_CTRL(fc_id));
		} else {
			ocs_snprintf(buffer, buffer_length, "%06x", fc_id);
		}
		break;
	}

}

/**
 * @brief update the node's display name
 *
 * The node's display name is updated, sometimes needed because the sport part
 * is updated after the node is allocated.
 *
 * @param node pointer to the node object
 *
 * @return none
 */

void
ocs_node_update_display_name(ocs_node_t *node)
{
	uint32_t port_id = node->rnode.fc_id;
	ocs_sport_t *sport = node->sport;
	char portid_display[16];

	ocs_assert(sport);

	ocs_node_fcid_display(port_id, portid_display, sizeof(portid_display));

	ocs_snprintf(node->display_name, sizeof(node->display_name), "%s.%s", sport->display_name, portid_display);
}

/**
 * @brief cleans up an XRI for the pending link services accept by aborting the
 *         XRI if required.
 *
 * <h3 class="desc">Description</h3>
 * This function is called when the LS accept is not sent.
 *
 * @param node Node for which should be cleaned up
 */

void
ocs_node_send_ls_io_cleanup(ocs_node_t *node)
{
	ocs_t *ocs = node->ocs;

	if (node->send_ls_acc != OCS_NODE_SEND_LS_ACC_NONE) {
		ocs_assert(node->ls_acc_io);
		ocs_log_debug(ocs, "[%s] cleaning up LS_ACC oxid=0x%x\n",
			node->display_name, node->ls_acc_oxid);

		node->ls_acc_io->hio = NULL;
		ocs_els_io_free(node->ls_acc_io);
		node->send_ls_acc = OCS_NODE_SEND_LS_ACC_NONE;
		node->ls_acc_io = NULL;
	}
}

/**
 * @ingroup node_common
 * @brief state: shutdown a node
 *
 * A node is shutdown,
 *
 * @param ctx remote node sm context
 * @param evt event to process
 * @param arg per event optional argument
 *
 * @return returns NULL
 *
 * @note
 */

void *
__ocs_node_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		ocs_node_hold_frames(node);
		ocs_assert(ocs_node_active_ios_empty(node), NULL);
		ocs_assert(ocs_els_io_list_empty(node, &node->els_io_active_list), NULL);

		/* by default, we will be freeing node after we unwind */
		node->req_free = 1;

		switch (node->shutdown_reason) {
		case OCS_NODE_SHUTDOWN_IMPLICIT_LOGO:
			/* sm: if shutdown reason is implicit logout / ocs_node_attach
			 * Node shutdown b/c of PLOGI received when node already
			 * logged in. We have PLOGI service parameters, so submit
			 * node attach; we won't be freeing this node
			 */

			/* currently, only case for implicit logo is PLOGI recvd. Thus,
			 * node's ELS IO pending list won't be empty (PLOGI will be on it)
			 */
			ocs_assert(node->send_ls_acc == OCS_NODE_SEND_LS_ACC_PLOGI, NULL);
			node_printf(node, "Shutdown reason: implicit logout, re-authenticate\n");

			ocs_scsi_io_alloc_enable(node);

			/* Re-attach node with the same HW node resources */
			node->req_free = 0;
			rc = ocs_node_attach(node);
			ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
			if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
				ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
			}
			break;
		case OCS_NODE_SHUTDOWN_EXPLICIT_LOGO: {
			int8_t pend_frames_empty;

			/* cleanup any pending LS_ACC ELSs */
			ocs_node_send_ls_io_cleanup(node);
			ocs_assert(ocs_els_io_list_empty(node, &node->els_io_pend_list), NULL);

			ocs_lock(&node->pend_frames_lock);
				pend_frames_empty = ocs_list_empty(&node->pend_frames);
			ocs_unlock(&node->pend_frames_lock);

			/* there are two scenarios where we want to keep this node alive:
			 * 1. there are pending frames that need to be processed or
			 * 2. we're an initiator and the remote node is a target and we
			 *    need to re-authenticate
			 */
			node_printf(node, "Shutdown: explicit logo pend=%d sport.ini=%d node.tgt=%d\n",
				    !pend_frames_empty, node->sport->enable_ini, node->targ);

			if((!pend_frames_empty) || (node->sport->enable_ini && node->targ)) {
				uint8_t send_plogi = FALSE;
				if (node->sport->enable_ini && node->targ) {
					/* we're an initiator and node shutting down is a target; we'll
					 * need to re-authenticate in initial state
					 */
					send_plogi = TRUE;
				}

				/* transition to __ocs_d_init (will retain HW node resources) */
				ocs_scsi_io_alloc_enable(node);
				node->req_free = 0;

				/* either pending frames exist, or we're re-authenticating with PLOGI
				 * (or both); in either case, return to initial state
				 */
				ocs_node_init_device(node, send_plogi);

			}
			/* else: let node shutdown occur */
			break;
		}
		case OCS_NODE_SHUTDOWN_DEFAULT:
		default:
			/* shutdown due to link down, node going away (xport event) or
			 * sport shutdown, purge pending and proceed to cleanup node
			 */

			/* cleanup any pending LS_ACC ELSs */
			ocs_node_send_ls_io_cleanup(node);
			ocs_assert(ocs_els_io_list_empty(node, &node->els_io_pend_list), NULL);

			node_printf(node, "Shutdown reason: default, purge pending\n");
			ocs_node_purge_pending(node);
			break;
		}

		break;
	}
	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	default:
		__ocs_node_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup common_node
 * @brief Checks to see if ELS's have been quiesced
 *
 * Check if ELS's have been quiesced. If so, transition to the
 * next state in the shutdown process.
 *
 * @param node Node for which ELS's are checked
 *
 * @return Returns 1 if ELS's have been quiesced, 0 otherwise.
 */
static int
ocs_node_check_els_quiesced(ocs_node_t *node)
{
	ocs_assert(node, -1);

	/* check to see if ELS requests, completions are quiesced */
	if ((node->els_req_cnt == 0) && (node->els_cmpl_cnt == 0) &&
	    ocs_els_io_list_empty(node, &node->els_io_active_list)) {
		if (!node->attached) {
			/* hw node detach already completed, proceed */
			node_printf(node, "HW node not attached\n");
			ocs_node_transition(node, __ocs_node_wait_ios_shutdown, NULL);
		} else {
			/* hw node detach hasn't completed, transition and wait */
			node_printf(node, "HW node still attached\n");
			ocs_node_transition(node, __ocs_node_wait_node_free, NULL);
		}
		return 1;
	}
	return 0;
}

/**
 * @ingroup common_node
 * @brief Initiate node IO cleanup.
 *
 * Note: this function must be called with a non-attached node
 * or a node for which the node detach (ocs_hw_node_detach())
 * has already been initiated.
 *
 * @param node Node for which shutdown is initiated
 *
 * @return Returns None.
 */

void
ocs_node_initiate_cleanup(ocs_node_t *node)
{
	ocs_io_t *els;
	ocs_io_t *els_next;
	ocs_t *ocs;
	ocs_assert(node);
	ocs = node->ocs;

	/* first cleanup ELS's that are pending (not yet active) */
	ocs_lock(&node->active_ios_lock);
		ocs_list_foreach_safe(&node->els_io_pend_list, els, els_next) {

			/* skip the ELS IO for which a response will be sent after shutdown */
			if ((node->send_ls_acc != OCS_NODE_SEND_LS_ACC_NONE) &&
			    (els == node->ls_acc_io)) {
				continue;
			}
			/* can't call ocs_els_io_free() because lock is held; cleanup manually */
                        node_printf(node, "Freeing pending els %s\n", els->display_name);
			ocs_list_remove(&node->els_io_pend_list, els);

			ocs_io_free(node->ocs, els);
		}
	ocs_unlock(&node->active_ios_lock);

	if (node->ls_acc_io && node->ls_acc_io->hio != NULL) {
		/*
		 * if there's an IO that will result in an LS_ACC after
		 * shutdown and its HW IO is non-NULL, it better be an
		 * implicit logout in vanilla sequence coalescing. In this
		 * case, force the LS_ACC to go out on another XRI (hio)
		 * since the previous will have been aborted by the UNREG_RPI
		 */
		ocs_assert(node->shutdown_reason == OCS_NODE_SHUTDOWN_IMPLICIT_LOGO);
		ocs_assert(node->send_ls_acc == OCS_NODE_SEND_LS_ACC_PLOGI);
		node_printf(node, "invalidating ls_acc_io due to implicit logo\n");

		/* No need to abort because the unreg_rpi takes care of it, just free */
		ocs_hw_io_free(&ocs->hw, node->ls_acc_io->hio);

		/* NULL out hio to force the LS_ACC to grab a new XRI */
		node->ls_acc_io->hio = NULL;
	}

	/*
	 * if ELS's have already been quiesced, will move to next state
	 * if ELS's have not been quiesced, abort them
	 */
	if (ocs_node_check_els_quiesced(node) == 0) {
		/*
		 * Abort all ELS's since ELS's won't be aborted by HW
		 * node free.
		 */
		ocs_node_abort_all_els(node);
		ocs_node_transition(node, __ocs_node_wait_els_shutdown, NULL);
	}
}

/**
 * @ingroup node_common
 * @brief Node state machine: Wait for all ELSs to complete.
 *
 * <h3 class="desc">Description</h3>
 * State waits for all ELSs to complete after aborting all
 * outstanding .
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_node_wait_els_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	uint8_t check_quiesce = FALSE;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {

	case OCS_EVT_ENTER: {
		ocs_node_hold_frames(node);
		if (ocs_els_io_list_empty(node, &node->els_io_active_list)) {
			node_printf(node, "All ELS IOs complete\n");
			check_quiesce = TRUE;
		}
		break;
	}
	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_OK:
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_ELS_REQ_ABORTED:
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		check_quiesce = TRUE;
		break;

	case OCS_EVT_SRRS_ELS_CMPL_OK:
	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		check_quiesce = TRUE;
		break;

	case OCS_EVT_ALL_CHILD_NODES_FREE:
		/* all ELS IO's complete */
		node_printf(node, "All ELS IOs complete\n");
		ocs_assert(ocs_els_io_list_empty(node, &node->els_io_active_list), NULL);
		check_quiesce = TRUE;
		break;

	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
		break;

	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		break;

	default:
		__ocs_node_common(__func__, ctx, evt, arg);
		return NULL;
	}

	if (check_quiesce) {
		ocs_node_check_els_quiesced(node);
	}
	return NULL;
}

/**
 * @ingroup node_command
 * @brief Node state machine: Wait for a HW node free event to
 * complete.
 *
 * <h3 class="desc">Description</h3>
 * State waits for the node free event to be received from the HW.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_node_wait_node_free(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {

	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_NODE_FREE_OK:
		/* node is officially no longer attached */
		node->attached = FALSE;
		ocs_node_transition(node, __ocs_node_wait_ios_shutdown, NULL);
		break;

	case OCS_EVT_ALL_CHILD_NODES_FREE:
	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
		/* As IOs and ELS IO's complete we expect to get these events */
		break;

	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* Fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		break;
	default:
		__ocs_node_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup node_common
 * @brief state: initiate node shutdown
 *
 * State is entered when a node receives a shutdown event, and it's waiting
 * for all the active IOs and ELS IOs associated with the node to complete.
 *
 * @param ctx remote node sm context
 * @param evt event to process
 * @param arg per event optional argument
 *
 * @return returns NULL
 */

void *
__ocs_node_wait_ios_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_io_t *io;
	ocs_io_t *next;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);

		/* first check to see if no ELS IOs are outstanding */
		if (ocs_els_io_list_empty(node, &node->els_io_active_list)) {
			/* If there are any active IOS, Free them. */
			if (!ocs_node_active_ios_empty(node)) {
				ocs_lock(&node->active_ios_lock);
				ocs_list_foreach_safe(&node->active_ios, io, next) {
					ocs_list_remove(&io->node->active_ios, io);
					ocs_io_free(node->ocs, io);
				}
				ocs_unlock(&node->active_ios_lock);
			}
			ocs_node_transition(node, __ocs_node_shutdown, NULL);
		}
		break;

	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
	case OCS_EVT_ALL_CHILD_NODES_FREE: {
		if (ocs_node_active_ios_empty(node) &&
		    ocs_els_io_list_empty(node, &node->els_io_active_list)) {
			ocs_node_transition(node, __ocs_node_shutdown, NULL);
		}
		break;
	}

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		ocs_log_debug(ocs, "[%s] %-20s\n", node->display_name, ocs_sm_event_name(evt));
		break;
	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__ocs_node_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup node_common
 * @brief state: common node event handler
 *
 * Handle common/shared node events
 *
 * @param funcname calling function's name
 * @param ctx remote node sm context
 * @param evt event to process
 * @param arg per event optional argument
 *
 * @return returns NULL
 */

void *
__ocs_node_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_t *node = NULL;
	ocs_t *ocs = NULL;
	ocs_node_cb_t *cbdata = arg;
	ocs_assert(ctx, NULL);
	ocs_assert(ctx->app, NULL);
	node = ctx->app;
	ocs_assert(node->ocs, NULL);
	ocs = node->ocs;

	switch(evt) {
	case OCS_EVT_ENTER:
	case OCS_EVT_REENTER:
	case OCS_EVT_EXIT:
	case OCS_EVT_SPORT_TOPOLOGY_NOTIFY:
	case OCS_EVT_NODE_MISSING:
	case OCS_EVT_FCP_CMD_RCVD:
		break;

	case OCS_EVT_NODE_REFOUND:
		node->refound = 1;
		break;

	/* node->attached must be set appropriately for all node attach/detach events */
	case OCS_EVT_NODE_ATTACH_OK:
		node->attached = TRUE;
		break;

	case OCS_EVT_NODE_FREE_OK:
	case OCS_EVT_NODE_ATTACH_FAIL:
		node->attached = FALSE;
		break;

	/* handle any ELS completions that other states either didn't care about
	 * or forgot about
	 */
	case OCS_EVT_SRRS_ELS_CMPL_OK:
	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		break;

	/* handle any ELS request completions that other states either didn't care about
	 * or forgot about
	 */
	case OCS_EVT_SRRS_ELS_REQ_OK:
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_ELS_REQ_ABORTED:
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;

	case OCS_EVT_ELS_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;

		/* Unsupported ELS was received, send LS_RJT, command not supported */
		ocs_log_debug(ocs, "[%s] (%s) ELS x%02x, LS_RJT not supported\n",
			      node->display_name, funcname, ((uint8_t*)cbdata->payload->dma.virt)[0]);
		ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id),
			FC_REASON_COMMAND_NOT_SUPPORTED, FC_EXPL_NO_ADDITIONAL, 0,
			NULL, NULL);
		break;
	}

	case OCS_EVT_PLOGI_RCVD:
	case OCS_EVT_FLOGI_RCVD:
	case OCS_EVT_LOGO_RCVD:
	case OCS_EVT_PRLI_RCVD:
	case OCS_EVT_PRLO_RCVD:
	case OCS_EVT_PDISC_RCVD:
	case OCS_EVT_FDISC_RCVD:
	case OCS_EVT_ADISC_RCVD:
	case OCS_EVT_RSCN_RCVD:
	case OCS_EVT_SCR_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		/* sm: / send ELS_RJT */
		ocs_log_debug(ocs, "[%s] (%s) %s sending ELS_RJT\n",
			      node->display_name, funcname, ocs_sm_event_name(evt));
		/* if we didn't catch this in a state, send generic LS_RJT */
		ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id),
			FC_REASON_UNABLE_TO_PERFORM, FC_EXPL_NO_ADDITIONAL, 0,
			NULL, NULL);

		break;
	}
	case OCS_EVT_GID_PT_RCVD:
	case OCS_EVT_RFT_ID_RCVD:
	case OCS_EVT_RFF_ID_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		ocs_log_debug(ocs, "[%s] (%s) %s sending CT_REJECT\n",
			      node->display_name, funcname, ocs_sm_event_name(evt));
		ocs_send_ct_rsp(cbdata->io, hdr->ox_id, cbdata->payload->dma.virt, FCCT_HDR_CMDRSP_REJECT, FCCT_COMMAND_NOT_SUPPORTED, 0);
		break;
	}

	case OCS_EVT_ABTS_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		ocs_log_debug(ocs, "[%s] (%s) %s sending BA_ACC\n",
			      node->display_name, funcname, ocs_sm_event_name(evt));

		/* sm: send BA_ACC */
		ocs_bls_send_acc_hdr(cbdata->io, hdr);
		break;
	}

	default:
		ocs_log_test(node->ocs, "[%s] %-20s %-20s not handled\n", node->display_name, funcname,
			ocs_sm_event_name(evt));
		break;
	}
	return NULL;
}


/**
 * @ingroup node_common
 * @brief save node service parameters
 *
 * Service parameters are copyed into the node structure
 *
 * @param node pointer to node structure
 * @param payload pointer to service parameters to save
 *
 * @return none
 */

void
ocs_node_save_sparms(ocs_node_t *node, void *payload)
{
	ocs_memcpy(node->service_params, payload, sizeof(node->service_params));
}

/**
 * @ingroup node_common
 * @brief Post event to node state machine context
 *
 * This is used by the node state machine code to post events to the nodes.  Upon
 * completion of the event posting, if the nesting depth is zero and we're not holding
 * inbound frames, then the pending frames are processed.
 *
 * @param node pointer to node
 * @param evt event to post
 * @param arg event posting argument
 *
 * @return none
 */

void
ocs_node_post_event(ocs_node_t *node, ocs_sm_event_t evt, void *arg)
{
	int free_node = FALSE;
	ocs_assert(node);

	ocs_node_lock(node);
		node->evtdepth ++;

		ocs_sm_post_event(&node->sm, evt, arg);

		/* If our event call depth is one and we're not holding frames
		 * then we can dispatch any pending frames.   We don't want to allow
		 * the ocs_process_node_pending() call to recurse.
		 */
		if (!node->hold_frames && (node->evtdepth == 1)) {
			ocs_process_node_pending(node);
		}
		node->evtdepth --;

		/* Free the node object if so requested, and we're at an event
		 * call depth of zero
		 */
		if ((node->evtdepth == 0) && node->req_free) {
			free_node = TRUE;
		}
	ocs_node_unlock(node);

	if (free_node) {
		ocs_node_free(node);
	}

	return;
}

/**
 * @ingroup node_common
 * @brief transition state of a node
 *
 * The node's state is transitioned to the requested state.  Entry/Exit
 * events are posted as needed.
 *
 * @param node pointer to node
 * @param state state to transition to
 * @param data transition data
 *
 * @return none
 */

void
ocs_node_transition(ocs_node_t *node, ocs_sm_function_t state, void *data)
{
	ocs_sm_ctx_t *ctx = &node->sm;

	ocs_node_lock(node);
		if (ctx->current_state == state) {
			ocs_node_post_event(node, OCS_EVT_REENTER, data);
		} else {
			ocs_node_post_event(node, OCS_EVT_EXIT, data);
			ctx->current_state = state;
			ocs_node_post_event(node, OCS_EVT_ENTER, data);
		}
	ocs_node_unlock(node);
}

/**
 * @ingroup node_common
 * @brief build EUI formatted WWN
 *
 * Build a WWN given the somewhat transport agnostic iScsi naming specification, for FC
 * use the eui. format, an ascii string such as: "eui.10000000C9A19501"
 *
 * @param buffer buffer to place formatted name into
 * @param buffer_len length in bytes of the buffer
 * @param eui_name cpu endian 64 bit WWN value
 *
 * @return none
 */

void
ocs_node_build_eui_name(char *buffer, uint32_t buffer_len, uint64_t eui_name)
{
	ocs_memset(buffer, 0, buffer_len);

	ocs_snprintf(buffer, buffer_len, "eui.%016llx", (unsigned long long)eui_name);
}

/**
 * @ingroup node_common
 * @brief return nodes' WWPN as a uint64_t
 *
 * The WWPN is computed from service parameters and returned as a uint64_t
 *
 * @param node pointer to node structure
 *
 * @return WWPN
 *
 */

uint64_t
ocs_node_get_wwpn(ocs_node_t *node)
{
	fc_plogi_payload_t *sp = (fc_plogi_payload_t*) node->service_params;

	return (((uint64_t)ocs_be32toh(sp->port_name_hi) << 32ll) | (ocs_be32toh(sp->port_name_lo)));
}

/**
 * @ingroup node_common
 * @brief return nodes' WWNN as a uint64_t
 *
 * The WWNN is computed from service parameters and returned as a uint64_t
 *
 * @param node pointer to node structure
 *
 * @return WWNN
 *
 */

uint64_t
ocs_node_get_wwnn(ocs_node_t *node)
{
	fc_plogi_payload_t *sp = (fc_plogi_payload_t*) node->service_params;

	return (((uint64_t)ocs_be32toh(sp->node_name_hi) << 32ll) | (ocs_be32toh(sp->node_name_lo)));
}

/**
 * @brief Generate node ddump data
 *
 * Generates the node ddumpdata
 *
 * @param textbuf pointer to text buffer
 * @param node pointer to node context
 *
 * @return Returns 0 on success, or a negative value on failure.
 */

int
ocs_ddump_node(ocs_textbuf_t *textbuf, ocs_node_t *node)
{
	ocs_io_t *io;
	ocs_io_t *els;
	int retval = 0;

	ocs_ddump_section(textbuf, "node", node->instance_index);
	ocs_ddump_value(textbuf, "display_name", "%s", node->display_name);
	ocs_ddump_value(textbuf, "current_state", "%s", node->current_state_name);
	ocs_ddump_value(textbuf, "prev_state", "%s", node->prev_state_name);
	ocs_ddump_value(textbuf, "current_evt", "%s", ocs_sm_event_name(node->current_evt));
	ocs_ddump_value(textbuf, "prev_evt", "%s", ocs_sm_event_name(node->prev_evt));

	ocs_ddump_value(textbuf, "indicator", "%#x", node->rnode.indicator);
	ocs_ddump_value(textbuf, "fc_id", "%#06x", node->rnode.fc_id);
	ocs_ddump_value(textbuf, "attached", "%d", node->rnode.attached);

	ocs_ddump_value(textbuf, "hold_frames", "%d", node->hold_frames);
	ocs_ddump_value(textbuf, "io_alloc_enabled", "%d", node->io_alloc_enabled);
	ocs_ddump_value(textbuf, "shutdown_reason", "%d", node->shutdown_reason);
	ocs_ddump_value(textbuf, "send_ls_acc", "%d", node->send_ls_acc);
	ocs_ddump_value(textbuf, "ls_acc_did", "%d", node->ls_acc_did);
	ocs_ddump_value(textbuf, "ls_acc_oxid", "%#04x", node->ls_acc_oxid);
	ocs_ddump_value(textbuf, "req_free", "%d", node->req_free);
	ocs_ddump_value(textbuf, "els_req_cnt", "%d", node->els_req_cnt);
	ocs_ddump_value(textbuf, "els_cmpl_cnt", "%d", node->els_cmpl_cnt);

	ocs_ddump_value(textbuf, "targ", "%d", node->targ);
	ocs_ddump_value(textbuf, "init", "%d", node->init);
	ocs_ddump_value(textbuf, "wwnn", "%s", node->wwnn);
	ocs_ddump_value(textbuf, "wwpn", "%s", node->wwpn);
	ocs_ddump_value(textbuf, "login_state", "%d", (node->sm.current_state == __ocs_d_device_ready) ? 1 : 0);
	ocs_ddump_value(textbuf, "chained_io_count", "%d", node->chained_io_count);
	ocs_ddump_value(textbuf, "abort_cnt", "%d", node->abort_cnt);

	ocs_display_sparams(NULL, "node_sparams", 1, textbuf, node->service_params+4);

	ocs_lock(&node->pend_frames_lock);
		if (!ocs_list_empty(&node->pend_frames)) {
			ocs_hw_sequence_t *frame;
			ocs_ddump_section(textbuf, "pending_frames", 0);
			ocs_list_foreach(&node->pend_frames, frame) {
				fc_header_t *hdr;
				char buf[128];

				hdr = frame->header->dma.virt;
				ocs_snprintf(buf, sizeof(buf), "%02x/%04x/%04x len %zu",
				 hdr->r_ctl, ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id),
				 frame->payload->dma.len);
				ocs_ddump_value(textbuf, "frame", "%s", buf);
			}
			ocs_ddump_endsection(textbuf, "pending_frames", 0);
		}
	ocs_unlock(&node->pend_frames_lock);

	ocs_scsi_ini_ddump(textbuf, OCS_SCSI_DDUMP_NODE, node);
	ocs_scsi_tgt_ddump(textbuf, OCS_SCSI_DDUMP_NODE, node);

	ocs_lock(&node->active_ios_lock);
		ocs_ddump_section(textbuf, "active_ios", 0);
		ocs_list_foreach(&node->active_ios, io) {
			ocs_ddump_io(textbuf, io);
		}
		ocs_ddump_endsection(textbuf, "active_ios", 0);

		ocs_ddump_section(textbuf, "els_io_pend_list", 0);
		ocs_list_foreach(&node->els_io_pend_list, els) {
			ocs_ddump_els(textbuf, els);
		}
		ocs_ddump_endsection(textbuf, "els_io_pend_list", 0);

		ocs_ddump_section(textbuf, "els_io_active_list", 0);
		ocs_list_foreach(&node->els_io_active_list, els) {
			ocs_ddump_els(textbuf, els);
		}
		ocs_ddump_endsection(textbuf, "els_io_active_list", 0);
	ocs_unlock(&node->active_ios_lock);

	ocs_ddump_endsection(textbuf, "node", node->instance_index);

	return retval;
}

/**
 * @brief check ELS request completion
 *
 * Check ELS request completion event to make sure it's for the
 * ELS request we expect. If not, invoke given common event
 * handler and return an error.
 *
 * @param ctx state machine context
 * @param evt ELS request event
 * @param arg event argument
 * @param cmd ELS command expected
 * @param node_common_func common event handler to call if ELS
 *      		   doesn't match
 * @param funcname function name that called this
 *
 * @return zero if ELS command matches, -1 otherwise
 */
int32_t
node_check_els_req(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg, uint8_t cmd, ocs_node_common_func_t node_common_func, const char *funcname)
{
	ocs_node_t *node = NULL;
	ocs_t *ocs = NULL;
	ocs_node_cb_t *cbdata = arg;
	fc_els_gen_t *els_gen = NULL;
	ocs_assert(ctx, -1);
	node = ctx->app;
	ocs_assert(node, -1);
	ocs = node->ocs;
	ocs_assert(ocs, -1);
	cbdata = arg;
	ocs_assert(cbdata, -1);
	ocs_assert(cbdata->els, -1);
	els_gen = (fc_els_gen_t *)cbdata->els->els_req.virt;
	ocs_assert(els_gen, -1);

	if ((cbdata->els->hio_type != OCS_HW_ELS_REQ) || (els_gen->command_code != cmd)) {
		if (cbdata->els->hio_type != OCS_HW_ELS_REQ) {
			ocs_log_debug(node->ocs, "[%s] %-20s expecting ELS cmd=x%x received type=%d\n",
				node->display_name, funcname, cmd, cbdata->els->hio_type);
		} else {
			ocs_log_debug(node->ocs, "[%s] %-20s expecting ELS cmd=x%x received cmd=x%x\n",
				node->display_name, funcname, cmd, els_gen->command_code);
		}
		/* send event to common handler */
		node_common_func(funcname, ctx, evt, arg);
		return -1;
	}
	return 0;
}

/**
 * @brief check NS request completion
 *
 * Check ELS request completion event to make sure it's for the
 * nameserver request we expect. If not, invoke given common
 * event handler and return an error.
 *
 * @param ctx state machine context
 * @param evt ELS request event
 * @param arg event argument
 * @param cmd nameserver command expected
 * @param node_common_func common event handler to call if
 *      		   nameserver cmd doesn't match
 * @param funcname function name that called this
 *
 * @return zero if NS command matches, -1 otherwise
 */
int32_t
node_check_ns_req(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg, uint32_t cmd, ocs_node_common_func_t node_common_func, const char *funcname)
{
	ocs_node_t *node = NULL;
	ocs_t *ocs = NULL;
	ocs_node_cb_t *cbdata = arg;
	fcct_iu_header_t *fcct = NULL;
	ocs_assert(ctx, -1);
	node = ctx->app;
	ocs_assert(node, -1);
	ocs = node->ocs;
	ocs_assert(ocs, -1);
	cbdata = arg;
	ocs_assert(cbdata, -1);
	ocs_assert(cbdata->els, -1);
	fcct = (fcct_iu_header_t *)cbdata->els->els_req.virt;
	ocs_assert(fcct, -1);

	if ((cbdata->els->hio_type != OCS_HW_FC_CT) || fcct->cmd_rsp_code != ocs_htobe16(cmd)) {
		if (cbdata->els->hio_type != OCS_HW_FC_CT) {
			ocs_log_debug(node->ocs, "[%s] %-20s expecting NS cmd=x%x received type=%d\n",
				node->display_name, funcname, cmd, cbdata->els->hio_type);
		} else {
			ocs_log_debug(node->ocs, "[%s] %-20s expecting NS cmd=x%x received cmd=x%x\n",
				node->display_name, funcname, cmd, fcct->cmd_rsp_code);
		}
		/* send event to common handler */
		node_common_func(funcname, ctx, evt, arg);
		return -1;
	}
	return 0;
}


void
ocs_mgmt_node_list(ocs_textbuf_t *textbuf, void *object)
{
	ocs_io_t *io;
	ocs_node_t *node = (ocs_node_t *)object;

	ocs_mgmt_start_section(textbuf, "node", node->instance_index);

	/* Readonly values */
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "display_name");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "indicator");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "fc_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "attached");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "hold_frames");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "shutting_down");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "req_free");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "ox_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "ox_id_in_use");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "abort_cnt");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "targ");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "init");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "wwpn");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "wwnn");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "pend_frames");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "chained_io_count");

	/* Actions */
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_EX, "resume");

	ocs_lock(&node->active_ios_lock);
	ocs_list_foreach(&node->active_ios, io) {
		if ((io->mgmt_functions) && (io->mgmt_functions->get_list_handler)) {
			io->mgmt_functions->get_list_handler(textbuf, io);
		}
	}
	ocs_unlock(&node->active_ios_lock);

	ocs_mgmt_end_section(textbuf, "node", node->instance_index);
}

int
ocs_mgmt_node_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *object)
{
	ocs_io_t *io;
	ocs_node_t *node = (ocs_node_t *)object;
	char qualifier[80];
	int retval = -1;

	ocs_mgmt_start_section(textbuf, "node", node->instance_index);

	ocs_snprintf(qualifier, sizeof(qualifier), "%s/node[%d]", parent, node->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {
		char *unqualified_name = name + strlen(qualifier) +1;

		/* See if it's a value I can supply */
		if (ocs_strcmp(unqualified_name, "display_name") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", node->display_name);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "indicator") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "0x%x", node->rnode.indicator);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "fc_id") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fc_id", "0x%06x", node->rnode.fc_id);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "attached") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "attached", node->rnode.attached);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "hold_frames") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "hold_frames", node->hold_frames);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "io_alloc_enabled") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "io_alloc_enabled", node->io_alloc_enabled);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "req_free") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "req_free", node->req_free);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "ls_acc_oxid") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "ls_acc_oxid", "0x%#04x", node->ls_acc_oxid);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "ls_acc_did") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "ls_acc_did", "0x%#04x", node->ls_acc_did);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "abort_cnt") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "abort_cnt", "%d", node->abort_cnt);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "targ") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "targ",  node->targ);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "init") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "init",  node->init);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "wwpn") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwpn", "%s", node->wwpn);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "wwnn") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwnn", "%s", node->wwnn);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "current_state") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "current_state", node->current_state_name);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "login_state") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "login_state", "%d", (node->sm.current_state == __ocs_d_device_ready) ? 1 : 0);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "pend_frames") == 0) {
			ocs_hw_sequence_t *frame;
			ocs_lock(&node->pend_frames_lock);
				ocs_list_foreach(&node->pend_frames, frame) {
					fc_header_t *hdr;
					char buf[128];

					hdr = frame->header->dma.virt;
					ocs_snprintf(buf, sizeof(buf), "%02x/%04x/%04x len %zu", hdr->r_ctl,
						 ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id),
						 frame->payload->dma.len);
					ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "pend_frames", buf);
				}
			ocs_unlock(&node->pend_frames_lock);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "chained_io_count") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "chained_io_count", "%d", node->chained_io_count);
			retval = 0;
		} else {
			/* If I didn't know the value of this status pass the request to each of my children */
			ocs_lock(&node->active_ios_lock);
				ocs_list_foreach(&node->active_ios, io) {
					if ((io->mgmt_functions) && (io->mgmt_functions->get_handler)) {
						retval = io->mgmt_functions->get_handler(textbuf, qualifier, name, io);
					}

					if (retval == 0) {
						break;
					}
				}
			ocs_unlock(&node->active_ios_lock);
		}
	}

	ocs_mgmt_end_section(textbuf, "node", node->instance_index);

	return retval;
}

void
ocs_mgmt_node_get_all(ocs_textbuf_t *textbuf, void *object)
{
	ocs_io_t *io;
	ocs_node_t *node = (ocs_node_t *)object;
	ocs_hw_sequence_t *frame;

	ocs_mgmt_start_section(textbuf, "node", node->instance_index);

	ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", node->display_name);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "0x%x", node->rnode.indicator);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fc_id", "0x%06x", node->rnode.fc_id);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "attached", node->rnode.attached);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "hold_frames", node->hold_frames);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "io_alloc_enabled", node->io_alloc_enabled);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "req_free", node->req_free);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "ls_acc_oxid", "0x%#04x", node->ls_acc_oxid);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "ls_acc_did", "0x%#04x", node->ls_acc_did);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "abort_cnt", "%d", node->abort_cnt);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "targ",  node->targ);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "init",  node->init);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwpn", "%s", node->wwpn);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwnn", "%s", node->wwnn);

	ocs_lock(&node->pend_frames_lock);
	ocs_list_foreach(&node->pend_frames, frame) {
		fc_header_t *hdr;
		char buf[128];

		hdr = frame->header->dma.virt;
		ocs_snprintf(buf, sizeof(buf), "%02x/%04x/%04x len %zu", hdr->r_ctl,
			     ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id),
			     frame->payload->dma.len);
		ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "pend_frames", buf);
	}
	ocs_unlock(&node->pend_frames_lock);

	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "chained_io_count", "%d", node->chained_io_count);
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_EX, "resume");
	ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "current_state", node->current_state_name);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "login_state", "%d", (node->sm.current_state == __ocs_d_device_ready) ? 1 : 0);

	ocs_lock(&node->active_ios_lock);
	ocs_list_foreach(&node->active_ios, io) {
		if ((io->mgmt_functions) && (io->mgmt_functions->get_all_handler)) {
			io->mgmt_functions->get_all_handler(textbuf,io);
		}
	}
	ocs_unlock(&node->active_ios_lock);

	ocs_mgmt_end_section(textbuf, "node", node->instance_index);
}

int
ocs_mgmt_node_set(char *parent, char *name, char *value, void *object)
{
	ocs_io_t *io;
	ocs_node_t *node = (ocs_node_t *)object;
	char qualifier[80];
	int retval = -1;

	ocs_snprintf(qualifier, sizeof(qualifier), "%s/node[%d]", parent, node->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {

		ocs_lock(&node->active_ios_lock);
		ocs_list_foreach(&node->active_ios, io) {
			if ((io->mgmt_functions) && (io->mgmt_functions->set_handler)) {
				retval = io->mgmt_functions->set_handler(qualifier, name, value, io);
			}

			if (retval == 0) {
				break;
			}

		}
		ocs_unlock(&node->active_ios_lock);

	}

	return retval;
}

int
ocs_mgmt_node_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
		   void *arg_out, uint32_t arg_out_length, void *object)
{
	ocs_io_t *io;
	ocs_node_t *node = (ocs_node_t *)object;
	char qualifier[80];
	int retval = -1;

	ocs_snprintf(qualifier, sizeof(qualifier), "%s.node%d", parent, node->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(action, qualifier, strlen(qualifier)) == 0) {
		char *unqualified_name = action + strlen(qualifier) +1;

		if (ocs_strcmp(unqualified_name, "resume") == 0) {
			ocs_node_post_event(node, OCS_EVT_RESUME, NULL);
		}

		{
			/* If I didn't know how to do this action pass the request to each of my children */
			ocs_lock(&node->active_ios_lock);
				ocs_list_foreach(&node->active_ios, io) {
					if ((io->mgmt_functions) && (io->mgmt_functions->exec_handler)) {
						retval = io->mgmt_functions->exec_handler(qualifier, action, arg_in, arg_in_length,
							arg_out, arg_out_length, io);
					}

					if (retval == 0) {
						break;
					}

				}
			ocs_unlock(&node->active_ios_lock);
		}
	}

	return retval;
}



/**
 * @brief Return TRUE if active ios list is empty
 *
 * Test if node->active_ios list is empty while holding the node->active_ios_lock.
 *
 * @param node pointer to node object
 *
 * @return TRUE if node active ios list is empty
 */

int
ocs_node_active_ios_empty(ocs_node_t *node)
{
	int empty;

	ocs_lock(&node->active_ios_lock);
		empty = ocs_list_empty(&node->active_ios);
	ocs_unlock(&node->active_ios_lock);
	return empty;
}

/**
 * @brief Pause a node
 *
 * The node is placed in the __ocs_node_paused state after saving the state
 * to return to
 *
 * @param node Pointer to node object
 * @param state State to resume to
 *
 * @return none
 */

void
ocs_node_pause(ocs_node_t *node, ocs_sm_function_t state)
{
	node->nodedb_state = state;
	ocs_node_transition(node, __ocs_node_paused, NULL);
}

/**
 * @brief Paused node state
 *
 * This state is entered when a state is "paused". When resumed, the node
 * is transitioned to a previously saved state (node->ndoedb_state)
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return returns NULL
 */

void *
__ocs_node_paused(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		node_printf(node, "Paused\n");
		break;

	case OCS_EVT_RESUME: {
		ocs_sm_function_t pf = node->nodedb_state;

		node->nodedb_state = NULL;
		ocs_node_transition(node, pf, NULL);
		break;
	}

	case OCS_EVT_DOMAIN_ATTACH_OK:
		break;

	case OCS_EVT_SHUTDOWN:
		node->req_free = 1;
		break;

	default:
		__ocs_node_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Resume a paused state
 *
 * Posts a resume event to the paused node.
 *
 * @param node Pointer to node object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_node_resume(ocs_node_t *node)
{
	ocs_assert(node != NULL, -1);

	ocs_node_post_event(node, OCS_EVT_RESUME, NULL);

	return 0;
}

/**
 * @ingroup node_common
 * @brief Dispatch a ELS frame.
 *
 * <h3 class="desc">Description</h3>
 * An ELS frame is dispatched to the \c node state machine.
 * RQ Pair mode: this function is always called with a NULL hw
 * io.
 *
 * @param node Node that originated the frame.
 * @param seq header/payload sequence buffers
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled and RX buffers need
 * to be returned.
 */

int32_t
ocs_node_recv_els_frame(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	struct {
		uint32_t cmd;
		ocs_sm_event_t evt;
		uint32_t payload_size;
	} els_cmd_list[] = {
		{FC_ELS_CMD_PLOGI,	OCS_EVT_PLOGI_RCVD, 	sizeof(fc_plogi_payload_t)},
		{FC_ELS_CMD_FLOGI,	OCS_EVT_FLOGI_RCVD, 	sizeof(fc_plogi_payload_t)},
		{FC_ELS_CMD_LOGO,	OCS_EVT_LOGO_RCVD, 	sizeof(fc_acc_payload_t)},
		{FC_ELS_CMD_RRQ,	OCS_EVT_RRQ_RCVD, 	sizeof(fc_acc_payload_t)},
		{FC_ELS_CMD_PRLI, 	OCS_EVT_PRLI_RCVD, 	sizeof(fc_prli_payload_t)},
		{FC_ELS_CMD_PRLO, 	OCS_EVT_PRLO_RCVD, 	sizeof(fc_prlo_payload_t)},
		{FC_ELS_CMD_PDISC, 	OCS_EVT_PDISC_RCVD, 	MAX_ACC_REJECT_PAYLOAD},
		{FC_ELS_CMD_FDISC, 	OCS_EVT_FDISC_RCVD, 	MAX_ACC_REJECT_PAYLOAD},
		{FC_ELS_CMD_ADISC, 	OCS_EVT_ADISC_RCVD, 	sizeof(fc_adisc_payload_t)},
		{FC_ELS_CMD_RSCN, 	OCS_EVT_RSCN_RCVD, 	MAX_ACC_REJECT_PAYLOAD},
		{FC_ELS_CMD_SCR	, 	OCS_EVT_SCR_RCVD, 	MAX_ACC_REJECT_PAYLOAD},
	};
	ocs_t *ocs = node->ocs;
	ocs_node_cb_t cbdata;
	fc_header_t *hdr = seq->header->dma.virt;
	uint8_t *buf = seq->payload->dma.virt;
	ocs_sm_event_t evt = OCS_EVT_ELS_RCVD;
	uint32_t payload_size = MAX_ACC_REJECT_PAYLOAD;
	uint32_t i;

	ocs_memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	/* find a matching event for the ELS command */
	for (i = 0; i < ARRAY_SIZE(els_cmd_list); i ++) {
		if (els_cmd_list[i].cmd == buf[0]) {
			evt = els_cmd_list[i].evt;
			payload_size = els_cmd_list[i].payload_size;
			break;
		}
	}

	switch(evt) {
	case OCS_EVT_FLOGI_RCVD:
		ocs_display_sparams(node->display_name, "flogi rcvd req", 0, NULL, ((uint8_t*)seq->payload->dma.virt)+4);
		break;
	case OCS_EVT_FDISC_RCVD:
		ocs_display_sparams(node->display_name, "fdisc rcvd req", 0, NULL, ((uint8_t*)seq->payload->dma.virt)+4);
		break;
	case OCS_EVT_PLOGI_RCVD:
		ocs_display_sparams(node->display_name, "plogi rcvd req", 0, NULL, ((uint8_t*)seq->payload->dma.virt)+4);
		break;
	default:
		break;
	}

	cbdata.io = ocs_els_io_alloc(node, payload_size, OCS_ELS_ROLE_RESPONDER);

	if (cbdata.io != NULL) {
		cbdata.io->hw_priv = seq->hw_priv;
		/* if we're here, sequence initiative has been transferred */
		cbdata.io->seq_init = 1;

		ocs_node_post_event(node, evt, &cbdata);
	} else {
		node_printf(node, "failure to allocate SCSI IO for ELS s_id %06x d_id %06x ox_id %04x rx_id %04x\n",
			    fc_be24toh(hdr->s_id), fc_be24toh(hdr->d_id), ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id));
	}
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup node_common
 * @brief Dispatch a ABTS frame (RQ Pair/sequence coalescing).
 *
 * <h3 class="desc">Description</h3>
 * An ABTS frame is dispatched to the node state machine. This
 * function is used for both RQ Pair and sequence coalescing.
 *
 * @param node Node that originated the frame.
 * @param seq Header/payload sequence buffers
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled and RX buffers need
 * to be returned.
 */

int32_t
ocs_node_recv_abts_frame(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = node->ocs;
	ocs_xport_t *xport = ocs->xport;
	fc_header_t *hdr = seq->header->dma.virt;
	uint16_t ox_id = ocs_be16toh(hdr->ox_id);
	uint16_t rx_id = ocs_be16toh(hdr->rx_id);
	ocs_node_cb_t cbdata;
	int32_t rc = 0;

	node->abort_cnt++;

	/*
	 * Check to see if the IO we want to abort is active, if it not active,
	 * then we can send the BA_ACC using the send frame option
	 */
	if (ocs_io_find_tgt_io(ocs, node, ox_id, rx_id) == NULL) {
		uint32_t send_frame_capable;

		ocs_log_debug(ocs, "IO not found (ox_id %04x)\n", ox_id);

		/* If we have SEND_FRAME capability, then use it to send BA_ACC */
		rc = ocs_hw_get(&ocs->hw, OCS_HW_SEND_FRAME_CAPABLE, &send_frame_capable);
		if ((rc == 0) && send_frame_capable) {
			rc = ocs_sframe_send_bls_acc(node, seq);
			if (rc) {
				ocs_log_test(ocs, "ocs_bls_acc_send_frame failed\n");
			}
			return rc;
		}
		/* continuing */
	}

	ocs_memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	cbdata.io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_RESPONDER);
	if (cbdata.io != NULL) {
		cbdata.io->hw_priv = seq->hw_priv;
		/* If we got this far, SIT=1 */
		cbdata.io->seq_init = 1;

		/* fill out generic fields */
		cbdata.io->ocs = ocs;
		cbdata.io->node = node;
		cbdata.io->cmd_tgt = TRUE;

		ocs_node_post_event(node, OCS_EVT_ABTS_RCVD, &cbdata);
	} else {
		ocs_atomic_add_return(&xport->io_alloc_failed_count, 1);
		node_printf(node, "SCSI IO allocation failed for ABTS received s_id %06x d_id %06x ox_id %04x rx_id %04x\n",
			    fc_be24toh(hdr->s_id), fc_be24toh(hdr->d_id), ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id));
	}

	/* ABTS processed, return RX buffer to the chip */
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup node_common
 * @brief Dispatch a CT frame.
 *
 * <h3 class="desc">Description</h3>
 * A CT frame is dispatched to the \c node state machine.
 * RQ Pair mode: this function is always called with a NULL hw
 * io.
 *
 * @param node Node that originated the frame.
 * @param seq header/payload sequence buffers
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled and RX buffers need
 * to be returned.
 */

int32_t
ocs_node_recv_ct_frame(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = node->ocs;
	fc_header_t *hdr = seq->header->dma.virt;
	fcct_iu_header_t *iu = seq->payload->dma.virt;
	ocs_sm_event_t evt = OCS_EVT_ELS_RCVD;
	uint32_t payload_size = MAX_ACC_REJECT_PAYLOAD;
	uint16_t gscmd = ocs_be16toh(iu->cmd_rsp_code);
	ocs_node_cb_t cbdata;
	uint32_t i;
	struct {
		uint32_t cmd;
		ocs_sm_event_t evt;
		uint32_t payload_size;
	} ct_cmd_list[] = {
		{FC_GS_NAMESERVER_RFF_ID, OCS_EVT_RFF_ID_RCVD, 100},
		{FC_GS_NAMESERVER_RFT_ID, OCS_EVT_RFT_ID_RCVD, 100},
		{FC_GS_NAMESERVER_GNN_ID, OCS_EVT_GNN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_GPN_ID, OCS_EVT_GPN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_GFPN_ID, OCS_EVT_GFPN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_GFF_ID, OCS_EVT_GFF_ID_RCVD, 100},
		{FC_GS_NAMESERVER_GID_FT, OCS_EVT_GID_FT_RCVD, 256},
		{FC_GS_NAMESERVER_GID_PT, OCS_EVT_GID_PT_RCVD, 256},
		{FC_GS_NAMESERVER_RPN_ID, OCS_EVT_RPN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_RNN_ID, OCS_EVT_RNN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_RCS_ID, OCS_EVT_RCS_ID_RCVD, 100},
		{FC_GS_NAMESERVER_RSNN_NN, OCS_EVT_RSNN_NN_RCVD, 100},
		{FC_GS_NAMESERVER_RSPN_ID, OCS_EVT_RSPN_ID_RCVD, 100},
		{FC_GS_NAMESERVER_RHBA, OCS_EVT_RHBA_RCVD, 100},
		{FC_GS_NAMESERVER_RPA, OCS_EVT_RPA_RCVD, 100},
	};

	ocs_memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;

	/* find a matching event for the ELS/GS command */
	for (i = 0; i < ARRAY_SIZE(ct_cmd_list); i ++) {
		if (ct_cmd_list[i].cmd == gscmd) {
			evt = ct_cmd_list[i].evt;
			payload_size = ct_cmd_list[i].payload_size;
			break;
		}
	}

	/* Allocate an IO and send a reject */
	cbdata.io = ocs_els_io_alloc(node, payload_size, OCS_ELS_ROLE_RESPONDER);
	if (cbdata.io == NULL) {
		node_printf(node, "GS IO failed for s_id %06x d_id %06x ox_id %04x rx_id %04x\n",
			fc_be24toh(hdr->s_id), fc_be24toh(hdr->d_id),
			ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id));
		return -1;
	}
	cbdata.io->hw_priv = seq->hw_priv;
	ocs_node_post_event(node, evt, &cbdata);

	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup node_common
 * @brief Dispatch a FCP command frame when the node is not ready.
 *
 * <h3 class="desc">Description</h3>
 * A frame is dispatched to the \c node state machine.
 *
 * @param node Node that originated the frame.
 * @param seq header/payload sequence buffers
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled.
 */

int32_t
ocs_node_recv_fcp_cmd(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_node_cb_t cbdata;
	ocs_t *ocs = node->ocs;

	ocs_memset(&cbdata, 0, sizeof(cbdata));
	cbdata.header = seq->header;
	cbdata.payload = seq->payload;
	ocs_node_post_event(node, OCS_EVT_FCP_CMD_RCVD, &cbdata);
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup node_common
 * @brief Stub handler for non-ABTS BLS frames
 *
 * <h3 class="desc">Description</h3>
 * Log message and drop. Customer can plumb it to their back-end as needed
 *
 * @param node Node that originated the frame.
 * @param seq header/payload sequence buffers
 *
 * @return Returns 0
 */

int32_t
ocs_node_recv_bls_no_sit(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	fc_header_t *hdr = seq->header->dma.virt;

	node_printf(node, "Dropping frame hdr = %08x %08x %08x %08x %08x %08x\n",
		    ocs_htobe32(((uint32_t *)hdr)[0]),
		    ocs_htobe32(((uint32_t *)hdr)[1]),
		    ocs_htobe32(((uint32_t *)hdr)[2]),
		    ocs_htobe32(((uint32_t *)hdr)[3]),
		    ocs_htobe32(((uint32_t *)hdr)[4]),
		    ocs_htobe32(((uint32_t *)hdr)[5]));

	return -1;
}
