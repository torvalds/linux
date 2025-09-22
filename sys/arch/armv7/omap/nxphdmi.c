/* $OpenBSD: nxphdmi.c,v 1.8 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2016 Ian Sutton <ians@openbsd.org>
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
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>

#include <dev/i2c/i2cvar.h>
#include <dev/videomode/videomode.h>

#include <dev/ofw/ofw_pinctrl.h>

#include <arch/armv7/omap/nxphdmivar.h>

/* TDA19988 registers */
#define	MKREG(page, addr)	(((page) << 8) | (addr))

#define	REGPAGE(reg)		(((reg) >> 8) & 0xff)
#define	REGADDR(reg)		((reg) & 0xff)

#define	TDA_VERSION		MKREG(0x00, 0x00)
#define	TDA_MAIN_CNTRL0		MKREG(0x00, 0x01)
#define		MAIN_CNTRL0_SR		(1 << 0)
#define	TDA_VERSION_MSB		MKREG(0x00, 0x02)
#define	TDA_SOFTRESET		MKREG(0x00, 0x0a)
#define		SOFTRESET_I2C		(1 << 1)
#define		SOFTRESET_AUDIO		(1 << 0)
#define	TDA_DDC_CTRL		MKREG(0x00, 0x0b)
#define		DDC_ENABLE		0
#define	TDA_CCLK		MKREG(0x00, 0x0c)
#define		CCLK_ENABLE		1
#define	TDA_INT_FLAGS_2		MKREG(0x00, 0x11)
#define		INT_FLAGS_2_EDID_BLK_RD	(1 << 1)

#define	TDA_VIP_CNTRL_0		MKREG(0x00, 0x20)
#define	TDA_VIP_CNTRL_1		MKREG(0x00, 0x21)
#define	TDA_VIP_CNTRL_2		MKREG(0x00, 0x22)
#define	TDA_VIP_CNTRL_3		MKREG(0x00, 0x23)
#define		VIP_CNTRL_3_SYNC_HS	(2 << 4)
#define		VIP_CNTRL_3_V_TGL	(1 << 2)
#define		VIP_CNTRL_3_H_TGL	(1 << 1)

#define	TDA_VIP_CNTRL_4		MKREG(0x00, 0x24)
#define		VIP_CNTRL_4_BLANKIT_NDE		(0 << 2)
#define		VIP_CNTRL_4_BLANKIT_HS_VS	(1 << 2)
#define		VIP_CNTRL_4_BLANKIT_NHS_VS	(2 << 2)
#define		VIP_CNTRL_4_BLANKIT_HE_VE	(3 << 2)
#define		VIP_CNTRL_4_BLC_NONE		(0 << 0)
#define		VIP_CNTRL_4_BLC_RGB444		(1 << 0)
#define		VIP_CNTRL_4_BLC_YUV444		(2 << 0)
#define		VIP_CNTRL_4_BLC_YUV422		(3 << 0)
#define	TDA_VIP_CNTRL_5		MKREG(0x00, 0x25)
#define		VIP_CNTRL_5_SP_CNT(n)	(((n) & 3) << 1)
#define	TDA_MUX_VP_VIP_OUT	MKREG(0x00, 0x27)
#define	TDA_MAT_CONTRL		MKREG(0x00, 0x80)
#define		MAT_CONTRL_MAT_BP	(1 << 2)
#define	TDA_VIDFORMAT		MKREG(0x00, 0xa0)
#define	TDA_REFPIX_MSB		MKREG(0x00, 0xa1)
#define	TDA_REFPIX_LSB		MKREG(0x00, 0xa2)
#define	TDA_REFLINE_MSB		MKREG(0x00, 0xa3)
#define	TDA_REFLINE_LSB		MKREG(0x00, 0xa4)
#define	TDA_NPIX_MSB		MKREG(0x00, 0xa5)
#define	TDA_NPIX_LSB		MKREG(0x00, 0xa6)
#define	TDA_NLINE_MSB		MKREG(0x00, 0xa7)
#define	TDA_NLINE_LSB		MKREG(0x00, 0xa8)
#define	TDA_VS_LINE_STRT_1_MSB	MKREG(0x00, 0xa9)
#define	TDA_VS_LINE_STRT_1_LSB	MKREG(0x00, 0xaa)
#define	TDA_VS_PIX_STRT_1_MSB	MKREG(0x00, 0xab)
#define	TDA_VS_PIX_STRT_1_LSB	MKREG(0x00, 0xac)
#define	TDA_VS_LINE_END_1_MSB	MKREG(0x00, 0xad)
#define	TDA_VS_LINE_END_1_LSB	MKREG(0x00, 0xae)
#define	TDA_VS_PIX_END_1_MSB	MKREG(0x00, 0xaf)
#define	TDA_VS_PIX_END_1_LSB	MKREG(0x00, 0xb0)
#define	TDA_VS_LINE_STRT_2_MSB	MKREG(0x00, 0xb1)
#define	TDA_VS_LINE_STRT_2_LSB	MKREG(0x00, 0xb2)
#define	TDA_VS_PIX_STRT_2_MSB	MKREG(0x00, 0xb3)
#define	TDA_VS_PIX_STRT_2_LSB	MKREG(0x00, 0xb4)
#define	TDA_VS_LINE_END_2_MSB	MKREG(0x00, 0xb5)
#define	TDA_VS_LINE_END_2_LSB	MKREG(0x00, 0xb6)
#define	TDA_VS_PIX_END_2_MSB	MKREG(0x00, 0xb7)
#define	TDA_VS_PIX_END_2_LSB	MKREG(0x00, 0xb8)
#define	TDA_HS_PIX_START_MSB	MKREG(0x00, 0xb9)
#define	TDA_HS_PIX_START_LSB	MKREG(0x00, 0xba)
#define	TDA_HS_PIX_STOP_MSB	MKREG(0x00, 0xbb)
#define	TDA_HS_PIX_STOP_LSB	MKREG(0x00, 0xbc)
#define	TDA_VWIN_START_1_MSB	MKREG(0x00, 0xbd)
#define	TDA_VWIN_START_1_LSB	MKREG(0x00, 0xbe)
#define	TDA_VWIN_END_1_MSB	MKREG(0x00, 0xbf)
#define	TDA_VWIN_END_1_LSB	MKREG(0x00, 0xc0)
#define	TDA_VWIN_START_2_MSB	MKREG(0x00, 0xc1)
#define	TDA_VWIN_START_2_LSB	MKREG(0x00, 0xc2)
#define	TDA_VWIN_END_2_MSB	MKREG(0x00, 0xc3)
#define	TDA_VWIN_END_2_LSB	MKREG(0x00, 0xc4)
#define	TDA_DE_START_MSB	MKREG(0x00, 0xc5)
#define	TDA_DE_START_LSB	MKREG(0x00, 0xc6)
#define	TDA_DE_STOP_MSB		MKREG(0x00, 0xc7)
#define	TDA_DE_STOP_LSB		MKREG(0x00, 0xc8)

#define	TDA_TBG_CNTRL_0		MKREG(0x00, 0xca)
#define		TBG_CNTRL_0_SYNC_ONCE	(1 << 7)
#define		TBG_CNTRL_0_SYNC_MTHD	(1 << 6)

#define	TDA_TBG_CNTRL_1		MKREG(0x00, 0xcb)
#define		TBG_CNTRL_1_DWIN_DIS	(1 << 6)
#define		TBG_CNTRL_1_TGL_EN	(1 << 2)
#define		TBG_CNTRL_1_V_TGL	(1 << 1)
#define		TBG_CNTRL_1_H_TGL	(1 << 0)

#define	TDA_HVF_CNTRL_0		MKREG(0x00, 0xe4)
#define		HVF_CNTRL_0_PREFIL_NONE		(0 << 2)
#define		HVF_CNTRL_0_INTPOL_BYPASS	(0 << 0)
#define	TDA_HVF_CNTRL_1		MKREG(0x00, 0xe5)
#define		HVF_CNTRL_1_VQR(x)	(((x) & 3) << 2)
#define		HVF_CNTRL_1_VQR_FULL	HVF_CNTRL_1_VQR(0)
#define	TDA_ENABLE_SPACE	MKREG(0x00, 0xd6)
#define	TDA_RPT_CNTRL		MKREG(0x00, 0xf0)

#define	TDA_PLL_SERIAL_1	MKREG(0x02, 0x00)
#define		PLL_SERIAL_1_SRL_MAN_IP	(1 << 6)
#define	TDA_PLL_SERIAL_2	MKREG(0x02, 0x01)
#define		PLL_SERIAL_2_SRL_PR(x)		(((x) & 0xf) << 4)
#define		PLL_SERIAL_2_SRL_NOSC(x)	(((x) & 0x3) << 0)
#define	TDA_PLL_SERIAL_3	MKREG(0x02, 0x02)
#define		PLL_SERIAL_3_SRL_PXIN_SEL	(1 << 4)
#define		PLL_SERIAL_3_SRL_DE		(1 << 2)
#define		PLL_SERIAL_3_SRL_CCIR		(1 << 0)
#define	TDA_SERIALIZER		MKREG(0x02, 0x03)
#define	TDA_BUFFER_OUT		MKREG(0x02, 0x04)
#define	TDA_PLL_SCG1		MKREG(0x02, 0x05)
#define	TDA_PLL_SCG2		MKREG(0x02, 0x06)
#define	TDA_PLL_SCGN1		MKREG(0x02, 0x07)
#define	TDA_PLL_SCGN2		MKREG(0x02, 0x08)
#define	TDA_PLL_SCGR1		MKREG(0x02, 0x09)
#define	TDA_PLL_SCGR2		MKREG(0x02, 0x0a)

#define	TDA_SEL_CLK		MKREG(0x02, 0x11)
#define		SEL_CLK_ENA_SC_CLK	(1 << 3)
#define		SEL_CLK_SEL_VRF_CLK(x)	(((x) & 3) << 1)
#define		SEL_CLK_SEL_CLK1	(1 << 0)
#define	TDA_ANA_GENERAL		MKREG(0x02, 0x12)

#define	TDA_EDID_DATA0		MKREG(0x09, 0x00)
#define	TDA_EDID_CTRL		MKREG(0x09, 0xfa)
#define	TDA_DDC_ADDR		MKREG(0x09, 0xfb)
#define	TDA_DDC_OFFS		MKREG(0x09, 0xfc)
#define	TDA_DDC_SEGM_ADDR	MKREG(0x09, 0xfd)
#define	TDA_DDC_SEGM		MKREG(0x09, 0xfe)

#define	TDA_IF_VSP		MKREG(0x10, 0x20)
#define	TDA_IF_AVI		MKREG(0x10, 0x40)
#define	TDA_IF_SPD		MKREG(0x10, 0x60)
#define	TDA_IF_AUD		MKREG(0x10, 0x80)
#define	TDA_IF_MPS		MKREG(0x10, 0xa0)

#define	TDA_ENC_CNTRL		MKREG(0x11, 0x0d)
#define		ENC_CNTRL_DVI_MODE	(0 << 2)
#define		ENC_CNTRL_HDMI_MODE	(1 << 2)
#define	TDA_DIP_IF_FLAGS	MKREG(0x11, 0x0f)
#define		DIP_IF_FLAGS_IF5	(1 << 5)
#define		DIP_IF_FLAGS_IF4	(1 << 4)
#define		DIP_IF_FLAGS_IF3	(1 << 3)
#define		DIP_IF_FLAGS_IF2	(1 << 2) /*  AVI IF on page 10h */
#define		DIP_IF_FLAGS_IF1	(1 << 1)

#define	TDA_TX3			MKREG(0x12, 0x9a)
#define	TDA_TX4			MKREG(0x12, 0x9b)
#define		TX4_PD_RAM		(1 << 1)
#define	TDA_HDCP_TX33		MKREG(0x12, 0xb8)
#define		HDCP_TX33_HDMI		(1 << 1)

#define	TDA_CURPAGE_ADDR	0xff

#define	TDA_CEC_ENAMODS		0xff
#define		ENAMODS_RXSENS		(1 << 2)
#define		ENAMODS_HDMI		(1 << 1)
#define	TDA_CEC_FRO_IM_CLK_CTRL	0xfb
#define		CEC_FRO_IM_CLK_CTRL_GHOST_DIS	(1 << 7)
#define		CEC_FRO_IM_CLK_CTRL_IMCLK_SEL	(1 << 1)

/* EDID reading */
#define	EDID_LENGTH		0x80
#define	MAX_READ_ATTEMPTS	100

/* EDID fields */
#define	EDID_MODES0		35
#define	EDID_MODES1		36
#define	EDID_TIMING_START	38
#define	EDID_TIMING_END		54
#define	EDID_TIMING_X(v)	(((v) + 31) * 8)
#define	EDID_FREQ(v)		(((v) & 0x3f) + 60)
#define	EDID_RATIO(v)		(((v) >> 6) & 0x3)
#define	EDID_RATIO_10x16	0
#define	EDID_RATIO_3x4		1	
#define	EDID_RATIO_4x5		2	
#define	EDID_RATIO_9x16		3

/* NXP TDA19988 slave addrs. */
#define	TDA_HDMI		0x70
#define	TDA_CEC			0x34

/* debug/etc macros */
#define DEVNAME(s)		((s)->sc_dev.dv_xname)
#ifdef NXPTDA_DEBUG
int nxphdmi_debug = 1;
#define DPRINTF(n,s)	do { if ((n) <= nxphdmi_debug) printf s; } while (0)
#else
#define DPRINTF(n,s)	do {} while (0)
#endif

struct nxphdmi_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	uint8_t		sc_curpage;
	uint8_t		sc_edid[EDID_LENGTH];
};

int	nxphdmi_match(struct device *, void *, void *);
void	nxphdmi_attach(struct device *, struct device *, void *);

int	nxphdmi_cec_read(struct nxphdmi_softc *, uint8_t, uint8_t *);
int	nxphdmi_cec_write(struct nxphdmi_softc *, uint8_t, uint8_t);
int	nxphdmi_read(struct nxphdmi_softc *, uint16_t, uint8_t *);
int	nxphdmi_write(struct nxphdmi_softc *, uint16_t, uint8_t);
int	nxphdmi_write2(struct nxphdmi_softc *, uint16_t, uint16_t);
int	nxphdmi_set(struct nxphdmi_softc *, uint16_t, uint8_t);
int	nxphdmi_clear(struct nxphdmi_softc *, uint16_t, uint8_t);
int	nxphdmi_set_page(struct nxphdmi_softc *, uint8_t);
int	nxphdmi_read_edid(struct nxphdmi_softc *);
int	nxphdmi_reset(struct nxphdmi_softc *);
int	nxphdmi_init_encoder(struct nxphdmi_softc *, struct videomode *);

int	nxphdmi_get_edid(uint8_t *, int);
int	nxphdmi_set_videomode(struct videomode *);

const struct cfattach nxphdmi_ca = {
	sizeof(struct nxphdmi_softc), nxphdmi_match, nxphdmi_attach
};

struct cfdriver nxphdmi_cd = {
	NULL, "nxphdmi", DV_DULL
};

int
nxphdmi_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "nxp,tda998x") == 0)
		return 1;

	return 0;
}

void
nxphdmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct nxphdmi_softc *sc = (struct nxphdmi_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t data = 0;
	uint16_t version = 0;
	int res = 0, node = *(int *)(ia->ia_cookie);

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_curpage = 0xff;

	if (!node) {
		printf(": not configured\n");
		return;
	} else if ((pinctrl_byname(node, "default") == -1)) {
		printf(": not configured\n");
		return;
	}

	iic_acquire_bus(sc->sc_tag, 0);

	DPRINTF(3,("\n"));

	/* enable HDMI core */
	nxphdmi_cec_write(sc, TDA_CEC_ENAMODS, ENAMODS_RXSENS | ENAMODS_HDMI);
	delay(1000);

	if (!(nxphdmi_reset(sc)))
		DPRINTF(3,("%s: software reset OK\n", DEVNAME(sc)));
	else
		DPRINTF(3,("%s: software reset failed!\n", DEVNAME(sc)));

	/*  PLL registers common configuration */
	nxphdmi_write(sc, TDA_PLL_SERIAL_1, 0x00);
	nxphdmi_write(sc, TDA_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
	nxphdmi_write(sc, TDA_PLL_SERIAL_3, 0x00);
	nxphdmi_write(sc, TDA_SERIALIZER, 0x00);
	nxphdmi_write(sc, TDA_BUFFER_OUT, 0x00);
	nxphdmi_write(sc, TDA_PLL_SCG1, 0x00);
	nxphdmi_write(sc, TDA_SEL_CLK, SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
	nxphdmi_write(sc, TDA_PLL_SCGN1, 0xfa);
	nxphdmi_write(sc, TDA_PLL_SCGN2, 0x00);
	nxphdmi_write(sc, TDA_PLL_SCGR1, 0x5b);
	nxphdmi_write(sc, TDA_PLL_SCGR2, 0x00);
	nxphdmi_write(sc, TDA_PLL_SCG2, 0x10);

	/*  Write the default value MUX register */
	nxphdmi_write(sc, TDA_MUX_VP_VIP_OUT, 0x24);

	res |= nxphdmi_read(sc, TDA_VERSION, &data);
	version |= data;
	res |= nxphdmi_read(sc, TDA_VERSION_MSB, &data);
	version |= (data << 8);
	version &= ~0x30;

	if (!res) {
		DPRINTF(3,("%s: ", DEVNAME(sc)));
		printf(": rev 0x%04x\n", version);
	} else {
		DPRINTF(3,("%s: ", DEVNAME(sc)));
		printf(": failed to enable HDMI core, exiting...\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}

	nxphdmi_write(sc, TDA_DDC_CTRL, DDC_ENABLE);
	nxphdmi_write(sc, TDA_TX3, 39);

	nxphdmi_cec_write(sc, TDA_CEC_FRO_IM_CLK_CTRL,
	    CEC_FRO_IM_CLK_CTRL_GHOST_DIS | CEC_FRO_IM_CLK_CTRL_IMCLK_SEL);

	if (nxphdmi_read_edid(sc)) {
		DPRINTF(3,("%s: failed to read EDID bits, exiting!\n",
		    DEVNAME(sc)));
		return;
	}

	/*  Default values for RGB 4:4:4 mapping */
	nxphdmi_write(sc, TDA_VIP_CNTRL_0, 0x23);
	nxphdmi_write(sc, TDA_VIP_CNTRL_1, 0x01);
	nxphdmi_write(sc, TDA_VIP_CNTRL_2, 0x45);

	iic_release_bus(sc->sc_tag, 0);
}

int
nxphdmi_cec_read(struct nxphdmi_softc *sc, uint8_t addr, uint8_t *buf)
{
	int ret = 0;

	if ((ret |= iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, TDA_CEC,
	    &addr, 1, NULL, 0, 0))) {
		DPRINTF(3,("%s: (CEC) failed to read addr 0x%02x, errno %d\n",
		    DEVNAME(sc), addr, ret));
		return ret;
	}

	DPRINTF(3,("%s: (CEC) read 0x%02x from 0x%02x\n", DEVNAME(sc), *buf,
	    addr));

	return ret;
}

int
nxphdmi_cec_write(struct nxphdmi_softc *sc, uint8_t addr, uint8_t val)
{
	int ret = 0;
	uint8_t sendbuf[] = { addr, val };

	if ((ret |= iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, TDA_CEC,
	    &sendbuf, 2, NULL, 0, 0))) {
		DPRINTF(3,(
		    "%s: (CEC) failed to write 0x%02x to 0x%02x, errno %d\n",
		    DEVNAME(sc), val, addr, ret));
		return ret;
	}

	DPRINTF(3,("%s: (CEC) wrote 0x%02x to 0x%02x\n", DEVNAME(sc), val,
	    addr));

	return ret;
}

int
nxphdmi_read(struct nxphdmi_softc *sc, uint16_t reg, uint8_t *buf)
{
	int ret = 0;

	nxphdmi_set_page(sc, REGPAGE(reg));

	if ((ret = iic_smbus_read_byte(sc->sc_tag, TDA_HDMI, REGADDR(reg),
	    buf, 0))) {
		DPRINTF(3,(
		    "%s: failed to read addr 0x%02x on page 0x%02x, errno %d\n",
		    DEVNAME(sc), REGADDR(reg), REGPAGE(reg), ret));
		return ret;
	}

	DPRINTF(3,("%s: read  0x%02x from 0x%02x on page 0x%02x\n",
	    DEVNAME(sc), *buf, REGADDR(reg), REGPAGE(reg)));

	return ret;
}

int
nxphdmi_write(struct nxphdmi_softc *sc, uint16_t reg, uint8_t val)
{
	int ret = 0;
	uint8_t sendbuf[] = { REGADDR(reg), val };

	nxphdmi_set_page(sc, REGPAGE(reg));

	if ((ret = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, TDA_HDMI,
	    &sendbuf, 2, NULL, 0, 0))) {
	    DPRINTF(3,(
		"%s: failed to write 0x%02x to 0x%02x on page 0x%02x, errno %d\n",
		DEVNAME(sc), val, REGADDR(reg), REGPAGE(reg), ret));
		return ret;
	}

	DPRINTF(3,("%s: wrote 0x%02x  to  0x%02x on page 0x%02x\n",
	    DEVNAME(sc), val, REGADDR(reg), REGPAGE(reg)));

	return ret;
}

int
nxphdmi_write2(struct nxphdmi_softc *sc, uint16_t reg, uint16_t val)
{
	int ret = 0;
	uint8_t sendbuf[] = { REGADDR(reg), val >> 8, val & 0xff };

	nxphdmi_set_page(sc, REGPAGE(reg));

	if ((ret = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, TDA_HDMI,
	    &sendbuf, 3, NULL, 0, 0))) {
		DPRINTF(3,(
		"%s: failed to write 0x%04x to 0x%02x on page 0x%02x, errno %d\n",
		DEVNAME(sc), val, REGADDR(reg), REGPAGE(reg), ret));
		return ret;
	}

	DPRINTF(3,("%s: wrote 0x%04x  to  0x%02x on page 0x%02x\n",
	    DEVNAME(sc), val, REGADDR(reg), REGPAGE(reg)));

	return ret;

}

int
nxphdmi_set(struct nxphdmi_softc *sc, uint16_t reg, uint8_t bits)
{
	int ret = 0;
	uint8_t buf;

	ret |= nxphdmi_read(sc, reg, &buf);
	buf |= bits;
	ret |= nxphdmi_write(sc, reg, buf);

	return ret;
}

int
nxphdmi_clear(struct nxphdmi_softc *sc, uint16_t reg, uint8_t bits)
{
	int ret = 0;
	uint8_t buf;

	ret |= nxphdmi_read(sc, reg, &buf);
	buf &= ~bits;
	ret |= nxphdmi_write(sc, reg, buf);

	return ret;
}

int
nxphdmi_set_page(struct nxphdmi_softc *sc, uint8_t page)
{
	int ret = 0;
	uint8_t sendbuf[] = { TDA_CURPAGE_ADDR, page };

	if (sc->sc_curpage == page)
		return ret;

	if ((ret = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, TDA_HDMI,
	    &sendbuf, sizeof(sendbuf), NULL, 0, 0))) {
		DPRINTF(3,("%s: failed to set memory page 0x%02x, errno %d\n",
		    DEVNAME(sc),
		page, ret));
		return ret;
	}

	sc->sc_curpage = page;
	DPRINTF(3,("%s: set page to 0x%02x\n", DEVNAME(sc), page));

	return ret;
}

int
nxphdmi_read_edid(struct nxphdmi_softc *sc)
{
	int i = 0, ret = 0;
	uint8_t reg;

	nxphdmi_set(sc, TDA_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	/*  Block 0 */
	nxphdmi_write(sc, TDA_DDC_ADDR, 0xa0);
	nxphdmi_write(sc, TDA_DDC_OFFS, 0x00);
	nxphdmi_write(sc, TDA_DDC_SEGM_ADDR, 0x60);
	nxphdmi_write(sc, TDA_DDC_SEGM, 0x00);

	nxphdmi_write(sc, TDA_EDID_CTRL, 1);
	nxphdmi_write(sc, TDA_EDID_CTRL, 0);

	for (; i < MAX_READ_ATTEMPTS; i++) {
		nxphdmi_read(sc, TDA_INT_FLAGS_2, &reg);
		if (reg & INT_FLAGS_2_EDID_BLK_RD) {
			DPRINTF(3,("%s: EDID-ready IRQ fired\n", DEVNAME(sc)));
			break;
		}
	}

	if (i == MAX_READ_ATTEMPTS) {
		printf("%s: no display detected\n", DEVNAME(sc));
		ret = ENXIO;
		return ret;
	}

	nxphdmi_set_page(sc, 0x09);

	reg = 0x00;
	DPRINTF(1,("%s: ------------- EDID -------------", DEVNAME(sc)));
	for (i = 0; i < EDID_LENGTH; i++) {
		iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, TDA_HDMI, &reg, 1,
		    &sc->sc_edid[i], 1, 0);
		if (!(i % 16))
			DPRINTF(1,("\n%s: ", DEVNAME(sc)));
		DPRINTF(1,("%02x", sc->sc_edid[i]));
		reg++;
	}
	DPRINTF(1,("\n%s: --------------------------------\n", DEVNAME(sc)));

	nxphdmi_clear(sc, TDA_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);
	nxphdmi_set(sc, TDA_TX4, TX4_PD_RAM);

	return ret;
}

int
nxphdmi_reset(struct nxphdmi_softc *sc)
{
	int ret = 0;

	/* reset core */
	ret |= nxphdmi_set(sc, TDA_SOFTRESET, 3);
	delay(100);
	ret |= nxphdmi_clear(sc, TDA_SOFTRESET, 3);
	delay(100);

	/* reset transmitter */
	ret |= nxphdmi_set(sc, TDA_MAIN_CNTRL0, MAIN_CNTRL0_SR);
	ret |= nxphdmi_clear(sc, TDA_MAIN_CNTRL0, MAIN_CNTRL0_SR);

	return ret;
}

int
nxphdmi_init_encoder(struct nxphdmi_softc *sc, struct videomode *mode)
{
	int ret = 0;

	uint16_t ref_pix, ref_line, n_pix, n_line;
	uint16_t hs_pix_start, hs_pix_stop;
	uint16_t vs1_pix_start, vs1_pix_stop;
	uint16_t vs1_line_start, vs1_line_end;
	uint16_t vs2_pix_start, vs2_pix_stop;
	uint16_t vs2_line_start, vs2_line_end;
	uint16_t vwin1_line_start, vwin1_line_end;
	uint16_t vwin2_line_start, vwin2_line_end;
	uint16_t de_start, de_stop;
	uint8_t reg, div;

	n_pix = mode->htotal;
	n_line = mode->vtotal;

	hs_pix_stop = mode->hsync_end - mode->hdisplay;
	hs_pix_start = mode->hsync_start - mode->hdisplay;

	de_stop = mode->htotal;
	de_start = mode->htotal - mode->hdisplay;
	ref_pix = hs_pix_start + 3;

	if (mode->flags & VID_HSKEW)
		ref_pix += mode->hsync_end - mode->hsync_start;

	if ((mode->flags & VID_INTERLACE) == 0) {
		ref_line = 1 + mode->vsync_start - mode->vdisplay;
		vwin1_line_start = mode->vtotal - mode->vdisplay - 1;
		vwin1_line_end = vwin1_line_start + mode->vdisplay;

		vs1_pix_start = vs1_pix_stop = hs_pix_start;
		vs1_line_start = mode->vsync_start - mode->vdisplay;
		vs1_line_end = vs1_line_start +
		    mode->vsync_end - mode->vsync_start;

		vwin2_line_start = vwin2_line_end = 0;
		vs2_pix_start = vs2_pix_stop = 0;
		vs2_line_start = vs2_line_end = 0;
	} else {
		ref_line = 1 + (mode->vsync_start - mode->vdisplay)/2;
		vwin1_line_start = (mode->vtotal - mode->vdisplay)/2;
		vwin1_line_end = vwin1_line_start + mode->vdisplay/2;

		vs1_pix_start = vs1_pix_stop = hs_pix_start;
		vs1_line_start = (mode->vsync_start - mode->vdisplay)/2;
		vs1_line_end = vs1_line_start +
		    (mode->vsync_end - mode->vsync_start)/2;

		vwin2_line_start = vwin1_line_start + mode->vtotal/2;
		vwin2_line_end = vwin2_line_start + mode->vdisplay/2;

		vs2_pix_start = vs2_pix_stop = hs_pix_start + mode->htotal/2;
		vs2_line_start = vs1_line_start + mode->vtotal/2 ;
		vs2_line_end = vs2_line_start +
		    (mode->vsync_end - mode->vsync_start)/2;
	}

	div = 148500 / mode->dot_clock;
	if (div != 0) {
		div--;
		if (div > 3)
			div = 3;
	}

	/*  set HDMI HDCP mode off */
	nxphdmi_set(sc, TDA_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	nxphdmi_clear(sc, TDA_HDCP_TX33, HDCP_TX33_HDMI);
	nxphdmi_write(sc, TDA_ENC_CNTRL, ENC_CNTRL_DVI_MODE);

	/*  no pre-filter or interpolator */
	nxphdmi_write(sc, TDA_HVF_CNTRL_0,
	    HVF_CNTRL_0_INTPOL_BYPASS | HVF_CNTRL_0_PREFIL_NONE);
	nxphdmi_write(sc, TDA_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
	nxphdmi_write(sc, TDA_VIP_CNTRL_4,
	    VIP_CNTRL_4_BLANKIT_NDE | VIP_CNTRL_4_BLC_NONE);

	nxphdmi_clear(sc, TDA_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR);
	nxphdmi_clear(sc, TDA_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IP);
	nxphdmi_clear(sc, TDA_PLL_SERIAL_3, PLL_SERIAL_3_SRL_DE);
	nxphdmi_write(sc, TDA_SERIALIZER, 0);
	nxphdmi_write(sc, TDA_HVF_CNTRL_1, HVF_CNTRL_1_VQR_FULL);

	nxphdmi_write(sc, TDA_RPT_CNTRL, 0);
	nxphdmi_write(sc, TDA_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
			SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);

	nxphdmi_write(sc, TDA_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) |
			PLL_SERIAL_2_SRL_PR(0));

	nxphdmi_set(sc, TDA_MAT_CONTRL, MAT_CONTRL_MAT_BP);

	nxphdmi_write(sc, TDA_ANA_GENERAL, 0x09);

	nxphdmi_clear(sc, TDA_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_MTHD);

	/*
	 * Sync on rising HSYNC/VSYNC
	 */
	reg = VIP_CNTRL_3_SYNC_HS;
	if (mode->flags & VID_NHSYNC)
		reg |= VIP_CNTRL_3_H_TGL;
	if (mode->flags & VID_NVSYNC)
		reg |= VIP_CNTRL_3_V_TGL;
	nxphdmi_write(sc, TDA_VIP_CNTRL_3, reg);

	reg = TBG_CNTRL_1_TGL_EN;
	if (mode->flags & VID_NHSYNC)
		reg |= TBG_CNTRL_1_H_TGL;
	if (mode->flags & VID_NVSYNC)
		reg |= TBG_CNTRL_1_V_TGL;
	nxphdmi_write(sc, TDA_TBG_CNTRL_1, reg);

	/*  Program timing */
	nxphdmi_write(sc, TDA_VIDFORMAT, 0x00);

	nxphdmi_write2(sc, TDA_REFPIX_MSB, ref_pix);
	nxphdmi_write2(sc, TDA_REFLINE_MSB, ref_line);
	nxphdmi_write2(sc, TDA_NPIX_MSB, n_pix);
	nxphdmi_write2(sc, TDA_NLINE_MSB, n_line);

	nxphdmi_write2(sc, TDA_VS_LINE_STRT_1_MSB, vs1_line_start);
	nxphdmi_write2(sc, TDA_VS_PIX_STRT_1_MSB, vs1_pix_start);
	nxphdmi_write2(sc, TDA_VS_LINE_END_1_MSB, vs1_line_end);
	nxphdmi_write2(sc, TDA_VS_PIX_END_1_MSB, vs1_pix_stop);
	nxphdmi_write2(sc, TDA_VS_LINE_STRT_2_MSB, vs2_line_start);
	nxphdmi_write2(sc, TDA_VS_PIX_STRT_2_MSB, vs2_pix_start);
	nxphdmi_write2(sc, TDA_VS_LINE_END_2_MSB, vs2_line_end);
	nxphdmi_write2(sc, TDA_VS_PIX_END_2_MSB, vs2_pix_stop);
	nxphdmi_write2(sc, TDA_HS_PIX_START_MSB, hs_pix_start);
	nxphdmi_write2(sc, TDA_HS_PIX_STOP_MSB, hs_pix_stop);
	nxphdmi_write2(sc, TDA_VWIN_START_1_MSB, vwin1_line_start);
	nxphdmi_write2(sc, TDA_VWIN_END_1_MSB, vwin1_line_end);
	nxphdmi_write2(sc, TDA_VWIN_START_2_MSB, vwin2_line_start);
	nxphdmi_write2(sc, TDA_VWIN_END_2_MSB, vwin2_line_end);
	nxphdmi_write2(sc, TDA_DE_START_MSB, de_start);
	nxphdmi_write2(sc, TDA_DE_STOP_MSB, de_stop);

	nxphdmi_write(sc, TDA_ENABLE_SPACE, 0x00);

	/*  must be last register set */
	nxphdmi_clear(sc, TDA_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_ONCE);

	return ret;
}

int
nxphdmi_get_edid(uint8_t *buf, int buflen)
{
	int ret = 0, i;
	struct nxphdmi_softc *sc = nxphdmi_cd.cd_devs[0];

	if (buflen < EDID_LENGTH)
		return -1;

	for (i = 0; i < EDID_LENGTH; i++)
		buf[i] = sc->sc_edid[i];

	return ret;
}

int
nxphdmi_set_videomode(struct videomode *mode)
{
	int ret = 0;
	struct nxphdmi_softc *sc = nxphdmi_cd.cd_devs[0];

	ret = nxphdmi_init_encoder(sc, mode);

	return ret;
}
