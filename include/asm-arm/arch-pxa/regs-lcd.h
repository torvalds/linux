#ifndef __ASM_ARCH_REGS_LCD_H
#define __ASM_ARCH_REGS_LCD_H
/*
 * LCD Controller Registers and Bits Definitions
 */
#define LCCR0		(0x000)	/* LCD Controller Control Register 0 */
#define LCCR1		(0x004)	/* LCD Controller Control Register 1 */
#define LCCR2		(0x008)	/* LCD Controller Control Register 2 */
#define LCCR3		(0x00C)	/* LCD Controller Control Register 3 */
#define LCCR4		(0x010)	/* LCD Controller Control Register 4 */
#define LCCR5		(0x014)	/* LCD Controller Control Register 5 */
#define DFBR0		(0x020)	/* DMA Channel 0 Frame Branch Register */
#define DFBR1		(0x024)	/* DMA Channel 1 Frame Branch Register */
#define LCSR		(0x038)	/* LCD Controller Status Register */
#define LIIDR		(0x03C)	/* LCD Controller Interrupt ID Register */
#define TMEDRGBR	(0x040)	/* TMED RGB Seed Register */
#define TMEDCR		(0x044)	/* TMED Control Register */

#define CMDCR		(0x100)	/* Command Control Register */
#define PRSR		(0x104)	/* Panel Read Status Register */

#define LCCR3_1BPP	(0 << 24)
#define LCCR3_2BPP	(1 << 24)
#define LCCR3_4BPP	(2 << 24)
#define LCCR3_8BPP	(3 << 24)
#define LCCR3_16BPP	(4 << 24)

#define LCCR3_PDFOR_0	(0 << 30)
#define LCCR3_PDFOR_1	(1 << 30)
#define LCCR3_PDFOR_2	(2 << 30)
#define LCCR3_PDFOR_3	(3 << 30)

#define LCCR4_PAL_FOR_0	(0 << 15)
#define LCCR4_PAL_FOR_1	(1 << 15)
#define LCCR4_PAL_FOR_2	(2 << 15)
#define LCCR4_PAL_FOR_MASK	(3 << 15)

#define FDADR0		(0x200)	/* DMA Channel 0 Frame Descriptor Address Register */
#define FSADR0		(0x204)	/* DMA Channel 0 Frame Source Address Register */
#define FIDR0		(0x208)	/* DMA Channel 0 Frame ID Register */
#define LDCMD0		(0x20C)	/* DMA Channel 0 Command Register */
#define FDADR1		(0x210)	/* DMA Channel 1 Frame Descriptor Address Register */
#define FSADR1		(0x214)	/* DMA Channel 1 Frame Source Address Register */
#define FIDR1		(0x218)	/* DMA Channel 1 Frame ID Register */
#define LDCMD1		(0x21C)	/* DMA Channel 1 Command Register */
#define FDADR6		(0x260) /* DMA Channel 6 Frame Descriptor Address Register */
#define FSADR6		(0x264) /* DMA Channel 6 Frame Source Address Register */
#define FIDR6		(0x268) /* DMA Channel 6 Frame ID Register */

#define LCCR0_ENB	(1 << 0)	/* LCD Controller enable */
#define LCCR0_CMS	(1 << 1)	/* Color/Monochrome Display Select */
#define LCCR0_Color	(LCCR0_CMS*0)	/*  Color display */
#define LCCR0_Mono	(LCCR0_CMS*1)	/*  Monochrome display */
#define LCCR0_SDS	(1 << 2)	/* Single/Dual Panel Display Select */
#define LCCR0_Sngl	(LCCR0_SDS*0)	/*  Single panel display */
#define LCCR0_Dual	(LCCR0_SDS*1)	/*  Dual panel display */

#define LCCR0_LDM	(1 << 3)	/* LCD Disable Done Mask */
#define LCCR0_SFM	(1 << 4)	/* Start of frame mask */
#define LCCR0_IUM	(1 << 5)	/* Input FIFO underrun mask */
#define LCCR0_EFM	(1 << 6)	/* End of Frame mask */
#define LCCR0_PAS	(1 << 7)	/* Passive/Active display Select */
#define LCCR0_Pas	(LCCR0_PAS*0)	/*  Passive display (STN) */
#define LCCR0_Act	(LCCR0_PAS*1)	/*  Active display (TFT) */
#define LCCR0_DPD	(1 << 9)	/* Double Pixel Data (monochrome) */
#define LCCR0_4PixMono	(LCCR0_DPD*0)	/*  4-Pixel/clock Monochrome display */
#define LCCR0_8PixMono	(LCCR0_DPD*1)	/*  8-Pixel/clock Monochrome display */
#define LCCR0_DIS	(1 << 10)	/* LCD Disable */
#define LCCR0_QDM	(1 << 11)	/* LCD Quick Disable mask */
#define LCCR0_PDD	(0xff << 12)	/* Palette DMA request delay */
#define LCCR0_PDD_S	12
#define LCCR0_BM	(1 << 20) 	/* Branch mask */
#define LCCR0_OUM	(1 << 21)	/* Output FIFO underrun mask */
#define LCCR0_LCDT	(1 << 22)	/* LCD panel type */
#define LCCR0_RDSTM	(1 << 23)	/* Read status interrupt mask */
#define LCCR0_CMDIM	(1 << 24)	/* Command interrupt mask */
#define LCCR0_OUC	(1 << 25)	/* Overlay Underlay control bit */
#define LCCR0_LDDALT	(1 << 26)	/* LDD alternate mapping control */

#define LCCR1_PPL	Fld (10, 0)	/* Pixels Per Line - 1 */
#define LCCR1_DisWdth(Pixel)	(((Pixel) - 1) << FShft (LCCR1_PPL))

#define LCCR1_HSW	Fld (6, 10)	/* Horizontal Synchronization */
#define LCCR1_HorSnchWdth(Tpix)	(((Tpix) - 1) << FShft (LCCR1_HSW))

#define LCCR1_ELW	Fld (8, 16)	/* End-of-Line pixel clock Wait - 1 */
#define LCCR1_EndLnDel(Tpix)	(((Tpix) - 1) << FShft (LCCR1_ELW))

#define LCCR1_BLW	Fld (8, 24)	/* Beginning-of-Line pixel clock */
#define LCCR1_BegLnDel(Tpix)	(((Tpix) - 1) << FShft (LCCR1_BLW))

#define LCCR2_LPP	Fld (10, 0)	/* Line Per Panel - 1 */
#define LCCR2_DisHght(Line)	(((Line) - 1) << FShft (LCCR2_LPP))

#define LCCR2_VSW	Fld (6, 10)	/* Vertical Synchronization pulse - 1 */
#define LCCR2_VrtSnchWdth(Tln)	(((Tln) - 1) << FShft (LCCR2_VSW))

#define LCCR2_EFW	Fld (8, 16)	/* End-of-Frame line clock Wait */
#define LCCR2_EndFrmDel(Tln)	((Tln) << FShft (LCCR2_EFW))

#define LCCR2_BFW	Fld (8, 24)	/* Beginning-of-Frame line clock */
#define LCCR2_BegFrmDel(Tln)	((Tln) << FShft (LCCR2_BFW))

#define LCCR3_API	(0xf << 16)	/* AC Bias pin trasitions per interrupt */
#define LCCR3_API_S	16
#define LCCR3_VSP	(1 << 20)	/* vertical sync polarity */
#define LCCR3_HSP	(1 << 21)	/* horizontal sync polarity */
#define LCCR3_PCP	(1 << 22)	/* Pixel Clock Polarity (L_PCLK) */
#define LCCR3_PixRsEdg	(LCCR3_PCP*0)	/*  Pixel clock Rising-Edge */
#define LCCR3_PixFlEdg	(LCCR3_PCP*1)	/*  Pixel clock Falling-Edge */

#define LCCR3_OEP	(1 << 23)	/* Output Enable Polarity */
#define LCCR3_OutEnH	(LCCR3_OEP*0)	/*  Output Enable active High */
#define LCCR3_OutEnL	(LCCR3_OEP*1)	/*  Output Enable active Low */

#define LCCR3_DPC	(1 << 27)	/* double pixel clock mode */
#define LCCR3_PCD	Fld (8, 0)	/* Pixel Clock Divisor */
#define LCCR3_PixClkDiv(Div)	(((Div) << FShft (LCCR3_PCD)))

#define LCCR3_BPP	Fld (3, 24)	/* Bit Per Pixel */
#define LCCR3_Bpp(Bpp)	(((Bpp) << FShft (LCCR3_BPP)))

#define LCCR3_ACB	Fld (8, 8)	/* AC Bias */
#define LCCR3_Acb(Acb)	(((Acb) << FShft (LCCR3_ACB)))

#define LCCR3_HorSnchH	(LCCR3_HSP*0)	/*  HSP Active High */
#define LCCR3_HorSnchL	(LCCR3_HSP*1)	/*  HSP Active Low */

#define LCCR3_VrtSnchH	(LCCR3_VSP*0)	/*  VSP Active High */
#define LCCR3_VrtSnchL	(LCCR3_VSP*1)	/*  VSP Active Low */

#define LCCR5_IUM(x)	(1 << ((x) + 23)) /* input underrun mask */
#define LCCR5_BSM(x)	(1 << ((x) + 15)) /* branch mask */
#define LCCR5_EOFM(x)	(1 << ((x) + 7))  /* end of frame mask */
#define LCCR5_SOFM(x)	(1 << ((x) + 0))  /* start of frame mask */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */
#define LCSR_RD_ST	(1 << 11)	/* read status */
#define LCSR_CMD_INT	(1 << 12)	/* command interrupt */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

/* smartpanel related */
#define PRSR_DATA(x)	((x) & 0xff)	/* Panel Data */
#define PRSR_A0		(1 << 8)	/* Read Data Source */
#define PRSR_ST_OK	(1 << 9)	/* Status OK */
#define PRSR_CON_NT	(1 << 10)	/* Continue to Next Command */

#define SMART_CMD_A0			 (0x1 << 8)
#define SMART_CMD_READ_STATUS_REG	 (0x0 << 9)
#define SMART_CMD_READ_FRAME_BUFFER	((0x0 << 9) | SMART_CMD_A0)
#define SMART_CMD_WRITE_COMMAND		 (0x1 << 9)
#define SMART_CMD_WRITE_DATA		((0x1 << 9) | SMART_CMD_A0)
#define SMART_CMD_WRITE_FRAME		((0x2 << 9) | SMART_CMD_A0)
#define SMART_CMD_WAIT_FOR_VSYNC	 (0x3 << 9)
#define SMART_CMD_NOOP			 (0x4 << 9)
#define SMART_CMD_INTERRUPT		 (0x5 << 9)

#define SMART_CMD(x)	(SMART_CMD_WRITE_COMMAND | ((x) & 0xff))
#define SMART_DAT(x)	(SMART_CMD_WRITE_DATA | ((x) & 0xff))
#endif /* __ASM_ARCH_REGS_LCD_H */
