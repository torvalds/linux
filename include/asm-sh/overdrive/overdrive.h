/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 */

#include <linux/config.h>

#ifndef __OVERDRIVE_H__
#define __OVERDRIVE_H__

#define OVERDRIVE_INT_CT 0xa3a00000
#define OVERDRIVE_INT_DT 0xa3b00000

#define OVERDRIVE_CTRL    0xa3000000

/* Shoving all these bits into the same register is not a good idea. 
 * As soon as I get a spare moment, I'll change the FPGA and put each 
 * bit in a separate register
 */

#define VALID_CTRL_BITS		          0x1f

#define ENABLE_RS232_MASK	  	  0x1e
#define DISABLE_RS232_BIT		  0x01

#define ENABLE_NMI_MASK			  0x1d
#define DISABLE_NMI_BIT			  0x02

#define RESET_PCI_MASK			  0x1b
#define ENABLE_PCI_BIT			  0x04

#define ENABLE_LED_MASK			  0x17
#define DISABLE_LED_BIT			  0x08

#define RESET_FPGA_MASK			  0x0f
#define ENABLE_FPGA_BIT			  0x10


#define FPGA_DCLK_ADDRESS           0xA3C00000

#define FPGA_DATA        0x01	/*   W */
#define FPGA_CONFDONE    0x02	/* R   */
#define FPGA_NOT_STATUS  0x04	/* R   */
#define FPGA_INITDONE    0x08	/* R   */

#define FPGA_TIMEOUT     100000


/* Interrupts for the overdrive. Note that these numbers have 
 * nothing to do with the actual IRQ numbers they appear on, 
 * this is all programmable. This is simply the position in the 
 * INT_CT register.
 */

#define OVERDRIVE_PCI_INTA              0
#define OVERDRIVE_PCI_INTB              1
#define OVERDRIVE_PCI_INTC              2
#define OVERDRIVE_PCI_INTD              3
#define OVERDRIVE_GALILEO_INT           4
#define OVERDRIVE_GALILEO_LOCAL_INT     5
#define OVERDRIVE_AUDIO_INT             6
#define OVERDRIVE_KEYBOARD_INT          7

/* Which Linux IRQ should we assign to each interrupt source? */
#define OVERDRIVE_PCI_IRQ1              2
#ifdef CONFIG_HACKED_NE2K
#define OVERDRIVE_PCI_IRQ2              7
#else
#define OVERDRIVE_PCI_IRQ2              2
#undef OVERDRIVE_PCI_INTB 
#define OVERDRIVE_PCI_INTB OVERDRIVE_PCI_INTA

#endif

/* Put the ESS solo audio chip on IRQ 4 */
#define OVERDRIVE_ESS_IRQ               4

/* Where the memory behind the PCI bus appears */
#define PCI_DRAM_BASE   0xb7000000
#define PCI_DRAM_SIZE (16*1024*1024)
#define PCI_DRAM_FINISH (PCI_DRAM_BASE+PCI_DRAM_SIZE-1)

/* Where the IO region appears in the memory */
#define PCI_GTIO_BASE   0xb8000000

#endif
