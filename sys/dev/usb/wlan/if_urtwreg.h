/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2008 Weongyo Jeong <weongyo@FreeBSD.org>
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

#define	URTW_CONFIG_INDEX		0
#define	URTW_IFACE_INDEX		0

/* for 8187  */
#define	URTW_MAC0			0x0000		/* 1 byte  */
#define	URTW_MAC1			0x0001		/* 1 byte  */
#define	URTW_MAC2			0x0002		/* 1 byte  */
#define	URTW_MAC3			0x0003		/* 1 byte  */
#define	URTW_MAC4			0x0004		/* 1 byte  */
#define	URTW_MAC5			0x0005		/* 1 byte  */
#define	URTW_MAR			0x0008		/* 6 byte  */
#define	URTW_RXFIFO_CNT			0x0010		/* 1 byte  */
#define	URTW_TXFIFO_CNT			0x0012		/* 1 byte  */
#define	URTW_BQREQ			0x0013		/* 1 byte  */
#define	URTW_TSFT			0x0018		/* 6 byte  */
#define	URTW_TLPDA			0x0020		/* 4 byte  */
#define	URTW_TNPDA			0x0024		/* 4 byte  */
#define	URTW_THPDA			0x0028		/* 4 byte  */
#define	URTW_BRSR			0x002c		/* 2 byte  */
#define	URTW_BRSR_MBR_8185		(0x0fff)
#define	URTW_8187B_EIFS			0x002d		/* 1 byte for 8187B  */
#define	URTW_BSSID			0x002e		/* 6 byte  */
#define	URTW_BRSR_8187B			0x0034		/* 2 byte for 8187B  */
#define	URTW_RESP_RATE			0x0034		/* 1 byte for 8187L  */
#define	URTW_RESP_MAX_RATE_SHIFT	(4)
#define	URTW_RESP_MIN_RATE_SHIFT	(0)
#define	URTW_EIFS			0x0035		/* 1 byte  */
#define	URTW_CMD			0x0037		/* 1 byte  */
#define	URTW_CMD_TX_ENABLE		(0x4)
#define	URTW_CMD_RX_ENABLE		(0x8)
#define	URTW_CMD_RST			(0x10)
#define	URTW_INTR_MASK			0x003c		/* 2 byte  */
#define	URTW_INTR_STATUS		0x003e		/* 2 byte  */
#define	URTW_TX_CONF			0x0040		/* 4 byte  */
#define	URTW_TX_LOOPBACK_SHIFT		(17)
#define	URTW_TX_LOOPBACK_NONE		(0 << URTW_TX_LOOPBACK_SHIFT)
#define	URTW_TX_LOOPBACK_MAC		(1 << URTW_TX_LOOPBACK_SHIFT)
#define	URTW_TX_LOOPBACK_BASEBAND	(2 << URTW_TX_LOOPBACK_SHIFT)
#define	URTW_TX_LOOPBACK_CONTINUE	(3 << URTW_TX_LOOPBACK_SHIFT)
#define	URTW_TX_LOOPBACK_MASK		(0x60000)
#define	URTW_TX_DPRETRY_MASK		(0xff00)
#define	URTW_TX_RTSRETRY_MASK		(0xff)
#define	URTW_TX_DPRETRY_SHIFT		(0)
#define	URTW_TX_RTSRETRY_SHIFT		(8)
#define	URTW_TX_NOCRC			(0x10000)
#define	URTW_TX_MXDMA_MASK		(0xe00000)
#define	URTW_TX_MXDMA_1024		(6 << URTW_TX_MXDMA_SHIFT)
#define	URTW_TX_MXDMA_2048		(7 << URTW_TX_MXDMA_SHIFT)
#define	URTW_TX_MXDMA_SHIFT		(21)
#define	URTW_TX_DISCW			(1 << 20)
#define	URTW_TX_SWPLCPLEN		(1 << 24)
#define	URTW_TX_R8187vD			(5 << 25)
#define	URTW_TX_R8187vD_B		(6 << 25)
#define	URTW_TX_HWMASK			(7 << 25)
#define	URTW_TX_DISREQQSIZE		(1 << 28)
#define	URTW_TX_HW_SEQNUM		(1 << 30)
#define	URTW_TX_CWMIN			(1U << 31)
#define	URTW_TX_NOICV			(0x80000)
#define	URTW_RX				0x0044		/* 4 byte  */
#define	URTW_RX_9356SEL			(1 << 6)
#define	URTW_RX_FILTER_MASK			\
	(URTW_RX_FILTER_ALLMAC | URTW_RX_FILTER_NICMAC | URTW_RX_FILTER_MCAST | \
	URTW_RX_FILTER_BCAST | URTW_RX_FILTER_CRCERR | URTW_RX_FILTER_ICVERR | \
	URTW_RX_FILTER_DATA | URTW_RX_FILTER_CTL | URTW_RX_FILTER_MNG |	\
	(1 << 21) |							\
	URTW_RX_FILTER_PWR | URTW_RX_CHECK_BSSID)
#define	URTW_RX_FILTER_ALLMAC		(0x00000001)
#define	URTW_RX_FILTER_NICMAC		(0x00000002)
#define	URTW_RX_FILTER_MCAST		(0x00000004)
#define	URTW_RX_FILTER_BCAST		(0x00000008)
#define	URTW_RX_FILTER_CRCERR		(0x00000020)
#define	URTW_RX_FILTER_ICVERR		(0x00001000)
#define	URTW_RX_FILTER_DATA		(0x00040000)
#define	URTW_RX_FILTER_CTL		(0x00080000)
#define	URTW_RX_FILTER_MNG		(0x00100000)
#define	URTW_RX_FILTER_PWR		(0x00400000)
#define	URTW_RX_CHECK_BSSID		(0x00800000)
#define	URTW_RX_FIFO_THRESHOLD_MASK	((1 << 13) | (1 << 14) | (1 << 15))
#define	URTW_RX_FIFO_THRESHOLD_SHIFT	(13)
#define	URTW_RX_FIFO_THRESHOLD_128	(3)
#define	URTW_RX_FIFO_THRESHOLD_256	(4)
#define	URTW_RX_FIFO_THRESHOLD_512	(5)
#define	URTW_RX_FIFO_THRESHOLD_1024	(6)
#define	URTW_RX_FIFO_THRESHOLD_NONE	(7 << URTW_RX_FIFO_THRESHOLD_SHIFT)
#define	URTW_RX_AUTORESETPHY		(1 << URTW_RX_AUTORESETPHY_SHIFT)
#define	URTW_RX_AUTORESETPHY_SHIFT	(28)
#define	URTW_MAX_RX_DMA_MASK		((1<<8) | (1<<9) | (1<<10))
#define	URTW_MAX_RX_DMA_2048		(7 << URTW_MAX_RX_DMA_SHIFT)
#define	URTW_MAX_RX_DMA_1024		(6)
#define	URTW_MAX_RX_DMA_SHIFT		(10)
#define	URTW_RCR_ONLYERLPKT		(1U << 31)
#define	URTW_INT_TIMEOUT		0x0048		/* 4 byte  */
#define	URTW_INT_TBDA			0x004c		/* 4 byte  */
#define	URTW_EPROM_CMD			0x0050		/* 1 byte  */
#define	URTW_EPROM_CMD_NORMAL		(0x0)
#define	URTW_EPROM_CMD_NORMAL_MODE				\
	(URTW_EPROM_CMD_NORMAL << URTW_EPROM_CMD_SHIFT)
#define	URTW_EPROM_CMD_LOAD		(0x1)
#define	URTW_EPROM_CMD_PROGRAM		(0x2)
#define	URTW_EPROM_CMD_PROGRAM_MODE				\
	(URTW_EPROM_CMD_PROGRAM << URTW_EPROM_CMD_SHIFT)
#define	URTW_EPROM_CMD_CONFIG		(0x3)
#define	URTW_EPROM_CMD_SHIFT		(6)
#define	URTW_EPROM_CMD_MASK		((1 << 7) | (1 << 6))
#define	URTW_EPROM_READBIT		(0x1)
#define	URTW_EPROM_WRITEBIT		(0x2)
#define	URTW_EPROM_CK			(0x4)
#define	URTW_EPROM_CS			(0x8)
#define	URTW_CONFIG0			0x0051		/* 1 byte  */
#define	URTW_CONFIG1			0x0052		/* 1 byte  */
#define	URTW_CONFIG2			0x0053		/* 1 byte  */
#define	URTW_ANAPARAM			0x0054		/* 4 byte  */
#define	URTW_8225_ANAPARAM_ON		(0xa0000a59)
#define	URTW_8225_ANAPARAM_OFF		(0xa00beb59)
#define	URTW_8187B_8225_ANAPARAM_ON	(0x45090658)
#define	URTW_8187B_8225_ANAPARAM_OFF	(0x55480658)
#define	URTW_MSR			0x0058		/* 1 byte  */
#define	URTW_MSR_LINK_MASK		((1 << 2) | (1 << 3))
#define	URTW_MSR_LINK_SHIFT		(2)
#define	URTW_MSR_LINK_NONE		(0 << URTW_MSR_LINK_SHIFT)
#define	URTW_MSR_LINK_ADHOC		(1 << URTW_MSR_LINK_SHIFT)
#define	URTW_MSR_LINK_STA		(2 << URTW_MSR_LINK_SHIFT)
#define	URTW_MSR_LINK_HOSTAP		(3 << URTW_MSR_LINK_SHIFT)
#define	URTW_MSR_LINK_ENEDCA		(1 << 4)
#define	URTW_CONFIG3			0x0059		/* 1 byte  */
#define	URTW_CONFIG3_ANAPARAM_WRITE	(0x40)
#define	URTW_CONFIG3_GNT_SELECT		(0x80)
#define	URTW_CONFIG3_ANAPARAM_W_SHIFT	(6)
#define	URTW_CONFIG4			0x005a		/* 1 byte  */
#define	URTW_CONFIG4_VCOOFF		(1 << 7)
#define	URTW_TESTR			0x005b		/* 1 byte  */
#define	URTW_PSR			0x005e		/* 1 byte  */
#define	URTW_SECURITY			0x005f		/* 1 byte  */
#define	URTW_ANAPARAM2			0x0060		/* 4 byte  */
#define	URTW_8225_ANAPARAM2_ON		(0x860c7312)
#define	URTW_8225_ANAPARAM2_OFF		(0x840dec11)
#define	URTW_8187B_8225_ANAPARAM2_ON	(0x727f3f52)
#define	URTW_8187B_8225_ANAPARAM2_OFF	(0x72003f50)
#define	URTW_BEACON_INTERVAL		0x0070		/* 2 byte  */
#define	URTW_ATIM_WND			0x0072		/* 2 byte  */
#define	URTW_BEACON_INTERVAL_TIME	0x0074		/* 2 byte  */
#define	URTW_ATIM_TR_ITV		0x0076		/* 2 byte  */
#define	URTW_PHY_DELAY			0x0078		/* 1 byte  */
#define	URTW_CARRIER_SCOUNT		0x0079		/* 1 byte  */
#define	URTW_PHY_MAGIC1			0x007c		/* 1 byte  */
#define	URTW_PHY_MAGIC2			0x007d		/* 1 byte  */
#define	URTW_PHY_MAGIC3			0x007e		/* 1 byte  */
#define	URTW_PHY_MAGIC4			0x007f		/* 1 byte  */
#define	URTW_RF_PINS_OUTPUT		0x0080		/* 2 byte  */
#define	URTW_RF_PINS_OUTPUT_MAGIC1	(0x3a0)
#define	URTW_BB_HOST_BANG_CLK		(1 << 1)
#define	URTW_BB_HOST_BANG_EN		(1 << 2)
#define	URTW_BB_HOST_BANG_RW		(1 << 3)
#define	URTW_RF_PINS_ENABLE		0x0082		/* 2 byte  */
#define	URTW_RF_PINS_SELECT		0x0084		/* 2 byte  */
#define	URTW_ADDR_MAGIC1		0x0085		/* broken?  */
#define	URTW_RF_PINS_INPUT		0x0086		/* 2 byte  */
#define	URTW_RF_PINS_MAGIC1		(0xfff3)
#define	URTW_RF_PINS_MAGIC2		(0xfff0)
#define	URTW_RF_PINS_MAGIC3		(0x0007)
#define	URTW_RF_PINS_MAGIC4		(0xf)
#define	URTW_RF_PINS_MAGIC5		(0x0080)
#define	URTW_RF_PARA			0x0088		/* 4 byte  */
#define	URTW_RF_TIMING			0x008c		/* 4 byte  */
#define	URTW_GP_ENABLE			0x0090		/* 1 byte  */
#define	URTW_GP_ENABLE_DATA_MAGIC1	(0x1)
#define	URTW_GPIO			0x0091		/* 1 byte  */
#define	URTW_GPIO_DATA_MAGIC1		(0x1)
#define	URTW_HSSI_PARA			0x0094		/* 4 byte  */
#define	URTW_TX_AGC_CTL			0x009c		/* 1 byte  */
#define	URTW_TX_AGC_CTL_PERPACKET_GAIN	(0x1)
#define	URTW_TX_AGC_CTL_PERPACKET_ANTSEL	(0x2)
#define	URTW_TX_AGC_CTL_FEEDBACK_ANT	(0x4)
#define	URTW_TX_GAIN_CCK		0x009d		/* 1 byte  */
#define	URTW_TX_GAIN_OFDM		0x009e		/* 1 byte  */
#define	URTW_TX_ANTENNA			0x009f		/* 1 byte  */
#define	URTW_WPA_CONFIG			0x00b0		/* 1 byte  */
#define	URTW_SIFS			0x00b4		/* 1 byte  */
#define	URTW_DIFS			0x00b5		/* 1 byte  */
#define	URTW_SLOT			0x00b6		/* 1 byte  */
#define	URTW_CW_CONF			0x00bc		/* 1 byte  */
#define	URTW_CW_CONF_PERPACKET_RETRY	(0x2)
#define	URTW_CW_CONF_PERPACKET_CW	(0x1)
#define	URTW_CW_VAL			0x00bd		/* 1 byte  */
#define	URTW_RATE_FALLBACK		0x00be		/* 1 byte  */
#define	URTW_RATE_FALLBACK_ENABLE	(0x80)
#define	URTW_ACM_CONTROL		0x00bf		/* 1 byte  */
#define	URTW_CONFIG5			0x00d8		/* 1 byte  */
#define	URTW_TXDMA_POLLING		0x00d9		/* 1 byte  */
#define	URTW_CWR			0x00dc		/* 2 byte  */
#define	URTW_RETRY_CTR			0x00de		/* 1 byte  */
#define	URTW_INT_MIG			0x00e2		/* 2 byte  */
#define	URTW_RDSAR			0x00e4		/* 4 byte  */
#define	URTW_TID_AC_MAP			0x00e8		/* 2 byte  */
#define	URTW_ANAPARAM3			0x00ee		/* 1 byte  */
#define	URTW_8187B_8225_ANAPARAM3_ON	(0x0)
#define	URTW_8187B_8225_ANAPARAM3_OFF	(0x0)
#define	URTW_8187B_AC_VO		0x00f0		/* 4 byte for 8187B  */
#define	URTW_FEMR			0x00f4		/* 2 byte  */
#define	URTW_8187B_AC_VI		0x00f4		/* 4 byte for 8187B  */
#define	URTW_8187B_AC_BE		0x00f8		/* 4 byte for 8187B  */
#define	URTW_TALLY_CNT			0x00fa		/* 2 byte  */
#define	URTW_TALLY_SEL			0x00fc		/* 1 byte  */
#define	URTW_8187B_AC_BK		0x00fc		/* 4 byte for 8187B  */
#define	URTW_ADDR_MAGIC2		0x00fe		/* 2 byte  */
#define	URTW_ADDR_MAGIC3		0x00ff		/* 1 byte  */

/* for 8225  */
#define	URTW_8225_ADDR_0_MAGIC		0x0
#define	URTW_8225_ADDR_0_DATA_MAGIC1	(0x1b7)
#define	URTW_8225_ADDR_0_DATA_MAGIC2	(0x0b7)
#define	URTW_8225_ADDR_0_DATA_MAGIC3	(0x127)
#define	URTW_8225_ADDR_0_DATA_MAGIC4	(0x027)
#define	URTW_8225_ADDR_0_DATA_MAGIC5	(0x22f)
#define	URTW_8225_ADDR_0_DATA_MAGIC6	(0x2bf)
#define	URTW_8225_ADDR_1_MAGIC		0x1
#define	URTW_8225_ADDR_2_MAGIC		0x2
#define	URTW_8225_ADDR_2_DATA_MAGIC1	(0xc4d)
#define	URTW_8225_ADDR_2_DATA_MAGIC2	(0x44d)
#define	URTW_8225_ADDR_3_MAGIC		0x3
#define	URTW_8225_ADDR_3_DATA_MAGIC1	(0x2)
#define	URTW_8225_ADDR_5_MAGIC		0x5
#define	URTW_8225_ADDR_5_DATA_MAGIC1	(0x4)
#define	URTW_8225_ADDR_6_MAGIC		0x6
#define	URTW_8225_ADDR_6_DATA_MAGIC1	(0xe6)
#define	URTW_8225_ADDR_6_DATA_MAGIC2	(0x80)
#define	URTW_8225_ADDR_7_MAGIC		0x7
#define	URTW_8225_ADDR_8_MAGIC		0x8
#define	URTW_8225_ADDR_8_DATA_MAGIC1	(0x588)
#define	URTW_8225_ADDR_9_MAGIC		0x9
#define	URTW_8225_ADDR_9_DATA_MAGIC1	(0x700)
#define	URTW_8225_ADDR_C_MAGIC		0xc
#define	URTW_8225_ADDR_C_DATA_MAGIC1	(0x850)
#define	URTW_8225_ADDR_C_DATA_MAGIC2	(0x050)

/* for EEPROM  */
#define	URTW_EPROM_CHANPLAN		0x03
#define	URTW_EPROM_CHANPLAN_BY_HW	(0x80)
#define	URTW_EPROM_TXPW_BASE		0x05
#define	URTW_EPROM_RFCHIPID		0x06
#define	URTW_EPROM_RFCHIPID_RTL8225U	(5)
#define	URTW_EPROM_RFCHIPID_RTL8225Z2	(6)
#define	URTW_EPROM_MACADDR		0x07
#define	URTW_EPROM_TXPW0		0x16 
#define	URTW_EPROM_TXPW2		0x1b
#define	URTW_EPROM_TXPW1		0x3d
#define	URTW_EPROM_SWREV		0x3f
#define	URTW_EPROM_CID_MASK		(0xff)
#define	URTW_EPROM_CID_RSVD0		(0x00)
#define	URTW_EPROM_CID_RSVD1		(0xff)
#define	URTW_EPROM_CID_ALPHA0		(0x01)
#define	URTW_EPROM_CID_SERCOMM_PS	(0x02)
#define	URTW_EPROM_CID_HW_LED		(0x03)

/* LED  */
#define	URTW_CID_DEFAULT		0
#define	URTW_CID_8187_ALPHA0		1
#define	URTW_CID_8187_SERCOMM_PS	2
#define	URTW_CID_8187_HW_LED		3
#define	URTW_SW_LED_MODE0		0
#define	URTW_SW_LED_MODE1		1
#define	URTW_SW_LED_MODE2		2
#define	URTW_SW_LED_MODE3		3
#define	URTW_HW_LED			4
#define	URTW_LED_CTL_POWER_ON		0
#define	URTW_LED_CTL_LINK		2
#define	URTW_LED_CTL_TX			4
#define	URTW_LED_PIN_GPIO0		0
#define	URTW_LED_PIN_LED0		1
#define	URTW_LED_PIN_LED1		2
#define	URTW_LED_UNKNOWN		0
#define	URTW_LED_ON			1
#define	URTW_LED_OFF			2
#define	URTW_LED_BLINK_NORMAL		3
#define	URTW_LED_BLINK_SLOWLY		4
#define	URTW_LED_POWER_ON_BLINK		5
#define	URTW_LED_SCAN_BLINK		6
#define	URTW_LED_NO_LINK_BLINK		7
#define	URTW_LED_BLINK_CM3		8

/* for extra area  */
#define	URTW_EPROM_DISABLE		0
#define	URTW_EPROM_ENABLE		1
#define	URTW_EPROM_DELAY		10
#define	URTW_8187_GETREGS_REQ		5
#define	URTW_8187_SETREGS_REQ		5
#define	URTW_8225_RF_MAX_SENS		6
#define	URTW_8225_RF_DEF_SENS		4
#define	URTW_DEFAULT_RTS_RETRY		7
#define	URTW_DEFAULT_TX_RETRY		7
#define	URTW_DEFAULT_RTS_THRESHOLD	2342U

#define	URTW_ASIFS_TIME			10
#define	URTW_ACKCTS_LEN			14	/* len for ACK and CTS */

struct urtw_8187b_rxhdr {
	uint32_t		flag;
#define	URTW_RX_FLAG_LEN			/*  0 ~ 11 bits */
#define	URTW_RX_FLAG_ICV_ERR	(1 << 12)
#define	URTW_RX_FLAG_CRC32_ERR	(1 << 13)
#define	URTW_RX_FLAG_PM		(1 << 14)
#define	URTW_RX_FLAG_RX_ERR	(1 << 15)
#define	URTW_RX_FLAG_BCAST	(1 << 16)
#define	URTW_RX_FLAG_PAM	(1 << 17)
#define	URTW_RX_FLAG_MCAST	(1 << 18)
#define	URTW_RX_FLAG_QOS	(1 << 19)	/* only for RTL8187B */
#define	URTW_RX_FLAG_RXRATE			/* 20 ~ 23 bits */
#define	URTW_RX_FLAG_RXRATE_SHIFT	20
#define	URTW_RX_FLAG_TRSW	(1 << 24)	/* only for RTL8187B */
#define	URTW_RX_FLAG_SPLCP	(1 << 25)
#define	URTW_RX_FLAG_FOF	(1 << 26)
#define	URTW_RX_FLAG_DMA_FAIL	(1 << 27)
#define	URTW_RX_FLAG_LAST	(1 << 28)
#define	URTW_RX_FLAG_FIRST	(1 << 29)
#define	URTW_RX_FLAG_EOR	(1 << 30)
#define	URTW_RX_FLAG_OWN	(1U << 31)
	uint64_t		mactime;
	uint8_t			noise;
	uint8_t			rssi;
#define	URTW_RX_RSSI				/*  0 ~  6 bits */
#define	URTW_RX_RSSI_MASK	0x3f
#define	URTW_RX_ANTENNA		(1 << 7)
	uint8_t			agc;
	uint8_t			flag2;
#define	URTW_RX_FLAG2_DECRYPTED	(1 << 0)
#define	URTW_RX_FLAG2_WAKUP	(1 << 1)
#define	URTW_RX_FLAG2_SHIFT	(1 << 2)
#define	URTW_RX_FLAG2_RSVD0			/*  3 ~  7 bits */
	uint16_t		flag3;
#define	URTW_RX_FLAG3_NUMMCSI			/*  0 ~  3 bits */
#define	URTW_RX_FLAG3_SNR_L2E			/*  4 ~  9 bits */
#define	URTW_RX_FLAG3_CFO_BIAS			/* 10 ~ 15 bits */
	int8_t			pwdb;
	uint8_t			fot;
} __packed;

struct urtw_8187b_txhdr {
	uint32_t		flag;
#define	URTW_TX_FLAG_PKTLEN			/*  0 ~ 11 bits */
#define	URTW_TX_FLAG_RSVD0			/* 12 ~ 14 bits */
#define	URTW_TX_FLAG_NO_ENC	(1 << 15)
#define	URTW_TX_FLAG_SPLCP	(1 << 16)
#define	URTW_TX_FLAG_MOREFRAG	(1 << 17)
#define	URTW_TX_FLAG_CTS	(1 << 18)
#define	URTW_TX_FLAG_RTSRATE			/* 19 ~ 22 bits */
#define	URTW_TX_FLAG_RTSRATE_SHIFT	19
#define	URTW_TX_FLAG_RTS	(1 << 23)
#define	URTW_TX_FLAG_TXRATE			/* 24 ~ 27 bits */
#define	URTW_TX_FLAG_TXRATE_SHIFT	24
#define	URTW_TX_FLAG_LAST	(1 << 28)
#define	URTW_TX_FLAG_FIRST	(1 << 29)
#define	URTW_TX_FLAG_DMA	(1 << 30)
#define	URTW_TX_FLAG_OWN	(1U << 31)
	uint16_t		rtsdur;
	uint16_t		len;
#define	URTW_TX_LEN				/*  0 ~ 14 bits */
#define	URTW_TX_LEN_EXT		(1 << 15)
	uint32_t		bufaddr;
	uint16_t		flag1;
#define	URTW_TX_FLAG1_RXLEN			/*  0 ~ 11 bits */
#define	URTW_TX_FLAG1_RSVD0			/* 12 ~ 14 bits */
#define	URTW_TX_FLAG1_MICCAL	(1 << 15)
	uint16_t		txdur;
	uint32_t		nextdescaddr;
	uint8_t			rtsagc;
	uint8_t			retry;
	uint16_t		flag2;
#define	URTW_TX_FLAG2_RTDB	(1 << 0)
#define	URTW_TX_FLAG2_NOACM	(1 << 1)
#define	URTW_TX_FLAG2_PIFS	(1 << 2)
#define	URTW_TX_FLAG2_RSVD0			/*  3 ~  6 bits */
#define	URTW_TX_FLAG2_RTSRATEFALLBACK		/*  7 ~ 10 bits */
#define	URTW_TX_FLAG2_RATEFALLBACK		/* 11 ~ 15 bits */
	uint16_t		delaybound;
	uint16_t		flag3;
#define	URTW_TX_FLAG3_RSVD0			/*  0 ~  3 bits */
#define	URTW_TX_FLAG3_AGC			/*  4 ~ 11 bits */
#define	URTW_TX_FLAG3_ANTENNA	(1 << 12)
#define	URTW_TX_FLAG3_SPC			/* 13 ~ 14 bits */
#define	URTW_TX_FLAG3_RSVD1	(1 << 15)
	uint32_t		flag4;
#define	URTW_TX_FLAG4_LENADJUST			/*  0 ~  1 bits */
#define	URTW_TX_FLAG4_RSVD0	(1 << 2)
#define	URTW_TX_FLAG4_TPCDESEN	(1 << 3)
#define	URTW_TX_FLAG4_TPCPOLARITY		/*  4 ~  5 bits */
#define	URTW_TX_FLAG4_TPCEN	(1 << 6)
#define	URTW_TX_FLAG4_PTEN	(1 << 7)
#define	URTW_TX_FLAG4_BCKEY			/*  8 ~ 13 bits */
#define	URTW_TX_FLAG4_ENBCKEY	(1 << 14)
#define	URTW_TX_FLAG4_ENPMPD	(1 << 15)
#define	URTW_TX_FLAG4_FRAGQSZ			/* 16 ~ 31 bits */
} __packed;

struct urtw_8187l_rxhdr {
	uint32_t		flag;
	uint8_t			noise;
	uint8_t			rssi;
#define	URTW_RX_8187L_RSSI			/*  0 ~  6 bits */
#define	URTW_RX_8187L_RSSI_MASK	0x3f
#define	URTW_RX_8187L_ANTENNA	(1 << 7)
	uint8_t			agc;
	uint8_t			flag2;
#define	URTW_RX_8187L_DECRYPTED	(1 << 0)
#define	URTW_RX_8187L_WAKEUP	(1 << 1)
#define	URTW_RX_8187L_SHIFT	(1 << 2)
#define	URTW_RX_8187L_RSVD0			/*  3 ~ 7 bits */
	uint64_t		mactime;
} __packed;

struct urtw_8187l_txhdr {
	uint32_t		flag;
	uint16_t		rtsdur;
	uint16_t		len;
	uint32_t		retry;
} __packed;
