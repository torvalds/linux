/*
 * (C) Copyright 2000, 2001
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __ASM_PPCBOOT_H__
#define __ASM_PPCBOOT_H__

/*
 * Board information passed to kernel from PPCBoot
 *
 * include/asm-ppc/ppcboot.h
 */

#ifndef __ASSEMBLY__
#include <linux/types.h>

typedef struct bd_info {
	unsigned long	bi_memstart;	/* start of DRAM memory */
	unsigned long	bi_memsize;	/* size	 of DRAM memory in bytes */
	unsigned long	bi_flashstart;	/* start of FLASH memory */
	unsigned long	bi_flashsize;	/* size	 of FLASH memory */
	unsigned long	bi_flashoffset; /* reserved area for startup monitor */
	unsigned long	bi_sramstart;	/* start of SRAM memory */
	unsigned long	bi_sramsize;	/* size	 of SRAM memory */
#if defined(CONFIG_8xx) || defined(CONFIG_CPM2) || defined(CONFIG_85xx) ||\
	defined(CONFIG_83xx)
	unsigned long	bi_immr_base;	/* base of IMMR register */
#endif
#if defined(CONFIG_PPC_MPC52xx)
	unsigned long   bi_mbar_base;   /* base of internal registers */
#endif
	unsigned long	bi_bootflags;	/* boot / reboot flag (for LynxOS) */
	unsigned long	bi_ip_addr;	/* IP Address */
	unsigned char	bi_enetaddr[6];	/* Ethernet address */
	unsigned short	bi_ethspeed;	/* Ethernet speed in Mbps */
	unsigned long	bi_intfreq;	/* Internal Freq, in MHz */
	unsigned long	bi_busfreq;	/* Bus Freq, in MHz */
#if defined(CONFIG_CPM2)
	unsigned long	bi_cpmfreq;	/* CPM_CLK Freq, in MHz */
	unsigned long	bi_brgfreq;	/* BRG_CLK Freq, in MHz */
	unsigned long	bi_sccfreq;	/* SCC_CLK Freq, in MHz */
	unsigned long	bi_vco;		/* VCO Out from PLL, in MHz */
#endif
#if defined(CONFIG_PPC_MPC52xx)
	unsigned long   bi_ipbfreq;     /* IPB Bus Freq, in MHz */
	unsigned long   bi_pcifreq;     /* PCI Bus Freq, in MHz */
#endif
	unsigned long	bi_baudrate;	/* Console Baudrate */
#if defined(CONFIG_4xx)
	unsigned char	bi_s_version[4];	/* Version of this structure */
	unsigned char	bi_r_version[32];	/* Version of the ROM (IBM) */
	unsigned int	bi_procfreq;	/* CPU (Internal) Freq, in Hz */
	unsigned int	bi_plb_busfreq;	/* PLB Bus speed, in Hz */
	unsigned int	bi_pci_busfreq;	/* PCI Bus speed, in Hz */
	unsigned char	bi_pci_enetaddr[6];	/* PCI Ethernet MAC address */
#endif
#if defined(CONFIG_HYMOD)
	hymod_conf_t	bi_hymod_conf;	/* hymod configuration information */
#endif
#if defined(CONFIG_EVB64260) || defined(CONFIG_44x) || defined(CONFIG_85xx) ||\
	defined(CONFIG_83xx)
	/* second onboard ethernet port */
	unsigned char	bi_enet1addr[6];
#endif
#if defined(CONFIG_EVB64260) || defined(CONFIG_440GX) || defined(CONFIG_85xx)
	/* third onboard ethernet ports */
	unsigned char	bi_enet2addr[6];
#endif
#if defined(CONFIG_440GX)
	/* fourth onboard ethernet ports */
	unsigned char	bi_enet3addr[6];
#endif
#if defined(CONFIG_4xx)
	unsigned int	bi_opbfreq;		/* OB clock in Hz */
	int		bi_iic_fast[2];		/* Use fast i2c mode */
#endif
#if defined(CONFIG_440GX)
	int		bi_phynum[4];		/* phy mapping */
	int		bi_phymode[4];		/* phy mode */
#endif
} bd_t;

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_PPCBOOT_H__ */
