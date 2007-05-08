#ifndef __ASM_SH_RENESAS_R7780RP_H
#define __ASM_SH_RENESAS_R7780RP_H

/* Box specific addresses.  */
#if defined(CONFIG_SH_R7780MP)
#define PA_BCR          0xa4000000      /* FPGA */
#define PA_SDPOW	(-1)

#define PA_IRLMSK       (PA_BCR+0x0000) /* Interrupt Mask control */
#define PA_IRLMON       (PA_BCR+0x0002) /* Interrupt Status control */
#define PA_IRLPRI1      (PA_BCR+0x0004) /* Interrupt Priorty 1 */
#define PA_IRLPRI2      (PA_BCR+0x0006) /* Interrupt Priorty 2 */
#define PA_IRLPRI3      (PA_BCR+0x0008) /* Interrupt Priorty 3 */
#define PA_IRLPRI4      (PA_BCR+0x000a) /* Interrupt Priorty 4 */
#define PA_RSTCTL       (PA_BCR+0x000c) /* Reset Control */
#define PA_PCIBD        (PA_BCR+0x000e) /* PCI Board detect control */
#define PA_PCICD        (PA_BCR+0x0010) /* PCI Conector detect control */
#define PA_EXTGIO       (PA_BCR+0x0016) /* Extension GPIO Control */
#define PA_IVDRMON      (PA_BCR+0x0018) /* iVDR Moniter control */
#define PA_IVDRCTL      (PA_BCR+0x001a) /* iVDR control */
#define PA_OBLED        (PA_BCR+0x001c) /* On Board LED control */
#define PA_OBSW         (PA_BCR+0x001e) /* On Board Switch control */
#define PA_AUDIOSEL     (PA_BCR+0x0020) /* Sound Interface Select control */
#define PA_EXTPLR       (PA_BCR+0x001e) /* Extention Pin Polarity control */
#define PA_TPCTL        (PA_BCR+0x0100) /* Touch Panel Access control */
#define PA_TPDCKCTL     (PA_BCR+0x0102) /* Touch Panel Access data control */
#define PA_TPCTLCLR     (PA_BCR+0x0104) /* Touch Panel Access control */
#define PA_TPXPOS       (PA_BCR+0x0106) /* Touch Panel X position control */
#define PA_TPYPOS       (PA_BCR+0x0108) /* Touch Panel Y position control */
#define PA_DBSW         (PA_BCR+0x0200) /* Debug Board Switch control */
#define PA_CFCTL        (PA_BCR+0x0300) /* CF Timing control */
#define PA_CFPOW        (PA_BCR+0x0302) /* CF Power control */
#define PA_CFCDINTCLR   (PA_BCR+0x0304) /* CF Insert Interrupt clear */
#define PA_SCSMR0       (PA_BCR+0x0400) /* SCIF0 Serial mode control */
#define PA_SCBRR0       (PA_BCR+0x0404) /* SCIF0 Bit rate control */
#define PA_SCSCR0       (PA_BCR+0x0408) /* SCIF0 Serial control */
#define PA_SCFTDR0      (PA_BCR+0x040c) /* SCIF0 Send FIFO control */
#define PA_SCFSR0       (PA_BCR+0x0410) /* SCIF0 Serial status control */
#define PA_SCFRDR0      (PA_BCR+0x0414) /* SCIF0 Receive FIFO control */
#define PA_SCFCR0       (PA_BCR+0x0418) /* SCIF0 FIFO control */
#define PA_SCTFDR0      (PA_BCR+0x041c) /* SCIF0 Send FIFO data control */
#define PA_SCRFDR0      (PA_BCR+0x0420) /* SCIF0 Receive FIFO data control */
#define PA_SCSPTR0      (PA_BCR+0x0424) /* SCIF0 Serial Port control */
#define PA_SCLSR0       (PA_BCR+0x0428) /* SCIF0 Line Status control */
#define PA_SCRER0       (PA_BCR+0x042c) /* SCIF0 Serial Error control */
#define PA_SCSMR1       (PA_BCR+0x0500) /* SCIF1 Serial mode control */
#define PA_SCBRR1       (PA_BCR+0x0504) /* SCIF1 Bit rate control */
#define PA_SCSCR1       (PA_BCR+0x0508) /* SCIF1 Serial control */
#define PA_SCFTDR1      (PA_BCR+0x050c) /* SCIF1 Send FIFO control */
#define PA_SCFSR1       (PA_BCR+0x0510) /* SCIF1 Serial status control */
#define PA_SCFRDR1      (PA_BCR+0x0514) /* SCIF1 Receive FIFO control */
#define PA_SCFCR1       (PA_BCR+0x0518) /* SCIF1 FIFO control */
#define PA_SCTFDR1      (PA_BCR+0x051c) /* SCIF1 Send FIFO data control */
#define PA_SCRFDR1      (PA_BCR+0x0520) /* SCIF1 Receive FIFO data control */
#define PA_SCSPTR1      (PA_BCR+0x0524) /* SCIF1 Serial Port control */
#define PA_SCLSR1       (PA_BCR+0x0528) /* SCIF1 Line Status control */
#define PA_SCRER1       (PA_BCR+0x052c) /* SCIF1 Serial Error control */
#define PA_ICCR         (PA_BCR+0x0600) /* Serial control */
#define PA_SAR          (PA_BCR+0x0602) /* Serial Slave control */
#define PA_MDR          (PA_BCR+0x0604) /* Serial Mode control */
#define PA_ADR1         (PA_BCR+0x0606) /* Serial Address1 control */
#define PA_DAR1         (PA_BCR+0x0646) /* Serial Data1 control */
#define PA_VERREG       (PA_BCR+0x0700) /* FPGA Version Register */
#define PA_POFF         (PA_BCR+0x0800) /* System Power Off control */
#define PA_PMR          (PA_BCR+0x0900) /*  */

#define IRLCNTR1        (PA_BCR + 0)    /* Interrupt Control Register1 */

#define IRQ_PCISLOT1    65              /* PCI Slot #1 IRQ */
#define IRQ_PCISLOT2    66              /* PCI Slot #2 IRQ */
#define IRQ_PCISLOT3    67              /* PCI Slot #3 IRQ */
#define IRQ_PCISLOT4    68              /* PCI Slot #4 IRQ */
#define IRQ_TP          2               /* Touch Panel IRQ */
#define IRQ_SCI1        3               /* SCI1 IRQ */
#define IRQ_SCI0        4               /* SCI0 IRQ */
#define IRQ_2SERIAL     5               /* Serial IRQ */
#define IRQ_RTC         6               /* RTC A / B IRQ */
#define IRQ_EXTENTION6  7               /* EXT6n IRQ */
#define IRQ_EXTENTION5  8               /* EXT5n IRQ */
#define IRQ_EXTENTION4  9               /* EXT4n IRQ */
#define IRQ_EXTENTION2  10              /* EXT2n IRQ */
#define IRQ_EXTENTION1  11              /* EXT1n IRQ */
#define IRQ_ONETH       13              /* On board Ethernet IRQ */
#define IRQ_PSW         14              /* Push Switch IRQ */

#define IVDR_CK_ON	8		/* iVDR Clock ON */

#elif defined(CONFIG_SH_R7780RP)
#define PA_POFF		(-1)

#define PA_BCR		0xa5000000	/* FPGA */
#define	PA_IRLMSK	(PA_BCR+0x0000)	/* Interrupt Mask control */
#define PA_IRLMON	(PA_BCR+0x0002)	/* Interrupt Status control */
#define	PA_SDPOW	(PA_BCR+0x0004)	/* SD Power control */
#define	PA_RSTCTL	(PA_BCR+0x0006)	/* Device Reset control */
#define	PA_PCIBD	(PA_BCR+0x0008)	/* PCI Board detect control */
#define	PA_PCICD	(PA_BCR+0x000a)	/* PCI Conector detect control */
#define	PA_ZIGIO1	(PA_BCR+0x000c)	/* Zigbee IO control 1 */
#define	PA_ZIGIO2	(PA_BCR+0x000e)	/* Zigbee IO control 2 */
#define	PA_ZIGIO3	(PA_BCR+0x0010)	/* Zigbee IO control 3 */
#define	PA_ZIGIO4	(PA_BCR+0x0012)	/* Zigbee IO control 4 */
#define	PA_IVDRMON	(PA_BCR+0x0014)	/* iVDR Moniter control */
#define	PA_IVDRCTL	(PA_BCR+0x0016)	/* iVDR control */
#define PA_OBLED	(PA_BCR+0x0018)	/* On Board LED control */
#define PA_OBSW		(PA_BCR+0x001a)	/* On Board Switch control */
#define PA_AUDIOSEL	(PA_BCR+0x001c)	/* Sound Interface Select control */
#define PA_EXTPLR	(PA_BCR+0x001e)	/* Extention Pin Polarity control */
#define PA_TPCTL	(PA_BCR+0x0100)	/* Touch Panel Access control */
#define PA_TPDCKCTL	(PA_BCR+0x0102)	/* Touch Panel Access data control */
#define PA_TPCTLCLR	(PA_BCR+0x0104)	/* Touch Panel Access control */
#define PA_TPXPOS	(PA_BCR+0x0106)	/* Touch Panel X position control */
#define PA_TPYPOS	(PA_BCR+0x0108)	/* Touch Panel Y position control */
#define PA_DBDET	(PA_BCR+0x0200)	/* Debug Board detect control */
#define PA_DBDISPCTL	(PA_BCR+0x0202)	/* Debug Board Dot timing control */
#define PA_DBSW		(PA_BCR+0x0204)	/* Debug Board Switch control */
#define PA_CFCTL	(PA_BCR+0x0300)	/* CF Timing control */
#define PA_CFPOW	(PA_BCR+0x0302)	/* CF Power control */
#define PA_CFCDINTCLR	(PA_BCR+0x0304)	/* CF Insert Interrupt clear */
#define PA_SCSMR	(PA_BCR+0x0400)	/* SCIF Serial mode control */
#define PA_SCBRR	(PA_BCR+0x0402)	/* SCIF Bit rate control */
#define PA_SCSCR	(PA_BCR+0x0404)	/* SCIF Serial control */
#define PA_SCFDTR	(PA_BCR+0x0406)	/* SCIF Send FIFO control */
#define PA_SCFSR	(PA_BCR+0x0408)	/* SCIF Serial status control */
#define PA_SCFRDR	(PA_BCR+0x040a)	/* SCIF Receive FIFO control */
#define PA_SCFCR	(PA_BCR+0x040c)	/* SCIF FIFO control */
#define PA_SCFDR	(PA_BCR+0x040e)	/* SCIF FIFO data control */
#define PA_SCLSR	(PA_BCR+0x0412)	/* SCIF Line Status control */
#define PA_ICCR		(PA_BCR+0x0500)	/* Serial control */
#define PA_SAR		(PA_BCR+0x0502)	/* Serial Slave control */
#define PA_MDR		(PA_BCR+0x0504)	/* Serial Mode control */
#define PA_ADR1		(PA_BCR+0x0506)	/* Serial Address1 control */
#define PA_DAR1		(PA_BCR+0x0546)	/* Serial Data1 control */
#define PA_VERREG	(PA_BCR+0x0600)	/* FPGA Version Register */

#define PA_AX88796L	0xa5800400	/* AX88796L Area */
#define PA_SC1602BSLB	0xa6000000	/* SC1602BSLB Area */
#define PA_IDE_OFFSET	0x1f0		/* CF IDE Offset */
#define AX88796L_IO_BASE	0x1000	/* AX88796L IO Base Address */

#define IRLCNTR1	(PA_BCR + 0)	/* Interrupt Control Register1 */

#define IRQ_PCISLOT1	0		/* PCI Slot #1 IRQ */
#define IRQ_PCISLOT2	1		/* PCI Slot #2 IRQ */
#define IRQ_PCISLOT3	2		/* PCI Slot #3 IRQ */
#define IRQ_PCISLOT4	3		/* PCI Slot #4 IRQ */
#define IRQ_CFINST	5		/* CF Card Insert IRQ */
#define IRQ_M66596	6		/* M66596 IRQ */
#define IRQ_SDCARD	7		/* SD Card IRQ */
#define IRQ_TUCHPANEL	8		/* Touch Panel IRQ */
#define IRQ_SCI		9		/* SCI IRQ */
#define IRQ_2SERIAL	10		/* Serial IRQ */
#define	IRQ_EXTENTION	11		/* EXTn IRQ */
#define IRQ_ONETH	12		/* On board Ethernet IRQ */
#define IRQ_PSW		13		/* Push Switch IRQ */
#define IRQ_ZIGBEE	14		/* Ziggbee IO IRQ */

#define IVDR_CK_ON	8		/* iVDR Clock ON */

#elif defined(CONFIG_SH_R7785RP)
#define PA_BCR		0xa4000000	/* FPGA */
#define PA_SDPOW	(-1)

#define	PA_PCISCR	(PA_BCR+0x0000)
#define PA_IRLPRA	(PA_BCR+0x0002)
#define	PA_IRLPRB	(PA_BCR+0x0004)
#define	PA_IRLPRC	(PA_BCR+0x0006)
#define	PA_IRLPRD	(PA_BCR+0x0008)
#define IRLCNTR1	(PA_BCR+0x0010)
#define	PA_IRLPRE	(PA_BCR+0x000a)
#define	PA_IRLPRF	(PA_BCR+0x000c)
#define	PA_EXIRLCR	(PA_BCR+0x000e)
#define	PA_IRLMCR1	(PA_BCR+0x0010)
#define	PA_IRLMCR2	(PA_BCR+0x0012)
#define	PA_IRLSSR1	(PA_BCR+0x0014)
#define	PA_IRLSSR2	(PA_BCR+0x0016)
#define PA_CFTCR	(PA_BCR+0x0100)
#define PA_CFPCR	(PA_BCR+0x0102)
#define PA_PCICR	(PA_BCR+0x0110)
#define PA_IVDRCTL	(PA_BCR+0x0112)
#define PA_IVDRSR	(PA_BCR+0x0114)
#define PA_PDRSTCR	(PA_BCR+0x0116)
#define PA_POFF		(PA_BCR+0x0120)
#define PA_LCDCR	(PA_BCR+0x0130)
#define PA_TPCR		(PA_BCR+0x0140)
#define PA_TPCKCR	(PA_BCR+0x0142)
#define PA_TPRSTR	(PA_BCR+0x0144)
#define PA_TPXPDR	(PA_BCR+0x0146)
#define PA_TPYPDR	(PA_BCR+0x0148)
#define PA_GPIOPFR	(PA_BCR+0x0150)
#define PA_GPIODR	(PA_BCR+0x0152)
#define PA_OBLED	(PA_BCR+0x0154)
#define PA_SWSR		(PA_BCR+0x0156)
#define PA_VERREG	(PA_BCR+0x0158)
#define PA_SMCR		(PA_BCR+0x0200)
#define PA_SMSMADR	(PA_BCR+0x0202)
#define PA_SMMR		(PA_BCR+0x0204)
#define PA_SMSADR1	(PA_BCR+0x0206)
#define PA_SMSADR32	(PA_BCR+0x0244)
#define PA_SMTRDR1	(PA_BCR+0x0246)
#define PA_SMTRDR16	(PA_BCR+0x0264)
#define PA_CU3MDR	(PA_BCR+0x0300)
#define PA_CU5MDR	(PA_BCR+0x0302)
#define PA_MMSR		(PA_BCR+0x0400)

#define IVDR_CK_ON	4		/* iVDR Clock ON */

#endif

void make_r7780rp_irq(unsigned int irq);
void highlander_init_irq(void);

#define __IO_PREFIX	r7780rp
#include <asm/io_generic.h>

#endif  /* __ASM_SH_RENESAS_R7780RP */
