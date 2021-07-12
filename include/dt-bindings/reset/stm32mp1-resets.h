/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com> for STMicroelectronics.
 */

#ifndef _DT_BINDINGS_STM32MP1_RESET_H_
#define _DT_BINDINGS_STM32MP1_RESET_H_

#define MCU_HOLD_BOOT_R	2144
#define LTDC_R		3072
#define DSI_R		3076
#define DDRPERFM_R	3080
#define USBPHY_R	3088
#define SPI6_R		3136
#define I2C4_R		3138
#define I2C6_R		3139
#define USART1_R	3140
#define STGEN_R		3156
#define GPIOZ_R		3200
#define CRYP1_R		3204
#define HASH1_R		3205
#define RNG1_R		3206
#define AXIM_R		3216
#define GPU_R		3269
#define ETHMAC_R	3274
#define FMC_R		3276
#define QSPI_R		3278
#define SDMMC1_R	3280
#define SDMMC2_R	3281
#define CRC1_R		3284
#define USBH_R		3288
#define MDMA_R		3328
#define MCU_R		8225
#define TIM2_R		19456
#define TIM3_R		19457
#define TIM4_R		19458
#define TIM5_R		19459
#define TIM6_R		19460
#define TIM7_R		19461
#define TIM12_R		16462
#define TIM13_R		16463
#define TIM14_R		16464
#define LPTIM1_R	19465
#define SPI2_R		19467
#define SPI3_R		19468
#define USART2_R	19470
#define USART3_R	19471
#define UART4_R		19472
#define UART5_R		19473
#define UART7_R		19474
#define UART8_R		19475
#define I2C1_R		19477
#define I2C2_R		19478
#define I2C3_R		19479
#define I2C5_R		19480
#define SPDIF_R		19482
#define CEC_R		19483
#define DAC12_R		19485
#define MDIO_R		19847
#define TIM1_R		19520
#define TIM8_R		19521
#define TIM15_R		19522
#define TIM16_R		19523
#define TIM17_R		19524
#define SPI1_R		19528
#define SPI4_R		19529
#define SPI5_R		19530
#define USART6_R	19533
#define SAI1_R		19536
#define SAI2_R		19537
#define SAI3_R		19538
#define DFSDM_R		19540
#define FDCAN_R		19544
#define LPTIM2_R	19584
#define LPTIM3_R	19585
#define LPTIM4_R	19586
#define LPTIM5_R	19587
#define SAI4_R		19592
#define SYSCFG_R	19595
#define VREF_R		19597
#define TMPSENS_R	19600
#define PMBCTRL_R	19601
#define DMA1_R		19648
#define DMA2_R		19649
#define DMAMUX_R	19650
#define ADC12_R		19653
#define USBO_R		19656
#define SDMMC3_R	19664
#define CAMITF_R	19712
#define CRYP2_R		19716
#define HASH2_R		19717
#define RNG2_R		19718
#define CRC2_R		19719
#define HSEM_R		19723
#define MBOX_R		19724
#define GPIOA_R		19776
#define GPIOB_R		19777
#define GPIOC_R		19778
#define GPIOD_R		19779
#define GPIOE_R		19780
#define GPIOF_R		19781
#define GPIOG_R		19782
#define GPIOH_R		19783
#define GPIOI_R		19784
#define GPIOJ_R		19785
#define GPIOK_R		19786

/* SCMI reset domain identifiers */
#define RST_SCMI0_SPI6		0
#define RST_SCMI0_I2C4		1
#define RST_SCMI0_I2C6		2
#define RST_SCMI0_USART1	3
#define RST_SCMI0_STGEN		4
#define RST_SCMI0_GPIOZ		5
#define RST_SCMI0_CRYP1		6
#define RST_SCMI0_HASH1		7
#define RST_SCMI0_RNG1		8
#define RST_SCMI0_MDMA		9
#define RST_SCMI0_MCU		10
#define RST_SCMI0_MCU_HOLD_BOOT	11

#endif /* _DT_BINDINGS_STM32MP1_RESET_H_ */
