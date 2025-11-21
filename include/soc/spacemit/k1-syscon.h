/* SPDX-License-Identifier: GPL-2.0-only */

/* SpacemiT clock and reset driver definitions for the K1 SoC */

#ifndef __SOC_K1_SYSCON_H__
#define __SOC_K1_SYSCON_H__

/* Auxiliary device used to represent a CCU reset controller */
struct spacemit_ccu_adev {
	struct auxiliary_device adev;
	struct regmap *regmap;
};

static inline struct spacemit_ccu_adev *
to_spacemit_ccu_adev(struct auxiliary_device *adev)
{
	return container_of(adev, struct spacemit_ccu_adev, adev);
}

/* APBS register offset */
#define APBS_PLL1_SWCR1			0x100
#define APBS_PLL1_SWCR2			0x104
#define APBS_PLL1_SWCR3			0x108
#define APBS_PLL2_SWCR1			0x118
#define APBS_PLL2_SWCR2			0x11c
#define APBS_PLL2_SWCR3			0x120
#define APBS_PLL3_SWCR1			0x124
#define APBS_PLL3_SWCR2			0x128
#define APBS_PLL3_SWCR3			0x12c

/* MPMU register offset */
#define MPMU_POSR			0x0010
#define MPMU_FCCR			0x0008
#define  POSR_PLL1_LOCK			BIT(27)
#define  POSR_PLL2_LOCK			BIT(28)
#define  POSR_PLL3_LOCK			BIT(29)
#define MPMU_SUCCR			0x0014
#define MPMU_ISCCR			0x0044
#define MPMU_WDTPCR			0x0200
#define MPMU_RIPCCR			0x0210
#define MPMU_ACGR			0x1024
#define MPMU_APBCSCR			0x1050
#define MPMU_SUCCR_1			0x10b0

/* APBC register offset */
#define APBC_UART1_CLK_RST		0x00
#define APBC_UART2_CLK_RST		0x04
#define APBC_GPIO_CLK_RST		0x08
#define APBC_PWM0_CLK_RST		0x0c
#define APBC_PWM1_CLK_RST		0x10
#define APBC_PWM2_CLK_RST		0x14
#define APBC_PWM3_CLK_RST		0x18
#define APBC_TWSI8_CLK_RST		0x20
#define APBC_UART3_CLK_RST		0x24
#define APBC_RTC_CLK_RST		0x28
#define APBC_TWSI0_CLK_RST		0x2c
#define APBC_TWSI1_CLK_RST		0x30
#define APBC_TIMERS1_CLK_RST		0x34
#define APBC_TWSI2_CLK_RST		0x38
#define APBC_AIB_CLK_RST		0x3c
#define APBC_TWSI4_CLK_RST		0x40
#define APBC_TIMERS2_CLK_RST		0x44
#define APBC_ONEWIRE_CLK_RST		0x48
#define APBC_TWSI5_CLK_RST		0x4c
#define APBC_DRO_CLK_RST		0x58
#define APBC_IR_CLK_RST			0x5c
#define APBC_TWSI6_CLK_RST		0x60
#define APBC_COUNTER_CLK_SEL		0x64
#define APBC_TWSI7_CLK_RST		0x68
#define APBC_TSEN_CLK_RST		0x6c
#define APBC_UART4_CLK_RST		0x70
#define APBC_UART5_CLK_RST		0x74
#define APBC_UART6_CLK_RST		0x78
#define APBC_SSP3_CLK_RST		0x7c
#define APBC_SSPA0_CLK_RST		0x80
#define APBC_SSPA1_CLK_RST		0x84
#define APBC_IPC_AP2AUD_CLK_RST		0x90
#define APBC_UART7_CLK_RST		0x94
#define APBC_UART8_CLK_RST		0x98
#define APBC_UART9_CLK_RST		0x9c
#define APBC_CAN0_CLK_RST		0xa0
#define APBC_PWM4_CLK_RST		0xa8
#define APBC_PWM5_CLK_RST		0xac
#define APBC_PWM6_CLK_RST		0xb0
#define APBC_PWM7_CLK_RST		0xb4
#define APBC_PWM8_CLK_RST		0xb8
#define APBC_PWM9_CLK_RST		0xbc
#define APBC_PWM10_CLK_RST		0xc0
#define APBC_PWM11_CLK_RST		0xc4
#define APBC_PWM12_CLK_RST		0xc8
#define APBC_PWM13_CLK_RST		0xcc
#define APBC_PWM14_CLK_RST		0xd0
#define APBC_PWM15_CLK_RST		0xd4
#define APBC_PWM16_CLK_RST		0xd8
#define APBC_PWM17_CLK_RST		0xdc
#define APBC_PWM18_CLK_RST		0xe0
#define APBC_PWM19_CLK_RST		0xe4

/* APMU register offset */
#define APMU_JPG_CLK_RES_CTRL		0x020
#define APMU_CSI_CCIC2_CLK_RES_CTRL	0x024
#define APMU_ISP_CLK_RES_CTRL		0x038
#define APMU_LCD_CLK_RES_CTRL1		0x044
#define APMU_LCD_SPI_CLK_RES_CTRL	0x048
#define APMU_LCD_CLK_RES_CTRL2		0x04c
#define APMU_CCIC_CLK_RES_CTRL		0x050
#define APMU_SDH0_CLK_RES_CTRL		0x054
#define APMU_SDH1_CLK_RES_CTRL		0x058
#define APMU_USB_CLK_RES_CTRL		0x05c
#define APMU_QSPI_CLK_RES_CTRL		0x060
#define APMU_DMA_CLK_RES_CTRL		0x064
#define APMU_AES_CLK_RES_CTRL		0x068
#define APMU_VPU_CLK_RES_CTRL		0x0a4
#define APMU_GPU_CLK_RES_CTRL		0x0cc
#define APMU_SDH2_CLK_RES_CTRL		0x0e0
#define APMU_PMUA_MC_CTRL		0x0e8
#define APMU_PMU_CC2_AP			0x100
#define APMU_PMUA_EM_CLK_RES_CTRL	0x104
#define APMU_AUDIO_CLK_RES_CTRL		0x14c
#define APMU_HDMI_CLK_RES_CTRL		0x1b8
#define APMU_CCI550_CLK_CTRL		0x300
#define APMU_ACLK_CLK_CTRL		0x388
#define APMU_CPU_C0_CLK_CTRL		0x38C
#define APMU_CPU_C1_CLK_CTRL		0x390
#define APMU_PCIE_CLK_RES_CTRL_0	0x3cc
#define APMU_PCIE_CLK_RES_CTRL_1	0x3d4
#define APMU_PCIE_CLK_RES_CTRL_2	0x3dc
#define APMU_EMAC0_CLK_RES_CTRL		0x3e4
#define APMU_EMAC1_CLK_RES_CTRL		0x3ec

/* RCPU register offsets */
#define RCPU_SSP0_CLK_RST		0x0028
#define RCPU_I2C0_CLK_RST		0x0030
#define RCPU_UART1_CLK_RST		0x003c
#define RCPU_CAN_CLK_RST		0x0048
#define RCPU_IR_CLK_RST			0x004c
#define RCPU_UART0_CLK_RST		0x00d8
#define AUDIO_HDMI_CLK_CTRL		0x2044

/* RCPU2 register offsets */
#define RCPU2_PWM0_CLK_RST		0x0000
#define RCPU2_PWM1_CLK_RST		0x0004
#define RCPU2_PWM2_CLK_RST		0x0008
#define RCPU2_PWM3_CLK_RST		0x000c
#define RCPU2_PWM4_CLK_RST		0x0010
#define RCPU2_PWM5_CLK_RST		0x0014
#define RCPU2_PWM6_CLK_RST		0x0018
#define RCPU2_PWM7_CLK_RST		0x001c
#define RCPU2_PWM8_CLK_RST		0x0020
#define RCPU2_PWM9_CLK_RST		0x0024

/* APBC2 register offsets */
#define APBC2_UART1_CLK_RST		0x0000
#define APBC2_SSP2_CLK_RST		0x0004
#define APBC2_TWSI3_CLK_RST		0x0008
#define APBC2_RTC_CLK_RST		0x000c
#define APBC2_TIMERS0_CLK_RST		0x0010
#define APBC2_KPC_CLK_RST		0x0014
#define APBC2_GPIO_CLK_RST		0x001c

#endif /* __SOC_K1_SYSCON_H__ */
