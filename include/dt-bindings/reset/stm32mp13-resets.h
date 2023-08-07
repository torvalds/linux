/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#ifndef _DT_BINDINGS_STM32MP13_RESET_H_
#define _DT_BINDINGS_STM32MP13_RESET_H_

#define TIM2_R		13568
#define TIM3_R		13569
#define TIM4_R		13570
#define TIM5_R		13571
#define TIM6_R		13572
#define TIM7_R		13573
#define LPTIM1_R	13577
#define SPI2_R		13579
#define SPI3_R		13580
#define USART3_R	13583
#define UART4_R		13584
#define UART5_R		13585
#define UART7_R		13586
#define UART8_R		13587
#define I2C1_R		13589
#define I2C2_R		13590
#define SPDIF_R		13594
#define TIM1_R		13632
#define TIM8_R		13633
#define SPI1_R		13640
#define USART6_R	13645
#define SAI1_R		13648
#define SAI2_R		13649
#define DFSDM_R		13652
#define FDCAN_R		13656
#define LPTIM2_R	13696
#define LPTIM3_R	13697
#define LPTIM4_R	13698
#define LPTIM5_R	13699
#define SYSCFG_R	13707
#define VREF_R		13709
#define DTS_R		13712
#define PMBCTRL_R	13713
#define LTDC_R		13760
#define DCMIPP_R	13761
#define DDRPERFM_R	13768
#define USBPHY_R	13776
#define STGEN_R		13844
#define USART1_R	13888
#define USART2_R	13889
#define SPI4_R		13890
#define SPI5_R		13891
#define I2C3_R		13892
#define I2C4_R		13893
#define I2C5_R		13894
#define TIM12_R		13895
#define TIM13_R		13896
#define TIM14_R		13897
#define TIM15_R		13898
#define TIM16_R		13899
#define TIM17_R		13900
#define DMA1_R		13952
#define DMA2_R		13953
#define DMAMUX1_R	13954
#define DMA3_R		13955
#define DMAMUX2_R	13956
#define ADC1_R		13957
#define ADC2_R		13958
#define USBO_R		13960
#define GPIOA_R		14080
#define GPIOB_R		14081
#define GPIOC_R		14082
#define GPIOD_R		14083
#define GPIOE_R		14084
#define GPIOF_R		14085
#define GPIOG_R		14086
#define GPIOH_R		14087
#define GPIOI_R		14088
#define TSC_R		14095
#define PKA_R		14146
#define SAES_R		14147
#define CRYP1_R		14148
#define HASH1_R		14149
#define RNG1_R		14150
#define AXIMC_R		14160
#define MDMA_R		14208
#define MCE_R		14209
#define ETH1MAC_R	14218
#define FMC_R		14220
#define QSPI_R		14222
#define SDMMC1_R	14224
#define SDMMC2_R	14225
#define CRC1_R		14228
#define USBH_R		14232
#define ETH2MAC_R	14238

/* SCMI reset domain identifiers */
#define RST_SCMI_LTDC		0
#define RST_SCMI_MDMA		1

#endif /* _DT_BINDINGS_STM32MP13_RESET_H_ */
