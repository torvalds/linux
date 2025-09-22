/* $OpenBSD: omdisplay.c,v 1.11 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2007 Dale Rahn <drahn@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <machine/bus.h>
#include <armv7/omap/omgpiovar.h>

#include <dev/cons.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_callbacks.h>

#include <dev/rasops/rasops.h>

#include "splash16.h"

#define OMDISPLAY_SIZE			0x1000
/* registers */
/* DSS */
#define DSS_REVISIONNUMBER		0x00
#define DSS_CONTROL			0x40
#define DSS_PSA_LCD_REG_1		0x50
#define DSS_PSA_LCD_REG_2		0x54
#define DSS_PSA_VIDEO_REG		0x58
#define DSS_STATUS			0x5C

/* DCR */
#define DISPC_REVISION			0x0000
#define DISPC_SYSCONFIG			0x0010
#define		DISPC_SYSCONFIG_AUTOIDLE		0x00000001
#define		DISPC_SYSCONFIG_SOFTRESET		0x00000002
#define		DISPC_SYSCONFIG_SIDLEMODE_FORCE		0x00000000
#define		DISPC_SYSCONFIG_SIDLEMODE_NONE		0x00000008
#define		DISPC_SYSCONFIG_SIDLEMODE_SMART		0x00000010
#define		DISPC_SYSCONFIG_MIDLEMODE_FORCE		0x00000000
#define		DISPC_SYSCONFIG_MIDLEMODE_NONE		0x00001000
#define		DISPC_SYSCONFIG_MIDLEMODE_SMART		0x00002000
#define DISPC_SYSSTATUS			0x0014
#define		DISPC_SYSTATUS_RESETDONE		0x00000001
#define DISPC_IRQSTATUS			0x0018
#define		DISPC_IRQSTATUS_FRAMEDONE		0x00000001
#define		DISPC_IRQSTATUS_VSYNC			0x00000002
#define		DISPC_IRQSTATUS_EVSYNCEVEN		0x00000004
#define		DISPC_IRQSTATUS_EVSYNCODD		0x00000008
#define		DISPC_IRQSTATUS_ACBIASCOUNT		0x00000010
#define		DISPC_IRQSTATUS_PROGLINENUM		0x00000020
#define		DISPC_IRQSTATUS_GFXFIFOUNDER		0x00000040
#define		DISPC_IRQSTATUS_GFXENDWINDOW		0x00000080
#define		DISPC_IRQSTATUS_PALGAMMA		0x00000100
#define		DISPC_IRQSTATUS_OCPERROR		0x00000200
#define		DISPC_IRQSTATUS_VID1FIFOUNDER		0x00000400
#define		DISPC_IRQSTATUS_VID1ENDWIND		0x00000800
#define		DISPC_IRQSTATUS_VID2FIFOUNDER		0x00001000
#define		DISPC_IRQSTATUS_VID2ENDWIND		0x00002000
#define		DISPC_IRQSTATUS_SYNCLOST		0x00004000
#define DISPC_IRQENABLE			0x001C
#define		DISPC_IRQENABLE_FRAMEDONE		0x00000001
#define		DISPC_IRQENABLE_VSYNC			0x00000002
#define		DISPC_IRQENABLE_EVSYNCEVEN		0x00000004
#define		DISPC_IRQENABLE_EVSYNCODD		0x00000008
#define		DISPC_IRQENABLE_ACBIASCOUNT		0x00000010
#define		DISPC_IRQENABLE_PROGLINENUM		0x00000020
#define		DISPC_IRQENABLE_GFXFIFOUNDER		0x00000040
#define		DISPC_IRQENABLE_GFXENDWINDOW		0x00000080
#define		DISPC_IRQENABLE_PALGAMMA		0x00000100
#define		DISPC_IRQENABLE_OCPERROR		0x00000200
#define		DISPC_IRQENABLE_VID1FIFOUNDER		0x00000400
#define		DISPC_IRQENABLE_VID1ENDWIND		0x00000800
#define		DISPC_IRQENABLE_VID2FIFOUNDER		0x00001000
#define		DISPC_IRQENABLE_VID2ENDWIND		0x00002000
#define		DISPC_IRQENABLE_SYNCLOST		0x00004000
#define DISPC_CONTROL			0x0040
#define 	DISPC_CONTROL_LCDENABLE			0x00000001
#define 	DISPC_CONTROL_DIGITALENABLE		0x00000002
#define 	DISPC_CONTROL_MONOCOLOR			0x00000004
#define 	DISPC_CONTROL_STNTFT			0x00000008
#define 	DISPC_CONTROL_M8B			0x00000010
#define 	DISPC_CONTROL_GOLCD			0x00000020
#define 	DISPC_CONTROL_GODIGITAL			0x00000040
#define 	DISPC_CONTROL_TFTDITHEREN		0x00000080
#define 	DISPC_CONTROL_TFTDATALINES_12		0x00000000
#define 	DISPC_CONTROL_TFTDATALINES_16		0x00000100
#define 	DISPC_CONTROL_TFTDATALINES_18		0x00000200
#define 	DISPC_CONTROL_TFTDATALINES_24		0x00000300
#define 	DISPC_CONTROL_SECURE			0x00000400
#define 	DISPC_CONTROL_RFBIMODE			0x00000800
#define 	DISPC_CONTROL_OVERLAYOPT		0x00001000
#define 	DISPC_CONTROL_GPIN0			0x00002000
#define 	DISPC_CONTROL_GPIN1			0x00004000
#define 	DISPC_CONTROL_GPOUT0			0x00008000
#define 	DISPC_CONTROL_GPOUT1			0x00010000
#define 	DISPC_CONTROL_HT			0x00070000
#define 	DISPC_CONTROL_HT_s(x)			((x) << 17)
#define 	DISPC_CONTROL_TDMENABLE			0x00100000
#define 	DISPC_CONTROL_TDMPARALLEL_8		0x00000000
#define 	DISPC_CONTROL_TDMPARALLEL_9		0x00200000
#define 	DISPC_CONTROL_TDMPARALLEL_12		0x00400000
#define 	DISPC_CONTROL_TDMPARALLEL_16		0x00600000
#define 	DISPC_CONTROL_TDMCYCLE_1		0x00000000
#define 	DISPC_CONTROL_TDMCYCLE_2		0x00800000
#define 	DISPC_CONTROL_TDMCYCLE_3		0x00000000
#define 	DISPC_CONTROL_TDMCYCLE_3_2		0x01800000
#define 	DISPC_CONTROL_TDMUNUSED_0		0x00000000
#define 	DISPC_CONTROL_TDMUNUSED_1		0x02000000
#define 	DISPC_CONTROL_TDMUNUSED_M		0x04000000
#define DISPC_CONFIG			0x0044
#define 	DISPC_CONFIG_PIXELGATED			0x00000001
#define 	DISPC_CONFIG_LOADMODE_PGE		0x00000000
#define 	DISPC_CONFIG_LOADMODE_PG		0x00000002
#define 	DISPC_CONFIG_LOADMODE_DATA		0x00000004
#define 	DISPC_CONFIG_LOADMODE_DATA_PG		0x00000006
#define 	DISPC_CONFIG_PALETTEGAMMA		0x00000008
#define 	DISPC_CONFIG_PIXELDATAGATED		0x00000010
#define 	DISPC_CONFIG_PIXELCLOCKGATED		0x00000020
#define 	DISPC_CONFIG_HSYNCGATED			0x00000040
#define 	DISPC_CONFIG_VSYNCGATED			0x00000080
#define 	DISPC_CONFIG_ACBIAGATED			0x00000100
#define 	DISPC_CONFIG_FUNCGATED			0x00000200
#define 	DISPC_CONFIG_TCKLCDEN			0x00000400
#define 	DISPC_CONFIG_TCKLCDSEL			0x00000800
#define 	DISPC_CONFIG_TCKDIGEN			0x00001000
#define 	DISPC_CONFIG_TCKDIGSEL			0x00002000
#define DISPC_CAPABLE			0x0048
#define DISPC_DEFAULT_COLOR0		0x004C
#define DISPC_DEFAULT_COLOR1		0x0050
#define DISPC_TRANS_COLOR0		0x0054
#define DISPC_TRANS_COLOR1		0x0058
#define DISPC_LINE_STATUS		0x005C
#define DISPC_LINE_NUMBER		0x0060
#define DISPC_TIMING_H			0x0064
#define		DISPC_TIMING_H_HSW_s(x)	((x) << 0)
#define		DISPC_TIMING_H_HFP_s(x)	((x) << 8)
#define		DISPC_TIMING_H_HBP_s(x)	((x) << 20)
#define DISPC_TIMING_V			0x0068
#define		DISPC_TIMING_V_VSW_s(x)	((x) << 0)
#define		DISPC_TIMING_V_VFP_s(x)	((x) << 8)
#define		DISPC_TIMING_V_VBP_s(x)	((x) << 20)
#define DISPC_POL_FREQ			0x006C
#define 	DISPC_POL_FREQ_ACB_s(x)	((x) << 0)
#define 	DISPC_POL_FREQ_ACBI_s(x)	((x) << 8)
#define 	DISPC_POL_FREQ_IVS	0x00001000
#define 	DISPC_POL_FREQ_IHS	0x00002000
#define 	DISPC_POL_FREQ_IPC	0x00004000
#define 	DISPC_POL_FREQ_IEO	0x00008000
#define 	DISPC_POL_FREQ_RF	0x00010000
#define 	DISPC_POL_FREQ_ONOFF	0x00020000
#define DISPC_DIVISOR			0x0070
#define		DISPC_DIVISOR_PCD_s(x)	((x) << 0)
#define		DISPC_DIVISOR_LCD_s(x)	((x) << 16)
#define DISPC_SIZE_DIG			0x0078
#define		DISPC_SIZE_DIG_PPL_s(x)	((x) << 0)
#define		DISPC_SIZE_DIG_LPP_s(x)	((x) << 16)
#define DISPC_SIZE_LCD			0x007C
#define		DISPC_SIZE_LCD_PPL_s(x)	((x) << 0)
#define		DISPC_SIZE_LCD_LPP_s(x)	((x) << 16)
#define DISPC_GFX_BA0			0x0080
#define DISPC_GFX_BA1			0x0084
#define DISPC_GFX_POSITION		0x0088
#define DISPC_GFX_SIZE			0x008C
#define		DISPC_GFX_SIZE_X_s(x)	((x) << 0)
#define		DISPC_GFX_SIZE_Y_s(x)	((x) << 16)
#define DISPC_GFX_ATTRIBUTES		0x00A0
#define		DISPC_GFX_ATTRIBUTES_GFXENABLE		0x001
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_1		0x000
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_2		0x002
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_4		0x004
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_8		0x006
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_12		0x008
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_16		0x00c
#define		DISPC_GFX_ATTRIBUTES_GFXFMT_24		0x010
#define		DISPC_GFX_ATTRIBUTES_GFXREPLICATE	0x020
#define		DISPC_GFX_ATTRIBUTES_BURST_4		0x000
#define		DISPC_GFX_ATTRIBUTES_BURST_8		0x040
#define		DISPC_GFX_ATTRIBUTES_BURST_16		0x080
#define		DISPC_GFX_ATTRIBUTES_GFXCHANNELOUT	0x100
#define		DISPC_GFX_ATTRIBUTES_NIBBLEMODE		0x200
#define		DISPC_GFX_ATTRIBUTES_ENDIAN		0x400
#define DISPC_GFX_FIFO_THRESHOLD	0x00A4
#define 	DISPC_GFX_FIFO_THRESHOLD_HIGH_SHIFT	16
#define 	DISPC_GFX_FIFO_THRESHOLD_LOW_SHIFT	0
#define DISPC_GFX_FIFO_SIZE_STATUS	0x00A8
#define DISPC_GFX_ROW_INC		0x00AC
#define DISPC_GFX_PIXEL_INC		0x00B0
#define DISPC_GFX_WINDOW_SKIP		0x00B4
#define DISPC_GFX_TABLE_BA		0x00B8
#define DISPC_VID1_BA0			0x00BC
#define DISPC_VID1_BA1			0x00C0
#define DISPC_VID1_POSITION		0x00C4
#define DISPC_VID1_SIZE			0x00C8
#define DISPC_VID1_ATTRIBUTES		0x00CC
#define DISPC_VID1_FIFO_THRESHOLD	0x00D0
#define DISPC_VID1_FIFO_SIZE_STATUS	0x00D4
#define DISPC_VID1_ROW_INC		0x00D8
#define DISPC_VID1_PIXEL_INC		0x00DC
#define DISPC_VID1_FIR			0x00E0
#define DISPC_VID1_PICTURE_SIZE		0x00E4
#define DISPC_VID1_ACCU0 		0x00E8
#define DISPC_VID1_ACCU1		0x00EC
#define DISPC_VID1_FIR_COEF_H0		0x00F0
#define DISPC_VID1_FIR_COEF_H1		0x00F8
#define DISPC_VID1_FIR_COEF_H2		0x0100
#define DISPC_VID1_FIR_COEF_H3		0x0108
#define DISPC_VID1_FIR_COEF_H4		0x0110
#define DISPC_VID1_FIR_COEF_H5		0x0118
#define DISPC_VID1_FIR_COEF_H6		0x0120
#define DISPC_VID1_FIR_COEF_H7		0x0128
#define DISPC_VID1_FIR_COEF_HV0		0x00F4
#define DISPC_VID1_FIR_COEF_HV1		0x00FC
#define DISPC_VID1_FIR_COEF_HV2		0x0104
#define DISPC_VID1_FIR_COEF_HV3		0x010C
#define DISPC_VID1_FIR_COEF_HV4		0x0114
#define DISPC_VID1_FIR_COEF_HV5		0x011C
#define DISPC_VID1_FIR_COEF_HV6		0x0124
#define DISPC_VID1_FIR_COEF_HV7		0x012C
#define DISPC_VID1_CONV_COEF0		0x0130
#define DISPC_VID1_CONV_COEF1		0x0134
#define DISPC_VID1_CONV_COEF2		0x0138
#define DISPC_VID1_CONV_COEF3		0x013C
#define DISPC_VID1_CONV_COEF4		0x0140
#define DISPC_VID2_BA0			0x014C
#define DISPC_VID2_BA1			0x0150
#define DISPC_VID2_POSITION		0x0154
#define DISPC_VID2_SIZE			0x0158
#define DISPC_VID2_ATTRIBUTES		0x015C
#define DISPC_VID2_FIFO_THRESHOLD	0x0160
#define DISPC_VID2_FIFO_SIZE_STATUS	0x0164
#define DISPC_VID2_ROW_INC		0x0168
#define DISPC_VID2_PIXEL_INC		0x016C
#define DISPC_VID2_FIR			0x0170
#define DISPC_VID2_PICTURE_SIZE		0x0174
#define DISPC_VID2_ACCU0		0x0178
#define DISPC_VID2_ACCU1		0x017C
#define DISPC_VID2_FIR_COEF_H0		0x0180
#define DISPC_VID2_FIR_COEF_H1		0x0188
#define DISPC_VID2_FIR_COEF_H2		0x0190
#define DISPC_VID2_FIR_COEF_H3		0x0198
#define DISPC_VID2_FIR_COEF_H4		0x01A0
#define DISPC_VID2_FIR_COEF_H5		0x01A8
#define DISPC_VID2_FIR_COEF_H6		0x01B0
#define DISPC_VID2_FIR_COEF_H7		0x01B8
#define DISPC_VID2_FIR_COEF_HV0		0x0184
#define DISPC_VID2_FIR_COEF_HV1		0x018C
#define DISPC_VID2_FIR_COEF_HV2		0x0194
#define DISPC_VID2_FIR_COEF_HV3		0x019C
#define DISPC_VID2_FIR_COEF_HV4		0x01A4
#define DISPC_VID2_FIR_COEF_HV5		0x01AC
#define DISPC_VID2_FIR_COEF_HV6		0x01B4
#define DISPC_VID2_FIR_COEF_HV7		0x01BC
#define DISPC_VID2_CONV_COEF0		0x01C0
#define DISPC_VID2_CONV_COEF1		0x01C4
#define DISPC_VID2_CONV_COEF2		0x01C8
#define DISPC_VID2_CONV_COEF3		0x01CC
#define DISPC_VID2_CONV_COEF4		0x01D0
#define DISPC_DATA_CYCLE1		0x01D4
#define DISPC_DATA_CYCLE2		0x01D8
#define DISPC_DATA_CYCLE3		0x01DC
#define DISPC_SIZE			0x0200

/* RFBI */
#define RFBI_REVISION		0x0000
#define RFBI_SYSCONFIG		0x0010
#define RFBI_SYSSTATUS		0x0014
#define RFBI_CONTROL		0x0040
#define RFBI_PIXEL_CNT		0x0044
#define RFBI_LINE_NUMBER	0x0048
#define RFBI_CMD		0x004C
#define RFBI_PARAM		0x0050
#define RFBI_DATA		0x0054
#define RFBI_READ		0x0058
#define RFBI_STATUS		0x005C
#define RFBI_CONFIG0		0x0060
#define RFBI_CONFIG1		0x0078
#define RFBI_ONOFF_TIME0	0x0064
#define RFBI_ONOFF_TIME1	0x007C
#define RFBI_CYCLE_TIME0	0x0068
#define RFBI_CYCLE_TIME1	0x0080
#define RFBI_DATA_CYCLE1_0	0x006C
#define RFBI_DATA_CYCLE1_1	0x0084
#define RFBI_DATA_CYCLE2_0	0x0070
#define RFBI_DATA_CYCLE2_1	0x0088
#define RFBI_DATA_CYCLE3_0	0x0074
#define RFBI_DATA_CYCLE3_1	0x008C
#define RFBI_VSYNC_WIDTH	0x0090
#define RFBI_HSYNC_WIDTH	0x0094

/* VENC1 */
#define REV_ID				0x0000
#define STATUS				0x0004
#define F_CONTROL			0x0008
#define VIDOUT_CTRL			0x0010
#define SYNC_CTRL			0x0014
#define LLEN				0x001C
#define FLENS				0x0020
#define HFLTR_CTRL			0x0024
#define CC_CARR_WSS_CARR		0x0028
#define C_PHASE				0x002C
#define GAIN_U				0x0030
#define GAIN_V				0x0034
#define GAIN_Y				0x0038
#define BLACK_LEVEL			0x003C
#define BLANK_LEVEL			0x0040
#define X_COLOR				0x0044
#define M_CONTROL			0x0048
#define BSTAMP_WSS_DATA			0x004C
#define S_CARR				0x0050
#define LINE21				0x0054
#define LN_SEL				0x0058
#define L21_WC_CTL			0x005C
#define HTRIGGER_VTRIGGER		0x0060
#define SAVID_EAVID			0x0064
#define FLEN_FAL			0x0068
#define LAL_PHASE_RESET			0x006C
#define HS_INT_START_STOP_X			0x0070
#define HS_EXT_START_STOP_X			0x0074
#define VS_INT_START_X				0x0078
#define VS_INT_STOP_X_VS_INT_START_Y		0x007C
#define VS_INT_STOP_Y_VS_EXT_START_X		0x0080
#define VS_EXT_STOP_X_VS_EXT_START_Y		0x0084
#define VS_EXT_STOP_Y				0x0088
#define AVID_START_STOP_X			0x0090
#define AVID_START_STOP_Y			0x0094
#define FID_INT_START_X_FID_INT_START_Y		0x00A0
#define FID_INT_OFFSET_Y_FID_EXT_START_X	0x00A4
#define FID_EXT_START_Y_FID_EXT_OFFSET_Y	0x00A8
#define TVDETGP_INT_START_STOP_X	0x00B0
#define TVDETGP_INT_START_STOP_Y	0x00B4
#define GEN_CTRL			0x00B8
#define DAC_TST_DAC_A			0x00C4
#define DAC_B_DAC_C			0x00C8


/* NO CONSOLE SUPPORT */


/* assumes 565 panel. */
struct omdisplay_panel_data {
	int width;
	int height;
	int horiz_sync_width;
	int horiz_front_porch;
	int horiz_back_porch;
	int vert_sync_width;
	int vert_front_porch;
	int vert_back_porch;
	int panel_flags;
	int sync;
	int depth;
#define PANEL_SYNC_H_ACTIVE_HIGH 1
#define PANEL_SYNC_V_ACTIVE_HIGH 2
	int linebytes;
};

#define PIXELDEPTH	16
#define PIXELWIDTH	2

struct omdisplay_panel_data	default_panel = {
	240,	/* Width */
	322,	/* Height */
	9, 9, 19, 	/* horiz sync, fp, bp */
	1, 2, 2,  	/* vert  sync, fp, bp */
	0, 	/* flags */
	0,	/* sync */
	PIXELDEPTH,
	240*PIXELWIDTH
};

struct omdisplay_screen {
	LIST_ENTRY(omdisplay_screen) link;

	/* Frame buffer */
	bus_dmamap_t dma;
	bus_dma_segment_t segs[1];
	int     nsegs;
	size_t  buf_size;
	size_t  map_size;
	void    *buf_va;
	int     depth;

	/* rasterop */
	struct rasops_info rinfo;
};

struct omdisplay_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_dsioh;
	bus_space_handle_t sc_dcioh;
	bus_space_handle_t sc_rfbioh;
	bus_space_handle_t sc_venioh;
	bus_dma_tag_t 		sc_dma_tag;

	void *sc_ih;

	int	sc_nscreens;
	LIST_HEAD(,omdisplay_screen) sc_screens;

	struct omdisplay_panel_data	*sc_geometry;
	struct omdisplay_screen		*sc_active;
};

int omdisplay_match(struct device *parent, void *v, void *aux);
void omdisplay_attach(struct device *parent, struct device *self, void *args);
int omdisplay_activate(struct device *, int);
int omdisplay_ioctl(void *v, u_long cmd, caddr_t data, int flag,
    struct proc *p);
void omdisplay_burner(void *v, u_int on, u_int flags);
int omdisplay_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg);
int omdisplay_param(struct omdisplay_softc *sc, ulong cmd,
    struct wsdisplay_param *dp);
int omdisplay_max_brightness(void);
int omdisplay_get_brightness(void);
void omdisplay_set_brightness(int newval);
void omdisplay_set_brightness_internal(int newval);
int omdisplay_get_backlight(void);
void omdisplay_set_backlight(int on);
void omdisplay_blank(int blank);
void omdisplay_suspend(struct omdisplay_softc *sc);
void omdisplay_resume(struct omdisplay_softc *sc);
void omdisplay_initialize(struct omdisplay_softc *sc,
    struct omdisplay_panel_data *geom);
void omdisplay_setup_rasops(struct omdisplay_softc *sc,
    struct rasops_info *rinfo);
int omdisplay_alloc_screen(void *v, const struct wsscreen_descr *_type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp);
int omdisplay_new_screen(struct omdisplay_softc *sc,
    struct omdisplay_screen *scr, int depth);
paddr_t omdisplay_mmap(void *v, off_t offset, int prot);
int omdisplay_load_font(void *, void *, struct wsdisplay_font *);
int omdisplay_list_font(void *, struct wsdisplay_font *);
void omdisplay_free_screen(void *v, void *cookie);
void omdisplay_start(struct omdisplay_softc *sc);
void omdisplay_stop(struct omdisplay_softc *sc);
int omdisplay_intr(void *v);

const struct cfattach	omdisplay_ca = {
	sizeof (struct omdisplay_softc), omdisplay_match, omdisplay_attach,
	NULL, omdisplay_activate
};

struct cfdriver omdisplay_cd = {
	NULL, "omdisplay", DV_DULL
};

struct wsdisplay_accessops omdisplay_accessops = {
	.ioctl = omdisplay_ioctl,
	.mmap = omdisplay_mmap,
	.alloc_screen = omdisplay_alloc_screen,
	.free_screen = omdisplay_free_screen,
	.show_screen = omdisplay_show_screen,
	.load_font = omdisplay_load_font,
	.list_font = omdisplay_list_font,
	.burn_screen = omdisplay_burner

};

struct omdisplay_wsscreen_descr {
	struct wsscreen_descr  c;       /* standard descriptor */
	int depth;                      /* bits per pixel */
	int flags;                      /* rasops flags */
};

struct omdisplay_wsscreen_descr omdisplay_screen = {
	{
		"std"
	},
	16,			/* bits per pixel */
	0 /* rotate */
};

const struct wsscreen_descr *omdisplay_scr_descr[] = {
	&omdisplay_screen.c
};

/* XXX - what about flip phones with CLI */
const struct wsscreen_list omdisplay_screen_list = {
        sizeof omdisplay_scr_descr / sizeof omdisplay_scr_descr[0],
	omdisplay_scr_descr
};


int
omdisplay_match(struct device *parent, void *v, void *aux)
{
	/* XXX */
	return (1);
}

void
omdisplay_attach(struct device *parent, struct device *self, void *args)
{
	struct ahb_attach_args *aa = args;
	struct omdisplay_softc *sc = (struct omdisplay_softc *) self;
	struct wsemuldisplaydev_attach_args wsaa;


	sc->sc_iot = aa->aa_iot;

	if (bus_space_map(sc->sc_iot, aa->aa_addr, OMDISPLAY_SIZE, 0,
	    &sc->sc_dsioh))
		panic("omdisplay_attach: bus_space_map failed!");

	if (bus_space_subregion(sc->sc_iot, sc->sc_dsioh, 0x400, 1024,
	    &sc->sc_dcioh))
		panic("omdisplay_attach: bus_space_submap failed!");

	if (bus_space_subregion(sc->sc_iot, sc->sc_dsioh, 0x800, 1024,
	    &sc->sc_rfbioh))
		panic("omdisplay_attach: bus_space_submap failed!");

	if (bus_space_subregion(sc->sc_iot, sc->sc_dsioh, 0xc00, 1024,
	    &sc->sc_venioh))
		panic("omdisplay_attach: bus_space_submap failed!");


	sc->sc_nscreens = 0;
	LIST_INIT(&sc->sc_screens);

	sc->sc_dma_tag = aa->aa_dmat;

	sc->sc_ih = arm_intr_establish(aa->aa_intr, IPL_BIO /* XXX */,
	    omdisplay_intr, sc, sc->sc_dev.dv_xname);

	printf ("\n");

	sc->sc_geometry = &default_panel;

	{
		/* XXX - dummy? */
		struct rasops_info dummy;

		omdisplay_initialize(sc, sc->sc_geometry);

		/*
		 * Initialize a dummy rasops_info to compute fontsize and
		 * the screen size in chars.
		 */
		bzero(&dummy, sizeof(dummy));
		omdisplay_setup_rasops(sc, &dummy);
	}

	wsaa.console = 0;
	wsaa.scrdata = &omdisplay_screen_list;
	wsaa.accessops = &omdisplay_accessops;
	wsaa.accesscookie = sc;
	wsaa.defaultscreens = 0;

	(void)config_found(self, &wsaa, wsemuldisplaydevprint);

	/* backlight? */
}


int
omdisplay_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct omdisplay_softc *sc = v;
	struct wsdisplay_fbinfo *wsdisp_info;
	struct omdisplay_screen *scr = sc->sc_active;
	int res = EINVAL;

	switch (cmd) {
	case WSDISPLAYIO_GETPARAM:
	case WSDISPLAYIO_SETPARAM:
		res = omdisplay_param(sc, cmd, (struct wsdisplay_param *)data);
		break;
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_PXALCD; /* XXX */
		break;

	case WSDISPLAYIO_GINFO:
		wsdisp_info = (struct wsdisplay_fbinfo *)data;

		wsdisp_info->height = sc->sc_geometry->height;
		wsdisp_info->width = sc->sc_geometry->width;
		wsdisp_info->depth = 16; /* XXX */
		if (scr != NULL)
			wsdisp_info->stride = scr->rinfo.r_stride;
		else
			wsdisp_info->stride = 0;
		wsdisp_info->offset = 0;
		wsdisp_info->cmsize = 0;
		break;
		
	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
		return EINVAL;  /* XXX Colormap */

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1;      /* not implemented */

	case WSDISPLAYIO_LINEBYTES:
		if (scr != NULL)
			*(u_int *)data = scr->rinfo.ri_stride;
		else
			*(u_int *)data = 0;
		break;

	}

	if (res == EINVAL)
		res = omdisplay_ioctl(v, cmd, data, flag, p);

	return res;
}

void
omdisplay_burner(void *v, u_int on, u_int flags)
{

        omdisplay_set_brightness(on ? omdisplay_get_brightness() : 0);

	/* GPIO controls for appsliver */
	if (on) {
		omgpio_set_bit(93);			/* 1 enable backlight */
		omgpio_set_dir(93, OMGPIO_DIR_OUT);
		omgpio_clear_bit(26);			/* 0 enable LCD */
		omgpio_set_dir(26, OMGPIO_DIR_OUT);
	} else {
		omgpio_clear_bit(93);			/* 0 disable backlt */
		omgpio_set_dir(93, OMGPIO_DIR_OUT);
		omgpio_set_bit(26);			/* 1 disable LCD */
		omgpio_set_dir(26, OMGPIO_DIR_OUT);
	}
}

int
omdisplay_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct omdisplay_softc *sc = v;
	struct rasops_info *ri = cookie;
	struct omdisplay_screen *scr = ri->ri_hw, *old;

	old = sc->sc_active;
	if (old == scr)
		return 0;

	if (old != NULL)
		; /* Stop old screen */

	sc->sc_active = scr;
	omdisplay_initialize(sc, sc->sc_geometry);
	
	/* Turn on LCD */
	omdisplay_burner(v, 1, 0);

	return (0);
}



/*
 * wsdisplay I/O controls
 */
int
omdisplay_param(struct omdisplay_softc *sc, ulong cmd,
    struct wsdisplay_param *dp)
{
	int res = EINVAL;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BACKLIGHT:
		if (cmd == WSDISPLAYIO_GETPARAM) {
			dp->min = 0;
			dp->max = 1;
			dp->curval = omdisplay_get_backlight();
			res = 0;
		} else if (cmd == WSDISPLAYIO_SETPARAM) {
/* XXX */
//			omdisplay_set_backlight(dp->curval);
			res = 0;
		}
		break;

	case WSDISPLAYIO_PARAM_CONTRAST:
		/* unsupported */
		res = ENOTTY;
		break;

	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (cmd == WSDISPLAYIO_GETPARAM) {
			dp->min = 1;
			dp->max = omdisplay_max_brightness();
			dp->curval = omdisplay_get_brightness();
			res = 0;
		} else if (cmd == WSDISPLAYIO_SETPARAM) {
/* XXX */
//			omdisplay_set_brightness(dp->curval);
			res = 0;
		}
		break;
	}

	return res;
}


/*
 * LCD backlight
 */

static  int lcdbrightnesscurval = 1;
static  int lcdislit = 1;
static  int lcdisblank = 0;

struct lcd_backlight {
	int	duty;		/* LZ9JG18 DAC value */
	int	cont;		/* BACKLIGHT_CONT signal */
	int	on;		/* BACKLIGHT_ON signal */
};

const struct lcd_backlight lcd_bl[] = {
	{ 0x00, 0, 0 },		/* 0:     Off */
	{ 0x00, 0, 1 },		/* 1:      0% */
	{ 0x01, 0, 1 },		/* 2:     20% */
	{ 0x07, 0, 1 },		/* 3:     40% */
	{ 0x01, 1, 1 },		/* 4:     60% */
	{ 0x07, 1, 1 },		/* 5:     80% */
	{ 0x11, 1, 1 },		/* 6:    100% */
	{ -1, -1, -1 }		/* 7: Invalid */
};
#define CURRENT_BACKLIGHT lcd_bl

int
omdisplay_max_brightness(void)
{
	int i;

	for (i = 0; CURRENT_BACKLIGHT[i].duty != -1; i++)
		;
	return i - 1;
}

int
omdisplay_get_brightness(void)
{

	return lcdbrightnesscurval;
}

void
omdisplay_set_brightness(int newval)
{
	int max;

	max = omdisplay_max_brightness();
	if (newval < 0)
		newval = 0;
	else if (newval > max)
		newval = max;

	if (omdisplay_get_backlight() && !lcdisblank)
		omdisplay_set_brightness_internal(newval);

	if (newval > 0)
		lcdbrightnesscurval = newval;
}

void
omdisplay_set_brightness_internal(int newval)
{
	static int curval = 1;
	int i;

	/*
	 * It appears that the C3000 backlight can draw too much power if we
	 * switch it from a low to a high brightness.  Increasing brightness
	 * in steps avoids this issue.
	 */
	if (newval > curval) {
		for (i = curval + 1; i <= newval; i++) {
/* atlas controls */
			/* CURRENT_BACKLIGHT[newval].duty); */
		}
	} else {
/* atlas controls */
		/* CURRENT_BACKLIGHT[newval].duty); */
	}

	curval = newval;
}

int
omdisplay_get_backlight(void)
{

	return lcdislit;
}

void
omdisplay_set_backlight(int on)
{

	if (!on) {
		omdisplay_set_brightness(0);
		lcdislit = 0;
	} else {
		lcdislit = 1;
		omdisplay_set_brightness(omdisplay_get_brightness());
	}
}

void
omdisplay_blank(int blank)
{
		
	if (blank) {
		omdisplay_set_brightness(0);
		lcdisblank = 1;
	} else {
		lcdisblank = 0;
		omdisplay_set_brightness(omdisplay_get_brightness());
	}
}

void
omdisplay_suspend(struct omdisplay_softc *sc)
{
	if (sc->sc_active != NULL) {
		omdisplay_stop(sc);
		/* XXX disable clocks */
	}
}

void
omdisplay_resume(struct omdisplay_softc *sc)
{
	if (sc->sc_active != NULL) {
		/* XXX - clocks? */
		omdisplay_initialize(sc, sc->sc_geometry);
		omdisplay_start(sc);
	}
}

void
omdisplay_activate(struct device *self, int act)
{
	struct omdisplay_softc *sc = (struct omdisplay_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		omdisplay_set_brightness(0);
		omdisplay_suspend(sc);
		break;
	case DVACT_RESUME:
		omdisplay_resume(sc);
		omdisplay_set_brightness(omdisplay_get_brightness());
		break;
	}
	return 0;
}

void
omdisplay_initialize(struct omdisplay_softc *sc,
    struct omdisplay_panel_data *geom)
{
	struct omdisplay_screen *scr;
	u_int32_t reg;
	u_int32_t mode;
#if 0
	int den, nom; /* pixel rate */
#endif


	reg = bus_space_read_4(sc->sc_iot, sc->sc_dcioh, DISPC_CONTROL);

	scr = sc->sc_active;

	if (reg & (DISPC_CONTROL_LCDENABLE|DISPC_CONTROL_DIGITALENABLE)) {
		omdisplay_stop(sc);
	}

	/* XXX - enable clocks */

	/* disable all interrupts */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_IRQENABLE, 0);

	/* GPIOs ? */

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_CONFIG,
	    DISPC_CONFIG_LOADMODE_PG|DISPC_CONFIG_LOADMODE_DATA);

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DEFAULT_COLOR0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DEFAULT_COLOR1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_TRANS_COLOR0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_TRANS_COLOR1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_LINE_NUMBER, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DATA_CYCLE1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DATA_CYCLE2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DATA_CYCLE3, 0);

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_SYSCONFIG,
	    DISPC_SYSCONFIG_SIDLEMODE_NONE|
	    DISPC_SYSCONFIG_MIDLEMODE_NONE);
		
#if 0
	if (geom->panel_flags & LCDPANEL_TDM) {
		nom = tdmflags >>8 & 0x3;
		den = tdmflags & 0x3;
	} else {
		nom = 1;
		den = 1;
	}
	hsync = geom->width*den/nom + geom->horiz_sync_width +
	    geom->horiz_front_porch + geom->horiz_back_porch;
#endif

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_TIMING_H,
	    DISPC_TIMING_H_HSW_s(geom->horiz_sync_width) |
	    DISPC_TIMING_H_HFP_s(geom->horiz_front_porch) |
	    DISPC_TIMING_H_HBP_s(geom->horiz_back_porch));
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_TIMING_V,
	    DISPC_TIMING_V_VSW_s(geom->vert_sync_width) |
	    DISPC_TIMING_V_VFP_s(geom->vert_front_porch) |
	    DISPC_TIMING_V_VBP_s(geom->vert_back_porch));

	reg = 0;
	if (geom->sync & PANEL_SYNC_H_ACTIVE_HIGH)
		reg |= DISPC_POL_FREQ_IHS;
	if (geom->sync & PANEL_SYNC_V_ACTIVE_HIGH)
		reg |= DISPC_POL_FREQ_IVS;
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_POL_FREQ, reg);

	
	/* clkdiv = pixclock/period; */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_SIZE_LCD,
	    DISPC_SIZE_LCD_PPL_s(geom->width-1) |
	    DISPC_SIZE_LCD_LPP_s(geom->height-1));
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_SIZE_DIG,
	    DISPC_SIZE_LCD_PPL_s(geom->width-1) |
	    DISPC_SIZE_LCD_LPP_s(geom->height-1));

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_SIZE,
		DISPC_GFX_SIZE_X_s(geom->width-1) |
		DISPC_GFX_SIZE_Y_s(geom->height-1));


	/* XXX!!! */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_DIVISOR,
		DISPC_DIVISOR_LCD_s(1) | DISPC_DIVISOR_PCD_s(6));

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_BA0,
	    scr->segs[0].ds_addr);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_BA1,
	    scr->segs[0].ds_addr);

	/* non-rotated */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_PIXEL_INC, 1);


	/* XXX 24bit -> 32 pixels */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_ROW_INC,
	    1 + scr->rinfo.ri_stride -
	    (scr->rinfo.ri_width * scr->rinfo.ri_depth / 8));

	switch (geom->depth) {
	case 1:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_1;
		break;
	case 2:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_2;
		break;
	case 4:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_4;
		break;
	case 8:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_8;
		break;
	case 12:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_12;
		break;
	case 16:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_16;
		break;
	case 24:
		mode = DISPC_GFX_ATTRIBUTES_GFXFMT_24;
		break;
	default:
		panic("invalid depth %d", geom->depth);
	}
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_ATTRIBUTES,
	    DISPC_GFX_ATTRIBUTES_GFXENABLE | mode |
	    DISPC_GFX_ATTRIBUTES_BURST_8);

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_POSITION, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_WINDOW_SKIP, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_FIFO_THRESHOLD,
	    (0xfc << DISPC_GFX_FIFO_THRESHOLD_HIGH_SHIFT) |
	    (0xc0 << DISPC_GFX_FIFO_THRESHOLD_LOW_SHIFT));
	
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_ROW_INC, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_PIXEL_INC, 1);

	/* DISPC_CONFIG_PALETTEGAMMA not enabled */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_GFX_TABLE_BA,
	    scr->segs[0].ds_addr);

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_BA0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_BA1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_SIZE, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_ATTRIBUTES, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIFO_THRESHOLD,
	    0xc00040); /* XXX */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIFO_SIZE_STATUS,
	    0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_ROW_INC, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_PIXEL_INC, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_PICTURE_SIZE, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_ACCU0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_ACCU1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H4, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H5, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H6, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_H7, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV4, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV5, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV6, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_FIR_COEF_HV7, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_CONV_COEF0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_CONV_COEF1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_CONV_COEF2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_CONV_COEF3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID1_CONV_COEF4, 0);

	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_BA0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_BA1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_SIZE, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_ATTRIBUTES, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIFO_THRESHOLD,
	    0xc00040); /* XXX */
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIFO_SIZE_STATUS,
	    0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_ROW_INC, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_PIXEL_INC, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_PICTURE_SIZE, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_ACCU0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_ACCU1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H4, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H5, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H6, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_H7, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV4, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV5, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV6, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_FIR_COEF_HV7, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_CONV_COEF0, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_CONV_COEF1, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_CONV_COEF2, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_CONV_COEF3, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_VID2_CONV_COEF4, 0);

	omdisplay_start(sc);
}

void
omdisplay_setup_rasops(struct omdisplay_softc *sc, struct rasops_info *rinfo)
{
	struct omdisplay_wsscreen_descr *descr;
	struct omdisplay_panel_data *geom;

	descr = &omdisplay_screen;
	geom = sc->sc_geometry;

	rinfo->ri_flg = descr->flags;
	rinfo->ri_depth = descr->depth;
	rinfo->ri_width = geom->width;
	rinfo->ri_height = geom->height;
	rinfo->ri_stride = geom->linebytes;

	/* pixel position */
	if (descr->depth == 16) {
		rinfo->ri_rnum = 5;
		rinfo->ri_rpos = 11;
		rinfo->ri_gnum = 6;
		rinfo->ri_gpos = 5;
		rinfo->ri_bnum = 5;
		rinfo->ri_bpos = 0;
	}

	if (descr->c.nrows == 0) {
		/* get rasops to compute screen size the first time */
		rasops_init(rinfo, 100, 100);
	} else {
		if (descr->flags != 0) /* rotate */
			rasops_init(rinfo, descr->c.ncols, descr->c.nrows);
		else
			rasops_init(rinfo, descr->c.nrows, descr->c.ncols);
	}

	descr->c.nrows = rinfo->ri_rows;
	descr->c.ncols = rinfo->ri_cols;
	descr->c.capabilities = rinfo->ri_caps;
	descr->c.textops = &rinfo->ri_ops;

}


int
omdisplay_alloc_screen(void *v, const struct wsscreen_descr *_type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct omdisplay_softc *sc = v;
	struct omdisplay_screen *scr;
	struct rasops_info *ri;
	struct omdisplay_wsscreen_descr *type =
	    (struct omdisplay_wsscreen_descr *)_type;
	int error;

	scr = malloc(sizeof *scr, M_DEVBUF, (cold ? M_NOWAIT : M_WAITOK));
	if (scr == NULL)
		return (ENOMEM);

	error = omdisplay_new_screen(sc, scr, type->depth);
	if (error != 0) {
		free(scr, M_DEVBUF, 0);
		return (error);
	}

	/*
	 * initialize raster operation for this screen.
	 */
	ri = &scr->rinfo;
	ri->ri_hw = (void *)scr;
	ri->ri_bits = scr->buf_va;
	omdisplay_setup_rasops(sc, ri);

	/* assumes 16 bpp */
	ri->ri_ops.pack_attr(ri, 0, 0, 0, attrp);

	*cookiep = ri;
	*curxp = 0;
	*curyp = 0;

	return 0;
}

/*
 * Create and initialize a new screen buffer.
 */
int
omdisplay_new_screen(struct omdisplay_softc *sc,
    struct omdisplay_screen *scr, int depth)
{
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_dma_tag_t dma_tag;
	struct omdisplay_panel_data *geometry;
	int width, height;
	bus_size_t size;
	int error, palette_size;
	int busdma_flag = (cold ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);

	if (sc != NULL) {
		iot = sc->sc_iot;
		ioh = sc->sc_dcioh;
		dma_tag = sc->sc_dma_tag;
		geometry = sc->sc_geometry;
	} else {
		/* We are creating the console screen. */
#if 0
		iot = omdisplay_console.iot;
		ioh = omdisplay_console.ioh;
		dma_tag = omdisplay_console.dma_tag;
		geometry = omdisplay_console.geometry;
#endif
	}

	width = geometry->width;
	height = geometry->height;
	palette_size = 0;

	switch (depth) {
	case 1:
	case 2:
	case 4:
	case 8:
		palette_size = (1 << depth) * sizeof (uint16_t);
		/* FALLTHROUGH */
	case 16:
	case 24:
		size = geometry->height * geometry->linebytes;
		break;
	default:
		printf("%s: Unknown depth (%d)\n",
		    sc != NULL ? sc->sc_dev.dv_xname : "console", depth);
		return (EINVAL);
	}

	bzero(scr, sizeof *scr);

	scr->nsegs = 0;
	scr->depth = depth;
	scr->buf_size = size;
	scr->buf_va = NULL;
	size = roundup(size, 16);
#if 0
	 + 3 * sizeof (struct lcd_dma_descriptor)
	    + palette_size;
#endif

	error = bus_dmamem_alloc(dma_tag, size, 0x100000, 0,
	    scr->segs, 1, &(scr->nsegs), busdma_flag);
	if (error != 0 || scr->nsegs != 1) {
		/* XXX: Actually we can handle nsegs > 1 case by means
		   of multiple DMA descriptors for a panel.  It would
		    make code here a bit hairy */
		if (error == 0)
			error = E2BIG;
		goto bad;
	}

	error = bus_dmamem_map(dma_tag, scr->segs, scr->nsegs,
	    size, (caddr_t *)&(scr->buf_va), busdma_flag | BUS_DMA_COHERENT);
	if (error != 0)
		goto bad;

	memset(scr->buf_va, 0, scr->buf_size);
	bcopy(splash, scr->buf_va,
	    sizeof (splash) > scr->buf_size ? scr->buf_size : sizeof (splash));

	/* map memory for DMA */
	if (bus_dmamap_create(dma_tag, 1024 * 1024 * 2, 1,
	    1024 * 1024 * 2, 0,  busdma_flag, &scr->dma))
		goto bad;
	error = bus_dmamap_load(dma_tag, scr->dma,
	    scr->buf_va, size, NULL, busdma_flag);
	if (error != 0) {
		goto bad;
	}

	scr->map_size = size;		/* used when unmap this. */

	if (sc != NULL) {
		LIST_INSERT_HEAD(&(sc->sc_screens), scr, link);
		sc->sc_nscreens++;
	}

	omdisplay_initialize(sc, geometry);
	
	return (0);

 bad:
	if (scr->buf_va)
		bus_dmamem_unmap(dma_tag, scr->buf_va, size);
	if (scr->nsegs)
		bus_dmamem_free(dma_tag, scr->segs, scr->nsegs);
	return (error);
}
paddr_t
omdisplay_mmap(void *v, off_t offset, int prot)
{
	struct omdisplay_softc *sc = v;
	struct omdisplay_screen *screen = sc->sc_active;  /* ??? */

	if ((offset & PAGE_MASK) != 0)
		return (-1);

	if (screen == NULL)
		return (-1);

	if (offset < 0 ||
	    offset >= screen->rinfo.ri_stride * screen->rinfo.ri_height)
		return (-1);

	return (bus_dmamem_mmap(sc->sc_dma_tag, screen->segs, screen->nsegs,
	    offset, prot, BUS_DMA_WAITOK | BUS_DMA_COHERENT));
}

void
omdisplay_free_screen(void *v, void *cookie)
{
	struct omdisplay_softc *sc = v;
	struct rasops_info *ri = cookie;
	struct omdisplay_screen *scr = ri->ri_hw;

	LIST_REMOVE(scr, link);
	sc->sc_nscreens--;
	if (scr == sc->sc_active) {
		/* at first, we need to stop LCD DMA */
		sc->sc_active = NULL;

#ifdef DEBUG
		printf("lcd_free on active screen\n");
#endif

		omdisplay_stop(sc);
	}

	if (scr->buf_va)
		bus_dmamem_unmap(sc->sc_dma_tag, scr->buf_va, scr->map_size);

	if (scr->nsegs > 0)
		bus_dmamem_free(sc->sc_dma_tag, scr->segs, scr->nsegs);

	free(scr, M_DEVBUF, 0);
}

int
omdisplay_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct omdisplay_softc *sc = v;
	struct omdisplay_screen *scr = sc->sc_active;

	if (scr == NULL)
		return ENXIO;

	return rasops_load_font(scr->rinfo, emulcookie, font);
}

int
omdisplay_list_font(void *v, struct wsdisplay_font *font)
{
	struct omdisplay_softc *sc = v;
	struct omdisplay_screen *scr = sc->sc_active;

	if (scr == NULL)
		return ENXIO;

	return rasops_list_font(scr->rinfo, font);
}

void
omdisplay_start(struct omdisplay_softc *sc)
{
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_CONTROL,
	    DISPC_CONTROL_GPOUT0 | DISPC_CONTROL_GPOUT1 |
	    DISPC_CONTROL_TFTDATALINES_18 /*XXX 18? */ |
	    DISPC_CONTROL_STNTFT |
	    DISPC_CONTROL_GOLCD |
	    DISPC_CONTROL_LCDENABLE);
}

void
omdisplay_stop(struct omdisplay_softc *sc)
{
	bus_space_write_4(sc->sc_iot, sc->sc_dcioh, DISPC_CONTROL,
	    bus_space_read_4(sc->sc_iot, sc->sc_dcioh, DISPC_CONTROL) &
	    ~(DISPC_CONTROL_DIGITALENABLE|DISPC_CONTROL_LCDENABLE));

	/* XXX - wait for end of frame? */
}

int
omdisplay_intr(void *v)
{
	/* XXX */
	return 1;
}

