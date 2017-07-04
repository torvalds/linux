/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _LINUX_STM32_GPTIMER_H_
#define _LINUX_STM32_GPTIMER_H_

#include <linux/clk.h>
#include <linux/regmap.h>

#define TIM_CR1		0x00	/* Control Register 1      */
#define TIM_CR2		0x04	/* Control Register 2      */
#define TIM_SMCR	0x08	/* Slave mode control reg  */
#define TIM_DIER	0x0C	/* DMA/interrupt register  */
#define TIM_SR		0x10	/* Status register	   */
#define TIM_EGR		0x14	/* Event Generation Reg    */
#define TIM_CCMR1	0x18	/* Capt/Comp 1 Mode Reg    */
#define TIM_CCMR2	0x1C	/* Capt/Comp 2 Mode Reg    */
#define TIM_CCER	0x20	/* Capt/Comp Enable Reg    */
#define TIM_CNT		0x24	/* Counter		   */
#define TIM_PSC		0x28	/* Prescaler               */
#define TIM_ARR		0x2c	/* Auto-Reload Register    */
#define TIM_CCR1	0x34	/* Capt/Comp Register 1    */
#define TIM_CCR2	0x38	/* Capt/Comp Register 2    */
#define TIM_CCR3	0x3C	/* Capt/Comp Register 3    */
#define TIM_CCR4	0x40	/* Capt/Comp Register 4    */
#define TIM_BDTR	0x44	/* Break and Dead-Time Reg */

#define TIM_CR1_CEN	BIT(0)	/* Counter Enable	   */
#define TIM_CR1_DIR	BIT(4)  /* Counter Direction	   */
#define TIM_CR1_ARPE	BIT(7)	/* Auto-reload Preload Ena */
#define TIM_CR2_MMS	(BIT(4) | BIT(5) | BIT(6)) /* Master mode selection */
#define TIM_CR2_MMS2	GENMASK(23, 20) /* Master mode selection 2 */
#define TIM_SMCR_SMS	(BIT(0) | BIT(1) | BIT(2)) /* Slave mode selection */
#define TIM_SMCR_TS	(BIT(4) | BIT(5) | BIT(6)) /* Trigger selection */
#define TIM_DIER_UIE	BIT(0)	/* Update interrupt	   */
#define TIM_SR_UIF	BIT(0)	/* Update interrupt flag   */
#define TIM_EGR_UG	BIT(0)	/* Update Generation       */
#define TIM_CCMR_PE	BIT(3)	/* Channel Preload Enable  */
#define TIM_CCMR_M1	(BIT(6) | BIT(5))  /* Channel PWM Mode 1 */
#define TIM_CCER_CC1E	BIT(0)	/* Capt/Comp 1  out Ena    */
#define TIM_CCER_CC1P	BIT(1)	/* Capt/Comp 1  Polarity   */
#define TIM_CCER_CC1NE	BIT(2)	/* Capt/Comp 1N out Ena    */
#define TIM_CCER_CC1NP	BIT(3)	/* Capt/Comp 1N Polarity   */
#define TIM_CCER_CC2E	BIT(4)	/* Capt/Comp 2  out Ena    */
#define TIM_CCER_CC3E	BIT(8)	/* Capt/Comp 3  out Ena    */
#define TIM_CCER_CC4E	BIT(12)	/* Capt/Comp 4  out Ena    */
#define TIM_CCER_CCXE	(BIT(0) | BIT(4) | BIT(8) | BIT(12))
#define TIM_BDTR_BKE	BIT(12) /* Break input enable	   */
#define TIM_BDTR_BKP	BIT(13) /* Break input polarity	   */
#define TIM_BDTR_AOE	BIT(14)	/* Automatic Output Enable */
#define TIM_BDTR_MOE	BIT(15)	/* Main Output Enable      */
#define TIM_BDTR_BKF	(BIT(16) | BIT(17) | BIT(18) | BIT(19))
#define TIM_BDTR_BK2F	(BIT(20) | BIT(21) | BIT(22) | BIT(23))
#define TIM_BDTR_BK2E	BIT(24) /* Break 2 input enable	   */
#define TIM_BDTR_BK2P	BIT(25) /* Break 2 input polarity  */

#define MAX_TIM_PSC		0xFFFF
#define TIM_CR2_MMS_SHIFT	4
#define TIM_CR2_MMS2_SHIFT	20
#define TIM_SMCR_TS_SHIFT	4
#define TIM_BDTR_BKF_MASK	0xF
#define TIM_BDTR_BKF_SHIFT	16
#define TIM_BDTR_BK2F_SHIFT	20

struct stm32_timers {
	struct clk *clk;
	struct regmap *regmap;
	u32 max_arr;
};
#endif
