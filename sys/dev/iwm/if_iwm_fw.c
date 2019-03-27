/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 * Driver version we are currently based off of is
 * Linux 4.7.3 (tag id d7f6728f57e3ecbb7ef34eb7d9f564d514775d75)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/linker.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net/bpf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/iwm/if_iwmreg.h>
#include <dev/iwm/if_iwmvar.h>
#include <dev/iwm/if_iwm_debug.h>
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_fw.h>

void
iwm_free_fw_paging(struct iwm_softc *sc)
{
	int i;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr == NULL)
		return;

	for (i = 0; i < IWM_NUM_OF_FW_PAGING_BLOCKS; i++) {
		iwm_dma_contig_free(&sc->fw_paging_db[i].fw_paging_block);
	}

	memset(sc->fw_paging_db, 0, sizeof(sc->fw_paging_db));
}

static int
iwm_fill_paging_mem(struct iwm_softc *sc, const struct iwm_fw_img *image)
{
	int sec_idx, idx;
	uint32_t offset = 0;

	/*
	 * find where is the paging image start point:
	 * if CPU2 exist and it's in paging format, then the image looks like:
	 * CPU1 sections (2 or more)
	 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between CPU1 to CPU2
	 * CPU2 sections (not paged)
	 * PAGING_SEPARATOR_SECTION delimiter - separate between CPU2
	 * non paged to CPU2 paging sec
	 * CPU2 paging CSS
	 * CPU2 paging image (including instruction and data)
	 */
	for (sec_idx = 0; sec_idx < IWM_UCODE_SECTION_MAX; sec_idx++) {
		if (image->sec[sec_idx].offset == IWM_PAGING_SEPARATOR_SECTION) {
			sec_idx++;
			break;
		}
	}

	/*
	 * If paging is enabled there should be at least 2 more sections left
	 * (one for CSS and one for Paging data)
	 */
	if (sec_idx >= nitems(image->sec) - 1) {
		device_printf(sc->sc_dev,
		    "Paging: Missing CSS and/or paging sections\n");
		iwm_free_fw_paging(sc);
		return EINVAL;
	}

	/* copy the CSS block to the dram */
	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "Paging: load paging CSS to FW, sec = %d\n",
		    sec_idx);

	memcpy(sc->fw_paging_db[0].fw_paging_block.vaddr,
	       image->sec[sec_idx].data,
	       sc->fw_paging_db[0].fw_paging_size);

	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "Paging: copied %d CSS bytes to first block\n",
		    sc->fw_paging_db[0].fw_paging_size);

	sec_idx++;

	/*
	 * copy the paging blocks to the dram
	 * loop index start from 1 since that CSS block already copied to dram
	 * and CSS index is 0.
	 * loop stop at num_of_paging_blk since that last block is not full.
	 */
	for (idx = 1; idx < sc->num_of_paging_blk; idx++) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		       (const char *)image->sec[sec_idx].data + offset,
		       sc->fw_paging_db[idx].fw_paging_size);

		IWM_DPRINTF(sc, IWM_DEBUG_FW,
			    "Paging: copied %d paging bytes to block %d\n",
			    sc->fw_paging_db[idx].fw_paging_size,
			    idx);

		offset += sc->fw_paging_db[idx].fw_paging_size;
	}

	/* copy the last paging block */
	if (sc->num_of_pages_in_last_blk > 0) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		       (const char *)image->sec[sec_idx].data + offset,
		       IWM_FW_PAGING_SIZE * sc->num_of_pages_in_last_blk);

		IWM_DPRINTF(sc, IWM_DEBUG_FW,
			    "Paging: copied %d pages in the last block %d\n",
			    sc->num_of_pages_in_last_blk, idx);
	}

	return 0;
}

static int
iwm_alloc_fw_paging_mem(struct iwm_softc *sc, const struct iwm_fw_img *image)
{
	int blk_idx = 0;
	int error, num_of_pages;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr != NULL) {
		int i;
		/* Device got reset, and we setup firmware paging again */
		for (i = 0; i < sc->num_of_paging_blk + 1; i++) {
			bus_dmamap_sync(sc->sc_dmat,
			    sc->fw_paging_db[i].fw_paging_block.map,
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		}
		return 0;
	}

	/* ensure IWM_BLOCK_2_EXP_SIZE is power of 2 of IWM_PAGING_BLOCK_SIZE */
        _Static_assert((1 << IWM_BLOCK_2_EXP_SIZE) == IWM_PAGING_BLOCK_SIZE,
	    "IWM_BLOCK_2_EXP_SIZE must be power of 2 of IWM_PAGING_BLOCK_SIZE");

	num_of_pages = image->paging_mem_size / IWM_FW_PAGING_SIZE;
	sc->num_of_paging_blk = ((num_of_pages - 1) /
				    IWM_NUM_OF_PAGE_PER_GROUP) + 1;

	sc->num_of_pages_in_last_blk =
		num_of_pages -
		IWM_NUM_OF_PAGE_PER_GROUP * (sc->num_of_paging_blk - 1);

	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "Paging: allocating mem for %d paging blocks, each block holds 8 pages, last block holds %d pages\n",
		    sc->num_of_paging_blk,
		    sc->num_of_pages_in_last_blk);

	/* allocate block of 4Kbytes for paging CSS */
	error = iwm_dma_contig_alloc(sc->sc_dmat,
	    &sc->fw_paging_db[blk_idx].fw_paging_block, IWM_FW_PAGING_SIZE,
	    4096);
	if (error) {
		/* free all the previous pages since we failed */
		iwm_free_fw_paging(sc);
		return ENOMEM;
	}

	sc->fw_paging_db[blk_idx].fw_paging_size = IWM_FW_PAGING_SIZE;

	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "Paging: allocated 4K(CSS) bytes for firmware paging.\n");

	/*
	 * allocate blocks in dram.
	 * since that CSS allocated in fw_paging_db[0] loop start from index 1
	 */
	for (blk_idx = 1; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		/* allocate block of IWM_PAGING_BLOCK_SIZE (32K) */
		/* XXX Use iwm_dma_contig_alloc for allocating */
		error = iwm_dma_contig_alloc(sc->sc_dmat,
		     &sc->fw_paging_db[blk_idx].fw_paging_block,
		    IWM_PAGING_BLOCK_SIZE, 4096);
		if (error) {
			/* free all the previous pages since we failed */
			iwm_free_fw_paging(sc);
			return ENOMEM;
		}

		sc->fw_paging_db[blk_idx].fw_paging_size = IWM_PAGING_BLOCK_SIZE;

		IWM_DPRINTF(sc, IWM_DEBUG_FW,
			    "Paging: allocated 32K bytes for firmware paging.\n");
	}

	return 0;
}

int
iwm_save_fw_paging(struct iwm_softc *sc, const struct iwm_fw_img *fw)
{
	int ret;

	ret = iwm_alloc_fw_paging_mem(sc, fw);
	if (ret)
		return ret;

	return iwm_fill_paging_mem(sc, fw);
}

/* send paging cmd to FW in case CPU2 has paging image */
int
iwm_send_paging_cmd(struct iwm_softc *sc, const struct iwm_fw_img *fw)
{
	int blk_idx;
	uint32_t dev_phy_addr;
	struct iwm_fw_paging_cmd fw_paging_cmd = {
		.flags =
			htole32(IWM_PAGING_CMD_IS_SECURED |
				IWM_PAGING_CMD_IS_ENABLED |
				(sc->num_of_pages_in_last_blk <<
				IWM_PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = htole32(IWM_BLOCK_2_EXP_SIZE),
		.block_num = htole32(sc->num_of_paging_blk),
	};

	/* loop for for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		dev_phy_addr = htole32(
		    sc->fw_paging_db[blk_idx].fw_paging_block.paddr >>
		    IWM_PAGE_2_EXP_SIZE);
		fw_paging_cmd.device_phy_addr[blk_idx] = dev_phy_addr;
		bus_dmamap_sync(sc->sc_dmat,
		    sc->fw_paging_db[blk_idx].fw_paging_block.map,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	}

	return iwm_mvm_send_cmd_pdu(sc, iwm_cmd_id(IWM_FW_PAGING_BLOCK_CMD,
						   IWM_ALWAYS_LONG_GROUP, 0),
				    0, sizeof(fw_paging_cmd), &fw_paging_cmd);
}
