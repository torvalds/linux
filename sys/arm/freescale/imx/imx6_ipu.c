/*-
 * Copyright 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/clock.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/vt/vt.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <arm/freescale/imx/imx6_src.h>
#include <arm/freescale/imx/imx_ccmvar.h>

#include "fb_if.h"
#include "hdmi_if.h"

#define EDID_DEBUG_not

static int have_ipu = 0;

#define	LDB_CLOCK_RATE	280000000

#define	MODE_HBP(mode)	((mode)->htotal - (mode)->hsync_end)
#define	MODE_HFP(mode)	((mode)->hsync_start - (mode)->hdisplay)
#define	MODE_HSW(mode)	((mode)->hsync_end - (mode)->hsync_start)
#define	MODE_VBP(mode)	((mode)->vtotal - (mode)->vsync_end)
#define	MODE_VFP(mode)	((mode)->vsync_start - (mode)->vdisplay)
#define	MODE_VSW(mode)	((mode)->vsync_end - (mode)->vsync_start)

#define	MODE_BPP	16
#define	MODE_PIXEL_CLOCK_INVERT	1

#define M(nm,hr,vr,clk,hs,he,ht,vs,ve,vt,f) \
	{ clk, hr, hs, he, ht, vr, vs, ve, vt, f, nm }

static struct videomode mode1024x768 = M("1024x768x60",1024,768,65000,1048,1184,1344,771,777,806,VID_NHSYNC|VID_PHSYNC);

#define	DMA_CHANNEL	23
#define	DC_CHAN5	5
#define	DI_PORT		0

#define	IPU_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	IPU_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	IPU_LOCK_INIT(_sc)	mtx_init(&(_sc)->sc_mtx, \
    device_get_nameunit(_sc->sc_dev), "ipu", MTX_DEF)
#define	IPU_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)

#define	IPU_READ4(_sc, reg)	bus_read_4((_sc)->sc_mem_res, (reg))
#define	IPU_WRITE4(_sc, reg, value)	\
    bus_write_4((_sc)->sc_mem_res, (reg), (value))

#define	CPMEM_BASE	0x300000
#define	DC_TEMPL_BASE	0x380000

/* Microcode */
/* Word 1 */
#define	TEMPLATE_SYNC(v)	((v) << 0)
#define	TEMPLATE_GLUELOGIC(v)	((v) << 4)
#define	TEMPLATE_MAPPING(v)	((v) << 15)
#define	TEMPLATE_WAVEFORM(v)	((v) << 11)
#define		GLUELOGIC_KEEP_ASSERTED	(1 << 3)
#define		GLUELOGIC_KEEP_NEGATED	(1 << 2)
/* Word 2 */
#define	TEMPLATE_OPCODE(v)	((v) << 4)
#define		OPCODE_WROD		0x18
#define	TEMPLATE_STOP		(1 << 9)

#define	IPU_CONF		0x200000
#define		IPU_CONF_DMFC_EN	(1 << 10)
#define		IPU_CONF_DC_EN		(1 << 9)
#define		IPU_CONF_DI1_EN		(1 << 7)
#define		IPU_CONF_DI0_EN		(1 << 6)
#define		IPU_CONF_DP_EN		(1 << 5)
#define	IPU_DISP_GEN		0x2000C4
#define		DISP_GEN_DI1_CNTR_RELEASE	(1 << 25)
#define		DISP_GEN_DI0_CNTR_RELEASE	(1 << 24)
#define		DISP_GEN_MCU_MAX_BURST_STOP	(1 << 22)
#define		DISP_GEN_MCU_T_SHIFT		18
#define	IPU_MEM_RST		0x2000DC
#define		IPU_MEM_RST_START	(1 << 31)
#define		IPU_MEM_RST_ALL		0x807FFFFF
#define	IPU_CH_DB_MODE_SEL_0	0x200150
#define	IPU_CH_DB_MODE_SEL_1	0x200154
#define	IPU_CUR_BUF_0		0x20023C
#define	IPU_CUR_BUF_1		0x200240

#define	IPU_IDMAC_CH_EN_1	0x208004
#define	IPU_IDMAC_CH_EN_2	0x208008
#define	IPU_IDMAC_CH_PRI_1	0x208014
#define	IPU_IDMAC_CH_PRI_2	0x208018

#define	IPU_DI0_GENERAL		0x240000
#define		DI_CLOCK_EXTERNAL	(1 << 20)
#define		DI_GENERAL_POL_CLK	(1 << 17)
#define		DI_GENERAL_POLARITY_3	(1 << 2)
#define		DI_GENERAL_POLARITY_2	(1 << 1)
#define	IPU_DI0_BS_CLKGEN0	0x240004
#define		DI_BS_CLKGEN0(_int, _frac)	(((_int) << 4) | (_frac))
#define	IPU_DI0_BS_CLKGEN1	0x240008
#define		DI_BS_CLKGEN1_DOWN(_int, _frac)	((((_int) << 1) | (_frac)) << 16)
#define	IPU_DI0_SW_GEN0_1	0x24000C
#define		DI_RUN_VALUE_M1(v)	((v) << 19)
#define		DI_RUN_RESOLUTION(v)	((v) << 16)
#define		DI_OFFSET_VALUE(v)	((v) << 3)
#define	IPU_DI0_SW_GEN1_1	0x240030
#define		DI_CNT_POLARITY_GEN_EN(v)	((v) << 29)
#define		DI_CNT_AUTO_RELOAD		(1 << 28)
#define		DI_CNT_CLR_SEL(v)		((v) << 25)
#define		DI_CNT_DOWN(v)			((v) << 16)
#define		DI_CNT_POLARITY_TRIGGER_SEL(v)	((v) << 12)
#define		DI_CNT_POLARITY_CLR_SEL(v)	((v) << 9)
#define	IPU_DI0_SYNC_AS_GEN	0x240054
#define		SYNC_AS_GEN_VSYNC_SEL(v)	((v) << 13)
#define		SYNC_AS_GEN_SYNC_START(v)	((v) << 0)
#define	IPU_DI0_DW_GEN_0	0x240058
#define		DW_GEN_DI_ACCESS_SIZE(v)	((v) << 24)
#define		DW_GEN_DI_COMPONENT_SIZE(v)	((v) << 16)
#define		DW_GEN_DI_SET_MASK		3
#define		DW_GEN_DI_PIN_15_SET(v)		((v) << 8)
#define	IPU_DI0_DW_SET3_0	0x240118
#define		DW_SET_DATA_CNT_DOWN(v)	((v) << 16)
#define		DW_SET_DATA_CNT_UP(v)	((v) << 0)
#define	IPU_DI0_STP_REP		0x240148
#define	IPU_DI0_POL		0x240164
#define		DI_POL_DRDY_POLARITY_15		(1 << 4)
#define	IPU_DI0_SCR_CONF	0x240170

#define	IPU_DI1_GENERAL		0x248000
#define	IPU_DI1_BS_CLKGEN0	0x248004
#define	IPU_DI1_BS_CLKGEN1	0x248008
#define	IPU_DI1_SW_GEN0_1	0x24800C
#define	IPU_DI1_SW_GEN1_1	0x248030
#define	IPU_DI1_SYNC_AS_GEN	0x248054
#define	IPU_DI1_DW_GEN_0	0x248058
#define	IPU_DI1_POL		0x248164
#define	IPU_DI1_DW_SET3_0	0x248118
#define	IPU_DI1_STP_REP		0x248148
#define	IPU_DI1_SCR_CONF	0x248170
#define	DMFC_RD_CHAN		0x260000
#define	DMFC_WR_CHAN		0x260004
#define		DMFC_WR_CHAN_BURST_SIZE_32	(0 << 6)
#define		DMFC_WR_CHAN_BURST_SIZE_16	(1 << 6)
#define		DMFC_WR_CHAN_BURST_SIZE_8	(2 << 6)
#define		DMFC_WR_CHAN_BURST_SIZE_4	(3 << 6)
#define		DMFC_WR_CHAN_BURST_SIZE_4	(3 << 6)
#define		DMFC_WR_CHAN_FIFO_SIZE_128	(2 << 3)
#define	DMFC_WR_CHAN_DEF	0x260008
#define		DMFC_WR_CHAN_DEF_WM_CLR_2C(v)	((v) << 29)
#define		DMFC_WR_CHAN_DEF_WM_CLR_1C(v)	((v) << 21)
#define		DMFC_WR_CHAN_DEF_WM_CLR_2(v)	((v) << 13)
#define		DMFC_WR_CHAN_DEF_WM_CLR_1(v)	((v) << 5)
#define		DMFC_WR_CHAN_DEF_WM_SET_1(v)	((v) << 2)
#define		DMFC_WR_CHAN_DEF_WM_EN_1	(1 << 1)
#define	DMFC_DP_CHAN		0x26000C
#define		DMFC_DP_CHAN_BURST_SIZE_8	2
#define		DMFC_DP_CHAN_FIFO_SIZE_256	1
#define		DMFC_DP_CHAN_FIFO_SIZE_128	2
#define		DMFC_DP_CHAN_BURST_SIZE_5F(v)	((v) << 14)
#define		DMFC_DP_CHAN_FIFO_SIZE_5F(v)	((v) << 11)
#define		DMFC_DP_CHAN_ST_ADDR_SIZE_5F(v)	((v) << 8)
#define		DMFC_DP_CHAN_BURST_SIZE_5B(v)	((v) << 6)
#define		DMFC_DP_CHAN_FIFO_SIZE_5B(v)	((v) << 3)
#define		DMFC_DP_CHAN_ST_ADDR_SIZE_5B(v)	((v) << 0)
#define	DMFC_DP_CHAN_DEF	0x260010
#define		DMFC_DP_CHAN_DEF_WM_CLR_6F(v)	((v) << 29)
#define		DMFC_DP_CHAN_DEF_WM_CLR_6B(v)	((v) << 21)
#define		DMFC_DP_CHAN_DEF_WM_CLR_5F(v)	((v) << 13)
#define		DMFC_DP_CHAN_DEF_WM_SET_5F(v)	((v) << 10)
#define		DMFC_DP_CHAN_DEF_WM_EN_5F	(1 << 9)
#define		DMFC_DP_CHAN_DEF_WM_CLR_5B(v)	((v) << 5)
#define		DMFC_DP_CHAN_DEF_WM_SET_5B(v)	((v) << 2)
#define		DMFC_DP_CHAN_DEF_WM_EN_5B	(1 << 1)
#define	DMFC_GENERAL_1		0x260014
#define		DMFC_GENERAL_1_WAIT4EOT_5B	(1 << 20)
#define	DMFC_IC_CTRL		0x26001C
#define		DMFC_IC_CTRL_DISABLED	0x2

#define	DC_WRITE_CH_CONF_1	0x0025801C
#define		WRITE_CH_CONF_PROG_CHAN_TYP_MASK	(7 << 5)
#define		WRITE_CH_CONF_PROG_CHAN_NORMAL		(4 << 5)
#define	DC_WRITE_CH_ADDR_1	0x00258020
#define	DC_WRITE_CH_CONF_5	0x0025805C
#define		WRITE_CH_CONF_PROG_DISP_ID(v)	((v) << 3)
#define		WRITE_CH_CONF_PROG_DI_ID(v)	((v) << 2)
#define		WRITE_CH_CONF_PROG_W_SIZE(v)	(v)
#define	DC_WRITE_CH_ADDR_5	0x00258060
#define	DC_RL0_CH_5		0x00258064
#define	DC_GEN			0x002580D4
#define		DC_GEN_SYNC_PRIORITY	(1 << 7)
#define		DC_GEN_ASYNC		(0 << 1)
#define		DC_GEN_SYNC		(2 << 1)
#define	DC_DISP_CONF2(di)	(0x002580E8 + (di) * 4)
#define	DC_MAP_CONF_0		0x00258108
#define	DC_MAP_CONF_15		0x00258144
#define	DC_MAP_CONF_VAL(map)	(DC_MAP_CONF_15 + ((map) / 2) * sizeof(uint32_t))
#define		MAP_CONF_VAL_MASK	0xffff
#define	DC_MAP_CONF_PTR(ptr)	(DC_MAP_CONF_0 + ((ptr) / 2) * sizeof(uint32_t))
#define		MAP_CONF_PTR_MASK	0x1f

#define	DI_COUNTER_INT_HSYNC	1
#define	DI_COUNTER_HSYNC	2
#define	DI_COUNTER_VSYNC	3
#define	DI_COUNTER_AD_0		4
#define	DI_COUNTER_AD_1		5

#define	DI_SYNC_NONE		0
#define	DI_SYNC_CLK		1
#define	DI_SYNC_COUNTER(c)	((c) + 1)

struct ipu_cpmem_word {
	uint32_t	data[5];
	uint32_t	padding[3];
};

struct ipu_cpmem_ch_param {
	struct ipu_cpmem_word	word[2];
};

#define	CH_PARAM_RESET(param) memset(param, 0, sizeof(*param))
#define	IPU_READ_CH_PARAM(_sc, ch, param) bus_read_region_4( \
	(_sc)->sc_mem_res, CPMEM_BASE + ch * (sizeof(*param)),\
	(uint32_t*)param, sizeof(*param) / 4)
#define	IPU_WRITE_CH_PARAM(_sc, ch, param) bus_write_region_4( \
	(_sc)->sc_mem_res, CPMEM_BASE + ch * (sizeof(*param)),\
	(uint32_t*)param, sizeof(*param) / 4)

#define	CH_PARAM_SET_FW(param, v) ipu_ch_param_set_value((param), \
	0, 125, 13, (v))
#define	CH_PARAM_SET_FH(param, v) ipu_ch_param_set_value((param), \
	0, 138, 12, (v))
#define	CH_PARAM_SET_SLY(param, v) ipu_ch_param_set_value((param), \
	1, 102, 14, (v))
#define	CH_PARAM_SET_EBA0(param, v) ipu_ch_param_set_value((param), \
	1, 0, 29, (v))
#define	CH_PARAM_SET_EBA1(param, v) ipu_ch_param_set_value((param), \
	1, 29, 29, (v))
#define	CH_PARAM_SET_BPP(param, v) ipu_ch_param_set_value((param), \
	0, 107, 3, (v))
#define	CH_PARAM_SET_PFS(param, v) ipu_ch_param_set_value((param), \
	1, 85, 4, (v))
#define	CH_PARAM_SET_NPB(param, v) ipu_ch_param_set_value((param), \
	1, 78, 7, (v))
#define	CH_PARAM_SET_UBO(param, v) ipu_ch_param_set_value((param), \
	0, 46, 22, (v))
#define	CH_PARAM_SET_VBO(param, v) ipu_ch_param_set_value((param), \
	0, 68, 22, (v))

#define	CH_PARAM_SET_RED_WIDTH(param, v) ipu_ch_param_set_value((param), \
	1, 116, 3, (v))
#define	CH_PARAM_SET_RED_OFFSET(param, v) ipu_ch_param_set_value((param), \
	1, 128, 5, (v))

#define	CH_PARAM_SET_GREEN_WIDTH(param, v) ipu_ch_param_set_value((param), \
	1, 119, 3, (v))
#define	CH_PARAM_SET_GREEN_OFFSET(param, v) ipu_ch_param_set_value((param), \
	1, 133, 5, (v))

#define	CH_PARAM_SET_BLUE_WIDTH(param, v) ipu_ch_param_set_value((param), \
	1, 122, 3, (v))
#define	CH_PARAM_SET_BLUE_OFFSET(param, v) ipu_ch_param_set_value((param), \
	1, 138, 5, (v))

#define	CH_PARAM_SET_ALPHA_WIDTH(param, v) ipu_ch_param_set_value((param), \
	1, 125, 3, (v))
#define	CH_PARAM_SET_ALPHA_OFFSET(param, v) ipu_ch_param_set_value((param), \
	1, 143, 5, (v))

#define	CH_PARAM_GET_FW(param) ipu_ch_param_get_value((param), \
	0, 125, 13)
#define	CH_PARAM_GET_FH(param) ipu_ch_param_get_value((param), \
	0, 138, 12)
#define	CH_PARAM_GET_SLY(param) ipu_ch_param_get_value((param), \
	1, 102, 14)
#define	CH_PARAM_GET_EBA0(param) ipu_ch_param_get_value((param), \
	1, 0, 29)
#define	CH_PARAM_GET_EBA1(param) ipu_ch_param_get_value((param), \
	1, 29, 29)
#define	CH_PARAM_GET_BPP(param) ipu_ch_param_get_value((param), \
	0, 107, 3)
#define	CH_PARAM_GET_PFS(param) ipu_ch_param_get_value((param), \
	1, 85, 4)
#define	CH_PARAM_GET_NPB(param) ipu_ch_param_get_value((param), \
	1, 78, 7)
#define	CH_PARAM_GET_UBO(param) ipu_ch_param_get_value((param), \
	0, 46, 22)
#define	CH_PARAM_GET_VBO(param) ipu_ch_param_get_value((param), \
	0, 68, 22)

#define	CH_PARAM_GET_RED_WIDTH(param) ipu_ch_param_get_value((param), \
	1, 116, 3)
#define	CH_PARAM_GET_RED_OFFSET(param) ipu_ch_param_get_value((param), \
	1, 128, 5)

#define	CH_PARAM_GET_GREEN_WIDTH(param) ipu_ch_param_get_value((param), \
	1, 119, 3)
#define	CH_PARAM_GET_GREEN_OFFSET(param) ipu_ch_param_get_value((param), \
	1, 133, 5)

#define	CH_PARAM_GET_BLUE_WIDTH(param) ipu_ch_param_get_value((param), \
	1, 122, 3)
#define	CH_PARAM_GET_BLUE_OFFSET(param) ipu_ch_param_get_value((param), \
	1, 138, 5)

#define	CH_PARAM_GET_ALPHA_WIDTH(param) ipu_ch_param_get_value((param), \
	1, 125, 3)
#define	CH_PARAM_GET_ALPHA_OFFSET(param) ipu_ch_param_get_value((param), \
	1, 143, 5)

#define	IPU_PIX_FORMAT_BPP_32	0
#define	IPU_PIX_FORMAT_BPP_24	1
#define	IPU_PIX_FORMAT_BPP_18	2
#define	IPU_PIX_FORMAT_BPP_16	3
#define	IPU_PIX_FORMAT_BPP_12	4
#define	IPU_PIX_FORMAT_BPP_8	5
#define	IPU_PIX_FORMAT_BPP_

#define	IPU_PIX_FORMAT_RGB	7

enum dc_event_t {
	DC_EVENT_NF = 0,
	DC_EVENT_NL,
	DC_EVENT_EOF,
	DC_EVENT_NFIELD,
	DC_EVENT_EOL,
	DC_EVENT_EOFIELD,
	DC_EVENT_NEW_ADDR,
	DC_EVENT_NEW_CHAN,
	DC_EVENT_NEW_DATA
};

struct ipu_softc {
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	int			sc_mem_rid;
	struct resource		*sc_irq_res;
	int			sc_irq_rid;
	void			*sc_intr_hl;
	struct mtx		sc_mtx;
	struct fb_info		sc_fb_info;
	struct videomode 	*sc_mode;

	/* Framebuffer */
	bus_dma_tag_t		sc_dma_tag;
	bus_dmamap_t		sc_dma_map;
	size_t			sc_fb_size;
	bus_addr_t		sc_fb_phys;
	uint8_t			*sc_fb_base;

	/* HDMI */
	eventhandler_tag	sc_hdmi_evh;
};

static void
ipu_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static void
ipu_ch_param_set_value(struct ipu_cpmem_ch_param *param,
    int word, unsigned int offset, int len, uint32_t value)
{
	uint32_t datapos, bitpos, mask;
	uint32_t data, data2;

	KASSERT((len <= 32), ("%s: field len is more than 32", __func__));

	datapos = offset / 32;
	bitpos = offset % 32;

	mask = (1 << len) - 1;
	data = param->word[word].data[datapos];
	data &= ~(mask << bitpos);
	data |= (value << bitpos);
	param->word[word].data[datapos] = data;

	if ((bitpos + len) > 32) {
		len = bitpos + len - 32;
		mask = (1UL << len) - 1;
		data2 = param->word[word].data[datapos + 1];
		data2 &= mask;
		data2 |= (value >> (32 - bitpos));
		param->word[word].data[datapos + 1] = data2;
	}
}

#ifdef DEBUG
static uint32_t
ipu_ch_param_get_value(struct ipu_cpmem_ch_param *param,
    int word, unsigned int offset, int len)
{
	uint32_t datapos, bitpos, mask;
	uint32_t data, data2;

	KASSERT((len <= 32), ("%s: field len is more than 32", __func__));

	datapos = offset / 32;
	bitpos = offset % 32;
	mask = (1UL << len) - 1;
	data = param->word[word].data[datapos];
	data = data >> bitpos;
	data &= mask;
	if ((bitpos + len) > 32) {
		len = bitpos + len - 32;
		mask = (1UL << len) - 1;
		data2 = param->word[word].data[datapos + 1];
		data2 &= mask;
		data |= (data2 << (32 - bitpos));
	}

	return (data);
}

static void
ipu_print_channel(struct ipu_cpmem_ch_param *param)
{
	int offset0[] = {0, 10, 19, 32, 44, 45, 46, 68, 90, 94, 95, 113, 114, 117, 119, 120, 121, 122, 123, 124, 125, 138, 150, 151, -1};
	int offset1[] = {0, 29, 58, 78, 85, 89, 90, 93, 95, 102, 116, 119, 122, 125, 128, 133, 138, 143, 148, 149, 150, -1};
	printf("WORD0: %08x %08x %08x %08x %08x\n",
		param->word[0].data[0], param->word[0].data[1],
		param->word[0].data[2], param->word[0].data[3],
		param->word[0].data[4]);
	printf("WORD1: %08x %08x %08x %08x %08x\n",
		param->word[1].data[0], param->word[1].data[1],
		param->word[1].data[2], param->word[1].data[3],
		param->word[1].data[4]);

	for (int i = 0; offset0[i + 1] != -1; i++) {
		int len = offset0[i + 1] - offset0[i];
		printf("W0[%d:%d] = %d\n", offset0[i],
			offset0[i] + len - 1,
			ipu_ch_param_get_value(param, 0, offset0[i], len)
			);
	}

	for (int i = 0; offset1[i + 1] != -1; i++) {
		int len = offset1[i + 1] - offset1[i];
		printf("W1[%d:%d] = %d\n", offset1[i],
			offset1[i] + len - 1,
			ipu_ch_param_get_value(param, 1, offset1[i], len)
			);
	}

	printf("FW:   %d\n", CH_PARAM_GET_FW(param));
	printf("FH:   %d\n", CH_PARAM_GET_FH(param));
	printf("SLY:  %d\n", CH_PARAM_GET_SLY(param));
	printf("EBA0: 0x%08x\n", CH_PARAM_GET_EBA0(param));
	printf("EBA1: 0x%08x\n", CH_PARAM_GET_EBA1(param));
	printf("BPP:  %d\n", CH_PARAM_GET_BPP(param));
	printf("PFS:  %d\n", CH_PARAM_GET_PFS(param));
	printf("NPB:  %d\n", CH_PARAM_GET_NPB(param));
	printf("UBO:  %d\n", CH_PARAM_GET_UBO(param));
	printf("VBO:  %d\n", CH_PARAM_GET_VBO(param));
	printf("RED:  %d bits @%d\n", CH_PARAM_GET_RED_WIDTH(param) + 1,
		CH_PARAM_GET_RED_OFFSET(param));
	printf("GREEN:  %d bits @%d\n", CH_PARAM_GET_GREEN_WIDTH(param) + 1,
		CH_PARAM_GET_GREEN_OFFSET(param));
	printf("BLUE:  %d bits @%d\n", CH_PARAM_GET_BLUE_WIDTH(param) + 1,
		CH_PARAM_GET_BLUE_OFFSET(param));
	printf("ALPHA:  %d bits @%d\n", CH_PARAM_GET_ALPHA_WIDTH(param) + 1,
		CH_PARAM_GET_ALPHA_OFFSET(param));
}
#endif

static void
ipu_di_enable(struct ipu_softc *sc, int di)
{
	uint32_t flag, reg;

	flag = di ? DISP_GEN_DI1_CNTR_RELEASE : DISP_GEN_DI0_CNTR_RELEASE;
	reg = IPU_READ4(sc, IPU_DISP_GEN);
	reg |= flag;
	IPU_WRITE4(sc, IPU_DISP_GEN, reg);
}

static void
ipu_config_wave_gen_0(struct ipu_softc *sc, int di,
	int wave_gen, int run_value, int run_res,
	int offset_value, int offset_res)
{
	uint32_t addr, reg;

	addr = (di ? IPU_DI1_SW_GEN0_1 : IPU_DI0_SW_GEN0_1)
	    + (wave_gen - 1) * sizeof(uint32_t);
	reg = DI_RUN_VALUE_M1(run_value) |
	    DI_RUN_RESOLUTION(run_res) |
	    DI_OFFSET_VALUE(offset_value) | offset_res;
	IPU_WRITE4(sc, addr, reg);
}

static void
ipu_config_wave_gen_1(struct ipu_softc *sc, int di, int wave_gen,
	int repeat_count, int cnt_clr_src,
	int cnt_polarity_gen_en,
	int cnt_polarity_clr_src,
	int cnt_polarity_trigger_src,
	int cnt_up, int cnt_down)
{
	uint32_t addr, reg;

	addr = (di ? IPU_DI1_SW_GEN1_1 : IPU_DI0_SW_GEN1_1)
	    + (wave_gen - 1) * sizeof(uint32_t);
	reg = DI_CNT_POLARITY_GEN_EN(cnt_polarity_gen_en) |
	    DI_CNT_CLR_SEL(cnt_clr_src) |
	    DI_CNT_POLARITY_TRIGGER_SEL(cnt_polarity_trigger_src) |
	    DI_CNT_POLARITY_CLR_SEL(cnt_polarity_clr_src);
	reg |= DI_CNT_DOWN(cnt_down) | cnt_up;
	if (repeat_count == 0)
		reg |= DI_CNT_AUTO_RELOAD;
	IPU_WRITE4(sc, addr, reg);

	addr = (di ? IPU_DI1_STP_REP : IPU_DI0_STP_REP)
	    + (wave_gen - 1) / 2 * sizeof(uint32_t);
	reg = IPU_READ4(sc, addr);
	if (wave_gen % 2) {
		reg &= ~(0xffff);
		reg |= repeat_count;
	}
	else {
		reg &= ~(0xffff << 16);
		reg |= (repeat_count << 16);
	}
	IPU_WRITE4(sc, addr, reg);
}

static void
ipu_reset_wave_gen(struct ipu_softc *sc, int di,
	int wave_gen)
{
	uint32_t addr, reg;

	addr = (di ? IPU_DI1_SW_GEN0_1 : IPU_DI0_SW_GEN0_1)
	    + (wave_gen - 1) * sizeof(uint32_t);
	IPU_WRITE4(sc, addr, 0);

	addr = (di ? IPU_DI1_SW_GEN1_1 : IPU_DI0_SW_GEN1_1)
	    + (wave_gen - 1) * sizeof(uint32_t);
	IPU_WRITE4(sc, addr, 0);

	addr = (di ? IPU_DI1_STP_REP : IPU_DI0_STP_REP)
	    + (wave_gen - 1) / 2 * sizeof(uint32_t);
	reg = IPU_READ4(sc, addr);
	if (wave_gen % 2)
		reg &= ~(0xffff);
	else
		reg &= ~(0xffff << 16);
	IPU_WRITE4(sc, addr, reg);
}

static void
ipu_init_microcode_template(struct ipu_softc *sc, int di, int map)
{
	uint32_t addr;
	uint32_t w1, w2;
	int i, word;
	int glue;

	word = di ? 2 : 5;

	for (i = 0; i < 3; i++) {
		if (i == 0)
			glue = GLUELOGIC_KEEP_ASSERTED;
		else if (i == 1)
			glue = GLUELOGIC_KEEP_NEGATED;
		else if (i == 2)
			glue = 0;

		w1 = TEMPLATE_SYNC(5) |
		    TEMPLATE_GLUELOGIC(glue) |
		    TEMPLATE_WAVEFORM(1) | /* wave unit 0 */
		    TEMPLATE_MAPPING(map + 1);
		/* operand is zero */

		/* Write data to DI and Hold data in register */
		w2 = TEMPLATE_OPCODE(OPCODE_WROD) |
		    TEMPLATE_STOP;

		addr = DC_TEMPL_BASE + (word + i) * 2 * sizeof(uint32_t);
		IPU_WRITE4(sc, addr, w1);
		IPU_WRITE4(sc, addr + sizeof(uint32_t), w2);
	}
}

static void
ipu_config_timing(struct ipu_softc *sc, int di)
{
	int div;
	uint32_t di_scr_conf;
	uint32_t gen_offset, gen;
	uint32_t as_gen_offset, as_gen;
	uint32_t dw_gen_offset, dw_gen;
	uint32_t dw_set_offset, dw_set;
	uint32_t bs_clkgen_offset;
	int map;

	/* TODO: check mode restrictions / fixup */
	/* TODO: enable timers, get divisors */
	div = 1;
	map = 0;

	bs_clkgen_offset = di ? IPU_DI1_BS_CLKGEN0 : IPU_DI0_BS_CLKGEN0;
	IPU_WRITE4(sc, bs_clkgen_offset, DI_BS_CLKGEN0(div, 0));
	/* half of the divider */
	IPU_WRITE4(sc, bs_clkgen_offset + 4, DI_BS_CLKGEN1_DOWN(div / 2, div % 2));

	/*
	 * TODO: Configure LLDB clock by changing following fields
	 * in CCM fields:
	 * 	CS2CDR_LDB_DI0_CLK_SEL
	 * 	CSCMR2_LDB_DI0_IPU_DIV
	 * 	CBCDR_MMDC_CH1_AXI_PODF
	 */

	/* Setup wave generator */
	dw_gen_offset = di ? IPU_DI1_DW_GEN_0 : IPU_DI0_DW_GEN_0;
	dw_gen = DW_GEN_DI_ACCESS_SIZE(div - 1) | DW_GEN_DI_COMPONENT_SIZE(div - 1);
	dw_gen &= ~DW_GEN_DI_PIN_15_SET(DW_GEN_DI_SET_MASK);
	dw_gen |= DW_GEN_DI_PIN_15_SET(3); /* set 3*/
	IPU_WRITE4(sc, dw_gen_offset, dw_gen);

	dw_set_offset = di ? IPU_DI1_DW_SET3_0 : IPU_DI0_DW_SET3_0;
	dw_set = DW_SET_DATA_CNT_DOWN(div * 2) | DW_SET_DATA_CNT_UP(0);
	IPU_WRITE4(sc, dw_set_offset, dw_set);

	/* DI_COUNTER_INT_HSYNC */
	ipu_config_wave_gen_0(sc, di, DI_COUNTER_INT_HSYNC,
	    sc->sc_mode->htotal - 1, DI_SYNC_CLK, 0, DI_SYNC_NONE);
	ipu_config_wave_gen_1(sc, di, DI_COUNTER_INT_HSYNC,
	    0, DI_SYNC_NONE, 0, DI_SYNC_NONE, DI_SYNC_NONE, 0, 0);

	/* DI_COUNTER_HSYNC */
	ipu_config_wave_gen_0(sc, di, DI_COUNTER_HSYNC,
	    sc->sc_mode->htotal - 1, DI_SYNC_CLK, 0, DI_SYNC_CLK);
	ipu_config_wave_gen_1(sc, di, DI_COUNTER_HSYNC,
	    0, DI_SYNC_NONE, 1, DI_SYNC_NONE, DI_SYNC_CLK,
	    0, MODE_HSW(sc->sc_mode) * 2);

	/* DI_COUNTER_VSYNC */
	ipu_config_wave_gen_0(sc, di, DI_COUNTER_VSYNC,
	    sc->sc_mode->vtotal - 1, DI_SYNC_COUNTER(DI_COUNTER_INT_HSYNC),
	    0, DI_SYNC_NONE);
	ipu_config_wave_gen_1(sc, di, DI_COUNTER_VSYNC,
	    0, DI_SYNC_NONE, 1, DI_SYNC_NONE,
	    DI_SYNC_COUNTER(DI_COUNTER_INT_HSYNC),
	    0, MODE_VSW(sc->sc_mode) * 2);

	di_scr_conf = di ? IPU_DI1_SCR_CONF : IPU_DI0_SCR_CONF;
	IPU_WRITE4(sc, di_scr_conf, sc->sc_mode->vtotal - 1);

	/* TODO: update DI_SCR_CONF */

	/* Active Data 0 */
	ipu_config_wave_gen_0(sc, di, DI_COUNTER_AD_0,
	    0, DI_SYNC_COUNTER(DI_COUNTER_HSYNC),
	    MODE_VSW(sc->sc_mode) + MODE_VFP(sc->sc_mode), DI_SYNC_COUNTER(DI_COUNTER_HSYNC));
	ipu_config_wave_gen_1(sc, di, DI_COUNTER_AD_0,
	    sc->sc_mode->vdisplay, DI_SYNC_COUNTER(DI_COUNTER_VSYNC),
	    0, DI_SYNC_NONE, DI_SYNC_NONE, 0, 0);

	ipu_config_wave_gen_0(sc, di, DI_COUNTER_AD_1,
	    0, DI_SYNC_CLK, MODE_HSW(sc->sc_mode) + MODE_HFP(sc->sc_mode), DI_SYNC_CLK);
	ipu_config_wave_gen_1(sc, di, DI_COUNTER_AD_1,
	    sc->sc_mode->hdisplay, DI_SYNC_COUNTER(DI_COUNTER_AD_0),
	    0, DI_SYNC_NONE, DI_SYNC_NONE, 0, 0);

	ipu_reset_wave_gen(sc, di, 6);
	ipu_reset_wave_gen(sc, di, 7);
	ipu_reset_wave_gen(sc, di, 8);
	ipu_reset_wave_gen(sc, di, 9);

	ipu_init_microcode_template(sc, di, map);

	gen_offset = di ?  IPU_DI1_GENERAL : IPU_DI0_GENERAL;
	gen = IPU_READ4(sc, gen_offset);

	if (sc->sc_mode->flags & VID_NHSYNC)
		gen &= ~DI_GENERAL_POLARITY_2;
	else /* active high */
		gen |= DI_GENERAL_POLARITY_2;

	if (sc->sc_mode->flags & VID_NVSYNC)
		gen &= ~DI_GENERAL_POLARITY_3;
	else /* active high */
		gen |= DI_GENERAL_POLARITY_3;

	if (MODE_PIXEL_CLOCK_INVERT)
		gen &= ~DI_GENERAL_POL_CLK;
	else
		gen |= DI_GENERAL_POL_CLK;

	/* Use LDB clock to drive pixel clock */
	gen |= DI_CLOCK_EXTERNAL;

	IPU_WRITE4(sc, gen_offset, gen);

	as_gen_offset = di ?  IPU_DI1_SYNC_AS_GEN : IPU_DI0_SYNC_AS_GEN;
	as_gen = SYNC_AS_GEN_VSYNC_SEL(DI_COUNTER_VSYNC - 1) |
	    SYNC_AS_GEN_SYNC_START(2);
	IPU_WRITE4(sc, as_gen_offset, as_gen);

	IPU_WRITE4(sc, (di ? IPU_DI1_POL : IPU_DI0_POL), DI_POL_DRDY_POLARITY_15);

	IPU_WRITE4(sc, DC_DISP_CONF2(di), sc->sc_mode->hdisplay);
}

static void
ipu_dc_enable(struct ipu_softc *sc)
{
	uint32_t conf;

	/* channel 1 uses DI1 */
	IPU_WRITE4(sc, DC_WRITE_CH_CONF_1, WRITE_CH_CONF_PROG_DI_ID(1));

	conf = IPU_READ4(sc, DC_WRITE_CH_CONF_5);
	conf &= ~WRITE_CH_CONF_PROG_CHAN_TYP_MASK;
	conf |= WRITE_CH_CONF_PROG_CHAN_NORMAL;
	IPU_WRITE4(sc, DC_WRITE_CH_CONF_5, conf);

	/* TODO: enable clock */
}

static void
ipu_dc_link_event(struct ipu_softc *sc, int event, int addr, int priority)
{
	uint32_t reg;
	int offset;
	int shift;

	if (event % 2)
		shift = 16;
	else
		shift = 0;

	offset = DC_RL0_CH_5 + (event / 2) * sizeof(uint32_t);

	reg = IPU_READ4(sc, offset);
	reg &= ~(0xFFFF << shift);
	reg |= ((addr << 8) | priority) << shift;
	IPU_WRITE4(sc, offset, reg);
}

static void
ipu_dc_setup_map(struct ipu_softc *sc, int map,
    int byte, int offset, int mask)
{
	uint32_t reg, shift, ptr;

	ptr = map * 3 + byte;

	reg = IPU_READ4(sc, DC_MAP_CONF_VAL(ptr));
	if (ptr & 1)
		shift = 16;
	else
		shift = 0;
	reg &= ~(0xffff << shift);
	reg |= ((offset << 8) | mask) << shift;
	IPU_WRITE4(sc, DC_MAP_CONF_VAL(ptr), reg);

	reg = IPU_READ4(sc, DC_MAP_CONF_PTR(map));
	if (map & 1)
		shift = 16  + 5 * byte;
	else
		shift = 5 * byte;
	reg &= ~(MAP_CONF_PTR_MASK << shift);
	reg |= (ptr) << shift;
	IPU_WRITE4(sc, DC_MAP_CONF_PTR(map), reg);
}

static void
ipu_dc_reset_map(struct ipu_softc *sc, int map)
{
	uint32_t reg, shift;

	reg = IPU_READ4(sc, DC_MAP_CONF_VAL(map));
	if (map & 1)
		shift = 16;
	else
		shift = 0;
	reg &= ~(MAP_CONF_VAL_MASK << shift);
	IPU_WRITE4(sc, DC_MAP_CONF_VAL(map), reg);
}

static void
ipu_dc_init(struct ipu_softc *sc, int di_port)
{
	int addr;
	uint32_t conf;

	if (di_port)
		addr = 2;
	else
		addr = 5;

	ipu_dc_link_event(sc, DC_EVENT_NL, addr, 3);
	ipu_dc_link_event(sc, DC_EVENT_EOL, addr + 1, 2);
	ipu_dc_link_event(sc, DC_EVENT_NEW_DATA, addr + 2, 1);
	ipu_dc_link_event(sc, DC_EVENT_NF, 0, 0);
	ipu_dc_link_event(sc, DC_EVENT_NFIELD, 0, 0);
	ipu_dc_link_event(sc, DC_EVENT_EOF, 0, 0);
	ipu_dc_link_event(sc, DC_EVENT_EOFIELD, 0, 0);
	ipu_dc_link_event(sc, DC_EVENT_NEW_CHAN, 0, 0);
	ipu_dc_link_event(sc, DC_EVENT_NEW_ADDR, 0, 0);

	conf = WRITE_CH_CONF_PROG_W_SIZE(0x02) |
            WRITE_CH_CONF_PROG_DISP_ID(DI_PORT) |
	    WRITE_CH_CONF_PROG_DI_ID(DI_PORT);

	IPU_WRITE4(sc, DC_WRITE_CH_CONF_5, conf);
	IPU_WRITE4(sc, DC_WRITE_CH_ADDR_5, 0x00000000);
	IPU_WRITE4(sc, DC_GEN, DC_GEN_SYNC_PRIORITY | DC_GEN_SYNC); /* High priority, sync */
}

static void
ipu_init_buffer(struct ipu_softc *sc)
{
	struct ipu_cpmem_ch_param param;
	uint32_t stride;
	uint32_t reg, db_mode_sel, cur_buf;

	stride = sc->sc_mode->hdisplay * MODE_BPP / 8;

	/* init channel parameters */
	CH_PARAM_RESET(&param);
	/* XXX: interlaced modes are not supported yet */
	CH_PARAM_SET_FW(&param, sc->sc_mode->hdisplay - 1);
	CH_PARAM_SET_FH(&param, sc->sc_mode->vdisplay - 1);
	CH_PARAM_SET_SLY(&param, stride - 1);

	CH_PARAM_SET_EBA0(&param, (sc->sc_fb_phys >> 3));
	CH_PARAM_SET_EBA1(&param, (sc->sc_fb_phys >> 3));

	CH_PARAM_SET_BPP(&param, IPU_PIX_FORMAT_BPP_16);
	CH_PARAM_SET_PFS(&param, IPU_PIX_FORMAT_RGB);
	/* 16 pixels per burst access */
	CH_PARAM_SET_NPB(&param, 16 - 1);

	CH_PARAM_SET_RED_OFFSET(&param, 0);
	CH_PARAM_SET_RED_WIDTH(&param, 5 - 1);
	CH_PARAM_SET_GREEN_OFFSET(&param, 5);
	CH_PARAM_SET_GREEN_WIDTH(&param, 6 - 1);
	CH_PARAM_SET_BLUE_OFFSET(&param, 11);
	CH_PARAM_SET_BLUE_WIDTH(&param, 5 - 1);
	CH_PARAM_SET_ALPHA_OFFSET(&param, 16);
	CH_PARAM_SET_ALPHA_WIDTH(&param, 8 - 1);

	CH_PARAM_SET_UBO(&param, 0);
	CH_PARAM_SET_VBO(&param, 0);

	IPU_WRITE_CH_PARAM(sc, DMA_CHANNEL, &param);
#ifdef DEBUG
	ipu_print_channel(&param);
#endif

	/* init DMFC */
	IPU_WRITE4(sc, DMFC_IC_CTRL, DMFC_IC_CTRL_DISABLED);
	/* High resolution DP */
	IPU_WRITE4(sc, DMFC_WR_CHAN, DMFC_WR_CHAN_BURST_SIZE_8 |
	    DMFC_WR_CHAN_FIFO_SIZE_128);
	IPU_WRITE4(sc, DMFC_WR_CHAN_DEF, DMFC_WR_CHAN_DEF_WM_CLR_2C(1) |
	    DMFC_WR_CHAN_DEF_WM_CLR_1C(1) |
	    DMFC_WR_CHAN_DEF_WM_CLR_2(1) |
	    DMFC_WR_CHAN_DEF_WM_CLR_1(7) |
	    DMFC_WR_CHAN_DEF_WM_SET_1(5) |
	    DMFC_WR_CHAN_DEF_WM_EN_1);

	IPU_WRITE4(sc, DMFC_DP_CHAN,
	    DMFC_DP_CHAN_BURST_SIZE_5F(DMFC_DP_CHAN_BURST_SIZE_8) |
	    DMFC_DP_CHAN_FIFO_SIZE_5F(DMFC_DP_CHAN_FIFO_SIZE_128) |
	    DMFC_DP_CHAN_ST_ADDR_SIZE_5F(6) /* segment 6 */ |
	    DMFC_DP_CHAN_BURST_SIZE_5B(DMFC_DP_CHAN_BURST_SIZE_8) |
	    DMFC_DP_CHAN_FIFO_SIZE_5B(DMFC_DP_CHAN_FIFO_SIZE_256) |
	    DMFC_DP_CHAN_ST_ADDR_SIZE_5B(2) /* segment 2 */);

	IPU_WRITE4(sc, DMFC_DP_CHAN_DEF, DMFC_DP_CHAN_DEF_WM_CLR_6F(1) |
	    DMFC_DP_CHAN_DEF_WM_CLR_6B(1) |
	    DMFC_DP_CHAN_DEF_WM_CLR_5F(7) |
	    DMFC_DP_CHAN_DEF_WM_SET_5F(5) |
	    DMFC_DP_CHAN_DEF_WM_EN_5F |
	    DMFC_DP_CHAN_DEF_WM_CLR_5B(7) |
	    DMFC_DP_CHAN_DEF_WM_SET_5B(5) |
	    DMFC_DP_CHAN_DEF_WM_EN_5B);

	reg = IPU_READ4(sc, DMFC_GENERAL_1);
	reg &= ~(DMFC_GENERAL_1_WAIT4EOT_5B);
	IPU_WRITE4(sc, DMFC_GENERAL_1, reg);

	/* XXX: set priority? */

	/* Set single buffer mode */
	if (DMA_CHANNEL < 32) {
		db_mode_sel = IPU_CH_DB_MODE_SEL_0;
		cur_buf = IPU_CUR_BUF_0;
	} else {
		db_mode_sel = IPU_CH_DB_MODE_SEL_1;
		cur_buf = IPU_CUR_BUF_1;
	}

	reg = IPU_READ4(sc, db_mode_sel);
	reg |= (1UL << (DMA_CHANNEL & 0x1f));
	IPU_WRITE4(sc, db_mode_sel, reg);

	IPU_WRITE4(sc, cur_buf, (1UL << (DMA_CHANNEL & 0x1f)));
}

static int
ipu_init(struct ipu_softc *sc)
{
	uint32_t reg, off;
	int i, err;
	size_t dma_size;

	IPU_WRITE4(sc, IPU_CONF, DI_PORT ? IPU_CONF_DI1_EN : IPU_CONF_DI0_EN);

	IPU_WRITE4(sc, IPU_MEM_RST, IPU_MEM_RST_ALL);
	i = 1000;
	while (i-- > 0) {
		if (!(IPU_READ4(sc, IPU_MEM_RST) & IPU_MEM_RST_START))
			break;
		DELAY(1);
	}

	if (i <= 0) {
		err = ETIMEDOUT;
		device_printf(sc->sc_dev, "timeout while resetting memory\n");
		goto fail;
	}

	ipu_dc_reset_map(sc, 0);
	ipu_dc_setup_map(sc, 0, 0,  7, 0xff);
	ipu_dc_setup_map(sc, 0, 1, 15, 0xff);
	ipu_dc_setup_map(sc, 0, 2, 23, 0xff);

	dma_size = round_page(sc->sc_mode->hdisplay * sc->sc_mode->vdisplay * (MODE_BPP / 8));

	/*
	 * Now allocate framebuffer memory
	 */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(sc->sc_dev),
	    4, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    dma_size, 1,			/* maxsize, nsegments */
	    dma_size, 0,			/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sc_dma_tag);
	if (err)
		goto fail;

	err = bus_dmamem_alloc(sc->sc_dma_tag, (void **)&sc->sc_fb_base,
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sc_dma_map);

	if (err) {
		device_printf(sc->sc_dev, "cannot allocate framebuffer\n");
		goto fail;
	}

	err = bus_dmamap_load(sc->sc_dma_tag, sc->sc_dma_map, sc->sc_fb_base,
	    dma_size, ipu_dmamap_cb, &sc->sc_fb_phys, BUS_DMA_NOWAIT);

	if (err) {
		device_printf(sc->sc_dev, "cannot load DMA map\n");
		goto fail;
	}

	/* Calculate actual FB Size */
	sc->sc_fb_size = sc->sc_mode->hdisplay * sc->sc_mode->vdisplay * MODE_BPP / 8;

	ipu_dc_init(sc, DI_PORT);
	reg = IPU_READ4(sc, IPU_CONF);
	reg |= IPU_CONF_DMFC_EN | IPU_CONF_DC_EN | IPU_CONF_DP_EN;
	IPU_WRITE4(sc, IPU_CONF, reg);

	ipu_config_timing(sc, DI_PORT);
	ipu_init_buffer(sc);
	ipu_di_enable(sc, DI_PORT);

	/* Enable DMA channel */
	off = (DMA_CHANNEL > 31) ? IPU_IDMAC_CH_EN_2 : IPU_IDMAC_CH_EN_1;
	reg = IPU_READ4(sc, off);
	reg |= (1 << (DMA_CHANNEL & 0x1f));
	IPU_WRITE4(sc, off, reg);

	ipu_dc_enable(sc);

	sc->sc_fb_info.fb_name = device_get_nameunit(sc->sc_dev);
	sc->sc_fb_info.fb_vbase = (intptr_t)sc->sc_fb_base;
	sc->sc_fb_info.fb_pbase = sc->sc_fb_phys;
	sc->sc_fb_info.fb_size = sc->sc_fb_size;
	sc->sc_fb_info.fb_bpp = sc->sc_fb_info.fb_depth = MODE_BPP;
	sc->sc_fb_info.fb_stride = sc->sc_mode->hdisplay * MODE_BPP / 8;
	sc->sc_fb_info.fb_width = sc->sc_mode->hdisplay;
	sc->sc_fb_info.fb_height = sc->sc_mode->vdisplay;

	device_t fbd = device_add_child(sc->sc_dev, "fbd",
	    device_get_unit(sc->sc_dev));
	if (fbd == NULL) {
		device_printf(sc->sc_dev, "Failed to add fbd child\n");
		goto fail;
	}
	if (device_probe_and_attach(fbd) != 0) {
		device_printf(sc->sc_dev, "Failed to attach fbd device\n");
		goto fail;
	}

	return (0);
fail:

	return (err);
}

static void
ipu_hdmi_event(void *arg, device_t hdmi_dev)
{
	struct ipu_softc *sc;
	uint8_t *edid;
	uint32_t edid_len;
#ifdef EDID_DEBUG
	struct edid_info ei;
#endif
	const struct videomode *videomode;

	sc = arg;

	edid = NULL;
	edid_len = 0;
	if (HDMI_GET_EDID(hdmi_dev, &edid, &edid_len) != 0) {
		device_printf(sc->sc_dev, "failed to get EDID info from HDMI framer\n");
	}

	videomode = NULL;

#ifdef EDID_DEBUG
	if ( edid && (edid_parse(edid, &ei) == 0)) {
		edid_print(&ei);
	} else
		device_printf(sc->sc_dev, "failed to parse EDID\n");
#endif

	sc->sc_mode = &mode1024x768;
	ipu_init(sc);

	HDMI_SET_VIDEOMODE(hdmi_dev, sc->sc_mode);
}

static int
ipu_probe(device_t dev)
{

	if (have_ipu)
		return (ENXIO);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,imx6q-ipu"))
		return (ENXIO);

	device_set_desc(dev, "Freescale IPU");

	return (BUS_PROBE_DEFAULT);
}

static int
ipu_attach(device_t dev)
{
	struct ipu_softc *sc;

	if (have_ipu)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_mem_rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_mem_rid, RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	sc->sc_irq_rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irq_rid, RF_ACTIVE);
	if (!sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_mem_rid, sc->sc_mem_res);
		device_printf(dev, "cannot allocate interrupt\n");
		return (ENXIO);
	}

	/* Enable IPU1 */
	imx_ccm_ipu_enable(1);

	if (src_reset_ipu() != 0) {
		device_printf(dev, "failed to reset IPU\n");
		return (ENXIO);
	}

	IPU_LOCK_INIT(sc);

	sc->sc_hdmi_evh = EVENTHANDLER_REGISTER(hdmi_event,
	    ipu_hdmi_event, sc, 0);

	have_ipu = 1;

	return (0);
}

static int
ipu_detach(device_t dev)
{
	/* Do not let unload driver */
	return (EBUSY);
}

static struct fb_info *
ipu_fb_getinfo(device_t dev)
{
	struct ipu_softc *sc;

	sc = device_get_softc(dev);

	return (&sc->sc_fb_info);
}

static device_method_t ipu_methods[] = {
	DEVMETHOD(device_probe,		ipu_probe),
	DEVMETHOD(device_attach,	ipu_attach),
	DEVMETHOD(device_detach,	ipu_detach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		ipu_fb_getinfo),

	DEVMETHOD_END
};

static driver_t ipu_driver = {
	"fb",
	ipu_methods,
	sizeof(struct ipu_softc),
};

static devclass_t ipu_devclass;

DRIVER_MODULE(ipu, simplebus, ipu_driver, ipu_devclass, 0, 0);
MODULE_VERSION(ipu, 1);
MODULE_DEPEND(ipu, simplebus, 1, 1, 1);
