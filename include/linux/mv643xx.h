/*
 * mv643xx.h - MV-643XX Internal registers definition file.
 *
 * Copyright 2002 Momentum Computer, Inc.
 * 	Author: Matthew Dharm <mdharm@momenco.com>
 * Copyright 2002 GALILEO TECHNOLOGY, LTD. 
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __ASM_MV643XX_H
#define __ASM_MV643XX_H

#ifdef __MIPS__
#include <asm/addrspace.h>
#include <asm/marvell.h>
#endif
#include <asm/types.h>

/****************************************/
/* Processor Address Space              */
/****************************************/

/* DDR SDRAM BAR and size registers */

#define MV64340_CS_0_BASE_ADDR                                      0x008
#define MV64340_CS_0_SIZE                                           0x010
#define MV64340_CS_1_BASE_ADDR                                      0x208
#define MV64340_CS_1_SIZE                                           0x210
#define MV64340_CS_2_BASE_ADDR                                      0x018
#define MV64340_CS_2_SIZE                                           0x020
#define MV64340_CS_3_BASE_ADDR                                      0x218
#define MV64340_CS_3_SIZE                                           0x220

/* Devices BAR and size registers */

#define MV64340_DEV_CS0_BASE_ADDR                                   0x028
#define MV64340_DEV_CS0_SIZE                                        0x030
#define MV64340_DEV_CS1_BASE_ADDR                                   0x228
#define MV64340_DEV_CS1_SIZE                                        0x230
#define MV64340_DEV_CS2_BASE_ADDR                                   0x248
#define MV64340_DEV_CS2_SIZE                                        0x250
#define MV64340_DEV_CS3_BASE_ADDR                                   0x038
#define MV64340_DEV_CS3_SIZE                                        0x040
#define MV64340_BOOTCS_BASE_ADDR                                    0x238
#define MV64340_BOOTCS_SIZE                                         0x240

/* PCI 0 BAR and size registers */

#define MV64340_PCI_0_IO_BASE_ADDR                                  0x048
#define MV64340_PCI_0_IO_SIZE                                       0x050
#define MV64340_PCI_0_MEMORY0_BASE_ADDR                             0x058
#define MV64340_PCI_0_MEMORY0_SIZE                                  0x060
#define MV64340_PCI_0_MEMORY1_BASE_ADDR                             0x080
#define MV64340_PCI_0_MEMORY1_SIZE                                  0x088
#define MV64340_PCI_0_MEMORY2_BASE_ADDR                             0x258
#define MV64340_PCI_0_MEMORY2_SIZE                                  0x260
#define MV64340_PCI_0_MEMORY3_BASE_ADDR                             0x280
#define MV64340_PCI_0_MEMORY3_SIZE                                  0x288

/* PCI 1 BAR and size registers */
#define MV64340_PCI_1_IO_BASE_ADDR                                  0x090
#define MV64340_PCI_1_IO_SIZE                                       0x098
#define MV64340_PCI_1_MEMORY0_BASE_ADDR                             0x0a0
#define MV64340_PCI_1_MEMORY0_SIZE                                  0x0a8
#define MV64340_PCI_1_MEMORY1_BASE_ADDR                             0x0b0
#define MV64340_PCI_1_MEMORY1_SIZE                                  0x0b8
#define MV64340_PCI_1_MEMORY2_BASE_ADDR                             0x2a0
#define MV64340_PCI_1_MEMORY2_SIZE                                  0x2a8
#define MV64340_PCI_1_MEMORY3_BASE_ADDR                             0x2b0
#define MV64340_PCI_1_MEMORY3_SIZE                                  0x2b8

/* SRAM base address */
#define MV64340_INTEGRATED_SRAM_BASE_ADDR                           0x268

/* internal registers space base address */
#define MV64340_INTERNAL_SPACE_BASE_ADDR                            0x068

/* Enables the CS , DEV_CS , PCI 0 and PCI 1 
   windows above */
#define MV64340_BASE_ADDR_ENABLE                                    0x278

/****************************************/
/* PCI remap registers                  */
/****************************************/
      /* PCI 0 */
#define MV64340_PCI_0_IO_ADDR_REMAP                                 0x0f0
#define MV64340_PCI_0_MEMORY0_LOW_ADDR_REMAP                        0x0f8
#define MV64340_PCI_0_MEMORY0_HIGH_ADDR_REMAP                       0x320
#define MV64340_PCI_0_MEMORY1_LOW_ADDR_REMAP                        0x100
#define MV64340_PCI_0_MEMORY1_HIGH_ADDR_REMAP                       0x328
#define MV64340_PCI_0_MEMORY2_LOW_ADDR_REMAP                        0x2f8
#define MV64340_PCI_0_MEMORY2_HIGH_ADDR_REMAP                       0x330
#define MV64340_PCI_0_MEMORY3_LOW_ADDR_REMAP                        0x300
#define MV64340_PCI_0_MEMORY3_HIGH_ADDR_REMAP                       0x338
      /* PCI 1 */
#define MV64340_PCI_1_IO_ADDR_REMAP                                 0x108
#define MV64340_PCI_1_MEMORY0_LOW_ADDR_REMAP                        0x110
#define MV64340_PCI_1_MEMORY0_HIGH_ADDR_REMAP                       0x340
#define MV64340_PCI_1_MEMORY1_LOW_ADDR_REMAP                        0x118
#define MV64340_PCI_1_MEMORY1_HIGH_ADDR_REMAP                       0x348
#define MV64340_PCI_1_MEMORY2_LOW_ADDR_REMAP                        0x310
#define MV64340_PCI_1_MEMORY2_HIGH_ADDR_REMAP                       0x350
#define MV64340_PCI_1_MEMORY3_LOW_ADDR_REMAP                        0x318
#define MV64340_PCI_1_MEMORY3_HIGH_ADDR_REMAP                       0x358
 
#define MV64340_CPU_PCI_0_HEADERS_RETARGET_CONTROL                  0x3b0
#define MV64340_CPU_PCI_0_HEADERS_RETARGET_BASE                     0x3b8
#define MV64340_CPU_PCI_1_HEADERS_RETARGET_CONTROL                  0x3c0
#define MV64340_CPU_PCI_1_HEADERS_RETARGET_BASE                     0x3c8
#define MV64340_CPU_GE_HEADERS_RETARGET_CONTROL                     0x3d0
#define MV64340_CPU_GE_HEADERS_RETARGET_BASE                        0x3d8
#define MV64340_CPU_IDMA_HEADERS_RETARGET_CONTROL                   0x3e0
#define MV64340_CPU_IDMA_HEADERS_RETARGET_BASE                      0x3e8

/****************************************/
/*         CPU Control Registers        */
/****************************************/

#define MV64340_CPU_CONFIG                                          0x000
#define MV64340_CPU_MODE                                            0x120
#define MV64340_CPU_MASTER_CONTROL                                  0x160
#define MV64340_CPU_CROSS_BAR_CONTROL_LOW                           0x150
#define MV64340_CPU_CROSS_BAR_CONTROL_HIGH                          0x158
#define MV64340_CPU_CROSS_BAR_TIMEOUT                               0x168

/****************************************/
/* SMP RegisterS                        */
/****************************************/

#define MV64340_SMP_WHO_AM_I                                        0x200
#define MV64340_SMP_CPU0_DOORBELL                                   0x214
#define MV64340_SMP_CPU0_DOORBELL_CLEAR                             0x21C
#define MV64340_SMP_CPU1_DOORBELL                                   0x224
#define MV64340_SMP_CPU1_DOORBELL_CLEAR                             0x22C
#define MV64340_SMP_CPU0_DOORBELL_MASK                              0x234
#define MV64340_SMP_CPU1_DOORBELL_MASK                              0x23C
#define MV64340_SMP_SEMAPHOR0                                       0x244
#define MV64340_SMP_SEMAPHOR1                                       0x24c
#define MV64340_SMP_SEMAPHOR2                                       0x254
#define MV64340_SMP_SEMAPHOR3                                       0x25c
#define MV64340_SMP_SEMAPHOR4                                       0x264
#define MV64340_SMP_SEMAPHOR5                                       0x26c
#define MV64340_SMP_SEMAPHOR6                                       0x274
#define MV64340_SMP_SEMAPHOR7                                       0x27c

/****************************************/
/*  CPU Sync Barrier Register           */
/****************************************/

#define MV64340_CPU_0_SYNC_BARRIER_TRIGGER                          0x0c0
#define MV64340_CPU_0_SYNC_BARRIER_VIRTUAL                          0x0c8
#define MV64340_CPU_1_SYNC_BARRIER_TRIGGER                          0x0d0
#define MV64340_CPU_1_SYNC_BARRIER_VIRTUAL                          0x0d8

/****************************************/
/* CPU Access Protect                   */
/****************************************/

#define MV64340_CPU_PROTECT_WINDOW_0_BASE_ADDR                      0x180
#define MV64340_CPU_PROTECT_WINDOW_0_SIZE                           0x188
#define MV64340_CPU_PROTECT_WINDOW_1_BASE_ADDR                      0x190
#define MV64340_CPU_PROTECT_WINDOW_1_SIZE                           0x198
#define MV64340_CPU_PROTECT_WINDOW_2_BASE_ADDR                      0x1a0
#define MV64340_CPU_PROTECT_WINDOW_2_SIZE                           0x1a8
#define MV64340_CPU_PROTECT_WINDOW_3_BASE_ADDR                      0x1b0
#define MV64340_CPU_PROTECT_WINDOW_3_SIZE                           0x1b8


/****************************************/
/*          CPU Error Report            */
/****************************************/

#define MV64340_CPU_ERROR_ADDR_LOW                                  0x070
#define MV64340_CPU_ERROR_ADDR_HIGH                                 0x078
#define MV64340_CPU_ERROR_DATA_LOW                                  0x128
#define MV64340_CPU_ERROR_DATA_HIGH                                 0x130
#define MV64340_CPU_ERROR_PARITY                                    0x138
#define MV64340_CPU_ERROR_CAUSE                                     0x140
#define MV64340_CPU_ERROR_MASK                                      0x148

/****************************************/
/*      CPU Interface Debug Registers 	*/
/****************************************/

#define MV64340_PUNIT_SLAVE_DEBUG_LOW                               0x360
#define MV64340_PUNIT_SLAVE_DEBUG_HIGH                              0x368
#define MV64340_PUNIT_MASTER_DEBUG_LOW                              0x370
#define MV64340_PUNIT_MASTER_DEBUG_HIGH                             0x378
#define MV64340_PUNIT_MMASK                                         0x3e4

/****************************************/
/*  Integrated SRAM Registers           */
/****************************************/

#define MV64340_SRAM_CONFIG                                         0x380
#define MV64340_SRAM_TEST_MODE                                      0X3F4
#define MV64340_SRAM_ERROR_CAUSE                                    0x388
#define MV64340_SRAM_ERROR_ADDR                                     0x390
#define MV64340_SRAM_ERROR_ADDR_HIGH                                0X3F8
#define MV64340_SRAM_ERROR_DATA_LOW                                 0x398
#define MV64340_SRAM_ERROR_DATA_HIGH                                0x3a0
#define MV64340_SRAM_ERROR_DATA_PARITY                              0x3a8

/****************************************/
/* SDRAM Configuration                  */
/****************************************/

#define MV64340_SDRAM_CONFIG                                        0x1400
#define MV64340_D_UNIT_CONTROL_LOW                                  0x1404
#define MV64340_D_UNIT_CONTROL_HIGH                                 0x1424
#define MV64340_SDRAM_TIMING_CONTROL_LOW                            0x1408
#define MV64340_SDRAM_TIMING_CONTROL_HIGH                           0x140c
#define MV64340_SDRAM_ADDR_CONTROL                                  0x1410
#define MV64340_SDRAM_OPEN_PAGES_CONTROL                            0x1414
#define MV64340_SDRAM_OPERATION                                     0x1418
#define MV64340_SDRAM_MODE                                          0x141c
#define MV64340_EXTENDED_DRAM_MODE                                  0x1420
#define MV64340_SDRAM_CROSS_BAR_CONTROL_LOW                         0x1430
#define MV64340_SDRAM_CROSS_BAR_CONTROL_HIGH                        0x1434
#define MV64340_SDRAM_CROSS_BAR_TIMEOUT                             0x1438
#define MV64340_SDRAM_ADDR_CTRL_PADS_CALIBRATION                    0x14c0
#define MV64340_SDRAM_DATA_PADS_CALIBRATION                         0x14c4

/****************************************/
/* SDRAM Error Report                   */
/****************************************/

#define MV64340_SDRAM_ERROR_DATA_LOW                                0x1444
#define MV64340_SDRAM_ERROR_DATA_HIGH                               0x1440
#define MV64340_SDRAM_ERROR_ADDR                                    0x1450
#define MV64340_SDRAM_RECEIVED_ECC                                  0x1448
#define MV64340_SDRAM_CALCULATED_ECC                                0x144c
#define MV64340_SDRAM_ECC_CONTROL                                   0x1454
#define MV64340_SDRAM_ECC_ERROR_COUNTER                             0x1458

/******************************************/
/*  Controlled Delay Line (CDL) Registers */
/******************************************/

#define MV64340_DFCDL_CONFIG0                                       0x1480
#define MV64340_DFCDL_CONFIG1                                       0x1484
#define MV64340_DLL_WRITE                                           0x1488
#define MV64340_DLL_READ                                            0x148c
#define MV64340_SRAM_ADDR                                           0x1490
#define MV64340_SRAM_DATA0                                          0x1494
#define MV64340_SRAM_DATA1                                          0x1498
#define MV64340_SRAM_DATA2                                          0x149c
#define MV64340_DFCL_PROBE                                          0x14a0

/******************************************/
/*   Debug Registers                      */
/******************************************/

#define MV64340_DUNIT_DEBUG_LOW                                     0x1460
#define MV64340_DUNIT_DEBUG_HIGH                                    0x1464
#define MV64340_DUNIT_MMASK                                         0X1b40

/****************************************/
/* Device Parameters			*/
/****************************************/

#define MV64340_DEVICE_BANK0_PARAMETERS				    0x45c
#define MV64340_DEVICE_BANK1_PARAMETERS				    0x460
#define MV64340_DEVICE_BANK2_PARAMETERS				    0x464
#define MV64340_DEVICE_BANK3_PARAMETERS				    0x468
#define MV64340_DEVICE_BOOT_BANK_PARAMETERS			    0x46c
#define MV64340_DEVICE_INTERFACE_CONTROL                            0x4c0
#define MV64340_DEVICE_INTERFACE_CROSS_BAR_CONTROL_LOW              0x4c8
#define MV64340_DEVICE_INTERFACE_CROSS_BAR_CONTROL_HIGH             0x4cc
#define MV64340_DEVICE_INTERFACE_CROSS_BAR_TIMEOUT                  0x4c4

/****************************************/
/* Device interrupt registers		*/
/****************************************/

#define MV64340_DEVICE_INTERRUPT_CAUSE				    0x4d0
#define MV64340_DEVICE_INTERRUPT_MASK				    0x4d4
#define MV64340_DEVICE_ERROR_ADDR				    0x4d8
#define MV64340_DEVICE_ERROR_DATA   				    0x4dc
#define MV64340_DEVICE_ERROR_PARITY     			    0x4e0

/****************************************/
/* Device debug registers   		*/
/****************************************/

#define MV64340_DEVICE_DEBUG_LOW     				    0x4e4
#define MV64340_DEVICE_DEBUG_HIGH     				    0x4e8
#define MV64340_RUNIT_MMASK                                         0x4f0

/****************************************/
/* PCI Slave Address Decoding registers */
/****************************************/

#define MV64340_PCI_0_CS_0_BANK_SIZE                                0xc08
#define MV64340_PCI_1_CS_0_BANK_SIZE                                0xc88
#define MV64340_PCI_0_CS_1_BANK_SIZE                                0xd08
#define MV64340_PCI_1_CS_1_BANK_SIZE                                0xd88
#define MV64340_PCI_0_CS_2_BANK_SIZE                                0xc0c
#define MV64340_PCI_1_CS_2_BANK_SIZE                                0xc8c
#define MV64340_PCI_0_CS_3_BANK_SIZE                                0xd0c
#define MV64340_PCI_1_CS_3_BANK_SIZE                                0xd8c
#define MV64340_PCI_0_DEVCS_0_BANK_SIZE                             0xc10
#define MV64340_PCI_1_DEVCS_0_BANK_SIZE                             0xc90
#define MV64340_PCI_0_DEVCS_1_BANK_SIZE                             0xd10
#define MV64340_PCI_1_DEVCS_1_BANK_SIZE                             0xd90
#define MV64340_PCI_0_DEVCS_2_BANK_SIZE                             0xd18
#define MV64340_PCI_1_DEVCS_2_BANK_SIZE                             0xd98
#define MV64340_PCI_0_DEVCS_3_BANK_SIZE                             0xc14
#define MV64340_PCI_1_DEVCS_3_BANK_SIZE                             0xc94
#define MV64340_PCI_0_DEVCS_BOOT_BANK_SIZE                          0xd14
#define MV64340_PCI_1_DEVCS_BOOT_BANK_SIZE                          0xd94
#define MV64340_PCI_0_P2P_MEM0_BAR_SIZE                             0xd1c
#define MV64340_PCI_1_P2P_MEM0_BAR_SIZE                             0xd9c
#define MV64340_PCI_0_P2P_MEM1_BAR_SIZE                             0xd20
#define MV64340_PCI_1_P2P_MEM1_BAR_SIZE                             0xda0
#define MV64340_PCI_0_P2P_I_O_BAR_SIZE                              0xd24
#define MV64340_PCI_1_P2P_I_O_BAR_SIZE                              0xda4
#define MV64340_PCI_0_CPU_BAR_SIZE                                  0xd28
#define MV64340_PCI_1_CPU_BAR_SIZE                                  0xda8
#define MV64340_PCI_0_INTERNAL_SRAM_BAR_SIZE                        0xe00
#define MV64340_PCI_1_INTERNAL_SRAM_BAR_SIZE                        0xe80
#define MV64340_PCI_0_EXPANSION_ROM_BAR_SIZE                        0xd2c
#define MV64340_PCI_1_EXPANSION_ROM_BAR_SIZE                        0xd9c
#define MV64340_PCI_0_BASE_ADDR_REG_ENABLE                          0xc3c
#define MV64340_PCI_1_BASE_ADDR_REG_ENABLE                          0xcbc
#define MV64340_PCI_0_CS_0_BASE_ADDR_REMAP			    0xc48
#define MV64340_PCI_1_CS_0_BASE_ADDR_REMAP			    0xcc8
#define MV64340_PCI_0_CS_1_BASE_ADDR_REMAP			    0xd48
#define MV64340_PCI_1_CS_1_BASE_ADDR_REMAP			    0xdc8
#define MV64340_PCI_0_CS_2_BASE_ADDR_REMAP			    0xc4c
#define MV64340_PCI_1_CS_2_BASE_ADDR_REMAP			    0xccc
#define MV64340_PCI_0_CS_3_BASE_ADDR_REMAP			    0xd4c
#define MV64340_PCI_1_CS_3_BASE_ADDR_REMAP			    0xdcc
#define MV64340_PCI_0_CS_0_BASE_HIGH_ADDR_REMAP			    0xF04
#define MV64340_PCI_1_CS_0_BASE_HIGH_ADDR_REMAP			    0xF84
#define MV64340_PCI_0_CS_1_BASE_HIGH_ADDR_REMAP			    0xF08
#define MV64340_PCI_1_CS_1_BASE_HIGH_ADDR_REMAP			    0xF88
#define MV64340_PCI_0_CS_2_BASE_HIGH_ADDR_REMAP			    0xF0C
#define MV64340_PCI_1_CS_2_BASE_HIGH_ADDR_REMAP			    0xF8C
#define MV64340_PCI_0_CS_3_BASE_HIGH_ADDR_REMAP			    0xF10
#define MV64340_PCI_1_CS_3_BASE_HIGH_ADDR_REMAP			    0xF90
#define MV64340_PCI_0_DEVCS_0_BASE_ADDR_REMAP			    0xc50
#define MV64340_PCI_1_DEVCS_0_BASE_ADDR_REMAP			    0xcd0
#define MV64340_PCI_0_DEVCS_1_BASE_ADDR_REMAP			    0xd50
#define MV64340_PCI_1_DEVCS_1_BASE_ADDR_REMAP			    0xdd0
#define MV64340_PCI_0_DEVCS_2_BASE_ADDR_REMAP			    0xd58
#define MV64340_PCI_1_DEVCS_2_BASE_ADDR_REMAP			    0xdd8
#define MV64340_PCI_0_DEVCS_3_BASE_ADDR_REMAP           	    0xc54
#define MV64340_PCI_1_DEVCS_3_BASE_ADDR_REMAP           	    0xcd4
#define MV64340_PCI_0_DEVCS_BOOTCS_BASE_ADDR_REMAP      	    0xd54
#define MV64340_PCI_1_DEVCS_BOOTCS_BASE_ADDR_REMAP      	    0xdd4
#define MV64340_PCI_0_P2P_MEM0_BASE_ADDR_REMAP_LOW                  0xd5c
#define MV64340_PCI_1_P2P_MEM0_BASE_ADDR_REMAP_LOW                  0xddc
#define MV64340_PCI_0_P2P_MEM0_BASE_ADDR_REMAP_HIGH                 0xd60
#define MV64340_PCI_1_P2P_MEM0_BASE_ADDR_REMAP_HIGH                 0xde0
#define MV64340_PCI_0_P2P_MEM1_BASE_ADDR_REMAP_LOW                  0xd64
#define MV64340_PCI_1_P2P_MEM1_BASE_ADDR_REMAP_LOW                  0xde4
#define MV64340_PCI_0_P2P_MEM1_BASE_ADDR_REMAP_HIGH                 0xd68
#define MV64340_PCI_1_P2P_MEM1_BASE_ADDR_REMAP_HIGH                 0xde8
#define MV64340_PCI_0_P2P_I_O_BASE_ADDR_REMAP                       0xd6c
#define MV64340_PCI_1_P2P_I_O_BASE_ADDR_REMAP                       0xdec 
#define MV64340_PCI_0_CPU_BASE_ADDR_REMAP_LOW                       0xd70
#define MV64340_PCI_1_CPU_BASE_ADDR_REMAP_LOW                       0xdf0
#define MV64340_PCI_0_CPU_BASE_ADDR_REMAP_HIGH                      0xd74
#define MV64340_PCI_1_CPU_BASE_ADDR_REMAP_HIGH                      0xdf4
#define MV64340_PCI_0_INTEGRATED_SRAM_BASE_ADDR_REMAP               0xf00
#define MV64340_PCI_1_INTEGRATED_SRAM_BASE_ADDR_REMAP               0xf80
#define MV64340_PCI_0_EXPANSION_ROM_BASE_ADDR_REMAP                 0xf38
#define MV64340_PCI_1_EXPANSION_ROM_BASE_ADDR_REMAP                 0xfb8
#define MV64340_PCI_0_ADDR_DECODE_CONTROL                           0xd3c
#define MV64340_PCI_1_ADDR_DECODE_CONTROL                           0xdbc
#define MV64340_PCI_0_HEADERS_RETARGET_CONTROL                      0xF40
#define MV64340_PCI_1_HEADERS_RETARGET_CONTROL                      0xFc0
#define MV64340_PCI_0_HEADERS_RETARGET_BASE                         0xF44
#define MV64340_PCI_1_HEADERS_RETARGET_BASE                         0xFc4
#define MV64340_PCI_0_HEADERS_RETARGET_HIGH                         0xF48
#define MV64340_PCI_1_HEADERS_RETARGET_HIGH                         0xFc8

/***********************************/
/*   PCI Control Register Map      */
/***********************************/

#define MV64340_PCI_0_DLL_STATUS_AND_COMMAND                        0x1d20
#define MV64340_PCI_1_DLL_STATUS_AND_COMMAND                        0x1da0
#define MV64340_PCI_0_MPP_PADS_DRIVE_CONTROL                        0x1d1C
#define MV64340_PCI_1_MPP_PADS_DRIVE_CONTROL                        0x1d9C
#define MV64340_PCI_0_COMMAND			         	    0xc00
#define MV64340_PCI_1_COMMAND					    0xc80
#define MV64340_PCI_0_MODE                                          0xd00
#define MV64340_PCI_1_MODE                                          0xd80
#define MV64340_PCI_0_RETRY	        	 		    0xc04
#define MV64340_PCI_1_RETRY				            0xc84
#define MV64340_PCI_0_READ_BUFFER_DISCARD_TIMER                     0xd04
#define MV64340_PCI_1_READ_BUFFER_DISCARD_TIMER                     0xd84
#define MV64340_PCI_0_MSI_TRIGGER_TIMER                             0xc38
#define MV64340_PCI_1_MSI_TRIGGER_TIMER                             0xcb8
#define MV64340_PCI_0_ARBITER_CONTROL                               0x1d00
#define MV64340_PCI_1_ARBITER_CONTROL                               0x1d80
#define MV64340_PCI_0_CROSS_BAR_CONTROL_LOW                         0x1d08
#define MV64340_PCI_1_CROSS_BAR_CONTROL_LOW                         0x1d88
#define MV64340_PCI_0_CROSS_BAR_CONTROL_HIGH                        0x1d0c
#define MV64340_PCI_1_CROSS_BAR_CONTROL_HIGH                        0x1d8c
#define MV64340_PCI_0_CROSS_BAR_TIMEOUT                             0x1d04
#define MV64340_PCI_1_CROSS_BAR_TIMEOUT                             0x1d84
#define MV64340_PCI_0_SYNC_BARRIER_TRIGGER_REG                      0x1D18
#define MV64340_PCI_1_SYNC_BARRIER_TRIGGER_REG                      0x1D98
#define MV64340_PCI_0_SYNC_BARRIER_VIRTUAL_REG                      0x1d10
#define MV64340_PCI_1_SYNC_BARRIER_VIRTUAL_REG                      0x1d90
#define MV64340_PCI_0_P2P_CONFIG                                    0x1d14
#define MV64340_PCI_1_P2P_CONFIG                                    0x1d94

#define MV64340_PCI_0_ACCESS_CONTROL_BASE_0_LOW                     0x1e00
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_0_HIGH                    0x1e04
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_0                         0x1e08
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_1_LOW                     0x1e10
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_1_HIGH                    0x1e14
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_1                         0x1e18
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_2_LOW                     0x1e20
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_2_HIGH                    0x1e24
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_2                         0x1e28
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_3_LOW                     0x1e30
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_3_HIGH                    0x1e34
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_3                         0x1e38
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_4_LOW                     0x1e40
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_4_HIGH                    0x1e44
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_4                         0x1e48
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_5_LOW                     0x1e50
#define MV64340_PCI_0_ACCESS_CONTROL_BASE_5_HIGH                    0x1e54
#define MV64340_PCI_0_ACCESS_CONTROL_SIZE_5                         0x1e58

#define MV64340_PCI_1_ACCESS_CONTROL_BASE_0_LOW                     0x1e80
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_0_HIGH                    0x1e84
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_0                         0x1e88
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_1_LOW                     0x1e90
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_1_HIGH                    0x1e94
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_1                         0x1e98
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_2_LOW                     0x1ea0
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_2_HIGH                    0x1ea4
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_2                         0x1ea8
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_3_LOW                     0x1eb0
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_3_HIGH                    0x1eb4
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_3                         0x1eb8
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_4_LOW                     0x1ec0
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_4_HIGH                    0x1ec4
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_4                         0x1ec8
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_5_LOW                     0x1ed0
#define MV64340_PCI_1_ACCESS_CONTROL_BASE_5_HIGH                    0x1ed4
#define MV64340_PCI_1_ACCESS_CONTROL_SIZE_5                         0x1ed8

/****************************************/
/*   PCI Configuration Access Registers */
/****************************************/

#define MV64340_PCI_0_CONFIG_ADDR 				    0xcf8
#define MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG                       0xcfc
#define MV64340_PCI_1_CONFIG_ADDR 				    0xc78
#define MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG                       0xc7c
#define MV64340_PCI_0_INTERRUPT_ACKNOWLEDGE_VIRTUAL_REG	            0xc34
#define MV64340_PCI_1_INTERRUPT_ACKNOWLEDGE_VIRTUAL_REG	            0xcb4

/****************************************/
/*   PCI Error Report Registers         */
/****************************************/

#define MV64340_PCI_0_SERR_MASK					    0xc28
#define MV64340_PCI_1_SERR_MASK					    0xca8
#define MV64340_PCI_0_ERROR_ADDR_LOW                                0x1d40
#define MV64340_PCI_1_ERROR_ADDR_LOW                                0x1dc0
#define MV64340_PCI_0_ERROR_ADDR_HIGH                               0x1d44
#define MV64340_PCI_1_ERROR_ADDR_HIGH                               0x1dc4
#define MV64340_PCI_0_ERROR_ATTRIBUTE                               0x1d48
#define MV64340_PCI_1_ERROR_ATTRIBUTE                               0x1dc8
#define MV64340_PCI_0_ERROR_COMMAND                                 0x1d50
#define MV64340_PCI_1_ERROR_COMMAND                                 0x1dd0
#define MV64340_PCI_0_ERROR_CAUSE                                   0x1d58
#define MV64340_PCI_1_ERROR_CAUSE                                   0x1dd8
#define MV64340_PCI_0_ERROR_MASK                                    0x1d5c
#define MV64340_PCI_1_ERROR_MASK                                    0x1ddc

/****************************************/
/*   PCI Debug Registers                */
/****************************************/

#define MV64340_PCI_0_MMASK                                         0X1D24
#define MV64340_PCI_1_MMASK                                         0X1DA4

/*********************************************/
/* PCI Configuration, Function 0, Registers  */
/*********************************************/

#define MV64340_PCI_DEVICE_AND_VENDOR_ID 			    0x000
#define MV64340_PCI_STATUS_AND_COMMAND				    0x004
#define MV64340_PCI_CLASS_CODE_AND_REVISION_ID			    0x008
#define MV64340_PCI_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE 	    0x00C

#define MV64340_PCI_SCS_0_BASE_ADDR_LOW   	      		    0x010
#define MV64340_PCI_SCS_0_BASE_ADDR_HIGH   		            0x014
#define MV64340_PCI_SCS_1_BASE_ADDR_LOW  	     	            0x018
#define MV64340_PCI_SCS_1_BASE_ADDR_HIGH 		            0x01C
#define MV64340_PCI_INTERNAL_REG_MEM_MAPPED_BASE_ADDR_LOW      	    0x020
#define MV64340_PCI_INTERNAL_REG_MEM_MAPPED_BASE_ADDR_HIGH     	    0x024
#define MV64340_PCI_SUBSYSTEM_ID_AND_SUBSYSTEM_VENDOR_ID	    0x02c
#define MV64340_PCI_EXPANSION_ROM_BASE_ADDR_REG	                    0x030
#define MV64340_PCI_CAPABILTY_LIST_POINTER                          0x034
#define MV64340_PCI_INTERRUPT_PIN_AND_LINE 			    0x03C
       /* capability list */
#define MV64340_PCI_POWER_MANAGEMENT_CAPABILITY                     0x040
#define MV64340_PCI_POWER_MANAGEMENT_STATUS_AND_CONTROL             0x044
#define MV64340_PCI_VPD_ADDR                                        0x048
#define MV64340_PCI_VPD_DATA                                        0x04c
#define MV64340_PCI_MSI_MESSAGE_CONTROL                             0x050
#define MV64340_PCI_MSI_MESSAGE_ADDR                                0x054
#define MV64340_PCI_MSI_MESSAGE_UPPER_ADDR                          0x058
#define MV64340_PCI_MSI_MESSAGE_DATA                                0x05c
#define MV64340_PCI_X_COMMAND                                       0x060
#define MV64340_PCI_X_STATUS                                        0x064
#define MV64340_PCI_COMPACT_PCI_HOT_SWAP                            0x068

/***********************************************/
/*   PCI Configuration, Function 1, Registers  */
/***********************************************/

#define MV64340_PCI_SCS_2_BASE_ADDR_LOW   			    0x110
#define MV64340_PCI_SCS_2_BASE_ADDR_HIGH			    0x114
#define MV64340_PCI_SCS_3_BASE_ADDR_LOW 			    0x118
#define MV64340_PCI_SCS_3_BASE_ADDR_HIGH			    0x11c
#define MV64340_PCI_INTERNAL_SRAM_BASE_ADDR_LOW          	    0x120
#define MV64340_PCI_INTERNAL_SRAM_BASE_ADDR_HIGH         	    0x124

/***********************************************/
/*  PCI Configuration, Function 2, Registers   */
/***********************************************/

#define MV64340_PCI_DEVCS_0_BASE_ADDR_LOW	    		    0x210
#define MV64340_PCI_DEVCS_0_BASE_ADDR_HIGH 			    0x214
#define MV64340_PCI_DEVCS_1_BASE_ADDR_LOW 			    0x218
#define MV64340_PCI_DEVCS_1_BASE_ADDR_HIGH      		    0x21c
#define MV64340_PCI_DEVCS_2_BASE_ADDR_LOW 			    0x220
#define MV64340_PCI_DEVCS_2_BASE_ADDR_HIGH      		    0x224

/***********************************************/
/*  PCI Configuration, Function 3, Registers   */
/***********************************************/

#define MV64340_PCI_DEVCS_3_BASE_ADDR_LOW	    		    0x310
#define MV64340_PCI_DEVCS_3_BASE_ADDR_HIGH 			    0x314
#define MV64340_PCI_BOOT_CS_BASE_ADDR_LOW			    0x318
#define MV64340_PCI_BOOT_CS_BASE_ADDR_HIGH      		    0x31c
#define MV64340_PCI_CPU_BASE_ADDR_LOW 				    0x220
#define MV64340_PCI_CPU_BASE_ADDR_HIGH      			    0x224

/***********************************************/
/*  PCI Configuration, Function 4, Registers   */
/***********************************************/

#define MV64340_PCI_P2P_MEM0_BASE_ADDR_LOW  			    0x410
#define MV64340_PCI_P2P_MEM0_BASE_ADDR_HIGH 			    0x414
#define MV64340_PCI_P2P_MEM1_BASE_ADDR_LOW   			    0x418
#define MV64340_PCI_P2P_MEM1_BASE_ADDR_HIGH 			    0x41c
#define MV64340_PCI_P2P_I_O_BASE_ADDR                 	            0x420
#define MV64340_PCI_INTERNAL_REGS_I_O_MAPPED_BASE_ADDR              0x424

/****************************************/
/* Messaging Unit Registers (I20)   	*/
/****************************************/

#define MV64340_I2O_INBOUND_MESSAGE_REG0_PCI_0_SIDE		    0x010
#define MV64340_I2O_INBOUND_MESSAGE_REG1_PCI_0_SIDE  		    0x014
#define MV64340_I2O_OUTBOUND_MESSAGE_REG0_PCI_0_SIDE 		    0x018
#define MV64340_I2O_OUTBOUND_MESSAGE_REG1_PCI_0_SIDE  		    0x01C
#define MV64340_I2O_INBOUND_DOORBELL_REG_PCI_0_SIDE  		    0x020
#define MV64340_I2O_INBOUND_INTERRUPT_CAUSE_REG_PCI_0_SIDE          0x024
#define MV64340_I2O_INBOUND_INTERRUPT_MASK_REG_PCI_0_SIDE	    0x028
#define MV64340_I2O_OUTBOUND_DOORBELL_REG_PCI_0_SIDE 		    0x02C
#define MV64340_I2O_OUTBOUND_INTERRUPT_CAUSE_REG_PCI_0_SIDE         0x030
#define MV64340_I2O_OUTBOUND_INTERRUPT_MASK_REG_PCI_0_SIDE          0x034
#define MV64340_I2O_INBOUND_QUEUE_PORT_VIRTUAL_REG_PCI_0_SIDE       0x040
#define MV64340_I2O_OUTBOUND_QUEUE_PORT_VIRTUAL_REG_PCI_0_SIDE      0x044
#define MV64340_I2O_QUEUE_CONTROL_REG_PCI_0_SIDE 		    0x050
#define MV64340_I2O_QUEUE_BASE_ADDR_REG_PCI_0_SIDE 		    0x054
#define MV64340_I2O_INBOUND_FREE_HEAD_POINTER_REG_PCI_0_SIDE        0x060
#define MV64340_I2O_INBOUND_FREE_TAIL_POINTER_REG_PCI_0_SIDE        0x064
#define MV64340_I2O_INBOUND_POST_HEAD_POINTER_REG_PCI_0_SIDE        0x068
#define MV64340_I2O_INBOUND_POST_TAIL_POINTER_REG_PCI_0_SIDE        0x06C
#define MV64340_I2O_OUTBOUND_FREE_HEAD_POINTER_REG_PCI_0_SIDE       0x070
#define MV64340_I2O_OUTBOUND_FREE_TAIL_POINTER_REG_PCI_0_SIDE       0x074
#define MV64340_I2O_OUTBOUND_POST_HEAD_POINTER_REG_PCI_0_SIDE       0x0F8
#define MV64340_I2O_OUTBOUND_POST_TAIL_POINTER_REG_PCI_0_SIDE       0x0FC

#define MV64340_I2O_INBOUND_MESSAGE_REG0_PCI_1_SIDE		    0x090
#define MV64340_I2O_INBOUND_MESSAGE_REG1_PCI_1_SIDE  		    0x094
#define MV64340_I2O_OUTBOUND_MESSAGE_REG0_PCI_1_SIDE 		    0x098
#define MV64340_I2O_OUTBOUND_MESSAGE_REG1_PCI_1_SIDE  		    0x09C
#define MV64340_I2O_INBOUND_DOORBELL_REG_PCI_1_SIDE  		    0x0A0
#define MV64340_I2O_INBOUND_INTERRUPT_CAUSE_REG_PCI_1_SIDE          0x0A4
#define MV64340_I2O_INBOUND_INTERRUPT_MASK_REG_PCI_1_SIDE	    0x0A8
#define MV64340_I2O_OUTBOUND_DOORBELL_REG_PCI_1_SIDE 		    0x0AC
#define MV64340_I2O_OUTBOUND_INTERRUPT_CAUSE_REG_PCI_1_SIDE         0x0B0
#define MV64340_I2O_OUTBOUND_INTERRUPT_MASK_REG_PCI_1_SIDE          0x0B4
#define MV64340_I2O_INBOUND_QUEUE_PORT_VIRTUAL_REG_PCI_1_SIDE       0x0C0
#define MV64340_I2O_OUTBOUND_QUEUE_PORT_VIRTUAL_REG_PCI_1_SIDE      0x0C4
#define MV64340_I2O_QUEUE_CONTROL_REG_PCI_1_SIDE 		    0x0D0
#define MV64340_I2O_QUEUE_BASE_ADDR_REG_PCI_1_SIDE 		    0x0D4
#define MV64340_I2O_INBOUND_FREE_HEAD_POINTER_REG_PCI_1_SIDE        0x0E0
#define MV64340_I2O_INBOUND_FREE_TAIL_POINTER_REG_PCI_1_SIDE        0x0E4
#define MV64340_I2O_INBOUND_POST_HEAD_POINTER_REG_PCI_1_SIDE        0x0E8
#define MV64340_I2O_INBOUND_POST_TAIL_POINTER_REG_PCI_1_SIDE        0x0EC
#define MV64340_I2O_OUTBOUND_FREE_HEAD_POINTER_REG_PCI_1_SIDE       0x0F0
#define MV64340_I2O_OUTBOUND_FREE_TAIL_POINTER_REG_PCI_1_SIDE       0x0F4
#define MV64340_I2O_OUTBOUND_POST_HEAD_POINTER_REG_PCI_1_SIDE       0x078
#define MV64340_I2O_OUTBOUND_POST_TAIL_POINTER_REG_PCI_1_SIDE       0x07C

#define MV64340_I2O_INBOUND_MESSAGE_REG0_CPU0_SIDE		    0x1C10
#define MV64340_I2O_INBOUND_MESSAGE_REG1_CPU0_SIDE  		    0x1C14
#define MV64340_I2O_OUTBOUND_MESSAGE_REG0_CPU0_SIDE 		    0x1C18
#define MV64340_I2O_OUTBOUND_MESSAGE_REG1_CPU0_SIDE  		    0x1C1C
#define MV64340_I2O_INBOUND_DOORBELL_REG_CPU0_SIDE  		    0x1C20
#define MV64340_I2O_INBOUND_INTERRUPT_CAUSE_REG_CPU0_SIDE  	    0x1C24
#define MV64340_I2O_INBOUND_INTERRUPT_MASK_REG_CPU0_SIDE	    0x1C28
#define MV64340_I2O_OUTBOUND_DOORBELL_REG_CPU0_SIDE 		    0x1C2C
#define MV64340_I2O_OUTBOUND_INTERRUPT_CAUSE_REG_CPU0_SIDE          0x1C30
#define MV64340_I2O_OUTBOUND_INTERRUPT_MASK_REG_CPU0_SIDE           0x1C34
#define MV64340_I2O_INBOUND_QUEUE_PORT_VIRTUAL_REG_CPU0_SIDE        0x1C40
#define MV64340_I2O_OUTBOUND_QUEUE_PORT_VIRTUAL_REG_CPU0_SIDE       0x1C44
#define MV64340_I2O_QUEUE_CONTROL_REG_CPU0_SIDE 		    0x1C50
#define MV64340_I2O_QUEUE_BASE_ADDR_REG_CPU0_SIDE 		    0x1C54
#define MV64340_I2O_INBOUND_FREE_HEAD_POINTER_REG_CPU0_SIDE         0x1C60
#define MV64340_I2O_INBOUND_FREE_TAIL_POINTER_REG_CPU0_SIDE         0x1C64
#define MV64340_I2O_INBOUND_POST_HEAD_POINTER_REG_CPU0_SIDE         0x1C68
#define MV64340_I2O_INBOUND_POST_TAIL_POINTER_REG_CPU0_SIDE         0x1C6C
#define MV64340_I2O_OUTBOUND_FREE_HEAD_POINTER_REG_CPU0_SIDE        0x1C70
#define MV64340_I2O_OUTBOUND_FREE_TAIL_POINTER_REG_CPU0_SIDE        0x1C74
#define MV64340_I2O_OUTBOUND_POST_HEAD_POINTER_REG_CPU0_SIDE        0x1CF8
#define MV64340_I2O_OUTBOUND_POST_TAIL_POINTER_REG_CPU0_SIDE        0x1CFC
#define MV64340_I2O_INBOUND_MESSAGE_REG0_CPU1_SIDE		    0x1C90
#define MV64340_I2O_INBOUND_MESSAGE_REG1_CPU1_SIDE  		    0x1C94
#define MV64340_I2O_OUTBOUND_MESSAGE_REG0_CPU1_SIDE 		    0x1C98
#define MV64340_I2O_OUTBOUND_MESSAGE_REG1_CPU1_SIDE  		    0x1C9C
#define MV64340_I2O_INBOUND_DOORBELL_REG_CPU1_SIDE  		    0x1CA0
#define MV64340_I2O_INBOUND_INTERRUPT_CAUSE_REG_CPU1_SIDE  	    0x1CA4
#define MV64340_I2O_INBOUND_INTERRUPT_MASK_REG_CPU1_SIDE	    0x1CA8
#define MV64340_I2O_OUTBOUND_DOORBELL_REG_CPU1_SIDE 		    0x1CAC
#define MV64340_I2O_OUTBOUND_INTERRUPT_CAUSE_REG_CPU1_SIDE          0x1CB0
#define MV64340_I2O_OUTBOUND_INTERRUPT_MASK_REG_CPU1_SIDE           0x1CB4
#define MV64340_I2O_INBOUND_QUEUE_PORT_VIRTUAL_REG_CPU1_SIDE        0x1CC0
#define MV64340_I2O_OUTBOUND_QUEUE_PORT_VIRTUAL_REG_CPU1_SIDE       0x1CC4
#define MV64340_I2O_QUEUE_CONTROL_REG_CPU1_SIDE 		    0x1CD0
#define MV64340_I2O_QUEUE_BASE_ADDR_REG_CPU1_SIDE 		    0x1CD4
#define MV64340_I2O_INBOUND_FREE_HEAD_POINTER_REG_CPU1_SIDE         0x1CE0
#define MV64340_I2O_INBOUND_FREE_TAIL_POINTER_REG_CPU1_SIDE         0x1CE4
#define MV64340_I2O_INBOUND_POST_HEAD_POINTER_REG_CPU1_SIDE         0x1CE8
#define MV64340_I2O_INBOUND_POST_TAIL_POINTER_REG_CPU1_SIDE         0x1CEC
#define MV64340_I2O_OUTBOUND_FREE_HEAD_POINTER_REG_CPU1_SIDE        0x1CF0
#define MV64340_I2O_OUTBOUND_FREE_TAIL_POINTER_REG_CPU1_SIDE        0x1CF4
#define MV64340_I2O_OUTBOUND_POST_HEAD_POINTER_REG_CPU1_SIDE        0x1C78
#define MV64340_I2O_OUTBOUND_POST_TAIL_POINTER_REG_CPU1_SIDE        0x1C7C

/****************************************/
/*        Ethernet Unit Registers  		*/
/****************************************/

#define MV643XX_ETH_SHARED_REGS                                     0x2000
#define MV643XX_ETH_SHARED_REGS_SIZE                                0x2000

#define MV643XX_ETH_PHY_ADDR_REG                                    0x2000
#define MV643XX_ETH_SMI_REG                                         0x2004
#define MV643XX_ETH_UNIT_DEFAULT_ADDR_REG                           0x2008
#define MV643XX_ETH_UNIT_DEFAULTID_REG                              0x200c
#define MV643XX_ETH_UNIT_INTERRUPT_CAUSE_REG                        0x2080
#define MV643XX_ETH_UNIT_INTERRUPT_MASK_REG                         0x2084
#define MV643XX_ETH_UNIT_INTERNAL_USE_REG                           0x24fc
#define MV643XX_ETH_UNIT_ERROR_ADDR_REG                             0x2094
#define MV643XX_ETH_BAR_0                                           0x2200
#define MV643XX_ETH_BAR_1                                           0x2208
#define MV643XX_ETH_BAR_2                                           0x2210
#define MV643XX_ETH_BAR_3                                           0x2218
#define MV643XX_ETH_BAR_4                                           0x2220
#define MV643XX_ETH_BAR_5                                           0x2228
#define MV643XX_ETH_SIZE_REG_0                                      0x2204
#define MV643XX_ETH_SIZE_REG_1                                      0x220c
#define MV643XX_ETH_SIZE_REG_2                                      0x2214
#define MV643XX_ETH_SIZE_REG_3                                      0x221c
#define MV643XX_ETH_SIZE_REG_4                                      0x2224
#define MV643XX_ETH_SIZE_REG_5                                      0x222c
#define MV643XX_ETH_HEADERS_RETARGET_BASE_REG                       0x2230
#define MV643XX_ETH_HEADERS_RETARGET_CONTROL_REG                    0x2234
#define MV643XX_ETH_HIGH_ADDR_REMAP_REG_0                           0x2280
#define MV643XX_ETH_HIGH_ADDR_REMAP_REG_1                           0x2284
#define MV643XX_ETH_HIGH_ADDR_REMAP_REG_2                           0x2288
#define MV643XX_ETH_HIGH_ADDR_REMAP_REG_3                           0x228c
#define MV643XX_ETH_BASE_ADDR_ENABLE_REG                            0x2290
#define MV643XX_ETH_ACCESS_PROTECTION_REG(port)                    (0x2294 + (port<<2))
#define MV643XX_ETH_MIB_COUNTERS_BASE(port)                        (0x3000 + (port<<7))
#define MV643XX_ETH_PORT_CONFIG_REG(port)                          (0x2400 + (port<<10))
#define MV643XX_ETH_PORT_CONFIG_EXTEND_REG(port)                   (0x2404 + (port<<10))
#define MV643XX_ETH_MII_SERIAL_PARAMETRS_REG(port)                 (0x2408 + (port<<10))
#define MV643XX_ETH_GMII_SERIAL_PARAMETRS_REG(port)                (0x240c + (port<<10))
#define MV643XX_ETH_VLAN_ETHERTYPE_REG(port)                       (0x2410 + (port<<10))
#define MV643XX_ETH_MAC_ADDR_LOW(port)                             (0x2414 + (port<<10))
#define MV643XX_ETH_MAC_ADDR_HIGH(port)                            (0x2418 + (port<<10))
#define MV643XX_ETH_SDMA_CONFIG_REG(port)                          (0x241c + (port<<10))
#define MV643XX_ETH_DSCP_0(port)                                   (0x2420 + (port<<10))
#define MV643XX_ETH_DSCP_1(port)                                   (0x2424 + (port<<10))
#define MV643XX_ETH_DSCP_2(port)                                   (0x2428 + (port<<10))
#define MV643XX_ETH_DSCP_3(port)                                   (0x242c + (port<<10))
#define MV643XX_ETH_DSCP_4(port)                                   (0x2430 + (port<<10))
#define MV643XX_ETH_DSCP_5(port)                                   (0x2434 + (port<<10))
#define MV643XX_ETH_DSCP_6(port)                                   (0x2438 + (port<<10))
#define MV643XX_ETH_PORT_SERIAL_CONTROL_REG(port)                  (0x243c + (port<<10))
#define MV643XX_ETH_VLAN_PRIORITY_TAG_TO_PRIORITY(port)            (0x2440 + (port<<10))
#define MV643XX_ETH_PORT_STATUS_REG(port)                          (0x2444 + (port<<10))
#define MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(port)               (0x2448 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_FIXED_PRIORITY(port)                  (0x244c + (port<<10))
#define MV643XX_ETH_PORT_TX_TOKEN_BUCKET_RATE_CONFIG(port)         (0x2450 + (port<<10))
#define MV643XX_ETH_MAXIMUM_TRANSMIT_UNIT(port)                    (0x2458 + (port<<10))
#define MV643XX_ETH_PORT_MAXIMUM_TOKEN_BUCKET_SIZE(port)           (0x245c + (port<<10))
#define MV643XX_ETH_INTERRUPT_CAUSE_REG(port)                      (0x2460 + (port<<10))
#define MV643XX_ETH_INTERRUPT_CAUSE_EXTEND_REG(port)               (0x2464 + (port<<10))
#define MV643XX_ETH_INTERRUPT_MASK_REG(port)                       (0x2468 + (port<<10))
#define MV643XX_ETH_INTERRUPT_EXTEND_MASK_REG(port)                (0x246c + (port<<10))
#define MV643XX_ETH_RX_FIFO_URGENT_THRESHOLD_REG(port)             (0x2470 + (port<<10))
#define MV643XX_ETH_TX_FIFO_URGENT_THRESHOLD_REG(port)             (0x2474 + (port<<10))
#define MV643XX_ETH_RX_MINIMAL_FRAME_SIZE_REG(port)                (0x247c + (port<<10))
#define MV643XX_ETH_RX_DISCARDED_FRAMES_COUNTER(port)              (0x2484 + (port<<10)
#define MV643XX_ETH_PORT_DEBUG_0_REG(port)                         (0x248c + (port<<10))
#define MV643XX_ETH_PORT_DEBUG_1_REG(port)                         (0x2490 + (port<<10))
#define MV643XX_ETH_PORT_INTERNAL_ADDR_ERROR_REG(port)             (0x2494 + (port<<10))
#define MV643XX_ETH_INTERNAL_USE_REG(port)                         (0x24fc + (port<<10))
#define MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port)                (0x2680 + (port<<10))
#define MV643XX_ETH_CURRENT_SERVED_TX_DESC_PTR(port)               (0x2684 + (port<<10))      
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_0(port)              (0x260c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_1(port)              (0x261c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_2(port)              (0x262c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_3(port)              (0x263c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_4(port)              (0x264c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_5(port)              (0x265c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_6(port)              (0x266c + (port<<10))     
#define MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_7(port)              (0x267c + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_0(port)              (0x26c0 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_1(port)              (0x26c4 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_2(port)              (0x26c8 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_3(port)              (0x26cc + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_4(port)              (0x26d0 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_5(port)              (0x26d4 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_6(port)              (0x26d8 + (port<<10))     
#define MV643XX_ETH_TX_CURRENT_QUEUE_DESC_PTR_7(port)              (0x26dc + (port<<10))     
#define MV643XX_ETH_TX_QUEUE_0_TOKEN_BUCKET_COUNT(port)            (0x2700 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_1_TOKEN_BUCKET_COUNT(port)            (0x2710 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_2_TOKEN_BUCKET_COUNT(port)            (0x2720 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_3_TOKEN_BUCKET_COUNT(port)            (0x2730 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_4_TOKEN_BUCKET_COUNT(port)            (0x2740 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_5_TOKEN_BUCKET_COUNT(port)            (0x2750 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_6_TOKEN_BUCKET_COUNT(port)            (0x2760 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_7_TOKEN_BUCKET_COUNT(port)            (0x2770 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_0_TOKEN_BUCKET_CONFIG(port)           (0x2704 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_1_TOKEN_BUCKET_CONFIG(port)           (0x2714 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_2_TOKEN_BUCKET_CONFIG(port)           (0x2724 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_3_TOKEN_BUCKET_CONFIG(port)           (0x2734 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_4_TOKEN_BUCKET_CONFIG(port)           (0x2744 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_5_TOKEN_BUCKET_CONFIG(port)           (0x2754 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_6_TOKEN_BUCKET_CONFIG(port)           (0x2764 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_7_TOKEN_BUCKET_CONFIG(port)           (0x2774 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_0_ARBITER_CONFIG(port)                (0x2708 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_1_ARBITER_CONFIG(port)                (0x2718 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_2_ARBITER_CONFIG(port)                (0x2728 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_3_ARBITER_CONFIG(port)                (0x2738 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_4_ARBITER_CONFIG(port)                (0x2748 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_5_ARBITER_CONFIG(port)                (0x2758 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_6_ARBITER_CONFIG(port)                (0x2768 + (port<<10))
#define MV643XX_ETH_TX_QUEUE_7_ARBITER_CONFIG(port)                (0x2778 + (port<<10))
#define MV643XX_ETH_PORT_TX_TOKEN_BUCKET_COUNT(port)               (0x2780 + (port<<10))
#define MV643XX_ETH_DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE(port)   (0x3400 + (port<<10))
#define MV643XX_ETH_DA_FILTER_OTHER_MULTICAST_TABLE_BASE(port)     (0x3500 + (port<<10))
#define MV643XX_ETH_DA_FILTER_UNICAST_TABLE_BASE(port)             (0x3600 + (port<<10))

/*******************************************/
/*          CUNIT  Registers               */
/*******************************************/

         /* Address Decoding Register Map */
           
#define MV64340_CUNIT_BASE_ADDR_REG0                                0xf200
#define MV64340_CUNIT_BASE_ADDR_REG1                                0xf208
#define MV64340_CUNIT_BASE_ADDR_REG2                                0xf210
#define MV64340_CUNIT_BASE_ADDR_REG3                                0xf218
#define MV64340_CUNIT_SIZE0                                         0xf204
#define MV64340_CUNIT_SIZE1                                         0xf20c
#define MV64340_CUNIT_SIZE2                                         0xf214
#define MV64340_CUNIT_SIZE3                                         0xf21c
#define MV64340_CUNIT_HIGH_ADDR_REMAP_REG0                          0xf240
#define MV64340_CUNIT_HIGH_ADDR_REMAP_REG1                          0xf244
#define MV64340_CUNIT_BASE_ADDR_ENABLE_REG                          0xf250
#define MV64340_MPSC0_ACCESS_PROTECTION_REG                         0xf254
#define MV64340_MPSC1_ACCESS_PROTECTION_REG                         0xf258
#define MV64340_CUNIT_INTERNAL_SPACE_BASE_ADDR_REG                  0xf25C

        /*  Error Report Registers  */

#define MV64340_CUNIT_INTERRUPT_CAUSE_REG                           0xf310
#define MV64340_CUNIT_INTERRUPT_MASK_REG                            0xf314
#define MV64340_CUNIT_ERROR_ADDR                                    0xf318

        /*  Cunit Control Registers */

#define MV64340_CUNIT_ARBITER_CONTROL_REG                           0xf300
#define MV64340_CUNIT_CONFIG_REG                                    0xb40c
#define MV64340_CUNIT_CRROSBAR_TIMEOUT_REG                          0xf304

        /*  Cunit Debug Registers   */

#define MV64340_CUNIT_DEBUG_LOW                                     0xf340
#define MV64340_CUNIT_DEBUG_HIGH                                    0xf344
#define MV64340_CUNIT_MMASK                                         0xf380

        /*  MPSCs Clocks Routing Registers  */

#define MV64340_MPSC_ROUTING_REG                                    0xb400
#define MV64340_MPSC_RX_CLOCK_ROUTING_REG                           0xb404
#define MV64340_MPSC_TX_CLOCK_ROUTING_REG                           0xb408

        /*  MPSCs Interrupts Registers    */

#define MV64340_MPSC_CAUSE_REG(port)                               (0xb804 + (port<<3))
#define MV64340_MPSC_MASK_REG(port)                                (0xb884 + (port<<3))
 
#define MV64340_MPSC_MAIN_CONFIG_LOW(port)                         (0x8000 + (port<<12))
#define MV64340_MPSC_MAIN_CONFIG_HIGH(port)                        (0x8004 + (port<<12))    
#define MV64340_MPSC_PROTOCOL_CONFIG(port)                         (0x8008 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG1(port)                            (0x800c + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG2(port)                            (0x8010 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG3(port)                            (0x8014 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG4(port)                            (0x8018 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG5(port)                            (0x801c + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG6(port)                            (0x8020 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG7(port)                            (0x8024 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG8(port)                            (0x8028 + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG9(port)                            (0x802c + (port<<12))    
#define MV64340_MPSC_CHANNEL_REG10(port)                           (0x8030 + (port<<12))    
        
        /*  MPSC0 Registers      */


/***************************************/
/*          SDMA Registers             */
/***************************************/

#define MV64340_SDMA_CONFIG_REG(channel)                        (0x4000 + (channel<<13))        
#define MV64340_SDMA_COMMAND_REG(channel)                       (0x4008 + (channel<<13))        
#define MV64340_SDMA_CURRENT_RX_DESCRIPTOR_POINTER(channel)     (0x4810 + (channel<<13))        
#define MV64340_SDMA_CURRENT_TX_DESCRIPTOR_POINTER(channel)     (0x4c10 + (channel<<13))        
#define MV64340_SDMA_FIRST_TX_DESCRIPTOR_POINTER(channel)       (0x4c14 + (channel<<13)) 

#define MV64340_SDMA_CAUSE_REG                                      0xb800
#define MV64340_SDMA_MASK_REG                                       0xb880
         
/* BRG Interrupts */

#define MV64340_BRG_CONFIG_REG(brg)                              (0xb200 + (brg<<3))
#define MV64340_BRG_BAUDE_TUNING_REG(brg)                        (0xb208 + (brg<<3))
#define MV64340_BRG_CAUSE_REG                                       0xb834
#define MV64340_BRG_MASK_REG                                        0xb8b4

/****************************************/
/* DMA Channel Control			*/
/****************************************/

#define MV64340_DMA_CHANNEL0_CONTROL 				    0x840
#define MV64340_DMA_CHANNEL0_CONTROL_HIGH			    0x880
#define MV64340_DMA_CHANNEL1_CONTROL 				    0x844
#define MV64340_DMA_CHANNEL1_CONTROL_HIGH			    0x884
#define MV64340_DMA_CHANNEL2_CONTROL 				    0x848
#define MV64340_DMA_CHANNEL2_CONTROL_HIGH			    0x888
#define MV64340_DMA_CHANNEL3_CONTROL 				    0x84C
#define MV64340_DMA_CHANNEL3_CONTROL_HIGH			    0x88C


/****************************************/
/*           IDMA Registers             */
/****************************************/

#define MV64340_DMA_CHANNEL0_BYTE_COUNT                             0x800
#define MV64340_DMA_CHANNEL1_BYTE_COUNT                             0x804
#define MV64340_DMA_CHANNEL2_BYTE_COUNT                             0x808
#define MV64340_DMA_CHANNEL3_BYTE_COUNT                             0x80C
#define MV64340_DMA_CHANNEL0_SOURCE_ADDR                            0x810
#define MV64340_DMA_CHANNEL1_SOURCE_ADDR                            0x814
#define MV64340_DMA_CHANNEL2_SOURCE_ADDR                            0x818
#define MV64340_DMA_CHANNEL3_SOURCE_ADDR                            0x81c
#define MV64340_DMA_CHANNEL0_DESTINATION_ADDR                       0x820
#define MV64340_DMA_CHANNEL1_DESTINATION_ADDR                       0x824
#define MV64340_DMA_CHANNEL2_DESTINATION_ADDR                       0x828
#define MV64340_DMA_CHANNEL3_DESTINATION_ADDR                       0x82C
#define MV64340_DMA_CHANNEL0_NEXT_DESCRIPTOR_POINTER                0x830
#define MV64340_DMA_CHANNEL1_NEXT_DESCRIPTOR_POINTER                0x834
#define MV64340_DMA_CHANNEL2_NEXT_DESCRIPTOR_POINTER                0x838
#define MV64340_DMA_CHANNEL3_NEXT_DESCRIPTOR_POINTER                0x83C
#define MV64340_DMA_CHANNEL0_CURRENT_DESCRIPTOR_POINTER             0x870
#define MV64340_DMA_CHANNEL1_CURRENT_DESCRIPTOR_POINTER             0x874
#define MV64340_DMA_CHANNEL2_CURRENT_DESCRIPTOR_POINTER             0x878
#define MV64340_DMA_CHANNEL3_CURRENT_DESCRIPTOR_POINTER             0x87C

 /*  IDMA Address Decoding Base Address Registers  */
 
#define MV64340_DMA_BASE_ADDR_REG0                                  0xa00
#define MV64340_DMA_BASE_ADDR_REG1                                  0xa08
#define MV64340_DMA_BASE_ADDR_REG2                                  0xa10
#define MV64340_DMA_BASE_ADDR_REG3                                  0xa18
#define MV64340_DMA_BASE_ADDR_REG4                                  0xa20
#define MV64340_DMA_BASE_ADDR_REG5                                  0xa28
#define MV64340_DMA_BASE_ADDR_REG6                                  0xa30
#define MV64340_DMA_BASE_ADDR_REG7                                  0xa38
 
 /*  IDMA Address Decoding Size Address Register   */
 
#define MV64340_DMA_SIZE_REG0                                       0xa04
#define MV64340_DMA_SIZE_REG1                                       0xa0c
#define MV64340_DMA_SIZE_REG2                                       0xa14
#define MV64340_DMA_SIZE_REG3                                       0xa1c
#define MV64340_DMA_SIZE_REG4                                       0xa24
#define MV64340_DMA_SIZE_REG5                                       0xa2c
#define MV64340_DMA_SIZE_REG6                                       0xa34
#define MV64340_DMA_SIZE_REG7                                       0xa3C

 /* IDMA Address Decoding High Address Remap and Access 
                  Protection Registers                    */
                  
#define MV64340_DMA_HIGH_ADDR_REMAP_REG0                            0xa60
#define MV64340_DMA_HIGH_ADDR_REMAP_REG1                            0xa64
#define MV64340_DMA_HIGH_ADDR_REMAP_REG2                            0xa68
#define MV64340_DMA_HIGH_ADDR_REMAP_REG3                            0xa6C
#define MV64340_DMA_BASE_ADDR_ENABLE_REG                            0xa80
#define MV64340_DMA_CHANNEL0_ACCESS_PROTECTION_REG                  0xa70
#define MV64340_DMA_CHANNEL1_ACCESS_PROTECTION_REG                  0xa74
#define MV64340_DMA_CHANNEL2_ACCESS_PROTECTION_REG                  0xa78
#define MV64340_DMA_CHANNEL3_ACCESS_PROTECTION_REG                  0xa7c
#define MV64340_DMA_ARBITER_CONTROL                                 0x860
#define MV64340_DMA_CROSS_BAR_TIMEOUT                               0x8d0

 /*  IDMA Headers Retarget Registers   */

#define MV64340_DMA_HEADERS_RETARGET_CONTROL                        0xa84
#define MV64340_DMA_HEADERS_RETARGET_BASE                           0xa88

 /*  IDMA Interrupt Register  */

#define MV64340_DMA_INTERRUPT_CAUSE_REG                             0x8c0
#define MV64340_DMA_INTERRUPT_CAUSE_MASK                            0x8c4
#define MV64340_DMA_ERROR_ADDR                                      0x8c8
#define MV64340_DMA_ERROR_SELECT                                    0x8cc

 /*  IDMA Debug Register ( for internal use )    */

#define MV64340_DMA_DEBUG_LOW                                       0x8e0
#define MV64340_DMA_DEBUG_HIGH                                      0x8e4
#define MV64340_DMA_SPARE                                           0xA8C

/****************************************/
/* Timer_Counter 			*/
/****************************************/

#define MV64340_TIMER_COUNTER0					    0x850
#define MV64340_TIMER_COUNTER1					    0x854
#define MV64340_TIMER_COUNTER2					    0x858
#define MV64340_TIMER_COUNTER3					    0x85C
#define MV64340_TIMER_COUNTER_0_3_CONTROL			    0x864
#define MV64340_TIMER_COUNTER_0_3_INTERRUPT_CAUSE		    0x868
#define MV64340_TIMER_COUNTER_0_3_INTERRUPT_MASK      		    0x86c

/****************************************/
/*         Watchdog registers  	        */
/****************************************/

#define MV64340_WATCHDOG_CONFIG_REG                                 0xb410
#define MV64340_WATCHDOG_VALUE_REG                                  0xb414

/****************************************/
/* I2C Registers                        */
/****************************************/

#define MV64XXX_I2C_CTLR_NAME					"mv64xxx_i2c"
#define MV64XXX_I2C_OFFSET                                          0xc000
#define MV64XXX_I2C_REG_BLOCK_SIZE                                  0x0020

/****************************************/
/* GPP Interface Registers              */
/****************************************/

#define MV64340_GPP_IO_CONTROL                                      0xf100
#define MV64340_GPP_LEVEL_CONTROL                                   0xf110
#define MV64340_GPP_VALUE                                           0xf104
#define MV64340_GPP_INTERRUPT_CAUSE                                 0xf108
#define MV64340_GPP_INTERRUPT_MASK0                                 0xf10c
#define MV64340_GPP_INTERRUPT_MASK1                                 0xf114
#define MV64340_GPP_VALUE_SET                                       0xf118
#define MV64340_GPP_VALUE_CLEAR                                     0xf11c

/****************************************/
/* Interrupt Controller Registers       */
/****************************************/

/****************************************/
/* Interrupts	  			*/
/****************************************/

#define MV64340_MAIN_INTERRUPT_CAUSE_LOW                            0x004
#define MV64340_MAIN_INTERRUPT_CAUSE_HIGH                           0x00c
#define MV64340_CPU_INTERRUPT0_MASK_LOW                             0x014
#define MV64340_CPU_INTERRUPT0_MASK_HIGH                            0x01c
#define MV64340_CPU_INTERRUPT0_SELECT_CAUSE                         0x024
#define MV64340_CPU_INTERRUPT1_MASK_LOW                             0x034
#define MV64340_CPU_INTERRUPT1_MASK_HIGH                            0x03c
#define MV64340_CPU_INTERRUPT1_SELECT_CAUSE                         0x044
#define MV64340_INTERRUPT0_MASK_0_LOW                               0x054
#define MV64340_INTERRUPT0_MASK_0_HIGH                              0x05c
#define MV64340_INTERRUPT0_SELECT_CAUSE                             0x064
#define MV64340_INTERRUPT1_MASK_0_LOW                               0x074
#define MV64340_INTERRUPT1_MASK_0_HIGH                              0x07c
#define MV64340_INTERRUPT1_SELECT_CAUSE                             0x084

/****************************************/
/*      MPP Interface Registers         */
/****************************************/

#define MV64340_MPP_CONTROL0                                        0xf000
#define MV64340_MPP_CONTROL1                                        0xf004
#define MV64340_MPP_CONTROL2                                        0xf008
#define MV64340_MPP_CONTROL3                                        0xf00c

/****************************************/
/*    Serial Initialization registers   */
/****************************************/

#define MV64340_SERIAL_INIT_LAST_DATA                               0xf324
#define MV64340_SERIAL_INIT_CONTROL                                 0xf328
#define MV64340_SERIAL_INIT_STATUS                                  0xf32c

extern void mv64340_irq_init(unsigned int base);

/* MPSC Platform Device, Driver Data (Shared register regions) */
#define	MPSC_SHARED_NAME		"mpsc_shared"

#define	MPSC_ROUTING_BASE_ORDER		0
#define	MPSC_SDMA_INTR_BASE_ORDER	1

#define MPSC_ROUTING_REG_BLOCK_SIZE	0x000c
#define MPSC_SDMA_INTR_REG_BLOCK_SIZE	0x0084

struct mpsc_shared_pdata {
	u32	mrr_val;
	u32	rcrr_val;
	u32	tcrr_val;
	u32	intr_cause_val;
	u32	intr_mask_val;
};

/* MPSC Platform Device, Driver Data */
#define	MPSC_CTLR_NAME			"mpsc"

#define	MPSC_BASE_ORDER			0
#define	MPSC_SDMA_BASE_ORDER		1
#define	MPSC_BRG_BASE_ORDER		2

#define MPSC_REG_BLOCK_SIZE		0x0038
#define MPSC_SDMA_REG_BLOCK_SIZE	0x0c18
#define MPSC_BRG_REG_BLOCK_SIZE		0x0008

struct mpsc_pdata {
	u8	mirror_regs;
	u8	cache_mgmt;
	u8	max_idle;
	int	default_baud;
	int	default_bits;
	int	default_parity;
	int	default_flow;
	u32	chr_1_val;
	u32	chr_2_val;
	u32	chr_10_val;
	u32	mpcr_val;
	u32	bcr_val;
	u8	brg_can_tune;
	u8	brg_clk_src;
	u32	brg_clk_freq;
};

/* i2c Platform Device, Driver Data */
struct mv64xxx_i2c_pdata {
	u32	freq_m;
	u32	freq_n;
	u32	timeout;	/* In milliseconds */
	u32	retries;
};

/* These macros describe Ethernet Port configuration reg (Px_cR) bits */
#define MV643XX_ETH_UNICAST_NORMAL_MODE		0
#define MV643XX_ETH_UNICAST_PROMISCUOUS_MODE	(1<<0)
#define MV643XX_ETH_DEFAULT_RX_QUEUE_0		0
#define MV643XX_ETH_DEFAULT_RX_QUEUE_1		(1<<1)
#define MV643XX_ETH_DEFAULT_RX_QUEUE_2		(1<<2)
#define MV643XX_ETH_DEFAULT_RX_QUEUE_3		((1<<2) | (1<<1))
#define MV643XX_ETH_DEFAULT_RX_QUEUE_4		(1<<3)
#define MV643XX_ETH_DEFAULT_RX_QUEUE_5		((1<<3) | (1<<1))
#define MV643XX_ETH_DEFAULT_RX_QUEUE_6		((1<<3) | (1<<2))
#define MV643XX_ETH_DEFAULT_RX_QUEUE_7		((1<<3) | (1<<2) | (1<<1))
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_0	0
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_1	(1<<4)
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_2	(1<<5)
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_3	((1<<5) | (1<<4))
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_4	(1<<6)
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_5	((1<<6) | (1<<4))
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_6	((1<<6) | (1<<5))
#define MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_7	((1<<6) | (1<<5) | (1<<4))
#define MV643XX_ETH_RECEIVE_BC_IF_NOT_IP_OR_ARP	0
#define MV643XX_ETH_REJECT_BC_IF_NOT_IP_OR_ARP	(1<<7)
#define MV643XX_ETH_RECEIVE_BC_IF_IP		0
#define MV643XX_ETH_REJECT_BC_IF_IP		(1<<8)
#define MV643XX_ETH_RECEIVE_BC_IF_ARP		0
#define MV643XX_ETH_REJECT_BC_IF_ARP		(1<<9)
#define MV643XX_ETH_TX_AM_NO_UPDATE_ERROR_SUMMARY (1<<12)
#define MV643XX_ETH_CAPTURE_TCP_FRAMES_DIS	0
#define MV643XX_ETH_CAPTURE_TCP_FRAMES_EN	(1<<14)
#define MV643XX_ETH_CAPTURE_UDP_FRAMES_DIS	0
#define MV643XX_ETH_CAPTURE_UDP_FRAMES_EN	(1<<15)
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_0	0
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_1	(1<<16)
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_2	(1<<17)
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_3	((1<<17) | (1<<16))
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_4	(1<<18)
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_5	((1<<18) | (1<<16))
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_6	((1<<18) | (1<<17))
#define MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_7	((1<<18) | (1<<17) | (1<<16))
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_0	0
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_1	(1<<19)
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_2	(1<<20)
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_3	((1<<20) | (1<<19))
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_4	((1<<21)
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_5	((1<<21) | (1<<19))
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_6	((1<<21) | (1<<20))
#define MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_7	((1<<21) | (1<<20) | (1<<19))
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_0	0
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_1	(1<<22)
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_2	(1<<23)
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_3	((1<<23) | (1<<22))
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_4	(1<<24)
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_5	((1<<24) | (1<<22))
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_6	((1<<24) | (1<<23))
#define MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_7	((1<<24) | (1<<23) | (1<<22))

#define	MV643XX_ETH_PORT_CONFIG_DEFAULT_VALUE			\
		MV643XX_ETH_UNICAST_NORMAL_MODE		|	\
		MV643XX_ETH_DEFAULT_RX_QUEUE_0		|	\
		MV643XX_ETH_DEFAULT_RX_ARP_QUEUE_0	|	\
		MV643XX_ETH_RECEIVE_BC_IF_NOT_IP_OR_ARP	|	\
		MV643XX_ETH_RECEIVE_BC_IF_IP		|	\
		MV643XX_ETH_RECEIVE_BC_IF_ARP		|	\
		MV643XX_ETH_CAPTURE_TCP_FRAMES_DIS	|	\
		MV643XX_ETH_CAPTURE_UDP_FRAMES_DIS	|	\
		MV643XX_ETH_DEFAULT_RX_TCP_QUEUE_0	|	\
		MV643XX_ETH_DEFAULT_RX_UDP_QUEUE_0	|	\
		MV643XX_ETH_DEFAULT_RX_BPDU_QUEUE_0

/* These macros describe Ethernet Port configuration extend reg (Px_cXR) bits*/
#define MV643XX_ETH_CLASSIFY_EN				(1<<0)
#define MV643XX_ETH_SPAN_BPDU_PACKETS_AS_NORMAL		0
#define MV643XX_ETH_SPAN_BPDU_PACKETS_TO_RX_QUEUE_7	(1<<1)
#define MV643XX_ETH_PARTITION_DISABLE			0
#define MV643XX_ETH_PARTITION_ENABLE			(1<<2)

#define	MV643XX_ETH_PORT_CONFIG_EXTEND_DEFAULT_VALUE		\
		MV643XX_ETH_SPAN_BPDU_PACKETS_AS_NORMAL	|	\
		MV643XX_ETH_PARTITION_DISABLE

/* These macros describe Ethernet Port Sdma configuration reg (SDCR) bits */
#define MV643XX_ETH_RIFB			(1<<0)
#define MV643XX_ETH_RX_BURST_SIZE_1_64BIT		0
#define MV643XX_ETH_RX_BURST_SIZE_2_64BIT		(1<<1)
#define MV643XX_ETH_RX_BURST_SIZE_4_64BIT		(1<<2)
#define MV643XX_ETH_RX_BURST_SIZE_8_64BIT		((1<<2) | (1<<1))
#define MV643XX_ETH_RX_BURST_SIZE_16_64BIT		(1<<3)
#define MV643XX_ETH_BLM_RX_NO_SWAP			(1<<4)
#define MV643XX_ETH_BLM_RX_BYTE_SWAP			0
#define MV643XX_ETH_BLM_TX_NO_SWAP			(1<<5)
#define MV643XX_ETH_BLM_TX_BYTE_SWAP			0
#define MV643XX_ETH_DESCRIPTORS_BYTE_SWAP		(1<<6)
#define MV643XX_ETH_DESCRIPTORS_NO_SWAP			0
#define MV643XX_ETH_TX_BURST_SIZE_1_64BIT		0
#define MV643XX_ETH_TX_BURST_SIZE_2_64BIT		(1<<22)
#define MV643XX_ETH_TX_BURST_SIZE_4_64BIT		(1<<23)
#define MV643XX_ETH_TX_BURST_SIZE_8_64BIT		((1<<23) | (1<<22))
#define MV643XX_ETH_TX_BURST_SIZE_16_64BIT		(1<<24)

#define	MV643XX_ETH_IPG_INT_RX(value) ((value & 0x3fff) << 8)

#define	MV643XX_ETH_PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		MV643XX_ETH_RX_BURST_SIZE_4_64BIT	|	\
		MV643XX_ETH_IPG_INT_RX(0)		|	\
		MV643XX_ETH_TX_BURST_SIZE_4_64BIT

/* These macros describe Ethernet Port serial control reg (PSCR) bits */
#define MV643XX_ETH_SERIAL_PORT_DISABLE			0
#define MV643XX_ETH_SERIAL_PORT_ENABLE			(1<<0)
#define MV643XX_ETH_FORCE_LINK_PASS			(1<<1)
#define MV643XX_ETH_DO_NOT_FORCE_LINK_PASS		0
#define MV643XX_ETH_ENABLE_AUTO_NEG_FOR_DUPLX		0
#define MV643XX_ETH_DISABLE_AUTO_NEG_FOR_DUPLX		(1<<2)
#define MV643XX_ETH_ENABLE_AUTO_NEG_FOR_FLOW_CTRL	0
#define MV643XX_ETH_DISABLE_AUTO_NEG_FOR_FLOW_CTRL	(1<<3)
#define MV643XX_ETH_ADV_NO_FLOW_CTRL			0
#define MV643XX_ETH_ADV_SYMMETRIC_FLOW_CTRL		(1<<4)
#define MV643XX_ETH_FORCE_FC_MODE_NO_PAUSE_DIS_TX	0
#define MV643XX_ETH_FORCE_FC_MODE_TX_PAUSE_DIS		(1<<5)
#define MV643XX_ETH_FORCE_BP_MODE_NO_JAM		0
#define MV643XX_ETH_FORCE_BP_MODE_JAM_TX		(1<<7)
#define MV643XX_ETH_FORCE_BP_MODE_JAM_TX_ON_RX_ERR	(1<<8)
#define MV643XX_ETH_SERIAL_PORT_CONTROL_RESERVED	(1<<9)
#define MV643XX_ETH_FORCE_LINK_FAIL			0
#define MV643XX_ETH_DO_NOT_FORCE_LINK_FAIL		(1<<10)
#define MV643XX_ETH_RETRANSMIT_16_ATTEMPTS		0
#define MV643XX_ETH_RETRANSMIT_FOREVER			(1<<11)
#define MV643XX_ETH_DISABLE_AUTO_NEG_SPEED_GMII		(1<<13)
#define MV643XX_ETH_ENABLE_AUTO_NEG_SPEED_GMII		0
#define MV643XX_ETH_DTE_ADV_0				0
#define MV643XX_ETH_DTE_ADV_1				(1<<14)
#define MV643XX_ETH_DISABLE_AUTO_NEG_BYPASS		0
#define MV643XX_ETH_ENABLE_AUTO_NEG_BYPASS		(1<<15)
#define MV643XX_ETH_AUTO_NEG_NO_CHANGE			0
#define MV643XX_ETH_RESTART_AUTO_NEG			(1<<16)
#define MV643XX_ETH_MAX_RX_PACKET_1518BYTE		0
#define MV643XX_ETH_MAX_RX_PACKET_1522BYTE		(1<<17)
#define MV643XX_ETH_MAX_RX_PACKET_1552BYTE		(1<<18)
#define MV643XX_ETH_MAX_RX_PACKET_9022BYTE		((1<<18) | (1<<17))
#define MV643XX_ETH_MAX_RX_PACKET_9192BYTE		(1<<19)
#define MV643XX_ETH_MAX_RX_PACKET_9700BYTE		((1<<19) | (1<<17))
#define MV643XX_ETH_SET_EXT_LOOPBACK			(1<<20)
#define MV643XX_ETH_CLR_EXT_LOOPBACK			0
#define MV643XX_ETH_SET_FULL_DUPLEX_MODE		(1<<21)
#define MV643XX_ETH_SET_HALF_DUPLEX_MODE		0
#define MV643XX_ETH_ENABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX (1<<22)
#define MV643XX_ETH_DISABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX 0
#define MV643XX_ETH_SET_GMII_SPEED_TO_10_100		0
#define MV643XX_ETH_SET_GMII_SPEED_TO_1000		(1<<23)
#define MV643XX_ETH_SET_MII_SPEED_TO_10			0
#define MV643XX_ETH_SET_MII_SPEED_TO_100		(1<<24)

#define MV643XX_ETH_MAX_RX_PACKET_MASK			(0x7<<17)

#define	MV643XX_ETH_PORT_SERIAL_CONTROL_DEFAULT_VALUE		\
		MV643XX_ETH_DO_NOT_FORCE_LINK_PASS	|	\
		MV643XX_ETH_ENABLE_AUTO_NEG_FOR_DUPLX	|	\
		MV643XX_ETH_DISABLE_AUTO_NEG_FOR_FLOW_CTRL |	\
		MV643XX_ETH_ADV_SYMMETRIC_FLOW_CTRL	|	\
		MV643XX_ETH_FORCE_FC_MODE_NO_PAUSE_DIS_TX |	\
		MV643XX_ETH_FORCE_BP_MODE_NO_JAM	|	\
		(1<<9)	/* reserved */			|	\
		MV643XX_ETH_DO_NOT_FORCE_LINK_FAIL	|	\
		MV643XX_ETH_RETRANSMIT_16_ATTEMPTS	|	\
		MV643XX_ETH_ENABLE_AUTO_NEG_SPEED_GMII	|	\
		MV643XX_ETH_DTE_ADV_0			|	\
		MV643XX_ETH_DISABLE_AUTO_NEG_BYPASS	|	\
		MV643XX_ETH_AUTO_NEG_NO_CHANGE		|	\
		MV643XX_ETH_MAX_RX_PACKET_9700BYTE	|	\
		MV643XX_ETH_CLR_EXT_LOOPBACK		|	\
		MV643XX_ETH_SET_FULL_DUPLEX_MODE	|	\
		MV643XX_ETH_ENABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX

/* These macros describe Ethernet Serial Status reg (PSR) bits */
#define MV643XX_ETH_PORT_STATUS_MODE_10_BIT		(1<<0)
#define MV643XX_ETH_PORT_STATUS_LINK_UP			(1<<1)
#define MV643XX_ETH_PORT_STATUS_FULL_DUPLEX		(1<<2)
#define MV643XX_ETH_PORT_STATUS_FLOW_CONTROL		(1<<3)
#define MV643XX_ETH_PORT_STATUS_GMII_1000		(1<<4)
#define MV643XX_ETH_PORT_STATUS_MII_100			(1<<5)
/* PSR bit 6 is undocumented */
#define MV643XX_ETH_PORT_STATUS_TX_IN_PROGRESS		(1<<7)
#define MV643XX_ETH_PORT_STATUS_AUTONEG_BYPASSED	(1<<8)
#define MV643XX_ETH_PORT_STATUS_PARTITION		(1<<9)
#define MV643XX_ETH_PORT_STATUS_TX_FIFO_EMPTY		(1<<10)
/* PSR bits 11-31 are reserved */

#define	MV643XX_ETH_PORT_DEFAULT_TRANSMIT_QUEUE_SIZE	800
#define	MV643XX_ETH_PORT_DEFAULT_RECEIVE_QUEUE_SIZE	400

#define MV643XX_ETH_DESC_SIZE				64

#define MV643XX_ETH_SHARED_NAME	"mv643xx_eth_shared"
#define MV643XX_ETH_NAME	"mv643xx_eth"

struct mv643xx_eth_platform_data {
	char		*mac_addr;	/* pointer to mac address */
	u16		force_phy_addr;	/* force override if phy_addr == 0 */
	u16		phy_addr;

	/* If speed is 0, then speed and duplex are autonegotiated. */
	int		speed;		/* 0, SPEED_10, SPEED_100, SPEED_1000 */
	int		duplex;		/* DUPLEX_HALF or DUPLEX_FULL */

	/* non-zero values of the following fields override defaults */
	u32		tx_queue_size;
	u32		rx_queue_size;
	u32		tx_sram_addr;
	u32		tx_sram_size;
	u32		rx_sram_addr;
	u32		rx_sram_size;
};

#endif /* __ASM_MV643XX_H */
