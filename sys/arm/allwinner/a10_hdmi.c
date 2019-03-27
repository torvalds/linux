/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Allwinner A10/A20 HDMI TX 
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/extres/clk/clk.h>

#include "hdmi_if.h"

#define	HDMI_CTRL		0x004
#define	CTRL_MODULE_EN		(1 << 31)
#define	HDMI_INT_STATUS		0x008
#define	HDMI_HPD		0x00c
#define	HPD_DET			(1 << 0)
#define	HDMI_VID_CTRL		0x010
#define	VID_CTRL_VIDEO_EN	(1 << 31)
#define	VID_CTRL_HDMI_MODE	(1 << 30)
#define	VID_CTRL_INTERLACE	(1 << 4)
#define	VID_CTRL_REPEATER_2X	(1 << 0)
#define	HDMI_VID_TIMING0	0x014
#define	VID_ACT_V(v)		(((v) - 1) << 16)
#define	VID_ACT_H(h)		(((h) - 1) << 0)
#define	HDMI_VID_TIMING1	0x018
#define	VID_VBP(vbp)		(((vbp) - 1) << 16)
#define	VID_HBP(hbp)		(((hbp) - 1) << 0)
#define	HDMI_VID_TIMING2	0x01c
#define	VID_VFP(vfp)		(((vfp) - 1) << 16)
#define	VID_HFP(hfp)		(((hfp) - 1) << 0)
#define	HDMI_VID_TIMING3	0x020
#define	VID_VSPW(vspw)		(((vspw) - 1) << 16)
#define	VID_HSPW(hspw)		(((hspw) - 1) << 0)
#define	HDMI_VID_TIMING4	0x024
#define	TX_CLOCK_NORMAL		0x03e00000
#define	VID_VSYNC_ACTSEL	(1 << 1)
#define	VID_HSYNC_ACTSEL	(1 << 0)
#define	HDMI_AUD_CTRL		0x040
#define	AUD_CTRL_EN		(1 << 31)
#define	AUD_CTRL_RST		(1 << 30)
#define	HDMI_ADMA_CTRL		0x044
#define	HDMI_ADMA_MODE		(1 << 31)
#define	HDMI_ADMA_MODE_DDMA	(0 << 31)
#define	HDMI_ADMA_MODE_NDMA	(1 << 31)
#define	HDMI_AUD_FMT		0x048
#define	AUD_FMT_CH(n)		((n) - 1)
#define	HDMI_PCM_CTRL		0x04c
#define	HDMI_AUD_CTS		0x050
#define	HDMI_AUD_N		0x054
#define	HDMI_AUD_CH_STATUS0	0x058
#define	CH_STATUS0_FS_FREQ	(0xf << 24)
#define	CH_STATUS0_FS_FREQ_48	(2 << 24)
#define	HDMI_AUD_CH_STATUS1	0x05c
#define	CH_STATUS1_WORD_LEN	(0x7 << 1)
#define	CH_STATUS1_WORD_LEN_16	(1 << 1)
#define	HDMI_AUDIO_RESET_RETRY	1000
#define	HDMI_AUDIO_CHANNELS	2
#define	HDMI_AUDIO_CHANNELMAP	0x76543210
#define	HDMI_AUDIO_N		6144	/* 48 kHz */
#define	HDMI_AUDIO_CTS(r, n)	((((r) * 10) * ((n) / 128)) / 480)
#define	HDMI_PADCTRL0		0x200
#define	PADCTRL0_BIASEN		(1 << 31)
#define	PADCTRL0_LDOCEN		(1 << 30)
#define	PADCTRL0_LDODEN		(1 << 29)
#define	PADCTRL0_PWENC		(1 << 28)
#define	PADCTRL0_PWEND		(1 << 27)
#define	PADCTRL0_PWENG		(1 << 26)
#define	PADCTRL0_CKEN		(1 << 25)
#define	PADCTRL0_SEN		(1 << 24)
#define	PADCTRL0_TXEN		(1 << 23)
#define	HDMI_PADCTRL1		0x204
#define	PADCTRL1_AMP_OPT	(1 << 23)
#define	PADCTRL1_AMPCK_OPT	(1 << 22)
#define	PADCTRL1_DMP_OPT	(1 << 21)
#define	PADCTRL1_EMP_OPT	(1 << 20)
#define	PADCTRL1_EMPCK_OPT	(1 << 19)
#define	PADCTRL1_PWSCK		(1 << 18)
#define	PADCTRL1_PWSDT		(1 << 17)
#define	PADCTRL1_REG_CSMPS	(1 << 16)
#define	PADCTRL1_REG_DEN	(1 << 15)
#define	PADCTRL1_REG_DENCK	(1 << 14)
#define	PADCTRL1_REG_PLRCK	(1 << 13)
#define	PADCTRL1_REG_EMP	(0x7 << 10)
#define	PADCTRL1_REG_EMP_EN	(0x2 << 10)
#define	PADCTRL1_REG_CD		(0x3 << 8)
#define	PADCTRL1_REG_CKSS	(0x3 << 6)
#define	PADCTRL1_REG_CKSS_1X	(0x1 << 6)
#define	PADCTRL1_REG_CKSS_2X	(0x0 << 6)
#define	PADCTRL1_REG_AMP	(0x7 << 3)
#define	PADCTRL1_REG_AMP_EN	(0x6 << 3)
#define	PADCTRL1_REG_PLR	(0x7 << 0)
#define	HDMI_PLLCTRL0		0x208
#define	PLLCTRL0_PLL_EN		(1 << 31)
#define	PLLCTRL0_BWS		(1 << 30)
#define	PLLCTRL0_HV_IS_33	(1 << 29)
#define	PLLCTRL0_LDO1_EN	(1 << 28)
#define	PLLCTRL0_LDO2_EN	(1 << 27)
#define	PLLCTRL0_SDIV2		(1 << 25)
#define	PLLCTRL0_VCO_GAIN	(0x1 << 22)
#define	PLLCTRL0_S		(0x7 << 17)
#define	PLLCTRL0_CP_S		(0xf << 12)
#define	PLLCTRL0_CS		(0x7 << 8)
#define	PLLCTRL0_PREDIV(x)	((x) << 4)
#define	PLLCTRL0_VCO_S		(0x8 << 0)
#define	HDMI_PLLDBG0		0x20c
#define	PLLDBG0_CKIN_SEL	(1 << 21)
#define	PLLDBG0_CKIN_SEL_PLL3	(0 << 21)
#define	PLLDBG0_CKIN_SEL_PLL7	(1 << 21)
#define	HDMI_PKTCTRL0		0x2f0
#define	HDMI_PKTCTRL1		0x2f4
#define	PKTCTRL_PACKET(n,t)	((t) << ((n) << 2))
#define	PKT_NULL		0
#define	PKT_GC			1
#define	PKT_AVI			2
#define	PKT_AI			3
#define	PKT_SPD			5
#define	PKT_END			15
#define	DDC_CTRL		0x500
#define	CTRL_DDC_EN		(1 << 31)
#define	CTRL_DDC_ACMD_START	(1 << 30)
#define	CTRL_DDC_FIFO_DIR	(1 << 8)
#define	CTRL_DDC_FIFO_DIR_READ	(0 << 8)
#define	CTRL_DDC_FIFO_DIR_WRITE	(1 << 8)
#define	CTRL_DDC_SWRST		(1 << 0)
#define	DDC_SLAVE_ADDR		0x504
#define	SLAVE_ADDR_SEG_SHIFT	24
#define	SLAVE_ADDR_EDDC_SHIFT	16
#define	SLAVE_ADDR_OFFSET_SHIFT	8
#define	SLAVE_ADDR_SHIFT	0
#define	DDC_INT_STATUS		0x50c
#define	INT_STATUS_XFER_DONE	(1 << 0)
#define	DDC_FIFO_CTRL		0x510
#define	FIFO_CTRL_CLEAR		(1 << 31)
#define	DDC_BYTE_COUNTER	0x51c
#define	DDC_COMMAND		0x520
#define	COMMAND_EOREAD		(4 << 0)
#define	DDC_CLOCK		0x528
#define	DDC_CLOCK_M		(1 << 3)
#define	DDC_CLOCK_N		(5 << 0)
#define	DDC_FIFO		0x518
#define	SWRST_DELAY		1000
#define	DDC_DELAY		1000
#define	DDC_RETRY		1000
#define	DDC_BLKLEN		16
#define	DDC_ADDR		0x50
#define	EDDC_ADDR		0x60
#define	EDID_LENGTH		128
#define	DDC_CTRL_LINE		0x540
#define	DDC_LINE_SCL_ENABLE	(1 << 8)
#define	DDC_LINE_SDA_ENABLE	(1 << 9)
#define	HDMI_ENABLE_DELAY	50000
#define	DDC_READ_RETRY		4
#define	EXT_TAG			0x00
#define	CEA_TAG_ID		0x02
#define	CEA_DTD			0x03
#define	DTD_BASIC_AUDIO		(1 << 6)
#define	CEA_REV			0x02
#define	CEA_DATA_OFF		0x03
#define	CEA_DATA_START		4
#define	BLOCK_TAG(x)		(((x) >> 5) & 0x7)
#define	BLOCK_TAG_VSDB		3
#define	BLOCK_LEN(x)		((x) & 0x1f)
#define	HDMI_VSDB_MINLEN	5
#define	HDMI_OUI		"\x03\x0c\x00"
#define	HDMI_OUI_LEN		3
#define	HDMI_DEFAULT_FREQ	297000000

struct a10hdmi_softc {
	struct resource		*res;

	struct intr_config_hook	mode_hook;

	uint8_t			edid[EDID_LENGTH];

	int			has_hdmi;
	int			has_audio;

	clk_t			clk_ahb;
	clk_t			clk_hdmi;
	clk_t			clk_lcd;
};

static struct resource_spec a10hdmi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	HDMI_READ(sc, reg)		bus_read_4((sc)->res, (reg))
#define	HDMI_WRITE(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static void
a10hdmi_init(struct a10hdmi_softc *sc)
{
	/* Enable the HDMI module */
	HDMI_WRITE(sc, HDMI_CTRL, CTRL_MODULE_EN);

	/* Configure PLL/DRV settings */
	HDMI_WRITE(sc, HDMI_PADCTRL0, PADCTRL0_BIASEN | PADCTRL0_LDOCEN |
	    PADCTRL0_LDODEN | PADCTRL0_PWENC | PADCTRL0_PWEND |
	    PADCTRL0_PWENG | PADCTRL0_CKEN | PADCTRL0_TXEN);
	HDMI_WRITE(sc, HDMI_PADCTRL1, PADCTRL1_AMP_OPT | PADCTRL1_AMPCK_OPT |
	    PADCTRL1_EMP_OPT | PADCTRL1_EMPCK_OPT | PADCTRL1_REG_DEN |
	    PADCTRL1_REG_DENCK | PADCTRL1_REG_EMP_EN | PADCTRL1_REG_AMP_EN);

	/* Select PLL3 as input clock */
	HDMI_WRITE(sc, HDMI_PLLDBG0, PLLDBG0_CKIN_SEL_PLL3);

	DELAY(HDMI_ENABLE_DELAY);
}

static void
a10hdmi_hpd(void *arg)
{
	struct a10hdmi_softc *sc;
	device_t dev;
	uint32_t hpd;

	dev = arg;
	sc = device_get_softc(dev);

	hpd = HDMI_READ(sc, HDMI_HPD);
	if ((hpd & HPD_DET) == HPD_DET)
		EVENTHANDLER_INVOKE(hdmi_event, dev, HDMI_EVENT_CONNECTED);

	config_intrhook_disestablish(&sc->mode_hook);
}

static int
a10hdmi_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun7i-a20-hdmi"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner HDMI TX");
	return (BUS_PROBE_DEFAULT);
}

static int
a10hdmi_attach(device_t dev)
{
	struct a10hdmi_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, a10hdmi_spec, &sc->res)) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	/* Setup clocks */
	error = clk_get_by_ofw_name(dev, 0, "ahb", &sc->clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot find ahb clock\n");
		return (error);
	}
	error = clk_get_by_ofw_name(dev, 0, "hdmi", &sc->clk_hdmi);
	if (error != 0) {
		device_printf(dev, "cannot find hdmi clock\n");
		return (error);
	}
	error = clk_get_by_ofw_name(dev, 0, "lcd", &sc->clk_lcd);
	if (error != 0) {
		device_printf(dev, "cannot find lcd clock\n");
	}
	/* Enable HDMI clock */
	error = clk_enable(sc->clk_hdmi);
	if (error != 0) {
		device_printf(dev, "cannot enable hdmi clock\n");
		return (error);
	}
	/* Gating AHB clock for HDMI */
	error = clk_enable(sc->clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot enable ahb gate\n");
		return (error);
	}

	a10hdmi_init(sc);

	sc->mode_hook.ich_func = a10hdmi_hpd;
	sc->mode_hook.ich_arg = dev;

	error = config_intrhook_establish(&sc->mode_hook);
	if (error != 0)
		return (error);

	return (0);
}

static int
a10hdmi_ddc_xfer(struct a10hdmi_softc *sc, uint16_t addr, uint8_t seg,
    uint8_t off, int len)
{
	uint32_t val;
	int retry;

	/* Set FIFO direction to read */
	val = HDMI_READ(sc, DDC_CTRL);
	val &= ~CTRL_DDC_FIFO_DIR;
	val |= CTRL_DDC_FIFO_DIR_READ;
	HDMI_WRITE(sc, DDC_CTRL, val);

	/* Setup DDC slave address */
	val = (addr << SLAVE_ADDR_SHIFT) | (seg << SLAVE_ADDR_SEG_SHIFT) |
	    (EDDC_ADDR << SLAVE_ADDR_EDDC_SHIFT) |
	    (off << SLAVE_ADDR_OFFSET_SHIFT);
	HDMI_WRITE(sc, DDC_SLAVE_ADDR, val);

	/* Clear FIFO */
	val = HDMI_READ(sc, DDC_FIFO_CTRL);
	val |= FIFO_CTRL_CLEAR;
	HDMI_WRITE(sc, DDC_FIFO_CTRL, val);

	/* Set transfer length */
	HDMI_WRITE(sc, DDC_BYTE_COUNTER, len);

	/* Set command to "Explicit Offset Address Read" */
	HDMI_WRITE(sc, DDC_COMMAND, COMMAND_EOREAD);

	/* Start transfer */
	val = HDMI_READ(sc, DDC_CTRL);
	val |= CTRL_DDC_ACMD_START;
	HDMI_WRITE(sc, DDC_CTRL, val);

	/* Wait for command to start */
	retry = DDC_RETRY;
	while (--retry > 0) {
		val = HDMI_READ(sc, DDC_CTRL);
		if ((val & CTRL_DDC_ACMD_START) == 0)
			break;
		DELAY(DDC_DELAY);
	}
	if (retry == 0)
		return (ETIMEDOUT);

	/* Ensure that the transfer completed */
	val = HDMI_READ(sc, DDC_INT_STATUS);
	if ((val & INT_STATUS_XFER_DONE) == 0)
		return (EIO);

	return (0);
}

static int
a10hdmi_ddc_read(struct a10hdmi_softc *sc, int block, uint8_t *edid)
{
	int resid, off, len, error;
	uint8_t *pbuf;

	pbuf = edid;
	resid = EDID_LENGTH;
	off = (block & 1) ? EDID_LENGTH : 0;

	while (resid > 0) {
		len = min(resid, DDC_BLKLEN);
		error = a10hdmi_ddc_xfer(sc, DDC_ADDR, block >> 1, off, len);
		if (error != 0)
			return (error);

		bus_read_multi_1(sc->res, DDC_FIFO, pbuf, len);

		pbuf += len;
		off += len;
		resid -= len;
	}

	return (0);
}

static int
a10hdmi_detect_hdmi_vsdb(uint8_t *edid)
{
	int off, p, btag, blen;

	if (edid[EXT_TAG] != CEA_TAG_ID)
		return (0);

	off = edid[CEA_DATA_OFF];

	/* CEA data block collection starts at byte 4 */
	if (off <= CEA_DATA_START)
		return (0);

	/* Parse the CEA data blocks */
	for (p = CEA_DATA_START; p < off;) {
		btag = BLOCK_TAG(edid[p]);
		blen = BLOCK_LEN(edid[p]);

		/* Make sure the length is sane */
		if (p + blen + 1 > off)
			break;

		/* Look for a VSDB with the HDMI 24-bit IEEE registration ID */
		if (btag == BLOCK_TAG_VSDB && blen >= HDMI_VSDB_MINLEN &&
		    memcmp(&edid[p + 1], HDMI_OUI, HDMI_OUI_LEN) == 0)
			return (1);

		/* Next data block */
		p += (1 + blen);
	}

	return (0);
}

static void
a10hdmi_detect_hdmi(struct a10hdmi_softc *sc, int *phdmi, int *paudio)
{
	struct edid_info ei;
	uint8_t edid[EDID_LENGTH];
	int block;

	*phdmi = *paudio = 0;

	if (edid_parse(sc->edid, &ei) != 0)
		return;

	/* Scan through extension blocks, looking for a CEA-861 block. */
	for (block = 1; block <= ei.edid_ext_block_count; block++) {
		if (a10hdmi_ddc_read(sc, block, edid) != 0)
			return;

		if (a10hdmi_detect_hdmi_vsdb(edid) != 0) {
			*phdmi = 1;
			*paudio = ((edid[CEA_DTD] & DTD_BASIC_AUDIO) != 0);
			return;
		}
	}
}

static int
a10hdmi_get_edid(device_t dev, uint8_t **edid, uint32_t *edid_len)
{
	struct a10hdmi_softc *sc;
	int error, retry;

	sc = device_get_softc(dev);
	retry = DDC_READ_RETRY;

	while (--retry > 0) {
		/* I2C software reset */
		HDMI_WRITE(sc, DDC_FIFO_CTRL, 0);
		HDMI_WRITE(sc, DDC_CTRL, CTRL_DDC_EN | CTRL_DDC_SWRST);
		DELAY(SWRST_DELAY);
		if (HDMI_READ(sc, DDC_CTRL) & CTRL_DDC_SWRST) {
			device_printf(dev, "DDC software reset failed\n");
			return (ENXIO);
		}

		/* Configure DDC clock */
		HDMI_WRITE(sc, DDC_CLOCK, DDC_CLOCK_M | DDC_CLOCK_N);

		/* Enable SDA/SCL */
		HDMI_WRITE(sc, DDC_CTRL_LINE,
		    DDC_LINE_SCL_ENABLE | DDC_LINE_SDA_ENABLE);

		/* Read EDID block */
		error = a10hdmi_ddc_read(sc, 0, sc->edid);
		if (error == 0) {
			*edid = sc->edid;
			*edid_len = sizeof(sc->edid);
			break;
		}
	}

	if (error == 0)
		a10hdmi_detect_hdmi(sc, &sc->has_hdmi, &sc->has_audio);
	else
		sc->has_hdmi = sc->has_audio = 0;

	return (error);
}

static void
a10hdmi_set_audiomode(device_t dev, const struct videomode *mode)
{
	struct a10hdmi_softc *sc;
	uint32_t val;
	int retry;

	sc = device_get_softc(dev);

	/* Disable and reset audio module and wait for reset bit to clear */
	HDMI_WRITE(sc, HDMI_AUD_CTRL, AUD_CTRL_RST);
	for (retry = HDMI_AUDIO_RESET_RETRY; retry > 0; retry--) {
		val = HDMI_READ(sc, HDMI_AUD_CTRL);
		if ((val & AUD_CTRL_RST) == 0)
			break;
	}
	if (retry == 0) {
		device_printf(dev, "timeout waiting for audio module\n");
		return;
	}

	if (!sc->has_audio)
		return;

	/* DMA and FIFO control */
	HDMI_WRITE(sc, HDMI_ADMA_CTRL, HDMI_ADMA_MODE_DDMA);

	/* Audio format control (LPCM, S16LE, stereo) */
	HDMI_WRITE(sc, HDMI_AUD_FMT, AUD_FMT_CH(HDMI_AUDIO_CHANNELS));

	/* Channel mappings */
	HDMI_WRITE(sc, HDMI_PCM_CTRL, HDMI_AUDIO_CHANNELMAP);

	/* Clocks */
	HDMI_WRITE(sc, HDMI_AUD_CTS,
	    HDMI_AUDIO_CTS(mode->dot_clock, HDMI_AUDIO_N));
	HDMI_WRITE(sc, HDMI_AUD_N, HDMI_AUDIO_N);

	/* Set sampling frequency to 48 kHz, word length to 16-bit */
	HDMI_WRITE(sc, HDMI_AUD_CH_STATUS0, CH_STATUS0_FS_FREQ_48);
	HDMI_WRITE(sc, HDMI_AUD_CH_STATUS1, CH_STATUS1_WORD_LEN_16);

	/* Enable */
	HDMI_WRITE(sc, HDMI_AUD_CTRL, AUD_CTRL_EN);
}

static int
a10hdmi_get_tcon_config(struct a10hdmi_softc *sc, int *div, int *dbl)
{
	uint64_t lcd_fin, lcd_fout;
	clk_t clk_lcd_parent;
	const char *pname;
	int error;

	error = clk_get_parent(sc->clk_lcd, &clk_lcd_parent);
	if (error != 0)
		return (error);

	/* Get the LCD CH1 special clock 2 divider */
	error = clk_get_freq(sc->clk_lcd, &lcd_fout);
	if (error != 0)
		return (error);
	error = clk_get_freq(clk_lcd_parent, &lcd_fin);
	if (error != 0)
		return (error);
	*div = lcd_fin / lcd_fout;

	/* Detect LCD CH1 special clock using a 1X or 2X source */
	/* XXX */
	pname = clk_get_name(clk_lcd_parent);
	if (strcmp(pname, "pll3") == 0 || strcmp(pname, "pll7") == 0)
		*dbl = 0;
	else
		*dbl = 1;

	return (0);
}

static int
a10hdmi_set_videomode(device_t dev, const struct videomode *mode)
{
	struct a10hdmi_softc *sc;
	int error, clk_div, clk_dbl;
	int dblscan, hfp, hspw, hbp, vfp, vspw, vbp;
	uint32_t val;

	sc = device_get_softc(dev);
	dblscan = !!(mode->flags & VID_DBLSCAN);
	hfp = mode->hsync_start - mode->hdisplay;
	hspw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vspw = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_start;

	error = a10hdmi_get_tcon_config(sc, &clk_div, &clk_dbl);
	if (error != 0) {
		device_printf(dev, "couldn't get tcon config: %d\n", error);
		return (error);
	}

	/* Clear interrupt status */
	HDMI_WRITE(sc, HDMI_INT_STATUS, HDMI_READ(sc, HDMI_INT_STATUS));

	/* Clock setup */
	val = HDMI_READ(sc, HDMI_PADCTRL1);
	val &= ~PADCTRL1_REG_CKSS;
	val |= (clk_dbl ? PADCTRL1_REG_CKSS_2X : PADCTRL1_REG_CKSS_1X);
	HDMI_WRITE(sc, HDMI_PADCTRL1, val);
	HDMI_WRITE(sc, HDMI_PLLCTRL0, PLLCTRL0_PLL_EN | PLLCTRL0_BWS |
	    PLLCTRL0_HV_IS_33 | PLLCTRL0_LDO1_EN | PLLCTRL0_LDO2_EN |
	    PLLCTRL0_SDIV2 | PLLCTRL0_VCO_GAIN | PLLCTRL0_S |
	    PLLCTRL0_CP_S | PLLCTRL0_CS | PLLCTRL0_PREDIV(clk_div) |
	    PLLCTRL0_VCO_S);

	/* Setup display settings */
	if (bootverbose)
		device_printf(dev, "HDMI: %s, Audio: %s\n",
		    sc->has_hdmi ? "yes" : "no", sc->has_audio ? "yes" : "no");
	val = 0;
	if (sc->has_hdmi)
		val |= VID_CTRL_HDMI_MODE;
	if (mode->flags & VID_INTERLACE)
		val |= VID_CTRL_INTERLACE;
	if (mode->flags & VID_DBLSCAN)
		val |= VID_CTRL_REPEATER_2X;
	HDMI_WRITE(sc, HDMI_VID_CTRL, val);

	/* Setup display timings */
	HDMI_WRITE(sc, HDMI_VID_TIMING0,
	    VID_ACT_V(mode->vdisplay) | VID_ACT_H(mode->hdisplay << dblscan));
	HDMI_WRITE(sc, HDMI_VID_TIMING1,
	    VID_VBP(vbp) | VID_HBP(hbp << dblscan));
	HDMI_WRITE(sc, HDMI_VID_TIMING2,
	    VID_VFP(vfp) | VID_HFP(hfp << dblscan));
	HDMI_WRITE(sc, HDMI_VID_TIMING3,
	    VID_VSPW(vspw) | VID_HSPW(hspw << dblscan));
	val = TX_CLOCK_NORMAL;
	if (mode->flags & VID_PVSYNC)
		val |= VID_VSYNC_ACTSEL;
	if (mode->flags & VID_PHSYNC)
		val |= VID_HSYNC_ACTSEL;
	HDMI_WRITE(sc, HDMI_VID_TIMING4, val);

	/* This is an ordered list of infoframe packets that the HDMI
	 * transmitter will send. Transmit packets in the following order:
	 *  1. General control packet
	 *  2. AVI infoframe
	 *  3. Audio infoframe
	 * There are 2 registers with 4 slots each. The list is terminated
	 * with the special PKT_END marker.
	 */
	HDMI_WRITE(sc, HDMI_PKTCTRL0,
	    PKTCTRL_PACKET(0, PKT_GC) | PKTCTRL_PACKET(1, PKT_AVI) |
	    PKTCTRL_PACKET(2, PKT_AI) | PKTCTRL_PACKET(3, PKT_END));
	HDMI_WRITE(sc, HDMI_PKTCTRL1, 0);

	/* Setup audio */
	a10hdmi_set_audiomode(dev, mode);

	return (0);
}

static int
a10hdmi_enable(device_t dev, int onoff)
{
	struct a10hdmi_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	/* Enable or disable video output */
	val = HDMI_READ(sc, HDMI_VID_CTRL);
	if (onoff)
		val |= VID_CTRL_VIDEO_EN;
	else
		val &= ~VID_CTRL_VIDEO_EN;
	HDMI_WRITE(sc, HDMI_VID_CTRL, val);

	return (0);
}

static device_method_t a10hdmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a10hdmi_probe),
	DEVMETHOD(device_attach,	a10hdmi_attach),

	/* HDMI interface */
	DEVMETHOD(hdmi_get_edid,	a10hdmi_get_edid),
	DEVMETHOD(hdmi_set_videomode,	a10hdmi_set_videomode),
	DEVMETHOD(hdmi_enable,		a10hdmi_enable),

	DEVMETHOD_END
};

static driver_t a10hdmi_driver = {
	"a10hdmi",
	a10hdmi_methods,
	sizeof(struct a10hdmi_softc),
};

static devclass_t a10hdmi_devclass;

DRIVER_MODULE(a10hdmi, simplebus, a10hdmi_driver, a10hdmi_devclass, 0, 0);
MODULE_VERSION(a10hdmi, 1);
