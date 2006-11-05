#ifndef __ASM_SH_CPU_SH2A_IRQ_H
#define __ASM_SH_CPU_SH2A_IRQ_H

#define INTC_IPR01	0xfffe0818UL
#define INTC_IPR02	0xfffe081aUL
#define INTC_IPR05	0xfffe0820UL
#define INTC_IPR06	0xfffe0c00UL
#define INTC_IPR07	0xfffe0c02UL
#define INTC_IPR08	0xfffe0c04UL
#define INTC_IPR09	0xfffe0c06UL
#define INTC_IPR10	0xfffe0c08UL
#define INTC_IPR11	0xfffe0c0aUL
#define INTC_IPR12	0xfffe0c0cUL
#define INTC_IPR13	0xfffe0c0eUL
#define INTC_IPR14	0xfffe0c10UL

#define INTC_ICR0	0xfffe0800UL
#define INTC_ICR1	0xfffe0802UL
#define INTC_ICR2	0xfffe0804UL
#define INTC_ISR	0xfffe0806UL

#define IRQ0_IRQ	64
#define IRQ1_IRQ	65
#define IRQ2_IRQ	66
#define IRQ3_IRQ	67
#define IRQ4_IRQ	68
#define IRQ5_IRQ	69
#define IRQ6_IRQ	70
#define IRQ7_IRQ	71

#define PINT0_IRQ	80
#define PINT1_IRQ	81
#define PINT2_IRQ	82
#define PINT3_IRQ	83
#define PINT4_IRQ	84
#define PINT5_IRQ	85
#define PINT6_IRQ	86
#define PINT7_IRQ	87

#define CMI0_IRQ	140
#define CMI1_IRQ	141

#define SCIF_BRI_IRQ	240
#define SCIF_ERI_IRQ	241
#define SCIF_RXI_IRQ	242
#define SCIF_TXI_IRQ	243
#define SCIF_IPR_ADDR	INTC_IPR14
#define SCIF_IPR_POS	3
#define SCIF_PRIORITY	3

#define SCIF1_BRI_IRQ	244
#define SCIF1_ERI_IRQ	245
#define SCIF1_RXI_IRQ	246
#define SCIF1_TXI_IRQ	247
#define SCIF1_IPR_ADDR	INTC_IPR14
#define SCIF1_IPR_POS	2
#define SCIF1_PRIORITY	3

#define SCIF2_BRI_IRQ	248
#define SCIF2_ERI_IRQ	249
#define SCIF2_RXI_IRQ	250
#define SCIF2_TXI_IRQ	251
#define SCIF2_IPR_ADDR	INTC_IPR14
#define SCIF2_IPR_POS	1
#define SCIF2_PRIORITY	3

#define SCIF3_BRI_IRQ	252
#define SCIF3_ERI_IRQ	253
#define SCIF3_RXI_IRQ	254
#define SCIF3_TXI_IRQ	255
#define SCIF3_IPR_ADDR	INTC_IPR14
#define SCIF3_IPR_POS	0
#define SCIF3_PRIORITY	3

#endif /* __ASM_SH_CPU_SH2A_IRQ_H */
