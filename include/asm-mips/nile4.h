/*
 *  asm-mips/nile4.h -- NEC Vrc-5074 Nile 4 definitions
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 *  This file is based on the following documentation:
 *
 *	NEC Vrc 5074 System Controller Data Sheet, June 1998
 */

#ifndef _ASM_NILE4_H
#define _ASM_NILE4_H

#define NILE4_BASE		0xbfa00000
#define NILE4_SIZE		0x00200000		/* 2 MB */


    /*
     *  Physical Device Address Registers (PDARs)
     */

#define NILE4_SDRAM0	0x0000	/* SDRAM Bank 0 [R/W] */
#define NILE4_SDRAM1	0x0008	/* SDRAM Bank 1 [R/W] */
#define NILE4_DCS2	0x0010	/* Device Chip-Select 2 [R/W] */
#define NILE4_DCS3	0x0018	/* Device Chip-Select 3 [R/W] */
#define NILE4_DCS4	0x0020	/* Device Chip-Select 4 [R/W] */
#define NILE4_DCS5	0x0028	/* Device Chip-Select 5 [R/W] */
#define NILE4_DCS6	0x0030	/* Device Chip-Select 6 [R/W] */
#define NILE4_DCS7	0x0038	/* Device Chip-Select 7 [R/W] */
#define NILE4_DCS8	0x0040	/* Device Chip-Select 8 [R/W] */
#define NILE4_PCIW0	0x0060	/* PCI Address Window 0 [R/W] */
#define NILE4_PCIW1	0x0068	/* PCI Address Window 1 [R/W] */
#define NILE4_INTCS	0x0070	/* Controller Internal Registers and Devices */
				/* [R/W] */
#define NILE4_BOOTCS	0x0078	/* Boot ROM Chip-Select [R/W] */


    /*
     *  CPU Interface Registers
     */

#define NILE4_CPUSTAT	0x0080	/* CPU Status [R/W] */
#define NILE4_INTCTRL	0x0088	/* Interrupt Control [R/W] */
#define NILE4_INTSTAT0	0x0090	/* Interrupt Status 0 [R] */
#define NILE4_INTSTAT1	0x0098	/* Interrupt Status 1 and CPU Interrupt */
				/* Enable [R/W] */
#define NILE4_INTCLR	0x00A0	/* Interrupt Clear [R/W] */
#define NILE4_INTPPES	0x00A8	/* PCI Interrupt Control [R/W] */


    /*
     *  Memory-Interface Registers
     */

#define NILE4_MEMCTRL	0x00C0	/* Memory Control */
#define NILE4_ACSTIME	0x00C8	/* Memory Access Timing [R/W] */
#define NILE4_CHKERR	0x00D0	/* Memory Check Error Status [R] */


    /*
     *  PCI-Bus Registers
     */

#define NILE4_PCICTRL	0x00E0	/* PCI Control [R/W] */
#define NILE4_PCIARB	0x00E8	/* PCI Arbiter [R/W] */
#define NILE4_PCIINIT0	0x00F0	/* PCI Master (Initiator) 0 [R/W] */
#define NILE4_PCIINIT1	0x00F8	/* PCI Master (Initiator) 1 [R/W] */
#define NILE4_PCIERR	0x00B8	/* PCI Error [R/W] */


    /*
     *  Local-Bus Registers
     */

#define NILE4_LCNFG	0x0100	/* Local Bus Configuration [R/W] */
#define NILE4_LCST2	0x0110	/* Local Bus Chip-Select Timing 2 [R/W] */
#define NILE4_LCST3	0x0118	/* Local Bus Chip-Select Timing 3 [R/W] */
#define NILE4_LCST4	0x0120	/* Local Bus Chip-Select Timing 4 [R/W] */
#define NILE4_LCST5	0x0128	/* Local Bus Chip-Select Timing 5 [R/W] */
#define NILE4_LCST6	0x0130	/* Local Bus Chip-Select Timing 6 [R/W] */
#define NILE4_LCST7	0x0138	/* Local Bus Chip-Select Timing 7 [R/W] */
#define NILE4_LCST8	0x0140	/* Local Bus Chip-Select Timing 8 [R/W] */
#define NILE4_DCSFN	0x0150	/* Device Chip-Select Muxing and Output */
				/* Enables [R/W] */
#define NILE4_DCSIO	0x0158	/* Device Chip-Selects As I/O Bits [R/W] */
#define NILE4_BCST	0x0178	/* Local Boot Chip-Select Timing [R/W] */


    /*
     *  DMA Registers
     */

#define NILE4_DMACTRL0	0x0180	/* DMA Control 0 [R/W] */
#define NILE4_DMASRCA0	0x0188	/* DMA Source Address 0 [R/W] */
#define NILE4_DMADESA0	0x0190	/* DMA Destination Address 0 [R/W] */
#define NILE4_DMACTRL1	0x0198	/* DMA Control 1 [R/W] */
#define NILE4_DMASRCA1	0x01A0	/* DMA Source Address 1 [R/W] */
#define NILE4_DMADESA1	0x01A8	/* DMA Destination Address 1 [R/W] */


    /*
     *  Timer Registers
     */

#define NILE4_T0CTRL	0x01C0	/* SDRAM Refresh Control [R/W] */
#define NILE4_T0CNTR	0x01C8	/* SDRAM Refresh Counter [R/W] */
#define NILE4_T1CTRL	0x01D0	/* CPU-Bus Read Time-Out Control [R/W] */
#define NILE4_T1CNTR	0x01D8	/* CPU-Bus Read Time-Out Counter [R/W] */
#define NILE4_T2CTRL	0x01E0	/* General-Purpose Timer Control [R/W] */
#define NILE4_T2CNTR	0x01E8	/* General-Purpose Timer Counter [R/W] */
#define NILE4_T3CTRL	0x01F0	/* Watchdog Timer Control [R/W] */
#define NILE4_T3CNTR	0x01F8	/* Watchdog Timer Counter [R/W] */


    /*
     *  PCI Configuration Space Registers
     */

#define NILE4_PCI_BASE	0x0200

#define NILE4_VID	0x0200	/* PCI Vendor ID [R] */
#define NILE4_DID	0x0202	/* PCI Device ID [R] */
#define NILE4_PCICMD	0x0204	/* PCI Command [R/W] */
#define NILE4_PCISTS	0x0206	/* PCI Status [R/W] */
#define NILE4_REVID	0x0208	/* PCI Revision ID [R] */
#define NILE4_CLASS	0x0209	/* PCI Class Code [R] */
#define NILE4_CLSIZ	0x020C	/* PCI Cache Line Size [R/W] */
#define NILE4_MLTIM	0x020D	/* PCI Latency Timer [R/W] */
#define NILE4_HTYPE	0x020E	/* PCI Header Type [R] */
#define NILE4_BIST	0x020F	/* BIST [R] (unimplemented) */
#define NILE4_BARC	0x0210	/* PCI Base Address Register Control [R/W] */
#define NILE4_BAR0	0x0218	/* PCI Base Address Register 0 [R/W] */
#define NILE4_BAR1	0x0220	/* PCI Base Address Register 1 [R/W] */
#define NILE4_CIS	0x0228	/* PCI Cardbus CIS Pointer [R] */
				/* (unimplemented) */
#define NILE4_SSVID	0x022C	/* PCI Sub-System Vendor ID [R/W] */
#define NILE4_SSID	0x022E	/* PCI Sub-System ID [R/W] */
#define NILE4_ROM	0x0230	/* Expansion ROM Base Address [R] */
				/* (unimplemented) */
#define NILE4_INTLIN	0x023C	/* PCI Interrupt Line [R/W] */
#define NILE4_INTPIN	0x023D	/* PCI Interrupt Pin [R] */
#define NILE4_MINGNT	0x023E	/* PCI Min_Gnt [R] (unimplemented) */
#define NILE4_MAXLAT	0x023F	/* PCI Max_Lat [R] (unimplemented) */
#define NILE4_BAR2	0x0240	/* PCI Base Address Register 2 [R/W] */
#define NILE4_BAR3	0x0248	/* PCI Base Address Register 3 [R/W] */
#define NILE4_BAR4	0x0250	/* PCI Base Address Register 4 [R/W] */
#define NILE4_BAR5	0x0258	/* PCI Base Address Register 5 [R/W] */
#define NILE4_BAR6	0x0260	/* PCI Base Address Register 6 [R/W] */
#define NILE4_BAR7	0x0268	/* PCI Base Address Register 7 [R/W] */
#define NILE4_BAR8	0x0270	/* PCI Base Address Register 8 [R/W] */
#define NILE4_BARB	0x0278	/* PCI Base Address Register BOOT [R/W] */


    /*
     *  Serial-Port Registers
     */

#define NILE4_UART_BASE	0x0300

#define NILE4_UARTRBR	0x0300	/* UART Receiver Data Buffer [R] */
#define NILE4_UARTTHR	0x0300	/* UART Transmitter Data Holding [W] */
#define NILE4_UARTIER	0x0308	/* UART Interrupt Enable [R/W] */
#define NILE4_UARTDLL	0x0300	/* UART Divisor Latch LSB [R/W] */
#define NILE4_UARTDLM	0x0308	/* UART Divisor Latch MSB [R/W] */
#define NILE4_UARTIIR	0x0310	/* UART Interrupt ID [R] */
#define NILE4_UARTFCR	0x0310	/* UART FIFO Control [W] */
#define NILE4_UARTLCR	0x0318	/* UART Line Control [R/W] */
#define NILE4_UARTMCR	0x0320	/* UART Modem Control [R/W] */
#define NILE4_UARTLSR	0x0328	/* UART Line Status [R/W] */
#define NILE4_UARTMSR	0x0330	/* UART Modem Status [R/W] */
#define NILE4_UARTSCR	0x0338	/* UART Scratch [R/W] */

#define NILE4_UART_BASE_BAUD	520833	/* 100 MHz / 12 / 16 */


    /*
     *  Interrupt Lines
     */

#define NILE4_INT_CPCE	0	/* CPU-Interface Parity-Error Interrupt */
#define NILE4_INT_CNTD	1	/* CPU No-Target Decode Interrupt */
#define NILE4_INT_MCE	2	/* Memory-Check Error Interrupt */
#define NILE4_INT_DMA	3	/* DMA Controller Interrupt */
#define NILE4_INT_UART	4	/* UART Interrupt */
#define NILE4_INT_WDOG	5	/* Watchdog Timer Interrupt */
#define NILE4_INT_GPT	6	/* General-Purpose Timer Interrupt */
#define NILE4_INT_LBRTD	7	/* Local-Bus Ready Timer Interrupt */
#define NILE4_INT_INTA	8	/* PCI Interrupt Signal INTA# */
#define NILE4_INT_INTB	9	/* PCI Interrupt Signal INTB# */
#define NILE4_INT_INTC	10	/* PCI Interrupt Signal INTC# */
#define NILE4_INT_INTD	11	/* PCI Interrupt Signal INTD# */
#define NILE4_INT_INTE	12	/* PCI Interrupt Signal INTE# (ISA cascade) */
#define NILE4_INT_RESV	13	/* Reserved */
#define NILE4_INT_PCIS	14	/* PCI SERR# Interrupt */
#define NILE4_INT_PCIE	15	/* PCI Internal Error Interrupt */


    /*
     *  Nile 4 Register Access
     */

static inline void nile4_sync(void)
{
    volatile u32 *p = (volatile u32 *)0xbfc00000;
    (void)(*p);
}

static inline void nile4_out32(u32 offset, u32 val)
{
    *(volatile u32 *)(NILE4_BASE+offset) = val;
    nile4_sync();
}

static inline u32 nile4_in32(u32 offset)
{
    u32 val = *(volatile u32 *)(NILE4_BASE+offset);
    nile4_sync();
    return val;
}

static inline void nile4_out16(u32 offset, u16 val)
{
    *(volatile u16 *)(NILE4_BASE+offset) = val;
    nile4_sync();
}

static inline u16 nile4_in16(u32 offset)
{
    u16 val = *(volatile u16 *)(NILE4_BASE+offset);
    nile4_sync();
    return val;
}

static inline void nile4_out8(u32 offset, u8 val)
{
    *(volatile u8 *)(NILE4_BASE+offset) = val;
    nile4_sync();
}

static inline u8 nile4_in8(u32 offset)
{
    u8 val = *(volatile u8 *)(NILE4_BASE+offset);
    nile4_sync();
    return val;
}


    /*
     *  Physical Device Address Registers
     */

extern void nile4_set_pdar(u32 pdar, u32 phys, u32 size, int width,
			   int on_memory_bus, int visible);


    /*
     *  PCI Master Registers
     */

#define NILE4_PCICMD_IACK	0	/* PCI Interrupt Acknowledge */
#define NILE4_PCICMD_IO		1	/* PCI I/O Space */
#define NILE4_PCICMD_MEM	3	/* PCI Memory Space */
#define NILE4_PCICMD_CFG	5	/* PCI Configuration Space */


    /*
     *  PCI Address Spaces
     *
     *  Note that these are multiplexed using PCIINIT[01]!
     */

#define NILE4_PCI_IO_BASE	0xa6000000
#define NILE4_PCI_MEM_BASE	0xa8000000
#define NILE4_PCI_CFG_BASE	NILE4_PCI_MEM_BASE
#define NILE4_PCI_IACK_BASE	NILE4_PCI_IO_BASE


extern void nile4_set_pmr(u32 pmr, u32 type, u32 addr);


    /*
     *  Interrupt Programming
     */

#define NUM_I8259_INTERRUPTS	16
#define NUM_NILE4_INTERRUPTS	16

#define IRQ_I8259_CASCADE	NILE4_INT_INTE
#define is_i8259_irq(irq)	((irq) < NUM_I8259_INTERRUPTS)
#define nile4_to_irq(n)		((n)+NUM_I8259_INTERRUPTS)
#define irq_to_nile4(n)		((n)-NUM_I8259_INTERRUPTS)

extern void nile4_map_irq(int nile4_irq, int cpu_irq);
extern void nile4_map_irq_all(int cpu_irq);
extern void nile4_enable_irq(unsigned int nile4_irq);
extern void nile4_disable_irq(unsigned int nile4_irq);
extern void nile4_disable_irq_all(void);
extern u16 nile4_get_irq_stat(int cpu_irq);
extern void nile4_enable_irq_output(int cpu_irq);
extern void nile4_disable_irq_output(int cpu_irq);
extern void nile4_set_pci_irq_polarity(int pci_irq, int high);
extern void nile4_set_pci_irq_level_or_edge(int pci_irq, int level);
extern void nile4_clear_irq(int nile4_irq);
extern void nile4_clear_irq_mask(u32 mask);
extern u8 nile4_i8259_iack(void);
extern void nile4_dump_irq_status(void);	/* Debug */

#endif

