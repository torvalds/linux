/*
 * linux/include/asm-arm/arch-cl7500/irqs.h
 *
 * Copyright (C) 1999 Nexus Electronics Ltd
 */

#define IRQ_INT2		0
#define IRQ_INT1		2
#define IRQ_VSYNCPULSE		3
#define IRQ_POWERON		4
#define IRQ_TIMER0		5
#define IRQ_TIMER1		6
#define IRQ_FORCE		7
#define IRQ_INT8		8
#define IRQ_ISA			9
#define IRQ_INT6		10
#define IRQ_INT5		11
#define IRQ_INT4		12
#define IRQ_INT3		13
#define IRQ_KEYBOARDTX		14
#define IRQ_KEYBOARDRX		15

#define IRQ_DMA0		16
#define IRQ_DMA1		17
#define IRQ_DMA2		18
#define IRQ_DMA3		19
#define IRQ_DMAS0		20
#define IRQ_DMAS1		21

#define IRQ_IOP0		24
#define IRQ_IOP1		25
#define IRQ_IOP2		26
#define IRQ_IOP3		27
#define IRQ_IOP4		28
#define IRQ_IOP5		29
#define IRQ_IOP6		30
#define IRQ_IOP7		31

#define IRQ_MOUSERX		40
#define IRQ_MOUSETX		41
#define IRQ_ADC			42
#define IRQ_EVENT1		43
#define IRQ_EVENT2		44

#define IRQ_ISA_BASE		48
#define IRQ_ISA_3		48
#define IRQ_ISA_4		49
#define IRQ_ISA_5		50
#define IRQ_ISA_7		51
#define IRQ_ISA_9		52
#define IRQ_ISA_10		53
#define IRQ_ISA_11		54
#define IRQ_ISA_14		55	

#define FIQ_INT9		0
#define FIQ_INT5		1
#define FIQ_INT6		4
#define FIQ_INT8		6
#define FIQ_FORCE		7

/*
 * This is the offset of the FIQ "IRQ" numbers
 */
#define FIQ_START		64

#define IRQ_TIMER		IRQ_TIMER0
