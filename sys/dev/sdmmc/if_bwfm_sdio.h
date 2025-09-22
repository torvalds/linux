/*	$OpenBSD: if_bwfm_sdio.h,v 1.3 2020/03/06 09:28:40 patrick Exp $	*/
/*
 * Copyright (c) 2010-2016 Broadcom Corporation
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/* Registers */
#define BWFM_SDIO_CCCR_CARDCAP			0xf0
#define  BWFM_SDIO_CCCR_CARDCAP_CMD14_SUPPORT		(1 << 1)
#define  BWFM_SDIO_CCCR_CARDCAP_CMD14_EXT		(1 << 2)
#define  BWFM_SDIO_CCCR_CARDCAP_CMD_NODEC		(1 << 3)
#define BWFM_SDIO_CCCR_CARDCTRL			0xf1
#define  BWFM_SDIO_CCCR_CARDCTRL_WLANRESET		(1 << 1)
#define BWFM_SDIO_CCCR_SEPINT			0xf2
#define  BWFM_SDIO_CCCR_SEPINT_MASK			0x01
#define  BWFM_SDIO_CCCR_SEPINT_OE			(1 << 1)
#define  BWFM_SDIO_CCCR_SEPINT_ACT_HI			(1 << 2)

#define BWFM_SDIO_WATERMARK			0x10008
#define BWFM_SDIO_DEVICE_CTL			0x10009
#define  BWFM_SDIO_DEVICE_CTL_SETBUSY				0x01
#define  BWFM_SDIO_DEVICE_CTL_SPI_INTR_SYNC			0x02
#define  BWFM_SDIO_DEVICE_CTL_CA_INT_ONLY			0x04
#define  BWFM_SDIO_DEVICE_CTL_PADS_ISO				0x08
#define  BWFM_SDIO_DEVICE_CTL_SB_RST_CTL			0x30
#define  BWFM_SDIO_DEVICE_CTL_RST_CORECTL			0x00
#define  BWFM_SDIO_DEVICE_CTL_RST_BPRESET			0x10
#define  BWFM_SDIO_DEVICE_CTL_RST_NOBPRESET			0x20
#define BWFM_SDIO_FUNC1_SBADDRLOW		0x1000A
#define BWFM_SDIO_FUNC1_SBADDRMID		0x1000B
#define BWFM_SDIO_FUNC1_SBADDRHIGH		0x1000C
#define BWFM_SDIO_FUNC1_CHIPCLKCSR		0x1000E
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_FORCE_ALP			0x01
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_FORCE_HT			0x02
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_FORCE_ILP			0x04
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_ALP_AVAIL_REQ		0x08
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_HT_AVAIL_REQ		0x10
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_FORCE_HW_CLKREQ_OFF		0x20
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_ALP_AVAIL			0x40
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_HT_AVAIL			0x80
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_CSR_MASK			0x1F
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_AVBITS				\
		(BWFM_SDIO_FUNC1_CHIPCLKCSR_HT_AVAIL | \
		 BWFM_SDIO_FUNC1_CHIPCLKCSR_ALP_AVAIL)
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_ALPAV(regval)			\
		((regval) & BWFM_SDIO_FUNC1_CHIPCLKCSR_AVBITS)
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_HTAV(regval)			\
		(((regval) & BWFM_SDIO_FUNC1_CHIPCLKCSR_AVBITS) == BWFM_SDIO_FUNC1_CHIPCLKCSR_AVBITS)
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_ALPONLY(regval)			\
		(BWFM_SDIO_FUNC1_CHIPCLKCSR_ALPAV(regval) && \
		 !BWFM_SDIO_FUNC1_CHIPCLKCSR_HTAV(regval))
#define  BWFM_SDIO_FUNC1_CHIPCLKCSR_CLKAV(regval, alponly) \
		(BWFM_SDIO_FUNC1_CHIPCLKCSR_ALPAV(regval) && \
		 (alponly ? 1 : BWFM_SDIO_FUNC1_CHIPCLKCSR_HTAV(regval)))
#define BWFM_SDIO_FUNC1_SDIOPULLUP		0x1000F
#define BWFM_SDIO_FUNC1_WAKEUPCTRL		0x1001E
#define  BWFM_SDIO_FUNC1_WAKEUPCTRL_HTWAIT		(1 << 1)
#define BWFM_SDIO_FUNC1_SLEEPCSR		0x1001F
#define  BWFM_SDIO_FUNC1_SLEEPCSR_KSO		(1 << 0)
#define  BWFM_SDIO_FUNC1_SLEEPCSR_DEVON		(1 << 1)

#define BWFM_SDIO_SB_OFT_ADDR_PAGE		0x08000
#define BWFM_SDIO_SB_OFT_ADDR_MASK		0x07FFF
#define BWFM_SDIO_SB_ACCESS_2_4B_FLAG		0x08000

/* Protocol defines */
#define SDPCM_PROT_VERSION			4
#define SDPCM_PROT_VERSION_SHIFT		16
#define SDPCM_SHARED_VERSION			0x0003
#define SDPCM_SHARED_VERSION_MASK		0x00FF
#define SDPCM_SHARED_ASSERT_BUILT		0x0100
#define SDPCM_SHARED_ASSERT			0x0200
#define SDPCM_SHARED_TRAP			0x0400

#define SDPCMD_INTSTATUS			0x020
#define  SDPCMD_INTSTATUS_SMB_SW0			(1 << 0) /* To SB Mail S/W interrupt 0 */
#define  SDPCMD_INTSTATUS_SMB_SW1			(1 << 1) /* To SB Mail S/W interrupt 1 */
#define  SDPCMD_INTSTATUS_SMB_SW2			(1 << 2) /* To SB Mail S/W interrupt 2 */
#define  SDPCMD_INTSTATUS_SMB_SW3			(1 << 3) /* To SB Mail S/W interrupt 3 */
#define  SDPCMD_INTSTATUS_SMB_SW_MASK			0x0000000f /* To SB Mail S/W interrupts mask */
#define  SDPCMD_INTSTATUS_SMB_SW_SHIFT			0	 /* To SB Mail S/W interrupts shift */
#define  SDPCMD_INTSTATUS_HMB_SW0			(1 << 4) /* To Host Mail S/W interrupt 0 */
#define  SDPCMD_INTSTATUS_HMB_SW1			(1 << 5) /* To Host Mail S/W interrupt 1 */
#define  SDPCMD_INTSTATUS_HMB_SW2			(1 << 6) /* To Host Mail S/W interrupt 2 */
#define  SDPCMD_INTSTATUS_HMB_SW3			(1 << 7) /* To Host Mail S/W interrupt 3 */
#define  SDPCMD_INTSTATUS_HMB_FC_STATE			SDPCMD_INTSTATUS_HMB_SW0
#define  SDPCMD_INTSTATUS_HMB_FC_CHANGE			SDPCMD_INTSTATUS_HMB_SW1
#define  SDPCMD_INTSTATUS_HMB_FRAME_IND			SDPCMD_INTSTATUS_HMB_SW2
#define  SDPCMD_INTSTATUS_HMB_HOST_INT			SDPCMD_INTSTATUS_HMB_SW3
#define  SDPCMD_INTSTATUS_HMB_SW_MASK			0x000000f0 /* To Host Mail S/W interrupts mask */
#define  SDPCMD_INTSTATUS_HMB_SW_SHIFT			4	 /* To Host Mail S/W interrupts shift */
#define  SDPCMD_INTSTATUS_WR_OOSYNC			(1 << 8) /* Write Frame Out Of Sync */
#define  SDPCMD_INTSTATUS_RD_OOSYNC			(1 << 9) /* Read Frame Out Of Sync */
#define  SDPCMD_INTSTATUS_PC				(1 << 10)/* descriptor error */
#define  SDPCMD_INTSTATUS_PD				(1 << 11)/* data error */
#define  SDPCMD_INTSTATUS_DE				(1 << 12)/* Descriptor protocol Error */
#define  SDPCMD_INTSTATUS_RU				(1 << 13)/* Receive descriptor Underflow */
#define  SDPCMD_INTSTATUS_RO				(1 << 14)/* Receive fifo Overflow */
#define  SDPCMD_INTSTATUS_XU				(1 << 15)/* Transmit fifo Underflow */
#define  SDPCMD_INTSTATUS_RI				(1 << 16)/* Receive Interrupt */
#define  SDPCMD_INTSTATUS_BUSPWR			(1 << 17)/* SDIO Bus Power Change (rev 9) */
#define  SDPCMD_INTSTATUS_XMTDATA_AVAIL			(1 << 23)/* bits in fifo */
#define  SDPCMD_INTSTATUS_XI				(1 << 24)/* Transmit Interrupt */
#define  SDPCMD_INTSTATUS_RF_TERM			(1 << 25)/* Read Frame Terminate */
#define  SDPCMD_INTSTATUS_WF_TERM			(1 << 26)/* Write Frame Terminate */
#define  SDPCMD_INTSTATUS_PCMCIA_XU			(1 << 27)/* PCMCIA Transmit FIFO Underflow */
#define  SDPCMD_INTSTATUS_SBINT				(1 << 28)/* sbintstatus Interrupt */
#define  SDPCMD_INTSTATUS_CHIPACTIVE			(1 << 29)/* chip from doze to active state */
#define  SDPCMD_INTSTATUS_SRESET			(1 << 30)/* CCCR RES interrupt */
#define  SDPCMD_INTSTATUS_IOE2				(1U << 31)/* CCCR IOE2 Bit Changed */
#define  SDPCMD_INTSTATUS_ERRORS			(SDPCMD_INTSTATUS_PC | \
							 SDPCMD_INTSTATUS_PD | \
							 SDPCMD_INTSTATUS_DE | \
							 SDPCMD_INTSTATUS_RU | \
							 SDPCMD_INTSTATUS_RO | \
							 SDPCMD_INTSTATUS_XU)
#define  SDPCMD_INTSTATUS_DMA				(SDPCMD_INTSTATUS_RI | \
							 SDPCMD_INTSTATUS_XI | \
							 SDPCMD_INTSTATUS_ERRORS)
#define SDPCMD_HOSTINTMASK			0x024
#define SDPCMD_INTMASK				0x028
#define SDPCMD_SBINTSTATUS			0x02c
#define SDPCMD_SBINTMASK			0x030
#define SDPCMD_FUNCTINTMASK			0x034
#define SDPCMD_TOSBMAILBOX			0x040
#define  SDPCMD_TOSBMAILBOX_NAK				(1 << 0)
#define  SDPCMD_TOSBMAILBOX_INT_ACK			(1 << 1)
#define  SDPCMD_TOSBMAILBOX_USE_OOB			(1 << 2)
#define  SDPCMD_TOSBMAILBOX_DEV_INT			(1 << 3)
#define SDPCMD_TOHOSTMAILBOX			0x044
#define SDPCMD_TOSBMAILBOXDATA			0x048
#define SDPCMD_TOHOSTMAILBOXDATA		0x04C
#define  SDPCMD_TOHOSTMAILBOXDATA_NAKHANDLED		(1 << 0)
#define  SDPCMD_TOHOSTMAILBOXDATA_DEVREADY		(1 << 1)
#define  SDPCMD_TOHOSTMAILBOXDATA_FC			(1 << 2)
#define  SDPCMD_TOHOSTMAILBOXDATA_FWREADY		(1 << 3)
#define  SDPCMD_TOHOSTMAILBOXDATA_FWHALT		(1 << 4)

struct bwfm_sdio_hwhdr {
	uint16_t frmlen;
	uint16_t cksum;
};

struct bwfm_sdio_hwexthdr {
	uint16_t pktlen;
	uint8_t res0;
	uint8_t flags;
	uint16_t res1;
	uint16_t padlen;
};

struct bwfm_sdio_swhdr {
	uint8_t seqnr;
	uint8_t chanflag; /* channel + flag */
#define BWFM_SDIO_SWHDR_CHANNEL_CONTROL		0x00
#define BWFM_SDIO_SWHDR_CHANNEL_EVENT		0x01
#define BWFM_SDIO_SWHDR_CHANNEL_DATA		0x02
#define BWFM_SDIO_SWHDR_CHANNEL_GLOM		0x03
#define BWFM_SDIO_SWHDR_CHANNEL_TEST		0x0F
#define BWFM_SDIO_SWHDR_CHANNEL_MASK		0x0F
	uint8_t nextlen;
	uint8_t dataoff;
	uint8_t flowctl;
	uint8_t maxseqnr;
	uint16_t res0;
};

struct bwfm_sdio_sdpcm {
	uint32_t flags;
	uint32_t trap_addr;
	uint32_t assert_exp_addr;
	uint32_t assert_file_addr;
	uint32_t assert_line;
	uint32_t console_addr;
	uint32_t msgtrace_addr;
	uint8_t tag[32];
	uint32_t brpt_addr;
};

struct bwfm_sdio_console {
	uint32_t vcons_in;
	uint32_t vcons_out;
	uint32_t log_buf;
	uint32_t log_bufsz;
	uint32_t log_idx;
};
