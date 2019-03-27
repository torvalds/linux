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
 * Declare driver's domain handler exported interface
 */

#if !defined(__OCS_DOMAIN_H__)
#define __OCS_DOMAIN_H__
extern int32_t ocs_domain_init(ocs_t *ocs, ocs_domain_t *domain);
extern ocs_domain_t *ocs_domain_find(ocs_t *ocs, uint64_t fcf_wwn);
extern ocs_domain_t *ocs_domain_alloc(ocs_t *ocs, uint64_t fcf_wwn);
extern void ocs_domain_free(ocs_domain_t *domain);
extern void ocs_domain_force_free(ocs_domain_t *domain);
extern void ocs_register_domain_list_empty_cb(ocs_t *ocs, void (*callback)(ocs_t *ocs, void *arg), void *arg);
extern uint64_t ocs_get_wwn(ocs_hw_t *hw, ocs_hw_property_e prop);

static inline void
ocs_domain_lock_init(ocs_domain_t *domain)
{
}

static inline int32_t
ocs_domain_lock_try(ocs_domain_t *domain)
{
	/* Use the device wide lock */
	return ocs_device_lock_try(domain->ocs);
}

static inline void
ocs_domain_lock(ocs_domain_t *domain)
{
	/* Use the device wide lock */
	ocs_device_lock(domain->ocs);
}

static inline void
ocs_domain_unlock(ocs_domain_t *domain)
{
	/* Use the device wide lock */
	ocs_device_unlock(domain->ocs);
}

extern int32_t ocs_domain_cb(void *arg, ocs_hw_domain_event_e event, void *data);
extern void *__ocs_domain_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_wait_alloc(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_allocated(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_wait_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_ready(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_wait_sports_free(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_wait_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_domain_wait_domain_lost(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);

extern void ocs_domain_save_sparms(ocs_domain_t *domain, void *payload);
extern void ocs_domain_attach(ocs_domain_t *domain, uint32_t s_id);
extern int ocs_domain_post_event(ocs_domain_t *domain, ocs_sm_event_t, void *);

extern int ocs_ddump_domain(ocs_textbuf_t *textbuf, ocs_domain_t *domain);
extern void __ocs_domain_attach_internal(ocs_domain_t *domain, uint32_t s_id);;
#endif
