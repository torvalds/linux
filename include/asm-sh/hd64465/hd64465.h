#ifndef _ASM_SH_HD64465_
#define _ASM_SH_HD64465_ 1
/*
 * $Id: hd64465.h,v 1.3 2003/05/04 19:30:15 lethal Exp $
 *
 * Hitachi HD64465 companion chip support
 *
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc.
 *
 * Derived from <asm/hd64461.h> which bore the message:
 * Copyright (C) 2000 YAEGASHI Takeshi
 */
#include <linux/config.h>
#include <asm/io.h>
#include <asm/irq.h>

/*
 * Note that registers are defined here as virtual port numbers,
 * which have no meaning except to get translated by hd64465_isa_port2addr()
 * to an address in the range 0xb0000000-0xb3ffffff.  Note that
 * this translation happens to consist of adding the lower 16 bits
 * of the virtual port number to 0xb0000000.  Note also that the manual
 * shows addresses as absolute physical addresses starting at 0x10000000,
 * so e.g. the NIRR register is listed as 0x15000 here, 0x10005000 in the
 * manual, and accessed using address 0xb0005000 - Greg.
 */

/* System registers */
#define HD64465_REG_SRR     0x1000c 	/* System Revision Register */
#define HD64465_REG_SDID    0x10010 	/* System Device ID Reg */
#define     HD64465_SDID            0x8122  /* 64465 device ID */

/* Power Management registers */
#define HD64465_REG_SMSCR   0x10000 	/* System Module Standby Control Reg */
#define	    HD64465_SMSCR_PS2ST     0x4000  /* PS/2 Standby */
#define	    HD64465_SMSCR_ADCST     0x1000  /* ADC Standby */
#define	    HD64465_SMSCR_UARTST    0x0800  /* UART Standby */
#define	    HD64465_SMSCR_SCDIST    0x0200  /* Serial Codec Standby */
#define	    HD64465_SMSCR_PPST	    0x0100  /* Parallel Port Standby */
#define	    HD64465_SMSCR_PC0ST     0x0040  /* PCMCIA0 Standby */
#define	    HD64465_SMSCR_PC1ST     0x0020  /* PCMCIA1 Standby */
#define	    HD64465_SMSCR_AFEST     0x0010  /* AFE Standby */
#define	    HD64465_SMSCR_TM0ST     0x0008  /* Timer0 Standby */
#define	    HD64465_SMSCR_TM1ST     0x0004  /* Timer1 Standby */
#define	    HD64465_SMSCR_IRDAST    0x0002  /* IRDA Standby */
#define	    HD64465_SMSCR_KBCST     0x0001  /* Keyboard Controller Standby */
 
/* Interrupt Controller registers */
#define HD64465_REG_NIRR    0x15000  	/* Interrupt Request Register */
#define HD64465_REG_NIMR    0x15002  	/* Interrupt Mask Register */
#define HD64465_REG_NITR    0x15004  	/* Interrupt Trigger Mode Register */

/* Timer registers */
#define HD64465_REG_TCVR1   0x16000  	/* Timer 1 constant value register  */
#define HD64465_REG_TCVR0   0x16002	/* Timer 0 constant value register  */
#define HD64465_REG_TRVR1   0x16004	/* Timer 1 read value register  */
#define HD64465_REG_TRVR0   0x16006	/* Timer 0 read value register  */
#define HD64465_REG_TCR1    0x16008	/* Timer 1 control register  */
#define HD64465_REG_TCR0    0x1600A	/* Timer 0 control register  */
#define	    HD64465_TCR_EADT 	0x10	    /* Enable ADTRIG# signal */
#define	    HD64465_TCR_ETMO 	0x08	    /* Enable TMO signal */
#define	    HD64465_TCR_PST_MASK 0x06	    /* Clock Prescale */
#define	    HD64465_TCR_PST_1 	0x06	    /* 1:1 */
#define	    HD64465_TCR_PST_4 	0x04	    /* 1:4 */
#define	    HD64465_TCR_PST_8 	0x02	    /* 1:8 */
#define	    HD64465_TCR_PST_16 	0x00	    /* 1:16 */
#define	    HD64465_TCR_TSTP 	0x01	    /* Start/Stop timer */
#define HD64465_REG_TIRR    0x1600C	/* Timer interrupt request register  */
#define HD64465_REG_TIDR    0x1600E	/* Timer interrupt disable register  */
#define HD64465_REG_PWM1CS  0x16010	/* PWM 1 clock scale register  */
#define HD64465_REG_PWM1LPC 0x16012	/* PWM 1 low pulse width counter register  */
#define HD64465_REG_PWM1HPC 0x16014	/* PWM 1 high pulse width counter register  */
#define HD64465_REG_PWM0CS  0x16018	/* PWM 0 clock scale register  */
#define HD64465_REG_PWM0LPC 0x1601A	/* PWM 0 low pulse width counter register  */
#define HD64465_REG_PWM0HPC 0x1601C	/* PWM 0 high pulse width counter register  */

/* Analog/Digital Converter registers */
#define HD64465_REG_ADDRA   0x1E000	/* A/D data register A */
#define HD64465_REG_ADDRB   0x1E002	/* A/D data register B */
#define HD64465_REG_ADDRC   0x1E004	/* A/D data register C */
#define HD64465_REG_ADDRD   0x1E006	/* A/D data register D */
#define HD64465_REG_ADCSR   0x1E008	/* A/D control/status register */
#define     HD64465_ADCSR_ADF	    0x80    /* A/D End Flag */
#define     HD64465_ADCSR_ADST	    0x40    /* A/D Start Flag */
#define     HD64465_ADCSR_ADIS	    0x20    /* A/D Interrupt Status */
#define     HD64465_ADCSR_TRGE	    0x10    /* A/D Trigger Enable */
#define     HD64465_ADCSR_ADIE	    0x08    /* A/D Interrupt Enable */
#define     HD64465_ADCSR_SCAN	    0x04    /* A/D Scan Mode */
#define     HD64465_ADCSR_CH_MASK   0x03    /* A/D Channel */
#define HD64465_REG_ADCALCR 0x1E00A  	/* A/D calibration sample control */
#define HD64465_REG_ADCAL   0x1E00C  	/* A/D calibration data register */


/* General Purpose I/O ports registers */
#define HD64465_REG_GPACR   0x14000  	/* Port A Control Register */
#define HD64465_REG_GPBCR   0x14002  	/* Port B Control Register */
#define HD64465_REG_GPCCR   0x14004  	/* Port C Control Register */
#define HD64465_REG_GPDCR   0x14006  	/* Port D Control Register */
#define HD64465_REG_GPECR   0x14008  	/* Port E Control Register */
#define HD64465_REG_GPADR   0x14010  	/* Port A Data Register */
#define HD64465_REG_GPBDR   0x14012  	/* Port B Data Register */
#define HD64465_REG_GPCDR   0x14014  	/* Port C Data Register */
#define HD64465_REG_GPDDR   0x14016  	/* Port D Data Register */
#define HD64465_REG_GPEDR   0x14018  	/* Port E Data Register */
#define HD64465_REG_GPAICR  0x14020  	/* Port A Interrupt Control Register */
#define HD64465_REG_GPBICR  0x14022  	/* Port B Interrupt Control Register */
#define HD64465_REG_GPCICR  0x14024  	/* Port C Interrupt Control Register */
#define HD64465_REG_GPDICR  0x14026  	/* Port D Interrupt Control Register */
#define HD64465_REG_GPEICR  0x14028  	/* Port E Interrupt Control Register */
#define HD64465_REG_GPAISR  0x14040  	/* Port A Interrupt Status Register */
#define HD64465_REG_GPBISR  0x14042  	/* Port B Interrupt Status Register */
#define HD64465_REG_GPCISR  0x14044  	/* Port C Interrupt Status Register */
#define HD64465_REG_GPDISR  0x14046  	/* Port D Interrupt Status Register */
#define HD64465_REG_GPEISR  0x14048  	/* Port E Interrupt Status Register */

/* PCMCIA bridge interface */
#define HD64465_REG_PCC0ISR	0x12000	/* socket 0 interface status */ 
#define     HD64465_PCCISR_PREADY   	 0x80    /* mem card ready / io card IREQ */
#define     HD64465_PCCISR_PIREQ    	 0x80
#define     HD64465_PCCISR_PMWP     	 0x40    /* mem card write-protected */
#define     HD64465_PCCISR_PVS2 	 0x20    /* voltage select pin 2 */
#define     HD64465_PCCISR_PVS1 	 0x10    /* voltage select pin 1 */
#define     HD64465_PCCISR_PCD_MASK 	 0x0c    /* card detect */
#define     HD64465_PCCISR_PBVD_MASK     0x03    /* battery voltage */
#define     HD64465_PCCISR_PBVD_BATGOOD  0x03    /* battery good */
#define     HD64465_PCCISR_PBVD_BATWARN  0x01    /* battery low warning */
#define     HD64465_PCCISR_PBVD_BATDEAD1 0x02    /* battery dead */
#define     HD64465_PCCISR_PBVD_BATDEAD2 0x00    /* battery dead */
#define HD64465_REG_PCC0GCR	0x12002	/* socket 0 general control */ 
#define     HD64465_PCCGCR_PDRV   	 0x80    /* output drive */
#define     HD64465_PCCGCR_PCCR   	 0x40    /* PC card reset */
#define     HD64465_PCCGCR_PCCT   	 0x20    /* PC card type, 1=IO&mem, 0=mem */
#define     HD64465_PCCGCR_PVCC0   	 0x10    /* voltage control pin VCC0SEL0 */
#define     HD64465_PCCGCR_PMMOD   	 0x08    /* memory mode */
#define     HD64465_PCCGCR_PPA25   	 0x04    /* pin A25 */
#define     HD64465_PCCGCR_PPA24   	 0x02    /* pin A24 */
#define     HD64465_PCCGCR_PREG   	 0x01    /* ping PCC0REG# */
#define HD64465_REG_PCC0CSCR	0x12004	/* socket 0 card status change */ 
#define     HD64465_PCCCSCR_PSCDI   	 0x80    /* sw card detect intr */
#define     HD64465_PCCCSCR_PSWSEL   	 0x40    /* power select */
#define     HD64465_PCCCSCR_PIREQ   	 0x20    /* IREQ intr req */
#define     HD64465_PCCCSCR_PSC   	 0x10    /* STSCHG (status change) pin */
#define     HD64465_PCCCSCR_PCDC   	 0x08    /* CD (card detect) change */
#define     HD64465_PCCCSCR_PRC   	 0x04    /* ready change */
#define     HD64465_PCCCSCR_PBW   	 0x02    /* battery warning change */
#define     HD64465_PCCCSCR_PBD   	 0x01    /* battery dead change */
#define HD64465_REG_PCC0CSCIER	0x12006	/* socket 0 card status change interrupt enable */ 
#define     HD64465_PCCCSCIER_PCRE   	 0x80    /* change reset enable */
#define     HD64465_PCCCSCIER_PIREQE_MASK   	0x60   /* IREQ enable */
#define     HD64465_PCCCSCIER_PIREQE_DISABLED	0x00   /* IREQ disabled */
#define     HD64465_PCCCSCIER_PIREQE_LEVEL  	0x20   /* IREQ level-triggered */
#define     HD64465_PCCCSCIER_PIREQE_FALLING	0x40   /* IREQ falling-edge-trig */
#define     HD64465_PCCCSCIER_PIREQE_RISING 	0x60   /* IREQ rising-edge-trig */
#define     HD64465_PCCCSCIER_PSCE   	 0x10    /* status change enable */
#define     HD64465_PCCCSCIER_PCDE   	 0x08    /* card detect change enable */
#define     HD64465_PCCCSCIER_PRE   	 0x04    /* ready change enable */
#define     HD64465_PCCCSCIER_PBWE   	 0x02    /* battery warn change enable */
#define     HD64465_PCCCSCIER_PBDE   	 0x01    /* battery dead change enable*/
#define HD64465_REG_PCC0SCR	0x12008	/* socket 0 software control */ 
#define     HD64465_PCCSCR_SHDN   	 0x10    /* TPS2206 SHutDowN pin */
#define     HD64465_PCCSCR_SWP   	 0x01    /* write protect */
#define HD64465_REG_PCCPSR	0x1200A	/* serial power switch control */ 
#define HD64465_REG_PCC1ISR	0x12010	/* socket 1 interface status */ 
#define HD64465_REG_PCC1GCR	0x12012	/* socket 1 general control */ 
#define HD64465_REG_PCC1CSCR	0x12014	/* socket 1 card status change */ 
#define HD64465_REG_PCC1CSCIER	0x12016	/* socket 1 card status change interrupt enable */ 
#define HD64465_REG_PCC1SCR	0x12018	/* socket 1 software control */ 


/* PS/2 Keyboard and mouse controller -- *not* register compatible */
#define HD64465_REG_KBCSR   	0x1dc00 /* Keyboard Control/Status reg */
#define     HD64465_KBCSR_KBCIE   	 0x8000    /* KBCK Input Enable */
#define     HD64465_KBCSR_KBCOE   	 0x4000    /* KBCK Output Enable */
#define     HD64465_KBCSR_KBDOE   	 0x2000    /* KB DATA Output Enable */
#define     HD64465_KBCSR_KBCD   	 0x1000    /* KBCK Driven */
#define     HD64465_KBCSR_KBDD   	 0x0800    /* KB DATA Driven */
#define     HD64465_KBCSR_KBCS   	 0x0400    /* KBCK pin Status */
#define     HD64465_KBCSR_KBDS   	 0x0200    /* KB DATA pin Status */
#define     HD64465_KBCSR_KBDP   	 0x0100    /* KB DATA Parity bit */
#define     HD64465_KBCSR_KBD_MASK   	 0x00ff    /* KD DATA shift reg */
#define HD64465_REG_KBISR   	0x1dc04 /* Keyboard Interrupt Status reg */
#define     HD64465_KBISR_KBRDF   	 0x0001    /* KB Received Data Full */
#define HD64465_REG_MSCSR   	0x1dc10 /* Mouse Control/Status reg */
#define HD64465_REG_MSISR   	0x1dc14 /* Mouse Interrupt Status reg */


/*
 * Logical address at which the HD64465 is mapped.  Note that this
 * should always be in the P2 segment (uncached and untranslated).
 */
#ifndef CONFIG_HD64465_IOBASE
#define CONFIG_HD64465_IOBASE	0xb0000000
#endif
/*
 * The HD64465 multiplexes all its modules' interrupts onto
 * this single interrupt.
 */
#ifndef CONFIG_HD64465_IRQ
#define CONFIG_HD64465_IRQ	5
#endif


#define _HD64465_IO_MASK	0xf8000000
#define is_hd64465_addr(addr) \
	((addr & _HD64465_IO_MASK) == (CONFIG_HD64465_IOBASE & _HD64465_IO_MASK))

/*
 * A range of 16 virtual interrupts generated by
 * demuxing the HD64465 muxed interrupt.
 */
#define HD64465_IRQ_BASE	OFFCHIP_IRQ_BASE
#define HD64465_IRQ_NUM 	16
#define HD64465_IRQ_ADC     	(HD64465_IRQ_BASE+0)
#define HD64465_IRQ_USB     	(HD64465_IRQ_BASE+1)
#define HD64465_IRQ_SCDI    	(HD64465_IRQ_BASE+2)
#define HD64465_IRQ_PARALLEL	(HD64465_IRQ_BASE+3)
/* bit 4 is reserved */
#define HD64465_IRQ_UART    	(HD64465_IRQ_BASE+5)
#define HD64465_IRQ_IRDA    	(HD64465_IRQ_BASE+6)
#define HD64465_IRQ_PS2MOUSE	(HD64465_IRQ_BASE+7)
#define HD64465_IRQ_KBC     	(HD64465_IRQ_BASE+8)
#define HD64465_IRQ_TIMER1   	(HD64465_IRQ_BASE+9)
#define HD64465_IRQ_TIMER0  	(HD64465_IRQ_BASE+10)
#define HD64465_IRQ_GPIO    	(HD64465_IRQ_BASE+11)
#define HD64465_IRQ_AFE     	(HD64465_IRQ_BASE+12)
#define HD64465_IRQ_PCMCIA1 	(HD64465_IRQ_BASE+13)
#define HD64465_IRQ_PCMCIA0 	(HD64465_IRQ_BASE+14)
#define HD64465_IRQ_PS2KBD     	(HD64465_IRQ_BASE+15)

/* Constants for PCMCIA mappings */
#define HD64465_PCC_WINDOW	0x01000000

#define HD64465_PCC0_BASE	0xb8000000	/* area 6 */
#define HD64465_PCC0_ATTR	(HD64465_PCC0_BASE)
#define HD64465_PCC0_COMM	(HD64465_PCC0_BASE+HD64465_PCC_WINDOW)
#define HD64465_PCC0_IO		(HD64465_PCC0_BASE+2*HD64465_PCC_WINDOW)

#define HD64465_PCC1_BASE	0xb4000000	/* area 5 */
#define HD64465_PCC1_ATTR	(HD64465_PCC1_BASE)
#define HD64465_PCC1_COMM	(HD64465_PCC1_BASE+HD64465_PCC_WINDOW)
#define HD64465_PCC1_IO		(HD64465_PCC1_BASE+2*HD64465_PCC_WINDOW)

/*
 * Base of USB controller interface (as memory)
 */
#define HD64465_USB_BASE    	(CONFIG_HD64465_IOBASE+0xb000)
#define HD64465_USB_LEN    	0x1000
/*
 * Base of embedded SRAM, used for USB controller.
 */
#define HD64465_SRAM_BASE    	(CONFIG_HD64465_IOBASE+0x9000)
#define HD64465_SRAM_LEN    	0x1000



#endif /* _ASM_SH_HD64465_  */
