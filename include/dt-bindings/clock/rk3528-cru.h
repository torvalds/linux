/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3528_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3528_H

/* cru-clocks indices */

/* core clocks */
#define PLL_APLL                       1
#define PLL_CPLL                       2
#define PLL_GPLL                       3
#define PLL_PPLL                       4
#define PLL_DPLL                       5
#define ARMCLK                         6

#define XIN_OSC0_HALF                  8
#define CLK_MATRIX_50M_SRC             9
#define CLK_MATRIX_100M_SRC            10
#define CLK_MATRIX_150M_SRC            11
#define CLK_MATRIX_200M_SRC            12
#define CLK_MATRIX_250M_SRC            13
#define CLK_MATRIX_300M_SRC            14
#define CLK_MATRIX_339M_SRC            15
#define CLK_MATRIX_400M_SRC            16
#define CLK_MATRIX_500M_SRC            17
#define CLK_MATRIX_600M_SRC            18
#define CLK_UART0_SRC                  19
#define CLK_UART0_FRAC                 20
#define SCLK_UART0                     21
#define CLK_UART1_SRC                  22
#define CLK_UART1_FRAC                 23
#define SCLK_UART1                     24
#define CLK_UART2_SRC                  25
#define CLK_UART2_FRAC                 26
#define SCLK_UART2                     27
#define CLK_UART3_SRC                  28
#define CLK_UART3_FRAC                 29
#define SCLK_UART3                     30
#define CLK_UART4_SRC                  31
#define CLK_UART4_FRAC                 32
#define SCLK_UART4                     33
#define CLK_UART5_SRC                  34
#define CLK_UART5_FRAC                 35
#define SCLK_UART5                     36
#define CLK_UART6_SRC                  37
#define CLK_UART6_FRAC                 38
#define SCLK_UART6                     39
#define CLK_UART7_SRC                  40
#define CLK_UART7_FRAC                 41
#define SCLK_UART7                     42
#define CLK_I2S0_2CH_SRC               43
#define CLK_I2S0_2CH_FRAC              44
#define MCLK_I2S0_2CH_SAI_SRC          45
#define CLK_I2S3_8CH_SRC               46
#define CLK_I2S3_8CH_FRAC              47
#define MCLK_I2S3_8CH_SAI_SRC          48
#define CLK_I2S1_8CH_SRC               49
#define CLK_I2S1_8CH_FRAC              50
#define MCLK_I2S1_8CH_SAI_SRC          51
#define CLK_I2S2_2CH_SRC               52
#define CLK_I2S2_2CH_FRAC              53
#define MCLK_I2S2_2CH_SAI_SRC          54
#define CLK_SPDIF_SRC                  55
#define CLK_SPDIF_FRAC                 56
#define MCLK_SPDIF_SRC                 57
#define DCLK_VOP_SRC0                  58
#define DCLK_VOP_SRC1                  59
#define CLK_HSM                        60
#define CLK_CORE_SRC_ACS               63
#define CLK_CORE_SRC_PVTMUX            65
#define CLK_CORE_SRC                   66
#define CLK_CORE                       67
#define ACLK_M_CORE_BIU                68
#define CLK_CORE_PVTPLL_SRC            69
#define PCLK_DBG                       70
#define SWCLKTCK                       71
#define CLK_SCANHS_CORE                72
#define CLK_SCANHS_ACLKM_CORE          73
#define CLK_SCANHS_PCLK_DBG            74
#define CLK_SCANHS_PCLK_CPU_BIU        76
#define PCLK_CPU_ROOT                  77
#define PCLK_CORE_GRF                  78
#define PCLK_DAPLITE_BIU               79
#define PCLK_CPU_BIU                   80
#define CLK_REF_PVTPLL_CORE            81
#define ACLK_BUS_VOPGL_ROOT            85
#define ACLK_BUS_VOPGL_BIU             86
#define ACLK_BUS_H_ROOT                87
#define ACLK_BUS_H_BIU                 88
#define ACLK_BUS_ROOT                  89
#define HCLK_BUS_ROOT                  90
#define PCLK_BUS_ROOT                  91
#define ACLK_BUS_M_ROOT                92
#define ACLK_SYSMEM_BIU                93
#define CLK_TIMER_ROOT                 95
#define ACLK_BUS_BIU                   96
#define HCLK_BUS_BIU                   97
#define PCLK_BUS_BIU                   98
#define PCLK_DFT2APB                   99
#define PCLK_BUS_GRF                   100
#define ACLK_BUS_M_BIU                 101
#define ACLK_GIC                       102
#define ACLK_SPINLOCK                  103
#define ACLK_DMAC                      104
#define PCLK_TIMER                     105
#define CLK_TIMER0                     106
#define CLK_TIMER1                     107
#define CLK_TIMER2                     108
#define CLK_TIMER3                     109
#define CLK_TIMER4                     110
#define CLK_TIMER5                     111
#define PCLK_JDBCK_DAP                 112
#define CLK_JDBCK_DAP                  113
#define PCLK_WDT_NS                    114
#define TCLK_WDT_NS                    115
#define HCLK_TRNG_NS                   116
#define PCLK_UART0                     117
#define PCLK_DMA2DDR                   123
#define ACLK_DMA2DDR                   124
#define PCLK_PWM0                      126
#define CLK_PWM0                       127
#define CLK_CAPTURE_PWM0               128
#define PCLK_PWM1                      129
#define CLK_PWM1                       130
#define CLK_CAPTURE_PWM1               131
#define PCLK_SCR                       134
#define ACLK_DCF                       135
#define PCLK_INTMUX                    138
#define CLK_PPLL_I                     141
#define CLK_PPLL_MUX                   142
#define CLK_PPLL_100M_MATRIX           143
#define CLK_PPLL_50M_MATRIX            144
#define CLK_REF_PCIE_INNER_PHY         145
#define CLK_REF_PCIE_100M_PHY          146
#define ACLK_VPU_L_ROOT                147
#define CLK_GMAC1_VPU_25M              148
#define CLK_PPLL_125M_MATRIX           149
#define ACLK_VPU_ROOT                  150
#define HCLK_VPU_ROOT                  151
#define PCLK_VPU_ROOT                  152
#define ACLK_VPU_BIU                   153
#define HCLK_VPU_BIU                   154
#define PCLK_VPU_BIU                   155
#define ACLK_VPU                       156
#define HCLK_VPU                       157
#define PCLK_CRU_PCIE                  158
#define PCLK_VPU_GRF                   159
#define HCLK_SFC                       160
#define SCLK_SFC                       161
#define CCLK_SRC_EMMC                  163
#define HCLK_EMMC                      164
#define ACLK_EMMC                      165
#define BCLK_EMMC                      166
#define TCLK_EMMC                      167
#define PCLK_GPIO1                     168
#define DBCLK_GPIO1                    169
#define ACLK_VPU_L_BIU                 172
#define PCLK_VPU_IOC                   173
#define HCLK_SAI_I2S0                  174
#define MCLK_SAI_I2S0                  175
#define HCLK_SAI_I2S2                  176
#define MCLK_SAI_I2S2                  177
#define PCLK_ACODEC                    178
#define MCLK_ACODEC_TX                 179
#define PCLK_GPIO3                     186
#define DBCLK_GPIO3                    187
#define PCLK_SPI1                      189
#define CLK_SPI1                       190
#define SCLK_IN_SPI1                   191
#define PCLK_UART2                     192
#define PCLK_UART5                     194
#define PCLK_UART6                     196
#define PCLK_UART7                     198
#define PCLK_I2C3                      200
#define CLK_I2C3                       201
#define PCLK_I2C5                      202
#define CLK_I2C5                       203
#define PCLK_I2C6                      204
#define CLK_I2C6                       205
#define ACLK_MAC_VPU                   206
#define PCLK_MAC_VPU                   207
#define CLK_GMAC1_RMII_VPU             209
#define CLK_GMAC1_SRC_VPU              210
#define PCLK_PCIE                      215
#define CLK_PCIE_AUX                   216
#define ACLK_PCIE                      217
#define HCLK_PCIE_SLV                  218
#define HCLK_PCIE_DBI                  219
#define PCLK_PCIE_PHY                  220
#define PCLK_PIPE_GRF                  221
#define CLK_PIPE_USB3OTG_COMBO         230
#define CLK_UTMI_USB3OTG               232
#define CLK_PCIE_PIPE_PHY              235
#define CCLK_SRC_SDIO0                 240
#define HCLK_SDIO0                     241
#define CCLK_SRC_SDIO1                 244
#define HCLK_SDIO1                     245
#define CLK_TS_0                       246
#define CLK_TS_1                       247
#define PCLK_CAN2                      250
#define CLK_CAN2                       251
#define PCLK_CAN3                      252
#define CLK_CAN3                       253
#define PCLK_SARADC                    256
#define CLK_SARADC                     257
#define PCLK_TSADC                     258
#define CLK_TSADC                      259
#define CLK_TSADC_TSEN                 260
#define ACLK_USB3OTG                   261
#define CLK_REF_USB3OTG                262
#define CLK_SUSPEND_USB3OTG            263
#define ACLK_GPU_ROOT                  269
#define PCLK_GPU_ROOT                  270
#define ACLK_GPU_BIU                   271
#define PCLK_GPU_BIU                   272
#define ACLK_GPU                       273
#define CLK_GPU_PVTPLL_SRC             274
#define ACLK_GPU_MALI                  275
#define HCLK_RKVENC_ROOT               281
#define ACLK_RKVENC_ROOT               282
#define PCLK_RKVENC_ROOT               283
#define HCLK_RKVENC_BIU                284
#define ACLK_RKVENC_BIU                285
#define PCLK_RKVENC_BIU                286
#define HCLK_RKVENC                    287
#define ACLK_RKVENC                    288
#define CLK_CORE_RKVENC                289
#define HCLK_SAI_I2S1                  290
#define MCLK_SAI_I2S1                  291
#define PCLK_I2C1                      292
#define CLK_I2C1                       293
#define PCLK_I2C0                      294
#define CLK_I2C0                       295
#define CLK_UART_JTAG                  296
#define PCLK_SPI0                      297
#define CLK_SPI0                       298
#define SCLK_IN_SPI0                   299
#define PCLK_GPIO4                     300
#define DBCLK_GPIO4                    301
#define PCLK_RKVENC_IOC                302
#define HCLK_SPDIF                     308
#define MCLK_SPDIF                     309
#define HCLK_PDM                       310
#define MCLK_PDM                       311
#define PCLK_UART1                     315
#define PCLK_UART3                     317
#define PCLK_RKVENC_GRF                319
#define PCLK_CAN0                      320
#define CLK_CAN0                       321
#define PCLK_CAN1                      322
#define CLK_CAN1                       323
#define ACLK_VO_ROOT                   324
#define HCLK_VO_ROOT                   325
#define PCLK_VO_ROOT                   326
#define ACLK_VO_BIU                    327
#define HCLK_VO_BIU                    328
#define PCLK_VO_BIU                    329
#define HCLK_RGA2E                     330
#define ACLK_RGA2E                     331
#define CLK_CORE_RGA2E                 332
#define HCLK_VDPP                      333
#define ACLK_VDPP                      334
#define CLK_CORE_VDPP                  335
#define PCLK_VO_GRF                    336
#define PCLK_CRU                       337
#define ACLK_VOP_ROOT                  338
#define ACLK_VOP_BIU                   339
#define HCLK_VOP                       340
#define DCLK_VOP0                      341
#define DCLK_VOP1                      342
#define ACLK_VOP                       343
#define PCLK_HDMI                      344
#define CLK_SFR_HDMI                   345
#define CLK_CEC_HDMI                   346
#define CLK_SPDIF_HDMI                 347
#define CLK_HDMIPHY_TMDSSRC            348
#define CLK_HDMIPHY_PREP               349
#define PCLK_HDMIPHY                   352
#define HCLK_HDCP_KEY                  354
#define ACLK_HDCP                      355
#define HCLK_HDCP                      356
#define PCLK_HDCP                      357
#define HCLK_CVBS                      358
#define DCLK_CVBS                      359
#define DCLK_4X_CVBS                   360
#define ACLK_JPEG_DECODER              361
#define HCLK_JPEG_DECODER              362
#define ACLK_VO_L_ROOT                 375
#define ACLK_VO_L_BIU                  376
#define ACLK_MAC_VO                    377
#define PCLK_MAC_VO                    378
#define CLK_GMAC0_SRC                  379
#define CLK_GMAC0_RMII_50M             380
#define CLK_GMAC0_TX                   381
#define CLK_GMAC0_RX                   382
#define ACLK_JPEG_ROOT                 385
#define ACLK_JPEG_BIU                  386
#define HCLK_SAI_I2S3                  387
#define MCLK_SAI_I2S3                  388
#define CLK_MACPHY                     398
#define PCLK_VCDCPHY                   399
#define PCLK_GPIO2                     404
#define DBCLK_GPIO2                    405
#define PCLK_VO_IOC                    406
#define CCLK_SRC_SDMMC0                407
#define HCLK_SDMMC0                    408
#define PCLK_OTPC_NS                   411
#define CLK_SBPI_OTPC_NS               412
#define CLK_USER_OTPC_NS               413
#define CLK_HDMIHDP0                   415
#define HCLK_USBHOST                   416
#define HCLK_USBHOST_ARB               417
#define CLK_USBHOST_OHCI               418
#define CLK_USBHOST_UTMI               419
#define PCLK_UART4                     420
#define PCLK_I2C4                      422
#define CLK_I2C4                       423
#define PCLK_I2C7                      424
#define CLK_I2C7                       425
#define PCLK_USBPHY                    426
#define CLK_REF_USBPHY                 427
#define HCLK_RKVDEC_ROOT               433
#define ACLK_RKVDEC_ROOT_NDFT          434
#define PCLK_DDRPHY_CRU                435
#define HCLK_RKVDEC_BIU                436
#define ACLK_RKVDEC_BIU                437
#define ACLK_RKVDEC                    439
#define HCLK_RKVDEC                    440
#define CLK_HEVC_CA_RKVDEC             441
#define ACLK_RKVDEC_PVTMUX_ROOT        442
#define CLK_RKVDEC_PVTPLL_SRC          443
#define PCLK_DDR_ROOT                  449
#define PCLK_DDR_BIU                   450
#define PCLK_DDRC                      451
#define PCLK_DDRMON                    452
#define CLK_TIMER_DDRMON               453
#define PCLK_MSCH_BIU                  454
#define PCLK_DDR_GRF                   455
#define PCLK_DDR_HWLP                  456
#define PCLK_DDRPHY                    457
#define CLK_MSCH_BIU                   463
#define ACLK_DDR_UPCTL                 464
#define CLK_DDR_UPCTL                  465
#define CLK_DDRMON                     466
#define ACLK_DDR_SCRAMBLE              467
#define ACLK_SPLIT                     468
#define CLK_DDRC_SRC                   470
#define CLK_DDR_PHY                    471
#define PCLK_OTPC_S                    472
#define CLK_SBPI_OTPC_S                473
#define CLK_USER_OTPC_S                474
#define PCLK_KEYREADER                 475
#define PCLK_BUS_SGRF                  476
#define PCLK_STIMER                    477
#define CLK_STIMER0                    478
#define CLK_STIMER1                    479
#define PCLK_WDT_S                     480
#define TCLK_WDT_S                     481
#define HCLK_TRNG_S                    482
#define HCLK_BOOTROM                   486
#define PCLK_DCF                       487
#define ACLK_SYSMEM                    488
#define HCLK_TSP                       489
#define ACLK_TSP                       490
#define CLK_CORE_TSP                   491
#define CLK_OTPC_ARB                   492
#define PCLK_OTP_MASK                  493
#define CLK_PMC_OTP                    494
#define PCLK_PMU_ROOT                  495
#define HCLK_PMU_ROOT                  496
#define PCLK_I2C2                      497
#define CLK_I2C2                       498
#define HCLK_PMU_BIU                   500
#define PCLK_PMU_BIU                   501
#define FCLK_MCU                       502
#define RTC_CLK_MCU                    504
#define PCLK_OSCCHK                    505
#define CLK_PMU_MCU_JTAG               506
#define PCLK_PMU                       508
#define PCLK_GPIO0                     509
#define DBCLK_GPIO0                    510
#define XIN_OSC0_DIV                   511
#define CLK_DEEPSLOW                   512
#define CLK_DDR_FAIL_SAFE              513
#define PCLK_PMU_HP_TIMER              514
#define CLK_PMU_HP_TIMER               515
#define CLK_PMU_32K_HP_TIMER           516
#define PCLK_PMU_IOC                   517
#define PCLK_PMU_CRU                   518
#define PCLK_PMU_GRF                   519
#define PCLK_PMU_WDT                   520
#define TCLK_PMU_WDT                   521
#define PCLK_PMU_MAILBOX               522
#define PCLK_SCRKEYGEN                 524
#define CLK_SCRKEYGEN                  525
#define CLK_PVTM_OSCCHK                526
#define CLK_REFOUT                     530
#define CLK_PVTM_PMU                   532
#define PCLK_PVTM_PMU                  533
#define PCLK_PMU_SGRF                  534
#define HCLK_PMU_SRAM                  535
#define CLK_UART0                      536
#define CLK_UART1                      537
#define CLK_UART2                      538
#define CLK_UART3                      539
#define CLK_UART4                      540
#define CLK_UART5                      541
#define CLK_UART6                      542
#define CLK_UART7                      543
#define MCLK_I2S0_2CH_SAI_SRC_PRE      544
#define MCLK_I2S1_8CH_SAI_SRC_PRE      545
#define MCLK_I2S2_2CH_SAI_SRC_PRE      546
#define MCLK_I2S3_8CH_SAI_SRC_PRE      547
#define MCLK_SDPDIF_SRC_PRE            548
#define CLK_NR_CLKS                    (MCLK_SDPDIF_SRC_PRE + 1)

/* grf-clocks indices */
#define SCLK_SDMMC_DRV                 1
#define SCLK_SDMMC_SAMPLE              2
#define SCLK_SDIO0_DRV                 3
#define SCLK_SDIO0_SAMPLE              4
#define SCLK_SDIO1_DRV                 5
#define SCLK_SDIO1_SAMPLE              6
#define CLK_NR_GRF_CLKS                (SCLK_SDIO1_SAMPLE + 1)

/* scmi-clocks indices */
#define SCMI_PCLK_KEYREADER            0
#define SCMI_HCLK_KLAD                 1
#define SCMI_PCLK_KLAD                 2
#define SCMI_HCLK_TRNG_S               3
#define SCMI_HCLK_CRYPTO_S             4
#define SCMI_PCLK_WDT_S                5
#define SCMI_TCLK_WDT_S                6
#define SCMI_PCLK_STIMER               7
#define SCMI_CLK_STIMER0               8
#define SCMI_CLK_STIMER1               9
#define SCMI_PCLK_OTP_MASK             10
#define SCMI_PCLK_OTPC_S               11
#define SCMI_CLK_SBPI_OTPC_S           12
#define SCMI_CLK_USER_OTPC_S           13
#define SCMI_CLK_PMC_OTP               14
#define SCMI_CLK_OTPC_ARB              15
#define SCMI_CLK_CORE_TSP              16
#define SCMI_ACLK_TSP                  17
#define SCMI_HCLK_TSP                  18
#define SCMI_PCLK_DCF                  19
#define SCMI_CLK_DDR                   20
#define SCMI_CLK_CPU                   21
#define SCMI_CLK_GPU                   22
#define SCMI_CORE_CRYPTO               23
#define SCMI_ACLK_CRYPTO               24
#define SCMI_PKA_CRYPTO                25
#define SCMI_HCLK_CRYPTO               26
#define SCMI_CORE_CRYPTO_S             27
#define SCMI_ACLK_CRYPTO_S             28
#define SCMI_PKA_CRYPTO_S              29
#define SCMI_CORE_KLAD                 30
#define SCMI_ACLK_KLAD                 31
#define SCMI_HCLK_TRNG                 32

// CRU_SOFTRST_CON03(Offset:0xA0C)
#define SRST_NCOREPORESET0             0x00000030
#define SRST_NCOREPORESET1             0x00000031
#define SRST_NCOREPORESET2             0x00000032
#define SRST_NCOREPORESET3             0x00000033
#define SRST_NCORESET0                 0x00000034
#define SRST_NCORESET1                 0x00000035
#define SRST_NCORESET2                 0x00000036
#define SRST_NCORESET3                 0x00000037
#define SRST_NL2RESET                  0x00000038
#define SRST_ARESETN_M_CORE_BIU        0x00000039
#define SRST_RESETN_CORE_CRYPTO        0x0000003A

// CRU_SOFTRST_CON05(Offset:0xA14)
#define SRST_PRESETN_DBG               0x0000005D
#define SRST_POTRESETN_DBG             0x0000005E
#define SRST_NTRESETN_DBG              0x0000005F

// CRU_SOFTRST_CON06(Offset:0xA18)
#define SRST_PRESETN_CORE_GRF          0x00000062
#define SRST_PRESETN_DAPLITE_BIU       0x00000063
#define SRST_PRESETN_CPU_BIU           0x00000064
#define SRST_RESETN_REF_PVTPLL_CORE    0x00000067

// CRU_SOFTRST_CON08(Offset:0xA20)
#define SRST_ARESETN_BUS_VOPGL_BIU     0x00000081
#define SRST_ARESETN_BUS_H_BIU         0x00000083
#define SRST_ARESETN_SYSMEM_BIU        0x00000088
#define SRST_ARESETN_BUS_BIU           0x0000008A
#define SRST_HRESETN_BUS_BIU           0x0000008B
#define SRST_PRESETN_BUS_BIU           0x0000008C
#define SRST_PRESETN_DFT2APB           0x0000008D
#define SRST_PRESETN_BUS_GRF           0x0000008F

// CRU_SOFTRST_CON09(Offset:0xA24)
#define SRST_ARESETN_BUS_M_BIU         0x00000090
#define SRST_ARESETN_GIC               0x00000091
#define SRST_ARESETN_SPINLOCK          0x00000092
#define SRST_ARESETN_DMAC              0x00000094
#define SRST_PRESETN_TIMER             0x00000095
#define SRST_RESETN_TIMER0             0x00000096
#define SRST_RESETN_TIMER1             0x00000097
#define SRST_RESETN_TIMER2             0x00000098
#define SRST_RESETN_TIMER3             0x00000099
#define SRST_RESETN_TIMER4             0x0000009A
#define SRST_RESETN_TIMER5             0x0000009B
#define SRST_PRESETN_JDBCK_DAP         0x0000009C
#define SRST_RESETN_JDBCK_DAP          0x0000009D
#define SRST_PRESETN_WDT_NS            0x0000009F

// CRU_SOFTRST_CON10(Offset:0xA28)
#define SRST_TRESETN_WDT_NS            0x000000A0
#define SRST_HRESETN_TRNG_NS           0x000000A3
#define SRST_PRESETN_UART0             0x000000A7
#define SRST_SRESETN_UART0             0x000000A8
#define SRST_RESETN_PKA_CRYPTO         0x000000AA
#define SRST_ARESETN_CRYPTO            0x000000AB
#define SRST_HRESETN_CRYPTO            0x000000AC
#define SRST_PRESETN_DMA2DDR           0x000000AD
#define SRST_ARESETN_DMA2DDR           0x000000AE

// CRU_SOFTRST_CON11(Offset:0xA2C)
#define SRST_PRESETN_PWM0              0x000000B4
#define SRST_RESETN_PWM0               0x000000B5
#define SRST_PRESETN_PWM1              0x000000B7
#define SRST_RESETN_PWM1               0x000000B8
#define SRST_PRESETN_SCR               0x000000BA
#define SRST_ARESETN_DCF               0x000000BB
#define SRST_PRESETN_INTMUX            0x000000BC

// CRU_SOFTRST_CON25(Offset:0xA64)
#define SRST_ARESETN_VPU_BIU           0x00000196
#define SRST_HRESETN_VPU_BIU           0x00000197
#define SRST_PRESETN_VPU_BIU           0x00000198
#define SRST_ARESETN_VPU               0x00000199
#define SRST_HRESETN_VPU               0x0000019A
#define SRST_PRESETN_CRU_PCIE          0x0000019B
#define SRST_PRESETN_VPU_GRF           0x0000019C
#define SRST_HRESETN_SFC               0x0000019D
#define SRST_SRESETN_SFC               0x0000019E
#define SRST_CRESETN_EMMC              0x0000019F

// CRU_SOFTRST_CON26(Offset:0xA68)
#define SRST_HRESETN_EMMC              0x000001A0
#define SRST_ARESETN_EMMC              0x000001A1
#define SRST_BRESETN_EMMC              0x000001A2
#define SRST_TRESETN_EMMC              0x000001A3
#define SRST_PRESETN_GPIO1             0x000001A4
#define SRST_DBRESETN_GPIO1            0x000001A5
#define SRST_ARESETN_VPU_L_BIU         0x000001A6
#define SRST_PRESETN_VPU_IOC           0x000001A8
#define SRST_HRESETN_SAI_I2S0          0x000001A9
#define SRST_MRESETN_SAI_I2S0          0x000001AA
#define SRST_HRESETN_SAI_I2S2          0x000001AB
#define SRST_MRESETN_SAI_I2S2          0x000001AC
#define SRST_PRESETN_ACODEC            0x000001AD

// CRU_SOFTRST_CON27(Offset:0xA6C)
#define SRST_PRESETN_GPIO3             0x000001B0
#define SRST_DBRESETN_GPIO3            0x000001B1
#define SRST_PRESETN_SPI1              0x000001B4
#define SRST_RESETN_SPI1               0x000001B5
#define SRST_PRESETN_UART2             0x000001B7
#define SRST_SRESETN_UART2             0x000001B8
#define SRST_PRESETN_UART5             0x000001B9
#define SRST_SRESETN_UART5             0x000001BA
#define SRST_PRESETN_UART6             0x000001BB
#define SRST_SRESETN_UART6             0x000001BC
#define SRST_PRESETN_UART7             0x000001BD
#define SRST_SRESETN_UART7             0x000001BE
#define SRST_PRESETN_I2C3              0x000001BF

// CRU_SOFTRST_CON28(Offset:0xA70)
#define SRST_RESETN_I2C3               0x000001C0
#define SRST_PRESETN_I2C5              0x000001C1
#define SRST_RESETN_I2C5               0x000001C2
#define SRST_PRESETN_I2C6              0x000001C3
#define SRST_RESETN_I2C6               0x000001C4
#define SRST_ARESETN_MAC               0x000001C5

// CRU_SOFTRST_CON30(Offset:0xA78)
#define SRST_PRESETN_PCIE              0x000001E1
#define SRST_RESETN_PCIE_PIPE_PHY      0x000001E2
#define SRST_RESETN_PCIE_POWER_UP      0x000001E3
#define SRST_PRESETN_PCIE_PHY          0x000001E6
#define SRST_PRESETN_PIPE_GRF          0x000001E7

// CRU_SOFTRST_CON32(Offset:0xA80)
#define SRST_HRESETN_SDIO0             0x00000202
#define SRST_HRESETN_SDIO1             0x00000204
#define SRST_RESETN_TS_0               0x00000205
#define SRST_RESETN_TS_1               0x00000206
#define SRST_PRESETN_CAN2              0x00000207
#define SRST_RESETN_CAN2               0x00000208
#define SRST_PRESETN_CAN3              0x00000209
#define SRST_RESETN_CAN3               0x0000020A
#define SRST_PRESETN_SARADC            0x0000020B
#define SRST_RESETN_SARADC             0x0000020C
#define SRST_RESETN_SARADC_PHY         0x0000020D
#define SRST_PRESETN_TSADC             0x0000020E
#define SRST_RESETN_TSADC              0x0000020F

// CRU_SOFTRST_CON33(Offset:0xA84)
#define SRST_ARESETN_USB3OTG           0x00000211

// CRU_SOFTRST_CON34(Offset:0xA88)
#define SRST_ARESETN_GPU_BIU           0x00000223
#define SRST_PRESETN_GPU_BIU           0x00000225
#define SRST_ARESETN_GPU               0x00000228
#define SRST_RESETN_REF_PVTPLL_GPU     0x00000229

// CRU_SOFTRST_CON36(Offset:0xA90)
#define SRST_HRESETN_RKVENC_BIU        0x00000243
#define SRST_ARESETN_RKVENC_BIU        0x00000244
#define SRST_PRESETN_RKVENC_BIU        0x00000245
#define SRST_HRESETN_RKVENC            0x00000246
#define SRST_ARESETN_RKVENC            0x00000247
#define SRST_RESETN_CORE_RKVENC        0x00000248
#define SRST_HRESETN_SAI_I2S1          0x00000249
#define SRST_MRESETN_SAI_I2S1          0x0000024A
#define SRST_PRESETN_I2C1              0x0000024B
#define SRST_RESETN_I2C1               0x0000024C
#define SRST_PRESETN_I2C0              0x0000024D
#define SRST_RESETN_I2C0               0x0000024E

// CRU_SOFTRST_CON37(Offset:0xA94)
#define SRST_PRESETN_SPI0              0x00000252
#define SRST_RESETN_SPI0               0x00000253
#define SRST_PRESETN_GPIO4             0x00000258
#define SRST_DBRESETN_GPIO4            0x00000259
#define SRST_PRESETN_RKVENC_IOC        0x0000025A
#define SRST_HRESETN_SPDIF             0x0000025E
#define SRST_MRESETN_SPDIF             0x0000025F

// CRU_SOFTRST_CON38(Offset:0xA98)
#define SRST_HRESETN_PDM               0x00000260
#define SRST_MRESETN_PDM               0x00000261
#define SRST_PRESETN_UART1             0x00000262
#define SRST_SRESETN_UART1             0x00000263
#define SRST_PRESETN_UART3             0x00000264
#define SRST_SRESETN_UART3             0x00000265
#define SRST_PRESETN_RKVENC_GRF        0x00000266
#define SRST_PRESETN_CAN0              0x00000267
#define SRST_RESETN_CAN0               0x00000268
#define SRST_PRESETN_CAN1              0x00000269
#define SRST_RESETN_CAN1               0x0000026A

// CRU_SOFTRST_CON39(Offset:0xA9C)
#define SRST_ARESETN_VO_BIU            0x00000273
#define SRST_HRESETN_VO_BIU            0x00000274
#define SRST_PRESETN_VO_BIU            0x00000275
#define SRST_HRESETN_RGA2E             0x00000277
#define SRST_ARESETN_RGA2E             0x00000278
#define SRST_RESETN_CORE_RGA2E         0x00000279
#define SRST_HRESETN_VDPP              0x0000027A
#define SRST_ARESETN_VDPP              0x0000027B
#define SRST_RESETN_CORE_VDPP          0x0000027C
#define SRST_PRESETN_VO_GRF            0x0000027D
#define SRST_PRESETN_CRU               0x0000027F

// CRU_SOFTRST_CON40(Offset:0xAA0)
#define SRST_ARESETN_VOP_BIU           0x00000281
#define SRST_HRESETN_VOP               0x00000282
#define SRST_DRESETN_VOP0              0x00000283
#define SRST_DRESETN_VOP1              0x00000284
#define SRST_ARESETN_VOP               0x00000285
#define SRST_PRESETN_HDMI              0x00000286
#define SRST_HDMI_RESETN               0x00000287
#define SRST_PRESETN_HDMIPHY           0x0000028E
#define SRST_HRESETN_HDCP_KEY          0x0000028F

// CRU_SOFTRST_CON41(Offset:0xAA4)
#define SRST_ARESETN_HDCP              0x00000290
#define SRST_HRESETN_HDCP              0x00000291
#define SRST_PRESETN_HDCP              0x00000292
#define SRST_HRESETN_CVBS              0x00000293
#define SRST_DRESETN_CVBS_VOP          0x00000294
#define SRST_DRESETN_4X_CVBS_VOP       0x00000295
#define SRST_ARESETN_JPEG_DECODER      0x00000296
#define SRST_HRESETN_JPEG_DECODER      0x00000297
#define SRST_ARESETN_VO_L_BIU          0x00000299
#define SRST_ARESETN_MAC_VO            0x0000029A

// CRU_SOFTRST_CON42(Offset:0xAA8)
#define SRST_ARESETN_JPEG_BIU          0x000002A0
#define SRST_HRESETN_SAI_I2S3          0x000002A1
#define SRST_MRESETN_SAI_I2S3          0x000002A2
#define SRST_RESETN_MACPHY             0x000002A3
#define SRST_PRESETN_VCDCPHY           0x000002A4
#define SRST_PRESETN_GPIO2             0x000002A5
#define SRST_DBRESETN_GPIO2            0x000002A6
#define SRST_PRESETN_VO_IOC            0x000002A7
#define SRST_HRESETN_SDMMC0            0x000002A9
#define SRST_PRESETN_OTPC_NS           0x000002AB
#define SRST_RESETN_SBPI_OTPC_NS       0x000002AC
#define SRST_RESETN_USER_OTPC_NS       0x000002AD

// CRU_SOFTRST_CON43(Offset:0xAAC)
#define SRST_RESETN_HDMIHDP0           0x000002B2
#define SRST_HRESETN_USBHOST           0x000002B3
#define SRST_HRESETN_USBHOST_ARB       0x000002B4
#define SRST_RESETN_HOST_UTMI          0x000002B6
#define SRST_PRESETN_UART4             0x000002B7
#define SRST_SRESETN_UART4             0x000002B8
#define SRST_PRESETN_I2C4              0x000002B9
#define SRST_RESETN_I2C4               0x000002BA
#define SRST_PRESETN_I2C7              0x000002BB
#define SRST_RESETN_I2C7               0x000002BC
#define SRST_PRESETN_USBPHY            0x000002BD
#define SRST_RESETN_USBPHY_POR         0x000002BE
#define SRST_RESETN_USBPHY_OTG         0x000002BF

// CRU_SOFTRST_CON44(Offset:0xAB0)
#define SRST_RESETN_USBPHY_HOST        0x000002C0
#define SRST_PRESETN_DDRPHY_CRU        0x000002C4
#define SRST_HRESETN_RKVDEC_BIU        0x000002C6
#define SRST_ARESETN_RKVDEC_BIU        0x000002C7
#define SRST_ARESETN_RKVDEC            0x000002C8
#define SRST_HRESETN_RKVDEC            0x000002C9
#define SRST_RESETN_HEVC_CA_RKVDEC     0x000002CB
#define SRST_RESETN_REF_PVTPLL_RKVDEC  0x000002CC

// CRU_SOFTRST_CON45(Offset:0xAB4)
#define SRST_PRESETN_DDR_BIU           0x000002D1
#define SRST_PRESETN_DDRC              0x000002D2
#define SRST_PRESETN_DDRMON            0x000002D3
#define SRST_RESETN_TIMER_DDRMON       0x000002D4
#define SRST_PRESETN_MSCH_BIU          0x000002D5
#define SRST_PRESETN_DDR_GRF           0x000002D6
#define SRST_PRESETN_DDR_HWLP          0x000002D8
#define SRST_PRESETN_DDRPHY            0x000002D9
#define SRST_RESETN_MSCH_BIU           0x000002DA
#define SRST_ARESETN_DDR_UPCTL         0x000002DB
#define SRST_RESETN_DDR_UPCTL          0x000002DC
#define SRST_RESETN_DDRMON             0x000002DD
#define SRST_ARESETN_DDR_SCRAMBLE      0x000002DE
#define SRST_ARESETN_SPLIT             0x000002DF

// CRU_SOFTRST_CON46(Offset:0xAB8)
#define SRST_RESETN_DDR_PHY            0x000002E0

#endif

