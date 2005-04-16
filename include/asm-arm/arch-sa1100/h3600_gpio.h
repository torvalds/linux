/*
 *
 * Definitions for H3600 Handheld Computer
 *
 * Copyright 2000 Compaq Computer Corporation.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks.
 *
 * History:
 *
 * 2001-10-??   Andrew Christian   Added support for iPAQ H3800
 *
 */

#ifndef _INCLUDE_H3600_GPIO_H_
#define _INCLUDE_H3600_GPIO_H_

/*
 * GPIO lines that are common across ALL iPAQ models are in "h3600.h"
 * This file contains machine-specific definitions
 */

#define GPIO_H3600_SUSPEND              GPIO_GPIO (0)
/* GPIO[2:9] used by LCD on H3600/3800, used as GPIO on H3100 */
#define GPIO_H3100_BT_ON		GPIO_GPIO (2)
#define GPIO_H3100_GPIO3		GPIO_GPIO (3)
#define GPIO_H3100_QMUTE		GPIO_GPIO (4)
#define GPIO_H3100_LCD_3V_ON		GPIO_GPIO (5)
#define GPIO_H3100_AUD_ON		GPIO_GPIO (6)
#define GPIO_H3100_AUD_PWR_ON		GPIO_GPIO (7)
#define GPIO_H3100_IR_ON		GPIO_GPIO (8)
#define GPIO_H3100_IR_FSEL		GPIO_GPIO (9)

/* for H3600, audio sample rate clock generator */
#define GPIO_H3600_CLK_SET0		GPIO_GPIO (12)
#define GPIO_H3600_CLK_SET1		GPIO_GPIO (13)

#define GPIO_H3600_ACTION_BUTTON	GPIO_GPIO (18)
#define GPIO_H3600_SOFT_RESET           GPIO_GPIO (20)   /* Also known as BATT_FAULT */
#define GPIO_H3600_OPT_LOCK		GPIO_GPIO (22)
#define GPIO_H3600_OPT_DET		GPIO_GPIO (27)

/* H3800 specific pins */
#define GPIO_H3800_AC_IN                GPIO_GPIO (12)
#define GPIO_H3800_COM_DSR              GPIO_GPIO (13)
#define GPIO_H3800_MMC_INT              GPIO_GPIO (18)
#define GPIO_H3800_NOPT_IND             GPIO_GPIO (20)   /* Almost exactly the same as GPIO_H3600_OPT_DET */
#define GPIO_H3800_OPT_BAT_FAULT        GPIO_GPIO (22)
#define GPIO_H3800_CLK_OUT              GPIO_GPIO (27)

/****************************************************/

#define IRQ_GPIO_H3600_ACTION_BUTTON    IRQ_GPIO18
#define IRQ_GPIO_H3600_OPT_DET		IRQ_GPIO27

#define IRQ_GPIO_H3800_MMC_INT          IRQ_GPIO18
#define IRQ_GPIO_H3800_NOPT_IND         IRQ_GPIO20 /* almost same as OPT_DET */

/* H3100 / 3600 EGPIO pins */
#define EGPIO_H3600_VPP_ON		(1 << 0)
#define EGPIO_H3600_CARD_RESET		(1 << 1)   /* reset the attached pcmcia/compactflash card.  active high. */
#define EGPIO_H3600_OPT_RESET		(1 << 2)   /* reset the attached option pack.  active high. */
#define EGPIO_H3600_CODEC_NRESET	(1 << 3)   /* reset the onboard UDA1341.  active low. */
#define EGPIO_H3600_OPT_NVRAM_ON	(1 << 4)   /* apply power to optionpack nvram, active high. */
#define EGPIO_H3600_OPT_ON		(1 << 5)   /* full power to option pack.  active high. */
#define EGPIO_H3600_LCD_ON		(1 << 6)   /* enable 3.3V to LCD.  active high. */
#define EGPIO_H3600_RS232_ON		(1 << 7)   /* UART3 transceiver force on.  Active high. */

/* H3600 only EGPIO pins */
#define EGPIO_H3600_LCD_PCI		(1 << 8)   /* LCD control IC enable.  active high. */
#define EGPIO_H3600_IR_ON		(1 << 9)   /* apply power to IR module.  active high. */
#define EGPIO_H3600_AUD_AMP_ON		(1 << 10)  /* apply power to audio power amp.  active high. */
#define EGPIO_H3600_AUD_PWR_ON		(1 << 11)  /* apply power to reset of audio circuit.  active high. */
#define EGPIO_H3600_QMUTE		(1 << 12)  /* mute control for onboard UDA1341.  active high. */
#define EGPIO_H3600_IR_FSEL		(1 << 13)  /* IR speed select: 1->fast, 0->slow */
#define EGPIO_H3600_LCD_5V_ON		(1 << 14)  /* enable 5V to LCD. active high. */
#define EGPIO_H3600_LVDD_ON		(1 << 15)  /* enable 9V and -6.5V to LCD. */

/********************* H3800, ASIC #2 ********************/

#define _H3800_ASIC2_Base            (H3600_EGPIO_VIRT)
#define H3800_ASIC2_OFFSET(s,x,y)    \
    (*((volatile s *) (_H3800_ASIC2_Base + _H3800_ASIC2_ ## x ## _Base + _H3800_ASIC2_ ## x ## _ ## y)))
#define H3800_ASIC2_NOFFSET(s,x,n,y) \
    (*((volatile s *) (_H3800_ASIC2_Base + _H3800_ASIC2_ ## x ## _ ## n ## _Base + _H3800_ASIC2_ ## x ## _ ## y)))

#define _H3800_ASIC2_GPIO_Base                 0x0000
#define _H3800_ASIC2_GPIO_Direction            0x0000    /* R/W, 16 bits 1:input, 0:output */
#define _H3800_ASIC2_GPIO_InterruptType        0x0004    /* R/W, 12 bits 1:edge, 0:level          */
#define _H3800_ASIC2_GPIO_InterruptEdgeType    0x0008    /* R/W, 12 bits 1:rising, 0:falling */
#define _H3800_ASIC2_GPIO_InterruptLevelType   0x000C    /* R/W, 12 bits 1:high, 0:low  */
#define _H3800_ASIC2_GPIO_InterruptClear       0x0010    /* W,   12 bits */
#define _H3800_ASIC2_GPIO_InterruptFlag        0x0010    /* R,   12 bits - reads int status */
#define _H3800_ASIC2_GPIO_Data                 0x0014    /* R/W, 16 bits */
#define _H3800_ASIC2_GPIO_BattFaultOut         0x0018    /* R/W, 16 bit - sets level on batt fault */
#define _H3800_ASIC2_GPIO_InterruptEnable      0x001c    /* R/W, 12 bits 1:enable interrupt */
#define _H3800_ASIC2_GPIO_Alternate            0x003c    /* R/W, 12+1 bits - set alternate functions */

#define H3800_ASIC2_GPIO_Direction          H3800_ASIC2_OFFSET( u16, GPIO, Direction )
#define H3800_ASIC2_GPIO_InterruptType      H3800_ASIC2_OFFSET( u16, GPIO, InterruptType )
#define H3800_ASIC2_GPIO_InterruptEdgeType  H3800_ASIC2_OFFSET( u16, GPIO, InterruptEdgeType )
#define H3800_ASIC2_GPIO_InterruptLevelType H3800_ASIC2_OFFSET( u16, GPIO, InterruptLevelType )
#define H3800_ASIC2_GPIO_InterruptClear     H3800_ASIC2_OFFSET( u16, GPIO, InterruptClear )
#define H3800_ASIC2_GPIO_InterruptFlag      H3800_ASIC2_OFFSET( u16, GPIO, InterruptFlag )
#define H3800_ASIC2_GPIO_Data               H3800_ASIC2_OFFSET( u16, GPIO, Data )
#define H3800_ASIC2_GPIO_BattFaultOut       H3800_ASIC2_OFFSET( u16, GPIO, BattFaultOut )
#define H3800_ASIC2_GPIO_InterruptEnable    H3800_ASIC2_OFFSET( u16, GPIO, InterruptEnable )
#define H3800_ASIC2_GPIO_Alternate          H3800_ASIC2_OFFSET( u16, GPIO, Alternate )

#define GPIO_H3800_ASIC2_IN_Y1_N          (1 << 0)   /* Output: Touchscreen Y1 */
#define GPIO_H3800_ASIC2_IN_X0            (1 << 1)   /* Output: Touchscreen X0 */
#define GPIO_H3800_ASIC2_IN_Y0            (1 << 2)   /* Output: Touchscreen Y0 */
#define GPIO_H3800_ASIC2_IN_X1_N          (1 << 3)   /* Output: Touchscreen X1 */
#define GPIO_H3800_ASIC2_BT_RST           (1 << 4)   /* Output: Bluetooth reset */
#define GPIO_H3800_ASIC2_PEN_IRQ          (1 << 5)   /* Input : Pen down        */
#define GPIO_H3800_ASIC2_SD_DETECT        (1 << 6)   /* Input : SD detect */
#define GPIO_H3800_ASIC2_EAR_IN_N         (1 << 7)   /* Input : Audio jack plug inserted */
#define GPIO_H3800_ASIC2_OPT_PCM_RESET    (1 << 8)   /* Output: */
#define GPIO_H3800_ASIC2_OPT_RESET        (1 << 9)   /* Output: */
#define GPIO_H3800_ASIC2_USB_DETECT_N     (1 << 10)  /* Input : */
#define GPIO_H3800_ASIC2_SD_CON_SLT       (1 << 11)  /* Input : */

#define _H3800_ASIC2_KPIO_Base                 0x0200
#define _H3800_ASIC2_KPIO_Direction            0x0000    /* R/W, 12 bits 1:input, 0:output */
#define _H3800_ASIC2_KPIO_InterruptType        0x0004    /* R/W, 12 bits 1:edge, 0:level          */
#define _H3800_ASIC2_KPIO_InterruptEdgeType    0x0008    /* R/W, 12 bits 1:rising, 0:falling */
#define _H3800_ASIC2_KPIO_InterruptLevelType   0x000C    /* R/W, 12 bits 1:high, 0:low  */
#define _H3800_ASIC2_KPIO_InterruptClear       0x0010    /* W,   20 bits - 8 special */
#define _H3800_ASIC2_KPIO_InterruptFlag        0x0010    /* R,   20 bits - 8 special - reads int status */
#define _H3800_ASIC2_KPIO_Data                 0x0014    /* R/W, 16 bits */
#define _H3800_ASIC2_KPIO_BattFaultOut         0x0018    /* R/W, 16 bit - sets level on batt fault */
#define _H3800_ASIC2_KPIO_InterruptEnable      0x001c    /* R/W, 20 bits - 8 special */
#define _H3800_ASIC2_KPIO_Alternate            0x003c    /* R/W, 6 bits */

#define H3800_ASIC2_KPIO_Direction          H3800_ASIC2_OFFSET( u16, KPIO, Direction )
#define H3800_ASIC2_KPIO_InterruptType      H3800_ASIC2_OFFSET( u16, KPIO, InterruptType )
#define H3800_ASIC2_KPIO_InterruptEdgeType  H3800_ASIC2_OFFSET( u16, KPIO, InterruptEdgeType )
#define H3800_ASIC2_KPIO_InterruptLevelType H3800_ASIC2_OFFSET( u16, KPIO, InterruptLevelType )
#define H3800_ASIC2_KPIO_InterruptClear     H3800_ASIC2_OFFSET( u32, KPIO, InterruptClear )
#define H3800_ASIC2_KPIO_InterruptFlag      H3800_ASIC2_OFFSET( u32, KPIO, InterruptFlag )
#define H3800_ASIC2_KPIO_Data               H3800_ASIC2_OFFSET( u16, KPIO, Data )
#define H3800_ASIC2_KPIO_BattFaultOut       H3800_ASIC2_OFFSET( u16, KPIO, BattFaultOut )
#define H3800_ASIC2_KPIO_InterruptEnable    H3800_ASIC2_OFFSET( u32, KPIO, InterruptEnable )
#define H3800_ASIC2_KPIO_Alternate          H3800_ASIC2_OFFSET( u16, KPIO, Alternate )

#define H3800_ASIC2_KPIO_SPI_INT        ( 1 << 16 )
#define H3800_ASIC2_KPIO_OWM_INT        ( 1 << 17 )
#define H3800_ASIC2_KPIO_ADC_INT        ( 1 << 18 )
#define H3800_ASIC2_KPIO_UART_0_INT     ( 1 << 19 )
#define H3800_ASIC2_KPIO_UART_1_INT     ( 1 << 20 )
#define H3800_ASIC2_KPIO_TIMER_0_INT    ( 1 << 21 )
#define H3800_ASIC2_KPIO_TIMER_1_INT    ( 1 << 22 )
#define H3800_ASIC2_KPIO_TIMER_2_INT    ( 1 << 23 )

#define KPIO_H3800_ASIC2_RECORD_BTN_N     (1 << 0)   /* Record button */
#define KPIO_H3800_ASIC2_KEY_5W1_N        (1 << 1)   /* Keypad */
#define KPIO_H3800_ASIC2_KEY_5W2_N        (1 << 2)   /* */
#define KPIO_H3800_ASIC2_KEY_5W3_N        (1 << 3)   /* */
#define KPIO_H3800_ASIC2_KEY_5W4_N        (1 << 4)   /* */
#define KPIO_H3800_ASIC2_KEY_5W5_N        (1 << 5)   /* */
#define KPIO_H3800_ASIC2_KEY_LEFT_N       (1 << 6)   /* */
#define KPIO_H3800_ASIC2_KEY_RIGHT_N      (1 << 7)   /* */
#define KPIO_H3800_ASIC2_KEY_AP1_N        (1 << 8)   /* Old "Calendar" */
#define KPIO_H3800_ASIC2_KEY_AP2_N        (1 << 9)   /* Old "Schedule" */
#define KPIO_H3800_ASIC2_KEY_AP3_N        (1 << 10)  /* Old "Q"        */
#define KPIO_H3800_ASIC2_KEY_AP4_N        (1 << 11)  /* Old "Undo"     */

/* Alternate KPIO functions (set by default) */
#define KPIO_ALT_H3800_ASIC2_KEY_5W1_N        (1 << 1)   /* Action key */
#define KPIO_ALT_H3800_ASIC2_KEY_5W2_N        (1 << 2)   /* J1 of keypad input */
#define KPIO_ALT_H3800_ASIC2_KEY_5W3_N        (1 << 3)   /* J2 of keypad input */
#define KPIO_ALT_H3800_ASIC2_KEY_5W4_N        (1 << 4)   /* J3 of keypad input */
#define KPIO_ALT_H3800_ASIC2_KEY_5W5_N        (1 << 5)   /* J4 of keypad input */

#define _H3800_ASIC2_SPI_Base                  0x0400
#define _H3800_ASIC2_SPI_Control               0x0000    /* R/W 8 bits */
#define _H3800_ASIC2_SPI_Data                  0x0004    /* R/W 8 bits */
#define _H3800_ASIC2_SPI_ChipSelectDisabled    0x0008    /* W   8 bits */

#define H3800_ASIC2_SPI_Control             H3800_ASIC2_OFFSET( u8, SPI, Control )
#define H3800_ASIC2_SPI_Data                H3800_ASIC2_OFFSET( u8, SPI, Data )
#define H3800_ASIC2_SPI_ChipSelectDisabled  H3800_ASIC2_OFFSET( u8, SPI, ChipSelectDisabled )

#define _H3800_ASIC2_PWM_0_Base                0x0600
#define _H3800_ASIC2_PWM_1_Base                0x0700
#define _H3800_ASIC2_PWM_TimeBase              0x0000    /* R/W 6 bits */
#define _H3800_ASIC2_PWM_PeriodTime            0x0004    /* R/W 12 bits */
#define _H3800_ASIC2_PWM_DutyTime              0x0008    /* R/W 12 bits */

#define H3800_ASIC2_PWM_0_TimeBase          H3800_ASIC2_NOFFSET(  u8, PWM, 0, TimeBase )
#define H3800_ASIC2_PWM_0_PeriodTime        H3800_ASIC2_NOFFSET( u16, PWM, 0, PeriodTime )
#define H3800_ASIC2_PWM_0_DutyTime          H3800_ASIC2_NOFFSET( u16, PWM, 0, DutyTime )

#define H3800_ASIC2_PWM_1_TimeBase          H3800_ASIC2_NOFFSET(  u8, PWM, 1, TimeBase )
#define H3800_ASIC2_PWM_1_PeriodTime        H3800_ASIC2_NOFFSET( u16, PWM, 1, PeriodTime )
#define H3800_ASIC2_PWM_1_DutyTime          H3800_ASIC2_NOFFSET( u16, PWM, 1, DutyTime )

#define H3800_ASIC2_PWM_TIMEBASE_MASK             0xf    /* Low 4 bits sets time base, max = 8 */
#define H3800_ASIC2_PWM_TIMEBASE_ENABLE    ( 1 << 4 )    /* Enable clock */
#define H3800_ASIC2_PWM_TIMEBASE_CLEAR     ( 1 << 5 )    /* Clear the PWM */

#define _H3800_ASIC2_LED_0_Base                0x0800
#define _H3800_ASIC2_LED_1_Base                0x0880
#define _H3800_ASIC2_LED_2_Base                0x0900
#define _H3800_ASIC2_LED_TimeBase              0x0000    /* R/W  7 bits */
#define _H3800_ASIC2_LED_PeriodTime            0x0004    /* R/W 12 bits */
#define _H3800_ASIC2_LED_DutyTime              0x0008    /* R/W 12 bits */
#define _H3800_ASIC2_LED_AutoStopCount         0x000c    /* R/W 16 bits */

#define H3800_ASIC2_LED_0_TimeBase          H3800_ASIC2_NOFFSET(  u8, LED, 0, TimeBase )
#define H3800_ASIC2_LED_0_PeriodTime        H3800_ASIC2_NOFFSET( u16, LED, 0, PeriodTime )
#define H3800_ASIC2_LED_0_DutyTime          H3800_ASIC2_NOFFSET( u16, LED, 0, DutyTime )
#define H3800_ASIC2_LED_0_AutoStopClock     H3800_ASIC2_NOFFSET( u16, LED, 0, AutoStopClock )

#define H3800_ASIC2_LED_1_TimeBase          H3800_ASIC2_NOFFSET(  u8, LED, 1, TimeBase )
#define H3800_ASIC2_LED_1_PeriodTime        H3800_ASIC2_NOFFSET( u16, LED, 1, PeriodTime )
#define H3800_ASIC2_LED_1_DutyTime          H3800_ASIC2_NOFFSET( u16, LED, 1, DutyTime )
#define H3800_ASIC2_LED_1_AutoStopClock     H3800_ASIC2_NOFFSET( u16, LED, 1, AutoStopClock )

#define H3800_ASIC2_LED_2_TimeBase          H3800_ASIC2_NOFFSET(  u8, LED, 2, TimeBase )
#define H3800_ASIC2_LED_2_PeriodTime        H3800_ASIC2_NOFFSET( u16, LED, 2, PeriodTime )
#define H3800_ASIC2_LED_2_DutyTime          H3800_ASIC2_NOFFSET( u16, LED, 2, DutyTime )
#define H3800_ASIC2_LED_2_AutoStopClock     H3800_ASIC2_NOFFSET( u16, LED, 2, AutoStopClock )

#define H3800_ASIC2_LED_TIMEBASE_MASK            0x0f    /* Low 4 bits sets time base, max = 13 */
#define H3800_ASIC2_LED_TIMEBASE_BLINK     ( 1 << 4 )    /* Enable blinking */
#define H3800_ASIC2_LED_TIMEBASE_AUTOSTOP  ( 1 << 5 )
#define H3800_ASIC2_LED_TIMEBASE_ALWAYS    ( 1 << 6 )    /* Enable blink always */

#define _H3800_ASIC2_UART_0_Base               0x0A00
#define _H3800_ASIC2_UART_1_Base               0x0C00
#define _H3800_ASIC2_UART_Receive              0x0000    /* R    8 bits */
#define _H3800_ASIC2_UART_Transmit             0x0000    /*   W  8 bits */
#define _H3800_ASIC2_UART_IntEnable            0x0004    /* R/W  8 bits */
#define _H3800_ASIC2_UART_IntVerify            0x0008    /* R/W  8 bits */
#define _H3800_ASIC2_UART_FIFOControl          0x000c    /* R/W  8 bits */
#define _H3800_ASIC2_UART_LineControl          0x0010    /* R/W  8 bits */
#define _H3800_ASIC2_UART_ModemStatus          0x0014    /* R/W  8 bits */
#define _H3800_ASIC2_UART_LineStatus           0x0018    /* R/W  8 bits */
#define _H3800_ASIC2_UART_ScratchPad           0x001c    /* R/W  8 bits */
#define _H3800_ASIC2_UART_DivisorLatchL        0x0020    /* R/W  8 bits */
#define _H3800_ASIC2_UART_DivisorLatchH        0x0024    /* R/W  8 bits */

#define H3800_ASIC2_UART_0_Receive          H3800_ASIC2_NOFFSET(  u8, UART, 0, Receive )
#define H3800_ASIC2_UART_0_Transmit         H3800_ASIC2_NOFFSET(  u8, UART, 0, Transmit )
#define H3800_ASIC2_UART_0_IntEnable        H3800_ASIC2_NOFFSET(  u8, UART, 0, IntEnable )
#define H3800_ASIC2_UART_0_IntVerify        H3800_ASIC2_NOFFSET(  u8, UART, 0, IntVerify )
#define H3800_ASIC2_UART_0_FIFOControl      H3800_ASIC2_NOFFSET(  u8, UART, 0, FIFOControl )
#define H3800_ASIC2_UART_0_LineControl      H3800_ASIC2_NOFFSET(  u8, UART, 0, LineControl )
#define H3800_ASIC2_UART_0_ModemStatus      H3800_ASIC2_NOFFSET(  u8, UART, 0, ModemStatus )
#define H3800_ASIC2_UART_0_LineStatus       H3800_ASIC2_NOFFSET(  u8, UART, 0, LineStatus )
#define H3800_ASIC2_UART_0_ScratchPad       H3800_ASIC2_NOFFSET(  u8, UART, 0, ScratchPad )
#define H3800_ASIC2_UART_0_DivisorLatchL    H3800_ASIC2_NOFFSET(  u8, UART, 0, DivisorLatchL )
#define H3800_ASIC2_UART_0_DivisorLatchH    H3800_ASIC2_NOFFSET(  u8, UART, 0, DivisorLatchH )

#define H3800_ASIC2_UART_1_Receive          H3800_ASIC2_NOFFSET(  u8, UART, 1, Receive )
#define H3800_ASIC2_UART_1_Transmit         H3800_ASIC2_NOFFSET(  u8, UART, 1, Transmit )
#define H3800_ASIC2_UART_1_IntEnable        H3800_ASIC2_NOFFSET(  u8, UART, 1, IntEnable )
#define H3800_ASIC2_UART_1_IntVerify        H3800_ASIC2_NOFFSET(  u8, UART, 1, IntVerify )
#define H3800_ASIC2_UART_1_FIFOControl      H3800_ASIC2_NOFFSET(  u8, UART, 1, FIFOControl )
#define H3800_ASIC2_UART_1_LineControl      H3800_ASIC2_NOFFSET(  u8, UART, 1, LineControl )
#define H3800_ASIC2_UART_1_ModemStatus      H3800_ASIC2_NOFFSET(  u8, UART, 1, ModemStatus )
#define H3800_ASIC2_UART_1_LineStatus       H3800_ASIC2_NOFFSET(  u8, UART, 1, LineStatus )
#define H3800_ASIC2_UART_1_ScratchPad       H3800_ASIC2_NOFFSET(  u8, UART, 1, ScratchPad )
#define H3800_ASIC2_UART_1_DivisorLatchL    H3800_ASIC2_NOFFSET(  u8, UART, 1, DivisorLatchL )
#define H3800_ASIC2_UART_1_DivisorLatchH    H3800_ASIC2_NOFFSET(  u8, UART, 1, DivisorLatchH )

#define _H3800_ASIC2_TIMER_Base                0x0E00
#define _H3800_ASIC2_TIMER_Command             0x0000    /* R/W  8 bits */

#define H3800_ASIC2_TIMER_Command           H3800_ASIC2_OFFSET( u8, Timer, Command )

#define H3800_ASIC2_TIMER_GAT_0            ( 1 << 0 )    /* Gate enable, counter 0 */
#define H3800_ASIC2_TIMER_GAT_1            ( 1 << 1 )    /* Gate enable, counter 1 */
#define H3800_ASIC2_TIMER_GAT_2            ( 1 << 2 )    /* Gate enable, counter 2 */
#define H3800_ASIC2_TIMER_CLK_0            ( 1 << 3 )    /* Clock enable, counter 0 */
#define H3800_ASIC2_TIMER_CLK_1            ( 1 << 4 )    /* Clock enable, counter 1 */
#define H3800_ASIC2_TIMER_CLK_2            ( 1 << 5 )    /* Clock enable, counter 2 */
#define H3800_ASIC2_TIMER_MODE_0           ( 1 << 6 )    /* Mode 0 enable, counter 0 */
#define H3800_ASIC2_TIMER_MODE_1           ( 1 << 7 )    /* Mode 0 enable, counter 1 */

#define _H3800_ASIC2_CLOCK_Base                0x1000
#define _H3800_ASIC2_CLOCK_Enable              0x0000    /* R/W  18 bits */

#define H3800_ASIC2_CLOCK_Enable            H3800_ASIC2_OFFSET( u32, CLOCK, Enable )

#define H3800_ASIC2_CLOCK_AUDIO_1              0x0001    /* Enable 4.1 MHz clock for 8Khz and 4khz sample rate */
#define H3800_ASIC2_CLOCK_AUDIO_2              0x0002    /* Enable 12.3 MHz clock for 48Khz and 32khz sample rate */
#define H3800_ASIC2_CLOCK_AUDIO_3              0x0004    /* Enable 5.6 MHz clock for 11 kHZ sample rate */
#define H3800_ASIC2_CLOCK_AUDIO_4              0x0008    /* Enable 11.289 MHz clock for 44 and 22 kHz sample rate */
#define H3800_ASIC2_CLOCK_ADC              ( 1 << 4 )    /* 1.024 MHz clock to ADC */
#define H3800_ASIC2_CLOCK_SPI              ( 1 << 5 )    /* 4.096 MHz clock to SPI */
#define H3800_ASIC2_CLOCK_OWM              ( 1 << 6 )    /* 4.096 MHz clock to OWM */
#define H3800_ASIC2_CLOCK_PWM              ( 1 << 7 )    /* 2.048 MHz clock to PWM */
#define H3800_ASIC2_CLOCK_UART_1           ( 1 << 8 )    /* 24.576 MHz clock to UART1 (turn off bit 16) */
#define H3800_ASIC2_CLOCK_UART_0           ( 1 << 9 )    /* 24.576 MHz clock to UART0 (turn off bit 17) */
#define H3800_ASIC2_CLOCK_SD_1             ( 1 << 10 )   /* 16.934 MHz to SD */
#define H3800_ASIC2_CLOCK_SD_2             ( 2 << 10 )   /* 24.576 MHz to SD */
#define H3800_ASIC2_CLOCK_SD_3             ( 3 << 10 )   /* 33.869 MHz to SD */
#define H3800_ASIC2_CLOCK_SD_4             ( 4 << 10 )   /* 49.152 MHz to SD */
#define H3800_ASIC2_CLOCK_EX0              ( 1 << 13 )   /* Enable 32.768 kHz crystal */
#define H3800_ASIC2_CLOCK_EX1              ( 1 << 14 )   /* Enable 24.576 MHz crystal */
#define H3800_ASIC2_CLOCK_EX2              ( 1 << 15 )   /* Enable 33.869 MHz crystal */
#define H3800_ASIC2_CLOCK_SLOW_UART_1      ( 1 << 16 )   /* Enable 3.686 MHz to UART1 (turn off bit 8) */
#define H3800_ASIC2_CLOCK_SLOW_UART_0      ( 1 << 17 )   /* Enable 3.686 MHz to UART0 (turn off bit 9) */

#define _H3800_ASIC2_ADC_Base                  0x1200
#define _H3800_ASIC2_ADC_Multiplexer           0x0000    /* R/W 4 bits - low 3 bits set channel */
#define _H3800_ASIC2_ADC_ControlStatus         0x0004    /* R/W 8 bits */
#define _H3800_ASIC2_ADC_Data                  0x0008    /* R   10 bits */

#define H3800_ASIC2_ADC_Multiplexer       H3800_ASIC2_OFFSET(  u8, ADC, Multiplexer )
#define H3800_ASIC2_ADC_ControlStatus     H3800_ASIC2_OFFSET(  u8, ADC, ControlStatus )
#define H3800_ASIC2_ADC_Data              H3800_ASIC2_OFFSET( u16, ADC, Data )

#define H3600_ASIC2_ADC_MUX_CHANNEL_MASK         0x07    /* Low 3 bits sets channel.  max = 4 */
#define H3600_ASIC2_ADC_MUX_CLKEN          ( 1 << 3 )    /* Enable clock */

#define H3600_ASIC2_ADC_CSR_ADPS_MASK            0x0f    /* Low 4 bits sets prescale, max = 8 */
#define H3600_ASIC2_ADC_CSR_FREE_RUN       ( 1 << 4 )
#define H3600_ASIC2_ADC_CSR_INT_ENABLE     ( 1 << 5 )
#define H3600_ASIC2_ADC_CSR_START          ( 1 << 6 )    /* Set to start conversion.  Goes to 0 when done */
#define H3600_ASIC2_ADC_CSR_ENABLE         ( 1 << 7 )    /* 1:power up ADC, 0:power down */


#define _H3800_ASIC2_INTR_Base                 0x1600
#define _H3800_ASIC2_INTR_MaskAndFlag          0x0000    /* R/(W) 8bits */
#define _H3800_ASIC2_INTR_ClockPrescale        0x0004    /* R/(W) 5bits */
#define _H3800_ASIC2_INTR_TimerSet             0x0008    /* R/(W) 8bits */

#define H3800_ASIC2_INTR_MaskAndFlag      H3800_ASIC2_OFFSET( u8, INTR, MaskAndFlag )
#define H3800_ASIC2_INTR_ClockPrescale    H3800_ASIC2_OFFSET( u8, INTR, ClockPrescale )
#define H3800_ASIC2_INTR_TimerSet         H3800_ASIC2_OFFSET( u8, INTR, TimerSet )

#define H3800_ASIC2_INTR_GLOBAL_MASK       ( 1 << 0 )    /* Global interrupt mask */
#define H3800_ASIC2_INTR_POWER_ON_RESET    ( 1 << 1 )    /* 01: Power on reset (bits 1 & 2 ) */
#define H3800_ASIC2_INTR_EXTERNAL_RESET    ( 2 << 1 )    /* 10: External reset (bits 1 & 2 ) */
#define H3800_ASIC2_INTR_MASK_UART_0       ( 1 << 4 )
#define H3800_ASIC2_INTR_MASK_UART_1       ( 1 << 5 )
#define H3800_ASIC2_INTR_MASK_TIMER        ( 1 << 6 )
#define H3800_ASIC2_INTR_MASK_OWM          ( 1 << 7 )

#define H3800_ASIC2_INTR_CLOCK_PRESCALE          0x0f    /* 4 bits, max 14 */
#define H3800_ASIC2_INTR_SET               ( 1 << 4 )    /* Time base enable */


#define _H3800_ASIC2_OWM_Base                  0x1800
#define _H3800_ASIC2_OWM_Command               0x0000    /* R/W 4 bits command register */
#define _H3800_ASIC2_OWM_Data                  0x0004    /* R/W 8 bits, transmit / receive buffer */
#define _H3800_ASIC2_OWM_Interrupt             0x0008    /* R/W Command register */
#define _H3800_ASIC2_OWM_InterruptEnable       0x000c    /* R/W Command register */
#define _H3800_ASIC2_OWM_ClockDivisor          0x0010    /* R/W 5 bits of divisor and pre-scale */

#define H3800_ASIC2_OWM_Command            H3800_ASIC2_OFFSET( u8, OWM, Command )
#define H3800_ASIC2_OWM_Data               H3800_ASIC2_OFFSET( u8, OWM, Data )
#define H3800_ASIC2_OWM_Interrupt          H3800_ASIC2_OFFSET( u8, OWM, Interrupt )
#define H3800_ASIC2_OWM_InterruptEnable    H3800_ASIC2_OFFSET( u8, OWM, InterruptEnable )
#define H3800_ASIC2_OWM_ClockDivisor       H3800_ASIC2_OFFSET( u8, OWM, ClockDivisor )

#define H3800_ASIC2_OWM_CMD_ONE_WIRE_RESET ( 1 << 0 )    /* Set to force reset on 1-wire bus */
#define H3800_ASIC2_OWM_CMD_SRA            ( 1 << 1 )    /* Set to switch to Search ROM accelerator mode */
#define H3800_ASIC2_OWM_CMD_DQ_OUTPUT      ( 1 << 2 )    /* Write only - forces bus low */
#define H3800_ASIC2_OWM_CMD_DQ_INPUT       ( 1 << 3 )    /* Read only - reflects state of bus */

#define H3800_ASIC2_OWM_INT_PD             ( 1 << 0 )    /* Presence detect */
#define H3800_ASIC2_OWM_INT_PDR            ( 1 << 1 )    /* Presence detect result */
#define H3800_ASIC2_OWM_INT_TBE            ( 1 << 2 )    /* Transmit buffer empty */
#define H3800_ASIC2_OWM_INT_TEMT           ( 1 << 3 )    /* Transmit shift register empty */
#define H3800_ASIC2_OWM_INT_RBF            ( 1 << 4 )    /* Receive buffer full */

#define H3800_ASIC2_OWM_INTEN_EPD          ( 1 << 0 )    /* Enable receive buffer full interrupt */
#define H3800_ASIC2_OWM_INTEN_IAS          ( 1 << 1 )    /* Enable transmit shift register empty interrupt */
#define H3800_ASIC2_OWM_INTEN_ETBE         ( 1 << 2 )    /* Enable transmit buffer empty interrupt */
#define H3800_ASIC2_OWM_INTEN_ETMT         ( 1 << 3 )    /* INTR active state */
#define H3800_ASIC2_OWM_INTEN_ERBF         ( 1 << 4 )    /* Enable presence detect interrupt */

#define _H3800_ASIC2_FlashCtl_Base             0x1A00

/****************************************************/
/* H3800, ASIC #1
 * This ASIC is accesed through ASIC #2, and
 * mapped into the 1c00 - 1f00 region
 */

#define H3800_ASIC1_OFFSET(s,x,y)   \
     (*((volatile s *) (_H3800_ASIC2_Base + _H3800_ASIC1_ ## x ## _Base + (_H3800_ASIC1_ ## x ## _ ## y << 1))))

#define _H3800_ASIC1_MMC_Base             0x1c00

#define _H3800_ASIC1_MMC_StartStopClock     0x00    /* R/W 8bit                                  */
#define _H3800_ASIC1_MMC_Status             0x02    /* R   See below, default 0x0040             */
#define _H3800_ASIC1_MMC_ClockRate          0x04    /* R/W 8bit, low 3 bits are clock divisor    */
#define _H3800_ASIC1_MMC_SPIRegister        0x08    /* R/W 8bit, see below                       */
#define _H3800_ASIC1_MMC_CmdDataCont        0x0a    /* R/W 8bit, write to start MMC adapter      */
#define _H3800_ASIC1_MMC_ResponseTimeout    0x0c    /* R/W 8bit, clocks before response timeout  */
#define _H3800_ASIC1_MMC_ReadTimeout        0x0e    /* R/W 16bit, clocks before received data timeout */
#define _H3800_ASIC1_MMC_BlockLength        0x10    /* R/W 10bit */
#define _H3800_ASIC1_MMC_NumOfBlocks        0x12    /* R/W 16bit, in block mode, number of blocks  */
#define _H3800_ASIC1_MMC_InterruptMask      0x1a    /* R/W 8bit */
#define _H3800_ASIC1_MMC_CommandNumber      0x1c    /* R/W 6 bits */
#define _H3800_ASIC1_MMC_ArgumentH          0x1e    /* R/W 16 bits  */
#define _H3800_ASIC1_MMC_ArgumentL          0x20    /* R/W 16 bits */
#define _H3800_ASIC1_MMC_ResFifo            0x22    /* R   8 x 16 bits - contains response FIFO */
#define _H3800_ASIC1_MMC_BufferPartFull     0x28    /* R/W 8 bits */

#define H3800_ASIC1_MMC_StartStopClock    H3800_ASIC1_OFFSET(  u8, MMC, StartStopClock )
#define H3800_ASIC1_MMC_Status            H3800_ASIC1_OFFSET( u16, MMC, Status )
#define H3800_ASIC1_MMC_ClockRate         H3800_ASIC1_OFFSET(  u8, MMC, ClockRate )
#define H3800_ASIC1_MMC_SPIRegister       H3800_ASIC1_OFFSET(  u8, MMC, SPIRegister )
#define H3800_ASIC1_MMC_CmdDataCont       H3800_ASIC1_OFFSET(  u8, MMC, CmdDataCont )
#define H3800_ASIC1_MMC_ResponseTimeout   H3800_ASIC1_OFFSET(  u8, MMC, ResponseTimeout )
#define H3800_ASIC1_MMC_ReadTimeout       H3800_ASIC1_OFFSET( u16, MMC, ReadTimeout )
#define H3800_ASIC1_MMC_BlockLength       H3800_ASIC1_OFFSET( u16, MMC, BlockLength )
#define H3800_ASIC1_MMC_NumOfBlocks       H3800_ASIC1_OFFSET( u16, MMC, NumOfBlocks )
#define H3800_ASIC1_MMC_InterruptMask     H3800_ASIC1_OFFSET(  u8, MMC, InterruptMask )
#define H3800_ASIC1_MMC_CommandNumber     H3800_ASIC1_OFFSET(  u8, MMC, CommandNumber )
#define H3800_ASIC1_MMC_ArgumentH         H3800_ASIC1_OFFSET( u16, MMC, ArgumentH )
#define H3800_ASIC1_MMC_ArgumentL         H3800_ASIC1_OFFSET( u16, MMC, ArgumentL )
#define H3800_ASIC1_MMC_ResFifo           H3800_ASIC1_OFFSET( u16, MMC, ResFifo )
#define H3800_ASIC1_MMC_BufferPartFull    H3800_ASIC1_OFFSET(  u8, MMC, BufferPartFull )

#define H3800_ASIC1_MMC_STOP_CLOCK                   (1 << 0)   /* Write to "StartStopClock" register */
#define H3800_ASIC1_MMC_START_CLOCK                  (1 << 1)

#define H3800_ASIC1_MMC_STATUS_READ_TIMEOUT          (1 << 0)
#define H3800_ASIC1_MMC_STATUS_RESPONSE_TIMEOUT      (1 << 1)
#define H3800_ASIC1_MMC_STATUS_CRC_WRITE_ERROR       (1 << 2)
#define H3800_ASIC1_MMC_STATUS_CRC_READ_ERROR        (1 << 3)
#define H3800_ASIC1_MMC_STATUS_SPI_READ_ERROR        (1 << 4)  /* SPI data token error received */
#define H3800_ASIC1_MMC_STATUS_CRC_RESPONSE_ERROR    (1 << 5)
#define H3800_ASIC1_MMC_STATUS_FIFO_EMPTY            (1 << 6)
#define H3800_ASIC1_MMC_STATUS_FIFO_FULL             (1 << 7)
#define H3800_ASIC1_MMC_STATUS_CLOCK_ENABLE          (1 << 8)  /* MultiMediaCard clock stopped */
#define H3800_ASIC1_MMC_STATUS_DATA_TRANSFER_DONE    (1 << 11) /* Write operation, indicates transfer finished */
#define H3800_ASIC1_MMC_STATUS_END_PROGRAM           (1 << 12) /* End write and read operations */
#define H3800_ASIC1_MMC_STATUS_END_COMMAND_RESPONSE  (1 << 13) /* End command response */

#define H3800_ASIC1_MMC_SPI_REG_SPI_ENABLE           (1 << 0)  /* Enables SPI mode */
#define H3800_ASIC1_MMC_SPI_REG_CRC_ON               (1 << 1)  /* 1:turn on CRC    */
#define H3800_ASIC1_MMC_SPI_REG_SPI_CS_ENABLE        (1 << 2)  /* 1:turn on SPI CS */
#define H3800_ASIC1_MMC_SPI_REG_CS_ADDRESS_MASK      0x38      /* Bits 3,4,5 are the SPI CS relative address */

#define H3800_ASIC1_MMC_CMD_DATA_CONT_FORMAT_NO_RESPONSE  0x00
#define H3800_ASIC1_MMC_CMD_DATA_CONT_FORMAT_R1           0x01
#define H3800_ASIC1_MMC_CMD_DATA_CONT_FORMAT_R2           0x02
#define H3800_ASIC1_MMC_CMD_DATA_CONT_FORMAT_R3           0x03
#define H3800_ASIC1_MMC_CMD_DATA_CONT_DATA_ENABLE         (1 << 2)  /* This command contains a data transfer */
#define H3800_ASIC1_MMC_CMD_DATA_CONT_WRITE               (1 << 3)  /* This data transfer is a write */
#define H3800_ASIC1_MMC_CMD_DATA_CONT_STREAM_MODE         (1 << 4)  /* This data transfer is in stream mode */
#define H3800_ASIC1_MMC_CMD_DATA_CONT_BUSY_BIT            (1 << 5)  /* Busy signal expected after current cmd */
#define H3800_ASIC1_MMC_CMD_DATA_CONT_INITIALIZE          (1 << 6)  /* Enables the 80 bits for initializing card */

#define H3800_ASIC1_MMC_INT_MASK_DATA_TRANSFER_DONE       (1 << 0)
#define H3800_ASIC1_MMC_INT_MASK_PROGRAM_DONE             (1 << 1)
#define H3800_ASIC1_MMC_INT_MASK_END_COMMAND_RESPONSE     (1 << 2)
#define H3800_ASIC1_MMC_INT_MASK_BUFFER_READY             (1 << 3)

#define H3800_ASIC1_MMC_BUFFER_PART_FULL                  (1 << 0)

/********* GPIO **********/

#define _H3800_ASIC1_GPIO_Base        0x1e00

#define _H3800_ASIC1_GPIO_Mask          0x30    /* R/W 0:don't mask, 1:mask interrupt */
#define _H3800_ASIC1_GPIO_Direction     0x32    /* R/W 0:input, 1:output              */
#define _H3800_ASIC1_GPIO_Out           0x34    /* R/W 0:output low, 1:output high    */
#define _H3800_ASIC1_GPIO_TriggerType   0x36    /* R/W 0:level, 1:edge                */
#define _H3800_ASIC1_GPIO_EdgeTrigger   0x38    /* R/W 0:falling, 1:rising            */
#define _H3800_ASIC1_GPIO_LevelTrigger  0x3A    /* R/W 0:low, 1:high level detect     */
#define _H3800_ASIC1_GPIO_LevelStatus   0x3C    /* R/W 0:none, 1:detect               */
#define _H3800_ASIC1_GPIO_EdgeStatus    0x3E    /* R/W 0:none, 1:detect               */
#define _H3800_ASIC1_GPIO_State         0x40    /* R   See masks below  (default 0)         */
#define _H3800_ASIC1_GPIO_Reset         0x42    /* R/W See masks below  (default 0x04)      */
#define _H3800_ASIC1_GPIO_SleepMask     0x44    /* R/W 0:don't mask, 1:mask trigger in sleep mode  */
#define _H3800_ASIC1_GPIO_SleepDir      0x46    /* R/W direction 0:input, 1:output in sleep mode    */
#define _H3800_ASIC1_GPIO_SleepOut      0x48    /* R/W level 0:low, 1:high in sleep mode           */
#define _H3800_ASIC1_GPIO_Status        0x4A    /* R   Pin status                                  */
#define _H3800_ASIC1_GPIO_BattFaultDir  0x4C    /* R/W direction 0:input, 1:output in batt_fault   */
#define _H3800_ASIC1_GPIO_BattFaultOut  0x4E    /* R/W level 0:low, 1:high in batt_fault           */

#define H3800_ASIC1_GPIO_Mask         H3800_ASIC1_OFFSET( u16, GPIO, Mask )
#define H3800_ASIC1_GPIO_Direction    H3800_ASIC1_OFFSET( u16, GPIO, Direction )
#define H3800_ASIC1_GPIO_Out          H3800_ASIC1_OFFSET( u16, GPIO, Out )
#define H3800_ASIC1_GPIO_TriggerType  H3800_ASIC1_OFFSET( u16, GPIO, TriggerType )
#define H3800_ASIC1_GPIO_EdgeTrigger  H3800_ASIC1_OFFSET( u16, GPIO, EdgeTrigger )
#define H3800_ASIC1_GPIO_LevelTrigger H3800_ASIC1_OFFSET( u16, GPIO, LevelTrigger )
#define H3800_ASIC1_GPIO_LevelStatus  H3800_ASIC1_OFFSET( u16, GPIO, LevelStatus )
#define H3800_ASIC1_GPIO_EdgeStatus   H3800_ASIC1_OFFSET( u16, GPIO, EdgeStatus )
#define H3800_ASIC1_GPIO_State        H3800_ASIC1_OFFSET(  u8, GPIO, State )
#define H3800_ASIC1_GPIO_Reset        H3800_ASIC1_OFFSET(  u8, GPIO, Reset )
#define H3800_ASIC1_GPIO_SleepMask    H3800_ASIC1_OFFSET( u16, GPIO, SleepMask )
#define H3800_ASIC1_GPIO_SleepDir     H3800_ASIC1_OFFSET( u16, GPIO, SleepDir )
#define H3800_ASIC1_GPIO_SleepOut     H3800_ASIC1_OFFSET( u16, GPIO, SleepOut )
#define H3800_ASIC1_GPIO_Status       H3800_ASIC1_OFFSET( u16, GPIO, Status )
#define H3800_ASIC1_GPIO_BattFaultDir H3800_ASIC1_OFFSET( u16, GPIO, BattFaultDir )
#define H3800_ASIC1_GPIO_BattFaultOut H3800_ASIC1_OFFSET( u16, GPIO, BattFaultOut )

#define H3800_ASIC1_GPIO_STATE_MASK            (1 << 0)
#define H3800_ASIC1_GPIO_STATE_DIRECTION       (1 << 1)
#define H3800_ASIC1_GPIO_STATE_OUT             (1 << 2)
#define H3800_ASIC1_GPIO_STATE_TRIGGER_TYPE    (1 << 3)
#define H3800_ASIC1_GPIO_STATE_EDGE_TRIGGER    (1 << 4)
#define H3800_ASIC1_GPIO_STATE_LEVEL_TRIGGER   (1 << 5)

#define H3800_ASIC1_GPIO_RESET_SOFTWARE        (1 << 0)
#define H3800_ASIC1_GPIO_RESET_AUTO_SLEEP      (1 << 1)
#define H3800_ASIC1_GPIO_RESET_FIRST_PWR_ON    (1 << 2)

/* These are all outputs */
#define GPIO_H3800_ASIC1_IR_ON_N          (1 << 0)   /* Apply power to the IR Module */
#define GPIO_H3800_ASIC1_SD_PWR_ON        (1 << 1)   /* Secure Digital power on */
#define GPIO_H3800_ASIC1_RS232_ON         (1 << 2)   /* Turn on power to the RS232 chip ? */
#define GPIO_H3800_ASIC1_PULSE_GEN        (1 << 3)   /* Goes to speaker / earphone */
#define GPIO_H3800_ASIC1_CH_TIMER         (1 << 4)   /* */
#define GPIO_H3800_ASIC1_LCD_5V_ON        (1 << 5)   /* Enables LCD_5V */
#define GPIO_H3800_ASIC1_LCD_ON           (1 << 6)   /* Enables LCD_3V */
#define GPIO_H3800_ASIC1_LCD_PCI          (1 << 7)   /* Connects to PDWN on LCD controller */
#define GPIO_H3800_ASIC1_VGH_ON           (1 << 8)   /* Drives VGH on the LCD (+9??) */
#define GPIO_H3800_ASIC1_VGL_ON           (1 << 9)   /* Drivers VGL on the LCD (-6??) */
#define GPIO_H3800_ASIC1_FL_PWR_ON        (1 << 10)  /* Frontlight power on */
#define GPIO_H3800_ASIC1_BT_PWR_ON        (1 << 11)  /* Bluetooth power on */
#define GPIO_H3800_ASIC1_SPK_ON           (1 << 12)  /* */
#define GPIO_H3800_ASIC1_EAR_ON_N         (1 << 13)  /* */
#define GPIO_H3800_ASIC1_AUD_PWR_ON       (1 << 14)  /* */

/* Write enable for the flash */

#define _H3800_ASIC1_FlashWP_Base         0x1F00
#define _H3800_ASIC1_FlashWP_VPP_ON         0x00    /* R   1: write, 0: protect */
#define H3800_ASIC1_FlashWP_VPP_ON       H3800_ASIC1_OFFSET( u8, FlashWP, VPP_ON )

#endif /* _INCLUDE_H3600_GPIO_H_ */
