/*
 * include/linux/mfd/asic3.h
 *
 * Compaq ASIC3 headers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2007 OpendHand.
 */

#ifndef __ASIC3_H__
#define __ASIC3_H__

#include <linux/types.h>

struct asic3 {
	void __iomem *mapping;
	unsigned int bus_shift;
	unsigned int irq_nr;
	unsigned int irq_base;
	spinlock_t lock;
	u16 irq_bothedge[4];
	struct device *dev;
};

struct asic3_platform_data {
	struct {
		u32 dir;
		u32 init;
		u32 sleep_mask;
		u32 sleep_out;
		u32 batt_fault_out;
		u32 sleep_conf;
		u32 alt_function;
	} gpio_a, gpio_b, gpio_c, gpio_d;

	unsigned int bus_shift;

	unsigned int irq_base;

	struct platform_device **children;
	unsigned int n_children;
};

int asic3_gpio_get_value(struct asic3 *asic, unsigned gpio);
void asic3_gpio_set_value(struct asic3 *asic, unsigned gpio, int val);

#define ASIC3_NUM_GPIO_BANKS	4
#define ASIC3_GPIOS_PER_BANK	16
#define ASIC3_NUM_GPIOS		64
#define ASIC3_NR_IRQS		ASIC3_NUM_GPIOS + 6

#define ASIC3_GPIO_BANK_A	0
#define ASIC3_GPIO_BANK_B	1
#define ASIC3_GPIO_BANK_C	2
#define ASIC3_GPIO_BANK_D	3

#define ASIC3_GPIO(bank, gpio) \
	((ASIC3_GPIOS_PER_BANK * ASIC3_GPIO_BANK_##bank) + (gpio))
#define ASIC3_GPIO_bit(gpio) (1 << (gpio & 0xf))
/* All offsets below are specified with this address bus shift */
#define ASIC3_DEFAULT_ADDR_SHIFT 2

#define ASIC3_OFFSET(base, reg) (ASIC3_##base##_Base + ASIC3_##base##_##reg)
#define ASIC3_GPIO_OFFSET(base, reg) \
	(ASIC3_GPIO_##base##_Base + ASIC3_GPIO_##reg)

#define ASIC3_GPIO_A_Base      0x0000
#define ASIC3_GPIO_B_Base      0x0100
#define ASIC3_GPIO_C_Base      0x0200
#define ASIC3_GPIO_D_Base      0x0300

#define ASIC3_GPIO_Mask          0x00    /* R/W 0:don't mask */
#define ASIC3_GPIO_Direction     0x04    /* R/W 0:input */
#define ASIC3_GPIO_Out           0x08    /* R/W 0:output low */
#define ASIC3_GPIO_TriggerType   0x0c    /* R/W 0:level */
#define ASIC3_GPIO_EdgeTrigger   0x10    /* R/W 0:falling */
#define ASIC3_GPIO_LevelTrigger  0x14    /* R/W 0:low level detect */
#define ASIC3_GPIO_SleepMask     0x18    /* R/W 0:don't mask in sleep mode */
#define ASIC3_GPIO_SleepOut      0x1c    /* R/W level 0:low in sleep mode */
#define ASIC3_GPIO_BattFaultOut  0x20    /* R/W level 0:low in batt_fault */
#define ASIC3_GPIO_IntStatus     0x24    /* R/W 0:none, 1:detect */
#define ASIC3_GPIO_AltFunction   0x28	 /* R/W 1:LED register control */
#define ASIC3_GPIO_SleepConf     0x2c    /*
					  * R/W bit 1: autosleep
					  * 0: disable gposlpout in normal mode,
					  * enable gposlpout in sleep mode.
					  */
#define ASIC3_GPIO_Status        0x30    /* R   Pin status */

#define ASIC3_SPI_Base		      0x0400
#define ASIC3_SPI_Control               0x0000
#define ASIC3_SPI_TxData                0x0004
#define ASIC3_SPI_RxData                0x0008
#define ASIC3_SPI_Int                   0x000c
#define ASIC3_SPI_Status                0x0010

#define SPI_CONTROL_SPR(clk)      ((clk) & 0x0f)  /* Clock rate */

#define ASIC3_PWM_0_Base                0x0500
#define ASIC3_PWM_1_Base                0x0600
#define ASIC3_PWM_TimeBase              0x0000
#define ASIC3_PWM_PeriodTime            0x0004
#define ASIC3_PWM_DutyTime              0x0008

#define PWM_TIMEBASE_VALUE(x)    ((x)&0xf)   /* Low 4 bits sets time base */
#define PWM_TIMEBASE_ENABLE     (1 << 4)   /* Enable clock */

#define ASIC3_LED_0_Base                0x0700
#define ASIC3_LED_1_Base                0x0800
#define ASIC3_LED_2_Base 		      0x0900
#define ASIC3_LED_TimeBase              0x0000    /* R/W  7 bits */
#define ASIC3_LED_PeriodTime            0x0004    /* R/W 12 bits */
#define ASIC3_LED_DutyTime              0x0008    /* R/W 12 bits */
#define ASIC3_LED_AutoStopCount         0x000c    /* R/W 16 bits */

/* LED TimeBase bits - match ASIC2 */
#define LED_TBS		0x0f /* Low 4 bits sets time base, max = 13 */
			     /* Note: max = 5 on hx4700	*/
			     /* 0: maximum time base */
			     /* 1: maximum time base / 2 */
			     /* n: maximum time base / 2^n */

#define LED_EN		(1 << 4) /* LED ON/OFF 0:off, 1:on */
#define LED_AUTOSTOP	(1 << 5) /* LED ON/OFF auto stop 0:disable, 1:enable */
#define LED_ALWAYS	(1 << 6) /* LED Interrupt Mask 0:No mask, 1:mask */

#define ASIC3_CLOCK_Base		0x0A00
#define ASIC3_CLOCK_CDEX           0x00
#define ASIC3_CLOCK_SEL            0x04

#define CLOCK_CDEX_SOURCE       (1 << 0)  /* 2 bits */
#define CLOCK_CDEX_SOURCE0      (1 << 0)
#define CLOCK_CDEX_SOURCE1      (1 << 1)
#define CLOCK_CDEX_SPI          (1 << 2)
#define CLOCK_CDEX_OWM          (1 << 3)
#define CLOCK_CDEX_PWM0         (1 << 4)
#define CLOCK_CDEX_PWM1         (1 << 5)
#define CLOCK_CDEX_LED0         (1 << 6)
#define CLOCK_CDEX_LED1         (1 << 7)
#define CLOCK_CDEX_LED2         (1 << 8)

/* Clocks settings: 1 for 24.576 MHz, 0 for 12.288Mhz */
#define CLOCK_CDEX_SD_HOST      (1 << 9)   /* R/W: SD host clock source */
#define CLOCK_CDEX_SD_BUS       (1 << 10)  /* R/W: SD bus clock source ctrl */
#define CLOCK_CDEX_SMBUS        (1 << 11)
#define CLOCK_CDEX_CONTROL_CX   (1 << 12)

#define CLOCK_CDEX_EX0          (1 << 13)  /* R/W: 32.768 kHz crystal */
#define CLOCK_CDEX_EX1          (1 << 14)  /* R/W: 24.576 MHz crystal */

#define CLOCK_SEL_SD_HCLK_SEL   (1 << 0)   /* R/W: SDIO host clock select */
#define CLOCK_SEL_SD_BCLK_SEL   (1 << 1)   /* R/W: SDIO bus clock select */

/* R/W: INT clock source control (32.768 kHz) */
#define CLOCK_SEL_CX            (1 << 2)


#define ASIC3_INTR_Base		0x0B00

#define ASIC3_INTR_IntMask       0x00  /* Interrupt mask control */
#define ASIC3_INTR_PIntStat      0x04  /* Peripheral interrupt status */
#define ASIC3_INTR_IntCPS        0x08  /* Interrupt timer clock pre-scale */
#define ASIC3_INTR_IntTBS        0x0c  /* Interrupt timer set */

#define ASIC3_INTMASK_GINTMASK    (1 << 0)  /* Global INTs mask 1:enable */
#define ASIC3_INTMASK_GINTEL      (1 << 1)  /* 1: rising edge, 0: hi level */
#define ASIC3_INTMASK_MASK0       (1 << 2)
#define ASIC3_INTMASK_MASK1       (1 << 3)
#define ASIC3_INTMASK_MASK2       (1 << 4)
#define ASIC3_INTMASK_MASK3       (1 << 5)
#define ASIC3_INTMASK_MASK4       (1 << 6)
#define ASIC3_INTMASK_MASK5       (1 << 7)

#define ASIC3_INTR_PERIPHERAL_A   (1 << 0)
#define ASIC3_INTR_PERIPHERAL_B   (1 << 1)
#define ASIC3_INTR_PERIPHERAL_C   (1 << 2)
#define ASIC3_INTR_PERIPHERAL_D   (1 << 3)
#define ASIC3_INTR_LED0           (1 << 4)
#define ASIC3_INTR_LED1           (1 << 5)
#define ASIC3_INTR_LED2           (1 << 6)
#define ASIC3_INTR_SPI            (1 << 7)
#define ASIC3_INTR_SMBUS          (1 << 8)
#define ASIC3_INTR_OWM            (1 << 9)

#define ASIC3_INTR_CPS(x)         ((x)&0x0f)    /* 4 bits, max 14 */
#define ASIC3_INTR_CPS_SET        (1 << 4)    /* Time base enable */


/* Basic control of the SD ASIC */
#define ASIC3_SDHWCTRL_Base	0x0E00
#define ASIC3_SDHWCTRL_SDConf    0x00

#define ASIC3_SDHWCTRL_SUSPEND    (1 << 0)  /* 1=suspend all SD operations */
#define ASIC3_SDHWCTRL_CLKSEL     (1 << 1)  /* 1=SDICK, 0=HCLK */
#define ASIC3_SDHWCTRL_PCLR       (1 << 2)  /* All registers of SDIO cleared */
#define ASIC3_SDHWCTRL_LEVCD      (1 << 3)  /* SD card detection: 0:low */

/* SD card write protection: 0=high */
#define ASIC3_SDHWCTRL_LEVWP      (1 << 4)
#define ASIC3_SDHWCTRL_SDLED      (1 << 5)  /* SD card LED signal 0=disable */

/* SD card power supply ctrl 1=enable */
#define ASIC3_SDHWCTRL_SDPWR      (1 << 6)

#define ASIC3_EXTCF_Base		0x1100

#define ASIC3_EXTCF_Select         0x00
#define ASIC3_EXTCF_Reset          0x04

#define ASIC3_EXTCF_SMOD0	         (1 << 0)  /* slot number of mode 0 */
#define ASIC3_EXTCF_SMOD1	         (1 << 1)  /* slot number of mode 1 */
#define ASIC3_EXTCF_SMOD2	         (1 << 2)  /* slot number of mode 2 */
#define ASIC3_EXTCF_OWM_EN	         (1 << 4)  /* enable onewire module */
#define ASIC3_EXTCF_OWM_SMB	         (1 << 5)  /* OWM bus selection */
#define ASIC3_EXTCF_OWM_RESET            (1 << 6)  /* ?? used by OWM and CF */
#define ASIC3_EXTCF_CF0_SLEEP_MODE       (1 << 7)  /* CF0 sleep state */
#define ASIC3_EXTCF_CF1_SLEEP_MODE       (1 << 8)  /* CF1 sleep state */
#define ASIC3_EXTCF_CF0_PWAIT_EN         (1 << 10) /* CF0 PWAIT_n control */
#define ASIC3_EXTCF_CF1_PWAIT_EN         (1 << 11) /* CF1 PWAIT_n control */
#define ASIC3_EXTCF_CF0_BUF_EN           (1 << 12) /* CF0 buffer control */
#define ASIC3_EXTCF_CF1_BUF_EN           (1 << 13) /* CF1 buffer control */
#define ASIC3_EXTCF_SD_MEM_ENABLE        (1 << 14)
#define ASIC3_EXTCF_CF_SLEEP             (1 << 15) /* CF sleep mode control */

/*********************************************
 *  The Onewire interface registers
 *
 *  OWM_CMD
 *  OWM_DAT
 *  OWM_INTR
 *  OWM_INTEN
 *  OWM_CLKDIV
 *
 *********************************************/

#define ASIC3_OWM_Base		0xC00

#define ASIC3_OWM_CMD         0x00
#define ASIC3_OWM_DAT         0x04
#define ASIC3_OWM_INTR        0x08
#define ASIC3_OWM_INTEN       0x0C
#define ASIC3_OWM_CLKDIV      0x10

#define ASIC3_OWM_CMD_ONEWR         (1 << 0)
#define ASIC3_OWM_CMD_SRA           (1 << 1)
#define ASIC3_OWM_CMD_DQO           (1 << 2)
#define ASIC3_OWM_CMD_DQI           (1 << 3)

#define ASIC3_OWM_INTR_PD          (1 << 0)
#define ASIC3_OWM_INTR_PDR         (1 << 1)
#define ASIC3_OWM_INTR_TBE         (1 << 2)
#define ASIC3_OWM_INTR_TEMP        (1 << 3)
#define ASIC3_OWM_INTR_RBF         (1 << 4)

#define ASIC3_OWM_INTEN_EPD        (1 << 0)
#define ASIC3_OWM_INTEN_IAS        (1 << 1)
#define ASIC3_OWM_INTEN_ETBE       (1 << 2)
#define ASIC3_OWM_INTEN_ETMT       (1 << 3)
#define ASIC3_OWM_INTEN_ERBF       (1 << 4)

#define ASIC3_OWM_CLKDIV_PRE       (3 << 0) /* two bits wide at bit 0 */
#define ASIC3_OWM_CLKDIV_DIV       (7 << 2) /* 3 bits wide at bit 2 */


/*****************************************************************************
 *  The SD configuration registers are at a completely different location
 *  in memory.  They are divided into three sets of registers:
 *
 *  SD_CONFIG         Core configuration register
 *  SD_CTRL           Control registers for SD operations
 *  SDIO_CTRL         Control registers for SDIO operations
 *
 *****************************************************************************/
#define ASIC3_SD_CONFIG_Base            0x0400 /* Assumes 32 bit addressing */

#define ASIC3_SD_CONFIG_Command           0x08   /* R/W: Command */

/* [0:8] SD Control Register Base Address */
#define ASIC3_SD_CONFIG_Addr0             0x20

/* [9:31] SD Control Register Base Address */
#define ASIC3_SD_CONFIG_Addr1             0x24

/* R/O: interrupt assigned to pin */
#define ASIC3_SD_CONFIG_IntPin            0x78

/*
 * Set to 0x1f to clock SD controller, 0 otherwise.
 * At 0x82 - Gated Clock Ctrl
 */
#define ASIC3_SD_CONFIG_ClkStop           0x80

/* Control clock of SD controller */
#define ASIC3_SD_CONFIG_ClockMode         0x84
#define ASIC3_SD_CONFIG_SDHC_PinStatus    0x88   /* R/0: SD pins status */
#define ASIC3_SD_CONFIG_SDHC_Power1       0x90   /* Power1 - manual pwr ctrl */

/* auto power up after card inserted */
#define ASIC3_SD_CONFIG_SDHC_Power2       0x92

/* auto power down when card removed */
#define ASIC3_SD_CONFIG_SDHC_Power3       0x94
#define ASIC3_SD_CONFIG_SDHC_CardDetect   0x98
#define ASIC3_SD_CONFIG_SDHC_Slot         0xA0   /* R/O: support slot number */
#define ASIC3_SD_CONFIG_SDHC_ExtGateClk1  0x1E0  /* Not used */
#define ASIC3_SD_CONFIG_SDHC_ExtGateClk2  0x1E2  /* Not used*/

/* GPIO Output Reg. , at 0x1EA - GPIO Output Enable Reg. */
#define ASIC3_SD_CONFIG_SDHC_GPIO_OutAndEnable  0x1E8
#define ASIC3_SD_CONFIG_SDHC_GPIO_Status  0x1EC  /* GPIO Status Reg. */

/* Bit 1: double buffer/single buffer */
#define ASIC3_SD_CONFIG_SDHC_ExtGateClk3  0x1F0

/* Memory access enable (set to 1 to access SD Controller) */
#define SD_CONFIG_COMMAND_MAE                (1<<1)

#define SD_CONFIG_CLK_ENABLE_ALL             0x1f

#define SD_CONFIG_POWER1_PC_33V              0x0200    /* Set for 3.3 volts */
#define SD_CONFIG_POWER1_PC_OFF              0x0000    /* Turn off power */

 /* two bits - number of cycles for card detection */
#define SD_CONFIG_CARDDETECTMODE_CLK           ((x) & 0x3)


#define ASIC3_SD_CTRL_Base            0x1000

#define ASIC3_SD_CTRL_Cmd                  0x00
#define ASIC3_SD_CTRL_Arg0                 0x08
#define ASIC3_SD_CTRL_Arg1                 0x0C
#define ASIC3_SD_CTRL_StopInternal         0x10
#define ASIC3_SD_CTRL_TransferSectorCount  0x14
#define ASIC3_SD_CTRL_Response0            0x18
#define ASIC3_SD_CTRL_Response1            0x1C
#define ASIC3_SD_CTRL_Response2            0x20
#define ASIC3_SD_CTRL_Response3            0x24
#define ASIC3_SD_CTRL_Response4            0x28
#define ASIC3_SD_CTRL_Response5            0x2C
#define ASIC3_SD_CTRL_Response6            0x30
#define ASIC3_SD_CTRL_Response7            0x34
#define ASIC3_SD_CTRL_CardStatus           0x38
#define ASIC3_SD_CTRL_BufferCtrl           0x3C
#define ASIC3_SD_CTRL_IntMaskCard          0x40
#define ASIC3_SD_CTRL_IntMaskBuffer        0x44
#define ASIC3_SD_CTRL_CardClockCtrl        0x48
#define ASIC3_SD_CTRL_MemCardXferDataLen   0x4C
#define ASIC3_SD_CTRL_MemCardOptionSetup   0x50
#define ASIC3_SD_CTRL_ErrorStatus0         0x58
#define ASIC3_SD_CTRL_ErrorStatus1         0x5C
#define ASIC3_SD_CTRL_DataPort             0x60
#define ASIC3_SD_CTRL_TransactionCtrl      0x68
#define ASIC3_SD_CTRL_SoftwareReset        0x1C0

#define SD_CTRL_SOFTWARE_RESET_CLEAR            (1<<0)

#define SD_CTRL_TRANSACTIONCONTROL_SET          (1<<8)

#define SD_CTRL_CARDCLOCKCONTROL_FOR_SD_CARD    (1<<15)
#define SD_CTRL_CARDCLOCKCONTROL_ENABLE_CLOCK   (1<<8)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_512    (1<<7)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_256    (1<<6)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_128    (1<<5)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_64     (1<<4)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_32     (1<<3)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_16     (1<<2)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_8      (1<<1)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_4      (1<<0)
#define SD_CTRL_CARDCLOCKCONTROL_CLK_DIV_2      (0<<0)

#define MEM_CARD_OPTION_REQUIRED                   0x000e
#define MEM_CARD_OPTION_DATA_RESPONSE_TIMEOUT(x)   (((x) & 0x0f) << 4)
#define MEM_CARD_OPTION_C2_MODULE_NOT_PRESENT      (1<<14)
#define MEM_CARD_OPTION_DATA_XFR_WIDTH_1           (1<<15)
#define MEM_CARD_OPTION_DATA_XFR_WIDTH_4           0

#define SD_CTRL_COMMAND_INDEX(x)                   ((x) & 0x3f)
#define SD_CTRL_COMMAND_TYPE_CMD                   (0 << 6)
#define SD_CTRL_COMMAND_TYPE_ACMD                  (1 << 6)
#define SD_CTRL_COMMAND_TYPE_AUTHENTICATION        (2 << 6)
#define SD_CTRL_COMMAND_RESPONSE_TYPE_NORMAL       (0 << 8)
#define SD_CTRL_COMMAND_RESPONSE_TYPE_EXT_R1       (4 << 8)
#define SD_CTRL_COMMAND_RESPONSE_TYPE_EXT_R1B      (5 << 8)
#define SD_CTRL_COMMAND_RESPONSE_TYPE_EXT_R2       (6 << 8)
#define SD_CTRL_COMMAND_RESPONSE_TYPE_EXT_R3       (7 << 8)
#define SD_CTRL_COMMAND_DATA_PRESENT               (1 << 11)
#define SD_CTRL_COMMAND_TRANSFER_READ              (1 << 12)
#define SD_CTRL_COMMAND_TRANSFER_WRITE             (0 << 12)
#define SD_CTRL_COMMAND_MULTI_BLOCK                (1 << 13)
#define SD_CTRL_COMMAND_SECURITY_CMD               (1 << 14)

#define SD_CTRL_STOP_INTERNAL_ISSSUE_CMD12         (1 << 0)
#define SD_CTRL_STOP_INTERNAL_AUTO_ISSUE_CMD12     (1 << 8)

#define SD_CTRL_CARDSTATUS_RESPONSE_END            (1 << 0)
#define SD_CTRL_CARDSTATUS_RW_END                  (1 << 2)
#define SD_CTRL_CARDSTATUS_CARD_REMOVED_0          (1 << 3)
#define SD_CTRL_CARDSTATUS_CARD_INSERTED_0         (1 << 4)
#define SD_CTRL_CARDSTATUS_SIGNAL_STATE_PRESENT_0  (1 << 5)
#define SD_CTRL_CARDSTATUS_WRITE_PROTECT           (1 << 7)
#define SD_CTRL_CARDSTATUS_CARD_REMOVED_3          (1 << 8)
#define SD_CTRL_CARDSTATUS_CARD_INSERTED_3         (1 << 9)
#define SD_CTRL_CARDSTATUS_SIGNAL_STATE_PRESENT_3  (1 << 10)

#define SD_CTRL_BUFFERSTATUS_CMD_INDEX_ERROR       (1 << 0)
#define SD_CTRL_BUFFERSTATUS_CRC_ERROR             (1 << 1)
#define SD_CTRL_BUFFERSTATUS_STOP_BIT_END_ERROR    (1 << 2)
#define SD_CTRL_BUFFERSTATUS_DATA_TIMEOUT          (1 << 3)
#define SD_CTRL_BUFFERSTATUS_BUFFER_OVERFLOW       (1 << 4)
#define SD_CTRL_BUFFERSTATUS_BUFFER_UNDERFLOW      (1 << 5)
#define SD_CTRL_BUFFERSTATUS_CMD_TIMEOUT           (1 << 6)
#define SD_CTRL_BUFFERSTATUS_UNK7                  (1 << 7)
#define SD_CTRL_BUFFERSTATUS_BUFFER_READ_ENABLE    (1 << 8)
#define SD_CTRL_BUFFERSTATUS_BUFFER_WRITE_ENABLE   (1 << 9)
#define SD_CTRL_BUFFERSTATUS_ILLEGAL_FUNCTION      (1 << 13)
#define SD_CTRL_BUFFERSTATUS_CMD_BUSY              (1 << 14)
#define SD_CTRL_BUFFERSTATUS_ILLEGAL_ACCESS        (1 << 15)

#define SD_CTRL_INTMASKCARD_RESPONSE_END           (1 << 0)
#define SD_CTRL_INTMASKCARD_RW_END                 (1 << 2)
#define SD_CTRL_INTMASKCARD_CARD_REMOVED_0         (1 << 3)
#define SD_CTRL_INTMASKCARD_CARD_INSERTED_0        (1 << 4)
#define SD_CTRL_INTMASKCARD_SIGNAL_STATE_PRESENT_0 (1 << 5)
#define SD_CTRL_INTMASKCARD_UNK6                   (1 << 6)
#define SD_CTRL_INTMASKCARD_WRITE_PROTECT          (1 << 7)
#define SD_CTRL_INTMASKCARD_CARD_REMOVED_3         (1 << 8)
#define SD_CTRL_INTMASKCARD_CARD_INSERTED_3        (1 << 9)
#define SD_CTRL_INTMASKCARD_SIGNAL_STATE_PRESENT_3 (1 << 10)

#define SD_CTRL_INTMASKBUFFER_CMD_INDEX_ERROR      (1 << 0)
#define SD_CTRL_INTMASKBUFFER_CRC_ERROR            (1 << 1)
#define SD_CTRL_INTMASKBUFFER_STOP_BIT_END_ERROR   (1 << 2)
#define SD_CTRL_INTMASKBUFFER_DATA_TIMEOUT         (1 << 3)
#define SD_CTRL_INTMASKBUFFER_BUFFER_OVERFLOW      (1 << 4)
#define SD_CTRL_INTMASKBUFFER_BUFFER_UNDERFLOW     (1 << 5)
#define SD_CTRL_INTMASKBUFFER_CMD_TIMEOUT          (1 << 6)
#define SD_CTRL_INTMASKBUFFER_UNK7                 (1 << 7)
#define SD_CTRL_INTMASKBUFFER_BUFFER_READ_ENABLE   (1 << 8)
#define SD_CTRL_INTMASKBUFFER_BUFFER_WRITE_ENABLE  (1 << 9)
#define SD_CTRL_INTMASKBUFFER_ILLEGAL_FUNCTION     (1 << 13)
#define SD_CTRL_INTMASKBUFFER_CMD_BUSY             (1 << 14)
#define SD_CTRL_INTMASKBUFFER_ILLEGAL_ACCESS       (1 << 15)

#define SD_CTRL_DETAIL0_RESPONSE_CMD_ERROR                   (1 << 0)
#define SD_CTRL_DETAIL0_END_BIT_ERROR_FOR_RESPONSE_NON_CMD12 (1 << 2)
#define SD_CTRL_DETAIL0_END_BIT_ERROR_FOR_RESPONSE_CMD12     (1 << 3)
#define SD_CTRL_DETAIL0_END_BIT_ERROR_FOR_READ_DATA          (1 << 4)
#define SD_CTRL_DETAIL0_END_BIT_ERROR_FOR_WRITE_CRC_STATUS   (1 << 5)
#define SD_CTRL_DETAIL0_CRC_ERROR_FOR_RESPONSE_NON_CMD12     (1 << 8)
#define SD_CTRL_DETAIL0_CRC_ERROR_FOR_RESPONSE_CMD12         (1 << 9)
#define SD_CTRL_DETAIL0_CRC_ERROR_FOR_READ_DATA              (1 << 10)
#define SD_CTRL_DETAIL0_CRC_ERROR_FOR_WRITE_CMD              (1 << 11)

#define SD_CTRL_DETAIL1_NO_CMD_RESPONSE                      (1 << 0)
#define SD_CTRL_DETAIL1_TIMEOUT_READ_DATA                    (1 << 4)
#define SD_CTRL_DETAIL1_TIMEOUT_CRS_STATUS                   (1 << 5)
#define SD_CTRL_DETAIL1_TIMEOUT_CRC_BUSY                     (1 << 6)

#define ASIC3_SDIO_CTRL_Base          0x1200

#define ASIC3_SDIO_CTRL_Cmd                  0x00
#define ASIC3_SDIO_CTRL_CardPortSel          0x04
#define ASIC3_SDIO_CTRL_Arg0                 0x08
#define ASIC3_SDIO_CTRL_Arg1                 0x0C
#define ASIC3_SDIO_CTRL_TransferBlockCount   0x14
#define ASIC3_SDIO_CTRL_Response0            0x18
#define ASIC3_SDIO_CTRL_Response1            0x1C
#define ASIC3_SDIO_CTRL_Response2            0x20
#define ASIC3_SDIO_CTRL_Response3            0x24
#define ASIC3_SDIO_CTRL_Response4            0x28
#define ASIC3_SDIO_CTRL_Response5            0x2C
#define ASIC3_SDIO_CTRL_Response6            0x30
#define ASIC3_SDIO_CTRL_Response7            0x34
#define ASIC3_SDIO_CTRL_CardStatus           0x38
#define ASIC3_SDIO_CTRL_BufferCtrl           0x3C
#define ASIC3_SDIO_CTRL_IntMaskCard          0x40
#define ASIC3_SDIO_CTRL_IntMaskBuffer        0x44
#define ASIC3_SDIO_CTRL_CardXferDataLen      0x4C
#define ASIC3_SDIO_CTRL_CardOptionSetup      0x50
#define ASIC3_SDIO_CTRL_ErrorStatus0         0x54
#define ASIC3_SDIO_CTRL_ErrorStatus1         0x58
#define ASIC3_SDIO_CTRL_DataPort             0x60
#define ASIC3_SDIO_CTRL_TransactionCtrl      0x68
#define ASIC3_SDIO_CTRL_CardIntCtrl          0x6C
#define ASIC3_SDIO_CTRL_ClocknWaitCtrl       0x70
#define ASIC3_SDIO_CTRL_HostInformation      0x74
#define ASIC3_SDIO_CTRL_ErrorCtrl            0x78
#define ASIC3_SDIO_CTRL_LEDCtrl              0x7C
#define ASIC3_SDIO_CTRL_SoftwareReset        0x1C0

#define ASIC3_MAP_SIZE	     		     0x2000

#endif /* __ASIC3_H__ */
