/*-
 * Hitachi HD64570 serial communications adaptor registers.
 *
 * Copyright (C) 1996 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: hdc64570.h,v 1.1.2.2 2003/11/12 17:31:21 rik Exp $
 * $FreeBSD$
 */

/*
 * Low power mode control register.
 */
#define HD_LPR	      0x00	/* low power register */

/*
 * Interrupt control registers.
 */
#define HD_IVR	      0x1a	/* interrupt vector register */
#define HD_IMVR	      0x1c	/* interrupt modified vector register */
#define HD_ITCR	      0x18	/* interrupt control register */
#define HD_ISR0	      0x10	/* interrupt status register 0, ro */
#define HD_ISR1	      0x11	/* interrupt status register 1, ro */
#define HD_ISR2	      0x12	/* interrupt status register 2, ro */
#define HD_IER0	      0x14	/* interrupt enable register 0 */
#define HD_IER1	      0x15	/* interrupt enable register 1 */
#define HD_IER2	      0x16	/* interrupt enable register 2 */

/*
 * Multiprotocol serial communication interface registers.
 */     
#define HD_MD0_0      0x2e	/* mode register 0 chan 0 */
#define HD_MD0_1      0x4e	/* mode register 0 chan 1 */
#define HD_MD1_0      0x2f	/* mode register 1 chan 0 */
#define HD_MD1_1      0x4f	/* mode register 1 chan 1 */
#define HD_MD2_0      0x30	/* mode register 2 chan 0 */
#define HD_MD2_1      0x50	/* mode register 2 chan 1 */
#define HD_CTL_0      0x31	/* control register chan 0 */
#define HD_CTL_1      0x51	/* control register chan 1 */
#define HD_RXS_0      0x36	/* RX clock source register chan 0 */
#define HD_RXS_1      0x56	/* RX clock source register chan 1 */
#define HD_TXS_0      0x37	/* TX clock source register chan 0 */
#define HD_TXS_1      0x57	/* TX clock source register chan 1 */
#define HD_TMC_0      0x35	/* time constant register chan 0 */
#define HD_TMC_1      0x55	/* time constant register chan 1 */
#define HD_CMD_0      0x2c	/* command register chan 0, wo */
#define HD_CMD_1      0x4c	/* command register chan 1, wo */
#define HD_ST0_0      0x22	/* status register 0 chan 0, ro */
#define HD_ST0_1      0x42	/* status register 0 chan 1, ro */
#define HD_ST1_0      0x23	/* status register 1 chan 0 */
#define HD_ST1_1      0x43	/* status register 1 chan 1 */
#define HD_ST2_0      0x24	/* status register 2 chan 0 */
#define HD_ST2_1      0x44	/* status register 2 chan 1 */
#define HD_ST3_0      0x25	/* status register 3 chan 0, ro */
#define HD_ST3_1      0x45	/* status register 3 chan 1, ro */
#define HD_FST_0      0x26	/* frame status register chan 0 */
#define HD_FST_1      0x46	/* frame status register chan 1 */
#define HD_IE0_0      0x28	/* interrupt enable register 0 chan 0 */
#define HD_IE0_1      0x48	/* interrupt enable register 0 chan 1 */
#define HD_IE1_0      0x29	/* interrupt enable register 1 chan 0 */
#define HD_IE1_1      0x49	/* interrupt enable register 1 chan 1 */
#define HD_IE2_0      0x2a	/* interrupt enable register 2 chan 0 */
#define HD_IE2_1      0x4a	/* interrupt enable register 2 chan 1 */
#define HD_FIE_0      0x2b	/* frame interrupt enable register chan 0 */
#define HD_FIE_1      0x4b	/* frame interrupt enable register chan 1 */
#define HD_SA0_0      0x32	/* sync/address register 0 chan 0 */
#define HD_SA0_1      0x52	/* sync/address register 0 chan 1 */
#define HD_SA1_0      0x33	/* sync/address register 1 chan 0 */
#define HD_SA1_1      0x53	/* sync/address register 1 chan 1 */
#define HD_IDL_0      0x34	/* idle pattern register chan 0 */
#define HD_IDL_1      0x54	/* idle pattern register chan 1 */
#define HD_TRB_0      0x20	/* TX/RX buffer register chan 0 */
#define HD_TRB_1      0x40	/* TX/RX buffer register chan 1 */
#define HD_RRC_0      0x3a	/* RX ready control register chan 0 */
#define HD_RRC_1      0x5a	/* RX ready control register chan 1 */
#define HD_TRC0_0     0x38	/* TX ready control register 0 chan 0 */
#define HD_TRC0_1     0x58	/* TX ready control register 0 chan 1 */
#define HD_TRC1_0     0x39	/* TX ready control register 1 chan 0 */
#define HD_TRC1_1     0x59	/* TX ready control register 1 chan 1 */
#define HD_CST_0      0x3c	/* current status register chan 0 */
#define HD_CST_1      0x5c	/* current status register chan 1 */

/*
 * DMA controller registers.
 */
#define HD_PCR	      0x08	/* DMA priority control register */
#define HD_DMER	      0x09	/* DMA master enable register */

#define HD_DAR_0R     0x80	/* destination address chan 0rx */
#define HD_DAR_0T     0xa0	/* destination address chan 0tx */
#define HD_DAR_1R     0xc0	/* destination address chan 1rx */
#define HD_DAR_1T     0xe0	/* destination address chan 1tx */
#define HD_DARB_0R    0x82	/* destination address B chan 0rx */
#define HD_DARB_0T    0xa2	/* destination address B chan 0tx */
#define HD_DARB_1R    0xc2	/* destination address B chan 1rx */
#define HD_DARB_1T    0xe2	/* destination address B chan 1tx */
#define HD_SAR_0R     0x84	/* source address chan 0rx */
#define HD_SAR_0T     0xa4	/* source address chan 0tx */
#define HD_SAR_1R     0xc4	/* source address chan 1rx */
#define HD_SAR_1T     0xe4	/* source address chan 1tx */
#define HD_SARB_0R    0x86	/* source address B chan 0rx */
#define HD_SARB_0T    0xa6	/* source address B chan 0tx */
#define HD_SARB_1R    0xc6	/* source address B chan 1rx */
#define HD_SARB_1T    0xe6	/* source address B chan 1tx */
#define HD_CDA_0R     0x88	/* current descriptor address chan 0rx */
#define HD_CDA_0T     0xa8	/* current descriptor address chan 0tx */
#define HD_CDA_1R     0xc8	/* current descriptor address chan 1rx */
#define HD_CDA_1T     0xe8	/* current descriptor address chan 1tx */
#define HD_EDA_0R     0x8a	/* error descriptor address chan 0rx */
#define HD_EDA_0T     0xaa	/* error descriptor address chan 0tx */
#define HD_EDA_1R     0xca	/* error descriptor address chan 1rx */
#define HD_EDA_1T     0xea	/* error descriptor address chan 1tx */
#define HD_BFL_0R     0x8c	/* receive buffer length chan 0rx */
#define HD_BFL_1R     0xcc	/* receive buffer length chan 1rx */
#define HD_BCR_0R     0x8e	/* byte count register chan 0rx */
#define HD_BCR_0T     0xae	/* byte count register chan 0tx */
#define HD_BCR_1R     0xce	/* byte count register chan 1rx */
#define HD_BCR_1T     0xee	/* byte count register chan 1tx */
#define HD_DSR_0R     0x90	/* DMA status register chan 0rx */
#define HD_DSR_0T     0xb0	/* DMA status register chan 0tx */
#define HD_DSR_1R     0xd0	/* DMA status register chan 1rx */
#define HD_DSR_1T     0xf0	/* DMA status register chan 1tx */
#define HD_DMR_0R     0x91	/* DMA mode register chan 0rx */
#define HD_DMR_0T     0xb1	/* DMA mode register chan 0tx */
#define HD_DMR_1R     0xd1	/* DMA mode register chan 1rx */
#define HD_DMR_1T     0xf1	/* DMA mode register chan 1tx */
#define HD_FCT_0R     0x93	/* end-of-frame intr counter chan 0rx, ro */
#define HD_FCT_0T     0xb3	/* end-of-frame intr counter chan 0tx, ro */
#define HD_FCT_1R     0xd3	/* end-of-frame intr counter chan 1rx, ro */
#define HD_FCT_1T     0xf3	/* end-of-frame intr counter chan 1tx, ro */
#define HD_DIR_0R     0x94	/* DMA interrupt enable register chan 0rx */
#define HD_DIR_0T     0xb4	/* DMA interrupt enable register chan 0tx */
#define HD_DIR_1R     0xd4	/* DMA interrupt enable register chan 1rx */
#define HD_DIR_1T     0xf4	/* DMA interrupt enable register chan 1tx */
#define HD_DCR_0R     0x95	/* DMA command register chan 0rx, wo */
#define HD_DCR_0T     0xb5	/* DMA command register chan 0tx, wo */
#define HD_DCR_1R     0xd5	/* DMA command register chan 1rx, wo */
#define HD_DCR_1T     0xf5	/* DMA command register chan 1tx, wo */

/*
 * Timer registers.
 */
#define HD_TCNT_0R    0x60	/* timer up counter chan 0rx */
#define HD_TCNT_0T    0x68	/* timer up counter chan 0tx */
#define HD_TCNT_1R    0x70	/* timer up counter chan 1rx */
#define HD_TCNT_1T    0x78	/* timer up counter chan 1tx */
#define HD_TCONR_0R   0x62	/* timer constant register chan 0rx, wo */
#define HD_TCONR_0T   0x6a	/* timer constant register chan 0tx, wo */
#define HD_TCONR_1R   0x72	/* timer constant register chan 1rx, wo */
#define HD_TCONR_1T   0x7a	/* timer constant register chan 1tx, wo */
#define HD_TCSR_0R    0x64	/* timer control/status register chan 0rx */
#define HD_TCSR_0T    0x6c	/* timer control/status register chan 0tx */
#define HD_TCSR_1R    0x74	/* timer control/status register chan 1rx */
#define HD_TCSR_1T    0x7c	/* timer control/status register chan 1tx */
#define HD_TEPR_0R    0x65	/* timer expand prescale register chan 0rx */
#define HD_TEPR_0T    0x6d	/* timer expand prescale register chan 0tx */
#define HD_TEPR_1R    0x75	/* timer expand prescale register chan 1rx */
#define HD_TEPR_1T    0x7d	/* timer expand prescale register chan 1tx */

/*
 * Wait controller registers.
 */
#define HD_PABR0      0x02	/* physical address boundary register 0 */
#define HD_PABR1      0x03	/* physical address boundary register 1 */
#define HD_WCRL	      0x04	/* wait control register L */
#define HD_WCRM	      0x05	/* wait control register M */
#define HD_WCRH	      0x06	/* wait control register H */

/*
 * Interrupt modified vector register (IMVR) bits.
 */
#define IMVR_CHAN1	040	/* channel 1 vector bit */
#define IMVR_VECT_MASK	037	/* interrupt reason mask */

#define IMVR_RX_RDY	004	/* receive buffer ready */
#define IMVR_RX_INT	010	/* receive status */
#define IMVR_RX_DMERR	024	/* receive DMA error */
#define IMVR_RX_DMOK	026	/* receive DMA normal end */
#define IMVR_RX_TIMER	034	/* timer 0/2 count match */

#define IMVR_TX_RDY	006	/* transmit buffer ready */
#define IMVR_TX_INT	012	/* transmit status */
#define IMVR_TX_DMERR	030	/* transmit DMA error */
#define IMVR_TX_DMOK	032	/* transmit DMA normal end */
#define IMVR_TX_TIMER	036	/* timer 1/3 count match */

/*
 * Interrupt control register (ITCR) bits.
 */
#define ITCR_PRIO_DMAC	  0x80	/* DMA priority higher than MSCI */
#define ITCR_CYCLE_VOID	  0x00	/* non-acknowledge cycle */
#define ITCR_CYCLE_SINGLE 0x20	/* single acknowledge cycle */
#define ITCR_CYCLE_DOUBLE 0x40	/* double acknowledge cycle */
#define ITCR_VECT_MOD	  0x10	/* interrupt modified vector flag */

/*
 * Interrupt status register 0 (ISR0) bits.
 */
#define ISR0_RX_RDY_0	0x01	/* channel 0 receiver ready */
#define ISR0_TX_RDY_0	0x02	/* channel 0 transmitter ready */
#define ISR0_RX_INT_0	0x04	/* channel 0 receiver status */
#define ISR0_TX_INT_0	0x08	/* channel 0 transmitter status */
#define ISR0_RX_RDY_1	0x10	/* channel 1 receiver ready */
#define ISR0_TX_RDY_1	0x20	/* channel 1 transmitter ready */
#define ISR0_RX_INT_1	0x40	/* channel 1 receiver status */
#define ISR0_TX_INT_1	0x80	/* channel 1 transmitter status */

/*
 * Interrupt status register 1 (ISR1) bits.
 */
#define ISR1_RX_DMERR_0	0x01	/* channel 0 receive DMA error */
#define ISR1_RX_DMOK_0	0x02	/* channel 0 receive DMA finished */
#define ISR1_TX_DMERR_0	0x04	/* channel 0 transmit DMA error */
#define ISR1_TX_DMOK_0	0x08	/* channel 0 transmit DMA finished */
#define ISR1_RX_DMERR_1	0x10	/* channel 1 receive DMA error */
#define ISR1_RX_DMOK_1	0x20	/* channel 1 receive DMA finished */
#define ISR1_TX_DMERR_1	0x40	/* channel 1 transmit DMA error */
#define ISR1_TX_DMOK_1	0x80	/* channel 1 transmit DMA finished */

/*
 * Interrupt status register 2 (ISR2) bits.
 */
#define ISR2_RX_TIMER_0	0x10	/* channel 0 receive timer */
#define ISR2_TX_TIMER_0	0x20	/* channel 0 transmit timer */
#define ISR2_RX_TIMER_1	0x40	/* channel 1 receive timer */
#define ISR2_TX_TIMER_1	0x80	/* channel 1 transmit timer */

/*
 * Interrupt enable register 0 (IER0) bits.
 */
#define IER0_RX_RDYE_0	0x01	/* channel 0 receiver ready enable */
#define IER0_TX_RDYE_0	0x02	/* channel 0 transmitter ready enable */
#define IER0_RX_INTE_0	0x04	/* channel 0 receiver status enable */
#define IER0_TX_INTE_0	0x08	/* channel 0 transmitter status enable */
#define IER0_RX_RDYE_1	0x10	/* channel 1 receiver ready enable */
#define IER0_TX_RDYE_1	0x20	/* channel 1 transmitter ready enable */
#define IER0_RX_INTE_1	0x40	/* channel 1 receiver status enable */
#define IER0_TX_INTE_1	0x80	/* channel 1 transmitter status enable */
#define IER0_MASK_0     0x0f    /* channel 0 bits */
#define IER0_MASK_1     0xf0    /* channel 1 bits */

/*
 * Interrupt enable register 1 (IER1) bits.
 */
#define IER1_RX_DMERE_0	0x01	/* channel 0 receive DMA error enable */
#define IER1_RX_DME_0	0x02	/* channel 0 receive DMA finished enable */
#define IER1_TX_DMERE_0	0x04	/* channel 0 transmit DMA error enable */
#define IER1_TX_DME_0	0x08	/* channel 0 transmit DMA finished enable */
#define IER1_RX_DMERE_1	0x10	/* channel 1 receive DMA error enable */
#define IER1_RX_DME_1	0x20	/* channel 1 receive DMA finished enable */
#define IER1_TX_DMERE_1	0x40	/* channel 1 transmit DMA error enable */
#define IER1_TX_DME_1	0x80	/* channel 1 transmit DMA finished enable */
#define IER1_MASK_0     0x0f    /* channel 0 bits */
#define IER1_MASK_1     0xf0    /* channel 1 bits */

/*
 * Interrupt enable register 2 (IER2) bits.
 */
#define IER2_RX_TME_0	0x10	/* channel 0 receive timer enable */
#define IER2_TX_TME_0	0x20	/* channel 0 transmit timer enable */
#define IER2_RX_TME_1	0x40	/* channel 1 receive timer enable */
#define IER2_TX_TME_1	0x80	/* channel 1 transmit timer enable */
#define IER2_MASK_0     0x30    /* channel 0 bits */
#define IER2_MASK_1     0xc0    /* channel 1 bits */

/*
 * Control register (CTL) bits.
 */
#define CTL_RTS_INV     0x01    /* RTS control bit (inverted) */
#define CTL_SYNCLD      0x04    /* load SYN characters */
#define CTL_BRK         0x08    /* async: send break */
#define CTL_IDLE_MARK   0       /* HDLC: when idle, transmit mark */
#define CTL_IDLE_PTRN   0x10    /* HDLC: when idle, transmit an idle pattern */
#define CTL_UDRN_ABORT  0       /* HDLC: on underrun - abort */
#define CTL_UDRN_FCS    0x20    /* HDLC: on underrun - send FCS/flag */

/*
 * Command register (CMD) values.
 */
#define	CMD_TX_RESET	001	/* reset: disable, clear buffer/status/BRK */
#define	CMD_TX_ENABLE	002	/* transmitter enable */
#define	CMD_TX_DISABLE	003	/* transmitter disable */
#define	CMD_TX_CRC_INIT	004	/* initialize CRC calculator */
#define	CMD_TX_EOM_CHAR	006	/* set end-of-message char */
#define	CMD_TX_ABORT	007	/* abort transmission (HDLC mode) */
#define	CMD_TX_MPON	010	/* transmit char with MP bit on (async) */
#define	CMD_TX_CLEAR	011	/* clear the transmit buffer */

#define	CMD_RX_RESET	021	/* reset: disable, clear buffer/status */
#define	CMD_RX_ENABLE	022	/* receiver enable */
#define	CMD_RX_DISABLE	023	/* receiver disable */
#define	CMD_RX_CRC_INIT	024	/* initialize CRC calculator */
#define	CMD_RX_REJECT	025	/* reject current message (sync mode) */
#define	CMD_RX_SRCH_MP	026	/* skip all until the char witn MP bit on */

#define	CMD_NOOP	000	/* continue current operation */
#define	CMD_CHAN_RESET	041	/* init registers, disable/clear RX/TX */
#define	CMD_SEARCH_MODE	061	/* set the ADPLL to search mode */

/*
 * Status register 0 (ST0) bits.
 */
#define ST0_RX_RDY	0x01	/* receiver ready */
#define ST0_TX_RDY	0x02	/* transmitter ready */
#define ST0_RX_INT	0x40	/* receiver status interrupt */
#define ST0_TX_INT	0x80	/* transmitter status interrupt */

/*
 * Status register 1 (ST1) bits.
 */
#define ST1_CDCD	0x04	/* carrier changed */
#define ST1_CCTS	0x08	/* CTS changed */
#define ST1_IDL		0x40	/* transmitter idle, ro */

#define ST1_ASYNC_BRKE	0x01	/* break end detected */
#define ST1_ASYNC_BRKD	0x02	/* break start detected */
#define ST1_ASYNC_BITS  "\20\1brke\2brkd\3cdcd\4ccts\7idl"

#define ST1_HDLC_IDLD	0x01	/* idle sequence start detected */
#define ST1_HDLC_ABTD	0x02	/* abort sequence start detected */
#define ST1_HDLC_FLGD	0x10	/* flag detected */
#define ST1_HDLC_UDRN	0x80	/* underrun detected */
#define ST1_HDLC_BITS   "\20\1idld\2abtd\3cdcd\4ccts\5flgd\7idl\10udrn"

/*
 * Status register 2 (ST2) bits.
 */
#define ST2_OVRN	0x08	/* overrun error detected */

#define ST2_ASYNC_FRME	0x10	/* framing error detected */
#define ST2_ASYNC_PE	0x20	/* parity error detected */
#define ST2_ASYNC_PMP	0x40	/* parity/MP bit = 1 */
#define ST2_ASYNC_BITS  "\20\4ovrn\5frme\6pe\7pmp"

#define ST2_HDLC_CRCE	0x04	/* CRC error detected */
#define ST2_HDLC_RBIT	0x10	/* residual bit frame detected */
#define ST2_HDLC_ABT	0x20	/* frame with abort end detected */
#define ST2_HDLC_SHRT	0x40	/* short frame detected */
#define ST2_HDLC_EOM	0x80	/* receive frame end detected */
#define ST2_HDLC_BITS   "\20\3crce\4ovrn\5rbit\6abt\7shrt\10eom"

/*
 * Status register 3 (ST3) bits.
 */
#define ST3_RX_ENABLED	0x01	/* receiver is enabled */
#define ST3_TX_ENABLED	0x02	/* transmitter is enabled */
#define ST3_DCD_INV	0x04	/* DCD input line inverted */
#define ST3_CTS_INV	0x08	/* CTS input line inverted */
#define ST3_ASYNC_BITS  "\20\1rx\2tx\3nodcd\4nocts"

#define ST3_HDLC_SEARCH	0x10	/* ADPLL search mode */
#define ST3_HDLC_TX	0x20	/* channel is transmitting data */
#define ST3_HDLC_BITS   "\20\1rx\2tx\3nodcd\4nocts\5search\6txact"

/*
 * Frame status register (FST) bits, HDLC mode only.
 */
#define FST_CRCE	0x04	/* CRC error detected */
#define FST_OVRN	0x08	/* overrun error detected */
#define FST_RBIT	0x10	/* residual bit frame detected */
#define FST_ABT		0x20	/* frame with abort end detected */
#define FST_SHRT	0x40	/* short frame detected */
#define FST_EOM		0x80	/* frame end flag */

#define FST_EOT		0x01	/* end of transfer, transmit only */

/*
 * Interrupt enable register 0 (IE0) bits.
 */
#define IE0_RX_RDYE	0x01	/* receiver ready interrupt enable */
#define IE0_TX_RDYE	0x02	/* transmitter ready interrupt enable */
#define IE0_RX_INTE	0x40	/* receiver status interrupt enable */
#define IE0_TX_INTE	0x80	/* transmitter status interrupt enable */

/*
 * Interrupt enable register 1 (IE1) bits.
 */
#define IE1_CDCDE	0x04	/* carrier changed */
#define IE1_CCTSE	0x08	/* CTS changed */
#define IE1_IDLE	0x40	/* transmitter idle, ro */

#define IE1_ASYNC_BRKEE	0x01	/* break end detected */
#define IE1_ASYNC_BRKDE	0x02	/* break start detected */

#define IE1_HDLC_IDLDE	0x01	/* idle sequence start detected */
#define IE1_HDLC_ABTDE	0x02	/* abort sequence start detected */
#define IE1_HDLC_FLGDE	0x10	/* flag detected */
#define IE1_HDLC_UDRNE	0x80	/* underrun detected */

/*
 * Interrupt enable register 2 (IE2) bits.
 */
#define IE2_OVRNE	0x08	/* overrun error detected */

#define IE2_ASYNC_FRMEE	0x10	/* framing error detected */
#define IE2_ASYNC_PEE	0x20	/* parity error detected */
#define IE2_ASYNC_PMPE	0x40	/* parity/MP bit = 1 */

#define IE2_HDLC_CRCEE	0x04	/* CRC error detected */
#define IE2_HDLC_RBITE	0x10	/* residual bit frame detected */
#define IE2_HDLC_ABTE	0x20	/* frame with abort end detected */
#define IE2_HDLC_SHRTE	0x40	/* short frame detected */
#define IE2_HDLC_EOME	0x80	/* receive frame end detected */

/*
 * Frame interrupt enable register (FIE) bits, HDLC mode only.
 */
#define FIE_EOMFE	0x80	/* receive frame end detected */

/*
 * Current status register (CST0,CST1) bits.
 * For other bits, see ST2.
 */
#define CST0_CDE	 0x0001	/* data present on top of FIFO */
#define CST1_CDE	 0x0100	/* data present on second stage of FIFO */

/*
 * Receive/transmit clock source register (RXS/TXS) bits.
 */
#define CLK_MASK          0x70  /* RXC/TXC clock input mask */
#define CLK_LINE          0x00  /* RXC/TXC line input */
#define CLK_INT           0x40  /* internal baud rate generator */

#define CLK_RXS_LINE_NS   0x20  /* RXC line with noise suppression */
#define CLK_RXS_DPLL_INT  0x60  /* ADPLL based on internal BRG */
#define CLK_RXS_DPLL_LINE 0x70  /* ADPLL based on RXC line */

#define CLK_TXS_RECV      0x60  /* receive clock */

/*
 * DMA status register (DSR) bits.
 */
#define DSR_DMA_DISABLE	 0x00	/* disable DMA channel */
#define DSR_DMA_ENABLE	 0x02	/* enable DMA channel */
#define DSR_DMA_CONTINUE 0x01	/* do not enable/disable DMA channel */
#define DSR_CHAIN_COF	 0x10	/* counter overflow */
#define DSR_CHAIN_BOF	 0x20	/* buffer overflow/underflow */
#define DSR_CHAIN_EOM	 0x40	/* frame transfer completed */
#define DSR_EOT		 0x80	/* transfer completed */
#define DSR_BITS         "\20\1cont\2enab\5cof\6bof\7eom\10eot"

/*
 * DMA mode register (DMR) bits.
 */
#define DMR_CHAIN_CNTE	0x02	/* enable frame interrupt counter (FCT) */
#define DMR_CHAIN_NF	0x04	/* multi-frame block chain */
#define DMR_TMOD	0x10	/* chained-block transfer mode */

/*
 * DMA interrupt enable register (DIR) bits.
 */
#define DIR_CHAIN_COFE	0x10	/* counter overflow */
#define DIR_CHAIN_BOFE	0x20	/* buffer overflow/underflow */
#define DIR_CHAIN_EOME	0x40	/* frame transfer completed */
#define DIR_EOTE	0x80	/* transfer completed */

/*
 * DMA command register (DCR) values.
 */
#define	DCR_ABORT	1	/* software abort: initialize DMA channel */
#define	DCR_CLEAR	2	/* clear FCT and EOM bit of DSR */

/*
 * DMA master enable register (DME) bits.
 */
#define DME_ENABLE	0x80	/* enable DMA master operation */

/*
 * Timer control/status register (TCSR) bits.
 */
#define TCSR_ENABLE	0x10	/* timer starts incrementing */
#define TCSR_INTR	0x40	/* timer interrupt enable */
#define TCSR_MATCH	0x80	/* TCNT and TCONR are equal */

/*
 * Timer expand prescale register (TEPR) values.
 */
#define TEPR_1		0	/* sysclk/8 */
#define TEPR_2		1	/* sysclk/8/2 */
#define TEPR_4		2	/* sysclk/8/4 */
#define TEPR_8		3	/* sysclk/8/8 */
#define TEPR_16		4	/* sysclk/8/16 */
#define TEPR_32		5	/* sysclk/8/32 */
#define TEPR_64		6	/* sysclk/8/64 */
#define TEPR_128	7	/* sysclk/8/128 */
