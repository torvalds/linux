/*
 * MPC8xx Communication Processor Module.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * This file contains structures and information for the communication
 * processor channels.  Some CPM control and status is available
 * throught the MPC8xx internal memory map.  See immap.h for details.
 * This file only contains what I need for the moment, not the total
 * CPM capabilities.  I (or someone else) will add definitions as they
 * are needed.  -- Dan
 *
 * On the MBX board, EPPC-Bug loads CPM microcode into the first 512
 * bytes of the DP RAM and relocates the I2C parameter area to the
 * IDMA1 space.  The remaining DP RAM is available for buffer descriptors
 * or other use.
 */
#ifndef __CPM_8XX__
#define __CPM_8XX__

#include <asm/8xx_immap.h>
#include <asm/ptrace.h>
#include <asm/cpm.h>

/* CPM Command register.
*/
#define CPM_CR_RST	((ushort)0x8000)
#define CPM_CR_OPCODE	((ushort)0x0f00)
#define CPM_CR_CHAN	((ushort)0x00f0)
#define CPM_CR_FLG	((ushort)0x0001)

/* Some commands (there are more...later)
*/
#define CPM_CR_INIT_TRX		((ushort)0x0000)
#define CPM_CR_INIT_RX		((ushort)0x0001)
#define CPM_CR_INIT_TX		((ushort)0x0002)
#define CPM_CR_HUNT_MODE	((ushort)0x0003)
#define CPM_CR_STOP_TX		((ushort)0x0004)
#define CPM_CR_GRA_STOP_TX	((ushort)0x0005)
#define CPM_CR_RESTART_TX	((ushort)0x0006)
#define CPM_CR_CLOSE_RX_BD	((ushort)0x0007)
#define CPM_CR_SET_GADDR	((ushort)0x0008)
#define CPM_CR_SET_TIMER	CPM_CR_SET_GADDR

/* Channel numbers.
*/
#define CPM_CR_CH_SCC1		((ushort)0x0000)
#define CPM_CR_CH_I2C		((ushort)0x0001)	/* I2C and IDMA1 */
#define CPM_CR_CH_SCC2		((ushort)0x0004)
#define CPM_CR_CH_SPI		((ushort)0x0005)	/* SPI / IDMA2 / Timers */
#define CPM_CR_CH_TIMER		CPM_CR_CH_SPI
#define CPM_CR_CH_SCC3		((ushort)0x0008)
#define CPM_CR_CH_SMC1		((ushort)0x0009)	/* SMC1 / DSP1 */
#define CPM_CR_CH_SCC4		((ushort)0x000c)
#define CPM_CR_CH_SMC2		((ushort)0x000d)	/* SMC2 / DSP2 */

#define mk_cr_cmd(CH, CMD)	((CMD << 8) | (CH << 4))

#ifndef CONFIG_PPC_CPM_NEW_BINDING
/* The dual ported RAM is multi-functional.  Some areas can be (and are
 * being) used for microcode.  There is an area that can only be used
 * as data ram for buffer descriptors, which is all we use right now.
 * Currently the first 512 and last 256 bytes are used for microcode.
 */
#define CPM_DATAONLY_BASE	((uint)0x0800)
#define CPM_DATAONLY_SIZE	((uint)0x0700)
#define CPM_DP_NOSPACE		((uint)0x7fffffff)
#endif

/* Export the base address of the communication processor registers
 * and dual port ram.
 */
extern cpm8xx_t __iomem *cpmp; /* Pointer to comm processor */

#ifdef CONFIG_PPC_CPM_NEW_BINDING
#define cpm_dpalloc cpm_muram_alloc
#define cpm_dpfree cpm_muram_free
#define cpm_dpram_addr cpm_muram_addr
#define cpm_dpram_phys cpm_muram_dma
#else
extern unsigned long cpm_dpalloc(uint size, uint align);
extern int cpm_dpfree(unsigned long offset);
extern unsigned long cpm_dpalloc_fixed(unsigned long offset, uint size, uint align);
extern void cpm_dpdump(void);
extern void *cpm_dpram_addr(unsigned long offset);
extern uint cpm_dpram_phys(u8* addr);
#endif

extern void cpm_setbrg(uint brg, uint rate);

extern uint m8xx_cpm_hostalloc(uint size);
extern int  m8xx_cpm_hostfree(uint start);
extern void m8xx_cpm_hostdump(void);

extern void cpm_load_patch(cpm8xx_t *cp);

/* Buffer descriptors used by many of the CPM protocols.
*/
typedef struct cpm_buf_desc {
	ushort	cbd_sc;		/* Status and Control */
	ushort	cbd_datlen;	/* Data length in buffer */
	uint	cbd_bufaddr;	/* Buffer address in host memory */
} cbd_t;

#define BD_SC_EMPTY	((ushort)0x8000)	/* Receive is empty */
#define BD_SC_READY	((ushort)0x8000)	/* Transmit is ready */
#define BD_SC_WRAP	((ushort)0x2000)	/* Last buffer descriptor */
#define BD_SC_INTRPT	((ushort)0x1000)	/* Interrupt on change */
#define BD_SC_LAST	((ushort)0x0800)	/* Last buffer in frame */
#define BD_SC_TC	((ushort)0x0400)	/* Transmit CRC */
#define BD_SC_CM	((ushort)0x0200)	/* Continous mode */
#define BD_SC_ID	((ushort)0x0100)	/* Rec'd too many idles */
#define BD_SC_P		((ushort)0x0100)	/* xmt preamble */
#define BD_SC_BR	((ushort)0x0020)	/* Break received */
#define BD_SC_FR	((ushort)0x0010)	/* Framing error */
#define BD_SC_PR	((ushort)0x0008)	/* Parity error */
#define BD_SC_NAK	((ushort)0x0004)	/* NAK - did not respond */
#define BD_SC_OV	((ushort)0x0002)	/* Overrun */
#define BD_SC_UN	((ushort)0x0002)	/* Underrun */
#define BD_SC_CD	((ushort)0x0001)	/* ?? */
#define BD_SC_CL	((ushort)0x0001)	/* Collision */

/* Parameter RAM offsets.
*/
#define PROFF_SCC1	((uint)0x0000)
#define PROFF_IIC	((uint)0x0080)
#define PROFF_SCC2	((uint)0x0100)
#define PROFF_SPI	((uint)0x0180)
#define PROFF_SCC3	((uint)0x0200)
#define PROFF_SMC1	((uint)0x0280)
#define PROFF_SCC4	((uint)0x0300)
#define PROFF_SMC2	((uint)0x0380)

/* Define enough so I can at least use the serial port as a UART.
 * The MBX uses SMC1 as the host serial port.
 */
typedef struct smc_uart {
	ushort	smc_rbase;	/* Rx Buffer descriptor base address */
	ushort	smc_tbase;	/* Tx Buffer descriptor base address */
	u_char	smc_rfcr;	/* Rx function code */
	u_char	smc_tfcr;	/* Tx function code */
	ushort	smc_mrblr;	/* Max receive buffer length */
	uint	smc_rstate;	/* Internal */
	uint	smc_idp;	/* Internal */
	ushort	smc_rbptr;	/* Internal */
	ushort	smc_ibc;	/* Internal */
	uint	smc_rxtmp;	/* Internal */
	uint	smc_tstate;	/* Internal */
	uint	smc_tdp;	/* Internal */
	ushort	smc_tbptr;	/* Internal */
	ushort	smc_tbc;	/* Internal */
	uint	smc_txtmp;	/* Internal */
	ushort	smc_maxidl;	/* Maximum idle characters */
	ushort	smc_tmpidl;	/* Temporary idle counter */
	ushort	smc_brklen;	/* Last received break length */
	ushort	smc_brkec;	/* rcv'd break condition counter */
	ushort	smc_brkcr;	/* xmt break count register */
	ushort	smc_rmask;	/* Temporary bit mask */
	char	res1[8];	/* Reserved */
	ushort	smc_rpbase;	/* Relocation pointer */
} smc_uart_t;

/* Function code bits.
*/
#define SMC_EB	((u_char)0x10)	/* Set big endian byte order */

/* SMC uart mode register.
*/
#define	SMCMR_REN	((ushort)0x0001)
#define SMCMR_TEN	((ushort)0x0002)
#define SMCMR_DM	((ushort)0x000c)
#define SMCMR_SM_GCI	((ushort)0x0000)
#define SMCMR_SM_UART	((ushort)0x0020)
#define SMCMR_SM_TRANS	((ushort)0x0030)
#define SMCMR_SM_MASK	((ushort)0x0030)
#define SMCMR_PM_EVEN	((ushort)0x0100)	/* Even parity, else odd */
#define SMCMR_REVD	SMCMR_PM_EVEN
#define SMCMR_PEN	((ushort)0x0200)	/* Parity enable */
#define SMCMR_BS	SMCMR_PEN
#define SMCMR_SL	((ushort)0x0400)	/* Two stops, else one */
#define SMCR_CLEN_MASK	((ushort)0x7800)	/* Character length */
#define smcr_mk_clen(C)	(((C) << 11) & SMCR_CLEN_MASK)

/* SMC2 as Centronics parallel printer.  It is half duplex, in that
 * it can only receive or transmit.  The parameter ram values for
 * each direction are either unique or properly overlap, so we can
 * include them in one structure.
 */
typedef struct smc_centronics {
	ushort	scent_rbase;
	ushort	scent_tbase;
	u_char	scent_cfcr;
	u_char	scent_smask;
	ushort	scent_mrblr;
	uint	scent_rstate;
	uint	scent_r_ptr;
	ushort	scent_rbptr;
	ushort	scent_r_cnt;
	uint	scent_rtemp;
	uint	scent_tstate;
	uint	scent_t_ptr;
	ushort	scent_tbptr;
	ushort	scent_t_cnt;
	uint	scent_ttemp;
	ushort	scent_max_sl;
	ushort	scent_sl_cnt;
	ushort	scent_character1;
	ushort	scent_character2;
	ushort	scent_character3;
	ushort	scent_character4;
	ushort	scent_character5;
	ushort	scent_character6;
	ushort	scent_character7;
	ushort	scent_character8;
	ushort	scent_rccm;
	ushort	scent_rccr;
} smc_cent_t;

/* Centronics Status Mask Register.
*/
#define SMC_CENT_F	((u_char)0x08)
#define SMC_CENT_PE	((u_char)0x04)
#define SMC_CENT_S	((u_char)0x02)

/* SMC Event and Mask register.
*/
#define	SMCM_BRKE	((unsigned char)0x40)	/* When in UART Mode */
#define	SMCM_BRK	((unsigned char)0x10)	/* When in UART Mode */
#define	SMCM_TXE	((unsigned char)0x10)	/* When in Transparent Mode */
#define	SMCM_BSY	((unsigned char)0x04)
#define	SMCM_TX		((unsigned char)0x02)
#define	SMCM_RX		((unsigned char)0x01)

/* Baud rate generators.
*/
#define CPM_BRG_RST		((uint)0x00020000)
#define CPM_BRG_EN		((uint)0x00010000)
#define CPM_BRG_EXTC_INT	((uint)0x00000000)
#define CPM_BRG_EXTC_CLK2	((uint)0x00004000)
#define CPM_BRG_EXTC_CLK6	((uint)0x00008000)
#define CPM_BRG_ATB		((uint)0x00002000)
#define CPM_BRG_CD_MASK		((uint)0x00001ffe)
#define CPM_BRG_DIV16		((uint)0x00000001)

/* SI Clock Route Register
*/
#define SICR_RCLK_SCC1_BRG1	((uint)0x00000000)
#define SICR_TCLK_SCC1_BRG1	((uint)0x00000000)
#define SICR_RCLK_SCC2_BRG2	((uint)0x00000800)
#define SICR_TCLK_SCC2_BRG2	((uint)0x00000100)
#define SICR_RCLK_SCC3_BRG3	((uint)0x00100000)
#define SICR_TCLK_SCC3_BRG3	((uint)0x00020000)
#define SICR_RCLK_SCC4_BRG4	((uint)0x18000000)
#define SICR_TCLK_SCC4_BRG4	((uint)0x03000000)

/* SCCs.
*/
#define SCC_GSMRH_IRP		((uint)0x00040000)
#define SCC_GSMRH_GDE		((uint)0x00010000)
#define SCC_GSMRH_TCRC_CCITT	((uint)0x00008000)
#define SCC_GSMRH_TCRC_BISYNC	((uint)0x00004000)
#define SCC_GSMRH_TCRC_HDLC	((uint)0x00000000)
#define SCC_GSMRH_REVD		((uint)0x00002000)
#define SCC_GSMRH_TRX		((uint)0x00001000)
#define SCC_GSMRH_TTX		((uint)0x00000800)
#define SCC_GSMRH_CDP		((uint)0x00000400)
#define SCC_GSMRH_CTSP		((uint)0x00000200)
#define SCC_GSMRH_CDS		((uint)0x00000100)
#define SCC_GSMRH_CTSS		((uint)0x00000080)
#define SCC_GSMRH_TFL		((uint)0x00000040)
#define SCC_GSMRH_RFW		((uint)0x00000020)
#define SCC_GSMRH_TXSY		((uint)0x00000010)
#define SCC_GSMRH_SYNL16	((uint)0x0000000c)
#define SCC_GSMRH_SYNL8		((uint)0x00000008)
#define SCC_GSMRH_SYNL4		((uint)0x00000004)
#define SCC_GSMRH_RTSM		((uint)0x00000002)
#define SCC_GSMRH_RSYN		((uint)0x00000001)

#define SCC_GSMRL_SIR		((uint)0x80000000)	/* SCC2 only */
#define SCC_GSMRL_EDGE_NONE	((uint)0x60000000)
#define SCC_GSMRL_EDGE_NEG	((uint)0x40000000)
#define SCC_GSMRL_EDGE_POS	((uint)0x20000000)
#define SCC_GSMRL_EDGE_BOTH	((uint)0x00000000)
#define SCC_GSMRL_TCI		((uint)0x10000000)
#define SCC_GSMRL_TSNC_3	((uint)0x0c000000)
#define SCC_GSMRL_TSNC_4	((uint)0x08000000)
#define SCC_GSMRL_TSNC_14	((uint)0x04000000)
#define SCC_GSMRL_TSNC_INF	((uint)0x00000000)
#define SCC_GSMRL_RINV		((uint)0x02000000)
#define SCC_GSMRL_TINV		((uint)0x01000000)
#define SCC_GSMRL_TPL_128	((uint)0x00c00000)
#define SCC_GSMRL_TPL_64	((uint)0x00a00000)
#define SCC_GSMRL_TPL_48	((uint)0x00800000)
#define SCC_GSMRL_TPL_32	((uint)0x00600000)
#define SCC_GSMRL_TPL_16	((uint)0x00400000)
#define SCC_GSMRL_TPL_8		((uint)0x00200000)
#define SCC_GSMRL_TPL_NONE	((uint)0x00000000)
#define SCC_GSMRL_TPP_ALL1	((uint)0x00180000)
#define SCC_GSMRL_TPP_01	((uint)0x00100000)
#define SCC_GSMRL_TPP_10	((uint)0x00080000)
#define SCC_GSMRL_TPP_ZEROS	((uint)0x00000000)
#define SCC_GSMRL_TEND		((uint)0x00040000)
#define SCC_GSMRL_TDCR_32	((uint)0x00030000)
#define SCC_GSMRL_TDCR_16	((uint)0x00020000)
#define SCC_GSMRL_TDCR_8	((uint)0x00010000)
#define SCC_GSMRL_TDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RDCR_32	((uint)0x0000c000)
#define SCC_GSMRL_RDCR_16	((uint)0x00008000)
#define SCC_GSMRL_RDCR_8	((uint)0x00004000)
#define SCC_GSMRL_RDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RENC_DFMAN	((uint)0x00003000)
#define SCC_GSMRL_RENC_MANCH	((uint)0x00002000)
#define SCC_GSMRL_RENC_FM0	((uint)0x00001000)
#define SCC_GSMRL_RENC_NRZI	((uint)0x00000800)
#define SCC_GSMRL_RENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_TENC_DFMAN	((uint)0x00000600)
#define SCC_GSMRL_TENC_MANCH	((uint)0x00000400)
#define SCC_GSMRL_TENC_FM0	((uint)0x00000200)
#define SCC_GSMRL_TENC_NRZI	((uint)0x00000100)
#define SCC_GSMRL_TENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_DIAG_LE	((uint)0x000000c0)	/* Loop and echo */
#define SCC_GSMRL_DIAG_ECHO	((uint)0x00000080)
#define SCC_GSMRL_DIAG_LOOP	((uint)0x00000040)
#define SCC_GSMRL_DIAG_NORM	((uint)0x00000000)
#define SCC_GSMRL_ENR		((uint)0x00000020)
#define SCC_GSMRL_ENT		((uint)0x00000010)
#define SCC_GSMRL_MODE_ENET	((uint)0x0000000c)
#define SCC_GSMRL_MODE_QMC	((uint)0x0000000a)
#define SCC_GSMRL_MODE_DDCMP	((uint)0x00000009)
#define SCC_GSMRL_MODE_BISYNC	((uint)0x00000008)
#define SCC_GSMRL_MODE_V14	((uint)0x00000007)
#define SCC_GSMRL_MODE_AHDLC	((uint)0x00000006)
#define SCC_GSMRL_MODE_PROFIBUS	((uint)0x00000005)
#define SCC_GSMRL_MODE_UART	((uint)0x00000004)
#define SCC_GSMRL_MODE_SS7	((uint)0x00000003)
#define SCC_GSMRL_MODE_ATALK	((uint)0x00000002)
#define SCC_GSMRL_MODE_HDLC	((uint)0x00000000)

#define SCC_TODR_TOD		((ushort)0x8000)

/* SCC Event and Mask register.
*/
#define	SCCM_TXE	((unsigned char)0x10)
#define	SCCM_BSY	((unsigned char)0x04)
#define	SCCM_TX		((unsigned char)0x02)
#define	SCCM_RX		((unsigned char)0x01)

typedef struct scc_param {
	ushort	scc_rbase;	/* Rx Buffer descriptor base address */
	ushort	scc_tbase;	/* Tx Buffer descriptor base address */
	u_char	scc_rfcr;	/* Rx function code */
	u_char	scc_tfcr;	/* Tx function code */
	ushort	scc_mrblr;	/* Max receive buffer length */
	uint	scc_rstate;	/* Internal */
	uint	scc_idp;	/* Internal */
	ushort	scc_rbptr;	/* Internal */
	ushort	scc_ibc;	/* Internal */
	uint	scc_rxtmp;	/* Internal */
	uint	scc_tstate;	/* Internal */
	uint	scc_tdp;	/* Internal */
	ushort	scc_tbptr;	/* Internal */
	ushort	scc_tbc;	/* Internal */
	uint	scc_txtmp;	/* Internal */
	uint	scc_rcrc;	/* Internal */
	uint	scc_tcrc;	/* Internal */
} sccp_t;

/* Function code bits.
*/
#define SCC_EB	((u_char)0x10)	/* Set big endian byte order */

/* CPM Ethernet through SCCx.
 */
typedef struct scc_enet {
	sccp_t	sen_genscc;
	uint	sen_cpres;	/* Preset CRC */
	uint	sen_cmask;	/* Constant mask for CRC */
	uint	sen_crcec;	/* CRC Error counter */
	uint	sen_alec;	/* alignment error counter */
	uint	sen_disfc;	/* discard frame counter */
	ushort	sen_pads;	/* Tx short frame pad character */
	ushort	sen_retlim;	/* Retry limit threshold */
	ushort	sen_retcnt;	/* Retry limit counter */
	ushort	sen_maxflr;	/* maximum frame length register */
	ushort	sen_minflr;	/* minimum frame length register */
	ushort	sen_maxd1;	/* maximum DMA1 length */
	ushort	sen_maxd2;	/* maximum DMA2 length */
	ushort	sen_maxd;	/* Rx max DMA */
	ushort	sen_dmacnt;	/* Rx DMA counter */
	ushort	sen_maxb;	/* Max BD byte count */
	ushort	sen_gaddr1;	/* Group address filter */
	ushort	sen_gaddr2;
	ushort	sen_gaddr3;
	ushort	sen_gaddr4;
	uint	sen_tbuf0data0;	/* Save area 0 - current frame */
	uint	sen_tbuf0data1;	/* Save area 1 - current frame */
	uint	sen_tbuf0rba;	/* Internal */
	uint	sen_tbuf0crc;	/* Internal */
	ushort	sen_tbuf0bcnt;	/* Internal */
	ushort	sen_paddrh;	/* physical address (MSB) */
	ushort	sen_paddrm;
	ushort	sen_paddrl;	/* physical address (LSB) */
	ushort	sen_pper;	/* persistence */
	ushort	sen_rfbdptr;	/* Rx first BD pointer */
	ushort	sen_tfbdptr;	/* Tx first BD pointer */
	ushort	sen_tlbdptr;	/* Tx last BD pointer */
	uint	sen_tbuf1data0;	/* Save area 0 - current frame */
	uint	sen_tbuf1data1;	/* Save area 1 - current frame */
	uint	sen_tbuf1rba;	/* Internal */
	uint	sen_tbuf1crc;	/* Internal */
	ushort	sen_tbuf1bcnt;	/* Internal */
	ushort	sen_txlen;	/* Tx Frame length counter */
	ushort	sen_iaddr1;	/* Individual address filter */
	ushort	sen_iaddr2;
	ushort	sen_iaddr3;
	ushort	sen_iaddr4;
	ushort	sen_boffcnt;	/* Backoff counter */

	/* NOTE: Some versions of the manual have the following items
	 * incorrectly documented.  Below is the proper order.
	 */
	ushort	sen_taddrh;	/* temp address (MSB) */
	ushort	sen_taddrm;
	ushort	sen_taddrl;	/* temp address (LSB) */
} scc_enet_t;

/* SCC Event register as used by Ethernet.
*/
#define SCCE_ENET_GRA	((ushort)0x0080)	/* Graceful stop complete */
#define SCCE_ENET_TXE	((ushort)0x0010)	/* Transmit Error */
#define SCCE_ENET_RXF	((ushort)0x0008)	/* Full frame received */
#define SCCE_ENET_BSY	((ushort)0x0004)	/* All incoming buffers full */
#define SCCE_ENET_TXB	((ushort)0x0002)	/* A buffer was transmitted */
#define SCCE_ENET_RXB	((ushort)0x0001)	/* A buffer was received */

/* SCC Mode Register (PMSR) as used by Ethernet.
*/
#define SCC_PSMR_HBC	((ushort)0x8000)	/* Enable heartbeat */
#define SCC_PSMR_FC	((ushort)0x4000)	/* Force collision */
#define SCC_PSMR_RSH	((ushort)0x2000)	/* Receive short frames */
#define SCC_PSMR_IAM	((ushort)0x1000)	/* Check individual hash */
#define SCC_PSMR_ENCRC	((ushort)0x0800)	/* Ethernet CRC mode */
#define SCC_PSMR_PRO	((ushort)0x0200)	/* Promiscuous mode */
#define SCC_PSMR_BRO	((ushort)0x0100)	/* Catch broadcast pkts */
#define SCC_PSMR_SBT	((ushort)0x0080)	/* Special backoff timer */
#define SCC_PSMR_LPB	((ushort)0x0040)	/* Set Loopback mode */
#define SCC_PSMR_SIP	((ushort)0x0020)	/* Sample Input Pins */
#define SCC_PSMR_LCW	((ushort)0x0010)	/* Late collision window */
#define SCC_PSMR_NIB22	((ushort)0x000a)	/* Start frame search */
#define SCC_PSMR_FDE	((ushort)0x0001)	/* Full duplex enable */

/* Buffer descriptor control/status used by Ethernet receive.
*/
#define BD_ENET_RX_EMPTY	((ushort)0x8000)
#define BD_ENET_RX_WRAP		((ushort)0x2000)
#define BD_ENET_RX_INTR		((ushort)0x1000)
#define BD_ENET_RX_LAST		((ushort)0x0800)
#define BD_ENET_RX_FIRST	((ushort)0x0400)
#define BD_ENET_RX_MISS		((ushort)0x0100)
#define BD_ENET_RX_LG		((ushort)0x0020)
#define BD_ENET_RX_NO		((ushort)0x0010)
#define BD_ENET_RX_SH		((ushort)0x0008)
#define BD_ENET_RX_CR		((ushort)0x0004)
#define BD_ENET_RX_OV		((ushort)0x0002)
#define BD_ENET_RX_CL		((ushort)0x0001)
#define BD_ENET_RX_BC		((ushort)0x0080)	/* DA is Broadcast */
#define BD_ENET_RX_MC		((ushort)0x0040)	/* DA is Multicast */
#define BD_ENET_RX_STATS	((ushort)0x013f)	/* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
*/
#define BD_ENET_TX_READY	((ushort)0x8000)
#define BD_ENET_TX_PAD		((ushort)0x4000)
#define BD_ENET_TX_WRAP		((ushort)0x2000)
#define BD_ENET_TX_INTR		((ushort)0x1000)
#define BD_ENET_TX_LAST		((ushort)0x0800)
#define BD_ENET_TX_TC		((ushort)0x0400)
#define BD_ENET_TX_DEF		((ushort)0x0200)
#define BD_ENET_TX_HB		((ushort)0x0100)
#define BD_ENET_TX_LC		((ushort)0x0080)
#define BD_ENET_TX_RL		((ushort)0x0040)
#define BD_ENET_TX_RCMASK	((ushort)0x003c)
#define BD_ENET_TX_UN		((ushort)0x0002)
#define BD_ENET_TX_CSL		((ushort)0x0001)
#define BD_ENET_TX_STATS	((ushort)0x03ff)	/* All status bits */

/* SCC as UART
*/
typedef struct scc_uart {
	sccp_t	scc_genscc;
	char	res1[8];	/* Reserved */
	ushort	scc_maxidl;	/* Maximum idle chars */
	ushort	scc_idlc;	/* temp idle counter */
	ushort	scc_brkcr;	/* Break count register */
	ushort	scc_parec;	/* receive parity error counter */
	ushort	scc_frmec;	/* receive framing error counter */
	ushort	scc_nosec;	/* receive noise counter */
	ushort	scc_brkec;	/* receive break condition counter */
	ushort	scc_brkln;	/* last received break length */
	ushort	scc_uaddr1;	/* UART address character 1 */
	ushort	scc_uaddr2;	/* UART address character 2 */
	ushort	scc_rtemp;	/* Temp storage */
	ushort	scc_toseq;	/* Transmit out of sequence char */
	ushort	scc_char1;	/* control character 1 */
	ushort	scc_char2;	/* control character 2 */
	ushort	scc_char3;	/* control character 3 */
	ushort	scc_char4;	/* control character 4 */
	ushort	scc_char5;	/* control character 5 */
	ushort	scc_char6;	/* control character 6 */
	ushort	scc_char7;	/* control character 7 */
	ushort	scc_char8;	/* control character 8 */
	ushort	scc_rccm;	/* receive control character mask */
	ushort	scc_rccr;	/* receive control character register */
	ushort	scc_rlbc;	/* receive last break character */
} scc_uart_t;

/* SCC Event and Mask registers when it is used as a UART.
*/
#define UART_SCCM_GLR		((ushort)0x1000)
#define UART_SCCM_GLT		((ushort)0x0800)
#define UART_SCCM_AB		((ushort)0x0200)
#define UART_SCCM_IDL		((ushort)0x0100)
#define UART_SCCM_GRA		((ushort)0x0080)
#define UART_SCCM_BRKE		((ushort)0x0040)
#define UART_SCCM_BRKS		((ushort)0x0020)
#define UART_SCCM_CCR		((ushort)0x0008)
#define UART_SCCM_BSY		((ushort)0x0004)
#define UART_SCCM_TX		((ushort)0x0002)
#define UART_SCCM_RX		((ushort)0x0001)

/* The SCC PMSR when used as a UART.
*/
#define SCU_PSMR_FLC		((ushort)0x8000)
#define SCU_PSMR_SL		((ushort)0x4000)
#define SCU_PSMR_CL		((ushort)0x3000)
#define SCU_PSMR_UM		((ushort)0x0c00)
#define SCU_PSMR_FRZ		((ushort)0x0200)
#define SCU_PSMR_RZS		((ushort)0x0100)
#define SCU_PSMR_SYN		((ushort)0x0080)
#define SCU_PSMR_DRT		((ushort)0x0040)
#define SCU_PSMR_PEN		((ushort)0x0010)
#define SCU_PSMR_RPM		((ushort)0x000c)
#define SCU_PSMR_REVP		((ushort)0x0008)
#define SCU_PSMR_TPM		((ushort)0x0003)
#define SCU_PSMR_TEVP		((ushort)0x0002)

/* CPM Transparent mode SCC.
 */
typedef struct scc_trans {
	sccp_t	st_genscc;
	uint	st_cpres;	/* Preset CRC */
	uint	st_cmask;	/* Constant mask for CRC */
} scc_trans_t;

#define BD_SCC_TX_LAST		((ushort)0x0800)

/* IIC parameter RAM.
*/
typedef struct iic {
	ushort	iic_rbase;	/* Rx Buffer descriptor base address */
	ushort	iic_tbase;	/* Tx Buffer descriptor base address */
	u_char	iic_rfcr;	/* Rx function code */
	u_char	iic_tfcr;	/* Tx function code */
	ushort	iic_mrblr;	/* Max receive buffer length */
	uint	iic_rstate;	/* Internal */
	uint	iic_rdp;	/* Internal */
	ushort	iic_rbptr;	/* Internal */
	ushort	iic_rbc;	/* Internal */
	uint	iic_rxtmp;	/* Internal */
	uint	iic_tstate;	/* Internal */
	uint	iic_tdp;	/* Internal */
	ushort	iic_tbptr;	/* Internal */
	ushort	iic_tbc;	/* Internal */
	uint	iic_txtmp;	/* Internal */
	char	res1[4];	/* Reserved */
	ushort	iic_rpbase;	/* Relocation pointer */
	char	res2[2];	/* Reserved */
} iic_t;

#define BD_IIC_START		((ushort)0x0400)

/* SPI parameter RAM.
*/
typedef struct spi {
	ushort	spi_rbase;	/* Rx Buffer descriptor base address */
	ushort	spi_tbase;	/* Tx Buffer descriptor base address */
	u_char	spi_rfcr;	/* Rx function code */
	u_char	spi_tfcr;	/* Tx function code */
	ushort	spi_mrblr;	/* Max receive buffer length */
	uint	spi_rstate;	/* Internal */
	uint	spi_rdp;	/* Internal */
	ushort	spi_rbptr;	/* Internal */
	ushort	spi_rbc;	/* Internal */
	uint	spi_rxtmp;	/* Internal */
	uint	spi_tstate;	/* Internal */
	uint	spi_tdp;	/* Internal */
	ushort	spi_tbptr;	/* Internal */
	ushort	spi_tbc;	/* Internal */
	uint	spi_txtmp;	/* Internal */
	uint	spi_res;
	ushort	spi_rpbase;	/* Relocation pointer */
	ushort	spi_res2;
} spi_t;

/* SPI Mode register.
*/
#define SPMODE_LOOP	((ushort)0x4000)	/* Loopback */
#define SPMODE_CI	((ushort)0x2000)	/* Clock Invert */
#define SPMODE_CP	((ushort)0x1000)	/* Clock Phase */
#define SPMODE_DIV16	((ushort)0x0800)	/* BRG/16 mode */
#define SPMODE_REV	((ushort)0x0400)	/* Reversed Data */
#define SPMODE_MSTR	((ushort)0x0200)	/* SPI Master */
#define SPMODE_EN	((ushort)0x0100)	/* Enable */
#define SPMODE_LENMSK	((ushort)0x00f0)	/* character length */
#define SPMODE_LEN4	((ushort)0x0030)	/*  4 bits per char */
#define SPMODE_LEN8	((ushort)0x0070)	/*  8 bits per char */
#define SPMODE_LEN16	((ushort)0x00f0)	/* 16 bits per char */
#define SPMODE_PMMSK	((ushort)0x000f)	/* prescale modulus */

/* SPIE fields */
#define SPIE_MME	0x20
#define SPIE_TXE	0x10
#define SPIE_BSY	0x04
#define SPIE_TXB	0x02
#define SPIE_RXB	0x01

/*
 * RISC Controller Configuration Register definitons
 */
#define RCCR_TIME	0x8000			/* RISC Timer Enable */
#define RCCR_TIMEP(t)	(((t) & 0x3F)<<8)	/* RISC Timer Period */
#define RCCR_TIME_MASK	0x00FF			/* not RISC Timer related bits */

/* RISC Timer Parameter RAM offset */
#define PROFF_RTMR	((uint)0x01B0)

typedef struct risc_timer_pram {
	unsigned short	tm_base;	/* RISC Timer Table Base Address */
	unsigned short	tm_ptr;		/* RISC Timer Table Pointer (internal) */
	unsigned short	r_tmr;		/* RISC Timer Mode Register */
	unsigned short	r_tmv;		/* RISC Timer Valid Register */
	unsigned long	tm_cmd;		/* RISC Timer Command Register */
	unsigned long	tm_cnt;		/* RISC Timer Internal Count */
} rt_pram_t;

/* Bits in RISC Timer Command Register */
#define TM_CMD_VALID	0x80000000	/* Valid - Enables the timer */
#define TM_CMD_RESTART	0x40000000	/* Restart - for automatic restart */
#define TM_CMD_PWM	0x20000000	/* Run in Pulse Width Modulation Mode */
#define TM_CMD_NUM(n)	(((n)&0xF)<<16)	/* Timer Number */
#define TM_CMD_PERIOD(p) ((p)&0xFFFF)	/* Timer Period */

/* CPM interrupts.  There are nearly 32 interrupts generated by CPM
 * channels or devices.  All of these are presented to the PPC core
 * as a single interrupt.  The CPM interrupt handler dispatches its
 * own handlers, in a similar fashion to the PPC core handler.  We
 * use the table as defined in the manuals (i.e. no special high
 * priority and SCC1 == SCCa, etc...).
 */
#define CPMVEC_NR		32
#define	CPMVEC_PIO_PC15		((ushort)0x1f)
#define	CPMVEC_SCC1		((ushort)0x1e)
#define	CPMVEC_SCC2		((ushort)0x1d)
#define	CPMVEC_SCC3		((ushort)0x1c)
#define	CPMVEC_SCC4		((ushort)0x1b)
#define	CPMVEC_PIO_PC14		((ushort)0x1a)
#define	CPMVEC_TIMER1		((ushort)0x19)
#define	CPMVEC_PIO_PC13		((ushort)0x18)
#define	CPMVEC_PIO_PC12		((ushort)0x17)
#define	CPMVEC_SDMA_CB_ERR	((ushort)0x16)
#define CPMVEC_IDMA1		((ushort)0x15)
#define CPMVEC_IDMA2		((ushort)0x14)
#define CPMVEC_TIMER2		((ushort)0x12)
#define CPMVEC_RISCTIMER	((ushort)0x11)
#define CPMVEC_I2C		((ushort)0x10)
#define	CPMVEC_PIO_PC11		((ushort)0x0f)
#define	CPMVEC_PIO_PC10		((ushort)0x0e)
#define CPMVEC_TIMER3		((ushort)0x0c)
#define	CPMVEC_PIO_PC9		((ushort)0x0b)
#define	CPMVEC_PIO_PC8		((ushort)0x0a)
#define	CPMVEC_PIO_PC7		((ushort)0x09)
#define CPMVEC_TIMER4		((ushort)0x07)
#define	CPMVEC_PIO_PC6		((ushort)0x06)
#define	CPMVEC_SPI		((ushort)0x05)
#define	CPMVEC_SMC1		((ushort)0x04)
#define	CPMVEC_SMC2		((ushort)0x03)
#define	CPMVEC_PIO_PC5		((ushort)0x02)
#define	CPMVEC_PIO_PC4		((ushort)0x01)
#define	CPMVEC_ERROR		((ushort)0x00)

/* CPM interrupt configuration vector.
*/
#define	CICR_SCD_SCC4		((uint)0x00c00000)	/* SCC4 @ SCCd */
#define	CICR_SCC_SCC3		((uint)0x00200000)	/* SCC3 @ SCCc */
#define	CICR_SCB_SCC2		((uint)0x00040000)	/* SCC2 @ SCCb */
#define	CICR_SCA_SCC1		((uint)0x00000000)	/* SCC1 @ SCCa */
#define CICR_IRL_MASK		((uint)0x0000e000)	/* Core interrrupt */
#define CICR_HP_MASK		((uint)0x00001f00)	/* Hi-pri int. */
#define CICR_IEN		((uint)0x00000080)	/* Int. enable */
#define CICR_SPS		((uint)0x00000001)	/* SCC Spread */

extern void cpm_install_handler(int vec, void (*handler)(void *), void *dev_id);
extern void cpm_free_handler(int vec);

#define IMAP_ADDR		(get_immrbase())

#define CPM_PIN_INPUT     0
#define CPM_PIN_OUTPUT    1
#define CPM_PIN_PRIMARY   0
#define CPM_PIN_SECONDARY 2
#define CPM_PIN_GPIO      4
#define CPM_PIN_OPENDRAIN 8

enum cpm_port {
	CPM_PORTA,
	CPM_PORTB,
	CPM_PORTC,
	CPM_PORTD,
	CPM_PORTE,
};

void cpm1_set_pin(enum cpm_port port, int pin, int flags);

enum cpm_clk_dir {
	CPM_CLK_RX,
	CPM_CLK_TX,
	CPM_CLK_RTX
};

enum cpm_clk_target {
	CPM_CLK_SCC1,
	CPM_CLK_SCC2,
	CPM_CLK_SCC3,
	CPM_CLK_SCC4,
	CPM_CLK_SMC1,
	CPM_CLK_SMC2,
};

enum cpm_clk {
	CPM_BRG1,	/* Baud Rate Generator  1 */
	CPM_BRG2,	/* Baud Rate Generator  2 */
	CPM_BRG3,	/* Baud Rate Generator  3 */
	CPM_BRG4,	/* Baud Rate Generator  4 */
	CPM_CLK1,	/* Clock  1 */
	CPM_CLK2,	/* Clock  2 */
	CPM_CLK3,	/* Clock  3 */
	CPM_CLK4,	/* Clock  4 */
	CPM_CLK5,	/* Clock  5 */
	CPM_CLK6,	/* Clock  6 */
	CPM_CLK7,	/* Clock  7 */
	CPM_CLK8,	/* Clock  8 */
};

int cpm1_clk_setup(enum cpm_clk_target target, int clock, int mode);

#endif /* __CPM_8XX__ */
