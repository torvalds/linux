/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Thomas Skibo
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
 *
 * $FreeBSD$
 */

/* 
 * Zynq-7000 Devcfg driver.  This allows programming the PL (FPGA) section
 * of Zynq.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.  PL Configuration is
 * covered in section 6.4.5.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/xilinx/zy7_slcr.h>

struct zy7_devcfg_softc {
	device_t	dev;
	struct mtx	sc_mtx;
	struct resource	*mem_res;
	struct resource *irq_res;
	struct cdev	*sc_ctl_dev;
	void		*intrhandle;

	bus_dma_tag_t	dma_tag;
	bus_dmamap_t	dma_map;

	int		is_open;

	struct sysctl_ctx_list sysctl_tree;
	struct sysctl_oid *sysctl_tree_top;
};

static struct zy7_devcfg_softc *zy7_devcfg_softc_p;

#define	FCLK_NUM	4

struct zy7_fclk_config {
	int		source;
	int		frequency;
	int		actual_frequency;
};

static struct zy7_fclk_config fclk_configs[FCLK_NUM];

#define DEVCFG_SC_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	DEVCFG_SC_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define DEVCFG_SC_LOCK_INIT(sc) \
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->dev),	\
	    "zy7_devcfg", MTX_DEF)
#define DEVCFG_SC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->sc_mtx);
#define DEVCFG_SC_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED);

#define RD4(sc, off) 		(bus_read_4((sc)->mem_res, (off)))
#define WR4(sc, off, val) 	(bus_write_4((sc)->mem_res, (off), (val)))

SYSCTL_NODE(_hw, OID_AUTO, fpga, CTLFLAG_RD, 0,	\
	    "Xilinx Zynq-7000 PL (FPGA) section");

static int zy7_devcfg_sysctl_pl_done(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_hw_fpga, OID_AUTO, pl_done, CTLTYPE_INT | CTLFLAG_RD, NULL, 0,
	    zy7_devcfg_sysctl_pl_done, "I", "PL section config DONE signal");

static int zy7_en_level_shifters = 1;
SYSCTL_INT(_hw_fpga, OID_AUTO, en_level_shifters, CTLFLAG_RW,
	   &zy7_en_level_shifters, 0,
	   "Enable PS-PL level shifters after device config");

static int zy7_ps_vers = 0;
SYSCTL_INT(_hw, OID_AUTO, ps_vers, CTLFLAG_RD, &zy7_ps_vers, 0,
	   "Zynq-7000 PS version");

static int zy7_devcfg_fclk_sysctl_level_shifters(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_hw_fpga, OID_AUTO, level_shifters, 
		    CTLFLAG_RW | CTLTYPE_INT, 
		    NULL, 0, zy7_devcfg_fclk_sysctl_level_shifters,
		    "I", "Enable/disable level shifters");

/* cdev entry points. */
static int zy7_devcfg_open(struct cdev *, int, int, struct thread *);
static int zy7_devcfg_write(struct cdev *, struct uio *, int);
static int zy7_devcfg_close(struct cdev *, int, int, struct thread *);

struct cdevsw zy7_devcfg_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	zy7_devcfg_open,
	.d_write =	zy7_devcfg_write,
	.d_close =	zy7_devcfg_close,
	.d_name =	"devcfg",
};

/* Devcfg block registers. */
#define ZY7_DEVCFG_CTRL			0x0000
#define   ZY7_DEVCFG_CTRL_FORCE_RST		(1<<31)
#define   ZY7_DEVCFG_CTRL_PCFG_PROG_B		(1<<30)
#define   ZY7_DEVCFG_CTRL_PCFG_POR_CNT_4K	(1<<29)
#define   ZY7_DEVCFG_CTRL_PCAP_PR		(1<<27)
#define   ZY7_DEVCFG_CTRL_PCAP_MODE		(1<<26)
#define   ZY7_DEVCFG_CTRL_QTR_PCAP_RATE_EN	(1<<25)
#define   ZY7_DEVCFG_CTRL_MULTIBOOT_EN		(1<<24)
#define   ZY7_DEVCFG_CTRL_JTAG_CHAIN_DIS	(1<<23)
#define   ZY7_DEVCFG_CTRL_USER_MODE		(1<<15)
#define   ZY7_DEVCFG_CTRL_RESVD_WR11		(3<<13)	/* always write 11 */
#define   ZY7_DEVCFG_CTRL_PCFG_AES_FUSE		(1<<12)
#define   ZY7_DEVCFG_CTRL_PCFG_AES_EN_MASK	(7<<9)	/* all 1's or 0's */
#define   ZY7_DEVCFG_CTRL_SEU_EN		(1<<8)
#define   ZY7_DEVCFG_CTRL_SEC_EN		(1<<7)
#define   ZY7_DEVCFG_CTRL_SPNIDEN		(1<<6)
#define   ZY7_DEVCFG_CTRL_SPIDEN		(1<<5)
#define   ZY7_DEVCFG_CTRL_NIDEN			(1<<4)
#define   ZY7_DEVCFG_CTRL_DBGEN			(1<<3)
#define   ZY7_DEVCFG_CTRL_DAP_EN_MASK		(7<<0)	/* all 1's to enable */

#define ZY7_DEVCFG_LOCK			0x004
#define   ZY7_DEVCFG_LOCK_AES_FUSE_LOCK		(1<<4)
#define   ZY7_DEVCFG_LOCK_AES_EN		(1<<3)
#define   ZY7_DEVCFG_LOCK_SEU_LOCK		(1<<2)
#define   ZY7_DEVCFG_LOCK_SEC_LOCK		(1<<1)
#define   ZY7_DEVCFG_LOCK_DBG_LOCK		(1<<0)

#define ZY7_DEVCFG_CFG			0x008
#define   ZY7_DEVCFG_CFG_RFIFO_TH_MASK		(3<<10)
#define   ZY7_DEVCFG_CFG_WFIFO_TH_MASK		(3<<8)
#define   ZY7_DEVCFG_CFG_RCLK_EDGE		(1<<7)
#define   ZY7_DEVCFG_CFG_WCLK_EDGE		(1<<6)
#define   ZY7_DEVCFG_CFG_DIS_SRC_INC		(1<<5)
#define   ZY7_DEVCFG_CFG_DIS_DST_INC		(1<<4)

#define ZY7_DEVCFG_INT_STATUS		0x00C
#define ZY7_DEVCFG_INT_MASK		0x010
#define   ZY7_DEVCFG_INT_PSS_GTS_USR_B		(1<<31)
#define   ZY7_DEVCFG_INT_PSS_FST_CFG_B		(1<<30)
#define   ZY7_DEVCFG_INT_PSS_GPWRDWN_B		(1<<29)
#define   ZY7_DEVCFG_INT_PSS_GTS_CFG_B		(1<<28)
#define   ZY7_DEVCFG_INT_CFG_RESET_B		(1<<27)
#define   ZY7_DEVCFG_INT_AXI_WTO		(1<<23)	/* axi write timeout */
#define   ZY7_DEVCFG_INT_AXI_WERR		(1<<22)	/* axi write err */
#define   ZY7_DEVCFG_INT_AXI_RTO		(1<<21)	/* axi read timeout */
#define   ZY7_DEVCFG_INT_AXI_RERR		(1<<20)	/* axi read err */
#define   ZY7_DEVCFG_INT_RX_FIFO_OV		(1<<18)	/* rx fifo overflow */
#define   ZY7_DEVCFG_INT_WR_FIFO_LVL		(1<<17)	/* wr fifo < level */
#define   ZY7_DEVCFG_INT_RD_FIFO_LVL		(1<<16)	/* rd fifo >= level */
#define   ZY7_DEVCFG_INT_DMA_CMD_ERR		(1<<15)
#define   ZY7_DEVCFG_INT_DMA_Q_OV		(1<<14)
#define   ZY7_DEVCFG_INT_DMA_DONE		(1<<13)
#define   ZY7_DEVCFG_INT_DMA_PCAP_DONE		(1<<12)
#define   ZY7_DEVCFG_INT_P2D_LEN_ERR		(1<<11)
#define   ZY7_DEVCFG_INT_PCFG_HMAC_ERR		(1<<6)
#define   ZY7_DEVCFG_INT_PCFG_SEU_ERR		(1<<5)
#define   ZY7_DEVCFG_INT_PCFG_POR_B		(1<<4)
#define   ZY7_DEVCFG_INT_PCFG_CFG_RST		(1<<3)
#define   ZY7_DEVCFG_INT_PCFG_DONE		(1<<2)
#define   ZY7_DEVCFG_INT_PCFG_INIT_PE		(1<<1)
#define   ZY7_DEVCFG_INT_PCFG_INIT_NE		(1<<0)
#define   ZY7_DEVCFG_INT_ERRORS			0x00f0f860
#define   ZY7_DEVCFG_INT_ALL			0xf8f7f87f

#define ZY7_DEVCFG_STATUS		0x014
#define   ZY7_DEVCFG_STATUS_DMA_CMD_Q_F		(1<<31)	/* cmd queue full */
#define   ZY7_DEVCFG_STATUS_DMA_CMD_Q_E		(1<<30) /* cmd queue empty */
#define   ZY7_DEVCFG_STATUS_DONE_COUNT_MASK	(3<<28)
#define   ZY7_DEVCFG_STATUS_DONE_COUNT_SHIFT	28
#define   ZY7_DEVCFG_STATUS_RX_FIFO_LVL_MASK	(0x1f<<20)
#define   ZY7_DEVCFG_STATUS_RX_FIFO_LVL_SHIFT	20
#define   ZY7_DEVCFG_STATUS_TX_FIFO_LVL_MASK	(0x7f<<12)
#define   ZY7_DEVCFG_STATUS_TX_FIFO_LVL_SHIFT	12
#define   ZY7_DEVCFG_STATUS_PSS_GTS_USR_B	(1<<11)
#define   ZY7_DEVCFG_STATUS_PSS_FST_CFG_B	(1<<10)
#define   ZY7_DEVCFG_STATUS_PSS_GPWRDWN_B	(1<<9)
#define   ZY7_DEVCFG_STATUS_PSS_GTS_CFG_B	(1<<8)
#define   ZY7_DEVCFG_STATUS_ILL_APB_ACCE	(1<<6)
#define   ZY7_DEVCFG_STATUS_PSS_CFG_RESET_B	(1<<5)
#define   ZY7_DEVCFG_STATUS_PCFG_INIT		(1<<4)
#define   ZY7_DEVCFG_STATUS_EFUSE_BBRAM_KEY_DIS	(1<<3)
#define   ZY7_DEVCFG_STATUS_EFUSE_SEC_EN	(1<<2)
#define   ZY7_DEVCFG_STATUS_EFUSE_JTAG_DIS	(1<<1)

#define ZY7_DEVCFG_DMA_SRC_ADDR		0x018
#define ZY7_DEVCFG_DMA_DST_ADDR		0x01c
#define   ZY7_DEVCFG_DMA_ADDR_WAIT_PCAP	1
#define   ZY7_DEVCFG_DMA_ADDR_ILLEGAL		0xffffffff

#define ZY7_DEVCFG_DMA_SRC_LEN		0x020	/* in 4-byte words. */
#define ZY7_DEVCFG_DMA_SRC_LEN_MAX		0x7ffffff
#define ZY7_DEVCFG_DMA_DST_LEN		0x024
#define ZY7_DEVCFG_ROM_SHADOW		0x028
#define ZY7_DEVCFG_MULTIBOOT_ADDR	0x02c
#define ZY7_DEVCFG_SW_ID		0x030
#define ZY7_DEVCFG_UNLOCK		0x034
#define ZY7_DEVCFG_UNLOCK_MAGIC			0x757bdf0d
#define ZY7_DEVCFG_MCTRL		0x080
#define   ZY7_DEVCFG_MCTRL_PS_VERS_MASK		(0xf<<28)
#define   ZY7_DEVCFG_MCTRL_PS_VERS_SHIFT	28
#define   ZY7_DEVCFG_MCTRL_PCFG_POR_B		(1<<8)
#define   ZY7_DEVCFG_MCTRL_INT_PCAP_LPBK	(1<<4)
#define ZY7_DEVCFG_XADCIF_CFG		0x100
#define ZY7_DEVCFG_XADCIF_INT_STAT	0x104
#define ZY7_DEVCFG_XADCIF_INT_MASK	0x108
#define ZY7_DEVCFG_XADCIF_MSTS		0x10c
#define ZY7_DEVCFG_XADCIF_CMD_FIFO	0x110
#define ZY7_DEVCFG_XADCIF_RD_FIFO	0x114
#define ZY7_DEVCFG_XADCIF_MCTL		0x118

static int
zy7_devcfg_fclk_sysctl_source(SYSCTL_HANDLER_ARGS)
{
	char buf[4];
	struct zy7_fclk_config *cfg;
	int unit;
	int error;

	cfg = arg1;
	unit = arg2;

	switch (cfg->source) {
		case ZY7_PL_FCLK_SRC_IO:
		case ZY7_PL_FCLK_SRC_IO_ALT:
			strncpy(buf, "IO", sizeof(buf));
			break;
		case ZY7_PL_FCLK_SRC_DDR:
			strncpy(buf, "DDR", sizeof(buf));
			break;
		case ZY7_PL_FCLK_SRC_ARM:
			strncpy(buf, "ARM", sizeof(buf));
			break;
		default:
			strncpy(buf, "???", sizeof(buf));
			break;
	}

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (strcasecmp(buf, "io") == 0)
		cfg->source = ZY7_PL_FCLK_SRC_IO;
	else if (strcasecmp(buf, "ddr") == 0)
		cfg->source = ZY7_PL_FCLK_SRC_DDR;
	else if (strcasecmp(buf, "arm") == 0)
		cfg->source = ZY7_PL_FCLK_SRC_ARM;
	else
		return (EINVAL);

	zy7_pl_fclk_set_source(unit, cfg->source);
	if (cfg->frequency > 0)
		cfg->actual_frequency = zy7_pl_fclk_get_freq(unit);

	return (0);
}

static int
zy7_devcfg_fclk_sysctl_freq(SYSCTL_HANDLER_ARGS)
{
	struct zy7_fclk_config *cfg;
	int unit;
	int error;
	int freq;
	int new_actual_freq;

	cfg = arg1;
	unit = arg2;

	freq = cfg->frequency;

	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (freq > 0) {
		new_actual_freq = zy7_pl_fclk_set_freq(unit, freq);
		if (new_actual_freq < 0)
			return (EINVAL);
		if (!zy7_pl_fclk_enabled(unit))
			zy7_pl_fclk_enable(unit);
	}
	else {
		zy7_pl_fclk_disable(unit);
		new_actual_freq = 0;
	}

	cfg->frequency = freq;
	cfg->actual_frequency = new_actual_freq;

	return (0);
}

static int
zy7_devcfg_fclk_sysctl_level_shifters(SYSCTL_HANDLER_ARGS)
{
	int error, enabled;

	enabled = zy7_pl_level_shifters_enabled();

	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (enabled)
		zy7_pl_level_shifters_enable();
	else
		zy7_pl_level_shifters_disable();

	return (0);
}

static int
zy7_devcfg_init_fclk_sysctl(struct zy7_devcfg_softc *sc)
{
	struct sysctl_oid *fclk_node;
	char fclk_num[4];
	int i;

	sysctl_ctx_init(&sc->sysctl_tree);
	sc->sysctl_tree_top = SYSCTL_ADD_NODE(&sc->sysctl_tree,
	    SYSCTL_STATIC_CHILDREN(_hw_fpga), OID_AUTO, "fclk",
	    CTLFLAG_RD, 0, "");
	if (sc->sysctl_tree_top == NULL) {
		sysctl_ctx_free(&sc->sysctl_tree);
		return (-1);
	}

	for (i = 0; i < FCLK_NUM; i++) {
		snprintf(fclk_num, sizeof(fclk_num), "%d", i);
		fclk_node = SYSCTL_ADD_NODE(&sc->sysctl_tree,
		    SYSCTL_CHILDREN(sc->sysctl_tree_top), OID_AUTO, fclk_num,
		    CTLFLAG_RD, 0, "");

		SYSCTL_ADD_INT(&sc->sysctl_tree,
		    SYSCTL_CHILDREN(fclk_node), OID_AUTO,
		    "actual_freq", CTLFLAG_RD, 
		    &fclk_configs[i].actual_frequency, i,
		    "Actual frequency");
		SYSCTL_ADD_PROC(&sc->sysctl_tree,
		    SYSCTL_CHILDREN(fclk_node), OID_AUTO,
		    "freq", CTLFLAG_RW | CTLTYPE_INT, 
		    &fclk_configs[i], i,
		    zy7_devcfg_fclk_sysctl_freq,
		    "I", "Configured frequency");
		SYSCTL_ADD_PROC(&sc->sysctl_tree,
		    SYSCTL_CHILDREN(fclk_node), OID_AUTO,
		    "source", CTLFLAG_RW | CTLTYPE_STRING, 
		    &fclk_configs[i], i, 
		    zy7_devcfg_fclk_sysctl_source,
		    "A", "Clock source");
	}

	return (0);
}

/* Enable programming the PL through PCAP. */
static void
zy7_devcfg_init_hw(struct zy7_devcfg_softc *sc)
{

	DEVCFG_SC_ASSERT_LOCKED(sc);

	/* Set devcfg control register. */
	WR4(sc, ZY7_DEVCFG_CTRL,
	    ZY7_DEVCFG_CTRL_PCFG_PROG_B |
	    ZY7_DEVCFG_CTRL_PCAP_PR |
	    ZY7_DEVCFG_CTRL_PCAP_MODE |
	    ZY7_DEVCFG_CTRL_USER_MODE |
	    ZY7_DEVCFG_CTRL_RESVD_WR11 |
	    ZY7_DEVCFG_CTRL_SPNIDEN |
	    ZY7_DEVCFG_CTRL_SPIDEN |
	    ZY7_DEVCFG_CTRL_NIDEN |
	    ZY7_DEVCFG_CTRL_DBGEN |
	    ZY7_DEVCFG_CTRL_DAP_EN_MASK);

	/* Turn off internal PCAP loopback. */
	WR4(sc, ZY7_DEVCFG_MCTRL, RD4(sc, ZY7_DEVCFG_MCTRL) &
	    ~ZY7_DEVCFG_MCTRL_INT_PCAP_LPBK);
}

/* Clear previous configuration of the PL by asserting PROG_B. */
static int
zy7_devcfg_reset_pl(struct zy7_devcfg_softc *sc)
{
	uint32_t devcfg_ctl;
	int tries, err;

	DEVCFG_SC_ASSERT_LOCKED(sc);

	devcfg_ctl = RD4(sc, ZY7_DEVCFG_CTRL);

	/* Clear sticky bits and set up INIT signal positive edge interrupt. */
	WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_ALL);
	WR4(sc, ZY7_DEVCFG_INT_MASK, ~ZY7_DEVCFG_INT_PCFG_INIT_PE);

	/* Deassert PROG_B (active low). */
	devcfg_ctl |= ZY7_DEVCFG_CTRL_PCFG_PROG_B;
	WR4(sc, ZY7_DEVCFG_CTRL, devcfg_ctl);

	/*
	 * Wait for INIT to assert.  If it is already asserted, we may not get
	 * an edge interrupt so cancel it and continue.
	 */
	if ((RD4(sc, ZY7_DEVCFG_STATUS) &
	     ZY7_DEVCFG_STATUS_PCFG_INIT) != 0) {
		/* Already asserted.  Cancel interrupt. */
		WR4(sc, ZY7_DEVCFG_INT_MASK, ~0);
	}
	else {
		/* Wait for positive edge interrupt. */
		err = mtx_sleep(sc, &sc->sc_mtx, PCATCH, "zy7i1", hz);
		if (err != 0)
			return (err);
	}
	
	/* Reassert PROG_B (active low). */
	devcfg_ctl &= ~ZY7_DEVCFG_CTRL_PCFG_PROG_B;
	WR4(sc, ZY7_DEVCFG_CTRL, devcfg_ctl);

	/* Wait for INIT deasserted.  This happens almost instantly. */
	tries = 0;
	while ((RD4(sc, ZY7_DEVCFG_STATUS) &
		ZY7_DEVCFG_STATUS_PCFG_INIT) != 0) {
		if (++tries >= 100)
			return (EIO);
		DELAY(5);
	}

	/* Clear sticky bits and set up INIT positive edge interrupt. */
	WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_ALL);
	WR4(sc, ZY7_DEVCFG_INT_MASK, ~ZY7_DEVCFG_INT_PCFG_INIT_PE);

	/* Deassert PROG_B again. */
	devcfg_ctl |= ZY7_DEVCFG_CTRL_PCFG_PROG_B;
	WR4(sc, ZY7_DEVCFG_CTRL, devcfg_ctl);

	/*
	 * Wait for INIT asserted indicating FPGA internal initialization
	 * is complete.
	 */
	err = mtx_sleep(sc, &sc->sc_mtx, PCATCH, "zy7i2", hz);
	if (err != 0)
		return (err);

	/* Clear sticky DONE bit in interrupt status. */
	WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_ALL);

	return (0);
}

/* Callback function for bus_dmamap_load(). */
static void
zy7_dma_cb2(void *arg, bus_dma_segment_t *seg, int nsegs, int error)
{
	if (!error && nsegs == 1)
		*(bus_addr_t *)arg = seg[0].ds_addr;
}

static int
zy7_devcfg_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct zy7_devcfg_softc *sc = dev->si_drv1;
	int err;

	DEVCFG_SC_LOCK(sc);
	if (sc->is_open) {
		DEVCFG_SC_UNLOCK(sc);
		return (EBUSY);
	}

	sc->dma_map = NULL;
	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), 4, 0,
				 BUS_SPACE_MAXADDR_32BIT,
				 BUS_SPACE_MAXADDR,
				 NULL, NULL,
				 PAGE_SIZE,
				 1,
				 PAGE_SIZE,
				 0,
				 busdma_lock_mutex,
				 &sc->sc_mtx,
				 &sc->dma_tag);
	if (err) {
		DEVCFG_SC_UNLOCK(sc);
		return (err);
	}

	sc->is_open = 1;
	DEVCFG_SC_UNLOCK(sc);
	return (0);
}

static int
zy7_devcfg_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct zy7_devcfg_softc *sc = dev->si_drv1;
	void *dma_mem;
	bus_addr_t dma_physaddr;
	int segsz, err;

	DEVCFG_SC_LOCK(sc);

	/* First write?  Reset PL. */
	if (uio->uio_offset == 0 && uio->uio_resid > 0)	{
		zy7_devcfg_init_hw(sc);
		zy7_slcr_preload_pl();
		err = zy7_devcfg_reset_pl(sc);
		if (err != 0) {
			DEVCFG_SC_UNLOCK(sc);
			return (err);
		}
	}

	/* Allocate dma memory and load. */
	err = bus_dmamem_alloc(sc->dma_tag, &dma_mem, BUS_DMA_NOWAIT,
			       &sc->dma_map);
	if (err != 0) {
		DEVCFG_SC_UNLOCK(sc);
		return (err);
	}
	err = bus_dmamap_load(sc->dma_tag, sc->dma_map, dma_mem, PAGE_SIZE,
			      zy7_dma_cb2, &dma_physaddr, 0);
	if (err != 0) {
		bus_dmamem_free(sc->dma_tag, dma_mem, sc->dma_map);
		DEVCFG_SC_UNLOCK(sc);
		return (err);
	}

	while (uio->uio_resid > 0) {
		/* If DONE signal has been set, we shouldn't write anymore. */
		if ((RD4(sc, ZY7_DEVCFG_INT_STATUS) &
		     ZY7_DEVCFG_INT_PCFG_DONE) != 0) {
			err = EIO;
			break;
		}

		/* uiomove the data from user buffer to our dma map. */
		segsz = MIN(PAGE_SIZE, uio->uio_resid);
		DEVCFG_SC_UNLOCK(sc);
		err = uiomove(dma_mem, segsz, uio);
		DEVCFG_SC_LOCK(sc);
		if (err != 0)
			break;

		/* Flush the cache to memory. */
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				BUS_DMASYNC_PREWRITE);

		/* Program devcfg's DMA engine.  The ordering of these
		 * register writes is critical.
		 */
		if (uio->uio_resid > segsz)
			WR4(sc, ZY7_DEVCFG_DMA_SRC_ADDR,
			    (uint32_t) dma_physaddr);
		else
			WR4(sc, ZY7_DEVCFG_DMA_SRC_ADDR,
			    (uint32_t) dma_physaddr |
			    ZY7_DEVCFG_DMA_ADDR_WAIT_PCAP);
		WR4(sc, ZY7_DEVCFG_DMA_DST_ADDR, ZY7_DEVCFG_DMA_ADDR_ILLEGAL);
		WR4(sc, ZY7_DEVCFG_DMA_SRC_LEN, (segsz+3)/4);
		WR4(sc, ZY7_DEVCFG_DMA_DST_LEN, 0);

		/* Now clear done bit and set up DMA done interrupt. */
		WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_ALL);
		WR4(sc, ZY7_DEVCFG_INT_MASK, ~ZY7_DEVCFG_INT_DMA_DONE);

		/* Wait for DMA done interrupt. */
		err = mtx_sleep(sc->dma_map, &sc->sc_mtx, PCATCH,
				"zy7dma", hz);
		if (err != 0)
			break;

		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				BUS_DMASYNC_POSTWRITE);

		/* Check DONE signal. */
		if ((RD4(sc, ZY7_DEVCFG_INT_STATUS) &
		     ZY7_DEVCFG_INT_PCFG_DONE) != 0)
			zy7_slcr_postload_pl(zy7_en_level_shifters);
	}

	bus_dmamap_unload(sc->dma_tag, sc->dma_map);
	bus_dmamem_free(sc->dma_tag, dma_mem, sc->dma_map);
	DEVCFG_SC_UNLOCK(sc);
	return (err);
}

static int
zy7_devcfg_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct zy7_devcfg_softc *sc = dev->si_drv1;

	DEVCFG_SC_LOCK(sc);
	sc->is_open = 0;
	bus_dma_tag_destroy(sc->dma_tag);
	DEVCFG_SC_UNLOCK(sc);

	zy7_slcr_postload_pl(zy7_en_level_shifters);

	return (0);
}

static void
zy7_devcfg_intr(void *arg)
{
	struct zy7_devcfg_softc *sc = (struct zy7_devcfg_softc *)arg;
	uint32_t istatus, imask;

	DEVCFG_SC_LOCK(sc);

	istatus = RD4(sc, ZY7_DEVCFG_INT_STATUS);
	imask = ~RD4(sc, ZY7_DEVCFG_INT_MASK);

	/* Turn interrupt off. */
	WR4(sc, ZY7_DEVCFG_INT_MASK, ~0);

	if ((istatus & imask) == 0) {
		DEVCFG_SC_UNLOCK(sc);
		return;
	}

	/* DMA done? */
	if ((istatus & ZY7_DEVCFG_INT_DMA_DONE) != 0)
		wakeup(sc->dma_map);

	/* INIT_B positive edge? */
	if ((istatus & ZY7_DEVCFG_INT_PCFG_INIT_PE) != 0)
		wakeup(sc);

	DEVCFG_SC_UNLOCK(sc);
}

/* zy7_devcfg_sysctl_pl_done() returns status of the PL_DONE signal.
 */
static int
zy7_devcfg_sysctl_pl_done(SYSCTL_HANDLER_ARGS)
{
	struct zy7_devcfg_softc *sc = zy7_devcfg_softc_p;
	int pl_done = 0;

	if (sc) {
		DEVCFG_SC_LOCK(sc);

		/* PCFG_DONE bit is sticky.  Clear it before checking it. */
		WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_PCFG_DONE);
		pl_done = ((RD4(sc, ZY7_DEVCFG_INT_STATUS) &
			    ZY7_DEVCFG_INT_PCFG_DONE) != 0);

		DEVCFG_SC_UNLOCK(sc);
	}
	return (sysctl_handle_int(oidp, &pl_done, 0, req));
}

static int
zy7_devcfg_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "xlnx,zy7_devcfg"))
		return (ENXIO);

	device_set_desc(dev, "Zynq devcfg block");
	return (0);
}

static int zy7_devcfg_detach(device_t dev);

static int
zy7_devcfg_attach(device_t dev)
{
	struct zy7_devcfg_softc *sc = device_get_softc(dev);
	int i;
	int rid, err;

	/* Allow only one attach. */
	if (zy7_devcfg_softc_p != NULL)
		return (ENXIO);

	sc->dev = dev;

	DEVCFG_SC_LOCK_INIT(sc);

	/* Get memory resource. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					     RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resources.\n");
		zy7_devcfg_detach(dev);
		return (ENOMEM);
	}

	/* Allocate IRQ. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					     RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "cannot allocate IRQ\n");
		zy7_devcfg_detach(dev);
		return (ENOMEM);
	}

	/* Activate the interrupt. */
	err = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
			     NULL, zy7_devcfg_intr, sc, &sc->intrhandle);
	if (err) {
		device_printf(dev, "cannot setup IRQ\n");
		zy7_devcfg_detach(dev);
		return (err);
	}

	/* Create /dev/devcfg */
	sc->sc_ctl_dev = make_dev(&zy7_devcfg_cdevsw, 0,
			  UID_ROOT, GID_WHEEL, 0600, "devcfg");
	if (sc->sc_ctl_dev == NULL) {
		device_printf(dev, "failed to create /dev/devcfg");
		zy7_devcfg_detach(dev);
		return (ENXIO);
	}
	sc->sc_ctl_dev->si_drv1 = sc;

	zy7_devcfg_softc_p = sc;

	/* Unlock devcfg registers. */
	WR4(sc, ZY7_DEVCFG_UNLOCK, ZY7_DEVCFG_UNLOCK_MAGIC);

	/* Make sure interrupts are completely disabled. */
	WR4(sc, ZY7_DEVCFG_INT_STATUS, ZY7_DEVCFG_INT_ALL);
	WR4(sc, ZY7_DEVCFG_INT_MASK, 0xffffffff);

	/* Get PS_VERS for SYSCTL. */
	zy7_ps_vers = (RD4(sc, ZY7_DEVCFG_MCTRL) &
		       ZY7_DEVCFG_MCTRL_PS_VERS_MASK) >>
		ZY7_DEVCFG_MCTRL_PS_VERS_SHIFT;

	for (i = 0; i < FCLK_NUM; i++) {
		fclk_configs[i].source = zy7_pl_fclk_get_source(i);
		fclk_configs[i].actual_frequency = 
			zy7_pl_fclk_enabled(i) ? zy7_pl_fclk_get_freq(i) : 0;
		/* Initially assume actual frequency is the configure one */
		fclk_configs[i].frequency = fclk_configs[i].actual_frequency;
	}

	if (zy7_devcfg_init_fclk_sysctl(sc) < 0)
		device_printf(dev, "failed to initialized sysctl tree\n");

	return (0);
}

static int
zy7_devcfg_detach(device_t dev)
{
	struct zy7_devcfg_softc *sc = device_get_softc(dev);

	if (sc->sysctl_tree_top != NULL) {
		sysctl_ctx_free(&sc->sysctl_tree);
		sc->sysctl_tree_top = NULL;
	}

	if (device_is_attached(dev))
		bus_generic_detach(dev);

	/* Get rid of /dev/devcfg0. */
	if (sc->sc_ctl_dev != NULL)
		destroy_dev(sc->sc_ctl_dev);

	/* Teardown and release interrupt. */
	if (sc->irq_res != NULL) {
		if (sc->intrhandle)
			bus_teardown_intr(dev, sc->irq_res, sc->intrhandle);
		bus_release_resource(dev, SYS_RES_IRQ,
			     rman_get_rid(sc->irq_res), sc->irq_res);
	}

	/* Release memory resource. */
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
			     rman_get_rid(sc->mem_res), sc->mem_res);

	zy7_devcfg_softc_p = NULL;

	DEVCFG_SC_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t zy7_devcfg_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe, 	zy7_devcfg_probe),
	DEVMETHOD(device_attach, 	zy7_devcfg_attach),
	DEVMETHOD(device_detach, 	zy7_devcfg_detach),

	DEVMETHOD_END
};

static driver_t zy7_devcfg_driver = {
	"zy7_devcfg",
	zy7_devcfg_methods,
	sizeof(struct zy7_devcfg_softc),
};
static devclass_t zy7_devcfg_devclass;

DRIVER_MODULE(zy7_devcfg, simplebus, zy7_devcfg_driver, zy7_devcfg_devclass, \
	      0, 0);
MODULE_DEPEND(zy7_devcfg, zy7_slcr, 1, 1, 1);
