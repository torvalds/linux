/*-
 * Copyright (C) 2012 Intel Corporation
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

__FBSDID("$FreeBSD$");

#ifndef __IOAT_INTERNAL_H__
#define __IOAT_INTERNAL_H__

#include <sys/_task.h>

#define	DEVICE2SOFTC(dev)	((struct ioat_softc *) device_get_softc(dev))
#define	KTR_IOAT		KTR_SPARE3

#define	ioat_read_chancnt(ioat) \
	ioat_read_1((ioat), IOAT_CHANCNT_OFFSET)

#define	ioat_read_xfercap(ioat) \
	(ioat_read_1((ioat), IOAT_XFERCAP_OFFSET) & IOAT_XFERCAP_VALID_MASK)

#define	ioat_write_intrctrl(ioat, value) \
	ioat_write_1((ioat), IOAT_INTRCTRL_OFFSET, (value))

#define	ioat_read_cbver(ioat) \
	(ioat_read_1((ioat), IOAT_CBVER_OFFSET) & 0xFF)

#define	ioat_read_dmacapability(ioat) \
	ioat_read_4((ioat), IOAT_DMACAPABILITY_OFFSET)

#define	ioat_write_chanctrl(ioat, value) \
	ioat_write_2((ioat), IOAT_CHANCTRL_OFFSET, (value))

static __inline uint64_t
ioat_bus_space_read_8_lower_first(bus_space_tag_t tag,
    bus_space_handle_t handle, bus_size_t offset)
{
	return (bus_space_read_4(tag, handle, offset) |
	    ((uint64_t)bus_space_read_4(tag, handle, offset + 4)) << 32);
}

static __inline void
ioat_bus_space_write_8_lower_first(bus_space_tag_t tag,
    bus_space_handle_t handle, bus_size_t offset, uint64_t val)
{
	bus_space_write_4(tag, handle, offset, val);
	bus_space_write_4(tag, handle, offset + 4, val >> 32);
}

#ifdef __i386__
#define ioat_bus_space_read_8 ioat_bus_space_read_8_lower_first
#define ioat_bus_space_write_8 ioat_bus_space_write_8_lower_first
#else
#define ioat_bus_space_read_8(tag, handle, offset) \
	bus_space_read_8((tag), (handle), (offset))
#define ioat_bus_space_write_8(tag, handle, offset, val) \
	bus_space_write_8((tag), (handle), (offset), (val))
#endif

#define ioat_read_1(ioat, offset) \
	bus_space_read_1((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset))

#define ioat_read_2(ioat, offset) \
	bus_space_read_2((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset))

#define ioat_read_4(ioat, offset) \
	bus_space_read_4((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset))

#define ioat_read_8(ioat, offset) \
	ioat_bus_space_read_8((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset))

#define ioat_read_double_4(ioat, offset) \
	ioat_bus_space_read_8_lower_first((ioat)->pci_bus_tag, \
	    (ioat)->pci_bus_handle, (offset))

#define ioat_write_1(ioat, offset, value) \
	bus_space_write_1((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset), (value))

#define ioat_write_2(ioat, offset, value) \
	bus_space_write_2((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset), (value))

#define ioat_write_4(ioat, offset, value) \
	bus_space_write_4((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset), (value))

#define ioat_write_8(ioat, offset, value) \
	ioat_bus_space_write_8((ioat)->pci_bus_tag, (ioat)->pci_bus_handle, \
	    (offset), (value))

#define ioat_write_double_4(ioat, offset, value) \
	ioat_bus_space_write_8_lower_first((ioat)->pci_bus_tag, \
	    (ioat)->pci_bus_handle, (offset), (value))

MALLOC_DECLARE(M_IOAT);

SYSCTL_DECL(_hw_ioat);

extern int g_ioat_debug_level;

struct generic_dma_control {
	uint32_t int_enable:1;
	uint32_t src_snoop_disable:1;
	uint32_t dest_snoop_disable:1;
	uint32_t completion_update:1;
	uint32_t fence:1;
	uint32_t reserved1:1;
	uint32_t src_page_break:1;
	uint32_t dest_page_break:1;
	uint32_t bundle:1;
	uint32_t dest_dca:1;
	uint32_t hint:1;
	uint32_t reserved2:13;
	uint32_t op:8;
};

struct ioat_generic_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
	} u;
	uint64_t src_addr;
	uint64_t dest_addr;
	uint64_t next;
	uint64_t reserved[4];
};

struct ioat_dma_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t src_snoop_disable:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t null:1;
			uint32_t src_page_break:1;
			uint32_t dest_page_break:1;
			uint32_t bundle:1;
			uint32_t dest_dca:1;
			uint32_t hint:1;
			uint32_t reserved:13;
			#define IOAT_OP_COPY 0x00
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_addr;
	uint64_t dest_addr;
	uint64_t next;
	uint64_t next_src_addr;
	uint64_t next_dest_addr;
	uint64_t user1;
	uint64_t user2;
};

struct ioat_fill_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t reserved:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t reserved2:2;
			uint32_t dest_page_break:1;
			uint32_t bundle:1;
			uint32_t reserved3:15;
			#define IOAT_OP_FILL 0x01
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_data;
	uint64_t dest_addr;
	uint64_t next;
	uint64_t reserved;
	uint64_t next_dest_addr;
	uint64_t user1;
	uint64_t user2;
};

struct ioat_crc32_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t src_snoop_disable:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t reserved1:3;
			uint32_t bundle:1;
			uint32_t dest_dca:1;
			uint32_t hint:1;
			uint32_t use_seed:1;
			/*
			 * crc_location:
			 * For IOAT_OP_MOVECRC_TEST and IOAT_OP_CRC_TEST:
			 * 0: comparison value is pointed to by CRC Address
			 *    field.
			 * 1: comparison value follows data in wire format
			 *    ("inverted reflected bit order") in the 4 bytes
			 *    following the source data.
			 *
			 * For IOAT_OP_CRC_STORE:
			 * 0: Result will be stored at location pointed to by
			 *    CRC Address field (in wire format).
			 * 1: Result will be stored directly following the
			 *    source data.
			 *
			 * For IOAT_OP_MOVECRC_STORE:
			 * 0: Result will be stored at location pointed to by
			 *    CRC Address field (in wire format).
			 * 1: Result will be stored directly following the
			 *    *destination* data.
			 */
			uint32_t crc_location:1;
			uint32_t reserved2:11;
			/*
			 * MOVECRC - Move data in the same way as standard copy
			 * operation, but also compute CRC32.
			 *
			 * CRC - Only compute CRC on source data.
			 *
			 * There is a CRC accumulator register in the hardware.
			 * If 'initial' is set, it is initialized to the value
			 * in 'seed.'
			 *
			 * In all modes, these operators accumulate size bytes
			 * at src_addr into the running CRC32C.
			 *
			 * Store mode emits the accumulated CRC, in wire
			 * format, as specified by the crc_location bit above.
			 *
			 * Test mode compares the accumulated CRC against the
			 * reference CRC, as described in crc_location above.
			 * On failure, halts the DMA engine with a CRC error
			 * status.
			 */
			#define	IOAT_OP_MOVECRC		0x41
			#define	IOAT_OP_MOVECRC_TEST	0x42
			#define	IOAT_OP_MOVECRC_STORE	0x43
			#define	IOAT_OP_CRC		0x81
			#define	IOAT_OP_CRC_TEST	0x82
			#define	IOAT_OP_CRC_STORE	0x83
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_addr;
	uint64_t dest_addr;
	uint64_t next;
	uint64_t next_src_addr;
	uint64_t next_dest_addr;
	uint32_t seed;
	uint32_t reserved;
	uint64_t crc_address;
};

struct ioat_xor_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t src_snoop_disable:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t src_count:3;
			uint32_t bundle:1;
			uint32_t dest_dca:1;
			uint32_t hint:1;
			uint32_t reserved:13;
			#define IOAT_OP_XOR 0x87
			#define IOAT_OP_XOR_VAL 0x88
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_addr;
	uint64_t dest_addr;
	uint64_t next;
	uint64_t src_addr2;
	uint64_t src_addr3;
	uint64_t src_addr4;
	uint64_t src_addr5;
};

struct ioat_xor_ext_hw_descriptor {
	uint64_t src_addr6;
	uint64_t src_addr7;
	uint64_t src_addr8;
	uint64_t next;
	uint64_t reserved[4];
};

struct ioat_pq_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t src_snoop_disable:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t src_count:3;
			uint32_t bundle:1;
			uint32_t dest_dca:1;
			uint32_t hint:1;
			uint32_t p_disable:1;
			uint32_t q_disable:1;
			uint32_t reserved:11;
			#define IOAT_OP_PQ 0x89
			#define IOAT_OP_PQ_VAL 0x8a
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_addr;
	uint64_t p_addr;
	uint64_t next;
	uint64_t src_addr2;
	uint64_t src_addr3;
	uint8_t  coef[8];
	uint64_t q_addr;
};

struct ioat_pq_ext_hw_descriptor {
	uint64_t src_addr4;
	uint64_t src_addr5;
	uint64_t src_addr6;
	uint64_t next;
	uint64_t src_addr7;
	uint64_t src_addr8;
	uint64_t reserved[2];
};

struct ioat_pq_update_hw_descriptor {
	uint32_t size;
	union {
		uint32_t control_raw;
		struct generic_dma_control control_generic;
		struct {
			uint32_t int_enable:1;
			uint32_t src_snoop_disable:1;
			uint32_t dest_snoop_disable:1;
			uint32_t completion_update:1;
			uint32_t fence:1;
			uint32_t src_cnt:3;
			uint32_t bundle:1;
			uint32_t dest_dca:1;
			uint32_t hint:1;
			uint32_t p_disable:1;
			uint32_t q_disable:1;
			uint32_t reserved:3;
			uint32_t coef:8;
			#define IOAT_OP_PQ_UP 0x8b
			uint32_t op:8;
		} control;
	} u;
	uint64_t src_addr;
	uint64_t p_addr;
	uint64_t next;
	uint64_t src_addr2;
	uint64_t p_src;
	uint64_t q_src;
	uint64_t q_addr;
};

struct ioat_raw_hw_descriptor {
	uint64_t field[8];
};

struct bus_dmadesc {
	bus_dmaengine_callback_t callback_fn;
	void			 *callback_arg;
};

struct ioat_descriptor {
	struct bus_dmadesc	bus_dmadesc;
	uint32_t		id;
};

/* Unused by this driver at this time. */
#define	IOAT_OP_MARKER		0x84

/*
 * Deprecated OPs -- v3 DMA generates an abort if given these.  And this driver
 * doesn't support anything older than v3.
 */
#define	IOAT_OP_OLD_XOR		0x85
#define	IOAT_OP_OLD_XOR_VAL	0x86

/* One of these per allocated PCI device. */
struct ioat_softc {
	bus_dmaengine_t		dmaengine;
#define	to_ioat_softc(_dmaeng)						\
({									\
	bus_dmaengine_t *_p = (_dmaeng);				\
	(struct ioat_softc *)((char *)_p -				\
	    offsetof(struct ioat_softc, dmaengine));			\
})

	device_t		device;
	int			version;
	unsigned		chan_idx;

	bus_space_tag_t		pci_bus_tag;
	bus_space_handle_t	pci_bus_handle;
	struct resource		*pci_resource;
	int			pci_resource_id;
	uint32_t		max_xfer_size;
	uint32_t		capabilities;
	uint32_t		ring_size_order;
	uint16_t		intrdelay_max;
	uint16_t		cached_intrdelay;

	int			rid;
	struct resource		*res;
	void			*tag;

	bus_dma_tag_t		hw_desc_tag;
	bus_dmamap_t		hw_desc_map;

	bus_dma_tag_t		comp_update_tag;
	bus_dmamap_t		comp_update_map;
	uint64_t		*comp_update;
	bus_addr_t		comp_update_bus_addr;

	boolean_t		quiescing;
	boolean_t		destroying;
	boolean_t		is_submitter_processing;
	boolean_t		intrdelay_supported;
	boolean_t		resetting;		/* submit_lock */
	boolean_t		resetting_cleanup;	/* cleanup_lock */

	struct ioat_descriptor	*ring;

	union ioat_hw_descriptor {
		struct ioat_generic_hw_descriptor	generic;
		struct ioat_dma_hw_descriptor		dma;
		struct ioat_fill_hw_descriptor		fill;
		struct ioat_crc32_hw_descriptor		crc32;
		struct ioat_xor_hw_descriptor		xor;
		struct ioat_xor_ext_hw_descriptor	xor_ext;
		struct ioat_pq_hw_descriptor		pq;
		struct ioat_pq_ext_hw_descriptor	pq_ext;
		struct ioat_raw_hw_descriptor		raw;
	} *hw_desc_ring;
	bus_addr_t		hw_desc_bus_addr;
#define	RING_PHYS_ADDR(sc, i)	(sc)->hw_desc_bus_addr + \
    (((i) % (1 << (sc)->ring_size_order)) * sizeof(struct ioat_dma_hw_descriptor))

	struct mtx_padalign	submit_lock;
	struct callout		poll_timer;
	struct task		reset_task;
	struct mtx_padalign	cleanup_lock;

	uint32_t		refcnt;
	uint32_t		head;
	uint32_t		acq_head;
	uint32_t		tail;
	bus_addr_t		last_seen;

	struct {
		uint64_t	interrupts;
		uint64_t	descriptors_processed;
		uint64_t	descriptors_error;
		uint64_t	descriptors_submitted;

		uint32_t	channel_halts;
		uint32_t	last_halt_chanerr;
	} stats;
};

void ioat_test_attach(void);
void ioat_test_detach(void);

/*
 * XXX DO NOT USE this routine for obtaining the current completed descriptor.
 *
 * The double_4 read on ioat<3.3 appears to result in torn reads.  And v3.2
 * hardware is still commonplace (Broadwell Xeon has it).  Instead, use the
 * device-pushed *comp_update.
 *
 * It is safe to use ioat_get_chansts() for the low status bits.
 */
static inline uint64_t
ioat_get_chansts(struct ioat_softc *ioat)
{
	uint64_t status;

	if (ioat->version >= IOAT_VER_3_3)
		status = ioat_read_8(ioat, IOAT_CHANSTS_OFFSET);
	else
		/* Must read lower 4 bytes before upper 4 bytes. */
		status = ioat_read_double_4(ioat, IOAT_CHANSTS_OFFSET);
	return (status);
}

static inline void
ioat_write_chancmp(struct ioat_softc *ioat, uint64_t addr)
{

	if (ioat->version >= IOAT_VER_3_3)
		ioat_write_8(ioat, IOAT_CHANCMP_OFFSET_LOW, addr);
	else
		ioat_write_double_4(ioat, IOAT_CHANCMP_OFFSET_LOW, addr);
}

static inline void
ioat_write_chainaddr(struct ioat_softc *ioat, uint64_t addr)
{

	if (ioat->version >= IOAT_VER_3_3)
		ioat_write_8(ioat, IOAT_CHAINADDR_OFFSET_LOW, addr);
	else
		ioat_write_double_4(ioat, IOAT_CHAINADDR_OFFSET_LOW, addr);
}

static inline boolean_t
is_ioat_active(uint64_t status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_ACTIVE);
}

static inline boolean_t
is_ioat_idle(uint64_t status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_IDLE);
}

static inline boolean_t
is_ioat_halted(uint64_t status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_HALTED);
}

static inline boolean_t
is_ioat_suspended(uint64_t status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_SUSPENDED);
}

static inline void
ioat_suspend(struct ioat_softc *ioat)
{
	ioat_write_1(ioat, IOAT_CHANCMD_OFFSET, IOAT_CHANCMD_SUSPEND);
}

static inline void
ioat_reset(struct ioat_softc *ioat)
{
	ioat_write_1(ioat, IOAT_CHANCMD_OFFSET, IOAT_CHANCMD_RESET);
}

static inline boolean_t
ioat_reset_pending(struct ioat_softc *ioat)
{
	uint8_t cmd;

	cmd = ioat_read_1(ioat, IOAT_CHANCMD_OFFSET);
	return ((cmd & IOAT_CHANCMD_RESET) != 0);
}

#endif /* __IOAT_INTERNAL_H__ */
