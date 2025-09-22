/*	$OpenBSD: if_qwz_pci.c,v 1.6 2024/12/09 09:35:33 patrick Exp $	*/

/*
 * Copyright 2023 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc.
 * Copyright (c) 2018-2021 The Linux Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *  * Neither the name of [Owner Organization] nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* XXX linux porting goo */
#ifdef __LP64__
#define BITS_PER_LONG		64
#else
#define BITS_PER_LONG		32
#endif
#define GENMASK(h, l) (((~0UL) >> (BITS_PER_LONG - (h) - 1)) & ((~0UL) << (l)))
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(_m, _v) ((typeof(_m))(((_v) & (_m)) >> __bf_shf(_m)))
#define BIT(x)               (1UL << (x))
#define test_bit(i, a)  ((a) & (1 << (i)))
#define clear_bit(i, a) ((a)) &= ~(1 << (i))
#define set_bit(i, a)   ((a)) |= (1 << (i))

/* #define QWZ_DEBUG */

#include <dev/ic/qwzreg.h>
#include <dev/ic/qwzvar.h>

#ifdef QWZ_DEBUG 
/* Headers needed for RDDM dump */
#include <sys/namei.h>
#include <sys/pledge.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/proc.h>
#endif

#define ATH12K_PCI_IRQ_CE0_OFFSET	3
#define ATH12K_PCI_IRQ_DP_OFFSET	14

#define ATH12K_PCI_CE_WAKE_IRQ		2

#define ATH12K_PCI_WINDOW_ENABLE_BIT	0x40000000
#define ATH12K_PCI_WINDOW_REG_ADDRESS	0x310c
#define ATH12K_PCI_WINDOW_VALUE_MASK	GENMASK(24, 19)
#define ATH12K_PCI_WINDOW_START		0x80000
#define ATH12K_PCI_WINDOW_RANGE_MASK	GENMASK(18, 0)
#define ATH12K_PCI_WINDOW_STATIC_MASK	GENMASK(31, 6)

/* BAR0 + 4k is always accessible, and no need to force wakeup. */
#define ATH12K_PCI_ACCESS_ALWAYS_OFF	0xFE0	/* 4K - 32 = 0xFE0 */

#define TCSR_SOC_HW_VERSION		0x1b00000
#define TCSR_SOC_HW_VERSION_MAJOR_MASK	GENMASK(11, 8)
#define TCSR_SOC_HW_VERSION_MINOR_MASK	GENMASK(7, 0)

/*
 * pci.h
 */
#define PCIE_SOC_GLOBAL_RESET			0x3008
#define PCIE_SOC_GLOBAL_RESET_V			1

#define WLAON_WARM_SW_ENTRY			0x1f80504
#define WLAON_SOC_RESET_CAUSE_REG		0x01f8060c

#define PCIE_Q6_COOKIE_ADDR			0x01f80500
#define PCIE_Q6_COOKIE_DATA			0xc0000000

/* register to wake the UMAC from power collapse */
#define PCIE_SCRATCH_0_SOC_PCIE_REG		0x4040

/* register used for handshake mechanism to validate UMAC is awake */
#define PCIE_SOC_WAKE_PCIE_LOCAL_REG		0x3004

#define PCIE_PCIE_PARF_LTSSM			0x1e081b0
#define PARM_LTSSM_VALUE			0x111

#define GCC_GCC_PCIE_HOT_RST			0x1e38338
#define GCC_GCC_PCIE_HOT_RST_VAL		0x10

#define PCIE_PCIE_INT_ALL_CLEAR			0x1e08228
#define PCIE_SMLH_REQ_RST_LINK_DOWN		0x2
#define PCIE_INT_CLEAR_ALL			0xffffffff

#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG(sc) \
		(sc->hw_params.regs->pcie_qserdes_sysclk_en_sel)
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL	0x10
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK	0xffffffff
#define PCIE_PCS_OSC_DTCT_CONFIG1_REG(sc) \
		(sc->hw_params.regs->pcie_pcs_osc_dtct_config_base)
#define PCIE_PCS_OSC_DTCT_CONFIG1_VAL		0x02
#define PCIE_PCS_OSC_DTCT_CONFIG2_REG(sc) \
		(sc->hw_params.regs->pcie_pcs_osc_dtct_config_base + 0x4)
#define PCIE_PCS_OSC_DTCT_CONFIG2_VAL		0x52
#define PCIE_PCS_OSC_DTCT_CONFIG4_REG(sc) \
		(sc->hw_params.regs->pcie_pcs_osc_dtct_config_base + 0xc)
#define PCIE_PCS_OSC_DTCT_CONFIG4_VAL		0xff
#define PCIE_PCS_OSC_DTCT_CONFIG_MSK		0x000000ff

#define WLAON_QFPROM_PWR_CTRL_REG		0x01f8031c
#define QFPROM_PWR_CTRL_VDD4BLOW_MASK		0x4

#define PCI_MHIREGLEN_REG			0x1e0e100
#define PCI_MHI_REGION_END			0x1e0effc

/*
 * mhi.h
 */
#define PCIE_TXVECDB				0x360
#define PCIE_TXVECSTATUS			0x368
#define PCIE_RXVECDB				0x394
#define PCIE_RXVECSTATUS			0x39C

#define MHI_CHAN_CTX_CHSTATE_MASK		GENMASK(7, 0)
#define   MHI_CHAN_CTX_CHSTATE_DISABLED		0
#define   MHI_CHAN_CTX_CHSTATE_ENABLED		1
#define   MHI_CHAN_CTX_CHSTATE_RUNNING		2
#define   MHI_CHAN_CTX_CHSTATE_SUSPENDED	3
#define   MHI_CHAN_CTX_CHSTATE_STOP		4
#define   MHI_CHAN_CTX_CHSTATE_ERROR		5
#define MHI_CHAN_CTX_BRSTMODE_MASK		GENMASK(9, 8)
#define MHI_CHAN_CTX_BRSTMODE_SHFT		8
#define   MHI_CHAN_CTX_BRSTMODE_DISABLE		2
#define   MHI_CHAN_CTX_BRSTMODE_ENABLE		3
#define MHI_CHAN_CTX_POLLCFG_MASK		GENMASK(15, 10)
#define MHI_CHAN_CTX_RESERVED_MASK		GENMASK(31, 16)

#define QWZ_MHI_CONFIG_WCN7850_MAX_CHANNELS	128
#define QWZ_MHI_CONFIG_WCN7850_TIMEOUT_MS	2000

#define MHI_CHAN_TYPE_INVALID		0
#define MHI_CHAN_TYPE_OUTBOUND		1 /* to device */
#define MHI_CHAN_TYPE_INBOUND		2 /* from device */
#define MHI_CHAN_TYPE_INBOUND_COALESCED	3

#define MHI_EV_CTX_RESERVED_MASK	GENMASK(7, 0)
#define MHI_EV_CTX_INTMODC_MASK		GENMASK(15, 8)
#define MHI_EV_CTX_INTMODT_MASK		GENMASK(31, 16)
#define MHI_EV_CTX_INTMODT_SHFT		16

#define MHI_ER_TYPE_INVALID	0
#define MHI_ER_TYPE_VALID	1

#define MHI_ER_DATA	0
#define MHI_ER_CTRL	1

#define MHI_CH_STATE_DISABLED	0
#define MHI_CH_STATE_ENABLED	1
#define MHI_CH_STATE_RUNNING	2
#define MHI_CH_STATE_SUSPENDED	3
#define MHI_CH_STATE_STOP	4
#define MHI_CH_STATE_ERROR	5

#define QWZ_NUM_EVENT_CTX	2

/* Event context. Shared with device. */
struct qwz_mhi_event_ctxt {
	uint32_t intmod;
	uint32_t ertype;
	uint32_t msivec;

	uint64_t rbase;
	uint64_t rlen;
	uint64_t rp;
	uint64_t wp;
} __packed;

/* Channel context. Shared with device. */
struct qwz_mhi_chan_ctxt {
	uint32_t chcfg;
	uint32_t chtype;
	uint32_t erindex;

	uint64_t rbase;
	uint64_t rlen;
	uint64_t rp;
	uint64_t wp;
} __packed;

/* Command context. Shared with device. */
struct qwz_mhi_cmd_ctxt {
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;

	uint64_t rbase;
	uint64_t rlen;
	uint64_t rp;
	uint64_t wp;
} __packed;

struct qwz_mhi_ring_element {
	uint64_t ptr;
	uint32_t dword[2];
};

struct qwz_xfer_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

#define QWZ_PCI_XFER_MAX_DATA_SIZE	0xffff
#define QWZ_PCI_XFER_RING_MAX_ELEMENTS	64

struct qwz_pci_xfer_ring {
	struct qwz_dmamem	*dmamem;
	bus_size_t		size;
	uint32_t		mhi_chan_id;
	uint32_t		mhi_chan_state;
	uint32_t		mhi_chan_direction;
	uint32_t		mhi_chan_event_ring_index;
	uint32_t		db_addr;
	uint32_t		cmd_status;
	int			num_elements;
	int			queued;
	struct qwz_xfer_data	data[QWZ_PCI_XFER_RING_MAX_ELEMENTS];
	uint64_t		rp;
	uint64_t		wp;
	struct qwz_mhi_chan_ctxt *chan_ctxt;
};


#define QWZ_PCI_EVENT_RING_MAX_ELEMENTS	256

struct qwz_pci_event_ring {
	struct qwz_dmamem	*dmamem;
	bus_size_t		size;
	uint32_t		mhi_er_type;
	uint32_t		mhi_er_irq;
	uint32_t		mhi_er_irq_moderation_ms;
	uint32_t		db_addr;
	int			num_elements;
	uint64_t		rp;
	uint64_t		wp;
	struct qwz_mhi_event_ctxt *event_ctxt;
};

struct qwz_cmd_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

#define QWZ_PCI_CMD_RING_MAX_ELEMENTS	128

struct qwz_pci_cmd_ring {
	struct qwz_dmamem	*dmamem;
	bus_size_t		size;
	uint64_t		rp;
	uint64_t		wp;
	int			num_elements;
	int			queued;
};

struct qwz_pci_ops;
struct qwz_msi_config;

#define QWZ_NUM_MSI_VEC	32

struct qwz_pci_softc {
	struct qwz_softc	sc_sc;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	int			sc_cap_off;
	int			sc_msi_off;
	pcireg_t		sc_msi_cap;
	void			*sc_ih[QWZ_NUM_MSI_VEC];
	char			sc_ivname[QWZ_NUM_MSI_VEC][16];
	struct qwz_ext_irq_grp	ext_irq_grp[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	int			mhi_irq[2];
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	bus_addr_t		sc_map;
	bus_size_t		sc_mapsize;

	pcireg_t		sc_lcsr;
	uint32_t		sc_flags;
#define ATH12K_PCI_ASPM_RESTORE	1

	uint32_t		register_window;
	const struct qwz_pci_ops *sc_pci_ops;

	uint32_t		 bhi_off;
	uint32_t		 bhi_ee;
	uint32_t		 bhie_off;
	uint32_t		 mhi_state;
	uint32_t		 max_chan;

	uint64_t		 wake_db;

	/*
	 * DMA memory for AMSS.bin firmware image.
	 * This memory must remain available to the device until
	 * the device is powered down.
	 */
	struct qwz_dmamem	*amss_data;
	struct qwz_dmamem	*amss_vec;

	struct qwz_dmamem	 *rddm_vec;
	struct qwz_dmamem	 *rddm_data;
	int			 rddm_triggered;
	struct task		 rddm_task;
#define	QWZ_RDDM_DUMP_SIZE	0x420000

	struct qwz_dmamem	*chan_ctxt;
	struct qwz_dmamem	*event_ctxt;
	struct qwz_dmamem	*cmd_ctxt;


	struct qwz_pci_xfer_ring xfer_rings[2];
#define QWZ_PCI_XFER_RING_IPCR_OUTBOUND		0
#define QWZ_PCI_XFER_RING_IPCR_INBOUND		1
	struct qwz_pci_event_ring event_rings[QWZ_NUM_EVENT_CTX];
	struct qwz_pci_cmd_ring cmd_ring;
};

int	qwz_pci_match(struct device *, void *, void *);
void	qwz_pci_attach(struct device *, struct device *, void *);
int	qwz_pci_detach(struct device *, int);
void	qwz_pci_attach_hook(struct device *);
void	qwz_pci_free_xfer_rings(struct qwz_pci_softc *);
int	qwz_pci_alloc_xfer_ring(struct qwz_softc *, struct qwz_pci_xfer_ring *,
	    uint32_t, uint32_t, uint32_t, size_t);
int	qwz_pci_alloc_xfer_rings_wcn7850(struct qwz_pci_softc *);
void	qwz_pci_free_event_rings(struct qwz_pci_softc *);
int	qwz_pci_alloc_event_ring(struct qwz_softc *,
	    struct qwz_pci_event_ring *, uint32_t, uint32_t, uint32_t, size_t);
int	qwz_pci_alloc_event_rings(struct qwz_pci_softc *);
void	qwz_pci_free_cmd_ring(struct qwz_pci_softc *);
int	qwz_pci_init_cmd_ring(struct qwz_softc *, struct qwz_pci_cmd_ring *);
uint32_t qwz_pci_read(struct qwz_softc *, uint32_t);
void	qwz_pci_write(struct qwz_softc *, uint32_t, uint32_t);

void	qwz_pci_read_hw_version(struct qwz_softc *, uint32_t *, uint32_t *);
uint32_t qwz_pcic_read32(struct qwz_softc *, uint32_t);
void	 qwz_pcic_write32(struct qwz_softc *, uint32_t, uint32_t);

void	qwz_pcic_ext_irq_enable(struct qwz_softc *);
void	qwz_pcic_ext_irq_disable(struct qwz_softc *);
int	qwz_pcic_config_irq(struct qwz_softc *, struct pci_attach_args *);

int	qwz_pci_start(struct qwz_softc *);
void	qwz_pci_stop(struct qwz_softc *);
void	qwz_pci_aspm_disable(struct qwz_softc *);
void	qwz_pci_aspm_restore(struct qwz_softc *);
int	qwz_pci_power_up(struct qwz_softc *);
void	qwz_pci_power_down(struct qwz_softc *);

int	qwz_pci_bus_wake_up(struct qwz_softc *);
void	qwz_pci_bus_release(struct qwz_softc *);
void	qwz_pci_window_write32(struct qwz_softc *, uint32_t, uint32_t);
uint32_t qwz_pci_window_read32(struct qwz_softc *, uint32_t);

int	qwz_mhi_register(struct qwz_softc *);
void	qwz_mhi_unregister(struct qwz_softc *);
void	qwz_mhi_ring_doorbell(struct qwz_softc *sc, uint64_t, uint64_t);
void	qwz_mhi_device_wake(struct qwz_softc *);
void	qwz_mhi_device_zzz(struct qwz_softc *);
int	qwz_mhi_wake_db_clear_valid(struct qwz_softc *);
void	qwz_mhi_init_xfer_rings(struct qwz_pci_softc *);
void	qwz_mhi_init_event_rings(struct qwz_pci_softc *);
void	qwz_mhi_init_cmd_ring(struct qwz_pci_softc *);
void	qwz_mhi_init_dev_ctxt(struct qwz_pci_softc *);
int	qwz_mhi_send_cmd(struct qwz_pci_softc *psc, uint32_t, uint32_t);
void *	qwz_pci_xfer_ring_get_elem(struct qwz_pci_xfer_ring *, uint64_t);
struct qwz_xfer_data *qwz_pci_xfer_ring_get_data(struct qwz_pci_xfer_ring *,
	    uint64_t);
int	qwz_mhi_submit_xfer(struct qwz_softc *sc, struct mbuf *m);
int	qwz_mhi_start_channel(struct qwz_pci_softc *,
	    struct qwz_pci_xfer_ring *);
int	qwz_mhi_start_channels(struct qwz_pci_softc *);
int	qwz_mhi_start(struct qwz_pci_softc *);
void	qwz_mhi_stop(struct qwz_softc *);
int	qwz_mhi_reset_device(struct qwz_softc *, int);
void	qwz_mhi_clear_vector(struct qwz_softc *);
int	qwz_mhi_fw_load_handler(struct qwz_pci_softc *);
int	qwz_mhi_await_device_reset(struct qwz_softc *);
int	qwz_mhi_await_device_ready(struct qwz_softc *);
void	qwz_mhi_ready_state_transition(struct qwz_pci_softc *);
void	qwz_mhi_mission_mode_state_transition(struct qwz_pci_softc *);
void	qwz_mhi_low_power_mode_state_transition(struct qwz_pci_softc *);
void	qwz_mhi_set_state(struct qwz_softc *, uint32_t);
void	qwz_mhi_init_mmio(struct qwz_pci_softc *);
int	qwz_mhi_fw_load_bhi(struct qwz_pci_softc *, uint8_t *, size_t);
int	qwz_mhi_fw_load_bhie(struct qwz_pci_softc *, uint8_t *, size_t);
void	qwz_rddm_prepare(struct qwz_pci_softc *);
#ifdef QWZ_DEBUG
void	qwz_rddm_task(void *);
#endif
void *	qwz_pci_event_ring_get_elem(struct qwz_pci_event_ring *, uint64_t);
void	qwz_pci_intr_ctrl_event_mhi(struct qwz_pci_softc *, uint32_t);
void	qwz_pci_intr_ctrl_event_ee(struct qwz_pci_softc *, uint32_t);
void	qwz_pci_intr_ctrl_event_cmd_complete(struct qwz_pci_softc *,
	    uint64_t, uint32_t);
int	qwz_pci_intr_ctrl_event(struct qwz_pci_softc *,
	    struct qwz_pci_event_ring *);
void	qwz_pci_intr_data_event_tx(struct qwz_pci_softc *,
	    struct qwz_mhi_ring_element *);
int	qwz_pci_intr_data_event(struct qwz_pci_softc *,
	    struct qwz_pci_event_ring *);
int	qwz_pci_intr_mhi_ctrl(void *);
int	qwz_pci_intr_mhi_data(void *);
int	qwz_pci_intr(void *);

struct qwz_pci_ops {
	int	 (*wakeup)(struct qwz_softc *);
	void	 (*release)(struct qwz_softc *);
	int	 (*get_msi_irq)(struct qwz_softc *, unsigned int);
	void	 (*window_write32)(struct qwz_softc *, uint32_t, uint32_t);
	uint32_t (*window_read32)(struct qwz_softc *, uint32_t);
	int	 (*alloc_xfer_rings)(struct qwz_pci_softc *);
};


static const struct qwz_pci_ops qwz_pci_ops_wcn7850 = {
	.wakeup = qwz_pci_bus_wake_up,
	.release = qwz_pci_bus_release,
	.window_write32 = qwz_pci_window_write32,
	.window_read32 = qwz_pci_window_read32,
	.alloc_xfer_rings = qwz_pci_alloc_xfer_rings_wcn7850,
};

const struct cfattach qwz_pci_ca = {
	sizeof(struct qwz_pci_softc),
	qwz_pci_match,
	qwz_pci_attach,
	qwz_pci_detach,
	qwz_activate
};

static const struct pci_matchid qwz_pci_devices[] = {
	{ PCI_VENDOR_QUALCOMM, PCI_PRODUCT_QUALCOMM_WCN7850 }
};

int
qwz_pci_match(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid(aux, qwz_pci_devices, nitems(qwz_pci_devices));
}

void
qwz_pci_init_qmi_ce_config(struct qwz_softc *sc)
{
	struct qwz_qmi_ce_cfg *cfg = &sc->qmi_ce_cfg;

	qwz_ce_get_shadow_config(sc, &cfg->shadow_reg_v3,
	    &cfg->shadow_reg_v3_len);
}

const struct qwz_msi_config qwz_msi_config_one_msi = {
	.total_vectors = 1,
	.total_users = 4,
	.users = (struct qwz_msi_user[]) {
		{ .name = "MHI", .num_vectors = 1, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "DP", .num_vectors = 1, .base_vector = 0 },
	},
};

const struct qwz_msi_config qwz_msi_config[] = {
	{
		.total_vectors = 16,
		.total_users = 3,
		.users = (struct qwz_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
			{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
		},
		.hw_rev = ATH12K_HW_WCN7850_HW20,
	},
};

int
qwz_pcic_init_msi_config(struct qwz_softc *sc)
{
	const struct qwz_msi_config *msi_config;
	int i;

	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags)) {
		sc->msi_cfg = &qwz_msi_config_one_msi;
		return 0;
	}
	for (i = 0; i < nitems(qwz_msi_config); i++) {
		msi_config = &qwz_msi_config[i];

		if (msi_config->hw_rev == sc->sc_hw_rev)
			break;
	}

	if (i == nitems(qwz_msi_config)) {
		printf("%s: failed to fetch msi config, "
		    "unsupported hw version: 0x%x\n",
		    sc->sc_dev.dv_xname, sc->sc_hw_rev);
		return EINVAL;
	}

	sc->msi_cfg = msi_config;
	return 0;
}

int
qwz_pci_alloc_msi(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	uint64_t addr;
	pcireg_t data;

	if (psc->sc_msi_cap & PCI_MSI_MC_C64) {
		uint64_t addr_hi;
		pcireg_t addr_lo;

		addr_lo = pci_conf_read(psc->sc_pc, psc->sc_tag,
		    psc->sc_msi_off + PCI_MSI_MA);
		addr_hi = pci_conf_read(psc->sc_pc, psc->sc_tag,
		    psc->sc_msi_off + PCI_MSI_MAU32);
		addr = addr_hi << 32 | addr_lo;
		data = pci_conf_read(psc->sc_pc, psc->sc_tag,
		    psc->sc_msi_off + PCI_MSI_MD64);
	} else {
		addr = pci_conf_read(psc->sc_pc, psc->sc_tag,
		    psc->sc_msi_off + PCI_MSI_MA);
		data = pci_conf_read(psc->sc_pc, psc->sc_tag,
		    psc->sc_msi_off + PCI_MSI_MD32);
	}

	sc->msi_addr_lo = addr & 0xffffffff;
	sc->msi_addr_hi = ((uint64_t)addr) >> 32;
	sc->msi_data_start = data;

	DPRINTF("%s: MSI addr: 0x%llx MSI data: 0x%x\n", sc->sc_dev.dv_xname,
	    addr, data);

	return 0;
}

int
qwz_pcic_map_service_to_pipe(struct qwz_softc *sc, uint16_t service_id,
    uint8_t *ul_pipe, uint8_t *dl_pipe)
{
	const struct service_to_pipe *entry;
	int ul_set = 0, dl_set = 0;
	int i;

	for (i = 0; i < sc->hw_params.svc_to_ce_map_len; i++) {
		entry = &sc->hw_params.svc_to_ce_map[i];

		if (le32toh(entry->service_id) != service_id)
			continue;

		switch (le32toh(entry->pipedir)) {
		case PIPEDIR_NONE:
			break;
		case PIPEDIR_IN:
			*dl_pipe = le32toh(entry->pipenum);
			dl_set = 1;
			break;
		case PIPEDIR_OUT:
			*ul_pipe = le32toh(entry->pipenum);
			ul_set = 1;
			break;
		case PIPEDIR_INOUT:
			*dl_pipe = le32toh(entry->pipenum);
			*ul_pipe = le32toh(entry->pipenum);
			dl_set = 1;
			ul_set = 1;
			break;
		}
	}

	if (!ul_set || !dl_set) {
		DPRINTF("%s: found no uplink and no downlink\n", __func__);
		return ENOENT;
	}

	return 0;
}

int
qwz_pcic_get_user_msi_vector(struct qwz_softc *sc, char *user_name,
    int *num_vectors, uint32_t *user_base_data, uint32_t *base_vector)
{
	const struct qwz_msi_config *msi_config = sc->msi_cfg;
	int idx;

	for (idx = 0; idx < msi_config->total_users; idx++) {
		if (strcmp(user_name, msi_config->users[idx].name) == 0) {
			*num_vectors = msi_config->users[idx].num_vectors;
			*base_vector =  msi_config->users[idx].base_vector;
			*user_base_data = *base_vector + sc->msi_data_start;

			DPRINTF("%s: MSI assignment %s num_vectors %d "
			    "user_base_data %u base_vector %u\n", __func__,
			    user_name, *num_vectors, *user_base_data,
			    *base_vector);
			return 0;
		}
	}

	DPRINTF("%s: Failed to find MSI assignment for %s\n",
	    sc->sc_dev.dv_xname, user_name);

	return EINVAL;
}

void
qwz_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)self;
	struct qwz_softc *sc = &psc->sc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t soc_hw_version_major, soc_hw_version_minor;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	pcireg_t memtype, reg;
	const char *intrstr;
	int error;
	pcireg_t sreg;

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;
	psc->sc_tag = pa->pa_tag;

#ifdef __HAVE_FDT
	sc->sc_node = PCITAG_NODE(pa->pa_tag);
#endif

	rw_init(&sc->ioctl_rwl, "qwzioctl");

	sreg = pci_conf_read(psc->sc_pc, psc->sc_tag, PCI_SUBSYS_ID_REG);
	sc->id.bdf_search = ATH12K_BDF_SEARCH_DEFAULT;
	sc->id.vendor = PCI_VENDOR(pa->pa_id);
	sc->id.device = PCI_PRODUCT(pa->pa_id);
	sc->id.subsystem_vendor = PCI_VENDOR(sreg);
	sc->id.subsystem_device = PCI_PRODUCT(sreg);

	strlcpy(sc->sc_bus_str, "pci", sizeof(sc->sc_bus_str));

	sc->ops.read32 = qwz_pcic_read32;
	sc->ops.write32 = qwz_pcic_write32;
	sc->ops.start = qwz_pci_start;
	sc->ops.stop = qwz_pci_stop;
	sc->ops.power_up = qwz_pci_power_up;
	sc->ops.power_down = qwz_pci_power_down;
	sc->ops.submit_xfer = qwz_mhi_submit_xfer;
	sc->ops.irq_enable = qwz_pcic_ext_irq_enable;
	sc->ops.irq_disable = qwz_pcic_ext_irq_disable;
	sc->ops.map_service_to_pipe = qwz_pcic_map_service_to_pipe;
	sc->ops.get_user_msi_vector = qwz_pcic_get_user_msi_vector;

	if (pci_get_capability(psc->sc_pc, psc->sc_tag, PCI_CAP_PCIEXPRESS,
	    &psc->sc_cap_off, NULL) == 0) {
		printf(": can't find PCIe capability structure\n");
		return;
	}

	if (pci_get_capability(psc->sc_pc, psc->sc_tag, PCI_CAP_MSI,
	    &psc->sc_msi_off, &psc->sc_msi_cap) == 0) {
		printf(": can't find MSI capability structure\n");
		return;
	}

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, reg);

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	if (pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &psc->sc_st, &psc->sc_sh, &psc->sc_map, &psc->sc_mapsize, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->mem = psc->sc_map;

	sc->num_msivec = 32;
	if (pci_intr_enable_msivec(pa, sc->num_msivec) != 0) {
		sc->num_msivec = 1;
		if (pci_intr_map_msi(pa, &ih) != 0) {
			printf(": can't map interrupt\n");
			return;
		}
		clear_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags);
	} else {
		if (pci_intr_map_msivec(pa, 0, &ih) != 0 &&
		    pci_intr_map_msi(pa, &ih) != 0) {
			printf(": can't map interrupt\n");
			return;
		}
		set_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags);
		psc->mhi_irq[MHI_ER_CTRL] = 1;
		psc->mhi_irq[MHI_ER_DATA] = 2;
	}

	intrstr = pci_intr_string(psc->sc_pc, ih);
	snprintf(psc->sc_ivname[0], sizeof(psc->sc_ivname[0]), "%s:bhi",
	    sc->sc_dev.dv_xname);
	psc->sc_ih[0] = pci_intr_establish(psc->sc_pc, ih, IPL_NET,
	    qwz_pci_intr, psc, psc->sc_ivname[0]);
	if (psc->sc_ih[0] == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	if (test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags)) {
		int msivec;

		msivec = psc->mhi_irq[MHI_ER_CTRL];
		if (pci_intr_map_msivec(pa, msivec, &ih) != 0 &&
		    pci_intr_map_msi(pa, &ih) != 0) {
			printf(": can't map interrupt\n");
			return;
		}
		snprintf(psc->sc_ivname[msivec],
		    sizeof(psc->sc_ivname[msivec]),
		    "%s:mhic", sc->sc_dev.dv_xname);
		psc->sc_ih[msivec] = pci_intr_establish(psc->sc_pc, ih,
		    IPL_NET, qwz_pci_intr_mhi_ctrl, psc,
		    psc->sc_ivname[msivec]);
		if (psc->sc_ih[msivec] == NULL) {
			printf("%s: can't establish interrupt\n",
			    sc->sc_dev.dv_xname);
			return;
		}

		msivec = psc->mhi_irq[MHI_ER_DATA];
		if (pci_intr_map_msivec(pa, msivec, &ih) != 0 &&
		    pci_intr_map_msi(pa, &ih) != 0) {
			printf(": can't map interrupt\n");
			return;
		}
		snprintf(psc->sc_ivname[msivec],
		    sizeof(psc->sc_ivname[msivec]),
		    "%s:mhid", sc->sc_dev.dv_xname);
		psc->sc_ih[msivec] = pci_intr_establish(psc->sc_pc, ih,
		    IPL_NET, qwz_pci_intr_mhi_data, psc,
		    psc->sc_ivname[msivec]);
		if (psc->sc_ih[msivec] == NULL) {
			printf("%s: can't establish interrupt\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	}

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_QUALCOMM_WCN7850:
		sc->static_window_map = 0;
		psc->sc_pci_ops = &qwz_pci_ops_wcn7850;
		sc->hal_rx_ops = &hal_rx_wcn7850_ops;
		sc->id.bdf_search = ATH12K_BDF_SEARCH_BUS_AND_BOARD;
		qwz_pci_read_hw_version(sc, &soc_hw_version_major,
		    &soc_hw_version_minor);
		switch (soc_hw_version_major) {
		case 2:
			sc->sc_hw_rev = ATH12K_HW_WCN7850_HW20;
			break;
		default:
			printf(": unknown hardware version found for WCN7850: "
			    "%d\n", soc_hw_version_major);
			return;
		}

		psc->max_chan = QWZ_MHI_CONFIG_WCN7850_MAX_CHANNELS;
		break;
	default:
		printf(": unsupported chip\n");
		return;
	}

	error = qwz_pcic_init_msi_config(sc);
	if (error)
		goto err_pci_free_region;

	error = qwz_pci_alloc_msi(sc);
	if (error) {
		printf("%s: failed to enable msi: %d\n", sc->sc_dev.dv_xname,
		    error);
		goto err_pci_free_region;
	}

	error = qwz_init_hw_params(sc);
	if (error)
		goto err_pci_disable_msi;

	psc->chan_ctxt = qwz_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct qwz_mhi_chan_ctxt) * psc->max_chan, 0);
	if (psc->chan_ctxt == NULL) {
		printf("%s: could not allocate channel context array\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_disable_msi;
	}

	if (psc->sc_pci_ops->alloc_xfer_rings(psc)) {
		printf("%s: could not allocate transfer rings\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_free_chan_ctxt;
	}

	psc->event_ctxt = qwz_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct qwz_mhi_event_ctxt) * QWZ_NUM_EVENT_CTX, 0);
	if (psc->event_ctxt == NULL) {
		printf("%s: could not allocate event context array\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_free_xfer_rings;
	}

	if (qwz_pci_alloc_event_rings(psc)) {
		printf("%s: could not allocate event rings\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_free_event_ctxt;
	}

	psc->cmd_ctxt = qwz_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct qwz_mhi_cmd_ctxt), 0);
	if (psc->cmd_ctxt == NULL) {
		printf("%s: could not allocate command context array\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_free_event_rings;
	}

	if (qwz_pci_init_cmd_ring(sc, &psc->cmd_ring))  {
		printf("%s: could not allocate command ring\n",
		    sc->sc_dev.dv_xname);
		goto err_pci_free_cmd_ctxt;
	}

	error = qwz_mhi_register(sc);
	if (error) {
		printf(": failed to register mhi: %d\n", error);
		goto err_pci_free_cmd_ring;
	}

	error = qwz_hal_srng_init(sc);
	if (error)
		goto err_mhi_unregister;

	error = qwz_ce_alloc_pipes(sc);
	if (error) {
		printf(": failed to allocate ce pipes: %d\n", error);
		goto err_hal_srng_deinit;
	}

	sc->sc_nswq = taskq_create("qwzns", 1, IPL_NET, 0);
	if (sc->sc_nswq == NULL)
		goto err_ce_free;

	error = qwz_pcic_config_irq(sc, pa);
	if (error) {
		printf("%s: failed to config irq: %d\n",
		    sc->sc_dev.dv_xname, error);
		goto err_ce_free;
	}
#if notyet
	ret = ath12k_pci_set_irq_affinity_hint(ab_pci, cpumask_of(0));
	if (ret) {
		ath12k_err(ab, "failed to set irq affinity %d\n", ret);
		goto err_free_irq;
	}

	/* kernel may allocate a dummy vector before request_irq and
	 * then allocate a real vector when request_irq is called.
	 * So get msi_data here again to avoid spurious interrupt
	 * as msi_data will configured to srngs.
	 */
	ret = ath12k_pci_config_msi_data(ab_pci);
	if (ret) {
		ath12k_err(ab, "failed to config msi_data: %d\n", ret);
		goto err_irq_affinity_cleanup;
	}
#endif
#ifdef QWZ_DEBUG
	task_set(&psc->rddm_task, qwz_rddm_task, psc);
#endif
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
#if 0
	    IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | /* A-MPDU */
#endif
	    IEEE80211_C_ADDBA_OFFLOAD | /* device sends ADDBA/DELBA frames */
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN |		/* WPA/RSN */
	    IEEE80211_C_SCANALL |	/* device scans all channels at once */
	    IEEE80211_C_SCANALLBAND |	/* device scans all bands at once */
#if 0
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
#endif
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE;	/* short preamble supported */

	ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = &ic->ic_channels[1];

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = qwz_ioctl;
	ifp->if_start = qwz_start;
	ifp->if_watchdog = qwz_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ieee80211_media_init(ifp, qwz_media_change, ieee80211_media_status);

	ic->ic_node_alloc = qwz_node_alloc;

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = qwz_newstate;
	ic->ic_set_key = qwz_set_key;
	ic->ic_delete_key = qwz_delete_key;
#if 0
	ic->ic_updatechan = qwz_updatechan;
	ic->ic_updateprot = qwz_updateprot;
	ic->ic_updateslot = qwz_updateslot;
	ic->ic_updateedca = qwz_updateedca;
	ic->ic_updatedtim = qwz_updatedtim;
#endif
	/*
	 * We cannot read the MAC address without loading the
	 * firmware from disk. Postpone until mountroot is done.
	 */
	config_mountroot(self, qwz_pci_attach_hook);
	return;

err_ce_free:
	qwz_ce_free_pipes(sc);
err_hal_srng_deinit:
err_mhi_unregister:
err_pci_free_cmd_ring:
	qwz_pci_free_cmd_ring(psc);
err_pci_free_cmd_ctxt:
	qwz_dmamem_free(sc->sc_dmat, psc->cmd_ctxt);
	psc->cmd_ctxt = NULL;
err_pci_free_event_rings:
	qwz_pci_free_event_rings(psc);
err_pci_free_event_ctxt:
	qwz_dmamem_free(sc->sc_dmat, psc->event_ctxt);
	psc->event_ctxt = NULL;
err_pci_free_xfer_rings:
	qwz_pci_free_xfer_rings(psc);
err_pci_free_chan_ctxt:
	qwz_dmamem_free(sc->sc_dmat, psc->chan_ctxt);
	psc->chan_ctxt = NULL;
err_pci_disable_msi:
err_pci_free_region:
	pci_intr_disestablish(psc->sc_pc, psc->sc_ih[0]);
	return;
}

int
qwz_pci_detach(struct device *self, int flags)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)self;
	struct qwz_softc *sc = &psc->sc_sc;

	if (psc->sc_ih[0]) {
		pci_intr_disestablish(psc->sc_pc, psc->sc_ih[0]);
		psc->sc_ih[0] = NULL;
	}

	qwz_detach(sc);

	qwz_pci_free_event_rings(psc);
	qwz_pci_free_xfer_rings(psc);
	qwz_pci_free_cmd_ring(psc);

	if (psc->event_ctxt) {
		qwz_dmamem_free(sc->sc_dmat, psc->event_ctxt);
		psc->event_ctxt = NULL;
	}
	if (psc->chan_ctxt) {
		qwz_dmamem_free(sc->sc_dmat, psc->chan_ctxt);
		psc->chan_ctxt = NULL;
	}
	if (psc->cmd_ctxt) {
		qwz_dmamem_free(sc->sc_dmat, psc->cmd_ctxt);
		psc->cmd_ctxt = NULL;
	}

	if (psc->amss_data) {
		qwz_dmamem_free(sc->sc_dmat, psc->amss_data);
		psc->amss_data = NULL;
	}
	if (psc->amss_vec) {
		qwz_dmamem_free(sc->sc_dmat, psc->amss_vec);
		psc->amss_vec = NULL;
	}

	return 0;
}

void
qwz_pci_attach_hook(struct device *self)
{
	struct qwz_softc *sc = (void *)self;
	int s = splnet();

	qwz_attach(sc);

	splx(s);
}

void
qwz_pci_free_xfer_rings(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int i;

	for (i = 0; i < nitems(psc->xfer_rings); i++) {
		struct qwz_pci_xfer_ring *ring = &psc->xfer_rings[i];
		if (ring->dmamem) {
			qwz_dmamem_free(sc->sc_dmat, ring->dmamem);
			ring->dmamem = NULL;
		}
		memset(ring, 0, sizeof(*ring));
	}
}

int
qwz_pci_alloc_xfer_ring(struct qwz_softc *sc, struct qwz_pci_xfer_ring *ring,
    uint32_t id, uint32_t direction, uint32_t event_ring_index,
    size_t num_elements)
{
	bus_size_t size;
	int i, err;

	memset(ring, 0, sizeof(*ring));

	size = sizeof(struct qwz_mhi_ring_element) * num_elements;
	/* Hardware requires that rings are aligned to ring size. */
	ring->dmamem = qwz_dmamem_alloc(sc->sc_dmat, size, size);
	if (ring->dmamem == NULL)
		return ENOMEM;

	ring->size = size;
	ring->mhi_chan_id = id;
	ring->mhi_chan_state = MHI_CH_STATE_DISABLED;
	ring->mhi_chan_direction = direction;
	ring->mhi_chan_event_ring_index = event_ring_index;
	ring->num_elements = num_elements;

	memset(ring->data, 0, sizeof(ring->data));
	for (i = 0; i < ring->num_elements; i++) {
		struct qwz_xfer_data *xfer = &ring->data[i];

		err = bus_dmamap_create(sc->sc_dmat, QWZ_PCI_XFER_MAX_DATA_SIZE,
		    1, QWZ_PCI_XFER_MAX_DATA_SIZE, 0, BUS_DMA_NOWAIT,
		    &xfer->map);
		if (err) {
			printf("%s: could not create xfer DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		if (direction == MHI_CHAN_TYPE_INBOUND) {
			struct mbuf *m;

			m = m_gethdr(M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				err = ENOBUFS;
				goto fail;
			}

			MCLGETL(m, M_DONTWAIT, QWZ_PCI_XFER_MAX_DATA_SIZE);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				err = ENOBUFS;
				goto fail;
			}

			m->m_len = m->m_pkthdr.len = QWZ_PCI_XFER_MAX_DATA_SIZE;
			err = bus_dmamap_load_mbuf(sc->sc_dmat, xfer->map,
			    m, BUS_DMA_READ | BUS_DMA_NOWAIT);
			if (err) {
				printf("%s: can't map mbuf (error %d)\n",
				    sc->sc_dev.dv_xname, err);
				m_freem(m);
				goto fail;
			}

			bus_dmamap_sync(sc->sc_dmat, xfer->map, 0,
			    QWZ_PCI_XFER_MAX_DATA_SIZE, BUS_DMASYNC_PREREAD);
			xfer->m = m;
		}
	}

	return 0;
fail:
	for (i = 0; i < ring->num_elements; i++) {
		struct qwz_xfer_data *xfer = &ring->data[i];

		if (xfer->map) {
			bus_dmamap_sync(sc->sc_dmat, xfer->map, 0,
			    xfer->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, xfer->map);
			bus_dmamap_destroy(sc->sc_dmat, xfer->map);
			xfer->map = NULL;
		}

		if (xfer->m) {
			m_freem(xfer->m);
			xfer->m = NULL;
		}
	}
	return 1;
}

int
qwz_pci_alloc_xfer_rings_wcn7850(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int ret;

	ret = qwz_pci_alloc_xfer_ring(sc,
	    &psc->xfer_rings[QWZ_PCI_XFER_RING_IPCR_OUTBOUND],
	    20, MHI_CHAN_TYPE_OUTBOUND, 1, 64);
	if (ret)
		goto fail;

	ret = qwz_pci_alloc_xfer_ring(sc,
	    &psc->xfer_rings[QWZ_PCI_XFER_RING_IPCR_INBOUND],
	    21, MHI_CHAN_TYPE_INBOUND, 1, 64);
	if (ret)
		goto fail;

	return 0;
fail:
	qwz_pci_free_xfer_rings(psc);
	return ret;
}

void
qwz_pci_free_event_rings(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int i;

	for (i = 0; i < nitems(psc->event_rings); i++) {
		struct qwz_pci_event_ring *ring = &psc->event_rings[i];
		if (ring->dmamem) {
			qwz_dmamem_free(sc->sc_dmat, ring->dmamem);
			ring->dmamem = NULL;
		}
		memset(ring, 0, sizeof(*ring));
	}
}

int
qwz_pci_alloc_event_ring(struct qwz_softc *sc, struct qwz_pci_event_ring *ring,
    uint32_t type, uint32_t irq, uint32_t intmod, size_t num_elements)
{
	bus_size_t size;

	memset(ring, 0, sizeof(*ring));

	size = sizeof(struct qwz_mhi_ring_element) * num_elements;
	/* Hardware requires that rings are aligned to ring size. */
	ring->dmamem = qwz_dmamem_alloc(sc->sc_dmat, size, size);
	if (ring->dmamem == NULL)
		return ENOMEM;

	ring->size = size;
	ring->mhi_er_type = type;
	ring->mhi_er_irq = irq;
	ring->mhi_er_irq_moderation_ms = intmod;
	ring->num_elements = num_elements;
	return 0;
}

int
qwz_pci_alloc_event_rings(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int ret;

	ret = qwz_pci_alloc_event_ring(sc, &psc->event_rings[0],
	    MHI_ER_CTRL, psc->mhi_irq[MHI_ER_CTRL], 0, 32);
	if (ret)
		goto fail;

	ret = qwz_pci_alloc_event_ring(sc, &psc->event_rings[1],
	    MHI_ER_DATA, psc->mhi_irq[MHI_ER_DATA], 1, 256);
	if (ret)
		goto fail;

	return 0;
fail:
	qwz_pci_free_event_rings(psc);
	return ret;
}

void
qwz_pci_free_cmd_ring(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_pci_cmd_ring *ring = &psc->cmd_ring;

	if (ring->dmamem)
		qwz_dmamem_free(sc->sc_dmat, ring->dmamem);

	memset(ring, 0, sizeof(*ring));
}

int
qwz_pci_init_cmd_ring(struct qwz_softc *sc, struct qwz_pci_cmd_ring *ring)
{
	memset(ring, 0, sizeof(*ring));

	ring->num_elements = QWZ_PCI_CMD_RING_MAX_ELEMENTS;
	ring->size = sizeof(struct qwz_mhi_ring_element) * ring->num_elements;

	/* Hardware requires that rings are aligned to ring size. */
	ring->dmamem = qwz_dmamem_alloc(sc->sc_dmat, ring->size, ring->size);
	if (ring->dmamem == NULL)
		return ENOMEM;

	return 0;
}

uint32_t
qwz_pci_read(struct qwz_softc *sc, uint32_t addr)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	return (bus_space_read_4(psc->sc_st, psc->sc_sh, addr));
}

void
qwz_pci_write(struct qwz_softc *sc, uint32_t addr, uint32_t val)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	bus_space_write_4(psc->sc_st, psc->sc_sh, addr, val);
}

void
qwz_pci_read_hw_version(struct qwz_softc *sc, uint32_t *major,
    uint32_t *minor)
{
	uint32_t soc_hw_version;

	soc_hw_version = qwz_pcic_read32(sc, TCSR_SOC_HW_VERSION);
	*major = FIELD_GET(TCSR_SOC_HW_VERSION_MAJOR_MASK, soc_hw_version);
	*minor = FIELD_GET(TCSR_SOC_HW_VERSION_MINOR_MASK, soc_hw_version);
	DPRINTF("%s: pci tcsr_soc_hw_version major %d minor %d\n",
	    sc->sc_dev.dv_xname, *major, *minor);
}

uint32_t
qwz_pcic_read32(struct qwz_softc *sc, uint32_t offset)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	int ret = 0;
	uint32_t val;
	bool wakeup_required;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup the device to access.
	 */
	wakeup_required = test_bit(ATH12K_FLAG_DEVICE_INIT_DONE, sc->sc_flags)
	    && offset >= ATH12K_PCI_ACCESS_ALWAYS_OFF;
	if (wakeup_required && psc->sc_pci_ops->wakeup)
		ret = psc->sc_pci_ops->wakeup(sc);

	if (offset < ATH12K_PCI_WINDOW_START)
		val = qwz_pci_read(sc, offset);
	else
		val = psc->sc_pci_ops->window_read32(sc, offset);

	if (wakeup_required && !ret && psc->sc_pci_ops->release)
		psc->sc_pci_ops->release(sc);

	return val;
}

void
qwz_pcic_write32(struct qwz_softc *sc, uint32_t offset, uint32_t value)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	int ret = 0;
	bool wakeup_required;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup the device to access.
	 */
	wakeup_required = test_bit(ATH12K_FLAG_DEVICE_INIT_DONE, sc->sc_flags)
	    && offset >= ATH12K_PCI_ACCESS_ALWAYS_OFF;
	if (wakeup_required && psc->sc_pci_ops->wakeup)
		ret = psc->sc_pci_ops->wakeup(sc);

	if (offset < ATH12K_PCI_WINDOW_START)
		qwz_pci_write(sc, offset, value);
	else
		psc->sc_pci_ops->window_write32(sc, offset, value);

	if (wakeup_required && !ret && psc->sc_pci_ops->release)
		psc->sc_pci_ops->release(sc);
}

void
qwz_pcic_ext_irq_disable(struct qwz_softc *sc)
{
	clear_bit(ATH12K_FLAG_EXT_IRQ_ENABLED, sc->sc_flags);

	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return;

	DPRINTF("%s not implemented\n", __func__);
}

void
qwz_pcic_ext_irq_enable(struct qwz_softc *sc)
{
	set_bit(ATH12K_FLAG_EXT_IRQ_ENABLED, sc->sc_flags);

	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return;

	DPRINTF("%s not implemented\n", __func__);
}

void
qwz_pcic_ce_irq_enable(struct qwz_softc *sc, uint16_t ce_id)
{
	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return;

	/* OpenBSD PCI stack does not yet implement MSI interrupt masking. */
	sc->msi_ce_irqmask |= (1U << ce_id);
}

void
qwz_pcic_ce_irq_disable(struct qwz_softc *sc, uint16_t ce_id)
{
	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return;

	/* OpenBSD PCI stack does not yet implement MSI interrupt masking. */
	sc->msi_ce_irqmask &= ~(1U << ce_id);
}

void
qwz_pcic_ext_grp_disable(struct qwz_ext_irq_grp *irq_grp)
{
	struct qwz_softc *sc = irq_grp->sc;

	/* In case of one MSI vector, we handle irq enable/disable
	 * in a uniform way since we only have one irq
	 */
	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return;
}

int
qwz_pcic_ext_irq_config(struct qwz_softc *sc, struct pci_attach_args *pa)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	int i, ret, num_vectors = 0;
	uint32_t msi_data_start = 0;
	uint32_t base_idx, base_vector = 0;

	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return 0;

	base_idx = ATH12K_PCI_IRQ_CE0_OFFSET + CE_COUNT_MAX;

	ret = qwz_pcic_get_user_msi_vector(sc, "DP", &num_vectors,
	    &msi_data_start, &base_vector);
	if (ret < 0)
		return ret;

	for (i = 0; i < nitems(sc->ext_irq_grp); i++) {
		struct qwz_ext_irq_grp *irq_grp = &sc->ext_irq_grp[i];
		uint32_t num_irq = 0;

		irq_grp->sc = sc;
		irq_grp->grp_id = i;
#if 0
		init_dummy_netdev(&irq_grp->napi_ndev);
		netif_napi_add(&irq_grp->napi_ndev, &irq_grp->napi,
			       ath12k_pcic_ext_grp_napi_poll);
#endif
		if (sc->hw_params.ring_mask->tx[i] ||
		    sc->hw_params.ring_mask->rx[i] ||
		    sc->hw_params.ring_mask->rx_err[i] ||
		    sc->hw_params.ring_mask->rx_wbm_rel[i] ||
		    sc->hw_params.ring_mask->reo_status[i] ||
		    sc->hw_params.ring_mask->host2rxdma[i] ||
		    sc->hw_params.ring_mask->rx_mon_dest[i]) {
			num_irq = 1;
		}

		irq_grp->num_irq = num_irq;
		irq_grp->irqs[0] = base_idx + i;

		if (num_irq) {
			int irq_idx = irq_grp->irqs[0];
			pci_intr_handle_t ih;

			if (pci_intr_map_msivec(pa, irq_idx, &ih) != 0 &&
			    pci_intr_map(pa, &ih) != 0) {
				printf("%s: can't map interrupt\n",
				    sc->sc_dev.dv_xname);
				return EIO;
			}

			snprintf(psc->sc_ivname[irq_idx], sizeof(psc->sc_ivname[0]),
			    "%s:ex%d", sc->sc_dev.dv_xname, i);
			psc->sc_ih[irq_idx] = pci_intr_establish(psc->sc_pc, ih,
			    IPL_NET, qwz_ext_intr, irq_grp, psc->sc_ivname[irq_idx]);
			if (psc->sc_ih[irq_idx] == NULL) {
				printf("%s: failed to request irq %d\n",
				    sc->sc_dev.dv_xname, irq_idx);
				return EIO;
			}
		}

		qwz_pcic_ext_grp_disable(irq_grp);
	}

	return 0;
}

int
qwz_pcic_config_irq(struct qwz_softc *sc, struct pci_attach_args *pa)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	struct qwz_ce_pipe *ce_pipe;
	uint32_t msi_data_start;
	uint32_t msi_data_count, msi_data_idx;
	uint32_t msi_irq_start;
	int i, ret, irq_idx;
	pci_intr_handle_t ih;

	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		return 0;

	ret = qwz_pcic_get_user_msi_vector(sc, "CE", &msi_data_count,
	    &msi_data_start, &msi_irq_start);
	if (ret)
		return ret;

	/* Configure CE irqs */
	for (i = 0, msi_data_idx = 0; i < sc->hw_params.ce_count; i++) {
		if (qwz_ce_get_attr_flags(sc, i) & CE_ATTR_DIS_INTR)
			continue;

		ce_pipe = &sc->ce.ce_pipe[i];
		irq_idx = ATH12K_PCI_IRQ_CE0_OFFSET + i;

		if (pci_intr_map_msivec(pa, irq_idx, &ih) != 0 &&
		    pci_intr_map(pa, &ih) != 0) {
			printf("%s: can't map interrupt\n",
			    sc->sc_dev.dv_xname);
			return EIO;
		}

		snprintf(psc->sc_ivname[irq_idx], sizeof(psc->sc_ivname[0]),
		    "%s:ce%d", sc->sc_dev.dv_xname, ce_pipe->pipe_num);
		psc->sc_ih[irq_idx] = pci_intr_establish(psc->sc_pc, ih,
		    IPL_NET, qwz_ce_intr, ce_pipe, psc->sc_ivname[irq_idx]);
		if (psc->sc_ih[irq_idx] == NULL) {
			printf("%s: failed to request irq %d\n",
			    sc->sc_dev.dv_xname, irq_idx);
			return EIO;
		}

		msi_data_idx++;

		qwz_pcic_ce_irq_disable(sc, i);
	}

	ret = qwz_pcic_ext_irq_config(sc, pa);
	if (ret)
		return ret;

	return 0;
}

void
qwz_pcic_ce_irqs_enable(struct qwz_softc *sc)
{
	int i;

	set_bit(ATH12K_FLAG_CE_IRQ_ENABLED, sc->sc_flags);

	for (i = 0; i < sc->hw_params.ce_count; i++) {
		if (qwz_ce_get_attr_flags(sc, i) & CE_ATTR_DIS_INTR)
			continue;
		qwz_pcic_ce_irq_enable(sc, i);
	}
}

void
qwz_pcic_ce_irqs_disable(struct qwz_softc *sc)
{
	int i;

	clear_bit(ATH12K_FLAG_CE_IRQ_ENABLED, sc->sc_flags);

	for (i = 0; i < sc->hw_params.ce_count; i++) {
		if (qwz_ce_get_attr_flags(sc, i) & CE_ATTR_DIS_INTR)
			continue;
		qwz_pcic_ce_irq_disable(sc, i);
	}
}

int
qwz_pci_start(struct qwz_softc *sc)
{
	/* TODO: for now don't restore ASPM in case of single MSI
	 * vector as MHI register reading in M2 causes system hang.
	 */
	if (test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags))
		qwz_pci_aspm_restore(sc);
	else
		DPRINTF("%s: leaving PCI ASPM disabled to avoid MHI M2 problems"
		    "\n", sc->sc_dev.dv_xname);

	set_bit(ATH12K_FLAG_DEVICE_INIT_DONE, sc->sc_flags);

	qwz_ce_rx_post_buf(sc);
	qwz_pcic_ce_irqs_enable(sc);

	return 0;
}

void
qwz_pcic_ce_irq_disable_sync(struct qwz_softc *sc)
{
	qwz_pcic_ce_irqs_disable(sc);
#if 0
	ath12k_pcic_sync_ce_irqs(ab);
	ath12k_pcic_kill_tasklets(ab);
#endif
}

void
qwz_pci_stop(struct qwz_softc *sc)
{
	qwz_pcic_ce_irq_disable_sync(sc);
	qwz_ce_cleanup_pipes(sc);
}

int
qwz_pci_bus_wake_up(struct qwz_softc *sc)
{
	if (qwz_mhi_wake_db_clear_valid(sc))
		qwz_mhi_device_wake(sc);

	return 0;
}

void
qwz_pci_bus_release(struct qwz_softc *sc)
{
	if (qwz_mhi_wake_db_clear_valid(sc))
		qwz_mhi_device_zzz(sc);
}

uint32_t
qwz_pci_get_window_start(struct qwz_softc *sc, uint32_t offset)
{
	if (!sc->static_window_map)
		return ATH12K_PCI_WINDOW_START;

	if ((offset ^ HAL_SEQ_WCSS_UMAC_OFFSET) < ATH12K_PCI_WINDOW_RANGE_MASK)
		/* if offset lies within DP register range, use 3rd window */
		return 3 * ATH12K_PCI_WINDOW_START;
	else if ((offset ^ HAL_SEQ_WCSS_UMAC_CE0_SRC_REG) <
		 ATH12K_PCI_WINDOW_RANGE_MASK)
		 /* if offset lies within CE register range, use 2nd window */
		return 2 * ATH12K_PCI_WINDOW_START;
	else
		return ATH12K_PCI_WINDOW_START;
}

void
qwz_pci_select_window(struct qwz_softc *sc, uint32_t offset)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	uint32_t window = FIELD_GET(ATH12K_PCI_WINDOW_VALUE_MASK, offset);

#if notyet
	lockdep_assert_held(&ab_pci->window_lock);
#endif

	/*
	 * Preserve the static window configuration and reset only
	 * dynamic window.
	 */
	window |= psc->register_window & ATH12K_PCI_WINDOW_STATIC_MASK;

	if (window != psc->register_window) {
		qwz_pci_write(sc, ATH12K_PCI_WINDOW_REG_ADDRESS,
		    ATH12K_PCI_WINDOW_ENABLE_BIT | window);
		(void) qwz_pci_read(sc, ATH12K_PCI_WINDOW_REG_ADDRESS);
		psc->register_window = window;
	}
}

static inline bool
qwz_pci_is_offset_within_mhi_region(uint32_t offset)
{
	return (offset >= PCI_MHIREGLEN_REG && offset <= PCI_MHI_REGION_END);
}

void
qwz_pci_window_write32(struct qwz_softc *sc, uint32_t offset, uint32_t value)
{
	uint32_t window_start;

	window_start = qwz_pci_get_window_start(sc, offset);

	if (window_start == ATH12K_PCI_WINDOW_START) {
#if notyet
		spin_lock_bh(&ab_pci->window_lock);
#endif
		qwz_pci_select_window(sc, offset);

		if (qwz_pci_is_offset_within_mhi_region(offset)) {
			offset = offset - PCI_MHIREGLEN_REG;
			qwz_pci_write(sc, offset & ATH12K_PCI_WINDOW_RANGE_MASK,
			    value);
		} else {
			qwz_pci_write(sc, window_start +
			    (offset & ATH12K_PCI_WINDOW_RANGE_MASK), value);
		}
#if notyet
		spin_unlock_bh(&ab_pci->window_lock);
#endif
	} else {
		qwz_pci_write(sc, window_start +
		    (offset & ATH12K_PCI_WINDOW_RANGE_MASK), value);
	}
}

uint32_t
qwz_pci_window_read32(struct qwz_softc *sc, uint32_t offset)
{
	uint32_t window_start, val;

	window_start = qwz_pci_get_window_start(sc, offset);

	if (window_start == ATH12K_PCI_WINDOW_START) {
#if notyet
		spin_lock_bh(&ab_pci->window_lock);
#endif
		qwz_pci_select_window(sc, offset);

		if (qwz_pci_is_offset_within_mhi_region(offset)) {
			offset = offset - PCI_MHIREGLEN_REG;
			val = qwz_pci_read(sc,
			    offset & ATH12K_PCI_WINDOW_RANGE_MASK);
		} else {
			val = qwz_pci_read(sc, window_start +
			    (offset & ATH12K_PCI_WINDOW_RANGE_MASK));
		}
#if notyet
		spin_unlock_bh(&ab_pci->window_lock);
#endif
	} else {
		val = qwz_pci_read(sc, window_start +
		    (offset & ATH12K_PCI_WINDOW_RANGE_MASK));
	}

	return val;
}

void
qwz_pci_select_static_window(struct qwz_softc *sc)
{
	uint32_t umac_window;
	uint32_t ce_window;
	uint32_t window;

	umac_window = FIELD_GET(ATH12K_PCI_WINDOW_VALUE_MASK, HAL_SEQ_WCSS_UMAC_OFFSET);
	ce_window = FIELD_GET(ATH12K_PCI_WINDOW_VALUE_MASK, HAL_CE_WFSS_CE_REG_BASE);
	window = (umac_window << 12) | (ce_window << 6);

	qwz_pci_write(sc, ATH12K_PCI_WINDOW_REG_ADDRESS,
	    ATH12K_PCI_WINDOW_ENABLE_BIT | window);
}

void
qwz_pci_soc_global_reset(struct qwz_softc *sc)
{
	uint32_t val, msecs;

	val = qwz_pcic_read32(sc, PCIE_SOC_GLOBAL_RESET);

	val |= PCIE_SOC_GLOBAL_RESET_V;

	qwz_pcic_write32(sc, PCIE_SOC_GLOBAL_RESET, val);

	/* TODO: exact time to sleep is uncertain */
	msecs = 10;
	DELAY(msecs * 1000);

	/* Need to toggle V bit back otherwise stuck in reset status */
	val &= ~PCIE_SOC_GLOBAL_RESET_V;

	qwz_pcic_write32(sc, PCIE_SOC_GLOBAL_RESET, val);

	DELAY(msecs * 1000);

	val = qwz_pcic_read32(sc, PCIE_SOC_GLOBAL_RESET);
	if (val == 0xffffffff)
		printf("%s: link down error during global reset\n",
		    sc->sc_dev.dv_xname);
}

void
qwz_pci_clear_dbg_registers(struct qwz_softc *sc)
{
	uint32_t val;

	/* read cookie */
	val = qwz_pcic_read32(sc, PCIE_Q6_COOKIE_ADDR);
	DPRINTF("%s: cookie:0x%x\n", sc->sc_dev.dv_xname, val);

	val = qwz_pcic_read32(sc, WLAON_WARM_SW_ENTRY);
	DPRINTF("%s: WLAON_WARM_SW_ENTRY 0x%x\n", sc->sc_dev.dv_xname, val);

	/* TODO: exact time to sleep is uncertain */
	DELAY(10 * 1000);

	/* write 0 to WLAON_WARM_SW_ENTRY to prevent Q6 from
	 * continuing warm path and entering dead loop.
	 */
	qwz_pcic_write32(sc, WLAON_WARM_SW_ENTRY, 0);
	DELAY(10 * 1000);

	val = qwz_pcic_read32(sc, WLAON_WARM_SW_ENTRY);
	DPRINTF("%s: WLAON_WARM_SW_ENTRY 0x%x\n", sc->sc_dev.dv_xname, val);

	/* A read clear register. clear the register to prevent
	 * Q6 from entering wrong code path.
	 */
	val = qwz_pcic_read32(sc, WLAON_SOC_RESET_CAUSE_REG);
	DPRINTF("%s: soc reset cause:%d\n", sc->sc_dev.dv_xname, val);
}

int
qwz_pci_set_link_reg(struct qwz_softc *sc, uint32_t offset, uint32_t value,
    uint32_t mask)
{
	uint32_t v;
	int i;

	v = qwz_pcic_read32(sc, offset);
	if ((v & mask) == value)
		return 0;

	for (i = 0; i < 10; i++) {
		qwz_pcic_write32(sc, offset, (v & ~mask) | value);

		v = qwz_pcic_read32(sc, offset);
		if ((v & mask) == value)
			return 0;

		delay((2 * 1000));
	}

	DPRINTF("failed to set pcie link register 0x%08x: 0x%08x != 0x%08x\n",
	    offset, v & mask, value);

	return ETIMEDOUT;
}

int
qwz_pci_fix_l1ss(struct qwz_softc *sc)
{
	int ret;

	ret = qwz_pci_set_link_reg(sc,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG(sc),
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK);
	if (ret) {
		DPRINTF("failed to set sysclk: %d\n", ret);
		return ret;
	}

	ret = qwz_pci_set_link_reg(sc,
				      PCIE_PCS_OSC_DTCT_CONFIG1_REG(sc),
				      PCIE_PCS_OSC_DTCT_CONFIG1_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		DPRINTF("failed to set dtct config1 error: %d\n", ret);
		return ret;
	}

	ret = qwz_pci_set_link_reg(sc,
				      PCIE_PCS_OSC_DTCT_CONFIG2_REG(sc),
				      PCIE_PCS_OSC_DTCT_CONFIG2_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		DPRINTF("failed to set dtct config2: %d\n", ret);
		return ret;
	}

	ret = qwz_pci_set_link_reg(sc,
				      PCIE_PCS_OSC_DTCT_CONFIG4_REG(sc),
				      PCIE_PCS_OSC_DTCT_CONFIG4_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		DPRINTF("failed to set dtct config4: %d\n", ret);
		return ret;
	}

	return 0;
}

void
qwz_pci_enable_ltssm(struct qwz_softc *sc)
{
	uint32_t val;
	int i;

	val = qwz_pcic_read32(sc, PCIE_PCIE_PARF_LTSSM);

	/* PCIE link seems very unstable after the Hot Reset*/
	for (i = 0; val != PARM_LTSSM_VALUE && i < 5; i++) {
		if (val == 0xffffffff)
			DELAY(5 * 1000);

		qwz_pcic_write32(sc, PCIE_PCIE_PARF_LTSSM, PARM_LTSSM_VALUE);
		val = qwz_pcic_read32(sc, PCIE_PCIE_PARF_LTSSM);
	}

	DPRINTF("%s: pci ltssm 0x%x\n", sc->sc_dev.dv_xname, val);

	val = qwz_pcic_read32(sc, GCC_GCC_PCIE_HOT_RST);
	val |= GCC_GCC_PCIE_HOT_RST_VAL;
	qwz_pcic_write32(sc, GCC_GCC_PCIE_HOT_RST, val);
	val = qwz_pcic_read32(sc, GCC_GCC_PCIE_HOT_RST);

	DPRINTF("%s: pci pcie_hot_rst 0x%x\n", sc->sc_dev.dv_xname, val);

	DELAY(5 * 1000);
}

void
qwz_pci_clear_all_intrs(struct qwz_softc *sc)
{
	/* This is a WAR for PCIE Hotreset.
	 * When target receive Hotreset, but will set the interrupt.
	 * So when download SBL again, SBL will open Interrupt and
	 * receive it, and crash immediately.
	 */
	qwz_pcic_write32(sc, PCIE_PCIE_INT_ALL_CLEAR, PCIE_INT_CLEAR_ALL);
}

void
qwz_pci_set_wlaon_pwr_ctrl(struct qwz_softc *sc)
{
	uint32_t val;

	val = qwz_pcic_read32(sc, WLAON_QFPROM_PWR_CTRL_REG);
	val &= ~QFPROM_PWR_CTRL_VDD4BLOW_MASK;
	qwz_pcic_write32(sc, WLAON_QFPROM_PWR_CTRL_REG, val);
}

void
qwz_pci_force_wake(struct qwz_softc *sc)
{
	qwz_pcic_write32(sc, PCIE_SOC_WAKE_PCIE_LOCAL_REG, 1);
	DELAY(5 * 1000);
}

void
qwz_pci_sw_reset(struct qwz_softc *sc, bool power_on)
{
	DELAY(100 * 1000); /* msecs */

	if (power_on) {
		qwz_pci_enable_ltssm(sc);
		qwz_pci_clear_all_intrs(sc);
		qwz_pci_set_wlaon_pwr_ctrl(sc);
		if (sc->hw_params.fix_l1ss)
			qwz_pci_fix_l1ss(sc);
	}

	qwz_mhi_clear_vector(sc);
	qwz_pci_clear_dbg_registers(sc);
	qwz_pci_soc_global_reset(sc);
	qwz_mhi_reset_device(sc, 0);
}

void
qwz_pci_msi_config(struct qwz_softc *sc, bool enable)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	uint32_t val;

	val = pci_conf_read(psc->sc_pc, psc->sc_tag,
	    psc->sc_msi_off + PCI_MSI_MC);

	if (enable)
		val |= PCI_MSI_MC_MSIE;
	else
		val &= ~PCI_MSI_MC_MSIE;

	pci_conf_write(psc->sc_pc, psc->sc_tag, psc->sc_msi_off + PCI_MSI_MC,
	    val);
}

void
qwz_pci_msi_enable(struct qwz_softc *sc)
{
	qwz_pci_msi_config(sc, true);
}

void
qwz_pci_msi_disable(struct qwz_softc *sc)
{
	qwz_pci_msi_config(sc, false);
}

void
qwz_pci_aspm_disable(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	psc->sc_lcsr = pci_conf_read(psc->sc_pc, psc->sc_tag,
	    psc->sc_cap_off + PCI_PCIE_LCSR);

	DPRINTF("%s: pci link_ctl 0x%04x L0s %d L1 %d\n", sc->sc_dev.dv_xname,
	    (uint16_t)psc->sc_lcsr, (psc->sc_lcsr & PCI_PCIE_LCSR_ASPM_L0S),
	    (psc->sc_lcsr & PCI_PCIE_LCSR_ASPM_L1));

	/* disable L0s and L1 */
	pci_conf_write(psc->sc_pc, psc->sc_tag, psc->sc_cap_off + PCI_PCIE_LCSR,
	    psc->sc_lcsr & ~(PCI_PCIE_LCSR_ASPM_L0S | PCI_PCIE_LCSR_ASPM_L1));

	psc->sc_flags |= ATH12K_PCI_ASPM_RESTORE;
}

void
qwz_pci_aspm_restore(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	if (psc->sc_flags & ATH12K_PCI_ASPM_RESTORE) {
		pci_conf_write(psc->sc_pc, psc->sc_tag,
		    psc->sc_cap_off + PCI_PCIE_LCSR, psc->sc_lcsr);
		psc->sc_flags &= ~ATH12K_PCI_ASPM_RESTORE;
	}
}

int
qwz_pci_power_up(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	int error;

	psc->register_window = 0;
	clear_bit(ATH12K_FLAG_DEVICE_INIT_DONE, sc->sc_flags);

	qwz_pci_sw_reset(sc, true);

	/* Disable ASPM during firmware download due to problems switching
	 * to AMSS state.
	 */
	qwz_pci_aspm_disable(sc);

	qwz_pci_msi_enable(sc);

	error = qwz_mhi_start(psc);
	if (error)
		return error;

	if (sc->static_window_map)
		qwz_pci_select_static_window(sc);

	return 0;
}

void
qwz_pci_power_down(struct qwz_softc *sc)
{
	/* restore aspm in case firmware bootup fails */
	qwz_pci_aspm_restore(sc);

	qwz_pci_force_wake(sc);

	qwz_pci_msi_disable(sc);

	qwz_mhi_stop(sc);
	clear_bit(ATH12K_FLAG_DEVICE_INIT_DONE, sc->sc_flags);
	qwz_pci_sw_reset(sc, false);
}

/*
 * MHI
 */
int
qwz_mhi_register(struct qwz_softc *sc)
{
	DNPRINTF(QWZ_D_MHI, "%s: STUB %s()\n", sc->sc_dev.dv_xname, __func__);
	return 0;
}

void
qwz_mhi_unregister(struct qwz_softc *sc)
{
	DNPRINTF(QWZ_D_MHI, "%s: STUB %s()\n", sc->sc_dev.dv_xname, __func__);
}

// XXX MHI is GPLd - we provide a compatible bare-bones implementation
#define MHI_CFG				0x10
#define   MHI_CFG_NHWER_MASK		GENMASK(31, 24)
#define   MHI_CFG_NHWER_SHFT		24
#define   MHI_CFG_NER_MASK		GENMASK(23, 16)
#define   MHI_CFG_NER_SHFT		16
#define   MHI_CFG_NHWCH_MASK		GENMASK(15, 8)
#define   MHI_CFG_NHWCH_SHFT		8
#define   MHI_CFG_NCH_MASK		GENMASK(7, 0)
#define MHI_CHDBOFF			0x18
#define MHI_DEV_WAKE_DB			127
#define MHI_ERDBOFF			0x20
#define MHI_BHI_OFFSET			0x28
#define   MHI_BHI_IMGADDR_LOW			0x08
#define   MHI_BHI_IMGADDR_HIGH			0x0c
#define   MHI_BHI_IMGSIZE			0x10
#define   MHI_BHI_IMGTXDB			0x18
#define   MHI_BHI_INTVEC			0x20
#define   MHI_BHI_EXECENV			0x28
#define   MHI_BHI_STATUS			0x2c
#define	  MHI_BHI_SERIALNU			0x40
#define MHI_BHIE_OFFSET			0x2c
#define   MHI_BHIE_TXVECADDR_LOW_OFFS		0x2c
#define   MHI_BHIE_TXVECADDR_HIGH_OFFS		0x30
#define   MHI_BHIE_TXVECSIZE_OFFS		0x34
#define   MHI_BHIE_TXVECDB_OFFS			0x3c
#define   MHI_BHIE_TXVECSTATUS_OFFS		0x44
#define   MHI_BHIE_RXVECADDR_LOW_OFFS		0x60
#define   MHI_BHIE_RXVECSTATUS_OFFS		0x78
#define MHI_CTRL			0x38
#define    MHI_CTRL_READY_MASK			0x1
#define    MHI_CTRL_RESET_MASK			0x2
#define    MHI_CTRL_MHISTATE_MASK		GENMASK(15, 8)
#define    MHI_CTRL_MHISTATE_SHFT		8
#define MHI_STATUS			0x48
#define    MHI_STATUS_MHISTATE_MASK		GENMASK(15, 8)
#define    MHI_STATUS_MHISTATE_SHFT		8
#define        MHI_STATE_RESET			0x0
#define        MHI_STATE_READY			0x1
#define        MHI_STATE_M0			0x2
#define        MHI_STATE_M1			0x3
#define        MHI_STATE_M2			0x4
#define        MHI_STATE_M3			0x5
#define        MHI_STATE_M3_FAST		0x6
#define        MHI_STATE_BHI			0x7
#define        MHI_STATE_SYS_ERR		0xff
#define    MHI_STATUS_READY_MASK		0x1
#define    MHI_STATUS_SYSERR_MASK		0x4
#define MHI_CCABAP_LOWER		0x58
#define MHI_CCABAP_HIGHER		0x5c
#define MHI_ECABAP_LOWER		0x60
#define MHI_ECABAP_HIGHER		0x64
#define MHI_CRCBAP_LOWER		0x68
#define MHI_CRCBAP_HIGHER		0x6c
#define MHI_CRDB_LOWER			0x70
#define MHI_CRDB_HIGHER			0x74
#define MHI_CTRLBASE_LOWER		0x80
#define MHI_CTRLBASE_HIGHER		0x84
#define MHI_CTRLLIMIT_LOWER		0x88
#define MHI_CTRLLIMIT_HIGHER		0x8c
#define MHI_DATABASE_LOWER		0x98
#define MHI_DATABASE_HIGHER		0x9c
#define MHI_DATALIMIT_LOWER		0xa0
#define MHI_DATALIMIT_HIGHER		0xa4

#define MHI_EE_PBL	0x0	/* Primary Bootloader */
#define MHI_EE_SBL	0x1	/* Secondary Bootloader */
#define MHI_EE_AMSS	0x2	/* Modem, aka the primary runtime EE */
#define MHI_EE_RDDM	0x3	/* Ram dump download mode */
#define MHI_EE_WFW	0x4	/* WLAN firmware mode */
#define MHI_EE_PTHRU	0x5	/* Passthrough */
#define MHI_EE_EDL	0x6	/* Embedded downloader */
#define MHI_EE_FP	0x7	/* Flash Programmer Environment */

#define MHI_IN_PBL(e) (e == MHI_EE_PBL || e == MHI_EE_PTHRU || e == MHI_EE_EDL)
#define MHI_POWER_UP_CAPABLE(e) (MHI_IN_PBL(e) || e == MHI_EE_AMSS)
#define MHI_IN_MISSION_MODE(e) \
	(e == MHI_EE_AMSS || e == MHI_EE_WFW || e == MHI_EE_FP)

/* BHI register bits */
#define MHI_BHI_TXDB_SEQNUM_BMSK	GENMASK(29, 0)
#define MHI_BHI_TXDB_SEQNUM_SHFT	0
#define MHI_BHI_STATUS_MASK		GENMASK(31, 30)
#define MHI_BHI_STATUS_SHFT		30
#define MHI_BHI_STATUS_ERROR		0x03
#define MHI_BHI_STATUS_SUCCESS		0x02
#define MHI_BHI_STATUS_RESET		0x00

/* MHI BHIE registers */
#define MHI_BHIE_MSMSOCID_OFFS		0x00
#define MHI_BHIE_RXVECADDR_LOW_OFFS	0x60
#define MHI_BHIE_RXVECADDR_HIGH_OFFS	0x64
#define MHI_BHIE_RXVECSIZE_OFFS		0x68
#define MHI_BHIE_RXVECDB_OFFS		0x70
#define MHI_BHIE_RXVECSTATUS_OFFS	0x78

/* BHIE register bits */
#define MHI_BHIE_TXVECDB_SEQNUM_BMSK		GENMASK(29, 0)
#define MHI_BHIE_TXVECDB_SEQNUM_SHFT		0
#define MHI_BHIE_TXVECSTATUS_SEQNUM_BMSK	GENMASK(29, 0)
#define MHI_BHIE_TXVECSTATUS_SEQNUM_SHFT	0
#define MHI_BHIE_TXVECSTATUS_STATUS_BMSK	GENMASK(31, 30)
#define MHI_BHIE_TXVECSTATUS_STATUS_SHFT	30
#define MHI_BHIE_TXVECSTATUS_STATUS_RESET	0x00
#define MHI_BHIE_TXVECSTATUS_STATUS_XFER_COMPL	0x02
#define MHI_BHIE_TXVECSTATUS_STATUS_ERROR	0x03
#define MHI_BHIE_RXVECDB_SEQNUM_BMSK		GENMASK(29, 0)
#define MHI_BHIE_RXVECDB_SEQNUM_SHFT		0
#define MHI_BHIE_RXVECSTATUS_SEQNUM_BMSK	GENMASK(29, 0)
#define MHI_BHIE_RXVECSTATUS_SEQNUM_SHFT	0
#define MHI_BHIE_RXVECSTATUS_STATUS_BMSK	GENMASK(31, 30)
#define MHI_BHIE_RXVECSTATUS_STATUS_SHFT	30
#define MHI_BHIE_RXVECSTATUS_STATUS_RESET	0x00
#define MHI_BHIE_RXVECSTATUS_STATUS_XFER_COMPL	0x02
#define MHI_BHIE_RXVECSTATUS_STATUS_ERROR	0x03

#define MHI_EV_CC_INVALID	0x0
#define MHI_EV_CC_SUCCESS	0x1
#define MHI_EV_CC_EOT		0x2
#define MHI_EV_CC_OVERFLOW	0x3
#define MHI_EV_CC_EOB		0x4
#define MHI_EV_CC_OOB		0x5
#define MHI_EV_CC_DB_MODE	0x6
#define MHI_EV_CC_UNDEFINED_ERR	0x10
#define MHI_EV_CC_BAD_TRE	0x11

#define MHI_CMD_NOP		01
#define MHI_CMD_RESET_CHAN	16
#define MHI_CMD_STOP_CHAN	17
#define MHI_CMD_START_CHAN	18

#define MHI_TRE_CMD_CHID_MASK	GENMASK(31, 24)
#define MHI_TRE_CMD_CHID_SHFT	24
#define MHI_TRE_CMD_CMDID_MASK	GENMASK(23, 16)
#define MHI_TRE_CMD_CMDID_SHFT	16

#define MHI_TRE0_EV_LEN_MASK	GENMASK(15, 0)
#define MHI_TRE0_EV_LEN_SHFT	0
#define MHI_TRE0_EV_CODE_MASK	GENMASK(31, 24)
#define MHI_TRE0_EV_CODE_SHFT	24
#define MHI_TRE1_EV_TYPE_MASK	GENMASK(23, 16)
#define MHI_TRE1_EV_TYPE_SHFT	16
#define MHI_TRE1_EV_CHID_MASK	GENMASK(31, 24)
#define MHI_TRE1_EV_CHID_SHFT	24

#define MHI_TRE0_DATA_LEN_MASK	GENMASK(15, 0)
#define MHI_TRE0_DATA_LEN_SHFT	0
#define MHI_TRE1_DATA_CHAIN	(1 << 0)
#define MHI_TRE1_DATA_IEOB	(1 << 8)
#define MHI_TRE1_DATA_IEOT	(1 << 9)
#define MHI_TRE1_DATA_BEI	(1 << 10)
#define MHI_TRE1_DATA_TYPE_MASK		GENMASK(23, 16)
#define MHI_TRE1_DATA_TYPE_SHIFT	16
#define MHI_TRE1_DATA_TYPE_TRANSFER	0x2

#define MHI_PKT_TYPE_INVALID			0x00
#define MHI_PKT_TYPE_NOOP_CMD			0x01
#define MHI_PKT_TYPE_TRANSFER			0x02
#define MHI_PKT_TYPE_COALESCING			0x08
#define MHI_PKT_TYPE_RESET_CHAN_CMD		0x10
#define MHI_PKT_TYPE_STOP_CHAN_CMD		0x11
#define MHI_PKT_TYPE_START_CHAN_CMD		0x12
#define MHI_PKT_TYPE_STATE_CHANGE_EVENT		0x20
#define MHI_PKT_TYPE_CMD_COMPLETION_EVENT	0x21
#define MHI_PKT_TYPE_TX_EVENT			0x22
#define MHI_PKT_TYPE_RSC_TX_EVENT		0x28
#define MHI_PKT_TYPE_EE_EVENT			0x40
#define MHI_PKT_TYPE_TSYNC_EVENT		0x48
#define MHI_PKT_TYPE_BW_REQ_EVENT		0x50


#define MHI_DMA_VEC_CHUNK_SIZE			524288 /* 512 KB */
struct qwz_dma_vec_entry {
	uint64_t paddr;
	uint64_t size;
};

void
qwz_mhi_ring_doorbell(struct qwz_softc *sc, uint64_t db_addr, uint64_t val)
{
	qwz_pci_write(sc, db_addr + 4, val >> 32);
	qwz_pci_write(sc, db_addr, val & 0xffffffff);
}

void
qwz_mhi_device_wake(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	/*
	 * Device wake is async only for now because we do not
	 * keep track of PM state in software.
	 */
	qwz_mhi_ring_doorbell(sc, psc->wake_db, 1);
}

void
qwz_mhi_device_zzz(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	qwz_mhi_ring_doorbell(sc, psc->wake_db, 0);
}

int
qwz_mhi_wake_db_clear_valid(struct qwz_softc *sc)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;

	return (psc->mhi_state == MHI_STATE_M0); /* TODO other states? */
}

void
qwz_mhi_init_xfer_rings(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int i;
	uint32_t chcfg;
	struct qwz_pci_xfer_ring *ring;
	struct qwz_mhi_chan_ctxt *cbase, *c;

	cbase = (struct qwz_mhi_chan_ctxt *)QWZ_DMA_KVA(psc->chan_ctxt);
	for (i = 0; i < psc->max_chan; i++) {
		c = &cbase[i];
		chcfg = le32toh(c->chcfg);
		chcfg &= ~(MHI_CHAN_CTX_CHSTATE_MASK |
		    MHI_CHAN_CTX_BRSTMODE_MASK |
		    MHI_CHAN_CTX_POLLCFG_MASK);
		chcfg |= (MHI_CHAN_CTX_CHSTATE_DISABLED |
		    (MHI_CHAN_CTX_BRSTMODE_DISABLE <<
		    MHI_CHAN_CTX_BRSTMODE_SHFT));
		c->chcfg = htole32(chcfg);
		c->chtype = htole32(MHI_CHAN_TYPE_INVALID);
		c->erindex = 0;
	}

	for (i = 0; i < nitems(psc->xfer_rings); i++) {
		ring = &psc->xfer_rings[i];
		KASSERT(ring->mhi_chan_id < psc->max_chan);
		c = &cbase[ring->mhi_chan_id];
		c->chtype = htole32(ring->mhi_chan_direction);
		c->erindex = htole32(ring->mhi_chan_event_ring_index);
		ring->chan_ctxt = c;
	}

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->chan_ctxt), 0,
	    QWZ_DMA_LEN(psc->chan_ctxt), BUS_DMASYNC_PREWRITE);
}

void
qwz_mhi_init_event_rings(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int i;
	uint32_t intmod;
	uint64_t paddr, len;
	struct qwz_pci_event_ring *ring;
	struct qwz_mhi_event_ctxt *c;

	c = (struct qwz_mhi_event_ctxt *)QWZ_DMA_KVA(psc->event_ctxt);
	for (i = 0; i < nitems(psc->event_rings); i++, c++) {
		ring = &psc->event_rings[i];

		ring->event_ctxt = c;

		intmod = le32toh(c->intmod);
		intmod &= ~(MHI_EV_CTX_INTMODC_MASK | MHI_EV_CTX_INTMODT_MASK);
		intmod |= (ring->mhi_er_irq_moderation_ms <<
		    MHI_EV_CTX_INTMODT_SHFT) & MHI_EV_CTX_INTMODT_MASK;
		c->intmod = htole32(intmod);

		c->ertype = htole32(MHI_ER_TYPE_VALID);
		c->msivec = htole32(ring->mhi_er_irq);

		paddr = QWZ_DMA_DVA(ring->dmamem);
		ring->rp = paddr;
		ring->wp = paddr + ring->size -
		    sizeof(struct qwz_mhi_ring_element);
		c->rbase = htole64(paddr);
		c->rp = htole64(ring->rp);
		c->wp = htole64(ring->wp);

		len = sizeof(struct qwz_mhi_ring_element) * ring->num_elements;
		c->rlen = htole64(len);
	}

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->event_ctxt), 0,
	    QWZ_DMA_LEN(psc->event_ctxt), BUS_DMASYNC_PREWRITE);
}

void
qwz_mhi_init_cmd_ring(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_pci_cmd_ring *ring = &psc->cmd_ring;
	struct qwz_mhi_cmd_ctxt *c;
	uint64_t paddr, len;

	paddr = QWZ_DMA_DVA(ring->dmamem);
	len = ring->size;

	ring->rp = ring->wp = paddr;

	c = (struct qwz_mhi_cmd_ctxt *)QWZ_DMA_KVA(psc->cmd_ctxt);
	c->rbase = htole64(paddr);
	c->rp = htole64(paddr);
	c->wp = htole64(paddr);
	c->rlen = htole64(len);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->cmd_ctxt), 0,
	    QWZ_DMA_LEN(psc->cmd_ctxt), BUS_DMASYNC_PREWRITE);
}

void
qwz_mhi_init_dev_ctxt(struct qwz_pci_softc *psc)
{
	qwz_mhi_init_xfer_rings(psc);
	qwz_mhi_init_event_rings(psc);
	qwz_mhi_init_cmd_ring(psc);
}

void *
qwz_pci_cmd_ring_get_elem(struct qwz_pci_cmd_ring *ring, uint64_t ptr)
{
	uint64_t base = QWZ_DMA_DVA(ring->dmamem), offset;

	if (ptr < base || ptr >= base + ring->size)
		return NULL;

	offset = ptr - base;
	if (offset >= ring->size)
		return NULL;

	return QWZ_DMA_KVA(ring->dmamem) + offset;
}

int
qwz_mhi_cmd_ring_submit(struct qwz_pci_softc *psc,
    struct qwz_pci_cmd_ring *ring)
{
	struct qwz_softc *sc = &psc->sc_sc;
	uint64_t base = QWZ_DMA_DVA(ring->dmamem);
	struct qwz_mhi_cmd_ctxt *c;

	if (ring->queued >= ring->num_elements)
		return 1;

	if (ring->wp + sizeof(struct qwz_mhi_ring_element) >= base + ring->size)
		ring->wp = base;
	else
		ring->wp += sizeof(struct qwz_mhi_ring_element);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->cmd_ctxt), 0,
	    QWZ_DMA_LEN(psc->cmd_ctxt), BUS_DMASYNC_POSTREAD);

	c = (struct qwz_mhi_cmd_ctxt *)QWZ_DMA_KVA(psc->cmd_ctxt);
	c->wp = htole64(ring->wp);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->cmd_ctxt), 0,
	    QWZ_DMA_LEN(psc->cmd_ctxt), BUS_DMASYNC_PREWRITE);

	ring->queued++;
	qwz_mhi_ring_doorbell(sc, MHI_CRDB_LOWER, ring->wp);
	return 0;
}

int
qwz_mhi_send_cmd(struct qwz_pci_softc *psc, uint32_t cmd, uint32_t chan)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_pci_cmd_ring	*ring = &psc->cmd_ring;
	struct qwz_mhi_ring_element *e;

	if (ring->queued >= ring->num_elements) {
		printf("%s: command ring overflow\n", sc->sc_dev.dv_xname);
		return 1;
	}

	e = qwz_pci_cmd_ring_get_elem(ring, ring->wp);
	if (e == NULL)
		return 1;

	e->ptr = 0ULL;
	e->dword[0] = 0;
	e->dword[1] = htole32(
	    ((chan << MHI_TRE_CMD_CHID_SHFT) & MHI_TRE_CMD_CHID_MASK) |
	    ((cmd << MHI_TRE_CMD_CMDID_SHFT) & MHI_TRE_CMD_CMDID_MASK));

	return qwz_mhi_cmd_ring_submit(psc, ring);
}

void *
qwz_pci_xfer_ring_get_elem(struct qwz_pci_xfer_ring *ring, uint64_t wp)
{
	uint64_t base = QWZ_DMA_DVA(ring->dmamem), offset;
	void *addr = QWZ_DMA_KVA(ring->dmamem);

	if (wp < base)
		return NULL;

	offset = wp - base;
	if (offset >= ring->size)
		return NULL;

	return addr + offset;
}

struct qwz_xfer_data *
qwz_pci_xfer_ring_get_data(struct qwz_pci_xfer_ring *ring, uint64_t wp)
{
	uint64_t base = QWZ_DMA_DVA(ring->dmamem), offset;

	if (wp < base)
		return NULL;

	offset = wp - base;
	if (offset >= ring->size)
		return NULL;

	return &ring->data[offset / sizeof(ring->data[0])];
}

int
qwz_mhi_submit_xfer(struct qwz_softc *sc, struct mbuf *m)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	struct qwz_pci_xfer_ring *ring;
	struct qwz_mhi_ring_element *e;
	struct qwz_xfer_data *xfer;
	uint64_t paddr, base;
	int err;

	ring = &psc->xfer_rings[QWZ_PCI_XFER_RING_IPCR_OUTBOUND];

	if (ring->queued >= ring->num_elements)
		return 1;

	if (m->m_pkthdr.len > QWZ_PCI_XFER_MAX_DATA_SIZE) {
		/* TODO: chunk xfers */
		printf("%s: xfer too large: %d bytes\n", __func__, m->m_pkthdr.len);
		return 1;

	}

	e = qwz_pci_xfer_ring_get_elem(ring, ring->wp);
	if (e == NULL)
		return 1;

	xfer = qwz_pci_xfer_ring_get_data(ring, ring->wp);
	if (xfer == NULL || xfer->m != NULL)
		return 1;

	err = bus_dmamap_load_mbuf(sc->sc_dmat, xfer->map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (err && err != EFBIG) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, err);
		return err;
	}
	if (err) {
		/* Too many DMA segments, linearize mbuf. */
		if (m_defrag(m, M_DONTWAIT))
			return ENOBUFS;
		err = bus_dmamap_load_mbuf(sc->sc_dmat, xfer->map, m,
		    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (err) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, err);
			return err;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, xfer->map, 0, m->m_pkthdr.len,
	    BUS_DMASYNC_PREWRITE);

	xfer->m = m;
	paddr = xfer->map->dm_segs[0].ds_addr;

	e->ptr = htole64(paddr);
	e->dword[0] = htole32((m->m_pkthdr.len << MHI_TRE0_DATA_LEN_SHFT) &
	    MHI_TRE0_DATA_LEN_MASK);
	e->dword[1] = htole32(MHI_TRE1_DATA_IEOT |
	    MHI_TRE1_DATA_TYPE_TRANSFER << MHI_TRE1_DATA_TYPE_SHIFT);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(ring->dmamem),
	    0, QWZ_DMA_LEN(ring->dmamem), BUS_DMASYNC_PREWRITE);

	base = QWZ_DMA_DVA(ring->dmamem);
	if (ring->wp + sizeof(struct qwz_mhi_ring_element) >= base + ring->size)
		ring->wp = base;
	else
		ring->wp += sizeof(struct qwz_mhi_ring_element);
	ring->queued++;

	ring->chan_ctxt->wp = htole64(ring->wp);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->chan_ctxt), 0,
	    QWZ_DMA_LEN(psc->chan_ctxt), BUS_DMASYNC_PREWRITE);

	qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	return 0;
}

int
qwz_mhi_start_channel(struct qwz_pci_softc *psc,
	struct qwz_pci_xfer_ring *ring)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_mhi_chan_ctxt *c;
	int ret = 0;
	uint32_t chcfg;
	uint64_t paddr, len;

	DNPRINTF(QWZ_D_MHI, "%s: start MHI channel %d in state %d\n", __func__,
	    ring->mhi_chan_id, ring->mhi_chan_state);

	c = ring->chan_ctxt;

	chcfg = le32toh(c->chcfg);
	chcfg &= ~MHI_CHAN_CTX_CHSTATE_MASK;
	chcfg |= MHI_CHAN_CTX_CHSTATE_ENABLED;
	c->chcfg = htole32(chcfg);

	paddr = QWZ_DMA_DVA(ring->dmamem);
	ring->rp = ring->wp = paddr;
	c->rbase = htole64(paddr);
	c->rp = htole64(ring->rp);
	c->wp = htole64(ring->wp);
	len = sizeof(struct qwz_mhi_ring_element) * ring->num_elements;
	c->rlen = htole64(len);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->chan_ctxt), 0,
	    QWZ_DMA_LEN(psc->chan_ctxt), BUS_DMASYNC_PREWRITE);

	ring->cmd_status = MHI_EV_CC_INVALID;
	if (qwz_mhi_send_cmd(psc, MHI_CMD_START_CHAN, ring->mhi_chan_id))
		return 1;

	while (ring->cmd_status != MHI_EV_CC_SUCCESS) {
		ret = tsleep_nsec(&ring->cmd_status, 0, "qwzcmd",
		    SEC_TO_NSEC(5));
		if (ret)
			break;
	}

	if (ret) {
		printf("%s: could not start MHI channel %d in state %d: status 0x%x\n",
		    sc->sc_dev.dv_xname, ring->mhi_chan_id,
		    ring->mhi_chan_state, ring->cmd_status);
		return 1;
	}

	if (ring->mhi_chan_direction == MHI_CHAN_TYPE_INBOUND) {
		uint64_t wp = QWZ_DMA_DVA(ring->dmamem);
		int i;

		for (i = 0; i < ring->num_elements; i++) {
			struct qwz_mhi_ring_element *e;
			struct qwz_xfer_data *xfer;
			uint64_t paddr;

			e = qwz_pci_xfer_ring_get_elem(ring, wp);
			xfer = qwz_pci_xfer_ring_get_data(ring, wp);
			paddr = xfer->map->dm_segs[0].ds_addr;

			e->ptr = htole64(paddr);
			e->dword[0] = htole32((QWZ_PCI_XFER_MAX_DATA_SIZE <<
			    MHI_TRE0_DATA_LEN_SHFT) &
			    MHI_TRE0_DATA_LEN_MASK);
			e->dword[1] = htole32(MHI_TRE1_DATA_IEOT |
			    MHI_TRE1_DATA_BEI |
			    MHI_TRE1_DATA_TYPE_TRANSFER <<
			    MHI_TRE1_DATA_TYPE_SHIFT);

			ring->wp = wp;
			wp += sizeof(*e);
		}

		bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(ring->dmamem), 0,
		    QWZ_DMA_LEN(ring->dmamem), BUS_DMASYNC_PREWRITE);

		qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	}

	return 0;
}

int
qwz_mhi_start_channels(struct qwz_pci_softc *psc)
{
	struct qwz_pci_xfer_ring *ring;
	int ret = 0;

	qwz_mhi_device_wake(&psc->sc_sc);

	ring = &psc->xfer_rings[QWZ_PCI_XFER_RING_IPCR_OUTBOUND];
	if (qwz_mhi_start_channel(psc, ring)) {
		ret = 1;
		goto done;
	}

	ring = &psc->xfer_rings[QWZ_PCI_XFER_RING_IPCR_INBOUND];
	if (qwz_mhi_start_channel(psc, ring))
		ret = 1;
done:
	qwz_mhi_device_zzz(&psc->sc_sc);
	return ret;
}

int
qwz_mhi_start(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	uint32_t off;
	uint32_t ee, state;
	int ret;

	qwz_mhi_init_dev_ctxt(psc);

	psc->bhi_off = qwz_pci_read(sc, MHI_BHI_OFFSET);
	DNPRINTF(QWZ_D_MHI, "%s: BHI offset 0x%x\n", __func__, psc->bhi_off);

	psc->bhie_off = qwz_pci_read(sc, MHI_BHIE_OFFSET);
	DNPRINTF(QWZ_D_MHI, "%s: BHIE offset 0x%x\n", __func__, psc->bhie_off);

	/* Clean BHIE RX registers */
	for (off = MHI_BHIE_RXVECADDR_LOW_OFFS;
	     off < (MHI_BHIE_RXVECSTATUS_OFFS - 4);
	     off += 4)
	     	qwz_pci_write(sc, psc->bhie_off + off, 0x0);

	qwz_rddm_prepare(psc);

	/* Program BHI INTVEC */
	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_INTVEC, 0x00);

	/*
	 * Get BHI execution environment and confirm that it is valid
	 * for power on.
	 */
	ee = qwz_pci_read(sc, psc->bhi_off + MHI_BHI_EXECENV);
	if (!MHI_POWER_UP_CAPABLE(ee)) {
		printf("%s: invalid EE for power on: 0x%x\n",
		     sc->sc_dev.dv_xname, ee);
		return 1;
	}

	/*
	 * Get MHI state of the device and reset it if it is in system
	 * error.
	 */
	state = qwz_pci_read(sc, MHI_STATUS);
	DNPRINTF(QWZ_D_MHI, "%s: MHI power on with EE: 0x%x, status: 0x%x\n",
	     sc->sc_dev.dv_xname, ee, state);
	state = (state & MHI_STATUS_MHISTATE_MASK) >> MHI_STATUS_MHISTATE_SHFT;
	if (state == MHI_STATE_SYS_ERR) {
		if (qwz_mhi_reset_device(sc, 0))
			return 1;
		state = qwz_pci_read(sc, MHI_STATUS);
		DNPRINTF(QWZ_D_MHI, "%s: MHI state after reset: 0x%x\n",
		    sc->sc_dev.dv_xname, state);
		state = (state & MHI_STATUS_MHISTATE_MASK) >>
		    MHI_STATUS_MHISTATE_SHFT;
		if (state == MHI_STATE_SYS_ERR) {
			printf("%s: MHI stuck in system error state\n",
			    sc->sc_dev.dv_xname);
			return 1;
		}
	}

	psc->bhi_ee = ee;
	psc->mhi_state = state;

#if notyet
	/* Enable IRQs */
	//  XXX todo?
#endif

	/* Transition to primary runtime. */
	if (MHI_IN_PBL(ee)) {
		ret = qwz_mhi_fw_load_handler(psc);
		if (ret)
			return ret;

		/* XXX without this delay starting the channels may fail */
		delay(1000);
		qwz_mhi_start_channels(psc);
	} else {
		/* XXX Handle partially initialized device...?!? */
		ee = qwz_pci_read(sc, psc->bhi_off + MHI_BHI_EXECENV);
		if (!MHI_IN_MISSION_MODE(ee)) {
			printf("%s: failed to power up MHI, ee=0x%x\n",
			    sc->sc_dev.dv_xname, ee);
			return EIO;
		}
	}

	return 0;
}

void
qwz_mhi_stop(struct qwz_softc *sc)
{
	qwz_mhi_reset_device(sc, 1);
}

int
qwz_mhi_reset_device(struct qwz_softc *sc, int force)
{
	struct qwz_pci_softc *psc = (struct qwz_pci_softc *)sc;
	uint32_t reg;
	int ret = 0;

	reg = qwz_pcic_read32(sc, MHI_STATUS);

	DNPRINTF(QWZ_D_MHI, "%s: MHISTATUS 0x%x\n", sc->sc_dev.dv_xname, reg);
	/*
	 * Observed on QCA6390 that after SOC_GLOBAL_RESET, MHISTATUS
	 * has SYSERR bit set and thus need to set MHICTRL_RESET
	 * to clear SYSERR.
	 */
	if (force || (reg & MHI_STATUS_SYSERR_MASK)) {
		/* Trigger MHI Reset in device. */
		qwz_pcic_write32(sc, MHI_CTRL, MHI_CTRL_RESET_MASK);

		/* Wait for the reset bit to be cleared by the device. */
		ret = qwz_mhi_await_device_reset(sc);
		if (ret)
			return ret;

		if (psc->bhi_off == 0)
			psc->bhi_off = qwz_pci_read(sc, MHI_BHI_OFFSET);

		/* Device clear BHI INTVEC so re-program it. */
		qwz_pci_write(sc, psc->bhi_off + MHI_BHI_INTVEC, 0x00);
	}

	return 0;
}

static inline void
qwz_mhi_reset_txvecdb(struct qwz_softc *sc)
{
	qwz_pcic_write32(sc, PCIE_TXVECDB, 0);
}

static inline void
qwz_mhi_reset_txvecstatus(struct qwz_softc *sc)
{
	qwz_pcic_write32(sc, PCIE_TXVECSTATUS, 0);
}

static inline void
qwz_mhi_reset_rxvecdb(struct qwz_softc *sc)
{
	qwz_pcic_write32(sc, PCIE_RXVECDB, 0);
}

static inline void
qwz_mhi_reset_rxvecstatus(struct qwz_softc *sc)
{
	qwz_pcic_write32(sc, PCIE_RXVECSTATUS, 0);
}

void
qwz_mhi_clear_vector(struct qwz_softc *sc)
{
	qwz_mhi_reset_txvecdb(sc);
	qwz_mhi_reset_txvecstatus(sc);
	qwz_mhi_reset_rxvecdb(sc);
	qwz_mhi_reset_rxvecstatus(sc);
}

int
qwz_mhi_fw_load_handler(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int ret;
	char amss_path[PATH_MAX];
	u_char *data;
	size_t len;

	if (sc->fw_img[QWZ_FW_AMSS].data) {
		data = sc->fw_img[QWZ_FW_AMSS].data;
		len = sc->fw_img[QWZ_FW_AMSS].size;
	} else {
		ret = snprintf(amss_path, sizeof(amss_path), "%s-%s-%s",
		    ATH12K_FW_DIR, sc->hw_params.fw.dir, ATH12K_AMSS_FILE);
		if (ret < 0 || ret >= sizeof(amss_path))
			return ENOSPC;

		ret = loadfirmware(amss_path, &data, &len);
		if (ret) {
			printf("%s: could not read %s (error %d)\n",
			    sc->sc_dev.dv_xname, amss_path, ret);
			return ret;
		}

		if (len < MHI_DMA_VEC_CHUNK_SIZE) {
			printf("%s: %s is too short, have only %zu bytes\n",
			    sc->sc_dev.dv_xname, amss_path, len);
			free(data, M_DEVBUF, len);
			return EINVAL;
		}

		sc->fw_img[QWZ_FW_AMSS].data = data;
		sc->fw_img[QWZ_FW_AMSS].size = len;
	}

	/* Second-stage boot loader sits in the first 512 KB of image. */
	ret = qwz_mhi_fw_load_bhi(psc, data, MHI_DMA_VEC_CHUNK_SIZE);
	if (ret != 0) {
		printf("%s: could not load firmware %s\n",
		    sc->sc_dev.dv_xname, amss_path);
		return ret;
	}

	/* Now load the full image. */
	ret = qwz_mhi_fw_load_bhie(psc, data, len);
	if (ret != 0) {
		printf("%s: could not load firmware %s\n",
		    sc->sc_dev.dv_xname, amss_path);
		return ret;
	}

	while (psc->bhi_ee < MHI_EE_AMSS) {
		ret = tsleep_nsec(&psc->bhi_ee, 0, "qwzamss",
		    SEC_TO_NSEC(5));
		if (ret)
			break;
	}
	if (ret != 0) {
		printf("%s: device failed to enter AMSS EE\n",
		    sc->sc_dev.dv_xname);
	}

	return ret;
}

int
qwz_mhi_await_device_reset(struct qwz_softc *sc)
{
	const uint32_t msecs = 24, retries = 2;
	uint32_t reg;
	int timeout;

	/* Poll for CTRL RESET to clear. */
	timeout = retries;
	while (timeout > 0) {
		reg = qwz_pci_read(sc, MHI_CTRL);
		DNPRINTF(QWZ_D_MHI, "%s: MHI_CTRL is 0x%x\n", __func__, reg);
		if ((reg & MHI_CTRL_RESET_MASK) == 0)
			break;
		DELAY((msecs / retries) * 1000);
		timeout--;
	}
	if (timeout == 0) {
		DNPRINTF(QWZ_D_MHI, "%s: MHI reset failed\n", __func__);
		return ETIMEDOUT;
	}

	return 0;
}

int
qwz_mhi_await_device_ready(struct qwz_softc *sc)
{
	uint32_t reg;
	int timeout;
	const uint32_t msecs = 2000, retries = 4;


	/* Poll for READY to be set. */
	timeout = retries;
	while (timeout > 0) {
		reg = qwz_pci_read(sc, MHI_STATUS);
		DNPRINTF(QWZ_D_MHI, "%s: MHI_STATUS is 0x%x\n", __func__, reg);
		if (reg & MHI_STATUS_READY_MASK) {
			reg &= ~MHI_STATUS_READY_MASK;
			qwz_pci_write(sc, MHI_STATUS, reg);
			break;
		}
		DELAY((msecs / retries) * 1000);
		timeout--;
	}
	if (timeout == 0) {
		printf("%s: MHI not ready\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	return 0;
}

void
qwz_mhi_ready_state_transition(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int ret, i;

	ret = qwz_mhi_await_device_reset(sc);
	if (ret)
		return;

	ret = qwz_mhi_await_device_ready(sc);
	if (ret)
		return;

	/* Set up memory-mapped IO for channels, events, etc. */
	qwz_mhi_init_mmio(psc);

	/* Notify event rings. */
	for (i = 0; i < nitems(psc->event_rings); i++) {
		struct qwz_pci_event_ring *ring = &psc->event_rings[i];
		qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	}

	/*
	 * Set the device into M0 state. The device will transition
	 * into M0 and the execution environment will switch to SBL.
	 */
	qwz_mhi_set_state(sc, MHI_STATE_M0);
}

void
qwz_mhi_mission_mode_state_transition(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	int i;

	qwz_mhi_device_wake(sc);

	/* Notify event rings. */
	for (i = 0; i < nitems(psc->event_rings); i++) {
		struct qwz_pci_event_ring *ring = &psc->event_rings[i];
		qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	}

	/* TODO: Notify transfer/command rings? */

	qwz_mhi_device_zzz(sc);
}

void
qwz_mhi_low_power_mode_state_transition(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;

	qwz_mhi_set_state(sc, MHI_STATE_M2);
}

void
qwz_mhi_set_state(struct qwz_softc *sc, uint32_t state)
{
	uint32_t reg;

	reg = qwz_pci_read(sc, MHI_CTRL);

	if (state != MHI_STATE_RESET) {
		reg &= ~MHI_CTRL_MHISTATE_MASK;
		reg |= (state << MHI_CTRL_MHISTATE_SHFT) & MHI_CTRL_MHISTATE_MASK;
	} else
		reg |= MHI_CTRL_RESET_MASK;

	qwz_pci_write(sc, MHI_CTRL, reg);
}

void
qwz_mhi_init_mmio(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	uint64_t paddr;
	uint32_t reg;
	int i;

	reg = qwz_pci_read(sc, MHI_CHDBOFF);

	/* Set device wake doorbell address. */
	psc->wake_db = reg + 8 * MHI_DEV_WAKE_DB;

	/* Set doorbell address for each transfer ring. */
	for (i = 0; i < nitems(psc->xfer_rings); i++) {
		struct qwz_pci_xfer_ring *ring = &psc->xfer_rings[i];
		ring->db_addr = reg + (8 * ring->mhi_chan_id);
	}

	reg = qwz_pci_read(sc, MHI_ERDBOFF);
	/* Set doorbell address for each event ring. */
	for (i = 0; i < nitems(psc->event_rings); i++) {
		struct qwz_pci_event_ring *ring = &psc->event_rings[i];
		ring->db_addr = reg + (8 * i);
	}

	paddr = QWZ_DMA_DVA(psc->chan_ctxt);
	qwz_pci_write(sc, MHI_CCABAP_HIGHER, paddr >> 32);
	qwz_pci_write(sc, MHI_CCABAP_LOWER, paddr & 0xffffffff);

	paddr = QWZ_DMA_DVA(psc->event_ctxt);
	qwz_pci_write(sc, MHI_ECABAP_HIGHER, paddr >> 32);
	qwz_pci_write(sc, MHI_ECABAP_LOWER, paddr & 0xffffffff);

	paddr = QWZ_DMA_DVA(psc->cmd_ctxt);
	qwz_pci_write(sc, MHI_CRCBAP_HIGHER, paddr >> 32);
	qwz_pci_write(sc, MHI_CRCBAP_LOWER, paddr & 0xffffffff);

	/* Not (yet?) using fixed memory space from a device-tree. */
	qwz_pci_write(sc, MHI_CTRLBASE_HIGHER, 0);
	qwz_pci_write(sc, MHI_CTRLBASE_LOWER, 0);
	qwz_pci_write(sc, MHI_DATABASE_HIGHER, 0);
	qwz_pci_write(sc, MHI_DATABASE_LOWER, 0);
	qwz_pci_write(sc, MHI_CTRLLIMIT_HIGHER, 0x0);
	qwz_pci_write(sc, MHI_CTRLLIMIT_LOWER, 0xffffffff);
	qwz_pci_write(sc, MHI_DATALIMIT_HIGHER, 0x0);
	qwz_pci_write(sc, MHI_DATALIMIT_LOWER, 0xffffffff);

	reg = qwz_pci_read(sc, MHI_CFG);
	reg &= ~(MHI_CFG_NER_MASK | MHI_CFG_NHWER_MASK);
	reg |= QWZ_NUM_EVENT_CTX << MHI_CFG_NER_SHFT;
	qwz_pci_write(sc, MHI_CFG, reg);
}

int
qwz_mhi_fw_load_bhi(struct qwz_pci_softc *psc, uint8_t *data, size_t len)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_dmamem *data_adm;
	uint32_t seq, reg, status = MHI_BHI_STATUS_RESET;
	uint64_t paddr;
	int ret;

	data_adm = qwz_dmamem_alloc(sc->sc_dmat, len, 0);
	if (data_adm == NULL) {
		printf("%s: could not allocate BHI DMA data buffer\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	/* Copy firmware image to DMA memory. */
	memcpy(QWZ_DMA_KVA(data_adm), data, len);

	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_STATUS, 0);

	/* Set data physical address and length. */
	paddr = QWZ_DMA_DVA(data_adm);
	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_IMGADDR_HIGH, paddr >> 32);
	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_IMGADDR_LOW,
	    paddr & 0xffffffff);
	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_IMGSIZE, len);

	/* Set a random transaction sequence number. */
	do {
		seq = arc4random_uniform(MHI_BHI_TXDB_SEQNUM_BMSK);
	} while (seq == 0);
	qwz_pci_write(sc, psc->bhi_off + MHI_BHI_IMGTXDB, seq);

	/* Wait for completion. */
	ret = 0;
	while (status != MHI_BHI_STATUS_SUCCESS && psc->bhi_ee < MHI_EE_SBL) {
		ret = tsleep_nsec(&psc->bhi_ee, 0, "qwzbhi", SEC_TO_NSEC(5));
		if (ret)
			break;
		reg = qwz_pci_read(sc, psc->bhi_off + MHI_BHI_STATUS);
		status = (reg & MHI_BHI_STATUS_MASK) >> MHI_BHI_STATUS_SHFT;
	}

	if (ret) {
		printf("%s: BHI load timeout\n", sc->sc_dev.dv_xname);
		reg = qwz_pci_read(sc, psc->bhi_off + MHI_BHI_STATUS);
		status = (reg & MHI_BHI_STATUS_MASK) >> MHI_BHI_STATUS_SHFT;
		DNPRINTF(QWZ_D_MHI, "%s: BHI status is 0x%x EE is 0x%x\n",
		    __func__, status, psc->bhi_ee);
	}

	qwz_dmamem_free(sc->sc_dmat, data_adm);
	return ret;
}

int
qwz_mhi_fw_load_bhie(struct qwz_pci_softc *psc, uint8_t *data, size_t len)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_dma_vec_entry *vec;
	uint32_t seq, reg, state = MHI_BHIE_TXVECSTATUS_STATUS_RESET;
	uint64_t paddr;
	const size_t chunk_size = MHI_DMA_VEC_CHUNK_SIZE;
	size_t nseg, remain, vec_size;
	int i, ret;

	nseg = howmany(len, chunk_size);
	if (nseg == 0) {
		printf("%s: BHIE data too short, have only %zu bytes\n",
		    sc->sc_dev.dv_xname, len);
		return 1;
	}

	if (psc->amss_data == NULL || QWZ_DMA_LEN(psc->amss_data) < len) {
		if (psc->amss_data)
			qwz_dmamem_free(sc->sc_dmat, psc->amss_data);
		psc->amss_data = qwz_dmamem_alloc(sc->sc_dmat, len, 0);
		if (psc->amss_data == NULL) {
			printf("%s: could not allocate BHIE DMA data buffer\n",
			    sc->sc_dev.dv_xname);
			return 1;
		}
	}

	vec_size = nseg * sizeof(*vec);
	if (psc->amss_vec == NULL || QWZ_DMA_LEN(psc->amss_vec) < vec_size) {
		if (psc->amss_vec)
			qwz_dmamem_free(sc->sc_dmat, psc->amss_vec);
		psc->amss_vec = qwz_dmamem_alloc(sc->sc_dmat, vec_size, 0);
		if (psc->amss_vec == NULL) {
			printf("%s: could not allocate BHIE DMA vec buffer\n",
			    sc->sc_dev.dv_xname);
			qwz_dmamem_free(sc->sc_dmat, psc->amss_data);
			psc->amss_data = NULL;
			return 1;
		}
	}

	/* Copy firmware image to DMA memory. */
	memcpy(QWZ_DMA_KVA(psc->amss_data), data, len);

	/* Create vector which controls chunk-wise DMA copy in hardware. */
	paddr = QWZ_DMA_DVA(psc->amss_data);
	vec = QWZ_DMA_KVA(psc->amss_vec);
	remain = len;
	for (i = 0; i < nseg; i++) {
		vec[i].paddr = paddr;
		if (remain >= chunk_size) {
			vec[i].size = chunk_size;
			remain -= chunk_size;
			paddr += chunk_size;
		} else
			vec[i].size = remain;
	}

	/* Set vector physical address and length. */
	paddr = QWZ_DMA_DVA(psc->amss_vec);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_TXVECADDR_HIGH_OFFS,
	    paddr >> 32);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_TXVECADDR_LOW_OFFS,
	    paddr & 0xffffffff);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_TXVECSIZE_OFFS, vec_size);

	/* Set a random transaction sequence number. */
	do {
		seq = arc4random_uniform(MHI_BHIE_TXVECSTATUS_SEQNUM_BMSK);
	} while (seq == 0);
	reg = qwz_pci_read(sc, psc->bhie_off + MHI_BHIE_TXVECDB_OFFS);
	reg &= ~MHI_BHIE_TXVECDB_SEQNUM_BMSK;
	reg |= seq << MHI_BHIE_TXVECDB_SEQNUM_SHFT;
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_TXVECDB_OFFS, reg);

	/* Wait for completion. */
	ret = 0;
	while (state != MHI_BHIE_TXVECSTATUS_STATUS_XFER_COMPL) {
		ret = tsleep_nsec(&psc->bhie_off, 0, "qwzbhie",
		    SEC_TO_NSEC(5));
		if (ret)
			break;
		reg = qwz_pci_read(sc,
		    psc->bhie_off + MHI_BHIE_TXVECSTATUS_OFFS);
		state = (reg & MHI_BHIE_TXVECSTATUS_STATUS_BMSK) >>
		    MHI_BHIE_TXVECSTATUS_STATUS_SHFT;
		DNPRINTF(QWZ_D_MHI, "%s: txvec state is 0x%x\n", __func__,
		    state);
	}

	if (ret) {
		printf("%s: BHIE load timeout\n", sc->sc_dev.dv_xname);
		return ret;
	}
	return 0;
}

void
qwz_rddm_prepare(struct qwz_pci_softc *psc)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_dma_vec_entry *vec;
	struct qwz_dmamem *data_adm, *vec_adm;
	uint32_t seq, reg;
	uint64_t paddr;
	const size_t len = QWZ_RDDM_DUMP_SIZE;
	const size_t chunk_size = MHI_DMA_VEC_CHUNK_SIZE;
	size_t nseg, remain, vec_size;
	int i;

	nseg = howmany(len, chunk_size);
	if (nseg == 0) {
		printf("%s: RDDM data too short, have only %zu bytes\n",
		    sc->sc_dev.dv_xname, len);
		return;
	}

	data_adm = qwz_dmamem_alloc(sc->sc_dmat, len, 0);
	if (data_adm == NULL) {
		printf("%s: could not allocate BHIE DMA data buffer\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	vec_size = nseg * sizeof(*vec);
	vec_adm = qwz_dmamem_alloc(sc->sc_dmat, vec_size, 0);
	if (vec_adm == NULL) {
		printf("%s: could not allocate BHIE DMA vector buffer\n",
		    sc->sc_dev.dv_xname);
		qwz_dmamem_free(sc->sc_dmat, data_adm);
		return;
	}

	/* Create vector which controls chunk-wise DMA copy from hardware. */
	paddr = QWZ_DMA_DVA(data_adm);
	vec = QWZ_DMA_KVA(vec_adm);
	remain = len;
	for (i = 0; i < nseg; i++) {
		vec[i].paddr = paddr;
		if (remain >= chunk_size) {
			vec[i].size = chunk_size;
			remain -= chunk_size;
			paddr += chunk_size;
		} else
			vec[i].size = remain;
	}

	/* Set vector physical address and length. */
	paddr = QWZ_DMA_DVA(vec_adm);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_RXVECADDR_HIGH_OFFS,
	    paddr >> 32);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_RXVECADDR_LOW_OFFS,
	    paddr & 0xffffffff);
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_RXVECSIZE_OFFS, vec_size);

	/* Set a random transaction sequence number. */
	do {
		seq = arc4random_uniform(MHI_BHIE_RXVECSTATUS_SEQNUM_BMSK);
	} while (seq == 0);

	reg = qwz_pci_read(sc, psc->bhie_off + MHI_BHIE_RXVECDB_OFFS);
	reg &= ~MHI_BHIE_RXVECDB_SEQNUM_BMSK;
	reg |= seq << MHI_BHIE_RXVECDB_SEQNUM_SHFT;
	qwz_pci_write(sc, psc->bhie_off + MHI_BHIE_RXVECDB_OFFS, reg);

	psc->rddm_data = data_adm;
	psc->rddm_vec = vec_adm;
}

#ifdef QWZ_DEBUG
void
qwz_rddm_task(void *arg)
{
	struct qwz_pci_softc *psc = arg;
	struct qwz_softc *sc = &psc->sc_sc;
	uint32_t reg, state = MHI_BHIE_RXVECSTATUS_STATUS_RESET;
	const size_t len = QWZ_RDDM_DUMP_SIZE;
	int i, timeout;
	const uint32_t msecs = 100, retries = 20;
	uint8_t *rddm;
	struct nameidata nd;
	struct vnode *vp = NULL;
	struct iovec iov[3];
	struct uio uio;
	char path[PATH_MAX];
	int error = 0;

	if (psc->rddm_data == NULL) {
		DPRINTF("%s: RDDM not prepared\n", __func__);
		return;
	}

	/* Poll for completion */
	timeout = retries;
	while (timeout > 0 && state != MHI_BHIE_RXVECSTATUS_STATUS_XFER_COMPL) {
		reg = qwz_pci_read(sc,
		    psc->bhie_off + MHI_BHIE_RXVECSTATUS_OFFS);
		state = (reg & MHI_BHIE_RXVECSTATUS_STATUS_BMSK) >>
		    MHI_BHIE_RXVECSTATUS_STATUS_SHFT;
		DPRINTF("%s: txvec state is 0x%x\n", __func__, state);
		DELAY((msecs / retries) * 1000);
		timeout--;
	}

	if (timeout == 0) {
		DPRINTF("%s: RDDM dump failed\n", sc->sc_dev.dv_xname);
		return;
	}

	rddm = QWZ_DMA_KVA(psc->rddm_data);
	DPRINTF("%s: RDDM snippet:\n", __func__);
	for (i = 0; i < MIN(64, len); i++) {
		DPRINTF("%s %.2x", i % 16 == 0 ? "\n" : "", rddm[i]);
	}
	DPRINTF("\n");

	DPRINTF("%s: sleeping for 30 seconds to allow userland to boot\n", __func__);
	tsleep_nsec(&psc->rddm_data, 0, "qwzrddm", SEC_TO_NSEC(30));

	snprintf(path, sizeof(path), "/root/%s-rddm.bin", sc->sc_dev.dv_xname);
	DPRINTF("%s: saving RDDM to %s\n", __func__, path);
	NDINIT(&nd, 0, 0, UIO_SYSSPACE, path, curproc);
	nd.ni_pledge = PLEDGE_CPATH | PLEDGE_WPATH;
	nd.ni_unveil = UNVEIL_CREATE | UNVEIL_WRITE;
	error = vn_open(&nd, FWRITE | O_CREAT | O_NOFOLLOW | O_TRUNC,
	    S_IRUSR | S_IWUSR);
	if (error) {
		DPRINTF("%s: vn_open: error %d\n", __func__, error);
		goto done;
	}
	vp = nd.ni_vp;
	VOP_UNLOCK(vp);

	iov[0].iov_base = (void *)rddm;
	iov[0].iov_len = len;
	iov[1].iov_len = 0;
	uio.uio_iov = &iov[0];
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_resid = len;
	uio.uio_iovcnt = 1;
	uio.uio_procp = curproc;
	error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error) {
		DPRINTF("%s: vget: error %d\n", __func__, error);
		goto done;
	}
	error = VOP_WRITE(vp, &uio, IO_UNIT|IO_APPEND, curproc->p_ucred);
	vput(vp);
	if (error)
		DPRINTF("%s: VOP_WRITE: error %d\n", __func__, error);
	#if 0
	error = vn_close(vp, FWRITE, curproc->p_ucred, curproc);
	if (error)
		DPRINTF("%s: vn_close: error %d\n", __func__, error);
	#endif
done:
	qwz_dmamem_free(sc->sc_dmat, psc->rddm_data);
	qwz_dmamem_free(sc->sc_dmat, psc->rddm_vec);
	psc->rddm_data = NULL;
	psc->rddm_vec = NULL;
	DPRINTF("%s: done, error %d\n", __func__, error);
}
#endif

void *
qwz_pci_event_ring_get_elem(struct qwz_pci_event_ring *ring, uint64_t rp)
{
	uint64_t base = QWZ_DMA_DVA(ring->dmamem), offset;
	void *addr = QWZ_DMA_KVA(ring->dmamem);

	if (rp < base)
		return NULL;

	offset = rp - base;
	if (offset >= ring->size)
		return NULL;

	return addr + offset;
}

void
qwz_mhi_state_change(struct qwz_pci_softc *psc, int ee, int mhi_state)
{
	struct qwz_softc *sc = &psc->sc_sc;
	uint32_t old_ee = psc->bhi_ee;
	uint32_t old_mhi_state = psc->mhi_state;

	if (ee != -1 && psc->bhi_ee != ee) {
		switch (ee) {
		case MHI_EE_PBL:
			DNPRINTF(QWZ_D_MHI, "%s: new EE PBL\n",
			    sc->sc_dev.dv_xname);
			psc->bhi_ee = ee;
			break;
		case MHI_EE_SBL:
			psc->bhi_ee = ee;
			DNPRINTF(QWZ_D_MHI, "%s: new EE SBL\n",
			    sc->sc_dev.dv_xname);
			break;
		case MHI_EE_AMSS:
			DNPRINTF(QWZ_D_MHI, "%s: new EE AMSS\n",
			    sc->sc_dev.dv_xname);
			psc->bhi_ee = ee;
			/* Wake thread loading the full AMSS image. */
			wakeup(&psc->bhie_off);
			break;
		case MHI_EE_WFW:
			DNPRINTF(QWZ_D_MHI, "%s: new EE WFW\n",
			    sc->sc_dev.dv_xname);
			psc->bhi_ee = ee;
			break;
		default:
			printf("%s: unhandled EE change to %x\n",
			    sc->sc_dev.dv_xname, ee);
			break;
		}
	}

	if (mhi_state != -1 && psc->mhi_state != mhi_state) {
		switch (mhi_state) {
		case -1:
			break;
		case MHI_STATE_RESET:
			DNPRINTF(QWZ_D_MHI, "%s: new MHI state RESET\n",
			    sc->sc_dev.dv_xname);
			psc->mhi_state = mhi_state;
			break;
		case MHI_STATE_READY:
			DNPRINTF(QWZ_D_MHI, "%s: new MHI state READY\n",
			    sc->sc_dev.dv_xname);
			psc->mhi_state = mhi_state;
			qwz_mhi_ready_state_transition(psc);
			break;
		case MHI_STATE_M0:
			DNPRINTF(QWZ_D_MHI, "%s: new MHI state M0\n",
			    sc->sc_dev.dv_xname);
			psc->mhi_state = mhi_state;
			qwz_mhi_mission_mode_state_transition(psc);
			break;
		case MHI_STATE_M1:
			DNPRINTF(QWZ_D_MHI, "%s: new MHI state M1\n",
			    sc->sc_dev.dv_xname);
			psc->mhi_state = mhi_state;
			qwz_mhi_low_power_mode_state_transition(psc);
			break;
		case MHI_STATE_SYS_ERR:
			DNPRINTF(QWZ_D_MHI,
			    "%s: new MHI state SYS ERR\n",
			    sc->sc_dev.dv_xname);
			psc->mhi_state = mhi_state;
			break;
		default:
			printf("%s: unhandled MHI state change to %x\n",
			    sc->sc_dev.dv_xname, mhi_state);
			break;
		}
	}

	if (old_ee != psc->bhi_ee)
		wakeup(&psc->bhi_ee);
	if (old_mhi_state != psc->mhi_state)
		wakeup(&psc->mhi_state);
}

void
qwz_pci_intr_ctrl_event_mhi(struct qwz_pci_softc *psc, uint32_t mhi_state)
{
	DNPRINTF(QWZ_D_MHI, "%s: MHI state change 0x%x -> 0x%x\n", __func__,
	    psc->mhi_state, mhi_state);

	if (psc->mhi_state != mhi_state)
		qwz_mhi_state_change(psc, -1, mhi_state);
}

void
qwz_pci_intr_ctrl_event_ee(struct qwz_pci_softc *psc, uint32_t ee)
{
	DNPRINTF(QWZ_D_MHI, "%s: EE change 0x%x to 0x%x\n", __func__,
	    psc->bhi_ee, ee);

	if (psc->bhi_ee != ee)
		qwz_mhi_state_change(psc, ee, -1);
}

void
qwz_pci_intr_ctrl_event_cmd_complete(struct qwz_pci_softc *psc,
    uint64_t ptr, uint32_t cmd_status)
{
	struct qwz_pci_cmd_ring	*cmd_ring = &psc->cmd_ring;
	uint64_t base = QWZ_DMA_DVA(cmd_ring->dmamem);
	struct qwz_pci_xfer_ring *xfer_ring = NULL;
	struct qwz_mhi_ring_element *e;
	uint32_t tre1, chid;
	size_t i;

	e = qwz_pci_cmd_ring_get_elem(cmd_ring, ptr);
	if (e == NULL)
		return;

	tre1 = le32toh(e->dword[1]);
	chid = (tre1 & MHI_TRE1_EV_CHID_MASK) >> MHI_TRE1_EV_CHID_SHFT;

	for (i = 0; i < nitems(psc->xfer_rings); i++) {
		if (psc->xfer_rings[i].mhi_chan_id == chid) {
			xfer_ring = &psc->xfer_rings[i];
			break;
		}
	}
	if (xfer_ring == NULL) {
		printf("%s: no transfer ring found for command completion "
		    "on channel %u\n", __func__, chid);
		return;
	}

	xfer_ring->cmd_status = cmd_status;
	wakeup(&xfer_ring->cmd_status);

	if (cmd_ring->rp + sizeof(*e) >= base + cmd_ring->size)
		cmd_ring->rp = base;
	else
		cmd_ring->rp += sizeof(*e);
}

int
qwz_pci_intr_ctrl_event(struct qwz_pci_softc *psc, struct qwz_pci_event_ring *ring)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_mhi_event_ctxt *c;
	uint64_t rp, wp, base;
	struct qwz_mhi_ring_element *e;
	uint32_t tre0, tre1, type, code, chid, len;

	c = ring->event_ctxt;
	if (c == NULL) {
		/*
		 * Interrupts can trigger before mhi_init_event_rings()
		 * if the device is still active after a warm reboot.
		 */
		return 0;
	}

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->event_ctxt), 0,
	    QWZ_DMA_LEN(psc->event_ctxt), BUS_DMASYNC_POSTREAD);

	rp = le64toh(c->rp);
	wp = le64toh(c->wp);

	DNPRINTF(QWZ_D_MHI, "%s: kernel rp=0x%llx\n", __func__, ring->rp);
	DNPRINTF(QWZ_D_MHI, "%s: device rp=0x%llx\n", __func__, rp);
	DNPRINTF(QWZ_D_MHI, "%s: kernel wp=0x%llx\n", __func__, ring->wp);
	DNPRINTF(QWZ_D_MHI, "%s: device wp=0x%llx\n", __func__, wp);

	base = QWZ_DMA_DVA(ring->dmamem);
	if (ring->rp == rp || rp < base || rp >= base + ring->size)
		return 0;
	if (wp < base || wp >= base + ring->size)
		return 0;

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(ring->dmamem),
	    0, QWZ_DMA_LEN(ring->dmamem), BUS_DMASYNC_POSTREAD);

	while (ring->rp != rp) {
		e = qwz_pci_event_ring_get_elem(ring, ring->rp);
		if (e == NULL)
			return 0;

		tre0 = le32toh(e->dword[0]);
		tre1 = le32toh(e->dword[1]);

		len = (tre0 & MHI_TRE0_EV_LEN_MASK) >> MHI_TRE0_EV_LEN_SHFT;
		code = (tre0 & MHI_TRE0_EV_CODE_MASK) >> MHI_TRE0_EV_CODE_SHFT;
		type = (tre1 & MHI_TRE1_EV_TYPE_MASK) >> MHI_TRE1_EV_TYPE_SHFT;
		chid = (tre1 & MHI_TRE1_EV_CHID_MASK) >> MHI_TRE1_EV_CHID_SHFT;
		DNPRINTF(QWZ_D_MHI, "%s: len=%u code=0x%x type=0x%x chid=%d\n",
		    __func__, len, code, type, chid);

		switch (type) {
		case MHI_PKT_TYPE_STATE_CHANGE_EVENT:
			qwz_pci_intr_ctrl_event_mhi(psc, code);
			break;
		case MHI_PKT_TYPE_EE_EVENT:
			qwz_pci_intr_ctrl_event_ee(psc, code);
			break;
		case MHI_PKT_TYPE_CMD_COMPLETION_EVENT:
			qwz_pci_intr_ctrl_event_cmd_complete(psc,
			    le64toh(e->ptr), code);
			break;
		default:
			printf("%s: unhandled event type 0x%x\n",
			    __func__, type);
			break;
		}

		if (ring->rp + sizeof(*e) >= base + ring->size)
			ring->rp = base;
		else
			ring->rp += sizeof(*e);

		if (ring->wp + sizeof(*e) >= base + ring->size)
			ring->wp = base;
		else
			ring->wp += sizeof(*e);
	}

	c->wp = htole64(ring->wp);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->event_ctxt), 0,
	    QWZ_DMA_LEN(psc->event_ctxt), BUS_DMASYNC_PREWRITE);

	qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	return 1;
}

void
qwz_pci_intr_data_event_tx(struct qwz_pci_softc *psc, struct qwz_mhi_ring_element *e)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_pci_xfer_ring *ring;
	struct qwz_xfer_data *xfer;
	uint64_t rp, evrp, base, paddr;
	uint32_t tre0, tre1, code, chid, evlen, len;
	int i;

	tre0 = le32toh(e->dword[0]);
	tre1 = le32toh(e->dword[1]);

	evlen = (tre0 & MHI_TRE0_EV_LEN_MASK) >> MHI_TRE0_EV_LEN_SHFT;
	code = (tre0 & MHI_TRE0_EV_CODE_MASK) >> MHI_TRE0_EV_CODE_SHFT;
	chid = (tre1 & MHI_TRE1_EV_CHID_MASK) >> MHI_TRE1_EV_CHID_SHFT;

	switch (code) {
	case MHI_EV_CC_EOT:
		for (i = 0; i < nitems(psc->xfer_rings); i++) {
			ring = &psc->xfer_rings[i];
			if (ring->mhi_chan_id == chid)
				break;
		}
		if (i == nitems(psc->xfer_rings)) {
			printf("%s: unhandled channel 0x%x\n",
			    __func__, chid);
			break;
		}
		base = QWZ_DMA_DVA(ring->dmamem);
		/* PTR contains the entry that was last written */
		evrp = letoh64(e->ptr);
		rp = evrp;
		if (rp < base || rp >= base + ring->size) {
			printf("%s: invalid ptr 0x%llx\n",
			    __func__, rp);
			break;
		}
		/* Point rp to next empty slot */
		if (rp + sizeof(*e) >= base + ring->size)
			rp = base;
		else
			rp += sizeof(*e);
		/* Parse until next empty slot */
		while (ring->rp != rp) {
			DNPRINTF(QWZ_D_MHI, "%s:%d: ring->rp 0x%llx "
			    "ring->wp 0x%llx rp 0x%llx\n", __func__,
			    __LINE__, ring->rp, ring->wp, rp);
			e = qwz_pci_xfer_ring_get_elem(ring, ring->rp);
			xfer = qwz_pci_xfer_ring_get_data(ring, ring->rp);

			if (ring->rp == evrp)
				len = evlen;
			else
				len = xfer->m->m_pkthdr.len;

			bus_dmamap_sync(sc->sc_dmat, xfer->map, 0,
			    xfer->m->m_pkthdr.len, BUS_DMASYNC_POSTREAD);
#ifdef QWZ_DEBUG
			{
			int i;
			DNPRINTF(QWZ_D_MHI, "%s: chan %u data (len %u): ",
			    __func__,
			    ring->mhi_chan_id, len);
			for (i = 0; i < MIN(32, len); i++) {
				DNPRINTF(QWZ_D_MHI, "%02x ",
				    (unsigned char)mtod(xfer->m, caddr_t)[i]);
			}
			if (i < len)
				DNPRINTF(QWZ_D_MHI, "...");
			DNPRINTF(QWZ_D_MHI, "\n");
			}
#endif
			if (ring->mhi_chan_direction == MHI_CHAN_TYPE_INBOUND) {
				/* Save m_data as upper layers use m_adj(9) */
				void *o_data = xfer->m->m_data;

				/* Pass mbuf to upper layers */
				qwz_qrtr_recv_msg(sc, xfer->m);

				/* Reset RX mbuf instead of free/alloc */
				KASSERT(xfer->m->m_next == NULL);
				xfer->m->m_data = o_data;
				xfer->m->m_len = xfer->m->m_pkthdr.len =
				    QWZ_PCI_XFER_MAX_DATA_SIZE;

				paddr = xfer->map->dm_segs[0].ds_addr;

				e->ptr = htole64(paddr);
				e->dword[0] = htole32((
				    QWZ_PCI_XFER_MAX_DATA_SIZE <<
				    MHI_TRE0_DATA_LEN_SHFT) &
				    MHI_TRE0_DATA_LEN_MASK);
				e->dword[1] = htole32(MHI_TRE1_DATA_IEOT |
				    MHI_TRE1_DATA_BEI |
				    MHI_TRE1_DATA_TYPE_TRANSFER <<
				    MHI_TRE1_DATA_TYPE_SHIFT);

				if (ring->wp + sizeof(*e) >= base + ring->size)
					ring->wp = base;
				else
					ring->wp += sizeof(*e);
			} else {
				/* Unload and free TX mbuf */
				bus_dmamap_unload(sc->sc_dmat, xfer->map);
				m_freem(xfer->m);
				xfer->m = NULL;
				ring->queued--;
			}

			if (ring->rp + sizeof(*e) >= base + ring->size)
				ring->rp = base;
			else
				ring->rp += sizeof(*e);
		}

		if (ring->mhi_chan_direction == MHI_CHAN_TYPE_INBOUND) {
			ring->chan_ctxt->wp = htole64(ring->wp);

			bus_dmamap_sync(sc->sc_dmat,
			    QWZ_DMA_MAP(psc->chan_ctxt), 0,
			    QWZ_DMA_LEN(psc->chan_ctxt),
			    BUS_DMASYNC_PREWRITE);

			qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
		}
		break;
	default:
		printf("%s: unhandled event code 0x%x\n",
		    __func__, code);
	}
}

int
qwz_pci_intr_data_event(struct qwz_pci_softc *psc, struct qwz_pci_event_ring *ring)
{
	struct qwz_softc *sc = &psc->sc_sc;
	struct qwz_mhi_event_ctxt *c;
	uint64_t rp, wp, base;
	struct qwz_mhi_ring_element *e;
	uint32_t tre0, tre1, type, code, chid, len;

	c = ring->event_ctxt;
	if (c == NULL) {
		/*
		 * Interrupts can trigger before mhi_init_event_rings()
		 * if the device is still active after a warm reboot.
		 */
		return 0;
	}

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->event_ctxt), 0,
	    QWZ_DMA_LEN(psc->event_ctxt), BUS_DMASYNC_POSTREAD);

	rp = le64toh(c->rp);
	wp = le64toh(c->wp);

	DNPRINTF(QWZ_D_MHI, "%s: kernel rp=0x%llx\n", __func__, ring->rp);
	DNPRINTF(QWZ_D_MHI, "%s: device rp=0x%llx\n", __func__, rp);
	DNPRINTF(QWZ_D_MHI, "%s: kernel wp=0x%llx\n", __func__, ring->wp);
	DNPRINTF(QWZ_D_MHI, "%s: device wp=0x%llx\n", __func__, wp);

	base = QWZ_DMA_DVA(ring->dmamem);
	if (ring->rp == rp || rp < base || rp >= base + ring->size)
		return 0;

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(ring->dmamem),
	    0, QWZ_DMA_LEN(ring->dmamem), BUS_DMASYNC_POSTREAD);

	while (ring->rp != rp) {
		e = qwz_pci_event_ring_get_elem(ring, ring->rp);
		if (e == NULL)
			return 0;

		tre0 = le32toh(e->dword[0]);
		tre1 = le32toh(e->dword[1]);

		len = (tre0 & MHI_TRE0_EV_LEN_MASK) >> MHI_TRE0_EV_LEN_SHFT;
		code = (tre0 & MHI_TRE0_EV_CODE_MASK) >> MHI_TRE0_EV_CODE_SHFT;
		type = (tre1 & MHI_TRE1_EV_TYPE_MASK) >> MHI_TRE1_EV_TYPE_SHFT;
		chid = (tre1 & MHI_TRE1_EV_CHID_MASK) >> MHI_TRE1_EV_CHID_SHFT;
		DNPRINTF(QWZ_D_MHI, "%s: len=%u code=0x%x type=0x%x chid=%d\n",
		    __func__, len, code, type, chid);

		switch (type) {
		case MHI_PKT_TYPE_TX_EVENT:
			qwz_pci_intr_data_event_tx(psc, e);
			break;
		default:
			printf("%s: unhandled event type 0x%x\n",
			    __func__, type);
			break;
		}

		if (ring->rp + sizeof(*e) >= base + ring->size)
			ring->rp = base;
		else
			ring->rp += sizeof(*e);

		if (ring->wp + sizeof(*e) >= base + ring->size)
			ring->wp = base;
		else
			ring->wp += sizeof(*e);
	}

	c->wp = htole64(ring->wp);

	bus_dmamap_sync(sc->sc_dmat, QWZ_DMA_MAP(psc->event_ctxt), 0,
	    QWZ_DMA_LEN(psc->event_ctxt), BUS_DMASYNC_PREWRITE);

	qwz_mhi_ring_doorbell(sc, ring->db_addr, ring->wp);
	return 1;
}

int
qwz_pci_intr_mhi_ctrl(void *arg)
{
	struct qwz_pci_softc *psc = arg;

	if (qwz_pci_intr_ctrl_event(psc, &psc->event_rings[0]))
		return 1;

	return 0;
}

int
qwz_pci_intr_mhi_data(void *arg)
{
	struct qwz_pci_softc *psc = arg;

	if (qwz_pci_intr_data_event(psc, &psc->event_rings[1]))
		return 1;

	return 0;
}

int
qwz_pci_intr(void *arg)
{
	struct qwz_pci_softc *psc = arg;
	struct qwz_softc *sc = (void *)psc;
	uint32_t ee, state;
	int ret = 0;

	/*
	 * Interrupts can trigger before mhi_start() during boot if the device
	 * is still active after a warm reboot.
	 */
	if (psc->bhi_off == 0)
		psc->bhi_off = qwz_pci_read(sc, MHI_BHI_OFFSET);

	ee = qwz_pci_read(sc, psc->bhi_off + MHI_BHI_EXECENV);
	state = qwz_pci_read(sc, MHI_STATUS);
	state = (state & MHI_STATUS_MHISTATE_MASK) >>
	    MHI_STATUS_MHISTATE_SHFT;

	DNPRINTF(QWZ_D_MHI,
	    "%s: BHI interrupt with EE: 0x%x -> 0x%x state: 0x%x -> 0x%x\n",
	     sc->sc_dev.dv_xname, psc->bhi_ee, ee, psc->mhi_state, state);

	if (ee == MHI_EE_RDDM) {
		/* Firmware crash, e.g. due to invalid DMA memory access. */
		psc->bhi_ee = ee;
#ifdef QWZ_DEBUG
		if (!psc->rddm_triggered) {
			/* Write fw memory dump to root's home directory. */
			task_add(systq, &psc->rddm_task);
			psc->rddm_triggered = 1;
		}
#else
		printf("%s: fatal firmware error\n",
		   sc->sc_dev.dv_xname);
		if (!test_bit(ATH12K_FLAG_CRASH_FLUSH, sc->sc_flags) &&
		    (sc->sc_ic.ic_if.if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			/* Try to reset the device. */
			set_bit(ATH12K_FLAG_CRASH_FLUSH, sc->sc_flags);
			task_add(systq, &sc->init_task);
		}
#endif
		return 1;
	} else if (psc->bhi_ee == MHI_EE_PBL || psc->bhi_ee == MHI_EE_SBL) {
		int new_ee = -1, new_mhi_state = -1;

		if (psc->bhi_ee != ee)
			new_ee = ee;

		if (psc->mhi_state != state)
			new_mhi_state = state;

		if (new_ee != -1 || new_mhi_state != -1)
			qwz_mhi_state_change(psc, new_ee, new_mhi_state);

		ret = 1;
	}

	if (!test_bit(ATH12K_FLAG_MULTI_MSI_VECTORS, sc->sc_flags)) {
		int i;

		if (qwz_pci_intr_ctrl_event(psc, &psc->event_rings[0]))
			ret = 1;
		if (qwz_pci_intr_data_event(psc, &psc->event_rings[1]))
			ret = 1;

		for (i = 0; i < sc->hw_params.ce_count; i++) {
			struct qwz_ce_pipe *ce_pipe = &sc->ce.ce_pipe[i];

			if (qwz_ce_intr(ce_pipe))
				ret = 1;
		}

		if (test_bit(ATH12K_FLAG_EXT_IRQ_ENABLED, sc->sc_flags)) {
			for (i = 0; i < nitems(sc->ext_irq_grp); i++) {
				if (qwz_dp_service_srng(sc, i))
					ret = 1;
			}
		}
	}

	return ret;
}
