/*-
 * Copyright (c) 2015 Michal Meloun
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
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_fb_helper.h>
#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/drm2/tegra_drm.h>
#include <arm/nvidia/drm2/tegra_hdmi_reg.h>
#include <arm/nvidia/drm2/tegra_dc_reg.h>
#include <arm/nvidia/drm2/hdmi.h>

#include "tegra_dc_if.h"
#include "tegra_drm_if.h"

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, 4 * (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, 4 * (_r))

/* HDA stream format verb. */
#define AC_FMT_CHAN_GET(x)		(((x) >>  0) & 0xf)
#define AC_FMT_CHAN_BITS_GET(x)		(((x) >>  4) & 0x7)
#define AC_FMT_DIV_GET(x)		(((x) >>  8) & 0x7)
#define AC_FMT_MUL_GET(x)		(((x) >> 11) & 0x7)
#define AC_FMT_BASE_44K			(1 << 14)
#define AC_FMT_TYPE_NON_PCM		(1 << 15)

#define	HDMI_REKEY_DEFAULT		56
#define HDMI_ELD_BUFFER_SIZE		96

#define	HDMI_DC_CLOCK_MULTIPIER		2

struct audio_reg {
	uint32_t	audio_clk;
	bus_size_t	acr_reg;
	bus_size_t	nval_reg;
	bus_size_t	aval_reg;
};

static const struct audio_reg audio_regs[] =
{
	{
		.audio_clk = 32000,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_0320,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0320,
	},
	{
		.audio_clk = 44100,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_0441,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0441,
	},
	{
		.audio_clk = 88200,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_0882,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0882,
	},
	{
		.audio_clk = 176400,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_1764,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_1764,
	},
	{
		.audio_clk = 48000,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_0480,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0480,
	},
	{
		.audio_clk = 96000,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_0960,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0960,
	},
	{
		.audio_clk = 192000,
		.acr_reg = HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW,
		.nval_reg = HDMI_NV_PDISP_SOR_AUDIO_NVAL_1920,
		.aval_reg = HDMI_NV_PDISP_SOR_AUDIO_AVAL_1920,
	},
};

struct tmds_config {
	uint32_t pclk;
	uint32_t pll0;
	uint32_t pll1;
	uint32_t drive_c;
	uint32_t pe_c;
	uint32_t peak_c;
	uint32_t pad_ctls;
};

static const struct tmds_config tegra124_tmds_config[] =
{
	{	/* 480p/576p / 25.2MHz/27MHz */
		.pclk = 27000000,
		.pll0 = 0x01003010,
		.pll1 = 0x00301B00,
		.drive_c = 0x1F1F1F1F,
		.pe_c = 0x00000000,
		.peak_c = 0x03030303,
		.pad_ctls = 0x800034BB,
	},
	{	/* 720p/1080i / 74.25MHz  */
		.pclk = 74250000,
		.pll0 = 0x01003110,
		.pll1 = 0x00301500,
		.drive_c = 0x2C2C2C2C,
		.pe_c = 0x00000000,
		.peak_c = 0x07070707,
		.pad_ctls = 0x800034BB,
	},
	{	 /* 1080p / 148.5MHz */
		.pclk = 148500000,
		.pll0 = 0x01003310,
		.pll1 = 0x00301500,
		.drive_c = 0x33333333,
		.pe_c = 0x00000000,
		.peak_c = 0x0C0C0C0C,
		.pad_ctls = 0x800034BB,
	},
	{	/* 2216p / 297MHz  */
		.pclk = UINT_MAX,
		.pll0 = 0x01003F10,
		.pll1 = 0x00300F00,
		.drive_c = 0x37373737,
		.pe_c = 0x00000000,
		.peak_c = 0x17171717,
		.pad_ctls = 0x800036BB,
	},
};


struct hdmi_softc {
	device_t		dev;
	struct resource		*mem_res;
	struct resource		*irq_res;
	void			*irq_ih;

	clk_t			clk_parent;
	clk_t			clk_hdmi;
	hwreset_t		hwreset_hdmi;
	regulator_t		supply_hdmi;
	regulator_t		supply_pll;
	regulator_t		supply_vdd;

	uint64_t		pclk;
	boolean_t 		hdmi_mode;

	int			audio_src_type;
	int			audio_freq;
	int			audio_chans;

	struct tegra_drm 	*drm;
	struct tegra_drm_encoder output;

	const struct tmds_config *tmds_config;
	int			n_tmds_configs;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-hdmi",	1},
	{NULL,				0},
};

/* These functions have been copied from newer version of drm_edid.c */
/* ELD Header Block */
#define DRM_ELD_HEADER_BLOCK_SIZE	4
#define DRM_ELD_BASELINE_ELD_LEN	2       /* in dwords! */
static int drm_eld_size(const uint8_t *eld)
{
	return DRM_ELD_HEADER_BLOCK_SIZE + eld[DRM_ELD_BASELINE_ELD_LEN] * 4;
}

static int
drm_hdmi_avi_infoframe_from_display_mode(struct hdmi_avi_infoframe *frame,
    struct drm_display_mode *mode)
{
	int rv;

	if (!frame || !mode)
		return -EINVAL;

	rv = hdmi_avi_infoframe_init(frame);
	if (rv < 0)
		return rv;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		frame->pixel_repeat = 1;

	frame->video_code = drm_match_cea_mode(mode);

	frame->picture_aspect = HDMI_PICTURE_ASPECT_NONE;
#ifdef FREEBSD_NOTYET
	/*
	 * Populate picture aspect ratio from either
	 * user input (if specified) or from the CEA mode list.
	*/
	if (mode->picture_aspect_ratio == HDMI_PICTURE_ASPECT_4_3 ||
	    mode->picture_aspect_ratio == HDMI_PICTURE_ASPECT_16_9)
		frame->picture_aspect = mode->picture_aspect_ratio;
	else if (frame->video_code > 0)
		frame->picture_aspect = drm_get_cea_aspect_ratio(
		    frame->video_code);
#endif

	frame->active_aspect = HDMI_ACTIVE_ASPECT_PICTURE;
	frame->scan_mode = HDMI_SCAN_MODE_UNDERSCAN;

	return 0;
}
/* --------------------------------------------------------------------- */

static int
hdmi_setup_clock(struct tegra_drm_encoder *output, clk_t clk, uint64_t pclk)
{
	struct hdmi_softc *sc;
	uint64_t freq;
	int rv;

	sc = device_get_softc(output->dev);

	/* Disable consumers clock for while. */
	rv = clk_disable(sc->clk_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot disable 'hdmi' clock\n");
		return (rv);
	}
	rv = clk_disable(clk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot disable display clock\n");
		return (rv);
	}

	/* Set frequency  for Display Controller PLL. */
	freq = HDMI_DC_CLOCK_MULTIPIER * pclk;
	rv = clk_set_freq(sc->clk_parent, freq, 0);
	if (rv != 0) {
		device_printf(output->dev,
		    "Cannot set display pixel frequency\n");
		return (rv);
	}

	/* Reparent display controller */
	rv = clk_set_parent_by_clk(clk, sc->clk_parent);
	if (rv != 0) {
		device_printf(output->dev, "Cannot set parent clock\n");
		return (rv);

	}
	rv = clk_set_freq(clk, freq, 0);
	if (rv != 0) {
		device_printf(output->dev,
		    "Cannot set display controller frequency\n");
		return (rv);
	}
	rv = clk_set_freq(sc->clk_hdmi, pclk, 0);
	if (rv != 0) {
		device_printf(output->dev,
		    "Cannot set display controller frequency\n");
		return (rv);
	}

	/* And reenable consumers clock. */
	rv = clk_enable(clk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable display clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'hdmi' clock\n");
		return (rv);
	}

	rv = clk_get_freq(clk, &freq);
	if (rv != 0) {
		device_printf(output->dev,
		    "Cannot get display controller frequency\n");
		return (rv);
	}

	DRM_DEBUG_KMS("DC frequency: %llu\n", freq);

	return (0);
}

/* -------------------------------------------------------------------
 *
 *	Infoframes.
 *
 */
static void
avi_setup_infoframe(struct hdmi_softc *sc, struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	uint8_t buf[17], *hdr, *pb;;
	ssize_t rv;

	rv = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (rv < 0) {
		device_printf(sc->dev, "Cannot setup AVI infoframe: %zd\n", rv);
		return;
	}
	rv = hdmi_avi_infoframe_pack(&frame, buf, sizeof(buf));
	if (rv < 0) {
		device_printf(sc->dev, "Cannot pack AVI infoframe: %zd\n", rv);
		return;
	}
	hdr = buf + 0;
	pb = buf + 3;
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER,
	    (hdr[2] << 16) | (hdr[1] << 8) | (hdr[0] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW,
	    (pb[3] << 24) |(pb[2] << 16) | (pb[1] << 8) | (pb[0] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH,
	    (pb[6] << 16) | (pb[5] << 8) | (pb[4] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW,
	    (pb[10] << 24) |(pb[9] << 16) | (pb[8] << 8) | (pb[7] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH,
	    (pb[13] << 16) | (pb[12] << 8) | (pb[11] << 0));

	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL,
	   AVI_INFOFRAME_CTRL_ENABLE);
}

static void
audio_setup_infoframe(struct hdmi_softc *sc)
{
	struct hdmi_audio_infoframe frame;
	uint8_t buf[14], *hdr, *pb;
	ssize_t rv;


	rv = hdmi_audio_infoframe_init(&frame);
	frame.channels = sc->audio_chans;
	rv = hdmi_audio_infoframe_pack(&frame, buf, sizeof(buf));
	if (rv < 0) {
		device_printf(sc->dev, "Cannot pack audio infoframe\n");
		return;
	}
	hdr = buf + 0;
	pb = buf + 3;
	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER,
	    (hdr[2] << 16) | (hdr[1] << 8) | (hdr[0] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW,
	    (pb[3] << 24) |(pb[2] << 16) | (pb[1] << 8) | (pb[0] << 0));
	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH,
	    (pb[5] << 8) | (pb[4] << 0));

	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL,
	   AUDIO_INFOFRAME_CTRL_ENABLE);
}

/* -------------------------------------------------------------------
 *
 *	Audio
 *
 */
static void
init_hda_eld(struct hdmi_softc *sc)
{
	size_t size;
	int i ;
	uint32_t val;

	size = drm_eld_size(sc->output.connector.eld);
	for (i = 0; i < HDMI_ELD_BUFFER_SIZE; i++) {
		val = i << 8;
		if (i < size)
			val |= sc->output.connector.eld[i];
		WR4(sc, HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR, val);
	}
	WR4(sc,HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE,
	    SOR_AUDIO_HDA_PRESENSE_VALID | SOR_AUDIO_HDA_PRESENSE_PRESENT);
}

static int
get_audio_regs(int freq, bus_size_t *acr_reg, bus_size_t *nval_reg,
    bus_size_t *aval_reg)
{
	int i;
	const struct audio_reg *reg;

	for (i = 0; i < nitems(audio_regs) ; i++) {
		reg = audio_regs + i;
		if (reg->audio_clk == freq) {
			if (acr_reg != NULL)
				*acr_reg = reg->acr_reg;
			if (nval_reg != NULL)
				*nval_reg = reg->nval_reg;
			if (aval_reg != NULL)
				*aval_reg = reg->aval_reg;
			return (0);
		}
	}
	return (ERANGE);
}

#define FR_BITS 16
#define TO_FFP(x) (((int64_t)(x)) << FR_BITS)
#define TO_INT(x) ((int)((x) >> FR_BITS))
static int
get_hda_cts_n(uint32_t audio_freq_hz, uint32_t pixclk_freq_hz,
    uint32_t *best_cts, uint32_t *best_n, uint32_t *best_a)
{
	int min_n;
	int max_n;
	int ideal_n;
	int n;
	int cts;
	int aval;
	int64_t err_f;
	int64_t min_err_f;
	int64_t cts_f;
	int64_t aval_f;
	int64_t half_f;		/* constant 0.5 */
	bool better_n;

	/*
	 * All floats are in fixed I48.16 format.
	 *
	 * Ideal ACR interval is 1000 hz (1 ms);
	 * acceptable is 300 hz .. 1500 hz
	 */
	min_n = 128 * audio_freq_hz / 1500;
	max_n = 128 * audio_freq_hz / 300;
	ideal_n = 128 * audio_freq_hz / 1000;
	min_err_f = TO_FFP(100);
	half_f = TO_FFP(1) / 2;

	*best_n = 0;
	*best_cts = 0;
	*best_a = 0;

	for (n = min_n; n <= max_n; n++) {
		cts_f = TO_FFP(pixclk_freq_hz);
		cts_f *= n;
		cts_f /= 128 * audio_freq_hz;
		cts = TO_INT(cts_f + half_f);		/* round */
		err_f = cts_f - TO_FFP(cts);
		if (err_f < 0)
			err_f = -err_f;
		aval_f = TO_FFP(24000000);
		aval_f *= n;
		aval_f /= 128 * audio_freq_hz;
		aval = TO_INT(aval_f);			/* truncate */

		better_n = abs(n - ideal_n) < abs((int)(*best_n) - ideal_n);
		if (TO_FFP(aval) == aval_f &&
		    (err_f < min_err_f || (err_f == min_err_f && better_n))) {

			min_err_f = err_f;
			*best_n = (uint32_t)n;
			*best_cts = (uint32_t)cts;
			*best_a = (uint32_t)aval;

			if (err_f == 0 && n == ideal_n)
				break;
		}
	}
	return (0);
}
#undef FR_BITS
#undef TO_FFP
#undef TO_INT

static int
audio_setup(struct hdmi_softc *sc)
{
	uint32_t val;
	uint32_t audio_n;
	uint32_t audio_cts;
	uint32_t audio_aval;
	uint64_t hdmi_freq;
	bus_size_t aval_reg;
	int rv;

	if (!sc->hdmi_mode)
		return (ENOTSUP);
	rv  = get_audio_regs(sc->audio_freq, NULL, NULL, &aval_reg);
	if (rv != 0) {
		device_printf(sc->dev, "Unsupported audio frequency.\n");
		return (rv);
	}

	rv = clk_get_freq(sc->clk_hdmi, &hdmi_freq);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get hdmi frequency: %d\n", rv);
		return (rv);
	}

	rv = get_hda_cts_n(sc->audio_freq, hdmi_freq, &audio_cts, &audio_n,
	    &audio_aval);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot compute audio coefs: %d\n", rv);
		return (rv);
	}

	/* Audio infoframe. */
	audio_setup_infoframe(sc);
	/* Setup audio source */
	WR4(sc, HDMI_NV_PDISP_SOR_AUDIO_CNTRL0,
	    SOR_AUDIO_CNTRL0_SOURCE_SELECT(sc->audio_src_type) |
	    SOR_AUDIO_CNTRL0_INJECT_NULLSMPL);

	val = RD4(sc, HDMI_NV_PDISP_SOR_AUDIO_SPARE0);
	val |= SOR_AUDIO_SPARE0_HBR_ENABLE;
	WR4(sc, HDMI_NV_PDISP_SOR_AUDIO_SPARE0, val);

	WR4(sc, HDMI_NV_PDISP_HDMI_ACR_CTRL, 0);

	WR4(sc, HDMI_NV_PDISP_AUDIO_N,
	    AUDIO_N_RESETF |
	    AUDIO_N_GENERATE_ALTERNATE |
	    AUDIO_N_VALUE(audio_n - 1));

	WR4(sc, HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH,
	    ACR_SUBPACK_N(audio_n) | ACR_ENABLE);

	WR4(sc, HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW,
	    ACR_SUBPACK_CTS(audio_cts));

	WR4(sc, HDMI_NV_PDISP_HDMI_SPARE,
	    SPARE_HW_CTS | SPARE_FORCE_SW_CTS | SPARE_CTS_RESET_VAL(1));

	val = RD4(sc, HDMI_NV_PDISP_AUDIO_N);
	val &= ~AUDIO_N_RESETF;
	WR4(sc, HDMI_NV_PDISP_AUDIO_N, val);

	WR4(sc, aval_reg, audio_aval);

	return (0);
}

static void
audio_disable(struct hdmi_softc *sc) {
	uint32_t val;

	/* Disable audio */
	val = RD4(sc,  HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	val &= ~GENERIC_CTRL_AUDIO;
	WR4(sc, HDMI_NV_PDISP_HDMI_GENERIC_CTRL, val);

	/* Disable audio infoframes */
	val = RD4(sc,  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	val &= ~AUDIO_INFOFRAME_CTRL_ENABLE;
	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL, val);
}

static void
audio_enable(struct hdmi_softc *sc) {
	uint32_t val;

	if (!sc->hdmi_mode)
		audio_disable(sc);

	/* Enable audio infoframes */
	val = RD4(sc,  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	val |= AUDIO_INFOFRAME_CTRL_ENABLE;
	WR4(sc, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL, val);

	/* Enable audio */
	val = RD4(sc,  HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	val |= GENERIC_CTRL_AUDIO;
	WR4(sc, HDMI_NV_PDISP_HDMI_GENERIC_CTRL, val);
}

/* -------------------------------------------------------------------
 *
 *    HDMI.
 *
 */
 /* Process format change notification from HDA */
static void
hda_intr(struct hdmi_softc *sc)
{
	uint32_t val;
	int rv;

	if (!sc->hdmi_mode)
		return;

	val = RD4(sc, HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH0);
	if ((val & (1 << 30)) == 0) {
		audio_disable(sc);
		return;
	}

	/* XXX Move this to any header */
	/* Keep in sync with HDA */
	sc->audio_freq =  val & 0x00FFFFFF;
	sc->audio_chans = (val >> 24) & 0x0f;
	DRM_DEBUG_KMS("%d channel(s) at %dHz\n", sc->audio_chans,
	    sc->audio_freq);

	rv = audio_setup(sc);
	if (rv != 0) {
		audio_disable(sc);
		return;
	}

	audio_enable(sc);
}

static void
tmds_init(struct hdmi_softc *sc, const struct tmds_config *tmds)
{

	WR4(sc, HDMI_NV_PDISP_SOR_PLL0, tmds->pll0);
	WR4(sc, HDMI_NV_PDISP_SOR_PLL1, tmds->pll1);
	WR4(sc, HDMI_NV_PDISP_PE_CURRENT, tmds->pe_c);
	WR4(sc, HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT, tmds->drive_c);
	WR4(sc, HDMI_NV_PDISP_SOR_IO_PEAK_CURRENT, tmds->peak_c);
	WR4(sc, HDMI_NV_PDISP_SOR_PAD_CTLS0, tmds->pad_ctls);
}

static int
hdmi_sor_start(struct hdmi_softc *sc, struct drm_display_mode *mode)
{
	int i;
	uint32_t val;

	/* Enable TMDS macro */
	val = RD4(sc, HDMI_NV_PDISP_SOR_PLL0);
	val &= ~SOR_PLL0_PWR;
	val &= ~SOR_PLL0_VCOPD;
	val &= ~SOR_PLL0_PULLDOWN;
	WR4(sc, HDMI_NV_PDISP_SOR_PLL0, val);
	DELAY(10);

	val = RD4(sc, HDMI_NV_PDISP_SOR_PLL0);
	val &= ~SOR_PLL0_PDBG;
	WR4(sc, HDMI_NV_PDISP_SOR_PLL0, val);

	WR4(sc, HDMI_NV_PDISP_SOR_PWR, SOR_PWR_SETTING_NEW);
	WR4(sc, HDMI_NV_PDISP_SOR_PWR, 0);

	/* Wait until SOR is ready */
	for (i = 1000; i > 0; i--) {
		val = RD4(sc, HDMI_NV_PDISP_SOR_PWR);
		if ((val & SOR_PWR_SETTING_NEW) == 0)
			break;
		DELAY(10);
	}
	if (i <= 0) {
		device_printf(sc->dev, "Timeouted while enabling SOR power.\n");
		return (ETIMEDOUT);
	}

	val = SOR_STATE2_ASY_OWNER(ASY_OWNER_HEAD0) |
	    SOR_STATE2_ASY_SUBOWNER(SUBOWNER_BOTH) |
	    SOR_STATE2_ASY_CRCMODE(ASY_CRCMODE_COMPLETE) |
	    SOR_STATE2_ASY_PROTOCOL(ASY_PROTOCOL_SINGLE_TMDS_A);
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= SOR_STATE2_ASY_HSYNCPOL_NEG;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= SOR_STATE2_ASY_VSYNCPOL_NEG;
	WR4(sc, HDMI_NV_PDISP_SOR_STATE2, val);

	WR4(sc, HDMI_NV_PDISP_SOR_STATE1, SOR_STATE1_ASY_ORMODE_NORMAL |
	    SOR_STATE1_ASY_HEAD_OPMODE(ASY_HEAD_OPMODE_AWAKE));

	WR4(sc, HDMI_NV_PDISP_SOR_STATE0, 0);
	WR4(sc, HDMI_NV_PDISP_SOR_STATE0, SOR_STATE0_UPDATE);

	val = RD4(sc, HDMI_NV_PDISP_SOR_STATE1);
	val |= SOR_STATE1_ATTACHED;
	WR4(sc, HDMI_NV_PDISP_SOR_STATE1, val);

	WR4(sc, HDMI_NV_PDISP_SOR_STATE0, 0);

	return 0;
}

static int
hdmi_disable(struct hdmi_softc *sc)
{
	struct tegra_crtc *crtc;
	device_t dc;
	uint32_t val;

	dc = NULL;
	if (sc->output.encoder.crtc != NULL) {
		crtc = container_of(sc->output.encoder.crtc, struct tegra_crtc,
		     drm_crtc);
		dc = crtc->dev;
	}

	if (dc != NULL) {
		TEGRA_DC_HDMI_ENABLE(dc, false);
		TEGRA_DC_DISPLAY_ENABLE(dc, false);
	}
	audio_disable(sc);
	val = RD4(sc,  HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
	val &= ~AVI_INFOFRAME_CTRL_ENABLE;
	WR4(sc, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL, val);

	/* Disable interrupts */
	WR4(sc, HDMI_NV_PDISP_INT_ENABLE, 0);
	WR4(sc, HDMI_NV_PDISP_INT_MASK, 0);

	return (0);
}

static int
hdmi_enable(struct hdmi_softc *sc)
{
	uint64_t freq;
	struct drm_display_mode *mode;
	struct tegra_crtc *crtc;
	uint32_t val, h_sync_width, h_back_porch, h_front_porch, h_pulse_start;
	uint32_t h_max_ac_packet, div8_2;
	device_t dc;
	int i, rv;

	mode = &sc->output.encoder.crtc->mode;
	crtc = container_of(sc->output.encoder.crtc, struct tegra_crtc,
	    drm_crtc);
	dc = crtc->dev;

	/* Compute all timings first. */
	sc->pclk = mode->clock * 1000;
	h_sync_width = mode->hsync_end - mode->hsync_start;
	h_back_porch = mode->htotal - mode->hsync_end;
	h_front_porch = mode->hsync_start - mode->hdisplay;
	h_pulse_start = 1 + h_sync_width + h_back_porch - 10;
	h_max_ac_packet = (h_sync_width + h_back_porch + h_front_porch -
	    HDMI_REKEY_DEFAULT - 18) / 32;

	/* Check if HDMI device is connected and detected. */
	if (sc->output.connector.edid_blob_ptr == NULL) {
		sc->hdmi_mode = false;
	} else {
		sc->hdmi_mode = drm_detect_hdmi_monitor(
		    (struct edid *)sc->output.connector.edid_blob_ptr->data);
	}

	/* Get exact HDMI pixel frequency. */
	rv = clk_get_freq(sc->clk_hdmi, &freq);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'hdmi' clock frequency\n");
		    return (rv);
	}
	DRM_DEBUG_KMS("HDMI frequency: %llu Hz\n", freq);

	/* Wakeup SOR power */
	val = RD4(sc, HDMI_NV_PDISP_SOR_PLL0);
	val &= ~SOR_PLL0_PDBG;
	WR4(sc, HDMI_NV_PDISP_SOR_PLL0, val);
	DELAY(10);

	val = RD4(sc, HDMI_NV_PDISP_SOR_PLL0);
	val &= ~SOR_PLL0_PWR;
	WR4(sc, HDMI_NV_PDISP_SOR_PLL0, val);

	/* Setup timings */
	TEGRA_DC_SETUP_TIMING(dc, h_pulse_start);
	WR4(sc, HDMI_NV_PDISP_HDMI_VSYNC_WINDOW,
	    VSYNC_WINDOW_START(0x200) | VSYNC_WINDOW_END(0x210) |
	    VSYNC_WINDOW_ENABLE);

	/* Setup video source and adjust video range */
	val = 0;
	if  (crtc->nvidia_head != 0)
		HDMI_SRC_DISPLAYB;
	if ((mode->hdisplay != 640) || (mode->vdisplay != 480))
		val |= ARM_VIDEO_RANGE_LIMITED;
	WR4(sc, HDMI_NV_PDISP_INPUT_CONTROL, val);

	/* Program SOR reference clock - it uses 8.2 fractional divisor */
	div8_2 = (freq * 4) / 1000000;
	val = SOR_REFCLK_DIV_INT(div8_2 >> 2) | SOR_REFCLK_DIV_FRAC(div8_2);
	WR4(sc, HDMI_NV_PDISP_SOR_REFCLK, val);

	/* Setup audio */
	if (sc->hdmi_mode) {
		rv = audio_setup(sc);
		if (rv != 0)
			sc->hdmi_mode = false;
	}

	/* Init HDA ELD */
	init_hda_eld(sc);
	val = HDMI_CTRL_REKEY(HDMI_REKEY_DEFAULT);
	val |= HDMI_CTRL_MAX_AC_PACKET(h_max_ac_packet);
	if (sc->hdmi_mode)
		val |= HDMI_CTRL_ENABLE;
	WR4(sc, HDMI_NV_PDISP_HDMI_CTRL, val);

	/* Setup TMDS */
	for (i = 0; i < sc->n_tmds_configs; i++) {
		if (sc->pclk <= sc->tmds_config[i].pclk) {
			tmds_init(sc, sc->tmds_config + i);
			break;
		}
	}

	/* Program sequencer. */
	WR4(sc, HDMI_NV_PDISP_SOR_SEQ_CTL,
	    SOR_SEQ_PU_PC(0) | SOR_SEQ_PU_PC_ALT(0) |
	    SOR_SEQ_PD_PC(8) | SOR_SEQ_PD_PC_ALT(8));

	val = SOR_SEQ_INST_WAIT_TIME(1) |
	    SOR_SEQ_INST_WAIT_UNITS(WAIT_UNITS_VSYNC) |
	    SOR_SEQ_INST_HALT |
	    SOR_SEQ_INST_DRIVE_PWM_OUT_LO;
	WR4(sc, HDMI_NV_PDISP_SOR_SEQ_INST(0), val);
	WR4(sc, HDMI_NV_PDISP_SOR_SEQ_INST(8), val);

	val = RD4(sc,HDMI_NV_PDISP_SOR_CSTM);
	val &= ~SOR_CSTM_LVDS_ENABLE;
	val &= ~SOR_CSTM_ROTCLK(~0);
	val |= SOR_CSTM_ROTCLK(2);
	val &= ~SOR_CSTM_MODE(~0);
	val |= SOR_CSTM_MODE(CSTM_MODE_TMDS);
	val |= SOR_CSTM_PLLDIV;
	WR4(sc, HDMI_NV_PDISP_SOR_CSTM, val);

	TEGRA_DC_DISPLAY_ENABLE(dc, false);

	rv = hdmi_sor_start(sc, mode);
	if (rv != 0)
		return (rv);

	TEGRA_DC_HDMI_ENABLE(dc, true);
	TEGRA_DC_DISPLAY_ENABLE(dc, true);

	/* Enable HDA codec interrupt */
	WR4(sc, HDMI_NV_PDISP_INT_MASK, INT_CODEC_SCRATCH0);
	WR4(sc, HDMI_NV_PDISP_INT_ENABLE, INT_CODEC_SCRATCH0);

	if (sc->hdmi_mode) {
		avi_setup_infoframe(sc, mode);
		audio_enable(sc);
	}

	return (0);
}

/* -------------------------------------------------------------------
 *
 *	DRM Interface.
 *
 */
static enum drm_mode_status
hdmi_connector_mode_valid(struct drm_connector *connector,
    struct drm_display_mode *mode)
{
	struct tegra_drm_encoder *output;
	struct hdmi_softc *sc;
	int rv;
	uint64_t freq;

	output = container_of(connector, struct tegra_drm_encoder,
	     connector);
	sc = device_get_softc(output->dev);

	freq = HDMI_DC_CLOCK_MULTIPIER * mode->clock * 1000;
	rv = clk_test_freq(sc->clk_parent, freq, 0);
	DRM_DEBUG_KMS("Test HDMI frequency: %u kHz, rv: %d\n", mode->clock, rv);
	if (rv != 0)
		return (MODE_NOCLOCK);

	return (MODE_OK);
}


static const struct drm_connector_helper_funcs hdmi_connector_helper_funcs = {
	.get_modes = tegra_drm_connector_get_modes,
	.mode_valid = hdmi_connector_mode_valid,
	.best_encoder = tegra_drm_connector_best_encoder,
};

static const struct drm_connector_funcs hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = tegra_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
};

static const struct drm_encoder_funcs hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void
hdmi_encoder_dpms(struct drm_encoder *encoder, int mode)
{

	/* Empty function. */
}

static bool
hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
    const struct drm_display_mode *mode,
    struct drm_display_mode *adjusted)
{

	return (true);
}

static void
hdmi_encoder_prepare(struct drm_encoder *encoder)
{

	/* Empty function. */
}

static void
hdmi_encoder_commit(struct drm_encoder *encoder)
{

	/* Empty function. */
}

static void
hdmi_encoder_mode_set(struct drm_encoder *encoder,
    struct drm_display_mode *mode, struct drm_display_mode *adjusted)
{
	struct tegra_drm_encoder *output;
	struct hdmi_softc *sc;
	int rv;

	output = container_of(encoder, struct tegra_drm_encoder, encoder);
	sc = device_get_softc(output->dev);
	rv = hdmi_enable(sc);
	if (rv != 0)
		device_printf(sc->dev, "Cannot enable HDMI port\n");

}

static void
hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_drm_encoder *output;
	struct hdmi_softc *sc;
	int rv;

	output = container_of(encoder, struct tegra_drm_encoder, encoder);
	sc = device_get_softc(output->dev);
	if (sc == NULL)
		return;
	rv = hdmi_disable(sc);
	if (rv != 0)
		device_printf(sc->dev, "Cannot disable HDMI port\n");
}

static const struct drm_encoder_helper_funcs hdmi_encoder_helper_funcs = {
	.dpms = hdmi_encoder_dpms,
	.mode_fixup = hdmi_encoder_mode_fixup,
	.prepare = hdmi_encoder_prepare,
	.commit = hdmi_encoder_commit,
	.mode_set = hdmi_encoder_mode_set,
	.disable = hdmi_encoder_disable,
};

/* -------------------------------------------------------------------
 *
 *	Bus and infrastructure.
 *
 */
static int
hdmi_init_client(device_t dev, device_t host1x, struct tegra_drm *drm)
{
	struct hdmi_softc *sc;
	phandle_t node;
	int rv;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(sc->dev);
	sc->drm = drm;
	sc->output.setup_clock = &hdmi_setup_clock;

	rv = tegra_drm_encoder_attach(&sc->output, node);
	if (rv != 0) {
		device_printf(dev, "Cannot attach output connector\n");
		return(ENXIO);
	}

	/* Connect this encoder + connector  to DRM. */
	drm_connector_init(&drm->drm_dev, &sc->output.connector,
	   &hdmi_connector_funcs, DRM_MODE_CONNECTOR_HDMIA);

	drm_connector_helper_add(&sc->output.connector,
	    &hdmi_connector_helper_funcs);

	sc->output.connector.dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_init(&drm->drm_dev, &sc->output.encoder,
	    &hdmi_encoder_funcs, DRM_MODE_ENCODER_TMDS);

	drm_encoder_helper_add(&sc->output.encoder, &hdmi_encoder_helper_funcs);

	drm_mode_connector_attach_encoder(&sc->output.connector,
	  &sc->output.encoder);

	rv = tegra_drm_encoder_init(&sc->output, drm);
	if (rv < 0) {
		device_printf(sc->dev, "Unable to init HDMI output\n");
		return (rv);
	}
	sc->output.encoder.possible_crtcs = 0x3;
	return (0);
}

static int
hdmi_exit_client(device_t dev, device_t host1x, struct tegra_drm *drm)
{
	struct hdmi_softc *sc;

	sc = device_get_softc(dev);
	tegra_drm_encoder_exit(&sc->output, drm);
	return (0);
}

static int
get_fdt_resources(struct hdmi_softc *sc, phandle_t node)
{
	int rv;

	rv = regulator_get_by_ofw_property(sc->dev, 0, "hdmi-supply",
	    &sc->supply_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'hdmi' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev,0,  "pll-supply",
	    &sc->supply_pll);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pll' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "vdd-supply",
	    &sc->supply_vdd);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'vdd' regulator\n");
		return (ENXIO);
	}

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "hdmi", &sc->hwreset_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'hdmi' reset\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "parent", &sc->clk_parent);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'parent' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "hdmi", &sc->clk_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'hdmi' clock\n");
		return (ENXIO);
	}

	return (0);
}

static int
enable_fdt_resources(struct hdmi_softc *sc)
{
	int rv;


	rv = clk_set_parent_by_clk(sc->clk_hdmi, sc->clk_parent);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot set parent for 'hdmi' clock\n");
		return (rv);
	}

	/* 594 MHz is arbitrarily selected value */
	rv = clk_set_freq(sc->clk_parent, 594000000, 0);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot set frequency for 'hdmi' parent clock\n");
		return (rv);
	}
	rv = clk_set_freq(sc->clk_hdmi, 594000000 / 4, 0);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot set frequency for 'hdmi' parent clock\n");
		return (rv);
	}

	rv = regulator_enable(sc->supply_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'hdmi' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_pll);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'pll' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_vdd);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'vdd' regulator\n");
		return (rv);
	}

	rv = clk_enable(sc->clk_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'hdmi' clock\n");
		return (rv);
	}

	rv = hwreset_deassert(sc->hwreset_hdmi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset  'hdmi' reset\n");
		return (rv);
	}
	return (0);
}

static void
hdmi_intr(void *arg)
{
	struct hdmi_softc *sc;
	uint32_t status;

	sc = arg;

	/* Confirm interrupt */
	status = RD4(sc, HDMI_NV_PDISP_INT_STATUS);
	WR4(sc, HDMI_NV_PDISP_INT_STATUS, status);

	/* process audio verb from HDA */
	if (status & INT_CODEC_SCRATCH0)
		hda_intr(sc);
}

static int
hdmi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Tegra HDMI");
	return (BUS_PROBE_DEFAULT);
}

static int
hdmi_attach(device_t dev)
{
	struct hdmi_softc *sc;
	phandle_t node;
	int rid, rv;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->output.dev = sc->dev;
	node = ofw_bus_get_node(sc->dev);

	sc->audio_src_type = SOURCE_SELECT_AUTO;
	sc->audio_freq = 44100;
	sc->audio_chans = 2;
	sc->hdmi_mode = false;

	sc->tmds_config = tegra124_tmds_config;
	sc->n_tmds_configs = nitems(tegra124_tmds_config);

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		goto fail;
	}

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate IRQ resources\n");
		goto fail;
	}

	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, hdmi_intr, sc, &sc->irq_ih);
	if (rv != 0) {
		device_printf(dev,
		    "WARNING: unable to register interrupt handler\n");
		goto fail;
	}

	rv = get_fdt_resources(sc, node);
	if (rv != 0) {
		device_printf(dev, "Cannot parse FDT resources\n");
		goto fail;
	}
	rv = enable_fdt_resources(sc);
	if (rv != 0) {
		device_printf(dev, "Cannot enable FDT resources\n");
		goto fail;
	}

	rv = TEGRA_DRM_REGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);
	if (rv != 0) {
		device_printf(dev, "Cannot register DRM device\n");
		goto fail;
	}
	return (bus_generic_attach(dev));

fail:
	TEGRA_DRM_DEREGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);

	if (sc->irq_ih != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_ih);
	if (sc->clk_parent != NULL)
		clk_release(sc->clk_parent);
	if (sc->clk_hdmi != NULL)
		clk_release(sc->clk_hdmi);
	if (sc->hwreset_hdmi != NULL)
		hwreset_release(sc->hwreset_hdmi);
	if (sc->supply_hdmi != NULL)
		regulator_release(sc->supply_hdmi);
	if (sc->supply_pll != NULL)
		regulator_release(sc->supply_pll);
	if (sc->supply_vdd != NULL)
		regulator_release(sc->supply_vdd);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	return (ENXIO);
}

static int
hdmi_detach(device_t dev)
{
	struct hdmi_softc *sc;
	sc = device_get_softc(dev);

	TEGRA_DRM_DEREGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);

	if (sc->irq_ih != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_ih);
	if (sc->clk_parent != NULL)
		clk_release(sc->clk_parent);
	if (sc->clk_hdmi != NULL)
		clk_release(sc->clk_hdmi);
	if (sc->hwreset_hdmi != NULL)
		hwreset_release(sc->hwreset_hdmi);
	if (sc->supply_hdmi != NULL)
		regulator_release(sc->supply_hdmi);
	if (sc->supply_pll != NULL)
		regulator_release(sc->supply_pll);
	if (sc->supply_vdd != NULL)
		regulator_release(sc->supply_vdd);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	return (bus_generic_detach(dev));
}

static device_method_t tegra_hdmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			hdmi_probe),
	DEVMETHOD(device_attach,		hdmi_attach),
	DEVMETHOD(device_detach,		hdmi_detach),

	/* tegra drm interface */
	DEVMETHOD(tegra_drm_init_client,	hdmi_init_client),
	DEVMETHOD(tegra_drm_exit_client,	hdmi_exit_client),

	DEVMETHOD_END
};

static devclass_t tegra_hdmi_devclass;
DEFINE_CLASS_0(tegra_hdmi, tegra_hdmi_driver, tegra_hdmi_methods,
    sizeof(struct hdmi_softc));
DRIVER_MODULE(tegra_hdmi, host1x, tegra_hdmi_driver,
tegra_hdmi_devclass, 0, 0);
