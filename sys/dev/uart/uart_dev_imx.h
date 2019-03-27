/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_UART_DEV_IMX5XX_H
#define	_UART_DEV_IMX5XX_H

#define	IMXUART_URXD_REG	0x0000 /* UART Receiver Register */
#define		IMXUART_URXD_CHARRDY		(1 << 15)
#define		IMXUART_URXD_ERR		(1 << 14)
#define		IMXUART_URXD_OVRRUN		(1 << 13)
#define		IMXUART_URXD_FRMERR		(1 << 12)
#define		IMXUART_URXD_BRK		(1 << 11)
#define		IMXUART_URXD_PRERR		(1 << 10)
#define		IMXUART_URXD_RX_DATA_MASK	0xff

#define	IMXUART_UTXD_REG	0x0040 /* UART Transmitter Register */
#define		IMXUART_UTXD_TX_DATA_MASK	0xff

#define	IMXUART_UCR1_REG	0x0080 /* UART Control Register 1 */
#define		IMXUART_UCR1_ADEN		(1 << 15)
#define		IMXUART_UCR1_ADBR		(1 << 14)
#define		IMXUART_UCR1_TRDYEN		(1 << 13)
#define		IMXUART_UCR1_IDEN		(1 << 12)
#define		IMXUART_UCR1_ICD_MASK		(3 << 10)
#define		IMXUART_UCR1_ICD_IDLE4		(0 << 10)
#define		IMXUART_UCR1_ICD_IDLE8		(1 << 10)
#define		IMXUART_UCR1_ICD_IDLE16		(2 << 10)
#define		IMXUART_UCR1_ICD_IDLE32		(3 << 10)
#define		IMXUART_UCR1_RRDYEN		(1 << 9)
#define		IMXUART_UCR1_RXDMAEN		(1 << 8)
#define		IMXUART_UCR1_IREN		(1 << 7)
#define		IMXUART_UCR1_TXMPTYEN		(1 << 6)
#define		IMXUART_UCR1_RTSDEN		(1 << 5)
#define		IMXUART_UCR1_SNDBRK		(1 << 4)
#define		IMXUART_UCR1_TXDMAEN		(1 << 3)
#define		IMXUART_UCR1_ATDMAEN		(1 << 2)
#define		IMXUART_UCR1_DOZE		(1 << 1)
#define		IMXUART_UCR1_UARTEN		(1 << 0)

#define	IMXUART_UCR2_REG	0x0084 /* UART Control Register 2 */
#define		IMXUART_UCR2_ESCI		(1 << 15)
#define		IMXUART_UCR2_IRTS		(1 << 14)
#define		IMXUART_UCR2_CTSC		(1 << 13)
#define		IMXUART_UCR2_CTS		(1 << 12)
#define		IMXUART_UCR2_ESCEN		(1 << 11)
#define		IMXUART_UCR2_RTEC_MASK		(3 << 9)
#define		IMXUART_UCR2_RTEC_REDGE		(0 << 9)
#define		IMXUART_UCR2_RTEC_FEDGE		(1 << 9)
#define		IMXUART_UCR2_RTEC_EDGE		(2 << 9)
#define		IMXUART_UCR2_PREN		(1 << 8)
#define		IMXUART_UCR2_PROE		(1 << 7)
#define		IMXUART_UCR2_STPB		(1 << 6)
#define		IMXUART_UCR2_WS			(1 << 5)
#define		IMXUART_UCR2_RTSEN		(1 << 4)
#define		IMXUART_UCR2_ATEN		(1 << 3)
#define		IMXUART_UCR2_TXEN		(1 << 2)
#define		IMXUART_UCR2_RXEN		(1 << 1)
#define		IMXUART_UCR2_N_SRST		(1 << 0)

#define	IMXUART_UCR3_REG	0x0088 /* UART Control Register 3 */
#define		IMXUART_UCR3_DPEC_MASK		(3 << 14)
#define		IMXUART_UCR3_DPEC_REDGE		(0 << 14)
#define		IMXUART_UCR3_DPEC_FEDGE		(1 << 14)
#define		IMXUART_UCR3_DPEC_EDGE		(2 << 14)
#define		IMXUART_UCR3_DTREN		(1 << 13)
#define		IMXUART_UCR3_PARERREN		(1 << 12)
#define		IMXUART_UCR3_FRAERREN		(1 << 11)
#define		IMXUART_UCR3_DSR		(1 << 10)
#define		IMXUART_UCR3_DCD		(1 << 9)
#define		IMXUART_UCR3_RI			(1 << 8)
#define		IMXUART_UCR3_ADNIMP		(1 << 7)
#define		IMXUART_UCR3_RXDSEN		(1 << 6)
#define		IMXUART_UCR3_AIRINTEN		(1 << 5)
#define		IMXUART_UCR3_AWAKEN		(1 << 4)
#define		IMXUART_UCR3_DTRDEN		(1 << 3)
#define		IMXUART_UCR3_RXDMUXSEL		(1 << 2)
#define		IMXUART_UCR3_INVT		(1 << 1)
#define		IMXUART_UCR3_ACIEN		(1 << 0)

#define	IMXUART_UCR4_REG	0x008c /* UART Control Register 4 */
#define		IMXUART_UCR4_CTSTL_MASK		(0x3f << 10)
#define		IMXUART_UCR4_CTSTL_SHIFT	10
#define		IMXUART_UCR4_INVR		(1 << 9)
#define		IMXUART_UCR4_ENIRI		(1 << 8)
#define		IMXUART_UCR4_WKEN		(1 << 7)
#define		IMXUART_UCR4_IDDMAEN		(1 << 6)
#define		IMXUART_UCR4_IRSC		(1 << 5)
#define		IMXUART_UCR4_LPBYP		(1 << 4)
#define		IMXUART_UCR4_TCEN		(1 << 3)
#define		IMXUART_UCR4_BKEN		(1 << 2)
#define		IMXUART_UCR4_OREN		(1 << 1)
#define		IMXUART_UCR4_DREN		(1 << 0)

#define	IMXUART_UFCR_REG	0x0090 /* UART FIFO Control Register */
#define		IMXUART_UFCR_TXTL_MASK		(0x3f << 10)
#define		IMXUART_UFCR_TXTL_SHIFT		10
#define		IMXUART_UFCR_RFDIV_MASK		(0x07 << 7)
#define		IMXUART_UFCR_RFDIV_SHIFT	7
#define		IMXUART_UFCR_RFDIV_DIV6		(0 << 7)
#define		IMXUART_UFCR_RFDIV_DIV5		(1 << 7)
#define		IMXUART_UFCR_RFDIV_DIV4		(2 << 7)
#define		IMXUART_UFCR_RFDIV_DIV3		(3 << 7)
#define		IMXUART_UFCR_RFDIV_DIV2		(4 << 7)
#define		IMXUART_UFCR_RFDIV_DIV1		(5 << 7)
#define		IMXUART_UFCR_RFDIV_DIV7		(6 << 7)
#define		IMXUART_UFCR_DCEDTE		(1 << 6)
#define		IMXUART_UFCR_RXTL_MASK		0x0000003f
#define		IMXUART_UFCR_RXTL_SHIFT		0

#define	IMXUART_USR1_REG	0x0094 /* UART Status Register 1 */
#define		IMXUART_USR1_PARITYERR		(1 << 15)
#define		IMXUART_USR1_RTSS		(1 << 14)
#define		IMXUART_USR1_TRDY		(1 << 13)
#define		IMXUART_USR1_RTSD		(1 << 12)
#define		IMXUART_USR1_ESCF		(1 << 11)
#define		IMXUART_USR1_FRAMERR		(1 << 10)
#define		IMXUART_USR1_RRDY		(1 << 9)
#define		IMXUART_USR1_AGTIM		(1 << 8)
#define		IMXUART_USR1_DTRD		(1 << 7)
#define		IMXUART_USR1_RXDS		(1 << 6)
#define		IMXUART_USR1_AIRINT		(1 << 5)
#define		IMXUART_USR1_AWAKE		(1 << 4)
/* 6040 5008 XXX */

#define	IMXUART_USR2_REG	0x0098 /* UART Status Register 2 */
#define		IMXUART_USR2_ADET		(1 << 15)
#define		IMXUART_USR2_TXFE		(1 << 14)
#define		IMXUART_USR2_DTRF		(1 << 13)
#define		IMXUART_USR2_IDLE		(1 << 12)
#define		IMXUART_USR2_ACST		(1 << 11)
#define		IMXUART_USR2_RIDELT		(1 << 10)
#define		IMXUART_USR2_RIIN		(1 << 9)
#define		IMXUART_USR2_IRINT		(1 << 8)
#define		IMXUART_USR2_WAKE		(1 << 7)
#define		IMXUART_USR2_DCDDELT		(1 << 6)
#define		IMXUART_USR2_DCDIN		(1 << 5)
#define		IMXUART_USR2_RTSF		(1 << 4)
#define		IMXUART_USR2_TXDC		(1 << 3)
#define		IMXUART_USR2_BRCD		(1 << 2)
#define		IMXUART_USR2_ORE		(1 << 1)
#define		IMXUART_USR2_RDR		(1 << 0)

#define	IMXUART_UESC_REG	0x009c /* UART Escape Character Register */
#define		IMXUART_UESC_ESC_CHAR_MASK	0x000000ff

#define	IMXUART_UTIM_REG	0x00a0 /* UART Escape Timer Register */
#define		IMXUART_UTIM_TIM_MASK		0x00000fff

#define	IMXUART_UBIR_REG	0x00a4 /* UART BRM Incremental Register */
#define		IMXUART_UBIR_INC_MASK		0x0000ffff

#define	IMXUART_UBMR_REG	0x00a8 /* UART BRM Modulator Register */
#define		IMXUART_UBMR_MOD_MASK		0x0000ffff

#define	IMXUART_UBRC_REG	0x00ac /* UART Baud Rate Count Register */
#define		IMXUART_UBRC_BCNT_MASK		0x0000ffff

#define	IMXUART_ONEMS_REG	0x00b0 /* UART One Millisecond Register */
#define		IMXUART_ONEMS_ONEMS_MASK	0x00ffffff

#define	IMXUART_UTS_REG		0x00b4 /* UART Test Register */
#define		IMXUART_UTS_FRCPERR		(1 << 13)
#define		IMXUART_UTS_LOOP		(1 << 12)
#define		IMXUART_UTS_DBGEN		(1 << 11)
#define		IMXUART_UTS_LOOPIR		(1 << 10)
#define		IMXUART_UTS_RXDBG		(1 << 9)
#define		IMXUART_UTS_TXEMPTY		(1 << 6)
#define		IMXUART_UTS_RXEMPTY		(1 << 5)
#define		IMXUART_UTS_TXFULL		(1 << 4)
#define		IMXUART_UTS_RXFULL		(1 << 3)
#define		IMXUART_UTS_SOFTRST		(1 << 0)

#define	REG(_r)		IMXUART_ ## _r ## _REG
#define	FLD(_r, _v)	IMXUART_ ## _r ## _ ## _v

#define	GETREG(bas, reg)						\
		bus_space_read_4((bas)->bst, (bas)->bsh, (reg))
#define	SETREG(bas, reg, value)						\
		bus_space_write_4((bas)->bst, (bas)->bsh, (reg), (value))

#define	CLR(_bas, _r, _b)						\
		SETREG((_bas), (_r), GETREG((_bas), (_r)) & ~(_b))
#define	SET(_bas, _r, _b)						\
		SETREG((_bas), (_r), GETREG((_bas), (_r)) | (_b))
#define	IS_SET(_bas, _r, _b)						\
		((GETREG((_bas), (_r)) & (_b)) ? 1 : 0)

#define	ENA(_bas, _r, _b)	SET((_bas), REG(_r), FLD(_r, _b))
#define	DIS(_bas, _r, _b)	CLR((_bas), REG(_r), FLD(_r, _b))
#define	IS(_bas, _r, _b)	IS_SET((_bas), REG(_r), FLD(_r, _b))


#endif	/* _UART_DEV_IMX5XX_H */
