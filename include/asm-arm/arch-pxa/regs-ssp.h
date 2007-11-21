#ifndef __ASM_ARCH_REGS_SSP_H
#define __ASM_ARCH_REGS_SSP_H

/*
 * SSP Serial Port Registers
 * PXA250, PXA255, PXA26x and PXA27x SSP controllers are all slightly different.
 * PXA255, PXA26x and PXA27x have extra ports, registers and bits.
 */

 /* Common PXA2xx bits first */
#define SSCR0_DSS	(0x0000000f)	/* Data Size Select (mask) */
#define SSCR0_DataSize(x)  ((x) - 1)	/* Data Size Select [4..16] */
#define SSCR0_FRF	(0x00000030)	/* FRame Format (mask) */
#define SSCR0_Motorola	(0x0 << 4)	/* Motorola's Serial Peripheral Interface (SPI) */
#define SSCR0_TI	(0x1 << 4)	/* Texas Instruments' Synchronous Serial Protocol (SSP) */
#define SSCR0_National	(0x2 << 4)	/* National Microwire */
#define SSCR0_ECS	(1 << 6)	/* External clock select */
#define SSCR0_SSE	(1 << 7)	/* Synchronous Serial Port Enable */
#if defined(CONFIG_PXA25x)
#define SSCR0_SCR	(0x0000ff00)	/* Serial Clock Rate (mask) */
#define SSCR0_SerClkDiv(x) ((((x) - 2)/2) << 8) /* Divisor [2..512] */
#elif defined(CONFIG_PXA27x)
#define SSCR0_SCR	(0x000fff00)	/* Serial Clock Rate (mask) */
#define SSCR0_SerClkDiv(x) (((x) - 1) << 8) /* Divisor [1..4096] */
#define SSCR0_EDSS	(1 << 20)	/* Extended data size select */
#define SSCR0_NCS	(1 << 21)	/* Network clock select */
#define SSCR0_RIM	(1 << 22)	/* Receive FIFO overrrun interrupt mask */
#define SSCR0_TUM	(1 << 23)	/* Transmit FIFO underrun interrupt mask */
#define SSCR0_FRDC	(0x07000000)	/* Frame rate divider control (mask) */
#define SSCR0_SlotsPerFrm(x) (((x) - 1) << 24)	/* Time slots per frame [1..8] */
#define SSCR0_ADC	(1 << 30)	/* Audio clock select */
#define SSCR0_MOD	(1 << 31)	/* Mode (normal or network) */
#endif

#define SSCR1_RIE	(1 << 0)	/* Receive FIFO Interrupt Enable */
#define SSCR1_TIE	(1 << 1)	/* Transmit FIFO Interrupt Enable */
#define SSCR1_LBM	(1 << 2)	/* Loop-Back Mode */
#define SSCR1_SPO	(1 << 3)	/* Motorola SPI SSPSCLK polarity setting */
#define SSCR1_SPH	(1 << 4)	/* Motorola SPI SSPSCLK phase setting */
#define SSCR1_MWDS	(1 << 5)	/* Microwire Transmit Data Size */
#define SSCR1_TFT	(0x000003c0)	/* Transmit FIFO Threshold (mask) */
#define SSCR1_TxTresh(x) (((x) - 1) << 6) /* level [1..16] */
#define SSCR1_RFT	(0x00003c00)	/* Receive FIFO Threshold (mask) */
#define SSCR1_RxTresh(x) (((x) - 1) << 10) /* level [1..16] */

#define SSSR_TNF	(1 << 2)	/* Transmit FIFO Not Full */
#define SSSR_RNE	(1 << 3)	/* Receive FIFO Not Empty */
#define SSSR_BSY	(1 << 4)	/* SSP Busy */
#define SSSR_TFS	(1 << 5)	/* Transmit FIFO Service Request */
#define SSSR_RFS	(1 << 6)	/* Receive FIFO Service Request */
#define SSSR_ROR	(1 << 7)	/* Receive FIFO Overrun */

#define SSCR0_TIM		(1 << 23)	/* Transmit FIFO Under Run Interrupt Mask */
#define SSCR0_RIM		(1 << 22)	/* Receive FIFO Over Run interrupt Mask */
#define SSCR0_NCS		(1 << 21)	/* Network Clock Select */
#define SSCR0_EDSS		(1 << 20)	/* Extended Data Size Select */

/* extra bits in PXA255, PXA26x and PXA27x SSP ports */
#define SSCR0_TISSP		(1 << 4)	/* TI Sync Serial Protocol */
#define SSCR0_PSP		(3 << 4)	/* PSP - Programmable Serial Protocol */
#define SSCR1_TTELP		(1 << 31)	/* TXD Tristate Enable Last Phase */
#define SSCR1_TTE		(1 << 30)	/* TXD Tristate Enable */
#define SSCR1_EBCEI		(1 << 29)	/* Enable Bit Count Error interrupt */
#define SSCR1_SCFR		(1 << 28)	/* Slave Clock free Running */
#define SSCR1_ECRA		(1 << 27)	/* Enable Clock Request A */
#define SSCR1_ECRB		(1 << 26)	/* Enable Clock request B */
#define SSCR1_SCLKDIR		(1 << 25)	/* Serial Bit Rate Clock Direction */
#define SSCR1_SFRMDIR		(1 << 24)	/* Frame Direction */
#define SSCR1_RWOT		(1 << 23)	/* Receive Without Transmit */
#define SSCR1_TRAIL		(1 << 22)	/* Trailing Byte */
#define SSCR1_TSRE		(1 << 21)	/* Transmit Service Request Enable */
#define SSCR1_RSRE		(1 << 20)	/* Receive Service Request Enable */
#define SSCR1_TINTE		(1 << 19)	/* Receiver Time-out Interrupt enable */
#define SSCR1_PINTE		(1 << 18)	/* Peripheral Trailing Byte Interupt Enable */
#define SSCR1_STRF		(1 << 15)	/* Select FIFO or EFWR */
#define SSCR1_EFWR		(1 << 14)	/* Enable FIFO Write/Read */

#define SSSR_BCE		(1 << 23)	/* Bit Count Error */
#define SSSR_CSS		(1 << 22)	/* Clock Synchronisation Status */
#define SSSR_TUR		(1 << 21)	/* Transmit FIFO Under Run */
#define SSSR_EOC		(1 << 20)	/* End Of Chain */
#define SSSR_TINT		(1 << 19)	/* Receiver Time-out Interrupt */
#define SSSR_PINT		(1 << 18)	/* Peripheral Trailing Byte Interrupt */

#define SSPSP_FSRT		(1 << 25)	/* Frame Sync Relative Timing */
#define SSPSP_DMYSTOP(x)	((x) << 23)	/* Dummy Stop */
#define SSPSP_SFRMWDTH(x)	((x) << 16)	/* Serial Frame Width */
#define SSPSP_SFRMDLY(x)	((x) << 9)	/* Serial Frame Delay */
#define SSPSP_DMYSTRT(x)	((x) << 7)	/* Dummy Start */
#define SSPSP_STRTDLY(x)	((x) << 4)	/* Start Delay */
#define SSPSP_ETDS		(1 << 3)	/* End of Transfer data State */
#define SSPSP_SFRMP		(1 << 2)	/* Serial Frame Polarity */
#define SSPSP_SCMODE(x)		((x) << 0)	/* Serial Bit Rate Clock Mode */

#define SSACD_SCDB		(1 << 3)	/* SSPSYSCLK Divider Bypass */
#define SSACD_ACPS(x)		((x) << 4)	/* Audio clock PLL select */
#define SSACD_ACDS(x)		((x) << 0)	/* Audio clock divider select */

#define SSCR0_P1	__REG(0x41000000)  /* SSP Port 1 Control Register 0 */
#define SSCR1_P1	__REG(0x41000004)  /* SSP Port 1 Control Register 1 */
#define SSSR_P1		__REG(0x41000008)  /* SSP Port 1 Status Register */
#define SSITR_P1	__REG(0x4100000C)  /* SSP Port 1 Interrupt Test Register */
#define SSDR_P1		__REG(0x41000010)  /* (Write / Read) SSP Port 1 Data Write Register/SSP Data Read Register */

/* Support existing PXA25x drivers */
#define SSCR0		SSCR0_P1  /* SSP Control Register 0 */
#define SSCR1		SSCR1_P1  /* SSP Control Register 1 */
#define SSSR		SSSR_P1	  /* SSP Status Register */
#define SSITR		SSITR_P1  /* SSP Interrupt Test Register */
#define SSDR		SSDR_P1	  /* (Write / Read) SSP Data Write Register/SSP Data Read Register */

/* PXA27x ports */
#if defined (CONFIG_PXA27x)
#define SSTO_P1		__REG(0x41000028)  /* SSP Port 1 Time Out Register */
#define SSPSP_P1	__REG(0x4100002C)  /* SSP Port 1 Programmable Serial Protocol */
#define SSTSA_P1	__REG(0x41000030)  /* SSP Port 1 Tx Timeslot Active */
#define SSRSA_P1	__REG(0x41000034)  /* SSP Port 1 Rx Timeslot Active */
#define SSTSS_P1	__REG(0x41000038)  /* SSP Port 1 Timeslot Status */
#define SSACD_P1	__REG(0x4100003C)  /* SSP Port 1 Audio Clock Divider */
#define SSCR0_P2	__REG(0x41700000)  /* SSP Port 2 Control Register 0 */
#define SSCR1_P2	__REG(0x41700004)  /* SSP Port 2 Control Register 1 */
#define SSSR_P2		__REG(0x41700008)  /* SSP Port 2 Status Register */
#define SSITR_P2	__REG(0x4170000C)  /* SSP Port 2 Interrupt Test Register */
#define SSDR_P2		__REG(0x41700010)  /* (Write / Read) SSP Port 2 Data Write Register/SSP Data Read Register */
#define SSTO_P2		__REG(0x41700028)  /* SSP Port 2 Time Out Register */
#define SSPSP_P2	__REG(0x4170002C)  /* SSP Port 2 Programmable Serial Protocol */
#define SSTSA_P2	__REG(0x41700030)  /* SSP Port 2 Tx Timeslot Active */
#define SSRSA_P2	__REG(0x41700034)  /* SSP Port 2 Rx Timeslot Active */
#define SSTSS_P2	__REG(0x41700038)  /* SSP Port 2 Timeslot Status */
#define SSACD_P2	__REG(0x4170003C)  /* SSP Port 2 Audio Clock Divider */
#define SSCR0_P3	__REG(0x41900000)  /* SSP Port 3 Control Register 0 */
#define SSCR1_P3	__REG(0x41900004)  /* SSP Port 3 Control Register 1 */
#define SSSR_P3		__REG(0x41900008)  /* SSP Port 3 Status Register */
#define SSITR_P3	__REG(0x4190000C)  /* SSP Port 3 Interrupt Test Register */
#define SSDR_P3		__REG(0x41900010)  /* (Write / Read) SSP Port 3 Data Write Register/SSP Data Read Register */
#define SSTO_P3		__REG(0x41900028)  /* SSP Port 3 Time Out Register */
#define SSPSP_P3	__REG(0x4190002C)  /* SSP Port 3 Programmable Serial Protocol */
#define SSTSA_P3	__REG(0x41900030)  /* SSP Port 3 Tx Timeslot Active */
#define SSRSA_P3	__REG(0x41900034)  /* SSP Port 3 Rx Timeslot Active */
#define SSTSS_P3	__REG(0x41900038)  /* SSP Port 3 Timeslot Status */
#define SSACD_P3	__REG(0x4190003C)  /* SSP Port 3 Audio Clock Divider */
#else /* PXA255 (only port 2) and PXA26x ports*/
#define SSTO_P1		__REG(0x41000028)  /* SSP Port 1 Time Out Register */
#define SSPSP_P1	__REG(0x4100002C)  /* SSP Port 1 Programmable Serial Protocol */
#define SSCR0_P2	__REG(0x41400000)  /* SSP Port 2 Control Register 0 */
#define SSCR1_P2	__REG(0x41400004)  /* SSP Port 2 Control Register 1 */
#define SSSR_P2		__REG(0x41400008)  /* SSP Port 2 Status Register */
#define SSITR_P2	__REG(0x4140000C)  /* SSP Port 2 Interrupt Test Register */
#define SSDR_P2		__REG(0x41400010)  /* (Write / Read) SSP Port 2 Data Write Register/SSP Data Read Register */
#define SSTO_P2		__REG(0x41400028)  /* SSP Port 2 Time Out Register */
#define SSPSP_P2	__REG(0x4140002C)  /* SSP Port 2 Programmable Serial Protocol */
#define SSCR0_P3	__REG(0x41500000)  /* SSP Port 3 Control Register 0 */
#define SSCR1_P3	__REG(0x41500004)  /* SSP Port 3 Control Register 1 */
#define SSSR_P3		__REG(0x41500008)  /* SSP Port 3 Status Register */
#define SSITR_P3	__REG(0x4150000C)  /* SSP Port 3 Interrupt Test Register */
#define SSDR_P3		__REG(0x41500010)  /* (Write / Read) SSP Port 3 Data Write Register/SSP Data Read Register */
#define SSTO_P3		__REG(0x41500028)  /* SSP Port 3 Time Out Register */
#define SSPSP_P3	__REG(0x4150002C)  /* SSP Port 3 Programmable Serial Protocol */
#endif

#define SSCR0_P(x) (*(((x) == 1) ? &SSCR0_P1 : ((x) == 2) ? &SSCR0_P2 : ((x) == 3) ? &SSCR0_P3 : NULL))
#define SSCR1_P(x) (*(((x) == 1) ? &SSCR1_P1 : ((x) == 2) ? &SSCR1_P2 : ((x) == 3) ? &SSCR1_P3 : NULL))
#define SSSR_P(x) (*(((x) == 1) ? &SSSR_P1 : ((x) == 2) ? &SSSR_P2 : ((x) == 3) ? &SSSR_P3 : NULL))
#define SSITR_P(x) (*(((x) == 1) ? &SSITR_P1 : ((x) == 2) ? &SSITR_P2 : ((x) == 3) ? &SSITR_P3 : NULL))
#define SSDR_P(x) (*(((x) == 1) ? &SSDR_P1 : ((x) == 2) ? &SSDR_P2 : ((x) == 3) ? &SSDR_P3 : NULL))
#define SSTO_P(x) (*(((x) == 1) ? &SSTO_P1 : ((x) == 2) ? &SSTO_P2 : ((x) == 3) ? &SSTO_P3 : NULL))
#define SSPSP_P(x) (*(((x) == 1) ? &SSPSP_P1 : ((x) == 2) ? &SSPSP_P2 : ((x) == 3) ? &SSPSP_P3 : NULL))
#define SSTSA_P(x) (*(((x) == 1) ? &SSTSA_P1 : ((x) == 2) ? &SSTSA_P2 : ((x) == 3) ? &SSTSA_P3 : NULL))
#define SSRSA_P(x) (*(((x) == 1) ? &SSRSA_P1 : ((x) == 2) ? &SSRSA_P2 : ((x) == 3) ? &SSRSA_P3 : NULL))
#define SSTSS_P(x) (*(((x) == 1) ? &SSTSS_P1 : ((x) == 2) ? &SSTSS_P2 : ((x) == 3) ? &SSTSS_P3 : NULL))
#define SSACD_P(x) (*(((x) == 1) ? &SSACD_P1 : ((x) == 2) ? &SSACD_P2 : ((x) == 3) ? &SSACD_P3 : NULL))

#endif /* __ASM_ARCH_REGS_SSP_H */
