/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This header provides index for the reset controller
 * based on hi6220 SoC.
 */
#ifndef _DT_BINDINGS_RESET_CONTROLLER_HI6220
#define _DT_BINDINGS_RESET_CONTROLLER_HI6220

#define PERIPH_RSTDIS0_MMC0             0x000
#define PERIPH_RSTDIS0_MMC1             0x001
#define PERIPH_RSTDIS0_MMC2             0x002
#define PERIPH_RSTDIS0_NANDC            0x003
#define PERIPH_RSTDIS0_USBOTG_BUS       0x004
#define PERIPH_RSTDIS0_POR_PICOPHY      0x005
#define PERIPH_RSTDIS0_USBOTG           0x006
#define PERIPH_RSTDIS0_USBOTG_32K       0x007
#define PERIPH_RSTDIS1_HIFI             0x100
#define PERIPH_RSTDIS1_DIGACODEC        0x105
#define PERIPH_RSTEN2_IPF               0x200
#define PERIPH_RSTEN2_SOCP              0x201
#define PERIPH_RSTEN2_DMAC              0x202
#define PERIPH_RSTEN2_SECENG            0x203
#define PERIPH_RSTEN2_ABB               0x204
#define PERIPH_RSTEN2_HPM0              0x205
#define PERIPH_RSTEN2_HPM1              0x206
#define PERIPH_RSTEN2_HPM2              0x207
#define PERIPH_RSTEN2_HPM3              0x208
#define PERIPH_RSTEN3_CSSYS             0x300
#define PERIPH_RSTEN3_I2C0              0x301
#define PERIPH_RSTEN3_I2C1              0x302
#define PERIPH_RSTEN3_I2C2              0x303
#define PERIPH_RSTEN3_I2C3              0x304
#define PERIPH_RSTEN3_UART1             0x305
#define PERIPH_RSTEN3_UART2             0x306
#define PERIPH_RSTEN3_UART3             0x307
#define PERIPH_RSTEN3_UART4             0x308
#define PERIPH_RSTEN3_SSP               0x309
#define PERIPH_RSTEN3_PWM               0x30a
#define PERIPH_RSTEN3_BLPWM             0x30b
#define PERIPH_RSTEN3_TSENSOR           0x30c
#define PERIPH_RSTEN3_DAPB              0x312
#define PERIPH_RSTEN3_HKADC             0x313
#define PERIPH_RSTEN3_CODEC_SSI         0x314
#define PERIPH_RSTEN3_PMUSSI1           0x316
#define PERIPH_RSTEN8_RS0               0x400
#define PERIPH_RSTEN8_RS2               0x401
#define PERIPH_RSTEN8_RS3               0x402
#define PERIPH_RSTEN8_MS0               0x403
#define PERIPH_RSTEN8_MS2               0x405
#define PERIPH_RSTEN8_XG2RAM0           0x406
#define PERIPH_RSTEN8_X2SRAM_TZMA       0x407
#define PERIPH_RSTEN8_SRAM              0x408
#define PERIPH_RSTEN8_HARQ              0x40a
#define PERIPH_RSTEN8_DDRC              0x40c
#define PERIPH_RSTEN8_DDRC_APB          0x40d
#define PERIPH_RSTEN8_DDRPACK_APB       0x40e
#define PERIPH_RSTEN8_DDRT              0x411
#define PERIPH_RSDIST9_CARM_DAP         0x500
#define PERIPH_RSDIST9_CARM_ATB         0x501
#define PERIPH_RSDIST9_CARM_LBUS        0x502
#define PERIPH_RSDIST9_CARM_POR         0x503
#define PERIPH_RSDIST9_CARM_CORE        0x504
#define PERIPH_RSDIST9_CARM_DBG         0x505
#define PERIPH_RSDIST9_CARM_L2          0x506
#define PERIPH_RSDIST9_CARM_SOCDBG      0x507
#define PERIPH_RSDIST9_CARM_ETM         0x508

#define MEDIA_G3D                       0
#define MEDIA_CODEC_VPU                 2
#define MEDIA_CODEC_JPEG                3
#define MEDIA_ISP                       4
#define MEDIA_ADE                       5
#define MEDIA_MMU                       6
#define MEDIA_XG2RAM1                   7

#endif /*_DT_BINDINGS_RESET_CONTROLLER_HI6220*/
