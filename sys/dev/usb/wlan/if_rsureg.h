/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
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
 * $OpenBSD: if_rsureg.h,v 1.3 2013/04/15 09:23:01 mglocker Exp $
 * $FreeBSD$
 */

/* USB Requests. */
#define R92S_REQ_REGS	0x05

/*
 * MAC registers.
 */
#define R92S_SYSCFG		0x0000
#define R92S_SYS_ISO_CTRL	(R92S_SYSCFG + 0x000)
#define R92S_SYS_FUNC_EN	(R92S_SYSCFG + 0x002)
#define R92S_PMC_FSM		(R92S_SYSCFG + 0x004)
#define R92S_SYS_CLKR		(R92S_SYSCFG + 0x008)
#define R92S_EE_9346CR		(R92S_SYSCFG + 0x00a)
#define R92S_AFE_MISC		(R92S_SYSCFG + 0x010)
#define R92S_SPS0_CTRL		(R92S_SYSCFG + 0x011)
#define R92S_SPS1_CTRL		(R92S_SYSCFG + 0x018)
#define R92S_RF_CTRL		(R92S_SYSCFG + 0x01f)
#define R92S_LDOA15_CTRL	(R92S_SYSCFG + 0x020)
#define R92S_LDOV12D_CTRL	(R92S_SYSCFG + 0x021)
#define R92S_AFE_XTAL_CTRL	(R92S_SYSCFG + 0x026)
#define R92S_AFE_PLL_CTRL	(R92S_SYSCFG + 0x028)
#define R92S_EFUSE_CTRL		(R92S_SYSCFG + 0x030)
#define R92S_EFUSE_TEST		(R92S_SYSCFG + 0x034)
#define R92S_EFUSE_CLK_CTRL	(R92S_SYSCFG + 0x2f8)

#define R92S_CMDCTRL		0x0040
#define R92S_CR			(R92S_CMDCTRL + 0x000)
#define R92S_TXPAUSE		(R92S_CMDCTRL + 0x002)
#define R92S_TCR		(R92S_CMDCTRL + 0x004)
#define R92S_RCR		(R92S_CMDCTRL + 0x008)

#define R92S_MACIDSETTING	0x0050
#define R92S_MACID		(R92S_MACIDSETTING + 0x000)
#define R92S_MAR		(R92S_MACIDSETTING + 0x010)

#define R92S_TIMECTRL		0x0080
#define R92S_TSFTR		(R92S_TIMECTRL + 0x000)

#define R92S_FIFOCTRL		0x00a0
#define R92S_RXFLTMAP_MGT	(R92S_FIFOCTRL + 0x076)
#define R92S_RXFLTMAP_CTL	(R92S_FIFOCTRL + 0x078)
#define R92S_RXFLTMAP_DATA	(R92S_FIFOCTRL + 0x07a)
#define R92S_RXFLTMAP_MESH	(R92S_FIFOCTRL + 0x07c)

#define R92S_SECURITY		0x0240
#define R92S_CAMCMD		(R92S_SECURITY + 0x000)
#define R92S_CAMWRITE		(R92S_SECURITY + 0x004)
#define R92S_CAMREAD		(R92S_SECURITY + 0x008)

#define R92S_GP			0x02e0
#define R92S_GPIO_CTRL		(R92S_GP + 0x00c)
#define R92S_GPIO_IO_SEL	(R92S_GP + 0x00e)
#define R92S_MAC_PINMUX_CTRL	(R92S_GP + 0x011)
#define R92S_LEDCFG		(R92S_GP + 0x012)

#define R92S_IOCMD_CTRL		0x0370
#define R92S_IOCMD_DATA		0x0374

#define R92S_USB_HRPWM		0xfe58

/* Bits for R92S_SYS_FUNC_EN. */
#define R92S_FEN_CPUEN	0x0400

/* Bits for R92S_PMC_FSM. */
#define R92S_PMC_FSM_CUT_M	0x000f8000
#define R92S_PMC_FSM_CUT_S	15

/* Bits for R92S_SYS_CLKR. */
#define R92S_SYS_CLKSEL		0x0001
#define R92S_SYS_PS_CLKSEL	0x0002
#define R92S_SYS_CPU_CLKSEL	0x0004
#define R92S_MAC_CLK_EN		0x0800
#define R92S_SYS_CLK_EN		0x1000
#define R92S_SWHW_SEL		0x4000
#define R92S_FWHW_SEL		0x8000

/* Bits for R92S_EE_9346CR. */
#define R92S_9356SEL		0x10
#define R92S_EEPROM_EN		0x20

/* Bits for R92S_AFE_MISC. */
#define R92S_AFE_MISC_BGEN	0x01
#define R92S_AFE_MISC_MBEN	0x02
#define R92S_AFE_MISC_I32_EN	0x08

/* Bits for R92S_SPS1_CTRL. */
#define R92S_SPS1_LDEN	0x01
#define R92S_SPS1_SWEN	0x02

/* Bits for R92S_LDOA15_CTRL. */
#define R92S_LDA15_EN	0x01

/* Bits for R92S_LDOV12D_CTRL. */
#define R92S_LDV12_EN	0x01

/* Bits for R92C_EFUSE_CTRL. */
#define R92S_EFUSE_CTRL_DATA_M	0x000000ff
#define R92S_EFUSE_CTRL_DATA_S	0
#define R92S_EFUSE_CTRL_ADDR_M	0x0003ff00
#define R92S_EFUSE_CTRL_ADDR_S	8
#define R92S_EFUSE_CTRL_VALID	0x80000000

/* Bits for R92S_CR. */
#define R92S_CR_TXDMA_EN	0x10

/* Bits for R92S_TXPAUSE. */
#define R92S_TXPAUSE_VO		0x01
#define R92S_TXPAUSE_VI		0x02
#define R92S_TXPAUSE_BE		0x04
#define R92S_TXPAUSE_BK		0x08
#define R92S_TXPAUSE_MGT	0x10
#define R92S_TXPAUSE_HIGH	0x20
#define R92S_TXPAUSE_HCCA	0x40

/* Shortcuts. */
#define R92S_TXPAUSE_AC				\
	(R92S_TXPAUSE_VO | R92S_TXPAUSE_VI |	\
	 R92S_TXPAUSE_BE | R92S_TXPAUSE_BK)

#define R92S_TXPAUSE_ALL			\
	(R92S_TXPAUSE_AC | R92S_TXPAUSE_MGT |	\
	 R92S_TXPAUSE_HIGH | R92S_TXPAUSE_HCCA | 0x80)

/* Bits for R92S_TCR. */
#define R92S_TCR_IMEM_CODE_DONE	0x01
#define R92S_TCR_IMEM_CHK_RPT	0x02
#define R92S_TCR_EMEM_CODE_DONE	0x04
#define R92S_TCR_EMEM_CHK_RPT	0x08
#define R92S_TCR_DMEM_CODE_DONE	0x10
#define R92S_TCR_IMEM_RDY	0x20
#define R92S_TCR_FWRDY		0x80

/* Bits for R92S_RCR. */
#define R92S_RCR_AAP		0x00000001
#define R92S_RCR_APM		0x00000002
#define R92S_RCR_AM		0x00000004
#define R92S_RCR_AB		0x00000008
#define R92S_RCR_ACRC32		0x00000020
#define R92S_RCR_AICV		0x00001000
#define R92S_RCR_APP_ICV	0x00010000
#define R92S_RCR_APP_MIC	0x00020000
#define R92S_RCR_ADF		0x00040000
#define R92S_RCR_ACF		0x00080000
#define R92S_RCR_AMF		0x00100000
#define R92S_RCR_ADD3		0x00200000
#define R92S_RCR_APWRMGT	0x00400000
#define R92S_RCR_CBSSID		0x00800000
#define R92S_RCR_APP_PHYSTS	0x02000000
#define R92S_RCR_TCP_OFFLD_EN	0x04000000
#define R92S_RCR_ENMBID		0x08000000

/* Bits for R92S_RXFLTMAP*. */
#define R92S_RXFLTMAP_MGT_DEF	0x3f3f
#define R92S_RXFLTMAP_FW(subtype)	\
	(1 << ((subtype) >> IEEE80211_FC0_SUBTYPE_SHIFT))

/* Bits for R92S_GPIO_IO_SEL. */
#define R92S_GPIO_WPS	0x10

/* Bits for R92S_MAC_PINMUX_CTRL. */
#define R92S_GPIOSEL_GPIO_M		0x03
#define R92S_GPIOSEL_GPIO_S		0
#define R92S_GPIOSEL_GPIO_JTAG		0
#define R92S_GPIOSEL_GPIO_PHYDBG	1
#define R92S_GPIOSEL_GPIO_BT		2
#define R92S_GPIOSEL_GPIO_WLANDBG	3
#define R92S_GPIOMUX_EN			0x08

/* Bits for R92S_CAMCMD. */
#define R92S_CAMCMD_ADDR_M		0x000000ff
#define R92S_CAMCMD_ADDR_S		0
#define R92S_CAMCMD_READ		0x00000000
#define R92S_CAMCMD_WRITE		0x00010000
#define R92S_CAMCMD_POLLING		0x80000000

/*
 * CAM entries.
 */
#define R92S_CAM_ENTRY_LIMIT	32
#define R92S_CAM_ENTRY_BYTES	howmany(R92S_CAM_ENTRY_LIMIT, NBBY)

#define R92S_CAM_CTL0(entry)	((entry) * 8 + 0)
#define R92S_CAM_CTL1(entry)	((entry) * 8 + 1)
#define R92S_CAM_KEY(entry, i)	((entry) * 8 + 2 + (i))

/* Bits for R92S_CAM_CTL0(i). */
#define R92S_CAM_KEYID_M	0x00000003
#define R92S_CAM_KEYID_S	0
#define R92S_CAM_ALGO_M		0x0000001c
#define R92S_CAM_ALGO_S		2
#define R92S_CAM_VALID		0x00008000
#define R92S_CAM_MACLO_M	0xffff0000
#define R92S_CAM_MACLO_S	16

/* Bits for R92S_IOCMD_CTRL. */
#define R92S_IOCMD_CLASS_M		0xff000000
#define R92S_IOCMD_CLASS_S		24
#define R92S_IOCMD_CLASS_BB_RF		0xf0
#define R92S_IOCMD_VALUE_M		0x00ffff00
#define R92S_IOCMD_VALUE_S		8
#define R92S_IOCMD_INDEX_M		0x000000ff
#define R92S_IOCMD_INDEX_S		0
#define R92S_IOCMD_INDEX_BB_READ	0
#define R92S_IOCMD_INDEX_BB_WRITE	1
#define R92S_IOCMD_INDEX_RF_READ	2
#define R92S_IOCMD_INDEX_RF_WRITE	3

/* Bits for R92S_USB_HRPWM. */
#define R92S_USB_HRPWM_PS_ALL_ON	0x04
#define R92S_USB_HRPWM_PS_ST_ACTIVE	0x08

/*
 * Macros to access subfields in registers.
 */
/* Mask and Shift (getter). */
#define MS(val, field)							\
	(((val) & field##_M) >> field##_S)

/* Shift and Mask (setter). */
#define SM(field, val)							\
	(((val) << field##_S) & field##_M)

/* Rewrite. */
#define RW(var, field, val)						\
	(((var) & ~field##_M) | SM(field, val))

/*
 * ROM field with RF config.
 */
enum {
	RTL8712_RFCONFIG_1T = 0x10,
	RTL8712_RFCONFIG_2T = 0x20,
	RTL8712_RFCONFIG_1R = 0x01,
	RTL8712_RFCONFIG_2R = 0x02,
	RTL8712_RFCONFIG_1T1R = 0x11,
	RTL8712_RFCONFIG_1T2R = 0x12,
	RTL8712_RFCONFIG_TURBO = 0x92,
	RTL8712_RFCONFIG_2T2R = 0x22
};

/*
 * Firmware image header.
 */
struct r92s_fw_priv {
	/* QWORD0 */
	uint16_t	signature;
	uint8_t		hci_sel;
#define R92S_HCI_SEL_PCIE	0x01
#define R92S_HCI_SEL_USB	0x02
#define R92S_HCI_SEL_SDIO	0x04
#define R92S_HCI_SEL_8172	0x10
#define R92S_HCI_SEL_AP		0x80

	uint8_t		chip_version;
	uint16_t	custid;
	uint8_t		rf_config;
//0x11:  1T1R, 0x12: 1T2R, 0x92: 1T2R turbo, 0x22: 2T2R
	uint8_t		nendpoints;
	/* QWORD1 */
	uint32_t	regulatory;
	uint8_t		rfintfs;
	uint8_t		def_nettype;
	uint8_t		turbo_mode;
	uint8_t		lowpower_mode;
	/* QWORD2 */
	uint8_t		lbk_mode;
	uint8_t		mp_mode;
	uint8_t		vcs_type;
#define R92S_VCS_TYPE_DISABLE	0
#define R92S_VCS_TYPE_ENABLE	1
#define R92S_VCS_TYPE_AUTO	2

	uint8_t		vcs_mode;
#define R92S_VCS_MODE_NONE	0
#define R92S_VCS_MODE_RTS_CTS	1
#define R92S_VCS_MODE_CTS2SELF	2

	uint32_t	reserved1;
	/* QWORD3 */
	uint8_t		qos_en;
	uint8_t		bw40_en;
	uint8_t		amsdu2ampdu_en;
	uint8_t		ampdu_en;
	uint8_t		rc_offload;
	uint8_t		agg_offload;
	uint16_t	reserved2;
	/* QWORD4 */
	uint8_t		beacon_offload;
	uint8_t		mlme_offload;
	uint8_t		hwpc_offload;
	uint8_t		tcpcsum_offload;
	uint8_t		tcp_offload;
	uint8_t		ps_offload;
	uint8_t		wwlan_offload;
	uint8_t		reserved3;
	/* QWORD5 */
	uint16_t	tcp_tx_len;
	uint16_t	tcp_rx_len;
	uint32_t	reserved4;
} __packed;

struct r92s_fw_hdr {
	uint16_t	signature;
	uint16_t	version;
	uint32_t	dmemsz;
	uint32_t	imemsz;
	uint32_t	sramsz;
	uint32_t	privsz;
	uint16_t	efuse_addr;
	uint16_t	h2c_resp_addr;
	uint32_t	svnrev;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hour;
	uint8_t		minute;
	struct		r92s_fw_priv priv;
} __packed;

/* Structure for FW commands and FW events notifications. */
struct r92s_fw_cmd_hdr {
	uint16_t	len;
	uint8_t		code;
	uint8_t		seq;
#define R92S_FW_CMD_MORE	0x80

	uint32_t	reserved;
} __packed;

/* FW commands codes. */
#define R92S_CMD_READ_MACREG		0
#define R92S_CMD_WRITE_MACREG		1
#define R92S_CMD_READ_BBREG		2
#define R92S_CMD_WRITE_BBREG		3
#define R92S_CMD_READ_RFREG		4
#define R92S_CMD_WRITE_RFREG		5
#define R92S_CMD_READ_EEPROM		6
#define R92S_CMD_WRITE_EEPROM		7
#define R92S_CMD_READ_EFUSE		8
#define R92S_CMD_WRITE_EFUSE		9
#define R92S_CMD_READ_CAM		10
#define R92S_CMD_WRITE_CAM		11
#define R92S_CMD_SET_BCNITV		12
#define R92S_CMD_SET_MBIDCFG		13
#define R92S_CMD_JOIN_BSS		14
#define R92S_CMD_DISCONNECT		15
#define R92S_CMD_CREATE_BSS		16
#define R92S_CMD_SET_OPMODE		17
#define R92S_CMD_SITE_SURVEY		18
#define R92S_CMD_SET_AUTH		19
#define R92S_CMD_SET_KEY		20
#define R92S_CMD_SET_STA_KEY		21
#define R92S_CMD_SET_ASSOC_STA		22
#define R92S_CMD_DEL_ASSOC_STA		23
#define R92S_CMD_SET_STAPWRSTATE	24
#define R92S_CMD_SET_BASIC_RATE		25
#define R92S_CMD_GET_BASIC_RATE		26
#define R92S_CMD_SET_DATA_RATE		27
#define R92S_CMD_GET_DATA_RATE		28
#define R92S_CMD_SET_PHY_INFO		29
#define R92S_CMD_GET_PHY_INFO		30
#define R92S_CMD_SET_PHY		31
#define R92S_CMD_GET_PHY		32
#define R92S_CMD_READ_RSSI		33
#define R92S_CMD_READ_GAIN		34
#define R92S_CMD_SET_ATIM		35
#define R92S_CMD_SET_PWR_MODE		36
#define R92S_CMD_JOIN_BSS_RPT		37
#define R92S_CMD_SET_RA_TABLE		38
#define R92S_CMD_GET_RA_TABLE		39
#define R92S_CMD_GET_CCX_REPORT		40
#define R92S_CMD_GET_DTM_REPORT		41
#define R92S_CMD_GET_TXRATE_STATS	42
#define R92S_CMD_SET_USB_SUSPEND	43
#define R92S_CMD_SET_H2C_LBK		44
#define R92S_CMD_ADDBA_REQ		45
#define R92S_CMD_SET_CHANNEL		46
#define R92S_CMD_SET_TXPOWER		47
#define R92S_CMD_SWITCH_ANTENNA		48
#define R92S_CMD_SET_CRYSTAL_CAL	49
#define R92S_CMD_SET_SINGLE_CARRIER_TX	50
#define R92S_CMD_SET_SINGLE_TONE_TX	51
#define R92S_CMD_SET_CARRIER_SUPPR_TX	52
#define R92S_CMD_SET_CONTINUOUS_TX	53
#define R92S_CMD_SWITCH_BANDWIDTH	54
#define R92S_CMD_TX_BEACON		55
#define R92S_CMD_SET_POWER_TRACKING	56
#define R92S_CMD_AMSDU_TO_AMPDU		57
#define R92S_CMD_SET_MAC_ADDRESS	58
#define R92S_CMD_GET_H2C_LBK		59
#define R92S_CMD_SET_PBREQ_IE		60
#define R92S_CMD_SET_ASSOCREQ_IE	61
#define R92S_CMD_SET_PBRESP_IE		62
#define R92S_CMD_SET_ASSOCRESP_IE	63
#define R92S_CMD_GET_CURDATARATE	64
#define R92S_CMD_GET_TXRETRY_CNT	65
#define R92S_CMD_GET_RXRETRY_CNT	66
#define R92S_CMD_GET_BCNOK_CNT		67
#define R92S_CMD_GET_BCNERR_CNT		68
#define R92S_CMD_GET_CURTXPWR_LEVEL	69
#define R92S_CMD_SET_DIG		70
#define R92S_CMD_SET_RA			71
#define R92S_CMD_SET_PT			72
#define R92S_CMD_READ_TSSI		73

/* FW events notifications codes. */
#define R92S_EVT_READ_MACREG		0
#define R92S_EVT_READ_BBREG		1
#define R92S_EVT_READ_RFREG		2
#define R92S_EVT_READ_EEPROM		3
#define R92S_EVT_READ_EFUSE		4
#define R92S_EVT_READ_CAM		5
#define R92S_EVT_GET_BASICRATE		6
#define R92S_EVT_GET_DATARATE		7
#define R92S_EVT_SURVEY			8
#define R92S_EVT_SURVEY_DONE		9
#define R92S_EVT_JOIN_BSS		10
#define R92S_EVT_ADD_STA		11
#define R92S_EVT_DEL_STA		12
#define R92S_EVT_ATIM_DONE		13
#define R92S_EVT_TX_REPORT		14
#define R92S_EVT_CCX_REPORT		15
#define R92S_EVT_DTM_REPORT		16
#define R92S_EVT_TXRATE_STATS		17
#define R92S_EVT_C2H_LBK		18
#define R92S_EVT_FWDBG			19
#define R92S_EVT_C2H_FEEDBACK		20
#define R92S_EVT_ADDBA			21
#define R92S_EVT_C2H_BCN		22
#define R92S_EVT_PWR_STATE		23
#define R92S_EVT_WPS_PBC		24
#define R92S_EVT_ADDBA_REQ_REPORT	25

/* Structure for R92S_CMD_SITE_SURVEY. */
struct r92s_fw_cmd_sitesurvey {
	uint32_t	active;
	uint32_t	limit;
	uint32_t	ssidlen;
	uint8_t		ssid[32 + 1];
} __packed;

/* Structure for R92S_CMD_SET_AUTH. */
struct r92s_fw_cmd_auth {
	uint8_t	mode;
#define R92S_AUTHMODE_OPEN	0
#define R92S_AUTHMODE_SHARED	1
#define R92S_AUTHMODE_WPA	2

	uint8_t	dot1x;
} __packed;

/* Structure for R92S_CMD_SET_KEY. */
struct r92s_fw_cmd_set_key {
	uint8_t	algo;
#define R92S_KEY_ALGO_NONE	0
#define R92S_KEY_ALGO_WEP40	1
#define R92S_KEY_ALGO_TKIP	2
#define R92S_KEY_ALGO_TKIP_MMIC	3
#define R92S_KEY_ALGO_AES	4
#define R92S_KEY_ALGO_WEP104	5
#define R92S_KEY_ALGO_INVALID	0xff	/* for rsu_crypto_mode() only */

	uint8_t	cam_id;
	uint8_t	grpkey;
	uint8_t	key[IEEE80211_KEYBUF_SIZE];
} __packed;

/* Structure for R92S_CMD_SET_STA_KEY. */
struct r92s_fw_cmd_set_key_mac {
	uint8_t	macaddr[IEEE80211_ADDR_LEN];
	uint8_t	algo;
	uint8_t	key[IEEE80211_KEYBUF_SIZE];
} __packed;

/* Structures for R92S_EVENT_SURVEY/R92S_CMD_JOIN_BSS. */
/* NDIS_802_11_SSID. */
struct ndis_802_11_ssid {
	uint32_t	ssidlen;
	uint8_t		ssid[32];
} __packed;

/* NDIS_802_11_CONFIGURATION_FH. */
struct ndis_802_11_configuration_fh {
	uint32_t	len;
	uint32_t	hoppattern;
	uint32_t	hopset;
	uint32_t	dwelltime;
} __packed;

/* NDIS_802_11_CONFIGURATION. */
struct ndis_802_11_configuration {
	uint32_t	len;
	uint32_t	bintval;
	uint32_t	atim;
	uint32_t	dsconfig;
	struct		ndis_802_11_configuration_fh fhconfig;
} __packed;

/* NDIS_WLAN_BSSID_EX. */
struct ndis_wlan_bssid_ex {
	uint32_t	len;
	uint8_t		macaddr[IEEE80211_ADDR_LEN];
	uint8_t		reserved[2];
	struct		ndis_802_11_ssid ssid;
	uint32_t	privacy;
	int32_t		rssi;
	uint32_t	networktype;
#define NDIS802_11FH		0
#define NDIS802_11DS		1
#define NDIS802_11OFDM5		2
#define NDIS802_11OFDM24	3
#define NDIS802_11AUTOMODE	4

	struct		ndis_802_11_configuration config;
	uint32_t	inframode;
#define NDIS802_11IBSS			0
#define NDIS802_11INFRASTRUCTURE	1
#define NDIS802_11AUTOUNKNOWN		2
#define NDIS802_11MONITOR		3
#define NDIS802_11APMODE		4

	uint8_t		supprates[16];
	uint32_t	ieslen;
	/* Followed by ``ieslen'' bytes. */
} __packed;

/* NDIS_802_11_FIXED_IEs. */
struct ndis_802_11_fixed_ies {
	uint8_t		tstamp[8];
	uint16_t	bintval;
	uint16_t	capabilities;
} __packed;

/* Structure for R92S_CMD_SET_PWR_MODE. */
struct r92s_set_pwr_mode {
	uint8_t		mode;
#define R92S_PS_MODE_ACTIVE	0
#define R92S_PS_MODE_MIN	1
#define R92S_PS_MODE_MAX	2
#define R92S_PS_MODE_DTIM	3
#define R92S_PS_MODE_VOIP	4
#define R92S_PS_MODE_UAPSD_WMM	5
#define R92S_PS_MODE_UAPSD	6
#define R92S_PS_MODE_IBSS	7
#define R92S_PS_MODE_WWLAN	8
#define R92S_PS_MODE_RADIOOFF	9
#define R92S_PS_MODE_DISABLE	10

	uint8_t		low_traffic_en;
	uint8_t		lpnav_en;
	uint8_t		rf_low_snr_en;
	uint8_t		dps_en;
	uint8_t		bcn_rx_en;
	uint8_t		bcn_pass_cnt;
	uint8_t		bcn_to;
	uint16_t	bcn_itv;
	uint8_t		app_itv;
	uint8_t		awake_bcn_itv;
	uint8_t		smart_ps;
	uint8_t		bcn_pass_time;
} __packed;

/* Structure for R92S_CMD_SET_CHANNEL. */
struct r92s_set_channel {
	uint32_t	channel;
} __packed;

/* Structure for event R92S_EVENT_JOIN_BSS. */
struct r92s_event_join_bss {
	uint32_t	next;
	uint32_t	prev;
	uint32_t	networktype;
	uint32_t	fixed;
	uint32_t	lastscanned;
	uint32_t	associd;
	uint32_t	join_res;
	struct		ndis_wlan_bssid_ex bss;
} __packed;

#define R92S_MACID_BSS	5	/* XXX hardcoded somewhere */

/* Rx MAC descriptor. */
struct r92s_rx_stat {
	uint32_t	rxdw0;
#define R92S_RXDW0_PKTLEN_M	0x00003fff
#define R92S_RXDW0_PKTLEN_S	0
#define R92S_RXDW0_CRCERR	0x00004000
#define R92S_RXDW0_ICVERR	0x00008000
#define R92S_RXDW0_INFOSZ_M	0x000f0000
#define R92S_RXDW0_INFOSZ_S	16
#define R92S_RXDW0_CIPHER_M	0x00700000
#define R92S_RXDW0_CIPHER_S	20
#define R92S_RXDW0_QOS		0x00800000
#define R92S_RXDW0_SHIFT_M	0x03000000
#define R92S_RXDW0_SHIFT_S	24
#define R92S_RXDW0_PHYST	0x04000000
#define R92S_RXDW0_DECRYPTED	0x08000000

	uint32_t	rxdw1;
#define R92S_RXDW1_MOREFRAG	0x08000000

	uint32_t	rxdw2;
#define R92S_RXDW2_FRAG_M	0x0000f000
#define R92S_RXDW2_FRAG_S	12
#define R92S_RXDW2_PKTCNT_M	0x00ff0000
#define R92S_RXDW2_PKTCNT_S	16

	uint32_t	rxdw3;
#define R92S_RXDW3_RATE_M	0x0000003f
#define R92S_RXDW3_RATE_S	0
#define R92S_RXDW3_TCPCHKRPT	0x00000800
#define R92S_RXDW3_IPCHKRPT	0x00001000
#define R92S_RXDW3_TCPCHKVALID	0x00002000
#define R92S_RXDW3_HTC		0x00004000

	uint32_t	rxdw4;
	uint32_t	tsf_low;
} __packed __aligned(4);

/* Rx PHY descriptor. */
struct r92s_rx_phystat {
	uint32_t	phydw0;
	uint32_t	phydw1;
	uint32_t	phydw2;
	uint32_t	phydw3;
	uint32_t	phydw4;
	uint32_t	phydw5;
	uint32_t	phydw6;
	uint32_t	phydw7;
} __packed __aligned(4);

/* Rx PHY CCK descriptor. */
struct r92s_rx_cck {
	uint8_t		adc_pwdb[4];
	uint8_t		sq_rpt;
	uint8_t		agc_rpt;
} __packed;

/* Tx MAC descriptor. */
struct r92s_tx_desc {
	uint32_t	txdw0;
#define R92S_TXDW0_PKTLEN_M	0x0000ffff
#define R92S_TXDW0_PKTLEN_S	0
#define R92S_TXDW0_OFFSET_M	0x00ff0000
#define R92S_TXDW0_OFFSET_S	16
#define R92S_TXDW0_TYPE_M	0x03000000
#define R92S_TXDW0_TYPE_S	24
#define R92S_TXDW0_LSG		0x04000000
#define R92S_TXDW0_FSG		0x08000000
#define R92S_TXDW0_LINIP	0x10000000
#define R92S_TXDW0_OWN		0x80000000

	uint32_t	txdw1;
#define R92S_TXDW1_MACID_M	0x0000001f
#define R92S_TXDW1_MACID_S	0
#define R92S_TXDW1_MOREDATA	0x00000020
#define R92S_TXDW1_MOREFRAG	0x00000040
#define R92S_TXDW1_QSEL_M	0x00001f00
#define R92S_TXDW1_QSEL_S	8
#define R92S_TXDW1_QSEL_BE	0x03
#define R92S_TXDW1_QSEL_H2C	0x13
#define R92S_TXDW1_NONQOS	0x00010000
#define R92S_TXDW1_KEYIDX_M	0x00060000
#define R92S_TXDW1_KEYIDX_S	17
#define R92S_TXDW1_CIPHER_M	0x00c00000
#define R92S_TXDW1_CIPHER_S	22
#define R92S_TXDW1_CIPHER_NONE	0
#define R92S_TXDW1_CIPHER_WEP	1
#define R92S_TXDW1_CIPHER_TKIP	2
#define R92S_TXDW1_CIPHER_AES	3
#define R92S_TXDW1_HWPC		0x80000000

	uint32_t	txdw2;
#define R92S_TXDW2_RTY_LMT_M	0x0000003f
#define R92S_TXDW2_RTY_LMT_S	0
#define R92S_TXDW2_RTY_LMT_ENA	0x00000040
#define R92S_TXDW2_BMCAST	0x00000080
#define R92S_TXDW2_AGGEN	0x20000000
#define R92S_TXDW2_BK		0x40000000

	uint32_t	txdw3;
#define R92S_TXDW3_SEQ_M	0x0fff0000
#define R92S_TXDW3_SEQ_S	16
#define R92S_TXDW3_FRAG_M	0xf0000000
#define R92S_TXDW3_FRAG_S	28

	uint32_t	txdw4;
#define R92S_TXDW4_TXBW		0x00040000
#define R92S_TXDW4_DRVRATE	0x80000000

	uint32_t	txdw5;
#define R92S_TXDW5_DATARATE_M		0x00007e00
#define R92S_TXDW5_DATARATE_S		9
#define R92S_TXDW5_DISFB		0x00008000
#define R92S_TXDW5_DATARATE_FB_LMT_M	0x001f0000
#define R92S_TXDW5_DATARATE_FB_LMT_S	16

	uint16_t	ipchksum;
	uint16_t	tcpchksum;

	uint16_t	txbufsize;
	uint16_t	reserved1;
} __packed __aligned(4);

struct r92s_add_ba_event {
	uint8_t mac_addr[IEEE80211_ADDR_LEN];
	uint16_t ssn;
	uint8_t tid;
};

struct r92s_add_ba_req {
	uint32_t tid;
};

/*
 * Driver definitions.
 */
#define RSU_RX_LIST_COUNT	1
#define RSU_TX_LIST_COUNT	32

#define RSU_RXBUFSZ	(30 * 1024)
#define RSU_TXBUFSZ	\
	((sizeof(struct r92s_tx_desc) + IEEE80211_MAX_LEN + 3) & ~3)

#define RSU_TX_TIMEOUT	5000	/* ms */
#define RSU_CMD_TIMEOUT	2000	/* ms */

/* Queue ids (used by soft only). */
#define RSU_QID_BCN	0
#define RSU_QID_MGT	1
#define RSU_QID_BMC	2
#define RSU_QID_VO	3
#define RSU_QID_VI	4
#define RSU_QID_BE	5
#define RSU_QID_BK	6
#define RSU_QID_RXOFF	7
#define RSU_QID_H2C	8
#define RSU_QID_C2H	9

/* Map AC to queue id. */
static const uint8_t rsu_ac2qid[WME_NUM_AC] = {
	RSU_QID_BE,
	RSU_QID_BK,
	RSU_QID_VI,
	RSU_QID_VO
};

/* Pipe index to endpoint address mapping. */
static const uint8_t r92s_epaddr[] =
    { 0x83, 0x04, 0x06, 0x0d,
      0x05, 0x07,
      0x89, 0x0a, 0x0b, 0x0c };

/* Queue id to pipe index mapping for 4 endpoints configurations. */
static const uint8_t rsu_qid2idx_4ep[] =
    { 3, 3, 3, 1, 1, 2, 2, 0, 3, 0 };

/* Queue id to pipe index mapping for 6 endpoints configurations. */
static const uint8_t rsu_qid2idx_6ep[] =
    { 3, 3, 3, 1, 4, 2, 5, 0, 3, 0 };

/* Queue id to pipe index mapping for 11 endpoints configurations. */
static const uint8_t rsu_qid2idx_11ep[] =
    { 7, 9, 8, 1, 4, 2, 5, 0, 3, 6 };

struct rsu_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsft;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
} __packed __aligned(8);

#define RSU_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_TSFT |		\
	 1 << IEEE80211_RADIOTAP_FLAGS |	\
	 1 << IEEE80211_RADIOTAP_RATE |		\
	 1 << IEEE80211_RADIOTAP_CHANNEL |	\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)

struct rsu_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_pad;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RSU_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |	\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct rsu_softc;

enum {
	RSU_BULK_RX,
	RSU_BULK_TX_BE_BK,	/* = WME_AC_BE/BK */
	RSU_BULK_TX_VI_VO,	/* = WME_AC_VI/VO */
	RSU_BULK_TX_H2C,	/* H2C */
	RSU_N_TRANSFER,
};

struct rsu_data {
	struct rsu_softc	*sc;
	uint8_t			*buf;
	uint16_t		buflen;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
	STAILQ_ENTRY(rsu_data)  next;
};

struct rsu_vap {
	struct ieee80211vap		vap;

	int				(*newstate)(struct ieee80211vap *,
					    enum ieee80211_state, int);
};
#define RSU_VAP(vap) 			((struct rsu_vap *)(vap))

#define	RSU_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define	RSU_UNLOCK(sc)			mtx_unlock(&(sc)->sc_mtx)
#define	RSU_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->sc_mtx, MA_OWNED)

#define RSU_DELKEY_BMAP_LOCK_INIT(_sc)	\
	mtx_init(&(_sc)->free_keys_bmap_mtx, "bmap lock", NULL, MTX_DEF)
#define RSU_DELKEY_BMAP_LOCK(_sc)	mtx_lock(&(_sc)->free_keys_bmap_mtx)
#define RSU_DELKEY_BMAP_UNLOCK(_sc)	mtx_unlock(&(_sc)->free_keys_bmap_mtx)
#define RSU_DELKEY_BMAP_LOCK_DESTROY(_sc)	\
	mtx_destroy(&(_sc)->free_keys_bmap_mtx)

struct rsu_softc {
	struct ieee80211com		sc_ic;
	struct mbufq			sc_snd;
	device_t			sc_dev;
	struct usb_device		*sc_udev;

	struct timeout_task		calib_task;
	struct task			tx_task;
	struct mtx			sc_mtx;
	int				sc_ht;
	int				sc_nendpoints;
	int				sc_curpwrstate;
	int				sc_currssi;

	u_int				sc_running:1,
					sc_vap_is_running:1,
					sc_rx_checksum_enable:1,
					sc_calibrating:1,
					sc_active_scan:1,
					sc_extra_scan:1;
	u_int				cut;
	uint8_t				sc_rftype;
	int8_t				sc_nrxstream;
	int8_t				sc_ntxstream;
	struct rsu_data			sc_rx[RSU_RX_LIST_COUNT];
	struct rsu_data			sc_tx[RSU_TX_LIST_COUNT];
	uint8_t				cmd_seq;
	uint8_t				rom[128];
	struct usb_xfer			*sc_xfer[RSU_N_TRANSFER];

	STAILQ_HEAD(, rsu_data)		sc_rx_active;
	STAILQ_HEAD(, rsu_data)		sc_rx_inactive;
	STAILQ_HEAD(, rsu_data)		sc_tx_active[RSU_N_TRANSFER];
	STAILQ_HEAD(, rsu_data)		sc_tx_inactive;
	STAILQ_HEAD(, rsu_data)		sc_tx_pending[RSU_N_TRANSFER];

	struct task			del_key_task;
	uint8_t				keys_bmap[R92S_CAM_ENTRY_BYTES];
	const struct ieee80211_key	*group_keys[IEEE80211_WEP_NKID];

	struct mtx			free_keys_bmap_mtx;
	uint8_t				free_keys_bmap[R92S_CAM_ENTRY_BYTES];

	union {
		struct rsu_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th

	union {
		struct rsu_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
};
