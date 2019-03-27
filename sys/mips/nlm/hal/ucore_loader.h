/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __NLM_UCORE_LOADER_H__
#define	__NLM_UCORE_LOADER_H__

/**
* @file_name ucore_loader.h
* @author Netlogic Microsystems
* @brief Ucore loader API header
*/

#define	CODE_SIZE_PER_UCORE	(4 << 10)

static __inline__ void
nlm_ucore_load_image(uint64_t nae_base, int ucore)
{
	uint64_t addr = nae_base + NAE_UCORE_SHARED_RAM_OFFSET +
	    (ucore * CODE_SIZE_PER_UCORE);
	uint32_t *p = (uint32_t *)ucore_app_bin;
	int i, size;

	size = sizeof(ucore_app_bin)/sizeof(uint32_t);
	for (i = 0; i < size; i++, addr += 4)
		nlm_store_word_daddr(addr, htobe32(p[i]));

	/* add a 'nop' if number of instructions are odd */
	if (size & 0x1)
		nlm_store_word_daddr(addr, 0x0);
}

static __inline int
nlm_ucore_write_sharedmem(uint64_t nae_base, int index, uint32_t data)
{
	uint32_t ucore_cfg;
	uint64_t addr = nae_base + NAE_UCORE_SHARED_RAM_OFFSET;

	if (index > 128)
		return (-1);

	ucore_cfg = nlm_read_nae_reg(nae_base, NAE_RX_UCORE_CFG);
	/* set iram to zero */
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG,
	    (ucore_cfg & ~(0x1 << 7)));

	nlm_store_word_daddr(addr + (index * 4), data);

	/* restore ucore config */
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG, ucore_cfg);
	return (0);
}

static __inline uint32_t
nlm_ucore_read_sharedmem(uint64_t nae_base, int index)
{
	uint64_t addr = nae_base + NAE_UCORE_SHARED_RAM_OFFSET;
	uint32_t ucore_cfg, val;

	ucore_cfg = nlm_read_nae_reg(nae_base, NAE_RX_UCORE_CFG);
	/* set iram to zero */
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG,
	    (ucore_cfg & ~(0x1 << 7)));

	val = nlm_load_word_daddr(addr + (index * 4));

	/* restore ucore config */
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG, ucore_cfg);

	return val;
}

static __inline__ int
nlm_ucore_load_all(uint64_t nae_base, uint32_t ucore_mask, int nae_reset_done)
{
	int i, count = 0;
	uint32_t mask;
	uint32_t ucore_cfg = 0;

	mask = ucore_mask & 0xffff;

	/* Stop all ucores */
	if (nae_reset_done == 0) { /* Skip the Ucore reset if NAE reset is done */
		ucore_cfg = nlm_read_nae_reg(nae_base, NAE_RX_UCORE_CFG);
		nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG,
		    ucore_cfg | (1 << 24));

		/* poll for ucore to get in to a wait state */
		do {
			ucore_cfg = nlm_read_nae_reg(nae_base,
			    NAE_RX_UCORE_CFG);
		} while ((ucore_cfg & (1 << 25)) == 0);
	}

	for (i = 0; i < sizeof(ucore_mask) * NBBY; i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		nlm_ucore_load_image(nae_base, i);
		count++;
	}

	/* Enable per-domain ucores */
	ucore_cfg = nlm_read_nae_reg(nae_base, NAE_RX_UCORE_CFG);

	/* write one to reset bits to put the ucores in reset */
	ucore_cfg = ucore_cfg | (((mask) & 0xffff) << 8);
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG, ucore_cfg);

	/* write zero to reset bits to pull them out of reset */
	ucore_cfg = ucore_cfg & (~(((mask) & 0xffff) << 8)) & ~(1 << 24);
	nlm_write_nae_reg(nae_base, NAE_RX_UCORE_CFG, ucore_cfg);

	return (count);
}
#endif
