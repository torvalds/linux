/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015-2017 Google, Inc
 */

#ifndef __LINUX_USB_PD_VDO_H
#define __LINUX_USB_PD_VDO_H

#include "pd.h"
#include <linux/bitfield.h>

/*
 * VDO : Vendor Defined Message Object
 * VDM object is minimum of VDM header + 6 additional data objects.
 */

#define VDO_MAX_OBJECTS		6
#define VDO_MAX_SIZE		(VDO_MAX_OBJECTS + 1)

/*
 * VDM header
 * ----------
 * <31:16>  :: SVID
 * <15>     :: VDM type ( 1b == structured, 0b == unstructured )
 * <14:13>  :: Structured VDM version
 * <12:11>  :: reserved
 * <10:8>   :: object position (1-7 valid ... used for enter/exit mode only)
 * <7:6>    :: command type (SVDM only?)
 * <5>      :: reserved (SVDM), command type (UVDM)
 * <4:0>    :: command
 */
#define VDO(vid, type, ver, custom)			\
	(((vid) << 16) |				\
	 ((type) << 15) |				\
	 ((ver) << 13) |				\
	 ((custom) & 0x7FFF))

#define VDO_SVDM_TYPE		(1 << 15)
#define VDO_SVDM_VERS(x)	((x) << 13)
#define VDO_OPOS(x)		((x) << 8)
#define VDO_CMDT(x)		((x) << 6)
#define VDO_SVDM_VERS_MASK	VDO_SVDM_VERS(0x3)
#define VDO_OPOS_MASK		VDO_OPOS(0x7)
#define VDO_CMDT_MASK		VDO_CMDT(0x3)

#define CMDT_INIT		0
#define CMDT_RSP_ACK		1
#define CMDT_RSP_NAK		2
#define CMDT_RSP_BUSY		3

/* reserved for SVDM ... for Google UVDM */
#define VDO_SRC_INITIATOR	(0 << 5)
#define VDO_SRC_RESPONDER	(1 << 5)

#define CMD_DISCOVER_IDENT	1
#define CMD_DISCOVER_SVID	2
#define CMD_DISCOVER_MODES	3
#define CMD_ENTER_MODE		4
#define CMD_EXIT_MODE		5
#define CMD_ATTENTION		6

#define VDO_CMD_VENDOR(x)    (((0x10 + (x)) & 0x1f))

/* ChromeOS specific commands */
#define VDO_CMD_VERSION		VDO_CMD_VENDOR(0)
#define VDO_CMD_SEND_INFO	VDO_CMD_VENDOR(1)
#define VDO_CMD_READ_INFO	VDO_CMD_VENDOR(2)
#define VDO_CMD_REBOOT		VDO_CMD_VENDOR(5)
#define VDO_CMD_FLASH_ERASE	VDO_CMD_VENDOR(6)
#define VDO_CMD_FLASH_WRITE	VDO_CMD_VENDOR(7)
#define VDO_CMD_ERASE_SIG	VDO_CMD_VENDOR(8)
#define VDO_CMD_PING_ENABLE	VDO_CMD_VENDOR(10)
#define VDO_CMD_CURRENT		VDO_CMD_VENDOR(11)
#define VDO_CMD_FLIP		VDO_CMD_VENDOR(12)
#define VDO_CMD_GET_LOG		VDO_CMD_VENDOR(13)
#define VDO_CMD_CCD_EN		VDO_CMD_VENDOR(14)

#define PD_VDO_VID(vdo)		((vdo) >> 16)
#define PD_VDO_SVDM(vdo)	(((vdo) >> 15) & 1)
#define PD_VDO_SVDM_VER(vdo)	(((vdo) >> 13) & 0x3)
#define PD_VDO_OPOS(vdo)	(((vdo) >> 8) & 0x7)
#define PD_VDO_CMD(vdo)		((vdo) & 0x1f)
#define PD_VDO_CMDT(vdo)	(((vdo) >> 6) & 0x3)

/*
 * SVDM Identity request -> response
 *
 * Request is simply properly formatted SVDM header
 *
 * Response is 4 data objects for Power Delivery 2.0 and Passive Cables for
 * Power Delivery 3.0. Active Cables in Power Delivery 3.0 have 5 data objects.
 * [0] :: SVDM header
 * [1] :: Identitiy header
 * [2] :: Cert Stat VDO
 * [3] :: (Product | Cable) VDO
 * [4] :: Cable VDO 1
 * [4] :: AMA VDO
 * [5] :: Cable VDO 2
 *
 */
#define VDO_INDEX_HDR		0
#define VDO_INDEX_IDH		1
#define VDO_INDEX_CSTAT		2
#define VDO_INDEX_CABLE		3
#define VDO_INDEX_PRODUCT	3
#define VDO_INDEX_AMA		4
#define VDO_INDEX_CABLE_1	4
#define VDO_INDEX_CABLE_2	5

/*
 * SVDM Identity Header
 * --------------------
 * <31>     :: data capable as a USB host
 * <30>     :: data capable as a USB device
 * <29:27>  :: product type (UFP / Cable / VPD)
 * <26>     :: modal operation supported (1b == yes)
 * <25:23>  :: product type (DFP) (SVDM version 2.0+ only; set to zero in version 1.0)
 * <22:21>  :: connector type (SVDM version 2.0+ only; set to zero in version 1.0)
 * <20:16>  :: Reserved, Shall be set to zero
 * <15:0>   :: USB-IF assigned VID for this cable vendor
 */

/* PD Rev2.0 definition */
#define IDH_PTYPE_UNDEF		0

/* SOP Product Type (UFP) */
#define IDH_PTYPE_NOT_UFP	0
#define IDH_PTYPE_HUB		1
#define IDH_PTYPE_PERIPH	2
#define IDH_PTYPE_PSD		3
#define IDH_PTYPE_AMA		5

/* SOP' Product Type (Cable Plug / VPD) */
#define IDH_PTYPE_NOT_CABLE	0
#define IDH_PTYPE_PCABLE	3
#define IDH_PTYPE_ACABLE	4
#define IDH_PTYPE_VPD		6

/* SOP Product Type (DFP) */
#define IDH_PTYPE_NOT_DFP	0
#define IDH_PTYPE_DFP_HUB	1
#define IDH_PTYPE_DFP_HOST	2
#define IDH_PTYPE_DFP_PB	3

/* ID Header Mask */
#define IDH_DFP_MASK		GENMASK(25, 23)
#define IDH_CONN_MASK		GENMASK(22, 21)

#define VDO_IDH(usbh, usbd, ufp_cable, is_modal, dfp, conn, vid)		\
	((usbh) << 31 | (usbd) << 30 | ((ufp_cable) & 0x7) << 27		\
	 | (is_modal) << 26 | ((dfp) & 0x7) << 23 | ((conn) & 0x3) << 21	\
	 | ((vid) & 0xffff))

#define PD_IDH_PTYPE(vdo)	(((vdo) >> 27) & 0x7)
#define PD_IDH_VID(vdo)		((vdo) & 0xffff)
#define PD_IDH_MODAL_SUPP(vdo)	((vdo) & (1 << 26))
#define PD_IDH_DFP_PTYPE(vdo)	(((vdo) >> 23) & 0x7)
#define PD_IDH_CONN_TYPE(vdo)	(((vdo) >> 21) & 0x3)
#define PD_IDH_HOST_SUPP(vdo)  ((vdo) & (1 << 31))

/*
 * Cert Stat VDO
 * -------------
 * <31:0>  : USB-IF assigned XID for this cable
 */
#define PD_CSTAT_XID(vdo)	(vdo)
#define VDO_CERT(xid)		((xid) & 0xffffffff)

/*
 * Product VDO
 * -----------
 * <31:16> : USB Product ID
 * <15:0>  : USB bcdDevice
 */
#define VDO_PRODUCT(pid, bcd)	(((pid) & 0xffff) << 16 | ((bcd) & 0xffff))
#define PD_PRODUCT_PID(vdo)	(((vdo) >> 16) & 0xffff)

/*
 * UFP VDO (PD Revision 3.0+ only)
 * --------
 * <31:29> :: UFP VDO version
 * <28>    :: Reserved
 * <27:24> :: Device capability
 * <23:22> :: Connector type (10b == receptacle, 11b == captive plug)
 * <21:11> :: Reserved
 * <10:8>  :: Vconn power (AMA only)
 * <7>     :: Vconn required (AMA only, 0b == no, 1b == yes)
 * <6>     :: Vbus required (AMA only, 0b == yes, 1b == no)
 * <5:3>   :: Alternate modes
 * <2:0>   :: USB highest speed
 */
#define PD_VDO_UFP_DEVCAP(vdo)	FIELD_GET(GENMASK(27, 24), vdo)

/* UFP VDO Version */
#define UFP_VDO_VER1_2		2

/* Device Capability */
#define DEV_USB2_CAPABLE	BIT(0)
#define DEV_USB2_BILLBOARD	BIT(1)
#define DEV_USB3_CAPABLE	BIT(2)
#define DEV_USB4_CAPABLE	BIT(3)

/* Connector Type */
#define UFP_RECEPTACLE		2
#define UFP_CAPTIVE		3

/* Vconn Power (AMA only, set to AMA_VCONN_NOT_REQ if Vconn is not required) */
#define AMA_VCONN_PWR_1W	0
#define AMA_VCONN_PWR_1W5	1
#define AMA_VCONN_PWR_2W	2
#define AMA_VCONN_PWR_3W	3
#define AMA_VCONN_PWR_4W	4
#define AMA_VCONN_PWR_5W	5
#define AMA_VCONN_PWR_6W	6

/* Vconn Required (AMA only) */
#define AMA_VCONN_NOT_REQ	0
#define AMA_VCONN_REQ		1

/* Vbus Required (AMA only) */
#define AMA_VBUS_REQ		0
#define AMA_VBUS_NOT_REQ	1

/* Alternate Modes */
#define UFP_ALTMODE_NOT_SUPP	0
#define UFP_ALTMODE_TBT3	BIT(0)
#define UFP_ALTMODE_RECFG	BIT(1)
#define UFP_ALTMODE_NO_RECFG	BIT(2)

/* USB Highest Speed */
#define UFP_USB2_ONLY		0
#define UFP_USB32_GEN1		1
#define UFP_USB32_4_GEN2	2
#define UFP_USB4_GEN3		3

#define VDO_UFP(ver, cap, conn, vcpwr, vcr, vbr, alt, spd)			\
	(((ver) & 0x7) << 29 | ((cap) & 0xf) << 24 | ((conn) & 0x3) << 22	\
	 | ((vcpwr) & 0x7) << 8 | (vcr) << 7 | (vbr) << 6 | ((alt) & 0x7) << 3	\
	 | ((spd) & 0x7))

/*
 * DFP VDO (PD Revision 3.0+ only)
 * --------
 * <31:29> :: DFP VDO version
 * <28:27> :: Reserved
 * <26:24> :: Host capability
 * <23:22> :: Connector type (10b == receptacle, 11b == captive plug)
 * <21:5>  :: Reserved
 * <4:0>   :: Port number
 */
#define PD_VDO_DFP_HOSTCAP(vdo)	FIELD_GET(GENMASK(26, 24), vdo)

#define DFP_VDO_VER1_1		1
#define HOST_USB2_CAPABLE	BIT(0)
#define HOST_USB3_CAPABLE	BIT(1)
#define HOST_USB4_CAPABLE	BIT(2)
#define DFP_RECEPTACLE		2
#define DFP_CAPTIVE		3

#define VDO_DFP(ver, cap, conn, pnum)						\
	(((ver) & 0x7) << 29 | ((cap) & 0x7) << 24 | ((conn) & 0x3) << 22	\
	 | ((pnum) & 0x1f))

/*
 * Cable VDO (for both Passive and Active Cable VDO in PD Rev2.0)
 * ---------
 * <31:28> :: Cable HW version
 * <27:24> :: Cable FW version
 * <23:20> :: Reserved, Shall be set to zero
 * <19:18> :: type-C to Type-A/B/C/Captive (00b == A, 01 == B, 10 == C, 11 == Captive)
 * <17>    :: Reserved, Shall be set to zero
 * <16:13> :: cable latency (0001 == <10ns(~1m length))
 * <12:11> :: cable termination type (11b == both ends active VCONN req)
 * <10>    :: SSTX1 Directionality support (0b == fixed, 1b == cfgable)
 * <9>     :: SSTX2 Directionality support
 * <8>     :: SSRX1 Directionality support
 * <7>     :: SSRX2 Directionality support
 * <6:5>   :: Vbus current handling capability (01b == 3A, 10b == 5A)
 * <4>     :: Vbus through cable (0b == no, 1b == yes)
 * <3>     :: SOP" controller present? (0b == no, 1b == yes)
 * <2:0>   :: USB SS Signaling support
 *
 * Passive Cable VDO (PD Rev3.0+)
 * ---------
 * <31:28> :: Cable HW version
 * <27:24> :: Cable FW version
 * <23:21> :: VDO version
 * <20>    :: Reserved, Shall be set to zero
 * <19:18> :: Type-C to Type-C/Captive (10b == C, 11b == Captive)
 * <17>    :: Reserved, Shall be set to zero
 * <16:13> :: cable latency (0001 == <10ns(~1m length))
 * <12:11> :: cable termination type (10b == Vconn not req, 01b == Vconn req)
 * <10:9>  :: Maximum Vbus voltage (00b == 20V, 01b == 30V, 10b == 40V, 11b == 50V)
 * <8:7>   :: Reserved, Shall be set to zero
 * <6:5>   :: Vbus current handling capability (01b == 3A, 10b == 5A)
 * <4:3>   :: Reserved, Shall be set to zero
 * <2:0>   :: USB highest speed
 *
 * Active Cable VDO 1 (PD Rev3.0+)
 * ---------
 * <31:28> :: Cable HW version
 * <27:24> :: Cable FW version
 * <23:21> :: VDO version
 * <20>    :: Reserved, Shall be set to zero
 * <19:18> :: Connector type (10b == C, 11b == Captive)
 * <17>    :: Reserved, Shall be set to zero
 * <16:13> :: cable latency (0001 == <10ns(~1m length))
 * <12:11> :: cable termination type (10b == one end active, 11b == both ends active VCONN req)
 * <10:9>  :: Maximum Vbus voltage (00b == 20V, 01b == 30V, 10b == 40V, 11b == 50V)
 * <8>     :: SBU supported (0b == supported, 1b == not supported)
 * <7>     :: SBU type (0b == passive, 1b == active)
 * <6:5>   :: Vbus current handling capability (01b == 3A, 10b == 5A)
 * <4>     :: Vbus through cable (0b == no, 1b == yes)
 * <3>     :: SOP" controller present? (0b == no, 1b == yes)
 * <2:0>   :: USB highest speed
 */
/* Cable VDO Version */
#define CABLE_VDO_VER1_0	0
#define CABLE_VDO_VER1_3	3

/* Connector Type (_ATYPE and _BTYPE are for PD Rev2.0 only) */
#define CABLE_ATYPE		0
#define CABLE_BTYPE		1
#define CABLE_CTYPE		2
#define CABLE_CAPTIVE		3

/* Cable Latency */
#define CABLE_LATENCY_1M	1
#define CABLE_LATENCY_2M	2
#define CABLE_LATENCY_3M	3
#define CABLE_LATENCY_4M	4
#define CABLE_LATENCY_5M	5
#define CABLE_LATENCY_6M	6
#define CABLE_LATENCY_7M	7
#define CABLE_LATENCY_7M_PLUS	8

/* Cable Termination Type */
#define PCABLE_VCONN_NOT_REQ	0
#define PCABLE_VCONN_REQ	1
#define ACABLE_ONE_END		2
#define ACABLE_BOTH_END		3

/* Maximum Vbus Voltage */
#define CABLE_MAX_VBUS_20V	0
#define CABLE_MAX_VBUS_30V	1
#define CABLE_MAX_VBUS_40V	2
#define CABLE_MAX_VBUS_50V	3

/* Active Cable SBU Supported/Type */
#define ACABLE_SBU_SUPP		0
#define ACABLE_SBU_NOT_SUPP	1
#define ACABLE_SBU_PASSIVE	0
#define ACABLE_SBU_ACTIVE	1

/* Vbus Current Handling Capability */
#define CABLE_CURR_DEF		0
#define CABLE_CURR_3A		1
#define CABLE_CURR_5A		2

/* USB SuperSpeed Signaling Support (PD Rev2.0) */
#define CABLE_USBSS_U2_ONLY	0
#define CABLE_USBSS_U31_GEN1	1
#define CABLE_USBSS_U31_GEN2	2

/* USB Highest Speed */
#define CABLE_USB2_ONLY		0
#define CABLE_USB32_GEN1	1
#define CABLE_USB32_4_GEN2	2
#define CABLE_USB4_GEN3		3

#define VDO_CABLE(hw, fw, cbl, lat, term, tx1d, tx2d, rx1d, rx2d, cur, vps, sopp, usbss) \
	(((hw) & 0x7) << 28 | ((fw) & 0x7) << 24 | ((cbl) & 0x3) << 18		\
	 | ((lat) & 0x7) << 13 | ((term) & 0x3) << 11 | (tx1d) << 10		\
	 | (tx2d) << 9 | (rx1d) << 8 | (rx2d) << 7 | ((cur) & 0x3) << 5		\
	 | (vps) << 4 | (sopp) << 3 | ((usbss) & 0x7))
#define VDO_PCABLE(hw, fw, ver, conn, lat, term, vbm, cur, spd)			\
	(((hw) & 0xf) << 28 | ((fw) & 0xf) << 24 | ((ver) & 0x7) << 21		\
	 | ((conn) & 0x3) << 18 | ((lat) & 0xf) << 13 | ((term) & 0x3) << 11	\
	 | ((vbm) & 0x3) << 9 | ((cur) & 0x3) << 5 | ((spd) & 0x7))
#define VDO_ACABLE1(hw, fw, ver, conn, lat, term, vbm, sbu, sbut, cur, vbt, sopp, spd) \
	(((hw) & 0xf) << 28 | ((fw) & 0xf) << 24 | ((ver) & 0x7) << 21		\
	 | ((conn) & 0x3) << 18	| ((lat) & 0xf) << 13 | ((term) & 0x3) << 11	\
	 | ((vbm) & 0x3) << 9 | (sbu) << 8 | (sbut) << 7 | ((cur) & 0x3) << 5	\
	 | (vbt) << 4 | (sopp) << 3 | ((spd) & 0x7))

#define VDO_TYPEC_CABLE_SPEED(vdo)	((vdo) & 0x7)
#define VDO_TYPEC_CABLE_TYPE(vdo)	(((vdo) >> 18) & 0x3)

/*
 * Active Cable VDO 2
 * ---------
 * <31:24> :: Maximum operating temperature
 * <23:16> :: Shutdown temperature
 * <15>    :: Reserved, Shall be set to zero
 * <14:12> :: U3/CLd power
 * <11>    :: U3 to U0 transition mode (0b == direct, 1b == through U3S)
 * <10>    :: Physical connection (0b == copper, 1b == optical)
 * <9>     :: Active element (0b == redriver, 1b == retimer)
 * <8>     :: USB4 supported (0b == yes, 1b == no)
 * <7:6>   :: USB2 hub hops consumed
 * <5>     :: USB2 supported (0b == yes, 1b == no)
 * <4>     :: USB3.2 supported (0b == yes, 1b == no)
 * <3>     :: USB lanes supported (0b == one lane, 1b == two lanes)
 * <2>     :: Optically isolated active cable (0b == no, 1b == yes)
 * <1>     :: Reserved, Shall be set to zero
 * <0>     :: USB gen (0b == gen1, 1b == gen2+)
 */
/* U3/CLd Power*/
#define ACAB2_U3_CLD_10MW_PLUS	0
#define ACAB2_U3_CLD_10MW	1
#define ACAB2_U3_CLD_5MW	2
#define ACAB2_U3_CLD_1MW	3
#define ACAB2_U3_CLD_500UW	4
#define ACAB2_U3_CLD_200UW	5
#define ACAB2_U3_CLD_50UW	6

/* Other Active Cable VDO 2 Fields */
#define ACAB2_U3U0_DIRECT	0
#define ACAB2_U3U0_U3S		1
#define ACAB2_PHY_COPPER	0
#define ACAB2_PHY_OPTICAL	1
#define ACAB2_REDRIVER		0
#define ACAB2_RETIMER		1
#define ACAB2_USB4_SUPP		0
#define ACAB2_USB4_NOT_SUPP	1
#define ACAB2_USB2_SUPP		0
#define ACAB2_USB2_NOT_SUPP	1
#define ACAB2_USB32_SUPP	0
#define ACAB2_USB32_NOT_SUPP	1
#define ACAB2_LANES_ONE		0
#define ACAB2_LANES_TWO		1
#define ACAB2_OPT_ISO_NO	0
#define ACAB2_OPT_ISO_YES	1
#define ACAB2_GEN_1		0
#define ACAB2_GEN_2_PLUS	1

#define VDO_ACABLE2(mtemp, stemp, u3p, trans, phy, ele, u4, hops, u2, u32, lane, iso, gen)	\
	(((mtemp) & 0xff) << 24 | ((stemp) & 0xff) << 16 | ((u3p) & 0x7) << 12	\
	 | (trans) << 11 | (phy) << 10 | (ele) << 9 | (u4) << 8			\
	 | ((hops) & 0x3) << 6 | (u2) << 5 | (u32) << 4 | (lane) << 3		\
	 | (iso) << 2 | (gen))

/*
 * AMA VDO (PD Rev2.0)
 * ---------
 * <31:28> :: Cable HW version
 * <27:24> :: Cable FW version
 * <23:12> :: Reserved, Shall be set to zero
 * <11>    :: SSTX1 Directionality support (0b == fixed, 1b == cfgable)
 * <10>    :: SSTX2 Directionality support
 * <9>     :: SSRX1 Directionality support
 * <8>     :: SSRX2 Directionality support
 * <7:5>   :: Vconn power
 * <4>     :: Vconn power required
 * <3>     :: Vbus power required
 * <2:0>   :: USB SS Signaling support
 */
#define VDO_AMA(hw, fw, tx1d, tx2d, rx1d, rx2d, vcpwr, vcr, vbr, usbss) \
	(((hw) & 0x7) << 28 | ((fw) & 0x7) << 24			\
	 | (tx1d) << 11 | (tx2d) << 10 | (rx1d) << 9 | (rx2d) << 8	\
	 | ((vcpwr) & 0x7) << 5 | (vcr) << 4 | (vbr) << 3		\
	 | ((usbss) & 0x7))

#define PD_VDO_AMA_VCONN_REQ(vdo)	(((vdo) >> 4) & 1)
#define PD_VDO_AMA_VBUS_REQ(vdo)	(((vdo) >> 3) & 1)

#define AMA_USBSS_U2_ONLY	0
#define AMA_USBSS_U31_GEN1	1
#define AMA_USBSS_U31_GEN2	2
#define AMA_USBSS_BBONLY	3

/*
 * VPD VDO
 * ---------
 * <31:28> :: HW version
 * <27:24> :: FW version
 * <23:21> :: VDO version
 * <20:17> :: Reserved, Shall be set to zero
 * <16:15> :: Maximum Vbus voltage (00b == 20V, 01b == 30V, 10b == 40V, 11b == 50V)
 * <14>    :: Charge through current support (0b == 3A, 1b == 5A)
 * <13>    :: Reserved, Shall be set to zero
 * <12:7>  :: Vbus impedance
 * <6:1>   :: Ground impedance
 * <0>     :: Charge through support (0b == no, 1b == yes)
 */
#define VPD_VDO_VER1_0		0
#define VPD_MAX_VBUS_20V	0
#define VPD_MAX_VBUS_30V	1
#define VPD_MAX_VBUS_40V	2
#define VPD_MAX_VBUS_50V	3
#define VPDCT_CURR_3A		0
#define VPDCT_CURR_5A		1
#define VPDCT_NOT_SUPP		0
#define VPDCT_SUPP		1

#define VDO_VPD(hw, fw, ver, vbm, curr, vbi, gi, ct)			\
	(((hw) & 0xf) << 28 | ((fw) & 0xf) << 24 | ((ver) & 0x7) << 21	\
	 | ((vbm) & 0x3) << 15 | (curr) << 14 | ((vbi) & 0x3f) << 7	\
	 | ((gi) & 0x3f) << 1 | (ct))

/*
 * SVDM Discover SVIDs request -> response
 *
 * Request is properly formatted VDM Header with discover SVIDs command.
 * Response is a set of SVIDs of all supported SVIDs with all zero's to
 * mark the end of SVIDs.  If more than 12 SVIDs are supported command SHOULD be
 * repeated.
 */
#define VDO_SVID(svid0, svid1)	(((svid0) & 0xffff) << 16 | ((svid1) & 0xffff))
#define PD_VDO_SVID_SVID0(vdo)	((vdo) >> 16)
#define PD_VDO_SVID_SVID1(vdo)	((vdo) & 0xffff)

/* USB-IF SIDs */
#define USB_SID_PD		0xff00 /* power delivery */
#define USB_SID_DISPLAYPORT	0xff01
#define USB_SID_MHL		0xff02	/* Mobile High-Definition Link */

/* VDM command timeouts (in ms) */

#define PD_T_VDM_UNSTRUCTURED	500
#define PD_T_VDM_BUSY		100
#define PD_T_VDM_WAIT_MODE_E	100
#define PD_T_VDM_SNDR_RSP	30
#define PD_T_VDM_E_MODE		25
#define PD_T_VDM_RCVR_RSP	15

#endif /* __LINUX_USB_PD_VDO_H */
