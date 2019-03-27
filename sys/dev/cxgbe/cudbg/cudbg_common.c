/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>

#include "common/common.h"
#include "cudbg.h"
#include "cudbg_lib_common.h"

int get_scratch_buff(struct cudbg_buffer *pdbg_buff, u32 size,
		     struct cudbg_buffer *pscratch_buff)
{
	u32 scratch_offset;
	int rc = 0;

	scratch_offset = pdbg_buff->size - size;

	if (pdbg_buff->offset > (int)scratch_offset || pdbg_buff->size < size) {
		rc = CUDBG_STATUS_NO_SCRATCH_MEM;
		goto err;
	} else {
		pscratch_buff->data = (char *)pdbg_buff->data + scratch_offset;
		pscratch_buff->offset = 0;
		pscratch_buff->size = size;
		pdbg_buff->size -= size;
	}

err:
	return rc;
}

void release_scratch_buff(struct cudbg_buffer *pscratch_buff,
			  struct cudbg_buffer *pdbg_buff)
{
	pdbg_buff->size += pscratch_buff->size;
	/* Reset the used buffer to zero.
 	 * If we dont do this, then it will effect the ext entity logic.
 	 */
	memset(pscratch_buff->data, 0, pscratch_buff->size);
	pscratch_buff->data = NULL;
	pscratch_buff->offset = 0;
	pscratch_buff->size = 0;
}

static inline void init_cudbg_hdr(struct cudbg_init_hdr *hdr)
{
	hdr->major_ver = CUDBG_MAJOR_VERSION;
	hdr->minor_ver = CUDBG_MINOR_VERSION;
	hdr->build_ver = CUDBG_BUILD_VERSION;
	hdr->init_struct_size = sizeof(struct cudbg_init);
}

void *
cudbg_alloc_handle(void)
{
	struct cudbg_private *handle;

	handle = malloc(sizeof(*handle), M_CXGBE, M_ZERO | M_WAITOK);
	init_cudbg_hdr(&handle->dbg_init.header);

	return (handle);
}

void
cudbg_free_handle(void *handle)
{

	free(handle, M_CXGBE);
}
