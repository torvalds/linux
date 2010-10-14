#ifndef _LINUX_SPI_CPCAP_H
#define _LINUX_SPI_CPCAP_H

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 *
 */

#include <linux/ioctl.h>
#ifdef __KERNEL__
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#endif

#ifdef CONFIG_RTC_INTF_CPCAP_SECCLKD
#include <linux/rtc.h>
#endif

#define CPCAP_DEV_NAME "cpcap"
#define CPCAP_NUM_REG_CPCAP (CPCAP_REG_END - CPCAP_REG_START + 1)

#define CPCAP_IRQ_INT1_INDEX 0
#define CPCAP_IRQ_INT2_INDEX 16
#define CPCAP_IRQ_INT3_INDEX 32
#define CPCAP_IRQ_INT4_INDEX 48
#define CPCAP_IRQ_INT5_INDEX 64

#define CPCAP_HWCFG_NUM       2    /* The number of hardware config words. */
/*
 * Tell the uC to setup the secondary standby bits for the regulators used.
 */
#define CPCAP_HWCFG0_SEC_STBY_SW1       0x0001
#define CPCAP_HWCFG0_SEC_STBY_SW2       0x0002
#define CPCAP_HWCFG0_SEC_STBY_SW3       0x0004
#define CPCAP_HWCFG0_SEC_STBY_SW4       0x0008
#define CPCAP_HWCFG0_SEC_STBY_SW5       0x0010
#define CPCAP_HWCFG0_SEC_STBY_VAUDIO    0x0020
#define CPCAP_HWCFG0_SEC_STBY_VCAM      0x0040
#define CPCAP_HWCFG0_SEC_STBY_VCSI      0x0080
#define CPCAP_HWCFG0_SEC_STBY_VDAC      0x0100
#define CPCAP_HWCFG0_SEC_STBY_VDIG      0x0200
#define CPCAP_HWCFG0_SEC_STBY_VHVIO     0x0400
#define CPCAP_HWCFG0_SEC_STBY_VPLL      0x0800
#define CPCAP_HWCFG0_SEC_STBY_VRF1      0x1000
#define CPCAP_HWCFG0_SEC_STBY_VRF2      0x2000
#define CPCAP_HWCFG0_SEC_STBY_VRFREF    0x4000
#define CPCAP_HWCFG0_SEC_STBY_VSDIO     0x8000

#define CPCAP_HWCFG1_SEC_STBY_VWLAN1    0x0001
#define CPCAP_HWCFG1_SEC_STBY_VWLAN2    0x0002
#define CPCAP_HWCFG1_SEC_STBY_VSIM      0x0004
#define CPCAP_HWCFG1_SEC_STBY_VSIMCARD  0x0008

#define CPCAP_WHISPER_MODE_PU       0x00000001
#define CPCAP_WHISPER_ENABLE_UART   0x00000002
#define CPCAP_WHISPER_ACCY_MASK     0xF8000000
#define CPCAP_WHISPER_ACCY_SHFT     27
#define CPCAP_WHISPER_ID_SIZE       16
#define CPCAP_WHISPER_PROP_SIZE     7

enum cpcap_regulator_id {
	CPCAP_SW2,
	CPCAP_SW4,
	CPCAP_SW5,
	CPCAP_VCAM,
	CPCAP_VCSI,
	CPCAP_VDAC,
	CPCAP_VDIG,
	CPCAP_VFUSE,
	CPCAP_VHVIO,
	CPCAP_VSDIO,
	CPCAP_VPLL,
	CPCAP_VRF1,
	CPCAP_VRF2,
	CPCAP_VRFREF,
	CPCAP_VWLAN1,
	CPCAP_VWLAN2,
	CPCAP_VSIM,
	CPCAP_VSIMCARD,
	CPCAP_VVIB,
	CPCAP_VUSB,
	CPCAP_VAUDIO,
	CPCAP_NUM_REGULATORS
};

/*
 * Enumeration of all registers in the cpcap. Note that the register
 * numbers on the CPCAP IC are not contiguous. The values of the enums below
 * are not the actual register numbers.
 */
enum cpcap_reg {
	CPCAP_REG_START,        /* Start of CPCAP registers. */

	CPCAP_REG_INT1 = CPCAP_REG_START, /* Interrupt 1 */
	CPCAP_REG_INT2,		/* Interrupt 2 */
	CPCAP_REG_INT3,		/* Interrupt 3 */
	CPCAP_REG_INT4,		/* Interrupt 4 */
	CPCAP_REG_INTM1,	/* Interrupt Mask 1 */
	CPCAP_REG_INTM2,	/* Interrupt Mask 2 */
	CPCAP_REG_INTM3,	/* Interrupt Mask 3 */
	CPCAP_REG_INTM4,	/* Interrupt Mask 4 */
	CPCAP_REG_INTS1,	/* Interrupt Sense 1 */
	CPCAP_REG_INTS2,	/* Interrupt Sense 2 */
	CPCAP_REG_INTS3,	/* Interrupt Sense 3 */
	CPCAP_REG_INTS4,	/* Interrupt Sense 4 */
	CPCAP_REG_ASSIGN1,	/* Resource Assignment 1 */
	CPCAP_REG_ASSIGN2,	/* Resource Assignment 2 */
	CPCAP_REG_ASSIGN3,	/* Resource Assignment 3 */
	CPCAP_REG_ASSIGN4,	/* Resource Assignment 4 */
	CPCAP_REG_ASSIGN5,	/* Resource Assignment 5 */
	CPCAP_REG_ASSIGN6,	/* Resource Assignment 6 */
	CPCAP_REG_VERSC1,	/* Version Control 1 */
	CPCAP_REG_VERSC2,	/* Version Control 2 */

	CPCAP_REG_MI1,		/* Macro Interrupt 1 */
	CPCAP_REG_MIM1,		/* Macro Interrupt Mask 1 */
	CPCAP_REG_MI2,		/* Macro Interrupt 2 */
	CPCAP_REG_MIM2,		/* Macro Interrupt Mask 2 */
	CPCAP_REG_UCC1,		/* UC Control 1 */
	CPCAP_REG_UCC2,		/* UC Control 2 */
	CPCAP_REG_PC1,		/* Power Cut 1 */
	CPCAP_REG_PC2,		/* Power Cut 2 */
	CPCAP_REG_BPEOL,	/* BP and EOL */
	CPCAP_REG_PGC,		/* Power Gate and Control */
	CPCAP_REG_MT1,		/* Memory Transfer 1 */
	CPCAP_REG_MT2,		/* Memory Transfer 2 */
	CPCAP_REG_MT3,		/* Memory Transfer 3 */
	CPCAP_REG_PF,		/* Print Format */

	CPCAP_REG_SCC,		/* System Clock Control */
	CPCAP_REG_SW1,		/* Stop Watch 1 */
	CPCAP_REG_SW2,		/* Stop Watch 2 */
	CPCAP_REG_UCTM,		/* UC Turbo Mode */
	CPCAP_REG_TOD1,		/* Time of Day 1 */
	CPCAP_REG_TOD2,		/* Time of Day 2 */
	CPCAP_REG_TODA1,	/* Time of Day Alarm 1 */
	CPCAP_REG_TODA2,	/* Time of Day Alarm 2 */
	CPCAP_REG_DAY,		/* Day */
	CPCAP_REG_DAYA,		/* Day Alarm */
	CPCAP_REG_VAL1,		/* Validity 1 */
	CPCAP_REG_VAL2,		/* Validity 2 */

	CPCAP_REG_SDVSPLL,	/* Switcher DVS and PLL */
	CPCAP_REG_SI2CC1,	/* Switcher I2C Control 1 */
	CPCAP_REG_Si2CC2,	/* Switcher I2C Control 2 */
	CPCAP_REG_S1C1,	        /* Switcher 1 Control 1 */
	CPCAP_REG_S1C2,	        /* Switcher 1 Control 2 */
	CPCAP_REG_S2C1,	        /* Switcher 2 Control 1 */
	CPCAP_REG_S2C2,	        /* Switcher 2 Control 2 */
	CPCAP_REG_S3C,	        /* Switcher 3 Control */
	CPCAP_REG_S4C1,	        /* Switcher 4 Control 1 */
	CPCAP_REG_S4C2,	        /* Switcher 4 Control 2 */
	CPCAP_REG_S5C,	        /* Switcher 5 Control */
	CPCAP_REG_S6C,	        /* Switcher 6 Control */
	CPCAP_REG_VCAMC,	/* VCAM Control */
	CPCAP_REG_VCSIC,	/* VCSI Control */
	CPCAP_REG_VDACC,	/* VDAC Control */
	CPCAP_REG_VDIGC,	/* VDIG Control */
	CPCAP_REG_VFUSEC,	/* VFUSE Control */
	CPCAP_REG_VHVIOC,	/* VHVIO Control */
	CPCAP_REG_VSDIOC,	/* VSDIO Control */
	CPCAP_REG_VPLLC,	/* VPLL Control */
	CPCAP_REG_VRF1C,	/* VRF1 Control */
	CPCAP_REG_VRF2C,	/* VRF2 Control */
	CPCAP_REG_VRFREFC,	/* VRFREF Control */
	CPCAP_REG_VWLAN1C,	/* VWLAN1 Control */
	CPCAP_REG_VWLAN2C,	/* VWLAN2 Control */
	CPCAP_REG_VSIMC,	/* VSIM Control */
	CPCAP_REG_VVIBC,	/* VVIB Control */
	CPCAP_REG_VUSBC,	/* VUSB Control */
	CPCAP_REG_VUSBINT1C,	/* VUSBINT1 Control */
	CPCAP_REG_VUSBINT2C,	/* VUSBINT2 Control */
	CPCAP_REG_URT,		/* Useroff Regulator Trigger */
	CPCAP_REG_URM1,		/* Useroff Regulator Mask 1 */
	CPCAP_REG_URM2,		/* Useroff Regulator Mask 2 */

	CPCAP_REG_VAUDIOC,	/* VAUDIO Control */
	CPCAP_REG_CC,		/* Codec Control */
	CPCAP_REG_CDI,		/* Codec Digital Interface */
	CPCAP_REG_SDAC,		/* Stereo DAC */
	CPCAP_REG_SDACDI,	/* Stereo DAC Digital Interface */
	CPCAP_REG_TXI,		/* TX Inputs */
	CPCAP_REG_TXMP,		/* TX MIC PGA's */
	CPCAP_REG_RXOA,		/* RX Output Amplifiers */
	CPCAP_REG_RXVC,		/* RX Volume Control */
	CPCAP_REG_RXCOA,	/* RX Codec to Output Amps */
	CPCAP_REG_RXSDOA,	/* RX Stereo DAC to Output Amps */
	CPCAP_REG_RXEPOA,	/* RX External PGA to Output Amps */
	CPCAP_REG_RXLL,		/* RX Low Latency */
	CPCAP_REG_A2LA,		/* A2 Loudspeaker Amplifier */
	CPCAP_REG_MIPIS1,	/* MIPI Slimbus 1 */
	CPCAP_REG_MIPIS2,	/* MIPI Slimbus 2 */
	CPCAP_REG_MIPIS3,	/* MIPI Slimbus 3. */
	CPCAP_REG_LVAB,		/* LMR Volume and A4 Balanced. */

	CPCAP_REG_CCC1,		/* Coulomb Counter Control 1 */
	CPCAP_REG_CRM,		/* Charger and Reverse Mode */
	CPCAP_REG_CCCC2,	/* Coincell and Coulomb Ctr Ctrl 2 */
	CPCAP_REG_CCS1,		/* Coulomb Counter Sample 1 */
	CPCAP_REG_CCS2,		/* Coulomb Counter Sample 2 */
	CPCAP_REG_CCA1,		/* Coulomb Counter Accumulator 1 */
	CPCAP_REG_CCA2,		/* Coulomb Counter Accumulator 2 */
	CPCAP_REG_CCM,		/* Coulomb Counter Mode */
	CPCAP_REG_CCO,		/* Coulomb Counter Offset */
	CPCAP_REG_CCI,		/* Coulomb Counter Integrator */

	CPCAP_REG_ADCC1,	/* A/D Converter Configuration 1 */
	CPCAP_REG_ADCC2,	/* A/D Converter Configuration 2 */
	CPCAP_REG_ADCD0,	/* A/D Converter Data 0 */
	CPCAP_REG_ADCD1,	/* A/D Converter Data 1 */
	CPCAP_REG_ADCD2,	/* A/D Converter Data 2 */
	CPCAP_REG_ADCD3,	/* A/D Converter Data 3 */
	CPCAP_REG_ADCD4,	/* A/D Converter Data 4 */
	CPCAP_REG_ADCD5,	/* A/D Converter Data 5 */
	CPCAP_REG_ADCD6,	/* A/D Converter Data 6 */
	CPCAP_REG_ADCD7,	/* A/D Converter Data 7 */
	CPCAP_REG_ADCAL1,	/* A/D Converter Calibration 1 */
	CPCAP_REG_ADCAL2,	/* A/D Converter Calibration 2 */

	CPCAP_REG_USBC1,	/* USB Control 1 */
	CPCAP_REG_USBC2,	/* USB Control 2 */
	CPCAP_REG_USBC3,	/* USB Control 3 */
	CPCAP_REG_UVIDL,	/* ULPI Vendor ID Low */
	CPCAP_REG_UVIDH,	/* ULPI Vendor ID High */
	CPCAP_REG_UPIDL,	/* ULPI Product ID Low */
	CPCAP_REG_UPIDH,	/* ULPI Product ID High */
	CPCAP_REG_UFC1,		/* ULPI Function Control 1 */
	CPCAP_REG_UFC2,		/* ULPI Function Control 2 */
	CPCAP_REG_UFC3,		/* ULPI Function Control 3 */
	CPCAP_REG_UIC1,		/* ULPI Interface Control 1 */
	CPCAP_REG_UIC2,		/* ULPI Interface Control 2 */
	CPCAP_REG_UIC3,		/* ULPI Interface Control 3 */
	CPCAP_REG_USBOTG1,	/* USB OTG Control 1 */
	CPCAP_REG_USBOTG2,	/* USB OTG Control 2 */
	CPCAP_REG_USBOTG3,	/* USB OTG Control 3 */
	CPCAP_REG_UIER1,	/* USB Interrupt Enable Rising 1 */
	CPCAP_REG_UIER2,	/* USB Interrupt Enable Rising 2 */
	CPCAP_REG_UIER3,	/* USB Interrupt Enable Rising 3 */
	CPCAP_REG_UIEF1,	/* USB Interrupt Enable Falling 1 */
	CPCAP_REG_UIEF2,	/* USB Interrupt Enable Falling 1 */
	CPCAP_REG_UIEF3,	/* USB Interrupt Enable Falling 1 */
	CPCAP_REG_UIS,		/* USB Interrupt Status */
	CPCAP_REG_UIL,		/* USB Interrupt Latch */
	CPCAP_REG_USBD,		/* USB Debug */
	CPCAP_REG_SCR1,		/* Scratch 1 */
	CPCAP_REG_SCR2,		/* Scratch 2 */
	CPCAP_REG_SCR3,		/* Scratch 3 */
	CPCAP_REG_VMC,		/* Video Mux Control */
	CPCAP_REG_OWDC,		/* One Wire Device Control */
	CPCAP_REG_GPIO0,	/* GPIO 0 Control */
	CPCAP_REG_GPIO1,	/* GPIO 1 Control */
	CPCAP_REG_GPIO2,	/* GPIO 2 Control */
	CPCAP_REG_GPIO3,	/* GPIO 3 Control */
	CPCAP_REG_GPIO4,	/* GPIO 4 Control */
	CPCAP_REG_GPIO5,	/* GPIO 5 Control */
	CPCAP_REG_GPIO6,	/* GPIO 6 Control */

	CPCAP_REG_MDLC,		/* Main Display Lighting Control */
	CPCAP_REG_KLC,		/* Keypad Lighting Control */
	CPCAP_REG_ADLC,		/* Aux Display Lighting Control */
	CPCAP_REG_REDC,		/* Red Triode Control */
	CPCAP_REG_GREENC,	/* Green Triode Control */
	CPCAP_REG_BLUEC,	/* Blue Triode Control */
	CPCAP_REG_CFC,		/* Camera Flash Control */
	CPCAP_REG_ABC,		/* Adaptive Boost Control */
	CPCAP_REG_BLEDC,	/* Bluetooth LED Control */
	CPCAP_REG_CLEDC,	/* Camera Privacy LED Control */

	CPCAP_REG_OW1C,		/* One Wire 1 Command */
	CPCAP_REG_OW1D,		/* One Wire 1 Data */
	CPCAP_REG_OW1I,		/* One Wire 1 Interrupt */
	CPCAP_REG_OW1IE,	/* One Wire 1 Interrupt Enable */
	CPCAP_REG_OW1,		/* One Wire 1 Control */
	CPCAP_REG_OW2C,		/* One Wire 2 Command */
	CPCAP_REG_OW2D,		/* One Wire 2 Data */
	CPCAP_REG_OW2I,		/* One Wire 2 Interrupt */
	CPCAP_REG_OW2IE,	/* One Wire 2 Interrupt Enable */
	CPCAP_REG_OW2,		/* One Wire 2 Control */
	CPCAP_REG_OW3C,		/* One Wire 3 Command */
	CPCAP_REG_OW3D,		/* One Wire 3 Data */
	CPCAP_REG_OW3I,		/* One Wire 3 Interrupt */
	CPCAP_REG_OW3IE,	/* One Wire 3 Interrupt Enable */
	CPCAP_REG_OW3,		/* One Wire 3 Control */
	CPCAP_REG_GCAIC,	/* GCAI Clock Control */
	CPCAP_REG_GCAIM,	/* GCAI GPIO Mode */
	CPCAP_REG_LGDIR,	/* LMR GCAI GPIO Direction */
	CPCAP_REG_LGPU,		/* LMR GCAI GPIO Pull-up */
	CPCAP_REG_LGPIN,	/* LMR GCAI GPIO Pin */
	CPCAP_REG_LGMASK,	/* LMR GCAI GPIO Mask */
	CPCAP_REG_LDEB,		/* LMR Debounce Settings */
	CPCAP_REG_LGDET,	/* LMR GCAI Detach Detect */
	CPCAP_REG_LMISC,	/* LMR Misc Bits */
	CPCAP_REG_LMACE,	/* LMR Mace IC Support */

	CPCAP_REG_END = CPCAP_REG_LMACE, /* End of CPCAP registers. */

	CPCAP_REG_MAX		/* The largest valid register value. */
	= CPCAP_REG_END,

	CPCAP_REG_SIZE = CPCAP_REG_MAX + 1,
	CPCAP_REG_UNUSED = CPCAP_REG_MAX + 2,
};

enum {
	CPCAP_IOCTL_NUM_TEST__START,
	CPCAP_IOCTL_NUM_TEST_READ_REG,
	CPCAP_IOCTL_NUM_TEST_WRITE_REG,
	CPCAP_IOCTL_NUM_TEST__END,

	CPCAP_IOCTL_NUM_ADC__START,
	CPCAP_IOCTL_NUM_ADC_PHASE,
	CPCAP_IOCTL_NUM_ADC__END,

	CPCAP_IOCTL_NUM_BATT__START,
	CPCAP_IOCTL_NUM_BATT_DISPLAY_UPDATE,
	CPCAP_IOCTL_NUM_BATT_ATOD_ASYNC,
	CPCAP_IOCTL_NUM_BATT_ATOD_SYNC,
	CPCAP_IOCTL_NUM_BATT_ATOD_READ,
	CPCAP_IOCTL_NUM_BATT__END,

	CPCAP_IOCTL_NUM_UC__START,
	CPCAP_IOCTL_NUM_UC_MACRO_START,
	CPCAP_IOCTL_NUM_UC_MACRO_STOP,
	CPCAP_IOCTL_NUM_UC_GET_VENDOR,
	CPCAP_IOCTL_NUM_UC_SET_TURBO_MODE,
	CPCAP_IOCTL_NUM_UC__END,

#ifdef CONFIG_RTC_INTF_CPCAP_SECCLKD
	CPCAP_IOCTL_NUM_RTC__START,
	CPCAP_IOCTL_NUM_RTC_COUNT,
	CPCAP_IOCTL_NUM_RTC__END,
#endif

	CPCAP_IOCTL_NUM_ACCY__START,
	CPCAP_IOCTL_NUM_ACCY_WHISPER,
	CPCAP_IOCTL_NUM_ACCY__END,
};

enum cpcap_irqs {
	CPCAP_IRQ__START,		/* 1st supported interrupt event */
	CPCAP_IRQ_HSCLK = CPCAP_IRQ_INT1_INDEX, /* High Speed Clock */
	CPCAP_IRQ_PRIMAC,		/* Primary Macro */
	CPCAP_IRQ_SECMAC,		/* Secondary Macro */
	CPCAP_IRQ_LOWBPL,		/* Low Battery Low Threshold */
	CPCAP_IRQ_SEC2PRI,		/* 2nd Macro to Primary Processor */
	CPCAP_IRQ_LOWBPH,		/* Low Battery High Threshold  */
	CPCAP_IRQ_EOL,			/* End of Life */
	CPCAP_IRQ_TS,			/* Touchscreen */
	CPCAP_IRQ_ADCDONE,		/* ADC Conversion Complete */
	CPCAP_IRQ_HS,			/* Headset */
	CPCAP_IRQ_MB2,			/* Mic Bias2 */
	CPCAP_IRQ_VBUSOV,		/* Overvoltage Detected */
	CPCAP_IRQ_RVRS_CHRG,		/* Reverse Charge */
	CPCAP_IRQ_CHRG_DET,		/* Charger Detected */
	CPCAP_IRQ_IDFLOAT,		/* ID Float */
	CPCAP_IRQ_IDGND,		/* ID Ground */

	CPCAP_IRQ_SE1 = CPCAP_IRQ_INT2_INDEX, /* SE1 Detector */
	CPCAP_IRQ_SESSEND,		/* Session End */
	CPCAP_IRQ_SESSVLD,		/* Session Valid */
	CPCAP_IRQ_VBUSVLD,		/* VBUS Valid */
	CPCAP_IRQ_CHRG_CURR1,		/* Charge Current Monitor (20mA) */
	CPCAP_IRQ_CHRG_CURR2,		/* Charge Current Monitor (250mA) */
	CPCAP_IRQ_RVRS_MODE,		/* Reverse Current Limit */
	CPCAP_IRQ_ON,			/* On Signal */
	CPCAP_IRQ_ON2,			/* On 2 Signal */
	CPCAP_IRQ_CLK,			/* 32k Clock Transition */
	CPCAP_IRQ_1HZ,			/* 1Hz Tick */
	CPCAP_IRQ_PTT,			/* Push To Talk */
	CPCAP_IRQ_SE0CONN,		/* SE0 Condition */
	CPCAP_IRQ_CHRG_SE1B,		/* CHRG_SE1B Pin */
	CPCAP_IRQ_UART_ECHO_OVERRUN,	/* UART Buffer Overflow */
	CPCAP_IRQ_EXTMEMHD,		/* External MEMHOLD */

	CPCAP_IRQ_WARM = CPCAP_IRQ_INT3_INDEX, /* Warm Start */
	CPCAP_IRQ_SYSRSTR,		/* System Restart */
	CPCAP_IRQ_SOFTRST,		/* Soft Reset */
	CPCAP_IRQ_DIEPWRDWN,		/* Die Temperature Powerdown */
	CPCAP_IRQ_DIETEMPH,		/* Die Temperature High */
	CPCAP_IRQ_PC,			/* Power Cut */
	CPCAP_IRQ_OFLOWSW,		/* Stopwatch Overflow */
	CPCAP_IRQ_TODA,			/* TOD Alarm */
	CPCAP_IRQ_OPT_SEL_DTCH,		/* Detach Detect */
	CPCAP_IRQ_OPT_SEL_STATE,	/* State Change */
	CPCAP_IRQ_ONEWIRE1,		/* Onewire 1 Block */
	CPCAP_IRQ_ONEWIRE2,		/* Onewire 2 Block */
	CPCAP_IRQ_ONEWIRE3,		/* Onewire 3 Block */
	CPCAP_IRQ_UCRESET,		/* Microcontroller Reset */
	CPCAP_IRQ_PWRGOOD,		/* BP Turn On */
	CPCAP_IRQ_USBDPLLCLK,		/* USB DPLL Status */

	CPCAP_IRQ_DPI = CPCAP_IRQ_INT4_INDEX, /* DP Line */
	CPCAP_IRQ_DMI,			/* DM Line */
	CPCAP_IRQ_UCBUSY,		/* Microcontroller Busy */
	CPCAP_IRQ_GCAI_CURR1,		/* Charge Current Monitor (65mA) */
	CPCAP_IRQ_GCAI_CURR2,		/* Charge Current Monitor (600mA) */
	CPCAP_IRQ_SB_MAX_RETRANSMIT_ERR,/* SLIMbus Retransmit Error */
	CPCAP_IRQ_BATTDETB,		/* Battery Presence Detected */
	CPCAP_IRQ_PRIHALT,		/* Primary Microcontroller Halt */
	CPCAP_IRQ_SECHALT,		/* Secondary Microcontroller Halt */
	CPCAP_IRQ_CC_CAL,		/* CC Calibration */

	CPCAP_IRQ_UC_PRIROMR = CPCAP_IRQ_INT5_INDEX, /* Prim ROM Rd Macro Int */
	CPCAP_IRQ_UC_PRIRAMW,		/* Primary RAM Write Macro Int */
	CPCAP_IRQ_UC_PRIRAMR,		/* Primary RAM Read Macro Int */
	CPCAP_IRQ_UC_USEROFF,		/* USEROFF Macro Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_4,	/* Primary Macro 4 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_5,	/* Primary Macro 5 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_6,	/* Primary Macro 6 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_7,	/* Primary Macro 7 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_8,	/* Primary Macro 8 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_9,	/* Primary Macro 9 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_10,	/* Primary Macro 10 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_11,	/* Primary Macro 11 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_12,	/* Primary Macro 12 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_13,	/* Primary Macro 13 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_14,	/* Primary Macro 14 Interrupt */
	CPCAP_IRQ_UC_PRIMACRO_15,	/* Primary Macro 15 Interrupt */
	CPCAP_IRQ__NUM			/* Number of allocated events */
};

enum cpcap_adc_bank0 {
	CPCAP_ADC_AD0_BATTDETB,
	CPCAP_ADC_BATTP,
	CPCAP_ADC_VBUS,
	CPCAP_ADC_AD3,
	CPCAP_ADC_BPLUS_AD4,
	CPCAP_ADC_CHG_ISENSE,
	CPCAP_ADC_BATTI_ADC,
	CPCAP_ADC_USB_ID,

	CPCAP_ADC_BANK0_NUM,
};

enum cpcap_adc_bank1 {
	CPCAP_ADC_AD8,
	CPCAP_ADC_AD9,
	CPCAP_ADC_LICELL,
	CPCAP_ADC_HV_BATTP,
	CPCAP_ADC_TSX1_AD12,
	CPCAP_ADC_TSX2_AD13,
	CPCAP_ADC_TSY1_AD14,
	CPCAP_ADC_TSY2_AD15,

	CPCAP_ADC_BANK1_NUM,
};

enum cpcap_adc_format {
	CPCAP_ADC_FORMAT_RAW,
	CPCAP_ADC_FORMAT_PHASED,
	CPCAP_ADC_FORMAT_CONVERTED,
};

enum cpcap_adc_timing {
	CPCAP_ADC_TIMING_IMM,
	CPCAP_ADC_TIMING_IN,
	CPCAP_ADC_TIMING_OUT,
};

enum cpcap_adc_type {
	CPCAP_ADC_TYPE_BANK_0,
	CPCAP_ADC_TYPE_BANK_1,
	CPCAP_ADC_TYPE_BATT_PI,
};

enum cpcap_macro {
	CPCAP_MACRO_ROMR,
	CPCAP_MACRO_RAMW,
	CPCAP_MACRO_RAMR,
	CPCAP_MACRO_USEROFF,
	CPCAP_MACRO_4,
	CPCAP_MACRO_5,
	CPCAP_MACRO_6,
	CPCAP_MACRO_7,
	CPCAP_MACRO_8,
	CPCAP_MACRO_9,
	CPCAP_MACRO_10,
	CPCAP_MACRO_11,
	CPCAP_MACRO_12,
	CPCAP_MACRO_13,
	CPCAP_MACRO_14,
	CPCAP_MACRO_15,

	CPCAP_MACRO__END,
};

enum cpcap_vendor {
	CPCAP_VENDOR_ST,
	CPCAP_VENDOR_TI,
};

enum cpcap_revision {
	CPCAP_REVISION_1_0 = 0x08,
	CPCAP_REVISION_1_1 = 0x09,
	CPCAP_REVISION_2_0 = 0x10,
	CPCAP_REVISION_2_1 = 0x11,
};

enum cpcap_batt_usb_model {
	CPCAP_BATT_USB_MODEL_NONE,
	CPCAP_BATT_USB_MODEL_USB,
	CPCAP_BATT_USB_MODEL_FACTORY,
};

struct cpcap_spi_init_data {
	enum cpcap_reg reg;
	unsigned short data;
};

struct cpcap_adc_ato {
	unsigned short ato_in;
	unsigned short atox_in;
	unsigned short adc_ps_factor_in;
	unsigned short atox_ps_factor_in;
	unsigned short ato_out;
	unsigned short atox_out;
	unsigned short adc_ps_factor_out;
	unsigned short atox_ps_factor_out;
};

struct cpcap_batt_data {
	int status;
	int health;
	int present;
	int capacity;
	int batt_volt;
	int batt_temp;
};

struct cpcap_batt_ac_data {
	int online;
};

struct cpcap_batt_usb_data {
	int online;
	int current_now;
	enum cpcap_batt_usb_model model;
};

#ifdef CONFIG_RTC_INTF_CPCAP_SECCLKD
struct cpcap_rtc_time_cnt {
	struct rtc_time time;
	unsigned short count;
};
#endif
struct cpcap_device;

#ifdef __KERNEL__
struct cpcap_platform_data {
	struct cpcap_spi_init_data *init;
	int init_len;
	unsigned short *regulator_mode_values;
	unsigned short *regulator_off_mode_values;
	struct regulator_init_data *regulator_init;
	struct cpcap_adc_ato *adc_ato;
	void (*ac_changed)(struct power_supply *,
			   struct cpcap_batt_ac_data *);
	void (*batt_changed)(struct power_supply *,
			     struct cpcap_batt_data *);
	void (*usb_changed)(struct power_supply *,
			    struct cpcap_batt_usb_data *);
	u16 hwcfg[CPCAP_HWCFG_NUM];
};

struct cpcap_whisper_pdata {
	unsigned int data_gpio;
	unsigned int pwr_gpio;
	unsigned char uartmux;
};

struct cpcap_adc_request {
	enum cpcap_adc_format format;
	enum cpcap_adc_timing timing;
	enum cpcap_adc_type type;
	int status;
	int result[CPCAP_ADC_BANK0_NUM];
	void (*callback)(struct cpcap_device *, void *);
	void *callback_param;

	/* Used in case of sync requests */
	struct completion completion;
};
#endif

struct cpcap_adc_us_request {
	enum cpcap_adc_format format;
	enum cpcap_adc_timing timing;
	enum cpcap_adc_type type;
	int status;
	int result[CPCAP_ADC_BANK0_NUM];
};

struct cpcap_adc_phase {
	signed char offset_batti;
	unsigned char slope_batti;
	signed char offset_chrgi;
	unsigned char slope_chrgi;
	signed char offset_battp;
	unsigned char slope_battp;
	signed char offset_bp;
	unsigned char slope_bp;
	signed char offset_battt;
	unsigned char slope_battt;
	signed char offset_chrgv;
	unsigned char slope_chrgv;
};

struct cpcap_regacc {
	unsigned short reg;
	unsigned short value;
	unsigned short mask;
};

struct cpcap_whisper_request {
	unsigned int cmd;
	char dock_id[CPCAP_WHISPER_ID_SIZE];
	char dock_prop[CPCAP_WHISPER_PROP_SIZE];
};

/*
 * Gets the contents of the specified cpcap register.
 *
 * INPUTS: The register number in the cpcap driver's format.
 *
 * OUTPUTS: The command writes the register data back to user space at the
 * location specified, or it may return an error code.
 */
#ifdef CONFIG_RTC_INTF_CPCAP_SECCLKD
#define CPCAP_IOCTL_GET_RTC_TIME_COUNTER \
	_IOR(0, CPCAP_IOCTL_NUM_RTC_COUNT, struct cpcap_rtc_time_cnt)
#endif

#define CPCAP_IOCTL_TEST_READ_REG \
	_IOWR(0, CPCAP_IOCTL_NUM_TEST_READ_REG, struct cpcap_regacc*)

/*
 * Writes the specifed cpcap register.
 *
 * This function writes the specified cpcap register with the specified
 * data.
 *
 * INPUTS: The register number in the cpcap driver's format and the data to
 * write to that register.
 *
 * OUTPUTS: The command has no output other than the returned error code for
 * the ioctl() call.
 */
#define CPCAP_IOCTL_TEST_WRITE_REG \
	_IOWR(0, CPCAP_IOCTL_NUM_TEST_WRITE_REG, struct cpcap_regacc*)

#define CPCAP_IOCTL_ADC_PHASE \
	_IOWR(0, CPCAP_IOCTL_NUM_ADC_PHASE, struct cpcap_adc_phase*)

#define CPCAP_IOCTL_BATT_DISPLAY_UPDATE \
	_IOW(0, CPCAP_IOCTL_NUM_BATT_DISPLAY_UPDATE, struct cpcap_batt_data*)

#define CPCAP_IOCTL_BATT_ATOD_ASYNC \
	_IOW(0, CPCAP_IOCTL_NUM_BATT_ATOD_ASYNC, struct cpcap_adc_us_request*)

#define CPCAP_IOCTL_BATT_ATOD_SYNC \
	_IOWR(0, CPCAP_IOCTL_NUM_BATT_ATOD_SYNC, struct cpcap_adc_us_request*)

#define CPCAP_IOCTL_BATT_ATOD_READ \
	_IOWR(0, CPCAP_IOCTL_NUM_BATT_ATOD_READ, struct cpcap_adc_us_request*)


#define CPCAP_IOCTL_UC_MACRO_START \
	_IOWR(0, CPCAP_IOCTL_NUM_UC_MACRO_START, enum cpcap_macro)

#define CPCAP_IOCTL_UC_MACRO_STOP \
	_IOWR(0, CPCAP_IOCTL_NUM_UC_MACRO_STOP, enum cpcap_macro)

#define CPCAP_IOCTL_UC_GET_VENDOR \
	_IOWR(0, CPCAP_IOCTL_NUM_UC_GET_VENDOR, enum cpcap_vendor)

#define CPCAP_IOCTL_UC_SET_TURBO_MODE \
	_IOW(0, CPCAP_IOCTL_NUM_UC_SET_TURBO_MODE, unsigned short)

#define CPCAP_IOCTL_ACCY_WHISPER \
	_IOW(0, CPCAP_IOCTL_NUM_ACCY_WHISPER, struct cpcap_whisper_request*)

#ifdef __KERNEL__
struct cpcap_device {
	struct spi_device	*spi;
	enum cpcap_vendor       vendor;
	enum cpcap_revision     revision;
	void			*keydata;
	struct platform_device  *regulator_pdev[CPCAP_NUM_REGULATORS];
	void			*irqdata;
	void			*adcdata;
	void			*battdata;
	void			*ucdata;
	void			*accydata;
	void			(*h2w_new_state)(int);
};

static inline void cpcap_set_keydata(struct cpcap_device *cpcap, void *data)
{
	cpcap->keydata = data;
}

static inline void *cpcap_get_keydata(struct cpcap_device *cpcap)
{
	return cpcap->keydata;
}

int cpcap_regacc_write(struct cpcap_device *cpcap, enum cpcap_reg reg,
		       unsigned short value, unsigned short mask);

int cpcap_regacc_read(struct cpcap_device *cpcap, enum cpcap_reg reg,
		      unsigned short *value_ptr);

int cpcap_regacc_init(struct cpcap_device *cpcap);

void cpcap_broadcast_key_event(struct cpcap_device *cpcap,
			       unsigned int code, int value);

int cpcap_irq_init(struct cpcap_device *cpcap);

void cpcap_irq_shutdown(struct cpcap_device *cpcap);

int cpcap_irq_register(struct cpcap_device *cpcap, enum cpcap_irqs irq,
		       void (*cb_func) (enum cpcap_irqs, void *), void *data);

int cpcap_irq_free(struct cpcap_device *cpcap, enum cpcap_irqs irq);

int cpcap_irq_get_data(struct cpcap_device *cpcap, enum cpcap_irqs irq,
		       void **data);

int cpcap_irq_clear(struct cpcap_device *cpcap, enum cpcap_irqs int_event);

int cpcap_irq_mask(struct cpcap_device *cpcap, enum cpcap_irqs int_event);

int cpcap_irq_unmask(struct cpcap_device *cpcap, enum cpcap_irqs int_event);

int cpcap_irq_mask_get(struct cpcap_device *cpcap, enum cpcap_irqs int_event);

int cpcap_irq_sense(struct cpcap_device *cpcap, enum cpcap_irqs int_event,
		    unsigned char clear);

#ifdef CONFIG_PM
int cpcap_irq_suspend(struct cpcap_device *cpcap);

int cpcap_irq_resume(struct cpcap_device *cpcap);
#endif

int cpcap_adc_sync_read(struct cpcap_device *cpcap,
			struct cpcap_adc_request *request);

int cpcap_adc_async_read(struct cpcap_device *cpcap,
			 struct cpcap_adc_request *request);

void cpcap_adc_phase(struct cpcap_device *cpcap, struct cpcap_adc_phase *phase);

void cpcap_batt_set_ac_prop(struct cpcap_device *cpcap, int online);

void cpcap_batt_set_usb_prop_online(struct cpcap_device *cpcap, int online,
				    enum cpcap_batt_usb_model model);

void cpcap_batt_set_usb_prop_curr(struct cpcap_device *cpcap,
				  unsigned int curr);

int cpcap_uc_start(struct cpcap_device *cpcap, enum cpcap_macro macro);

int cpcap_uc_stop(struct cpcap_device *cpcap, enum cpcap_macro macro);

unsigned char cpcap_uc_status(struct cpcap_device *cpcap,
			      enum cpcap_macro macro);

int cpcap_accy_whisper(struct cpcap_device *cpcap,
		       struct cpcap_whisper_request *req);

void cpcap_accy_whisper_spdif_set_state(int state);

#define  cpcap_driver_register platform_driver_register
#define  cpcap_driver_unregister platform_driver_unregister

int cpcap_device_register(struct platform_device *pdev);
int cpcap_device_unregister(struct platform_device *pdev);


#endif /* __KERNEL__ */
#endif /* _LINUX_SPI_CPCAP_H */
