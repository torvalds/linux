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
 * Handles the domain object callback from the HW.
 */

/*!
@defgroup domain_sm Domain State Machine: States
*/

#include "ocs.h"

#include "ocs_fabric.h"
#include "ocs_device.h"

#define domain_sm_trace(domain)  \
	do { \
		if (OCS_LOG_ENABLE_DOMAIN_SM_TRACE(domain->ocs)) \
			ocs_log_info(domain->ocs, "[domain] %-20s %-20s\n", __func__, ocs_sm_event_name(evt)); \
	} while (0)

#define domain_trace(domain, fmt, ...) \
	do { \
		if (OCS_LOG_ENABLE_DOMAIN_SM_TRACE(domain ? domain->ocs : NULL)) \
			ocs_log_info(domain ? domain->ocs : NULL, fmt, ##__VA_ARGS__); \
	} while (0)

#define domain_printf(domain, fmt, ...) \
	do { \
		ocs_log_info(domain ? domain->ocs : NULL, fmt, ##__VA_ARGS__); \
	} while (0)

void ocs_mgmt_domain_list(ocs_textbuf_t *textbuf, void *domain);
void ocs_mgmt_domain_get_all(ocs_textbuf_t *textbuf, void *domain);
int ocs_mgmt_domain_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *domain);
int ocs_mgmt_domain_set(char *parent, char *name, char *value, void *domain);
int ocs_mgmt_domain_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
		void *arg_out, uint32_t arg_out_length, void *domain);

static ocs_mgmt_functions_t domain_mgmt_functions = {
	.get_list_handler = ocs_mgmt_domain_list,
	.get_handler = ocs_mgmt_domain_get,
	.get_all_handler = ocs_mgmt_domain_get_all,
	.set_handler = ocs_mgmt_domain_set,
	.exec_handler = ocs_mgmt_domain_exec,
};



/**
 * @brief Accept domain callback events from the HW.
 *
 * <h3 class="desc">Description</h3>
 * HW calls this function with various domain-related events.
 *
 * @param arg Application-specified argument.
 * @param event Domain event.
 * @param data Event specific data.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

int32_t
ocs_domain_cb(void *arg, ocs_hw_domain_event_e event, void *data)
{
	ocs_t *ocs = arg;
	ocs_domain_t *domain = NULL;
	int32_t rc = 0;

	ocs_assert(data, -1);

	if (event != OCS_HW_DOMAIN_FOUND) {
		domain = data;
	}

	switch (event) {
	case OCS_HW_DOMAIN_FOUND: {
		uint64_t fcf_wwn = 0;
		ocs_domain_record_t *drec = data;
		ocs_assert(drec, -1);

		/* extract the fcf_wwn */
		fcf_wwn = ocs_be64toh(*((uint64_t*)drec->wwn));

		/* lookup domain, or allocate a new one if one doesn't exist already */
		domain = ocs_domain_find(ocs, fcf_wwn);
		if (domain == NULL) {
			domain = ocs_domain_alloc(ocs, fcf_wwn);
			if (domain == NULL) {
				ocs_log_err(ocs, "ocs_domain_alloc() failed\n");
				rc = -1;
				break;
			}
			ocs_sm_transition(&domain->drvsm, __ocs_domain_init, NULL);
		}
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_FOUND, drec);
		break;
	}

	case OCS_HW_DOMAIN_LOST:
		domain_trace(domain, "OCS_HW_DOMAIN_LOST:\n");
		ocs_domain_hold_frames(domain);
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_LOST, NULL);
		break;

	case OCS_HW_DOMAIN_ALLOC_OK: {
		domain_trace(domain, "OCS_HW_DOMAIN_ALLOC_OK:\n");
		domain->instance_index = 0;
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_ALLOC_OK, NULL);
		break;
	}

	case OCS_HW_DOMAIN_ALLOC_FAIL:
		domain_trace(domain, "OCS_HW_DOMAIN_ALLOC_FAIL:\n");
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_ALLOC_FAIL, NULL);
		break;

	case OCS_HW_DOMAIN_ATTACH_OK:
		domain_trace(domain, "OCS_HW_DOMAIN_ATTACH_OK:\n");
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_ATTACH_OK, NULL);
		break;

	case OCS_HW_DOMAIN_ATTACH_FAIL:
		domain_trace(domain, "OCS_HW_DOMAIN_ATTACH_FAIL:\n");
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_ATTACH_FAIL, NULL);
		break;

	case OCS_HW_DOMAIN_FREE_OK:
		domain_trace(domain, "OCS_HW_DOMAIN_FREE_OK:\n");
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_FREE_OK, NULL);
		break;

	case OCS_HW_DOMAIN_FREE_FAIL:
		domain_trace(domain, "OCS_HW_DOMAIN_FREE_FAIL:\n");
		ocs_domain_post_event(domain, OCS_EVT_DOMAIN_FREE_FAIL, NULL);
		break;

	default:
		ocs_log_warn(ocs, "unsupported event %#x\n", event);
	}

	return rc;
}


/**
 * @brief Find the domain, given its FCF_WWN.
 *
 * <h3 class="desc">Description</h3>
 * Search the domain_list to find a matching domain object.
 *
 * @param ocs Pointer to the OCS device.
 * @param fcf_wwn FCF WWN to find.
 *
 * @return Returns the pointer to the domain if found; or NULL otherwise.
 */

ocs_domain_t *
ocs_domain_find(ocs_t *ocs, uint64_t fcf_wwn)
{
	ocs_domain_t *domain = NULL;

	/* Check to see if this domain is already allocated */
	ocs_device_lock(ocs);
		ocs_list_foreach(&ocs->domain_list, domain) {
			if (fcf_wwn == domain->fcf_wwn) {
				break;
			}
		}
	ocs_device_unlock(ocs);
	return domain;
}

/**
 * @brief Allocate a domain object.
 *
 * <h3 class="desc">Description</h3>
 * A domain object is allocated and initialized. It is associated with the
 * \c ocs argument.
 *
 * @param ocs Pointer to the OCS device.
 * @param fcf_wwn FCF WWN of the domain.
 *
 * @return Returns a pointer to the ocs_domain_t object; or NULL.
 */

ocs_domain_t *
ocs_domain_alloc(ocs_t *ocs, uint64_t fcf_wwn)
{
	ocs_domain_t *domain;

	ocs_assert(ocs, NULL);

	domain = ocs_malloc(ocs, sizeof(*domain), OCS_M_NOWAIT | OCS_M_ZERO);
	if (domain) {

		domain->ocs = ocs;
		domain->instance_index = ocs->domain_instance_count++;
		domain->drvsm.app = domain;
		ocs_domain_lock_init(domain);
		ocs_lock_init(ocs, &domain->lookup_lock, "Domain lookup[%d]", domain->instance_index);

		/* Allocate a sparse vector for sport FC_ID's */
		domain->lookup = spv_new(ocs);
		if (domain->lookup == NULL) {
			ocs_log_err(ocs, "spv_new() failed\n");
			ocs_free(ocs, domain, sizeof(*domain));
			return NULL;
		}

		ocs_list_init(&domain->sport_list, ocs_sport_t, link);
		domain->fcf_wwn = fcf_wwn;
		ocs_log_debug(ocs, "Domain allocated: wwn %016" PRIX64 "\n", domain->fcf_wwn);
		domain->femul_enable = (ocs->ctrlmask & OCS_CTRLMASK_ENABLE_FABRIC_EMULATION) != 0;

		ocs_device_lock(ocs);
			/* if this is the first domain, then assign it as the "root" domain */
			if (ocs_list_empty(&ocs->domain_list)) {
				ocs->domain = domain;
			}
			ocs_list_add_tail(&ocs->domain_list, domain);
		ocs_device_unlock(ocs);

		domain->mgmt_functions = &domain_mgmt_functions;
	} else {
		ocs_log_err(ocs, "domain allocation failed\n");
	}


	return domain;
}

/**
 * @brief Free a domain object.
 *
 * <h3 class="desc">Description</h3>
 * The domain object is freed.
 *
 * @param domain Domain object to free.
 *
 * @return None.
 */

void
ocs_domain_free(ocs_domain_t *domain)
{
	ocs_t *ocs;

	ocs_assert(domain);
	ocs_assert(domain->ocs);

	/* Hold frames to clear the domain pointer from the xport lookup */
	ocs_domain_hold_frames(domain);

	ocs = domain->ocs;

	ocs_log_debug(ocs, "Domain free: wwn %016" PRIX64 "\n", domain->fcf_wwn);

	spv_del(domain->lookup);
	domain->lookup = NULL;

	ocs_device_lock(ocs);
		ocs_list_remove(&ocs->domain_list, domain);
		if (domain == ocs->domain) {
			/* set global domain to the new head */
			ocs->domain = ocs_list_get_head(&ocs->domain_list);
			if (ocs->domain) {
				ocs_log_debug(ocs, "setting new domain, old=%p new=%p\n",
						domain, ocs->domain);
			}
		}

		if (ocs_list_empty(&ocs->domain_list) && ocs->domain_list_empty_cb ) {
			(*ocs->domain_list_empty_cb)(ocs, ocs->domain_list_empty_cb_arg);
		}
	ocs_device_unlock(ocs);

	ocs_lock_free(&domain->lookup_lock);

	ocs_free(ocs, domain, sizeof(*domain));
}

/**
 * @brief Free memory resources of a domain object.
 *
 * <h3 class="desc">Description</h3>
 * After the domain object is freed, its child objects are also freed.
 *
 * @param domain Pointer to a domain object.
 *
 * @return None.
 */

void
ocs_domain_force_free(ocs_domain_t *domain)
{
	ocs_sport_t *sport;
	ocs_sport_t *next;

	/* Shutdown domain sm */
	ocs_sm_disable(&domain->drvsm);

	ocs_scsi_notify_domain_force_free(domain);

	ocs_domain_lock(domain);
		ocs_list_foreach_safe(&domain->sport_list, sport, next) {
			ocs_sport_force_free(sport);
		}
	ocs_domain_unlock(domain);
	ocs_hw_domain_force_free(&domain->ocs->hw, domain);
	ocs_domain_free(domain);
}

/**
 * @brief Register a callback when the domain_list goes empty.
 *
 * <h3 class="desc">Description</h3>
 * A function callback may be registered when the domain_list goes empty.
 *
 * @param ocs Pointer to a device object.
 * @param callback Callback function.
 * @param arg Callback argument.
 *
 * @return None.
 */

void
ocs_register_domain_list_empty_cb(ocs_t *ocs, void (*callback)(ocs_t *ocs, void *arg), void *arg)
{
	ocs_device_lock(ocs);
		ocs->domain_list_empty_cb = callback;
		ocs->domain_list_empty_cb_arg = arg;
		if (ocs_list_empty(&ocs->domain_list) && callback) {
			(*callback)(ocs, arg);
		}
	ocs_device_unlock(ocs);
}

/**
 * @brief Return a pointer to the domain, given the instance index.
 *
 * <h3 class="desc">Description</h3>
 * A pointer to the domain context, given by the index, is returned.
 *
 * @param ocs Pointer to the driver instance context.
 * @param index Instance index.
 *
 * @return Returns a pointer to the domain; or NULL.
 */

ocs_domain_t *
ocs_domain_get_instance(ocs_t *ocs, uint32_t index)
{
	ocs_domain_t *domain = NULL;

	if (index >= OCS_MAX_DOMAINS) {
		ocs_log_err(ocs, "invalid index: %d\n", index);
		return NULL;
	}
	ocs_device_lock(ocs);
		ocs_list_foreach(&ocs->domain_list, domain) {
			if (domain->instance_index == index) {
				break;
			}
		}
	ocs_device_unlock(ocs);
	return domain;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Common event handler.
 *
 * <h3 class="desc">Description</h3>
 * Common/shared events are handled here for the domain state machine.
 *
 * @param funcname Function name text.
 * @param ctx Domain state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

static void *
__ocs_domain_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_domain_t *domain = ctx->app;

	switch(evt) {
	case OCS_EVT_ENTER:
	case OCS_EVT_REENTER:
	case OCS_EVT_EXIT:
	case OCS_EVT_ALL_CHILD_NODES_FREE:
		/* this can arise if an FLOGI fails on the SPORT, and the SPORT is shutdown */
		break;
	default:
		ocs_log_warn(domain->ocs, "%-20s %-20s not handled\n", funcname, ocs_sm_event_name(evt));
		break;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Common shutdown.
 *
 * <h3 class="desc">Description</h3>
 * Handles common shutdown events.
 *
 * @param funcname Function name text.
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

static void *
__ocs_domain_common_shutdown(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_domain_t *domain = ctx->app;

	switch(evt) {
	case OCS_EVT_ENTER:
	case OCS_EVT_REENTER:
	case OCS_EVT_EXIT:
		break;
	case OCS_EVT_DOMAIN_FOUND:
		ocs_assert(arg, NULL);
		/* sm: / save drec, mark domain_found_pending */
		ocs_memcpy(&domain->pending_drec, arg, sizeof(domain->pending_drec));
		domain->domain_found_pending = TRUE;
		break;
	case OCS_EVT_DOMAIN_LOST: 
		/* clear drec available
		 * sm: unmark domain_found_pending */
		domain->domain_found_pending = FALSE;
		break;

	default:
		ocs_log_warn(domain->ocs, "%-20s %-20s not handled\n", funcname, ocs_sm_event_name(evt));
		break;
	}

	return NULL;
}

#define std_domain_state_decl(...) \
	ocs_domain_t *domain = NULL; \
	ocs_t *ocs = NULL; \
	\
	ocs_assert(ctx, NULL); \
	ocs_assert(ctx->app, NULL); \
	domain = ctx->app; \
	ocs_assert(domain->ocs, NULL); \
	ocs = domain->ocs; \
	ocs_assert(ocs->xport, NULL);

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Initial state.
 *
 * <h3 class="desc">Description</h3>
 * The initial state for a domain. Each domain is initialized to
 * this state at start of day (SOD).
 *
 * @param ctx Domain state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_ENTER:
		domain->attached = 0;
		break;

	case OCS_EVT_DOMAIN_FOUND: {
		int32_t		vlan = 0;
		uint32_t	i;
		ocs_domain_record_t *drec = arg;
		ocs_sport_t *sport;

		uint64_t	my_wwnn = ocs->xport->req_wwnn;
		uint64_t	my_wwpn = ocs->xport->req_wwpn;
		uint64_t	be_wwpn;

		/* For now, user must specify both port name and node name, or we let firmware
		 * pick both (same as for vports).
		 * TODO: do we want to allow setting only port name or only node name?
		 */
		if ((my_wwpn == 0) || (my_wwnn == 0)) {
			ocs_log_debug(ocs, "using default hardware WWN configuration \n");
			my_wwpn = ocs_get_wwn(&ocs->hw, OCS_HW_WWN_PORT);
			my_wwnn = ocs_get_wwn(&ocs->hw, OCS_HW_WWN_NODE);
		}

		ocs_log_debug(ocs, "Creating base sport using WWPN %016" PRIx64 " WWNN %016" PRIx64 "\n",
			my_wwpn, my_wwnn);

		/* Allocate a sport and transition to __ocs_sport_allocated */
		sport = ocs_sport_alloc(domain, my_wwpn, my_wwnn, UINT32_MAX, ocs->enable_ini, ocs->enable_tgt);

		if (sport == NULL) {
			ocs_log_err(ocs, "ocs_sport_alloc() failed\n");
			break;
		}
		ocs_sm_transition(&sport->sm, __ocs_sport_allocated, NULL);

		/* If domain is ethernet, then fetch the vlan id value */
		if (drec->is_ethernet) {
			vlan = ocs_bitmap_search((void *)drec->map.vlan, TRUE, 512 * 8);
			if (vlan < 0) {
				ocs_log_err(ocs, "no VLAN id available (FCF=%d)\n",
						drec->index);
				break;
			}
		}

		be_wwpn = ocs_htobe64(sport->wwpn);

		/* allocate ocs_sli_port_t object for local port
		 * Note: drec->fc_id is ALPA from read_topology only if loop
		 */
		if (ocs_hw_port_alloc(&ocs->hw, sport, NULL, (uint8_t *)&be_wwpn)) {
			ocs_log_err(ocs, "Can't allocate port\n");
			ocs_sport_free(sport);
			break;
		}

		/* initialize domain object */
		domain->is_loop = drec->is_loop;
		domain->is_fc = drec->is_fc;

		/*
		 * If the loop position map includes ALPA == 0, then we are in a public loop (NL_PORT)
		 * Note that the first element of the loopmap[] contains the count of elements, and if
		 * ALPA == 0 is present, it will occupy the first location after the count.
		 */
		domain->is_nlport = drec->map.loop[1] == 0x00;

		if (domain->is_loop) {
			ocs_log_debug(ocs, "%s fc_id=%#x speed=%d\n",
					drec->is_loop ? (domain->is_nlport ? "public-loop" : "loop") : "other",
					drec->fc_id, drec->speed);

			sport->fc_id = drec->fc_id;
			sport->topology = OCS_SPORT_TOPOLOGY_LOOP;
			ocs_snprintf(sport->display_name, sizeof(sport->display_name), "s%06x", drec->fc_id);

			if (ocs->enable_ini) {
				uint32_t count = drec->map.loop[0];
				ocs_log_debug(ocs, "%d position map entries\n", count);
				for (i = 1; i <= count; i++) {
					if (drec->map.loop[i] != drec->fc_id) {
						ocs_node_t *node;

						ocs_log_debug(ocs, "%#x -> %#x\n",
								drec->fc_id, drec->map.loop[i]);
						node = ocs_node_alloc(sport, drec->map.loop[i], FALSE, TRUE);
						if (node == NULL) {
							ocs_log_err(ocs, "ocs_node_alloc() failed\n");
							break;
						}
						ocs_node_transition(node, __ocs_d_wait_loop, NULL);
					}
				}
			}
		}

		/* Initiate HW domain alloc */
		if (ocs_hw_domain_alloc(&ocs->hw, domain, drec->index, vlan)) {
			ocs_log_err(ocs, "Failed to initiate HW domain allocation\n");
			break;
		}
		ocs_sm_transition(ctx, __ocs_domain_wait_alloc, arg);
		break;
	}
	default:
		__ocs_domain_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Wait for the domain allocation to complete.
 *
 * <h3 class="desc">Description</h3>
 * Waits for the domain state to be allocated. After the HW domain
 * allocation process has been initiated, this state waits for
 * that process to complete (i.e. a domain-alloc-ok event).
 *
 * @param ctx Domain state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_wait_alloc(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_sport_t *sport;
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_DOMAIN_ALLOC_OK: {
		char prop_buf[32];
		uint64_t wwn_bump = 0;
		fc_plogi_payload_t *sp;

		if (ocs_get_property("wwn_bump", prop_buf, sizeof(prop_buf)) == 0) {
			wwn_bump = ocs_strtoull(prop_buf, 0, 0);
		}

		sport = domain->sport;
		ocs_assert(sport, NULL);
		sp = (fc_plogi_payload_t*) sport->service_params;

		/* Save the domain service parameters */
		ocs_memcpy(domain->service_params + 4, domain->dma.virt, sizeof(fc_plogi_payload_t) - 4);
		ocs_memcpy(sport->service_params + 4, domain->dma.virt, sizeof(fc_plogi_payload_t) - 4);

		/* If we're in fabric emulation mode, the flogi service parameters have not been setup yet,
		 * so we need some reasonable BB credit value
		 */
		if (domain->femul_enable) {
			ocs_memcpy(domain->flogi_service_params + 4, domain->service_params + 4, sizeof(fc_plogi_payload_t) - 4);
		}

		/* Update the sport's service parameters, user might have specified non-default names */
		sp->port_name_hi = ocs_htobe32((uint32_t) (sport->wwpn >> 32ll));
		sp->port_name_lo = ocs_htobe32((uint32_t) sport->wwpn);
		sp->node_name_hi = ocs_htobe32((uint32_t) (sport->wwnn >> 32ll));
		sp->node_name_lo = ocs_htobe32((uint32_t) sport->wwnn);

		if (wwn_bump) {
			sp->port_name_lo = ocs_htobe32(ocs_be32toh(sp->port_name_lo) ^ ((uint32_t)(wwn_bump)));
			sp->port_name_hi = ocs_htobe32(ocs_be32toh(sp->port_name_hi) ^ ((uint32_t)(wwn_bump >> 32)));
			sp->node_name_lo = ocs_htobe32(ocs_be32toh(sp->node_name_lo) ^ ((uint32_t)(wwn_bump)));
			sp->node_name_hi = ocs_htobe32(ocs_be32toh(sp->node_name_hi) ^ ((uint32_t)(wwn_bump >> 32)));
			ocs_log_info(ocs, "Overriding WWN\n");
		}

		/* Take the loop topology path, unless we are an NL_PORT (public loop) */
		if (domain->is_loop && !domain->is_nlport) {
			/*
			 * For loop, we already have our FC ID and don't need fabric login.
			 * Transition to the allocated state and post an event to attach to
			 * the domain. Note that this breaks the normal action/transition
			 * pattern here to avoid a race with the domain attach callback.
			 */
			/* sm: is_loop / domain_attach */
			ocs_sm_transition(ctx, __ocs_domain_allocated, NULL);
			__ocs_domain_attach_internal(domain, sport->fc_id);
			break;
		} else {
			ocs_node_t *node;

			/* alloc fabric node, send FLOGI */
			node = ocs_node_find(sport, FC_ADDR_FABRIC);
			if (node) {
				ocs_log_err(ocs, "Hmmmm ... Fabric Controller node already exists\n");
				break;
			}
			node = ocs_node_alloc(sport, FC_ADDR_FABRIC, FALSE, FALSE);
			if (!node) {
				ocs_log_err(ocs, "Error: ocs_node_alloc() failed\n");
			} else {
				if (ocs->nodedb_mask & OCS_NODEDB_PAUSE_FABRIC_LOGIN) {
					ocs_node_pause(node, __ocs_fabric_init);
				} else {
					ocs_node_transition(node, __ocs_fabric_init, NULL);
				}
			}
			/* Accept frames */
			domain->req_accept_frames = 1;
		}
		/* sm: start fabric logins */
		ocs_sm_transition(ctx, __ocs_domain_allocated, NULL);
		break;
	}

	case OCS_EVT_DOMAIN_ALLOC_FAIL:
		/* TODO: hw/device reset */
		ocs_log_err(ocs, "%s recv'd waiting for DOMAIN_ALLOC_OK; shutting down domain\n",
			ocs_sm_event_name(evt));
		domain->req_domain_free = 1;
		break;

	case OCS_EVT_DOMAIN_FOUND:
		/* Should not happen */
		ocs_assert(evt, NULL);
		break;

	case OCS_EVT_DOMAIN_LOST:
		ocs_log_debug(ocs, "%s received while waiting for ocs_hw_domain_alloc() to complete\n", ocs_sm_event_name(evt));
		ocs_sm_transition(ctx, __ocs_domain_wait_domain_lost, NULL);
		break;

	default:
		__ocs_domain_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Wait for the domain attach request.
 *
 * <h3 class="desc">Description</h3>
 * In this state, the domain has been allocated and is waiting for a domain attach request.
 * The attach request comes from a node instance completing the fabric login,
 * or from a point-to-point negotiation and login.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_allocated(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc = 0;
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_DOMAIN_REQ_ATTACH: {
		uint32_t fc_id;

		ocs_assert(arg, NULL);

		fc_id = *((uint32_t*)arg);
		ocs_log_debug(ocs, "Requesting hw domain attach fc_id x%x\n", fc_id);
		/* Update sport lookup */
		ocs_lock(&domain->lookup_lock);
			spv_set(domain->lookup, fc_id, domain->sport);
		ocs_unlock(&domain->lookup_lock);

		/* Update display name for the sport */
		ocs_node_fcid_display(fc_id, domain->sport->display_name, sizeof(domain->sport->display_name));

		/* Issue domain attach call */
		rc = ocs_hw_domain_attach(&ocs->hw, domain, fc_id);
		if (rc) {
			ocs_log_err(ocs, "ocs_hw_domain_attach failed: %d\n", rc);
			return NULL;
		}
		/* sm: / domain_attach */
		ocs_sm_transition(ctx, __ocs_domain_wait_attach, NULL);
		break;
	}

	case OCS_EVT_DOMAIN_FOUND:
		/* Should not happen */
		ocs_assert(evt, NULL);
		break;

	case OCS_EVT_DOMAIN_LOST: {
		int32_t rc;
		ocs_log_debug(ocs, "%s received while waiting for OCS_EVT_DOMAIN_REQ_ATTACH\n",
			ocs_sm_event_name(evt));
		ocs_domain_lock(domain);
		if (!ocs_list_empty(&domain->sport_list)) {
			/* if there are sports, transition to wait state and
			 * send shutdown to each sport */
			ocs_sport_t	*sport = NULL;
			ocs_sport_t	*sport_next = NULL;
			ocs_sm_transition(ctx, __ocs_domain_wait_sports_free, NULL);
			ocs_list_foreach_safe(&domain->sport_list, sport, sport_next) {
				ocs_sm_post_event(&sport->sm, OCS_EVT_SHUTDOWN, NULL);
			}
			ocs_domain_unlock(domain);
		} else {
			ocs_domain_unlock(domain);
			/* no sports exist, free domain */
			ocs_sm_transition(ctx, __ocs_domain_wait_shutdown, NULL);
			rc = ocs_hw_domain_free(&ocs->hw, domain);
			if (rc) {
				ocs_log_err(ocs, "ocs_hw_domain_free() failed: %d\n", rc);
				/* TODO: hw/device reset needed */
			}
		}

		break;
	}

	default:
		__ocs_domain_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Wait for the HW domain attach to complete.
 *
 * <h3 class="desc">Description</h3>
 * Waits for the HW domain attach to complete. Forwards attach ok event to the
 * fabric node state machine.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_wait_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_DOMAIN_ATTACH_OK: {
		ocs_node_t *node = NULL;
		ocs_node_t *next_node = NULL;
		ocs_sport_t *sport;
		ocs_sport_t *next_sport;

		/* Mark as attached */
		domain->attached = 1;

		/* Register with SCSI API */
		if (ocs->enable_tgt)
			ocs_scsi_tgt_new_domain(domain);
		if (ocs->enable_ini)
			ocs_scsi_ini_new_domain(domain);

		/* Transition to ready */
		/* sm: / forward event to all sports and nodes */
		ocs_sm_transition(ctx, __ocs_domain_ready, NULL);

		/* We have an FCFI, so we can accept frames */
		domain->req_accept_frames = 1;
		/* Set domain notify pending state to avoid duplicate domain event post */
		domain->domain_notify_pend = 1;

		/* Notify all nodes that the domain attach request has completed
		 * Note: sport will have already received notification of sport attached
		 * as a result of the HW's port attach.
		 */
		ocs_domain_lock(domain);
			ocs_list_foreach_safe(&domain->sport_list, sport, next_sport) {
				ocs_sport_lock(sport);
					ocs_list_foreach_safe(&sport->node_list, node, next_node) {
						ocs_node_post_event(node, OCS_EVT_DOMAIN_ATTACH_OK, NULL);
					}
				ocs_sport_unlock(sport);
			}
		ocs_domain_unlock(domain);
		domain->domain_notify_pend = 0;
		break;
	}

	case OCS_EVT_DOMAIN_ATTACH_FAIL:
		ocs_log_debug(ocs, "%s received while waiting for hw attach to complete\n", ocs_sm_event_name(evt));
		/* TODO: hw/device reset */
		break;

	case OCS_EVT_DOMAIN_FOUND:
		/* Should not happen */
		ocs_assert(evt, NULL);
		break;

	case OCS_EVT_DOMAIN_LOST:
		/* Domain lost while waiting for an attach to complete, go to a state that waits for
		 * the domain attach to complete, then handle domain lost
		 */
		ocs_sm_transition(ctx, __ocs_domain_wait_domain_lost, NULL);
		break;

	case OCS_EVT_DOMAIN_REQ_ATTACH:
		/* In P2P we can get an attach request from the other FLOGI path, so drop this one */
		break;

	default:
		__ocs_domain_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Ready state.
 *
 * <h3 class="desc">Description</h3>
 * This is a domain ready state. It waits for a domain-lost event, and initiates shutdown.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_ready(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_ENTER: {

		/* start any pending vports */
		if (ocs_vport_start(domain)) {
			ocs_log_debug(domain->ocs, "ocs_vport_start() did not start all vports\n");
		}
		break;
	}
	case OCS_EVT_DOMAIN_LOST: {
		int32_t rc;
		ocs_domain_lock(domain);
		if (!ocs_list_empty(&domain->sport_list)) {
			/* if there are sports, transition to wait state and send
			* shutdown to each sport */
			ocs_sport_t	*sport = NULL;
			ocs_sport_t	*sport_next = NULL;
			ocs_sm_transition(ctx, __ocs_domain_wait_sports_free, NULL);
			ocs_list_foreach_safe(&domain->sport_list, sport, sport_next) {
				ocs_sm_post_event(&sport->sm, OCS_EVT_SHUTDOWN, NULL);
			}
			ocs_domain_unlock(domain);
		} else {
			ocs_domain_unlock(domain);
			/* no sports exist, free domain */
			ocs_sm_transition(ctx, __ocs_domain_wait_shutdown, NULL);
			rc = ocs_hw_domain_free(&ocs->hw, domain);
			if (rc) {
				ocs_log_err(ocs, "ocs_hw_domain_free() failed: %d\n", rc);
				/* TODO: hw/device reset needed */
			}
		}
		break;
	}

	case OCS_EVT_DOMAIN_FOUND:
		/* Should not happen */
		ocs_assert(evt, NULL);
		break;

	case OCS_EVT_DOMAIN_REQ_ATTACH: {
		/* can happen during p2p */
		uint32_t fc_id;

		ocs_assert(arg, NULL);
		fc_id = *((uint32_t*)arg);

		/* Assume that the domain is attached */
		ocs_assert(domain->attached, NULL);

		/* Verify that the requested FC_ID is the same as the one we're working with */
		ocs_assert(domain->sport->fc_id == fc_id, NULL);
		break;
	}

	default:
		__ocs_domain_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Wait for nodes to free prior to the domain shutdown.
 *
 * <h3 class="desc">Description</h3>
 * All nodes are freed, and ready for a domain shutdown.
 *
 * @param ctx Remote node sm context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_wait_sports_free(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_ALL_CHILD_NODES_FREE: {
		int32_t rc;

		/* sm: ocs_hw_domain_free */
		ocs_sm_transition(ctx, __ocs_domain_wait_shutdown, NULL);

		/* Request ocs_hw_domain_free and wait for completion */
		rc = ocs_hw_domain_free(&ocs->hw, domain);
		if (rc) {
			ocs_log_err(ocs, "ocs_hw_domain_free() failed: %d\n", rc);
		}
		break;
	}
	default:
		__ocs_domain_common_shutdown(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Complete the domain shutdown.
 *
 * <h3 class="desc">Description</h3>
 * Waits for a HW domain free to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_wait_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_DOMAIN_FREE_OK: {
		if (ocs->enable_ini)
			ocs_scsi_ini_del_domain(domain);
		if (ocs->enable_tgt)
			ocs_scsi_tgt_del_domain(domain);

		/* sm: domain_free */
		if (domain->domain_found_pending) {
			/* save fcf_wwn and drec from this domain, free current domain and allocate
			 * a new one with the same fcf_wwn
			 * TODO: could use a SLI-4 "re-register VPI" operation here
			 */
			uint64_t fcf_wwn = domain->fcf_wwn;
			ocs_domain_record_t drec = domain->pending_drec;

			ocs_log_debug(ocs, "Reallocating domain\n");
			domain->req_domain_free = 1;
			domain = ocs_domain_alloc(ocs, fcf_wwn);

			if (domain == NULL) {
				ocs_log_err(ocs, "ocs_domain_alloc() failed\n");
				/* TODO: hw/device reset needed */
				return NULL;
			}
			/*
			 * got a new domain; at this point, there are at least two domains
			 * once the req_domain_free flag is processed, the associated domain
			 * will be removed.
			 */
			ocs_sm_transition(&domain->drvsm, __ocs_domain_init, NULL);
			ocs_sm_post_event(&domain->drvsm, OCS_EVT_DOMAIN_FOUND, &drec);
		} else {
			domain->req_domain_free = 1;
		}
		break;
	}

	default:
		__ocs_domain_common_shutdown(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup domain_sm
 * @brief Domain state machine: Wait for the domain alloc/attach completion
 * after receiving a domain lost.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when receiving a domain lost while waiting for a domain alloc
 * or a domain attach to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_domain_wait_domain_lost(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_domain_state_decl();

	domain_sm_trace(domain);

	switch(evt) {
	case OCS_EVT_DOMAIN_ALLOC_OK:
	case OCS_EVT_DOMAIN_ATTACH_OK: {
		int32_t rc;
		ocs_domain_lock(domain);
		if (!ocs_list_empty(&domain->sport_list)) {
			/* if there are sports, transition to wait state and send
			* shutdown to each sport */
			ocs_sport_t	*sport = NULL;
			ocs_sport_t	*sport_next = NULL;
			ocs_sm_transition(ctx, __ocs_domain_wait_sports_free, NULL);
			ocs_list_foreach_safe(&domain->sport_list, sport, sport_next) {
				ocs_sm_post_event(&sport->sm, OCS_EVT_SHUTDOWN, NULL);
			}
			ocs_domain_unlock(domain);
		} else {
			ocs_domain_unlock(domain);
			/* no sports exist, free domain */
			ocs_sm_transition(ctx, __ocs_domain_wait_shutdown, NULL);
			rc = ocs_hw_domain_free(&ocs->hw, domain);
			if (rc) {
				ocs_log_err(ocs, "ocs_hw_domain_free() failed: %d\n", rc);
				/* TODO: hw/device reset needed */
			}
		}
		break;
	}
	case OCS_EVT_DOMAIN_ALLOC_FAIL:
	case OCS_EVT_DOMAIN_ATTACH_FAIL:
		ocs_log_err(ocs, "[domain] %-20s: failed\n", ocs_sm_event_name(evt));
		/* TODO: hw/device reset needed */
		break;

	default:
		__ocs_domain_common_shutdown(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}



/**
 * @brief Save the port's service parameters.
 *
 * <h3 class="desc">Description</h3>
 * Service parameters from the fabric FLOGI are saved in the domain's
 * flogi_service_params array.
 *
 * @param domain Pointer to the domain.
 * @param payload Service parameters to save.
 *
 * @return None.
 */

void
ocs_domain_save_sparms(ocs_domain_t *domain, void *payload)
{
	ocs_memcpy(domain->flogi_service_params, payload, sizeof (fc_plogi_payload_t));
}
/**
 * @brief Initiator domain attach. (internal call only)
 *
 * Assumes that the domain SM lock is already locked
 *
 * <h3 class="desc">Description</h3>
 * The HW domain attach function is started.
 *
 * @param domain Pointer to the domain object.
 * @param s_id FC_ID of which to register this domain.
 *
 * @return None.
 */

void
__ocs_domain_attach_internal(ocs_domain_t *domain, uint32_t s_id)
{
	ocs_memcpy(domain->dma.virt, ((uint8_t*)domain->flogi_service_params)+4, sizeof (fc_plogi_payload_t) - 4);
	(void)ocs_sm_post_event(&domain->drvsm, OCS_EVT_DOMAIN_REQ_ATTACH, &s_id);
}

/**
 * @brief Initiator domain attach.
 *
 * <h3 class="desc">Description</h3>
 * The HW domain attach function is started.
 *
 * @param domain Pointer to the domain object.
 * @param s_id FC_ID of which to register this domain.
 *
 * @return None.
 */

void
ocs_domain_attach(ocs_domain_t *domain, uint32_t s_id)
{
	__ocs_domain_attach_internal(domain, s_id);
}

int
ocs_domain_post_event(ocs_domain_t *domain, ocs_sm_event_t event, void *arg)
{
	int rc;
	int accept_frames;
	int req_domain_free;

	rc = ocs_sm_post_event(&domain->drvsm, event, arg);

	req_domain_free = domain->req_domain_free;
	domain->req_domain_free = 0;

	accept_frames = domain->req_accept_frames;
	domain->req_accept_frames = 0;

	if (accept_frames) {
		ocs_domain_accept_frames(domain);
	}

	if (req_domain_free) {
		ocs_domain_free(domain);
	}

	return rc;
}


/**
 * @brief Return the WWN as a uint64_t.
 *
 * <h3 class="desc">Description</h3>
 * Calls the HW property function for the WWNN or WWPN, and returns the value
 * as a uint64_t.
 *
 * @param hw Pointer to the HW object.
 * @param prop HW property.
 *
 * @return Returns uint64_t request value.
 */

uint64_t
ocs_get_wwn(ocs_hw_t *hw, ocs_hw_property_e prop)
{
	uint8_t *p = ocs_hw_get_ptr(hw, prop);
	uint64_t value = 0;

	if (p) {
		uint32_t i;
		for (i = 0; i < sizeof(value); i++) {
			value = (value << 8) | p[i];
		}
	}
	return value;
}

/**
 * @brief Generate a domain ddump.
 *
 * <h3 class="desc">Description</h3>
 * Generates a domain ddump.
 *
 * @param textbuf Pointer to the text buffer.
 * @param domain Pointer to the domain context.
 *
 * @return Returns 0 on success, or a negative value on failure.
 */

int
ocs_ddump_domain(ocs_textbuf_t *textbuf, ocs_domain_t *domain)
{
	ocs_sport_t *sport;
	int retval = 0;

	ocs_ddump_section(textbuf, "domain", domain->instance_index);
	ocs_ddump_value(textbuf, "display_name", "%s", domain->display_name);

	ocs_ddump_value(textbuf, "fcf", "%#x", domain->fcf);
	ocs_ddump_value(textbuf, "fcf_indicator", "%#x", domain->fcf_indicator);
	ocs_ddump_value(textbuf, "vlan_id", "%#x", domain->vlan_id);
	ocs_ddump_value(textbuf, "indicator", "%#x", domain->indicator);
	ocs_ddump_value(textbuf, "attached", "%d", domain->attached);
	ocs_ddump_value(textbuf, "is_loop", "%d", domain->is_loop);
	ocs_ddump_value(textbuf, "is_nlport", "%d", domain->is_nlport);

	ocs_scsi_ini_ddump(textbuf, OCS_SCSI_DDUMP_DOMAIN, domain);
	ocs_scsi_tgt_ddump(textbuf, OCS_SCSI_DDUMP_DOMAIN, domain);

	ocs_display_sparams(NULL, "domain_sparms", 1, textbuf, domain->dma.virt);

	if (ocs_domain_lock_try(domain) != TRUE) {
		/* Didn't get the lock */
		return -1;
	}
		ocs_list_foreach(&domain->sport_list, sport) {
			retval = ocs_ddump_sport(textbuf, sport);
			if (retval != 0) {
				break;
			}
		}

#if defined(ENABLE_FABRIC_EMULATION)
		ocs_ddump_ns(textbuf, domain->ocs_ns);
#endif

	ocs_domain_unlock(domain);

	ocs_ddump_endsection(textbuf, "domain", domain->instance_index);

	return retval;
}


void
ocs_mgmt_domain_list(ocs_textbuf_t *textbuf, void *object)
{
	ocs_sport_t *sport;
	ocs_domain_t *domain = (ocs_domain_t *)object;

	ocs_mgmt_start_section(textbuf, "domain", domain->instance_index);

	/* Add my status values to textbuf */
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "fcf");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "fcf_indicator");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "vlan_id");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "indicator");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "attached");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "is_loop");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "display_name");
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RD, "num_sports");
#if defined(ENABLE_FABRIC_EMULATION)
	ocs_mgmt_emit_property_name(textbuf, MGMT_MODE_RW, "femul_enable");
#endif

	if (ocs_domain_lock_try(domain) == TRUE) {


		/* If we get here, then we are holding the domain lock */
		ocs_list_foreach(&domain->sport_list, sport) {
			if ((sport->mgmt_functions) && (sport->mgmt_functions->get_list_handler)) {
				sport->mgmt_functions->get_list_handler(textbuf, sport);
			}
		}
		ocs_domain_unlock(domain);
	}

	ocs_mgmt_end_section(textbuf, "domain", domain->instance_index);
}

int
ocs_mgmt_domain_get(ocs_textbuf_t *textbuf, char *parent, char *name, void *object)
{
	ocs_sport_t *sport;
	ocs_domain_t *domain = (ocs_domain_t *)object;
	char qualifier[80];
	int retval = -1;

	ocs_mgmt_start_section(textbuf, "domain", domain->instance_index);

	snprintf(qualifier, sizeof(qualifier), "%s/domain[%d]", parent, domain->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {
		char *unqualified_name = name + strlen(qualifier) +1;

		/* See if it's a value I can supply */
		if (ocs_strcmp(unqualified_name, "display_name") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", domain->display_name);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "fcf") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fcf", "%#x", domain->fcf);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "fcf_indicator") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fcf_indicator", "%#x", domain->fcf_indicator);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "vlan_id") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "vlan_id", "%#x", domain->vlan_id);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "indicator") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "%#x", domain->indicator);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "attached") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "attached", domain->attached);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "is_loop") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_loop",  domain->is_loop);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "is_nlport") == 0) {
			ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_nlport",  domain->is_nlport);
			retval = 0;
		} else if (ocs_strcmp(unqualified_name, "display_name") == 0) {
			ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", domain->display_name);
			retval = 0;
#if defined(ENABLE_FABRIC_EMULATION)
		} else if (ocs_strcmp(unqualified_name, "femul_enable") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RW, "femul_enable", "%d", domain->femul_enable);
			retval = 0;
#endif
		} else if (ocs_strcmp(unqualified_name, "num_sports") == 0) {
			ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "num_sports", "%d", domain->sport_instance_count);
			retval = 0;
		} else {
			/* If I didn't know the value of this status pass the request to each of my children */

			ocs_domain_lock(domain);
			ocs_list_foreach(&domain->sport_list, sport) {
				if ((sport->mgmt_functions) && (sport->mgmt_functions->get_handler)) {
					retval = sport->mgmt_functions->get_handler(textbuf, qualifier, name, sport);
				}

				if (retval == 0) {
					break;
				}

			}
			ocs_domain_unlock(domain);
		}
	}

	ocs_mgmt_end_section(textbuf, "domain", domain->instance_index);
	return retval;
}

void
ocs_mgmt_domain_get_all(ocs_textbuf_t *textbuf, void *object)
{
	ocs_sport_t *sport;
	ocs_domain_t *domain = (ocs_domain_t *)object;

	ocs_mgmt_start_section(textbuf, "domain", domain->instance_index);

	ocs_mgmt_emit_string(textbuf, MGMT_MODE_RD, "display_name", domain->display_name);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fcf", "%#x", domain->fcf);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "fcf_indicator", "%#x", domain->fcf_indicator);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "vlan_id", "%#x", domain->vlan_id);
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "indicator", "%#x", domain->indicator);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "attached", domain->attached);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_loop",  domain->is_loop);
	ocs_mgmt_emit_boolean(textbuf, MGMT_MODE_RD, "is_nlport",  domain->is_nlport);
#if defined(ENABLE_FABRIC_EMULATION)
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RW, "femul_enable", "%d", domain->femul_enable);
#endif
	ocs_mgmt_emit_int(textbuf, MGMT_MODE_RD, "num_sports",  "%d", domain->sport_instance_count);

	ocs_domain_lock(domain);
	ocs_list_foreach(&domain->sport_list, sport) {
		if ((sport->mgmt_functions) && (sport->mgmt_functions->get_all_handler)) {
			sport->mgmt_functions->get_all_handler(textbuf, sport);
		}
	}
	ocs_domain_unlock(domain);


	ocs_mgmt_end_unnumbered_section(textbuf, "domain");

}

int
ocs_mgmt_domain_set(char *parent, char *name, char *value, void *object)
{
	ocs_sport_t *sport;
	ocs_domain_t *domain = (ocs_domain_t *)object;
	char qualifier[80];
	int retval = -1;

	snprintf(qualifier, sizeof(qualifier), "%s/domain[%d]", parent, domain->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(name, qualifier, strlen(qualifier)) == 0) {

		/* See if it's a value I can supply */

		/* if (ocs_strcmp(unqualified_name, "display_name") == 0) {

		} else */
		{
			/* If I didn't know the value of this status pass the request to each of my children */
			ocs_domain_lock(domain);
			ocs_list_foreach(&domain->sport_list, sport) {
				if ((sport->mgmt_functions) && (sport->mgmt_functions->set_handler)) {
					retval = sport->mgmt_functions->set_handler(qualifier, name, value, sport);
				}

				if (retval == 0) {
					break;
				}

			}
			ocs_domain_unlock(domain);
		}
	}

	return retval;
}

int
ocs_mgmt_domain_exec(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
			void *arg_out, uint32_t arg_out_length, void *object)
{
	ocs_sport_t *sport;
	ocs_domain_t *domain = (ocs_domain_t *)object;
	char qualifier[80];
	int retval = -1;

	snprintf(qualifier, sizeof(qualifier), "%s.domain%d", parent, domain->instance_index);

	/* If it doesn't start with my qualifier I don't know what to do with it */
	if (ocs_strncmp(action, qualifier, strlen(qualifier)) == 0) {

		{
			/* If I didn't know how to do this action pass the request to each of my children */
			ocs_domain_lock(domain);
			ocs_list_foreach(&domain->sport_list, sport) {
				if ((sport->mgmt_functions) && (sport->mgmt_functions->exec_handler)) {
					retval = sport->mgmt_functions->exec_handler(qualifier, action, arg_in, arg_in_length, arg_out, arg_out_length, sport);
				}

				if (retval == 0) {
					break;
				}

			}
			ocs_domain_unlock(domain);
		}
	}

	return retval;
}
