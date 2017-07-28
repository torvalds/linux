/*
 * pmic.h  --  Power Management Driver for Wolfson WM8350 PMIC
 *
 * Copyright 2007 Wolfson Microelectronics PLC
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_WM8350_PMIC_H
#define __LINUX_MFD_WM8350_PMIC_H

#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/regulator/machine.h>

/*
 * Register values.
 */

#define WM8350_CURRENT_SINK_DRIVER_A            0xAC
#define WM8350_CSA_FLASH_CONTROL                0xAD
#define WM8350_CURRENT_SINK_DRIVER_B            0xAE
#define WM8350_CSB_FLASH_CONTROL                0xAF
#define WM8350_DCDC_LDO_REQUESTED               0xB0
#define WM8350_DCDC_ACTIVE_OPTIONS              0xB1
#define WM8350_DCDC_SLEEP_OPTIONS               0xB2
#define WM8350_POWER_CHECK_COMPARATOR           0xB3
#define WM8350_DCDC1_CONTROL                    0xB4
#define WM8350_DCDC1_TIMEOUTS                   0xB5
#define WM8350_DCDC1_LOW_POWER                  0xB6
#define WM8350_DCDC2_CONTROL                    0xB7
#define WM8350_DCDC2_TIMEOUTS                   0xB8
#define WM8350_DCDC3_CONTROL                    0xBA
#define WM8350_DCDC3_TIMEOUTS                   0xBB
#define WM8350_DCDC3_LOW_POWER                  0xBC
#define WM8350_DCDC4_CONTROL                    0xBD
#define WM8350_DCDC4_TIMEOUTS                   0xBE
#define WM8350_DCDC4_LOW_POWER                  0xBF
#define WM8350_DCDC5_CONTROL                    0xC0
#define WM8350_DCDC5_TIMEOUTS                   0xC1
#define WM8350_DCDC6_CONTROL                    0xC3
#define WM8350_DCDC6_TIMEOUTS                   0xC4
#define WM8350_DCDC6_LOW_POWER                  0xC5
#define WM8350_LIMIT_SWITCH_CONTROL             0xC7
#define WM8350_LDO1_CONTROL                     0xC8
#define WM8350_LDO1_TIMEOUTS                    0xC9
#define WM8350_LDO1_LOW_POWER                   0xCA
#define WM8350_LDO2_CONTROL                     0xCB
#define WM8350_LDO2_TIMEOUTS                    0xCC
#define WM8350_LDO2_LOW_POWER                   0xCD
#define WM8350_LDO3_CONTROL                     0xCE
#define WM8350_LDO3_TIMEOUTS                    0xCF
#define WM8350_LDO3_LOW_POWER                   0xD0
#define WM8350_LDO4_CONTROL                     0xD1
#define WM8350_LDO4_TIMEOUTS                    0xD2
#define WM8350_LDO4_LOW_POWER                   0xD3
#define WM8350_VCC_FAULT_MASKS                  0xD7
#define WM8350_MAIN_BANDGAP_CONTROL             0xD8
#define WM8350_OSC_CONTROL                      0xD9
#define WM8350_RTC_TICK_CONTROL                 0xDA
#define WM8350_SECURITY                         0xDB
#define WM8350_RAM_BIST_1                       0xDC
#define WM8350_DCDC_LDO_STATUS                  0xE1
#define WM8350_GPIO_PIN_STATUS                  0xE6

#define WM8350_DCDC1_FORCE_PWM                  0xF8
#define WM8350_DCDC3_FORCE_PWM                  0xFA
#define WM8350_DCDC4_FORCE_PWM                  0xFB
#define WM8350_DCDC6_FORCE_PWM                  0xFD

/*
 * R172 (0xAC) - Current Sink Driver A
 */
#define WM8350_CS1_HIB_MODE                     0x1000
#define WM8350_CS1_HIB_MODE_MASK                0x1000
#define WM8350_CS1_HIB_MODE_SHIFT                   12
#define WM8350_CS1_ISEL_MASK                    0x003F
#define WM8350_CS1_ISEL_SHIFT                        0

/* Bit values for R172 (0xAC) */
#define WM8350_CS1_HIB_MODE_DISABLE                  0
#define WM8350_CS1_HIB_MODE_LEAVE                    1

#define WM8350_CS1_ISEL_220M                      0x3F

/*
 * R173 (0xAD) - CSA Flash control
 */
#define WM8350_CS1_FLASH_MODE                   0x8000
#define WM8350_CS1_TRIGSRC                      0x4000
#define WM8350_CS1_DRIVE                        0x2000
#define WM8350_CS1_FLASH_DUR_MASK               0x0300
#define WM8350_CS1_OFF_RAMP_MASK                0x0030
#define WM8350_CS1_ON_RAMP_MASK                 0x0003

/*
 * R174 (0xAE) - Current Sink Driver B
 */
#define WM8350_CS2_HIB_MODE                     0x1000
#define WM8350_CS2_ISEL_MASK                    0x003F

/*
 * R175 (0xAF) - CSB Flash control
 */
#define WM8350_CS2_FLASH_MODE                   0x8000
#define WM8350_CS2_TRIGSRC                      0x4000
#define WM8350_CS2_DRIVE                        0x2000
#define WM8350_CS2_FLASH_DUR_MASK               0x0300
#define WM8350_CS2_OFF_RAMP_MASK                0x0030
#define WM8350_CS2_ON_RAMP_MASK                 0x0003

/*
 * R176 (0xB0) - DCDC/LDO requested
 */
#define WM8350_LS_ENA                           0x8000
#define WM8350_LDO4_ENA                         0x0800
#define WM8350_LDO3_ENA                         0x0400
#define WM8350_LDO2_ENA                         0x0200
#define WM8350_LDO1_ENA                         0x0100
#define WM8350_DC6_ENA                          0x0020
#define WM8350_DC5_ENA                          0x0010
#define WM8350_DC4_ENA                          0x0008
#define WM8350_DC3_ENA                          0x0004
#define WM8350_DC2_ENA                          0x0002
#define WM8350_DC1_ENA                          0x0001

/*
 * R177 (0xB1) - DCDC Active options
 */
#define WM8350_PUTO_MASK                        0x3000
#define WM8350_PWRUP_DELAY_MASK                 0x0300
#define WM8350_DC6_ACTIVE                       0x0020
#define WM8350_DC4_ACTIVE                       0x0008
#define WM8350_DC3_ACTIVE                       0x0004
#define WM8350_DC1_ACTIVE                       0x0001

/*
 * R178 (0xB2) - DCDC Sleep options
 */
#define WM8350_DC6_SLEEP                        0x0020
#define WM8350_DC4_SLEEP                        0x0008
#define WM8350_DC3_SLEEP                        0x0004
#define WM8350_DC1_SLEEP                        0x0001

/*
 * R179 (0xB3) - Power-check comparator
 */
#define WM8350_PCCMP_ERRACT                     0x4000
#define WM8350_PCCMP_RAIL                       0x0100
#define WM8350_PCCMP_OFF_THR_MASK               0x0070
#define WM8350_PCCMP_ON_THR_MASK                0x0007

/*
 * R180 (0xB4) - DCDC1 Control
 */
#define WM8350_DC1_OPFLT                        0x0400
#define WM8350_DC1_VSEL_MASK                    0x007F
#define WM8350_DC1_VSEL_SHIFT                        0

/*
 * R181 (0xB5) - DCDC1 Timeouts
 */
#define WM8350_DC1_ERRACT_MASK                  0xC000
#define WM8350_DC1_ERRACT_SHIFT                     14
#define WM8350_DC1_ENSLOT_MASK                  0x3C00
#define WM8350_DC1_ENSLOT_SHIFT                     10
#define WM8350_DC1_SDSLOT_MASK                  0x03C0
#define WM8350_DC1_UVTO_MASK                    0x0030
#define WM8350_DC1_SDSLOT_SHIFT                      6

/* Bit values for R181 (0xB5) */
#define WM8350_DC1_ERRACT_NONE                       0
#define WM8350_DC1_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC1_ERRACT_SHUTDOWN_SYS               2

/*
 * R182 (0xB6) - DCDC1 Low Power
 */
#define WM8350_DC1_HIB_MODE_MASK                0x7000
#define WM8350_DC1_HIB_TRIG_MASK                0x0300
#define WM8350_DC1_VIMG_MASK                    0x007F

/*
 * R183 (0xB7) - DCDC2 Control
 */
#define WM8350_DC2_MODE                         0x4000
#define WM8350_DC2_MODE_MASK                    0x4000
#define WM8350_DC2_MODE_SHIFT                       14
#define WM8350_DC2_HIB_MODE                     0x1000
#define WM8350_DC2_HIB_MODE_MASK                0x1000
#define WM8350_DC2_HIB_MODE_SHIFT                   12
#define WM8350_DC2_HIB_TRIG_MASK                0x0300
#define WM8350_DC2_HIB_TRIG_SHIFT                    8
#define WM8350_DC2_ILIM                         0x0040
#define WM8350_DC2_ILIM_MASK                    0x0040
#define WM8350_DC2_ILIM_SHIFT                        6
#define WM8350_DC2_RMP_MASK                     0x0018
#define WM8350_DC2_RMP_SHIFT                         3
#define WM8350_DC2_FBSRC_MASK                   0x0003
#define WM8350_DC2_FBSRC_SHIFT                       0

/* Bit values for R183 (0xB7) */
#define WM8350_DC2_MODE_BOOST                        0
#define WM8350_DC2_MODE_SWITCH                       1

#define WM8350_DC2_HIB_MODE_ACTIVE                   1
#define WM8350_DC2_HIB_MODE_DISABLE                  0

#define WM8350_DC2_HIB_TRIG_NONE                     0
#define WM8350_DC2_HIB_TRIG_LPWR1                    1
#define WM8350_DC2_HIB_TRIG_LPWR2                    2
#define WM8350_DC2_HIB_TRIG_LPWR3                    3

#define WM8350_DC2_ILIM_HIGH                         0
#define WM8350_DC2_ILIM_LOW                          1

#define WM8350_DC2_RMP_30V                           0
#define WM8350_DC2_RMP_20V                           1
#define WM8350_DC2_RMP_10V                           2
#define WM8350_DC2_RMP_5V                            3

#define WM8350_DC2_FBSRC_FB2                         0
#define WM8350_DC2_FBSRC_ISINKA                      1
#define WM8350_DC2_FBSRC_ISINKB                      2
#define WM8350_DC2_FBSRC_USB                         3

/*
 * R184 (0xB8) - DCDC2 Timeouts
 */
#define WM8350_DC2_ERRACT_MASK                  0xC000
#define WM8350_DC2_ERRACT_SHIFT                     14
#define WM8350_DC2_ENSLOT_MASK                  0x3C00
#define WM8350_DC2_ENSLOT_SHIFT                     10
#define WM8350_DC2_SDSLOT_MASK                  0x03C0
#define WM8350_DC2_UVTO_MASK                    0x0030

/* Bit values for R184 (0xB8) */
#define WM8350_DC2_ERRACT_NONE                       0
#define WM8350_DC2_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC2_ERRACT_SHUTDOWN_SYS               2

/*
 * R186 (0xBA) - DCDC3 Control
 */
#define WM8350_DC3_OPFLT                        0x0400
#define WM8350_DC3_VSEL_MASK                    0x007F
#define WM8350_DC3_VSEL_SHIFT                        0

/*
 * R187 (0xBB) - DCDC3 Timeouts
 */
#define WM8350_DC3_ERRACT_MASK                  0xC000
#define WM8350_DC3_ERRACT_SHIFT                     14
#define WM8350_DC3_ENSLOT_MASK                  0x3C00
#define WM8350_DC3_ENSLOT_SHIFT                     10
#define WM8350_DC3_SDSLOT_MASK                  0x03C0
#define WM8350_DC3_UVTO_MASK                    0x0030
#define WM8350_DC3_SDSLOT_SHIFT                      6

/* Bit values for R187 (0xBB) */
#define WM8350_DC3_ERRACT_NONE                       0
#define WM8350_DC3_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC3_ERRACT_SHUTDOWN_SYS               2
/*
 * R188 (0xBC) - DCDC3 Low Power
 */
#define WM8350_DC3_HIB_MODE_MASK                0x7000
#define WM8350_DC3_HIB_TRIG_MASK                0x0300
#define WM8350_DC3_VIMG_MASK                    0x007F

/*
 * R189 (0xBD) - DCDC4 Control
 */
#define WM8350_DC4_OPFLT                        0x0400
#define WM8350_DC4_VSEL_MASK                    0x007F
#define WM8350_DC4_VSEL_SHIFT                        0

/*
 * R190 (0xBE) - DCDC4 Timeouts
 */
#define WM8350_DC4_ERRACT_MASK                  0xC000
#define WM8350_DC4_ERRACT_SHIFT                     14
#define WM8350_DC4_ENSLOT_MASK                  0x3C00
#define WM8350_DC4_ENSLOT_SHIFT                     10
#define WM8350_DC4_SDSLOT_MASK                  0x03C0
#define WM8350_DC4_UVTO_MASK                    0x0030
#define WM8350_DC4_SDSLOT_SHIFT                      6

/* Bit values for R190 (0xBE) */
#define WM8350_DC4_ERRACT_NONE                       0
#define WM8350_DC4_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC4_ERRACT_SHUTDOWN_SYS               2

/*
 * R191 (0xBF) - DCDC4 Low Power
 */
#define WM8350_DC4_HIB_MODE_MASK                0x7000
#define WM8350_DC4_HIB_TRIG_MASK                0x0300
#define WM8350_DC4_VIMG_MASK                    0x007F

/*
 * R192 (0xC0) - DCDC5 Control
 */
#define WM8350_DC5_MODE                         0x4000
#define WM8350_DC5_MODE_MASK                    0x4000
#define WM8350_DC5_MODE_SHIFT                       14
#define WM8350_DC5_HIB_MODE                     0x1000
#define WM8350_DC5_HIB_MODE_MASK                0x1000
#define WM8350_DC5_HIB_MODE_SHIFT                   12
#define WM8350_DC5_HIB_TRIG_MASK                0x0300
#define WM8350_DC5_HIB_TRIG_SHIFT                    8
#define WM8350_DC5_ILIM                         0x0040
#define WM8350_DC5_ILIM_MASK                    0x0040
#define WM8350_DC5_ILIM_SHIFT                        6
#define WM8350_DC5_RMP_MASK                     0x0018
#define WM8350_DC5_RMP_SHIFT                         3
#define WM8350_DC5_FBSRC_MASK                   0x0003
#define WM8350_DC5_FBSRC_SHIFT                       0

/* Bit values for R192 (0xC0) */
#define WM8350_DC5_MODE_BOOST                        0
#define WM8350_DC5_MODE_SWITCH                       1

#define WM8350_DC5_HIB_MODE_ACTIVE                   1
#define WM8350_DC5_HIB_MODE_DISABLE                  0

#define WM8350_DC5_HIB_TRIG_NONE                     0
#define WM8350_DC5_HIB_TRIG_LPWR1                    1
#define WM8350_DC5_HIB_TRIG_LPWR2                    2
#define WM8350_DC5_HIB_TRIG_LPWR3                    3

#define WM8350_DC5_ILIM_HIGH                         0
#define WM8350_DC5_ILIM_LOW                          1

#define WM8350_DC5_RMP_30V                           0
#define WM8350_DC5_RMP_20V                           1
#define WM8350_DC5_RMP_10V                           2
#define WM8350_DC5_RMP_5V                            3

#define WM8350_DC5_FBSRC_FB2                         0
#define WM8350_DC5_FBSRC_ISINKA                      1
#define WM8350_DC5_FBSRC_ISINKB                      2
#define WM8350_DC5_FBSRC_USB                         3

/*
 * R193 (0xC1) - DCDC5 Timeouts
 */
#define WM8350_DC5_ERRACT_MASK                  0xC000
#define WM8350_DC5_ERRACT_SHIFT                     14
#define WM8350_DC5_ENSLOT_MASK                  0x3C00
#define WM8350_DC5_ENSLOT_SHIFT                     10
#define WM8350_DC5_SDSLOT_MASK                  0x03C0
#define WM8350_DC5_UVTO_MASK                    0x0030
#define WM8350_DC5_SDSLOT_SHIFT                      6

/* Bit values for R193 (0xC1) */
#define WM8350_DC5_ERRACT_NONE                       0
#define WM8350_DC5_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC5_ERRACT_SHUTDOWN_SYS               2

/*
 * R195 (0xC3) - DCDC6 Control
 */
#define WM8350_DC6_OPFLT                        0x0400
#define WM8350_DC6_VSEL_MASK                    0x007F
#define WM8350_DC6_VSEL_SHIFT                        0

/*
 * R196 (0xC4) - DCDC6 Timeouts
 */
#define WM8350_DC6_ERRACT_MASK                  0xC000
#define WM8350_DC6_ERRACT_SHIFT                     14
#define WM8350_DC6_ENSLOT_MASK                  0x3C00
#define WM8350_DC6_ENSLOT_SHIFT                     10
#define WM8350_DC6_SDSLOT_MASK                  0x03C0
#define WM8350_DC6_UVTO_MASK                    0x0030
#define WM8350_DC6_SDSLOT_SHIFT                      6

/* Bit values for R196 (0xC4) */
#define WM8350_DC6_ERRACT_NONE                       0
#define WM8350_DC6_ERRACT_SHUTDOWN_CONV              1
#define WM8350_DC6_ERRACT_SHUTDOWN_SYS               2

/*
 * R197 (0xC5) - DCDC6 Low Power
 */
#define WM8350_DC6_HIB_MODE_MASK                0x7000
#define WM8350_DC6_HIB_TRIG_MASK                0x0300
#define WM8350_DC6_VIMG_MASK                    0x007F

/*
 * R199 (0xC7) - Limit Switch Control
 */
#define WM8350_LS_ERRACT_MASK                   0xC000
#define WM8350_LS_ERRACT_SHIFT                      14
#define WM8350_LS_ENSLOT_MASK                   0x3C00
#define WM8350_LS_ENSLOT_SHIFT                      10
#define WM8350_LS_SDSLOT_MASK                   0x03C0
#define WM8350_LS_SDSLOT_SHIFT                       6
#define WM8350_LS_HIB_MODE                      0x0010
#define WM8350_LS_HIB_MODE_MASK                 0x0010
#define WM8350_LS_HIB_MODE_SHIFT                     4
#define WM8350_LS_HIB_PROT                      0x0002
#define WM8350_LS_HIB_PROT_MASK                 0x0002
#define WM8350_LS_HIB_PROT_SHIFT                     1
#define WM8350_LS_PROT                          0x0001
#define WM8350_LS_PROT_MASK                     0x0001
#define WM8350_LS_PROT_SHIFT                         0

/* Bit values for R199 (0xC7) */
#define WM8350_LS_ERRACT_NONE                       0
#define WM8350_LS_ERRACT_SHUTDOWN_CONV              1
#define WM8350_LS_ERRACT_SHUTDOWN_SYS               2

/*
 * R200 (0xC8) - LDO1 Control
 */
#define WM8350_LDO1_SWI                         0x4000
#define WM8350_LDO1_OPFLT                       0x0400
#define WM8350_LDO1_VSEL_MASK                   0x001F
#define WM8350_LDO1_VSEL_SHIFT                       0

/*
 * R201 (0xC9) - LDO1 Timeouts
 */
#define WM8350_LDO1_ERRACT_MASK                 0xC000
#define WM8350_LDO1_ERRACT_SHIFT                    14
#define WM8350_LDO1_ENSLOT_MASK                 0x3C00
#define WM8350_LDO1_ENSLOT_SHIFT                    10
#define WM8350_LDO1_SDSLOT_MASK                 0x03C0
#define WM8350_LDO1_UVTO_MASK                   0x0030
#define WM8350_LDO1_SDSLOT_SHIFT                     6

/* Bit values for R201 (0xC9) */
#define WM8350_LDO1_ERRACT_NONE                       0
#define WM8350_LDO1_ERRACT_SHUTDOWN_CONV              1
#define WM8350_LDO1_ERRACT_SHUTDOWN_SYS               2

/*
 * R202 (0xCA) - LDO1 Low Power
 */
#define WM8350_LDO1_HIB_MODE_MASK               0x3000
#define WM8350_LDO1_HIB_TRIG_MASK               0x0300
#define WM8350_LDO1_VIMG_MASK                   0x001F
#define WM8350_LDO1_HIB_MODE_DIS		(0x1 << 12)


/*
 * R203 (0xCB) - LDO2 Control
 */
#define WM8350_LDO2_SWI                         0x4000
#define WM8350_LDO2_OPFLT                       0x0400
#define WM8350_LDO2_VSEL_MASK                   0x001F
#define WM8350_LDO2_VSEL_SHIFT                       0

/*
 * R204 (0xCC) - LDO2 Timeouts
 */
#define WM8350_LDO2_ERRACT_MASK                 0xC000
#define WM8350_LDO2_ERRACT_SHIFT                    14
#define WM8350_LDO2_ENSLOT_MASK                 0x3C00
#define WM8350_LDO2_ENSLOT_SHIFT                    10
#define WM8350_LDO2_SDSLOT_MASK                 0x03C0
#define WM8350_LDO2_SDSLOT_SHIFT                     6

/* Bit values for R204 (0xCC) */
#define WM8350_LDO2_ERRACT_NONE                       0
#define WM8350_LDO2_ERRACT_SHUTDOWN_CONV              1
#define WM8350_LDO2_ERRACT_SHUTDOWN_SYS               2

/*
 * R205 (0xCD) - LDO2 Low Power
 */
#define WM8350_LDO2_HIB_MODE_MASK               0x3000
#define WM8350_LDO2_HIB_TRIG_MASK               0x0300
#define WM8350_LDO2_VIMG_MASK                   0x001F

/*
 * R206 (0xCE) - LDO3 Control
 */
#define WM8350_LDO3_SWI                         0x4000
#define WM8350_LDO3_OPFLT                       0x0400
#define WM8350_LDO3_VSEL_MASK                   0x001F
#define WM8350_LDO3_VSEL_SHIFT                       0

/*
 * R207 (0xCF) - LDO3 Timeouts
 */
#define WM8350_LDO3_ERRACT_MASK                 0xC000
#define WM8350_LDO3_ERRACT_SHIFT                    14
#define WM8350_LDO3_ENSLOT_MASK                 0x3C00
#define WM8350_LDO3_ENSLOT_SHIFT                    10
#define WM8350_LDO3_SDSLOT_MASK                 0x03C0
#define WM8350_LDO3_UVTO_MASK                   0x0030
#define WM8350_LDO3_SDSLOT_SHIFT                     6

/* Bit values for R207 (0xCF) */
#define WM8350_LDO3_ERRACT_NONE                       0
#define WM8350_LDO3_ERRACT_SHUTDOWN_CONV              1
#define WM8350_LDO3_ERRACT_SHUTDOWN_SYS               2

/*
 * R208 (0xD0) - LDO3 Low Power
 */
#define WM8350_LDO3_HIB_MODE_MASK               0x3000
#define WM8350_LDO3_HIB_TRIG_MASK               0x0300
#define WM8350_LDO3_VIMG_MASK                   0x001F

/*
 * R209 (0xD1) - LDO4 Control
 */
#define WM8350_LDO4_SWI                         0x4000
#define WM8350_LDO4_OPFLT                       0x0400
#define WM8350_LDO4_VSEL_MASK                   0x001F
#define WM8350_LDO4_VSEL_SHIFT                       0

/*
 * R210 (0xD2) - LDO4 Timeouts
 */
#define WM8350_LDO4_ERRACT_MASK                 0xC000
#define WM8350_LDO4_ERRACT_SHIFT                    14
#define WM8350_LDO4_ENSLOT_MASK                 0x3C00
#define WM8350_LDO4_ENSLOT_SHIFT                    10
#define WM8350_LDO4_SDSLOT_MASK                 0x03C0
#define WM8350_LDO4_UVTO_MASK                   0x0030
#define WM8350_LDO4_SDSLOT_SHIFT                     6

/* Bit values for R210 (0xD2) */
#define WM8350_LDO4_ERRACT_NONE                       0
#define WM8350_LDO4_ERRACT_SHUTDOWN_CONV              1
#define WM8350_LDO4_ERRACT_SHUTDOWN_SYS               2

/*
 * R211 (0xD3) - LDO4 Low Power
 */
#define WM8350_LDO4_HIB_MODE_MASK               0x3000
#define WM8350_LDO4_HIB_TRIG_MASK               0x0300
#define WM8350_LDO4_VIMG_MASK                   0x001F

/*
 * R215 (0xD7) - VCC_FAULT Masks
 */
#define WM8350_LS_FAULT                         0x8000
#define WM8350_LDO4_FAULT                       0x0800
#define WM8350_LDO3_FAULT                       0x0400
#define WM8350_LDO2_FAULT                       0x0200
#define WM8350_LDO1_FAULT                       0x0100
#define WM8350_DC6_FAULT                        0x0020
#define WM8350_DC5_FAULT                        0x0010
#define WM8350_DC4_FAULT                        0x0008
#define WM8350_DC3_FAULT                        0x0004
#define WM8350_DC2_FAULT                        0x0002
#define WM8350_DC1_FAULT                        0x0001

/*
 * R216 (0xD8) - Main Bandgap Control
 */
#define WM8350_MBG_LOAD_FUSES                   0x8000
#define WM8350_MBG_FUSE_WPREP                   0x4000
#define WM8350_MBG_FUSE_WRITE                   0x2000
#define WM8350_MBG_FUSE_TRIM_MASK               0x1F00
#define WM8350_MBG_TRIM_SRC                     0x0020
#define WM8350_MBG_USER_TRIM_MASK               0x001F

/*
 * R217 (0xD9) - OSC Control
 */
#define WM8350_OSC_LOAD_FUSES                   0x8000
#define WM8350_OSC_FUSE_WPREP                   0x4000
#define WM8350_OSC_FUSE_WRITE                   0x2000
#define WM8350_OSC_FUSE_TRIM_MASK               0x0F00
#define WM8350_OSC_TRIM_SRC                     0x0020
#define WM8350_OSC_USER_TRIM_MASK               0x000F

/*
 * R248 (0xF8) - DCDC1 Force PWM
 */
#define WM8350_DCDC1_FORCE_PWM_ENA              0x0010

/*
 * R250 (0xFA) - DCDC3 Force PWM
 */
#define WM8350_DCDC3_FORCE_PWM_ENA              0x0010

/*
 * R251 (0xFB) - DCDC4 Force PWM
 */
#define WM8350_DCDC4_FORCE_PWM_ENA              0x0010

/*
 * R253 (0xFD) - DCDC1 Force PWM
 */
#define WM8350_DCDC6_FORCE_PWM_ENA              0x0010

/*
 * DCDC's
 */
#define WM8350_DCDC_1				0
#define WM8350_DCDC_2				1
#define WM8350_DCDC_3				2
#define WM8350_DCDC_4				3
#define WM8350_DCDC_5				4
#define WM8350_DCDC_6				5

/* DCDC modes */
#define WM8350_DCDC_ACTIVE_STANDBY		0
#define WM8350_DCDC_ACTIVE_PULSE		1
#define WM8350_DCDC_SLEEP_NORMAL		0
#define WM8350_DCDC_SLEEP_LOW			1

/* DCDC Low power (Hibernate) mode */
#define WM8350_DCDC_HIB_MODE_CUR		(0 << 12)
#define WM8350_DCDC_HIB_MODE_IMAGE		(1 << 12)
#define WM8350_DCDC_HIB_MODE_STANDBY		(2 << 12)
#define WM8350_DCDC_HIB_MODE_LDO		(4 << 12)
#define WM8350_DCDC_HIB_MODE_LDO_IM		(5 << 12)
#define WM8350_DCDC_HIB_MODE_DIS		(7 << 12)
#define WM8350_DCDC_HIB_MODE_MASK		(7 << 12)

/* DCDC Low Power (Hibernate) signal */
#define WM8350_DCDC_HIB_SIG_REG			(0 << 8)
#define WM8350_DCDC_HIB_SIG_LPWR1		(1 << 8)
#define WM8350_DCDC_HIB_SIG_LPWR2		(2 << 8)
#define WM8350_DCDC_HIB_SIG_LPWR3		(3 << 8)

/* LDO Low power (Hibernate) mode */
#define WM8350_LDO_HIB_MODE_IMAGE		(0 << 0)
#define WM8350_LDO_HIB_MODE_DIS			(1 << 0)

/* LDO Low Power (Hibernate) signal */
#define WM8350_LDO_HIB_SIG_REG			(0 << 8)
#define WM8350_LDO_HIB_SIG_LPWR1		(1 << 8)
#define WM8350_LDO_HIB_SIG_LPWR2		(2 << 8)
#define WM8350_LDO_HIB_SIG_LPWR3		(3 << 8)

/*
 * LDOs
 */
#define WM8350_LDO_1				6
#define WM8350_LDO_2				7
#define WM8350_LDO_3				8
#define WM8350_LDO_4				9

/*
 * ISINKs
 */
#define WM8350_ISINK_A				10
#define WM8350_ISINK_B				11

#define WM8350_ISINK_MODE_BOOST			0
#define WM8350_ISINK_MODE_SWITCH		1
#define WM8350_ISINK_ILIM_NORMAL		0
#define WM8350_ISINK_ILIM_LOW			1

#define WM8350_ISINK_FLASH_DISABLE		0
#define WM8350_ISINK_FLASH_ENABLE		1
#define WM8350_ISINK_FLASH_TRIG_BIT		0
#define WM8350_ISINK_FLASH_TRIG_GPIO		1
#define WM8350_ISINK_FLASH_MODE_EN		(1 << 13)
#define WM8350_ISINK_FLASH_MODE_DIS		(0 << 13)
#define WM8350_ISINK_FLASH_DUR_32MS		(0 << 8)
#define WM8350_ISINK_FLASH_DUR_64MS		(1 << 8)
#define WM8350_ISINK_FLASH_DUR_96MS		(2 << 8)
#define WM8350_ISINK_FLASH_DUR_1024MS		(3 << 8)
#define WM8350_ISINK_FLASH_ON_INSTANT		(0 << 0)
#define WM8350_ISINK_FLASH_ON_0_25S		(1 << 0)
#define WM8350_ISINK_FLASH_ON_0_50S		(2 << 0)
#define WM8350_ISINK_FLASH_ON_1_00S		(3 << 0)
#define WM8350_ISINK_FLASH_ON_1_95S		(1 << 0)
#define WM8350_ISINK_FLASH_ON_3_91S		(2 << 0)
#define WM8350_ISINK_FLASH_ON_7_80S		(3 << 0)
#define WM8350_ISINK_FLASH_OFF_INSTANT		(0 << 4)
#define WM8350_ISINK_FLASH_OFF_0_25S		(1 << 4)
#define WM8350_ISINK_FLASH_OFF_0_50S		(2 << 4)
#define WM8350_ISINK_FLASH_OFF_1_00S		(3 << 4)
#define WM8350_ISINK_FLASH_OFF_1_95S		(1 << 4)
#define WM8350_ISINK_FLASH_OFF_3_91S		(2 << 4)
#define WM8350_ISINK_FLASH_OFF_7_80S		(3 << 4)

/*
 * Regulator Interrupts.
 */
#define WM8350_IRQ_CS1				13
#define WM8350_IRQ_CS2				14
#define WM8350_IRQ_UV_LDO4			25
#define WM8350_IRQ_UV_LDO3			26
#define WM8350_IRQ_UV_LDO2			27
#define WM8350_IRQ_UV_LDO1			28
#define WM8350_IRQ_UV_DC6			29
#define WM8350_IRQ_UV_DC5			30
#define WM8350_IRQ_UV_DC4			31
#define WM8350_IRQ_UV_DC3			32
#define WM8350_IRQ_UV_DC2			33
#define WM8350_IRQ_UV_DC1			34
#define WM8350_IRQ_OC_LS			35

#define NUM_WM8350_REGULATORS			12

struct wm8350;
struct platform_device;
struct regulator_init_data;

/*
 * WM8350 LED platform data
 */
struct wm8350_led_platform_data {
	const char *name;
	const char *default_trigger;
	int max_uA;
};

struct wm8350_led {
	struct platform_device *pdev;
	struct work_struct work;
	spinlock_t value_lock;
	enum led_brightness value;
	struct led_classdev cdev;
	int max_uA_index;
	int enabled;

	struct regulator *isink;
	struct regulator_consumer_supply isink_consumer;
	struct regulator_init_data isink_init;
	struct regulator *dcdc;
	struct regulator_consumer_supply dcdc_consumer;
	struct regulator_init_data dcdc_init;
};

struct wm8350_pmic {
	/* Number of regulators of each type on this device */
	int max_dcdc;
	int max_isink;

	/* ISINK to DCDC mapping */
	int isink_A_dcdc;
	int isink_B_dcdc;

	/* hibernate configs */
	u16 dcdc1_hib_mode;
	u16 dcdc3_hib_mode;
	u16 dcdc4_hib_mode;
	u16 dcdc6_hib_mode;

	/* regulator devices */
	struct platform_device *pdev[NUM_WM8350_REGULATORS];

	/* LED devices */
	struct wm8350_led led[2];
};

int wm8350_register_regulator(struct wm8350 *wm8350, int reg,
			      struct regulator_init_data *initdata);
int wm8350_register_led(struct wm8350 *wm8350, int lednum, int dcdc, int isink,
			struct wm8350_led_platform_data *pdata);

/*
 * Additional DCDC control not supported via regulator API
 */
int wm8350_dcdc_set_slot(struct wm8350 *wm8350, int dcdc, u16 start,
			 u16 stop, u16 fault);
int wm8350_dcdc25_set_mode(struct wm8350 *wm8350, int dcdc, u16 mode,
			   u16 ilim, u16 ramp, u16 feedback);

/*
 * Additional LDO control not supported via regulator API
 */
int wm8350_ldo_set_slot(struct wm8350 *wm8350, int ldo, u16 start, u16 stop);

/*
 * Additional ISINK control not supported via regulator API
 */
int wm8350_isink_set_flash(struct wm8350 *wm8350, int isink, u16 mode,
			   u16 trigger, u16 duration, u16 on_ramp,
			   u16 off_ramp, u16 drive);

#endif
