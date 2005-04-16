/*
 *
 * BRIEF MODULE DESCRIPTION
 *	ITE 8172 Interrupt Numbering
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MIPS_ITEINT_H
#define _MIPS_ITEINT_H

/*
 * Here's the "strategy":
 * We number the LPC serial irqs from 0 to 15,
 * the local bus irqs from 16 to 31,
 * the pci dev register interrupts from 32 to 47,
 * and the non-maskable ints from 48 to 53.
 */

#define IT8172_LPC_IRQ_BASE  0    /* first LPC int number */
#define IT8172_SERIRQ_0      (IT8172_LPC_IRQ_BASE + 0)
#define IT8172_SERIRQ_1      (IT8172_LPC_IRQ_BASE + 1)
#define IT8172_SERIRQ_2      (IT8172_LPC_IRQ_BASE + 2)
#define IT8172_SERIRQ_3      (IT8172_LPC_IRQ_BASE + 3)
#define IT8172_SERIRQ_4      (IT8172_LPC_IRQ_BASE + 4)
#define IT8172_SERIRQ_5      (IT8172_LPC_IRQ_BASE + 5)
#define IT8172_SERIRQ_6      (IT8172_LPC_IRQ_BASE + 6)
#define IT8172_SERIRQ_7      (IT8172_LPC_IRQ_BASE + 7)
#define IT8172_SERIRQ_8      (IT8172_LPC_IRQ_BASE + 8)
#define IT8172_SERIRQ_9      (IT8172_LPC_IRQ_BASE + 9)
#define IT8172_SERIRQ_10     (IT8172_LPC_IRQ_BASE + 10)
#define IT8172_SERIRQ_11     (IT8172_LPC_IRQ_BASE + 11)
#define IT8172_SERIRQ_12     (IT8172_LPC_IRQ_BASE + 12)
#define IT8172_SERIRQ_13     (IT8172_LPC_IRQ_BASE + 13)
#define IT8172_SERIRQ_14     (IT8172_LPC_IRQ_BASE + 14)
#define IT8172_SERIRQ_15     (IT8172_LPC_IRQ_BASE + 15)

#define IT8172_LB_IRQ_BASE  16   /* first local bus int number */
#define IT8172_PPR_IRQ          (IT8172_LB_IRQ_BASE + 0) /* parallel port */
#define IT8172_TIMER0_IRQ       (IT8172_LB_IRQ_BASE + 1)
#define IT8172_TIMER1_IRQ       (IT8172_LB_IRQ_BASE + 2)
#define IT8172_I2C_IRQ          (IT8172_LB_IRQ_BASE + 3)
#define IT8172_GPIO_IRQ         (IT8172_LB_IRQ_BASE + 4)
#define IT8172_CIR0_IRQ         (IT8172_LB_IRQ_BASE + 5)
#define IT8172_CIR1_IRQ         (IT8172_LB_IRQ_BASE + 6)
#define IT8172_UART_IRQ         (IT8172_LB_IRQ_BASE + 7)
#define IT8172_SCR0_IRQ         (IT8172_LB_IRQ_BASE + 8)
#define IT8172_SCR1_IRQ         (IT8172_LB_IRQ_BASE + 9)
#define IT8172_RTC_IRQ          (IT8172_LB_IRQ_BASE + 10)
#define IT8172_IOCHK_IRQ        (IT8172_LB_IRQ_BASE + 11)
/* 12 - 15 reserved */

/*
 * Note here that the pci dev registers includes bits for more than
 * just the pci devices.
 */
#define IT8172_PCI_DEV_IRQ_BASE  32   /* first pci dev irq */
#define IT8172_AC97_IRQ          (IT8172_PCI_DEV_IRQ_BASE + 0)
#define IT8172_MC68K_IRQ         (IT8172_PCI_DEV_IRQ_BASE + 1)
#define IT8172_IDE_IRQ           (IT8172_PCI_DEV_IRQ_BASE + 2)
#define IT8172_USB_IRQ           (IT8172_PCI_DEV_IRQ_BASE + 3)
#define IT8172_BRIDGE_MASTER_IRQ (IT8172_PCI_DEV_IRQ_BASE + 4)
#define IT8172_BRIDGE_TARGET_IRQ (IT8172_PCI_DEV_IRQ_BASE + 5)
#define IT8172_PCI_INTA_IRQ      (IT8172_PCI_DEV_IRQ_BASE + 6)
#define IT8172_PCI_INTB_IRQ      (IT8172_PCI_DEV_IRQ_BASE + 7)
#define IT8172_PCI_INTC_IRQ      (IT8172_PCI_DEV_IRQ_BASE + 8)
#define IT8172_PCI_INTD_IRQ      (IT8172_PCI_DEV_IRQ_BASE + 9)
#define IT8172_S_INTA_IRQ        (IT8172_PCI_DEV_IRQ_BASE + 10)
#define IT8172_S_INTB_IRQ        (IT8172_PCI_DEV_IRQ_BASE + 11)
#define IT8172_S_INTC_IRQ        (IT8172_PCI_DEV_IRQ_BASE + 12)
#define IT8172_S_INTD_IRQ        (IT8172_PCI_DEV_IRQ_BASE + 13)
#define IT8172_CDMA_IRQ          (IT8172_PCI_DEV_IRQ_BASE + 14)
#define IT8172_DMA_IRQ           (IT8172_PCI_DEV_IRQ_BASE + 15)

#define IT8172_NMI_IRQ_BASE      48
#define IT8172_SER_NMI_IRQ       (IT8172_NMI_IRQ_BASE + 0)
#define IT8172_PCI_NMI_IRQ       (IT8172_NMI_IRQ_BASE + 1)
#define IT8172_RTC_NMI_IRQ       (IT8172_NMI_IRQ_BASE + 2)
#define IT8172_CPUIF_NMI_IRQ     (IT8172_NMI_IRQ_BASE + 3)
#define IT8172_PMER_NMI_IRQ      (IT8172_NMI_IRQ_BASE + 4)
#define IT8172_POWER_NMI_IRQ     (IT8172_NMI_IRQ_BASE + 5)

#define IT8172_LAST_IRQ          (IT8172_POWER_NMI_IRQ)
/* Finally, let's move over here the mips cpu timer interrupt.
 */
#define MIPS_CPU_TIMER_IRQ       (NR_IRQS-1)

/*
 * IT8172 Interrupt Controller Registers
 */
struct it8172_intc_regs {
        volatile unsigned short lb_req;      /* offset 0 */
        volatile unsigned short lb_mask;
        volatile unsigned short lb_trigger;
        volatile unsigned short lb_level;
	unsigned char pad0[8];

        volatile unsigned short lpc_req;     /* offset 0x10 */
        volatile unsigned short lpc_mask;
        volatile unsigned short lpc_trigger;
        volatile unsigned short lpc_level;
	unsigned char pad1[8];

        volatile unsigned short pci_req;     /* offset 0x20 */
        volatile unsigned short pci_mask;
        volatile unsigned short pci_trigger;
        volatile unsigned short pci_level;
	unsigned char pad2[8];

        volatile unsigned short nmi_req;     /* offset 0x30 */
        volatile unsigned short nmi_mask;
        volatile unsigned short nmi_trigger;
        volatile unsigned short nmi_level;
	unsigned char pad3[6];

        volatile unsigned short nmi_redir;   /* offset 0x3E */
	unsigned char pad4[0xBE];

        volatile unsigned short intstatus;    /* offset 0xFE */
};

#endif /* _MIPS_ITEINT_H */
