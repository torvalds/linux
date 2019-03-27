/*
 * Copyright (c) 2008 AnyWi Technologies
 * Author: Andrea Guzzo <aguzzo@anywi.com>
 * * based on uark.c 1.1 2006/08/14 08:30:22 jsg *
 * * parts from ubsa.c 183348 2008-09-25 12:00:56Z phk *
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
 *
 * $FreeBSD$
 */

/*
 * NOTE:
 *
 * - The detour through the tty layer is ridiculously expensive wrt
 *   buffering due to the high speeds.
 *
 *   We should consider adding a simple r/w device which allows
 *   attaching of PPP in a more efficient way.
 *
 */


#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR u3g_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_msctest.h>

#include <dev/usb/serial/usb_serial.h>
#include <dev/usb/quirk/usb_quirk.h>

#ifdef USB_DEBUG
static int u3g_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, u3g, CTLFLAG_RW, 0, "USB 3g");
SYSCTL_INT(_hw_usb_u3g, OID_AUTO, debug, CTLFLAG_RWTUN,
    &u3g_debug, 0, "Debug level");
#endif

#define	U3G_MAXPORTS		12
#define	U3G_CONFIG_INDEX	0
#define	U3G_BSIZE		2048
#define	U3G_TXSIZE		(U3G_BSIZE / U3G_TXFRAMES)
#define	U3G_TXFRAMES		4

/* Eject methods; See also usb_quirks.h:UQ_MSC_EJECT_* */
#define	U3GINIT_HUAWEI		1	/* Requires Huawei init command */
#define	U3GINIT_SIERRA		2	/* Requires Sierra init command */
#define	U3GINIT_SCSIEJECT	3	/* Requires SCSI eject command */
#define	U3GINIT_REZERO		4	/* Requires SCSI rezero command */
#define	U3GINIT_ZTESTOR		5	/* Requires ZTE SCSI command */
#define	U3GINIT_CMOTECH		6	/* Requires CMOTECH SCSI command */
#define	U3GINIT_WAIT		7	/* Device reappears after a delay */
#define	U3GINIT_SAEL_M460	8	/* Requires vendor init */
#define	U3GINIT_HUAWEISCSI	9	/* Requires Huawei SCSI init command */
#define	U3GINIT_HUAWEISCSI2	10	/* Requires Huawei SCSI init command (2) */
#define	U3GINIT_TCT		11	/* Requires TCT Mobile init command */

enum {
	U3G_BULK_WR,
	U3G_BULK_RD,
	U3G_INTR,
	U3G_N_TRANSFER,
};

struct u3g_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom[U3G_MAXPORTS];

	struct usb_xfer *sc_xfer[U3G_MAXPORTS][U3G_N_TRANSFER];
	uint8_t sc_iface[U3G_MAXPORTS];			/* local status register */
	uint8_t sc_lsr[U3G_MAXPORTS];			/* local status register */
	uint8_t sc_msr[U3G_MAXPORTS];			/* u3g status register */
	uint16_t sc_line[U3G_MAXPORTS];			/* line status */

	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint8_t sc_numports;
};

static device_probe_t u3g_probe;
static device_attach_t u3g_attach;
static device_detach_t u3g_detach;
static void u3g_free_softc(struct u3g_softc *);

static usb_callback_t u3g_write_callback;
static usb_callback_t u3g_read_callback;
static usb_callback_t u3g_intr_callback;

static void u3g_cfg_get_status(struct ucom_softc *, uint8_t *, uint8_t *);
static void u3g_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void u3g_cfg_set_rts(struct ucom_softc *, uint8_t);
static void u3g_start_read(struct ucom_softc *ucom);
static void u3g_stop_read(struct ucom_softc *ucom);
static void u3g_start_write(struct ucom_softc *ucom);
static void u3g_stop_write(struct ucom_softc *ucom);
static void u3g_poll(struct ucom_softc *ucom);
static void u3g_free(struct ucom_softc *ucom);


static void u3g_test_autoinst(void *, struct usb_device *,
		struct usb_attach_arg *);
static int u3g_driver_loaded(struct module *mod, int what, void *arg);

static eventhandler_tag u3g_etag;

static const struct usb_config u3g_config[U3G_N_TRANSFER] = {

	[U3G_BULK_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = U3G_BSIZE,/* bytes */
		.frames = U3G_TXFRAMES,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &u3g_write_callback,
	},

	[U3G_BULK_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = U3G_BSIZE,/* bytes */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &u3g_read_callback,
	},

	[U3G_INTR] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &u3g_intr_callback,
	},
};

static const struct ucom_callback u3g_callback = {
	.ucom_cfg_get_status = &u3g_cfg_get_status,
	.ucom_cfg_set_dtr = &u3g_cfg_set_dtr,
	.ucom_cfg_set_rts = &u3g_cfg_set_rts,
	.ucom_start_read = &u3g_start_read,
	.ucom_stop_read = &u3g_stop_read,
	.ucom_start_write = &u3g_start_write,
	.ucom_stop_write = &u3g_stop_write,
	.ucom_poll = &u3g_poll,
	.ucom_free = &u3g_free,
};

static device_method_t u3g_methods[] = {
	DEVMETHOD(device_probe, u3g_probe),
	DEVMETHOD(device_attach, u3g_attach),
	DEVMETHOD(device_detach, u3g_detach),
	DEVMETHOD_END
};

static devclass_t u3g_devclass;

static driver_t u3g_driver = {
	.name = "u3g",
	.methods = u3g_methods,
	.size = sizeof(struct u3g_softc),
};

static const STRUCT_USB_HOST_ID u3g_devs[] = {
#define	U3G_DEV(v,p,i) { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }
	U3G_DEV(ABIT, AK_020, 0),
	U3G_DEV(ACERP, H10, 0),
	U3G_DEV(AIRPLUS, MCD650, 0),
	U3G_DEV(AIRPRIME, PC5220, 0),
	U3G_DEV(AIRPRIME, AC313U, 0),
	U3G_DEV(ALINK, 3G, 0),
	U3G_DEV(ALINK, 3GU, 0),
	U3G_DEV(ALINK, DWM652U5, 0),
	U3G_DEV(ALINK, SIM7600E, 0),
	U3G_DEV(AMOI, H01, 0),
	U3G_DEV(AMOI, H01A, 0),
	U3G_DEV(AMOI, H02, 0),
	U3G_DEV(ANYDATA, ADU_500A, 0),
	U3G_DEV(ANYDATA, ADU_620UW, 0),
	U3G_DEV(ANYDATA, ADU_E100X, 0),
	U3G_DEV(AXESSTEL, DATAMODEM, 0),
	U3G_DEV(CMOTECH, CDMA_MODEM1, 0),
	U3G_DEV(CMOTECH, CGU628, U3GINIT_CMOTECH),
	U3G_DEV(DELL, U5500, 0),
	U3G_DEV(DELL, U5505, 0),
	U3G_DEV(DELL, U5510, 0),
	U3G_DEV(DELL, U5520, 0),
	U3G_DEV(DELL, U5520_2, 0),
	U3G_DEV(DELL, U5520_3, 0),
	U3G_DEV(DELL, U5700, 0),
	U3G_DEV(DELL, U5700_2, 0),
	U3G_DEV(DELL, U5700_3, 0),
	U3G_DEV(DELL, U5700_4, 0),
	U3G_DEV(DELL, U5720, 0),
	U3G_DEV(DELL, U5720_2, 0),
	U3G_DEV(DELL, U5730, 0),
	U3G_DEV(DELL, U5730_2, 0),
	U3G_DEV(DELL, U5730_3, 0),
	U3G_DEV(DELL, U740, 0),
	U3G_DEV(DLINK, DWR510_CD, U3GINIT_SCSIEJECT),
	U3G_DEV(DLINK, DWR510, 0),
	U3G_DEV(DLINK, DWM157_CD, U3GINIT_SCSIEJECT),
	U3G_DEV(DLINK, DWM157, 0),
	U3G_DEV(DLINK, DWM222_CD, U3GINIT_SCSIEJECT),
	U3G_DEV(DLINK, DWM222, 0),
	U3G_DEV(DLINK3, DWM652, 0),
	U3G_DEV(HP, EV2200, 0),
	U3G_DEV(HP, HS2300, 0),
	U3G_DEV(HP, UN2420_QDL, 0),
	U3G_DEV(HP, UN2420, 0),
	U3G_DEV(HUAWEI, E1401, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1402, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1403, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1404, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1405, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1406, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1407, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1408, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1409, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E140A, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E140B, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E140D, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E140E, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E140F, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1410, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1411, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1412, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1413, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1414, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1415, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1416, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1417, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1418, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1419, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141A, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141B, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141C, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141D, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141E, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E141F, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1420, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1421, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1422, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1423, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1424, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1425, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1426, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1427, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1428, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1429, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142A, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142B, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142C, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142D, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142E, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E142F, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1430, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1431, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1432, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1433, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1434, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1435, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1436, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1437, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1438, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1439, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143A, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143B, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143C, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143D, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143E, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E143F, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E173, 0),
	U3G_DEV(HUAWEI, E173_INIT, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, E3131, 0),
	U3G_DEV(HUAWEI, E3131_INIT, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, E180V, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E220, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E220BIS, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E392, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, ME909U, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, ME909S, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, MOBILE, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E1752, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, E1820, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, K3771, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, K3771_INIT, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, K3772, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, K3772_INIT, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, K3765, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, K3765_INIT, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, K3770, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, K3770_INIT, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, K4505, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, K4505_INIT, U3GINIT_HUAWEISCSI),
	U3G_DEV(HUAWEI, ETS2055, U3GINIT_HUAWEI),
	U3G_DEV(HUAWEI, E3272_INIT, U3GINIT_HUAWEISCSI2),
	U3G_DEV(HUAWEI, E3272, 0),
	U3G_DEV(KYOCERA2, CDMA_MSM_K, 0),
	U3G_DEV(KYOCERA2, KPC680, 0),
	U3G_DEV(LONGCHEER, WM66, U3GINIT_HUAWEI),
	U3G_DEV(LONGCHEER, DISK, U3GINIT_TCT),
	U3G_DEV(LONGCHEER, W14, 0),
	U3G_DEV(LONGCHEER, XSSTICK, 0),
	U3G_DEV(MERLIN, V620, 0),
	U3G_DEV(NEOTEL, PRIME, 0),
	U3G_DEV(NOVATEL, E725, 0),
	U3G_DEV(NOVATEL, ES620, 0),
	U3G_DEV(NOVATEL, ES620_2, 0),
	U3G_DEV(NOVATEL, EU730, 0),
	U3G_DEV(NOVATEL, EU740, 0),
	U3G_DEV(NOVATEL, EU870D, 0),
	U3G_DEV(NOVATEL, MC760, 0),
	U3G_DEV(NOVATEL, MC547, 0),
	U3G_DEV(NOVATEL, MC679, 0),
	U3G_DEV(NOVATEL, MC950D, 0),
	U3G_DEV(NOVATEL, MC990D, 0),
	U3G_DEV(NOVATEL, MIFI2200, U3GINIT_SCSIEJECT),
	U3G_DEV(NOVATEL, MIFI2200V, U3GINIT_SCSIEJECT),
	U3G_DEV(NOVATEL, U720, 0),
	U3G_DEV(NOVATEL, U727, 0),
	U3G_DEV(NOVATEL, U727_2, 0),
	U3G_DEV(NOVATEL, U740, 0),
	U3G_DEV(NOVATEL, U740_2, 0),
	U3G_DEV(NOVATEL, U760, U3GINIT_SCSIEJECT),
	U3G_DEV(NOVATEL, U870, 0),
	U3G_DEV(NOVATEL, V620, 0),
	U3G_DEV(NOVATEL, V640, 0),
	U3G_DEV(NOVATEL, V720, 0),
	U3G_DEV(NOVATEL, V740, 0),
	U3G_DEV(NOVATEL, X950D, 0),
	U3G_DEV(NOVATEL, XU870, 0),
	U3G_DEV(MOTOROLA2, MB886, U3GINIT_SCSIEJECT),
	U3G_DEV(OPTION, E6500, 0),
	U3G_DEV(OPTION, E6501, 0),
	U3G_DEV(OPTION, E6601, 0),
	U3G_DEV(OPTION, E6721, 0),
	U3G_DEV(OPTION, E6741, 0),
	U3G_DEV(OPTION, E6761, 0),
	U3G_DEV(OPTION, E6800, 0),
	U3G_DEV(OPTION, E7021, 0),
	U3G_DEV(OPTION, E7041, 0),
	U3G_DEV(OPTION, E7061, 0),
	U3G_DEV(OPTION, E7100, 0),
	U3G_DEV(OPTION, GE40X, 0),
	U3G_DEV(OPTION, GT3G, 0),
	U3G_DEV(OPTION, GT3GPLUS, 0),
	U3G_DEV(OPTION, GT3GQUAD, 0),
	U3G_DEV(OPTION, GT3G_1, 0),
	U3G_DEV(OPTION, GT3G_2, 0),
	U3G_DEV(OPTION, GT3G_3, 0),
	U3G_DEV(OPTION, GT3G_4, 0),
	U3G_DEV(OPTION, GT3G_5, 0),
	U3G_DEV(OPTION, GT3G_6, 0),
	U3G_DEV(OPTION, GTHSDPA, 0),
	U3G_DEV(OPTION, GTM380, 0),
	U3G_DEV(OPTION, GTMAX36, 0),
	U3G_DEV(OPTION, GTMAX380HSUPAE, 0),
	U3G_DEV(OPTION, GTMAXHSUPA, 0),
	U3G_DEV(OPTION, GTMAXHSUPAE, 0),
	U3G_DEV(OPTION, VODAFONEMC3G, 0),
	U3G_DEV(QISDA, H20_1, 0),
	U3G_DEV(QISDA, H20_2, 0),
	U3G_DEV(QISDA, H21_1, 0),
	U3G_DEV(QISDA, H21_2, 0),
	U3G_DEV(QUALCOMM, NTT_L02C_MODEM, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMM2, AC8700, 0),
	U3G_DEV(QUALCOMM2, MF330, 0),
	U3G_DEV(QUALCOMM2, SIM5218, 0),
	U3G_DEV(QUALCOMM2, WM620, 0),
	U3G_DEV(QUALCOMM2, VW110L, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMM2, GOBI2000_QDL, 0),
	U3G_DEV(QUALCOMM2, GOBI2000, 0),
	U3G_DEV(QUALCOMM2, VT80N, 0),
	U3G_DEV(QUALCOMM3, VFAST2, 0),
	U3G_DEV(QUALCOMMINC, AC2726, 0),
	U3G_DEV(QUALCOMMINC, AC682_INIT, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMMINC, AC682, 0),
	U3G_DEV(QUALCOMMINC, AC8700, 0),
	U3G_DEV(QUALCOMMINC, AC8710, 0),
	U3G_DEV(QUALCOMMINC, CDMA_MSM, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMMINC, E0002, 0),
	U3G_DEV(QUALCOMMINC, E0003, 0),
	U3G_DEV(QUALCOMMINC, E0004, 0),
	U3G_DEV(QUALCOMMINC, E0005, 0),
	U3G_DEV(QUALCOMMINC, E0006, 0),
	U3G_DEV(QUALCOMMINC, E0007, 0),
	U3G_DEV(QUALCOMMINC, E0008, 0),
	U3G_DEV(QUALCOMMINC, E0009, 0),
	U3G_DEV(QUALCOMMINC, E000A, 0),
	U3G_DEV(QUALCOMMINC, E000B, 0),
	U3G_DEV(QUALCOMMINC, E000C, 0),
	U3G_DEV(QUALCOMMINC, E000D, 0),
	U3G_DEV(QUALCOMMINC, E000E, 0),
	U3G_DEV(QUALCOMMINC, E000F, 0),
	U3G_DEV(QUALCOMMINC, E0010, 0),
	U3G_DEV(QUALCOMMINC, E0011, 0),
	U3G_DEV(QUALCOMMINC, E0012, 0),
	U3G_DEV(QUALCOMMINC, E0013, 0),
	U3G_DEV(QUALCOMMINC, E0014, 0),
	U3G_DEV(QUALCOMMINC, E0017, 0),
	U3G_DEV(QUALCOMMINC, E0018, 0),
	U3G_DEV(QUALCOMMINC, E0019, 0),
	U3G_DEV(QUALCOMMINC, E0020, 0),
	U3G_DEV(QUALCOMMINC, E0021, 0),
	U3G_DEV(QUALCOMMINC, E0022, 0),
	U3G_DEV(QUALCOMMINC, E0023, 0),
	U3G_DEV(QUALCOMMINC, E0024, 0),
	U3G_DEV(QUALCOMMINC, E0025, 0),
	U3G_DEV(QUALCOMMINC, E0026, 0),
	U3G_DEV(QUALCOMMINC, E0027, 0),
	U3G_DEV(QUALCOMMINC, E0028, 0),
	U3G_DEV(QUALCOMMINC, E0029, 0),
	U3G_DEV(QUALCOMMINC, E0030, 0),
	U3G_DEV(QUALCOMMINC, E0032, 0),
	U3G_DEV(QUALCOMMINC, E0033, 0),
	U3G_DEV(QUALCOMMINC, E0037, 0),
	U3G_DEV(QUALCOMMINC, E0039, 0),
	U3G_DEV(QUALCOMMINC, E0042, 0),
	U3G_DEV(QUALCOMMINC, E0043, 0),
	U3G_DEV(QUALCOMMINC, E0048, 0),
	U3G_DEV(QUALCOMMINC, E0049, 0),
	U3G_DEV(QUALCOMMINC, E0051, 0),
	U3G_DEV(QUALCOMMINC, E0052, 0),
	U3G_DEV(QUALCOMMINC, E0054, 0),
	U3G_DEV(QUALCOMMINC, E0055, 0),
	U3G_DEV(QUALCOMMINC, E0057, 0),
	U3G_DEV(QUALCOMMINC, E0058, 0),
	U3G_DEV(QUALCOMMINC, E0059, 0),
	U3G_DEV(QUALCOMMINC, E0060, 0),
	U3G_DEV(QUALCOMMINC, E0061, 0),
	U3G_DEV(QUALCOMMINC, E0062, 0),
	U3G_DEV(QUALCOMMINC, E0063, 0),
	U3G_DEV(QUALCOMMINC, E0064, 0),
	U3G_DEV(QUALCOMMINC, E0066, 0),
	U3G_DEV(QUALCOMMINC, E0069, 0),
	U3G_DEV(QUALCOMMINC, E0070, 0),
	U3G_DEV(QUALCOMMINC, E0073, 0),
	U3G_DEV(QUALCOMMINC, E0076, 0),
	U3G_DEV(QUALCOMMINC, E0078, 0),
	U3G_DEV(QUALCOMMINC, E0082, 0),
	U3G_DEV(QUALCOMMINC, E0086, 0),
	U3G_DEV(QUALCOMMINC, SURFSTICK, 0),
	U3G_DEV(QUALCOMMINC, E2002, 0),
	U3G_DEV(QUALCOMMINC, E2003, 0),
	U3G_DEV(QUALCOMMINC, K3772_Z, 0),
	U3G_DEV(QUALCOMMINC, K3772_Z_INIT, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMMINC, MF112, U3GINIT_ZTESTOR),
	U3G_DEV(QUALCOMMINC, MF195E, 0),
	U3G_DEV(QUALCOMMINC, MF195E_INIT, U3GINIT_SCSIEJECT),
	U3G_DEV(QUALCOMMINC, MF626, 0),
	U3G_DEV(QUALCOMMINC, MF628, 0),
	U3G_DEV(QUALCOMMINC, MF633R, 0),
	/* the following is a RNDIS device, no modem features */
	U3G_DEV(QUALCOMMINC, ZTE_MF730M, U3GINIT_SCSIEJECT),
	U3G_DEV(QUANTA, GKE, 0),
	U3G_DEV(QUANTA, GLE, 0),
	U3G_DEV(QUANTA, GLX, 0),
	U3G_DEV(QUANTA, Q101, 0),
	U3G_DEV(QUANTA, Q111, 0),
	U3G_DEV(QUECTEL, EC25, 0),
	U3G_DEV(SIERRA, AC402, 0),
	U3G_DEV(SIERRA, AC595U, 0),
	U3G_DEV(SIERRA, AC313U, 0),
	U3G_DEV(SIERRA, AC597E, 0),
	U3G_DEV(SIERRA, AC875, 0),
	U3G_DEV(SIERRA, AC875E, 0),
	U3G_DEV(SIERRA, AC875U, 0),
	U3G_DEV(SIERRA, AC875U_2, 0),
	U3G_DEV(SIERRA, AC880, 0),
	U3G_DEV(SIERRA, AC880E, 0),
	U3G_DEV(SIERRA, AC880U, 0),
	U3G_DEV(SIERRA, AC881, 0),
	U3G_DEV(SIERRA, AC881E, 0),
	U3G_DEV(SIERRA, AC881U, 0),
	U3G_DEV(SIERRA, AC885E, 0),
	U3G_DEV(SIERRA, AC885E_2, 0),
	U3G_DEV(SIERRA, AC885U, 0),
	U3G_DEV(SIERRA, AIRCARD580, 0),
	U3G_DEV(SIERRA, AIRCARD595, 0),
	U3G_DEV(SIERRA, C22, 0),
	U3G_DEV(SIERRA, C597, 0),
	U3G_DEV(SIERRA, C888, 0),
	U3G_DEV(SIERRA, E0029, 0),
	U3G_DEV(SIERRA, E6892, 0),
	U3G_DEV(SIERRA, E6893, 0),
	U3G_DEV(SIERRA, EM5625, 0),
	U3G_DEV(SIERRA, EM5725, 0),
	U3G_DEV(SIERRA, MC5720, 0),
	U3G_DEV(SIERRA, MC5720_2, 0),
	U3G_DEV(SIERRA, MC5725, 0),
	U3G_DEV(SIERRA, MC5727, 0),
	U3G_DEV(SIERRA, MC5727_2, 0),
	U3G_DEV(SIERRA, MC5728, 0),
	U3G_DEV(SIERRA, MC7354, 0),
	U3G_DEV(SIERRA, MC7355, 0),
	U3G_DEV(SIERRA, MC7430, 0),
	U3G_DEV(SIERRA, MC8700, 0),
	U3G_DEV(SIERRA, MC8755, 0),
	U3G_DEV(SIERRA, MC8755_2, 0),
	U3G_DEV(SIERRA, MC8755_3, 0),
	U3G_DEV(SIERRA, MC8755_4, 0),
	U3G_DEV(SIERRA, MC8765, 0),
	U3G_DEV(SIERRA, MC8765_2, 0),
	U3G_DEV(SIERRA, MC8765_3, 0),
	U3G_DEV(SIERRA, MC8775, 0),
	U3G_DEV(SIERRA, MC8775_2, 0),
	U3G_DEV(SIERRA, MC8780, 0),
	U3G_DEV(SIERRA, MC8780_2, 0),
	U3G_DEV(SIERRA, MC8780_3, 0),
	U3G_DEV(SIERRA, MC8781, 0),
	U3G_DEV(SIERRA, MC8781_2, 0),
	U3G_DEV(SIERRA, MC8781_3, 0),
	U3G_DEV(SIERRA, MC8785, 0),
	U3G_DEV(SIERRA, MC8785_2, 0),
	U3G_DEV(SIERRA, MC8790, 0),
	U3G_DEV(SIERRA, MC8791, 0),
	U3G_DEV(SIERRA, MC8792, 0),
	U3G_DEV(SIERRA, MINI5725, 0),
	U3G_DEV(SIERRA, T11, 0),
	U3G_DEV(SIERRA, T598, 0),
	U3G_DEV(SILABS, SAEL, U3GINIT_SAEL_M460),
	U3G_DEV(STELERA, C105, 0),
	U3G_DEV(STELERA, E1003, 0),
	U3G_DEV(STELERA, E1004, 0),
	U3G_DEV(STELERA, E1005, 0),
	U3G_DEV(STELERA, E1006, 0),
	U3G_DEV(STELERA, E1007, 0),
	U3G_DEV(STELERA, E1008, 0),
	U3G_DEV(STELERA, E1009, 0),
	U3G_DEV(STELERA, E100A, 0),
	U3G_DEV(STELERA, E100B, 0),
	U3G_DEV(STELERA, E100C, 0),
	U3G_DEV(STELERA, E100D, 0),
	U3G_DEV(STELERA, E100E, 0),
	U3G_DEV(STELERA, E100F, 0),
	U3G_DEV(STELERA, E1010, 0),
	U3G_DEV(STELERA, E1011, 0),
	U3G_DEV(STELERA, E1012, 0),
	U3G_DEV(TCTMOBILE, X060S, 0),
	U3G_DEV(TCTMOBILE, X080S, U3GINIT_TCT),
	U3G_DEV(TELIT, UC864E, 0),
	U3G_DEV(TELIT, UC864G, 0),
	U3G_DEV(TLAYTECH, TEU800, 0),
	U3G_DEV(TOSHIBA, G450, 0),
	U3G_DEV(TOSHIBA, HSDPA, 0),
	U3G_DEV(YISO, C893, 0),
	U3G_DEV(WETELECOM, WM_D200, 0),
	/* Autoinstallers */
	U3G_DEV(NOVATEL, ZEROCD, U3GINIT_SCSIEJECT),
	U3G_DEV(OPTION, GTICON322, U3GINIT_REZERO),
	U3G_DEV(QUALCOMMINC, ZTE_STOR, U3GINIT_ZTESTOR),
	U3G_DEV(QUALCOMMINC, ZTE_STOR2, U3GINIT_SCSIEJECT),
	U3G_DEV(QUANTA, Q101_STOR, U3GINIT_SCSIEJECT),
	U3G_DEV(SIERRA, TRUINSTALL, U3GINIT_SIERRA),
#undef	U3G_DEV
};

DRIVER_MODULE(u3g, uhub, u3g_driver, u3g_devclass, u3g_driver_loaded, 0);
MODULE_DEPEND(u3g, ucom, 1, 1, 1);
MODULE_DEPEND(u3g, usb, 1, 1, 1);
MODULE_VERSION(u3g, 1);
USB_PNP_HOST_INFO(u3g_devs);

static int
u3g_sierra_init(struct usb_device *udev)
{
	struct usb_device_request req;

	req.bmRequestType = UT_VENDOR;
	req.bRequest = UR_SET_INTERFACE;
	USETW(req.wValue, UF_DEVICE_REMOTE_WAKEUP);
	USETW(req.wIndex, UHF_PORT_CONNECTION);
	USETW(req.wLength, 0);

	if (usbd_do_request_flags(udev, NULL, &req,
	    NULL, 0, NULL, USB_MS_HZ)) {
		/* ignore any errors */
	}
	return (0);
}

static int
u3g_huawei_init(struct usb_device *udev)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UF_DEVICE_REMOTE_WAKEUP);
	USETW(req.wIndex, UHF_PORT_SUSPEND);
	USETW(req.wLength, 0);

	if (usbd_do_request_flags(udev, NULL, &req,
	    NULL, 0, NULL, USB_MS_HZ)) {
		/* ignore any errors */
	}
	return (0);
}

static int
u3g_huawei_is_cdce(uint16_t idVendor, uint8_t bInterfaceSubClass,
    uint8_t bInterfaceProtocol)
{
	/*
	 * This function returns non-zero if the interface being
	 * probed is of type CDC ethernet, which the U3G driver should
	 * not attach to. See sys/dev/usb/net/if_cdce.c for matching
	 * entries.
	 */
	if (idVendor != USB_VENDOR_HUAWEI)
		goto done;

	switch (bInterfaceSubClass) {
	case 0x02:
		switch (bInterfaceProtocol) {
		case 0x16:
		case 0x46:
		case 0x76:
			return (1);
		default:
			break;
		}
		break;
	case 0x03:
		switch (bInterfaceProtocol) {
		case 0x16:
			return (1);
		default:
			break;
		}
		break;
	default:
		break;
	}
done:
	return (0);
}

static void
u3g_sael_m460_init(struct usb_device *udev)
{
	static const uint8_t setup[][24] = {
	     { 0x41, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x13, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	       0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
	       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0xc1, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40, 0x02 },
	     { 0xc1, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
	     { 0x41, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 },
	     { 0xc1, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	     { 0x41, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x19, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
	       0x00, 0x00, 0x00, 0x00, 0x11, 0x13 },
	     { 0x41, 0x13, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	       0x09, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
	       0x0a, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00 },
	     { 0x41, 0x12, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00 },
	     { 0x41, 0x19, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
	       0x00, 0x00, 0x00, 0x00, 0x11, 0x13 },
	     { 0x41, 0x13, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	       0x09, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
	       0x0a, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00 },
	     { 0x41, 0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 },
	};

	struct usb_device_request req;
	usb_error_t err;
	uint16_t len;
	uint8_t buf[0x300];
	uint8_t n;

	DPRINTFN(1, "\n");

	if (usbd_req_set_alt_interface_no(udev, NULL, 0, 0)) {
		DPRINTFN(0, "Alt setting 0 failed\n");
		return;
	}

	for (n = 0; n != nitems(setup); n++) {

		memcpy(&req, setup[n], sizeof(req));

		len = UGETW(req.wLength);
		if (req.bmRequestType & UE_DIR_IN) {
			if (len > sizeof(buf)) {
				DPRINTFN(0, "too small buffer\n");
				continue;
			}
			err = usbd_do_request(udev, NULL, &req, buf);
		} else {
			if (len > (sizeof(setup[0]) - 8)) {
				DPRINTFN(0, "too small buffer\n");
				continue;
			}
			err = usbd_do_request(udev, NULL, &req,
			    __DECONST(uint8_t *, &setup[n][8]));
		}
		if (err) {
			DPRINTFN(1, "request %u failed\n",
			    (unsigned int)n);
			/*
			 * Some of the requests will fail. Stop doing
			 * requests when we are getting timeouts so
			 * that we don't block the explore/attach
			 * thread forever.
			 */
			if (err == USB_ERR_TIMEOUT)
				break;
		}
	}
}

/*
 * The following function handles 3G modem devices (E220, Mobile,
 * etc.) with auto-install flash disks for Windows/MacOSX on the first
 * interface.  After some command or some delay they change appearance
 * to a modem.
 */
static void
u3g_test_autoinst(void *arg, struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;
	int error;
	unsigned long method;

	if (uaa->dev_state != UAA_DEV_READY)
		return;

	iface = usbd_get_iface(udev, 0);
	if (iface == NULL)
		return;
	id = iface->idesc;
	if (id == NULL || id->bInterfaceClass != UICLASS_MASS)
		return;

	if (usb_test_quirk(uaa, UQ_MSC_EJECT_HUAWEI))
		method = U3GINIT_HUAWEI;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_SIERRA))
		method = U3GINIT_SIERRA;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_SCSIEJECT))
		method = U3GINIT_SCSIEJECT;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_REZERO))
		method = U3GINIT_REZERO;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_ZTESTOR))
		method = U3GINIT_ZTESTOR;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_CMOTECH))
		method = U3GINIT_CMOTECH;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_WAIT))
		method = U3GINIT_WAIT;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_HUAWEISCSI))
		method = U3GINIT_HUAWEISCSI;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_HUAWEISCSI2))
		method = U3GINIT_HUAWEISCSI2;
	else if (usb_test_quirk(uaa, UQ_MSC_EJECT_TCT))
		method = U3GINIT_TCT;
	else if (usbd_lookup_id_by_uaa(u3g_devs, sizeof(u3g_devs), uaa) == 0)
		method = USB_GET_DRIVER_INFO(uaa);
	else
		return;		/* no device match */

	if (bootverbose) {
		printf("Ejecting %s %s using method %ld\n",
		       usb_get_manufacturer(udev),
		       usb_get_product(udev), method);
	}

	switch (method) {
		case U3GINIT_HUAWEI:
			error = u3g_huawei_init(udev);
			break;
		case U3GINIT_HUAWEISCSI:
			error = usb_msc_eject(udev, 0, MSC_EJECT_HUAWEI);
			break;
		case U3GINIT_HUAWEISCSI2:
			error = usb_msc_eject(udev, 0, MSC_EJECT_HUAWEI2);
			break;
		case U3GINIT_SCSIEJECT:
			error = usb_msc_eject(udev, 0, MSC_EJECT_STOPUNIT);
			break;
		case U3GINIT_REZERO:
			error = usb_msc_eject(udev, 0, MSC_EJECT_REZERO);
			break;
		case U3GINIT_ZTESTOR:
			error = usb_msc_eject(udev, 0, MSC_EJECT_STOPUNIT);
			if (error == 0)
			    error = usb_msc_eject(udev, 0, MSC_EJECT_ZTESTOR);
			break;
		case U3GINIT_CMOTECH:
			error = usb_msc_eject(udev, 0, MSC_EJECT_CMOTECH);
			break;
		case U3GINIT_TCT:
			error = usb_msc_eject(udev, 0, MSC_EJECT_TCT);
			break;
		case U3GINIT_SIERRA:
			error = u3g_sierra_init(udev);
			break;
		case U3GINIT_WAIT:
			/* Just pretend we ejected, the card will timeout */
			error = 0;
			break;
		default:
			/* no 3G eject quirks */
			error = EOPNOTSUPP;
			break;
	}
	if (error == 0) {
		/* success, mark the udev as disappearing */
		uaa->dev_state = UAA_DEV_EJECTING;
	}
}

static int
u3g_driver_loaded(struct module *mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* register our autoinstall handler */
		u3g_etag = EVENTHANDLER_REGISTER(usb_dev_configured,
		    u3g_test_autoinst, NULL, EVENTHANDLER_PRI_ANY);
		break;
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(usb_dev_configured, u3g_etag);
		break;
	default:
		return (EOPNOTSUPP);
	}
 	return (0);
}

static int
u3g_probe(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != U3G_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bInterfaceClass != UICLASS_VENDOR) {
		return (ENXIO);
	}
	if (u3g_huawei_is_cdce(uaa->info.idVendor, uaa->info.bInterfaceSubClass,
	    uaa->info.bInterfaceProtocol)) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(u3g_devs, sizeof(u3g_devs), uaa));
}

static int
u3g_attach(device_t dev)
{
	struct usb_config u3g_config_tmp[U3G_N_TRANSFER];
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct u3g_softc *sc = device_get_softc(dev);
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;
	uint32_t iface_valid;
	int error, type, nports;
	int ep, n;
	uint8_t i;

	DPRINTF("sc=%p\n", sc);

	type = USB_GET_DRIVER_INFO(uaa);
	if (type == U3GINIT_SAEL_M460
	    || usb_test_quirk(uaa, UQ_MSC_EJECT_SAEL_M460)) {
		u3g_sael_m460_init(uaa->device);
	}

	/* copy in USB config */
	for (n = 0; n != U3G_N_TRANSFER; n++)
		u3g_config_tmp[n] = u3g_config[n];

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "u3g", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_udev = uaa->device;

	/* Claim all interfaces on the device */
	iface_valid = 0;
	for (i = uaa->info.bIfaceIndex; i < USB_IFACE_MAX; i++) {
		iface = usbd_get_iface(uaa->device, i);
		if (iface == NULL)
			break;
		id = usbd_get_interface_descriptor(iface);
		if (id == NULL || id->bInterfaceClass != UICLASS_VENDOR)
			continue;
		if (u3g_huawei_is_cdce(uaa->info.idVendor,
		    id->bInterfaceSubClass, id->bInterfaceProtocol))
			continue;
		usbd_set_parent_iface(uaa->device, i, uaa->info.bIfaceIndex);
		iface_valid |= (1<<i);
	}

	i = 0;		/* interface index */
	ep = 0;		/* endpoint index */
	nports = 0;	/* number of ports */
	while (i < USB_IFACE_MAX) {
		if ((iface_valid & (1<<i)) == 0) {
			i++;
			continue;
		}

		/* update BULK endpoint index */
		for (n = 0; n < U3G_N_TRANSFER; n++)
			u3g_config_tmp[n].ep_index = ep;

		/* try to allocate a set of BULK endpoints */
		error = usbd_transfer_setup(uaa->device, &i,
		    sc->sc_xfer[nports], u3g_config_tmp, U3G_N_TRANSFER,
		    &sc->sc_ucom[nports], &sc->sc_mtx);
		if (error) {
			/* next interface */
			i++;
			ep = 0;
			continue;
		}

		iface = usbd_get_iface(uaa->device, i);
		id = usbd_get_interface_descriptor(iface);
		sc->sc_iface[nports] = id->bInterfaceNumber;

		if (bootverbose && sc->sc_xfer[nports][U3G_INTR]) {
			device_printf(dev, "port %d supports modem control\n",
				      nports);
		}

		/* set stall by default */
		mtx_lock(&sc->sc_mtx);
		usbd_xfer_set_stall(sc->sc_xfer[nports][U3G_BULK_WR]);
		usbd_xfer_set_stall(sc->sc_xfer[nports][U3G_BULK_RD]);
		mtx_unlock(&sc->sc_mtx);

		nports++;	/* found one port */
		ep++;
		if (nports == U3G_MAXPORTS)
			break;
	}
	if (nports == 0) {
		device_printf(dev, "no ports found\n");
		goto detach;
	}
	sc->sc_numports = nports;

	error = ucom_attach(&sc->sc_super_ucom, sc->sc_ucom,
	    sc->sc_numports, sc, &u3g_callback, &sc->sc_mtx);
	if (error) {
		DPRINTF("ucom_attach failed\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);
	device_printf(dev, "Found %u port%s.\n", sc->sc_numports,
	    sc->sc_numports > 1 ? "s":"");

	return (0);

detach:
	u3g_detach(dev);
	return (ENXIO);
}

static int
u3g_detach(device_t dev)
{
	struct u3g_softc *sc = device_get_softc(dev);
	uint8_t subunit;

	DPRINTF("sc=%p\n", sc);

	/* NOTE: It is not dangerous to detach more ports than attached! */
	ucom_detach(&sc->sc_super_ucom, sc->sc_ucom);

	for (subunit = 0; subunit != U3G_MAXPORTS; subunit++)
		usbd_transfer_unsetup(sc->sc_xfer[subunit], U3G_N_TRANSFER);

	device_claim_softc(dev);

	u3g_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(u3g);

static void
u3g_free_softc(struct u3g_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
u3g_free(struct ucom_softc *ucom)
{
	u3g_free_softc(ucom->sc_parent);
}

static void
u3g_start_read(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;

	/* start interrupt endpoint (if configured) */
	usbd_transfer_start(sc->sc_xfer[ucom->sc_subunit][U3G_INTR]);

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[ucom->sc_subunit][U3G_BULK_RD]);
}

static void
u3g_stop_read(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;

	/* stop interrupt endpoint (if configured) */
	usbd_transfer_stop(sc->sc_xfer[ucom->sc_subunit][U3G_INTR]);

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[ucom->sc_subunit][U3G_BULK_RD]);
}

static void
u3g_start_write(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[ucom->sc_subunit][U3G_BULK_WR]);
}

static void
u3g_stop_write(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[ucom->sc_subunit][U3G_BULK_WR]);
}

static void
u3g_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucom_softc *ucom = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;
	uint32_t frame;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
tr_setup:
		for (frame = 0; frame != U3G_TXFRAMES; frame++) {
			usbd_xfer_set_frame_offset(xfer, frame * U3G_TXSIZE, frame);

			pc = usbd_xfer_get_frame(xfer, frame);
			if (ucom_get_data(ucom, pc, 0, U3G_TXSIZE, &actlen) == 0)
				break;
			usbd_xfer_set_frame_len(xfer, frame, actlen);
		}
		if (frame != 0) {
			usbd_xfer_set_frames(xfer, frame);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* do a builtin clear-stall */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
u3g_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucom_softc *ucom = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(ucom, pc, 0, actlen);

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* do a builtin clear-stall */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
u3g_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct u3g_softc *sc = ucom->sc_parent;

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr[ucom->sc_subunit];
	*msr = sc->sc_msr[ucom->sc_subunit];
}

static void
u3g_cfg_set_line(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, sc->sc_line[ucom->sc_subunit]);
	req.wIndex[0] = sc->sc_iface[ucom->sc_subunit];
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, ucom,
	    &req, NULL, 0, 1000);
}

static void
u3g_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct u3g_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		sc->sc_line[ucom->sc_subunit] |= UCDC_LINE_DTR;
	else
		sc->sc_line[ucom->sc_subunit] &= ~UCDC_LINE_DTR;

	u3g_cfg_set_line(ucom);
}

static void
u3g_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct u3g_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		sc->sc_line[ucom->sc_subunit] |= UCDC_LINE_RTS;
	else
		sc->sc_line[ucom->sc_subunit] &= ~UCDC_LINE_RTS;

	u3g_cfg_set_line(ucom);
}

static void
u3g_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucom_softc *ucom = usbd_xfer_softc(xfer);
	struct u3g_softc *sc = ucom->sc_parent;
	struct usb_page_cache *pc;
	struct usb_cdc_notification pkt;
	int actlen;
	uint16_t wLen;
	uint8_t mstatus;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen < 8) {	/* usb_cdc_notification with 2 data bytes */
			DPRINTF("message too short (expected 8, received %d)\n", actlen);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &pkt, actlen);

		wLen = UGETW(pkt.wLength);
		if (wLen < 2) {
			DPRINTF("message too short (expected 2 data bytes, received %d)\n", wLen);
			goto tr_setup;
		}

		if (pkt.bmRequestType == UCDC_NOTIFICATION
		    && pkt.bNotification == UCDC_N_SERIAL_STATE) {
			/*
		         * Set the serial state in ucom driver based on
		         * the bits from the notify message
		         */
			DPRINTF("notify bytes = 0x%02x, 0x%02x\n",
			    pkt.data[0], pkt.data[1]);

			/* currently, lsr is always zero. */
			sc->sc_lsr[ucom->sc_subunit] = 0;
			sc->sc_msr[ucom->sc_subunit] = 0;

			mstatus = pkt.data[0];

			if (mstatus & UCDC_N_SERIAL_RI)
				sc->sc_msr[ucom->sc_subunit] |= SER_RI;
			if (mstatus & UCDC_N_SERIAL_DSR)
				sc->sc_msr[ucom->sc_subunit] |= SER_DSR;
			if (mstatus & UCDC_N_SERIAL_DCD)
				sc->sc_msr[ucom->sc_subunit] |= SER_DCD;
			ucom_status_change(ucom);
		}

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
u3g_poll(struct ucom_softc *ucom)
{
	struct u3g_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer[ucom->sc_subunit], U3G_N_TRANSFER);
}
