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

#include "smartpqi_includes.h"

/*
 * Function to submit the request to the adapter.
 */

int pqisrc_submit_cmnd(pqisrc_softstate_t *softs, 
				ib_queue_t *ib_q, void *req)
{
	char *slot = NULL;
	uint32_t offset;
	iu_header_t *hdr = (iu_header_t *)req;
	uint32_t iu_len = hdr->iu_length + 4 ; /* header size */
	int i = 0;
	DBG_FUNC("IN\n");

	PQI_LOCK(&ib_q->lock);
	
	/* Check queue full */
	if ((ib_q->pi_local + 1) % ib_q->num_elem == *(ib_q->ci_virt_addr)) {
		DBG_WARN("OUT Q full\n");
	PQI_UNLOCK(&ib_q->lock);	
		return PQI_STATUS_QFULL;
	}

	/* Get the slot */
	offset = ib_q->pi_local * ib_q->elem_size;
	slot = ib_q->array_virt_addr + offset;

	/* Copy the IU */
	memcpy(slot, req, iu_len);
	DBG_INFO("IU : \n");
	for(i = 0; i< iu_len; i++)
		DBG_INFO(" IU [ %d ] : %x\n", i, *((unsigned char *)(slot + i)));

	/* Update the local PI */
	ib_q->pi_local = (ib_q->pi_local + 1) % ib_q->num_elem;
	DBG_INFO("ib_q->pi_local : %x IU size : %d\n",
			 ib_q->pi_local, hdr->iu_length);
	DBG_INFO("*ib_q->ci_virt_addr: %x\n",
				*(ib_q->ci_virt_addr));

	/* Inform the fw about the new IU */
	PCI_MEM_PUT32(softs, ib_q->pi_register_abs, ib_q->pi_register_offset, ib_q->pi_local);
	PQI_UNLOCK(&ib_q->lock);	
	DBG_FUNC("OUT\n");
	return PQI_STATUS_SUCCESS;
}
