/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*
 *  \file lio_main.h
 *  \brief Host Driver: This file is included by all host driver source files
 *  to include common definitions.
 */

#ifndef _LIO_MAIN_H_
#define  _LIO_MAIN_H_

extern unsigned int    lio_hwlro;

#define LIO_CAST64(v)	((long long)(long)(v))

#define LIO_DRV_NAME	"lio"

/** Swap 8B blocks */
static inline void
lio_swap_8B_data(uint64_t *data, uint32_t blocks)
{

	while (blocks) {
		*data = htobe64(*data);
		blocks--;
		data++;
	}
}

/*
 * \brief unmaps a PCI BAR
 * @param oct Pointer to Octeon device
 * @param baridx bar index
 */
static inline void
lio_unmap_pci_barx(struct octeon_device *oct, int baridx)
{

	lio_dev_dbg(oct, "Freeing PCI mapped regions for Bar%d\n", baridx);

	if (oct->mem_bus_space[baridx].pci_mem != NULL) {
		bus_release_resource(oct->device, SYS_RES_MEMORY,
				     PCIR_BAR(baridx * 2),
				     oct->mem_bus_space[baridx].pci_mem);
		oct->mem_bus_space[baridx].pci_mem = NULL;
	}
}

/*
 * \brief maps a PCI BAR
 * @param oct Pointer to Octeon device
 * @param baridx bar index
 */
static inline int
lio_map_pci_barx(struct octeon_device *oct, int baridx)
{
	int	rid = PCIR_BAR(baridx * 2);

	oct->mem_bus_space[baridx].pci_mem =
		bus_alloc_resource_any(oct->device, SYS_RES_MEMORY, &rid,
				       RF_ACTIVE);

	if (oct->mem_bus_space[baridx].pci_mem == NULL) {
		lio_dev_err(oct, "Unable to allocate bus resource: memory\n");
		return (ENXIO);
	}

	/* Save bus_space values for READ/WRITE_REG macros */
	oct->mem_bus_space[baridx].tag =
		rman_get_bustag(oct->mem_bus_space[baridx].pci_mem);
	oct->mem_bus_space[baridx].handle =
		rman_get_bushandle(oct->mem_bus_space[baridx].pci_mem);

	lio_dev_dbg(oct, "BAR%d Tag 0x%llx Handle 0x%llx\n",
		    baridx, LIO_CAST64(oct->mem_bus_space[baridx].tag),
		    LIO_CAST64(oct->mem_bus_space[baridx].handle));

	return (0);
}

static inline void
lio_sleep_cond(struct octeon_device *oct, volatile int *condition)
{

	while (!(*condition)) {
		lio_mdelay(1);
		lio_flush_iq(oct, oct->instr_queue[0], 0);
		lio_process_ordered_list(oct, 0);
	}
}

int	lio_console_debug_enabled(uint32_t console);

#ifndef ROUNDUP4
#define ROUNDUP4(val)	(((val) + 3) & 0xfffffffc)
#endif

#ifndef ROUNDUP8
#define ROUNDUP8(val)	(((val) + 7) & 0xfffffff8)
#endif

#define BIT_ULL(nr)	(1ULL << (nr))

void	lio_free_mbuf(struct lio_instr_queue *iq,
		      struct lio_mbuf_free_info *finfo);
void	lio_free_sgmbuf(struct lio_instr_queue *iq,
			struct lio_mbuf_free_info *finfo);

#endif	/* _LIO_MAIN_H_ */
