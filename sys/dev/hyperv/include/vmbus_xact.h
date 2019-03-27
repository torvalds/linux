/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMBUS_XACT_H_
#define _VMBUS_XACT_H_

#include <sys/param.h>
#include <sys/bus.h>

struct vmbus_xact;
struct vmbus_xact_ctx;

struct vmbus_xact_ctx	*vmbus_xact_ctx_create(bus_dma_tag_t dtag,
			    size_t req_size, size_t resp_size,
			    size_t priv_size);
void			vmbus_xact_ctx_destroy(struct vmbus_xact_ctx *ctx);
bool			vmbus_xact_ctx_orphan(struct vmbus_xact_ctx *ctx);

struct vmbus_xact	*vmbus_xact_get(struct vmbus_xact_ctx *ctx,
			    size_t req_len);
void			vmbus_xact_put(struct vmbus_xact *xact);

void			*vmbus_xact_req_data(const struct vmbus_xact *xact);
bus_addr_t		vmbus_xact_req_paddr(const struct vmbus_xact *xact);
void			*vmbus_xact_priv(const struct vmbus_xact *xact,
			    size_t priv_len);
void			vmbus_xact_activate(struct vmbus_xact *xact);
void			vmbus_xact_deactivate(struct vmbus_xact *xact);
const void		*vmbus_xact_wait(struct vmbus_xact *xact,
			    size_t *resp_len);
const void		*vmbus_xact_busywait(struct vmbus_xact *xact,
			    size_t *resp_len);
const void		*vmbus_xact_poll(struct vmbus_xact *xact,
			    size_t *resp_len);
void			vmbus_xact_wakeup(struct vmbus_xact *xact,
			    const void *data, size_t dlen);
void			vmbus_xact_ctx_wakeup(struct vmbus_xact_ctx *ctx,
			    const void *data, size_t dlen);

#endif	/* !_VMBUS_XACT_H_ */
