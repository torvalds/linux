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
 * Details SLI port (sport) functions.
 */


#include "ocs.h"
#include "ocs_fabric.h"
#include "ocs_els.h"
#include "ocs_device.h"

static void ocs_vport_update_spec(ocs_sport_t *sport);
static void ocs_vport_link_down(ocs_sport_t *sport);

void ocs_mgmt_sport_list(ocs_textbuf_t *textbuf, void *sport);
void ocs_mgmt_sport_get_all(ocs_textbuf_t *textbuf, void *sport);
int ocs_mgmt_sport_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *sport);
int ocs_mgmt_sport_set(char *parent, char *name, char *value, void *sport);
int ocs_mgmt_sport_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
		void *arg_out, uint32_t arg_out_length, void *sport);
static ocs_mgmt_functions_t sport_mgmt_functions = {
	.get_list_handler = ocs_mgmt_sport_list,
	.get_handler = ocs_mgmt_sport_get,
	.get_all_handler = ocs_mgmt_sport_get_all,
	.set_handler = ocs_mgmt_sport_set,
	.exec_handler = ocs_mgmt_sport_exec,
};

/*!
@defgroup sport_sm SLI Port (sport) State Machine: States
*/

/**
 * @ingroup sport_sm
 * @brief SLI port HW callback.
 *
 * @par Description
 * This function is called in response to a HW sport event. This code resolves
 * the reference to the sport object, and posts the corresponding event.
 *
 * @param arg Pointer to the OCS context.
 * @param event HW sport event.
 * @param data Application-specific event (pointer to the sport).
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_port_cb(void *arg, ocs_hw_port_event_e event, void *data)
{
	ocs_t *ocs = arg;
	ocs_sli_port_t *sport = data;

	switch (event) {
	case OCS_HW_PORT_ALLOC_OK:
		ocs_log_debug(ocs, "OCS_HW_PORT_ALLOC_OK\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_ALLOC_OK, NULL);
		break;
	case OCS_HW_PORT_ALLOC_FAIL:
		ocs_log_debug(ocs, "OCS_HW_PORT_ALLOC_FAIL\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_ALLOC_FAIL, NULL);
		break;
	case OCS_HW_PORT_ATTACH_OK:
		ocs_log_debug(ocs, "OCS_HW_PORT_ATTACH_OK\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_ATTACH_OK, NULL);
		break;
	case OCS_HW_PORT_ATTACH_FAIL:
		ocs_log_debug(ocs, "OCS_HW_PORT_ATTACH_FAIL\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_ATTACH_FAIL, NULL);
		break;
	case OCS_HW_PORT_FREE_OK:
		ocs_log_debug(ocs, "OCS_HW_PORT_FREE_OK\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_FREE_OK, NULL);
		break;
	case OCS_HW_PORT_FREE_FAIL:
		ocs_log_debug(ocs, "OCS_HW_PORT_FREE_FAIL\n");
		ocs_sm_post_event(&sport->sm, OCS_EVT_SPORT_FREE_FAIL, NULL);
		break;
	default:
		ocs_log_test(ocs, "unknown event %#x\n", event);
	}

	return 0;
}

/**
 * @ingroup sport_sm
 * @brief Allocate a SLI port object.
 *
 * @par Description
 * A sport object is allocated and associated with the domain. Various
 * structure members are initialized.
 *
 * @param domain Pointer to the domain structure.
 * @param wwpn World wide port name in host endian.
 * @param wwnn World wide node name in host endian.
 * @param fc_id Port ID of sport may be specified, use UINT32_MAX to fabric choose
 * @param enable_ini Enables initiator capability on this port using a non-zero value.
 * @param enable_tgt Enables target capability on this port using a non-zero value.
 *
 * @return Pointer to an ocs_sport_t object; or NULL.
 */

ocs_sport_t *
ocs_sport_alloc(ocs_domain_t *domain, uint64_t wwpn, uint64_t wwnn, uint32_t fc_id, uint8_t enable_ini, uint8_t enable_tgt)
{
	ocs_sport_t *sport;

	if (domain->ocs->ctrlmask & OCS_CTRLMASK_INHIBIT_INITIATOR) {
		enable_ini = 0;
	}

	/* Return a failure if this sport has already been allocated */
	if (wwpn != 0) {
		sport = ocs_sport_find_wwn(domain, wwnn, wwpn);
		if (sport != NULL) {
			ocs_log_test(domain->ocs, "Failed: SPORT %016llx  %016llx already allocated\n",
				     (unsigned long long)wwnn, (unsigned long long)wwpn);
			return NULL;
		}
	}

	sport = ocs_malloc(domain->ocs, sizeof(*sport), OCS_M_NOWAIT | OCS_M_ZERO);
	if (sport) {
		sport->ocs = domain->ocs;
		ocs_snprintf(sport->display_name, sizeof(sport->display_name), "------");
		sport->domain = domain;
		sport->lookup = spv_new(domain->ocs);
		sport->instance_index = domain->sport_instance_count++;
		ocs_sport_lock_init(sport);
		ocs_list_init(&sport->node_list, ocs_node_t, link);
		sport->sm.app = sport;
		sport->enable_ini = enable_ini;
		sport->enable_tgt = enable_tgt;
		sport->enable_rscn = (sport->enable_ini || (sport->enable_tgt && enable_target_rscn(sport->ocs)));

		/* Copy service parameters from domain */
		ocs_memcpy(sport->service_params, domain->service_params, sizeof(fc_plogi_payload_t));

		/* Update requested fc_id */
		sport->fc_id = fc_id;

		/* Update the sport's service parameters for the new wwn's */
		sport->wwpn = wwpn;
		sport->wwnn = wwnn;
		ocs_snprintf(sport->wwnn_str, sizeof(sport->wwnn_str), "%016llx" , (unsigned long long)wwnn);

		/* Initialize node group list */
		ocs_lock_init(sport->ocs, &sport->node_group_lock, "node_group_lock[%d]", sport->instance_index);
		ocs_list_init(&sport->node_group_dir_list, ocs_node_group_dir_t, link);

		/* if this is the "first" sport of the domain, then make it the "phys" sport */
		ocs_domain_lock(domain);
			if (ocs_list_empty(&domain->sport_list)) {
				domain->sport = sport;
			}

			ocs_list_add_tail(&domain->sport_list, sport);
		ocs_domain_unlock(domain);

		sport->mgmt_functions = &sport_mgmt_functions;

		ocs_log_debug(domain->ocs, "[%s] allocate sport\n", sport->display_name);
	}
	return sport;
}

/**
 * @ingroup sport_sm
 * @brief Free a SLI port object.
 *
 * @par Description
 * The sport object is freed.
 *
 * @param sport Pointer to the SLI port object.
 *
 * @return None.
 */

void
ocs_sport_free(ocs_sport_t *sport)
{
	ocs_domain_t *domain;
	ocs_node_group_dir_t *node_group_dir;
	ocs_node_group_dir_t *node_group_dir_next;
	int post_all_free = FALSE;

	if (sport) {
		domain = sport->domain;
		ocs_log_debug(domain->ocs, "[%s] free sport\n", sport->display_name);
		ocs_domain_lock(domain);
			ocs_list_remove(&domain->sport_list, sport);
			ocs_sport_lock(sport);
				spv_del(sport->lookup);
				sport->lookup = NULL;

				ocs_lock(&domain->lookup_lock);
					/* Remove the sport from the domain's sparse vector lookup table */
					spv_set(domain->lookup, sport->fc_id, NULL);
				ocs_unlock(&domain->lookup_lock);

				/* if this is the physical sport, then clear it out of the domain */
				if (sport == domain->sport) {
					domain->sport = NULL;
				}

				/*
				 * If the domain's sport_list is empty, then post the ALL_NODES_FREE event to the domain,
				 * after the lock is released. The domain may be free'd as a result of the event.
				 */
				if (ocs_list_empty(&domain->sport_list)) {
					post_all_free = TRUE;
				}

				/* Free any node group directories */
				ocs_lock(&sport->node_group_lock);
					ocs_list_foreach_safe(&sport->node_group_dir_list, node_group_dir, node_group_dir_next) {
						ocs_unlock(&sport->node_group_lock);
							ocs_node_group_dir_free(node_group_dir);
						ocs_lock(&sport->node_group_lock);
					}
				ocs_unlock(&sport->node_group_lock);
			ocs_sport_unlock(sport);
		ocs_domain_unlock(domain);

		if (post_all_free) {
			ocs_domain_post_event(domain, OCS_EVT_ALL_CHILD_NODES_FREE, NULL);
		}

		ocs_sport_lock_free(sport);
		ocs_lock_free(&sport->node_group_lock);
		ocs_scsi_sport_deleted(sport);

		ocs_free(domain->ocs, sport, sizeof(*sport));
		
	}
}

/**
 * @ingroup sport_sm
 * @brief Free memory resources of a SLI port object.
 *
 * @par Description
 * After the sport object is freed, its child objects are freed.
 *
 * @param sport Pointer to the SLI port object.
 *
 * @return None.
 */

void ocs_sport_force_free(ocs_sport_t *sport)
{
	ocs_node_t *node;
	ocs_node_t *next;

	/* shutdown sm processing */
	ocs_sm_disable(&sport->sm);

	ocs_scsi_notify_sport_force_free(sport);

	ocs_sport_lock(sport);
		ocs_list_foreach_safe(&sport->node_list, node, next) {
			ocs_node_force_free(node);
		}
	ocs_sport_unlock(sport);
	ocs_sport_free(sport);
}

/**
 * @ingroup sport_sm
 * @brief Return a SLI port object, given an instance index.
 *
 * @par Description
 * A pointer to a sport object is returned, given its instance @c index.
 *
 * @param domain Pointer to the domain.
 * @param index Instance index value to find.
 *
 * @return Returns a pointer to the ocs_sport_t object; or NULL.
 */

ocs_sport_t *
ocs_sport_get_instance(ocs_domain_t *domain, uint32_t index)
{
	ocs_sport_t *sport;

	ocs_domain_lock(domain);
		ocs_list_foreach(&domain->sport_list, sport) {
			if (sport->instance_index == index) {
				ocs_domain_unlock(domain);
				return sport;
			}
		}
	ocs_domain_unlock(domain);
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief Find a SLI port object, given an FC_ID.
 *
 * @par Description
 * Returns a pointer to the sport object, given an FC_ID.
 *
 * @param domain Pointer to the domain.
 * @param d_id FC_ID to find.
 *
 * @return Returns a pointer to the ocs_sport_t; or NULL.
 */

ocs_sport_t *
ocs_sport_find(ocs_domain_t *domain, uint32_t d_id)
{
	ocs_sport_t *sport;

	ocs_assert(domain, NULL);
	ocs_lock(&domain->lookup_lock);
		if (domain->lookup == NULL) {
			ocs_log_test(domain->ocs, "assertion failed: domain->lookup is not valid\n");
			ocs_unlock(&domain->lookup_lock);
			return NULL;
		}

		sport = spv_get(domain->lookup, d_id);
	ocs_unlock(&domain->lookup_lock);
	return sport;
}

/**
 * @ingroup sport_sm
 * @brief Find a SLI port, given the WWNN and WWPN.
 *
 * @par Description
 * Return a pointer to a sport, given the WWNN and WWPN.
 *
 * @param domain Pointer to the domain.
 * @param wwnn World wide node name.
 * @param wwpn World wide port name.
 *
 * @return Returns a pointer to a SLI port, if found; or NULL.
 */

ocs_sport_t *
ocs_sport_find_wwn(ocs_domain_t *domain, uint64_t wwnn, uint64_t wwpn)
{
	ocs_sport_t *sport = NULL;

	ocs_domain_lock(domain);
		ocs_list_foreach(&domain->sport_list, sport) {
			if ((sport->wwnn == wwnn) && (sport->wwpn == wwpn)) {
				ocs_domain_unlock(domain);
				return sport;
			}
		}
	ocs_domain_unlock(domain);
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief Request a SLI port attach.
 *
 * @par Description
 * External call to request an attach for a sport, given an FC_ID.
 *
 * @param sport Pointer to the sport context.
 * @param fc_id FC_ID of which to attach.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_sport_attach(ocs_sport_t *sport, uint32_t fc_id)
{
	ocs_hw_rtn_e rc;
	ocs_node_t *node;

	/* Set our lookup */
	ocs_lock(&sport->domain->lookup_lock);
		spv_set(sport->domain->lookup, fc_id, sport);
	ocs_unlock(&sport->domain->lookup_lock);

	/* Update our display_name */
	ocs_node_fcid_display(fc_id, sport->display_name, sizeof(sport->display_name));
	ocs_sport_lock(sport);
		ocs_list_foreach(&sport->node_list, node) {
			ocs_node_update_display_name(node);
		}
	ocs_sport_unlock(sport);
	ocs_log_debug(sport->ocs, "[%s] attach sport: fc_id x%06x\n", sport->display_name, fc_id);

	rc = ocs_hw_port_attach(&sport->ocs->hw, sport, fc_id);
	if (rc != OCS_HW_RTN_SUCCESS) {
		ocs_log_err(sport->ocs, "ocs_hw_port_attach failed: %d\n", rc);
		return -1;
	}
	return 0;
}

/**
 * @brief Common SLI port state machine declarations and initialization.
 */
#define std_sport_state_decl() \
	ocs_sport_t *sport = NULL; \
	ocs_domain_t *domain = NULL; \
	ocs_t *ocs = NULL; \
	\
	ocs_assert(ctx, NULL); \
	sport = ctx->app; \
	ocs_assert(sport, NULL); \
	\
	domain = sport->domain; \
	ocs_assert(domain, NULL); \
	ocs = sport->ocs; \
	ocs_assert(ocs, NULL);

/**
 * @brief Common SLI port state machine trace logging.
 */
#define sport_sm_trace(sport)  \
	do { \
		if (OCS_LOG_ENABLE_DOMAIN_SM_TRACE(ocs)) \
			ocs_log_debug(ocs, "[%s] %-20s\n", sport->display_name, ocs_sm_event_name(evt)); \
	} while (0)


/**
 * @brief SLI port state machine: Common event handler.
 *
 * @par Description
 * Handle common sport events.
 *
 * @param funcname Function name to display.
 * @param ctx Sport state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

static void *
__ocs_sport_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	switch(evt) {
	case OCS_EVT_ENTER:
	case OCS_EVT_REENTER:
	case OCS_EVT_EXIT:
	case OCS_EVT_ALL_CHILD_NODES_FREE:
		break;
	case OCS_EVT_SPORT_ATTACH_OK:
			ocs_sm_transition(ctx, __ocs_sport_attached, NULL);
		break;
	case OCS_EVT_SHUTDOWN: {
		ocs_node_t *node;
		ocs_node_t *node_next;
		int node_list_empty;

		/* Flag this sport as shutting down */
		sport->shutting_down = 1;

		if (sport->is_vport) {
			ocs_vport_link_down(sport);
		}

		ocs_sport_lock(sport);
			node_list_empty = ocs_list_empty(&sport->node_list);
		ocs_sport_unlock(sport);

		if (node_list_empty) {
			/* sm: node list is empty / ocs_hw_port_free
			 * Remove the sport from the domain's sparse vector lookup table */
			ocs_lock(&domain->lookup_lock);
				spv_set(domain->lookup, sport->fc_id, NULL);
			ocs_unlock(&domain->lookup_lock);
			ocs_sm_transition(ctx, __ocs_sport_wait_port_free, NULL);
			if (ocs_hw_port_free(&ocs->hw, sport)) {
				ocs_log_test(sport->ocs, "ocs_hw_port_free failed\n");
				/* Not much we can do, free the sport anyways */
				ocs_sport_free(sport);
			}
		} else {
			/* sm: node list is not empty / shutdown nodes */
			ocs_sm_transition(ctx, __ocs_sport_wait_shutdown, NULL);
			ocs_sport_lock(sport);
				ocs_list_foreach_safe(&sport->node_list, node, node_next) {
					/*
					 * If this is a vport, logout of the fabric controller so that it
					 * deletes the vport on the switch.
					 */
					if((node->rnode.fc_id == FC_ADDR_FABRIC) && (sport->is_vport)) {
						/* if link is down, don't send logo */
						if (sport->ocs->hw.link.status == SLI_LINK_STATUS_DOWN) {
							ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
						} else {
							ocs_log_debug(ocs,"[%s] sport shutdown vport,sending logo to node\n",
								      node->display_name);
						
							if (ocs_send_logo(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT,
								  0, NULL, NULL) == NULL) {
								/* failed to send LOGO, go ahead and cleanup node anyways */
								node_printf(node, "Failed to send LOGO\n");
								ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
							} else {
								/* sent LOGO, wait for response */
								ocs_node_transition(node, __ocs_d_wait_logo_rsp, NULL);
							}
						}
					} else {
						ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
					}
				}
			ocs_sport_unlock(sport);
		}
		break;
	}
	default:
		ocs_log_test(sport->ocs, "[%s] %-20s %-20s not handled\n", sport->display_name, funcname, ocs_sm_event_name(evt));
		break;
	}

	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Physical sport allocated.
 *
 * @par Description
 * This is the initial state for sport objects.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_allocated(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	/* the physical sport is attached */
	case OCS_EVT_SPORT_ATTACH_OK:
		ocs_assert(sport == domain->sport, NULL);
		ocs_sm_transition(ctx, __ocs_sport_attached, NULL);
		break;

	case OCS_EVT_SPORT_ALLOC_OK:
		/* ignore */
		break;
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Handle initial virtual port events.
 *
 * @par Description
 * This state is entered when a virtual port is instantiated,
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_vport_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_ENTER: {
		uint64_t be_wwpn = ocs_htobe64(sport->wwpn);

		if (sport->wwpn == 0) {
			ocs_log_debug(ocs, "vport: letting f/w select WWN\n");
		}

		if (sport->fc_id != UINT32_MAX) {
			ocs_log_debug(ocs, "vport: hard coding port id: %x\n", sport->fc_id);
		}

		ocs_sm_transition(ctx, __ocs_sport_vport_wait_alloc, NULL);
		/* If wwpn is zero, then we'll let the f/w */
		if (ocs_hw_port_alloc(&ocs->hw, sport, sport->domain,
			(sport->wwpn == 0) ? NULL : (uint8_t *)&be_wwpn)) {
			ocs_log_err(ocs, "Can't allocate port\n");
			break;
		}


		break;
	}
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Wait for the HW SLI port allocation to complete.
 *
 * @par Description
 * Waits for the HW sport allocation request to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_vport_wait_alloc(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_SPORT_ALLOC_OK: {
		fc_plogi_payload_t *sp = (fc_plogi_payload_t*) sport->service_params;
		ocs_node_t *fabric;

		/* If we let f/w assign wwn's, then sport wwn's with those returned by hw */
		if (sport->wwnn == 0) {
			sport->wwnn = ocs_be64toh(sport->sli_wwnn);
			sport->wwpn = ocs_be64toh(sport->sli_wwpn);
			ocs_snprintf(sport->wwnn_str, sizeof(sport->wwnn_str), "%016llx", (unsigned long long) sport->wwpn);
		}

		/* Update the sport's service parameters */
		sp->port_name_hi = ocs_htobe32((uint32_t) (sport->wwpn >> 32ll));
		sp->port_name_lo = ocs_htobe32((uint32_t) sport->wwpn);
		sp->node_name_hi = ocs_htobe32((uint32_t) (sport->wwnn >> 32ll));
		sp->node_name_lo = ocs_htobe32((uint32_t) sport->wwnn);

		/* if sport->fc_id is uninitialized, then request that the fabric node use FDISC
		 * to find an fc_id.   Otherwise we're restoring vports, or we're in
		 * fabric emulation mode, so attach the fc_id
		 */
		if (sport->fc_id == UINT32_MAX) {
			fabric = ocs_node_alloc(sport, FC_ADDR_FABRIC, FALSE, FALSE);
			if (fabric == NULL) {
				ocs_log_err(ocs, "ocs_node_alloc() failed\n");
				return NULL;
			}
			ocs_node_transition(fabric, __ocs_vport_fabric_init, NULL);
		} else {
			ocs_snprintf(sport->wwnn_str, sizeof(sport->wwnn_str), "%016llx", (unsigned long long)sport->wwpn);
			ocs_sport_attach(sport, sport->fc_id);
		}
		ocs_sm_transition(ctx, __ocs_sport_vport_allocated, NULL);
		break;
	}
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: virtual sport allocated.
 *
 * @par Description
 * This state is entered after the sport is allocated; it then waits for a fabric node
 * FDISC to complete, which requests a sport attach.
 * The sport attach complete is handled in this state.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_vport_allocated(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_SPORT_ATTACH_OK: {
		ocs_node_t *node;

		if (!(domain->femul_enable)) {
			/* Find our fabric node, and forward this event */
			node = ocs_node_find(sport, FC_ADDR_FABRIC);
			if (node == NULL) {
				ocs_log_test(ocs, "can't find node %06x\n", FC_ADDR_FABRIC);
				break;
			}
			/* sm: / forward sport attach to fabric node */
			ocs_node_post_event(node, evt, NULL);
		}
		ocs_sm_transition(ctx, __ocs_sport_attached, NULL);
		break;
	}
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Attached.
 *
 * @par Description
 * State entered after the sport attach has completed.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_attached(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_ENTER: {
		ocs_node_t *node;

		ocs_log_debug(ocs, "[%s] SPORT attached WWPN %016llx WWNN %016llx \n", sport->display_name,
			sport->wwpn, sport->wwnn);
		ocs_sport_lock(sport);
			ocs_list_foreach(&sport->node_list, node) {
				ocs_node_update_display_name(node);
			}
		ocs_sport_unlock(sport);
		sport->tgt_id = sport->fc_id;
		if (sport->enable_ini) {
			ocs_scsi_ini_new_sport(sport);
		}
		if (sport->enable_tgt) {
			ocs_scsi_tgt_new_sport(sport);
		}

		/* Update the vport (if its not the physical sport) parameters */
		if (sport->is_vport) {
			ocs_vport_update_spec(sport);
		}

		break;
	}

	case OCS_EVT_EXIT:
		ocs_log_debug(ocs, "[%s] SPORT deattached WWPN %016llx WWNN %016llx \n", sport->display_name,
			sport->wwpn, sport->wwnn);
		if (sport->enable_ini) {
			ocs_scsi_ini_del_sport(sport);
		}
		if (sport->enable_tgt) {
			ocs_scsi_tgt_del_sport(sport);
		}
		break;
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Wait for the node shutdowns to complete.
 *
 * @par Description
 * Waits for the ALL_CHILD_NODES_FREE event to be posted from the node
 * shutdown process.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_wait_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_SPORT_ALLOC_OK:
	case OCS_EVT_SPORT_ALLOC_FAIL:
	case OCS_EVT_SPORT_ATTACH_OK:
	case OCS_EVT_SPORT_ATTACH_FAIL:
		/* ignore these events - just wait for the all free event */
		break;

	case OCS_EVT_ALL_CHILD_NODES_FREE: {
		/* Remove the sport from the domain's sparse vector lookup table */
		ocs_lock(&domain->lookup_lock);
			spv_set(domain->lookup, sport->fc_id, NULL);
		ocs_unlock(&domain->lookup_lock);
		ocs_sm_transition(ctx, __ocs_sport_wait_port_free, NULL);
		if (ocs_hw_port_free(&ocs->hw, sport)) {
			ocs_log_err(sport->ocs, "ocs_hw_port_free failed\n");
			/* Not much we can do, free the sport anyways */
			ocs_sport_free(sport);
		}
		break;
	}
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief SLI port state machine: Wait for the HW's port free to complete.
 *
 * @par Description
 * Waits for the HW's port free to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_sport_wait_port_free(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_sport_state_decl();

	sport_sm_trace(sport);

	switch(evt) {
	case OCS_EVT_SPORT_ATTACH_OK:
		/* Ignore as we are waiting for the free CB */
		break;
	case OCS_EVT_SPORT_FREE_OK: {
		/* All done, free myself */
		ocs_sport_free(sport);
		break;
	}
	default:
		__ocs_sport_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup sport_sm
 * @brief Start the vports on a domain
 *
 * @par Description
 * Use the vport specification to find the associated vports and start them.
 *
 * @param domain Pointer to the domain context.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */
int32_t
ocs_vport_start(ocs_domain_t *domain)
{
	ocs_t *ocs = domain->ocs;
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;
	ocs_vport_spec_t *next;
	ocs_sport_t *sport;
	int32_t rc = 0;

	ocs_device_lock(ocs);
	ocs_list_foreach_safe(&xport->vport_list, vport, next) {
		if (vport->domain_instance == domain->instance_index &&
		    vport->sport == NULL) {
			/* If role not set, skip this vport */
			if (!(vport->enable_ini || vport->enable_tgt)) {
				continue;
			}

			/* Allocate a sport */
			vport->sport = sport = ocs_sport_alloc(domain, vport->wwpn, vport->wwnn, vport->fc_id,
							       vport->enable_ini, vport->enable_tgt);
			if (sport == NULL) {
				rc = -1;
			} else {
				sport->is_vport = 1;
				sport->tgt_data = vport->tgt_data;
				sport->ini_data = vport->ini_data;

				/* Transition to vport_init */
				ocs_sm_transition(&sport->sm, __ocs_sport_vport_init, NULL);
			}
		}
	}
	ocs_device_unlock(ocs);
	return rc;
}

/**
 * @ingroup sport_sm
 * @brief Clear the sport reference in the vport specification.
 *
 * @par Description
 * Clear the sport pointer on the vport specification when the vport is torn down. This allows it to be
 * re-created when the link is re-established.
 *
 * @param sport Pointer to the sport context.
 */
static void
ocs_vport_link_down(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;

	ocs_device_lock(ocs);
	ocs_list_foreach(&xport->vport_list, vport) {
		if (vport->sport == sport) {
			vport->sport = NULL;
			break;
		}
	}
	ocs_device_unlock(ocs);
}

/**
 * @ingroup sport_sm
 * @brief Allocate a new virtual SLI port.
 *
 * @par Description
 * A new sport is created, in response to an external management request.
 *
 * @n @b Note: If the WWPN is zero, the firmware will assign the WWNs.
 *
 * @param domain Pointer to the domain context.
 * @param wwpn World wide port name.
 * @param wwnn World wide node name
 * @param fc_id Requested port ID (used in fabric emulation mode).
 * @param ini TRUE, if port is created as an initiator node.
 * @param tgt TRUE, if port is created as a target node.
 * @param tgt_data Pointer to target specific data
 * @param ini_data Pointer to initiator specific data
 * @param restore_vport If TRUE, then the vport will be re-created automatically
 *                      on link disruption.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

int32_t
ocs_sport_vport_new(ocs_domain_t *domain, uint64_t wwpn, uint64_t wwnn,
		    uint32_t fc_id, uint8_t ini, uint8_t tgt, void *tgt_data,
		    void *ini_data, uint8_t restore_vport)
{
	ocs_sport_t *sport;

	if (ini && (domain->ocs->enable_ini == 0)) {
		ocs_log_test(domain->ocs, "driver initiator functionality not enabled\n");
		return -1;
	}

	if (tgt && (domain->ocs->enable_tgt == 0)) {
		ocs_log_test(domain->ocs, "driver target functionality not enabled\n");
		return -1;
	}

	/* Create a vport spec if we need to recreate this vport after a link up event */
	if (restore_vport) {
		if (ocs_vport_create_spec(domain->ocs, wwnn, wwpn, fc_id, ini, tgt, tgt_data, ini_data)) {
			ocs_log_test(domain->ocs, "failed to create vport object entry\n");
			return -1;
		}
		return ocs_vport_start(domain);
	}

	/* Allocate a sport */
	sport = ocs_sport_alloc(domain, wwpn, wwnn, fc_id, ini, tgt);

	if (sport == NULL) {
		return -1;
	}

	sport->is_vport = 1;
	sport->tgt_data = tgt_data;
	sport->ini_data = ini_data;
	
	/* Transition to vport_init */
	ocs_sm_transition(&sport->sm, __ocs_sport_vport_init, NULL);

	return 0;
}

int32_t
ocs_sport_vport_alloc(ocs_domain_t *domain, ocs_vport_spec_t *vport)
{
	ocs_sport_t *sport = NULL;

	if (domain == NULL) {
		return (0);
	}

	ocs_assert((vport->sport == NULL), -1);	

	/* Allocate a sport */
	vport->sport = sport = ocs_sport_alloc(domain, vport->wwpn, vport->wwnn, UINT32_MAX, vport->enable_ini, vport->enable_tgt);

	if (sport == NULL) {
		return -1;
	}

	sport->is_vport = 1;
	sport->tgt_data = vport->tgt_data;
	sport->ini_data = vport->tgt_data;

	/* Transition to vport_init */
	ocs_sm_transition(&sport->sm, __ocs_sport_vport_init, NULL);

	return (0);
}

/**
 * @ingroup sport_sm
 * @brief Remove a previously-allocated virtual port.
 *
 * @par Description
 * A previously-allocated virtual port is removed by posting the shutdown event to the
 * sport with a matching WWN.
 *
 * @param ocs Pointer to the device object.
 * @param domain Pointer to the domain structure (may be NULL).
 * @param wwpn World wide port name of the port to delete (host endian).
 * @param wwnn World wide node name of the port to delete (host endian).
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t ocs_sport_vport_del(ocs_t *ocs, ocs_domain_t *domain, uint64_t wwpn, uint64_t wwnn)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_sport_t *sport;
	int found = 0;
	ocs_vport_spec_t *vport;
	ocs_vport_spec_t *next;
	uint32_t instance;

	/* If no domain is given, use instance 0, otherwise use domain instance */
	if (domain == NULL) {
		instance = 0;
	} else {
		instance = domain->instance_index;
	}

	/* walk the ocs_vport_list and remove from there */

	ocs_device_lock(ocs);
		ocs_list_foreach_safe(&xport->vport_list, vport, next) {
			if ((vport->domain_instance == instance) &&
				(vport->wwpn == wwpn) && (vport->wwnn == wwnn)) {
				vport->sport = NULL;
				break;
			}
		}
	ocs_device_unlock(ocs);

	if (domain == NULL) {
		/* No domain means no sport to look for */
		return 0;
	}

	ocs_domain_lock(domain);
		ocs_list_foreach(&domain->sport_list, sport) {
			if ((sport->wwpn == wwpn) && (sport->wwnn == wwnn)) {
				found = 1;
				break;
			}
		}
		if (found) {
			/* Shutdown this SPORT */
			ocs_sm_post_event(&sport->sm, OCS_EVT_SHUTDOWN, NULL);
		}
	ocs_domain_unlock(domain);
	return 0;
}

/**
 * @brief Force free all saved vports.
 *
 * @par Description
 * Delete all device vports.
 *
 * @param ocs Pointer to the device object.
 *
 * @return None.
 */

void
ocs_vport_del_all(ocs_t *ocs)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;
	ocs_vport_spec_t *next;

	ocs_device_lock(ocs);
		ocs_list_foreach_safe(&xport->vport_list, vport, next) {
			ocs_list_remove(&xport->vport_list, vport);
			ocs_free(ocs, vport, sizeof(*vport));
		}
	ocs_device_unlock(ocs);
}

/**
 * @ingroup sport_sm
 * @brief Generate a SLI port ddump.
 *
 * @par Description
 * Generates the SLI port ddump data.
 *
 * @param textbuf Pointer to the text buffer.
 * @param sport Pointer to the SLI-4 port.
 *
 * @return Returns 0 on success, or a negative value on failure.
 */

int
ocs_ddump_sport(ocs_textbuf_t *textbuf, ocs_sli_port_t *sport)
{
	ocs_node_t *node;
	ocs_node_group_dir_t *node_group_dir;
	int retval = 0;

	ocs_ddump_section(textbuf, "sport", sport->instance_index);
	ocs_ddump_value(textbuf, "display_name", "%s", sport->display_name);

	ocs_ddump_value(textbuf, "is_vport", "%d", sport->is_vport);
	ocs_ddump_value(textbuf, "enable_ini", "%d", sport->enable_ini);
	ocs_ddump_value(textbuf, "enable_tgt", "%d", sport->enable_tgt);
	ocs_ddump_value(textbuf, "shutting_down", "%d", sport->shutting_down);
	ocs_ddump_value(textbuf, "topology", "%d", sport->topology);
	ocs_ddump_value(textbuf, "p2p_winner", "%d", sport->p2p_winner);
	ocs_ddump_value(textbuf, "p2p_port_id", "%06x", sport->p2p_port_id);
	ocs_ddump_value(textbuf, "p2p_remote_port_id", "%06x", sport->p2p_remote_port_id);
	ocs_ddump_value(textbuf, "wwpn", "%016llx", (unsigned long long)sport->wwpn);
	ocs_ddump_value(textbuf, "wwnn", "%016llx", (unsigned long long)sport->wwnn);
	/*TODO: service_params */

	ocs_ddump_value(textbuf, "indicator", "x%x", sport->indicator);
	ocs_ddump_value(textbuf, "fc_id", "x%06x", sport->fc_id);
	ocs_ddump_value(textbuf, "index", "%d", sport->index);

	ocs_display_sparams(NULL, "sport_sparams", 1, textbuf, sport->service_params+4);

	/* HLM dump */
	ocs_ddump_section(textbuf, "hlm", sport->instance_index);
	ocs_lock(&sport->node_group_lock);
		ocs_list_foreach(&sport->node_group_dir_list, node_group_dir) {
			ocs_remote_node_group_t *remote_node_group;

			ocs_ddump_section(textbuf, "node_group_dir", node_group_dir->instance_index);

			ocs_ddump_value(textbuf, "node_group_list_count", "%d", node_group_dir->node_group_list_count);
			ocs_ddump_value(textbuf, "next_idx", "%d", node_group_dir->next_idx);
			ocs_list_foreach(&node_group_dir->node_group_list, remote_node_group) {
				ocs_ddump_section(textbuf, "node_group", remote_node_group->instance_index);
				ocs_ddump_value(textbuf, "indicator", "x%x", remote_node_group->indicator);
				ocs_ddump_value(textbuf, "index", "x%x", remote_node_group->index);
				ocs_ddump_value(textbuf, "instance_index", "x%x", remote_node_group->instance_index);
				ocs_ddump_endsection(textbuf, "node_group", 0);
			}
			ocs_ddump_endsection(textbuf, "node_group_dir", 0);
		}
	ocs_unlock(&sport->node_group_lock);
	ocs_ddump_endsection(textbuf, "hlm", sport->instance_index);

	ocs_scsi_ini_ddump(textbuf, OCS_SCSI_DDUMP_SPORT, sport);
	ocs_scsi_tgt_ddump(textbuf, OCS_SCSI_DDUMP_SPORT, sport);

	/* Dump all the nodes */
	if (ocs_sport_lock_try(sport) != TRUE) {
		/* Didn't get lock */
		return -1;
	}
		/* Here the sport lock is held */
		ocs_list_foreach(&sport->node_list, node) {
			retval = ocs_ddump_node(textbuf, node);
			if (retval != 0) {
				break;
			}
		}
	ocs_sport_unlock(sport);

	ocs_ddump_endsection(textbuf, "sport", sport->index);

	return retval;
}


void
ocs_mgmt_sport_list(ocs_textbuf_t *textbuf, void *object)
{
	ocs_node_t *node;
	ocs_sport_t *sport = (ocs_sport_t *)object;

	ocs_mgmt_start_section(textbuf, "sport", sport->instance_index);

	/* Add my status values to textbuf */
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "indicator");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "fc_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "index");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "display_name");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "is_vport");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "enable_ini");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "enable_tgt");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "p2p");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "p2p_winner");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "p2p_port_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "p2p_remote_port_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "wwpn");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "wwnn");

	if (ocs_sport_lock_try(sport) == TRUE) {

		/* If we get here, then we are holding the sport lock */
		ocs_list_foreach(&sport->node_list, node) {
			if ((node->mgmt_functions) && (node->mgmt_functions->get_list_handler)) {
				node->mgmt_functions->get_list_handler(textbuf, node);
			}

		}
		ocs_sport_unlock(sport);
	}

	ocs_mgmt_end_section(textbuf, "sport", sport->instance_index);
}

int
ocs_mgmt_sport_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *object)
{
	ocs_node_t *node;
	ocs_sport_t *sport = (ocs_sport_t *)object;
	char qualifier[80];
	int retval = -1;

	ocs_mgmt_start_section(textbuf, "sport", sport->instance_index);

	snprintf(qualifier, sizeof(qualifier), "%s/sport[%d]", parent, sport->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {
		char *unqualified_name = name + strlen(qualifier) +1;

		/* See if it's a value I can supply */
		if (ocs_strcmp(unqualified_name, "indicator") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "0x%x", sport->indicator);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "fc_id") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fc_id", "0x%06x", sport->fc_id);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "index") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "index", "%d", sport->index);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "display_name") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", sport->display_name);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "is_vport") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_vport",  sport->is_vport);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "enable_ini") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "enable_ini",  sport->enable_ini);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "enable_tgt") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "enable_tgt",  sport->enable_tgt);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "p2p_winner") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "p2p_winner",  sport->p2p_winner);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "p2p_port_id") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "p2p_port_id", "0x%06x", sport->p2p_port_id);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "p2p_remote_port_id") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "p2p_remote_port_id", "0x%06x", sport->p2p_remote_port_id);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "wwpn") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwpn", "0x%016llx", (unsigned long long)sport->wwpn);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "wwnn") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwnn", "0x%016llx", (unsigned long long)sport->wwnn);
			retval = 0;
		} else {
			/* If I didn't know the value of this status pass the request to each of my children */
			ocs_sport_lock(sport);
				ocs_list_foreach(&sport->node_list, node) {
					if ((node->mgmt_functions) && (node->mgmt_functions->get_handler)) {
						retval = node->mgmt_functions->get_handler(textbuf, qualifier, name, node);
					}

					if (retval == 0) {
						break;
					}
				}
			ocs_sport_unlock(sport);
		}
	}

	ocs_mgmt_end_section(textbuf, "sport", sport->instance_index);

	return retval;
}

void
ocs_mgmt_sport_get_all(ocs_textbuf_t *textbuf, void *object)
{
	ocs_node_t *node;
	ocs_sport_t *sport = (ocs_sport_t *)object;

	ocs_mgmt_start_section(textbuf, "sport", sport->instance_index);

	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "0x%x", sport->indicator);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fc_id", "0x%06x", sport->fc_id);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "index", "%d", sport->index);
	ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", sport->display_name);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_vport",  sport->is_vport);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "enable_ini",  sport->enable_ini);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "enable_tgt",  sport->enable_tgt);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "p2p_winner",  sport->p2p_winner);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "p2p_port_id", "0x%06x", sport->p2p_port_id);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "p2p_remote_port_id", "0x%06x", sport->p2p_remote_port_id);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwpn", "0x%016llx" , (unsigned long long)sport->wwpn);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "wwnn", "0x%016llx", (unsigned long long)sport->wwnn);

	ocs_sport_lock(sport);
	ocs_list_foreach(&sport->node_list, node) {
		if ((node->mgmt_functions) && (node->mgmt_functions->get_all_handler)) {
			node->mgmt_functions->get_all_handler(textbuf, node);
		}
	}
	ocs_sport_unlock(sport);

	ocs_mgmt_end_section(textbuf, "sport", sport->instance_index);
}

int
ocs_mgmt_sport_set(char *parent, char *name, char *value, void *object)
{
	ocs_node_t *node;
	ocs_sport_t *sport = (ocs_sport_t *)object;
	char qualifier[80];
	int retval = -1;

	snprintf(qualifier, sizeof(qualifier), "%s/sport[%d]", parent, sport->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {
		/* The sport has no settable values.  Pass the request to each node. */

		ocs_sport_lock(sport);
		ocs_list_foreach(&sport->node_list, node) {
			if ((node->mgmt_functions) && (node->mgmt_functions->set_handler)) {
				retval = node->mgmt_functions->set_handler(qualifier, name, value, node);
			}
			if (retval == 0) {
				break;
			}
		}
		ocs_sport_unlock(sport);
	}

	return retval;
}


int
ocs_mgmt_sport_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
		    void *arg_out, uint32_t arg_out_length, void *object)
{
	ocs_node_t *node;
	ocs_sport_t *sport = (ocs_sport_t *)object;
	char qualifier[80];
	int retval = -1;

	snprintf(qualifier, sizeof(qualifier), "%s.sport%d", parent, sport->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(action, qualifier, strlen(qualifier)) == 0) {

		/* See if it's an action I can perform */

		/* if (ocs_strcmp ....
		 * {
		 * } else
		 */

		{
			/* If I didn't know how to do this action pass the request to each of my children */
			ocs_sport_lock(sport);
				ocs_list_foreach(&sport->node_list, node) {
					if ((node->mgmt_functions) && (node->mgmt_functions->exec_handler)) {
						retval = node->mgmt_functions->exec_handler(qualifier, action, arg_in, arg_in_length,
											    arg_out, arg_out_length, node);
					}

					if (retval == 0) {
						break;
					}

				}
			ocs_sport_unlock(sport);
		}
	}

	return retval;
}

/**
 * @brief Save the virtual port's parameters.
 *
 * @par Description
 * The information required to restore a virtual port is saved.
 *
 * @param sport Pointer to the sport context.
 *
 * @return None.
 */

static void
ocs_vport_update_spec(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;

	ocs_device_lock(ocs);
	ocs_list_foreach(&xport->vport_list, vport) {
		if (vport->sport == sport) {
			vport->wwnn = sport->wwnn;
			vport->wwpn = sport->wwpn;
			vport->tgt_data = sport->tgt_data;
			vport->ini_data = sport->ini_data;
			break;
		}
	}
	ocs_device_unlock(ocs);
}

/**
 * @brief Create a saved vport entry.
 *
 * A saved vport entry is added to the vport list, which is restored following
 * a link up. This function is used to allow vports to be created the first time
 * the link comes up without having to go through the ioctl() API.
 *
 * @param ocs Pointer to device context.
 * @param wwnn World wide node name (may be zero for auto-select).
 * @param wwpn World wide port name (may be zero for auto-select).
 * @param fc_id Requested port ID (used in fabric emulation mode).
 * @param enable_ini TRUE if vport is to be an initiator port.
 * @param enable_tgt TRUE if vport is to be a target port.
 * @param tgt_data Pointer to target specific data.
 * @param ini_data Pointer to initiator specific data.
 *
 * @return None.
 */

int8_t 
ocs_vport_create_spec(ocs_t *ocs, uint64_t wwnn, uint64_t wwpn, uint32_t fc_id, uint32_t enable_ini, uint32_t enable_tgt, void *tgt_data, void *ini_data)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;

	/* walk the ocs_vport_list and return failure if a valid(vport with non zero WWPN and WWNN) vport entry 
	   is already created */
	ocs_list_foreach(&xport->vport_list, vport) {
		if ((wwpn && (vport->wwpn == wwpn)) && (wwnn && (vport->wwnn == wwnn))) {
			ocs_log_test(ocs, "Failed: VPORT %016llx  %016llx already allocated\n",
				     (unsigned long long)wwnn, (unsigned long long)wwpn);
			return -1;
		}
	}

	vport = ocs_malloc(ocs, sizeof(*vport), OCS_M_ZERO | OCS_M_NOWAIT);
	if (vport == NULL) {
		ocs_log_err(ocs, "ocs_malloc failed\n");
		return -1;
	}

	vport->wwnn = wwnn;
	vport->wwpn = wwpn;
	vport->fc_id = fc_id;
	vport->domain_instance = 0;	/*TODO: may need to change this */
	vport->enable_tgt = enable_tgt;
	vport->enable_ini = enable_ini;
	vport->tgt_data = tgt_data;
	vport->ini_data = ini_data;

	ocs_device_lock(ocs);
		ocs_list_add_tail(&xport->vport_list, vport);
	ocs_device_unlock(ocs);
	return 0;
}

/* node group api */

/**
 * @brief Perform the AND operation on source vectors.
 *
 * @par Description
 * Performs an AND operation on the 8-bit values in source vectors @c b and @c c.
 * The resulting value is stored in @c a.
 *
 * @param a Destination-byte vector.
 * @param b Source-byte vector.
 * @param c Source-byte vector.
 * @param n Byte count.
 *
 * @return None.
 */

static void
and8(uint8_t *a, uint8_t *b, uint8_t *c, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n; i ++) {
		*a = *b & *c;
		a++;
		b++;
		c++;
	}
}

/**
 * @brief Service parameters mask data.
 */
static fc_sparms_t sparms_cmp_mask = {
	0,			/*uint32_t	command_code: 8, */
	0,			/*		resv1: 24; */
	{~0, ~0, ~0, ~0},	/* uint32_t	common_service_parameters[4]; */
	0,			/* uint32_t	port_name_hi; */
	0,			/* uint32_t	port_name_lo; */
	0,			/* uint32_t	node_name_hi; */
	0,			/* uint32_t	node_name_lo; */
	{~0, ~0, ~0, ~0},	/* uint32_t	class1_service_parameters[4]; */
	{~0, ~0, ~0, ~0},	/* uint32_t	class2_service_parameters[4]; */
	{~0, ~0, ~0, ~0},	/* uint32_t	class3_service_parameters[4]; */
	{~0, ~0, ~0, ~0},	/* uint32_t	class4_service_parameters[4]; */
	{~0, ~0, ~0, ~0}};	/* uint32_t	vendor_version_level[4]; */

/**
 * @brief Compare service parameters.
 *
 * @par Description
 * Returns 0 if the two service parameters are the same, excluding the port/node name
 * elements.
 *
 * @param sp1 Pointer to service parameters 1.
 * @param sp2 Pointer to service parameters 2.
 *
 * @return Returns 0 if parameters match; otherwise, returns a positive or negative value,
 * depending on the arithmetic magnitude of the first mismatching byte.
 */

int
ocs_sparm_cmp(uint8_t *sp1, uint8_t *sp2)
{
	int i;
	int v;
	uint8_t *sp3 = (uint8_t*) &sparms_cmp_mask;

	for (i = 0; i < OCS_SERVICE_PARMS_LENGTH; i ++) {
		v = ((int)(sp1[i] & sp3[i])) - ((int)(sp2[i] & sp3[i]));
		if (v) {
			break;
		}
	}
	return v;
}

/**
 * @brief Allocate a node group directory entry.
 *
 * @par Description
 * A node group directory entry is allocated, initialized, and added to the sport's
 * node group directory list.
 *
 * @param sport Pointer to the sport object.
 * @param sparms Pointer to the service parameters.
 *
 * @return Returns a pointer to the allocated ocs_node_group_dir_t; or NULL.
 */

ocs_node_group_dir_t *
ocs_node_group_dir_alloc(ocs_sport_t *sport, uint8_t *sparms)
{
	ocs_node_group_dir_t *node_group_dir;

	node_group_dir = ocs_malloc(sport->ocs, sizeof(*node_group_dir), OCS_M_ZERO | OCS_M_NOWAIT);
	if (node_group_dir != NULL) {
		node_group_dir->sport = sport;

		ocs_lock(&sport->node_group_lock);
			node_group_dir->instance_index = sport->node_group_dir_next_instance++;
			and8(node_group_dir->service_params, sparms, (uint8_t*)&sparms_cmp_mask, OCS_SERVICE_PARMS_LENGTH);
			ocs_list_init(&node_group_dir->node_group_list, ocs_remote_node_group_t, link);

			node_group_dir->node_group_list_count = 0;
			node_group_dir->next_idx = 0;
			ocs_list_add_tail(&sport->node_group_dir_list, node_group_dir);
		ocs_unlock(&sport->node_group_lock);

		ocs_log_debug(sport->ocs, "[%s] [%d] allocating node group directory\n", sport->display_name,
			node_group_dir->instance_index);
	}
	return node_group_dir;
}

/**
 * @brief Free a node group directory entry.
 *
 * @par Description
 * The node group directory entry @c node_group_dir is removed
 * from the sport's node group directory list and freed.
 *
 * @param node_group_dir Pointer to the node group directory entry.
 *
 * @return None.
 */

void
ocs_node_group_dir_free(ocs_node_group_dir_t *node_group_dir)
{
	ocs_sport_t *sport;
	if (node_group_dir != NULL) {
		sport = node_group_dir->sport;
		ocs_log_debug(sport->ocs, "[%s] [%d] freeing node group directory\n", sport->display_name,
			node_group_dir->instance_index);
		ocs_lock(&sport->node_group_lock);
			if (!ocs_list_empty(&node_group_dir->node_group_list)) {
				ocs_log_test(sport->ocs, "[%s] WARNING: node group list not empty\n", sport->display_name);
			}
			ocs_list_remove(&sport->node_group_dir_list, node_group_dir);
		ocs_unlock(&sport->node_group_lock);
		ocs_free(sport->ocs, node_group_dir, sizeof(*node_group_dir));
	}
}

/**
 * @brief Find a matching node group directory entry.
 *
 * @par Description
 * The sport's node group directory list is searched for a matching set of
 * service parameters. The first matching entry is returned; otherwise
 * NULL is returned.
 *
 * @param sport Pointer to the sport object.
 * @param sparms Pointer to the sparams to match.
 *
 * @return Returns a pointer to the first matching entry found; or NULL.
 */

ocs_node_group_dir_t *
ocs_node_group_dir_find(ocs_sport_t *sport, uint8_t *sparms)
{
	ocs_node_group_dir_t *node_dir = NULL;

	ocs_lock(&sport->node_group_lock);
		ocs_list_foreach(&sport->node_group_dir_list, node_dir) {
			if (ocs_sparm_cmp(sparms, node_dir->service_params) == 0) {
				ocs_unlock(&sport->node_group_lock);
				return node_dir;
			}
		}
	ocs_unlock(&sport->node_group_lock);
	return NULL;
}

/**
 * @brief Allocate a remote node group object.
 *
 * @par Description
 * A remote node group object is allocated, initialized, and placed on the node group
 * list of @c node_group_dir. The HW remote node group @b alloc function is called.
 *
 * @param node_group_dir Pointer to the node group directory.
 *
 * @return Returns a pointer to the allocated remote node group object; or NULL.
 */

ocs_remote_node_group_t *
ocs_remote_node_group_alloc(ocs_node_group_dir_t *node_group_dir)
{
	ocs_t *ocs;
	ocs_sport_t *sport;
	ocs_remote_node_group_t *node_group;
	ocs_hw_rtn_e hrc;

	ocs_assert(node_group_dir, NULL);
	ocs_assert(node_group_dir->sport, NULL);
	ocs_assert(node_group_dir->sport->ocs, NULL);

	sport = node_group_dir->sport;
	ocs = sport->ocs;


	node_group = ocs_malloc(ocs, sizeof(*node_group), OCS_M_ZERO | OCS_M_NOWAIT);
	if (node_group != NULL) {

		/* set pointer to node group directory */
		node_group->node_group_dir = node_group_dir;

		ocs_lock(&node_group_dir->sport->node_group_lock);
			node_group->instance_index = sport->node_group_next_instance++;
		ocs_unlock(&node_group_dir->sport->node_group_lock);

		/* invoke HW node group inialization */
		hrc = ocs_hw_node_group_alloc(&ocs->hw, node_group);
		if (hrc != OCS_HW_RTN_SUCCESS) {
			ocs_log_err(ocs, "ocs_hw_node_group_alloc() failed: %d\n", hrc);
			ocs_free(ocs, node_group, sizeof(*node_group));
			return NULL;
		}

		ocs_log_debug(ocs, "[%s] [%d] indicator x%03x allocating node group\n", sport->display_name,
			node_group->indicator, node_group->instance_index);

			/* add to the node group directory entry node group list */
		ocs_lock(&node_group_dir->sport->node_group_lock);
			ocs_list_add_tail(&node_group_dir->node_group_list, node_group);
			node_group_dir->node_group_list_count ++;
		ocs_unlock(&node_group_dir->sport->node_group_lock);
	}
	return node_group;
}

/**
 * @brief Free a remote node group object.
 *
 * @par Description
 * The remote node group object @c node_group is removed from its
 * node group directory entry and freed.
 *
 * @param node_group Pointer to the remote node group object.
 *
 * @return None.
 */

void
ocs_remote_node_group_free(ocs_remote_node_group_t *node_group)
{
	ocs_sport_t *sport;
	ocs_node_group_dir_t *node_group_dir;

	if (node_group != NULL) {

		ocs_assert(node_group->node_group_dir);
		ocs_assert(node_group->node_group_dir->sport);
		ocs_assert(node_group->node_group_dir->sport->ocs);

		node_group_dir = node_group->node_group_dir;
		sport = node_group_dir->sport;

		ocs_log_debug(sport->ocs, "[%s] [%d] freeing node group\n", sport->display_name, node_group->instance_index);

		/* Remove from node group directory node group list */
		ocs_lock(&sport->node_group_lock);
			ocs_list_remove(&node_group_dir->node_group_list, node_group);
			node_group_dir->node_group_list_count --;
		/* TODO: note that we're going to have the node_group_dir entry persist forever ... we could delete it if
		 * the group_list_count goes to zero (or the linked list is empty */
		ocs_unlock(&sport->node_group_lock);
		ocs_free(sport->ocs, node_group, sizeof(*node_group));
	}
}

/**
 * @brief Initialize a node for high login mode.
 *
 * @par Description
 * The @c node is initialized for high login mode. The following steps are performed:
 * 1. The sports node group directory is searched for a matching set of service parameters.
 * 2. If a matching set is not found, a node group directory entry is allocated.
 * 3. If less than the @c hlm_group_size number of remote node group objects is present in the
 *   node group directory, a new remote node group object is allocated and added to the list.
 * 4. A remote node group object is selected, and the node is attached to the node group.
 *
 * @param node Pointer to the node.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int
ocs_node_group_init(ocs_node_t *node)
{
	ocs_t *ocs;
	ocs_sport_t *sport;
	ocs_node_group_dir_t *node_group_dir;
	ocs_remote_node_group_t *node_group;
	ocs_hw_rtn_e hrc;

	ocs_assert(node, -1);
	ocs_assert(node->sport, -1);
	ocs_assert(node->ocs, -1);

	ocs = node->ocs;
	sport = node->sport;

	ocs_assert(ocs->enable_hlm, -1);

	/* see if there's a node group directory allocated for this service parameter set */
	node_group_dir = ocs_node_group_dir_find(sport, node->service_params);
	if (node_group_dir == NULL) {
		/* not found, so allocate one */
		node_group_dir = ocs_node_group_dir_alloc(sport, node->service_params);
		if (node_group_dir == NULL) {
			/* node group directory allocation failed ... can't continue, however,
			 * the node will be allocated with a normal (not shared) RPI
			 */
			ocs_log_err(ocs, "ocs_node_group_dir_alloc() failed\n");
			return -1;
		}
	}

	/* check to see if we've allocated hlm_group_size's worth of node group structures for this
	 * directory entry, if not, then allocate and use a new one, otherwise pick the next one.
	 */
	ocs_lock(&node->sport->node_group_lock);
		if (node_group_dir->node_group_list_count < ocs->hlm_group_size) {
			ocs_unlock(&node->sport->node_group_lock);
				node_group = ocs_remote_node_group_alloc(node_group_dir);
			if (node_group == NULL) {
				ocs_log_err(ocs, "ocs_remote_node_group_alloc() failed\n");
				return -1;
			}
			ocs_lock(&node->sport->node_group_lock);
		} else {
			uint32_t idx = 0;

			ocs_list_foreach(&node_group_dir->node_group_list, node_group) {
				if (idx >= ocs->hlm_group_size) {
					ocs_log_err(node->ocs, "assertion failed: idx >= ocs->hlm_group_size\n");
					ocs_unlock(&node->sport->node_group_lock);
					return -1;
				}

				if (idx == node_group_dir->next_idx) {
					break;
				}
				idx ++;
			}
			if (idx == ocs->hlm_group_size) {
				node_group = ocs_list_get_head(&node_group_dir->node_group_list);
			}
			if (++node_group_dir->next_idx >= node_group_dir->node_group_list_count) {
				node_group_dir->next_idx = 0;
			}
		}
	ocs_unlock(&node->sport->node_group_lock);

	/* Initialize a pointer in the node back to the node group */
	node->node_group = node_group;

	/* Join this node into the group */
	hrc = ocs_hw_node_group_attach(&ocs->hw, node_group, &node->rnode);

	return (hrc == OCS_HW_RTN_SUCCESS) ? 0 : -1;
}


