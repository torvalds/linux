/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _dpcs_2_0_3_SH_MASK_HEADER
#define _dpcs_2_0_3_SH_MASK_HEADER
// addressBlock: dpcssysa_dpcs0_dpcstx0_dispdec
//DPCSTX0_DPCSTX_TX_CLOCK_CNTL
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_GATE_DIS__SHIFT                                             0x0
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_EN__SHIFT                                                   0x1
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_CLOCK_ON__SHIFT                                             0x2
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_DIV2_CLOCK_ON__SHIFT                                        0x3
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_GATE_DIS_MASK                                               0x00000001L
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_EN_MASK                                                     0x00000002L
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_CLOCK_ON_MASK                                               0x00000004L
#define DPCSTX0_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_DIV2_CLOCK_ON_MASK                                          0x00000008L
//DPCSTX0_DPCSTX_TX_CNTL
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_REQ__SHIFT                                                 0xc
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_PENDING__SHIFT                                             0xd
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_DATA_SWAP__SHIFT                                                      0xe
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_DATA_ORDER_INVERT__SHIFT                                              0xf
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_EN__SHIFT                                                        0x10
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_START__SHIFT                                                     0x11
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_RD_START_DELAY__SHIFT                                            0x14
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_SOFT_RESET__SHIFT                                                     0x1f
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_REQ_MASK                                                   0x00001000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_PENDING_MASK                                               0x00002000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_DATA_SWAP_MASK                                                        0x00004000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_DATA_ORDER_INVERT_MASK                                                0x00008000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_EN_MASK                                                          0x00010000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_START_MASK                                                       0x00020000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_FIFO_RD_START_DELAY_MASK                                              0x00F00000L
#define DPCSTX0_DPCSTX_TX_CNTL__DPCS_TX_SOFT_RESET_MASK                                                       0x80000000L
//DPCSTX0_DPCSTX_CBUS_CNTL
#define DPCSTX0_DPCSTX_CBUS_CNTL__DPCS_CBUS_WR_CMD_DELAY__SHIFT                                               0x0
#define DPCSTX0_DPCSTX_CBUS_CNTL__DPCS_CBUS_SOFT_RESET__SHIFT                                                 0x1f
#define DPCSTX0_DPCSTX_CBUS_CNTL__DPCS_CBUS_WR_CMD_DELAY_MASK                                                 0x000000FFL
#define DPCSTX0_DPCSTX_CBUS_CNTL__DPCS_CBUS_SOFT_RESET_MASK                                                   0x80000000L
//DPCSTX0_DPCSTX_INTERRUPT_CNTL
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_OVERFLOW__SHIFT                                          0x0
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_ERROR_CLR__SHIFT                                              0x1
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_ERROR_MASK__SHIFT                                        0x4
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX0_FIFO_ERROR__SHIFT                                             0x8
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX1_FIFO_ERROR__SHIFT                                             0x9
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX2_FIFO_ERROR__SHIFT                                             0xa
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX3_FIFO_ERROR__SHIFT                                             0xb
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX_ERROR_CLR__SHIFT                                               0xc
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX_FIFO_ERROR_MASK__SHIFT                                         0x10
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_INTERRUPT_MASK__SHIFT                                             0x14
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_OVERFLOW_MASK                                            0x00000001L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_ERROR_CLR_MASK                                                0x00000002L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_ERROR_MASK_MASK                                          0x00000010L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX0_FIFO_ERROR_MASK                                               0x00000100L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX1_FIFO_ERROR_MASK                                               0x00000200L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX2_FIFO_ERROR_MASK                                               0x00000400L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX3_FIFO_ERROR_MASK                                               0x00000800L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX_ERROR_CLR_MASK                                                 0x00001000L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_TX_FIFO_ERROR_MASK_MASK                                           0x00010000L
#define DPCSTX0_DPCSTX_INTERRUPT_CNTL__DPCS_INTERRUPT_MASK_MASK                                               0x00100000L
//DPCSTX0_DPCSTX_PLL_UPDATE_ADDR
#define DPCSTX0_DPCSTX_PLL_UPDATE_ADDR__DPCS_PLL_UPDATE_ADDR__SHIFT                                           0x0
#define DPCSTX0_DPCSTX_PLL_UPDATE_ADDR__DPCS_PLL_UPDATE_ADDR_MASK                                             0x0003FFFFL
//DPCSTX0_DPCSTX_PLL_UPDATE_DATA
#define DPCSTX0_DPCSTX_PLL_UPDATE_DATA__DPCS_PLL_UPDATE_DATA__SHIFT                                           0x0
#define DPCSTX0_DPCSTX_PLL_UPDATE_DATA__DPCS_PLL_UPDATE_DATA_MASK                                             0xFFFFFFFFL
//DPCSTX0_DPCSTX_DEBUG_CONFIG
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_EN__SHIFT                                                       0x0
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_CFGCLK_SEL__SHIFT                                               0x1
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_TX_SYMCLK_SEL__SHIFT                                            0x4
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_TX_SYMCLK_DIV2_SEL__SHIFT                                       0x8
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_CBUS_DIS__SHIFT                                                 0xe
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_TEST_DEBUG_WRITE_EN__SHIFT                                          0x10
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_TEST_DEBUG_INDEX__SHIFT                                             0x18
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_EN_MASK                                                         0x00000001L
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_CFGCLK_SEL_MASK                                                 0x0000000EL
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_TX_SYMCLK_SEL_MASK                                              0x00000070L
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_TX_SYMCLK_DIV2_SEL_MASK                                         0x00000700L
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_DBG_CBUS_DIS_MASK                                                   0x00004000L
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_TEST_DEBUG_WRITE_EN_MASK                                            0x00010000L
#define DPCSTX0_DPCSTX_DEBUG_CONFIG__DPCS_TEST_DEBUG_INDEX_MASK                                               0xFF000000L
//DPCSTX0_DPCSTX_TEST_DEBUG_DATA
#define DPCSTX0_DPCSTX_TEST_DEBUG_DATA__DPCS_TEST_DEBUG_DATA__SHIFT                                           0x0
#define DPCSTX0_DPCSTX_TEST_DEBUG_DATA__DPCS_TEST_DEBUG_DATA_MASK                                             0xFFFFFFFFL


// addressBlock: dpcssysa_dpcs0_rdpcstx0_dispdec
//RDPCSTX0_RDPCSTX_CNTL
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_CBUS_SOFT_RESET__SHIFT                                                   0x0
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_SRAM_SOFT_RESET__SHIFT                                                   0x4
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE0_EN__SHIFT                                                  0xc
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE1_EN__SHIFT                                                  0xd
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE2_EN__SHIFT                                                  0xe
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE3_EN__SHIFT                                                  0xf
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_EN__SHIFT                                                        0x10
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_START__SHIFT                                                     0x11
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_RD_START_DELAY__SHIFT                                            0x14
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_SOFT_RESET__SHIFT                                                     0x1f
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_CBUS_SOFT_RESET_MASK                                                     0x00000001L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_SRAM_SOFT_RESET_MASK                                                     0x00000010L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE0_EN_MASK                                                    0x00001000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE1_EN_MASK                                                    0x00002000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE2_EN_MASK                                                    0x00004000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE3_EN_MASK                                                    0x00008000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_EN_MASK                                                          0x00010000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_START_MASK                                                       0x00020000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_FIFO_RD_START_DELAY_MASK                                              0x00F00000L
#define RDPCSTX0_RDPCSTX_CNTL__RDPCS_TX_SOFT_RESET_MASK                                                       0x80000000L
//RDPCSTX0_RDPCSTX_CLOCK_CNTL
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_EXT_REFCLK_EN__SHIFT                                               0x0
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX0_EN__SHIFT                                          0x4
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX1_EN__SHIFT                                          0x5
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX2_EN__SHIFT                                          0x6
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX3_EN__SHIFT                                          0x7
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_GATE_DIS__SHIFT                                        0x8
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_EN__SHIFT                                              0x9
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_CLOCK_ON__SHIFT                                        0xa
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_GATE_DIS__SHIFT                                            0xc
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_EN__SHIFT                                                  0xd
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_CLOCK_ON__SHIFT                                            0xe
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_BYPASS__SHIFT                                              0x10
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_EXT_REFCLK_EN_MASK                                                 0x00000001L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX0_EN_MASK                                            0x00000010L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX1_EN_MASK                                            0x00000020L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX2_EN_MASK                                            0x00000040L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX3_EN_MASK                                            0x00000080L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_GATE_DIS_MASK                                          0x00000100L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_EN_MASK                                                0x00000200L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_CLOCK_ON_MASK                                          0x00000400L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_GATE_DIS_MASK                                              0x00001000L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_EN_MASK                                                    0x00002000L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_CLOCK_ON_MASK                                              0x00004000L
#define RDPCSTX0_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_BYPASS_MASK                                                0x00010000L
//RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_OVERFLOW__SHIFT                                    0x0
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE__SHIFT                                 0x1
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE__SHIFT                                   0x2
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX0_FIFO_ERROR__SHIFT                                       0x4
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX1_FIFO_ERROR__SHIFT                                       0x5
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX2_FIFO_ERROR__SHIFT                                       0x6
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX3_FIFO_ERROR__SHIFT                                       0x7
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_ERROR_CLR__SHIFT                                        0x8
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_CLR__SHIFT                             0x9
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_CLR__SHIFT                               0xa
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_ERROR_CLR__SHIFT                                         0xc
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_ERROR_MASK__SHIFT                                  0x10
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK__SHIFT                            0x11
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK__SHIFT                              0x12
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_FIFO_ERROR_MASK__SHIFT                                   0x14
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_OVERFLOW_MASK                                      0x00000001L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK                                   0x00000002L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK                                     0x00000004L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX0_FIFO_ERROR_MASK                                         0x00000010L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX1_FIFO_ERROR_MASK                                         0x00000020L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX2_FIFO_ERROR_MASK                                         0x00000040L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX3_FIFO_ERROR_MASK                                         0x00000080L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_ERROR_CLR_MASK                                          0x00000100L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_CLR_MASK                               0x00000200L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_CLR_MASK                                 0x00000400L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_ERROR_CLR_MASK                                           0x00001000L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_ERROR_MASK_MASK                                    0x00010000L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK_MASK                              0x00020000L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK_MASK                                0x00040000L
#define RDPCSTX0_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_FIFO_ERROR_MASK_MASK                                     0x00100000L
//RDPCSTX0_RDPCSTX_PLL_UPDATE_DATA
#define RDPCSTX0_RDPCSTX_PLL_UPDATE_DATA__RDPCS_PLL_UPDATE_DATA__SHIFT                                        0x0
#define RDPCSTX0_RDPCSTX_PLL_UPDATE_DATA__RDPCS_PLL_UPDATE_DATA_MASK                                          0x00000001L
//RDPCSTX0_RDPCS_TX_CR_ADDR
#define RDPCSTX0_RDPCS_TX_CR_ADDR__RDPCS_TX_CR_ADDR__SHIFT                                                    0x0
#define RDPCSTX0_RDPCS_TX_CR_ADDR__RDPCS_TX_CR_ADDR_MASK                                                      0x0000FFFFL
//RDPCSTX0_RDPCS_TX_CR_DATA
#define RDPCSTX0_RDPCS_TX_CR_DATA__RDPCS_TX_CR_DATA__SHIFT                                                    0x0
#define RDPCSTX0_RDPCS_TX_CR_DATA__RDPCS_TX_CR_DATA_MASK                                                      0x0000FFFFL
//RDPCSTX0_RDPCSTX_SCRATCH
#define RDPCSTX0_RDPCSTX_SCRATCH__RDPCSTX_SCRATCH__SHIFT                                                      0x0
#define RDPCSTX0_RDPCSTX_SCRATCH__RDPCSTX_SCRATCH_MASK                                                        0xFFFFFFFFL
//RDPCSTX0_RDPCSTX_PHY_CNTL0
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RESET__SHIFT                                                    0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_PHY_RESET__SHIFT                                            0x1
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_APB_RESET_N__SHIFT                                          0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TEST_POWERDOWN__SHIFT                                           0x3
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_DTB_OUT__SHIFT                                                  0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_HDMIMODE_ENABLE__SHIFT                                          0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_RANGE__SHIFT                                                0x9
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TX_VBOOST_LVL__SHIFT                                            0xe
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_REQ__SHIFT                                                0x11
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_ACK__SHIFT                                                0x12
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_PARA_SEL__SHIFT                                              0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_MUX_SEL__SHIFT                                               0x15
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_EN__SHIFT                                            0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_RESULT__SHIFT                                        0x19
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_INIT_DONE__SHIFT                                               0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_EXT_LD_DONE__SHIFT                                             0x1d
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_BYPASS__SHIFT                                                  0x1f
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RESET_MASK                                                      0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_PHY_RESET_MASK                                              0x00000002L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_APB_RESET_N_MASK                                            0x00000004L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TEST_POWERDOWN_MASK                                             0x00000008L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_DTB_OUT_MASK                                                    0x00000030L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_HDMIMODE_ENABLE_MASK                                            0x00000100L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_RANGE_MASK                                                  0x00003E00L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TX_VBOOST_LVL_MASK                                              0x0001C000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_REQ_MASK                                                  0x00020000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_ACK_MASK                                                  0x00040000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_PARA_SEL_MASK                                                0x00100000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_MUX_SEL_MASK                                                 0x00200000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_EN_MASK                                              0x01000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_RESULT_MASK                                          0x02000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_INIT_DONE_MASK                                                 0x10000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_EXT_LD_DONE_MASK                                               0x20000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_BYPASS_MASK                                                    0x80000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL1
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PG_MODE_EN__SHIFT                                               0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_EN__SHIFT                                               0x1
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_STABLE__SHIFT                                           0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_EN__SHIFT                                               0x3
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_STABLE__SHIFT                                           0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_DP_PG_RESET__SHIFT                                              0x5
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_EN__SHIFT                                               0x6
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_STABLE__SHIFT                                           0x7
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PG_MODE_EN_MASK                                                 0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_EN_MASK                                                 0x00000002L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_STABLE_MASK                                             0x00000004L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_EN_MASK                                                 0x00000008L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_STABLE_MASK                                             0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_DP_PG_RESET_MASK                                                0x00000020L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_EN_MASK                                                 0x00000040L
#define RDPCSTX0_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_STABLE_MASK                                             0x00000080L
//RDPCSTX0_RDPCSTX_PHY_CNTL2
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DP4__SHIFT                                                0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE__SHIFT                                            0x1
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_ACK__SHIFT                                        0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP4_POR__SHIFT                                                  0x3
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_RX2TX_PAR_LB_EN__SHIFT                                 0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_RX2TX_PAR_LB_EN__SHIFT                                 0x5
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_RX2TX_PAR_LB_EN__SHIFT                                 0x6
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_RX2TX_PAR_LB_EN__SHIFT                                 0x7
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_TX2RX_SER_LB_EN__SHIFT                                 0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_TX2RX_SER_LB_EN__SHIFT                                 0x9
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_TX2RX_SER_LB_EN__SHIFT                                 0xa
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_TX2RX_SER_LB_EN__SHIFT                                 0xb
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DP4_MASK                                                  0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_MASK                                              0x00000002L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_ACK_MASK                                          0x00000004L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP4_POR_MASK                                                    0x00000008L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_RX2TX_PAR_LB_EN_MASK                                   0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_RX2TX_PAR_LB_EN_MASK                                   0x00000020L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_RX2TX_PAR_LB_EN_MASK                                   0x00000040L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_RX2TX_PAR_LB_EN_MASK                                   0x00000080L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_TX2RX_SER_LB_EN_MASK                                   0x00000100L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_TX2RX_SER_LB_EN_MASK                                   0x00000200L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_TX2RX_SER_LB_EN_MASK                                   0x00000400L
#define RDPCSTX0_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_TX2RX_SER_LB_EN_MASK                                   0x00000800L
//RDPCSTX0_RDPCSTX_PHY_CNTL3
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_RESET__SHIFT                                             0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DISABLE__SHIFT                                           0x1
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_CLK_RDY__SHIFT                                           0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DATA_EN__SHIFT                                           0x3
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_REQ__SHIFT                                               0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_ACK__SHIFT                                               0x5
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_RESET__SHIFT                                             0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DISABLE__SHIFT                                           0x9
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_CLK_RDY__SHIFT                                           0xa
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DATA_EN__SHIFT                                           0xb
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_REQ__SHIFT                                               0xc
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_ACK__SHIFT                                               0xd
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_RESET__SHIFT                                             0x10
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DISABLE__SHIFT                                           0x11
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_CLK_RDY__SHIFT                                           0x12
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DATA_EN__SHIFT                                           0x13
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_REQ__SHIFT                                               0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_ACK__SHIFT                                               0x15
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_RESET__SHIFT                                             0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DISABLE__SHIFT                                           0x19
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_CLK_RDY__SHIFT                                           0x1a
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DATA_EN__SHIFT                                           0x1b
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_REQ__SHIFT                                               0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_ACK__SHIFT                                               0x1d
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_RESET_MASK                                               0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DISABLE_MASK                                             0x00000002L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_CLK_RDY_MASK                                             0x00000004L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DATA_EN_MASK                                             0x00000008L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_REQ_MASK                                                 0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_ACK_MASK                                                 0x00000020L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_RESET_MASK                                               0x00000100L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DISABLE_MASK                                             0x00000200L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_CLK_RDY_MASK                                             0x00000400L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DATA_EN_MASK                                             0x00000800L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_REQ_MASK                                                 0x00001000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_ACK_MASK                                                 0x00002000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_RESET_MASK                                               0x00010000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DISABLE_MASK                                             0x00020000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_CLK_RDY_MASK                                             0x00040000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DATA_EN_MASK                                             0x00080000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_REQ_MASK                                                 0x00100000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_ACK_MASK                                                 0x00200000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_RESET_MASK                                               0x01000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DISABLE_MASK                                             0x02000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_CLK_RDY_MASK                                             0x04000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DATA_EN_MASK                                             0x08000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_REQ_MASK                                                 0x10000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_ACK_MASK                                                 0x20000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL4
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_TERM_CTRL__SHIFT                                         0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_INVERT__SHIFT                                            0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_BYPASS_EQ_CALC__SHIFT                                    0x6
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_HP_PROT_EN__SHIFT                                        0x7
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_TERM_CTRL__SHIFT                                         0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_INVERT__SHIFT                                            0xc
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_BYPASS_EQ_CALC__SHIFT                                    0xe
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_HP_PROT_EN__SHIFT                                        0xf
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_TERM_CTRL__SHIFT                                         0x10
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_INVERT__SHIFT                                            0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_BYPASS_EQ_CALC__SHIFT                                    0x16
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_HP_PROT_EN__SHIFT                                        0x17
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_TERM_CTRL__SHIFT                                         0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_INVERT__SHIFT                                            0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_BYPASS_EQ_CALC__SHIFT                                    0x1e
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_HP_PROT_EN__SHIFT                                        0x1f
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_TERM_CTRL_MASK                                           0x00000007L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_INVERT_MASK                                              0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_BYPASS_EQ_CALC_MASK                                      0x00000040L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_HP_PROT_EN_MASK                                          0x00000080L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_TERM_CTRL_MASK                                           0x00000700L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_INVERT_MASK                                              0x00001000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_BYPASS_EQ_CALC_MASK                                      0x00004000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_HP_PROT_EN_MASK                                          0x00008000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_TERM_CTRL_MASK                                           0x00070000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_INVERT_MASK                                              0x00100000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_BYPASS_EQ_CALC_MASK                                      0x00400000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_HP_PROT_EN_MASK                                          0x00800000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_TERM_CTRL_MASK                                           0x07000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_INVERT_MASK                                              0x10000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_BYPASS_EQ_CALC_MASK                                      0x40000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_HP_PROT_EN_MASK                                          0x80000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL5
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_PSTATE__SHIFT                                            0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_LPD__SHIFT                                               0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_RATE__SHIFT                                              0x5
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_WIDTH__SHIFT                                             0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_MPLL_EN__SHIFT                                           0xa
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_REQ__SHIFT                                         0xb
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_RESULT__SHIFT                                      0xc
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_PSTATE__SHIFT                                            0x12
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_LPD__SHIFT                                               0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_RATE__SHIFT                                              0x15
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_WIDTH__SHIFT                                             0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_MPLL_EN__SHIFT                                           0x1a
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_REQ__SHIFT                                         0x1b
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_RESULT__SHIFT                                      0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_PSTATE_MASK                                              0x0000000CL
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_LPD_MASK                                                 0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_RATE_MASK                                                0x000000E0L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_WIDTH_MASK                                               0x00000300L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_MPLL_EN_MASK                                             0x00000400L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_REQ_MASK                                           0x00000800L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_RESULT_MASK                                        0x00001000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_PSTATE_MASK                                              0x000C0000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_LPD_MASK                                                 0x00100000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_RATE_MASK                                                0x00E00000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_WIDTH_MASK                                               0x03000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_MPLL_EN_MASK                                             0x04000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_REQ_MASK                                           0x08000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_RESULT_MASK                                        0x10000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL6
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_PSTATE__SHIFT                                            0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_LPD__SHIFT                                               0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_RATE__SHIFT                                              0x5
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_WIDTH__SHIFT                                             0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_MPLL_EN__SHIFT                                           0xa
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_REQ__SHIFT                                         0xb
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_RESULT__SHIFT                                      0xc
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_PSTATE__SHIFT                                            0x12
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_LPD__SHIFT                                               0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_RATE__SHIFT                                              0x15
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_WIDTH__SHIFT                                             0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_MPLL_EN__SHIFT                                           0x1a
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_REQ__SHIFT                                         0x1b
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_RESULT__SHIFT                                      0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_PSTATE_MASK                                              0x0000000CL
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_LPD_MASK                                                 0x00000010L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_RATE_MASK                                                0x000000E0L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_WIDTH_MASK                                               0x00000300L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_MPLL_EN_MASK                                             0x00000400L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_REQ_MASK                                           0x00000800L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_RESULT_MASK                                        0x00001000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_PSTATE_MASK                                              0x000C0000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_LPD_MASK                                                 0x00100000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_RATE_MASK                                                0x00E00000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_WIDTH_MASK                                               0x03000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_MPLL_EN_MASK                                             0x04000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_REQ_MASK                                           0x08000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_RESULT_MASK                                        0x10000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL7
#define RDPCSTX0_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_DEN__SHIFT                                       0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_QUOT__SHIFT                                      0x10
#define RDPCSTX0_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_DEN_MASK                                         0x0000FFFFL
#define RDPCSTX0_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_QUOT_MASK                                        0xFFFF0000L
//RDPCSTX0_RDPCSTX_PHY_CNTL8
#define RDPCSTX0_RDPCSTX_PHY_CNTL8__RDPCS_PHY_DP_MPLLB_SSC_PEAK__SHIFT                                        0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL8__RDPCS_PHY_DP_MPLLB_SSC_PEAK_MASK                                          0x000FFFFFL
//RDPCSTX0_RDPCSTX_PHY_CNTL9
#define RDPCSTX0_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_STEPSIZE__SHIFT                                    0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_UP_SPREAD__SHIFT                                   0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_STEPSIZE_MASK                                      0x001FFFFFL
#define RDPCSTX0_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_UP_SPREAD_MASK                                     0x01000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL10
#define RDPCSTX0_RDPCSTX_PHY_CNTL10__RDPCS_PHY_DP_MPLLB_FRACN_REM__SHIFT                                      0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL10__RDPCS_PHY_DP_MPLLB_FRACN_REM_MASK                                        0x0000FFFFL
//RDPCSTX0_RDPCSTX_PHY_CNTL11
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_EN__SHIFT                                           0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_REQ__SHIFT                                          0x1
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_MPLLB_MULTIPLIER__SHIFT                                     0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_DIV__SHIFT                                     0x10
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_MPLLB_DIV__SHIFT                                    0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV__SHIFT                           0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_EN_MASK                                             0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_REQ_MASK                                            0x00000002L
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_MPLLB_MULTIPLIER_MASK                                       0x0000FFF0L
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_DIV_MASK                                       0x00070000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_MPLLB_DIV_MASK                                      0x00700000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_MASK                             0x03000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL12
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_DIV5_CLK_EN__SHIFT                                    0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_WORD_DIV2_EN__SHIFT                                   0x2
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_TX_CLK_DIV__SHIFT                                     0x4
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_STATE__SHIFT                                          0x7
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_SSC_EN__SHIFT                                         0x8
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_DIV5_CLK_EN_MASK                                      0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_WORD_DIV2_EN_MASK                                     0x00000004L
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_TX_CLK_DIV_MASK                                       0x00000070L
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_STATE_MASK                                            0x00000080L
#define RDPCSTX0_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_SSC_EN_MASK                                           0x00000100L
//RDPCSTX0_RDPCSTX_PHY_CNTL13
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_MULTIPLIER__SHIFT                                 0x14
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_CLK_EN__SHIFT                                     0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_FORCE_EN__SHIFT                                       0x1d
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_INIT_CAL_DISABLE__SHIFT                               0x1e
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_MULTIPLIER_MASK                                   0x0FF00000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_CLK_EN_MASK                                       0x10000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_FORCE_EN_MASK                                         0x20000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_INIT_CAL_DISABLE_MASK                                 0x40000000L
//RDPCSTX0_RDPCSTX_PHY_CNTL14
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_CAL_FORCE__SHIFT                                      0x0
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_FRACN_EN__SHIFT                                       0x18
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_PMIX_EN__SHIFT                                        0x1c
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_CAL_FORCE_MASK                                        0x00000001L
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_FRACN_EN_MASK                                         0x01000000L
#define RDPCSTX0_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_PMIX_EN_MASK                                          0x10000000L
//RDPCSTX0_RDPCSTX_PHY_FUSE0
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_MAIN__SHIFT                                           0x0
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_PRE__SHIFT                                            0x6
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_POST__SHIFT                                           0xc
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_MPLLB_V2I__SHIFT                                             0x12
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_MPLLB_FREQ_VCO__SHIFT                                        0x14
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_MAIN_MASK                                             0x0000003FL
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_PRE_MASK                                              0x00000FC0L
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_TX0_EQ_POST_MASK                                             0x0003F000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_MPLLB_V2I_MASK                                               0x000C0000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE0__RDPCS_PHY_DP_MPLLB_FREQ_VCO_MASK                                          0x00300000L
//RDPCSTX0_RDPCSTX_PHY_FUSE1
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_MAIN__SHIFT                                           0x0
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_PRE__SHIFT                                            0x6
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_POST__SHIFT                                           0xc
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_MPLLB_CP_INT__SHIFT                                          0x12
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_MPLLB_CP_PROP__SHIFT                                         0x19
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_MAIN_MASK                                             0x0000003FL
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_PRE_MASK                                              0x00000FC0L
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_TX1_EQ_POST_MASK                                             0x0003F000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_MPLLB_CP_INT_MASK                                            0x01FC0000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE1__RDPCS_PHY_DP_MPLLB_CP_PROP_MASK                                           0xFE000000L
//RDPCSTX0_RDPCSTX_PHY_FUSE2
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_MAIN__SHIFT                                           0x0
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_PRE__SHIFT                                            0x6
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_POST__SHIFT                                           0xc
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_MAIN_MASK                                             0x0000003FL
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_PRE_MASK                                              0x00000FC0L
#define RDPCSTX0_RDPCSTX_PHY_FUSE2__RDPCS_PHY_DP_TX2_EQ_POST_MASK                                             0x0003F000L
//RDPCSTX0_RDPCSTX_PHY_FUSE3
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_MAIN__SHIFT                                           0x0
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_PRE__SHIFT                                            0x6
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_POST__SHIFT                                           0xc
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DCO_FINETUNE__SHIFT                                             0x12
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DCO_RANGE__SHIFT                                                0x18
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_MAIN_MASK                                             0x0000003FL
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_PRE_MASK                                              0x00000FC0L
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DP_TX3_EQ_POST_MASK                                             0x0003F000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DCO_FINETUNE_MASK                                               0x00FC0000L
#define RDPCSTX0_RDPCSTX_PHY_FUSE3__RDPCS_PHY_DCO_RANGE_MASK                                                  0x03000000L
//RDPCSTX0_RDPCSTX_PHY_RX_LD_VAL
#define RDPCSTX0_RDPCSTX_PHY_RX_LD_VAL__RDPCS_PHY_RX_REF_LD_VAL__SHIFT                                        0x0
#define RDPCSTX0_RDPCSTX_PHY_RX_LD_VAL__RDPCS_PHY_RX_VCO_LD_VAL__SHIFT                                        0x8
#define RDPCSTX0_RDPCSTX_PHY_RX_LD_VAL__RDPCS_PHY_RX_REF_LD_VAL_MASK                                          0x0000007FL
#define RDPCSTX0_RDPCSTX_PHY_RX_LD_VAL__RDPCS_PHY_RX_VCO_LD_VAL_MASK                                          0x001FFF00L


// addressBlock: dpcssysa_dpcssys_cr0_dispdec
//DPCSSYS_CR0_DPCSSYS_CR_ADDR
#define DPCSSYS_CR0_DPCSSYS_CR_ADDR__RDPCS_TX_CR_ADDR__SHIFT                                                  0x0
#define DPCSSYS_CR0_DPCSSYS_CR_ADDR__RDPCS_TX_CR_ADDR_MASK                                                    0x0000FFFFL
//DPCSSYS_CR0_DPCSSYS_CR_DATA
#define DPCSSYS_CR0_DPCSSYS_CR_DATA__RDPCS_TX_CR_DATA__SHIFT                                                  0x0
#define DPCSSYS_CR0_DPCSSYS_CR_DATA__RDPCS_TX_CR_DATA_MASK                                                    0x0000FFFFL


// addressBlock: dpcssysa_dpcs0_dpcstx1_dispdec
//DPCSTX1_DPCSTX_TX_CLOCK_CNTL
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_GATE_DIS__SHIFT                                             0x0
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_EN__SHIFT                                                   0x1
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_CLOCK_ON__SHIFT                                             0x2
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_DIV2_CLOCK_ON__SHIFT                                        0x3
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_GATE_DIS_MASK                                               0x00000001L
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_EN_MASK                                                     0x00000002L
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_CLOCK_ON_MASK                                               0x00000004L
#define DPCSTX1_DPCSTX_TX_CLOCK_CNTL__DPCS_SYMCLK_DIV2_CLOCK_ON_MASK                                          0x00000008L
//DPCSTX1_DPCSTX_TX_CNTL
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_REQ__SHIFT                                                 0xc
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_PENDING__SHIFT                                             0xd
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_DATA_SWAP__SHIFT                                                      0xe
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_DATA_ORDER_INVERT__SHIFT                                              0xf
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_EN__SHIFT                                                        0x10
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_START__SHIFT                                                     0x11
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_RD_START_DELAY__SHIFT                                            0x14
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_SOFT_RESET__SHIFT                                                     0x1f
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_REQ_MASK                                                   0x00001000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_PLL_UPDATE_PENDING_MASK                                               0x00002000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_DATA_SWAP_MASK                                                        0x00004000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_DATA_ORDER_INVERT_MASK                                                0x00008000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_EN_MASK                                                          0x00010000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_START_MASK                                                       0x00020000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_FIFO_RD_START_DELAY_MASK                                              0x00F00000L
#define DPCSTX1_DPCSTX_TX_CNTL__DPCS_TX_SOFT_RESET_MASK                                                       0x80000000L
//DPCSTX1_DPCSTX_CBUS_CNTL
#define DPCSTX1_DPCSTX_CBUS_CNTL__DPCS_CBUS_WR_CMD_DELAY__SHIFT                                               0x0
#define DPCSTX1_DPCSTX_CBUS_CNTL__DPCS_CBUS_SOFT_RESET__SHIFT                                                 0x1f
#define DPCSTX1_DPCSTX_CBUS_CNTL__DPCS_CBUS_WR_CMD_DELAY_MASK                                                 0x000000FFL
#define DPCSTX1_DPCSTX_CBUS_CNTL__DPCS_CBUS_SOFT_RESET_MASK                                                   0x80000000L
//DPCSTX1_DPCSTX_INTERRUPT_CNTL
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_OVERFLOW__SHIFT                                          0x0
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_ERROR_CLR__SHIFT                                              0x1
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_ERROR_MASK__SHIFT                                        0x4
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX0_FIFO_ERROR__SHIFT                                             0x8
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX1_FIFO_ERROR__SHIFT                                             0x9
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX2_FIFO_ERROR__SHIFT                                             0xa
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX3_FIFO_ERROR__SHIFT                                             0xb
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX_ERROR_CLR__SHIFT                                               0xc
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX_FIFO_ERROR_MASK__SHIFT                                         0x10
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_INTERRUPT_MASK__SHIFT                                             0x14
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_OVERFLOW_MASK                                            0x00000001L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_ERROR_CLR_MASK                                                0x00000002L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_REG_FIFO_ERROR_MASK_MASK                                          0x00000010L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX0_FIFO_ERROR_MASK                                               0x00000100L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX1_FIFO_ERROR_MASK                                               0x00000200L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX2_FIFO_ERROR_MASK                                               0x00000400L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX3_FIFO_ERROR_MASK                                               0x00000800L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX_ERROR_CLR_MASK                                                 0x00001000L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_TX_FIFO_ERROR_MASK_MASK                                           0x00010000L
#define DPCSTX1_DPCSTX_INTERRUPT_CNTL__DPCS_INTERRUPT_MASK_MASK                                               0x00100000L
//DPCSTX1_DPCSTX_PLL_UPDATE_ADDR
#define DPCSTX1_DPCSTX_PLL_UPDATE_ADDR__DPCS_PLL_UPDATE_ADDR__SHIFT                                           0x0
#define DPCSTX1_DPCSTX_PLL_UPDATE_ADDR__DPCS_PLL_UPDATE_ADDR_MASK                                             0x0003FFFFL
//DPCSTX1_DPCSTX_PLL_UPDATE_DATA
#define DPCSTX1_DPCSTX_PLL_UPDATE_DATA__DPCS_PLL_UPDATE_DATA__SHIFT                                           0x0
#define DPCSTX1_DPCSTX_PLL_UPDATE_DATA__DPCS_PLL_UPDATE_DATA_MASK                                             0xFFFFFFFFL
// addressBlock: dpcssysa_dpcs0_rdpcstx1_dispdec
//RDPCSTX1_RDPCSTX_CNTL
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_CBUS_SOFT_RESET__SHIFT                                                   0x0
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_SRAM_SOFT_RESET__SHIFT                                                   0x4
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE0_EN__SHIFT                                                  0xc
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE1_EN__SHIFT                                                  0xd
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE2_EN__SHIFT                                                  0xe
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE3_EN__SHIFT                                                  0xf
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_EN__SHIFT                                                        0x10
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_START__SHIFT                                                     0x11
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_RD_START_DELAY__SHIFT                                            0x14
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_SOFT_RESET__SHIFT                                                     0x1f
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_CBUS_SOFT_RESET_MASK                                                     0x00000001L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_SRAM_SOFT_RESET_MASK                                                     0x00000010L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE0_EN_MASK                                                    0x00001000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE1_EN_MASK                                                    0x00002000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE2_EN_MASK                                                    0x00004000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_LANE3_EN_MASK                                                    0x00008000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_EN_MASK                                                          0x00010000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_START_MASK                                                       0x00020000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_FIFO_RD_START_DELAY_MASK                                              0x00F00000L
#define RDPCSTX1_RDPCSTX_CNTL__RDPCS_TX_SOFT_RESET_MASK                                                       0x80000000L
//RDPCSTX1_RDPCSTX_CLOCK_CNTL
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_EXT_REFCLK_EN__SHIFT                                               0x0
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX0_EN__SHIFT                                          0x4
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX1_EN__SHIFT                                          0x5
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX2_EN__SHIFT                                          0x6
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX3_EN__SHIFT                                          0x7
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_GATE_DIS__SHIFT                                        0x8
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_EN__SHIFT                                              0x9
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_CLOCK_ON__SHIFT                                        0xa
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_GATE_DIS__SHIFT                                            0xc
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_EN__SHIFT                                                  0xd
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_CLOCK_ON__SHIFT                                            0xe
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_BYPASS__SHIFT                                              0x10
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_EXT_REFCLK_EN_MASK                                                 0x00000001L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX0_EN_MASK                                            0x00000010L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX1_EN_MASK                                            0x00000020L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX2_EN_MASK                                            0x00000040L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_TX3_EN_MASK                                            0x00000080L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_GATE_DIS_MASK                                          0x00000100L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_EN_MASK                                                0x00000200L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SYMCLK_DIV2_CLOCK_ON_MASK                                          0x00000400L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_GATE_DIS_MASK                                              0x00001000L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_EN_MASK                                                    0x00002000L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_CLOCK_ON_MASK                                              0x00004000L
#define RDPCSTX1_RDPCSTX_CLOCK_CNTL__RDPCS_SRAMCLK_BYPASS_MASK                                                0x00010000L
//RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_OVERFLOW__SHIFT                                    0x0
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE__SHIFT                                 0x1
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE__SHIFT                                   0x2
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX0_FIFO_ERROR__SHIFT                                       0x4
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX1_FIFO_ERROR__SHIFT                                       0x5
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX2_FIFO_ERROR__SHIFT                                       0x6
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX3_FIFO_ERROR__SHIFT                                       0x7
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_ERROR_CLR__SHIFT                                        0x8
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_CLR__SHIFT                             0x9
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_CLR__SHIFT                               0xa
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_ERROR_CLR__SHIFT                                         0xc
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_ERROR_MASK__SHIFT                                  0x10
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK__SHIFT                            0x11
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK__SHIFT                              0x12
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_FIFO_ERROR_MASK__SHIFT                                   0x14
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_OVERFLOW_MASK                                      0x00000001L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK                                   0x00000002L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK                                     0x00000004L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX0_FIFO_ERROR_MASK                                         0x00000010L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX1_FIFO_ERROR_MASK                                         0x00000020L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX2_FIFO_ERROR_MASK                                         0x00000040L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX3_FIFO_ERROR_MASK                                         0x00000080L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_ERROR_CLR_MASK                                          0x00000100L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_CLR_MASK                               0x00000200L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_CLR_MASK                                 0x00000400L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_ERROR_CLR_MASK                                           0x00001000L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_REG_FIFO_ERROR_MASK_MASK                                    0x00010000L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_DISABLE_TOGGLE_MASK_MASK                              0x00020000L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_DPALT_4LANE_TOGGLE_MASK_MASK                                0x00040000L
#define RDPCSTX1_RDPCSTX_INTERRUPT_CONTROL__RDPCS_TX_FIFO_ERROR_MASK_MASK                                     0x00100000L
//RDPCSTX1_RDPCSTX_PLL_UPDATE_DATA
#define RDPCSTX1_RDPCSTX_PLL_UPDATE_DATA__RDPCS_PLL_UPDATE_DATA__SHIFT                                        0x0
#define RDPCSTX1_RDPCSTX_PLL_UPDATE_DATA__RDPCS_PLL_UPDATE_DATA_MASK                                          0x00000001L
//RDPCSTX1_RDPCS_TX_CR_ADDR
#define RDPCSTX1_RDPCS_TX_CR_ADDR__RDPCS_TX_CR_ADDR__SHIFT                                                    0x0
#define RDPCSTX1_RDPCS_TX_CR_ADDR__RDPCS_TX_CR_ADDR_MASK                                                      0x0000FFFFL
//RDPCSTX1_RDPCS_TX_CR_DATA
#define RDPCSTX1_RDPCS_TX_CR_DATA__RDPCS_TX_CR_DATA__SHIFT                                                    0x0
#define RDPCSTX1_RDPCS_TX_CR_DATA__RDPCS_TX_CR_DATA_MASK                                                      0x0000FFFFL
//RDPCSTX1_RDPCS_TX_SRAM_CNTL
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_DIS__SHIFT                                                 0x14
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_FORCE__SHIFT                                               0x18
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_PWR_STATE__SHIFT                                           0x1c
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_DIS_MASK                                                   0x00100000L
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_FORCE_MASK                                                 0x03000000L
#define RDPCSTX1_RDPCS_TX_SRAM_CNTL__RDPCS_MEM_PWR_PWR_STATE_MASK                                             0x30000000L
//RDPCSTX1_RDPCSTX_SCRATCH
#define RDPCSTX1_RDPCSTX_SCRATCH__RDPCSTX_SCRATCH__SHIFT                                                      0x0
#define RDPCSTX1_RDPCSTX_SCRATCH__RDPCSTX_SCRATCH_MASK                                                        0xFFFFFFFFL
//RDPCSTX1_RDPCSTX_PHY_CNTL0
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RESET__SHIFT                                                    0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_PHY_RESET__SHIFT                                            0x1
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_APB_RESET_N__SHIFT                                          0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TEST_POWERDOWN__SHIFT                                           0x3
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_DTB_OUT__SHIFT                                                  0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_HDMIMODE_ENABLE__SHIFT                                          0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_RANGE__SHIFT                                                0x9
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TX_VBOOST_LVL__SHIFT                                            0xe
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_REQ__SHIFT                                                0x11
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_ACK__SHIFT                                                0x12
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_PARA_SEL__SHIFT                                              0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_MUX_SEL__SHIFT                                               0x15
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_EN__SHIFT                                            0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_RESULT__SHIFT                                        0x19
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_INIT_DONE__SHIFT                                               0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_EXT_LD_DONE__SHIFT                                             0x1d
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_BYPASS__SHIFT                                                  0x1f
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RESET_MASK                                                      0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_PHY_RESET_MASK                                              0x00000002L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TCA_APB_RESET_N_MASK                                            0x00000004L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TEST_POWERDOWN_MASK                                             0x00000008L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_DTB_OUT_MASK                                                    0x00000030L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_HDMIMODE_ENABLE_MASK                                            0x00000100L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_RANGE_MASK                                                  0x00003E00L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_TX_VBOOST_LVL_MASK                                              0x0001C000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_REQ_MASK                                                  0x00020000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_RTUNE_ACK_MASK                                                  0x00040000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_PARA_SEL_MASK                                                0x00100000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_CR_MUX_SEL_MASK                                                 0x00200000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_EN_MASK                                              0x01000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_PHY_REF_CLKDET_RESULT_MASK                                          0x02000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_INIT_DONE_MASK                                                 0x10000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_EXT_LD_DONE_MASK                                               0x20000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL0__RDPCS_SRAM_BYPASS_MASK                                                    0x80000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL1
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PG_MODE_EN__SHIFT                                               0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_EN__SHIFT                                               0x1
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_STABLE__SHIFT                                           0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_EN__SHIFT                                               0x3
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_STABLE__SHIFT                                           0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_DP_PG_RESET__SHIFT                                              0x5
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_EN__SHIFT                                               0x6
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_STABLE__SHIFT                                           0x7
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PG_MODE_EN_MASK                                                 0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_EN_MASK                                                 0x00000002L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PCS_PWR_STABLE_MASK                                             0x00000004L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_EN_MASK                                                 0x00000008L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_PMA_PWR_STABLE_MASK                                             0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_DP_PG_RESET_MASK                                                0x00000020L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_EN_MASK                                                 0x00000040L
#define RDPCSTX1_RDPCSTX_PHY_CNTL1__RDPCS_PHY_ANA_PWR_STABLE_MASK                                             0x00000080L
//RDPCSTX1_RDPCSTX_PHY_CNTL2
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DP4__SHIFT                                                0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE__SHIFT                                            0x1
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_ACK__SHIFT                                        0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP4_POR__SHIFT                                                  0x3
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_RX2TX_PAR_LB_EN__SHIFT                                 0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_RX2TX_PAR_LB_EN__SHIFT                                 0x5
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_RX2TX_PAR_LB_EN__SHIFT                                 0x6
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_RX2TX_PAR_LB_EN__SHIFT                                 0x7
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_TX2RX_SER_LB_EN__SHIFT                                 0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_TX2RX_SER_LB_EN__SHIFT                                 0x9
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_TX2RX_SER_LB_EN__SHIFT                                 0xa
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_TX2RX_SER_LB_EN__SHIFT                                 0xb
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DP4_MASK                                                  0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_MASK                                              0x00000002L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DPALT_DISABLE_ACK_MASK                                          0x00000004L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP4_POR_MASK                                                    0x00000008L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_RX2TX_PAR_LB_EN_MASK                                   0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_RX2TX_PAR_LB_EN_MASK                                   0x00000020L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_RX2TX_PAR_LB_EN_MASK                                   0x00000040L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_RX2TX_PAR_LB_EN_MASK                                   0x00000080L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE0_TX2RX_SER_LB_EN_MASK                                   0x00000100L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE1_TX2RX_SER_LB_EN_MASK                                   0x00000200L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE2_TX2RX_SER_LB_EN_MASK                                   0x00000400L
#define RDPCSTX1_RDPCSTX_PHY_CNTL2__RDPCS_PHY_DP_LANE3_TX2RX_SER_LB_EN_MASK                                   0x00000800L
//RDPCSTX1_RDPCSTX_PHY_CNTL3
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_RESET__SHIFT                                             0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DISABLE__SHIFT                                           0x1
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_CLK_RDY__SHIFT                                           0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DATA_EN__SHIFT                                           0x3
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_REQ__SHIFT                                               0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_ACK__SHIFT                                               0x5
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_RESET__SHIFT                                             0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DISABLE__SHIFT                                           0x9
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_CLK_RDY__SHIFT                                           0xa
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DATA_EN__SHIFT                                           0xb
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_REQ__SHIFT                                               0xc
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_ACK__SHIFT                                               0xd
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_RESET__SHIFT                                             0x10
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DISABLE__SHIFT                                           0x11
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_CLK_RDY__SHIFT                                           0x12
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DATA_EN__SHIFT                                           0x13
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_REQ__SHIFT                                               0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_ACK__SHIFT                                               0x15
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_RESET__SHIFT                                             0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DISABLE__SHIFT                                           0x19
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_CLK_RDY__SHIFT                                           0x1a
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DATA_EN__SHIFT                                           0x1b
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_REQ__SHIFT                                               0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_ACK__SHIFT                                               0x1d
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_RESET_MASK                                               0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DISABLE_MASK                                             0x00000002L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_CLK_RDY_MASK                                             0x00000004L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_DATA_EN_MASK                                             0x00000008L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_REQ_MASK                                                 0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX0_ACK_MASK                                                 0x00000020L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_RESET_MASK                                               0x00000100L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DISABLE_MASK                                             0x00000200L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_CLK_RDY_MASK                                             0x00000400L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_DATA_EN_MASK                                             0x00000800L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_REQ_MASK                                                 0x00001000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX1_ACK_MASK                                                 0x00002000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_RESET_MASK                                               0x00010000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DISABLE_MASK                                             0x00020000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_CLK_RDY_MASK                                             0x00040000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_DATA_EN_MASK                                             0x00080000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_REQ_MASK                                                 0x00100000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX2_ACK_MASK                                                 0x00200000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_RESET_MASK                                               0x01000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DISABLE_MASK                                             0x02000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_CLK_RDY_MASK                                             0x04000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_DATA_EN_MASK                                             0x08000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_REQ_MASK                                                 0x10000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL3__RDPCS_PHY_DP_TX3_ACK_MASK                                                 0x20000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL4
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_TERM_CTRL__SHIFT                                         0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_INVERT__SHIFT                                            0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_BYPASS_EQ_CALC__SHIFT                                    0x6
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_HP_PROT_EN__SHIFT                                        0x7
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_TERM_CTRL__SHIFT                                         0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_INVERT__SHIFT                                            0xc
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_BYPASS_EQ_CALC__SHIFT                                    0xe
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_HP_PROT_EN__SHIFT                                        0xf
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_TERM_CTRL__SHIFT                                         0x10
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_INVERT__SHIFT                                            0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_BYPASS_EQ_CALC__SHIFT                                    0x16
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_HP_PROT_EN__SHIFT                                        0x17
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_TERM_CTRL__SHIFT                                         0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_INVERT__SHIFT                                            0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_BYPASS_EQ_CALC__SHIFT                                    0x1e
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_HP_PROT_EN__SHIFT                                        0x1f
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_TERM_CTRL_MASK                                           0x00000007L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_INVERT_MASK                                              0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_BYPASS_EQ_CALC_MASK                                      0x00000040L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX0_HP_PROT_EN_MASK                                          0x00000080L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_TERM_CTRL_MASK                                           0x00000700L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_INVERT_MASK                                              0x00001000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_BYPASS_EQ_CALC_MASK                                      0x00004000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX1_HP_PROT_EN_MASK                                          0x00008000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_TERM_CTRL_MASK                                           0x00070000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_INVERT_MASK                                              0x00100000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_BYPASS_EQ_CALC_MASK                                      0x00400000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX2_HP_PROT_EN_MASK                                          0x00800000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_TERM_CTRL_MASK                                           0x07000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_INVERT_MASK                                              0x10000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_BYPASS_EQ_CALC_MASK                                      0x40000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL4__RDPCS_PHY_DP_TX3_HP_PROT_EN_MASK                                          0x80000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL5
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_PSTATE__SHIFT                                            0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_LPD__SHIFT                                               0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_RATE__SHIFT                                              0x5
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_WIDTH__SHIFT                                             0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_MPLL_EN__SHIFT                                           0xa
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_REQ__SHIFT                                         0xb
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_RESULT__SHIFT                                      0xc
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_PSTATE__SHIFT                                            0x12
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_LPD__SHIFT                                               0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_RATE__SHIFT                                              0x15
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_WIDTH__SHIFT                                             0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_MPLL_EN__SHIFT                                           0x1a
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_REQ__SHIFT                                         0x1b
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_RESULT__SHIFT                                      0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_PSTATE_MASK                                              0x0000000CL
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_LPD_MASK                                                 0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_RATE_MASK                                                0x000000E0L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_WIDTH_MASK                                               0x00000300L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_MPLL_EN_MASK                                             0x00000400L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_REQ_MASK                                           0x00000800L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX0_DETRX_RESULT_MASK                                        0x00001000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_PSTATE_MASK                                              0x000C0000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_LPD_MASK                                                 0x00100000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_RATE_MASK                                                0x00E00000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_WIDTH_MASK                                               0x03000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_MPLL_EN_MASK                                             0x04000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_REQ_MASK                                           0x08000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL5__RDPCS_PHY_DP_TX1_DETRX_RESULT_MASK                                        0x10000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL6
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_PSTATE__SHIFT                                            0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_LPD__SHIFT                                               0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_RATE__SHIFT                                              0x5
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_WIDTH__SHIFT                                             0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_MPLL_EN__SHIFT                                           0xa
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_REQ__SHIFT                                         0xb
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_RESULT__SHIFT                                      0xc
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_PSTATE__SHIFT                                            0x12
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_LPD__SHIFT                                               0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_RATE__SHIFT                                              0x15
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_WIDTH__SHIFT                                             0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_MPLL_EN__SHIFT                                           0x1a
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_REQ__SHIFT                                         0x1b
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_RESULT__SHIFT                                      0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_PSTATE_MASK                                              0x0000000CL
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_LPD_MASK                                                 0x00000010L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_RATE_MASK                                                0x000000E0L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_WIDTH_MASK                                               0x00000300L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_MPLL_EN_MASK                                             0x00000400L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_REQ_MASK                                           0x00000800L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX2_DETRX_RESULT_MASK                                        0x00001000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_PSTATE_MASK                                              0x000C0000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_LPD_MASK                                                 0x00100000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_RATE_MASK                                                0x00E00000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_WIDTH_MASK                                               0x03000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_MPLL_EN_MASK                                             0x04000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_REQ_MASK                                           0x08000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL6__RDPCS_PHY_DP_TX3_DETRX_RESULT_MASK                                        0x10000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL7
#define RDPCSTX1_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_DEN__SHIFT                                       0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_QUOT__SHIFT                                      0x10
#define RDPCSTX1_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_DEN_MASK                                         0x0000FFFFL
#define RDPCSTX1_RDPCSTX_PHY_CNTL7__RDPCS_PHY_DP_MPLLB_FRACN_QUOT_MASK                                        0xFFFF0000L
//RDPCSTX1_RDPCSTX_PHY_CNTL8
#define RDPCSTX1_RDPCSTX_PHY_CNTL8__RDPCS_PHY_DP_MPLLB_SSC_PEAK__SHIFT                                        0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL8__RDPCS_PHY_DP_MPLLB_SSC_PEAK_MASK                                          0x000FFFFFL
//RDPCSTX1_RDPCSTX_PHY_CNTL9
#define RDPCSTX1_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_STEPSIZE__SHIFT                                    0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_UP_SPREAD__SHIFT                                   0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_STEPSIZE_MASK                                      0x001FFFFFL
#define RDPCSTX1_RDPCSTX_PHY_CNTL9__RDPCS_PHY_DP_MPLLB_SSC_UP_SPREAD_MASK                                     0x01000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL10
#define RDPCSTX1_RDPCSTX_PHY_CNTL10__RDPCS_PHY_DP_MPLLB_FRACN_REM__SHIFT                                      0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL10__RDPCS_PHY_DP_MPLLB_FRACN_REM_MASK                                        0x0000FFFFL
//RDPCSTX1_RDPCSTX_PHY_CNTL11
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_EN__SHIFT                                           0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_REQ__SHIFT                                          0x1
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_MPLLB_MULTIPLIER__SHIFT                                     0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_DIV__SHIFT                                     0x10
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_MPLLB_DIV__SHIFT                                    0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV__SHIFT                           0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_EN_MASK                                             0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_REQ_MASK                                            0x00000002L
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_MPLLB_MULTIPLIER_MASK                                       0x0000FFF0L
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_DIV_MASK                                       0x00070000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_DP_REF_CLK_MPLLB_DIV_MASK                                      0x00700000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL11__RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_MASK                             0x03000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL12
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_DIV5_CLK_EN__SHIFT                                    0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_WORD_DIV2_EN__SHIFT                                   0x2
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_TX_CLK_DIV__SHIFT                                     0x4
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_STATE__SHIFT                                          0x7
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_SSC_EN__SHIFT                                         0x8
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_DIV5_CLK_EN_MASK                                      0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_WORD_DIV2_EN_MASK                                     0x00000004L
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_TX_CLK_DIV_MASK                                       0x00000070L
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_STATE_MASK                                            0x00000080L
#define RDPCSTX1_RDPCSTX_PHY_CNTL12__RDPCS_PHY_DP_MPLLB_SSC_EN_MASK                                           0x00000100L
//RDPCSTX1_RDPCSTX_PHY_CNTL13
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_MULTIPLIER__SHIFT                                 0x14
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_CLK_EN__SHIFT                                     0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_FORCE_EN__SHIFT                                       0x1d
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_INIT_CAL_DISABLE__SHIFT                               0x1e
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_MULTIPLIER_MASK                                   0x0FF00000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_DIV_CLK_EN_MASK                                       0x10000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_FORCE_EN_MASK                                         0x20000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL13__RDPCS_PHY_DP_MPLLB_INIT_CAL_DISABLE_MASK                                 0x40000000L
//RDPCSTX1_RDPCSTX_PHY_CNTL14
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_CAL_FORCE__SHIFT                                      0x0
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_FRACN_EN__SHIFT                                       0x18
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_PMIX_EN__SHIFT                                        0x1c
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_CAL_FORCE_MASK                                        0x00000001L
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_FRACN_EN_MASK                                         0x01000000L
#define RDPCSTX1_RDPCSTX_PHY_CNTL14__RDPCS_PHY_DP_MPLLB_PMIX_EN_MASK                                          0x10000000L

#endif
