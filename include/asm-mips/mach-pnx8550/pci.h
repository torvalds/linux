/*
 *
 * BRIEF MODULE DESCRIPTION
 * PCI specific definitions
 *
 * Author: source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#ifndef __PNX8550_PCI_H
#define __PNX8550_PCI_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#define PCI_CMD_IOR                     0x20
#define PCI_CMD_IOW                     0x30
#define PCI_CMD_CONFIG_READ             0xa0
#define PCI_CMD_CONFIG_WRITE            0xb0

#define PCI_IO_TIMEOUT                  1000
#define PCI_IO_RETRY			5
/* Timeout for IO and CFG accesses.
   This is in 1/1024 th of a jiffie(=10ms)
   i.e. approx 10us */
#define PCI_IO_JIFFIES_TIMEOUT          40
#define PCI_IO_JIFFIES_SHIFT            10

#define PCI_BYTE_ENABLE_MASK		0x0000000f
#define PCI_CFG_BUS_SHIFT               16
#define PCI_CFG_FUNC_SHIFT              8
#define PCI_CFG_REG_SHIFT               2

#define PCI_BASE                  0x1be00000
#define PCI_SETUP                 0x00040010
#define PCI_DIS_REQGNT           (1<<30)
#define PCI_DIS_REQGNTA          (1<<29)
#define PCI_DIS_REQGNTB          (1<<28)
#define PCI_D2_SUPPORT           (1<<27)
#define PCI_D1_SUPPORT           (1<<26)
#define PCI_EN_TA                (1<<24)
#define PCI_EN_PCI2MMI           (1<<23)
#define PCI_EN_XIO               (1<<22)
#define PCI_BASE18_PREF          (1<<21)
#define SIZE_16M                 0x3
#define SIZE_32M                 0x4
#define SIZE_64M                 0x5
#define SIZE_128M                0x6
#define PCI_SETUP_BASE18_SIZE(X) (X<<18)
#define PCI_SETUP_BASE18_EN      (1<<17)
#define PCI_SETUP_BASE14_PREF    (1<<16)
#define PCI_SETUP_BASE14_SIZE(X) (X<<12)
#define PCI_SETUP_BASE14_EN      (1<<11)
#define PCI_SETUP_BASE10_PREF    (1<<10)
#define PCI_SETUP_BASE10_SIZE(X) (X<<7)
#define PCI_SETUP_CFGMANAGE_EN   (1<<1)
#define PCI_SETUP_PCIARB_EN      (1<<0)

#define PCI_CTRL                  0x040014
#define PCI_SWPB_DCS_PCI         (1<<16)
#define PCI_SWPB_PCI_PCI         (1<<15)
#define PCI_SWPB_PCI_DCS         (1<<14)
#define PCI_REG_WR_POST          (1<<13)
#define PCI_XIO_WR_POST          (1<<12)
#define PCI_PCI2_WR_POST         (1<<13)
#define PCI_PCI1_WR_POST         (1<<12)
#define PCI_SERR_SEEN            (1<<11)
#define PCI_B10_SPEC_RD          (1<<6)
#define PCI_B14_SPEC_RD          (1<<5)
#define PCI_B18_SPEC_RD          (1<<4)
#define PCI_B10_NOSUBWORD        (1<<3)
#define PCI_B14_NOSUBWORD        (1<<2)
#define PCI_B18_NOSUBWORD        (1<<1)
#define PCI_RETRY_TMREN          (1<<0)

#define PCI_BASE1_LO              0x040018
#define PCI_BASE1_HI              0x04001C
#define PCI_BASE2_LO              0x040020
#define PCI_BASE2_HI              0x040024
#define PCI_RDLIFETIM             0x040028
#define PCI_GPPM_ADDR             0x04002C
#define PCI_GPPM_WDAT             0x040030
#define PCI_GPPM_RDAT             0x040034
#define PCI_GPPM_CTRL             0x040038
#define GPPM_DONE                (1<<10)
#define INIT_PCI_CYCLE           (1<<9)
#define GPPM_CMD(X)              (((X)&0xf)<<4)
#define GPPM_BYTEEN(X)           ((X)&0xf)
#define PCI_UNLOCKREG             0x04003C
#define UNLOCK_SSID(X)           (((X)&0xff)<<8)
#define UNLOCK_SETUP(X)          (((X)&0xff)<<0)
#define UNLOCK_MAGIC             0xCA
#define PCI_DEV_VEND_ID           0x040040
#define DEVICE_ID(X)             (((X)>>16)&0xffff)
#define VENDOR_ID(X)             (((X)&0xffff))
#define PCI_CFG_CMDSTAT           0x040044
#define PCI_CFG_STATUS(X)            (((X)>>16)&0xffff)
#define PCI_CFG_COMMAND(X)           ((X)&0xffff)
#define PCI_CLASS_REV             0x040048
#define PCI_CLASSCODE(X)         (((X)>>8)&0xffffff)
#define PCI_REVID(X)             ((X)&0xff)
#define PCI_LAT_TMR     0x04004c
#define PCI_BASE10      0x040050
#define PCI_BASE14      0x040054
#define PCI_BASE18      0x040058
#define PCI_SUBSYS_ID   0x04006c
#define PCI_CAP_PTR     0x040074
#define PCI_CFG_MISC    0x04007c
#define PCI_PMC         0x040080
#define PCI_PWR_STATE   0x040084
#define PCI_IO          0x040088
#define PCI_SLVTUNING   0x04008C
#define PCI_DMATUNING   0x040090
#define PCI_DMAEADDR    0x040800
#define PCI_DMAIADDR    0x040804
#define PCI_DMALEN      0x040808
#define PCI_DMACTRL     0x04080C
#define PCI_XIOCTRL     0x040810
#define PCI_SEL0PROF    0x040814
#define PCI_SEL1PROF    0x040818
#define PCI_SEL2PROF    0x04081C
#define PCI_GPXIOADDR   0x040820
#define PCI_NANDCTRLS   0x400830
#define PCI_SEL3PROF    0x040834
#define PCI_SEL4PROF    0x040838
#define PCI_GPXIO_STAT  0x040FB0
#define PCI_GPXIO_IMASK 0x040FB4
#define PCI_GPXIO_ICLR  0x040FB8
#define PCI_GPXIO_ISET  0x040FBC
#define PCI_GPPM_STATUS 0x040FC0
#define GPPM_DONE      (1<<10)
#define GPPM_ERR       (1<<9)
#define GPPM_MPAR_ERR  (1<<8)
#define GPPM_PAR_ERR   (1<<7)
#define GPPM_R_MABORT  (1<<2)
#define GPPM_R_TABORT  (1<<1)
#define PCI_GPPM_IMASK  0x040FC4
#define PCI_GPPM_ICLR   0x040FC8
#define PCI_GPPM_ISET   0x040FCC
#define PCI_DMA_STATUS  0x040FD0
#define PCI_DMA_IMASK   0x040FD4
#define PCI_DMA_ICLR    0x040FD8
#define PCI_DMA_ISET    0x040FDC
#define PCI_ISTATUS     0x040FE0
#define PCI_IMASK       0x040FE4
#define PCI_ICLR        0x040FE8
#define PCI_ISET        0x040FEC
#define PCI_MOD_ID      0x040FFC

/*
 *  PCI configuration cycle AD bus definition
 */
/* Type 0 */
#define PCI_CFG_TYPE0_REG_SHF           0
#define PCI_CFG_TYPE0_FUNC_SHF          8

/* Type 1 */
#define PCI_CFG_TYPE1_REG_SHF           0
#define PCI_CFG_TYPE1_FUNC_SHF          8
#define PCI_CFG_TYPE1_DEV_SHF           11
#define PCI_CFG_TYPE1_BUS_SHF           16

/*
 *  Ethernet device DP83816 definition
 */
#define DP83816_IRQ_ETHER               66

#endif
