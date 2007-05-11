/*
 * include/asm-arm/arch-netx/netx-regs.h
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_NETX_REGS_H
#define __ASM_ARCH_NETX_REGS_H

/* offsets relative to the beginning of the io space */
#define NETX_OFS_SYSTEM  0x00000
#define NETX_OFS_MEMCR   0x00100
#define NETX_OFS_DPMAS   0x03000
#define NETX_OFS_GPIO    0x00800
#define NETX_OFS_PIO     0x00900
#define NETX_OFS_UART0   0x00a00
#define NETX_OFS_UART1   0x00a40
#define NETX_OFS_UART2   0x00a80
#define NETX_OF_MIIMU    0x00b00
#define NETX_OFS_SPI     0x00c00
#define NETX_OFS_I2C     0x00d00
#define NETX_OFS_SYSTIME 0x01100
#define NETX_OFS_RTC     0x01200
#define NETX_OFS_EXTBUS  0x03600
#define NETX_OFS_LCD     0x04000
#define NETX_OFS_USB     0x20000
#define NETX_OFS_XMAC0   0x60000
#define NETX_OFS_XMAC1   0x61000
#define NETX_OFS_XMAC2   0x62000
#define NETX_OFS_XMAC3   0x63000
#define NETX_OFS_XMAC(no) (0x60000 + (no) * 0x1000)
#define NETX_OFS_PFIFO   0x64000
#define NETX_OFS_XPEC0   0x70000
#define NETX_OFS_XPEC1   0x74000
#define NETX_OFS_XPEC2   0x78000
#define NETX_OFS_XPEC3   0x7c000
#define NETX_OFS_XPEC(no) (0x70000 + (no) * 0x4000)
#define NETX_OFS_VIC     0xff000

/* physical addresses */
#define NETX_PA_SYSTEM   (NETX_IO_PHYS + NETX_OFS_SYSTEM)
#define NETX_PA_MEMCR    (NETX_IO_PHYS + NETX_OFS_MEMCR)
#define NETX_PA_DPMAS    (NETX_IO_PHYS + NETX_OFS_DPMAS)
#define NETX_PA_GPIO     (NETX_IO_PHYS + NETX_OFS_GPIO)
#define NETX_PA_PIO      (NETX_IO_PHYS + NETX_OFS_PIO)
#define NETX_PA_UART0    (NETX_IO_PHYS + NETX_OFS_UART0)
#define NETX_PA_UART1    (NETX_IO_PHYS + NETX_OFS_UART1)
#define NETX_PA_UART2    (NETX_IO_PHYS + NETX_OFS_UART2)
#define NETX_PA_MIIMU    (NETX_IO_PHYS + NETX_OF_MIIMU)
#define NETX_PA_SPI      (NETX_IO_PHYS + NETX_OFS_SPI)
#define NETX_PA_I2C      (NETX_IO_PHYS + NETX_OFS_I2C)
#define NETX_PA_SYSTIME  (NETX_IO_PHYS + NETX_OFS_SYSTIME)
#define NETX_PA_RTC      (NETX_IO_PHYS + NETX_OFS_RTC)
#define NETX_PA_EXTBUS   (NETX_IO_PHYS + NETX_OFS_EXTBUS)
#define NETX_PA_LCD      (NETX_IO_PHYS + NETX_OFS_LCD)
#define NETX_PA_USB      (NETX_IO_PHYS + NETX_OFS_USB)
#define NETX_PA_XMAC0    (NETX_IO_PHYS + NETX_OFS_XMAC0)
#define NETX_PA_XMAC1    (NETX_IO_PHYS + NETX_OFS_XMAC1)
#define NETX_PA_XMAC2    (NETX_IO_PHYS + NETX_OFS_XMAC2)
#define NETX_PA_XMAC3    (NETX_IO_PHYS + NETX_OFS_XMAC3)
#define NETX_PA_XMAC(no) (NETX_IO_PHYS + NETX_OFS_XMAC(no))
#define NETX_PA_PFIFO    (NETX_IO_PHYS + NETX_OFS_PFIFO)
#define NETX_PA_XPEC0    (NETX_IO_PHYS + NETX_OFS_XPEC0)
#define NETX_PA_XPEC1    (NETX_IO_PHYS + NETX_OFS_XPEC1)
#define NETX_PA_XPEC2    (NETX_IO_PHYS + NETX_OFS_XPEC2)
#define NETX_PA_XPEC3    (NETX_IO_PHYS + NETX_OFS_XPEC3)
#define NETX_PA_XPEC(no) (NETX_IO_PHYS + NETX_OFS_XPEC(no))
#define NETX_PA_VIC      (NETX_IO_PHYS + NETX_OFS_VIC)

/* virual addresses */
#define NETX_VA_SYSTEM   (NETX_IO_VIRT + NETX_OFS_SYSTEM)
#define NETX_VA_MEMCR    (NETX_IO_VIRT + NETX_OFS_MEMCR)
#define NETX_VA_DPMAS    (NETX_IO_VIRT + NETX_OFS_DPMAS)
#define NETX_VA_GPIO     (NETX_IO_VIRT + NETX_OFS_GPIO)
#define NETX_VA_PIO      (NETX_IO_VIRT + NETX_OFS_PIO)
#define NETX_VA_UART0    (NETX_IO_VIRT + NETX_OFS_UART0)
#define NETX_VA_UART1    (NETX_IO_VIRT + NETX_OFS_UART1)
#define NETX_VA_UART2    (NETX_IO_VIRT + NETX_OFS_UART2)
#define NETX_VA_MIIMU    (NETX_IO_VIRT + NETX_OF_MIIMU)
#define NETX_VA_SPI      (NETX_IO_VIRT + NETX_OFS_SPI)
#define NETX_VA_I2C      (NETX_IO_VIRT + NETX_OFS_I2C)
#define NETX_VA_SYSTIME  (NETX_IO_VIRT + NETX_OFS_SYSTIME)
#define NETX_VA_RTC      (NETX_IO_VIRT + NETX_OFS_RTC)
#define NETX_VA_EXTBUS   (NETX_IO_VIRT + NETX_OFS_EXTBUS)
#define NETX_VA_LCD      (NETX_IO_VIRT + NETX_OFS_LCD)
#define NETX_VA_USB      (NETX_IO_VIRT + NETX_OFS_USB)
#define NETX_VA_XMAC0    (NETX_IO_VIRT + NETX_OFS_XMAC0)
#define NETX_VA_XMAC1    (NETX_IO_VIRT + NETX_OFS_XMAC1)
#define NETX_VA_XMAC2    (NETX_IO_VIRT + NETX_OFS_XMAC2)
#define NETX_VA_XMAC3    (NETX_IO_VIRT + NETX_OFS_XMAC3)
#define NETX_VA_XMAC(no) (NETX_IO_VIRT + NETX_OFS_XMAC(no))
#define NETX_VA_PFIFO    (NETX_IO_VIRT + NETX_OFS_PFIFO)
#define NETX_VA_XPEC0    (NETX_IO_VIRT + NETX_OFS_XPEC0)
#define NETX_VA_XPEC1    (NETX_IO_VIRT + NETX_OFS_XPEC1)
#define NETX_VA_XPEC2    (NETX_IO_VIRT + NETX_OFS_XPEC2)
#define NETX_VA_XPEC3    (NETX_IO_VIRT + NETX_OFS_XPEC3)
#define NETX_VA_XPEC(no) (NETX_IO_VIRT + NETX_OFS_XPEC(no))
#define NETX_VA_VIC      (NETX_IO_VIRT + NETX_OFS_VIC)

/*********************************
 * System functions              *
 *********************************/

/* Registers */
#define NETX_SYSTEM_REG(ofs)            __io(NETX_VA_SYSTEM + (ofs))
#define NETX_SYSTEM_BOO_SR          NETX_SYSTEM_REG(0x00)
#define NETX_SYSTEM_IOC_CR          NETX_SYSTEM_REG(0x04)
#define NETX_SYSTEM_IOC_MR          NETX_SYSTEM_REG(0x08)

/* FIXME: Docs are not consistent */
/* #define NETX_SYSTEM_RES_CR          NETX_SYSTEM_REG(0x08) */
#define NETX_SYSTEM_RES_CR          NETX_SYSTEM_REG(0x0c)

#define NETX_SYSTEM_PHY_CONTROL     NETX_SYSTEM_REG(0x10)
#define NETX_SYSTEM_REV             NETX_SYSTEM_REG(0x34)
#define NETX_SYSTEM_IOC_ACCESS_KEY  NETX_SYSTEM_REG(0x70)
#define NETX_SYSTEM_WDG_TR          NETX_SYSTEM_REG(0x200)
#define NETX_SYSTEM_WDG_CTR         NETX_SYSTEM_REG(0x204)
#define NETX_SYSTEM_WDG_IRQ_TIMEOUT NETX_SYSTEM_REG(0x208)
#define NETX_SYSTEM_WDG_RES_TIMEOUT NETX_SYSTEM_REG(0x20c)

/* Bits */
#define NETX_SYSTEM_RES_CR_RSTIN         (1<<0)
#define NETX_SYSTEM_RES_CR_WDG_RES       (1<<1)
#define NETX_SYSTEM_RES_CR_HOST_RES      (1<<2)
#define NETX_SYSTEM_RES_CR_FIRMW_RES     (1<<3)
#define NETX_SYSTEM_RES_CR_XPEC0_RES     (1<<4)
#define NETX_SYSTEM_RES_CR_XPEC1_RES     (1<<5)
#define NETX_SYSTEM_RES_CR_XPEC2_RES     (1<<6)
#define NETX_SYSTEM_RES_CR_XPEC3_RES     (1<<7)
#define NETX_SYSTEM_RES_CR_DIS_XPEC0_RES (1<<16)
#define NETX_SYSTEM_RES_CR_DIS_XPEC1_RES (1<<17)
#define NETX_SYSTEM_RES_CR_DIS_XPEC2_RES (1<<18)
#define NETX_SYSTEM_RES_CR_DIS_XPEC3_RES (1<<19)
#define NETX_SYSTEM_RES_CR_FIRMW_FLG0    (1<<20)
#define NETX_SYSTEM_RES_CR_FIRMW_FLG1    (1<<21)
#define NETX_SYSTEM_RES_CR_FIRMW_FLG2    (1<<22)
#define NETX_SYSTEM_RES_CR_FIRMW_FLG3    (1<<23)
#define NETX_SYSTEM_RES_CR_FIRMW_RES_EN  (1<<24)
#define NETX_SYSTEM_RES_CR_RSTOUT        (1<<25)
#define NETX_SYSTEM_RES_CR_EN_RSTOUT     (1<<26)

#define PHY_CONTROL_RESET            (1<<31)
#define PHY_CONTROL_SIM_BYP          (1<<30)
#define PHY_CONTROL_CLK_XLATIN       (1<<29)
#define PHY_CONTROL_PHY1_EN          (1<<21)
#define PHY_CONTROL_PHY1_NP_MSG_CODE
#define PHY_CONTROL_PHY1_AUTOMDIX    (1<<17)
#define PHY_CONTROL_PHY1_FIXMODE     (1<<16)
#define PHY_CONTROL_PHY1_MODE(mode)  (((mode) & 0x7) << 13)
#define PHY_CONTROL_PHY0_EN          (1<<12)
#define PHY_CONTROL_PHY0_NP_MSG_CODE
#define PHY_CONTROL_PHY0_AUTOMDIX    (1<<8)
#define PHY_CONTROL_PHY0_FIXMODE     (1<<7)
#define PHY_CONTROL_PHY0_MODE(mode)  (((mode) & 0x7) << 4)
#define PHY_CONTROL_PHY_ADDRESS(adr) ((adr) & 0xf)

#define PHY_MODE_10BASE_T_HALF      0
#define PHY_MODE_10BASE_T_FULL      1
#define PHY_MODE_100BASE_TX_FX_FULL 2
#define PHY_MODE_100BASE_TX_FX_HALF 3
#define PHY_MODE_100BASE_TX_HALF    4
#define PHY_MODE_REPEATER           5
#define PHY_MODE_POWER_DOWN         6
#define PHY_MODE_ALL                7

/* Bits */
#define VECT_CNTL_ENABLE               (1 << 5)

/*******************************
 * GPIO and timer module       *
 *******************************/

/* Registers */
#define NETX_GPIO_REG(ofs)                     __io(NETX_VA_GPIO + (ofs))
#define NETX_GPIO_CFG(gpio)                NETX_GPIO_REG(0x0  + ((gpio)<<2))
#define NETX_GPIO_THRESHOLD_CAPTURE(gpio)  NETX_GPIO_REG(0x40 + ((gpio)<<2))
#define NETX_GPIO_COUNTER_CTRL(counter)    NETX_GPIO_REG(0x80 + ((counter)<<2))
#define NETX_GPIO_COUNTER_MAX(counter)     NETX_GPIO_REG(0x94 + ((counter)<<2))
#define NETX_GPIO_COUNTER_CURRENT(counter) NETX_GPIO_REG(0xa8 + ((counter)<<2))
#define NETX_GPIO_IRQ_ENABLE               NETX_GPIO_REG(0xbc)
#define NETX_GPIO_IRQ_DISABLE              NETX_GPIO_REG(0xc0)
#define NETX_GPIO_SYSTIME_NS_CMP           NETX_GPIO_REG(0xc4)
#define NETX_GPIO_LINE                     NETX_GPIO_REG(0xc8)
#define NETX_GPIO_IRQ                      NETX_GPIO_REG(0xd0)

/* Bits */
#define NETX_GPIO_CFG_IOCFG_GP_INPUT                 (0x0)
#define NETX_GPIO_CFG_IOCFG_GP_OUTPUT                (0x1)
#define NETX_GPIO_CFG_IOCFG_GP_UART                  (0x2)
#define NETX_GPIO_CFG_INV                            (1<<2)
#define NETX_GPIO_CFG_MODE_INPUT_READ                (0<<3)
#define NETX_GPIO_CFG_MODE_INPUT_CAPTURE_CONT_RISING (1<<3)
#define NETX_GPIO_CFG_MODE_INPUT_CAPTURE_ONCE_RISING (2<<3)
#define NETX_GPIO_CFG_MODE_INPUT_CAPTURE_HIGH_LEVEL  (3<<3)
#define NETX_GPIO_CFG_COUNT_REF_COUNTER0             (0<<5)
#define NETX_GPIO_CFG_COUNT_REF_COUNTER1             (1<<5)
#define NETX_GPIO_CFG_COUNT_REF_COUNTER2             (2<<5)
#define NETX_GPIO_CFG_COUNT_REF_COUNTER3             (3<<5)
#define NETX_GPIO_CFG_COUNT_REF_COUNTER4             (4<<5)
#define NETX_GPIO_CFG_COUNT_REF_SYSTIME              (7<<5)

#define NETX_GPIO_COUNTER_CTRL_RUN                   (1<<0)
#define NETX_GPIO_COUNTER_CTRL_SYM                   (1<<1)
#define NETX_GPIO_COUNTER_CTRL_ONCE                  (1<<2)
#define NETX_GPIO_COUNTER_CTRL_IRQ_EN                (1<<3)
#define NETX_GPIO_COUNTER_CTRL_CNT_EVENT             (1<<4)
#define NETX_GPIO_COUNTER_CTRL_RST_EN                (1<<5)
#define NETX_GPIO_COUNTER_CTRL_SEL_EVENT             (1<<6)
#define NETX_GPIO_COUNTER_CTRL_GPIO_REF /* FIXME */

#define GPIO_BIT(gpio)                     (1<<(gpio))
#define COUNTER_BIT(counter)               ((1<<16)<<(counter))

/*******************************
 * PIO                         *
 *******************************/

/* Registers */
#define NETX_PIO_REG(ofs)        __io(NETX_VA_PIO + (ofs))
#define NETX_PIO_INPIO       NETX_PIO_REG(0x0)
#define NETX_PIO_OUTPIO      NETX_PIO_REG(0x4)
#define NETX_PIO_OEPIO       NETX_PIO_REG(0x8)

/*******************************
 * MII Unit                    *
 *******************************/

/* Registers */
#define NETX_MIIMU           __io(NETX_VA_MIIMU)

/* Bits */
#define MIIMU_SNRDY        (1<<0)
#define MIIMU_PREAMBLE     (1<<1)
#define MIIMU_OPMODE_WRITE (1<<2)
#define MIIMU_MDC_PERIOD   (1<<3)
#define MIIMU_PHY_NRES     (1<<4)
#define MIIMU_RTA          (1<<5)
#define MIIMU_REGADDR(adr) (((adr) & 0x1f) << 6)
#define MIIMU_PHYADDR(adr) (((adr) & 0x1f) << 11)
#define MIIMU_DATA(data)   (((data) & 0xffff) << 16)

/*******************************
 * xmac / xpec                 *
 *******************************/

/* XPEC register offsets relative to NETX_VA_XPEC(no) */
#define NETX_XPEC_R0_OFS           0x00
#define NETX_XPEC_R1_OFS           0x04
#define NETX_XPEC_R2_OFS           0x08
#define NETX_XPEC_R3_OFS           0x0c
#define NETX_XPEC_R4_OFS           0x10
#define NETX_XPEC_R5_OFS           0x14
#define NETX_XPEC_R6_OFS           0x18
#define NETX_XPEC_R7_OFS           0x1c
#define NETX_XPEC_RANGE01_OFS      0x20
#define NETX_XPEC_RANGE23_OFS      0x24
#define NETX_XPEC_RANGE45_OFS      0x28
#define NETX_XPEC_RANGE67_OFS      0x2c
#define NETX_XPEC_PC_OFS           0x48
#define NETX_XPEC_TIMER_OFS(timer) (0x30 + ((timer)<<2))
#define NETX_XPEC_IRQ_OFS          0x8c
#define NETX_XPEC_SYSTIME_NS_OFS   0x90
#define NETX_XPEC_FIFO_DATA_OFS    0x94
#define NETX_XPEC_SYSTIME_S_OFS    0x98
#define NETX_XPEC_ADC_OFS          0x9c
#define NETX_XPEC_URX_COUNT_OFS    0x40
#define NETX_XPEC_UTX_COUNT_OFS    0x44
#define NETX_XPEC_PC_OFS           0x48
#define NETX_XPEC_ZERO_OFS         0x4c
#define NETX_XPEC_STATCFG_OFS      0x50
#define NETX_XPEC_EC_MASKA_OFS     0x54
#define NETX_XPEC_EC_MASKB_OFS     0x58
#define NETX_XPEC_EC_MASK0_OFS     0x5c
#define NETX_XPEC_EC_MASK8_OFS     0x7c
#define NETX_XPEC_EC_MASK9_OFS     0x80
#define NETX_XPEC_XPU_HOLD_PC_OFS  0x100
#define NETX_XPEC_RAM_START_OFS    0x2000

/* Bits */
#define XPU_HOLD_PC (1<<0)

/* XMAC register offsets relative to NETX_VA_XMAC(no) */
#define NETX_XMAC_RPU_PROGRAM_START_OFS       0x000
#define NETX_XMAC_RPU_PROGRAM_END_OFS         0x3ff
#define NETX_XMAC_TPU_PROGRAM_START_OFS       0x400
#define NETX_XMAC_TPU_PROGRAM_END_OFS         0x7ff
#define NETX_XMAC_RPU_HOLD_PC_OFS             0xa00
#define NETX_XMAC_TPU_HOLD_PC_OFS             0xa04
#define NETX_XMAC_STATUS_SHARED0_OFS          0x840
#define NETX_XMAC_CONFIG_SHARED0_OFS          0x844
#define NETX_XMAC_STATUS_SHARED1_OFS          0x848
#define NETX_XMAC_CONFIG_SHARED1_OFS          0x84c
#define NETX_XMAC_STATUS_SHARED2_OFS          0x850
#define NETX_XMAC_CONFIG_SHARED2_OFS          0x854
#define NETX_XMAC_STATUS_SHARED3_OFS          0x858
#define NETX_XMAC_CONFIG_SHARED3_OFS          0x85c

#define RPU_HOLD_PC            (1<<15)
#define TPU_HOLD_PC            (1<<15)

/*******************************
 * Pointer FIFO                *
 *******************************/

/* Registers */
#define NETX_PFIFO_REG(ofs)               __io(NETX_VA_PFIFO + (ofs))
#define NETX_PFIFO_BASE(pfifo)        NETX_PFIFO_REG(0x00 + ((pfifo)<<2))
#define NETX_PFIFO_BORDER_BASE(pfifo) NETX_PFIFO_REG(0x80 + ((pfifo)<<2))
#define NETX_PFIFO_RESET              NETX_PFIFO_REG(0x100)
#define NETX_PFIFO_FULL               NETX_PFIFO_REG(0x104)
#define NETX_PFIFO_EMPTY              NETX_PFIFO_REG(0x108)
#define NETX_PFIFO_OVEFLOW            NETX_PFIFO_REG(0x10c)
#define NETX_PFIFO_UNDERRUN           NETX_PFIFO_REG(0x110)
#define NETX_PFIFO_FILL_LEVEL(pfifo)  NETX_PFIFO_REG(0x180 + ((pfifo)<<2))
#define NETX_PFIFO_XPEC_ISR(xpec)     NETX_PFIFO_REG(0x400 + ((xpec) << 2))

/*******************************
 * Dual Port Memory            *
 *******************************/

/* Registers */
#define NETX_DPMAS_REG(ofs)               __io(NETX_VA_DPMAS + (ofs))
#define NETX_DPMAS_SYS_STAT           NETX_DPMAS_REG(0x4d8)
#define NETX_DPMAS_INT_STAT           NETX_DPMAS_REG(0x4e0)
#define NETX_DPMAS_INT_EN             NETX_DPMAS_REG(0x4f0)
#define NETX_DPMAS_IF_CONF0           NETX_DPMAS_REG(0x608)
#define NETX_DPMAS_IF_CONF1           NETX_DPMAS_REG(0x60c)
#define NETX_DPMAS_EXT_CONFIG(cs)     NETX_DPMAS_REG(0x610 + 4 * (cs))
#define NETX_DPMAS_IO_MODE0           NETX_DPMAS_REG(0x620) /* I/O 32..63 */
#define NETX_DPMAS_DRV_EN0            NETX_DPMAS_REG(0x624)
#define NETX_DPMAS_DATA0              NETX_DPMAS_REG(0x628)
#define NETX_DPMAS_IO_MODE1           NETX_DPMAS_REG(0x630) /* I/O 64..84 */
#define NETX_DPMAS_DRV_EN1            NETX_DPMAS_REG(0x634)
#define NETX_DPMAS_DATA1              NETX_DPMAS_REG(0x638)

/* Bits */
#define NETX_DPMAS_INT_EN_GLB_EN         (1<<31)
#define NETX_DPMAS_INT_EN_MEM_LCK        (1<<30)
#define NETX_DPMAS_INT_EN_WDG            (1<<29)
#define NETX_DPMAS_INT_EN_PIO72          (1<<28)
#define NETX_DPMAS_INT_EN_PIO47          (1<<27)
#define NETX_DPMAS_INT_EN_PIO40          (1<<26)
#define NETX_DPMAS_INT_EN_PIO36          (1<<25)
#define NETX_DPMAS_INT_EN_PIO35          (1<<24)

#define NETX_DPMAS_IF_CONF0_HIF_DISABLED (0<<28)
#define NETX_DPMAS_IF_CONF0_HIF_EXT_BUS  (1<<28)
#define NETX_DPMAS_IF_CONF0_HIF_UP_8BIT  (2<<28)
#define NETX_DPMAS_IF_CONF0_HIF_UP_16BIT (3<<28)
#define NETX_DPMAS_IF_CONF0_HIF_IO       (4<<28)
#define NETX_DPMAS_IF_CONF0_WAIT_DRV_PP  (1<<14)
#define NETX_DPMAS_IF_CONF0_WAIT_DRV_OD  (2<<14)
#define NETX_DPMAS_IF_CONF0_WAIT_DRV_TRI (3<<14)

#define NETX_DPMAS_IF_CONF1_IRQ_POL_PIO35 (1<<26)
#define NETX_DPMAS_IF_CONF1_IRQ_POL_PIO36 (1<<27)
#define NETX_DPMAS_IF_CONF1_IRQ_POL_PIO40 (1<<28)
#define NETX_DPMAS_IF_CONF1_IRQ_POL_PIO47 (1<<29)
#define NETX_DPMAS_IF_CONF1_IRQ_POL_PIO72 (1<<30)

#define NETX_EXT_CONFIG_TALEWIDTH(x) (((x) & 0x7) << 29)
#define NETX_EXT_CONFIG_TADRHOLD(x)  (((x) & 0x7) << 26)
#define NETX_EXT_CONFIG_TCSON(x)     (((x) & 0x7) << 23)
#define NETX_EXT_CONFIG_TRDON(x)     (((x) & 0x7) << 20)
#define NETX_EXT_CONFIG_TWRON(x)     (((x) & 0x7)  << 17)
#define NETX_EXT_CONFIG_TWROFF(x)    (((x) & 0x1f) << 12)
#define NETX_EXT_CONFIG_TRDWRCYC(x)  (((x) & 0x1f) << 7)
#define NETX_EXT_CONFIG_WAIT_POL     (1<<6)
#define NETX_EXT_CONFIG_WAIT_EN      (1<<5)
#define NETX_EXT_CONFIG_NRD_MODE     (1<<4)
#define NETX_EXT_CONFIG_DS_MODE      (1<<3)
#define NETX_EXT_CONFIG_NWR_MODE     (1<<2)
#define NETX_EXT_CONFIG_16BIT        (1<<1)
#define NETX_EXT_CONFIG_CS_ENABLE    (1<<0)

#define NETX_DPMAS_IO_MODE0_WRL   (1<<13)
#define NETX_DPMAS_IO_MODE0_WAIT  (1<<14)
#define NETX_DPMAS_IO_MODE0_READY (1<<15)
#define NETX_DPMAS_IO_MODE0_CS0   (1<<19)
#define NETX_DPMAS_IO_MODE0_EXTRD (1<<20)

#define NETX_DPMAS_IO_MODE1_CS2           (1<<15)
#define NETX_DPMAS_IO_MODE1_CS1           (1<<16)
#define NETX_DPMAS_IO_MODE1_SAMPLE_NPOR   (0<<30)
#define NETX_DPMAS_IO_MODE1_SAMPLE_100MHZ (1<<30)
#define NETX_DPMAS_IO_MODE1_SAMPLE_NPIO36 (2<<30)
#define NETX_DPMAS_IO_MODE1_SAMPLE_PIO36  (3<<30)

/*******************************
 * I2C                         *
 *******************************/
#define NETX_I2C_REG(ofs)	__io(NETX_VA_I2C, (ofs))
#define NETX_I2C_CTRL	NETX_I2C_REG(0x0)
#define NETX_I2C_DATA	NETX_I2C_REG(0x4)

#endif /* __ASM_ARCH_NETX_REGS_H */
