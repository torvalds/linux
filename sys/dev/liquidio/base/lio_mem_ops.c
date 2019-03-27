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

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_mem_ops.h"

#define MEMOPS_IDX   LIO_MAX_BAR1_MAP_INDEX

#if BYTE_ORDER == BIG_ENDIAN
static inline void
lio_toggle_bar1_swapmode(struct octeon_device *oct, uint32_t idx)
{
	uint32_t mask;

	mask = oct->fn_list.bar1_idx_read(oct, idx);
	mask = (mask & 0x2) ? (mask & ~2) : (mask | 2);
	oct->fn_list.bar1_idx_write(oct, idx, mask);
}

#else	/* BYTE_ORDER != BIG_ENDIAN */
#define lio_toggle_bar1_swapmode(oct, idx)
#endif	/* BYTE_ORDER == BIG_ENDIAN */

static inline void
lio_write_bar1_mem8(struct octeon_device *oct, uint32_t reg, uint64_t val)
{

	bus_space_write_1(oct->mem_bus_space[1].tag,
			  oct->mem_bus_space[1].handle, reg, val);
}

#ifdef __i386__
static inline uint32_t
lio_read_bar1_mem32(struct octeon_device *oct, uint32_t reg)
{

	return (bus_space_read_4(oct->mem_bus_space[1].tag,
				 oct->mem_bus_space[1].handle, reg));
}

static inline void
lio_write_bar1_mem32(struct octeon_device *oct, uint32_t reg, uint32_t val)
{

	bus_space_write_4(oct->mem_bus_space[1].tag,
			  oct->mem_bus_space[1].handle, reg, val);
}
#endif

static inline uint64_t
lio_read_bar1_mem64(struct octeon_device *oct, uint32_t reg)
{

#ifdef __i386__
	return (lio_read_bar1_mem32(oct, reg) |
			((uint64_t)lio_read_bar1_mem32(oct, reg + 4) << 32));
#else
	return (bus_space_read_8(oct->mem_bus_space[1].tag,
				 oct->mem_bus_space[1].handle, reg));
#endif
}

static inline void
lio_write_bar1_mem64(struct octeon_device *oct, uint32_t reg, uint64_t val)
{

#ifdef __i386__
	lio_write_bar1_mem32(oct, reg, (uint32_t)val);
	lio_write_bar1_mem32(oct, reg + 4, val >> 32);
#else
	bus_space_write_8(oct->mem_bus_space[1].tag,
			  oct->mem_bus_space[1].handle, reg, val);
#endif
}

static void
lio_pci_fastwrite(struct octeon_device *oct, uint32_t offset,
		  uint8_t *hostbuf, uint32_t len)
{

	while ((len) && ((unsigned long)offset) & 7) {
		lio_write_bar1_mem8(oct, offset++, *(hostbuf++));
		len--;
	}

	lio_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len >= 8) {
		lio_write_bar1_mem64(oct, offset, *((uint64_t *)hostbuf));
		offset += 8;
		hostbuf += 8;
		len -= 8;
	}

	lio_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len--)
		lio_write_bar1_mem8(oct, offset++, *(hostbuf++));
}

static inline uint64_t
lio_read_bar1_mem8(struct octeon_device *oct, uint32_t reg)
{

	return (bus_space_read_1(oct->mem_bus_space[1].tag,
				 oct->mem_bus_space[1].handle, reg));
}

static void
lio_pci_fastread(struct octeon_device *oct, uint32_t offset,
		 uint8_t *hostbuf, uint32_t len)
{

	while ((len) && ((unsigned long)offset) & 7) {
		*(hostbuf++) = lio_read_bar1_mem8(oct, offset++);
		len--;
	}

	lio_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len >= 8) {
		*((uint64_t *)hostbuf) = lio_read_bar1_mem64(oct, offset);
		offset += 8;
		hostbuf += 8;
		len -= 8;
	}

	lio_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len--)
		*(hostbuf++) = lio_read_bar1_mem8(oct, offset++);
}

/* Core mem read/write with temporary bar1 settings. */
/* op = 1 to read, op = 0 to write. */
static void
lio_pci_rw_core_mem(struct octeon_device *oct, uint64_t addr,
		    uint8_t *hostbuf, uint32_t len, uint32_t op)
{
	uint64_t	static_mapping_base;
	uint32_t	copy_len = 0, index_reg_val = 0;
	uint32_t	offset;

	static_mapping_base = oct->console_nb_info.dram_region_base;

	if (static_mapping_base && static_mapping_base ==
	    (addr & 0xFFFFFFFFFFC00000ULL)) {
		int	bar1_index = oct->console_nb_info.bar1_index;

		offset = (bar1_index << 22) + (addr & 0x3fffff);

		if (op)
			lio_pci_fastread(oct, offset, hostbuf, len);
		else
			lio_pci_fastwrite(oct, offset, hostbuf, len);

		return;
	}
	mtx_lock(&oct->mem_access_lock);

	/* Save the original index reg value. */
	index_reg_val = oct->fn_list.bar1_idx_read(oct, MEMOPS_IDX);
	do {
		oct->fn_list.bar1_idx_setup(oct, addr, MEMOPS_IDX, 1);
		offset = (MEMOPS_IDX << 22) + (addr & 0x3fffff);

		/*
		 * If operation crosses a 4MB boundary, split the transfer
		 * at the 4MB boundary.
		 */
		if (((addr + len - 1) & ~(0x3fffff)) != (addr & ~(0x3fffff))) {
			copy_len = (uint32_t)(((addr & ~(0x3fffff)) +
					       (MEMOPS_IDX << 22)) - addr);
		} else {
			copy_len = len;
		}

		if (op) {	/* read from core */
			lio_pci_fastread(oct, offset, hostbuf,
					 copy_len);
		} else {
			lio_pci_fastwrite(oct, offset, hostbuf,
					  copy_len);
		}

		len -= copy_len;
		addr += copy_len;
		hostbuf += copy_len;

	} while (len);

	oct->fn_list.bar1_idx_write(oct, MEMOPS_IDX, index_reg_val);

	mtx_unlock(&oct->mem_access_lock);
}

void
lio_pci_read_core_mem(struct octeon_device *oct, uint64_t coreaddr,
		      uint8_t *buf, uint32_t len)
{

	lio_pci_rw_core_mem(oct, coreaddr, buf, len, 1);
}

void
lio_pci_write_core_mem(struct octeon_device *oct, uint64_t coreaddr,
		       uint8_t *buf, uint32_t len)
{

	lio_pci_rw_core_mem(oct, coreaddr, buf, len, 0);
}

uint64_t
lio_read_device_mem64(struct octeon_device *oct, uint64_t coreaddr)
{
	__be64	ret;

	lio_pci_rw_core_mem(oct, coreaddr, (uint8_t *)&ret, 8, 1);

	return (be64toh(ret));
}

uint32_t
lio_read_device_mem32(struct octeon_device *oct, uint64_t coreaddr)
{
	__be32	ret;

	lio_pci_rw_core_mem(oct, coreaddr, (uint8_t *)&ret, 4, 1);

	return (be32toh(ret));
}

void
lio_write_device_mem32(struct octeon_device *oct, uint64_t coreaddr,
		       uint32_t val)
{
	__be32	t = htobe32(val);

	lio_pci_rw_core_mem(oct, coreaddr, (uint8_t *)&t, 4, 0);
}
