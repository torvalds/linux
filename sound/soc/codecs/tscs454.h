// SPDX-License-Identifier: GPL-2.0
// tscs454.h -- TSCS454 ALSA SoC Audio driver
// Copyright 2018 Tempo Semiconductor, Inc.
// Author: Steven Eckhoff <steven.eckhoff.opensource@gmail.com>

#ifndef __REDWOODPUBLIC_H__
#define __REDWOODPUBLIC_H__

#define VIRT_BASE 0x00
#define PAGE_LEN 0x100
#define VIRT_PAGE_BASE(page) (VIRT_BASE + (PAGE_LEN * page))
#define VIRT_ADDR(page, address) (VIRT_PAGE_BASE(page) + address)
#define ADDR(page, virt_address) (virt_address - VIRT_PAGE_BASE(page))

#define R_PAGESEL                       0x0
#define R_RESET                         VIRT_ADDR(0x0, 0x1)
#define R_IRQEN                         VIRT_ADDR(0x0, 0x2)
#define R_IRQMASK                       VIRT_ADDR(0x0, 0x3)
#define R_IRQSTAT                       VIRT_ADDR(0x0, 0x4)
#define R_DEVADD0                       VIRT_ADDR(0x0, 0x6)
#define R_DEVID                         VIRT_ADDR(0x0, 0x8)
#define R_DEVREV                        VIRT_ADDR(0x0, 0x9)
#define R_PLLSTAT                       VIRT_ADDR(0x0, 0x0A)
#define R_PLL1CTL                       VIRT_ADDR(0x0, 0x0B)
#define R_PLL1RDIV                      VIRT_ADDR(0x0, 0x0C)
#define R_PLL1ODIV                      VIRT_ADDR(0x0, 0x0D)
#define R_PLL1FDIVL                     VIRT_ADDR(0x0, 0x0E)
#define R_PLL1FDIVH                     VIRT_ADDR(0x0, 0x0F)
#define R_PLL2CTL                       VIRT_ADDR(0x0, 0x10)
#define R_PLL2RDIV                      VIRT_ADDR(0x0, 0x11)
#define R_PLL2ODIV                      VIRT_ADDR(0x0, 0x12)
#define R_PLL2FDIVL                     VIRT_ADDR(0x0, 0x13)
#define R_PLL2FDIVH                     VIRT_ADDR(0x0, 0x14)
#define R_PLLCTL                        VIRT_ADDR(0x0, 0x15)
#define R_ISRC                          VIRT_ADDR(0x0, 0x16)
#define R_SCLKCTL                       VIRT_ADDR(0x0, 0x18)
#define R_TIMEBASE                      VIRT_ADDR(0x0, 0x19)
#define R_I2SP1CTL                      VIRT_ADDR(0x0, 0x1A)
#define R_I2SP2CTL                      VIRT_ADDR(0x0, 0x1B)
#define R_I2SP3CTL                      VIRT_ADDR(0x0, 0x1C)
#define R_I2S1MRATE                     VIRT_ADDR(0x0, 0x1D)
#define R_I2S2MRATE                     VIRT_ADDR(0x0, 0x1E)
#define R_I2S3MRATE                     VIRT_ADDR(0x0, 0x1F)
#define R_I2SCMC                        VIRT_ADDR(0x0, 0x20)
#define R_MCLK2PINC                     VIRT_ADDR(0x0, 0x21)
#define R_I2SPINC0                      VIRT_ADDR(0x0, 0x22)
#define R_I2SPINC1                      VIRT_ADDR(0x0, 0x23)
#define R_I2SPINC2                      VIRT_ADDR(0x0, 0x24)
#define R_GPIOCTL0                      VIRT_ADDR(0x0, 0x25)
#define R_GPIOCTL1                      VIRT_ADDR(0x0, 0x26)
#define R_ASRC                          VIRT_ADDR(0x0, 0x28)
#define R_TDMCTL0                       VIRT_ADDR(0x0, 0x2D)
#define R_TDMCTL1                       VIRT_ADDR(0x0, 0x2E)
#define R_PCMP2CTL0                     VIRT_ADDR(0x0, 0x2F)
#define R_PCMP2CTL1                     VIRT_ADDR(0x0, 0x30)
#define R_PCMP3CTL0                     VIRT_ADDR(0x0, 0x31)
#define R_PCMP3CTL1                     VIRT_ADDR(0x0, 0x32)
#define R_PWRM0                         VIRT_ADDR(0x0, 0x33)
#define R_PWRM1                         VIRT_ADDR(0x0, 0x34)
#define R_PWRM2                         VIRT_ADDR(0x0, 0x35)
#define R_PWRM3                         VIRT_ADDR(0x0, 0x36)
#define R_PWRM4                         VIRT_ADDR(0x0, 0x37)
#define R_I2SIDCTL                      VIRT_ADDR(0x0, 0x38)
#define R_I2SODCTL                      VIRT_ADDR(0x0, 0x39)
#define R_AUDIOMUX1                     VIRT_ADDR(0x0, 0x3A)
#define R_AUDIOMUX2                     VIRT_ADDR(0x0, 0x3B)
#define R_AUDIOMUX3                     VIRT_ADDR(0x0, 0x3C)
#define R_HSDCTL1                       VIRT_ADDR(0x1, 0x1)
#define R_HSDCTL2                       VIRT_ADDR(0x1, 0x2)
#define R_HSDSTAT                       VIRT_ADDR(0x1, 0x3)
#define R_HSDDELAY                      VIRT_ADDR(0x1, 0x4)
#define R_BUTCTL                        VIRT_ADDR(0x1, 0x5)
#define R_CH0AIC                        VIRT_ADDR(0x1, 0x6)
#define R_CH1AIC                        VIRT_ADDR(0x1, 0x7)
#define R_CH2AIC                        VIRT_ADDR(0x1, 0x8)
#define R_CH3AIC                        VIRT_ADDR(0x1, 0x9)
#define R_ICTL0                         VIRT_ADDR(0x1, 0x0A)
#define R_ICTL1                         VIRT_ADDR(0x1, 0x0B)
#define R_MICBIAS                       VIRT_ADDR(0x1, 0x0C)
#define R_PGACTL0                       VIRT_ADDR(0x1, 0x0D)
#define R_PGACTL1                       VIRT_ADDR(0x1, 0x0E)
#define R_PGACTL2                       VIRT_ADDR(0x1, 0x0F)
#define R_PGACTL3                       VIRT_ADDR(0x1, 0x10)
#define R_PGAZ                          VIRT_ADDR(0x1, 0x11)
#define R_ICH0VOL                       VIRT_ADDR(0x1, 0x12)
#define R_ICH1VOL                       VIRT_ADDR(0x1, 0x13)
#define R_ICH2VOL                       VIRT_ADDR(0x1, 0x14)
#define R_ICH3VOL                       VIRT_ADDR(0x1, 0x15)
#define R_ASRCILVOL                     VIRT_ADDR(0x1, 0x16)
#define R_ASRCIRVOL                     VIRT_ADDR(0x1, 0x17)
#define R_ASRCOLVOL                     VIRT_ADDR(0x1, 0x18)
#define R_ASRCORVOL                     VIRT_ADDR(0x1, 0x19)
#define R_IVOLCTLU                      VIRT_ADDR(0x1, 0x1C)
#define R_ALCCTL0                       VIRT_ADDR(0x1, 0x1D)
#define R_ALCCTL1                       VIRT_ADDR(0x1, 0x1E)
#define R_ALCCTL2                       VIRT_ADDR(0x1, 0x1F)
#define R_ALCCTL3                       VIRT_ADDR(0x1, 0x20)
#define R_NGATE                         VIRT_ADDR(0x1, 0x21)
#define R_DMICCTL                       VIRT_ADDR(0x1, 0x22)
#define R_DACCTL                        VIRT_ADDR(0x2, 0x1)
#define R_SPKCTL                        VIRT_ADDR(0x2, 0x2)
#define R_SUBCTL                        VIRT_ADDR(0x2, 0x3)
#define R_DCCTL                         VIRT_ADDR(0x2, 0x4)
#define R_OVOLCTLU                      VIRT_ADDR(0x2, 0x6)
#define R_MUTEC                         VIRT_ADDR(0x2, 0x7)
#define R_MVOLL                         VIRT_ADDR(0x2, 0x8)
#define R_MVOLR                         VIRT_ADDR(0x2, 0x9)
#define R_HPVOLL                        VIRT_ADDR(0x2, 0x0A)
#define R_HPVOLR                        VIRT_ADDR(0x2, 0x0B)
#define R_SPKVOLL                       VIRT_ADDR(0x2, 0x0C)
#define R_SPKVOLR                       VIRT_ADDR(0x2, 0x0D)
#define R_SUBVOL                        VIRT_ADDR(0x2, 0x10)
#define R_COP0                          VIRT_ADDR(0x2, 0x11)
#define R_COP1                          VIRT_ADDR(0x2, 0x12)
#define R_COPSTAT                       VIRT_ADDR(0x2, 0x13)
#define R_PWM0                          VIRT_ADDR(0x2, 0x14)
#define R_PWM1                          VIRT_ADDR(0x2, 0x15)
#define R_PWM2                          VIRT_ADDR(0x2, 0x16)
#define R_PWM3                          VIRT_ADDR(0x2, 0x17)
#define R_HPSW                          VIRT_ADDR(0x2, 0x18)
#define R_THERMTS                       VIRT_ADDR(0x2, 0x19)
#define R_THERMSPK1                     VIRT_ADDR(0x2, 0x1A)
#define R_THERMSTAT                     VIRT_ADDR(0x2, 0x1B)
#define R_SCSTAT                        VIRT_ADDR(0x2, 0x1C)
#define R_SDMON                         VIRT_ADDR(0x2, 0x1D)
#define R_SPKEQFILT                     VIRT_ADDR(0x3, 0x1)
#define R_SPKCRWDL                      VIRT_ADDR(0x3, 0x2)
#define R_SPKCRWDM                      VIRT_ADDR(0x3, 0x3)
#define R_SPKCRWDH                      VIRT_ADDR(0x3, 0x4)
#define R_SPKCRRDL                      VIRT_ADDR(0x3, 0x5)
#define R_SPKCRRDM                      VIRT_ADDR(0x3, 0x6)
#define R_SPKCRRDH                      VIRT_ADDR(0x3, 0x7)
#define R_SPKCRADD                      VIRT_ADDR(0x3, 0x8)
#define R_SPKCRS                        VIRT_ADDR(0x3, 0x9)
#define R_SPKMBCEN                      VIRT_ADDR(0x3, 0x0A)
#define R_SPKMBCCTL                     VIRT_ADDR(0x3, 0x0B)
#define R_SPKMBCMUG1                    VIRT_ADDR(0x3, 0x0C)
#define R_SPKMBCTHR1                    VIRT_ADDR(0x3, 0x0D)
#define R_SPKMBCRAT1                    VIRT_ADDR(0x3, 0x0E)
#define R_SPKMBCATK1L                   VIRT_ADDR(0x3, 0x0F)
#define R_SPKMBCATK1H                   VIRT_ADDR(0x3, 0x10)
#define R_SPKMBCREL1L                   VIRT_ADDR(0x3, 0x11)
#define R_SPKMBCREL1H                   VIRT_ADDR(0x3, 0x12)
#define R_SPKMBCMUG2                    VIRT_ADDR(0x3, 0x13)
#define R_SPKMBCTHR2                    VIRT_ADDR(0x3, 0x14)
#define R_SPKMBCRAT2                    VIRT_ADDR(0x3, 0x15)
#define R_SPKMBCATK2L                   VIRT_ADDR(0x3, 0x16)
#define R_SPKMBCATK2H                   VIRT_ADDR(0x3, 0x17)
#define R_SPKMBCREL2L                   VIRT_ADDR(0x3, 0x18)
#define R_SPKMBCREL2H                   VIRT_ADDR(0x3, 0x19)
#define R_SPKMBCMUG3                    VIRT_ADDR(0x3, 0x1A)
#define R_SPKMBCTHR3                    VIRT_ADDR(0x3, 0x1B)
#define R_SPKMBCRAT3                    VIRT_ADDR(0x3, 0x1C)
#define R_SPKMBCATK3L                   VIRT_ADDR(0x3, 0x1D)
#define R_SPKMBCATK3H                   VIRT_ADDR(0x3, 0x1E)
#define R_SPKMBCREL3L                   VIRT_ADDR(0x3, 0x1F)
#define R_SPKMBCREL3H                   VIRT_ADDR(0x3, 0x20)
#define R_SPKCLECTL                     VIRT_ADDR(0x3, 0x21)
#define R_SPKCLEMUG                     VIRT_ADDR(0x3, 0x22)
#define R_SPKCOMPTHR                    VIRT_ADDR(0x3, 0x23)
#define R_SPKCOMPRAT                    VIRT_ADDR(0x3, 0x24)
#define R_SPKCOMPATKL                   VIRT_ADDR(0x3, 0x25)
#define R_SPKCOMPATKH                   VIRT_ADDR(0x3, 0x26)
#define R_SPKCOMPRELL                   VIRT_ADDR(0x3, 0x27)
#define R_SPKCOMPRELH                   VIRT_ADDR(0x3, 0x28)
#define R_SPKLIMTHR                     VIRT_ADDR(0x3, 0x29)
#define R_SPKLIMTGT                     VIRT_ADDR(0x3, 0x2A)
#define R_SPKLIMATKL                    VIRT_ADDR(0x3, 0x2B)
#define R_SPKLIMATKH                    VIRT_ADDR(0x3, 0x2C)
#define R_SPKLIMRELL                    VIRT_ADDR(0x3, 0x2D)
#define R_SPKLIMRELH                    VIRT_ADDR(0x3, 0x2E)
#define R_SPKEXPTHR                     VIRT_ADDR(0x3, 0x2F)
#define R_SPKEXPRAT                     VIRT_ADDR(0x3, 0x30)
#define R_SPKEXPATKL                    VIRT_ADDR(0x3, 0x31)
#define R_SPKEXPATKH                    VIRT_ADDR(0x3, 0x32)
#define R_SPKEXPRELL                    VIRT_ADDR(0x3, 0x33)
#define R_SPKEXPRELH                    VIRT_ADDR(0x3, 0x34)
#define R_SPKFXCTL                      VIRT_ADDR(0x3, 0x35)
#define R_DACEQFILT                     VIRT_ADDR(0x4, 0x1)
#define R_DACCRWDL                      VIRT_ADDR(0x4, 0x2)
#define R_DACCRWDM                      VIRT_ADDR(0x4, 0x3)
#define R_DACCRWDH                      VIRT_ADDR(0x4, 0x4)
#define R_DACCRRDL                      VIRT_ADDR(0x4, 0x5)
#define R_DACCRRDM                      VIRT_ADDR(0x4, 0x6)
#define R_DACCRRDH                      VIRT_ADDR(0x4, 0x7)
#define R_DACCRADD                      VIRT_ADDR(0x4, 0x8)
#define R_DACCRS                        VIRT_ADDR(0x4, 0x9)
#define R_DACMBCEN                      VIRT_ADDR(0x4, 0x0A)
#define R_DACMBCCTL                     VIRT_ADDR(0x4, 0x0B)
#define R_DACMBCMUG1                    VIRT_ADDR(0x4, 0x0C)
#define R_DACMBCTHR1                    VIRT_ADDR(0x4, 0x0D)
#define R_DACMBCRAT1                    VIRT_ADDR(0x4, 0x0E)
#define R_DACMBCATK1L                   VIRT_ADDR(0x4, 0x0F)
#define R_DACMBCATK1H                   VIRT_ADDR(0x4, 0x10)
#define R_DACMBCREL1L                   VIRT_ADDR(0x4, 0x11)
#define R_DACMBCREL1H                   VIRT_ADDR(0x4, 0x12)
#define R_DACMBCMUG2                    VIRT_ADDR(0x4, 0x13)
#define R_DACMBCTHR2                    VIRT_ADDR(0x4, 0x14)
#define R_DACMBCRAT2                    VIRT_ADDR(0x4, 0x15)
#define R_DACMBCATK2L                   VIRT_ADDR(0x4, 0x16)
#define R_DACMBCATK2H                   VIRT_ADDR(0x4, 0x17)
#define R_DACMBCREL2L                   VIRT_ADDR(0x4, 0x18)
#define R_DACMBCREL2H                   VIRT_ADDR(0x4, 0x19)
#define R_DACMBCMUG3                    VIRT_ADDR(0x4, 0x1A)
#define R_DACMBCTHR3                    VIRT_ADDR(0x4, 0x1B)
#define R_DACMBCRAT3                    VIRT_ADDR(0x4, 0x1C)
#define R_DACMBCATK3L                   VIRT_ADDR(0x4, 0x1D)
#define R_DACMBCATK3H                   VIRT_ADDR(0x4, 0x1E)
#define R_DACMBCREL3L                   VIRT_ADDR(0x4, 0x1F)
#define R_DACMBCREL3H                   VIRT_ADDR(0x4, 0x20)
#define R_DACCLECTL                     VIRT_ADDR(0x4, 0x21)
#define R_DACCLEMUG                     VIRT_ADDR(0x4, 0x22)
#define R_DACCOMPTHR                    VIRT_ADDR(0x4, 0x23)
#define R_DACCOMPRAT                    VIRT_ADDR(0x4, 0x24)
#define R_DACCOMPATKL                   VIRT_ADDR(0x4, 0x25)
#define R_DACCOMPATKH                   VIRT_ADDR(0x4, 0x26)
#define R_DACCOMPRELL                   VIRT_ADDR(0x4, 0x27)
#define R_DACCOMPRELH                   VIRT_ADDR(0x4, 0x28)
#define R_DACLIMTHR                     VIRT_ADDR(0x4, 0x29)
#define R_DACLIMTGT                     VIRT_ADDR(0x4, 0x2A)
#define R_DACLIMATKL                    VIRT_ADDR(0x4, 0x2B)
#define R_DACLIMATKH                    VIRT_ADDR(0x4, 0x2C)
#define R_DACLIMRELL                    VIRT_ADDR(0x4, 0x2D)
#define R_DACLIMRELH                    VIRT_ADDR(0x4, 0x2E)
#define R_DACEXPTHR                     VIRT_ADDR(0x4, 0x2F)
#define R_DACEXPRAT                     VIRT_ADDR(0x4, 0x30)
#define R_DACEXPATKL                    VIRT_ADDR(0x4, 0x31)
#define R_DACEXPATKH                    VIRT_ADDR(0x4, 0x32)
#define R_DACEXPRELL                    VIRT_ADDR(0x4, 0x33)
#define R_DACEXPRELH                    VIRT_ADDR(0x4, 0x34)
#define R_DACFXCTL                      VIRT_ADDR(0x4, 0x35)
#define R_SUBEQFILT                     VIRT_ADDR(0x5, 0x1)
#define R_SUBCRWDL                      VIRT_ADDR(0x5, 0x2)
#define R_SUBCRWDM                      VIRT_ADDR(0x5, 0x3)
#define R_SUBCRWDH                      VIRT_ADDR(0x5, 0x4)
#define R_SUBCRRDL                      VIRT_ADDR(0x5, 0x5)
#define R_SUBCRRDM                      VIRT_ADDR(0x5, 0x6)
#define R_SUBCRRDH                      VIRT_ADDR(0x5, 0x7)
#define R_SUBCRADD                      VIRT_ADDR(0x5, 0x8)
#define R_SUBCRS                        VIRT_ADDR(0x5, 0x9)
#define R_SUBMBCEN                      VIRT_ADDR(0x5, 0x0A)
#define R_SUBMBCCTL                     VIRT_ADDR(0x5, 0x0B)
#define R_SUBMBCMUG1                    VIRT_ADDR(0x5, 0x0C)
#define R_SUBMBCTHR1                    VIRT_ADDR(0x5, 0x0D)
#define R_SUBMBCRAT1                    VIRT_ADDR(0x5, 0x0E)
#define R_SUBMBCATK1L                   VIRT_ADDR(0x5, 0x0F)
#define R_SUBMBCATK1H                   VIRT_ADDR(0x5, 0x10)
#define R_SUBMBCREL1L                   VIRT_ADDR(0x5, 0x11)
#define R_SUBMBCREL1H                   VIRT_ADDR(0x5, 0x12)
#define R_SUBMBCMUG2                    VIRT_ADDR(0x5, 0x13)
#define R_SUBMBCTHR2                    VIRT_ADDR(0x5, 0x14)
#define R_SUBMBCRAT2                    VIRT_ADDR(0x5, 0x15)
#define R_SUBMBCATK2L                   VIRT_ADDR(0x5, 0x16)
#define R_SUBMBCATK2H                   VIRT_ADDR(0x5, 0x17)
#define R_SUBMBCREL2L                   VIRT_ADDR(0x5, 0x18)
#define R_SUBMBCREL2H                   VIRT_ADDR(0x5, 0x19)
#define R_SUBMBCMUG3                    VIRT_ADDR(0x5, 0x1A)
#define R_SUBMBCTHR3                    VIRT_ADDR(0x5, 0x1B)
#define R_SUBMBCRAT3                    VIRT_ADDR(0x5, 0x1C)
#define R_SUBMBCATK3L                   VIRT_ADDR(0x5, 0x1D)
#define R_SUBMBCATK3H                   VIRT_ADDR(0x5, 0x1E)
#define R_SUBMBCREL3L                   VIRT_ADDR(0x5, 0x1F)
#define R_SUBMBCREL3H                   VIRT_ADDR(0x5, 0x20)
#define R_SUBCLECTL                     VIRT_ADDR(0x5, 0x21)
#define R_SUBCLEMUG                     VIRT_ADDR(0x5, 0x22)
#define R_SUBCOMPTHR                    VIRT_ADDR(0x5, 0x23)
#define R_SUBCOMPRAT                    VIRT_ADDR(0x5, 0x24)
#define R_SUBCOMPATKL                   VIRT_ADDR(0x5, 0x25)
#define R_SUBCOMPATKH                   VIRT_ADDR(0x5, 0x26)
#define R_SUBCOMPRELL                   VIRT_ADDR(0x5, 0x27)
#define R_SUBCOMPRELH                   VIRT_ADDR(0x5, 0x28)
#define R_SUBLIMTHR                     VIRT_ADDR(0x5, 0x29)
#define R_SUBLIMTGT                     VIRT_ADDR(0x5, 0x2A)
#define R_SUBLIMATKL                    VIRT_ADDR(0x5, 0x2B)
#define R_SUBLIMATKH                    VIRT_ADDR(0x5, 0x2C)
#define R_SUBLIMRELL                    VIRT_ADDR(0x5, 0x2D)
#define R_SUBLIMRELH                    VIRT_ADDR(0x5, 0x2E)
#define R_SUBEXPTHR                     VIRT_ADDR(0x5, 0x2F)
#define R_SUBEXPRAT                     VIRT_ADDR(0x5, 0x30)
#define R_SUBEXPATKL                    VIRT_ADDR(0x5, 0x31)
#define R_SUBEXPATKH                    VIRT_ADDR(0x5, 0x32)
#define R_SUBEXPRELL                    VIRT_ADDR(0x5, 0x33)
#define R_SUBEXPRELH                    VIRT_ADDR(0x5, 0x34)
#define R_SUBFXCTL                      VIRT_ADDR(0x5, 0x35)

// *** PLLCTL ***
#define FB_PLLCTL_VCCI_PLL                                  6
#define FM_PLLCTL_VCCI_PLL                                  0xC0

#define FB_PLLCTL_RZ_PLL                                    3
#define FM_PLLCTL_RZ_PLL                                    0x38

#define FB_PLLCTL_CP_PLL                                    0
#define FM_PLLCTL_CP_PLL                                    0x7

// *** PLLRDIV ***
#define FB_PLLRDIV_REFDIV_PLL                               0
#define FM_PLLRDIV_REFDIV_PLL                               0xFF

// *** PLLODIV ***
#define FB_PLLODIV_OUTDIV_PLL                               0
#define FM_PLLODIV_OUTDIV_PLL                               0xFF

// *** PLLFDIVL ***
#define FB_PLLFDIVL_FBDIVL_PLL                              0
#define FM_PLLFDIVL_FBDIVL_PLL                              0xFF

// *** PLLFDIVH ***
#define FB_PLLFDIVH_FBDIVH_PLL                              0
#define FM_PLLFDIVH_FBDIVH_PLL                              0xF

// *** I2SPCTL ***
#define FB_I2SPCTL_BCLKSTAT                                 7
#define FM_I2SPCTL_BCLKSTAT                                 0x80
#define FV_BCLKSTAT_LOST                                    0x80
#define FV_BCLKSTAT_NOT_LOST                                0x0

#define FB_I2SPCTL_BCLKP                                    6
#define FM_I2SPCTL_BCLKP                                    0x40
#define FV_BCLKP_NOT_INVERTED                               0x0
#define FV_BCLKP_INVERTED                                   0x40

#define FB_I2SPCTL_PORTMS                                   5
#define FM_I2SPCTL_PORTMS                                   0x20
#define FV_PORTMS_SLAVE                                     0x0
#define FV_PORTMS_MASTER                                    0x20

#define FB_I2SPCTL_LRCLKP                                   4
#define FM_I2SPCTL_LRCLKP                                   0x10
#define FV_LRCLKP_NOT_INVERTED                              0x0
#define FV_LRCLKP_INVERTED                                  0x10

#define FB_I2SPCTL_WL                                       2
#define FM_I2SPCTL_WL                                       0xC
#define FV_WL_16                                            0x0
#define FV_WL_20                                            0x4
#define FV_WL_24                                            0x8
#define FV_WL_32                                            0xC

#define FB_I2SPCTL_FORMAT                                   0
#define FM_I2SPCTL_FORMAT                                   0x3
#define FV_FORMAT_RIGHT                                     0x0
#define FV_FORMAT_LEFT                                      0x1
#define FV_FORMAT_I2S                                       0x2
#define FV_FORMAT_TDM                                       0x3

// *** I2SMRATE ***
#define FB_I2SMRATE_I2SMCLKHALF                             7
#define FM_I2SMRATE_I2SMCLKHALF                             0x80
#define FV_I2SMCLKHALF_I2S1MCLKDIV_DIV_2                    0x0
#define FV_I2SMCLKHALF_I2S1MCLKDIV_ONLY                     0x80

#define FB_I2SMRATE_I2SMCLKDIV                              5
#define FM_I2SMRATE_I2SMCLKDIV                              0x60
#define FV_I2SMCLKDIV_125                                   0x0
#define FV_I2SMCLKDIV_128                                   0x20
#define FV_I2SMCLKDIV_136                                   0x40
#define FV_I2SMCLKDIV_192                                   0x60

#define FB_I2SMRATE_I2SMBR                                  3
#define FM_I2SMRATE_I2SMBR                                  0x18
#define FV_I2SMBR_32                                        0x0
#define FV_I2SMBR_44PT1                                     0x8
#define FV_I2SMBR_48                                        0x10
#define FV_I2SMBR_MCLK_MODE                                 0x18

#define FB_I2SMRATE_I2SMBM                                  0
#define FM_I2SMRATE_I2SMBM                                  0x3
#define FV_I2SMBM_0PT25                                     0x0
#define FV_I2SMBM_0PT5                                      0x1
#define FV_I2SMBM_1                                         0x2
#define FV_I2SMBM_2                                         0x3

// *** PCMPCTL0 ***
#define FB_PCMPCTL0_PCMFLENP                                2
#define FM_PCMPCTL0_PCMFLENP                                0x4
#define FV_PCMFLENP_128                                     0x0
#define FV_PCMFLENP_256                                     0x4

#define FB_PCMPCTL0_SLSYNCP                                 1
#define FM_PCMPCTL0_SLSYNCP                                 0x2
#define FV_SLSYNCP_SHORT                                    0x0
#define FV_SLSYNCP_LONG                                     0x2

#define FB_PCMPCTL0_BDELAYP                                 0
#define FM_PCMPCTL0_BDELAYP                                 0x1
#define FV_BDELAYP_NO_DELAY                                 0x0
#define FV_BDELAYP_1BCLK_DELAY                              0x1

// *** PCMPCTL1 ***
#define FB_PCMPCTL1_PCMMOMP                                 6
#define FM_PCMPCTL1_PCMMOMP                                 0x40

#define FB_PCMPCTL1_PCMSOP                                  5
#define FM_PCMPCTL1_PCMSOP                                  0x20
#define FV_PCMSOP_1                                         0x0
#define FV_PCMSOP_2                                         0x20

#define FB_PCMPCTL1_PCMDSSP                                 3
#define FM_PCMPCTL1_PCMDSSP                                 0x18
#define FV_PCMDSSP_16                                       0x0
#define FV_PCMDSSP_24                                       0x8
#define FV_PCMDSSP_32                                       0x10

#define FB_PCMPCTL1_PCMMIMP                                 1
#define FM_PCMPCTL1_PCMMIMP                                 0x2

#define FB_PCMPCTL1_PCMSIP                                  0
#define FM_PCMPCTL1_PCMSIP                                  0x1
#define FV_PCMSIP_1                                         0x0
#define FV_PCMSIP_2                                         0x1

// *** CHAIC ***
#define FB_CHAIC_MICBST                                     4
#define FM_CHAIC_MICBST                                     0x30

// *** PGACTL ***
#define FB_PGACTL_PGAMUTE                                   7
#define FM_PGACTL_PGAMUTE                                   0x80

#define FB_PGACTL_PGAVOL                                    0
#define FM_PGACTL_PGAVOL                                    0x3F

// *** ICHVOL ***
#define FB_ICHVOL_ICHVOL                                    0
#define FM_ICHVOL_ICHVOL                                    0xFF

// *** SPKMBCMUG ***
#define FB_SPKMBCMUG_PHASE                                  5
#define FM_SPKMBCMUG_PHASE                                  0x20

#define FB_SPKMBCMUG_MUGAIN                                 0
#define FM_SPKMBCMUG_MUGAIN                                 0x1F

// *** SPKMBCTHR ***
#define FB_SPKMBCTHR_THRESH                                 0
#define FM_SPKMBCTHR_THRESH                                 0xFF

// *** SPKMBCRAT ***
#define FB_SPKMBCRAT_RATIO                                  0
#define FM_SPKMBCRAT_RATIO                                  0x1F

// *** SPKMBCATKL ***
#define FB_SPKMBCATKL_TCATKL                                0
#define FM_SPKMBCATKL_TCATKL                                0xFF

// *** SPKMBCATKH ***
#define FB_SPKMBCATKH_TCATKH                                0
#define FM_SPKMBCATKH_TCATKH                                0xFF

// *** SPKMBCRELL ***
#define FB_SPKMBCRELL_TCRELL                                0
#define FM_SPKMBCRELL_TCRELL                                0xFF

// *** SPKMBCRELH ***
#define FB_SPKMBCRELH_TCRELH                                0
#define FM_SPKMBCRELH_TCRELH                                0xFF

// *** DACMBCMUG ***
#define FB_DACMBCMUG_PHASE                                  5
#define FM_DACMBCMUG_PHASE                                  0x20

#define FB_DACMBCMUG_MUGAIN                                 0
#define FM_DACMBCMUG_MUGAIN                                 0x1F

// *** DACMBCTHR ***
#define FB_DACMBCTHR_THRESH                                 0
#define FM_DACMBCTHR_THRESH                                 0xFF

// *** DACMBCRAT ***
#define FB_DACMBCRAT_RATIO                                  0
#define FM_DACMBCRAT_RATIO                                  0x1F

// *** DACMBCATKL ***
#define FB_DACMBCATKL_TCATKL                                0
#define FM_DACMBCATKL_TCATKL                                0xFF

// *** DACMBCATKH ***
#define FB_DACMBCATKH_TCATKH                                0
#define FM_DACMBCATKH_TCATKH                                0xFF

// *** DACMBCRELL ***
#define FB_DACMBCRELL_TCRELL                                0
#define FM_DACMBCRELL_TCRELL                                0xFF

// *** DACMBCRELH ***
#define FB_DACMBCRELH_TCRELH                                0
#define FM_DACMBCRELH_TCRELH                                0xFF

// *** SUBMBCMUG ***
#define FB_SUBMBCMUG_PHASE                                  5
#define FM_SUBMBCMUG_PHASE                                  0x20

#define FB_SUBMBCMUG_MUGAIN                                 0
#define FM_SUBMBCMUG_MUGAIN                                 0x1F

// *** SUBMBCTHR ***
#define FB_SUBMBCTHR_THRESH                                 0
#define FM_SUBMBCTHR_THRESH                                 0xFF

// *** SUBMBCRAT ***
#define FB_SUBMBCRAT_RATIO                                  0
#define FM_SUBMBCRAT_RATIO                                  0x1F

// *** SUBMBCATKL ***
#define FB_SUBMBCATKL_TCATKL                                0
#define FM_SUBMBCATKL_TCATKL                                0xFF

// *** SUBMBCATKH ***
#define FB_SUBMBCATKH_TCATKH                                0
#define FM_SUBMBCATKH_TCATKH                                0xFF

// *** SUBMBCRELL ***
#define FB_SUBMBCRELL_TCRELL                                0
#define FM_SUBMBCRELL_TCRELL                                0xFF

// *** SUBMBCRELH ***
#define FB_SUBMBCRELH_TCRELH                                0
#define FM_SUBMBCRELH_TCRELH                                0xFF

// *** PAGESEL ***
#define FB_PAGESEL_PAGESEL                                  0
#define FM_PAGESEL_PAGESEL                                  0xFF

// *** RESET ***
#define FB_RESET_RESET                                      0
#define FM_RESET_RESET                                      0xFF
#define FV_RESET_PWR_ON_DEFAULTS                            0x85

// *** IRQEN ***
#define FB_IRQEN_THRMINTEN                                  6
#define FM_IRQEN_THRMINTEN                                  0x40
#define FV_THRMINTEN_ENABLED                                0x40
#define FV_THRMINTEN_DISABLED                               0x0

#define FB_IRQEN_HBPINTEN                                   5
#define FM_IRQEN_HBPINTEN                                   0x20
#define FV_HBPINTEN_ENABLED                                 0x20
#define FV_HBPINTEN_DISABLED                                0x0

#define FB_IRQEN_HSDINTEN                                   4
#define FM_IRQEN_HSDINTEN                                   0x10
#define FV_HSDINTEN_ENABLED                                 0x10
#define FV_HSDINTEN_DISABLED                                0x0

#define FB_IRQEN_HPDINTEN                                   3
#define FM_IRQEN_HPDINTEN                                   0x8
#define FV_HPDINTEN_ENABLED                                 0x8
#define FV_HPDINTEN_DISABLED                                0x0

#define FB_IRQEN_GPIO3INTEN                                 1
#define FM_IRQEN_GPIO3INTEN                                 0x2
#define FV_GPIO3INTEN_ENABLED                               0x2
#define FV_GPIO3INTEN_DISABLED                              0x0

#define FB_IRQEN_GPIO2INTEN                                 0
#define FM_IRQEN_GPIO2INTEN                                 0x1
#define FV_GPIO2INTEN_ENABLED                               0x1
#define FV_GPIO2INTEN_DISABLED                              0x0

#define IRQEN_GPIOINTEN_ENABLED                             0x1
#define IRQEN_GPIOINTEN_DISABLED                            0x0

// *** IRQMASK ***
#define FB_IRQMASK_THRMIM                                   6
#define FM_IRQMASK_THRMIM                                   0x40
#define FV_THRMIM_MASKED                                    0x0
#define FV_THRMIM_NOT_MASKED                                0x40

#define FB_IRQMASK_HBPIM                                    5
#define FM_IRQMASK_HBPIM                                    0x20
#define FV_HBPIM_MASKED                                     0x0
#define FV_HBPIM_NOT_MASKED                                 0x20

#define FB_IRQMASK_HSDIM                                    4
#define FM_IRQMASK_HSDIM                                    0x10
#define FV_HSDIM_MASKED                                     0x0
#define FV_HSDIM_NOT_MASKED                                 0x10

#define FB_IRQMASK_HPDIM                                    3
#define FM_IRQMASK_HPDIM                                    0x8
#define FV_HPDIM_MASKED                                     0x0
#define FV_HPDIM_NOT_MASKED                                 0x8

#define FB_IRQMASK_GPIO3M                                   1
#define FM_IRQMASK_GPIO3M                                   0x2
#define FV_GPIO3M_MASKED                                    0x0
#define FV_GPIO3M_NOT_MASKED                                0x2

#define FB_IRQMASK_GPIO2M                                   0
#define FM_IRQMASK_GPIO2M                                   0x1
#define FV_GPIO2M_MASKED                                    0x0
#define FV_GPIO2M_NOT_MASKED                                0x1

#define IRQMASK_GPIOM_MASKED                                0x0
#define IRQMASK_GPIOM_NOT_MASKED                            0x1

// *** IRQSTAT ***
#define FB_IRQSTAT_THRMINT                                  6
#define FM_IRQSTAT_THRMINT                                  0x40
#define FV_THRMINT_INTERRUPTED                              0x40
#define FV_THRMINT_NOT_INTERRUPTED                          0x0

#define FB_IRQSTAT_HBPINT                                   5
#define FM_IRQSTAT_HBPINT                                   0x20
#define FV_HBPINT_INTERRUPTED                               0x20
#define FV_HBPINT_NOT_INTERRUPTED                           0x0

#define FB_IRQSTAT_HSDINT                                   4
#define FM_IRQSTAT_HSDINT                                   0x10
#define FV_HSDINT_INTERRUPTED                               0x10
#define FV_HSDINT_NOT_INTERRUPTED                           0x0

#define FB_IRQSTAT_HPDINT                                   3
#define FM_IRQSTAT_HPDINT                                   0x8
#define FV_HPDINT_INTERRUPTED                               0x8
#define FV_HPDINT_NOT_INTERRUPTED                           0x0

#define FB_IRQSTAT_GPIO3INT                                 1
#define FM_IRQSTAT_GPIO3INT                                 0x2
#define FV_GPIO3INT_INTERRUPTED                             0x2
#define FV_GPIO3INT_NOT_INTERRUPTED                         0x0

#define FB_IRQSTAT_GPIO2INT                                 0
#define FM_IRQSTAT_GPIO2INT                                 0x1
#define FV_GPIO2INT_INTERRUPTED                             0x1
#define FV_GPIO2INT_NOT_INTERRUPTED                         0x0

#define IRQSTAT_GPIOINT_INTERRUPTED                         0x1
#define IRQSTAT_GPIOINT_NOT_INTERRUPTED                     0x0

// *** DEVADD0 ***
#define FB_DEVADD0_DEVADD0                                  1
#define FM_DEVADD0_DEVADD0                                  0xFE

#define FB_DEVADD0_I2C_ADDRLK                               0
#define FM_DEVADD0_I2C_ADDRLK                               0x1
#define FV_I2C_ADDRLK_LOCK                                  0x1

// *** DEVID ***
#define FB_DEVID_DEV_ID                                     0
#define FM_DEVID_DEV_ID                                     0xFF

// *** DEVREV ***
#define FB_DEVREV_MAJ_REV                                   4
#define FM_DEVREV_MAJ_REV                                   0xF0

#define FB_DEVREV_MIN_REV                                   0
#define FM_DEVREV_MIN_REV                                   0xF

// *** PLLSTAT ***
#define FB_PLLSTAT_PLL2LK                                   1
#define FM_PLLSTAT_PLL2LK                                   0x2
#define FV_PLL2LK_LOCKED                                    0x2
#define FV_PLL2LK_UNLOCKED                                  0x0

#define FB_PLLSTAT_PLL1LK                                   0
#define FM_PLLSTAT_PLL1LK                                   0x1
#define FV_PLL1LK_LOCKED                                    0x1
#define FV_PLL1LK_UNLOCKED                                  0x0

#define PLLSTAT_PLLLK_LOCKED                                0x1
#define PLLSTAT_PLLLK_UNLOCKED                              0x0

// *** PLLCTL ***
#define FB_PLLCTL_PU_PLL2                                   7
#define FM_PLLCTL_PU_PLL2                                   0x80
#define FV_PU_PLL2_PWR_UP                                   0x80
#define FV_PU_PLL2_PWR_DWN                                  0x0

#define FB_PLLCTL_PU_PLL1                                   6
#define FM_PLLCTL_PU_PLL1                                   0x40
#define FV_PU_PLL1_PWR_UP                                   0x40
#define FV_PU_PLL1_PWR_DWN                                  0x0

#define FB_PLLCTL_PLL2CLKEN                                 5
#define FM_PLLCTL_PLL2CLKEN                                 0x20
#define FV_PLL2CLKEN_ENABLE                                 0x20
#define FV_PLL2CLKEN_DISABLE                                0x0

#define FB_PLLCTL_PLL1CLKEN                                 4
#define FM_PLLCTL_PLL1CLKEN                                 0x10
#define FV_PLL1CLKEN_ENABLE                                 0x10
#define FV_PLL1CLKEN_DISABLE                                0x0

#define FB_PLLCTL_BCLKSEL                                   2
#define FM_PLLCTL_BCLKSEL                                   0xC
#define FV_BCLKSEL_BCLK1                                    0x0
#define FV_BCLKSEL_BCLK2                                    0x4
#define FV_BCLKSEL_BCLK3                                    0x8

#define FB_PLLCTL_PLLISEL                                   0
#define FM_PLLCTL_PLLISEL                                   0x3
#define FV_PLLISEL_XTAL                                     0x0
#define FV_PLLISEL_MCLK1                                    0x1
#define FV_PLLISEL_MCLK2                                    0x2
#define FV_PLLISEL_BCLK                                     0x3

#define PLLCTL_PU_PLL_PWR_UP                                0x1
#define PLLCTL_PU_PLL_PWR_DWN                               0x0
#define PLLCTL_PLLCLKEN_ENABLE                              0x1
#define PLLCTL_PLLCLKEN_DISABLE                             0x0

// *** ISRC ***
#define FB_ISRC_IBR                                         2
#define FM_ISRC_IBR                                         0x4
#define FV_IBR_44PT1                                        0x0
#define FV_IBR_48                                           0x4

#define FB_ISRC_IBM                                         0
#define FM_ISRC_IBM                                         0x3
#define FV_IBM_0PT25                                        0x0
#define FV_IBM_0PT5                                         0x1
#define FV_IBM_1                                            0x2
#define FV_IBM_2                                            0x3

// *** SCLKCTL ***
#define FB_SCLKCTL_ASDM                                     6
#define FM_SCLKCTL_ASDM                                     0xC0
#define FV_ASDM_HALF                                        0x40
#define FV_ASDM_FULL                                        0x80
#define FV_ASDM_AUTO                                        0xC0

#define FB_SCLKCTL_DSDM                                     4
#define FM_SCLKCTL_DSDM                                     0x30
#define FV_DSDM_HALF                                        0x10
#define FV_DSDM_FULL                                        0x20
#define FV_DSDM_AUTO                                        0x30

// *** TIMEBASE ***
#define FB_TIMEBASE_TIMEBASE                                0
#define FM_TIMEBASE_TIMEBASE                                0xFF

// *** I2SCMC ***
#define FB_I2SCMC_BCMP3                                     4
#define FM_I2SCMC_BCMP3                                     0x30
#define FV_BCMP3_AUTO                                       0x0
#define FV_BCMP3_32X                                        0x10
#define FV_BCMP3_40X                                        0x20
#define FV_BCMP3_64X                                        0x30

#define FB_I2SCMC_BCMP2                                     2
#define FM_I2SCMC_BCMP2                                     0xC
#define FV_BCMP2_AUTO                                       0x0
#define FV_BCMP2_32X                                        0x4
#define FV_BCMP2_40X                                        0x8
#define FV_BCMP2_64X                                        0xC

#define FB_I2SCMC_BCMP1                                     0
#define FM_I2SCMC_BCMP1                                     0x3
#define FV_BCMP1_AUTO                                       0x0
#define FV_BCMP1_32X                                        0x1
#define FV_BCMP1_40X                                        0x2
#define FV_BCMP1_64X                                        0x3

#define I2SCMC_BCMP_AUTO                                    0x0
#define I2SCMC_BCMP_32X                                     0x1
#define I2SCMC_BCMP_40X                                     0x2
#define I2SCMC_BCMP_64X                                     0x3

// *** MCLK2PINC ***
#define FB_MCLK2PINC_SLEWOUT                                4
#define FM_MCLK2PINC_SLEWOUT                                0xF0

#define FB_MCLK2PINC_MCLK2IO                                2
#define FM_MCLK2PINC_MCLK2IO                                0x4
#define FV_MCLK2IO_INPUT                                    0x0
#define FV_MCLK2IO_OUTPUT                                   0x4

#define FB_MCLK2PINC_MCLK2OS                                0
#define FM_MCLK2PINC_MCLK2OS                                0x3
#define FV_MCLK2OS_24PT576                                  0x0
#define FV_MCLK2OS_22PT5792                                 0x1
#define FV_MCLK2OS_PLL2                                     0x2

// *** I2SPINC0 ***
#define FB_I2SPINC0_SDO3TRI                                 7
#define FM_I2SPINC0_SDO3TRI                                 0x80

#define FB_I2SPINC0_SDO2TRI                                 6
#define FM_I2SPINC0_SDO2TRI                                 0x40

#define FB_I2SPINC0_SDO1TRI                                 5
#define FM_I2SPINC0_SDO1TRI                                 0x20

#define FB_I2SPINC0_PCM3TRI                                 2
#define FM_I2SPINC0_PCM3TRI                                 0x4

#define FB_I2SPINC0_PCM2TRI                                 1
#define FM_I2SPINC0_PCM2TRI                                 0x2

#define FB_I2SPINC0_PCM1TRI                                 0
#define FM_I2SPINC0_PCM1TRI                                 0x1

// *** I2SPINC1 ***
#define FB_I2SPINC1_SDO3PDD                                 2
#define FM_I2SPINC1_SDO3PDD                                 0x4

#define FB_I2SPINC1_SDO2PDD                                 1
#define FM_I2SPINC1_SDO2PDD                                 0x2

#define FB_I2SPINC1_SDO1PDD                                 0
#define FM_I2SPINC1_SDO1PDD                                 0x1

// *** I2SPINC2 ***
#define FB_I2SPINC2_LR3PDD                                  5
#define FM_I2SPINC2_LR3PDD                                  0x20

#define FB_I2SPINC2_BC3PDD                                  4
#define FM_I2SPINC2_BC3PDD                                  0x10

#define FB_I2SPINC2_LR2PDD                                  3
#define FM_I2SPINC2_LR2PDD                                  0x8

#define FB_I2SPINC2_BC2PDD                                  2
#define FM_I2SPINC2_BC2PDD                                  0x4

#define FB_I2SPINC2_LR1PDD                                  1
#define FM_I2SPINC2_LR1PDD                                  0x2

#define FB_I2SPINC2_BC1PDD                                  0
#define FM_I2SPINC2_BC1PDD                                  0x1

// *** GPIOCTL0 ***
#define FB_GPIOCTL0_GPIO3INTP                               7
#define FM_GPIOCTL0_GPIO3INTP                               0x80

#define FB_GPIOCTL0_GPIO2INTP                               6
#define FM_GPIOCTL0_GPIO2INTP                               0x40

#define FB_GPIOCTL0_GPIO3CFG                                5
#define FM_GPIOCTL0_GPIO3CFG                                0x20

#define FB_GPIOCTL0_GPIO2CFG                                4
#define FM_GPIOCTL0_GPIO2CFG                                0x10

#define FB_GPIOCTL0_GPIO3IO                                 3
#define FM_GPIOCTL0_GPIO3IO                                 0x8

#define FB_GPIOCTL0_GPIO2IO                                 2
#define FM_GPIOCTL0_GPIO2IO                                 0x4

#define FB_GPIOCTL0_GPIO1IO                                 1
#define FM_GPIOCTL0_GPIO1IO                                 0x2

#define FB_GPIOCTL0_GPIO0IO                                 0
#define FM_GPIOCTL0_GPIO0IO                                 0x1

// *** GPIOCTL1 ***
#define FB_GPIOCTL1_GPIO3                                   7
#define FM_GPIOCTL1_GPIO3                                   0x80

#define FB_GPIOCTL1_GPIO2                                   6
#define FM_GPIOCTL1_GPIO2                                   0x40

#define FB_GPIOCTL1_GPIO1                                   5
#define FM_GPIOCTL1_GPIO1                                   0x20

#define FB_GPIOCTL1_GPIO0                                   4
#define FM_GPIOCTL1_GPIO0                                   0x10

#define FB_GPIOCTL1_GPIO3RD                                 3
#define FM_GPIOCTL1_GPIO3RD                                 0x8

#define FB_GPIOCTL1_GPIO2RD                                 2
#define FM_GPIOCTL1_GPIO2RD                                 0x4

#define FB_GPIOCTL1_GPIO1RD                                 1
#define FM_GPIOCTL1_GPIO1RD                                 0x2

#define FB_GPIOCTL1_GPIO0RD                                 0
#define FM_GPIOCTL1_GPIO0RD                                 0x1

// *** ASRC ***
#define FB_ASRC_ASRCOBW                                     7
#define FM_ASRC_ASRCOBW                                     0x80

#define FB_ASRC_ASRCIBW                                     6
#define FM_ASRC_ASRCIBW                                     0x40

#define FB_ASRC_ASRCOB                                      5
#define FM_ASRC_ASRCOB                                      0x20
#define FV_ASRCOB_ACTIVE                                    0x0
#define FV_ASRCOB_BYPASSED                                  0x20

#define FB_ASRC_ASRCIB                                      4
#define FM_ASRC_ASRCIB                                      0x10
#define FV_ASRCIB_ACTIVE                                    0x0
#define FV_ASRCIB_BYPASSED                                  0x10

#define FB_ASRC_ASRCOL                                      3
#define FM_ASRC_ASRCOL                                      0x8

#define FB_ASRC_ASRCIL                                      2
#define FM_ASRC_ASRCIL                                      0x4

// *** TDMCTL0 ***
#define FB_TDMCTL0_TDMMD                                    2
#define FM_TDMCTL0_TDMMD                                    0x4
#define FV_TDMMD_200                                        0x0
#define FV_TDMMD_256                                        0x4

#define FB_TDMCTL0_SLSYNC                                   1
#define FM_TDMCTL0_SLSYNC                                   0x2
#define FV_SLSYNC_SHORT                                     0x0
#define FV_SLSYNC_LONG                                      0x2

#define FB_TDMCTL0_BDELAY                                   0
#define FM_TDMCTL0_BDELAY                                   0x1
#define FV_BDELAY_NO_DELAY                                  0x0
#define FV_BDELAY_1BCLK_DELAY                               0x1

// *** TDMCTL1 ***
#define FB_TDMCTL1_TDMSO                                    5
#define FM_TDMCTL1_TDMSO                                    0x60
#define FV_TDMSO_2                                          0x0
#define FV_TDMSO_4                                          0x20
#define FV_TDMSO_6                                          0x40

#define FB_TDMCTL1_TDMDSS                                   3
#define FM_TDMCTL1_TDMDSS                                   0x18
#define FV_TDMDSS_16                                        0x0
#define FV_TDMDSS_24                                        0x10
#define FV_TDMDSS_32                                        0x18

#define FB_TDMCTL1_TDMSI                                    0
#define FM_TDMCTL1_TDMSI                                    0x3
#define FV_TDMSI_2                                          0x0
#define FV_TDMSI_4                                          0x1
#define FV_TDMSI_6                                          0x2

// *** PWRM0 ***
#define FB_PWRM0_INPROC3PU                                  6
#define FM_PWRM0_INPROC3PU                                  0x40

#define FB_PWRM0_INPROC2PU                                  5
#define FM_PWRM0_INPROC2PU                                  0x20

#define FB_PWRM0_INPROC1PU                                  4
#define FM_PWRM0_INPROC1PU                                  0x10

#define FB_PWRM0_INPROC0PU                                  3
#define FM_PWRM0_INPROC0PU                                  0x8

#define FB_PWRM0_MICB2PU                                    2
#define FM_PWRM0_MICB2PU                                    0x4

#define FB_PWRM0_MICB1PU                                    1
#define FM_PWRM0_MICB1PU                                    0x2

#define FB_PWRM0_MCLKPEN                                    0
#define FM_PWRM0_MCLKPEN                                    0x1

// *** PWRM1 ***
#define FB_PWRM1_SUBPU                                      7
#define FM_PWRM1_SUBPU                                      0x80

#define FB_PWRM1_HPLPU                                      6
#define FM_PWRM1_HPLPU                                      0x40

#define FB_PWRM1_HPRPU                                      5
#define FM_PWRM1_HPRPU                                      0x20

#define FB_PWRM1_SPKLPU                                     4
#define FM_PWRM1_SPKLPU                                     0x10

#define FB_PWRM1_SPKRPU                                     3
#define FM_PWRM1_SPKRPU                                     0x8

#define FB_PWRM1_D2S2PU                                     2
#define FM_PWRM1_D2S2PU                                     0x4

#define FB_PWRM1_D2S1PU                                     1
#define FM_PWRM1_D2S1PU                                     0x2

#define FB_PWRM1_VREFPU                                     0
#define FM_PWRM1_VREFPU                                     0x1

// *** PWRM2 ***
#define FB_PWRM2_I2S3OPU                                    5
#define FM_PWRM2_I2S3OPU                                    0x20
#define FV_I2S3OPU_PWR_DOWN                                 0x0
#define FV_I2S3OPU_PWR_UP                                   0x20

#define FB_PWRM2_I2S2OPU                                    4
#define FM_PWRM2_I2S2OPU                                    0x10
#define FV_I2S2OPU_PWR_DOWN                                 0x0
#define FV_I2S2OPU_PWR_UP                                   0x10

#define FB_PWRM2_I2S1OPU                                    3
#define FM_PWRM2_I2S1OPU                                    0x8
#define FV_I2S1OPU_PWR_DOWN                                 0x0
#define FV_I2S1OPU_PWR_UP                                   0x8

#define FB_PWRM2_I2S3IPU                                    2
#define FM_PWRM2_I2S3IPU                                    0x4
#define FV_I2S3IPU_PWR_DOWN                                 0x0
#define FV_I2S3IPU_PWR_UP                                   0x4

#define FB_PWRM2_I2S2IPU                                    1
#define FM_PWRM2_I2S2IPU                                    0x2
#define FV_I2S2IPU_PWR_DOWN                                 0x0
#define FV_I2S2IPU_PWR_UP                                   0x2

#define FB_PWRM2_I2S1IPU                                    0
#define FM_PWRM2_I2S1IPU                                    0x1
#define FV_I2S1IPU_PWR_DOWN                                 0x0
#define FV_I2S1IPU_PWR_UP                                   0x1

#define PWRM2_I2SOPU_PWR_DOWN                               0x0
#define PWRM2_I2SOPU_PWR_UP                                 0x1
#define PWRM2_I2SIPU_PWR_DOWN                               0x0
#define PWRM2_I2SIPU_PWR_UP                                 0x1

// *** PWRM3 ***
#define FB_PWRM3_BGSBUP                                     6
#define FM_PWRM3_BGSBUP                                     0x40
#define FV_BGSBUP_ON                                        0x0
#define FV_BGSBUP_OFF                                       0x40

#define FB_PWRM3_VGBAPU                                     5
#define FM_PWRM3_VGBAPU                                     0x20
#define FV_VGBAPU_ON                                        0x0
#define FV_VGBAPU_OFF                                       0x20

#define FB_PWRM3_LLINEPU                                    4
#define FM_PWRM3_LLINEPU                                    0x10

#define FB_PWRM3_RLINEPU                                    3
#define FM_PWRM3_RLINEPU                                    0x8

// *** PWRM4 ***
#define FB_PWRM4_OPSUBPU                                    4
#define FM_PWRM4_OPSUBPU                                    0x10

#define FB_PWRM4_OPDACLPU                                   3
#define FM_PWRM4_OPDACLPU                                   0x8

#define FB_PWRM4_OPDACRPU                                   2
#define FM_PWRM4_OPDACRPU                                   0x4

#define FB_PWRM4_OPSPKLPU                                   1
#define FM_PWRM4_OPSPKLPU                                   0x2

#define FB_PWRM4_OPSPKRPU                                   0
#define FM_PWRM4_OPSPKRPU                                   0x1

// *** I2SIDCTL ***
#define FB_I2SIDCTL_I2SI3DCTL                               4
#define FM_I2SIDCTL_I2SI3DCTL                               0x30

#define FB_I2SIDCTL_I2SI2DCTL                               2
#define FM_I2SIDCTL_I2SI2DCTL                               0xC

#define FB_I2SIDCTL_I2SI1DCTL                               0
#define FM_I2SIDCTL_I2SI1DCTL                               0x3

// *** I2SODCTL ***
#define FB_I2SODCTL_I2SO3DCTL                               4
#define FM_I2SODCTL_I2SO3DCTL                               0x30

#define FB_I2SODCTL_I2SO2DCTL                               2
#define FM_I2SODCTL_I2SO2DCTL                               0xC

#define FB_I2SODCTL_I2SO1DCTL                               0
#define FM_I2SODCTL_I2SO1DCTL                               0x3

// *** AUDIOMUX1 ***
#define FB_AUDIOMUX1_ASRCIMUX                               6
#define FM_AUDIOMUX1_ASRCIMUX                               0xC0
#define FV_ASRCIMUX_NONE                                    0x0
#define FV_ASRCIMUX_I2S1                                    0x40
#define FV_ASRCIMUX_I2S2                                    0x80
#define FV_ASRCIMUX_I2S3                                    0xC0

#define FB_AUDIOMUX1_I2S2MUX                                3
#define FM_AUDIOMUX1_I2S2MUX                                0x38
#define FV_I2S2MUX_I2S1                                     0x0
#define FV_I2S2MUX_I2S2                                     0x8
#define FV_I2S2MUX_I2S3                                     0x10
#define FV_I2S2MUX_ADC_DMIC                                 0x18
#define FV_I2S2MUX_DMIC2                                    0x20
#define FV_I2S2MUX_CLASSD_DSP                               0x28
#define FV_I2S2MUX_DAC_DSP                                  0x30
#define FV_I2S2MUX_SUB_DSP                                  0x38

#define FB_AUDIOMUX1_I2S1MUX                                0
#define FM_AUDIOMUX1_I2S1MUX                                0x7
#define FV_I2S1MUX_I2S1                                     0x0
#define FV_I2S1MUX_I2S2                                     0x1
#define FV_I2S1MUX_I2S3                                     0x2
#define FV_I2S1MUX_ADC_DMIC                                 0x3
#define FV_I2S1MUX_DMIC2                                    0x4
#define FV_I2S1MUX_CLASSD_DSP                               0x5
#define FV_I2S1MUX_DAC_DSP                                  0x6
#define FV_I2S1MUX_SUB_DSP                                  0x7

#define AUDIOMUX1_I2SMUX_I2S1                               0x0
#define AUDIOMUX1_I2SMUX_I2S2                               0x1
#define AUDIOMUX1_I2SMUX_I2S3                               0x2
#define AUDIOMUX1_I2SMUX_ADC_DMIC                           0x3
#define AUDIOMUX1_I2SMUX_DMIC2                              0x4
#define AUDIOMUX1_I2SMUX_CLASSD_DSP                         0x5
#define AUDIOMUX1_I2SMUX_DAC_DSP                            0x6
#define AUDIOMUX1_I2SMUX_SUB_DSP                            0x7

// *** AUDIOMUX2 ***
#define FB_AUDIOMUX2_ASRCOMUX                               6
#define FM_AUDIOMUX2_ASRCOMUX                               0xC0
#define FV_ASRCOMUX_NONE                                    0x0
#define FV_ASRCOMUX_I2S1                                    0x40
#define FV_ASRCOMUX_I2S2                                    0x80
#define FV_ASRCOMUX_I2S3                                    0xC0

#define FB_AUDIOMUX2_DACMUX                                 3
#define FM_AUDIOMUX2_DACMUX                                 0x38
#define FV_DACMUX_I2S1                                      0x0
#define FV_DACMUX_I2S2                                      0x8
#define FV_DACMUX_I2S3                                      0x10
#define FV_DACMUX_ADC_DMIC                                  0x18
#define FV_DACMUX_DMIC2                                     0x20
#define FV_DACMUX_CLASSD_DSP                                0x28
#define FV_DACMUX_DAC_DSP                                   0x30
#define FV_DACMUX_SUB_DSP                                   0x38

#define FB_AUDIOMUX2_I2S3MUX                                0
#define FM_AUDIOMUX2_I2S3MUX                                0x7
#define FV_I2S3MUX_I2S1                                     0x0
#define FV_I2S3MUX_I2S2                                     0x1
#define FV_I2S3MUX_I2S3                                     0x2
#define FV_I2S3MUX_ADC_DMIC                                 0x3
#define FV_I2S3MUX_DMIC2                                    0x4
#define FV_I2S3MUX_CLASSD_DSP                               0x5
#define FV_I2S3MUX_DAC_DSP                                  0x6
#define FV_I2S3MUX_SUB_DSP                                  0x7

// *** AUDIOMUX3 ***
#define FB_AUDIOMUX3_SUBMUX                                 3
#define FM_AUDIOMUX3_SUBMUX                                 0xF8
#define FV_SUBMUX_I2S1_L                                    0x0
#define FV_SUBMUX_I2S1_R                                    0x8
#define FV_SUBMUX_I2S1_LR                                   0x10
#define FV_SUBMUX_I2S2_L                                    0x18
#define FV_SUBMUX_I2S2_R                                    0x20
#define FV_SUBMUX_I2S2_LR                                   0x28
#define FV_SUBMUX_I2S3_L                                    0x30
#define FV_SUBMUX_I2S3_R                                    0x38
#define FV_SUBMUX_I2S3_LR                                   0x40
#define FV_SUBMUX_ADC_DMIC_L                                0x48
#define FV_SUBMUX_ADC_DMIC_R                                0x50
#define FV_SUBMUX_ADC_DMIC_LR                               0x58
#define FV_SUBMUX_DMIC_L                                    0x60
#define FV_SUBMUX_DMIC_R                                    0x68
#define FV_SUBMUX_DMIC_LR                                   0x70
#define FV_SUBMUX_CLASSD_DSP_L                              0x78
#define FV_SUBMUX_CLASSD_DSP_R                              0x80
#define FV_SUBMUX_CLASSD_DSP_LR                             0x88

#define FB_AUDIOMUX3_CLSSDMUX                               0
#define FM_AUDIOMUX3_CLSSDMUX                               0x7
#define FV_CLSSDMUX_I2S1                                    0x0
#define FV_CLSSDMUX_I2S2                                    0x1
#define FV_CLSSDMUX_I2S3                                    0x2
#define FV_CLSSDMUX_ADC_DMIC                                0x3
#define FV_CLSSDMUX_DMIC2                                   0x4
#define FV_CLSSDMUX_CLASSD_DSP                              0x5
#define FV_CLSSDMUX_DAC_DSP                                 0x6
#define FV_CLSSDMUX_SUB_DSP                                 0x7

// *** HSDCTL1 ***
#define FB_HSDCTL1_HPJKTYPE                                 7
#define FM_HSDCTL1_HPJKTYPE                                 0x80

#define FB_HSDCTL1_CON_DET_PWD                              6
#define FM_HSDCTL1_CON_DET_PWD                              0x40

#define FB_HSDCTL1_DETCYC                                   4
#define FM_HSDCTL1_DETCYC                                   0x30

#define FB_HSDCTL1_HPDLYBYP                                 3
#define FM_HSDCTL1_HPDLYBYP                                 0x8

#define FB_HSDCTL1_HSDETPOL                                 2
#define FM_HSDCTL1_HSDETPOL                                 0x4

#define FB_HSDCTL1_HPID_EN                                  1
#define FM_HSDCTL1_HPID_EN                                  0x2

#define FB_HSDCTL1_GBLHS_EN                                 0
#define FM_HSDCTL1_GBLHS_EN                                 0x1

// *** HSDCTL2 ***
#define FB_HSDCTL2_FMICBIAS1                                6
#define FM_HSDCTL2_FMICBIAS1                                0xC0

#define FB_HSDCTL2_MB1MODE                                  5
#define FM_HSDCTL2_MB1MODE                                  0x20
#define FV_MB1MODE_AUTO                                     0x0
#define FV_MB1MODE_MANUAL                                   0x20

#define FB_HSDCTL2_FORCETRG                                 4
#define FM_HSDCTL2_FORCETRG                                 0x10

#define FB_HSDCTL2_SWMODE                                   3
#define FM_HSDCTL2_SWMODE                                   0x8

#define FB_HSDCTL2_GHSHIZ                                   2
#define FM_HSDCTL2_GHSHIZ                                   0x4

#define FB_HSDCTL2_FPLUGTYPE                                0
#define FM_HSDCTL2_FPLUGTYPE                                0x3

// *** HSDSTAT ***
#define FB_HSDSTAT_MBIAS1DRV                                5
#define FM_HSDSTAT_MBIAS1DRV                                0x60

#define FB_HSDSTAT_HSDETSTAT                                3
#define FM_HSDSTAT_HSDETSTAT                                0x8

#define FB_HSDSTAT_PLUGTYPE                                 1
#define FM_HSDSTAT_PLUGTYPE                                 0x6

#define FB_HSDSTAT_HSDETDONE                                0
#define FM_HSDSTAT_HSDETDONE                                0x1

// *** HSDDELAY ***
#define FB_HSDDELAY_T_STABLE                                0
#define FM_HSDDELAY_T_STABLE                                0x7

// *** BUTCTL ***
#define FB_BUTCTL_BPUSHSTAT                                 7
#define FM_BUTCTL_BPUSHSTAT                                 0x80

#define FB_BUTCTL_BPUSHDET                                  6
#define FM_BUTCTL_BPUSHDET                                  0x40

#define FB_BUTCTL_BPUSHEN                                   5
#define FM_BUTCTL_BPUSHEN                                   0x20

#define FB_BUTCTL_BSTABLE_L                                 3
#define FM_BUTCTL_BSTABLE_L                                 0x18

#define FB_BUTCTL_BSTABLE_S                                 0
#define FM_BUTCTL_BSTABLE_S                                 0x7

// *** CH0AIC ***
#define FB_CH0AIC_INSELL                                    6
#define FM_CH0AIC_INSELL                                    0xC0

#define FB_CH0AIC_MICBST0                                   4
#define FM_CH0AIC_MICBST0                                   0x30

#define FB_CH0AIC_LADCIN                                    2
#define FM_CH0AIC_LADCIN                                    0xC

#define FB_CH0AIC_IN_BYPS_L_SEL                             1
#define FM_CH0AIC_IN_BYPS_L_SEL                             0x2

#define FB_CH0AIC_IPCH0S                                    0
#define FM_CH0AIC_IPCH0S                                    0x1

// *** CH1AIC ***
#define FB_CH1AIC_INSELR                                    6
#define FM_CH1AIC_INSELR                                    0xC0

#define FB_CH1AIC_MICBST1                                   4
#define FM_CH1AIC_MICBST1                                   0x30

#define FB_CH1AIC_RADCIN                                    2
#define FM_CH1AIC_RADCIN                                    0xC

#define FB_CH1AIC_IN_BYPS_R_SEL                             1
#define FM_CH1AIC_IN_BYPS_R_SEL                             0x2

#define FB_CH1AIC_IPCH1S                                    0
#define FM_CH1AIC_IPCH1S                                    0x1

// *** ICTL0 ***
#define FB_ICTL0_IN1POL                                     7
#define FM_ICTL0_IN1POL                                     0x80

#define FB_ICTL0_IN0POL                                     6
#define FM_ICTL0_IN0POL                                     0x40

#define FB_ICTL0_INPCH10SEL                                 4
#define FM_ICTL0_INPCH10SEL                                 0x30

#define FB_ICTL0_IN1MUTE                                    3
#define FM_ICTL0_IN1MUTE                                    0x8

#define FB_ICTL0_IN0MUTE                                    2
#define FM_ICTL0_IN0MUTE                                    0x4

#define FB_ICTL0_IN1HP                                      1
#define FM_ICTL0_IN1HP                                      0x2

#define FB_ICTL0_IN0HP                                      0
#define FM_ICTL0_IN0HP                                      0x1

// *** ICTL1 ***
#define FB_ICTL1_IN3POL                                     7
#define FM_ICTL1_IN3POL                                     0x80

#define FB_ICTL1_IN2POL                                     6
#define FM_ICTL1_IN2POL                                     0x40

#define FB_ICTL1_INPCH32SEL                                 4
#define FM_ICTL1_INPCH32SEL                                 0x30

#define FB_ICTL1_IN3MUTE                                    3
#define FM_ICTL1_IN3MUTE                                    0x8

#define FB_ICTL1_IN2MUTE                                    2
#define FM_ICTL1_IN2MUTE                                    0x4

#define FB_ICTL1_IN3HP                                      1
#define FM_ICTL1_IN3HP                                      0x2

#define FB_ICTL1_IN2HP                                      0
#define FM_ICTL1_IN2HP                                      0x1

// *** MICBIAS ***
#define FB_MICBIAS_MICBOV2                                  4
#define FM_MICBIAS_MICBOV2                                  0x30

#define FB_MICBIAS_MICBOV1                                  6
#define FM_MICBIAS_MICBOV1                                  0xC0

#define FB_MICBIAS_SPARE1                                   2
#define FM_MICBIAS_SPARE1                                   0xC

#define FB_MICBIAS_SPARE2                                   0
#define FM_MICBIAS_SPARE2                                   0x3

// *** PGAZ ***
#define FB_PGAZ_INHPOR                                      1
#define FM_PGAZ_INHPOR                                      0x2

#define FB_PGAZ_TOEN                                        0
#define FM_PGAZ_TOEN                                        0x1

// *** ASRCILVOL ***
#define FB_ASRCILVOL_ASRCILVOL                              0
#define FM_ASRCILVOL_ASRCILVOL                              0xFF

// *** ASRCIRVOL ***
#define FB_ASRCIRVOL_ASRCIRVOL                              0
#define FM_ASRCIRVOL_ASRCIRVOL                              0xFF

// *** ASRCOLVOL ***
#define FB_ASRCOLVOL_ASRCOLVOL                              0
#define FM_ASRCOLVOL_ASRCOLVOL                              0xFF

// *** ASRCORVOL ***
#define FB_ASRCORVOL_ASRCOLVOL                              0
#define FM_ASRCORVOL_ASRCOLVOL                              0xFF

// *** IVOLCTLU ***
#define FB_IVOLCTLU_IFADE                                   3
#define FM_IVOLCTLU_IFADE                                   0x8

#define FB_IVOLCTLU_INPVOLU                                 2
#define FM_IVOLCTLU_INPVOLU                                 0x4

#define FB_IVOLCTLU_PGAVOLU                                 1
#define FM_IVOLCTLU_PGAVOLU                                 0x2

#define FB_IVOLCTLU_ASRCVOLU                                0
#define FM_IVOLCTLU_ASRCVOLU                                0x1

// *** ALCCTL0 ***
#define FB_ALCCTL0_ALCMODE                                  7
#define FM_ALCCTL0_ALCMODE                                  0x80

#define FB_ALCCTL0_ALCREF                                   4
#define FM_ALCCTL0_ALCREF                                   0x70

#define FB_ALCCTL0_ALCEN3                                   3
#define FM_ALCCTL0_ALCEN3                                   0x8

#define FB_ALCCTL0_ALCEN2                                   2
#define FM_ALCCTL0_ALCEN2                                   0x4

#define FB_ALCCTL0_ALCEN1                                   1
#define FM_ALCCTL0_ALCEN1                                   0x2

#define FB_ALCCTL0_ALCEN0                                   0
#define FM_ALCCTL0_ALCEN0                                   0x1

// *** ALCCTL1 ***
#define FB_ALCCTL1_MAXGAIN                                  4
#define FM_ALCCTL1_MAXGAIN                                  0x70

#define FB_ALCCTL1_ALCL                                     0
#define FM_ALCCTL1_ALCL                                     0xF

// *** ALCCTL2 ***
#define FB_ALCCTL2_ALCZC                                    7
#define FM_ALCCTL2_ALCZC                                    0x80

#define FB_ALCCTL2_MINGAIN                                  4
#define FM_ALCCTL2_MINGAIN                                  0x70

#define FB_ALCCTL2_HLD                                      0
#define FM_ALCCTL2_HLD                                      0xF

// *** ALCCTL3 ***
#define FB_ALCCTL3_DCY                                      4
#define FM_ALCCTL3_DCY                                      0xF0

#define FB_ALCCTL3_ATK                                      0
#define FM_ALCCTL3_ATK                                      0xF

// *** NGATE ***
#define FB_NGATE_NGTH                                       3
#define FM_NGATE_NGTH                                       0xF8

#define FB_NGATE_NGG                                        1
#define FM_NGATE_NGG                                        0x6

#define FB_NGATE_NGAT                                       0
#define FM_NGATE_NGAT                                       0x1

// *** DMICCTL ***
#define FB_DMICCTL_DMIC2EN                                  7
#define FM_DMICCTL_DMIC2EN                                  0x80

#define FB_DMICCTL_DMIC1EN                                  6
#define FM_DMICCTL_DMIC1EN                                  0x40

#define FB_DMICCTL_DMONO                                    4
#define FM_DMICCTL_DMONO                                    0x10

#define FB_DMICCTL_DMDCLK                                   2
#define FM_DMICCTL_DMDCLK                                   0xC

#define FB_DMICCTL_DMRATE                                   0
#define FM_DMICCTL_DMRATE                                   0x3

// *** DACCTL ***
#define FB_DACCTL_DACPOLR                                   7
#define FM_DACCTL_DACPOLR                                   0x80
#define FV_DACPOLR_NORMAL                                   0x0
#define FV_DACPOLR_INVERTED                                 0x80

#define FB_DACCTL_DACPOLL                                   6
#define FM_DACCTL_DACPOLL                                   0x40
#define FV_DACPOLL_NORMAL                                   0x0
#define FV_DACPOLL_INVERTED                                 0x40

#define FB_DACCTL_DACDITH                                   4
#define FM_DACCTL_DACDITH                                   0x30
#define FV_DACDITH_DYNAMIC_HALF                             0x0
#define FV_DACDITH_DYNAMIC_FULL                             0x10
#define FV_DACDITH_DISABLED                                 0x20
#define FV_DACDITH_STATIC                                   0x30

#define FB_DACCTL_DACMUTE                                   3
#define FM_DACCTL_DACMUTE                                   0x8
#define FV_DACMUTE_ENABLE                                   0x8
#define FV_DACMUTE_DISABLE                                  0x0

#define FB_DACCTL_DACDEM                                    2
#define FM_DACCTL_DACDEM                                    0x4
#define FV_DACDEM_ENABLE                                    0x4
#define FV_DACDEM_DISABLE                                   0x0

#define FB_DACCTL_ABYPASS                                   0
#define FM_DACCTL_ABYPASS                                   0x1

// *** SPKCTL ***
#define FB_SPKCTL_SPKPOLR                                   7
#define FM_SPKCTL_SPKPOLR                                   0x80
#define FV_SPKPOLR_NORMAL                                   0x0
#define FV_SPKPOLR_INVERTED                                 0x80

#define FB_SPKCTL_SPKPOLL                                   6
#define FM_SPKCTL_SPKPOLL                                   0x40
#define FV_SPKPOLL_NORMAL                                   0x0
#define FV_SPKPOLL_INVERTED                                 0x40

#define FB_SPKCTL_SPKMUTE                                   3
#define FM_SPKCTL_SPKMUTE                                   0x8
#define FV_SPKMUTE_ENABLE                                   0x8
#define FV_SPKMUTE_DISABLE                                  0x0

#define FB_SPKCTL_SPKDEM                                    2
#define FM_SPKCTL_SPKDEM                                    0x4
#define FV_SPKDEM_ENABLE                                    0x4
#define FV_SPKDEM_DISABLE                                   0x0

// *** SUBCTL ***
#define FB_SUBCTL_SUBPOL                                    7
#define FM_SUBCTL_SUBPOL                                    0x80

#define FB_SUBCTL_SUBMUTE                                   3
#define FM_SUBCTL_SUBMUTE                                   0x8

#define FB_SUBCTL_SUBDEM                                    2
#define FM_SUBCTL_SUBDEM                                    0x4

#define FB_SUBCTL_SUBMUX                                    1
#define FM_SUBCTL_SUBMUX                                    0x2

#define FB_SUBCTL_SUBILMDIS                                 0
#define FM_SUBCTL_SUBILMDIS                                 0x1

// *** DCCTL ***
#define FB_DCCTL_SUBDCBYP                                   7
#define FM_DCCTL_SUBDCBYP                                   0x80

#define FB_DCCTL_DACDCBYP                                   6
#define FM_DCCTL_DACDCBYP                                   0x40

#define FB_DCCTL_SPKDCBYP                                   5
#define FM_DCCTL_SPKDCBYP                                   0x20

#define FB_DCCTL_DCCOEFSEL                                  0
#define FM_DCCTL_DCCOEFSEL                                  0x7

// *** OVOLCTLU ***
#define FB_OVOLCTLU_OFADE                                   4
#define FM_OVOLCTLU_OFADE                                   0x10

#define FB_OVOLCTLU_SUBVOLU                                 3
#define FM_OVOLCTLU_SUBVOLU                                 0x8

#define FB_OVOLCTLU_MVOLU                                   2
#define FM_OVOLCTLU_MVOLU                                   0x4

#define FB_OVOLCTLU_SPKVOLU                                 1
#define FM_OVOLCTLU_SPKVOLU                                 0x2

#define FB_OVOLCTLU_HPVOLU                                  0
#define FM_OVOLCTLU_HPVOLU                                  0x1

// *** MUTEC ***
#define FB_MUTEC_ZDSTAT                                     7
#define FM_MUTEC_ZDSTAT                                     0x80

#define FB_MUTEC_ZDLEN                                      4
#define FM_MUTEC_ZDLEN                                      0x30

#define FB_MUTEC_APWD                                       3
#define FM_MUTEC_APWD                                       0x8

#define FB_MUTEC_AMUTE                                      2
#define FM_MUTEC_AMUTE                                      0x4

// *** MVOLL ***
#define FB_MVOLL_MVOL_L                                     0
#define FM_MVOLL_MVOL_L                                     0xFF

// *** MVOLR ***
#define FB_MVOLR_MVOL_R                                     0
#define FM_MVOLR_MVOL_R                                     0xFF

// *** HPVOLL ***
#define FB_HPVOLL_HPVOL_L                                   0
#define FM_HPVOLL_HPVOL_L                                   0x7F

// *** HPVOLR ***
#define FB_HPVOLR_HPVOL_R                                   0
#define FM_HPVOLR_HPVOL_R                                   0x7F

// *** SPKVOLL ***
#define FB_SPKVOLL_SPKVOL_L                                 0
#define FM_SPKVOLL_SPKVOL_L                                 0x7F

// *** SPKVOLR ***
#define FB_SPKVOLR_SPKVOL_R                                 0
#define FM_SPKVOLR_SPKVOL_R                                 0x7F

// *** SUBVOL ***
#define FB_SUBVOL_SUBVOL                                    0
#define FM_SUBVOL_SUBVOL                                    0x7F

// *** COP0 ***
#define FB_COP0_COPATTEN                                    7
#define FM_COP0_COPATTEN                                    0x80

#define FB_COP0_COPGAIN                                     6
#define FM_COP0_COPGAIN                                     0x40

#define FB_COP0_HDELTAEN                                    5
#define FM_COP0_HDELTAEN                                    0x20

#define FB_COP0_COPTARGET                                   0
#define FM_COP0_COPTARGET                                   0x1F

// *** COP1 ***
#define FB_COP1_HDCOMPMODE                                  6
#define FM_COP1_HDCOMPMODE                                  0x40

#define FB_COP1_AVGLENGTH                                   2
#define FM_COP1_AVGLENGTH                                   0x3C

#define FB_COP1_MONRATE                                     0
#define FM_COP1_MONRATE                                     0x3

// *** COPSTAT ***
#define FB_COPSTAT_HDELTADET                                7
#define FM_COPSTAT_HDELTADET                                0x80

#define FB_COPSTAT_UV                                       6
#define FM_COPSTAT_UV                                       0x40

#define FB_COPSTAT_COPADJ                                   0
#define FM_COPSTAT_COPADJ                                   0x3F

// *** PWM0 ***
#define FB_PWM0_SCTO                                        6
#define FM_PWM0_SCTO                                        0xC0

#define FB_PWM0_UVLO                                        5
#define FM_PWM0_UVLO                                        0x20

#define FB_PWM0_BFDIS                                       3
#define FM_PWM0_BFDIS                                       0x8

#define FB_PWM0_PWMMODE                                     2
#define FM_PWM0_PWMMODE                                     0x4

#define FB_PWM0_NOOFFSET                                    0
#define FM_PWM0_NOOFFSET                                    0x1

// *** PWM1 ***
#define FB_PWM1_DITHPOS                                     4
#define FM_PWM1_DITHPOS                                     0x70

#define FB_PWM1_DYNDITH                                     1
#define FM_PWM1_DYNDITH                                     0x2

#define FB_PWM1_DITHDIS                                     0
#define FM_PWM1_DITHDIS                                     0x1

// *** PWM2 ***
// *** PWM3 ***
#define FB_PWM3_PWMMUX                                      6
#define FM_PWM3_PWMMUX                                      0xC0

#define FB_PWM3_CVALUE                                      0
#define FM_PWM3_CVALUE                                      0x7

// *** HPSW ***
#define FB_HPSW_HPDETSTATE                                  4
#define FM_HPSW_HPDETSTATE                                  0x10

#define FB_HPSW_HPSWEN                                      2
#define FM_HPSW_HPSWEN                                      0xC

#define FB_HPSW_HPSWPOL                                     1
#define FM_HPSW_HPSWPOL                                     0x2

#define FB_HPSW_TSDEN                                       0
#define FM_HPSW_TSDEN                                       0x1

// *** THERMTS ***
#define FB_THERMTS_TRIPHS                                   7
#define FM_THERMTS_TRIPHS                                   0x80

#define FB_THERMTS_TRIPLS                                   6
#define FM_THERMTS_TRIPLS                                   0x40

#define FB_THERMTS_TRIPSPLIT                                4
#define FM_THERMTS_TRIPSPLIT                                0x30

#define FB_THERMTS_TRIPSHIFT                                2
#define FM_THERMTS_TRIPSHIFT                                0xC

#define FB_THERMTS_TSPOLL                                   0
#define FM_THERMTS_TSPOLL                                   0x3

// *** THERMSPK1 ***
#define FB_THERMSPK1_FORCEPWD                               7
#define FM_THERMSPK1_FORCEPWD                               0x80

#define FB_THERMSPK1_INSTCUTMODE                            6
#define FM_THERMSPK1_INSTCUTMODE                            0x40

#define FB_THERMSPK1_INCRATIO                               4
#define FM_THERMSPK1_INCRATIO                               0x30

#define FB_THERMSPK1_INCSTEP                                2
#define FM_THERMSPK1_INCSTEP                                0xC

#define FB_THERMSPK1_DECSTEP                                0
#define FM_THERMSPK1_DECSTEP                                0x3

// *** THERMSTAT ***
#define FB_THERMSTAT_FPWDS                                  7
#define FM_THERMSTAT_FPWDS                                  0x80

#define FB_THERMSTAT_VOLSTAT                                0
#define FM_THERMSTAT_VOLSTAT                                0x7F

// *** SCSTAT ***
#define FB_SCSTAT_ESDF                                      3
#define FM_SCSTAT_ESDF                                      0x18

#define FB_SCSTAT_CPF                                       2
#define FM_SCSTAT_CPF                                       0x4

#define FB_SCSTAT_CLSDF                                     0
#define FM_SCSTAT_CLSDF                                     0x3

// *** SDMON ***
#define FB_SDMON_SDFORCE                                    7
#define FM_SDMON_SDFORCE                                    0x80

#define FB_SDMON_SDVALUE                                    0
#define FM_SDMON_SDVALUE                                    0x1F

// *** SPKEQFILT ***
#define FB_SPKEQFILT_EQ2EN                                  7
#define FM_SPKEQFILT_EQ2EN                                  0x80
#define FV_EQ2EN_ENABLE                                     0x80
#define FV_EQ2EN_DISABLE                                    0x0

#define FB_SPKEQFILT_EQ2BE                                  4
#define FM_SPKEQFILT_EQ2BE                                  0x70

#define FB_SPKEQFILT_EQ1EN                                  3
#define FM_SPKEQFILT_EQ1EN                                  0x8
#define FV_EQ1EN_ENABLE                                     0x8
#define FV_EQ1EN_DISABLE                                    0x0

#define FB_SPKEQFILT_EQ1BE                                  0
#define FM_SPKEQFILT_EQ1BE                                  0x7

#define SPKEQFILT_EQEN_ENABLE                               0x1
#define SPKEQFILT_EQEN_DISABLE                              0x0

// *** SPKCRWDL ***
#define FB_SPKCRWDL_WDATA_L                                 0
#define FM_SPKCRWDL_WDATA_L                                 0xFF

// *** SPKCRWDM ***
#define FB_SPKCRWDM_WDATA_M                                 0
#define FM_SPKCRWDM_WDATA_M                                 0xFF

// *** SPKCRWDH ***
#define FB_SPKCRWDH_WDATA_H                                 0
#define FM_SPKCRWDH_WDATA_H                                 0xFF

// *** SPKCRRDL ***
#define FB_SPKCRRDL_RDATA_L                                 0
#define FM_SPKCRRDL_RDATA_L                                 0xFF

// *** SPKCRRDM ***
#define FB_SPKCRRDM_RDATA_M                                 0
#define FM_SPKCRRDM_RDATA_M                                 0xFF

// *** SPKCRRDH ***
#define FB_SPKCRRDH_RDATA_H                                 0
#define FM_SPKCRRDH_RDATA_H                                 0xFF

// *** SPKCRADD ***
#define FB_SPKCRADD_ADDRESS                                 0
#define FM_SPKCRADD_ADDRESS                                 0xFF

// *** SPKCRS ***
#define FB_SPKCRS_ACCSTAT                                   7
#define FM_SPKCRS_ACCSTAT                                   0x80

// *** SPKMBCEN ***
#define FB_SPKMBCEN_MBCEN3                                  2
#define FM_SPKMBCEN_MBCEN3                                  0x4
#define FV_MBCEN3_ENABLE                                    0x4
#define FV_MBCEN3_DISABLE                                   0x0

#define FB_SPKMBCEN_MBCEN2                                  1
#define FM_SPKMBCEN_MBCEN2                                  0x2
#define FV_MBCEN2_ENABLE                                    0x2
#define FV_MBCEN2_DISABLE                                   0x0

#define FB_SPKMBCEN_MBCEN1                                  0
#define FM_SPKMBCEN_MBCEN1                                  0x1
#define FV_MBCEN1_ENABLE                                    0x1
#define FV_MBCEN1_DISABLE                                   0x0

#define SPKMBCEN_MBCEN_ENABLE                               0x1
#define SPKMBCEN_MBCEN_DISABLE                              0x0

// *** SPKMBCCTL ***
#define FB_SPKMBCCTL_LVLMODE3                               5
#define FM_SPKMBCCTL_LVLMODE3                               0x20

#define FB_SPKMBCCTL_WINSEL3                                4
#define FM_SPKMBCCTL_WINSEL3                                0x10

#define FB_SPKMBCCTL_LVLMODE2                               3
#define FM_SPKMBCCTL_LVLMODE2                               0x8

#define FB_SPKMBCCTL_WINSEL2                                2
#define FM_SPKMBCCTL_WINSEL2                                0x4

#define FB_SPKMBCCTL_LVLMODE1                               1
#define FM_SPKMBCCTL_LVLMODE1                               0x2

#define FB_SPKMBCCTL_WINSEL1                                0
#define FM_SPKMBCCTL_WINSEL1                                0x1

// *** SPKCLECTL ***
#define FB_SPKCLECTL_LVLMODE                                4
#define FM_SPKCLECTL_LVLMODE                                0x10

#define FB_SPKCLECTL_WINSEL                                 3
#define FM_SPKCLECTL_WINSEL                                 0x8

#define FB_SPKCLECTL_EXPEN                                  2
#define FM_SPKCLECTL_EXPEN                                  0x4
#define FV_EXPEN_ENABLE                                     0x4
#define FV_EXPEN_DISABLE                                    0x0

#define FB_SPKCLECTL_LIMEN                                  1
#define FM_SPKCLECTL_LIMEN                                  0x2
#define FV_LIMEN_ENABLE                                     0x2
#define FV_LIMEN_DISABLE                                    0x0

#define FB_SPKCLECTL_COMPEN                                 0
#define FM_SPKCLECTL_COMPEN                                 0x1
#define FV_COMPEN_ENABLE                                    0x1
#define FV_COMPEN_DISABLE                                   0x0

// *** SPKCLEMUG ***
#define FB_SPKCLEMUG_MUGAIN                                 0
#define FM_SPKCLEMUG_MUGAIN                                 0x1F

// *** SPKCOMPTHR ***
#define FB_SPKCOMPTHR_THRESH                                0
#define FM_SPKCOMPTHR_THRESH                                0xFF

// *** SPKCOMPRAT ***
#define FB_SPKCOMPRAT_RATIO                                 0
#define FM_SPKCOMPRAT_RATIO                                 0x1F

// *** SPKCOMPATKL ***
#define FB_SPKCOMPATKL_TCATKL                               0
#define FM_SPKCOMPATKL_TCATKL                               0xFF

// *** SPKCOMPATKH ***
#define FB_SPKCOMPATKH_TCATKH                               0
#define FM_SPKCOMPATKH_TCATKH                               0xFF

// *** SPKCOMPRELL ***
#define FB_SPKCOMPRELL_TCRELL                               0
#define FM_SPKCOMPRELL_TCRELL                               0xFF

// *** SPKCOMPRELH ***
#define FB_SPKCOMPRELH_TCRELH                               0
#define FM_SPKCOMPRELH_TCRELH                               0xFF

// *** SPKLIMTHR ***
#define FB_SPKLIMTHR_THRESH                                 0
#define FM_SPKLIMTHR_THRESH                                 0xFF

// *** SPKLIMTGT ***
#define FB_SPKLIMTGT_TARGET                                 0
#define FM_SPKLIMTGT_TARGET                                 0xFF

// *** SPKLIMATKL ***
#define FB_SPKLIMATKL_TCATKL                                0
#define FM_SPKLIMATKL_TCATKL                                0xFF

// *** SPKLIMATKH ***
#define FB_SPKLIMATKH_TCATKH                                0
#define FM_SPKLIMATKH_TCATKH                                0xFF

// *** SPKLIMRELL ***
#define FB_SPKLIMRELL_TCRELL                                0
#define FM_SPKLIMRELL_TCRELL                                0xFF

// *** SPKLIMRELH ***
#define FB_SPKLIMRELH_TCRELH                                0
#define FM_SPKLIMRELH_TCRELH                                0xFF

// *** SPKEXPTHR ***
#define FB_SPKEXPTHR_THRESH                                 0
#define FM_SPKEXPTHR_THRESH                                 0xFF

// *** SPKEXPRAT ***
#define FB_SPKEXPRAT_RATIO                                  0
#define FM_SPKEXPRAT_RATIO                                  0x7

// *** SPKEXPATKL ***
#define FB_SPKEXPATKL_TCATKL                                0
#define FM_SPKEXPATKL_TCATKL                                0xFF

// *** SPKEXPATKH ***
#define FB_SPKEXPATKH_TCATKH                                0
#define FM_SPKEXPATKH_TCATKH                                0xFF

// *** SPKEXPRELL ***
#define FB_SPKEXPRELL_TCRELL                                0
#define FM_SPKEXPRELL_TCRELL                                0xFF

// *** SPKEXPRELH ***
#define FB_SPKEXPRELH_TCRELH                                0
#define FM_SPKEXPRELH_TCRELH                                0xFF

// *** SPKFXCTL ***
#define FB_SPKFXCTL_3DEN                                    4
#define FM_SPKFXCTL_3DEN                                    0x10

#define FB_SPKFXCTL_TEEN                                    3
#define FM_SPKFXCTL_TEEN                                    0x8

#define FB_SPKFXCTL_TNLFBYP                                 2
#define FM_SPKFXCTL_TNLFBYP                                 0x4

#define FB_SPKFXCTL_BEEN                                    1
#define FM_SPKFXCTL_BEEN                                    0x2

#define FB_SPKFXCTL_BNLFBYP                                 0
#define FM_SPKFXCTL_BNLFBYP                                 0x1

// *** DACEQFILT ***
#define FB_DACEQFILT_EQ2EN                                  7
#define FM_DACEQFILT_EQ2EN                                  0x80
#define FV_EQ2EN_ENABLE                                     0x80
#define FV_EQ2EN_DISABLE                                    0x0

#define FB_DACEQFILT_EQ2BE                                  4
#define FM_DACEQFILT_EQ2BE                                  0x70

#define FB_DACEQFILT_EQ1EN                                  3
#define FM_DACEQFILT_EQ1EN                                  0x8
#define FV_EQ1EN_ENABLE                                     0x8
#define FV_EQ1EN_DISABLE                                    0x0

#define FB_DACEQFILT_EQ1BE                                  0
#define FM_DACEQFILT_EQ1BE                                  0x7

#define DACEQFILT_EQEN_ENABLE                               0x1
#define DACEQFILT_EQEN_DISABLE                              0x0

// *** DACCRWDL ***
#define FB_DACCRWDL_WDATA_L                                 0
#define FM_DACCRWDL_WDATA_L                                 0xFF

// *** DACCRWDM ***
#define FB_DACCRWDM_WDATA_M                                 0
#define FM_DACCRWDM_WDATA_M                                 0xFF

// *** DACCRWDH ***
#define FB_DACCRWDH_WDATA_H                                 0
#define FM_DACCRWDH_WDATA_H                                 0xFF

// *** DACCRRDL ***
#define FB_DACCRRDL_RDATA_L                                 0
#define FM_DACCRRDL_RDATA_L                                 0xFF

// *** DACCRRDM ***
#define FB_DACCRRDM_RDATA_M                                 0
#define FM_DACCRRDM_RDATA_M                                 0xFF

// *** DACCRRDH ***
#define FB_DACCRRDH_RDATA_H                                 0
#define FM_DACCRRDH_RDATA_H                                 0xFF

// *** DACCRADD ***
#define FB_DACCRADD_ADDRESS                                 0
#define FM_DACCRADD_ADDRESS                                 0xFF

// *** DACCRS ***
#define FB_DACCRS_ACCSTAT                                   7
#define FM_DACCRS_ACCSTAT                                   0x80

// *** DACMBCEN ***
#define FB_DACMBCEN_MBCEN3                                  2
#define FM_DACMBCEN_MBCEN3                                  0x4
#define FV_MBCEN3_ENABLE                                    0x4
#define FV_MBCEN3_DISABLE                                   0x0

#define FB_DACMBCEN_MBCEN2                                  1
#define FM_DACMBCEN_MBCEN2                                  0x2
#define FV_MBCEN2_ENABLE                                    0x2
#define FV_MBCEN2_DISABLE                                   0x0

#define FB_DACMBCEN_MBCEN1                                  0
#define FM_DACMBCEN_MBCEN1                                  0x1
#define FV_MBCEN1_ENABLE                                    0x1
#define FV_MBCEN1_DISABLE                                   0x0

#define DACMBCEN_MBCEN_ENABLE                               0x1
#define DACMBCEN_MBCEN_DISABLE                              0x0

// *** DACMBCCTL ***
#define FB_DACMBCCTL_LVLMODE3                               5
#define FM_DACMBCCTL_LVLMODE3                               0x20

#define FB_DACMBCCTL_WINSEL3                                4
#define FM_DACMBCCTL_WINSEL3                                0x10

#define FB_DACMBCCTL_LVLMODE2                               3
#define FM_DACMBCCTL_LVLMODE2                               0x8

#define FB_DACMBCCTL_WINSEL2                                2
#define FM_DACMBCCTL_WINSEL2                                0x4

#define FB_DACMBCCTL_LVLMODE1                               1
#define FM_DACMBCCTL_LVLMODE1                               0x2

#define FB_DACMBCCTL_WINSEL1                                0
#define FM_DACMBCCTL_WINSEL1                                0x1

// *** DACCLECTL ***
#define FB_DACCLECTL_LVLMODE                                4
#define FM_DACCLECTL_LVLMODE                                0x10

#define FB_DACCLECTL_WINSEL                                 3
#define FM_DACCLECTL_WINSEL                                 0x8

#define FB_DACCLECTL_EXPEN                                  2
#define FM_DACCLECTL_EXPEN                                  0x4
#define FV_EXPEN_ENABLE                                     0x4
#define FV_EXPEN_DISABLE                                    0x0

#define FB_DACCLECTL_LIMEN                                  1
#define FM_DACCLECTL_LIMEN                                  0x2
#define FV_LIMEN_ENABLE                                     0x2
#define FV_LIMEN_DISABLE                                    0x0

#define FB_DACCLECTL_COMPEN                                 0
#define FM_DACCLECTL_COMPEN                                 0x1
#define FV_COMPEN_ENABLE                                    0x1
#define FV_COMPEN_DISABLE                                   0x0

// *** DACCLEMUG ***
#define FB_DACCLEMUG_MUGAIN                                 0
#define FM_DACCLEMUG_MUGAIN                                 0x1F

// *** DACCOMPTHR ***
#define FB_DACCOMPTHR_THRESH                                0
#define FM_DACCOMPTHR_THRESH                                0xFF

// *** DACCOMPRAT ***
#define FB_DACCOMPRAT_RATIO                                 0
#define FM_DACCOMPRAT_RATIO                                 0x1F

// *** DACCOMPATKL ***
#define FB_DACCOMPATKL_TCATKL                               0
#define FM_DACCOMPATKL_TCATKL                               0xFF

// *** DACCOMPATKH ***
#define FB_DACCOMPATKH_TCATKH                               0
#define FM_DACCOMPATKH_TCATKH                               0xFF

// *** DACCOMPRELL ***
#define FB_DACCOMPRELL_TCRELL                               0
#define FM_DACCOMPRELL_TCRELL                               0xFF

// *** DACCOMPRELH ***
#define FB_DACCOMPRELH_TCRELH                               0
#define FM_DACCOMPRELH_TCRELH                               0xFF

// *** DACLIMTHR ***
#define FB_DACLIMTHR_THRESH                                 0
#define FM_DACLIMTHR_THRESH                                 0xFF

// *** DACLIMTGT ***
#define FB_DACLIMTGT_TARGET                                 0
#define FM_DACLIMTGT_TARGET                                 0xFF

// *** DACLIMATKL ***
#define FB_DACLIMATKL_TCATKL                                0
#define FM_DACLIMATKL_TCATKL                                0xFF

// *** DACLIMATKH ***
#define FB_DACLIMATKH_TCATKH                                0
#define FM_DACLIMATKH_TCATKH                                0xFF

// *** DACLIMRELL ***
#define FB_DACLIMRELL_TCRELL                                0
#define FM_DACLIMRELL_TCRELL                                0xFF

// *** DACLIMRELH ***
#define FB_DACLIMRELH_TCRELH                                0
#define FM_DACLIMRELH_TCRELH                                0xFF

// *** DACEXPTHR ***
#define FB_DACEXPTHR_THRESH                                 0
#define FM_DACEXPTHR_THRESH                                 0xFF

// *** DACEXPRAT ***
#define FB_DACEXPRAT_RATIO                                  0
#define FM_DACEXPRAT_RATIO                                  0x7

// *** DACEXPATKL ***
#define FB_DACEXPATKL_TCATKL                                0
#define FM_DACEXPATKL_TCATKL                                0xFF

// *** DACEXPATKH ***
#define FB_DACEXPATKH_TCATKH                                0
#define FM_DACEXPATKH_TCATKH                                0xFF

// *** DACEXPRELL ***
#define FB_DACEXPRELL_TCRELL                                0
#define FM_DACEXPRELL_TCRELL                                0xFF

// *** DACEXPRELH ***
#define FB_DACEXPRELH_TCRELH                                0
#define FM_DACEXPRELH_TCRELH                                0xFF

// *** DACFXCTL ***
#define FB_DACFXCTL_3DEN                                    4
#define FM_DACFXCTL_3DEN                                    0x10

#define FB_DACFXCTL_TEEN                                    3
#define FM_DACFXCTL_TEEN                                    0x8

#define FB_DACFXCTL_TNLFBYP                                 2
#define FM_DACFXCTL_TNLFBYP                                 0x4

#define FB_DACFXCTL_BEEN                                    1
#define FM_DACFXCTL_BEEN                                    0x2

#define FB_DACFXCTL_BNLFBYP                                 0
#define FM_DACFXCTL_BNLFBYP                                 0x1

// *** SUBEQFILT ***
#define FB_SUBEQFILT_EQ2EN                                  7
#define FM_SUBEQFILT_EQ2EN                                  0x80
#define FV_EQ2EN_ENABLE                                     0x80
#define FV_EQ2EN_DISABLE                                    0x0

#define FB_SUBEQFILT_EQ2BE                                  4
#define FM_SUBEQFILT_EQ2BE                                  0x70

#define FB_SUBEQFILT_EQ1EN                                  3
#define FM_SUBEQFILT_EQ1EN                                  0x8
#define FV_EQ1EN_ENABLE                                     0x8
#define FV_EQ1EN_DISABLE                                    0x0

#define FB_SUBEQFILT_EQ1BE                                  0
#define FM_SUBEQFILT_EQ1BE                                  0x7

#define SUBEQFILT_EQEN_ENABLE                               0x1
#define SUBEQFILT_EQEN_DISABLE                              0x0

// *** SUBCRWDL ***
#define FB_SUBCRWDL_WDATA_L                                 0
#define FM_SUBCRWDL_WDATA_L                                 0xFF

// *** SUBCRWDM ***
#define FB_SUBCRWDM_WDATA_M                                 0
#define FM_SUBCRWDM_WDATA_M                                 0xFF

// *** SUBCRWDH ***
#define FB_SUBCRWDH_WDATA_H                                 0
#define FM_SUBCRWDH_WDATA_H                                 0xFF

// *** SUBCRRDL ***
#define FB_SUBCRRDL_RDATA_L                                 0
#define FM_SUBCRRDL_RDATA_L                                 0xFF

// *** SUBCRRDM ***
#define FB_SUBCRRDM_RDATA_M                                 0
#define FM_SUBCRRDM_RDATA_M                                 0xFF

// *** SUBCRRDH ***
#define FB_SUBCRRDH_RDATA_H                                 0
#define FM_SUBCRRDH_RDATA_H                                 0xFF

// *** SUBCRADD ***
#define FB_SUBCRADD_ADDRESS                                 0
#define FM_SUBCRADD_ADDRESS                                 0xFF

// *** SUBCRS ***
#define FB_SUBCRS_ACCSTAT                                   7
#define FM_SUBCRS_ACCSTAT                                   0x80

// *** SUBMBCEN ***
#define FB_SUBMBCEN_MBCEN3                                  2
#define FM_SUBMBCEN_MBCEN3                                  0x4
#define FV_MBCEN3_ENABLE                                    0x4
#define FV_MBCEN3_DISABLE                                   0x0

#define FB_SUBMBCEN_MBCEN2                                  1
#define FM_SUBMBCEN_MBCEN2                                  0x2
#define FV_MBCEN2_ENABLE                                    0x2
#define FV_MBCEN2_DISABLE                                   0x0

#define FB_SUBMBCEN_MBCEN1                                  0
#define FM_SUBMBCEN_MBCEN1                                  0x1
#define FV_MBCEN1_ENABLE                                    0x1
#define FV_MBCEN1_DISABLE                                   0x0

#define SUBMBCEN_MBCEN_ENABLE                               0x1
#define SUBMBCEN_MBCEN_DISABLE                              0x0

// *** SUBMBCCTL ***
#define FB_SUBMBCCTL_LVLMODE3                               5
#define FM_SUBMBCCTL_LVLMODE3                               0x20

#define FB_SUBMBCCTL_WINSEL3                                4
#define FM_SUBMBCCTL_WINSEL3                                0x10

#define FB_SUBMBCCTL_LVLMODE2                               3
#define FM_SUBMBCCTL_LVLMODE2                               0x8

#define FB_SUBMBCCTL_WINSEL2                                2
#define FM_SUBMBCCTL_WINSEL2                                0x4

#define FB_SUBMBCCTL_LVLMODE1                               1
#define FM_SUBMBCCTL_LVLMODE1                               0x2

#define FB_SUBMBCCTL_WINSEL1                                0
#define FM_SUBMBCCTL_WINSEL1                                0x1

// *** SUBCLECTL ***
#define FB_SUBCLECTL_LVLMODE                                4
#define FM_SUBCLECTL_LVLMODE                                0x10

#define FB_SUBCLECTL_WINSEL                                 3
#define FM_SUBCLECTL_WINSEL                                 0x8

#define FB_SUBCLECTL_EXPEN                                  2
#define FM_SUBCLECTL_EXPEN                                  0x4
#define FV_EXPEN_ENABLE                                     0x4
#define FV_EXPEN_DISABLE                                    0x0

#define FB_SUBCLECTL_LIMEN                                  1
#define FM_SUBCLECTL_LIMEN                                  0x2
#define FV_LIMEN_ENABLE                                     0x2
#define FV_LIMEN_DISABLE                                    0x0

#define FB_SUBCLECTL_COMPEN                                 0
#define FM_SUBCLECTL_COMPEN                                 0x1
#define FV_COMPEN_ENABLE                                    0x1
#define FV_COMPEN_DISABLE                                   0x0

// *** SUBCLEMUG ***
#define FB_SUBCLEMUG_MUGAIN                                 0
#define FM_SUBCLEMUG_MUGAIN                                 0x1F

// *** SUBCOMPTHR ***
#define FB_SUBCOMPTHR_THRESH                                0
#define FM_SUBCOMPTHR_THRESH                                0xFF

// *** SUBCOMPRAT ***
#define FB_SUBCOMPRAT_RATIO                                 0
#define FM_SUBCOMPRAT_RATIO                                 0x1F

// *** SUBCOMPATKL ***
#define FB_SUBCOMPATKL_TCATKL                               0
#define FM_SUBCOMPATKL_TCATKL                               0xFF

// *** SUBCOMPATKH ***
#define FB_SUBCOMPATKH_TCATKH                               0
#define FM_SUBCOMPATKH_TCATKH                               0xFF

// *** SUBCOMPRELL ***
#define FB_SUBCOMPRELL_TCRELL                               0
#define FM_SUBCOMPRELL_TCRELL                               0xFF

// *** SUBCOMPRELH ***
#define FB_SUBCOMPRELH_TCRELH                               0
#define FM_SUBCOMPRELH_TCRELH                               0xFF

// *** SUBLIMTHR ***
#define FB_SUBLIMTHR_THRESH                                 0
#define FM_SUBLIMTHR_THRESH                                 0xFF

// *** SUBLIMTGT ***
#define FB_SUBLIMTGT_TARGET                                 0
#define FM_SUBLIMTGT_TARGET                                 0xFF

// *** SUBLIMATKL ***
#define FB_SUBLIMATKL_TCATKL                                0
#define FM_SUBLIMATKL_TCATKL                                0xFF

// *** SUBLIMATKH ***
#define FB_SUBLIMATKH_TCATKH                                0
#define FM_SUBLIMATKH_TCATKH                                0xFF

// *** SUBLIMRELL ***
#define FB_SUBLIMRELL_TCRELL                                0
#define FM_SUBLIMRELL_TCRELL                                0xFF

// *** SUBLIMRELH ***
#define FB_SUBLIMRELH_TCRELH                                0
#define FM_SUBLIMRELH_TCRELH                                0xFF

// *** SUBEXPTHR ***
#define FB_SUBEXPTHR_THRESH                                 0
#define FM_SUBEXPTHR_THRESH                                 0xFF

// *** SUBEXPRAT ***
#define FB_SUBEXPRAT_RATIO                                  0
#define FM_SUBEXPRAT_RATIO                                  0x7

// *** SUBEXPATKL ***
#define FB_SUBEXPATKL_TCATKL                                0
#define FM_SUBEXPATKL_TCATKL                                0xFF

// *** SUBEXPATKH ***
#define FB_SUBEXPATKH_TCATKH                                0
#define FM_SUBEXPATKH_TCATKH                                0xFF

// *** SUBEXPRELL ***
#define FB_SUBEXPRELL_TCRELL                                0
#define FM_SUBEXPRELL_TCRELL                                0xFF

// *** SUBEXPRELH ***
#define FB_SUBEXPRELH_TCRELH                                0
#define FM_SUBEXPRELH_TCRELH                                0xFF

// *** SUBFXCTL ***
#define FB_SUBFXCTL_TEEN                                    3
#define FM_SUBFXCTL_TEEN                                    0x8

#define FB_SUBFXCTL_TNLFBYP                                 2
#define FM_SUBFXCTL_TNLFBYP                                 0x4

#define FB_SUBFXCTL_BEEN                                    1
#define FM_SUBFXCTL_BEEN                                    0x2

#define FB_SUBFXCTL_BNLFBYP                                 0
#define FM_SUBFXCTL_BNLFBYP                                 0x1

#endif /* __REDWOODPUBLIC_H__ */
