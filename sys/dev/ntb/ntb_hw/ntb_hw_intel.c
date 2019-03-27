/*-
 * Copyright (c) 2016-2017 Alexander Motin <mav@FreeBSD.org>
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2015 EMC Corporation
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

/*
 * The Non-Transparent Bridge (NTB) is a device that allows you to connect
 * two or more systems using a PCI-e links, providing remote memory access.
 *
 * This module contains a driver for NTB hardware in Intel Xeon/Atom CPUs.
 *
 * NOTE: Much of the code in this module is shared with Linux. Any patches may
 * be picked up and redistributed in Linux with a dual GPL/BSD license.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pciio.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "ntb_hw_intel.h"
#include "../ntb.h"

#define MAX_MSIX_INTERRUPTS MAX(XEON_DB_COUNT, ATOM_DB_COUNT)

#define NTB_HB_TIMEOUT		1 /* second */
#define ATOM_LINK_RECOVERY_TIME	500 /* ms */
#define BAR_HIGH_MASK		(~((1ull << 12) - 1))

#define	NTB_MSIX_VER_GUARD	0xaabbccdd
#define	NTB_MSIX_RECEIVED	0xe0f0e0f0

/*
 * PCI constants could be somewhere more generic, but aren't defined/used in
 * pci.c.
 */
#define	PCI_MSIX_ENTRY_SIZE		16
#define	PCI_MSIX_ENTRY_LOWER_ADDR	0
#define	PCI_MSIX_ENTRY_UPPER_ADDR	4
#define	PCI_MSIX_ENTRY_DATA		8

enum ntb_device_type {
	NTB_XEON,
	NTB_ATOM
};

/* ntb_conn_type are hardware numbers, cannot change. */
enum ntb_conn_type {
	NTB_CONN_TRANSPARENT = 0,
	NTB_CONN_B2B = 1,
	NTB_CONN_RP = 2,
};

enum ntb_b2b_direction {
	NTB_DEV_USD = 0,
	NTB_DEV_DSD = 1,
};

enum ntb_bar {
	NTB_CONFIG_BAR = 0,
	NTB_B2B_BAR_1,
	NTB_B2B_BAR_2,
	NTB_B2B_BAR_3,
	NTB_MAX_BARS
};

enum {
	NTB_MSIX_GUARD = 0,
	NTB_MSIX_DATA0,
	NTB_MSIX_DATA1,
	NTB_MSIX_DATA2,
	NTB_MSIX_OFS0,
	NTB_MSIX_OFS1,
	NTB_MSIX_OFS2,
	NTB_MSIX_DONE,
	NTB_MAX_MSIX_SPAD
};

/* Device features and workarounds */
#define HAS_FEATURE(ntb, feature)	\
	(((ntb)->features & (feature)) != 0)

struct ntb_hw_info {
	uint32_t		device_id;
	const char		*desc;
	enum ntb_device_type	type;
	uint32_t		features;
};

struct ntb_pci_bar_info {
	bus_space_tag_t		pci_bus_tag;
	bus_space_handle_t	pci_bus_handle;
	int			pci_resource_id;
	struct resource		*pci_resource;
	vm_paddr_t		pbase;
	caddr_t			vbase;
	vm_size_t		size;
	vm_memattr_t		map_mode;

	/* Configuration register offsets */
	uint32_t		psz_off;
	uint32_t		ssz_off;
	uint32_t		pbarxlat_off;
};

struct ntb_int_info {
	struct resource	*res;
	int		rid;
	void		*tag;
};

struct ntb_vec {
	struct ntb_softc	*ntb;
	uint32_t		num;
	unsigned		masked;
};

struct ntb_reg {
	uint32_t	ntb_ctl;
	uint32_t	lnk_sta;
	uint8_t		db_size;
	unsigned	mw_bar[NTB_MAX_BARS];
};

struct ntb_alt_reg {
	uint32_t	db_bell;
	uint32_t	db_mask;
	uint32_t	spad;
};

struct ntb_xlat_reg {
	uint32_t	bar0_base;
	uint32_t	bar2_base;
	uint32_t	bar4_base;
	uint32_t	bar5_base;

	uint32_t	bar2_xlat;
	uint32_t	bar4_xlat;
	uint32_t	bar5_xlat;

	uint32_t	bar2_limit;
	uint32_t	bar4_limit;
	uint32_t	bar5_limit;
};

struct ntb_b2b_addr {
	uint64_t	bar0_addr;
	uint64_t	bar2_addr64;
	uint64_t	bar4_addr64;
	uint64_t	bar4_addr32;
	uint64_t	bar5_addr32;
};

struct ntb_msix_data {
	uint32_t	nmd_ofs;
	uint32_t	nmd_data;
};

struct ntb_softc {
	/* ntb.c context. Do not move! Must go first! */
	void			*ntb_store;

	device_t		device;
	enum ntb_device_type	type;
	uint32_t		features;

	struct ntb_pci_bar_info	bar_info[NTB_MAX_BARS];
	struct ntb_int_info	int_info[MAX_MSIX_INTERRUPTS];
	uint32_t		allocated_interrupts;

	struct ntb_msix_data	peer_msix_data[XEON_NONLINK_DB_MSIX_BITS];
	struct ntb_msix_data	msix_data[XEON_NONLINK_DB_MSIX_BITS];
	bool			peer_msix_good;
	bool			peer_msix_done;
	struct ntb_pci_bar_info	*peer_lapic_bar;
	struct callout		peer_msix_work;

	struct callout		heartbeat_timer;
	struct callout		lr_timer;

	struct ntb_vec		*msix_vec;

	uint32_t		ppd;
	enum ntb_conn_type	conn_type;
	enum ntb_b2b_direction	dev_type;

	/* Offset of peer bar0 in B2B BAR */
	uint64_t			b2b_off;
	/* Memory window used to access peer bar0 */
#define B2B_MW_DISABLED			UINT8_MAX
	uint8_t				b2b_mw_idx;
	uint32_t			msix_xlat;
	uint8_t				msix_mw_idx;

	uint8_t				mw_count;
	uint8_t				spad_count;
	uint8_t				db_count;
	uint8_t				db_vec_count;
	uint8_t				db_vec_shift;

	/* Protects local db_mask. */
#define DB_MASK_LOCK(sc)	mtx_lock_spin(&(sc)->db_mask_lock)
#define DB_MASK_UNLOCK(sc)	mtx_unlock_spin(&(sc)->db_mask_lock)
#define DB_MASK_ASSERT(sc,f)	mtx_assert(&(sc)->db_mask_lock, (f))
	struct mtx			db_mask_lock;

	volatile uint32_t		ntb_ctl;
	volatile uint32_t		lnk_sta;

	uint64_t			db_valid_mask;
	uint64_t			db_link_mask;
	uint64_t			db_mask;
	uint64_t			fake_db;	/* NTB_SB01BASE_LOCKUP*/
	uint64_t			force_db;	/* NTB_SB01BASE_LOCKUP*/

	int				last_ts;	/* ticks @ last irq */

	const struct ntb_reg		*reg;
	const struct ntb_alt_reg	*self_reg;
	const struct ntb_alt_reg	*peer_reg;
	const struct ntb_xlat_reg	*xlat_reg;
};

#ifdef __i386__
static __inline uint64_t
bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (bus_space_read_4(tag, handle, offset) |
	    ((uint64_t)bus_space_read_4(tag, handle, offset + 4)) << 32);
}

static __inline void
bus_space_write_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint64_t val)
{

	bus_space_write_4(tag, handle, offset, val);
	bus_space_write_4(tag, handle, offset + 4, val >> 32);
}
#endif

#define intel_ntb_bar_read(SIZE, bar, offset) \
	    bus_space_read_ ## SIZE (ntb->bar_info[(bar)].pci_bus_tag, \
	    ntb->bar_info[(bar)].pci_bus_handle, (offset))
#define intel_ntb_bar_write(SIZE, bar, offset, val) \
	    bus_space_write_ ## SIZE (ntb->bar_info[(bar)].pci_bus_tag, \
	    ntb->bar_info[(bar)].pci_bus_handle, (offset), (val))
#define intel_ntb_reg_read(SIZE, offset) \
	    intel_ntb_bar_read(SIZE, NTB_CONFIG_BAR, offset)
#define intel_ntb_reg_write(SIZE, offset, val) \
	    intel_ntb_bar_write(SIZE, NTB_CONFIG_BAR, offset, val)
#define intel_ntb_mw_read(SIZE, offset) \
	    intel_ntb_bar_read(SIZE, intel_ntb_mw_to_bar(ntb, ntb->b2b_mw_idx), \
		offset)
#define intel_ntb_mw_write(SIZE, offset, val) \
	    intel_ntb_bar_write(SIZE, intel_ntb_mw_to_bar(ntb, ntb->b2b_mw_idx), \
		offset, val)

static int intel_ntb_probe(device_t device);
static int intel_ntb_attach(device_t device);
static int intel_ntb_detach(device_t device);
static uint64_t intel_ntb_db_valid_mask(device_t dev);
static void intel_ntb_spad_clear(device_t dev);
static uint64_t intel_ntb_db_vector_mask(device_t dev, uint32_t vector);
static bool intel_ntb_link_is_up(device_t dev, enum ntb_speed *speed,
    enum ntb_width *width);
static int intel_ntb_link_enable(device_t dev, enum ntb_speed speed,
    enum ntb_width width);
static int intel_ntb_link_disable(device_t dev);
static int intel_ntb_spad_read(device_t dev, unsigned int idx, uint32_t *val);
static int intel_ntb_peer_spad_write(device_t dev, unsigned int idx, uint32_t val);

static unsigned intel_ntb_user_mw_to_idx(struct ntb_softc *, unsigned uidx);
static inline enum ntb_bar intel_ntb_mw_to_bar(struct ntb_softc *, unsigned mw);
static inline bool bar_is_64bit(struct ntb_softc *, enum ntb_bar);
static inline void bar_get_xlat_params(struct ntb_softc *, enum ntb_bar,
    uint32_t *base, uint32_t *xlat, uint32_t *lmt);
static int intel_ntb_map_pci_bars(struct ntb_softc *ntb);
static int intel_ntb_mw_set_wc_internal(struct ntb_softc *, unsigned idx,
    vm_memattr_t);
static void print_map_success(struct ntb_softc *, struct ntb_pci_bar_info *,
    const char *);
static int map_mmr_bar(struct ntb_softc *ntb, struct ntb_pci_bar_info *bar);
static int map_memory_window_bar(struct ntb_softc *ntb,
    struct ntb_pci_bar_info *bar);
static void intel_ntb_unmap_pci_bar(struct ntb_softc *ntb);
static int intel_ntb_remap_msix(device_t, uint32_t desired, uint32_t avail);
static int intel_ntb_init_isr(struct ntb_softc *ntb);
static int intel_ntb_setup_legacy_interrupt(struct ntb_softc *ntb);
static int intel_ntb_setup_msix(struct ntb_softc *ntb, uint32_t num_vectors);
static void intel_ntb_teardown_interrupts(struct ntb_softc *ntb);
static inline uint64_t intel_ntb_vec_mask(struct ntb_softc *, uint64_t db_vector);
static void intel_ntb_interrupt(struct ntb_softc *, uint32_t vec);
static void ndev_vec_isr(void *arg);
static void ndev_irq_isr(void *arg);
static inline uint64_t db_ioread(struct ntb_softc *, uint64_t regoff);
static inline void db_iowrite(struct ntb_softc *, uint64_t regoff, uint64_t);
static inline void db_iowrite_raw(struct ntb_softc *, uint64_t regoff, uint64_t);
static int intel_ntb_create_msix_vec(struct ntb_softc *ntb, uint32_t num_vectors);
static void intel_ntb_free_msix_vec(struct ntb_softc *ntb);
static void intel_ntb_get_msix_info(struct ntb_softc *ntb);
static void intel_ntb_exchange_msix(void *);
static struct ntb_hw_info *intel_ntb_get_device_info(uint32_t device_id);
static void intel_ntb_detect_max_mw(struct ntb_softc *ntb);
static int intel_ntb_detect_xeon(struct ntb_softc *ntb);
static int intel_ntb_detect_atom(struct ntb_softc *ntb);
static int intel_ntb_xeon_init_dev(struct ntb_softc *ntb);
static int intel_ntb_atom_init_dev(struct ntb_softc *ntb);
static void intel_ntb_teardown_xeon(struct ntb_softc *ntb);
static void configure_atom_secondary_side_bars(struct ntb_softc *ntb);
static void xeon_reset_sbar_size(struct ntb_softc *, enum ntb_bar idx,
    enum ntb_bar regbar);
static void xeon_set_sbar_base_and_limit(struct ntb_softc *,
    uint64_t base_addr, enum ntb_bar idx, enum ntb_bar regbar);
static void xeon_set_pbar_xlat(struct ntb_softc *, uint64_t base_addr,
    enum ntb_bar idx);
static int xeon_setup_b2b_mw(struct ntb_softc *,
    const struct ntb_b2b_addr *addr, const struct ntb_b2b_addr *peer_addr);
static inline bool link_is_up(struct ntb_softc *ntb);
static inline bool _xeon_link_is_up(struct ntb_softc *ntb);
static inline bool atom_link_is_err(struct ntb_softc *ntb);
static inline enum ntb_speed intel_ntb_link_sta_speed(struct ntb_softc *);
static inline enum ntb_width intel_ntb_link_sta_width(struct ntb_softc *);
static void atom_link_hb(void *arg);
static void recover_atom_link(void *arg);
static bool intel_ntb_poll_link(struct ntb_softc *ntb);
static void save_bar_parameters(struct ntb_pci_bar_info *bar);
static void intel_ntb_sysctl_init(struct ntb_softc *);
static int sysctl_handle_features(SYSCTL_HANDLER_ARGS);
static int sysctl_handle_link_admin(SYSCTL_HANDLER_ARGS);
static int sysctl_handle_link_status_human(SYSCTL_HANDLER_ARGS);
static int sysctl_handle_link_status(SYSCTL_HANDLER_ARGS);
static int sysctl_handle_register(SYSCTL_HANDLER_ARGS);

static unsigned g_ntb_hw_debug_level;
SYSCTL_UINT(_hw_ntb, OID_AUTO, debug_level, CTLFLAG_RWTUN,
    &g_ntb_hw_debug_level, 0, "ntb_hw log level -- higher is more verbose");
#define intel_ntb_printf(lvl, ...) do {				\
	if ((lvl) <= g_ntb_hw_debug_level) {			\
		device_printf(ntb->device, __VA_ARGS__);	\
	}							\
} while (0)

#define	_NTB_PAT_UC	0
#define	_NTB_PAT_WC	1
#define	_NTB_PAT_WT	4
#define	_NTB_PAT_WP	5
#define	_NTB_PAT_WB	6
#define	_NTB_PAT_UCM	7
static unsigned g_ntb_mw_pat = _NTB_PAT_UC;
SYSCTL_UINT(_hw_ntb, OID_AUTO, default_mw_pat, CTLFLAG_RDTUN,
    &g_ntb_mw_pat, 0, "Configure the default memory window cache flags (PAT): "
    "UC: "  __XSTRING(_NTB_PAT_UC) ", "
    "WC: "  __XSTRING(_NTB_PAT_WC) ", "
    "WT: "  __XSTRING(_NTB_PAT_WT) ", "
    "WP: "  __XSTRING(_NTB_PAT_WP) ", "
    "WB: "  __XSTRING(_NTB_PAT_WB) ", "
    "UC-: " __XSTRING(_NTB_PAT_UCM));

static inline vm_memattr_t
intel_ntb_pat_flags(void)
{

	switch (g_ntb_mw_pat) {
	case _NTB_PAT_WC:
		return (VM_MEMATTR_WRITE_COMBINING);
	case _NTB_PAT_WT:
		return (VM_MEMATTR_WRITE_THROUGH);
	case _NTB_PAT_WP:
		return (VM_MEMATTR_WRITE_PROTECTED);
	case _NTB_PAT_WB:
		return (VM_MEMATTR_WRITE_BACK);
	case _NTB_PAT_UCM:
		return (VM_MEMATTR_WEAK_UNCACHEABLE);
	case _NTB_PAT_UC:
		/* FALLTHROUGH */
	default:
		return (VM_MEMATTR_UNCACHEABLE);
	}
}

/*
 * Well, this obviously doesn't belong here, but it doesn't seem to exist
 * anywhere better yet.
 */
static inline const char *
intel_ntb_vm_memattr_to_str(vm_memattr_t pat)
{

	switch (pat) {
	case VM_MEMATTR_WRITE_COMBINING:
		return ("WRITE_COMBINING");
	case VM_MEMATTR_WRITE_THROUGH:
		return ("WRITE_THROUGH");
	case VM_MEMATTR_WRITE_PROTECTED:
		return ("WRITE_PROTECTED");
	case VM_MEMATTR_WRITE_BACK:
		return ("WRITE_BACK");
	case VM_MEMATTR_WEAK_UNCACHEABLE:
		return ("UNCACHED");
	case VM_MEMATTR_UNCACHEABLE:
		return ("UNCACHEABLE");
	default:
		return ("UNKNOWN");
	}
}

static int g_ntb_msix_idx = 1;
SYSCTL_INT(_hw_ntb, OID_AUTO, msix_mw_idx, CTLFLAG_RDTUN, &g_ntb_msix_idx,
    0, "Use this memory window to access the peer MSIX message complex on "
    "certain Xeon-based NTB systems, as a workaround for a hardware errata.  "
    "Like b2b_mw_idx, negative values index from the last available memory "
    "window.  (Applies on Xeon platforms with SB01BASE_LOCKUP errata.)");

static int g_ntb_mw_idx = -1;
SYSCTL_INT(_hw_ntb, OID_AUTO, b2b_mw_idx, CTLFLAG_RDTUN, &g_ntb_mw_idx,
    0, "Use this memory window to access the peer NTB registers.  A "
    "non-negative value starts from the first MW index; a negative value "
    "starts from the last MW index.  The default is -1, i.e., the last "
    "available memory window.  Both sides of the NTB MUST set the same "
    "value here!  (Applies on Xeon platforms with SDOORBELL_LOCKUP errata.)");

/* Hardware owns the low 16 bits of features. */
#define NTB_BAR_SIZE_4K		(1 << 0)
#define NTB_SDOORBELL_LOCKUP	(1 << 1)
#define NTB_SB01BASE_LOCKUP	(1 << 2)
#define NTB_B2BDOORBELL_BIT14	(1 << 3)
/* Software/configuration owns the top 16 bits. */
#define NTB_SPLIT_BAR		(1ull << 16)

#define NTB_FEATURES_STR \
    "\20\21SPLIT_BAR4\04B2B_DOORBELL_BIT14\03SB01BASE_LOCKUP" \
    "\02SDOORBELL_LOCKUP\01BAR_SIZE_4K"

static struct ntb_hw_info pci_ids[] = {
	/* XXX: PS/SS IDs left out until they are supported. */
	{ 0x0C4E8086, "BWD Atom Processor S1200 Non-Transparent Bridge B2B",
		NTB_ATOM, 0 },

	{ 0x37258086, "JSF Xeon C35xx/C55xx Non-Transparent Bridge B2B",
		NTB_XEON, NTB_SDOORBELL_LOCKUP | NTB_B2BDOORBELL_BIT14 },
	{ 0x3C0D8086, "SNB Xeon E5/Core i7 Non-Transparent Bridge B2B",
		NTB_XEON, NTB_SDOORBELL_LOCKUP | NTB_B2BDOORBELL_BIT14 },
	{ 0x0E0D8086, "IVT Xeon E5 V2 Non-Transparent Bridge B2B", NTB_XEON,
		NTB_SDOORBELL_LOCKUP | NTB_B2BDOORBELL_BIT14 |
		    NTB_SB01BASE_LOCKUP | NTB_BAR_SIZE_4K },
	{ 0x2F0D8086, "HSX Xeon E5 V3 Non-Transparent Bridge B2B", NTB_XEON,
		NTB_SDOORBELL_LOCKUP | NTB_B2BDOORBELL_BIT14 |
		    NTB_SB01BASE_LOCKUP },
	{ 0x6F0D8086, "BDX Xeon E5 V4 Non-Transparent Bridge B2B", NTB_XEON,
		NTB_SDOORBELL_LOCKUP | NTB_B2BDOORBELL_BIT14 |
		    NTB_SB01BASE_LOCKUP },
};

static const struct ntb_reg atom_reg = {
	.ntb_ctl = ATOM_NTBCNTL_OFFSET,
	.lnk_sta = ATOM_LINK_STATUS_OFFSET,
	.db_size = sizeof(uint64_t),
	.mw_bar = { NTB_B2B_BAR_1, NTB_B2B_BAR_2 },
};

static const struct ntb_alt_reg atom_pri_reg = {
	.db_bell = ATOM_PDOORBELL_OFFSET,
	.db_mask = ATOM_PDBMSK_OFFSET,
	.spad = ATOM_SPAD_OFFSET,
};

static const struct ntb_alt_reg atom_b2b_reg = {
	.db_bell = ATOM_B2B_DOORBELL_OFFSET,
	.spad = ATOM_B2B_SPAD_OFFSET,
};

static const struct ntb_xlat_reg atom_sec_xlat = {
#if 0
	/* "FIXME" says the Linux driver. */
	.bar0_base = ATOM_SBAR0BASE_OFFSET,
	.bar2_base = ATOM_SBAR2BASE_OFFSET,
	.bar4_base = ATOM_SBAR4BASE_OFFSET,

	.bar2_limit = ATOM_SBAR2LMT_OFFSET,
	.bar4_limit = ATOM_SBAR4LMT_OFFSET,
#endif

	.bar2_xlat = ATOM_SBAR2XLAT_OFFSET,
	.bar4_xlat = ATOM_SBAR4XLAT_OFFSET,
};

static const struct ntb_reg xeon_reg = {
	.ntb_ctl = XEON_NTBCNTL_OFFSET,
	.lnk_sta = XEON_LINK_STATUS_OFFSET,
	.db_size = sizeof(uint16_t),
	.mw_bar = { NTB_B2B_BAR_1, NTB_B2B_BAR_2, NTB_B2B_BAR_3 },
};

static const struct ntb_alt_reg xeon_pri_reg = {
	.db_bell = XEON_PDOORBELL_OFFSET,
	.db_mask = XEON_PDBMSK_OFFSET,
	.spad = XEON_SPAD_OFFSET,
};

static const struct ntb_alt_reg xeon_b2b_reg = {
	.db_bell = XEON_B2B_DOORBELL_OFFSET,
	.spad = XEON_B2B_SPAD_OFFSET,
};

static const struct ntb_xlat_reg xeon_sec_xlat = {
	.bar0_base = XEON_SBAR0BASE_OFFSET,
	.bar2_base = XEON_SBAR2BASE_OFFSET,
	.bar4_base = XEON_SBAR4BASE_OFFSET,
	.bar5_base = XEON_SBAR5BASE_OFFSET,

	.bar2_limit = XEON_SBAR2LMT_OFFSET,
	.bar4_limit = XEON_SBAR4LMT_OFFSET,
	.bar5_limit = XEON_SBAR5LMT_OFFSET,

	.bar2_xlat = XEON_SBAR2XLAT_OFFSET,
	.bar4_xlat = XEON_SBAR4XLAT_OFFSET,
	.bar5_xlat = XEON_SBAR5XLAT_OFFSET,
};

static struct ntb_b2b_addr xeon_b2b_usd_addr = {
	.bar0_addr = XEON_B2B_BAR0_ADDR,
	.bar2_addr64 = XEON_B2B_BAR2_ADDR64,
	.bar4_addr64 = XEON_B2B_BAR4_ADDR64,
	.bar4_addr32 = XEON_B2B_BAR4_ADDR32,
	.bar5_addr32 = XEON_B2B_BAR5_ADDR32,
};

static struct ntb_b2b_addr xeon_b2b_dsd_addr = {
	.bar0_addr = XEON_B2B_BAR0_ADDR,
	.bar2_addr64 = XEON_B2B_BAR2_ADDR64,
	.bar4_addr64 = XEON_B2B_BAR4_ADDR64,
	.bar4_addr32 = XEON_B2B_BAR4_ADDR32,
	.bar5_addr32 = XEON_B2B_BAR5_ADDR32,
};

SYSCTL_NODE(_hw_ntb, OID_AUTO, xeon_b2b, CTLFLAG_RW, 0,
    "B2B MW segment overrides -- MUST be the same on both sides");

SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, usd_bar2_addr64, CTLFLAG_RDTUN,
    &xeon_b2b_usd_addr.bar2_addr64, 0, "If using B2B topology on Xeon "
    "hardware, use this 64-bit address on the bus between the NTB devices for "
    "the window at BAR2, on the upstream side of the link.  MUST be the same "
    "address on both sides.");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, usd_bar4_addr64, CTLFLAG_RDTUN,
    &xeon_b2b_usd_addr.bar4_addr64, 0, "See usd_bar2_addr64, but BAR4.");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, usd_bar4_addr32, CTLFLAG_RDTUN,
    &xeon_b2b_usd_addr.bar4_addr32, 0, "See usd_bar2_addr64, but BAR4 "
    "(split-BAR mode).");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, usd_bar5_addr32, CTLFLAG_RDTUN,
    &xeon_b2b_usd_addr.bar5_addr32, 0, "See usd_bar2_addr64, but BAR5 "
    "(split-BAR mode).");

SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, dsd_bar2_addr64, CTLFLAG_RDTUN,
    &xeon_b2b_dsd_addr.bar2_addr64, 0, "If using B2B topology on Xeon "
    "hardware, use this 64-bit address on the bus between the NTB devices for "
    "the window at BAR2, on the downstream side of the link.  MUST be the same"
    " address on both sides.");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, dsd_bar4_addr64, CTLFLAG_RDTUN,
    &xeon_b2b_dsd_addr.bar4_addr64, 0, "See dsd_bar2_addr64, but BAR4.");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, dsd_bar4_addr32, CTLFLAG_RDTUN,
    &xeon_b2b_dsd_addr.bar4_addr32, 0, "See dsd_bar2_addr64, but BAR4 "
    "(split-BAR mode).");
SYSCTL_UQUAD(_hw_ntb_xeon_b2b, OID_AUTO, dsd_bar5_addr32, CTLFLAG_RDTUN,
    &xeon_b2b_dsd_addr.bar5_addr32, 0, "See dsd_bar2_addr64, but BAR5 "
    "(split-BAR mode).");

/*
 * OS <-> Driver interface structures
 */
MALLOC_DEFINE(M_NTB, "ntb_hw", "ntb_hw driver memory allocations");

/*
 * OS <-> Driver linkage functions
 */
static int
intel_ntb_probe(device_t device)
{
	struct ntb_hw_info *p;

	p = intel_ntb_get_device_info(pci_get_devid(device));
	if (p == NULL)
		return (ENXIO);

	device_set_desc(device, p->desc);
	return (0);
}

static int
intel_ntb_attach(device_t device)
{
	struct ntb_softc *ntb;
	struct ntb_hw_info *p;
	int error;

	ntb = device_get_softc(device);
	p = intel_ntb_get_device_info(pci_get_devid(device));

	ntb->device = device;
	ntb->type = p->type;
	ntb->features = p->features;
	ntb->b2b_mw_idx = B2B_MW_DISABLED;
	ntb->msix_mw_idx = B2B_MW_DISABLED;

	/* Heartbeat timer for NTB_ATOM since there is no link interrupt */
	callout_init(&ntb->heartbeat_timer, 1);
	callout_init(&ntb->lr_timer, 1);
	callout_init(&ntb->peer_msix_work, 1);
	mtx_init(&ntb->db_mask_lock, "ntb hw bits", NULL, MTX_SPIN);

	if (ntb->type == NTB_ATOM)
		error = intel_ntb_detect_atom(ntb);
	else
		error = intel_ntb_detect_xeon(ntb);
	if (error != 0)
		goto out;

	intel_ntb_detect_max_mw(ntb);

	pci_enable_busmaster(ntb->device);

	error = intel_ntb_map_pci_bars(ntb);
	if (error != 0)
		goto out;
	if (ntb->type == NTB_ATOM)
		error = intel_ntb_atom_init_dev(ntb);
	else
		error = intel_ntb_xeon_init_dev(ntb);
	if (error != 0)
		goto out;

	intel_ntb_spad_clear(device);

	intel_ntb_poll_link(ntb);

	intel_ntb_sysctl_init(ntb);

	/* Attach children to this controller */
	error = ntb_register_device(device);

out:
	if (error != 0)
		intel_ntb_detach(device);
	return (error);
}

static int
intel_ntb_detach(device_t device)
{
	struct ntb_softc *ntb;

	ntb = device_get_softc(device);

	/* Detach & delete all children */
	ntb_unregister_device(device);

	if (ntb->self_reg != NULL) {
		DB_MASK_LOCK(ntb);
		db_iowrite(ntb, ntb->self_reg->db_mask, ntb->db_valid_mask);
		DB_MASK_UNLOCK(ntb);
	}
	callout_drain(&ntb->heartbeat_timer);
	callout_drain(&ntb->lr_timer);
	callout_drain(&ntb->peer_msix_work);
	pci_disable_busmaster(ntb->device);
	if (ntb->type == NTB_XEON)
		intel_ntb_teardown_xeon(ntb);
	intel_ntb_teardown_interrupts(ntb);

	mtx_destroy(&ntb->db_mask_lock);

	intel_ntb_unmap_pci_bar(ntb);

	return (0);
}

/*
 * Driver internal routines
 */
static inline enum ntb_bar
intel_ntb_mw_to_bar(struct ntb_softc *ntb, unsigned mw)
{

	KASSERT(mw < ntb->mw_count,
	    ("%s: mw:%u > count:%u", __func__, mw, (unsigned)ntb->mw_count));
	KASSERT(ntb->reg->mw_bar[mw] != 0, ("invalid mw"));

	return (ntb->reg->mw_bar[mw]);
}

static inline bool
bar_is_64bit(struct ntb_softc *ntb, enum ntb_bar bar)
{
	/* XXX This assertion could be stronger. */
	KASSERT(bar < NTB_MAX_BARS, ("bogus bar"));
	return (bar < NTB_B2B_BAR_2 || !HAS_FEATURE(ntb, NTB_SPLIT_BAR));
}

static inline void
bar_get_xlat_params(struct ntb_softc *ntb, enum ntb_bar bar, uint32_t *base,
    uint32_t *xlat, uint32_t *lmt)
{
	uint32_t basev, lmtv, xlatv;

	switch (bar) {
	case NTB_B2B_BAR_1:
		basev = ntb->xlat_reg->bar2_base;
		lmtv = ntb->xlat_reg->bar2_limit;
		xlatv = ntb->xlat_reg->bar2_xlat;
		break;
	case NTB_B2B_BAR_2:
		basev = ntb->xlat_reg->bar4_base;
		lmtv = ntb->xlat_reg->bar4_limit;
		xlatv = ntb->xlat_reg->bar4_xlat;
		break;
	case NTB_B2B_BAR_3:
		basev = ntb->xlat_reg->bar5_base;
		lmtv = ntb->xlat_reg->bar5_limit;
		xlatv = ntb->xlat_reg->bar5_xlat;
		break;
	default:
		KASSERT(bar >= NTB_B2B_BAR_1 && bar < NTB_MAX_BARS,
		    ("bad bar"));
		basev = lmtv = xlatv = 0;
		break;
	}

	if (base != NULL)
		*base = basev;
	if (xlat != NULL)
		*xlat = xlatv;
	if (lmt != NULL)
		*lmt = lmtv;
}

static int
intel_ntb_map_pci_bars(struct ntb_softc *ntb)
{
	int rc;

	ntb->bar_info[NTB_CONFIG_BAR].pci_resource_id = PCIR_BAR(0);
	rc = map_mmr_bar(ntb, &ntb->bar_info[NTB_CONFIG_BAR]);
	if (rc != 0)
		goto out;

	ntb->bar_info[NTB_B2B_BAR_1].pci_resource_id = PCIR_BAR(2);
	rc = map_memory_window_bar(ntb, &ntb->bar_info[NTB_B2B_BAR_1]);
	if (rc != 0)
		goto out;
	ntb->bar_info[NTB_B2B_BAR_1].psz_off = XEON_PBAR23SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_1].ssz_off = XEON_SBAR23SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_1].pbarxlat_off = XEON_PBAR2XLAT_OFFSET;

	ntb->bar_info[NTB_B2B_BAR_2].pci_resource_id = PCIR_BAR(4);
	rc = map_memory_window_bar(ntb, &ntb->bar_info[NTB_B2B_BAR_2]);
	if (rc != 0)
		goto out;
	ntb->bar_info[NTB_B2B_BAR_2].psz_off = XEON_PBAR4SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_2].ssz_off = XEON_SBAR4SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_2].pbarxlat_off = XEON_PBAR4XLAT_OFFSET;

	if (!HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		goto out;

	ntb->bar_info[NTB_B2B_BAR_3].pci_resource_id = PCIR_BAR(5);
	rc = map_memory_window_bar(ntb, &ntb->bar_info[NTB_B2B_BAR_3]);
	ntb->bar_info[NTB_B2B_BAR_3].psz_off = XEON_PBAR5SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_3].ssz_off = XEON_SBAR5SZ_OFFSET;
	ntb->bar_info[NTB_B2B_BAR_3].pbarxlat_off = XEON_PBAR5XLAT_OFFSET;

out:
	if (rc != 0)
		device_printf(ntb->device,
		    "unable to allocate pci resource\n");
	return (rc);
}

static void
print_map_success(struct ntb_softc *ntb, struct ntb_pci_bar_info *bar,
    const char *kind)
{

	device_printf(ntb->device,
	    "Mapped BAR%d v:[%p-%p] p:[%p-%p] (0x%jx bytes) (%s)\n",
	    PCI_RID2BAR(bar->pci_resource_id), bar->vbase,
	    (char *)bar->vbase + bar->size - 1,
	    (void *)bar->pbase, (void *)(bar->pbase + bar->size - 1),
	    (uintmax_t)bar->size, kind);
}

static int
map_mmr_bar(struct ntb_softc *ntb, struct ntb_pci_bar_info *bar)
{

	bar->pci_resource = bus_alloc_resource_any(ntb->device, SYS_RES_MEMORY,
	    &bar->pci_resource_id, RF_ACTIVE);
	if (bar->pci_resource == NULL)
		return (ENXIO);

	save_bar_parameters(bar);
	bar->map_mode = VM_MEMATTR_UNCACHEABLE;
	print_map_success(ntb, bar, "mmr");
	return (0);
}

static int
map_memory_window_bar(struct ntb_softc *ntb, struct ntb_pci_bar_info *bar)
{
	int rc;
	vm_memattr_t mapmode;
	uint8_t bar_size_bits = 0;

	bar->pci_resource = bus_alloc_resource_any(ntb->device, SYS_RES_MEMORY,
	    &bar->pci_resource_id, RF_ACTIVE);

	if (bar->pci_resource == NULL)
		return (ENXIO);

	save_bar_parameters(bar);
	/*
	 * Ivytown NTB BAR sizes are misreported by the hardware due to a
	 * hardware issue. To work around this, query the size it should be
	 * configured to by the device and modify the resource to correspond to
	 * this new size. The BIOS on systems with this problem is required to
	 * provide enough address space to allow the driver to make this change
	 * safely.
	 *
	 * Ideally I could have just specified the size when I allocated the
	 * resource like:
	 *  bus_alloc_resource(ntb->device,
	 *	SYS_RES_MEMORY, &bar->pci_resource_id, 0ul, ~0ul,
	 *	1ul << bar_size_bits, RF_ACTIVE);
	 * but the PCI driver does not honor the size in this call, so we have
	 * to modify it after the fact.
	 */
	if (HAS_FEATURE(ntb, NTB_BAR_SIZE_4K)) {
		if (bar->pci_resource_id == PCIR_BAR(2))
			bar_size_bits = pci_read_config(ntb->device,
			    XEON_PBAR23SZ_OFFSET, 1);
		else
			bar_size_bits = pci_read_config(ntb->device,
			    XEON_PBAR45SZ_OFFSET, 1);

		rc = bus_adjust_resource(ntb->device, SYS_RES_MEMORY,
		    bar->pci_resource, bar->pbase,
		    bar->pbase + (1ul << bar_size_bits) - 1);
		if (rc != 0) {
			device_printf(ntb->device,
			    "unable to resize bar\n");
			return (rc);
		}

		save_bar_parameters(bar);
	}

	bar->map_mode = VM_MEMATTR_UNCACHEABLE;
	print_map_success(ntb, bar, "mw");

	/*
	 * Optionally, mark MW BARs as anything other than UC to improve
	 * performance.
	 */
	mapmode = intel_ntb_pat_flags();
	if (mapmode == bar->map_mode)
		return (0);

	rc = pmap_change_attr((vm_offset_t)bar->vbase, bar->size, mapmode);
	if (rc == 0) {
		bar->map_mode = mapmode;
		device_printf(ntb->device,
		    "Marked BAR%d v:[%p-%p] p:[%p-%p] as "
		    "%s.\n",
		    PCI_RID2BAR(bar->pci_resource_id), bar->vbase,
		    (char *)bar->vbase + bar->size - 1,
		    (void *)bar->pbase, (void *)(bar->pbase + bar->size - 1),
		    intel_ntb_vm_memattr_to_str(mapmode));
	} else
		device_printf(ntb->device,
		    "Unable to mark BAR%d v:[%p-%p] p:[%p-%p] as "
		    "%s: %d\n",
		    PCI_RID2BAR(bar->pci_resource_id), bar->vbase,
		    (char *)bar->vbase + bar->size - 1,
		    (void *)bar->pbase, (void *)(bar->pbase + bar->size - 1),
		    intel_ntb_vm_memattr_to_str(mapmode), rc);
		/* Proceed anyway */
	return (0);
}

static void
intel_ntb_unmap_pci_bar(struct ntb_softc *ntb)
{
	struct ntb_pci_bar_info *current_bar;
	int i;

	for (i = 0; i < NTB_MAX_BARS; i++) {
		current_bar = &ntb->bar_info[i];
		if (current_bar->pci_resource != NULL)
			bus_release_resource(ntb->device, SYS_RES_MEMORY,
			    current_bar->pci_resource_id,
			    current_bar->pci_resource);
	}
}

static int
intel_ntb_setup_msix(struct ntb_softc *ntb, uint32_t num_vectors)
{
	uint32_t i;
	int rc;

	for (i = 0; i < num_vectors; i++) {
		ntb->int_info[i].rid = i + 1;
		ntb->int_info[i].res = bus_alloc_resource_any(ntb->device,
		    SYS_RES_IRQ, &ntb->int_info[i].rid, RF_ACTIVE);
		if (ntb->int_info[i].res == NULL) {
			device_printf(ntb->device,
			    "bus_alloc_resource failed\n");
			return (ENOMEM);
		}
		ntb->int_info[i].tag = NULL;
		ntb->allocated_interrupts++;
		rc = bus_setup_intr(ntb->device, ntb->int_info[i].res,
		    INTR_MPSAFE | INTR_TYPE_MISC, NULL, ndev_vec_isr,
		    &ntb->msix_vec[i], &ntb->int_info[i].tag);
		if (rc != 0) {
			device_printf(ntb->device, "bus_setup_intr failed\n");
			return (ENXIO);
		}
	}
	return (0);
}

/*
 * The Linux NTB driver drops from MSI-X to legacy INTx if a unique vector
 * cannot be allocated for each MSI-X message.  JHB seems to think remapping
 * should be okay.  This tunable should enable us to test that hypothesis
 * when someone gets their hands on some Xeon hardware.
 */
static int ntb_force_remap_mode;
SYSCTL_INT(_hw_ntb, OID_AUTO, force_remap_mode, CTLFLAG_RDTUN,
    &ntb_force_remap_mode, 0, "If enabled, force MSI-X messages to be remapped"
    " to a smaller number of ithreads, even if the desired number are "
    "available");

/*
 * In case it is NOT ok, give consumers an abort button.
 */
static int ntb_prefer_intx;
SYSCTL_INT(_hw_ntb, OID_AUTO, prefer_intx_to_remap, CTLFLAG_RDTUN,
    &ntb_prefer_intx, 0, "If enabled, prefer to use legacy INTx mode rather "
    "than remapping MSI-X messages over available slots (match Linux driver "
    "behavior)");

/*
 * Remap the desired number of MSI-X messages to available ithreads in a simple
 * round-robin fashion.
 */
static int
intel_ntb_remap_msix(device_t dev, uint32_t desired, uint32_t avail)
{
	u_int *vectors;
	uint32_t i;
	int rc;

	if (ntb_prefer_intx != 0)
		return (ENXIO);

	vectors = malloc(desired * sizeof(*vectors), M_NTB, M_ZERO | M_WAITOK);

	for (i = 0; i < desired; i++)
		vectors[i] = (i % avail) + 1;

	rc = pci_remap_msix(dev, desired, vectors);
	free(vectors, M_NTB);
	return (rc);
}

static int
intel_ntb_init_isr(struct ntb_softc *ntb)
{
	uint32_t desired_vectors, num_vectors;
	int rc;

	ntb->allocated_interrupts = 0;
	ntb->last_ts = ticks;

	/*
	 * Mask all doorbell interrupts.  (Except link events!)
	 */
	DB_MASK_LOCK(ntb);
	ntb->db_mask = ntb->db_valid_mask;
	db_iowrite(ntb, ntb->self_reg->db_mask, ntb->db_mask);
	DB_MASK_UNLOCK(ntb);

	num_vectors = desired_vectors = MIN(pci_msix_count(ntb->device),
	    ntb->db_count);
	if (desired_vectors >= 1) {
		rc = pci_alloc_msix(ntb->device, &num_vectors);

		if (ntb_force_remap_mode != 0 && rc == 0 &&
		    num_vectors == desired_vectors)
			num_vectors--;

		if (rc == 0 && num_vectors < desired_vectors) {
			rc = intel_ntb_remap_msix(ntb->device, desired_vectors,
			    num_vectors);
			if (rc == 0)
				num_vectors = desired_vectors;
			else
				pci_release_msi(ntb->device);
		}
		if (rc != 0)
			num_vectors = 1;
	} else
		num_vectors = 1;

	if (ntb->type == NTB_XEON && num_vectors < ntb->db_vec_count) {
		if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
			device_printf(ntb->device,
			    "Errata workaround does not support MSI or INTX\n");
			return (EINVAL);
		}

		ntb->db_vec_count = 1;
		ntb->db_vec_shift = XEON_DB_TOTAL_SHIFT;
		rc = intel_ntb_setup_legacy_interrupt(ntb);
	} else {
		if (num_vectors - 1 != XEON_NONLINK_DB_MSIX_BITS &&
		    HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
			device_printf(ntb->device,
			    "Errata workaround expects %d doorbell bits\n",
			    XEON_NONLINK_DB_MSIX_BITS);
			return (EINVAL);
		}

		intel_ntb_create_msix_vec(ntb, num_vectors);
		rc = intel_ntb_setup_msix(ntb, num_vectors);
	}
	if (rc != 0) {
		device_printf(ntb->device,
		    "Error allocating interrupts: %d\n", rc);
		intel_ntb_free_msix_vec(ntb);
	}

	return (rc);
}

static int
intel_ntb_setup_legacy_interrupt(struct ntb_softc *ntb)
{
	int rc;

	ntb->int_info[0].rid = 0;
	ntb->int_info[0].res = bus_alloc_resource_any(ntb->device, SYS_RES_IRQ,
	    &ntb->int_info[0].rid, RF_SHAREABLE|RF_ACTIVE);
	if (ntb->int_info[0].res == NULL) {
		device_printf(ntb->device, "bus_alloc_resource failed\n");
		return (ENOMEM);
	}

	ntb->int_info[0].tag = NULL;
	ntb->allocated_interrupts = 1;

	rc = bus_setup_intr(ntb->device, ntb->int_info[0].res,
	    INTR_MPSAFE | INTR_TYPE_MISC, NULL, ndev_irq_isr,
	    ntb, &ntb->int_info[0].tag);
	if (rc != 0) {
		device_printf(ntb->device, "bus_setup_intr failed\n");
		return (ENXIO);
	}

	return (0);
}

static void
intel_ntb_teardown_interrupts(struct ntb_softc *ntb)
{
	struct ntb_int_info *current_int;
	int i;

	for (i = 0; i < ntb->allocated_interrupts; i++) {
		current_int = &ntb->int_info[i];
		if (current_int->tag != NULL)
			bus_teardown_intr(ntb->device, current_int->res,
			    current_int->tag);

		if (current_int->res != NULL)
			bus_release_resource(ntb->device, SYS_RES_IRQ,
			    rman_get_rid(current_int->res), current_int->res);
	}

	intel_ntb_free_msix_vec(ntb);
	pci_release_msi(ntb->device);
}

/*
 * Doorbell register and mask are 64-bit on Atom, 16-bit on Xeon.  Abstract it
 * out to make code clearer.
 */
static inline uint64_t
db_ioread(struct ntb_softc *ntb, uint64_t regoff)
{

	if (ntb->type == NTB_ATOM)
		return (intel_ntb_reg_read(8, regoff));

	KASSERT(ntb->type == NTB_XEON, ("bad ntb type"));

	return (intel_ntb_reg_read(2, regoff));
}

static inline void
db_iowrite(struct ntb_softc *ntb, uint64_t regoff, uint64_t val)
{

	KASSERT((val & ~ntb->db_valid_mask) == 0,
	    ("%s: Invalid bits 0x%jx (valid: 0x%jx)", __func__,
	     (uintmax_t)(val & ~ntb->db_valid_mask),
	     (uintmax_t)ntb->db_valid_mask));

	if (regoff == ntb->self_reg->db_mask)
		DB_MASK_ASSERT(ntb, MA_OWNED);
	db_iowrite_raw(ntb, regoff, val);
}

static inline void
db_iowrite_raw(struct ntb_softc *ntb, uint64_t regoff, uint64_t val)
{

	if (ntb->type == NTB_ATOM) {
		intel_ntb_reg_write(8, regoff, val);
		return;
	}

	KASSERT(ntb->type == NTB_XEON, ("bad ntb type"));
	intel_ntb_reg_write(2, regoff, (uint16_t)val);
}

static void
intel_ntb_db_set_mask(device_t dev, uint64_t bits)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	DB_MASK_LOCK(ntb);
	ntb->db_mask |= bits;
	if (!HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP))
		db_iowrite(ntb, ntb->self_reg->db_mask, ntb->db_mask);
	DB_MASK_UNLOCK(ntb);
}

static void
intel_ntb_db_clear_mask(device_t dev, uint64_t bits)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	uint64_t ibits;
	int i;

	KASSERT((bits & ~ntb->db_valid_mask) == 0,
	    ("%s: Invalid bits 0x%jx (valid: 0x%jx)", __func__,
	     (uintmax_t)(bits & ~ntb->db_valid_mask),
	     (uintmax_t)ntb->db_valid_mask));

	DB_MASK_LOCK(ntb);
	ibits = ntb->fake_db & ntb->db_mask & bits;
	ntb->db_mask &= ~bits;
	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		/* Simulate fake interrupts if unmasked DB bits are set. */
		ntb->force_db |= ibits;
		for (i = 0; i < XEON_NONLINK_DB_MSIX_BITS; i++) {
			if ((ibits & intel_ntb_db_vector_mask(dev, i)) != 0)
				swi_sched(ntb->int_info[i].tag, 0);
		}
	} else {
		db_iowrite(ntb, ntb->self_reg->db_mask, ntb->db_mask);
	}
	DB_MASK_UNLOCK(ntb);
}

static uint64_t
intel_ntb_db_read(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP))
		return (ntb->fake_db);

	return (db_ioread(ntb, ntb->self_reg->db_bell));
}

static void
intel_ntb_db_clear(device_t dev, uint64_t bits)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	KASSERT((bits & ~ntb->db_valid_mask) == 0,
	    ("%s: Invalid bits 0x%jx (valid: 0x%jx)", __func__,
	     (uintmax_t)(bits & ~ntb->db_valid_mask),
	     (uintmax_t)ntb->db_valid_mask));

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		DB_MASK_LOCK(ntb);
		ntb->fake_db &= ~bits;
		DB_MASK_UNLOCK(ntb);
		return;
	}

	db_iowrite(ntb, ntb->self_reg->db_bell, bits);
}

static inline uint64_t
intel_ntb_vec_mask(struct ntb_softc *ntb, uint64_t db_vector)
{
	uint64_t shift, mask;

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		/*
		 * Remap vectors in custom way to make at least first
		 * three doorbells to not generate stray events.
		 * This breaks Linux compatibility (if one existed)
		 * when more then one DB is used (not by if_ntb).
		 */
		if (db_vector < XEON_NONLINK_DB_MSIX_BITS - 1)
			return (1 << db_vector);
		if (db_vector == XEON_NONLINK_DB_MSIX_BITS - 1)
			return (0x7ffc);
	}

	shift = ntb->db_vec_shift;
	mask = (1ull << shift) - 1;
	return (mask << (shift * db_vector));
}

static void
intel_ntb_interrupt(struct ntb_softc *ntb, uint32_t vec)
{
	uint64_t vec_mask;

	ntb->last_ts = ticks;
	vec_mask = intel_ntb_vec_mask(ntb, vec);

	if ((vec_mask & ntb->db_link_mask) != 0) {
		if (intel_ntb_poll_link(ntb))
			ntb_link_event(ntb->device);
	}

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP) &&
	    (vec_mask & ntb->db_link_mask) == 0) {
		DB_MASK_LOCK(ntb);

		/*
		 * Do not report same DB events again if not cleared yet,
		 * unless the mask was just cleared for them and this
		 * interrupt handler call can be the consequence of it.
		 */
		vec_mask &= ~ntb->fake_db | ntb->force_db;
		ntb->force_db &= ~vec_mask;

		/* Update our internal doorbell register. */
		ntb->fake_db |= vec_mask;

		/* Do not report masked DB events. */
		vec_mask &= ~ntb->db_mask;

		DB_MASK_UNLOCK(ntb);
	}

	if ((vec_mask & ntb->db_valid_mask) != 0)
		ntb_db_event(ntb->device, vec);
}

static void
ndev_vec_isr(void *arg)
{
	struct ntb_vec *nvec = arg;

	intel_ntb_interrupt(nvec->ntb, nvec->num);
}

static void
ndev_irq_isr(void *arg)
{
	/* If we couldn't set up MSI-X, we only have the one vector. */
	intel_ntb_interrupt(arg, 0);
}

static int
intel_ntb_create_msix_vec(struct ntb_softc *ntb, uint32_t num_vectors)
{
	uint32_t i;

	ntb->msix_vec = malloc(num_vectors * sizeof(*ntb->msix_vec), M_NTB,
	    M_ZERO | M_WAITOK);
	for (i = 0; i < num_vectors; i++) {
		ntb->msix_vec[i].num = i;
		ntb->msix_vec[i].ntb = ntb;
	}

	return (0);
}

static void
intel_ntb_free_msix_vec(struct ntb_softc *ntb)
{

	if (ntb->msix_vec == NULL)
		return;

	free(ntb->msix_vec, M_NTB);
	ntb->msix_vec = NULL;
}

static void
intel_ntb_get_msix_info(struct ntb_softc *ntb)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_msix *msix;
	uint32_t laddr, data, i, offset;

	dinfo = device_get_ivars(ntb->device);
	msix = &dinfo->cfg.msix;

	CTASSERT(XEON_NONLINK_DB_MSIX_BITS == nitems(ntb->msix_data));

	for (i = 0; i < XEON_NONLINK_DB_MSIX_BITS; i++) {
		offset = msix->msix_table_offset + i * PCI_MSIX_ENTRY_SIZE;

		laddr = bus_read_4(msix->msix_table_res, offset +
		    PCI_MSIX_ENTRY_LOWER_ADDR);
		intel_ntb_printf(2, "local MSIX addr(%u): 0x%x\n", i, laddr);

		KASSERT((laddr & MSI_INTEL_ADDR_BASE) == MSI_INTEL_ADDR_BASE,
		    ("local MSIX addr 0x%x not in MSI base 0x%x", laddr,
		     MSI_INTEL_ADDR_BASE));
		ntb->msix_data[i].nmd_ofs = laddr;

		data = bus_read_4(msix->msix_table_res, offset +
		    PCI_MSIX_ENTRY_DATA);
		intel_ntb_printf(2, "local MSIX data(%u): 0x%x\n", i, data);

		ntb->msix_data[i].nmd_data = data;
	}
}

static struct ntb_hw_info *
intel_ntb_get_device_info(uint32_t device_id)
{
	struct ntb_hw_info *ep;

	for (ep = pci_ids; ep < &pci_ids[nitems(pci_ids)]; ep++) {
		if (ep->device_id == device_id)
			return (ep);
	}
	return (NULL);
}

static void
intel_ntb_teardown_xeon(struct ntb_softc *ntb)
{

	if (ntb->reg != NULL)
		intel_ntb_link_disable(ntb->device);
}

static void
intel_ntb_detect_max_mw(struct ntb_softc *ntb)
{

	if (ntb->type == NTB_ATOM) {
		ntb->mw_count = ATOM_MW_COUNT;
		return;
	}

	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		ntb->mw_count = XEON_HSX_SPLIT_MW_COUNT;
	else
		ntb->mw_count = XEON_SNB_MW_COUNT;
}

static int
intel_ntb_detect_xeon(struct ntb_softc *ntb)
{
	uint8_t ppd, conn_type;

	ppd = pci_read_config(ntb->device, NTB_PPD_OFFSET, 1);
	ntb->ppd = ppd;

	if ((ppd & XEON_PPD_DEV_TYPE) != 0)
		ntb->dev_type = NTB_DEV_DSD;
	else
		ntb->dev_type = NTB_DEV_USD;

	if ((ppd & XEON_PPD_SPLIT_BAR) != 0)
		ntb->features |= NTB_SPLIT_BAR;

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP) &&
	    !HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		device_printf(ntb->device,
		    "Can not apply SB01BASE_LOCKUP workaround "
		    "with split BARs disabled!\n");
		device_printf(ntb->device,
		    "Expect system hangs under heavy NTB traffic!\n");
		ntb->features &= ~NTB_SB01BASE_LOCKUP;
	}

	/*
	 * SDOORBELL errata workaround gets in the way of SB01BASE_LOCKUP
	 * errata workaround; only do one at a time.
	 */
	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP))
		ntb->features &= ~NTB_SDOORBELL_LOCKUP;

	conn_type = ppd & XEON_PPD_CONN_TYPE;
	switch (conn_type) {
	case NTB_CONN_B2B:
		ntb->conn_type = conn_type;
		break;
	case NTB_CONN_RP:
	case NTB_CONN_TRANSPARENT:
	default:
		device_printf(ntb->device, "Unsupported connection type: %u\n",
		    (unsigned)conn_type);
		return (ENXIO);
	}
	return (0);
}

static int
intel_ntb_detect_atom(struct ntb_softc *ntb)
{
	uint32_t ppd, conn_type;

	ppd = pci_read_config(ntb->device, NTB_PPD_OFFSET, 4);
	ntb->ppd = ppd;

	if ((ppd & ATOM_PPD_DEV_TYPE) != 0)
		ntb->dev_type = NTB_DEV_DSD;
	else
		ntb->dev_type = NTB_DEV_USD;

	conn_type = (ppd & ATOM_PPD_CONN_TYPE) >> 8;
	switch (conn_type) {
	case NTB_CONN_B2B:
		ntb->conn_type = conn_type;
		break;
	default:
		device_printf(ntb->device, "Unsupported NTB configuration\n");
		return (ENXIO);
	}
	return (0);
}

static int
intel_ntb_xeon_init_dev(struct ntb_softc *ntb)
{
	int rc;

	ntb->spad_count		= XEON_SPAD_COUNT;
	ntb->db_count		= XEON_DB_COUNT;
	ntb->db_link_mask	= XEON_DB_LINK_BIT;
	ntb->db_vec_count	= XEON_DB_MSIX_VECTOR_COUNT;
	ntb->db_vec_shift	= XEON_DB_MSIX_VECTOR_SHIFT;

	if (ntb->conn_type != NTB_CONN_B2B) {
		device_printf(ntb->device, "Connection type %d not supported\n",
		    ntb->conn_type);
		return (ENXIO);
	}

	ntb->reg = &xeon_reg;
	ntb->self_reg = &xeon_pri_reg;
	ntb->peer_reg = &xeon_b2b_reg;
	ntb->xlat_reg = &xeon_sec_xlat;

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		ntb->force_db = ntb->fake_db = 0;
		ntb->msix_mw_idx = (ntb->mw_count + g_ntb_msix_idx) %
		    ntb->mw_count;
		intel_ntb_printf(2, "Setting up MSIX mw idx %d means %u\n",
		    g_ntb_msix_idx, ntb->msix_mw_idx);
		rc = intel_ntb_mw_set_wc_internal(ntb, ntb->msix_mw_idx,
		    VM_MEMATTR_UNCACHEABLE);
		KASSERT(rc == 0, ("shouldn't fail"));
	} else if (HAS_FEATURE(ntb, NTB_SDOORBELL_LOCKUP)) {
		/*
		 * There is a Xeon hardware errata related to writes to SDOORBELL or
		 * B2BDOORBELL in conjunction with inbound access to NTB MMIO space,
		 * which may hang the system.  To workaround this, use a memory
		 * window to access the interrupt and scratch pad registers on the
		 * remote system.
		 */
		ntb->b2b_mw_idx = (ntb->mw_count + g_ntb_mw_idx) %
		    ntb->mw_count;
		intel_ntb_printf(2, "Setting up b2b mw idx %d means %u\n",
		    g_ntb_mw_idx, ntb->b2b_mw_idx);
		rc = intel_ntb_mw_set_wc_internal(ntb, ntb->b2b_mw_idx,
		    VM_MEMATTR_UNCACHEABLE);
		KASSERT(rc == 0, ("shouldn't fail"));
	} else if (HAS_FEATURE(ntb, NTB_B2BDOORBELL_BIT14))
		/*
		 * HW Errata on bit 14 of b2bdoorbell register.  Writes will not be
		 * mirrored to the remote system.  Shrink the number of bits by one,
		 * since bit 14 is the last bit.
		 *
		 * On REGS_THRU_MW errata mode, we don't use the b2bdoorbell register
		 * anyway.  Nor for non-B2B connection types.
		 */
		ntb->db_count = XEON_DB_COUNT - 1;

	ntb->db_valid_mask = (1ull << ntb->db_count) - 1;

	if (ntb->dev_type == NTB_DEV_USD)
		rc = xeon_setup_b2b_mw(ntb, &xeon_b2b_dsd_addr,
		    &xeon_b2b_usd_addr);
	else
		rc = xeon_setup_b2b_mw(ntb, &xeon_b2b_usd_addr,
		    &xeon_b2b_dsd_addr);
	if (rc != 0)
		return (rc);

	/* Enable Bus Master and Memory Space on the secondary side */
	intel_ntb_reg_write(2, XEON_SPCICMD_OFFSET,
	    PCIM_CMD_MEMEN | PCIM_CMD_BUSMASTEREN);

	/*
	 * Mask all doorbell interrupts.
	 */
	DB_MASK_LOCK(ntb);
	ntb->db_mask = ntb->db_valid_mask;
	db_iowrite(ntb, ntb->self_reg->db_mask, ntb->db_mask);
	DB_MASK_UNLOCK(ntb);

	rc = intel_ntb_init_isr(ntb);
	return (rc);
}

static int
intel_ntb_atom_init_dev(struct ntb_softc *ntb)
{
	int error;

	KASSERT(ntb->conn_type == NTB_CONN_B2B,
	    ("Unsupported NTB configuration (%d)\n", ntb->conn_type));

	ntb->spad_count		 = ATOM_SPAD_COUNT;
	ntb->db_count		 = ATOM_DB_COUNT;
	ntb->db_vec_count	 = ATOM_DB_MSIX_VECTOR_COUNT;
	ntb->db_vec_shift	 = ATOM_DB_MSIX_VECTOR_SHIFT;
	ntb->db_valid_mask	 = (1ull << ntb->db_count) - 1;

	ntb->reg = &atom_reg;
	ntb->self_reg = &atom_pri_reg;
	ntb->peer_reg = &atom_b2b_reg;
	ntb->xlat_reg = &atom_sec_xlat;

	/*
	 * FIXME - MSI-X bug on early Atom HW, remove once internal issue is
	 * resolved.  Mask transaction layer internal parity errors.
	 */
	pci_write_config(ntb->device, 0xFC, 0x4, 4);

	configure_atom_secondary_side_bars(ntb);

	/* Enable Bus Master and Memory Space on the secondary side */
	intel_ntb_reg_write(2, ATOM_SPCICMD_OFFSET,
	    PCIM_CMD_MEMEN | PCIM_CMD_BUSMASTEREN);

	error = intel_ntb_init_isr(ntb);
	if (error != 0)
		return (error);

	/* Initiate PCI-E link training */
	intel_ntb_link_enable(ntb->device, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);

	callout_reset(&ntb->heartbeat_timer, 0, atom_link_hb, ntb);

	return (0);
}

/* XXX: Linux driver doesn't seem to do any of this for Atom. */
static void
configure_atom_secondary_side_bars(struct ntb_softc *ntb)
{

	if (ntb->dev_type == NTB_DEV_USD) {
		intel_ntb_reg_write(8, ATOM_PBAR2XLAT_OFFSET,
		    XEON_B2B_BAR2_ADDR64);
		intel_ntb_reg_write(8, ATOM_PBAR4XLAT_OFFSET,
		    XEON_B2B_BAR4_ADDR64);
		intel_ntb_reg_write(8, ATOM_MBAR23_OFFSET, XEON_B2B_BAR2_ADDR64);
		intel_ntb_reg_write(8, ATOM_MBAR45_OFFSET, XEON_B2B_BAR4_ADDR64);
	} else {
		intel_ntb_reg_write(8, ATOM_PBAR2XLAT_OFFSET,
		    XEON_B2B_BAR2_ADDR64);
		intel_ntb_reg_write(8, ATOM_PBAR4XLAT_OFFSET,
		    XEON_B2B_BAR4_ADDR64);
		intel_ntb_reg_write(8, ATOM_MBAR23_OFFSET, XEON_B2B_BAR2_ADDR64);
		intel_ntb_reg_write(8, ATOM_MBAR45_OFFSET, XEON_B2B_BAR4_ADDR64);
	}
}


/*
 * When working around Xeon SDOORBELL errata by remapping remote registers in a
 * MW, limit the B2B MW to half a MW.  By sharing a MW, half the shared MW
 * remains for use by a higher layer.
 *
 * Will only be used if working around SDOORBELL errata and the BIOS-configured
 * MW size is sufficiently large.
 */
static unsigned int ntb_b2b_mw_share;
SYSCTL_UINT(_hw_ntb, OID_AUTO, b2b_mw_share, CTLFLAG_RDTUN, &ntb_b2b_mw_share,
    0, "If enabled (non-zero), prefer to share half of the B2B peer register "
    "MW with higher level consumers.  Both sides of the NTB MUST set the same "
    "value here.");

static void
xeon_reset_sbar_size(struct ntb_softc *ntb, enum ntb_bar idx,
    enum ntb_bar regbar)
{
	struct ntb_pci_bar_info *bar;
	uint8_t bar_sz;

	if (!HAS_FEATURE(ntb, NTB_SPLIT_BAR) && idx >= NTB_B2B_BAR_3)
		return;

	bar = &ntb->bar_info[idx];
	bar_sz = pci_read_config(ntb->device, bar->psz_off, 1);
	if (idx == regbar) {
		if (ntb->b2b_off != 0)
			bar_sz--;
		else
			bar_sz = 0;
	}
	pci_write_config(ntb->device, bar->ssz_off, bar_sz, 1);
	bar_sz = pci_read_config(ntb->device, bar->ssz_off, 1);
	(void)bar_sz;
}

static void
xeon_set_sbar_base_and_limit(struct ntb_softc *ntb, uint64_t bar_addr,
    enum ntb_bar idx, enum ntb_bar regbar)
{
	uint64_t reg_val;
	uint32_t base_reg, lmt_reg;

	bar_get_xlat_params(ntb, idx, &base_reg, NULL, &lmt_reg);
	if (idx == regbar) {
		if (ntb->b2b_off)
			bar_addr += ntb->b2b_off;
		else
			bar_addr = 0;
	}

	if (!bar_is_64bit(ntb, idx)) {
		intel_ntb_reg_write(4, base_reg, bar_addr);
		reg_val = intel_ntb_reg_read(4, base_reg);
		(void)reg_val;

		intel_ntb_reg_write(4, lmt_reg, bar_addr);
		reg_val = intel_ntb_reg_read(4, lmt_reg);
		(void)reg_val;
	} else {
		intel_ntb_reg_write(8, base_reg, bar_addr);
		reg_val = intel_ntb_reg_read(8, base_reg);
		(void)reg_val;

		intel_ntb_reg_write(8, lmt_reg, bar_addr);
		reg_val = intel_ntb_reg_read(8, lmt_reg);
		(void)reg_val;
	}
}

static void
xeon_set_pbar_xlat(struct ntb_softc *ntb, uint64_t base_addr, enum ntb_bar idx)
{
	struct ntb_pci_bar_info *bar;

	bar = &ntb->bar_info[idx];
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR) && idx >= NTB_B2B_BAR_2) {
		intel_ntb_reg_write(4, bar->pbarxlat_off, base_addr);
		base_addr = intel_ntb_reg_read(4, bar->pbarxlat_off);
	} else {
		intel_ntb_reg_write(8, bar->pbarxlat_off, base_addr);
		base_addr = intel_ntb_reg_read(8, bar->pbarxlat_off);
	}
	(void)base_addr;
}

static int
xeon_setup_b2b_mw(struct ntb_softc *ntb, const struct ntb_b2b_addr *addr,
    const struct ntb_b2b_addr *peer_addr)
{
	struct ntb_pci_bar_info *b2b_bar;
	vm_size_t bar_size;
	uint64_t bar_addr;
	enum ntb_bar b2b_bar_num, i;

	if (ntb->b2b_mw_idx == B2B_MW_DISABLED) {
		b2b_bar = NULL;
		b2b_bar_num = NTB_CONFIG_BAR;
		ntb->b2b_off = 0;
	} else {
		b2b_bar_num = intel_ntb_mw_to_bar(ntb, ntb->b2b_mw_idx);
		KASSERT(b2b_bar_num > 0 && b2b_bar_num < NTB_MAX_BARS,
		    ("invalid b2b mw bar"));

		b2b_bar = &ntb->bar_info[b2b_bar_num];
		bar_size = b2b_bar->size;

		if (ntb_b2b_mw_share != 0 &&
		    (bar_size >> 1) >= XEON_B2B_MIN_SIZE)
			ntb->b2b_off = bar_size >> 1;
		else if (bar_size >= XEON_B2B_MIN_SIZE) {
			ntb->b2b_off = 0;
		} else {
			device_printf(ntb->device,
			    "B2B bar size is too small!\n");
			return (EIO);
		}
	}

	/*
	 * Reset the secondary bar sizes to match the primary bar sizes.
	 * (Except, disable or halve the size of the B2B secondary bar.)
	 */
	for (i = NTB_B2B_BAR_1; i < NTB_MAX_BARS; i++)
		xeon_reset_sbar_size(ntb, i, b2b_bar_num);

	bar_addr = 0;
	if (b2b_bar_num == NTB_CONFIG_BAR)
		bar_addr = addr->bar0_addr;
	else if (b2b_bar_num == NTB_B2B_BAR_1)
		bar_addr = addr->bar2_addr64;
	else if (b2b_bar_num == NTB_B2B_BAR_2 && !HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		bar_addr = addr->bar4_addr64;
	else if (b2b_bar_num == NTB_B2B_BAR_2)
		bar_addr = addr->bar4_addr32;
	else if (b2b_bar_num == NTB_B2B_BAR_3)
		bar_addr = addr->bar5_addr32;
	else
		KASSERT(false, ("invalid bar"));

	intel_ntb_reg_write(8, XEON_SBAR0BASE_OFFSET, bar_addr);

	/*
	 * Other SBARs are normally hit by the PBAR xlat, except for the b2b
	 * register BAR.  The B2B BAR is either disabled above or configured
	 * half-size.  It starts at PBAR xlat + offset.
	 *
	 * Also set up incoming BAR limits == base (zero length window).
	 */
	xeon_set_sbar_base_and_limit(ntb, addr->bar2_addr64, NTB_B2B_BAR_1,
	    b2b_bar_num);
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		xeon_set_sbar_base_and_limit(ntb, addr->bar4_addr32,
		    NTB_B2B_BAR_2, b2b_bar_num);
		xeon_set_sbar_base_and_limit(ntb, addr->bar5_addr32,
		    NTB_B2B_BAR_3, b2b_bar_num);
	} else
		xeon_set_sbar_base_and_limit(ntb, addr->bar4_addr64,
		    NTB_B2B_BAR_2, b2b_bar_num);

	/* Zero incoming translation addrs */
	intel_ntb_reg_write(8, XEON_SBAR2XLAT_OFFSET, 0);
	intel_ntb_reg_write(8, XEON_SBAR4XLAT_OFFSET, 0);

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		uint32_t xlat_reg, lmt_reg;
		enum ntb_bar bar_num;

		/*
		 * We point the chosen MSIX MW BAR xlat to remote LAPIC for
		 * workaround
		 */
		bar_num = intel_ntb_mw_to_bar(ntb, ntb->msix_mw_idx);
		bar_get_xlat_params(ntb, bar_num, NULL, &xlat_reg, &lmt_reg);
		if (bar_is_64bit(ntb, bar_num)) {
			intel_ntb_reg_write(8, xlat_reg, MSI_INTEL_ADDR_BASE);
			ntb->msix_xlat = intel_ntb_reg_read(8, xlat_reg);
			intel_ntb_reg_write(8, lmt_reg, 0);
		} else {
			intel_ntb_reg_write(4, xlat_reg, MSI_INTEL_ADDR_BASE);
			ntb->msix_xlat = intel_ntb_reg_read(4, xlat_reg);
			intel_ntb_reg_write(4, lmt_reg, 0);
		}

		ntb->peer_lapic_bar =  &ntb->bar_info[bar_num];
	}
	(void)intel_ntb_reg_read(8, XEON_SBAR2XLAT_OFFSET);
	(void)intel_ntb_reg_read(8, XEON_SBAR4XLAT_OFFSET);

	/* Zero outgoing translation limits (whole bar size windows) */
	intel_ntb_reg_write(8, XEON_PBAR2LMT_OFFSET, 0);
	intel_ntb_reg_write(8, XEON_PBAR4LMT_OFFSET, 0);

	/* Set outgoing translation offsets */
	xeon_set_pbar_xlat(ntb, peer_addr->bar2_addr64, NTB_B2B_BAR_1);
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		xeon_set_pbar_xlat(ntb, peer_addr->bar4_addr32, NTB_B2B_BAR_2);
		xeon_set_pbar_xlat(ntb, peer_addr->bar5_addr32, NTB_B2B_BAR_3);
	} else
		xeon_set_pbar_xlat(ntb, peer_addr->bar4_addr64, NTB_B2B_BAR_2);

	/* Set the translation offset for B2B registers */
	bar_addr = 0;
	if (b2b_bar_num == NTB_CONFIG_BAR)
		bar_addr = peer_addr->bar0_addr;
	else if (b2b_bar_num == NTB_B2B_BAR_1)
		bar_addr = peer_addr->bar2_addr64;
	else if (b2b_bar_num == NTB_B2B_BAR_2 && !HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		bar_addr = peer_addr->bar4_addr64;
	else if (b2b_bar_num == NTB_B2B_BAR_2)
		bar_addr = peer_addr->bar4_addr32;
	else if (b2b_bar_num == NTB_B2B_BAR_3)
		bar_addr = peer_addr->bar5_addr32;
	else
		KASSERT(false, ("invalid bar"));

	/*
	 * B2B_XLAT_OFFSET is a 64-bit register but can only be written 32 bits
	 * at a time.
	 */
	intel_ntb_reg_write(4, XEON_B2B_XLAT_OFFSETL, bar_addr & 0xffffffff);
	intel_ntb_reg_write(4, XEON_B2B_XLAT_OFFSETU, bar_addr >> 32);
	return (0);
}

static inline bool
_xeon_link_is_up(struct ntb_softc *ntb)
{

	if (ntb->conn_type == NTB_CONN_TRANSPARENT)
		return (true);
	return ((ntb->lnk_sta & NTB_LINK_STATUS_ACTIVE) != 0);
}

static inline bool
link_is_up(struct ntb_softc *ntb)
{

	if (ntb->type == NTB_XEON)
		return (_xeon_link_is_up(ntb) && (ntb->peer_msix_good ||
		    !HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)));

	KASSERT(ntb->type == NTB_ATOM, ("ntb type"));
	return ((ntb->ntb_ctl & ATOM_CNTL_LINK_DOWN) == 0);
}

static inline bool
atom_link_is_err(struct ntb_softc *ntb)
{
	uint32_t status;

	KASSERT(ntb->type == NTB_ATOM, ("ntb type"));

	status = intel_ntb_reg_read(4, ATOM_LTSSMSTATEJMP_OFFSET);
	if ((status & ATOM_LTSSMSTATEJMP_FORCEDETECT) != 0)
		return (true);

	status = intel_ntb_reg_read(4, ATOM_IBSTERRRCRVSTS0_OFFSET);
	return ((status & ATOM_IBIST_ERR_OFLOW) != 0);
}

/* Atom does not have link status interrupt, poll on that platform */
static void
atom_link_hb(void *arg)
{
	struct ntb_softc *ntb = arg;
	sbintime_t timo, poll_ts;

	timo = NTB_HB_TIMEOUT * hz;
	poll_ts = ntb->last_ts + timo;

	/*
	 * Delay polling the link status if an interrupt was received, unless
	 * the cached link status says the link is down.
	 */
	if ((sbintime_t)ticks - poll_ts < 0 && link_is_up(ntb)) {
		timo = poll_ts - ticks;
		goto out;
	}

	if (intel_ntb_poll_link(ntb))
		ntb_link_event(ntb->device);

	if (!link_is_up(ntb) && atom_link_is_err(ntb)) {
		/* Link is down with error, proceed with recovery */
		callout_reset(&ntb->lr_timer, 0, recover_atom_link, ntb);
		return;
	}

out:
	callout_reset(&ntb->heartbeat_timer, timo, atom_link_hb, ntb);
}

static void
atom_perform_link_restart(struct ntb_softc *ntb)
{
	uint32_t status;

	/* Driver resets the NTB ModPhy lanes - magic! */
	intel_ntb_reg_write(1, ATOM_MODPHY_PCSREG6, 0xe0);
	intel_ntb_reg_write(1, ATOM_MODPHY_PCSREG4, 0x40);
	intel_ntb_reg_write(1, ATOM_MODPHY_PCSREG4, 0x60);
	intel_ntb_reg_write(1, ATOM_MODPHY_PCSREG6, 0x60);

	/* Driver waits 100ms to allow the NTB ModPhy to settle */
	pause("ModPhy", hz / 10);

	/* Clear AER Errors, write to clear */
	status = intel_ntb_reg_read(4, ATOM_ERRCORSTS_OFFSET);
	status &= PCIM_AER_COR_REPLAY_ROLLOVER;
	intel_ntb_reg_write(4, ATOM_ERRCORSTS_OFFSET, status);

	/* Clear unexpected electrical idle event in LTSSM, write to clear */
	status = intel_ntb_reg_read(4, ATOM_LTSSMERRSTS0_OFFSET);
	status |= ATOM_LTSSMERRSTS0_UNEXPECTEDEI;
	intel_ntb_reg_write(4, ATOM_LTSSMERRSTS0_OFFSET, status);

	/* Clear DeSkew Buffer error, write to clear */
	status = intel_ntb_reg_read(4, ATOM_DESKEWSTS_OFFSET);
	status |= ATOM_DESKEWSTS_DBERR;
	intel_ntb_reg_write(4, ATOM_DESKEWSTS_OFFSET, status);

	status = intel_ntb_reg_read(4, ATOM_IBSTERRRCRVSTS0_OFFSET);
	status &= ATOM_IBIST_ERR_OFLOW;
	intel_ntb_reg_write(4, ATOM_IBSTERRRCRVSTS0_OFFSET, status);

	/* Releases the NTB state machine to allow the link to retrain */
	status = intel_ntb_reg_read(4, ATOM_LTSSMSTATEJMP_OFFSET);
	status &= ~ATOM_LTSSMSTATEJMP_FORCEDETECT;
	intel_ntb_reg_write(4, ATOM_LTSSMSTATEJMP_OFFSET, status);
}

static int
intel_ntb_link_enable(device_t dev, enum ntb_speed speed __unused,
    enum ntb_width width __unused)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	uint32_t cntl;

	intel_ntb_printf(2, "%s\n", __func__);

	if (ntb->type == NTB_ATOM) {
		pci_write_config(ntb->device, NTB_PPD_OFFSET,
		    ntb->ppd | ATOM_PPD_INIT_LINK, 4);
		return (0);
	}

	if (ntb->conn_type == NTB_CONN_TRANSPARENT) {
		ntb_link_event(dev);
		return (0);
	}

	cntl = intel_ntb_reg_read(4, ntb->reg->ntb_ctl);
	cntl &= ~(NTB_CNTL_LINK_DISABLE | NTB_CNTL_CFG_LOCK);
	cntl |= NTB_CNTL_P2S_BAR23_SNOOP | NTB_CNTL_S2P_BAR23_SNOOP;
	cntl |= NTB_CNTL_P2S_BAR4_SNOOP | NTB_CNTL_S2P_BAR4_SNOOP;
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		cntl |= NTB_CNTL_P2S_BAR5_SNOOP | NTB_CNTL_S2P_BAR5_SNOOP;
	intel_ntb_reg_write(4, ntb->reg->ntb_ctl, cntl);
	return (0);
}

static int
intel_ntb_link_disable(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	uint32_t cntl;

	intel_ntb_printf(2, "%s\n", __func__);

	if (ntb->conn_type == NTB_CONN_TRANSPARENT) {
		ntb_link_event(dev);
		return (0);
	}

	cntl = intel_ntb_reg_read(4, ntb->reg->ntb_ctl);
	cntl &= ~(NTB_CNTL_P2S_BAR23_SNOOP | NTB_CNTL_S2P_BAR23_SNOOP);
	cntl &= ~(NTB_CNTL_P2S_BAR4_SNOOP | NTB_CNTL_S2P_BAR4_SNOOP);
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR))
		cntl &= ~(NTB_CNTL_P2S_BAR5_SNOOP | NTB_CNTL_S2P_BAR5_SNOOP);
	cntl |= NTB_CNTL_LINK_DISABLE | NTB_CNTL_CFG_LOCK;
	intel_ntb_reg_write(4, ntb->reg->ntb_ctl, cntl);
	return (0);
}

static bool
intel_ntb_link_enabled(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	uint32_t cntl;

	if (ntb->type == NTB_ATOM) {
		cntl = pci_read_config(ntb->device, NTB_PPD_OFFSET, 4);
		return ((cntl & ATOM_PPD_INIT_LINK) != 0);
	}

	if (ntb->conn_type == NTB_CONN_TRANSPARENT)
		return (true);

	cntl = intel_ntb_reg_read(4, ntb->reg->ntb_ctl);
	return ((cntl & NTB_CNTL_LINK_DISABLE) == 0);
}

static void
recover_atom_link(void *arg)
{
	struct ntb_softc *ntb = arg;
	unsigned speed, width, oldspeed, oldwidth;
	uint32_t status32;

	atom_perform_link_restart(ntb);

	/*
	 * There is a potential race between the 2 NTB devices recovering at
	 * the same time.  If the times are the same, the link will not recover
	 * and the driver will be stuck in this loop forever.  Add a random
	 * interval to the recovery time to prevent this race.
	 */
	status32 = arc4random() % ATOM_LINK_RECOVERY_TIME;
	pause("Link", (ATOM_LINK_RECOVERY_TIME + status32) * hz / 1000);

	if (atom_link_is_err(ntb))
		goto retry;

	status32 = intel_ntb_reg_read(4, ntb->reg->ntb_ctl);
	if ((status32 & ATOM_CNTL_LINK_DOWN) != 0)
		goto out;

	status32 = intel_ntb_reg_read(4, ntb->reg->lnk_sta);
	width = NTB_LNK_STA_WIDTH(status32);
	speed = status32 & NTB_LINK_SPEED_MASK;

	oldwidth = NTB_LNK_STA_WIDTH(ntb->lnk_sta);
	oldspeed = ntb->lnk_sta & NTB_LINK_SPEED_MASK;
	if (oldwidth != width || oldspeed != speed)
		goto retry;

out:
	callout_reset(&ntb->heartbeat_timer, NTB_HB_TIMEOUT * hz, atom_link_hb,
	    ntb);
	return;

retry:
	callout_reset(&ntb->lr_timer, NTB_HB_TIMEOUT * hz, recover_atom_link,
	    ntb);
}

/*
 * Polls the HW link status register(s); returns true if something has changed.
 */
static bool
intel_ntb_poll_link(struct ntb_softc *ntb)
{
	uint32_t ntb_cntl;
	uint16_t reg_val;

	if (ntb->type == NTB_ATOM) {
		ntb_cntl = intel_ntb_reg_read(4, ntb->reg->ntb_ctl);
		if (ntb_cntl == ntb->ntb_ctl)
			return (false);

		ntb->ntb_ctl = ntb_cntl;
		ntb->lnk_sta = intel_ntb_reg_read(4, ntb->reg->lnk_sta);
	} else {
		db_iowrite_raw(ntb, ntb->self_reg->db_bell, ntb->db_link_mask);

		reg_val = pci_read_config(ntb->device, ntb->reg->lnk_sta, 2);
		if (reg_val == ntb->lnk_sta)
			return (false);

		ntb->lnk_sta = reg_val;

		if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
			if (_xeon_link_is_up(ntb)) {
				if (!ntb->peer_msix_good) {
					callout_reset(&ntb->peer_msix_work, 0,
					    intel_ntb_exchange_msix, ntb);
					return (false);
				}
			} else {
				ntb->peer_msix_good = false;
				ntb->peer_msix_done = false;
			}
		}
	}
	return (true);
}

static inline enum ntb_speed
intel_ntb_link_sta_speed(struct ntb_softc *ntb)
{

	if (!link_is_up(ntb))
		return (NTB_SPEED_NONE);
	return (ntb->lnk_sta & NTB_LINK_SPEED_MASK);
}

static inline enum ntb_width
intel_ntb_link_sta_width(struct ntb_softc *ntb)
{

	if (!link_is_up(ntb))
		return (NTB_WIDTH_NONE);
	return (NTB_LNK_STA_WIDTH(ntb->lnk_sta));
}

SYSCTL_NODE(_hw_ntb, OID_AUTO, debug_info, CTLFLAG_RW, 0,
    "Driver state, statistics, and HW registers");

#define NTB_REGSZ_MASK	(3ul << 30)
#define NTB_REG_64	(1ul << 30)
#define NTB_REG_32	(2ul << 30)
#define NTB_REG_16	(3ul << 30)
#define NTB_REG_8	(0ul << 30)

#define NTB_DB_READ	(1ul << 29)
#define NTB_PCI_REG	(1ul << 28)
#define NTB_REGFLAGS_MASK	(NTB_REGSZ_MASK | NTB_DB_READ | NTB_PCI_REG)

static void
intel_ntb_sysctl_init(struct ntb_softc *ntb)
{
	struct sysctl_oid_list *globals, *tree_par, *regpar, *statpar, *errpar;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree, *tmptree;

	ctx = device_get_sysctl_ctx(ntb->device);
	globals = SYSCTL_CHILDREN(device_get_sysctl_tree(ntb->device));

	SYSCTL_ADD_PROC(ctx, globals, OID_AUTO, "link_status",
	    CTLFLAG_RD | CTLTYPE_STRING, ntb, 0,
	    sysctl_handle_link_status_human, "A",
	    "Link status (human readable)");
	SYSCTL_ADD_PROC(ctx, globals, OID_AUTO, "active",
	    CTLFLAG_RD | CTLTYPE_UINT, ntb, 0, sysctl_handle_link_status,
	    "IU", "Link status (1=active, 0=inactive)");
	SYSCTL_ADD_PROC(ctx, globals, OID_AUTO, "admin_up",
	    CTLFLAG_RW | CTLTYPE_UINT, ntb, 0, sysctl_handle_link_admin,
	    "IU", "Set/get interface status (1=UP, 0=DOWN)");

	tree = SYSCTL_ADD_NODE(ctx, globals, OID_AUTO, "debug_info",
	    CTLFLAG_RD, NULL, "Driver state, statistics, and HW registers");
	tree_par = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_UINT(ctx, tree_par, OID_AUTO, "conn_type", CTLFLAG_RD,
	    &ntb->conn_type, 0, "0 - Transparent; 1 - B2B; 2 - Root Port");
	SYSCTL_ADD_UINT(ctx, tree_par, OID_AUTO, "dev_type", CTLFLAG_RD,
	    &ntb->dev_type, 0, "0 - USD; 1 - DSD");
	SYSCTL_ADD_UINT(ctx, tree_par, OID_AUTO, "ppd", CTLFLAG_RD,
	    &ntb->ppd, 0, "Raw PPD register (cached)");

	if (ntb->b2b_mw_idx != B2B_MW_DISABLED) {
		SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "b2b_idx", CTLFLAG_RD,
		    &ntb->b2b_mw_idx, 0,
		    "Index of the MW used for B2B remote register access");
		SYSCTL_ADD_UQUAD(ctx, tree_par, OID_AUTO, "b2b_off",
		    CTLFLAG_RD, &ntb->b2b_off,
		    "If non-zero, offset of B2B register region in shared MW");
	}

	SYSCTL_ADD_PROC(ctx, tree_par, OID_AUTO, "features",
	    CTLFLAG_RD | CTLTYPE_STRING, ntb, 0, sysctl_handle_features, "A",
	    "Features/errata of this NTB device");

	SYSCTL_ADD_UINT(ctx, tree_par, OID_AUTO, "ntb_ctl", CTLFLAG_RD,
	    __DEVOLATILE(uint32_t *, &ntb->ntb_ctl), 0,
	    "NTB CTL register (cached)");
	SYSCTL_ADD_UINT(ctx, tree_par, OID_AUTO, "lnk_sta", CTLFLAG_RD,
	    __DEVOLATILE(uint32_t *, &ntb->lnk_sta), 0,
	    "LNK STA register (cached)");

	SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "mw_count", CTLFLAG_RD,
	    &ntb->mw_count, 0, "MW count");
	SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "spad_count", CTLFLAG_RD,
	    &ntb->spad_count, 0, "Scratchpad count");
	SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "db_count", CTLFLAG_RD,
	    &ntb->db_count, 0, "Doorbell count");
	SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "db_vec_count", CTLFLAG_RD,
	    &ntb->db_vec_count, 0, "Doorbell vector count");
	SYSCTL_ADD_U8(ctx, tree_par, OID_AUTO, "db_vec_shift", CTLFLAG_RD,
	    &ntb->db_vec_shift, 0, "Doorbell vector shift");

	SYSCTL_ADD_UQUAD(ctx, tree_par, OID_AUTO, "db_valid_mask", CTLFLAG_RD,
	    &ntb->db_valid_mask, "Doorbell valid mask");
	SYSCTL_ADD_UQUAD(ctx, tree_par, OID_AUTO, "db_link_mask", CTLFLAG_RD,
	    &ntb->db_link_mask, "Doorbell link mask");
	SYSCTL_ADD_UQUAD(ctx, tree_par, OID_AUTO, "db_mask", CTLFLAG_RD,
	    &ntb->db_mask, "Doorbell mask (cached)");

	tmptree = SYSCTL_ADD_NODE(ctx, tree_par, OID_AUTO, "registers",
	    CTLFLAG_RD, NULL, "Raw HW registers (big-endian)");
	regpar = SYSCTL_CHILDREN(tmptree);

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "ntbcntl",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb, NTB_REG_32 |
	    ntb->reg->ntb_ctl, sysctl_handle_register, "IU",
	    "NTB Control register");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "lnkcap",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb, NTB_REG_32 |
	    0x19c, sysctl_handle_register, "IU",
	    "NTB Link Capabilities");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "lnkcon",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb, NTB_REG_32 |
	    0x1a0, sysctl_handle_register, "IU",
	    "NTB Link Control register");

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "db_mask",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | NTB_DB_READ | ntb->self_reg->db_mask,
	    sysctl_handle_register, "QU", "Doorbell mask register");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "db_bell",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | NTB_DB_READ | ntb->self_reg->db_bell,
	    sysctl_handle_register, "QU", "Doorbell register");

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_xlat23",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | ntb->xlat_reg->bar2_xlat,
	    sysctl_handle_register, "QU", "Incoming XLAT23 register");
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_xlat4",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar4_xlat,
		    sysctl_handle_register, "IU", "Incoming XLAT4 register");
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_xlat5",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar5_xlat,
		    sysctl_handle_register, "IU", "Incoming XLAT5 register");
	} else {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_xlat45",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_64 | ntb->xlat_reg->bar4_xlat,
		    sysctl_handle_register, "QU", "Incoming XLAT45 register");
	}

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_lmt23",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | ntb->xlat_reg->bar2_limit,
	    sysctl_handle_register, "QU", "Incoming LMT23 register");
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_lmt4",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar4_limit,
		    sysctl_handle_register, "IU", "Incoming LMT4 register");
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_lmt5",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar5_limit,
		    sysctl_handle_register, "IU", "Incoming LMT5 register");
	} else {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "incoming_lmt45",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_64 | ntb->xlat_reg->bar4_limit,
		    sysctl_handle_register, "QU", "Incoming LMT45 register");
	}

	if (ntb->type == NTB_ATOM)
		return;

	tmptree = SYSCTL_ADD_NODE(ctx, regpar, OID_AUTO, "xeon_stats",
	    CTLFLAG_RD, NULL, "Xeon HW statistics");
	statpar = SYSCTL_CHILDREN(tmptree);
	SYSCTL_ADD_PROC(ctx, statpar, OID_AUTO, "upstream_mem_miss",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_16 | XEON_USMEMMISS_OFFSET,
	    sysctl_handle_register, "SU", "Upstream Memory Miss");

	tmptree = SYSCTL_ADD_NODE(ctx, regpar, OID_AUTO, "xeon_hw_err",
	    CTLFLAG_RD, NULL, "Xeon HW errors");
	errpar = SYSCTL_CHILDREN(tmptree);

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "ppd",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | NTB_PPD_OFFSET,
	    sysctl_handle_register, "CU", "PPD");

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "pbar23_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_PBAR23SZ_OFFSET,
	    sysctl_handle_register, "CU", "PBAR23 SZ (log2)");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "pbar4_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_PBAR4SZ_OFFSET,
	    sysctl_handle_register, "CU", "PBAR4 SZ (log2)");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "pbar5_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_PBAR5SZ_OFFSET,
	    sysctl_handle_register, "CU", "PBAR5 SZ (log2)");

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar23_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_SBAR23SZ_OFFSET,
	    sysctl_handle_register, "CU", "SBAR23 SZ (log2)");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar4_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_SBAR4SZ_OFFSET,
	    sysctl_handle_register, "CU", "SBAR4 SZ (log2)");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar5_sz",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_8 | NTB_PCI_REG | XEON_SBAR5SZ_OFFSET,
	    sysctl_handle_register, "CU", "SBAR5 SZ (log2)");

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "devsts",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_16 | NTB_PCI_REG | XEON_DEVSTS_OFFSET,
	    sysctl_handle_register, "SU", "DEVSTS");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "lnksts",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_16 | NTB_PCI_REG | XEON_LINK_STATUS_OFFSET,
	    sysctl_handle_register, "SU", "LNKSTS");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "slnksts",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_16 | NTB_PCI_REG | XEON_SLINK_STATUS_OFFSET,
	    sysctl_handle_register, "SU", "SLNKSTS");

	SYSCTL_ADD_PROC(ctx, errpar, OID_AUTO, "uncerrsts",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_32 | NTB_PCI_REG | XEON_UNCERRSTS_OFFSET,
	    sysctl_handle_register, "IU", "UNCERRSTS");
	SYSCTL_ADD_PROC(ctx, errpar, OID_AUTO, "corerrsts",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_32 | NTB_PCI_REG | XEON_CORERRSTS_OFFSET,
	    sysctl_handle_register, "IU", "CORERRSTS");

	if (ntb->conn_type != NTB_CONN_B2B)
		return;

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_xlat23",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | ntb->bar_info[NTB_B2B_BAR_1].pbarxlat_off,
	    sysctl_handle_register, "QU", "Outgoing XLAT23 register");
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_xlat4",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->bar_info[NTB_B2B_BAR_2].pbarxlat_off,
		    sysctl_handle_register, "IU", "Outgoing XLAT4 register");
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_xlat5",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->bar_info[NTB_B2B_BAR_3].pbarxlat_off,
		    sysctl_handle_register, "IU", "Outgoing XLAT5 register");
	} else {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_xlat45",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_64 | ntb->bar_info[NTB_B2B_BAR_2].pbarxlat_off,
		    sysctl_handle_register, "QU", "Outgoing XLAT45 register");
	}

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_lmt23",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | XEON_PBAR2LMT_OFFSET,
	    sysctl_handle_register, "QU", "Outgoing LMT23 register");
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_lmt4",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | XEON_PBAR4LMT_OFFSET,
		    sysctl_handle_register, "IU", "Outgoing LMT4 register");
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_lmt5",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | XEON_PBAR5LMT_OFFSET,
		    sysctl_handle_register, "IU", "Outgoing LMT5 register");
	} else {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "outgoing_lmt45",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_64 | XEON_PBAR4LMT_OFFSET,
		    sysctl_handle_register, "QU", "Outgoing LMT45 register");
	}

	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar01_base",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | ntb->xlat_reg->bar0_base,
	    sysctl_handle_register, "QU", "Secondary BAR01 base register");
	SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar23_base",
	    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
	    NTB_REG_64 | ntb->xlat_reg->bar2_base,
	    sysctl_handle_register, "QU", "Secondary BAR23 base register");
	if (HAS_FEATURE(ntb, NTB_SPLIT_BAR)) {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar4_base",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar4_base,
		    sysctl_handle_register, "IU",
		    "Secondary BAR4 base register");
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar5_base",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_32 | ntb->xlat_reg->bar5_base,
		    sysctl_handle_register, "IU",
		    "Secondary BAR5 base register");
	} else {
		SYSCTL_ADD_PROC(ctx, regpar, OID_AUTO, "sbar45_base",
		    CTLFLAG_RD | CTLTYPE_OPAQUE, ntb,
		    NTB_REG_64 | ntb->xlat_reg->bar4_base,
		    sysctl_handle_register, "QU",
		    "Secondary BAR45 base register");
	}
}

static int
sysctl_handle_features(SYSCTL_HANDLER_ARGS)
{
	struct ntb_softc *ntb = arg1;
	struct sbuf sb;
	int error;

	sbuf_new_for_sysctl(&sb, NULL, 256, req);

	sbuf_printf(&sb, "%b", ntb->features, NTB_FEATURES_STR);
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);

	if (error || !req->newptr)
		return (error);
	return (EINVAL);
}

static int
sysctl_handle_link_admin(SYSCTL_HANDLER_ARGS)
{
	struct ntb_softc *ntb = arg1;
	unsigned old, new;
	int error;

	old = intel_ntb_link_enabled(ntb->device);

	error = SYSCTL_OUT(req, &old, sizeof(old));
	if (error != 0 || req->newptr == NULL)
		return (error);

	error = SYSCTL_IN(req, &new, sizeof(new));
	if (error != 0)
		return (error);

	intel_ntb_printf(0, "Admin set interface state to '%sabled'\n",
	    (new != 0)? "en" : "dis");

	if (new != 0)
		error = intel_ntb_link_enable(ntb->device, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	else
		error = intel_ntb_link_disable(ntb->device);
	return (error);
}

static int
sysctl_handle_link_status_human(SYSCTL_HANDLER_ARGS)
{
	struct ntb_softc *ntb = arg1;
	struct sbuf sb;
	enum ntb_speed speed;
	enum ntb_width width;
	int error;

	sbuf_new_for_sysctl(&sb, NULL, 32, req);

	if (intel_ntb_link_is_up(ntb->device, &speed, &width))
		sbuf_printf(&sb, "up / PCIe Gen %u / Width x%u",
		    (unsigned)speed, (unsigned)width);
	else
		sbuf_printf(&sb, "down");

	error = sbuf_finish(&sb);
	sbuf_delete(&sb);

	if (error || !req->newptr)
		return (error);
	return (EINVAL);
}

static int
sysctl_handle_link_status(SYSCTL_HANDLER_ARGS)
{
	struct ntb_softc *ntb = arg1;
	unsigned res;
	int error;

	res = intel_ntb_link_is_up(ntb->device, NULL, NULL);

	error = SYSCTL_OUT(req, &res, sizeof(res));
	if (error || !req->newptr)
		return (error);
	return (EINVAL);
}

static int
sysctl_handle_register(SYSCTL_HANDLER_ARGS)
{
	struct ntb_softc *ntb;
	const void *outp;
	uintptr_t sz;
	uint64_t umv;
	char be[sizeof(umv)];
	size_t outsz;
	uint32_t reg;
	bool db, pci;
	int error;

	ntb = arg1;
	reg = arg2 & ~NTB_REGFLAGS_MASK;
	sz = arg2 & NTB_REGSZ_MASK;
	db = (arg2 & NTB_DB_READ) != 0;
	pci = (arg2 & NTB_PCI_REG) != 0;

	KASSERT(!(db && pci), ("bogus"));

	if (db) {
		KASSERT(sz == NTB_REG_64, ("bogus"));
		umv = db_ioread(ntb, reg);
		outsz = sizeof(uint64_t);
	} else {
		switch (sz) {
		case NTB_REG_64:
			if (pci)
				umv = pci_read_config(ntb->device, reg, 8);
			else
				umv = intel_ntb_reg_read(8, reg);
			outsz = sizeof(uint64_t);
			break;
		case NTB_REG_32:
			if (pci)
				umv = pci_read_config(ntb->device, reg, 4);
			else
				umv = intel_ntb_reg_read(4, reg);
			outsz = sizeof(uint32_t);
			break;
		case NTB_REG_16:
			if (pci)
				umv = pci_read_config(ntb->device, reg, 2);
			else
				umv = intel_ntb_reg_read(2, reg);
			outsz = sizeof(uint16_t);
			break;
		case NTB_REG_8:
			if (pci)
				umv = pci_read_config(ntb->device, reg, 1);
			else
				umv = intel_ntb_reg_read(1, reg);
			outsz = sizeof(uint8_t);
			break;
		default:
			panic("bogus");
			break;
		}
	}

	/* Encode bigendian so that sysctl -x is legible. */
	be64enc(be, umv);
	outp = ((char *)be) + sizeof(umv) - outsz;

	error = SYSCTL_OUT(req, outp, outsz);
	if (error || !req->newptr)
		return (error);
	return (EINVAL);
}

static unsigned
intel_ntb_user_mw_to_idx(struct ntb_softc *ntb, unsigned uidx)
{

	if ((ntb->b2b_mw_idx != B2B_MW_DISABLED && ntb->b2b_off == 0 &&
	    uidx >= ntb->b2b_mw_idx) ||
	    (ntb->msix_mw_idx != B2B_MW_DISABLED && uidx >= ntb->msix_mw_idx))
		uidx++;
	if ((ntb->b2b_mw_idx != B2B_MW_DISABLED && ntb->b2b_off == 0 &&
	    uidx >= ntb->b2b_mw_idx) &&
	    (ntb->msix_mw_idx != B2B_MW_DISABLED && uidx >= ntb->msix_mw_idx))
		uidx++;
	return (uidx);
}

#ifndef EARLY_AP_STARTUP
static int msix_ready;

static void
intel_ntb_msix_ready(void *arg __unused)
{

	msix_ready = 1;
}
SYSINIT(intel_ntb_msix_ready, SI_SUB_SMP, SI_ORDER_ANY,
    intel_ntb_msix_ready, NULL);
#endif

static void
intel_ntb_exchange_msix(void *ctx)
{
	struct ntb_softc *ntb;
	uint32_t val;
	unsigned i;

	ntb = ctx;

	if (ntb->peer_msix_good)
		goto msix_good;
	if (ntb->peer_msix_done)
		goto msix_done;

#ifndef EARLY_AP_STARTUP
	/* Block MSIX negotiation until SMP started and IRQ reshuffled. */
	if (!msix_ready)
		goto reschedule;
#endif

	intel_ntb_get_msix_info(ntb);
	for (i = 0; i < XEON_NONLINK_DB_MSIX_BITS; i++) {
		intel_ntb_peer_spad_write(ntb->device, NTB_MSIX_DATA0 + i,
		    ntb->msix_data[i].nmd_data);
		intel_ntb_peer_spad_write(ntb->device, NTB_MSIX_OFS0 + i,
		    ntb->msix_data[i].nmd_ofs - ntb->msix_xlat);
	}
	intel_ntb_peer_spad_write(ntb->device, NTB_MSIX_GUARD, NTB_MSIX_VER_GUARD);

	intel_ntb_spad_read(ntb->device, NTB_MSIX_GUARD, &val);
	if (val != NTB_MSIX_VER_GUARD)
		goto reschedule;

	for (i = 0; i < XEON_NONLINK_DB_MSIX_BITS; i++) {
		intel_ntb_spad_read(ntb->device, NTB_MSIX_DATA0 + i, &val);
		intel_ntb_printf(2, "remote MSIX data(%u): 0x%x\n", i, val);
		ntb->peer_msix_data[i].nmd_data = val;
		intel_ntb_spad_read(ntb->device, NTB_MSIX_OFS0 + i, &val);
		intel_ntb_printf(2, "remote MSIX addr(%u): 0x%x\n", i, val);
		ntb->peer_msix_data[i].nmd_ofs = val;
	}

	ntb->peer_msix_done = true;

msix_done:
	intel_ntb_peer_spad_write(ntb->device, NTB_MSIX_DONE, NTB_MSIX_RECEIVED);
	intel_ntb_spad_read(ntb->device, NTB_MSIX_DONE, &val);
	if (val != NTB_MSIX_RECEIVED)
		goto reschedule;

	intel_ntb_spad_clear(ntb->device);
	ntb->peer_msix_good = true;
	/* Give peer time to see our NTB_MSIX_RECEIVED. */
	goto reschedule;

msix_good:
	intel_ntb_poll_link(ntb);
	ntb_link_event(ntb->device);
	return;

reschedule:
	ntb->lnk_sta = pci_read_config(ntb->device, ntb->reg->lnk_sta, 2);
	if (_xeon_link_is_up(ntb)) {
		callout_reset(&ntb->peer_msix_work,
		    hz * (ntb->peer_msix_good ? 2 : 1) / 10,
		    intel_ntb_exchange_msix, ntb);
	} else
		intel_ntb_spad_clear(ntb->device);
}

/*
 * Public API to the rest of the OS
 */

static uint8_t
intel_ntb_spad_count(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	return (ntb->spad_count);
}

static uint8_t
intel_ntb_mw_count(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	uint8_t res;

	res = ntb->mw_count;
	if (ntb->b2b_mw_idx != B2B_MW_DISABLED && ntb->b2b_off == 0)
		res--;
	if (ntb->msix_mw_idx != B2B_MW_DISABLED)
		res--;
	return (res);
}

static int
intel_ntb_spad_write(device_t dev, unsigned int idx, uint32_t val)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (idx >= ntb->spad_count)
		return (EINVAL);

	intel_ntb_reg_write(4, ntb->self_reg->spad + idx * 4, val);

	return (0);
}

/*
 * Zeros the local scratchpad.
 */
static void
intel_ntb_spad_clear(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	unsigned i;

	for (i = 0; i < ntb->spad_count; i++)
		intel_ntb_spad_write(dev, i, 0);
}

static int
intel_ntb_spad_read(device_t dev, unsigned int idx, uint32_t *val)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (idx >= ntb->spad_count)
		return (EINVAL);

	*val = intel_ntb_reg_read(4, ntb->self_reg->spad + idx * 4);

	return (0);
}

static int
intel_ntb_peer_spad_write(device_t dev, unsigned int idx, uint32_t val)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (idx >= ntb->spad_count)
		return (EINVAL);

	if (HAS_FEATURE(ntb, NTB_SDOORBELL_LOCKUP))
		intel_ntb_mw_write(4, XEON_SPAD_OFFSET + idx * 4, val);
	else
		intel_ntb_reg_write(4, ntb->peer_reg->spad + idx * 4, val);

	return (0);
}

static int
intel_ntb_peer_spad_read(device_t dev, unsigned int idx, uint32_t *val)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (idx >= ntb->spad_count)
		return (EINVAL);

	if (HAS_FEATURE(ntb, NTB_SDOORBELL_LOCKUP))
		*val = intel_ntb_mw_read(4, XEON_SPAD_OFFSET + idx * 4);
	else
		*val = intel_ntb_reg_read(4, ntb->peer_reg->spad + idx * 4);

	return (0);
}

static int
intel_ntb_mw_get_range(device_t dev, unsigned mw_idx, vm_paddr_t *base,
    caddr_t *vbase, size_t *size, size_t *align, size_t *align_size,
    bus_addr_t *plimit)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	struct ntb_pci_bar_info *bar;
	bus_addr_t limit;
	size_t bar_b2b_off;
	enum ntb_bar bar_num;

	if (mw_idx >= intel_ntb_mw_count(dev))
		return (EINVAL);
	mw_idx = intel_ntb_user_mw_to_idx(ntb, mw_idx);

	bar_num = intel_ntb_mw_to_bar(ntb, mw_idx);
	bar = &ntb->bar_info[bar_num];
	bar_b2b_off = 0;
	if (mw_idx == ntb->b2b_mw_idx) {
		KASSERT(ntb->b2b_off != 0,
		    ("user shouldn't get non-shared b2b mw"));
		bar_b2b_off = ntb->b2b_off;
	}

	if (bar_is_64bit(ntb, bar_num))
		limit = BUS_SPACE_MAXADDR;
	else
		limit = BUS_SPACE_MAXADDR_32BIT;

	if (base != NULL)
		*base = bar->pbase + bar_b2b_off;
	if (vbase != NULL)
		*vbase = bar->vbase + bar_b2b_off;
	if (size != NULL)
		*size = bar->size - bar_b2b_off;
	if (align != NULL)
		*align = bar->size;
	if (align_size != NULL)
		*align_size = 1;
	if (plimit != NULL)
		*plimit = limit;
	return (0);
}

static int
intel_ntb_mw_set_trans(device_t dev, unsigned idx, bus_addr_t addr, size_t size)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	struct ntb_pci_bar_info *bar;
	uint64_t base, limit, reg_val;
	size_t bar_size, mw_size;
	uint32_t base_reg, xlat_reg, limit_reg;
	enum ntb_bar bar_num;

	if (idx >= intel_ntb_mw_count(dev))
		return (EINVAL);
	idx = intel_ntb_user_mw_to_idx(ntb, idx);

	bar_num = intel_ntb_mw_to_bar(ntb, idx);
	bar = &ntb->bar_info[bar_num];

	bar_size = bar->size;
	if (idx == ntb->b2b_mw_idx)
		mw_size = bar_size - ntb->b2b_off;
	else
		mw_size = bar_size;

	/* Hardware requires that addr is aligned to bar size */
	if ((addr & (bar_size - 1)) != 0)
		return (EINVAL);

	if (size > mw_size)
		return (EINVAL);

	bar_get_xlat_params(ntb, bar_num, &base_reg, &xlat_reg, &limit_reg);

	limit = 0;
	if (bar_is_64bit(ntb, bar_num)) {
		base = intel_ntb_reg_read(8, base_reg) & BAR_HIGH_MASK;

		if (limit_reg != 0 && size != mw_size)
			limit = base + size;

		/* Set and verify translation address */
		intel_ntb_reg_write(8, xlat_reg, addr);
		reg_val = intel_ntb_reg_read(8, xlat_reg) & BAR_HIGH_MASK;
		if (reg_val != addr) {
			intel_ntb_reg_write(8, xlat_reg, 0);
			return (EIO);
		}

		/* Set and verify the limit */
		intel_ntb_reg_write(8, limit_reg, limit);
		reg_val = intel_ntb_reg_read(8, limit_reg) & BAR_HIGH_MASK;
		if (reg_val != limit) {
			intel_ntb_reg_write(8, limit_reg, base);
			intel_ntb_reg_write(8, xlat_reg, 0);
			return (EIO);
		}
	} else {
		/* Configure 32-bit (split) BAR MW */

		if ((addr & UINT32_MAX) != addr)
			return (ERANGE);
		if (((addr + size) & UINT32_MAX) != (addr + size))
			return (ERANGE);

		base = intel_ntb_reg_read(4, base_reg) & BAR_HIGH_MASK;

		if (limit_reg != 0 && size != mw_size)
			limit = base + size;

		/* Set and verify translation address */
		intel_ntb_reg_write(4, xlat_reg, addr);
		reg_val = intel_ntb_reg_read(4, xlat_reg) & BAR_HIGH_MASK;
		if (reg_val != addr) {
			intel_ntb_reg_write(4, xlat_reg, 0);
			return (EIO);
		}

		/* Set and verify the limit */
		intel_ntb_reg_write(4, limit_reg, limit);
		reg_val = intel_ntb_reg_read(4, limit_reg) & BAR_HIGH_MASK;
		if (reg_val != limit) {
			intel_ntb_reg_write(4, limit_reg, base);
			intel_ntb_reg_write(4, xlat_reg, 0);
			return (EIO);
		}
	}
	return (0);
}

static int
intel_ntb_mw_clear_trans(device_t dev, unsigned mw_idx)
{

	return (intel_ntb_mw_set_trans(dev, mw_idx, 0, 0));
}

static int
intel_ntb_mw_get_wc(device_t dev, unsigned idx, vm_memattr_t *mode)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	struct ntb_pci_bar_info *bar;

	if (idx >= intel_ntb_mw_count(dev))
		return (EINVAL);
	idx = intel_ntb_user_mw_to_idx(ntb, idx);

	bar = &ntb->bar_info[intel_ntb_mw_to_bar(ntb, idx)];
	*mode = bar->map_mode;
	return (0);
}

static int
intel_ntb_mw_set_wc(device_t dev, unsigned idx, vm_memattr_t mode)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (idx >= intel_ntb_mw_count(dev))
		return (EINVAL);

	idx = intel_ntb_user_mw_to_idx(ntb, idx);
	return (intel_ntb_mw_set_wc_internal(ntb, idx, mode));
}

static int
intel_ntb_mw_set_wc_internal(struct ntb_softc *ntb, unsigned idx, vm_memattr_t mode)
{
	struct ntb_pci_bar_info *bar;
	int rc;

	bar = &ntb->bar_info[intel_ntb_mw_to_bar(ntb, idx)];
	if (bar->map_mode == mode)
		return (0);

	rc = pmap_change_attr((vm_offset_t)bar->vbase, bar->size, mode);
	if (rc == 0)
		bar->map_mode = mode;

	return (rc);
}

static void
intel_ntb_peer_db_set(device_t dev, uint64_t bit)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (HAS_FEATURE(ntb, NTB_SB01BASE_LOCKUP)) {
		struct ntb_pci_bar_info *lapic;
		unsigned i;

		lapic = ntb->peer_lapic_bar;

		for (i = 0; i < XEON_NONLINK_DB_MSIX_BITS; i++) {
			if ((bit & intel_ntb_db_vector_mask(dev, i)) != 0)
				bus_space_write_4(lapic->pci_bus_tag,
				    lapic->pci_bus_handle,
				    ntb->peer_msix_data[i].nmd_ofs,
				    ntb->peer_msix_data[i].nmd_data);
		}
		return;
	}

	if (HAS_FEATURE(ntb, NTB_SDOORBELL_LOCKUP)) {
		intel_ntb_mw_write(2, XEON_PDOORBELL_OFFSET, bit);
		return;
	}

	db_iowrite(ntb, ntb->peer_reg->db_bell, bit);
}

static int
intel_ntb_peer_db_addr(device_t dev, bus_addr_t *db_addr, vm_size_t *db_size)
{
	struct ntb_softc *ntb = device_get_softc(dev);
	struct ntb_pci_bar_info *bar;
	uint64_t regoff;

	KASSERT((db_addr != NULL && db_size != NULL), ("must be non-NULL"));

	if (!HAS_FEATURE(ntb, NTB_SDOORBELL_LOCKUP)) {
		bar = &ntb->bar_info[NTB_CONFIG_BAR];
		regoff = ntb->peer_reg->db_bell;
	} else {
		KASSERT(ntb->b2b_mw_idx != B2B_MW_DISABLED,
		    ("invalid b2b idx"));

		bar = &ntb->bar_info[intel_ntb_mw_to_bar(ntb, ntb->b2b_mw_idx)];
		regoff = XEON_PDOORBELL_OFFSET;
	}
	KASSERT(bar->pci_bus_tag != X86_BUS_SPACE_IO, ("uh oh"));

	/* HACK: Specific to current x86 bus implementation. */
	*db_addr = ((uint64_t)bar->pci_bus_handle + regoff);
	*db_size = ntb->reg->db_size;
	return (0);
}

static uint64_t
intel_ntb_db_valid_mask(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	return (ntb->db_valid_mask);
}

static int
intel_ntb_db_vector_count(device_t dev)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	return (ntb->db_vec_count);
}

static uint64_t
intel_ntb_db_vector_mask(device_t dev, uint32_t vector)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (vector > ntb->db_vec_count)
		return (0);
	return (ntb->db_valid_mask & intel_ntb_vec_mask(ntb, vector));
}

static bool
intel_ntb_link_is_up(device_t dev, enum ntb_speed *speed, enum ntb_width *width)
{
	struct ntb_softc *ntb = device_get_softc(dev);

	if (speed != NULL)
		*speed = intel_ntb_link_sta_speed(ntb);
	if (width != NULL)
		*width = intel_ntb_link_sta_width(ntb);
	return (link_is_up(ntb));
}

static void
save_bar_parameters(struct ntb_pci_bar_info *bar)
{

	bar->pci_bus_tag = rman_get_bustag(bar->pci_resource);
	bar->pci_bus_handle = rman_get_bushandle(bar->pci_resource);
	bar->pbase = rman_get_start(bar->pci_resource);
	bar->size = rman_get_size(bar->pci_resource);
	bar->vbase = rman_get_virtual(bar->pci_resource);
}

static device_method_t ntb_intel_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		intel_ntb_probe),
	DEVMETHOD(device_attach,	intel_ntb_attach),
	DEVMETHOD(device_detach,	intel_ntb_detach),
	/* Bus interface */
	DEVMETHOD(bus_child_location_str, ntb_child_location_str),
	DEVMETHOD(bus_print_child,	ntb_print_child),
	/* NTB interface */
	DEVMETHOD(ntb_link_is_up,	intel_ntb_link_is_up),
	DEVMETHOD(ntb_link_enable,	intel_ntb_link_enable),
	DEVMETHOD(ntb_link_disable,	intel_ntb_link_disable),
	DEVMETHOD(ntb_link_enabled,	intel_ntb_link_enabled),
	DEVMETHOD(ntb_mw_count,		intel_ntb_mw_count),
	DEVMETHOD(ntb_mw_get_range,	intel_ntb_mw_get_range),
	DEVMETHOD(ntb_mw_set_trans,	intel_ntb_mw_set_trans),
	DEVMETHOD(ntb_mw_clear_trans,	intel_ntb_mw_clear_trans),
	DEVMETHOD(ntb_mw_get_wc,	intel_ntb_mw_get_wc),
	DEVMETHOD(ntb_mw_set_wc,	intel_ntb_mw_set_wc),
	DEVMETHOD(ntb_spad_count,	intel_ntb_spad_count),
	DEVMETHOD(ntb_spad_clear,	intel_ntb_spad_clear),
	DEVMETHOD(ntb_spad_write,	intel_ntb_spad_write),
	DEVMETHOD(ntb_spad_read,	intel_ntb_spad_read),
	DEVMETHOD(ntb_peer_spad_write,	intel_ntb_peer_spad_write),
	DEVMETHOD(ntb_peer_spad_read,	intel_ntb_peer_spad_read),
	DEVMETHOD(ntb_db_valid_mask,	intel_ntb_db_valid_mask),
	DEVMETHOD(ntb_db_vector_count,	intel_ntb_db_vector_count),
	DEVMETHOD(ntb_db_vector_mask,	intel_ntb_db_vector_mask),
	DEVMETHOD(ntb_db_clear,		intel_ntb_db_clear),
	DEVMETHOD(ntb_db_clear_mask,	intel_ntb_db_clear_mask),
	DEVMETHOD(ntb_db_read,		intel_ntb_db_read),
	DEVMETHOD(ntb_db_set_mask,	intel_ntb_db_set_mask),
	DEVMETHOD(ntb_peer_db_addr,	intel_ntb_peer_db_addr),
	DEVMETHOD(ntb_peer_db_set,	intel_ntb_peer_db_set),
	DEVMETHOD_END
};

static DEFINE_CLASS_0(ntb_hw, ntb_intel_driver, ntb_intel_methods,
    sizeof(struct ntb_softc));
DRIVER_MODULE(ntb_hw_intel, pci, ntb_intel_driver, ntb_hw_devclass, NULL, NULL);
MODULE_DEPEND(ntb_hw_intel, ntb, 1, 1, 1);
MODULE_VERSION(ntb_hw_intel, 1);
MODULE_PNP_INFO("W32:vendor/device;D:#", pci, ntb_hw_intel, pci_ids,
    nitems(pci_ids));
