/*
* Huawei SSD device driver
* Copyright (c) 2016, Huawei Technologies Co., Ltd.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*/

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
#include <linux/config.h>
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/list.h>
#include <linux/reboot.h>
#include <linux/kthread.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#include <linux/seq_file.h>
#endif
#include <asm/uaccess.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0))
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#else
#include <asm/scatterlist.h>
#endif
#include <asm/io.h>
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,17))
#include <linux/devfs_fs_kernel.h>
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0))
#define bio_endio(bio, errors) bio_endio(bio)
#endif

/* driver */
#define MODULE_NAME		"hio"
#define DRIVER_VERSION	"2.1.0.23"
#define DRIVER_VERSION_LEN	16

#define SSD_FW_MIN		0x1

#define SSD_DEV_NAME	MODULE_NAME
#define SSD_DEV_NAME_LEN	16
#define SSD_CDEV_NAME	"c"SSD_DEV_NAME
#define SSD_SDEV_NAME	"s"SSD_DEV_NAME


#define SSD_CMAJOR		0
#define SSD_MAJOR		0
#define SSD_MAJOR_SL	0
#define SSD_MINORS		16

#define SSD_MAX_DEV		702
#define SSD_ALPHABET_NUM	26

#define hio_info(f, arg...) printk(KERN_INFO MODULE_NAME"info: " f , ## arg)
#define hio_note(f, arg...) printk(KERN_NOTICE MODULE_NAME"note: " f , ## arg)
#define hio_warn(f, arg...) printk(KERN_WARNING MODULE_NAME"warn: " f , ## arg)
#define hio_err(f, arg...)  printk(KERN_ERR MODULE_NAME"err: " f , ## arg)

/* slave port */
#define SSD_SLAVE_PORT_DEVID	0x000a

/* int mode */

/* 2.6.9 msi affinity bug, should turn msi & msi-x off */
//#define SSD_MSI
#define SSD_ESCAPE_IRQ

//#define SSD_MSIX
#ifndef MODULE
#define SSD_MSIX
#endif
#define SSD_MSIX_VEC	8
#ifdef SSD_MSIX
#undef SSD_MSI
//#undef SSD_ESCAPE_IRQ
#define SSD_MSIX_AFFINITY_FORCE
#endif

#define SSD_TRIM

/* Over temperature protect */
#define SSD_OT_PROTECT

#ifdef SSD_QUEUE_PBIO
#define BIO_SSD_PBIO		20
#endif

/* debug */
//#define SSD_DEBUG_ERR

/* cmd timer */
#define SSD_CMD_TIMEOUT		(60*HZ)

/* i2c & smbus */
#define SSD_SPI_TIMEOUT		(5*HZ)
#define SSD_I2C_TIMEOUT		(5*HZ)

#define SSD_I2C_MAX_DATA	(127)
#define SSD_SMBUS_BLOCK_MAX	(32)
#define SSD_SMBUS_DATA_MAX	(SSD_SMBUS_BLOCK_MAX + 2)

/* wait for init */
#define SSD_INIT_WAIT		(1000) //1s
#define SSD_CONTROLLER_WAIT	(20*1000/SSD_INIT_WAIT)	//20s
#define SSD_INIT_MAX_WAIT	(500*1000/SSD_INIT_WAIT) //500s
#define SSD_INIT_MAX_WAIT_V3_2	(1400*1000/SSD_INIT_WAIT) //1400s
#define SSD_RAM_INIT_MAX_WAIT	(10*1000/SSD_INIT_WAIT) //10s
#define SSD_CH_INFO_MAX_WAIT	(10*1000/SSD_INIT_WAIT) //10s

/* blkdev busy wait */
#define SSD_DEV_BUSY_WAIT	1000 //ms
#define SSD_DEV_BUSY_MAX_WAIT	(8*1000/SSD_DEV_BUSY_WAIT) //8s

/* smbus retry */
#define SSD_SMBUS_RETRY_INTERVAL	(5) //ms
#define SSD_SMBUS_RETRY_MAX			(1000/SSD_SMBUS_RETRY_INTERVAL)

#define SSD_BM_RETRY_MAX			7

/* bm routine interval */
#define SSD_BM_CAP_LEARNING_DELAY	(10*60*1000)

/* routine interval */
#define SSD_ROUTINE_INTERVAL		(10*1000)	//10s
#define SSD_HWMON_ROUTINE_TICK		(60*1000/SSD_ROUTINE_INTERVAL)
#define SSD_CAPMON_ROUTINE_TICK		((3600*1000/SSD_ROUTINE_INTERVAL)*24*30)
#define SSD_CAPMON2_ROUTINE_TICK	(10*60*1000/SSD_ROUTINE_INTERVAL)	//fault recover

/* dma align */
#define SSD_DMA_ALIGN		(16)

/* some hw defalut */
#define SSD_LOG_MAX_SZ		4096

#define SSD_NAND_OOB_SZ		1024
#define SSD_NAND_ID_SZ		8
#define SSD_NAND_ID_BUFF_SZ	1024
#define SSD_NAND_MAX_CE		2

#define SSD_BBT_RESERVED	8

#define SSD_ECC_MAX_FLIP	(64+1)

#define SSD_RAM_ALIGN		16


#define SSD_RELOAD_FLAG		0x3333CCCC
#define SSD_RELOAD_FW		0xAA5555AA
#define SSD_RESET_NOINIT	0xAA5555AA
#define SSD_RESET			0x55AAAA55
#define SSD_RESET_FULL		0x5A
//#define SSD_RESET_WAIT		1000	//1s
//#define SSD_RESET_MAX_WAIT	(200*1000/SSD_RESET_WAIT) //200s


/* reverion 1 */
#define SSD_PROTOCOL_V1			0x0

#define SSD_ROM_SIZE			(16*1024*1024)
#define SSD_ROM_BLK_SIZE		(256*1024)
#define SSD_ROM_PAGE_SIZE		(256)
#define SSD_ROM_NR_BRIDGE_FW	2
#define SSD_ROM_NR_CTRL_FW		2
#define SSD_ROM_BRIDGE_FW_BASE	0
#define SSD_ROM_BRIDGE_FW_SIZE	(2*1024*1024)
#define SSD_ROM_CTRL_FW_BASE	(SSD_ROM_NR_BRIDGE_FW*SSD_ROM_BRIDGE_FW_SIZE)
#define SSD_ROM_CTRL_FW_SIZE	(5*1024*1024)
#define SSD_ROM_LABEL_BASE		(SSD_ROM_CTRL_FW_BASE+SSD_ROM_CTRL_FW_SIZE*SSD_ROM_NR_CTRL_FW)
#define SSD_ROM_VP_BASE			(SSD_ROM_LABEL_BASE+SSD_ROM_BLK_SIZE)

/* reverion 3 */
#define SSD_PROTOCOL_V3			0x3000000
#define SSD_PROTOCOL_V3_1_1		0x3010001
#define SSD_PROTOCOL_V3_1_3		0x3010003
#define SSD_PROTOCOL_V3_2		0x3020000
#define SSD_PROTOCOL_V3_2_1		0x3020001	/* <4KB improved */
#define SSD_PROTOCOL_V3_2_2		0x3020002	/* ot protect */
#define SSD_PROTOCOL_V3_2_4		0x3020004


#define SSD_PV3_ROM_NR_BM_FW	1
#define SSD_PV3_ROM_BM_FW_SZ	(64*1024*8)

#define SSD_ROM_LOG_SZ			(64*1024*4)

#define SSD_ROM_NR_SMART_MAX	2
#define SSD_PV3_ROM_NR_SMART	SSD_ROM_NR_SMART_MAX
#define SSD_PV3_ROM_SMART_SZ	(64*1024)

/* reverion 3.2 */
#define SSD_PV3_2_ROM_LOG_SZ	(64*1024*80) /* 5MB */
#define SSD_PV3_2_ROM_SEC_SZ	(256*1024) /* 256KB */


/* register */
#define SSD_REQ_FIFO_REG		0x0000
#define SSD_RESP_FIFO_REG		0x0008	//0x0010
#define SSD_RESP_PTR_REG		0x0010	//0x0018
#define SSD_INTR_INTERVAL_REG	0x0018
#define SSD_READY_REG			0x001C
#define SSD_BRIDGE_TEST_REG		0x0020
#define SSD_STRIPE_SIZE_REG		0x0028
#define SSD_CTRL_VER_REG		0x0030	//controller
#define SSD_BRIDGE_VER_REG		0x0034	//bridge
#define SSD_PCB_VER_REG			0x0038
#define SSD_BURN_FLAG_REG		0x0040
#define SSD_BRIDGE_INFO_REG		0x0044

#define SSD_WL_VAL_REG			0x0048	//32-bit

#define SSD_BB_INFO_REG			0x004C

#define SSD_ECC_TEST_REG		0x0050 //test only
#define SSD_ERASE_TEST_REG		0x0058 //test only
#define SSD_WRITE_TEST_REG		0x0060 //test only

#define SSD_RESET_REG 			0x0068
#define SSD_RELOAD_FW_REG		0x0070

#define SSD_RESERVED_BLKS_REG	0x0074
#define SSD_VALID_PAGES_REG		0x0078
#define SSD_CH_INFO_REG			0x007C

#define SSD_CTRL_TEST_REG_SZ	0x8
#define SSD_CTRL_TEST_REG0		0x0080
#define SSD_CTRL_TEST_REG1		0x0088
#define SSD_CTRL_TEST_REG2		0x0090
#define SSD_CTRL_TEST_REG3		0x0098
#define SSD_CTRL_TEST_REG4		0x00A0
#define SSD_CTRL_TEST_REG5		0x00A8
#define SSD_CTRL_TEST_REG6		0x00B0
#define SSD_CTRL_TEST_REG7		0x00B8

#define SSD_FLASH_INFO_REG0		0x00C0
#define SSD_FLASH_INFO_REG1		0x00C8
#define SSD_FLASH_INFO_REG2		0x00D0
#define SSD_FLASH_INFO_REG3		0x00D8
#define SSD_FLASH_INFO_REG4		0x00E0
#define SSD_FLASH_INFO_REG5		0x00E8
#define SSD_FLASH_INFO_REG6		0x00F0
#define SSD_FLASH_INFO_REG7		0x00F8

#define SSD_RESP_INFO_REG		0x01B8
#define SSD_NAND_BUFF_BASE		0x01BC //for nand write

#define SSD_CHIP_INFO_REG_SZ	0x10
#define SSD_CHIP_INFO_REG0		0x0100	//128 bit
#define SSD_CHIP_INFO_REG1		0x0110
#define SSD_CHIP_INFO_REG2		0x0120
#define SSD_CHIP_INFO_REG3		0x0130
#define SSD_CHIP_INFO_REG4		0x0140
#define SSD_CHIP_INFO_REG5		0x0150
#define SSD_CHIP_INFO_REG6		0x0160
#define SSD_CHIP_INFO_REG7		0x0170

#define SSD_RAM_INFO_REG		0x01C4

#define SSD_BBT_BASE_REG		0x01C8
#define SSD_ECT_BASE_REG		0x01CC

#define SSD_CLEAR_INTR_REG		0x01F0

#define SSD_INIT_STATE_REG_SZ	0x8
#define SSD_INIT_STATE_REG0		0x0200
#define SSD_INIT_STATE_REG1		0x0208
#define SSD_INIT_STATE_REG2		0x0210
#define SSD_INIT_STATE_REG3		0x0218
#define SSD_INIT_STATE_REG4		0x0220
#define SSD_INIT_STATE_REG5		0x0228
#define SSD_INIT_STATE_REG6		0x0230
#define SSD_INIT_STATE_REG7		0x0238

#define SSD_ROM_INFO_REG		0x0600
#define SSD_ROM_BRIDGE_FW_INFO_REG	0x0604
#define SSD_ROM_CTRL_FW_INFO_REG	0x0608
#define SSD_ROM_VP_INFO_REG		0x060C

#define SSD_LOG_INFO_REG		0x0610
#define SSD_LED_REG				0x0614
#define SSD_MSG_BASE_REG		0x06F8

/*spi reg */
#define SSD_SPI_REG_CMD			0x0180
#define SSD_SPI_REG_CMD_HI		0x0184
#define SSD_SPI_REG_WDATA		0x0188
#define SSD_SPI_REG_ID			0x0190
#define SSD_SPI_REG_STATUS		0x0198
#define SSD_SPI_REG_RDATA		0x01A0
#define SSD_SPI_REG_READY		0x01A8

/* i2c register */
#define SSD_I2C_CTRL_REG		0x06F0
#define SSD_I2C_RDATA_REG		0x06F4

/* temperature reg */
#define SSD_BRIGE_TEMP_REG		0x0618

#define SSD_CTRL_TEMP_REG0		0x0700
#define SSD_CTRL_TEMP_REG1		0x0708
#define SSD_CTRL_TEMP_REG2		0x0710
#define SSD_CTRL_TEMP_REG3		0x0718
#define SSD_CTRL_TEMP_REG4		0x0720
#define SSD_CTRL_TEMP_REG5		0x0728
#define SSD_CTRL_TEMP_REG6		0x0730
#define SSD_CTRL_TEMP_REG7		0x0738

/* reversion 3 reg */
#define SSD_PROTOCOL_VER_REG	0x01B4

#define SSD_FLUSH_TIMEOUT_REG	0x02A4
#define SSD_BM_FAULT_REG		0x0660

#define SSD_PV3_RAM_STATUS_REG_SZ	0x4
#define SSD_PV3_RAM_STATUS_REG0	0x0260
#define SSD_PV3_RAM_STATUS_REG1	0x0264
#define SSD_PV3_RAM_STATUS_REG2	0x0268
#define SSD_PV3_RAM_STATUS_REG3	0x026C
#define SSD_PV3_RAM_STATUS_REG4	0x0270
#define SSD_PV3_RAM_STATUS_REG5	0x0274
#define SSD_PV3_RAM_STATUS_REG6	0x0278
#define SSD_PV3_RAM_STATUS_REG7	0x027C

#define SSD_PV3_CHIP_INFO_REG_SZ	0x40
#define SSD_PV3_CHIP_INFO_REG0	0x0300
#define SSD_PV3_CHIP_INFO_REG1	0x0340
#define SSD_PV3_CHIP_INFO_REG2	0x0380
#define SSD_PV3_CHIP_INFO_REG3	0x03B0
#define SSD_PV3_CHIP_INFO_REG4	0x0400
#define SSD_PV3_CHIP_INFO_REG5	0x0440
#define SSD_PV3_CHIP_INFO_REG6	0x0480
#define SSD_PV3_CHIP_INFO_REG7	0x04B0

#define SSD_PV3_INIT_STATE_REG_SZ 0x20
#define SSD_PV3_INIT_STATE_REG0	0x0500
#define SSD_PV3_INIT_STATE_REG1	0x0520
#define SSD_PV3_INIT_STATE_REG2	0x0540
#define SSD_PV3_INIT_STATE_REG3	0x0560
#define SSD_PV3_INIT_STATE_REG4	0x0580
#define SSD_PV3_INIT_STATE_REG5	0x05A0
#define SSD_PV3_INIT_STATE_REG6	0x05C0
#define SSD_PV3_INIT_STATE_REG7	0x05E0

/* reversion 3.1.1 reg */
#define SSD_FULL_RESET_REG		0x01B0

#define SSD_CTRL_REG_ZONE_SZ	0x800

#define SSD_BB_THRESHOLD_L1_REG	0x2C0
#define SSD_BB_THRESHOLD_L2_REG	0x2C4

#define SSD_BB_ACC_REG_SZ		0x4
#define SSD_BB_ACC_REG0			0x21C0
#define SSD_BB_ACC_REG1			0x29C0
#define SSD_BB_ACC_REG2			0x31C0

#define SSD_EC_THRESHOLD_L1_REG	0x2C8
#define SSD_EC_THRESHOLD_L2_REG	0x2CC

#define SSD_EC_ACC_REG_SZ		0x4
#define SSD_EC_ACC_REG0			0x21E0
#define SSD_EC_ACC_REG1			0x29E0
#define SSD_EC_ACC_REG2			0x31E0

/* reversion 3.1.2 & 3.1.3 reg */
#define SSD_HW_STATUS_REG		0x02AC

#define SSD_PLP_INFO_REG		0x0664

/*reversion 3.2 reg*/
#define SSD_POWER_ON_REG		0x01EC
#define SSD_PCIE_LINKSTATUS_REG	0x01F8
#define SSD_PL_CAP_LEARN_REG	0x01FC

#define SSD_FPGA_1V0_REG0		0x2070
#define SSD_FPGA_1V8_REG0		0x2078
#define SSD_FPGA_1V0_REG1		0x2870
#define SSD_FPGA_1V8_REG1		0x2878

/*reversion 3.2 reg*/
#define SSD_READ_OT_REG0		0x2260
#define SSD_WRITE_OT_REG0		0x2264
#define SSD_READ_OT_REG1		0x2A60
#define SSD_WRITE_OT_REG1		0x2A64


/* function */
#define SSD_FUNC_READ			0x01
#define SSD_FUNC_WRITE			0x02
#define SSD_FUNC_NAND_READ_WOOB	0x03
#define SSD_FUNC_NAND_READ		0x04
#define SSD_FUNC_NAND_WRITE		0x05
#define SSD_FUNC_NAND_ERASE		0x06
#define SSD_FUNC_NAND_READ_ID	0x07
#define SSD_FUNC_READ_LOG		0x08
#define SSD_FUNC_TRIM			0x09
#define SSD_FUNC_RAM_READ		0x10
#define SSD_FUNC_RAM_WRITE		0x11
#define SSD_FUNC_FLUSH			0x12	//cache / bbt

/* spi function */
#define SSD_SPI_CMD_PROGRAM		0x02
#define SSD_SPI_CMD_READ		0x03
#define SSD_SPI_CMD_W_DISABLE	0x04
#define SSD_SPI_CMD_READ_STATUS	0x05
#define SSD_SPI_CMD_W_ENABLE	0x06
#define SSD_SPI_CMD_ERASE		0xd8
#define SSD_SPI_CMD_CLSR		0x30
#define SSD_SPI_CMD_READ_ID		0x9f

/* i2c */
#define SSD_I2C_CTRL_READ		0x00
#define SSD_I2C_CTRL_WRITE		0x01

/* i2c internal register */
#define SSD_I2C_CFG_REG			0x00
#define SSD_I2C_DATA_REG		0x01
#define SSD_I2C_CMD_REG			0x02
#define SSD_I2C_STATUS_REG		0x03
#define SSD_I2C_SADDR_REG		0x04
#define SSD_I2C_LEN_REG			0x05
#define SSD_I2C_RLEN_REG		0x06
#define SSD_I2C_WLEN_REG		0x07
#define SSD_I2C_RESET_REG		0x08	//write for reset
#define SSD_I2C_PRER_REG		0x09


/* hw mon */
/* FPGA volt = ADC_value / 4096 * 3v */
#define SSD_FPGA_1V0_ADC_MIN	1228 // 0.9v
#define SSD_FPGA_1V0_ADC_MAX	1502 // 1.1v
#define SSD_FPGA_1V8_ADC_MIN	2211 // 1.62v
#define SSD_FPGA_1V8_ADC_MAX	2703 // 1.98

/* ADC value */
#define SSD_FPGA_VOLT_MAX(val)	(((val) & 0xffff) >> 4)
#define SSD_FPGA_VOLT_MIN(val)	(((val >> 16) & 0xffff) >> 4)
#define SSD_FPGA_VOLT_CUR(val)	(((val >> 32) & 0xffff) >> 4)
#define SSD_FPGA_VOLT(val)		((val * 3000) >> 12)

#define SSD_VOLT_LOG_DATA(idx, ctrl, volt)	(((uint32_t)idx << 24) | ((uint32_t)ctrl << 16) | ((uint32_t)volt))

enum ssd_fpga_volt
{
	SSD_FPGA_1V0 = 0, 
	SSD_FPGA_1V8, 
	SSD_FPGA_VOLT_NR 
};

enum ssd_clock
{
	SSD_CLOCK_166M_LOST = 0, 
	SSD_CLOCK_166M_SKEW, 
	SSD_CLOCK_156M_LOST, 
	SSD_CLOCK_156M_SKEW, 
	SSD_CLOCK_NR
};

/* sensor */
#define SSD_SENSOR_LM75_SADDRESS	(0x49 << 1)
#define SSD_SENSOR_LM80_SADDRESS	(0x28 << 1)

#define SSD_SENSOR_CONVERT_TEMP(val)	((int)(val >> 8))

#define SSD_INLET_OT_TEMP			(55)	//55 DegC
#define SSD_INLET_OT_HYST			(50)	//50 DegC
#define SSD_FLASH_OT_TEMP			(70)	//70 DegC
#define SSD_FLASH_OT_HYST			(65)	//65 DegC

enum ssd_sensor
{
	SSD_SENSOR_LM80 = 0,
	SSD_SENSOR_LM75,
	SSD_SENSOR_NR
};


/* lm75 */
enum ssd_lm75_reg
{
	SSD_LM75_REG_TEMP = 0, 
	SSD_LM75_REG_CONF, 
	SSD_LM75_REG_THYST, 
	SSD_LM75_REG_TOS
};

/* lm96080 */
#define SSD_LM80_REG_IN_MAX(nr)		(0x2a + (nr) * 2)
#define SSD_LM80_REG_IN_MIN(nr)		(0x2b + (nr) * 2)
#define SSD_LM80_REG_IN(nr)			(0x20 + (nr))

#define SSD_LM80_REG_FAN1			0x28
#define SSD_LM80_REG_FAN2			0x29
#define SSD_LM80_REG_FAN_MIN(nr)	(0x3b + (nr))

#define SSD_LM80_REG_TEMP			0x27
#define SSD_LM80_REG_TEMP_HOT_MAX	0x38
#define SSD_LM80_REG_TEMP_HOT_HYST	0x39
#define SSD_LM80_REG_TEMP_OS_MAX	0x3a
#define SSD_LM80_REG_TEMP_OS_HYST	0x3b

#define SSD_LM80_REG_CONFIG			0x00
#define SSD_LM80_REG_ALARM1			0x01
#define SSD_LM80_REG_ALARM2			0x02
#define SSD_LM80_REG_MASK1			0x03
#define SSD_LM80_REG_MASK2			0x04
#define SSD_LM80_REG_FANDIV			0x05
#define SSD_LM80_REG_RES			0x06

#define SSD_LM80_CONVERT_VOLT(val)	((val * 10) >> 8)

#define SSD_LM80_3V3_VOLT(val)		((val)*33/19)

#define SSD_LM80_CONV_INTERVAL		(1000)

enum ssd_lm80_in
{
	SSD_LM80_IN_CAP = 0, 
	SSD_LM80_IN_1V2, 
	SSD_LM80_IN_1V2a, 
	SSD_LM80_IN_1V5, 
	SSD_LM80_IN_1V8, 
	SSD_LM80_IN_FPGA_3V3, 
	SSD_LM80_IN_3V3, 
	SSD_LM80_IN_NR 
};

struct ssd_lm80_limit
{
	uint8_t low;
	uint8_t high;
};

/* +/- 5% except cap in*/
static struct ssd_lm80_limit ssd_lm80_limit[SSD_LM80_IN_NR] = {
	{171, 217}, /* CAP in: 1710 ~ 2170 */
	{114, 126}, 
	{114, 126}, 
	{142, 158}, 
	{171, 189}, 
	{180, 200}, 
	{180, 200}, 
};

/* temperature sensors */
enum ssd_temp_sensor
{
	SSD_TEMP_INLET = 0, 
	SSD_TEMP_FLASH, 
	SSD_TEMP_CTRL, 
	SSD_TEMP_NR 
};


#ifdef SSD_OT_PROTECT
#define SSD_OT_DELAY		(60) //ms

#define SSD_OT_TEMP			(90) //90 DegC

#define SSD_OT_TEMP_HYST	(85) //85 DegC
#endif

/* fpga temperature */
//#define CONVERT_TEMP(val)	((float)(val)*503.975f/4096.0f-273.15f)
#define CONVERT_TEMP(val)	((val)*504/4096-273)

#define MAX_TEMP(val)		CONVERT_TEMP(((val & 0xffff) >> 4))
#define MIN_TEMP(val)		CONVERT_TEMP((((val>>16) & 0xffff) >> 4))
#define CUR_TEMP(val)		CONVERT_TEMP((((val>>32) & 0xffff) >> 4))


/* CAP monitor */
#define SSD_PL_CAP_U1				SSD_LM80_REG_IN(SSD_LM80_IN_CAP)
#define SSD_PL_CAP_U2				SSD_LM80_REG_IN(SSD_LM80_IN_1V8)
#define SSD_PL_CAP_LEARN(u1, u2, t)	((t*(u1+u2))/(2*162*(u1-u2)))
#define SSD_PL_CAP_LEARN_WAIT		(20)	//20ms
#define SSD_PL_CAP_LEARN_MAX_WAIT	(1000/SSD_PL_CAP_LEARN_WAIT)	//1s

#define SSD_PL_CAP_CHARGE_WAIT		(1000)
#define SSD_PL_CAP_CHARGE_MAX_WAIT	((120*1000)/SSD_PL_CAP_CHARGE_WAIT)	//120s

#define SSD_PL_CAP_VOLT(val)		(val*7)

#define SSD_PL_CAP_VOLT_FULL		(13700)
#define SSD_PL_CAP_VOLT_READY		(12880)

#define SSD_PL_CAP_THRESHOLD		(8900)
#define SSD_PL_CAP_CP_THRESHOLD		(5800)
#define SSD_PL_CAP_THRESHOLD_HYST	(100)

enum ssd_pl_cap_status
{
	SSD_PL_CAP = 0, 
	SSD_PL_CAP_NR
};

enum ssd_pl_cap_type
{
	SSD_PL_CAP_DEFAULT = 0,	/* 4 cap */
	SSD_PL_CAP_CP	/* 3 cap */
};


/* hwmon offset */
#define SSD_HWMON_OFFS_TEMP			(0)
#define SSD_HWMON_OFFS_SENSOR		(SSD_HWMON_OFFS_TEMP + SSD_TEMP_NR)
#define SSD_HWMON_OFFS_PL_CAP		(SSD_HWMON_OFFS_SENSOR + SSD_SENSOR_NR)
#define SSD_HWMON_OFFS_LM80			(SSD_HWMON_OFFS_PL_CAP + SSD_PL_CAP_NR)
#define SSD_HWMON_OFFS_CLOCK		(SSD_HWMON_OFFS_LM80 + SSD_LM80_IN_NR)
#define SSD_HWMON_OFFS_FPGA 		(SSD_HWMON_OFFS_CLOCK + SSD_CLOCK_NR) 

#define SSD_HWMON_TEMP(idx) 		(SSD_HWMON_OFFS_TEMP + idx)
#define SSD_HWMON_SENSOR(idx) 		(SSD_HWMON_OFFS_SENSOR + idx)
#define SSD_HWMON_PL_CAP(idx)		(SSD_HWMON_OFFS_PL_CAP + idx)
#define SSD_HWMON_LM80(idx)			(SSD_HWMON_OFFS_LM80 + idx)
#define SSD_HWMON_CLOCK(idx)		(SSD_HWMON_OFFS_CLOCK + idx)
#define SSD_HWMON_FPGA(ctrl, idx)	(SSD_HWMON_OFFS_FPGA + (ctrl * SSD_FPGA_VOLT_NR) + idx)



/* fifo */
typedef struct sfifo
{
	uint32_t in;
	uint32_t out;
	uint32_t size;
	uint32_t esize;
	uint32_t mask;
	spinlock_t lock;
	void *data;
} sfifo_t;

static int sfifo_alloc(struct sfifo *fifo, uint32_t size, uint32_t esize)
{
	uint32_t __size = 1;

	if (!fifo || size > INT_MAX || esize == 0) {
		return -EINVAL;
	}

	while (__size < size) __size <<= 1;

	if (__size < 2) {
		return -EINVAL;
	}

	fifo->data = vmalloc(esize * __size);
	if (!fifo->data) {
		return -ENOMEM;
	}

	fifo->in = 0;
	fifo->out = 0;
	fifo->mask = __size - 1;
	fifo->size = __size;
	fifo->esize = esize;
	spin_lock_init(&fifo->lock);

	return 0;
}

static void sfifo_free(struct sfifo *fifo)
{
	if (!fifo) {
		return;
	}

	vfree(fifo->data);
	fifo->data = NULL;
	fifo->in = 0;
	fifo->out = 0;
	fifo->mask = 0;
	fifo->size = 0;
	fifo->esize = 0;
}

static int __sfifo_put(struct sfifo *fifo, void *val)
{
	if (((fifo->in + 1) & fifo->mask) == fifo->out) {
		return -1;
	}

	memcpy((fifo->data + (fifo->in * fifo->esize)), val, fifo->esize);
	fifo->in = (fifo->in + 1) & fifo->mask;

	return 0;
}

static int sfifo_put(struct sfifo *fifo, void *val)
{
	int ret = 0;

	if (!fifo || !val) {
		return -EINVAL;
	}
	
	if (!in_interrupt()) {
		spin_lock_irq(&fifo->lock);
		ret = __sfifo_put(fifo, val);
		spin_unlock_irq(&fifo->lock);
	} else {
		spin_lock(&fifo->lock);
		ret = __sfifo_put(fifo, val);
		spin_unlock(&fifo->lock);
	}

	return ret;
}

static int __sfifo_get(struct sfifo *fifo, void *val)
{
	if (fifo->out == fifo->in) {
		return -1;
	}

	memcpy(val, (fifo->data + (fifo->out * fifo->esize)), fifo->esize);
	fifo->out = (fifo->out + 1) & fifo->mask;

	return 0;
}

static int sfifo_get(struct sfifo *fifo, void *val)
{
	int ret = 0;

	if (!fifo || !val) {
		return -EINVAL;
	}

	if (!in_interrupt()) {
		spin_lock_irq(&fifo->lock);
		ret = __sfifo_get(fifo, val);
		spin_unlock_irq(&fifo->lock);
	} else {
		spin_lock(&fifo->lock);
		ret = __sfifo_get(fifo, val);
		spin_unlock(&fifo->lock);
	}

	return ret;
}

/* bio list */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
struct ssd_blist {
	struct bio *prev;
	struct bio *next;
};

static inline void ssd_blist_init(struct ssd_blist *ssd_bl)
{
	ssd_bl->prev = NULL;
	ssd_bl->next = NULL;
}

static inline struct bio *ssd_blist_get(struct ssd_blist *ssd_bl)
{
	struct bio *bio = ssd_bl->prev;

	ssd_bl->prev = NULL;
	ssd_bl->next = NULL;

	return bio;
}

static inline void ssd_blist_add(struct ssd_blist *ssd_bl, struct bio *bio)
{
	bio->bi_next = NULL;

	if (ssd_bl->next) {
		ssd_bl->next->bi_next = bio;
	} else {
		ssd_bl->prev = bio;
	}

	ssd_bl->next = bio;
}

#else
#define ssd_blist		bio_list
#define ssd_blist_init	bio_list_init
#define ssd_blist_get	bio_list_get
#define ssd_blist_add	bio_list_add
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define bio_start(bio)	(bio->bi_sector)
#else
#define bio_start(bio)	(bio->bi_iter.bi_sector)
#endif

/* mutex */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
#define mutex_lock down
#define mutex_unlock up
#define mutex semaphore
#define mutex_init init_MUTEX
#endif

/* i2c */
typedef union ssd_i2c_ctrl {
	uint32_t val;
	struct {
		uint8_t wdata;
		uint8_t addr;
		uint16_t rw:1;
		uint16_t pad:15;
	} bits;
}__attribute__((packed)) ssd_i2c_ctrl_t;

typedef union ssd_i2c_data {
	uint32_t val;
	struct {
		uint32_t rdata:8;
		uint32_t valid:1;
		uint32_t pad:23;
	} bits;
}__attribute__((packed)) ssd_i2c_data_t;

/* write mode */
enum ssd_write_mode
{
	SSD_WMODE_BUFFER = 0,
	SSD_WMODE_BUFFER_EX,
	SSD_WMODE_FUA,
	/* dummy */
	SSD_WMODE_AUTO, 
	SSD_WMODE_DEFAULT
};

/* reset type */
enum ssd_reset_type
{
	SSD_RST_NOINIT = 0,
	SSD_RST_NORMAL,
	SSD_RST_FULL
};

/* ssd msg */
typedef struct ssd_sg_entry
{
	uint64_t block:48;
	uint64_t length:16;
	uint64_t buf;
}__attribute__((packed))ssd_sg_entry_t;

typedef struct ssd_rw_msg
{
	uint8_t tag;
	uint8_t flag;
	uint8_t nsegs;
	uint8_t fun;
	uint32_t reserved;	//for 64-bit align
	struct ssd_sg_entry sge[1]; //base
}__attribute__((packed))ssd_rw_msg_t;

typedef struct ssd_resp_msg
{
	uint8_t tag;
	uint8_t status:2;
	uint8_t bitflip:6;
	uint8_t log;
	uint8_t fun;
	uint32_t reserved;
}__attribute__((packed))ssd_resp_msg_t;

typedef struct ssd_flush_msg
{
	uint8_t tag;
	uint8_t flag:2;	//flash cache 0 or bbt 1
	uint8_t flash:6;
	uint8_t ctrl_idx;
	uint8_t fun;
	uint32_t reserved;	//align
}__attribute__((packed))ssd_flush_msg_t;

typedef struct ssd_nand_op_msg
{
	uint8_t tag;
	uint8_t flag;
	uint8_t ctrl_idx;
	uint8_t fun;
	uint32_t reserved;	//align
	uint16_t page_count;
	uint8_t chip_ce;
	uint8_t chip_no;
	uint32_t page_no;
	uint64_t buf;
}__attribute__((packed))ssd_nand_op_msg_t;

typedef struct ssd_ram_op_msg
{
	uint8_t tag;
	uint8_t flag;
	uint8_t ctrl_idx;
	uint8_t fun;
	uint32_t reserved;	//align
	uint32_t start;
	uint32_t length;
	uint64_t buf;
}__attribute__((packed))ssd_ram_op_msg_t;


/* log msg */
typedef struct ssd_log_msg
{
	uint8_t tag;
	uint8_t flag;
	uint8_t ctrl_idx;
	uint8_t fun;
	uint32_t reserved;	//align
	uint64_t buf;
}__attribute__((packed))ssd_log_msg_t;

typedef struct ssd_log_op_msg
{
	uint8_t tag;
	uint8_t flag;
	uint8_t ctrl_idx;
	uint8_t fun;
	uint32_t reserved;	//align
	uint64_t reserved1;	//align
	uint64_t buf;
}__attribute__((packed))ssd_log_op_msg_t;

typedef struct ssd_log_resp_msg
{
	uint8_t tag;
	uint16_t status :2;
	uint16_t reserved1 :2;	//align with the normal resp msg
	uint16_t nr_log :12;
	uint8_t fun;
	uint32_t reserved;
}__attribute__((packed))ssd_log_resp_msg_t;


/* resp msg */
typedef union ssd_response_msq
{
	ssd_resp_msg_t resp_msg;
	ssd_log_resp_msg_t log_resp_msg;
	uint64_t u64_msg;
	uint32_t u32_msg[2];
} ssd_response_msq_t;


/* custom struct */
typedef struct ssd_protocol_info
{
	uint32_t ver;
	uint32_t init_state_reg;
	uint32_t init_state_reg_sz;
	uint32_t chip_info_reg;
	uint32_t chip_info_reg_sz;
} ssd_protocol_info_t;

typedef struct ssd_hw_info
{
	uint32_t bridge_ver;
	uint32_t ctrl_ver;

	uint32_t cmd_fifo_sz;
	uint32_t cmd_fifo_sz_mask;
	uint32_t cmd_max_sg;
	uint32_t sg_max_sec;
	uint32_t resp_ptr_sz;
	uint32_t resp_msg_sz;

	uint16_t nr_ctrl;

	uint16_t nr_data_ch;
	uint16_t nr_ch;
	uint16_t max_ch;
	uint16_t nr_chip;

	uint8_t  pcb_ver;
	uint8_t  upper_pcb_ver;

	uint8_t  nand_vendor_id;
	uint8_t  nand_dev_id;

	uint8_t  max_ce;
	uint8_t  id_size;
	uint16_t oob_size;

	uint16_t bbf_pages;
	uint16_t bbf_seek;	//

	uint16_t page_count;	//per block
	uint32_t page_size;
	uint32_t block_count;	//per flash

	uint64_t ram_size;
	uint32_t ram_align;
	uint32_t ram_max_len;

	uint64_t bbt_base;
	uint32_t bbt_size;
	uint64_t md_base; //metadata
	uint32_t md_size;
	uint32_t md_entry_sz;

	uint32_t log_sz;

	uint64_t nand_wbuff_base;

	uint32_t md_reserved_blks;
	uint32_t reserved_blks;
	uint32_t valid_pages;
	uint32_t max_valid_pages;
	uint64_t size;
} ssd_hw_info_t;

typedef struct ssd_hw_info_extend
{
	uint8_t board_type;
	uint8_t cap_type;
	uint8_t plp_type;
	uint8_t work_mode;
	uint8_t form_factor;

	uint8_t pad[59];
}ssd_hw_info_extend_t;

typedef struct ssd_rom_info
{
	uint32_t size;
	uint32_t block_size;
	uint16_t page_size;
	uint8_t  nr_bridge_fw;
	uint8_t  nr_ctrl_fw;
	uint8_t  nr_bm_fw;
	uint8_t  nr_smart;
	uint32_t bridge_fw_base;
	uint32_t bridge_fw_sz;
	uint32_t ctrl_fw_base;
	uint32_t ctrl_fw_sz;
	uint32_t bm_fw_base;
	uint32_t bm_fw_sz;
	uint32_t log_base;
	uint32_t log_sz;
	uint32_t smart_base;
	uint32_t smart_sz;
	uint32_t vp_base;
	uint32_t label_base;
} ssd_rom_info_t;

/* debug info */
enum ssd_debug_type
{
	SSD_DEBUG_NONE = 0, 
	SSD_DEBUG_READ_ERR, 
	SSD_DEBUG_WRITE_ERR, 
	SSD_DEBUG_RW_ERR, 
	SSD_DEBUG_READ_TO, 
	SSD_DEBUG_WRITE_TO, 
	SSD_DEBUG_RW_TO, 
	SSD_DEBUG_LOG, 
	SSD_DEBUG_OFFLINE, 
	SSD_DEBUG_NR
};

typedef struct ssd_debug_info
{
	int type;
	union {
		struct {
			uint64_t off;
			uint32_t len;
		} loc;
		struct {
			int event;
			uint32_t extra;
		} log;
	} data;
}ssd_debug_info_t;

/* label */
#define SSD_LABEL_FIELD_SZ	32
#define SSD_SN_SZ			16

typedef struct ssd_label
{
	char date[SSD_LABEL_FIELD_SZ];
	char sn[SSD_LABEL_FIELD_SZ];
	char part[SSD_LABEL_FIELD_SZ];
	char desc[SSD_LABEL_FIELD_SZ];
	char other[SSD_LABEL_FIELD_SZ];
	char maf[SSD_LABEL_FIELD_SZ];
} ssd_label_t;

#define SSD_LABEL_DESC_SZ	256

typedef struct ssd_labelv3
{
	char boardtype[SSD_LABEL_FIELD_SZ];
	char barcode[SSD_LABEL_FIELD_SZ];
	char item[SSD_LABEL_FIELD_SZ];
	char description[SSD_LABEL_DESC_SZ];
	char manufactured[SSD_LABEL_FIELD_SZ];
	char vendorname[SSD_LABEL_FIELD_SZ];
	char issuenumber[SSD_LABEL_FIELD_SZ];
	char cleicode[SSD_LABEL_FIELD_SZ];
	char bom[SSD_LABEL_FIELD_SZ];
} ssd_labelv3_t;

/* battery */
typedef struct ssd_battery_info
{
	uint32_t fw_ver;
} ssd_battery_info_t;

/* ssd power stat */
typedef struct ssd_power_stat
{
	uint64_t nr_poweron;
	uint64_t nr_powerloss;
	uint64_t init_failed;
} ssd_power_stat_t;

/* io stat */
typedef struct ssd_io_stat
{
	uint64_t run_time;
	uint64_t nr_to;
	uint64_t nr_ioerr;
	uint64_t nr_rwerr;
	uint64_t nr_read;
	uint64_t nr_write;
	uint64_t rsectors;
	uint64_t wsectors;
} ssd_io_stat_t;

/* ecc */
typedef struct ssd_ecc_info
{
	uint64_t bitflip[SSD_ECC_MAX_FLIP];
} ssd_ecc_info_t;

/* log */
enum ssd_log_level
{
	SSD_LOG_LEVEL_INFO = 0,
	SSD_LOG_LEVEL_NOTICE,
	SSD_LOG_LEVEL_WARNING,
	SSD_LOG_LEVEL_ERR,
	SSD_LOG_NR_LEVEL
};

typedef struct ssd_log_info
{
	uint64_t nr_log;
	uint64_t stat[SSD_LOG_NR_LEVEL];
} ssd_log_info_t;

/* S.M.A.R.T. */
#define SSD_SMART_MAGIC	(0x5452414D53445353ull)

typedef struct ssd_smart
{
	struct ssd_power_stat pstat;
	struct ssd_io_stat io_stat;
	struct ssd_ecc_info ecc_info;
	struct ssd_log_info log_info;
	uint64_t version;
	uint64_t magic;
} ssd_smart_t;

/* internal log */
typedef struct ssd_internal_log
{
	uint32_t nr_log;
	void *log;
} ssd_internal_log_t;

/* ssd cmd */
typedef struct ssd_cmd
{
	struct bio *bio;
	struct scatterlist *sgl;
	struct list_head list;
	void *dev;
	int nsegs;
	int flag;		/*pbio(1) or bio(0)*/

	int tag;
	void *msg;
	dma_addr_t msg_dma;

	unsigned long start_time;

	int errors;
	unsigned int nr_log;

	struct timer_list cmd_timer;
	struct completion *waiting;
} ssd_cmd_t;

typedef void (*send_cmd_func)(struct ssd_cmd *);
typedef int (*ssd_event_call)(struct gendisk *, int, int);	/* gendisk, event id, event level */

/* dcmd sz */
#define SSD_DCMD_MAX_SZ 32

typedef struct ssd_dcmd
{
	struct list_head list;
	void *dev;
	uint8_t msg[SSD_DCMD_MAX_SZ];
} ssd_dcmd_t;


enum ssd_state {
	SSD_INIT_WORKQ, 
	SSD_INIT_BD, 
	SSD_ONLINE, 
	/* full reset */
	SSD_RESETING, 
	/* hw log */
	SSD_LOG_HW, 
	/* log err */
	SSD_LOG_ERR
};

#define SSD_QUEUE_NAME_LEN	16
typedef struct ssd_queue {
	char name[SSD_QUEUE_NAME_LEN];
	void *dev;

	int idx;

	uint32_t resp_idx;
	uint32_t resp_idx_mask;
	uint32_t resp_msg_sz;

	void *resp_msg;
	void *resp_ptr;

	struct ssd_cmd *cmd;

	struct ssd_io_stat io_stat;
	struct ssd_ecc_info ecc_info;
} ssd_queue_t;

typedef struct ssd_device {
	char name[SSD_DEV_NAME_LEN];

	int idx;
	int major;
	int readonly;

	int int_mode;
#ifdef SSD_ESCAPE_IRQ
	int irq_cpu;
#endif

	int reload_fw;

	int ot_delay; //in ms

	atomic_t refcnt;
	atomic_t tocnt;
	atomic_t in_flight[2]; //r&w

	uint64_t uptime;

	struct list_head list;
	struct pci_dev *pdev;

	unsigned long mmio_base;
	unsigned long mmio_len;
	void __iomem *ctrlp;

	struct mutex spi_mutex;
	struct mutex i2c_mutex;

	struct ssd_protocol_info protocol_info;
	struct ssd_hw_info hw_info;
	struct ssd_rom_info rom_info;
	struct ssd_label label;

	struct ssd_smart smart;

	atomic_t in_sendq;
	spinlock_t sendq_lock;
	struct ssd_blist sendq;
	struct task_struct *send_thread;
	wait_queue_head_t send_waitq;

	atomic_t in_doneq;
	spinlock_t doneq_lock;
	struct ssd_blist doneq;
	struct task_struct *done_thread;
	wait_queue_head_t done_waitq;

	struct ssd_dcmd *dcmd;
	spinlock_t dcmd_lock;
	struct list_head dcmd_list; /* direct cmd list */
	wait_queue_head_t dcmd_wq;

	unsigned long *tag_map;
	wait_queue_head_t tag_wq;

	spinlock_t cmd_lock;
	struct ssd_cmd *cmd;
	send_cmd_func scmd;

	ssd_event_call event_call;
	void *msg_base;
	dma_addr_t msg_base_dma;

	uint32_t resp_idx;
	void *resp_msg_base;
	void *resp_ptr_base;
	dma_addr_t resp_msg_base_dma;
	dma_addr_t resp_ptr_base_dma;

	int nr_queue;
	struct msix_entry entry[SSD_MSIX_VEC];
	struct ssd_queue queue[SSD_MSIX_VEC];

	struct request_queue *rq; /* The device request queue */
	struct gendisk *gd; /* The gendisk structure */

	struct mutex internal_log_mutex;
	struct ssd_internal_log internal_log;
	struct workqueue_struct *workq;
	struct work_struct log_work; /* get log */
	void *log_buf;

	unsigned long state; /* device state, for example, block device inited */

	struct module *owner;

	/* extend */

	int slave;
	int cmajor;
	int save_md;
	int ot_protect;

	struct kref kref;

	struct mutex gd_mutex;
	struct ssd_log_info log_info; /* volatile */

	atomic_t queue_depth;
	struct mutex barrier_mutex;
	struct mutex fw_mutex;

	struct ssd_hw_info_extend hw_info_ext;
	struct ssd_labelv3 labelv3;

	int wmode;
	int user_wmode;
	struct mutex bm_mutex;
	struct work_struct bm_work; /* check bm */
	struct timer_list bm_timer;
	struct sfifo log_fifo;

	struct timer_list routine_timer;
	unsigned long routine_tick;
	unsigned long hwmon;

	struct work_struct hwmon_work; /* check hw */
	struct work_struct capmon_work; /* check battery */
	struct work_struct tempmon_work; /* check temp */

	/* debug info */
	struct ssd_debug_info db_info;
} ssd_device_t;


/* Ioctl struct */
typedef struct ssd_acc_info {
	uint32_t threshold_l1;
	uint32_t threshold_l2;
	uint32_t val;
} ssd_acc_info_t;

typedef struct ssd_reg_op_info
{
	uint32_t offset;
	uint32_t value;
} ssd_reg_op_info_t;

typedef struct ssd_spi_op_info
{
	void __user *buf;
	uint32_t off;
	uint32_t len;
} ssd_spi_op_info_t;

typedef struct ssd_i2c_op_info
{
	uint8_t saddr;
	uint8_t wsize;
	uint8_t rsize;
	void __user *wbuf;
	void __user *rbuf;
} ssd_i2c_op_info_t;

typedef struct ssd_smbus_op_info
{
	uint8_t saddr;
	uint8_t cmd;
	uint8_t size;
	void __user *buf;
} ssd_smbus_op_info_t;

typedef struct ssd_ram_op_info {
	uint8_t ctrl_idx;
	uint32_t length;
	uint64_t start;
	uint8_t __user *buf;
} ssd_ram_op_info_t;

typedef struct ssd_flash_op_info {
	uint32_t page;
	uint16_t flash;
	uint8_t chip;
	uint8_t ctrl_idx;
	uint8_t __user *buf;
} ssd_flash_op_info_t;

typedef struct ssd_sw_log_info {
	uint16_t event;
	uint16_t pad;
	uint32_t data;
} ssd_sw_log_info_t;

typedef struct ssd_version_info
{
	uint32_t bridge_ver;	/* bridge fw version */
	uint32_t ctrl_ver;		/* controller fw version */
	uint32_t bm_ver;		/* battery manager fw version */
	uint8_t  pcb_ver;		/* main pcb version */
	uint8_t  upper_pcb_ver;
	uint8_t  pad0;
	uint8_t  pad1;
} ssd_version_info_t;

typedef struct pci_addr
{
	uint16_t domain;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
} pci_addr_t;

typedef struct ssd_drv_param_info {
	int mode;
	int status_mask;
	int int_mode;
	int threaded_irq;
	int log_level;
	int wmode;
	int ot_protect;
	int finject;
	int pad[8];
} ssd_drv_param_info_t;


/* form factor */
enum ssd_form_factor
{
	SSD_FORM_FACTOR_HHHL = 0, 
	SSD_FORM_FACTOR_FHHL
};


/* ssd power loss protect */
enum ssd_plp_type
{
	SSD_PLP_SCAP = 0,
	SSD_PLP_CAP,
	SSD_PLP_NONE
};

/* ssd bm */
#define SSD_BM_SLAVE_ADDRESS	0x16
#define SSD_BM_CAP	5

/* SBS cmd */
#define SSD_BM_SAFETYSTATUS			0x51
#define SSD_BM_OPERATIONSTATUS		0x54

/* ManufacturerAccess */
#define SSD_BM_MANUFACTURERACCESS	0x00		
#define SSD_BM_ENTER_CAP_LEARNING	0x0023		/* cap learning */

/* Data flash access */
#define SSD_BM_DATA_FLASH_SUBCLASS_ID 0x77
#define SSD_BM_DATA_FLASH_SUBCLASS_ID_PAGE1 0x78
#define SSD_BM_SYSTEM_DATA_SUBCLASS_ID	56
#define SSD_BM_CONFIGURATION_REGISTERS_ID	64

/* min cap voltage */
#define SSD_BM_CAP_VOLT_MIN			500

/*
enum ssd_bm_cap
{
	SSD_BM_CAP_VINA = 1, 
	SSD_BM_CAP_JH = 3
};*/

enum ssd_bmstatus
{
	SSD_BMSTATUS_OK = 0,
	SSD_BMSTATUS_CHARGING, 		/* not fully charged */
	SSD_BMSTATUS_WARNING
};

enum sbs_unit {
	SBS_UNIT_VALUE = 0,
	SBS_UNIT_TEMPERATURE,
	SBS_UNIT_VOLTAGE,
	SBS_UNIT_CURRENT,
	SBS_UNIT_ESR,
	SBS_UNIT_PERCENT,
	SBS_UNIT_CAPACITANCE
};

enum sbs_size {
	SBS_SIZE_BYTE = 1,
	SBS_SIZE_WORD,
	SBS_SIZE_BLK,
};

struct sbs_cmd {
	uint8_t cmd;
	uint8_t size;
	uint8_t unit;
	uint8_t off;
	uint16_t mask;
	char *desc;
};

struct ssd_bm {
	uint16_t temp;
	uint16_t volt;
	uint16_t curr;
	uint16_t esr;
	uint16_t rsoc;
	uint16_t health;
	uint16_t cap;
	uint16_t chg_curr;
	uint16_t chg_volt;
	uint16_t cap_volt[SSD_BM_CAP];
	uint16_t sf_alert;
	uint16_t sf_status;
	uint16_t op_status;
	uint16_t sys_volt;
};

struct ssd_bm_manufacturer_data
{
	uint16_t pack_lot_code;
	uint16_t pcb_lot_code;
	uint16_t firmware_ver;
	uint16_t hardware_ver;
};

struct ssd_bm_configuration_registers
{
	struct {
		uint16_t cc:3;
		uint16_t rsvd:5;
		uint16_t stack:1;
		uint16_t rsvd1:2;
		uint16_t temp:2;
		uint16_t rsvd2:1;
		uint16_t lt_en:1;
		uint16_t rsvd3:1;
	} operation_cfg;
	uint16_t pad;
	uint16_t fet_action;
	uint16_t pad1;
	uint16_t fault;
};

#define SBS_VALUE_MASK	0xffff

#define bm_var_offset(var)	((size_t) &((struct ssd_bm *)0)->var)
#define bm_var(start, offset)	((void *) start + (offset))

static struct sbs_cmd ssd_bm_sbs[] = {
	{0x08, SBS_SIZE_WORD, SBS_UNIT_TEMPERATURE, bm_var_offset(temp), SBS_VALUE_MASK, "Temperature"}, 
	{0x09, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, bm_var_offset(volt), SBS_VALUE_MASK, "Voltage"}, 
	{0x0a, SBS_SIZE_WORD, SBS_UNIT_CURRENT, bm_var_offset(curr), SBS_VALUE_MASK, "Current"}, 
	{0x0b, SBS_SIZE_WORD, SBS_UNIT_ESR, bm_var_offset(esr), SBS_VALUE_MASK, "ESR"}, 
	{0x0d, SBS_SIZE_BYTE, SBS_UNIT_PERCENT, bm_var_offset(rsoc), SBS_VALUE_MASK, "RelativeStateOfCharge"}, 
	{0x0e, SBS_SIZE_BYTE, SBS_UNIT_PERCENT, bm_var_offset(health), SBS_VALUE_MASK, "Health"}, 
	{0x10, SBS_SIZE_WORD, SBS_UNIT_CAPACITANCE, bm_var_offset(cap), SBS_VALUE_MASK, "Capacitance"}, 
	{0x14, SBS_SIZE_WORD, SBS_UNIT_CURRENT, bm_var_offset(chg_curr), SBS_VALUE_MASK, "ChargingCurrent"}, 
	{0x15, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, bm_var_offset(chg_volt), SBS_VALUE_MASK, "ChargingVoltage"}, 
	{0x3b, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, (uint8_t)bm_var_offset(cap_volt[4]), SBS_VALUE_MASK, "CapacitorVoltage5"}, 
	{0x3c, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, (uint8_t)bm_var_offset(cap_volt[3]), SBS_VALUE_MASK, "CapacitorVoltage4"}, 
	{0x3d, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, (uint8_t)bm_var_offset(cap_volt[2]), SBS_VALUE_MASK, "CapacitorVoltage3"}, 
	{0x3e, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, (uint8_t)bm_var_offset(cap_volt[1]), SBS_VALUE_MASK, "CapacitorVoltage2"}, 
	{0x3f, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, (uint8_t)bm_var_offset(cap_volt[0]), SBS_VALUE_MASK, "CapacitorVoltage1"}, 
	{0x50, SBS_SIZE_WORD, SBS_UNIT_VALUE, bm_var_offset(sf_alert), 0x870F, "SafetyAlert"}, 
	{0x51, SBS_SIZE_WORD, SBS_UNIT_VALUE, bm_var_offset(sf_status), 0xE7BF, "SafetyStatus"}, 
	{0x54, SBS_SIZE_WORD, SBS_UNIT_VALUE, bm_var_offset(op_status), 0x79F4, "OperationStatus"}, 
	{0x5a, SBS_SIZE_WORD, SBS_UNIT_VOLTAGE, bm_var_offset(sys_volt), SBS_VALUE_MASK, "SystemVoltage"}, 
	{0, 0, 0, 0, 0, NULL}, 
};

/* ssd ioctl  */
#define SSD_CMD_GET_PROTOCOL_INFO	_IOR('H', 100, struct ssd_protocol_info)
#define SSD_CMD_GET_HW_INFO			_IOR('H', 101, struct ssd_hw_info)
#define SSD_CMD_GET_ROM_INFO		_IOR('H', 102, struct ssd_rom_info)
#define SSD_CMD_GET_SMART			_IOR('H', 103, struct ssd_smart)
#define SSD_CMD_GET_IDX				_IOR('H', 105, int)
#define SSD_CMD_GET_AMOUNT			_IOR('H', 106, int)
#define SSD_CMD_GET_TO_INFO			_IOR('H', 107, int)
#define SSD_CMD_GET_DRV_VER			_IOR('H', 108, char[DRIVER_VERSION_LEN])

#define SSD_CMD_GET_BBACC_INFO		_IOR('H', 109, struct ssd_acc_info)
#define SSD_CMD_GET_ECACC_INFO		_IOR('H', 110, struct ssd_acc_info)

#define SSD_CMD_GET_HW_INFO_EXT		_IOR('H', 111, struct ssd_hw_info_extend)

#define SSD_CMD_REG_READ			_IOWR('H', 120, struct ssd_reg_op_info)
#define SSD_CMD_REG_WRITE			_IOWR('H', 121, struct ssd_reg_op_info)

#define SSD_CMD_SPI_READ			_IOWR('H', 125, struct ssd_spi_op_info)
#define SSD_CMD_SPI_WRITE			_IOWR('H', 126, struct ssd_spi_op_info)
#define SSD_CMD_SPI_ERASE			_IOWR('H', 127, struct ssd_spi_op_info)

#define SSD_CMD_I2C_READ			_IOWR('H', 128, struct ssd_i2c_op_info)
#define SSD_CMD_I2C_WRITE			_IOWR('H', 129, struct ssd_i2c_op_info)
#define SSD_CMD_I2C_WRITE_READ		_IOWR('H', 130, struct ssd_i2c_op_info)

#define SSD_CMD_SMBUS_SEND_BYTE		_IOWR('H', 131, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_RECEIVE_BYTE	_IOWR('H', 132, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_WRITE_BYTE	_IOWR('H', 133, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_READ_BYTE		_IOWR('H', 135, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_WRITE_WORD	_IOWR('H', 136, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_READ_WORD		_IOWR('H', 137, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_WRITE_BLOCK	_IOWR('H', 138, struct ssd_smbus_op_info)
#define SSD_CMD_SMBUS_READ_BLOCK	_IOWR('H', 139, struct ssd_smbus_op_info)

#define SSD_CMD_BM_GET_VER			_IOR('H', 140, uint16_t)
#define SSD_CMD_BM_GET_NR_CAP		_IOR('H', 141, int)
#define SSD_CMD_BM_CAP_LEARNING		_IOW('H', 142, int)
#define SSD_CMD_CAP_LEARN			_IOR('H', 143, uint32_t)
#define SSD_CMD_GET_CAP_STATUS		_IOR('H', 144, int)

#define SSD_CMD_RAM_READ			_IOWR('H', 150, struct ssd_ram_op_info)
#define SSD_CMD_RAM_WRITE			_IOWR('H', 151, struct ssd_ram_op_info)

#define SSD_CMD_NAND_READ_ID		_IOR('H', 160, struct ssd_flash_op_info)
#define SSD_CMD_NAND_READ			_IOWR('H', 161, struct ssd_flash_op_info) //with oob
#define SSD_CMD_NAND_WRITE			_IOWR('H', 162, struct ssd_flash_op_info)
#define SSD_CMD_NAND_ERASE			_IOWR('H', 163, struct ssd_flash_op_info)
#define SSD_CMD_NAND_READ_EXT		_IOWR('H', 164, struct ssd_flash_op_info) //ingore EIO

#define SSD_CMD_UPDATE_BBT			_IOW('H', 180, struct ssd_flash_op_info)

#define SSD_CMD_CLEAR_ALARM			_IOW('H', 190, int)
#define SSD_CMD_SET_ALARM			_IOW('H', 191, int)

#define SSD_CMD_RESET				_IOW('H', 200, int)
#define SSD_CMD_RELOAD_FW			_IOW('H', 201, int)
#define SSD_CMD_UNLOAD_DEV			_IOW('H', 202, int)
#define SSD_CMD_LOAD_DEV			_IOW('H', 203, int)
#define SSD_CMD_UPDATE_VP			_IOWR('H', 205, uint32_t)
#define SSD_CMD_FULL_RESET			_IOW('H', 206, int)

#define SSD_CMD_GET_NR_LOG			_IOR('H', 220, uint32_t)
#define SSD_CMD_GET_LOG				_IOR('H', 221, void *)
#define SSD_CMD_LOG_LEVEL			_IOW('H', 222, int)

#define SSD_CMD_OT_PROTECT			_IOW('H', 223, int)
#define SSD_CMD_GET_OT_STATUS		_IOR('H', 224, int)

#define SSD_CMD_CLEAR_LOG			_IOW('H', 230, int)
#define SSD_CMD_CLEAR_SMART			_IOW('H', 231, int)

#define SSD_CMD_SW_LOG				_IOW('H', 232, struct ssd_sw_log_info)

#define SSD_CMD_GET_LABEL			_IOR('H', 235, struct ssd_label)
#define SSD_CMD_GET_VERSION			_IOR('H', 236, struct ssd_version_info)
#define SSD_CMD_GET_TEMPERATURE		_IOR('H', 237, int)
#define SSD_CMD_GET_BMSTATUS		_IOR('H', 238, int)
#define SSD_CMD_GET_LABEL2			_IOR('H', 239, void *)


#define SSD_CMD_FLUSH				_IOW('H', 240, int)
#define SSD_CMD_SAVE_MD				_IOW('H', 241, int)

#define SSD_CMD_SET_WMODE			_IOW('H', 242, int)
#define SSD_CMD_GET_WMODE			_IOR('H', 243, int)
#define SSD_CMD_GET_USER_WMODE		_IOR('H', 244, int)

#define SSD_CMD_DEBUG				_IOW('H', 250, struct ssd_debug_info)
#define SSD_CMD_DRV_PARAM_INFO		_IOR('H', 251, struct ssd_drv_param_info)


/* log */
#define SSD_LOG_MAX_SZ				4096
#define SSD_LOG_LEVEL				SSD_LOG_LEVEL_NOTICE

enum ssd_log_data
{
	SSD_LOG_DATA_NONE = 0,
	SSD_LOG_DATA_LOC, 
	SSD_LOG_DATA_HEX
};

typedef struct ssd_log_entry
{
	union {
		struct {
			uint32_t page:10;
			uint32_t block:14;
			uint32_t flash:8;
		} loc;
		struct {
			uint32_t page:12;
			uint32_t block:12;
			uint32_t flash:8;
		} loc1;
		uint32_t val;
	} data;
	uint16_t event:10;
	uint16_t mod:6;
	uint16_t idx;
}__attribute__((packed))ssd_log_entry_t;

typedef struct ssd_log
{
	uint64_t time:56;
	uint64_t ctrl_idx:8;
	ssd_log_entry_t le;
} __attribute__((packed)) ssd_log_t;

typedef struct ssd_log_desc
{
	uint16_t event;
	uint8_t level;
	uint8_t data;
	uint8_t sblock;
	uint8_t spage;
	char *desc;
} __attribute__((packed)) ssd_log_desc_t;

#define SSD_LOG_SW_IDX		0xF
#define SSD_UNKNOWN_EVENT	((uint16_t)-1)
static struct ssd_log_desc ssd_log_desc[] = {
	/* event, level, show flash, show block, show page, desc */
	{0x0,  SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_LOC,  0, 0, "Create BBT failure"}, //g3
	{0x1,  SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_LOC,  0, 0, "Read BBT failure"}, //g3
	{0x2,  SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Mark bad block"}, 
	{0x3,  SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Flush BBT failure"}, 
	{0x4,  SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0x7,  SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "No available blocks"}, 
	{0x8,  SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Bad EC header"}, 
	{0x9,  SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_LOC,  1, 0, "Bad VID header"}, //g3
	{0xa,  SSD_LOG_LEVEL_INFO,    SSD_LOG_DATA_LOC,  1, 0, "Wear leveling"}, 
	{0xb,  SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "WL read back failure"}, 
	{0x11, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Data recovery failure"}, // err
	{0x20, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Init: scan mapping table failure"}, // err g3
	{0x21, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0x22, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0x23, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0x24, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Merge: read mapping page failure"}, 
	{0x25, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Merge: read back failure"}, 
	{0x26, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0x27, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_LOC,  1, 1, "Data corrupted for abnormal power down"}, //g3
	{0x28, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Merge: mapping page corrupted"}, 
	{0x29, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Init: no mapping page"}, 
	{0x2a, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: mapping pages incomplete"}, 
	{0x2b, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Read back failure after programming failure"}, // err
	{0xf1, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Read failure without recovery"}, // err
	{0xf2, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  0, 0, "No available blocks"}, // maybe err g3
	{0xf3, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 0, "Init: RAID incomplete"}, // err g3
	{0xf4, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0xf5, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read failure in moving data"}, 
	{0xf6, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Program failure"}, 
	{0xf7, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_LOC,  1, 1, "Init: RAID not complete"}, 
	{0xf8, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Init: data moving interrupted"}, 
	{0xfe, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Data inspection failure"}, 
	{0xff, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "IO: ECC failed"}, 

	/* new */
	{0x2e, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  0, 0, "No available reserved blocks" }, // err
	{0x30, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Init: PMT membership not found"}, 
	{0x31, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "Init: PMT corrupted"}, 
	{0x32, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Init: PBT membership not found"}, 
	{0x33, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Init: PBT not found"}, 
	{0x34, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Init: PBT corrupted"}, 
	{0x35, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PMT page read failure"}, 
	{0x36, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PBT page read failure"}, 
	{0x37, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PBT backup page read failure"}, 
	{0x38, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PBMT read failure"}, 
	{0x39, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Init: PBMT scan failure"}, // err
	{0x3a, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: first page read failure"}, 
	{0x3b, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Init: first page scan failure"}, // err
	{0x3c, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Init: scan unclosed block failure"}, // err
	{0x3d, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: write pointer mismatch"}, 
	{0x3e, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PMT recovery: PBMT read failure"}, 
	{0x3f, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Init: PMT recovery: PBMT scan failure"}, 
	{0x40, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "Init: PMT recovery: data page read failure"}, //err
	{0x41, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PBT write pointer mismatch"}, 
	{0x42, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: PBT latest version corrupted"}, 
	{0x43, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 0, "Init: too many unclosed blocks"}, 
	{0x44, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "Init: PDW block found"}, 
	{0x45, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_HEX,  0, 0, "Init: more than one PDW block found"}, //err
	{0x46, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Init: first page is blank or read failure"}, 
	{0x47, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Init: PDW block not found"}, 

	{0x50, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 0, "Cache: hit error data"}, // err
	{0x51, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 0, "Cache: read back failure"}, // err
	{0x52, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Cache: unknown command"}, //?
	{0x53, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_LOC,  1, 1, "GC/WL read back failure"}, // err

	{0x60, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "Erase failure"}, 

	{0x70, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "LPA not matched"}, 
	{0x71, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "PBN not matched"}, 
	{0x72, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read retry failure"}, 
	{0x73, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Need raid recovery"}, 
	{0x74, SSD_LOG_LEVEL_INFO,    SSD_LOG_DATA_LOC,  1, 1, "Need read retry"}, 
	{0x75, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read invalid data page"}, 
	{0x76, SSD_LOG_LEVEL_INFO,    SSD_LOG_DATA_LOC,  1, 1, "ECC error, data in cache, PBN matched"}, 
	{0x77, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "ECC error, data in cache, PBN not matched"}, 
	{0x78, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "ECC error, data in flash, PBN not matched"}, 
	{0x79, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "ECC ok, data in cache, LPA not matched"},
	{0x7a, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "ECC ok, data in flash, LPA not matched"},
	{0x7b, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID data in cache, LPA not matched"}, 
	{0x7c, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID data in flash, LPA not matched"}, 
	{0x7d, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read data page status error"}, 
	{0x7e, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read blank page"}, 
	{0x7f, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Access flash timeout"}, 

	{0x80, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "EC overflow"}, 
	{0x81, SSD_LOG_LEVEL_INFO,    SSD_LOG_DATA_NONE, 0, 0, "Scrubbing completed"}, 
	{0x82, SSD_LOG_LEVEL_INFO,    SSD_LOG_DATA_LOC,  1, 0, "Unstable block(too much bit flip)"}, 
	{0x83, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "GC: ram error"}, //?
	{0x84, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "GC: one PBMT read failure"}, 

	{0x88, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "GC: mark bad block"}, 
	{0x89, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 0, "GC: invalid page count error"}, // maybe err
	{0x8a, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_NONE, 0, 0, "Warning: Bad Block close to limit"}, 
	{0x8b, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "Error: Bad Block over limit"}, 
	{0x8c, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_NONE, 0, 0, "Warning: P/E cycles close to limit"}, 
	{0x8d, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "Error: P/E cycles over limit"}, 

	{0x90, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Over temperature"}, //xx
	{0x91, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Temperature is OK"}, //xx
	{0x92, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_NONE, 0, 0, "Battery fault"}, 
	{0x93, SSD_LOG_LEVEL_WARNING, SSD_LOG_DATA_NONE, 0, 0, "SEU fault"}, //err
	{0x94, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "DDR error"}, //err
	{0x95, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "Controller serdes error"}, //err
	{0x96, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "Bridge serdes 1 error"}, //err
	{0x97, SSD_LOG_LEVEL_ERR,     SSD_LOG_DATA_NONE, 0, 0, "Bridge serdes 2 error"}, //err
	{0x98, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "SEU fault (corrected)"}, //err
	{0x99, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Battery is OK"}, 
	{0x9a, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Temperature close to limit"}, //xx
	
	{0x9b, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "SEU fault address (low)"}, 
	{0x9c, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "SEU fault address (high)"}, 
	{0x9d, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "I2C fault" }, 
	{0x9e, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "DDR single bit error" }, 
	{0x9f, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Board voltage fault" },

	{0xa0, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "LPA not matched"}, 
	{0xa1, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Re-read data in cache"}, 
	{0xa2, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read blank page"}, 
	{0xa3, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: Read blank page"}, 
	{0xa4, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: new data in cache"}, 
	{0xa5, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: PBN not matched"}, 
	{0xa6, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Read data with error flag"}, 
	{0xa7, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: recoverd data with error flag"}, 
	{0xa8, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Blank page in cache, PBN matched"}, 
	{0xa9, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: Blank page in cache, PBN matched"}, 
	{0xaa, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  0, 0, "Flash init failure"}, 
	{0xab, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "Mapping table recovery failure"}, 
	{0xac, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_LOC,  1, 1, "RAID recovery: ECC failed"}, 
	{0xb0, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Temperature is up to degree 95"},
	{0xb1, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_NONE, 0, 0, "Temperature is up to degree 100"},

	{0x300, SSD_LOG_LEVEL_ERR,    SSD_LOG_DATA_HEX,  0, 0, "CMD timeout"}, 
	{0x301, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Power on"}, 
	{0x302, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Power off"}, 
	{0x303, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Clear log"}, 
	{0x304, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Set capacity"}, 
	{0x305, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Clear data"}, 
	{0x306, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "BM safety status"}, 
	{0x307, SSD_LOG_LEVEL_ERR,    SSD_LOG_DATA_HEX,  0, 0, "I/O error"}, 
	{0x308, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "CMD error"}, 
	{0x309, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Set wmode"}, 
	{0x30a, SSD_LOG_LEVEL_ERR,    SSD_LOG_DATA_HEX,  0, 0, "DDR init failed" }, 
	{0x30b, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "PCIe link status" }, 
	{0x30c, SSD_LOG_LEVEL_ERR,    SSD_LOG_DATA_HEX,  0, 0, "Controller reset sync error" }, 
	{0x30d, SSD_LOG_LEVEL_ERR,    SSD_LOG_DATA_HEX,  0, 0, "Clock fault" }, 
	{0x30e, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "FPGA voltage fault status" }, 
	{0x30f, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Set capacity finished"}, 
	{0x310, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Clear data finished"}, 
	{0x311, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Reset"}, 
	{0x312, SSD_LOG_LEVEL_WARNING,SSD_LOG_DATA_HEX,  0, 0, "CAP: voltage fault"}, 
	{0x313, SSD_LOG_LEVEL_WARNING,SSD_LOG_DATA_NONE, 0, 0, "CAP: learn fault"}, 
	{0x314, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "CAP status"}, 
	{0x315, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_HEX,  0, 0, "Board voltage fault status"}, 
	{0x316, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Inlet over temperature"}, 
	{0x317, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Inlet temperature is OK"}, 
	{0x318, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Flash over temperature"}, 
	{0x319, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Flash temperature is OK"}, 
	{0x31a, SSD_LOG_LEVEL_WARNING,SSD_LOG_DATA_NONE, 0, 0, "CAP: short circuit"}, 
	{0x31b, SSD_LOG_LEVEL_WARNING,SSD_LOG_DATA_HEX,  0, 0, "Sensor fault"}, 
	{0x31c, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Erase all data"}, 
	{0x31d, SSD_LOG_LEVEL_NOTICE, SSD_LOG_DATA_NONE, 0, 0, "Erase all data finished"},

	{SSD_UNKNOWN_EVENT, SSD_LOG_LEVEL_NOTICE,  SSD_LOG_DATA_HEX,  0, 0, "unknown event"}, 
};
/* */
#define SSD_LOG_OVER_TEMP		0x90
#define SSD_LOG_NORMAL_TEMP		0x91
#define SSD_LOG_WARN_TEMP		0x9a
#define SSD_LOG_SEU_FAULT		0x93
#define SSD_LOG_SEU_FAULT1		0x98
#define SSD_LOG_BATTERY_FAULT	0x92
#define SSD_LOG_BATTERY_OK		0x99
#define SSD_LOG_BOARD_VOLT_FAULT	0x9f

/* software log */
#define SSD_LOG_TIMEOUT			0x300
#define SSD_LOG_POWER_ON		0x301
#define SSD_LOG_POWER_OFF		0x302
#define SSD_LOG_CLEAR_LOG		0x303
#define SSD_LOG_SET_CAPACITY	0x304
#define SSD_LOG_CLEAR_DATA		0x305
#define SSD_LOG_BM_SFSTATUS		0x306
#define SSD_LOG_EIO				0x307
#define SSD_LOG_ECMD			0x308
#define SSD_LOG_SET_WMODE		0x309
#define SSD_LOG_DDR_INIT_ERR	0x30a
#define SSD_LOG_PCIE_LINK_STATUS	0x30b
#define SSD_LOG_CTRL_RST_SYNC	0x30c
#define SSD_LOG_CLK_FAULT		0x30d
#define SSD_LOG_VOLT_FAULT		0x30e
#define SSD_LOG_SET_CAPACITY_END	0x30F
#define SSD_LOG_CLEAR_DATA_END	0x310
#define SSD_LOG_RESET			0x311
#define SSD_LOG_CAP_VOLT_FAULT	0x312
#define SSD_LOG_CAP_LEARN_FAULT	0x313
#define SSD_LOG_CAP_STATUS		0x314
#define SSD_LOG_VOLT_STATUS		0x315
#define SSD_LOG_INLET_OVER_TEMP	0x316
#define SSD_LOG_INLET_NORMAL_TEMP	0x317
#define SSD_LOG_FLASH_OVER_TEMP	0x318
#define SSD_LOG_FLASH_NORMAL_TEMP	0x319
#define SSD_LOG_CAP_SHORT_CIRCUIT	0x31a
#define SSD_LOG_SENSOR_FAULT	0x31b
#define SSD_LOG_ERASE_ALL		0x31c
#define SSD_LOG_ERASE_ALL_END	0x31d


/* sw log fifo depth */
#define SSD_LOG_FIFO_SZ		1024


/* done queue */
static DEFINE_PER_CPU(struct list_head, ssd_doneq);
static DEFINE_PER_CPU(struct tasklet_struct, ssd_tasklet);


/* unloading driver */
static volatile int ssd_exiting = 0;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
static struct class_simple *ssd_class;
#else
static struct class *ssd_class;
#endif

static int ssd_cmajor = SSD_CMAJOR;

/* ssd block device major, minors */
static int ssd_major = SSD_MAJOR;
static int ssd_major_sl = SSD_MAJOR_SL;
static int ssd_minors = SSD_MINORS;

/* ssd device list */
static struct list_head	ssd_list;
static unsigned long ssd_index_bits[SSD_MAX_DEV / BITS_PER_LONG + 1];
static unsigned long ssd_index_bits_sl[SSD_MAX_DEV / BITS_PER_LONG + 1];
static atomic_t ssd_nr;

/* module param */
enum ssd_drv_mode
{
	SSD_DRV_MODE_STANDARD = 0,	/* full */
	SSD_DRV_MODE_DEBUG = 2,	/* debug */
	SSD_DRV_MODE_BASE	/* base only */
};

enum ssd_int_mode
{
	SSD_INT_LEGACY = 0, 
	SSD_INT_MSI, 
	SSD_INT_MSIX
};

#if (defined SSD_MSIX)
#define SSD_INT_MODE_DEFAULT SSD_INT_MSIX
#elif (defined SSD_MSI)
#define SSD_INT_MODE_DEFAULT SSD_INT_MSI
#else
/* auto select the defaut int mode according to the kernel version*/
/* suse 11 sp1 irqbalance bug: use msi instead*/
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) || (defined RHEL_MAJOR && RHEL_MAJOR >= 6) || (defined RHEL_MAJOR && RHEL_MAJOR == 5 && RHEL_MINOR >= 5))
#define SSD_INT_MODE_DEFAULT SSD_INT_MSIX
#else
#define SSD_INT_MODE_DEFAULT SSD_INT_MSI
#endif
#endif

static int mode = SSD_DRV_MODE_STANDARD;
static int status_mask = 0xFF;
static int int_mode = SSD_INT_MODE_DEFAULT;
static int threaded_irq = 0;
static int log_level = SSD_LOG_LEVEL_WARNING;
static int ot_protect = 1;
static int wmode = SSD_WMODE_DEFAULT;
static int finject = 0;

module_param(mode, int, 0);
module_param(status_mask, int, 0);
module_param(int_mode, int, 0);
module_param(threaded_irq, int, 0);
module_param(log_level, int, 0);
module_param(ot_protect, int, 0);
module_param(wmode, int, 0);
module_param(finject, int, 0);


MODULE_PARM_DESC(mode, "driver mode, 0 - standard, 1 - debug, 2 - debug without IO, 3 - basic debug mode");
MODULE_PARM_DESC(status_mask, "command status mask, 0 - without command error, 0xff - with command error");
MODULE_PARM_DESC(int_mode, "preferred interrupt mode, 0 - legacy, 1 - msi, 2 - msix");
MODULE_PARM_DESC(threaded_irq, "threaded irq, 0 - normal irq, 1 - threaded irq");
MODULE_PARM_DESC(log_level, "log level to display, 0 - info and above, 1 - notice and above, 2 - warning and above, 3 - error only");
MODULE_PARM_DESC(ot_protect, "over temperature protect, 0 - disable, 1 - enable");
MODULE_PARM_DESC(wmode, "write mode, 0 - write buffer (with risk for the 6xx firmware), 1 - write buffer ex, 2 - write through, 3 - auto, 4 - default");
MODULE_PARM_DESC(finject, "enable fault simulation, 0 - off, 1 - on, for debug purpose only");


#ifndef MODULE
static int __init ssd_drv_mode(char *str)
{
	mode = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_status_mask(char *str)
{
	status_mask = (int)simple_strtoul(str, NULL, 16);

	return 1;
}

static int __init ssd_int_mode(char *str)
{
	int_mode = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_threaded_irq(char *str)
{
	threaded_irq = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_log_level(char *str)
{
	log_level = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_ot_protect(char *str)
{
	ot_protect = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_wmode(char *str)
{
	wmode = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

static int __init ssd_finject(char *str)
{
	finject = (int)simple_strtoul(str, NULL, 0);

	return 1;
}

__setup(MODULE_NAME"_mode=", ssd_drv_mode);
__setup(MODULE_NAME"_status_mask=", ssd_status_mask);
__setup(MODULE_NAME"_int_mode=", ssd_int_mode);
__setup(MODULE_NAME"_threaded_irq=", ssd_threaded_irq);
__setup(MODULE_NAME"_log_level=", ssd_log_level);
__setup(MODULE_NAME"_ot_protect=", ssd_ot_protect);
__setup(MODULE_NAME"_wmode=", ssd_wmode);
__setup(MODULE_NAME"_finject=", ssd_finject);
#endif


#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define SSD_PROC_DIR	MODULE_NAME
#define SSD_PROC_INFO	"info"

static struct proc_dir_entry *ssd_proc_dir = NULL;
static struct proc_dir_entry *ssd_proc_info = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
static int ssd_proc_read(char *page, char **start, 
	off_t off, int count, int *eof, void *data)
{
	struct ssd_device *dev = NULL;
	struct ssd_device *n = NULL;
	uint64_t size;
	int idx;
	int len = 0;
	//char type; //xx

	if (ssd_exiting) {
		return 0;
	}

	len += snprintf((page + len), (count - len), "Driver          Version:\t%s\n", DRIVER_VERSION);

	list_for_each_entry_safe(dev, n, &ssd_list, list) {
		idx = dev->idx + 1;
		size = dev->hw_info.size ;
		do_div(size, 1000000000);

		len += snprintf((page + len), (count - len), "\n");

		len += snprintf((page + len), (count - len), "HIO %d              Size:\t%uGB\n", idx, (uint32_t)size);

		len += snprintf((page + len), (count - len), "HIO %d     Bridge FW VER:\t%03X\n", idx, dev->hw_info.bridge_ver);
		if (dev->hw_info.ctrl_ver != 0) {
			len += snprintf((page + len), (count - len), "HIO %d Controller FW VER:\t%03X\n", idx, dev->hw_info.ctrl_ver);
		}

		len += snprintf((page + len), (count - len), "HIO %d           PCB VER:\t.%c\n", idx, dev->hw_info.pcb_ver);

		if (dev->hw_info.upper_pcb_ver >= 'A') {
			len += snprintf((page + len), (count - len), "HIO %d     Upper PCB VER:\t.%c\n", idx, dev->hw_info.upper_pcb_ver);
		}

		len += snprintf((page + len), (count - len), "HIO %d            Device:\t%s\n", idx, dev->name);
	}

	return len;
}

#else

static int ssd_proc_show(struct seq_file *m, void *v)
{
	struct ssd_device *dev = NULL;
	struct ssd_device *n = NULL;
	uint64_t size;
	int idx;

	if (ssd_exiting) {
		return 0;
	}

	seq_printf(m, "Driver          Version:\t%s\n", DRIVER_VERSION);

	list_for_each_entry_safe(dev, n, &ssd_list, list) {
		idx = dev->idx + 1;
		size = dev->hw_info.size ;
		do_div(size, 1000000000);

		seq_printf(m, "\n");

		seq_printf(m, "HIO %d              Size:\t%uGB\n", idx, (uint32_t)size);

		seq_printf(m, "HIO %d     Bridge FW VER:\t%03X\n", idx, dev->hw_info.bridge_ver);
		if (dev->hw_info.ctrl_ver != 0) {
			seq_printf(m, "HIO %d Controller FW VER:\t%03X\n", idx, dev->hw_info.ctrl_ver);
		}

		seq_printf(m, "HIO %d           PCB VER:\t.%c\n", idx, dev->hw_info.pcb_ver);

		if (dev->hw_info.upper_pcb_ver >= 'A') {
			seq_printf(m, "HIO %d     Upper PCB VER:\t.%c\n", idx, dev->hw_info.upper_pcb_ver);
		}

		seq_printf(m, "HIO %d            Device:\t%s\n", idx, dev->name);
	}

	return 0;
}

static int ssd_proc_open(struct inode *inode, struct file *file)
{
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,9,0))
	return single_open(file, ssd_proc_show, PDE(inode)->data);
#else
	return single_open(file, ssd_proc_show, PDE_DATA(inode));
#endif
}

static const struct file_operations ssd_proc_fops = {
	.open		= ssd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif


static void ssd_cleanup_proc(void)
{
	if (ssd_proc_info) {
		remove_proc_entry(SSD_PROC_INFO, ssd_proc_dir);
		ssd_proc_info = NULL;
	}
	if (ssd_proc_dir) {
		remove_proc_entry(SSD_PROC_DIR, NULL);
		ssd_proc_dir = NULL;
	}
}
static int ssd_init_proc(void)
{
	ssd_proc_dir = proc_mkdir(SSD_PROC_DIR, NULL);
	if (!ssd_proc_dir)
		goto out_proc_mkdir;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	ssd_proc_info = create_proc_entry(SSD_PROC_INFO, S_IFREG | S_IRUGO | S_IWUSR, ssd_proc_dir);
	if (!ssd_proc_info)
		goto out_create_proc_entry;

	ssd_proc_info->read_proc = ssd_proc_read;

/* kernel bug */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
	ssd_proc_info->owner = THIS_MODULE;
#endif
#else
	ssd_proc_info = proc_create(SSD_PROC_INFO, 0600, ssd_proc_dir, &ssd_proc_fops);
	if (!ssd_proc_info)
		goto out_create_proc_entry;
#endif

	return 0;

out_create_proc_entry:
	remove_proc_entry(SSD_PROC_DIR, NULL);
out_proc_mkdir:
	return -ENOMEM;
}

#else
static void ssd_cleanup_proc(void)
{
	return;
}
static int ssd_init_proc(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */

/* sysfs */
static void ssd_unregister_sysfs(struct ssd_device *dev)
{
	return;
}

static int ssd_register_sysfs(struct ssd_device *dev)
{
	return 0;
}

static void ssd_cleanup_sysfs(void)
{
	return;
}

static int ssd_init_sysfs(void)
{
	return 0;
}

static inline void ssd_put_index(int slave, int index)
{
	unsigned long *index_bits = ssd_index_bits;

	if (slave) {
		index_bits = ssd_index_bits_sl;
	}

	if (test_and_clear_bit(index,  index_bits)) {
		atomic_dec(&ssd_nr);
	}
}

static inline int ssd_get_index(int slave)
{
	unsigned long *index_bits = ssd_index_bits;
	int index;

	if (slave) {
		index_bits = ssd_index_bits_sl;
	}

find_index:
	if ((index = find_first_zero_bit(index_bits, SSD_MAX_DEV)) >= SSD_MAX_DEV) {
			return -1;
	}

	if (test_and_set_bit(index, index_bits)) {
		goto find_index;
	}

	atomic_inc(&ssd_nr);

	return index;
}

static void ssd_cleanup_index(void)
{
	return;
}

static int ssd_init_index(void)
{
	INIT_LIST_HEAD(&ssd_list);
	atomic_set(&ssd_nr, 0);
	memset(ssd_index_bits, 0, (SSD_MAX_DEV / BITS_PER_LONG + 1));
	memset(ssd_index_bits_sl, 0, (SSD_MAX_DEV / BITS_PER_LONG + 1));

	return 0;
}

static void ssd_set_dev_name(char *name, size_t size, int idx)
{
	if(idx < SSD_ALPHABET_NUM) {
		snprintf(name, size, "%c", 'a'+idx);
	} else {
		idx -= SSD_ALPHABET_NUM;
		snprintf(name, size, "%c%c", 'a'+(idx/SSD_ALPHABET_NUM), 'a'+(idx%SSD_ALPHABET_NUM));
	}
}

/* pci register r&w */
static inline void ssd_reg_write(void *addr, uint64_t val)
{
	iowrite32((uint32_t)val, addr);
	iowrite32((uint32_t)(val >> 32), addr + 4);
	wmb();
}

static inline uint64_t ssd_reg_read(void *addr)
{
	uint64_t val;
	uint32_t val_lo, val_hi;

	val_lo = ioread32(addr);
	val_hi = ioread32(addr + 4);

	rmb();
	val = val_lo | ((uint64_t)val_hi << 32);

	return val;
}


#define ssd_reg32_write(addr, val)	writel(val, addr)
#define ssd_reg32_read(addr)		readl(addr)

/* alarm led */
static void ssd_clear_alarm(struct ssd_device *dev)
{
	uint32_t val;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_LED_REG);

	/* firmware control */
	val &= ~0x2;

	ssd_reg32_write(dev->ctrlp + SSD_LED_REG, val);
}

static void ssd_set_alarm(struct ssd_device *dev)
{
	uint32_t val;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_LED_REG);

	/* light up */
	val &= ~0x1;
	/* software control */
	val |= 0x2;

	ssd_reg32_write(dev->ctrlp + SSD_LED_REG, val);
}

#define u32_swap(x) \
	((uint32_t)( \
	(((uint32_t)(x) & (uint32_t)0x000000ffUL) << 24) | \
	(((uint32_t)(x) & (uint32_t)0x0000ff00UL) <<  8) | \
	(((uint32_t)(x) & (uint32_t)0x00ff0000UL) >>  8) | \
	(((uint32_t)(x) & (uint32_t)0xff000000UL) >> 24)))

#define u16_swap(x) \
	((uint16_t)( \
	(((uint16_t)(x) & (uint16_t)0x00ff) <<  8) | \
	(((uint16_t)(x) & (uint16_t)0xff00) >>  8) ))


#if 0
/* No lock, for init only*/
static int ssd_spi_read_id(struct ssd_device *dev, uint32_t *id)
{
	uint32_t val;
	unsigned long st;
	int ret = 0;

	if (!dev || !id) {
		return -EINVAL;
	}

	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_READ_ID);

	val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
	val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
	val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
	val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);

	st = jiffies;
	for (;;) {
		val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
		if (val == 0x1000000) {
			break;
		}

		if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_ID);
	*id = val;

out:
	return ret;
}
#endif

/* spi access */
static int ssd_init_spi(struct ssd_device *dev)
{
	uint32_t val;
	unsigned long st;
	int ret = 0;

	mutex_lock(&dev->spi_mutex);
	st = jiffies;
	for(;;) {
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_READ_STATUS);

		do {
			val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);

			if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		} while (val != 0x1000000);

		val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_STATUS);
		if (!(val & 0x1)) {
			break;
		}

		if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}

out:
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if (val & 0x1) {
			ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_CLSR);
		}
	}
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_DISABLE);
	mutex_unlock(&dev->spi_mutex);

	ret = 0;

	return ret;
}

static int ssd_spi_page_read(struct ssd_device *dev, void *buf, uint32_t off, uint32_t size)
{
	uint32_t val;
	uint32_t rlen = 0;
	unsigned long st;
	int ret = 0;

	if (!dev || !buf) {
		return -EINVAL;
	}

	if ((off % sizeof(uint32_t)) != 0 || (size % sizeof(uint32_t)) != 0 || size == 0 || 
		((uint64_t)off + (uint64_t)size) > dev->rom_info.size || size > dev->rom_info.page_size) {
		return -EINVAL;
	}

	mutex_lock(&dev->spi_mutex);
	while (rlen < size) {
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD_HI, ((off + rlen) >> 24));
		wmb();
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, (((off + rlen) << 8) | SSD_SPI_CMD_READ));

		(void)ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
		(void)ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
		(void)ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
		(void)ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);

		st = jiffies;
		for (;;) {
			val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);
			if (val == 0x1000000) {
				break;
			}

			if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		}

		val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_RDATA);
		*(uint32_t *)(buf + rlen)= u32_swap(val);

		rlen += sizeof(uint32_t);
	}

out:
	mutex_unlock(&dev->spi_mutex);
	return ret;
}

static int ssd_spi_page_write(struct ssd_device *dev, void *buf, uint32_t off, uint32_t size)
{
	uint32_t val;
	uint32_t wlen;
	unsigned long st;
	int i;
	int ret = 0;

	if (!dev || !buf) {
		return -EINVAL;
	}

	if ((off % sizeof(uint32_t)) != 0 || (size % sizeof(uint32_t)) != 0 || size == 0 || 
		((uint64_t)off + (uint64_t)size) > dev->rom_info.size || size > dev->rom_info.page_size || 
		(off / dev->rom_info.page_size) !=  ((off + size - 1) / dev->rom_info.page_size)) {
		return -EINVAL;
	}

	mutex_lock(&dev->spi_mutex);

	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_ENABLE);

	wlen = size / sizeof(uint32_t);
	for (i=0; i<(int)wlen; i++) {
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_WDATA, u32_swap(*((uint32_t *)buf + i)));
	}

	wmb();
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD_HI, (off >> 24));
	wmb();
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, ((off << 8) | SSD_SPI_CMD_PROGRAM));

	udelay(1);

	st = jiffies;
	for (;;) {
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_READ_STATUS);
		do {
			val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);

			if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		} while (val != 0x1000000);

		val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_STATUS);
		if (!(val & 0x1)) {
			break;
		}

		if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if ((val >> 6) & 0x1) {
			ret = -EIO;
			goto out;
		}
	}

out:
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if (val & 0x1) {
			ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_CLSR);
		}
	}
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_DISABLE);

	mutex_unlock(&dev->spi_mutex);

	return ret;
}

static int ssd_spi_block_erase(struct ssd_device *dev, uint32_t off)
{
	uint32_t val;
	unsigned long st;
	int ret = 0;

	if (!dev) {
		return -EINVAL;
	}

	if ((off % dev->rom_info.block_size) != 0 || off >= dev->rom_info.size) {
		return -EINVAL;
	}

	mutex_lock(&dev->spi_mutex);

	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_ENABLE);
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_ENABLE);

	wmb();
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD_HI, (off >> 24));
	wmb();
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, ((off << 8) | SSD_SPI_CMD_ERASE));

	st = jiffies;
	for (;;) {
		ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_READ_STATUS);

		do {
			val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_READY);

			if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		} while (val != 0x1000000);

		val = ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_STATUS);
		if (!(val & 0x1)) {
			break;
		}

		if (time_after(jiffies, (st + SSD_SPI_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if ((val >> 5) & 0x1) {
			ret = -EIO;
			goto out;
		}
	}

out:
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if (val & 0x1) {
			ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_CLSR);
		}
	}
	ssd_reg32_write(dev->ctrlp + SSD_SPI_REG_CMD, SSD_SPI_CMD_W_DISABLE);

	mutex_unlock(&dev->spi_mutex);

	return ret;
}

static int ssd_spi_read(struct ssd_device *dev, void *buf, uint32_t off, uint32_t size)
{
	uint32_t len = 0;
	uint32_t roff;
	uint32_t rsize;
	int ret = 0;

	if (!dev || !buf) {
		return -EINVAL;
	}

	if ((off % sizeof(uint32_t)) != 0 || (size % sizeof(uint32_t)) != 0 || size == 0 || 
		((uint64_t)off + (uint64_t)size) > dev->rom_info.size) {
		return -EINVAL;
	}

	while (len < size) {
		roff = (off + len) % dev->rom_info.page_size;
		rsize = dev->rom_info.page_size - roff;
		if ((size - len) < rsize) {
			rsize = (size - len);
		}
		roff = off + len;

		ret = ssd_spi_page_read(dev, (buf + len), roff, rsize);
		if (ret) {
			goto out;
		}

		len += rsize;

		cond_resched();
	}

out:
	return ret;
}

static int ssd_spi_write(struct ssd_device *dev, void *buf, uint32_t off, uint32_t size)
{
	uint32_t len = 0;
	uint32_t woff;
	uint32_t wsize;
	int ret = 0;

	if (!dev || !buf) {
		return -EINVAL;
	}

	if ((off % sizeof(uint32_t)) != 0 || (size % sizeof(uint32_t)) != 0 || size == 0 || 
		((uint64_t)off + (uint64_t)size) > dev->rom_info.size) {
		return -EINVAL;
	}

	while (len < size) {
		woff = (off + len) % dev->rom_info.page_size;
		wsize = dev->rom_info.page_size - woff;
		if ((size - len) < wsize) {
			wsize = (size - len);
		}
		woff = off + len;

		ret = ssd_spi_page_write(dev, (buf + len), woff, wsize);
		if (ret) {
			goto out;
		}

		len += wsize;

		cond_resched();
	}

out:
	return ret;
}

static int ssd_spi_erase(struct ssd_device *dev, uint32_t off, uint32_t size)
{
	uint32_t len = 0;
	uint32_t eoff;
	int ret = 0;

	if (!dev) {
		return -EINVAL;
	}

	if (size == 0 || ((uint64_t)off + (uint64_t)size) > dev->rom_info.size ||
		(off % dev->rom_info.block_size) != 0 || (size % dev->rom_info.block_size) != 0) {
		return -EINVAL;
	}

	while (len < size) {
		eoff = (off + len);

		ret = ssd_spi_block_erase(dev, eoff);
		if (ret) {
			goto out;
		}

		len += dev->rom_info.block_size;

		cond_resched();
	}

out:
	return ret;
}

/* i2c access */
static uint32_t __ssd_i2c_reg32_read(void *addr)
{
	return ssd_reg32_read(addr);
}

static void __ssd_i2c_reg32_write(void *addr, uint32_t val)
{
	ssd_reg32_write(addr, val);
	ssd_reg32_read(addr);
}

static int __ssd_i2c_clear(struct ssd_device *dev, uint8_t saddr)
{
	ssd_i2c_ctrl_t ctrl;
	ssd_i2c_data_t data;
	uint8_t status = 0;
	int nr_data = 0;
	unsigned long st;
	int ret = 0;

check_status:
	ctrl.bits.wdata	= 0;
	ctrl.bits.addr	= SSD_I2C_STATUS_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	st = jiffies;
	for (;;) {
		data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
		if (data.bits.valid == 0) {
			break;
		}

		/* retry */
		if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out;
		}
		cond_resched();
	}
	status = data.bits.rdata;

	if (!(status & 0x4)) {
		/* clear read fifo data */
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_DATA_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		st = jiffies;
		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out;
			}
			cond_resched();
		}

		nr_data++;
		if (nr_data <= SSD_I2C_MAX_DATA) {
			goto check_status;
		} else {
			goto out_reset;
		}
	}

	if (status & 0x3) {
		/* clear int */
		ctrl.bits.wdata	= 0x04;
		ctrl.bits.addr	= SSD_I2C_CMD_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);
	}

	if (!(status & 0x8)) {
out_reset:
		/* reset i2c controller */
		ctrl.bits.wdata	= 0x0;
		ctrl.bits.addr	= SSD_I2C_RESET_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);
	}

out:
	return ret;
}

static int ssd_i2c_write(struct ssd_device *dev, uint8_t saddr, uint8_t size, uint8_t *buf)
{
	ssd_i2c_ctrl_t ctrl;
	ssd_i2c_data_t data;
	uint8_t off = 0;
	uint8_t status = 0;
	unsigned long st;
	int ret = 0;

	mutex_lock(&dev->i2c_mutex);

	ctrl.val = 0;

	/* slave addr */
	ctrl.bits.wdata	= saddr;
	ctrl.bits.addr	= SSD_I2C_SADDR_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* data */
	while (off < size) {
		ctrl.bits.wdata	= buf[off];
		ctrl.bits.addr	= SSD_I2C_DATA_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		off++;
	}

	/* write */
	ctrl.bits.wdata	= 0x01;
	ctrl.bits.addr	= SSD_I2C_CMD_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* wait */
	st = jiffies;
	for (;;) {
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_STATUS_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out_clear;
			}
			cond_resched();
		}

		status = data.bits.rdata;
		if (status & 0x1) {
			break;
		}

		if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out_clear;
		}
		cond_resched();
	}

	if (!(status & 0x1)) {
		ret =  -1;
		goto out_clear;
	}

	/* busy ? */
	if (status & 0x20) {
		ret =  -2;
		goto out_clear;
	}

	/* ack ? */
	if (status & 0x10) {
		ret =  -3;
		goto out_clear;
	}

	/* clear */
out_clear:
	if (__ssd_i2c_clear(dev, saddr)) {
		if (!ret) ret = -4;
	}

	mutex_unlock(&dev->i2c_mutex);

	return ret;
}

static int ssd_i2c_read(struct ssd_device *dev, uint8_t saddr, uint8_t size, uint8_t *buf)
{
	ssd_i2c_ctrl_t ctrl;
	ssd_i2c_data_t data;
	uint8_t off = 0;
	uint8_t status = 0;
	unsigned long st;
	int ret = 0;

	mutex_lock(&dev->i2c_mutex);

	ctrl.val = 0;

	/* slave addr */
	ctrl.bits.wdata	= saddr;
	ctrl.bits.addr	= SSD_I2C_SADDR_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* read len */
	ctrl.bits.wdata	= size;
	ctrl.bits.addr	= SSD_I2C_LEN_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* read */
	ctrl.bits.wdata	= 0x02;
	ctrl.bits.addr	= SSD_I2C_CMD_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* wait */
	st = jiffies;
	for (;;) {
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_STATUS_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out_clear;
			}
			cond_resched();
		}

		status = data.bits.rdata;
		if (status & 0x2) {
			break;
		}

		if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out_clear;
		}
		cond_resched();
	}

	if (!(status & 0x2)) {
		ret =  -1;
		goto out_clear;
	}

	/* busy ? */
	if (status & 0x20) {
		ret =  -2;
		goto out_clear;
	}

	/* ack ? */
	if (status & 0x10) {
		ret =  -3;
		goto out_clear;
	}

	/* data */
	while (off < size) {
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_DATA_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		st = jiffies;
		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out_clear;
			}
			cond_resched();
		}

		buf[off] = data.bits.rdata;

		off++;
	}

	/* clear */
out_clear:
	if (__ssd_i2c_clear(dev, saddr)) {
		if (!ret) ret = -4;
	}

	mutex_unlock(&dev->i2c_mutex);

	return ret;
}

static int ssd_i2c_write_read(struct ssd_device *dev, uint8_t saddr, uint8_t wsize, uint8_t *wbuf, uint8_t rsize, uint8_t *rbuf)
{
	ssd_i2c_ctrl_t ctrl;
	ssd_i2c_data_t data;
	uint8_t off = 0;
	uint8_t status = 0;
	unsigned long st;
	int ret = 0;

	mutex_lock(&dev->i2c_mutex);

	ctrl.val = 0;

	/* slave addr */
	ctrl.bits.wdata	= saddr;
	ctrl.bits.addr	= SSD_I2C_SADDR_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* data */
	off = 0;
	while (off < wsize) {
		ctrl.bits.wdata	= wbuf[off];
		ctrl.bits.addr	= SSD_I2C_DATA_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		off++;
	}

	/* read len */
	ctrl.bits.wdata	= rsize;
	ctrl.bits.addr	= SSD_I2C_LEN_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* write -> read */
	ctrl.bits.wdata	= 0x03;
	ctrl.bits.addr	= SSD_I2C_CMD_REG;
	ctrl.bits.rw 	= SSD_I2C_CTRL_WRITE;
	__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

	/* wait */
	st = jiffies;
	for (;;) {
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_STATUS_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out_clear;
			}
			cond_resched();
		}

		status = data.bits.rdata;
		if (status & 0x2) {
			break;
		}

		if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
			ret = -ETIMEDOUT;
			goto out_clear;
		}
		cond_resched();
	}

	if (!(status & 0x2)) {
		ret =  -1;
		goto out_clear;
	}

	/* busy ? */
	if (status & 0x20) {
		ret =  -2;
		goto out_clear;
	}

	/* ack ? */
	if (status & 0x10) {
		ret =  -3;
		goto out_clear;
	}

	/* data */
	off = 0;
	while (off < rsize) {
		ctrl.bits.wdata	= 0;
		ctrl.bits.addr	= SSD_I2C_DATA_REG;
		ctrl.bits.rw 	= SSD_I2C_CTRL_READ;
		__ssd_i2c_reg32_write(dev->ctrlp + SSD_I2C_CTRL_REG, ctrl.val);

		st = jiffies;
		for (;;) {
			data.val = __ssd_i2c_reg32_read(dev->ctrlp + SSD_I2C_RDATA_REG);
			if (data.bits.valid == 0) {
				break;
			}

			/* retry */
			if (time_after(jiffies, (st + SSD_I2C_TIMEOUT))) {
				ret = -ETIMEDOUT;
				goto out_clear;
			}
			cond_resched();
		}

		rbuf[off] = data.bits.rdata;

		off++;
	}

	/* clear */
out_clear:
	if (__ssd_i2c_clear(dev, saddr)) {
		if (!ret) ret = -4;
	}
	mutex_unlock(&dev->i2c_mutex);

	return ret;
}

static int ssd_smbus_send_byte(struct ssd_device *dev, uint8_t saddr, uint8_t *buf)
{
	int i = 0;
	int ret = 0;

	for (;;) {
		ret = ssd_i2c_write(dev, saddr, 1, buf);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_receive_byte(struct ssd_device *dev, uint8_t saddr, uint8_t *buf)
{
	int i = 0;
	int ret = 0;

	for (;;) {
		ret = ssd_i2c_read(dev, saddr, 1, buf);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_write_byte(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;
	memcpy((smb_data + 1), buf, 1);

	for (;;) {
		ret = ssd_i2c_write(dev, saddr, 2, smb_data);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_read_byte(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;

	for (;;) {
		ret = ssd_i2c_write_read(dev, saddr, 1, smb_data, 1, buf);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_write_word(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;
	memcpy((smb_data + 1), buf, 2);

	for (;;) {
		ret = ssd_i2c_write(dev, saddr, 3, smb_data);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_read_word(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;

	for (;;) {
		ret = ssd_i2c_write_read(dev, saddr, 1, smb_data, 2, buf);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_write_block(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t size, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;
	smb_data[1] = size;
	memcpy((smb_data + 2), buf, size);

	for (;;) {
		ret = ssd_i2c_write(dev, saddr, (2 + size), smb_data);
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}

	return ret;
}

static int ssd_smbus_read_block(struct ssd_device *dev, uint8_t saddr, uint8_t cmd, uint8_t size, uint8_t *buf)
{
	uint8_t smb_data[SSD_SMBUS_DATA_MAX] = {0};
	uint8_t rsize;
	int i = 0;
	int ret = 0;

	smb_data[0] = cmd;

	for (;;) {
		ret = ssd_i2c_write_read(dev, saddr, 1, smb_data, (SSD_SMBUS_BLOCK_MAX + 1), (smb_data + 1));
		if (!ret || -ETIMEDOUT == ret) {
			break;
		}

		i++;
		if (i >= SSD_SMBUS_RETRY_MAX) {
			break;
		}
		msleep(SSD_SMBUS_RETRY_INTERVAL);
	}
	if (ret) {
		return ret;
	}

	rsize = smb_data[1];

	if (rsize > size ) {
		rsize = size;
	}

	memcpy(buf, (smb_data + 2), rsize);

	return 0;
}


static int ssd_gen_swlog(struct ssd_device *dev, uint16_t event, uint32_t data);

/* sensor */
static int ssd_init_lm75(struct ssd_device *dev, uint8_t saddr)
{
	uint8_t conf = 0;
	int ret = 0;

	ret = ssd_smbus_read_byte(dev, saddr, SSD_LM75_REG_CONF, &conf);
	if (ret) {
		goto out;
	}

	conf &= (uint8_t)(~1u);

	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM75_REG_CONF, &conf);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

static int ssd_lm75_read(struct ssd_device *dev, uint8_t saddr, uint16_t *data)
{
	uint16_t val = 0;
	int ret;

	ret = ssd_smbus_read_word(dev, saddr, SSD_LM75_REG_TEMP, (uint8_t *)&val);
	if (ret) {
		return ret;
	}

	*data = u16_swap(val);

	return 0;
}

static int ssd_init_lm80(struct ssd_device *dev, uint8_t saddr)
{
	uint8_t val;
	uint8_t low, high;
	int i;
	int ret = 0;

	/* init */
	val = 0x80;
	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_CONFIG, &val);
	if (ret) {
		goto out;
	}

	/* 11-bit temp */
	val = 0x08;
	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_RES, &val);
	if (ret) {
		goto out;
	}

	/* set volt limit */
	for (i=0; i<SSD_LM80_IN_NR; i++) {
		high = ssd_lm80_limit[i].high;
		low = ssd_lm80_limit[i].low;

		if (SSD_LM80_IN_CAP == i) {
			low = 0;
		}

		if (dev->hw_info.nr_ctrl <= 1 && SSD_LM80_IN_1V2 == i) {
			high = 0xFF;
			low = 0;
		}

		/* high limit */
		ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_IN_MAX(i), &high);
		if (ret) {
			goto out;
		}

		/* low limit*/
		ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_IN_MIN(i), &low);
		if (ret) {
			goto out;
		}
	}

	/* set interrupt mask: allow volt in interrupt except cap in*/
	val = 0x81;
	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_MASK1, &val);
	if (ret) {
		goto out;
	}

	/* set interrupt mask: disable others */
	val = 0xFF;
	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_MASK2, &val);
	if (ret) {
		goto out;
	}

	/* start */
	val = 0x03;
	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_CONFIG, &val);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

static int ssd_lm80_enable_in(struct ssd_device *dev, uint8_t saddr, int idx)
{
	uint8_t val = 0;
	int ret = 0;

	if (idx >= SSD_LM80_IN_NR || idx < 0) {
		return -EINVAL;
	}

	ret = ssd_smbus_read_byte(dev, saddr, SSD_LM80_REG_MASK1, &val);
	if (ret) {
		goto out;
	}

	val &= ~(1UL << (uint32_t)idx);

	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_MASK1, &val);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

static int ssd_lm80_disable_in(struct ssd_device *dev, uint8_t saddr, int idx)
{
	uint8_t val = 0;
	int ret = 0;

	if (idx >= SSD_LM80_IN_NR || idx < 0) {
		return -EINVAL;
	}

	ret = ssd_smbus_read_byte(dev, saddr, SSD_LM80_REG_MASK1, &val);
	if (ret) {
		goto out;
	}

	val |= (1UL << (uint32_t)idx);

	ret = ssd_smbus_write_byte(dev, saddr, SSD_LM80_REG_MASK1, &val);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

static int ssd_lm80_read_temp(struct ssd_device *dev, uint8_t saddr, uint16_t *data)
{
	uint16_t val = 0;
	int ret;

	ret = ssd_smbus_read_word(dev, saddr, SSD_LM80_REG_TEMP, (uint8_t *)&val);
	if (ret) {
		return ret;
	}

	*data = u16_swap(val);

	return 0;
}

static int ssd_lm80_check_event(struct ssd_device *dev, uint8_t saddr)
{
	uint32_t volt;
	uint16_t val = 0, status;
	uint8_t alarm1 = 0, alarm2 = 0;
	int i;
	int ret = 0;

	/* read interrupt status to clear interrupt */
	ret = ssd_smbus_read_byte(dev, saddr, SSD_LM80_REG_ALARM1, &alarm1);
	if (ret) {
		goto out;
	}

	ret = ssd_smbus_read_byte(dev, saddr, SSD_LM80_REG_ALARM2, &alarm2);
	if (ret) {
		goto out;
	}

	status = (uint16_t)alarm1 | ((uint16_t)alarm2 << 8);

	/* parse inetrrupt status */
	for (i=0; i<SSD_LM80_IN_NR; i++) {
		if (!((status >> (uint32_t)i) & 0x1)) {
			if (test_and_clear_bit(SSD_HWMON_LM80(i), &dev->hwmon)) {
				/* enable INx irq */
				ret = ssd_lm80_enable_in(dev, saddr, i);
				if (ret) {
					goto out;
				}
			}

			continue;
		}

		/* disable INx irq */
		ret = ssd_lm80_disable_in(dev, saddr, i);
		if (ret) {
			goto out;
		}

		if (test_and_set_bit(SSD_HWMON_LM80(i), &dev->hwmon)) {
			continue;
		}

		ret = ssd_smbus_read_word(dev, saddr, SSD_LM80_REG_IN(i), (uint8_t *)&val);
		if (ret) {
			goto out;
		}

		volt = SSD_LM80_CONVERT_VOLT(u16_swap(val));

		switch (i) {
			case SSD_LM80_IN_CAP: {
				if (0 == volt) {
					ssd_gen_swlog(dev, SSD_LOG_CAP_SHORT_CIRCUIT, 0);
				} else {
					ssd_gen_swlog(dev, SSD_LOG_CAP_VOLT_FAULT, SSD_PL_CAP_VOLT(volt));
				}
				break;
			}

			case SSD_LM80_IN_1V2:
			case SSD_LM80_IN_1V2a:
			case SSD_LM80_IN_1V5:
			case SSD_LM80_IN_1V8: {
				ssd_gen_swlog(dev, SSD_LOG_VOLT_STATUS, SSD_VOLT_LOG_DATA(i, 0, volt));
				break;
			}
			case SSD_LM80_IN_FPGA_3V3:
			case SSD_LM80_IN_3V3: {
				ssd_gen_swlog(dev, SSD_LOG_VOLT_STATUS, SSD_VOLT_LOG_DATA(i, 0, SSD_LM80_3V3_VOLT(volt)));
				break;
			}
			default:
				break;
		}
	}

out:
	if (ret) {
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, (uint32_t)saddr);
		}
	} else {
		test_and_clear_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon);
	}
	return ret;
}

static int ssd_init_sensor(struct ssd_device *dev)
{
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		goto out;
	}

	ret = ssd_init_lm75(dev, SSD_SENSOR_LM75_SADDRESS);
	if (ret) {
		hio_warn("%s: init lm75 failed\n", dev->name);
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM75), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM75_SADDRESS);
		}
		goto out;
	}

	if (dev->hw_info.pcb_ver >= 'B' || dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_HHHL) {
		ret = ssd_init_lm80(dev, SSD_SENSOR_LM80_SADDRESS);
		if (ret) {
			hio_warn("%s: init lm80 failed\n", dev->name);
			if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
				ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
			}
			goto out;
		}
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

/* board volt */
static int ssd_mon_boardvolt(struct ssd_device *dev)
{
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		return 0;
	}

	return ssd_lm80_check_event(dev, SSD_SENSOR_LM80_SADDRESS);
}

/* temperature */
static int ssd_mon_temp(struct ssd_device *dev)
{
	int cur;
	uint16_t val = 0;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		return 0;
	}

	/* inlet */
	ret = ssd_lm80_read_temp(dev, SSD_SENSOR_LM80_SADDRESS, &val);
	if (ret) {
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
		}
		goto out;
	}
	test_and_clear_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon);

	cur = SSD_SENSOR_CONVERT_TEMP(val);
	if (cur >= SSD_INLET_OT_TEMP) {
		if (!test_and_set_bit(SSD_HWMON_TEMP(SSD_TEMP_INLET), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_INLET_OVER_TEMP, (uint32_t)cur);
		}
	} else if(cur < SSD_INLET_OT_HYST) {
		if (test_and_clear_bit(SSD_HWMON_TEMP(SSD_TEMP_INLET), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_INLET_NORMAL_TEMP, (uint32_t)cur);
		}
	}

	/* flash */
	ret = ssd_lm75_read(dev, SSD_SENSOR_LM75_SADDRESS, &val);
	if (ret) {
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM75), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM75_SADDRESS);
		}
		goto out;
	}
	test_and_clear_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM75), &dev->hwmon);

	cur = SSD_SENSOR_CONVERT_TEMP(val);
	if (cur >= SSD_FLASH_OT_TEMP) {
		if (!test_and_set_bit(SSD_HWMON_TEMP(SSD_TEMP_FLASH), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_FLASH_OVER_TEMP, (uint32_t)cur);
		}
	} else if(cur < SSD_FLASH_OT_HYST) {
		if (test_and_clear_bit(SSD_HWMON_TEMP(SSD_TEMP_FLASH), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_FLASH_NORMAL_TEMP, (uint32_t)cur);
		}
	}

out:
	return ret;
}

/* cmd tag */
static inline void ssd_put_tag(struct ssd_device *dev, int tag)
{
	test_and_clear_bit(tag,  dev->tag_map);
	wake_up(&dev->tag_wq);
}

static inline int ssd_get_tag(struct ssd_device *dev, int wait)
{
	int tag;

find_tag:
	while ((tag = find_first_zero_bit(dev->tag_map, dev->hw_info.cmd_fifo_sz)) >= atomic_read(&dev->queue_depth)) {
		DEFINE_WAIT(__wait);

		if (!wait) {
			return -1;
		}

		prepare_to_wait_exclusive(&dev->tag_wq, &__wait, TASK_UNINTERRUPTIBLE);
		schedule();

		finish_wait(&dev->tag_wq, &__wait);
	}

	if (test_and_set_bit(tag, dev->tag_map)) {
		goto find_tag;
	}

	return tag;
}

static void ssd_barrier_put_tag(struct ssd_device *dev, int tag)
{
	test_and_clear_bit(tag,  dev->tag_map);
}

static int ssd_barrier_get_tag(struct ssd_device *dev)
{
	int tag = 0;

	if (test_and_set_bit(tag, dev->tag_map)) {
		return -1;
	}

	return tag;
}

static void ssd_barrier_end(struct ssd_device *dev)
{
	atomic_set(&dev->queue_depth, dev->hw_info.cmd_fifo_sz);
	wake_up_all(&dev->tag_wq);

	mutex_unlock(&dev->barrier_mutex);
}

static int ssd_barrier_start(struct ssd_device *dev)
{
	int i;

	mutex_lock(&dev->barrier_mutex);

	atomic_set(&dev->queue_depth, 0);

	for (i=0; i<SSD_CMD_TIMEOUT; i++) {
		if (find_first_bit(dev->tag_map, dev->hw_info.cmd_fifo_sz) >= dev->hw_info.cmd_fifo_sz) {
			return 0;
		}

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}

	atomic_set(&dev->queue_depth, dev->hw_info.cmd_fifo_sz);
	wake_up_all(&dev->tag_wq);

	mutex_unlock(&dev->barrier_mutex);

	return -EBUSY;
}

static int ssd_busy(struct ssd_device *dev)
{
	if (find_first_bit(dev->tag_map, dev->hw_info.cmd_fifo_sz) >= dev->hw_info.cmd_fifo_sz) {
		return 0;
	}

	return 1;
}

static int ssd_wait_io(struct ssd_device *dev)
{
	int i;

	for (i=0; i<SSD_CMD_TIMEOUT; i++) {
		if (find_first_bit(dev->tag_map, dev->hw_info.cmd_fifo_sz) >= dev->hw_info.cmd_fifo_sz) {
			return 0;
		}

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}

	return -EBUSY;
}

#if 0
static int ssd_in_barrier(struct ssd_device *dev)
{
	return (0 == atomic_read(&dev->queue_depth));
}
#endif

static void ssd_cleanup_tag(struct ssd_device *dev)
{
	kfree(dev->tag_map);
}

static int ssd_init_tag(struct ssd_device *dev)
{
	int nr_ulongs = ALIGN(dev->hw_info.cmd_fifo_sz, BITS_PER_LONG) / BITS_PER_LONG;

	mutex_init(&dev->barrier_mutex);

	atomic_set(&dev->queue_depth, dev->hw_info.cmd_fifo_sz);

	dev->tag_map = kmalloc(nr_ulongs * sizeof(unsigned long), GFP_ATOMIC);
	if (!dev->tag_map) {
		return -ENOMEM;
	}

	memset(dev->tag_map, 0, nr_ulongs * sizeof(unsigned long));

	init_waitqueue_head(&dev->tag_wq);

	return 0;
}

/* io stat */
static void ssd_end_io_acct(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = cmd->dev;
	struct bio *bio = cmd->bio;
	unsigned long dur = jiffies - cmd->start_time;
	int rw = bio_data_dir(bio);

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6 && RHEL_MINOR >= 7))
	int cpu = part_stat_lock();
	struct hd_struct *part = disk_map_sector_rcu(dev->gd, bio_start(bio));
	part_round_stats(cpu, part);
	part_stat_add(cpu, part, ticks[rw], dur);
	part_dec_in_flight(part, rw);
	part_stat_unlock();
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
	int cpu = part_stat_lock();
	struct hd_struct *part = &dev->gd->part0;
	part_round_stats(cpu, part);
	part_stat_add(cpu, part, ticks[rw], dur);
	part_stat_unlock();
	part->in_flight[rw] = atomic_dec_return(&dev->in_flight[rw]);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,14))
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();
	disk_stat_add(dev->gd, ticks[rw], dur);
	dev->gd->in_flight = atomic_dec_return(&dev->in_flight[0]);
#else
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();
	if (rw == WRITE) {
		disk_stat_add(dev->gd, write_ticks, dur);
	} else {
		disk_stat_add(dev->gd, read_ticks, dur);
	}
	dev->gd->in_flight = atomic_dec_return(&dev->in_flight[0]);
#endif
}

static void ssd_start_io_acct(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = cmd->dev;
	struct bio *bio = cmd->bio;
	int rw = bio_data_dir(bio);

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6 && RHEL_MINOR >= 7))
	int cpu = part_stat_lock();
	struct hd_struct *part = disk_map_sector_rcu(dev->gd, bio_start(bio));
	part_round_stats(cpu, part);
	part_stat_inc(cpu, part, ios[rw]);
	part_stat_add(cpu, part, sectors[rw], bio_sectors(bio));
	part_inc_in_flight(part, rw);
	part_stat_unlock();
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
	int cpu = part_stat_lock();
	struct hd_struct *part = &dev->gd->part0;
	part_round_stats(cpu, part);
	part_stat_inc(cpu, part, ios[rw]);
	part_stat_add(cpu, part, sectors[rw], bio_sectors(bio));
	part_stat_unlock();
	part->in_flight[rw] = atomic_inc_return(&dev->in_flight[rw]);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,14))
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();
	disk_stat_inc(dev->gd, ios[rw]);
	disk_stat_add(dev->gd, sectors[rw], bio_sectors(bio));
	dev->gd->in_flight = atomic_inc_return(&dev->in_flight[0]);
#else
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();
	if (rw == WRITE) {
		disk_stat_inc(dev->gd, writes);
		disk_stat_add(dev->gd, write_sectors, bio_sectors(bio));
	} else {
		disk_stat_inc(dev->gd, reads);
		disk_stat_add(dev->gd, read_sectors, bio_sectors(bio));
	}
	dev->gd->in_flight = atomic_inc_return(&dev->in_flight[0]);
#endif

	cmd->start_time = jiffies;
}

/* io */
static void ssd_queue_bio(struct ssd_device *dev, struct bio *bio)
{
	spin_lock(&dev->sendq_lock);
	ssd_blist_add(&dev->sendq, bio);
	spin_unlock(&dev->sendq_lock);

	atomic_inc(&dev->in_sendq);
	wake_up(&dev->send_waitq);
}

static inline void ssd_end_request(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = cmd->dev;
	struct bio *bio = cmd->bio;
	int errors = cmd->errors;
	int tag = cmd->tag;

	if (bio) {
#if (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)))
		if (!(bio->bi_rw & REQ_DISCARD)) {
			ssd_end_io_acct(cmd);
			if (!cmd->flag) {
				pci_unmap_sg(dev->pdev, cmd->sgl, cmd->nsegs, 
					bio_data_dir(bio) == READ ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
			}
		}
#elif (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)))
		if (!bio_rw_flagged(bio, BIO_RW_DISCARD)) {
			ssd_end_io_acct(cmd);
			if (!cmd->flag) {
				pci_unmap_sg(dev->pdev, cmd->sgl, cmd->nsegs, 
					bio_data_dir(bio) == READ ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
			}
		}
#else
		ssd_end_io_acct(cmd);

		if (!cmd->flag) {
			pci_unmap_sg(dev->pdev, cmd->sgl, cmd->nsegs, 
				bio_data_dir(bio) == READ ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
		}
#endif

		cmd->bio = NULL;
		ssd_put_tag(dev, tag);

		if (SSD_INT_MSIX == dev->int_mode || tag < 16 || errors) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
			bio_endio(bio, errors);
#else
			bio_endio(bio, bio->bi_size, errors);
#endif
		} else /* if (bio->bi_idx >= bio->bi_vcnt)*/ {
			spin_lock(&dev->doneq_lock);
			ssd_blist_add(&dev->doneq, bio);
			spin_unlock(&dev->doneq_lock);

			atomic_inc(&dev->in_doneq);
			wake_up(&dev->done_waitq);
		}
	} else {
		if (cmd->waiting) {
			complete(cmd->waiting);
		}
	}
}

static void ssd_end_timeout_request(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = cmd->dev;
	struct ssd_rw_msg *msg = (struct ssd_rw_msg *)cmd->msg;
	int i;

	for (i=0; i<dev->nr_queue; i++) {
		disable_irq(dev->entry[i].vector);
	}

	atomic_inc(&dev->tocnt);
	//if (cmd->bio) {
		hio_err("%s: cmd timeout: tag %d fun %#x\n", dev->name, msg->tag, msg->fun);
		cmd->errors = -ETIMEDOUT;
		ssd_end_request(cmd);
	//}

	for (i=0; i<dev->nr_queue; i++) {
		enable_irq(dev->entry[i].vector);
	}

	/* alarm led */
	ssd_set_alarm(dev);
}

/* cmd timer */
static void ssd_cmd_add_timer(struct ssd_cmd *cmd, int timeout, void (*complt)(struct ssd_cmd *))
{
	init_timer(&cmd->cmd_timer);

	cmd->cmd_timer.data = (unsigned long)cmd;
	cmd->cmd_timer.expires = jiffies + timeout;
	cmd->cmd_timer.function = (void (*)(unsigned long)) complt;

	add_timer(&cmd->cmd_timer);
}

static int ssd_cmd_del_timer(struct ssd_cmd *cmd)
{
	return del_timer(&cmd->cmd_timer);
}

static void ssd_add_timer(struct timer_list *timer, int timeout, void (*complt)(void *), void *data)
{
	init_timer(timer);

	timer->data = (unsigned long)data;
	timer->expires = jiffies + timeout;
	timer->function = (void (*)(unsigned long)) complt;

	add_timer(timer);
}

static int ssd_del_timer(struct timer_list *timer)
{
	return del_timer(timer);
}

static void ssd_cmd_timeout(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = cmd->dev;
	uint32_t msg = *(uint32_t *)cmd->msg;

	ssd_end_timeout_request(cmd);

	ssd_gen_swlog(dev, SSD_LOG_TIMEOUT, msg);
}


static void __ssd_done(unsigned long data)
{
	struct ssd_cmd *cmd;
	LIST_HEAD(localq);

	local_irq_disable();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0))
	list_splice_init(&__get_cpu_var(ssd_doneq), &localq);
#else
	list_splice_init(this_cpu_ptr(&ssd_doneq), &localq);
#endif
	local_irq_enable();

	while (!list_empty(&localq)) {
		cmd = list_entry(localq.next, struct ssd_cmd, list);
		list_del_init(&cmd->list);

		ssd_end_request(cmd);
	}
}

static void __ssd_done_db(unsigned long data)
{
	struct ssd_cmd *cmd;
	struct ssd_device *dev;
	struct bio *bio;
	LIST_HEAD(localq);

	local_irq_disable();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0))
	list_splice_init(&__get_cpu_var(ssd_doneq), &localq);
#else
	list_splice_init(this_cpu_ptr(&ssd_doneq), &localq);
#endif
	local_irq_enable();

	while (!list_empty(&localq)) {
		cmd = list_entry(localq.next, struct ssd_cmd, list);
		list_del_init(&cmd->list);

		dev = (struct ssd_device *)cmd->dev;
		bio = cmd->bio;

		if (bio) {
			sector_t off = dev->db_info.data.loc.off;
			uint32_t len = dev->db_info.data.loc.len;

			switch (dev->db_info.type) {
				case SSD_DEBUG_READ_ERR:
					if (bio_data_dir(bio) == READ && 
						!((off + len) <= bio_start(bio) || off >= (bio_start(bio) + bio_sectors(bio)))) {
						cmd->errors = -EIO;
					}
					break;
				case SSD_DEBUG_WRITE_ERR:
					if (bio_data_dir(bio) == WRITE && 
						!((off + len) <= bio_start(bio) || off >= (bio_start(bio) + bio_sectors(bio)))) {
						cmd->errors = -EROFS;
					}
					break;
				case SSD_DEBUG_RW_ERR:
					if (!((off + len) <= bio_start(bio) || off >= (bio_start(bio) + bio_sectors(bio)))) {
						if (bio_data_dir(bio) == READ) {
							cmd->errors = -EIO;
						} else {
							cmd->errors = -EROFS;
						}
					}
					break;
				default:
					break;
			}
		}

		ssd_end_request(cmd);
	}
}

static inline void ssd_done_bh(struct ssd_cmd *cmd)
{
	unsigned long flags = 0;

	if (unlikely(!ssd_cmd_del_timer(cmd))) {
		struct ssd_device *dev = cmd->dev;
		struct ssd_rw_msg *msg = (struct ssd_rw_msg *)cmd->msg;
		hio_err("%s: unknown cmd: tag %d fun %#x\n", dev->name, msg->tag, msg->fun);

		/* alarm led */
		ssd_set_alarm(dev);
		return;
	}

	local_irq_save(flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0))
	list_add_tail(&cmd->list, &__get_cpu_var(ssd_doneq));
	tasklet_hi_schedule(&__get_cpu_var(ssd_tasklet));
#else
	list_add_tail(&cmd->list, this_cpu_ptr(&ssd_doneq));
	tasklet_hi_schedule(this_cpu_ptr(&ssd_tasklet));
#endif
	local_irq_restore(flags);

	return;
}

static inline void ssd_done(struct ssd_cmd *cmd)
{
	if (unlikely(!ssd_cmd_del_timer(cmd))) {
		struct ssd_device *dev = cmd->dev;
		struct ssd_rw_msg *msg = (struct ssd_rw_msg *)cmd->msg;
		hio_err("%s: unknown cmd: tag %d fun %#x\n", dev->name, msg->tag, msg->fun);

		/* alarm led */
		ssd_set_alarm(dev);
		return;
	}

	ssd_end_request(cmd);

	return;
}

static inline void ssd_dispatch_cmd(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = (struct ssd_device *)cmd->dev;

	ssd_cmd_add_timer(cmd, SSD_CMD_TIMEOUT, ssd_cmd_timeout);

	spin_lock(&dev->cmd_lock);
	ssd_reg_write(dev->ctrlp + SSD_REQ_FIFO_REG, cmd->msg_dma);
	spin_unlock(&dev->cmd_lock);
}

static inline void ssd_send_cmd(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = (struct ssd_device *)cmd->dev;

	ssd_cmd_add_timer(cmd, SSD_CMD_TIMEOUT, ssd_cmd_timeout);

	ssd_reg32_write(dev->ctrlp + SSD_REQ_FIFO_REG, ((uint32_t)cmd->tag | ((uint32_t)cmd->nsegs << 16)));
}

static inline void ssd_send_cmd_db(struct ssd_cmd *cmd)
{
	struct ssd_device *dev = (struct ssd_device *)cmd->dev;
	struct bio *bio = cmd->bio;

	ssd_cmd_add_timer(cmd, SSD_CMD_TIMEOUT, ssd_cmd_timeout);

	if (bio) {
		switch (dev->db_info.type) {
			case SSD_DEBUG_READ_TO:
				if (bio_data_dir(bio) == READ) {
					return;
				}
				break;
			case SSD_DEBUG_WRITE_TO:
				if (bio_data_dir(bio) == WRITE) {
					return;
				}
				break;
			case SSD_DEBUG_RW_TO:
				return;
				break;
			default:
				break;
		}
	}

	ssd_reg32_write(dev->ctrlp + SSD_REQ_FIFO_REG, ((uint32_t)cmd->tag | ((uint32_t)cmd->nsegs << 16)));
}


/* fixed for BIOVEC_PHYS_MERGEABLE */
#ifdef SSD_BIOVEC_PHYS_MERGEABLE_FIXED
#include <linux/bio.h>
#include <linux/io.h>
#include <xen/page.h>

static bool xen_biovec_phys_mergeable_fixed(const struct bio_vec *vec1,
			       const struct bio_vec *vec2)
{
	unsigned long mfn1 = pfn_to_mfn(page_to_pfn(vec1->bv_page));
	unsigned long mfn2 = pfn_to_mfn(page_to_pfn(vec2->bv_page));

	return __BIOVEC_PHYS_MERGEABLE(vec1, vec2) &&
		((mfn1 == mfn2) || ((mfn1+1) == mfn2));
}

#ifdef BIOVEC_PHYS_MERGEABLE
#undef BIOVEC_PHYS_MERGEABLE
#endif
#define BIOVEC_PHYS_MERGEABLE(vec1, vec2)				\
	(__BIOVEC_PHYS_MERGEABLE(vec1, vec2) &&				\
	 (!xen_domain() || xen_biovec_phys_mergeable_fixed(vec1, vec2)))

#endif

static inline int ssd_bio_map_sg(struct ssd_device *dev, struct bio *bio, struct scatterlist *sgl)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
	struct bio_vec *bvec, *bvprv = NULL;
	struct scatterlist *sg = NULL;
	int i = 0, nsegs = 0;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23))
	sg_init_table(sgl, dev->hw_info.cmd_max_sg);
#endif

	/*
	* for each segment in bio
	*/
	bio_for_each_segment(bvec, bio, i) {
		if (bvprv && BIOVEC_PHYS_MERGEABLE(bvprv, bvec)) {
			sg->length += bvec->bv_len;
		} else {
			if (unlikely(nsegs >= (int)dev->hw_info.cmd_max_sg)) {
				break;
			}

			sg = sg ? (sg + 1) : sgl;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
			sg_set_page(sg, bvec->bv_page, bvec->bv_len, bvec->bv_offset);
#else
			sg->page = bvec->bv_page;
			sg->length = bvec->bv_len;
			sg->offset = bvec->bv_offset;
#endif
			nsegs++;
		}
		bvprv = bvec;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	if (sg) {
		sg_mark_end(sg);
	}
#endif

	bio->bi_idx = i;

	return nsegs;
#else
	struct bio_vec bvec, bvprv;
	struct bvec_iter iter;
	struct scatterlist *sg = NULL;
	int nsegs = 0;
	int first = 1;

	sg_init_table(sgl, dev->hw_info.cmd_max_sg);

	/*
	* for each segment in bio
	*/
	bio_for_each_segment(bvec, bio, iter) {
		if (!first && BIOVEC_PHYS_MERGEABLE(&bvprv, &bvec)) {
			sg->length += bvec.bv_len;
		} else {
			if (unlikely(nsegs >= (int)dev->hw_info.cmd_max_sg)) {
				break;
			}

			sg = sg ? (sg + 1) : sgl;

			sg_set_page(sg, bvec.bv_page, bvec.bv_len, bvec.bv_offset);

			nsegs++;
			first = 0;
		}
		bvprv = bvec;
	}

	if (sg) {
		sg_mark_end(sg);
	}

	return nsegs;
#endif
}


static int __ssd_submit_pbio(struct ssd_device *dev, struct bio *bio, int wait)
{
	struct ssd_cmd *cmd;
	struct ssd_rw_msg *msg;
	struct ssd_sg_entry *sge;
	sector_t block = bio_start(bio);
	int tag;
	int i;

	tag = ssd_get_tag(dev, wait);
	if (tag < 0) {
		return -EBUSY;
	}

	cmd = &dev->cmd[tag];
	cmd->bio = bio;
	cmd->flag = 1;

	msg = (struct ssd_rw_msg *)cmd->msg;

#if (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)))
	if (bio->bi_rw & REQ_DISCARD) {
		unsigned int length = bio_sectors(bio);

		//printk(KERN_WARNING "%s: discard len %u, block %llu\n", dev->name, bio_sectors(bio), block);
		msg->tag = tag;
		msg->fun = SSD_FUNC_TRIM;

		sge = msg->sge;
		for (i=0; i<(dev->hw_info.cmd_max_sg); i++) {
			sge->block = block;
			sge->length = (length >= dev->hw_info.sg_max_sec) ? dev->hw_info.sg_max_sec : length;
			sge->buf = 0;

			block += sge->length;
			length -= sge->length;
			sge++;

			if (length <= 0) {
				break;
			}
		}
		msg->nsegs = cmd->nsegs = (i + 1);

		dev->scmd(cmd);
		return 0;
	}
#elif (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)))
	if (bio_rw_flagged(bio, BIO_RW_DISCARD)) {
		unsigned int length = bio_sectors(bio);

		//printk(KERN_WARNING "%s: discard len %u, block %llu\n", dev->name, bio_sectors(bio), block);
		msg->tag = tag;
		msg->fun = SSD_FUNC_TRIM;

		sge = msg->sge;
		for (i=0; i<(dev->hw_info.cmd_max_sg); i++) {
			sge->block = block;
			sge->length = (length >= dev->hw_info.sg_max_sec) ? dev->hw_info.sg_max_sec : length;
			sge->buf = 0;

			block += sge->length;
			length -= sge->length;
			sge++;

			if (length <= 0) {
				break;
			}
		}
		msg->nsegs = cmd->nsegs = (i + 1);

		dev->scmd(cmd);
		return 0;
	}
#endif

	//msg->nsegs = cmd->nsegs = ssd_bio_map_sg(dev, bio, sgl);
	msg->nsegs = cmd->nsegs = bio->bi_vcnt;

	//xx
	if (bio_data_dir(bio) == READ) {
		msg->fun = SSD_FUNC_READ;
		msg->flag = 0;
	} else {
		msg->fun = SSD_FUNC_WRITE;
		msg->flag = dev->wmode;
	}

	sge = msg->sge;
	for (i=0; i<bio->bi_vcnt; i++) {
		sge->block = block;
		sge->length = bio->bi_io_vec[i].bv_len >> 9;
		sge->buf = (uint64_t)((void *)bio->bi_io_vec[i].bv_page + bio->bi_io_vec[i].bv_offset);

		block += sge->length;
		sge++;
	}

	msg->tag = tag;

#ifdef SSD_OT_PROTECT
	if (unlikely(dev->ot_delay > 0 && dev->ot_protect != 0)) {
		msleep_interruptible(dev->ot_delay);
	}
#endif

	ssd_start_io_acct(cmd);
	dev->scmd(cmd);

	return 0;
}

static inline int ssd_submit_bio(struct ssd_device *dev, struct bio *bio, int wait)
{
	struct ssd_cmd *cmd;
	struct ssd_rw_msg *msg;
	struct ssd_sg_entry *sge;
	struct scatterlist *sgl;
	sector_t block = bio_start(bio);
	int tag;
	int i;

	tag = ssd_get_tag(dev, wait);
	if (tag < 0) {
		return -EBUSY;
	}

	cmd = &dev->cmd[tag];
	cmd->bio = bio;
	cmd->flag = 0;

	msg = (struct ssd_rw_msg *)cmd->msg;

	sgl = cmd->sgl;

#if (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)))
	if (bio->bi_rw & REQ_DISCARD) {
		unsigned int length = bio_sectors(bio);

		//printk(KERN_WARNING "%s: discard len %u, block %llu\n", dev->name, bio_sectors(bio), block);
		msg->tag = tag;
		msg->fun = SSD_FUNC_TRIM;

		sge = msg->sge;
		for (i=0; i<(dev->hw_info.cmd_max_sg); i++) {
			sge->block = block;
			sge->length = (length >= dev->hw_info.sg_max_sec) ? dev->hw_info.sg_max_sec : length;
			sge->buf = 0;

			block += sge->length;
			length -= sge->length;
			sge++;

			if (length <= 0) {
				break;
			}
		}
		msg->nsegs = cmd->nsegs = (i + 1);

		dev->scmd(cmd);
		return 0;
	}
#elif (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)))
	if (bio_rw_flagged(bio, BIO_RW_DISCARD)) {
		unsigned int length = bio_sectors(bio);

		//printk(KERN_WARNING "%s: discard len %u, block %llu\n", dev->name, bio_sectors(bio), block);
		msg->tag = tag;
		msg->fun = SSD_FUNC_TRIM;

		sge = msg->sge;
		for (i=0; i<(dev->hw_info.cmd_max_sg); i++) {
			sge->block = block;
			sge->length = (length >= dev->hw_info.sg_max_sec) ? dev->hw_info.sg_max_sec : length;
			sge->buf = 0;

			block += sge->length;
			length -= sge->length;
			sge++;

			if (length <= 0) {
				break;
			}
		}
		msg->nsegs = cmd->nsegs = (i + 1);

		dev->scmd(cmd);
		return 0;
	}
#endif

	msg->nsegs = cmd->nsegs = ssd_bio_map_sg(dev, bio, sgl);

	//xx
	if (bio_data_dir(bio) == READ) {
		msg->fun = SSD_FUNC_READ;
		msg->flag = 0;
		pci_map_sg(dev->pdev, sgl, cmd->nsegs, PCI_DMA_FROMDEVICE);
	} else {
		msg->fun = SSD_FUNC_WRITE;
		msg->flag = dev->wmode;
		pci_map_sg(dev->pdev, sgl, cmd->nsegs, PCI_DMA_TODEVICE);
	}

	sge = msg->sge;
	for (i=0; i<cmd->nsegs; i++) {
		sge->block = block;
		sge->length = sg_dma_len(sgl) >> 9;
		sge->buf = sg_dma_address(sgl);

		block += sge->length;
		sgl++;
		sge++;
	}

	msg->tag = tag;

#ifdef SSD_OT_PROTECT
	if (unlikely(dev->ot_delay > 0 && dev->ot_protect != 0)) {
		msleep_interruptible(dev->ot_delay);
	}
#endif

	ssd_start_io_acct(cmd);
	dev->scmd(cmd);

	return 0;
}

/* threads */
static int ssd_done_thread(void *data)
{
	struct ssd_device *dev;
	struct bio *bio;
	struct bio *next;

	if (!data) {
		return -EINVAL;
	}
	dev = data;

	//set_user_nice(current, -5);

	while (!kthread_should_stop()) {
		wait_event_interruptible(dev->done_waitq, (atomic_read(&dev->in_doneq) || kthread_should_stop()));

		while (atomic_read(&dev->in_doneq)) {
			if (threaded_irq) {
				spin_lock(&dev->doneq_lock);
				bio = ssd_blist_get(&dev->doneq);
				spin_unlock(&dev->doneq_lock);
			} else {
				spin_lock_irq(&dev->doneq_lock);
				bio = ssd_blist_get(&dev->doneq);
				spin_unlock_irq(&dev->doneq_lock);
			}

			while (bio) {
				next = bio->bi_next;
				bio->bi_next = NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
				bio_endio(bio, 0);
#else
				bio_endio(bio, bio->bi_size, 0);
#endif
				atomic_dec(&dev->in_doneq);
				bio = next;
			}

			cond_resched();

#ifdef SSD_ESCAPE_IRQ
			if (unlikely(smp_processor_id() == dev->irq_cpu)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
				cpumask_var_t new_mask;
				alloc_cpumask_var(&new_mask, GFP_ATOMIC);
				cpumask_setall(new_mask);
				cpumask_clear_cpu(dev->irq_cpu, new_mask);
				set_cpus_allowed_ptr(current, new_mask);
				free_cpumask_var(new_mask);
#else
				cpumask_t new_mask;
				cpus_setall(new_mask);
				cpu_clear(dev->irq_cpu, new_mask);
				set_cpus_allowed(current, new_mask);
#endif
			}
#endif
		}
	}
	return 0;
}

static int ssd_send_thread(void *data)
{
	struct ssd_device *dev;
	struct bio *bio;
	struct bio *next;

	if (!data) {
		return -EINVAL;
	}
	dev = data;

	//set_user_nice(current, -5);

	while (!kthread_should_stop()) {
		wait_event_interruptible(dev->send_waitq, (atomic_read(&dev->in_sendq) || kthread_should_stop()));

		while (atomic_read(&dev->in_sendq)) {
			spin_lock(&dev->sendq_lock);
			bio = ssd_blist_get(&dev->sendq);
			spin_unlock(&dev->sendq_lock);

			while (bio) {
				next = bio->bi_next;
				bio->bi_next = NULL;
#ifdef SSD_QUEUE_PBIO
				if (test_and_clear_bit(BIO_SSD_PBIO, &bio->bi_flags)) {
					__ssd_submit_pbio(dev, bio, 1);
				} else {
					ssd_submit_bio(dev, bio, 1);
				}
#else
				ssd_submit_bio(dev, bio, 1);
#endif
				atomic_dec(&dev->in_sendq);
				bio = next;
			}

			cond_resched();

#ifdef SSD_ESCAPE_IRQ
			if (unlikely(smp_processor_id() == dev->irq_cpu)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
				cpumask_var_t new_mask;
				alloc_cpumask_var(&new_mask, GFP_ATOMIC);
				cpumask_setall(new_mask);
				cpumask_clear_cpu(dev->irq_cpu, new_mask);
				set_cpus_allowed_ptr(current, new_mask);
				free_cpumask_var(new_mask);
#else
				cpumask_t new_mask;
				cpus_setall(new_mask);
				cpu_clear(dev->irq_cpu, new_mask);
				set_cpus_allowed(current, new_mask);
#endif
			}
#endif
		}
	}

	return 0;
}

static void ssd_cleanup_thread(struct ssd_device *dev)
{
	kthread_stop(dev->send_thread);
	kthread_stop(dev->done_thread);
}

static int ssd_init_thread(struct ssd_device *dev)
{
	int ret;

	atomic_set(&dev->in_doneq, 0);
	atomic_set(&dev->in_sendq, 0);

	spin_lock_init(&dev->doneq_lock);
	spin_lock_init(&dev->sendq_lock);

	ssd_blist_init(&dev->doneq);
	ssd_blist_init(&dev->sendq);

	init_waitqueue_head(&dev->done_waitq);
	init_waitqueue_head(&dev->send_waitq);

	dev->done_thread = kthread_run(ssd_done_thread, dev, "%s/d", dev->name);
	if (IS_ERR(dev->done_thread)) {
		ret = PTR_ERR(dev->done_thread);
		goto out_done_thread;
	}

	dev->send_thread = kthread_run(ssd_send_thread, dev, "%s/s", dev->name);
	if (IS_ERR(dev->send_thread)) {
		ret = PTR_ERR(dev->send_thread);
		goto out_send_thread;
	}

	return 0;

out_send_thread:
	kthread_stop(dev->done_thread);
out_done_thread:
	return ret;
}

/* dcmd pool */
static void ssd_put_dcmd(struct ssd_dcmd *dcmd)
{
	struct ssd_device *dev = (struct ssd_device *)dcmd->dev;

	spin_lock(&dev->dcmd_lock);
	list_add_tail(&dcmd->list, &dev->dcmd_list);
	spin_unlock(&dev->dcmd_lock);
}

static struct ssd_dcmd *ssd_get_dcmd(struct ssd_device *dev)
{
	struct ssd_dcmd *dcmd = NULL;

	spin_lock(&dev->dcmd_lock);
	if (!list_empty(&dev->dcmd_list)) {
		dcmd = list_entry(dev->dcmd_list.next, 
				 struct ssd_dcmd, list);
		list_del_init(&dcmd->list);
	}
	spin_unlock(&dev->dcmd_lock);

	return dcmd;
}

static void ssd_cleanup_dcmd(struct ssd_device *dev)
{
	kfree(dev->dcmd);
}

static int ssd_init_dcmd(struct ssd_device *dev)
{
	struct ssd_dcmd *dcmd;
	int dcmd_sz = sizeof(struct ssd_dcmd)*dev->hw_info.cmd_fifo_sz;
	int i;

	spin_lock_init(&dev->dcmd_lock);
	INIT_LIST_HEAD(&dev->dcmd_list);
	init_waitqueue_head(&dev->dcmd_wq);

	dev->dcmd = kmalloc(dcmd_sz, GFP_KERNEL);
	if (!dev->dcmd) {
		hio_warn("%s: can not alloc dcmd\n", dev->name);
		goto out_alloc_dcmd;
	}
	memset(dev->dcmd, 0, dcmd_sz);

	for (i=0, dcmd=dev->dcmd; i<(int)dev->hw_info.cmd_fifo_sz; i++, dcmd++) {
		dcmd->dev = dev;
		INIT_LIST_HEAD(&dcmd->list);
		list_add_tail(&dcmd->list, &dev->dcmd_list);
	}

	return 0;

out_alloc_dcmd:
	return -ENOMEM;
}

static void ssd_put_dmsg(void *msg)
{
	struct ssd_dcmd *dcmd = container_of(msg, struct ssd_dcmd, msg);
	struct ssd_device *dev = (struct ssd_device *)dcmd->dev;

	memset(dcmd->msg, 0, SSD_DCMD_MAX_SZ);
	ssd_put_dcmd(dcmd);
	wake_up(&dev->dcmd_wq);
}

static void *ssd_get_dmsg(struct ssd_device *dev)
{
	struct ssd_dcmd *dcmd = ssd_get_dcmd(dev);

	while (!dcmd) {
		DEFINE_WAIT(wait);
		prepare_to_wait_exclusive(&dev->dcmd_wq, &wait, TASK_UNINTERRUPTIBLE);
		schedule();

		dcmd = ssd_get_dcmd(dev);

		finish_wait(&dev->dcmd_wq, &wait);
	}
	return dcmd->msg;
}

/* do direct cmd */
static int ssd_do_request(struct ssd_device *dev, int rw, void *msg, int *done)
{
	DECLARE_COMPLETION(wait);
	struct ssd_cmd *cmd;
	int tag;
	int ret = 0;

	tag = ssd_get_tag(dev, 1);
	if (tag < 0) {
		return -EBUSY;
	}

	cmd = &dev->cmd[tag];
	cmd->nsegs = 1;
	memcpy(cmd->msg, msg, SSD_DCMD_MAX_SZ);
	((struct ssd_rw_msg *)cmd->msg)->tag = tag;

	cmd->waiting = &wait;

	dev->scmd(cmd);

	wait_for_completion(cmd->waiting);
	cmd->waiting = NULL;

	if (cmd->errors == -ETIMEDOUT) {
		ret = cmd->errors;
	} else if (cmd->errors) {
		ret = -EIO;
	}

	if (done != NULL) {
		*done = cmd->nr_log;
	}
	ssd_put_tag(dev, cmd->tag);

	return ret;
}

static int ssd_do_barrier_request(struct ssd_device *dev, int rw, void *msg, int *done)
{
	DECLARE_COMPLETION(wait);
	struct ssd_cmd *cmd;
	int tag;
	int ret = 0;

	tag = ssd_barrier_get_tag(dev);
	if (tag < 0) {
		return -EBUSY;
	}

	cmd = &dev->cmd[tag];
	cmd->nsegs = 1;
	memcpy(cmd->msg, msg, SSD_DCMD_MAX_SZ);
	((struct ssd_rw_msg *)cmd->msg)->tag = tag;

	cmd->waiting = &wait;

	dev->scmd(cmd);

	wait_for_completion(cmd->waiting);
	cmd->waiting = NULL;

	if (cmd->errors == -ETIMEDOUT) {
		ret = cmd->errors;
	} else if (cmd->errors) {
		ret = -EIO;
	}

	if (done != NULL) {
		*done = cmd->nr_log;
	}
	ssd_barrier_put_tag(dev, cmd->tag);

	return ret;
}

#ifdef SSD_OT_PROTECT
static void ssd_check_temperature(struct ssd_device *dev, int temp)
{
	uint64_t val;
	uint32_t off;
	int cur;
	int i;

	if (mode != SSD_DRV_MODE_STANDARD) {
		return;
	}

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
	}

	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		off = SSD_CTRL_TEMP_REG0 + i * sizeof(uint64_t);

		val = ssd_reg_read(dev->ctrlp + off);
		if (val == 0xffffffffffffffffull) {
			continue;
		}

		cur = (int)CUR_TEMP(val);
		if (cur >= temp) {
			if (!test_and_set_bit(SSD_HWMON_TEMP(SSD_TEMP_CTRL), &dev->hwmon)) {
				if (dev->protocol_info.ver > SSD_PROTOCOL_V3 && dev->protocol_info.ver < SSD_PROTOCOL_V3_2_2) {
					hio_warn("%s: Over temperature, please check the fans.\n", dev->name);
					dev->ot_delay = SSD_OT_DELAY;
				}
			}
			return;
		}
	}

	if (test_and_clear_bit(SSD_HWMON_TEMP(SSD_TEMP_CTRL), &dev->hwmon)) {
		if (dev->protocol_info.ver > SSD_PROTOCOL_V3 && dev->protocol_info.ver < SSD_PROTOCOL_V3_2_2) {
			hio_warn("%s: Temperature is OK.\n", dev->name);
			dev->ot_delay = 0;
		}
	}
}
#endif

static int ssd_get_ot_status(struct ssd_device *dev, int *status)
{
	uint32_t off;
	uint32_t val;
	int i;

	if (!dev || !status) {
		return -EINVAL;
	}

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2_2) {
		for (i=0; i<dev->hw_info.nr_ctrl; i++) {
			off = SSD_READ_OT_REG0 + (i * SSD_CTRL_REG_ZONE_SZ);
			val = ssd_reg32_read(dev->ctrlp + off);
			if ((val >> 22) & 0x1) {
				*status = 1;
				goto out;
			}

			
			off = SSD_WRITE_OT_REG0 + (i * SSD_CTRL_REG_ZONE_SZ);
			val = ssd_reg32_read(dev->ctrlp + off);
			if ((val >> 22) & 0x1) {
				*status = 1;
				goto out;
			}
		}
	} else {
		*status = !!dev->ot_delay;
	}

out:
	return 0;
}

static void ssd_set_ot_protect(struct ssd_device *dev, int protect)
{
	uint32_t off;
	uint32_t val;
	int i;
	
	mutex_lock(&dev->fw_mutex);

	dev->ot_protect = !!protect;

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2_2) {
		for (i=0; i<dev->hw_info.nr_ctrl; i++) {
			off = SSD_READ_OT_REG0 + (i * SSD_CTRL_REG_ZONE_SZ);
			val = ssd_reg32_read(dev->ctrlp + off);
			if (dev->ot_protect) {
				val |= (1U << 21);
			} else {
				val &= ~(1U << 21);
			}
			ssd_reg32_write(dev->ctrlp + off, val);

			
			off = SSD_WRITE_OT_REG0 + (i * SSD_CTRL_REG_ZONE_SZ);
			val = ssd_reg32_read(dev->ctrlp + off);
			if (dev->ot_protect) {
				val |= (1U << 21);
			} else {
				val &= ~(1U << 21);
			}
			ssd_reg32_write(dev->ctrlp + off, val);
		}
	}

	mutex_unlock(&dev->fw_mutex);
}

static int ssd_init_ot_protect(struct ssd_device *dev)
{
	ssd_set_ot_protect(dev, ot_protect);

#ifdef SSD_OT_PROTECT
	ssd_check_temperature(dev, SSD_OT_TEMP);
#endif

	return 0;
}

/* log */
static int ssd_read_log(struct ssd_device *dev, int ctrl_idx, void *buf, int *nr_log)
{
	struct ssd_log_op_msg *msg;
	struct ssd_log_msg *lmsg;
	dma_addr_t buf_dma;
	size_t length = dev->hw_info.log_sz;
	int ret = 0;

	if (ctrl_idx >= dev->hw_info.nr_ctrl) {
		return -EINVAL;
	}

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map read DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	msg = (struct ssd_log_op_msg *)ssd_get_dmsg(dev);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		lmsg = (struct ssd_log_msg *)msg;
		lmsg->fun = SSD_FUNC_READ_LOG;
		lmsg->ctrl_idx = ctrl_idx;
		lmsg->buf = buf_dma;
	} else {
		msg->fun = SSD_FUNC_READ_LOG;
		msg->ctrl_idx = ctrl_idx;
		msg->buf = buf_dma;
	}

	ret = ssd_do_request(dev, READ, msg, nr_log);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_FROMDEVICE);

out_dma_mapping:
	 return ret;
}

#define SSD_LOG_PRINT_BUF_SZ	256
static int ssd_parse_log(struct ssd_device *dev, struct ssd_log *log, int print)
{
	struct ssd_log_desc *log_desc = ssd_log_desc;
	struct ssd_log_entry *le;
	char *sn = NULL;
	char print_buf[SSD_LOG_PRINT_BUF_SZ];
	int print_len;

	le = &log->le;

	/* find desc */
	while (log_desc->event != SSD_UNKNOWN_EVENT) {
		if (log_desc->event == le->event) {
			break;
		}
		log_desc++;
	}

	if (!print) {
		goto out;
	}

	if (log_desc->level < log_level) {
		goto out;
	}

	/* parse */
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		sn = dev->label.sn;
	} else {
		sn = dev->labelv3.barcode;
	}

	print_len = snprintf(print_buf, SSD_LOG_PRINT_BUF_SZ, "%s (%s): <%#x>", dev->name, sn, le->event);

	if (log->ctrl_idx != SSD_LOG_SW_IDX) {
	 	print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " controller %d", log->ctrl_idx);
	}

	switch (log_desc->data) {
		case SSD_LOG_DATA_NONE:
			break;
		case SSD_LOG_DATA_LOC:
			if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
				print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " flash %d", le->data.loc.flash);
				if (log_desc->sblock) {
					print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " block %d", le->data.loc.block);
				}
				if (log_desc->spage) {
					print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " page %d", le->data.loc.page);
				}
			} else {
				print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " flash %d", le->data.loc1.flash);
				if (log_desc->sblock) {
					print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " block %d", le->data.loc1.block);
				}
				if (log_desc->spage) {
					print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " page %d", le->data.loc1.page);
				}
			}
			break;
		case SSD_LOG_DATA_HEX: 
			print_len += snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), " info %#x", le->data.val);
			break;
		default:
			break;
	}
	/*print_len += */snprintf((print_buf + print_len), (SSD_LOG_PRINT_BUF_SZ - print_len), ": %s", log_desc->desc);

	switch (log_desc->level) {
		case SSD_LOG_LEVEL_INFO:
			hio_info("%s\n", print_buf);
			break;
		case SSD_LOG_LEVEL_NOTICE:
			hio_note("%s\n", print_buf);
			break;
		case SSD_LOG_LEVEL_WARNING:
			hio_warn("%s\n", print_buf);
			break;
		case SSD_LOG_LEVEL_ERR:
			hio_err("%s\n", print_buf);
			//printk(KERN_ERR MODULE_NAME": some exception occurred, please check the data or refer to FAQ.");
			break;
		default:
			hio_warn("%s\n", print_buf);
			break;
	}

out:
	return log_desc->level;
}

static int ssd_bm_get_sfstatus(struct ssd_device *dev, uint16_t *status);
static int ssd_switch_wmode(struct ssd_device *dev, int wmode);


static int ssd_handle_event(struct ssd_device *dev, uint16_t event, int level)
{
	int ret = 0;

	switch (event) {
		case SSD_LOG_OVER_TEMP: {
#ifdef SSD_OT_PROTECT
			if (!test_and_set_bit(SSD_HWMON_TEMP(SSD_TEMP_CTRL), &dev->hwmon)) {
				if (dev->protocol_info.ver > SSD_PROTOCOL_V3 && dev->protocol_info.ver < SSD_PROTOCOL_V3_2_2) {
					hio_warn("%s: Over temperature, please check the fans.\n", dev->name);
					dev->ot_delay = SSD_OT_DELAY;
				}
			}
#endif
			break;
		}

		case SSD_LOG_NORMAL_TEMP: {
#ifdef SSD_OT_PROTECT
			/* need to check all controller's temperature */
			ssd_check_temperature(dev, SSD_OT_TEMP_HYST);
#endif
			break;
		}

		case SSD_LOG_BATTERY_FAULT: {
			uint16_t sfstatus;

			if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
				if (!ssd_bm_get_sfstatus(dev, &sfstatus)) {
					ssd_gen_swlog(dev, SSD_LOG_BM_SFSTATUS, sfstatus);
				}
			}

			if (!test_and_set_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
				ssd_switch_wmode(dev, dev->user_wmode); 
			}
			break;
		}

		case SSD_LOG_BATTERY_OK: {
			if (test_and_clear_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
				ssd_switch_wmode(dev, dev->user_wmode); 
			}
			break;
		}

		case SSD_LOG_BOARD_VOLT_FAULT: {
			ssd_mon_boardvolt(dev);
			break;
		}

		case SSD_LOG_CLEAR_LOG: {
			/* update smart */
			memset(&dev->smart.log_info, 0, sizeof(struct ssd_log_info));
			break;
		}

		case SSD_LOG_CAP_VOLT_FAULT: 
		case SSD_LOG_CAP_LEARN_FAULT: 
		case SSD_LOG_CAP_SHORT_CIRCUIT: {
			if (!test_and_set_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
				ssd_switch_wmode(dev, dev->user_wmode); 
			}
			break;
		}

		default:
			break;
	}

	/* ssd event call */
	if (dev->event_call) {
		dev->event_call(dev->gd, event, level);

		/* FIXME */
		if (SSD_LOG_CAP_VOLT_FAULT == event || SSD_LOG_CAP_LEARN_FAULT == event || SSD_LOG_CAP_SHORT_CIRCUIT == event) {
			dev->event_call(dev->gd, SSD_LOG_BATTERY_FAULT, level);
		}
	}

	return ret;
}

static int ssd_save_log(struct ssd_device *dev, struct ssd_log *log)
{
	uint32_t off, size;
	void *internal_log;
	int ret = 0;

	mutex_lock(&dev->internal_log_mutex);

	size = sizeof(struct ssd_log);
	off = dev->internal_log.nr_log * size;

	if (off == dev->rom_info.log_sz) {
		if (dev->internal_log.nr_log == dev->smart.log_info.nr_log) {
			hio_warn("%s: internal log is full\n", dev->name);
		}
		goto out;
	}

	internal_log = dev->internal_log.log + off;
	memcpy(internal_log, log, size);

	if (dev->protocol_info.ver > SSD_PROTOCOL_V3) {
		off += dev->rom_info.log_base;

		ret = ssd_spi_write(dev, log, off, size);
		if (ret) {
			goto out;
		}
	}

	dev->internal_log.nr_log++;

out:
	mutex_unlock(&dev->internal_log_mutex);
	return ret;
}

static int ssd_save_swlog(struct ssd_device *dev, uint16_t event, uint32_t data)
{
	struct ssd_log log;
	struct timeval tv;
	int level;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	memset(&log, 0, sizeof(struct ssd_log));

	do_gettimeofday(&tv);
	log.ctrl_idx = SSD_LOG_SW_IDX;
	log.time = tv.tv_sec;
	log.le.event = event;
	log.le.data.val = data;

	level = ssd_parse_log(dev, &log, 0);
	if (level >= SSD_LOG_LEVEL) {
		ret = ssd_save_log(dev, &log);
	}

	/* set alarm */
	if (SSD_LOG_LEVEL_ERR == level) {
		ssd_set_alarm(dev);
	}

	/* update smart */
	dev->smart.log_info.nr_log++;
	dev->smart.log_info.stat[level]++;

	/* handle event */
	ssd_handle_event(dev, event, level);

	return ret;
}

static int ssd_gen_swlog(struct ssd_device *dev, uint16_t event, uint32_t data)
{
	struct ssd_log_entry le;
	int ret;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	/* slave port ? */
	if (dev->slave) {
		return 0;
	}

	memset(&le, 0, sizeof(struct ssd_log_entry));
	le.event = event;
	le.data.val = data;

	ret = sfifo_put(&dev->log_fifo, &le);
	if (ret) {
		return ret;
	}

	if (test_bit(SSD_INIT_WORKQ, &dev->state)) {
		queue_work(dev->workq, &dev->log_work);
	}

	return 0;
}

static int ssd_do_swlog(struct ssd_device *dev)
{
	struct ssd_log_entry le;
	int ret = 0;

	memset(&le, 0, sizeof(struct ssd_log_entry));
	while (!sfifo_get(&dev->log_fifo, &le)) {
		ret = ssd_save_swlog(dev, le.event, le.data.val);
		if (ret) {
			break;
		}
	}

	return ret;
}

static int __ssd_clear_log(struct ssd_device *dev)
{
	uint32_t off, length;
	int ret;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	if (dev->internal_log.nr_log == 0) {
		return 0;
	}

	mutex_lock(&dev->internal_log_mutex);

	off = dev->rom_info.log_base;
	length = dev->rom_info.log_sz;

	ret = ssd_spi_erase(dev, off, length);
	if (ret) {
		hio_warn("%s: log erase: failed\n", dev->name);
		goto out;
	}

	dev->internal_log.nr_log = 0;

out:
	mutex_unlock(&dev->internal_log_mutex);
	return ret;
}

static int ssd_clear_log(struct ssd_device *dev)
{
	int ret;

	ret = __ssd_clear_log(dev);
	if(!ret) {
		ssd_gen_swlog(dev, SSD_LOG_CLEAR_LOG, 0);
	}

	return ret;
}

static int ssd_do_log(struct ssd_device *dev, int ctrl_idx, void *buf)
{
	struct ssd_log_entry *le;
	struct ssd_log log;
	struct timeval tv;
	int nr_log = 0;
	int level;
	int ret = 0;

	ret = ssd_read_log(dev, ctrl_idx, buf, &nr_log);
	if (ret) {
		return ret;
	}

	do_gettimeofday(&tv);

	log.time = tv.tv_sec;
	log.ctrl_idx = ctrl_idx;

	le = (ssd_log_entry_t *)buf;
	while (nr_log > 0) {
		memcpy(&log.le, le, sizeof(struct ssd_log_entry));

		level = ssd_parse_log(dev, &log, 1);
		if (level >= SSD_LOG_LEVEL) {
			ssd_save_log(dev, &log);
		}

		/* set alarm */
		if (SSD_LOG_LEVEL_ERR == level) {
			ssd_set_alarm(dev);
		}
		
		dev->smart.log_info.nr_log++;
		if (SSD_LOG_SEU_FAULT != le->event && SSD_LOG_SEU_FAULT1 != le->event) {
			dev->smart.log_info.stat[level]++;
		} else {
			/* SEU fault */

			/* log to the volatile log info */
			dev->log_info.nr_log++;
			dev->log_info.stat[level]++;

			/* do something */
			dev->reload_fw = 1;
			ssd_reg32_write(dev->ctrlp + SSD_RELOAD_FW_REG, SSD_RELOAD_FLAG);

			/*dev->readonly = 1;
			set_disk_ro(dev->gd, 1);
			hio_warn("%s: switched to read-only mode.\n", dev->name);*/
		}

		/* handle event */
		ssd_handle_event(dev, le->event, level);

		le++;
		nr_log--;
	}

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_log_worker(void *data)
{
	struct ssd_device *dev = (struct ssd_device *)data;
#else
static void ssd_log_worker(struct work_struct *work)
{
	struct ssd_device *dev = container_of(work, struct ssd_device, log_work);
#endif
	int i;
	int ret;

	if (!test_bit(SSD_LOG_ERR, &dev->state) && test_bit(SSD_ONLINE, &dev->state)) {
		/* alloc log buf */
		if (!dev->log_buf) {
			dev->log_buf = kmalloc(dev->hw_info.log_sz, GFP_KERNEL);
			if (!dev->log_buf) {
				hio_warn("%s: ssd_log_worker: no mem\n", dev->name);
				return;
			}
		}

		/* get log */
		if (test_and_clear_bit(SSD_LOG_HW, &dev->state)) {
			for (i=0; i<dev->hw_info.nr_ctrl; i++) {
				ret = ssd_do_log(dev, i, dev->log_buf);
				if (ret) {
					(void)test_and_set_bit(SSD_LOG_ERR, &dev->state);
					hio_warn("%s: do log fail\n", dev->name);
				}
			}
		}
	}

	ret = ssd_do_swlog(dev);
	if (ret) {
		hio_warn("%s: do swlog fail\n", dev->name);
	}
}

static void ssd_cleanup_log(struct ssd_device *dev)
{
	if (dev->log_buf) {
		kfree(dev->log_buf);
		dev->log_buf = NULL;
	}

	sfifo_free(&dev->log_fifo);

	if (dev->internal_log.log) {
		vfree(dev->internal_log.log);
		dev->internal_log.log = NULL;
	}
}

static int ssd_init_log(struct ssd_device *dev)
{
	struct ssd_log *log;
	uint32_t off, size;
	uint32_t len = 0;
	int ret = 0;

	mutex_init(&dev->internal_log_mutex);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	INIT_WORK(&dev->log_work, ssd_log_worker, dev);
#else
	INIT_WORK(&dev->log_work, ssd_log_worker);
#endif

	off = dev->rom_info.log_base;
	size = dev->rom_info.log_sz;

	dev->internal_log.log = vmalloc(size);
	if (!dev->internal_log.log) {
		ret = -ENOMEM;
		goto out_alloc_log;
	}

	ret = sfifo_alloc(&dev->log_fifo, SSD_LOG_FIFO_SZ, sizeof(struct ssd_log_entry));
	if (ret < 0) {
		goto out_alloc_log_fifo;
	}

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	log = (struct ssd_log *)dev->internal_log.log;
	while (len < size) {
		ret = ssd_spi_read(dev, log, off, sizeof(struct ssd_log));
		if (ret) {
			goto out_read_log;
		}

		if (log->ctrl_idx == 0xff) {
			break;
		}

		dev->internal_log.nr_log++;
		log++;
		len += sizeof(struct ssd_log);
		off += sizeof(struct ssd_log);
	}

	return 0;

out_read_log:
	sfifo_free(&dev->log_fifo);
out_alloc_log_fifo:
	vfree(dev->internal_log.log);
	dev->internal_log.log = NULL;
	dev->internal_log.nr_log = 0;
out_alloc_log:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

/* work queue */
static void ssd_stop_workq(struct ssd_device *dev)
{
	test_and_clear_bit(SSD_INIT_WORKQ, &dev->state);
	flush_workqueue(dev->workq);
}

static void ssd_start_workq(struct ssd_device *dev)
{
	(void)test_and_set_bit(SSD_INIT_WORKQ, &dev->state);

	/* log ? */
	queue_work(dev->workq, &dev->log_work);
}

static void ssd_cleanup_workq(struct ssd_device *dev)
{
	flush_workqueue(dev->workq);
	destroy_workqueue(dev->workq);
	dev->workq = NULL;
}

static int ssd_init_workq(struct ssd_device *dev)
{
	int ret = 0;
	
	dev->workq = create_singlethread_workqueue(dev->name);
	if (!dev->workq) {
		ret = -ESRCH;
		goto out;
	}

out:
	return ret;
}

/* rom */
static int ssd_init_rom_info(struct ssd_device *dev)
{
	uint32_t val;

	mutex_init(&dev->spi_mutex);
	mutex_init(&dev->i2c_mutex);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		/* fix bug: read data to clear status */
		(void)ssd_reg32_read(dev->ctrlp + SSD_SPI_REG_RDATA);

		dev->rom_info.size = SSD_ROM_SIZE;
		dev->rom_info.block_size = SSD_ROM_BLK_SIZE;
		dev->rom_info.page_size = SSD_ROM_PAGE_SIZE;

		dev->rom_info.bridge_fw_base = SSD_ROM_BRIDGE_FW_BASE;
		dev->rom_info.bridge_fw_sz = SSD_ROM_BRIDGE_FW_SIZE;
		dev->rom_info.nr_bridge_fw = SSD_ROM_NR_BRIDGE_FW;

		dev->rom_info.ctrl_fw_base = SSD_ROM_CTRL_FW_BASE;
		dev->rom_info.ctrl_fw_sz = SSD_ROM_CTRL_FW_SIZE;
		dev->rom_info.nr_ctrl_fw = SSD_ROM_NR_CTRL_FW;

		dev->rom_info.log_sz = SSD_ROM_LOG_SZ;

		dev->rom_info.vp_base = SSD_ROM_VP_BASE;
		dev->rom_info.label_base = SSD_ROM_LABEL_BASE;
	} else if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_INFO_REG);
		dev->rom_info.size = 0x100000 * (1U << (val & 0xFF));
		dev->rom_info.block_size = 0x10000 * (1U << ((val>>8) & 0xFF));
		dev->rom_info.page_size = (val>>16) & 0xFFFF;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_BRIDGE_FW_INFO_REG);
		dev->rom_info.bridge_fw_base = dev->rom_info.block_size * (val & 0xFFFF);
		dev->rom_info.bridge_fw_sz = dev->rom_info.block_size * ((val>>16) & 0x3FFF);
		dev->rom_info.nr_bridge_fw = ((val >> 30) & 0x3) + 1;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_CTRL_FW_INFO_REG);
		dev->rom_info.ctrl_fw_base = dev->rom_info.block_size * (val & 0xFFFF);
		dev->rom_info.ctrl_fw_sz = dev->rom_info.block_size * ((val>>16) & 0x3FFF);
		dev->rom_info.nr_ctrl_fw = ((val >> 30) & 0x3) + 1;

		dev->rom_info.bm_fw_base = dev->rom_info.ctrl_fw_base + (dev->rom_info.nr_ctrl_fw * dev->rom_info.ctrl_fw_sz);
		dev->rom_info.bm_fw_sz = SSD_PV3_ROM_BM_FW_SZ;
		dev->rom_info.nr_bm_fw = SSD_PV3_ROM_NR_BM_FW;

		dev->rom_info.log_base = dev->rom_info.bm_fw_base + (dev->rom_info.nr_bm_fw * dev->rom_info.bm_fw_sz);
		dev->rom_info.log_sz = SSD_ROM_LOG_SZ;

		dev->rom_info.smart_base = dev->rom_info.log_base + dev->rom_info.log_sz;
		dev->rom_info.smart_sz = SSD_PV3_ROM_SMART_SZ;
		dev->rom_info.nr_smart = SSD_PV3_ROM_NR_SMART;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_VP_INFO_REG);
		dev->rom_info.vp_base = dev->rom_info.block_size * val;
		dev->rom_info.label_base = dev->rom_info.vp_base + dev->rom_info.block_size;
		if (dev->rom_info.label_base >= dev->rom_info.size) {
			dev->rom_info.label_base = dev->rom_info.vp_base - dev->rom_info.block_size;
		}
	} else {
		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_INFO_REG);
		dev->rom_info.size = 0x100000 * (1U << (val & 0xFF));
		dev->rom_info.block_size = 0x10000 * (1U << ((val>>8) & 0xFF));
		dev->rom_info.page_size = (val>>16) & 0xFFFF;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_BRIDGE_FW_INFO_REG);
		dev->rom_info.bridge_fw_base = dev->rom_info.block_size * (val & 0xFFFF);
		dev->rom_info.bridge_fw_sz = dev->rom_info.block_size * ((val>>16) & 0x3FFF);
		dev->rom_info.nr_bridge_fw = ((val >> 30) & 0x3) + 1;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_CTRL_FW_INFO_REG);
		dev->rom_info.ctrl_fw_base = dev->rom_info.block_size * (val & 0xFFFF);
		dev->rom_info.ctrl_fw_sz = dev->rom_info.block_size * ((val>>16) & 0x3FFF);
		dev->rom_info.nr_ctrl_fw = ((val >> 30) & 0x3) + 1;

		val = ssd_reg32_read(dev->ctrlp + SSD_ROM_VP_INFO_REG);
		dev->rom_info.vp_base = dev->rom_info.block_size * val;
		dev->rom_info.label_base = dev->rom_info.vp_base - SSD_PV3_2_ROM_SEC_SZ;

		dev->rom_info.nr_smart = SSD_PV3_ROM_NR_SMART;
		dev->rom_info.smart_sz = SSD_PV3_2_ROM_SEC_SZ;
		dev->rom_info.smart_base = dev->rom_info.label_base - (dev->rom_info.smart_sz * dev->rom_info.nr_smart);
		if (dev->rom_info.smart_sz > dev->rom_info.block_size) {
			dev->rom_info.smart_sz = dev->rom_info.block_size;
		}

		dev->rom_info.log_sz = SSD_PV3_2_ROM_LOG_SZ;
		dev->rom_info.log_base = dev->rom_info.smart_base - dev->rom_info.log_sz;
	}

	return ssd_init_spi(dev);
}

/* smart */
static int ssd_update_smart(struct ssd_device *dev, struct ssd_smart *smart)
{
	struct timeval tv;
	uint64_t run_time;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
	struct hd_struct *part;
	int cpu;
#endif
	int i, j;
	int ret = 0;

	if (!test_bit(SSD_INIT_BD, &dev->state)) {
		return 0;
	}

	do_gettimeofday(&tv);
	if ((uint64_t)tv.tv_sec < dev->uptime) {
		run_time = 0;
	} else {
		run_time = tv.tv_sec - dev->uptime;
	}

	/* avoid frequently update */
	if (run_time >= 60) {
		ret = 1;
	}

	/* io stat */
	smart->io_stat.run_time += run_time;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27))
	cpu = part_stat_lock();
	part = &dev->gd->part0;
	part_round_stats(cpu, part);
	part_stat_unlock();

	smart->io_stat.nr_read += part_stat_read(part, ios[READ]);
	smart->io_stat.nr_write += part_stat_read(part, ios[WRITE]);
	smart->io_stat.rsectors += part_stat_read(part, sectors[READ]);
	smart->io_stat.wsectors += part_stat_read(part, sectors[WRITE]);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,14))
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();

	smart->io_stat.nr_read += disk_stat_read(dev->gd, ios[READ]);
	smart->io_stat.nr_write += disk_stat_read(dev->gd, ios[WRITE]);
	smart->io_stat.rsectors += disk_stat_read(dev->gd, sectors[READ]);
	smart->io_stat.wsectors += disk_stat_read(dev->gd, sectors[WRITE]);
#else
	preempt_disable();
	disk_round_stats(dev->gd);
	preempt_enable();

	smart->io_stat.nr_read += disk_stat_read(dev->gd, reads);
	smart->io_stat.nr_write += disk_stat_read(dev->gd, writes);
	smart->io_stat.rsectors += disk_stat_read(dev->gd, read_sectors);
	smart->io_stat.wsectors += disk_stat_read(dev->gd, write_sectors);
#endif

	smart->io_stat.nr_to += atomic_read(&dev->tocnt);

	for (i=0; i<dev->nr_queue; i++) {
		smart->io_stat.nr_rwerr += dev->queue[i].io_stat.nr_rwerr;
		smart->io_stat.nr_ioerr += dev->queue[i].io_stat.nr_ioerr;
	}

	for (i=0; i<dev->nr_queue; i++) {
		for (j=0; j<SSD_ECC_MAX_FLIP; j++) {
			smart->ecc_info.bitflip[j] += dev->queue[i].ecc_info.bitflip[j];
		}
	}

	//dev->uptime = tv.tv_sec;

	return ret;
}

static int ssd_clear_smart(struct ssd_device *dev)
{
	struct timeval tv;
	uint64_t sversion;
	uint32_t off, length;
	int i;
	int ret;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	/* clear smart */
	off = dev->rom_info.smart_base;
	length = dev->rom_info.smart_sz * dev->rom_info.nr_smart;

	ret = ssd_spi_erase(dev, off, length);
	if (ret) {
		hio_warn("%s: info erase: failed\n", dev->name);
		goto out;
	}

	sversion = dev->smart.version;

	memset(&dev->smart, 0, sizeof(struct ssd_smart));
	dev->smart.version = sversion + 1;
	dev->smart.magic = SSD_SMART_MAGIC;

	/* clear all tmp acc */
	for (i=0; i<dev->nr_queue; i++) {
		memset(&(dev->queue[i].io_stat), 0, sizeof(struct ssd_io_stat));
		memset(&(dev->queue[i].ecc_info), 0, sizeof(struct ssd_ecc_info));
	}

	atomic_set(&dev->tocnt, 0);

	/* clear tmp log info */
	memset(&dev->log_info, 0, sizeof(struct ssd_log_info));

	do_gettimeofday(&tv);
	dev->uptime = tv.tv_sec;

	/* clear alarm ? */
	//ssd_clear_alarm(dev);
out:
	return ret;
}

static int ssd_save_smart(struct ssd_device *dev)
{
	uint32_t off, size;
	int i;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	if (!ssd_update_smart(dev, &dev->smart)) {
		return 0;
	}

	dev->smart.version++;

	for (i=0; i<dev->rom_info.nr_smart; i++) {
		off = dev->rom_info.smart_base + (dev->rom_info.smart_sz * i);
		size = dev->rom_info.smart_sz;

		ret = ssd_spi_erase(dev, off, size);
		if (ret) {
			hio_warn("%s: info erase failed\n", dev->name);
			goto out;
		}

		size = sizeof(struct ssd_smart);

		ret = ssd_spi_write(dev, &dev->smart, off, size);
		if (ret) {
			hio_warn("%s: info write failed\n", dev->name);
			goto out;
		}

		//xx
	}

out:
	return ret;
}

static int ssd_init_smart(struct ssd_device *dev)
{
	struct ssd_smart *smart;
	struct timeval tv;
	uint32_t off, size;
	int i;
	int ret = 0;

	do_gettimeofday(&tv);
	dev->uptime = tv.tv_sec;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	smart = kmalloc(sizeof(struct ssd_smart) * SSD_ROM_NR_SMART_MAX, GFP_KERNEL);
	if (!smart) {
		ret = -ENOMEM;
		goto out_nomem;
	}

	memset(&dev->smart, 0, sizeof(struct ssd_smart));

	/* read smart */
	for (i=0; i<dev->rom_info.nr_smart; i++) {
		memset(&smart[i], 0, sizeof(struct ssd_smart));

		off = dev->rom_info.smart_base + (dev->rom_info.smart_sz * i);
		size = sizeof(struct ssd_smart);

		ret = ssd_spi_read(dev, &smart[i], off, size);
		if (ret) {
			hio_warn("%s: info read failed\n", dev->name);
			goto out;
		}

		if (smart[i].magic != SSD_SMART_MAGIC) {
			smart[i].magic = 0;
			smart[i].version = 0;
			continue;
		}

		if (smart[i].version > dev->smart.version) {
			memcpy(&dev->smart, &smart[i], sizeof(struct ssd_smart));
		}
	}

	if (dev->smart.magic != SSD_SMART_MAGIC) {
		/* first time power up */
		dev->smart.magic = SSD_SMART_MAGIC;
		dev->smart.version = 1;
	}

	/* check log info */
	{
		struct ssd_log_info log_info;
		struct ssd_log *log = (struct ssd_log *)dev->internal_log.log;

		memset(&log_info, 0, sizeof(struct ssd_log_info));

		while (log_info.nr_log < dev->internal_log.nr_log) {
			/* skip the volatile log info */
			if (SSD_LOG_SEU_FAULT != log->le.event && SSD_LOG_SEU_FAULT1 != log->le.event) {
				log_info.stat[ssd_parse_log(dev, log, 0)]++;
			}

			log_info.nr_log++;
			log++;
		}

		/* check */
		for (i=(SSD_LOG_NR_LEVEL-1); i>=0; i--) {
			if (log_info.stat[i] > dev->smart.log_info.stat[i]) {
				/* unclean */
				memcpy(&dev->smart.log_info, &log_info, sizeof(struct ssd_log_info));
				dev->smart.version++;
				break;
			}
		}
	}

	for (i=0; i<dev->rom_info.nr_smart; i++) {
		if (smart[i].magic == SSD_SMART_MAGIC && smart[i].version == dev->smart.version) {
			continue;
		}

		off = dev->rom_info.smart_base + (dev->rom_info.smart_sz * i);
		size = dev->rom_info.smart_sz;

		ret = ssd_spi_erase(dev, off, size);
		if (ret) {
			hio_warn("%s: info erase failed\n", dev->name);
			goto out;
		}

		size = sizeof(struct ssd_smart);
		ret = ssd_spi_write(dev, &dev->smart, off, size);
		if (ret) {
			hio_warn("%s: info write failed\n", dev->name);
			goto out;
		}

		//xx
	}

	/* sync smart with alarm led */
	if (dev->smart.io_stat.nr_to || dev->smart.io_stat.nr_rwerr || dev->smart.log_info.stat[SSD_LOG_LEVEL_ERR]) {
		hio_warn("%s: some fault found in the history info\n", dev->name);
		ssd_set_alarm(dev);
	}

out:
	kfree(smart);
out_nomem:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

/* bm */
static int __ssd_bm_get_version(struct ssd_device *dev, uint16_t *ver)
{
	struct ssd_bm_manufacturer_data bm_md = {0};
	uint16_t sc_id = SSD_BM_SYSTEM_DATA_SUBCLASS_ID;
	uint8_t cmd;
	int ret = 0;

	if (!dev || !ver) {
		return -EINVAL;
	}

	mutex_lock(&dev->bm_mutex);

	cmd = SSD_BM_DATA_FLASH_SUBCLASS_ID;
	ret = ssd_smbus_write_word(dev, SSD_BM_SLAVE_ADDRESS, cmd, (uint8_t *)&sc_id);
	if (ret) {
		goto out;
	}

	cmd = SSD_BM_DATA_FLASH_SUBCLASS_ID_PAGE1;
	ret = ssd_smbus_read_block(dev, SSD_BM_SLAVE_ADDRESS, cmd, sizeof(struct ssd_bm_manufacturer_data), (uint8_t *)&bm_md);
	if (ret) {
		goto out;
	}

	if (bm_md.firmware_ver & 0xF000) {
		ret = -EIO;
		goto out;
	}

	*ver = bm_md.firmware_ver;

out:
	mutex_unlock(&dev->bm_mutex);
	return ret;
}

static int ssd_bm_get_version(struct ssd_device *dev, uint16_t *ver)
{
	uint16_t tmp = 0;
	int i = SSD_BM_RETRY_MAX;
	int ret = 0;

	while (i-- > 0) {
		ret = __ssd_bm_get_version(dev, &tmp);
		if (!ret) {
			break;
		}
	}
	if (ret) {
		return ret;
	}

	*ver = tmp;

	return 0;
}

static int __ssd_bm_nr_cap(struct ssd_device *dev, int *nr_cap)
{
	struct ssd_bm_configuration_registers bm_cr;
	uint16_t sc_id = SSD_BM_CONFIGURATION_REGISTERS_ID;
	uint8_t cmd;
	int ret;

	mutex_lock(&dev->bm_mutex);

	cmd = SSD_BM_DATA_FLASH_SUBCLASS_ID;
	ret = ssd_smbus_write_word(dev, SSD_BM_SLAVE_ADDRESS, cmd, (uint8_t *)&sc_id);
	if (ret) {
		goto out;
	}

	cmd = SSD_BM_DATA_FLASH_SUBCLASS_ID_PAGE1;
	ret = ssd_smbus_read_block(dev, SSD_BM_SLAVE_ADDRESS, cmd, sizeof(struct ssd_bm_configuration_registers), (uint8_t *)&bm_cr);
	if (ret) {
		goto out;
	}

	if (bm_cr.operation_cfg.cc == 0 || bm_cr.operation_cfg.cc > 4) {
		ret = -EIO;
		goto out;
	}

	*nr_cap = bm_cr.operation_cfg.cc + 1;

out:
	mutex_unlock(&dev->bm_mutex);
	return ret;
}

static int ssd_bm_nr_cap(struct ssd_device *dev, int *nr_cap)
{
	int tmp = 0;
	int i = SSD_BM_RETRY_MAX;
	int ret = 0;

	while (i-- > 0) {
		ret = __ssd_bm_nr_cap(dev, &tmp);
		if (!ret) {
			break;
		}
	}
	if (ret) {
		return ret;
	}

	*nr_cap = tmp;

	return 0;
}

static int ssd_bm_enter_cap_learning(struct ssd_device *dev)
{
	uint16_t buf = SSD_BM_ENTER_CAP_LEARNING;
	uint8_t cmd = SSD_BM_MANUFACTURERACCESS;
	int ret;

	ret = ssd_smbus_write_word(dev, SSD_BM_SLAVE_ADDRESS, cmd, (uint8_t *)&buf);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

static int ssd_bm_get_sfstatus(struct ssd_device *dev, uint16_t *status)
{
	uint16_t val = 0;
	uint8_t cmd = SSD_BM_SAFETYSTATUS;
	int ret;

	ret = ssd_smbus_read_word(dev, SSD_BM_SLAVE_ADDRESS, cmd, (uint8_t *)&val);
	if (ret) {
		goto out;
	}

	*status = val;
out:
	return ret;
}

static int ssd_bm_get_opstatus(struct ssd_device *dev, uint16_t *status)
{
	uint16_t val = 0;
	uint8_t cmd = SSD_BM_OPERATIONSTATUS;
	int ret;

	ret = ssd_smbus_read_word(dev, SSD_BM_SLAVE_ADDRESS, cmd, (uint8_t *)&val);
	if (ret) {
		goto out;
	}

	*status = val;
out:
	return ret;
}
 
static int ssd_get_bmstruct(struct ssd_device *dev, struct ssd_bm *bm_status_out)
{
	struct sbs_cmd *bm_sbs = ssd_bm_sbs;
	struct ssd_bm bm_status;
	uint8_t buf[2] = {0, };
	uint16_t val = 0;
	uint16_t cval;
	int ret = 0;

	memset(&bm_status, 0, sizeof(struct ssd_bm));

	while (bm_sbs->desc != NULL) {
		switch (bm_sbs->size) {
			case SBS_SIZE_BYTE:
				ret = ssd_smbus_read_byte(dev, SSD_BM_SLAVE_ADDRESS, bm_sbs->cmd, buf);
				if (ret) {
					//printf("Error: smbus read byte %#x\n", bm_sbs->cmd);
					goto out;
				}
				val = buf[0];
				break;
			case SBS_SIZE_WORD:
				ret = ssd_smbus_read_word(dev, SSD_BM_SLAVE_ADDRESS, bm_sbs->cmd, (uint8_t *)&val);
				if (ret) {
					//printf("Error: smbus read word %#x\n", bm_sbs->cmd);
					goto out;
				}
				//val = *(uint16_t *)buf;
				break;
			default:
				ret = -1;
				goto out;
				break;
		}

		switch (bm_sbs->unit) {
			case SBS_UNIT_VALUE:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val & bm_sbs->mask;
				break;
			case SBS_UNIT_TEMPERATURE:
				cval = (uint16_t)(val - 2731) / 10;
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = cval;
				break;
			case SBS_UNIT_VOLTAGE:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val;
				break;
			case SBS_UNIT_CURRENT:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val;
				break;
			case SBS_UNIT_ESR:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val;
				break;
			case SBS_UNIT_PERCENT:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val;
				break;
			case SBS_UNIT_CAPACITANCE:
				*(uint16_t *)bm_var(&bm_status, bm_sbs->off) = val;
				break;
			default:
				ret = -1;
				goto out;
				break;
		}

		bm_sbs++;
	}

	memcpy(bm_status_out, &bm_status, sizeof(struct ssd_bm));

out:
	return ret;
}

static int __ssd_bm_status(struct ssd_device *dev, int *status)
{
	struct ssd_bm bm_status = {0};
	int nr_cap = 0;
	int i;
	int ret = 0;

	ret = ssd_get_bmstruct(dev, &bm_status);
	if (ret) {
		goto out;
	}

	/* capacitor voltage */
	ret = ssd_bm_nr_cap(dev, &nr_cap);
	if (ret) {
		goto out;
	}

	for (i=0; i<nr_cap; i++) {
		if (bm_status.cap_volt[i] < SSD_BM_CAP_VOLT_MIN) {
			*status = SSD_BMSTATUS_WARNING;
			goto out;
		}
	}

	/* Safety Status */
	if (bm_status.sf_status) {
		*status = SSD_BMSTATUS_WARNING;
		goto out;
	}

	/* charge status */
	if (!((bm_status.op_status >> 12) & 0x1)) {
		*status = SSD_BMSTATUS_CHARGING;
	}else{
		*status = SSD_BMSTATUS_OK;
	}

out:
	return ret;
}

static void ssd_set_flush_timeout(struct ssd_device *dev, int mode);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_bm_worker(void *data)
{
	struct ssd_device *dev = (struct ssd_device *)data;
#else
static void ssd_bm_worker(struct work_struct *work)
{
	struct ssd_device *dev = container_of(work, struct ssd_device, bm_work);
#endif

	uint16_t opstatus;
	int ret = 0;

	if (mode != SSD_DRV_MODE_STANDARD) {
		return;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_1) {
		return;
	}

	if (dev->hw_info_ext.plp_type != SSD_PLP_SCAP) {
		return;
	}

	ret = ssd_bm_get_opstatus(dev, &opstatus);
	if (ret) {
		hio_warn("%s: get bm operationstatus failed\n", dev->name);
		return;
	}

	/* need cap learning ? */
	if (!(opstatus & 0xF0)) {
		ret = ssd_bm_enter_cap_learning(dev);
		if (ret) {
			hio_warn("%s: enter capacitance learning failed\n", dev->name);
			return;
		}
	}
}

static void ssd_bm_routine_start(void *data)
{
	struct ssd_device *dev;

	if (!data) {
		return;
	}
	dev = data;

	if (test_bit(SSD_INIT_WORKQ, &dev->state)) {
		if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
			queue_work(dev->workq, &dev->bm_work);
		} else {
			queue_work(dev->workq, &dev->capmon_work);
		}
	}
}

/* CAP */
static int ssd_do_cap_learn(struct ssd_device *dev, uint32_t *cap)
{
	uint32_t u1, u2, t;
	uint16_t val = 0;
	int wait = 0;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		*cap = 0;
		return 0;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		*cap = 0;
		return 0;
	}

	/* make sure the lm80 voltage value is updated */
	msleep(SSD_LM80_CONV_INTERVAL);

	/* check if full charged */
	wait = 0;
	for (;;) {
		ret = ssd_smbus_read_word(dev, SSD_SENSOR_LM80_SADDRESS, SSD_PL_CAP_U1, (uint8_t *)&val);
		if (ret) {
			if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
				ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
			}
			goto out;
		}
		u1 = SSD_LM80_CONVERT_VOLT(u16_swap(val));
		if (SSD_PL_CAP_VOLT(u1) >= SSD_PL_CAP_VOLT_FULL) {
			break;
		}

		wait++;
		if (wait > SSD_PL_CAP_CHARGE_MAX_WAIT) {
			ret = -ETIMEDOUT;
			goto out;
		}
		msleep(SSD_PL_CAP_CHARGE_WAIT);
	}

	ret = ssd_smbus_read_word(dev, SSD_SENSOR_LM80_SADDRESS, SSD_PL_CAP_U2, (uint8_t *)&val);
	if (ret) {
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
		}
		goto out;
	}
	u2 = SSD_LM80_CONVERT_VOLT(u16_swap(val));

	if (u1 == u2) {
		ret = -EINVAL;
		goto out;
	}

	/* enter cap learn */
	ssd_reg32_write(dev->ctrlp + SSD_PL_CAP_LEARN_REG, 0x1);
	
	wait = 0;
	for (;;) {
		msleep(SSD_PL_CAP_LEARN_WAIT);

		t = ssd_reg32_read(dev->ctrlp + SSD_PL_CAP_LEARN_REG);
		if (!((t >> 1) & 0x1)) {
			break;
		}

		wait++;
		if (wait > SSD_PL_CAP_LEARN_MAX_WAIT) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	if ((t >> 4) & 0x1) {
		ret = -ETIMEDOUT;
		goto out;
	}

	t = (t >> 8);
	if (0 == t) {
		ret = -EINVAL;
		goto out;
	}

	*cap = SSD_PL_CAP_LEARN(u1, u2, t);

out:
	return ret;
}

static int ssd_cap_learn(struct ssd_device *dev, uint32_t *cap)
{
	int ret = 0;

	if (!dev || !cap) {
		return -EINVAL;
	}

	mutex_lock(&dev->bm_mutex);

	ssd_stop_workq(dev);

	ret = ssd_do_cap_learn(dev, cap);
	if (ret) {
		ssd_gen_swlog(dev, SSD_LOG_CAP_LEARN_FAULT, 0);
		goto out;
	}

	ssd_gen_swlog(dev, SSD_LOG_CAP_STATUS, *cap);

out:
	ssd_start_workq(dev);
	mutex_unlock(&dev->bm_mutex);

	return ret;
}

static int ssd_check_pl_cap(struct ssd_device *dev)
{
	uint32_t u1;
	uint16_t val = 0;
	uint8_t low = 0;
	int wait = 0;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		return 0;
	}

	/* cap ready ? */
	wait = 0;
	for (;;) {
		ret = ssd_smbus_read_word(dev, SSD_SENSOR_LM80_SADDRESS, SSD_PL_CAP_U1, (uint8_t *)&val);
		if (ret) {
			if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
				ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
			}
			goto out;
		}
		u1 = SSD_LM80_CONVERT_VOLT(u16_swap(val));
		if (SSD_PL_CAP_VOLT(u1) >= SSD_PL_CAP_VOLT_READY) {
			break;
		}

		wait++;
		if (wait > SSD_PL_CAP_CHARGE_MAX_WAIT) {
			ret = -ETIMEDOUT;
			ssd_gen_swlog(dev, SSD_LOG_CAP_VOLT_FAULT, SSD_PL_CAP_VOLT(u1));
			goto out;
		}
		msleep(SSD_PL_CAP_CHARGE_WAIT);
	}

	low = ssd_lm80_limit[SSD_LM80_IN_CAP].low;
	ret = ssd_smbus_write_byte(dev, SSD_SENSOR_LM80_SADDRESS, SSD_LM80_REG_IN_MIN(SSD_LM80_IN_CAP), &low);
	if (ret) {
		goto out;
	}

	/* enable cap INx */
	ret = ssd_lm80_enable_in(dev, SSD_SENSOR_LM80_SADDRESS, SSD_LM80_IN_CAP);
	if (ret) {
		if (!test_and_set_bit(SSD_HWMON_SENSOR(SSD_SENSOR_LM80), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_SENSOR_FAULT, SSD_SENSOR_LM80_SADDRESS);
		}
		goto out;
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

static int ssd_check_pl_cap_fast(struct ssd_device *dev)
{
	uint32_t u1;
	uint16_t val = 0;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		return 0;
	}

	/* cap ready ? */
	ret = ssd_smbus_read_word(dev, SSD_SENSOR_LM80_SADDRESS, SSD_PL_CAP_U1, (uint8_t *)&val);
	if (ret) {
		goto out;
	}
	u1 = SSD_LM80_CONVERT_VOLT(u16_swap(val));
	if (SSD_PL_CAP_VOLT(u1) < SSD_PL_CAP_VOLT_READY) {
		ret = 1;
	}

out:
	return ret;
}

static int ssd_init_pl_cap(struct ssd_device *dev)
{
	int ret = 0;

	/* set here: user write mode */
	dev->user_wmode = wmode;
	
	mutex_init(&dev->bm_mutex);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		uint32_t val;
		val = ssd_reg32_read(dev->ctrlp + SSD_BM_FAULT_REG);
		if ((val >> 1) & 0x1) {
			(void)test_and_set_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon);
		}
	} else {
		ret = ssd_check_pl_cap(dev);
		if (ret) {
			(void)test_and_set_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon);
		}
	}

	return 0;
}

/* label */
static void __end_str(char *str, int len)
{
	int i;

	for(i=0; i<len; i++) {
		if (*(str+i) == '\0')
			return;
	}
	*str = '\0';
}

static int ssd_init_label(struct ssd_device *dev)
{
	uint32_t off;
	uint32_t size;
	int ret;

	/* label location */
	off = dev->rom_info.label_base;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		size = sizeof(struct ssd_label);

		/* read label */
		ret = ssd_spi_read(dev, &dev->label, off, size);
		if (ret) {
			memset(&dev->label, 0, size);
			goto out;
		}

		__end_str(dev->label.date, SSD_LABEL_FIELD_SZ);
		__end_str(dev->label.sn, SSD_LABEL_FIELD_SZ);
		__end_str(dev->label.part, SSD_LABEL_FIELD_SZ);
		__end_str(dev->label.desc, SSD_LABEL_FIELD_SZ);
		__end_str(dev->label.other, SSD_LABEL_FIELD_SZ);
		__end_str(dev->label.maf, SSD_LABEL_FIELD_SZ);
	} else {
		size = sizeof(struct ssd_labelv3);

		/* read label */
		ret = ssd_spi_read(dev, &dev->labelv3, off, size);
		if (ret) {
			memset(&dev->labelv3, 0, size);
			goto out;
		}

		__end_str(dev->labelv3.boardtype, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.barcode, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.item, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.description, SSD_LABEL_DESC_SZ);
		__end_str(dev->labelv3.manufactured, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.vendorname, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.issuenumber, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.cleicode, SSD_LABEL_FIELD_SZ);
		__end_str(dev->labelv3.bom, SSD_LABEL_FIELD_SZ);
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

int ssd_get_label(struct block_device *bdev, struct ssd_label *label)
{
	struct ssd_device *dev;

	if (!bdev || !label || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		memset(label, 0, sizeof(struct ssd_label));
		memcpy(label->date, dev->labelv3.manufactured, SSD_LABEL_FIELD_SZ);
		memcpy(label->sn, dev->labelv3.barcode, SSD_LABEL_FIELD_SZ);
		memcpy(label->desc, dev->labelv3.boardtype, SSD_LABEL_FIELD_SZ);
		memcpy(label->maf, dev->labelv3.vendorname, SSD_LABEL_FIELD_SZ);
	} else {
		memcpy(label, &dev->label, sizeof(struct ssd_label));
	}

	return 0;
}

static int __ssd_get_version(struct ssd_device *dev, struct ssd_version_info *ver)
{
	uint16_t bm_ver = 0;
	int ret = 0;

	if (dev->protocol_info.ver > SSD_PROTOCOL_V3 && dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		ret = ssd_bm_get_version(dev, &bm_ver);
		if(ret){
			goto out;
		}
	}

	ver->bridge_ver = dev->hw_info.bridge_ver;
	ver->ctrl_ver = dev->hw_info.ctrl_ver;
	ver->bm_ver = bm_ver;
	ver->pcb_ver = dev->hw_info.pcb_ver;
	ver->upper_pcb_ver = dev->hw_info.upper_pcb_ver;

out:
	return ret;

}

int ssd_get_version(struct block_device *bdev, struct ssd_version_info *ver)
{
	struct ssd_device *dev;
	int ret;

	if (!bdev || !ver || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	mutex_lock(&dev->fw_mutex);
	ret = __ssd_get_version(dev, ver);
	mutex_unlock(&dev->fw_mutex);

	return ret;
}

static int __ssd_get_temperature(struct ssd_device *dev, int *temp)
{
	uint64_t val;
	uint32_t off;
	int max = -300;
	int cur;
	int i;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		*temp = 0;
		return 0;
	}

	if (finject) {
		if (dev->db_info.type == SSD_DEBUG_LOG && 
			(dev->db_info.data.log.event == SSD_LOG_OVER_TEMP || 
			dev->db_info.data.log.event == SSD_LOG_NORMAL_TEMP || 
			dev->db_info.data.log.event == SSD_LOG_WARN_TEMP)) {
			*temp = (int)dev->db_info.data.log.extra;
			return 0;
		}
	}

	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		off = SSD_CTRL_TEMP_REG0 + i * sizeof(uint64_t);

		val = ssd_reg_read(dev->ctrlp + off);
		if (val == 0xffffffffffffffffull) {
			continue;
		}

		cur = (int)CUR_TEMP(val);
		if (cur >= max) {
			max = cur;
		}
	}

	*temp = max;

	return 0;
}

int ssd_get_temperature(struct block_device *bdev, int *temp)
{
	struct ssd_device *dev;
	int ret;

	if (!bdev || !temp || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;


	mutex_lock(&dev->fw_mutex);
	ret = __ssd_get_temperature(dev, temp);
	mutex_unlock(&dev->fw_mutex);

	return ret;
}

int ssd_set_otprotect(struct block_device *bdev, int otprotect)
 {
 	struct ssd_device *dev;

	if (!bdev || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data; 
	ssd_set_ot_protect(dev, !!otprotect);

	return 0;
 }

int ssd_bm_status(struct block_device *bdev, int *status)
{
	struct ssd_device *dev;
	int ret = 0;

	if (!bdev || !status || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	mutex_lock(&dev->fw_mutex);
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
			*status = SSD_BMSTATUS_WARNING;
		} else {
			*status = SSD_BMSTATUS_OK;
		}
	} else if(dev->protocol_info.ver > SSD_PROTOCOL_V3) {
		ret = __ssd_bm_status(dev, status);
	} else {
		*status = SSD_BMSTATUS_OK;
	}
	mutex_unlock(&dev->fw_mutex);

	return ret;
}

int ssd_get_pciaddr(struct block_device *bdev, struct pci_addr *paddr)
{
	struct ssd_device *dev;

	if (!bdev || !paddr || !bdev->bd_disk) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	paddr->domain = pci_domain_nr(dev->pdev->bus);
	paddr->bus = dev->pdev->bus->number;
	paddr->slot = PCI_SLOT(dev->pdev->devfn);
	paddr->func= PCI_FUNC(dev->pdev->devfn);

	return 0;
}

/* acc */
static int ssd_bb_acc(struct ssd_device *dev, struct ssd_acc_info *acc)
{
	uint32_t val;
	int ctrl, chip;
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_1) {
		return -EOPNOTSUPP;
	}

	acc->threshold_l1 = ssd_reg32_read(dev->ctrlp + SSD_BB_THRESHOLD_L1_REG);
	if (0xffffffffull == acc->threshold_l1) {
		return -EIO;
	}
	acc->threshold_l2 = ssd_reg32_read(dev->ctrlp + SSD_BB_THRESHOLD_L2_REG);
	if (0xffffffffull == acc->threshold_l2) {
		return -EIO;
	}
	acc->val = 0;

	for (ctrl=0; ctrl<dev->hw_info.nr_ctrl; ctrl++) {
		for (chip=0; chip<dev->hw_info.nr_chip; chip++) {
			val = ssd_reg32_read(dev->ctrlp + SSD_BB_ACC_REG0 + (SSD_CTRL_REG_ZONE_SZ * ctrl) + (SSD_BB_ACC_REG_SZ * chip));
			if (0xffffffffull == acc->val) {
				return -EIO;
			}
			if (val > acc->val) {
				acc->val = val;
			}
		}
	}

	return 0;
}

static int ssd_ec_acc(struct ssd_device *dev, struct ssd_acc_info *acc)
{
	uint32_t val;
	int ctrl, chip;
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_1) {
		return -EOPNOTSUPP;
	}

	acc->threshold_l1 = ssd_reg32_read(dev->ctrlp + SSD_EC_THRESHOLD_L1_REG);
	if (0xffffffffull == acc->threshold_l1) {
		return -EIO;
	}
	acc->threshold_l2 = ssd_reg32_read(dev->ctrlp + SSD_EC_THRESHOLD_L2_REG);
	if (0xffffffffull == acc->threshold_l2) {
		return -EIO;
	}
	acc->val = 0;

	for (ctrl=0; ctrl<dev->hw_info.nr_ctrl; ctrl++) {
		for (chip=0; chip<dev->hw_info.nr_chip; chip++) {
			val = ssd_reg32_read(dev->ctrlp + SSD_EC_ACC_REG0 + (SSD_CTRL_REG_ZONE_SZ * ctrl) + (SSD_EC_ACC_REG_SZ * chip));
			if (0xffffffffull == acc->val) {
				return -EIO;
			}

			if (val > acc->val) {
				acc->val = val;
			}
		}
	}

	return 0;
}


/* ram r&w */
static int ssd_ram_read_4k(struct ssd_device *dev, void *buf, size_t length, loff_t ofs, int ctrl_idx)
{
	struct ssd_ram_op_msg *msg;
	dma_addr_t buf_dma;
	size_t len = length;
	loff_t ofs_w = ofs;
	int ret = 0;

	if (ctrl_idx >= dev->hw_info.nr_ctrl || (uint64_t)(ofs + length) > dev->hw_info.ram_size 
		|| !length || length > dev->hw_info.ram_max_len 
		|| (length & (dev->hw_info.ram_align - 1)) != 0 || ((uint64_t)ofs & (dev->hw_info.ram_align - 1)) != 0) {
		return -EINVAL;
	}

	len /= dev->hw_info.ram_align;
	do_div(ofs_w, dev->hw_info.ram_align);

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map read DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	msg = (struct ssd_ram_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_RAM_READ;
	msg->ctrl_idx = ctrl_idx;
	msg->start = (uint32_t)ofs_w;
	msg->length = len;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, READ, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_FROMDEVICE);

out_dma_mapping:
	 return ret;
}

static int ssd_ram_write_4k(struct ssd_device *dev, void *buf, size_t length, loff_t ofs, int ctrl_idx)
{
	struct ssd_ram_op_msg *msg;
	dma_addr_t buf_dma;
	size_t len = length;
	loff_t ofs_w = ofs;
	int ret = 0;

	if (ctrl_idx >= dev->hw_info.nr_ctrl || (uint64_t)(ofs + length) > dev->hw_info.ram_size 
		|| !length || length > dev->hw_info.ram_max_len 
		|| (length & (dev->hw_info.ram_align - 1)) != 0 || ((uint64_t)ofs & (dev->hw_info.ram_align - 1)) != 0) {
		return -EINVAL;
	}

	len /= dev->hw_info.ram_align;
	do_div(ofs_w, dev->hw_info.ram_align);

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_TODEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map write DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	msg = (struct ssd_ram_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_RAM_WRITE;
	msg->ctrl_idx = ctrl_idx;
	msg->start = (uint32_t)ofs_w;
	msg->length = len;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_TODEVICE);

out_dma_mapping:
	 return ret;

}

static int ssd_ram_read(struct ssd_device *dev, void *buf, size_t length, loff_t ofs, int ctrl_idx)
{
	int left = length;
	size_t len;
	loff_t off = ofs;
	int ret = 0;

	if (ctrl_idx >= dev->hw_info.nr_ctrl || (uint64_t)(ofs + length) > dev->hw_info.ram_size || !length 
		|| (length & (dev->hw_info.ram_align - 1)) != 0 || ((uint64_t)ofs & (dev->hw_info.ram_align - 1)) != 0) {
		return -EINVAL;
	}

	while (left > 0) {
		len = dev->hw_info.ram_max_len;
		if (left < (int)dev->hw_info.ram_max_len) {
			len = left;
		}

		ret = ssd_ram_read_4k(dev, buf, len, off, ctrl_idx);
		if (ret) {
			break;
		}

		left -= len;
		off += len;
		buf += len;
	}

	return ret;
}

static int ssd_ram_write(struct ssd_device *dev, void *buf, size_t length, loff_t ofs, int ctrl_idx)
{
	int left = length;
	size_t len;
	loff_t off = ofs;
	int ret = 0;

	if (ctrl_idx >= dev->hw_info.nr_ctrl || (uint64_t)(ofs + length) > dev->hw_info.ram_size || !length 
		|| (length & (dev->hw_info.ram_align - 1)) != 0 || ((uint64_t)ofs & (dev->hw_info.ram_align - 1)) != 0) {
		return -EINVAL;
	}

	while (left > 0) {
		len = dev->hw_info.ram_max_len;
		if (left < (int)dev->hw_info.ram_max_len) {
			len = left;
		}

		ret = ssd_ram_write_4k(dev, buf, len, off, ctrl_idx);
		if (ret) {
			break;
		}

		left -= len;
		off += len;
		buf += len;
	}

	return ret;
}


/* flash op */
static int ssd_check_flash(struct ssd_device *dev, int flash, int page, int ctrl_idx)
{
	int cur_ch = flash % dev->hw_info.max_ch;
	int cur_chip = flash /dev->hw_info.max_ch;

	if (ctrl_idx >= dev->hw_info.nr_ctrl) {
		return -EINVAL;
	}

	if (cur_ch >= dev->hw_info.nr_ch || cur_chip >= dev->hw_info.nr_chip) {
		return -EINVAL;
	}

	if (page >= (int)(dev->hw_info.block_count * dev->hw_info.page_count)) {
		return -EINVAL;
	}
	return 0;
}

static int ssd_nand_read_id(struct ssd_device *dev, void *id, int flash, int chip, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	dma_addr_t buf_dma;
	int ret = 0;

	if (unlikely(!id))
		return -EINVAL;

	buf_dma = pci_map_single(dev->pdev, id, SSD_NAND_ID_BUFF_SZ, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map read DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		flash = ((uint32_t)flash << 1) | (uint32_t)chip;
		chip = 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_NAND_READ_ID;
	msg->chip_no = flash;
	msg->chip_ce = chip;
	msg->ctrl_idx = ctrl_idx;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, READ, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, SSD_NAND_ID_BUFF_SZ, PCI_DMA_FROMDEVICE);

out_dma_mapping:
	return ret;
}

#if 0
static int ssd_nand_read(struct ssd_device *dev, void *buf,
	int flash, int chip, int page, int page_count, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	dma_addr_t buf_dma;
	int length;
	int ret = 0;

	if (!buf) {
		return -EINVAL;
	}

	if ((page + page_count) > dev->hw_info.block_count*dev->hw_info.page_count) {
		return -EINVAL;
	}

	ret = ssd_check_flash(dev, flash, page, ctrl_idx);
	if (ret) {
		return ret;
	}

	length = page_count * dev->hw_info.page_size;

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map read DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		flash = (flash << 1) | chip;
		chip = 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_NAND_READ;
	msg->ctrl_idx = ctrl_idx;
	msg->chip_no = flash;
	msg->chip_ce = chip;
	msg->page_no = page;
	msg->page_count = page_count;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, READ, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_FROMDEVICE);

out_dma_mapping:
	return ret;
}
#endif

static int ssd_nand_read_w_oob(struct ssd_device *dev, void *buf, 
	int flash, int chip, int page, int count, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	dma_addr_t buf_dma;
	int length;
	int ret = 0;

	if (!buf) {
		return -EINVAL;
	}

	if ((page + count) > (int)(dev->hw_info.block_count * dev->hw_info.page_count)) {
		return -EINVAL;
	}

	ret = ssd_check_flash(dev, flash, page, ctrl_idx);
	if (ret) {
		return ret;
	}

	length = count * (dev->hw_info.page_size + dev->hw_info.oob_size);

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map read DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		flash = ((uint32_t)flash << 1) | (uint32_t)chip;
		chip = 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_NAND_READ_WOOB;
	msg->ctrl_idx = ctrl_idx;
	msg->chip_no = flash;
	msg->chip_ce = chip;
	msg->page_no = page;
	msg->page_count = count;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, READ, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_FROMDEVICE);

out_dma_mapping:
	return ret;
}

/* write 1 page */
static int ssd_nand_write(struct ssd_device *dev, void *buf, 
	int flash, int chip, int page, int count, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	dma_addr_t buf_dma;
	int length;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		return -EINVAL;
	}

	if (!buf) {
		return -EINVAL;
	}

	if (count != 1) {
		return -EINVAL;
	}

	ret = ssd_check_flash(dev, flash, page, ctrl_idx);
	if (ret) {
		return ret;
	}

	length = count * (dev->hw_info.page_size + dev->hw_info.oob_size);

	/* write data to ram */
	/*ret = ssd_ram_write(dev, buf, length, dev->hw_info.nand_wbuff_base, ctrl_idx);
	if (ret) {
		return ret;
	}*/

	buf_dma = pci_map_single(dev->pdev, buf, length, PCI_DMA_TODEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	ret = dma_mapping_error(buf_dma);
#else
	ret = dma_mapping_error(&(dev->pdev->dev), buf_dma);
#endif
	if (ret) {
		hio_warn("%s: unable to map write DMA buffer\n", dev->name);
		goto out_dma_mapping;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		flash = ((uint32_t)flash << 1) | (uint32_t)chip;
		chip = 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_NAND_WRITE;
	msg->ctrl_idx = ctrl_idx;
	msg->chip_no = flash;
	msg->chip_ce = chip;

	msg->page_no = page;
	msg->page_count = count;
	msg->buf = buf_dma;

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	pci_unmap_single(dev->pdev, buf_dma, length, PCI_DMA_TODEVICE);

out_dma_mapping:
	return ret;
}

static int ssd_nand_erase(struct ssd_device *dev, int flash, int chip, int page, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	int ret = 0;

	ret = ssd_check_flash(dev, flash, page, ctrl_idx);
	if (ret) {
		return ret;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		flash = ((uint32_t)flash << 1) | (uint32_t)chip;
		chip = 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_NAND_ERASE;
	msg->ctrl_idx = ctrl_idx;
	msg->chip_no = flash;
	msg->chip_ce = chip;
	msg->page_no = page;

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

static int ssd_update_bbt(struct ssd_device *dev, int flash, int ctrl_idx)
{
	struct ssd_nand_op_msg *msg;
	struct ssd_flush_msg *fmsg;
	int ret = 0;

	ret = ssd_check_flash(dev, flash, 0, ctrl_idx);
	if (ret) {
		return ret;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		fmsg = (struct ssd_flush_msg *)msg;

		fmsg->fun = SSD_FUNC_FLUSH;
		fmsg->flag = 0x1;
		fmsg->flash = flash;
		fmsg->ctrl_idx = ctrl_idx;
	} else {
		msg->fun = SSD_FUNC_FLUSH;
		msg->flag = 0x1;
		msg->chip_no = flash;
		msg->ctrl_idx = ctrl_idx;
	}

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

/* flash controller init state */
static int __ssd_check_init_state(struct ssd_device *dev)
{
	uint32_t *init_state = NULL;
	int reg_base, reg_sz;
	int max_wait = SSD_INIT_MAX_WAIT;
	int init_wait = 0;
	int i, j, k;
	int ch_start = 0;

/*
	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		ssd_reg32_write(dev->ctrlp + SSD_CTRL_TEST_REG0 + i * 8, test_data);
		read_data = ssd_reg32_read(dev->ctrlp + SSD_CTRL_TEST_REG0 + i * 8);
		if (read_data == ~test_data) {
			//dev->hw_info.nr_ctrl++;
			dev->hw_info.nr_ctrl_map |= 1<<i;
		}
	}
*/

/*
	read_data = ssd_reg32_read(dev->ctrlp + SSD_READY_REG);
	j=0;
	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		if (((read_data>>i) & 0x1) == 0) {
			j++;
		}
	}

	if (dev->hw_info.nr_ctrl != j) {
		printk(KERN_WARNING "%s: nr_ctrl mismatch: %d %d\n", dev->name, dev->hw_info.nr_ctrl, j);
		return -1;
	}
*/

/*
	init_state = ssd_reg_read(dev->ctrlp + SSD_FLASH_INFO_REG0);
	for (j=1; j<dev->hw_info.nr_ctrl;j++) {
		if (init_state != ssd_reg_read(dev->ctrlp + SSD_FLASH_INFO_REG0 + j*8)) {
			printk(KERN_WARNING "SSD_FLASH_INFO_REG[%d], not match\n", j);
			return -1;
			}
		}
*/

/*	init_state = ssd_reg_read(dev->ctrlp + SSD_CHIP_INFO_REG0);
	for (j=1; j<dev->hw_info.nr_ctrl; j++) {
		if (init_state != ssd_reg_read(dev->ctrlp + SSD_CHIP_INFO_REG0 + j*16)) {
			printk(KERN_WARNING "SSD_CHIP_INFO_REG Lo [%d], not match\n", j);
			return -1;
			}
		}

	init_state = ssd_reg_read(dev->ctrlp + SSD_CHIP_INFO_REG0 + 8);
	for (j=1; j<dev->hw_info.nr_ctrl; j++) {
		if (init_state != ssd_reg_read(dev->ctrlp + SSD_CHIP_INFO_REG0 + 8 + j*16)) {
			printk(KERN_WARNING "SSD_CHIP_INFO_REG Hi [%d], not match\n", j);
			return -1;
			}
		}
*/

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		max_wait = SSD_INIT_MAX_WAIT_V3_2;
	}

	reg_base = dev->protocol_info.init_state_reg;
	reg_sz = dev->protocol_info.init_state_reg_sz;

	init_state = (uint32_t *)kmalloc(reg_sz, GFP_KERNEL);
	if (!init_state) {
		return -ENOMEM;
	}

	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
check_init:
		for (j=0, k=0; j<reg_sz; j+=sizeof(uint32_t), k++) {
			init_state[k] = ssd_reg32_read(dev->ctrlp + reg_base + j);
		}

		if (dev->protocol_info.ver > SSD_PROTOCOL_V3) {
			/* just check the last bit, no need to check all channel */
			ch_start = dev->hw_info.max_ch - 1;
		} else {
			ch_start = 0;
		}

		for (j=0; j<dev->hw_info.nr_chip; j++) {
			for (k=ch_start; k<dev->hw_info.max_ch; k++) {
				if (test_bit((j*dev->hw_info.max_ch + k), (void *)init_state)) {
					continue;
				}

				init_wait++;
				if (init_wait <= max_wait) {
					msleep(SSD_INIT_WAIT);
					goto check_init;
				} else {
					if (k < dev->hw_info.nr_ch) {
						hio_warn("%s: controller %d chip %d ch %d init failed\n", 
							dev->name, i, j, k);
					} else {
						hio_warn("%s: controller %d chip %d init failed\n", 
							dev->name, i, j);
					}

					kfree(init_state);
					return -1;
				}
			}
		}
		reg_base += reg_sz;
	}
	//printk(KERN_WARNING "%s: init wait %d\n", dev->name, init_wait);

	kfree(init_state);
	return 0;
}

static int ssd_check_init_state(struct ssd_device *dev)
{
	if (mode != SSD_DRV_MODE_STANDARD) {
		return 0;
	}

	return __ssd_check_init_state(dev);
}

static void ssd_reset_resp_ptr(struct ssd_device *dev);

/* reset flash controller etc */
static int __ssd_reset(struct ssd_device *dev, int type)
{
	if (type < SSD_RST_NOINIT || type > SSD_RST_FULL) {
		return -EINVAL;
	}

	mutex_lock(&dev->fw_mutex);

	if (type == SSD_RST_NOINIT) {	//no init
		ssd_reg32_write(dev->ctrlp + SSD_RESET_REG, SSD_RESET_NOINIT);
	} else if (type == SSD_RST_NORMAL) {	//reset & init
		ssd_reg32_write(dev->ctrlp + SSD_RESET_REG, SSD_RESET);
	} else {	// full reset
		if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
			mutex_unlock(&dev->fw_mutex);
			return -EINVAL;
		}

		ssd_reg32_write(dev->ctrlp + SSD_FULL_RESET_REG, SSD_RESET_FULL);

		/* ?? */
		ssd_reset_resp_ptr(dev);
	}

#ifdef SSD_OT_PROTECT
	dev->ot_delay = 0;
#endif

	msleep(1000);

	/* xx */
	ssd_set_flush_timeout(dev, dev->wmode);

	mutex_unlock(&dev->fw_mutex);
	ssd_gen_swlog(dev, SSD_LOG_RESET, (uint32_t)type);

	return __ssd_check_init_state(dev);
}

static int ssd_save_md(struct ssd_device *dev)
{
	struct ssd_nand_op_msg *msg;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	if (!dev->save_md) {
		return 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_FLUSH;
	msg->flag = 0x2;
	msg->ctrl_idx = 0;
	msg->chip_no = 0;

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

static int ssd_barrier_save_md(struct ssd_device *dev)
{
	struct ssd_nand_op_msg *msg;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return 0;
	}

	if (!dev->save_md) {
		return 0;
	}

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	msg->fun = SSD_FUNC_FLUSH;
	msg->flag = 0x2;
	msg->ctrl_idx = 0;
	msg->chip_no = 0;

	ret = ssd_do_barrier_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

static int ssd_flush(struct ssd_device *dev)
{
	struct ssd_nand_op_msg *msg;
	struct ssd_flush_msg *fmsg;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		fmsg = (struct ssd_flush_msg *)msg;

		fmsg->fun = SSD_FUNC_FLUSH;
		fmsg->flag = 0;
		fmsg->ctrl_idx = 0;
		fmsg->flash = 0;
	} else {
		msg->fun = SSD_FUNC_FLUSH;
		msg->flag = 0;
		msg->ctrl_idx = 0;
		msg->chip_no = 0;
	}

	ret = ssd_do_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

static int ssd_barrier_flush(struct ssd_device *dev)
{
	struct ssd_nand_op_msg *msg;
	struct ssd_flush_msg *fmsg;
	int ret = 0;

	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

	msg = (struct ssd_nand_op_msg *)ssd_get_dmsg(dev);

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		fmsg = (struct ssd_flush_msg *)msg;

		fmsg->fun = SSD_FUNC_FLUSH;
		fmsg->flag = 0;
		fmsg->ctrl_idx = 0;
		fmsg->flash = 0;
	} else {
		msg->fun = SSD_FUNC_FLUSH;
		msg->flag = 0;
		msg->ctrl_idx = 0;
		msg->chip_no = 0;
	}

	ret = ssd_do_barrier_request(dev, WRITE, msg, NULL);
	ssd_put_dmsg(msg);

	return ret;
}

#define SSD_WMODE_BUFFER_TIMEOUT	0x00c82710
#define SSD_WMODE_BUFFER_EX_TIMEOUT	0x000500c8
#define SSD_WMODE_FUA_TIMEOUT		0x000503E8
static void ssd_set_flush_timeout(struct ssd_device *dev, int m)
{
	uint32_t to;
	uint32_t val = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_1) {
		return;
	}

	switch(m) {
		case SSD_WMODE_BUFFER:
			to = SSD_WMODE_BUFFER_TIMEOUT;
			break;
		case SSD_WMODE_BUFFER_EX:
			if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2_1) {
				to = SSD_WMODE_BUFFER_EX_TIMEOUT;
			} else {
				to = SSD_WMODE_BUFFER_TIMEOUT;
			}
			break;
		case SSD_WMODE_FUA:
			to = SSD_WMODE_FUA_TIMEOUT;
			break;
		default:
			return;
	}

	val = (((uint32_t)((uint32_t)m & 0x3) << 28) | to);

	ssd_reg32_write(dev->ctrlp + SSD_FLUSH_TIMEOUT_REG, val);
}

static int ssd_do_switch_wmode(struct ssd_device *dev, int m)
{
	int ret = 0;

	ret = ssd_barrier_start(dev);
	if (ret) {
		goto out;
	}

	ret = ssd_barrier_flush(dev);
	if (ret) {
		goto out_barrier_end;
	}

	/* set contoller flush timeout */
	ssd_set_flush_timeout(dev, m);

	dev->wmode = m;
	mb();

out_barrier_end:
	ssd_barrier_end(dev);
out:
	return ret;
}

static int ssd_switch_wmode(struct ssd_device *dev, int m)
{
	int default_wmode;
	int next_wmode;
	int ret = 0;

	if (!test_bit(SSD_ONLINE, &dev->state)) {
		return -ENODEV;
	}
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		default_wmode = SSD_WMODE_BUFFER;
	} else {
		default_wmode = SSD_WMODE_BUFFER_EX;
	}

	if (SSD_WMODE_AUTO == m) {
		/* battery fault ? */
		if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
			next_wmode = SSD_WMODE_FUA;
		} else {
			next_wmode = default_wmode;
		}
	} else if (SSD_WMODE_DEFAULT == m) {
		next_wmode = default_wmode;
	} else {
		next_wmode = m;
	}

	if (next_wmode != dev->wmode) {
		hio_warn("%s: switch write mode (%d -> %d)\n", dev->name, dev->wmode, next_wmode);
		ret = ssd_do_switch_wmode(dev, next_wmode);
		if (ret) {
			hio_err("%s: can not switch write mode (%d -> %d)\n", dev->name, dev->wmode, next_wmode);
		}
	}

	return ret;
}

static int ssd_init_wmode(struct ssd_device *dev)
{
	int default_wmode;
	int ret = 0;
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		default_wmode = SSD_WMODE_BUFFER;
	} else {
		default_wmode = SSD_WMODE_BUFFER_EX;
	}

	/* dummy mode */
	if (SSD_WMODE_AUTO == dev->user_wmode) {
		/* battery fault ? */
		if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
			dev->wmode = SSD_WMODE_FUA;
		} else {
			dev->wmode = default_wmode;
		}
	} else if (SSD_WMODE_DEFAULT == dev->user_wmode) {
		dev->wmode = default_wmode;
	} else {
		dev->wmode = dev->user_wmode;
	}
	ssd_set_flush_timeout(dev, dev->wmode);

	return ret;
}

static int __ssd_set_wmode(struct ssd_device *dev, int m)
{
	int ret = 0;

	/* not support old fw*/
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_1) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (m < SSD_WMODE_BUFFER || m > SSD_WMODE_DEFAULT) {
		ret = -EINVAL;
		goto out;
	}
	
	ssd_gen_swlog(dev, SSD_LOG_SET_WMODE, m);

	dev->user_wmode = m;

	ret = ssd_switch_wmode(dev, dev->user_wmode);
	if (ret) {
		goto out;
	}

out:
	return ret;
}

int ssd_set_wmode(struct block_device *bdev, int m)
{
	struct ssd_device *dev;

	if (!bdev || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	return __ssd_set_wmode(dev, m);
}

static int ssd_do_reset(struct ssd_device *dev)
{
	int ret = 0;

	if (test_and_set_bit(SSD_RESETING, &dev->state)) {
		return 0;
	}

	ssd_stop_workq(dev);

	ret = ssd_barrier_start(dev);
	if (ret) {
		goto out;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		/* old reset */
		ret = __ssd_reset(dev, SSD_RST_NORMAL);
	} else {
		/* full reset */
		//ret = __ssd_reset(dev, SSD_RST_FULL);
		ret = __ssd_reset(dev, SSD_RST_NORMAL);
	}
	if (ret) {
		goto out_barrier_end;
	}

out_barrier_end:
	ssd_barrier_end(dev);
out:
	ssd_start_workq(dev);
	test_and_clear_bit(SSD_RESETING, &dev->state);
	return ret;
}

static int ssd_full_reset(struct ssd_device *dev)
{
	int ret = 0;

	if (test_and_set_bit(SSD_RESETING, &dev->state)) {
		return 0;
	}

	ssd_stop_workq(dev);

	ret = ssd_barrier_start(dev);
	if (ret) {
		goto out;
	}

	ret = ssd_barrier_flush(dev);
	if (ret) {
		goto out_barrier_end;
	}

	ret = ssd_barrier_save_md(dev);
	if (ret) {
		goto out_barrier_end;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		/* old reset */
		ret = __ssd_reset(dev, SSD_RST_NORMAL);
	} else {
		/* full reset */
		//ret = __ssd_reset(dev, SSD_RST_FULL);
		ret = __ssd_reset(dev, SSD_RST_NORMAL);
	}
	if (ret) {
		goto out_barrier_end;
	}

out_barrier_end:
	ssd_barrier_end(dev);
out:
	ssd_start_workq(dev);
	test_and_clear_bit(SSD_RESETING, &dev->state);
	return ret;
}

int ssd_reset(struct block_device *bdev)
{
	struct ssd_device *dev;

	if (!bdev || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;

	return ssd_full_reset(dev);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static int ssd_issue_flush_fn(struct request_queue *q, struct gendisk *disk, 
		sector_t *error_sector)
{
	struct ssd_device *dev = q->queuedata;

	return ssd_flush(dev);
}
#endif

void ssd_submit_pbio(struct request_queue *q, struct bio *bio)
{
	struct ssd_device *dev = q->queuedata;
#ifdef SSD_QUEUE_PBIO
	int ret = -EBUSY;
#endif

	if (!test_bit(SSD_ONLINE, &dev->state)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -ENODEV);
#else
		bio_endio(bio, bio->bi_size, -ENODEV);
#endif
		goto out;
	}

#ifdef SSD_DEBUG_ERR
	if (atomic_read(&dev->tocnt)) {
		hio_warn("%s: IO rejected because of IO timeout!\n", dev->name);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EIO);
#else
		bio_endio(bio, bio->bi_size, -EIO);
#endif
		goto out;
	}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	if (unlikely(bio_barrier(bio))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
	if (unlikely(bio_rw_flagged(bio, BIO_RW_BARRIER))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
	if (unlikely(bio->bi_rw & REQ_HARDBARRIER)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#else
	//xx
	if (unlikely(bio->bi_rw & REQ_FUA)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#endif

	 if (unlikely(dev->readonly && bio_data_dir(bio) == WRITE)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EROFS);
#else
		bio_endio(bio, bio->bi_size, -EROFS);
#endif
		goto out;
	}

#ifdef SSD_QUEUE_PBIO
	if (0 == atomic_read(&dev->in_sendq)) {
		ret = __ssd_submit_pbio(dev, bio, 0);
	}

	if (ret) {
		(void)test_and_set_bit(BIO_SSD_PBIO, &bio->bi_flags);
		ssd_queue_bio(dev, bio);
	}
#else
	__ssd_submit_pbio(dev, bio, 1);
#endif

out:
	return;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
static blk_qc_t ssd_make_request(struct request_queue *q, struct bio *bio)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
static void ssd_make_request(struct request_queue *q, struct bio *bio)
#else
static int ssd_make_request(struct request_queue *q, struct bio *bio)
#endif
{
	struct ssd_device *dev = q->queuedata;
	int ret = -EBUSY;

	if (!test_bit(SSD_ONLINE, &dev->state)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -ENODEV);
#else
		bio_endio(bio, bio->bi_size, -ENODEV);
#endif
		goto out;
	}

#ifdef SSD_DEBUG_ERR
	if (atomic_read(&dev->tocnt)) {
		hio_warn("%s: IO rejected because of IO timeout!\n", dev->name);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EIO);
#else
		bio_endio(bio, bio->bi_size, -EIO);
#endif
		goto out;
	}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	if (unlikely(bio_barrier(bio))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
	if (unlikely(bio_rw_flagged(bio, BIO_RW_BARRIER))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
	if (unlikely(bio->bi_rw & REQ_HARDBARRIER)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}
#else
	//xx
	if (unlikely(bio->bi_rw & REQ_FUA)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		bio_endio(bio, -EOPNOTSUPP);
#else
		bio_endio(bio, bio->bi_size, -EOPNOTSUPP);
#endif
		goto out;
	}

	/* writeback_cache_control.txt: REQ_FLUSH requests without data can be completed successfully without doing any work */
	if (unlikely((bio->bi_rw & REQ_FLUSH) && !bio_sectors(bio))) {
		bio_endio(bio, 0);
		goto out;
	}

#endif

	if (0 == atomic_read(&dev->in_sendq)) {
		ret = ssd_submit_bio(dev, bio, 0);
	}

	if (ret) {
		ssd_queue_bio(dev, bio);
	}

out:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
	return BLK_QC_T_NONE;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	return;
#else
	return 0;
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16))
static int ssd_block_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ssd_device *dev;

	if (!bdev) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}

	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = (dev->hw_info.size & ~0x3f) >> 6;
	return 0;
}
#endif

static void ssd_cleanup_blkdev(struct ssd_device *dev);
static int ssd_init_blkdev(struct ssd_device *dev);
static int ssd_ioctl_common(struct ssd_device *dev, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	void __user *buf = NULL;
	void *kbuf = NULL;
	int ret = 0;

	switch (cmd) {
		case SSD_CMD_GET_PROTOCOL_INFO:
			if (copy_to_user(argp, &dev->protocol_info, sizeof(struct ssd_protocol_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_GET_HW_INFO:
			if (copy_to_user(argp, &dev->hw_info, sizeof(struct ssd_hw_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_GET_ROM_INFO:
			if (copy_to_user(argp, &dev->rom_info, sizeof(struct ssd_rom_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_GET_SMART: {
			struct ssd_smart smart;
			int i;

			memcpy(&smart, &dev->smart, sizeof(struct ssd_smart));

			mutex_lock(&dev->gd_mutex);
			ssd_update_smart(dev, &smart);
			mutex_unlock(&dev->gd_mutex);

			/* combine the volatile log info */
			if (dev->log_info.nr_log) {
				for (i=0; i<SSD_LOG_NR_LEVEL; i++) {
					smart.log_info.stat[i] += dev->log_info.stat[i];
				}
			}

			if (copy_to_user(argp, &smart, sizeof(struct ssd_smart))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_GET_IDX:
			if (copy_to_user(argp, &dev->idx, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_GET_AMOUNT: {
			int nr_ssd = atomic_read(&ssd_nr);
			if (copy_to_user(argp, &nr_ssd, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_TO_INFO: {
			int tocnt = atomic_read(&dev->tocnt);

			if (copy_to_user(argp, &tocnt, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_DRV_VER: {
			char ver[] = DRIVER_VERSION;
			int len = sizeof(ver);

			if (len > (DRIVER_VERSION_LEN - 1)) {
				len = (DRIVER_VERSION_LEN - 1);
			}
			if (copy_to_user(argp, ver, len)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_BBACC_INFO: {
			struct ssd_acc_info acc;

			mutex_lock(&dev->fw_mutex);
			ret = ssd_bb_acc(dev, &acc);
			mutex_unlock(&dev->fw_mutex);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &acc, sizeof(struct ssd_acc_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_ECACC_INFO: {
			struct ssd_acc_info acc;

			mutex_lock(&dev->fw_mutex);
			ret = ssd_ec_acc(dev, &acc);
			mutex_unlock(&dev->fw_mutex);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &acc, sizeof(struct ssd_acc_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_HW_INFO_EXT:
			if (copy_to_user(argp, &dev->hw_info_ext, sizeof(struct ssd_hw_info_extend))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_REG_READ: {
			struct ssd_reg_op_info reg_info;

			if (copy_from_user(&reg_info, argp, sizeof(struct ssd_reg_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			if (reg_info.offset > dev->mmio_len-sizeof(uint32_t)) {
				ret = -EINVAL;
				break;
			}

			reg_info.value = ssd_reg32_read(dev->ctrlp + reg_info.offset);
			if (copy_to_user(argp, &reg_info, sizeof(struct ssd_reg_op_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_REG_WRITE: {
			struct ssd_reg_op_info reg_info;

			if (copy_from_user(&reg_info, argp, sizeof(struct ssd_reg_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			if (reg_info.offset > dev->mmio_len-sizeof(uint32_t)) {
				ret = -EINVAL;
				break;
			}

			ssd_reg32_write(dev->ctrlp + reg_info.offset, reg_info.value);

			break;
		}

		case SSD_CMD_SPI_READ: {
			struct ssd_spi_op_info spi_info;
			uint32_t off, size;

			if (copy_from_user(&spi_info, argp, sizeof(struct ssd_spi_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			off = spi_info.off;
			size = spi_info.len;
			buf = spi_info.buf;

			if (size > dev->rom_info.size || 0 == size || (off + size) > dev->rom_info.size) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(size, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			ret = ssd_spi_page_read(dev, kbuf, off, size);
			if (ret) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, size)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_SPI_WRITE: {
			struct ssd_spi_op_info spi_info;
			uint32_t off, size;

			if (copy_from_user(&spi_info, argp, sizeof(struct ssd_spi_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			off = spi_info.off;
			size = spi_info.len;
			buf = spi_info.buf;

			if (size > dev->rom_info.size || 0 == size || (off + size) > dev->rom_info.size) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(size, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			if (copy_from_user(kbuf, buf, size)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			ret = ssd_spi_page_write(dev, kbuf, off, size);
			if (ret) {
				kfree(kbuf);
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_SPI_ERASE: {
			struct ssd_spi_op_info spi_info;
			uint32_t off;

			if (copy_from_user(&spi_info, argp, sizeof(struct ssd_spi_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			off = spi_info.off;

			if ((off + dev->rom_info.block_size) > dev->rom_info.size) {
				ret = -EINVAL;
				break;
			}

			ret = ssd_spi_block_erase(dev, off);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_I2C_READ: {
			struct ssd_i2c_op_info i2c_info;
			uint8_t saddr;
			uint8_t rsize;

			if (copy_from_user(&i2c_info, argp, sizeof(struct ssd_i2c_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = i2c_info.saddr;
			rsize = i2c_info.rsize;
			buf = i2c_info.rbuf;

			if (rsize <= 0 || rsize > SSD_I2C_MAX_DATA) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(rsize, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			ret = ssd_i2c_read(dev, saddr, rsize, kbuf);
			if (ret) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, rsize)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_I2C_WRITE: {
			struct ssd_i2c_op_info i2c_info;
			uint8_t saddr;
			uint8_t wsize;

			if (copy_from_user(&i2c_info, argp, sizeof(struct ssd_i2c_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = i2c_info.saddr;
			wsize = i2c_info.wsize;
			buf = i2c_info.wbuf;

			if (wsize <= 0 || wsize > SSD_I2C_MAX_DATA) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(wsize, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			if (copy_from_user(kbuf, buf, wsize)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			ret = ssd_i2c_write(dev, saddr, wsize, kbuf);
			if (ret) {
				kfree(kbuf);
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_I2C_WRITE_READ: {
			struct ssd_i2c_op_info i2c_info;
			uint8_t saddr;
			uint8_t wsize;
			uint8_t rsize;
			uint8_t size;

			if (copy_from_user(&i2c_info, argp, sizeof(struct ssd_i2c_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = i2c_info.saddr;
			wsize = i2c_info.wsize;
			rsize = i2c_info.rsize;
			buf = i2c_info.wbuf;

			if (wsize <= 0 || wsize > SSD_I2C_MAX_DATA) {
				ret = -EINVAL;
				break;
			}

			if (rsize <= 0 || rsize > SSD_I2C_MAX_DATA) {
				ret = -EINVAL;
				break;
			}

			size = wsize + rsize;

			kbuf = kmalloc(size, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			if (copy_from_user((kbuf + rsize), buf, wsize)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			buf = i2c_info.rbuf;

			ret = ssd_i2c_write_read(dev, saddr, wsize, (kbuf + rsize), rsize, kbuf);
			if (ret) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, rsize)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_SMBUS_SEND_BYTE: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			buf = smbus_info.buf;
			size = 1;

			if (copy_from_user(smb_data, buf, size)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = ssd_smbus_send_byte(dev, saddr, smb_data);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_RECEIVE_BYTE: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			buf = smbus_info.buf;
			size = 1;

			ret = ssd_smbus_receive_byte(dev, saddr, smb_data);
			if (ret) {
				break;
			}

			if (copy_to_user(buf, smb_data, size)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_WRITE_BYTE: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = 1;

			if (copy_from_user(smb_data, buf, size)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = ssd_smbus_write_byte(dev, saddr, command, smb_data);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_READ_BYTE: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = 1;

			ret = ssd_smbus_read_byte(dev, saddr, command, smb_data);
			if (ret) {
				break;
			}

			if (copy_to_user(buf, smb_data, size)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_WRITE_WORD: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = 2;

			if (copy_from_user(smb_data, buf, size)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = ssd_smbus_write_word(dev, saddr, command, smb_data);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_READ_WORD: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = 2;

			ret = ssd_smbus_read_word(dev, saddr, command, smb_data);
			if (ret) {
				break;
			}

			if (copy_to_user(buf, smb_data, size)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_WRITE_BLOCK: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = smbus_info.size;

			if (size > SSD_SMBUS_BLOCK_MAX) {
				ret = -EINVAL;
				break;
			}

			if (copy_from_user(smb_data, buf, size)) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = ssd_smbus_write_block(dev, saddr, command, size, smb_data);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_SMBUS_READ_BLOCK: {
			struct ssd_smbus_op_info smbus_info;
			uint8_t smb_data[SSD_SMBUS_BLOCK_MAX];
			uint8_t saddr;
			uint8_t command;
			uint8_t size;

			if (copy_from_user(&smbus_info, argp, sizeof(struct ssd_smbus_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			saddr = smbus_info.saddr;
			command = smbus_info.cmd;
			buf = smbus_info.buf;
			size = smbus_info.size;

			if (size > SSD_SMBUS_BLOCK_MAX) {
				ret = -EINVAL;
				break;
			}

			ret = ssd_smbus_read_block(dev, saddr, command, size, smb_data);
			if (ret) {
				break;
			}

			if (copy_to_user(buf, smb_data, size)) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_BM_GET_VER: {
			uint16_t ver;

			ret = ssd_bm_get_version(dev, &ver);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &ver, sizeof(uint16_t))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_BM_GET_NR_CAP: {
			int nr_cap;

			ret = ssd_bm_nr_cap(dev, &nr_cap);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &nr_cap, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_BM_CAP_LEARNING: {
			ret = ssd_bm_enter_cap_learning(dev);

			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_CAP_LEARN: {
			uint32_t cap = 0;

			ret = ssd_cap_learn(dev, &cap);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &cap, sizeof(uint32_t))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_GET_CAP_STATUS: {
			int cap_status = 0;

			if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
				cap_status = 1;
			}

			if (copy_to_user(argp, &cap_status, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_RAM_READ: {
			struct ssd_ram_op_info ram_info;
			uint64_t ofs;
			uint32_t length;
			size_t rlen, len = dev->hw_info.ram_max_len;
			int ctrl_idx;

			if (copy_from_user(&ram_info, argp, sizeof(struct ssd_ram_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ofs = ram_info.start;
			length = ram_info.length;
			buf = ram_info.buf;
			ctrl_idx = ram_info.ctrl_idx;

			if (ofs >= dev->hw_info.ram_size || length > dev->hw_info.ram_size || 0 == length || (ofs + length) > dev->hw_info.ram_size) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(len, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			for (rlen=0; rlen<length; rlen+=len, buf+=len, ofs+=len) {
				if ((length - rlen) < len) {
					len = length - rlen;
				}

				ret = ssd_ram_read(dev, kbuf, len, ofs, ctrl_idx);
				if (ret) {
					break;
				}

				if (copy_to_user(buf, kbuf, len)) {
					ret = -EFAULT;
					break;
				}
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_RAM_WRITE: {
			struct ssd_ram_op_info ram_info;
			uint64_t ofs;
			uint32_t length;
			size_t wlen, len = dev->hw_info.ram_max_len;
			int ctrl_idx;

			if (copy_from_user(&ram_info, argp, sizeof(struct ssd_ram_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			ofs = ram_info.start;
			length = ram_info.length;
			buf = ram_info.buf;
			ctrl_idx = ram_info.ctrl_idx;

			if (ofs >= dev->hw_info.ram_size || length > dev->hw_info.ram_size || 0 == length || (ofs + length) > dev->hw_info.ram_size) {
				ret = -EINVAL;
				break;
			}

			kbuf = kmalloc(len, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			for (wlen=0; wlen<length; wlen+=len, buf+=len, ofs+=len) {
				if ((length - wlen) < len) {
					len = length - wlen;
				}

				if (copy_from_user(kbuf, buf, len)) {
					ret = -EFAULT;
					break;
				}

				ret = ssd_ram_write(dev, kbuf, len, ofs, ctrl_idx);
				if (ret) {
					break;
				}
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_NAND_READ_ID: {
			struct ssd_flash_op_info flash_info;
			int chip_no, chip_ce, length, ctrl_idx;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			chip_no = flash_info.flash;
			chip_ce = flash_info.chip;
			ctrl_idx = flash_info.ctrl_idx;
			buf = flash_info.buf;
			length = dev->hw_info.id_size;

			//kbuf = kmalloc(length, GFP_KERNEL);
			kbuf = kmalloc(SSD_NAND_ID_BUFF_SZ, GFP_KERNEL); //xx
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}
			memset(kbuf, 0, length);

			ret = ssd_nand_read_id(dev, kbuf, chip_no, chip_ce, ctrl_idx);
 			if (ret) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, length)) {
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			kfree(kbuf);

			break;
		}

		case SSD_CMD_NAND_READ: {	//with oob
			struct ssd_flash_op_info flash_info;
			uint32_t length;
			int flash, chip, page, ctrl_idx;
			int err = 0;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			flash = flash_info.flash;
			chip = flash_info.chip;
			page = flash_info.page;
			buf = flash_info.buf;
			ctrl_idx = flash_info.ctrl_idx;

			length = dev->hw_info.page_size + dev->hw_info.oob_size;

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			err = ret = ssd_nand_read_w_oob(dev, kbuf, flash, chip, page, 1, ctrl_idx);
			if (ret && (-EIO != ret)) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, length)) {
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			ret = err;

			kfree(kbuf);
			break;
		}

		case SSD_CMD_NAND_WRITE: {
			struct ssd_flash_op_info flash_info;
			int flash, chip, page, ctrl_idx;
			uint32_t length;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			flash = flash_info.flash;
			chip = flash_info.chip;
			page = flash_info.page;
			buf = flash_info.buf;
			ctrl_idx = flash_info.ctrl_idx;

			length = dev->hw_info.page_size + dev->hw_info.oob_size;

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			if (copy_from_user(kbuf, buf, length)) {
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			ret = ssd_nand_write(dev, kbuf, flash, chip, page, 1, ctrl_idx);
			if (ret) {
				kfree(kbuf);
				break;
			}

			kfree(kbuf);
			break;
		}

		case SSD_CMD_NAND_ERASE: {
			struct ssd_flash_op_info flash_info;
			int flash, chip, page, ctrl_idx;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			flash = flash_info.flash;
			chip = flash_info.chip;
			page = flash_info.page;
			ctrl_idx = flash_info.ctrl_idx;

			if ((page % dev->hw_info.page_count) != 0) {
				ret = -EINVAL;
				break;
			}

			//hio_warn("erase fs = %llx\n", ofs);
			ret = ssd_nand_erase(dev, flash, chip, page, ctrl_idx);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_NAND_READ_EXT: {	//ingore EIO
			struct ssd_flash_op_info flash_info;
			uint32_t length;
			int flash, chip, page, ctrl_idx;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			flash = flash_info.flash;
			chip = flash_info.chip;
			page = flash_info.page;
			buf = flash_info.buf;
			ctrl_idx = flash_info.ctrl_idx;

			length = dev->hw_info.page_size + dev->hw_info.oob_size;

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				break;
			}

			ret = ssd_nand_read_w_oob(dev, kbuf, flash, chip, page, 1, ctrl_idx);
			if (-EIO == ret) {	//ingore EIO
				ret = 0;
			}
			if (ret) {
				kfree(kbuf);
				break;
			}

			if (copy_to_user(buf, kbuf, length)) {
				kfree(kbuf);
				ret = -EFAULT;
				break;
			}

			kfree(kbuf);
			break;
		}

		case SSD_CMD_UPDATE_BBT: {
			struct ssd_flash_op_info flash_info;
			int ctrl_idx, flash;

			if (copy_from_user(&flash_info, argp, sizeof(struct ssd_flash_op_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ctrl_idx = flash_info.ctrl_idx;
			flash = flash_info.flash;
			ret = ssd_update_bbt(dev, flash, ctrl_idx);
			if (ret) {
				break;
			}

			break;
		}

		case SSD_CMD_CLEAR_ALARM:
			ssd_clear_alarm(dev);
			break;

		case SSD_CMD_SET_ALARM:
			ssd_set_alarm(dev);
			break;

		case SSD_CMD_RESET:
			ret = ssd_do_reset(dev);
			break;

		case SSD_CMD_RELOAD_FW:
			dev->reload_fw = 1;
			if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
				ssd_reg32_write(dev->ctrlp + SSD_RELOAD_FW_REG, SSD_RELOAD_FLAG);
			} else if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_1_1) {
				ssd_reg32_write(dev->ctrlp + SSD_RELOAD_FW_REG, SSD_RELOAD_FW);
				
			}
			break;

		case SSD_CMD_UNLOAD_DEV: {
			if (atomic_read(&dev->refcnt)) {
				ret = -EBUSY;
				break;
			}

			/* save smart */
			ssd_save_smart(dev);

			ret = ssd_flush(dev);
			if (ret) {
				break;
			}

			/* cleanup the block device */
			if (test_and_clear_bit(SSD_INIT_BD, &dev->state)) {
				mutex_lock(&dev->gd_mutex);
				ssd_cleanup_blkdev(dev);
				mutex_unlock(&dev->gd_mutex);
			}

			break;
		}

		case SSD_CMD_LOAD_DEV: {

			if (test_bit(SSD_INIT_BD, &dev->state)) {
				ret = -EINVAL;
				break;
			}

			ret = ssd_init_smart(dev);
			if (ret) {
				hio_warn("%s: init info: failed\n", dev->name);
				break;
			}

			ret = ssd_init_blkdev(dev);
			if (ret) {
				hio_warn("%s: register block device: failed\n", dev->name);
				break;
			}
			(void)test_and_set_bit(SSD_INIT_BD, &dev->state);

			break;
		}

		case SSD_CMD_UPDATE_VP: {
			uint32_t val;
			uint32_t new_vp, new_vp1 = 0;

			if (test_bit(SSD_INIT_BD, &dev->state)) {
				ret = -EINVAL;
				break;
			}

			if (copy_from_user(&new_vp, argp, sizeof(uint32_t))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			if (new_vp > dev->hw_info.max_valid_pages || new_vp <= 0) {
				ret = -EINVAL;
				break;
			}

			while (new_vp <= dev->hw_info.max_valid_pages) {
				ssd_reg32_write(dev->ctrlp + SSD_VALID_PAGES_REG, new_vp);
				msleep(10);
				val = ssd_reg32_read(dev->ctrlp + SSD_VALID_PAGES_REG);
				if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
					new_vp1 = val & 0x3FF;
				} else {
					new_vp1 = val & 0x7FFF;
				}

				if (new_vp1 == new_vp) {
					break;
				}

				new_vp++;
				/*if (new_vp == dev->hw_info.valid_pages) {
					new_vp++;
				}*/
			}

			if (new_vp1 != new_vp || new_vp > dev->hw_info.max_valid_pages) {
				/* restore */
				ssd_reg32_write(dev->ctrlp + SSD_VALID_PAGES_REG, dev->hw_info.valid_pages);
				ret = -EINVAL;
				break;
			}

			if (copy_to_user(argp, &new_vp, sizeof(uint32_t))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ssd_reg32_write(dev->ctrlp + SSD_VALID_PAGES_REG, dev->hw_info.valid_pages);
				ret = -EFAULT;
				break;
			}

			/* new */
			dev->hw_info.valid_pages = new_vp;
			dev->hw_info.size = (uint64_t)dev->hw_info.valid_pages * dev->hw_info.page_size;
			dev->hw_info.size *= (dev->hw_info.block_count - dev->hw_info.reserved_blks);
			dev->hw_info.size *= ((uint64_t)dev->hw_info.nr_data_ch * (uint64_t)dev->hw_info.nr_chip * (uint64_t)dev->hw_info.nr_ctrl);

			break;
		}

		case SSD_CMD_FULL_RESET: {
			ret = ssd_full_reset(dev);
			break;
		}

		case SSD_CMD_GET_NR_LOG: {
			if (copy_to_user(argp, &dev->internal_log.nr_log, sizeof(dev->internal_log.nr_log))) {
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_LOG: {
			uint32_t length = dev->rom_info.log_sz;

			buf = argp;

			if (copy_to_user(buf, dev->internal_log.log, length)) {
				ret = -EFAULT;
				break;
			}

			break;
		}

		case SSD_CMD_LOG_LEVEL: {
			int level = 0;
			if (copy_from_user(&level, argp, sizeof(int))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			if (level >= SSD_LOG_NR_LEVEL || level < SSD_LOG_LEVEL_INFO) {
				level = SSD_LOG_LEVEL_ERR;
			}

			//just for showing log, no need to protect
			log_level = level;
			break;
		}

		case SSD_CMD_OT_PROTECT: {
			int protect = 0;

			if (copy_from_user(&protect, argp, sizeof(int))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ssd_set_ot_protect(dev, !!protect);
			break;
		}

		case SSD_CMD_GET_OT_STATUS: {
			int status = ssd_get_ot_status(dev, &status);

			if (copy_to_user(argp, &status, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_CLEAR_LOG: {
			ret = ssd_clear_log(dev);
			break;
		}

		case SSD_CMD_CLEAR_SMART: {
			ret = ssd_clear_smart(dev);
			break;
		}

		case SSD_CMD_SW_LOG: {
			struct ssd_sw_log_info sw_log;

			if (copy_from_user(&sw_log, argp, sizeof(struct ssd_sw_log_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = ssd_gen_swlog(dev, sw_log.event, sw_log.data);
			break;
		}

		case SSD_CMD_GET_LABEL: {

			if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
				ret = -EINVAL;
				break;
			}
			
			if (copy_to_user(argp, &dev->label, sizeof(struct ssd_label))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_VERSION: {
			struct ssd_version_info ver;

			mutex_lock(&dev->fw_mutex);
			ret = __ssd_get_version(dev, &ver);
			mutex_unlock(&dev->fw_mutex);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &ver, sizeof(struct ssd_version_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_TEMPERATURE: {
			int temp;

			mutex_lock(&dev->fw_mutex);
			ret = __ssd_get_temperature(dev, &temp);
			mutex_unlock(&dev->fw_mutex);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &temp, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_BMSTATUS: {
			int status;

			mutex_lock(&dev->fw_mutex);
			if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
				if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
					status = SSD_BMSTATUS_WARNING;
				} else {
					status = SSD_BMSTATUS_OK;
				}
			} else if(dev->protocol_info.ver > SSD_PROTOCOL_V3) {
				ret = __ssd_bm_status(dev, &status);
			} else {
				status = SSD_BMSTATUS_OK;
			}
			mutex_unlock(&dev->fw_mutex);
			if (ret) {
				break;
			}

			if (copy_to_user(argp, &status, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_GET_LABEL2: {
			void *label;
			int length;

			if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
				label = &dev->label;
				length = sizeof(struct ssd_label);
			} else {
				label = &dev->labelv3;
				length = sizeof(struct ssd_labelv3);
			}

			if (copy_to_user(argp, label, length)) {
				ret = -EFAULT;
				break;
			}
			break;
		}

		case SSD_CMD_FLUSH:
			ret = ssd_flush(dev);
			if (ret) {
				hio_warn("%s: ssd_flush: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		case SSD_CMD_SAVE_MD: {
			int save_md = 0;

			if (copy_from_user(&save_md, argp, sizeof(int))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			dev->save_md = !!save_md;
			break;
		}

		case SSD_CMD_SET_WMODE: {
			int new_wmode = 0;
			
			if (copy_from_user(&new_wmode, argp, sizeof(int))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			ret = __ssd_set_wmode(dev, new_wmode);
			if (ret) {
				break;
			}
			
			break;
		}

		case SSD_CMD_GET_WMODE: {
			if (copy_to_user(argp, &dev->wmode, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			
			break;
		}

		case SSD_CMD_GET_USER_WMODE: {
			if (copy_to_user(argp, &dev->user_wmode, sizeof(int))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			
			break;
		}

		case SSD_CMD_DEBUG: {
			struct ssd_debug_info db_info;

			if (!finject) {
				ret = -EOPNOTSUPP;
				break;
			}

			if (copy_from_user(&db_info, argp, sizeof(struct ssd_debug_info))) {
				hio_warn("%s: copy_from_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}

			if (db_info.type < SSD_DEBUG_NONE || db_info.type >= SSD_DEBUG_NR) {
				ret = -EINVAL;
				break;
			}

			/* IO */
			if (db_info.type >= SSD_DEBUG_READ_ERR && db_info.type <= SSD_DEBUG_RW_ERR && 
				(db_info.data.loc.off + db_info.data.loc.len) > (dev->hw_info.size >> 9)) {
				ret = -EINVAL;
				break;
			}

			memcpy(&dev->db_info, &db_info, sizeof(struct ssd_debug_info));

#ifdef SSD_OT_PROTECT
			/* temperature */
			if (db_info.type == SSD_DEBUG_NONE) {
				ssd_check_temperature(dev, SSD_OT_TEMP);
			} else if (db_info.type == SSD_DEBUG_LOG) {
				if (db_info.data.log.event == SSD_LOG_OVER_TEMP) {
					dev->ot_delay = SSD_OT_DELAY;
				} else if (db_info.data.log.event == SSD_LOG_NORMAL_TEMP) {
					dev->ot_delay = 0;
				}
			}
#endif

			/* offline */
			if (db_info.type == SSD_DEBUG_OFFLINE) {
				test_and_clear_bit(SSD_ONLINE, &dev->state);
			} else if (db_info.type == SSD_DEBUG_NONE) {
				(void)test_and_set_bit(SSD_ONLINE, &dev->state);
			}

			/* log */
			if (db_info.type == SSD_DEBUG_LOG && dev->event_call && dev->gd) {
				dev->event_call(dev->gd, db_info.data.log.event, 0);
			}

			break;
		}

		case SSD_CMD_DRV_PARAM_INFO: {
			struct ssd_drv_param_info drv_param;

			memset(&drv_param, 0, sizeof(struct ssd_drv_param_info));

			drv_param.mode = mode;
			drv_param.status_mask = status_mask;
			drv_param.int_mode = int_mode;
			drv_param.threaded_irq = threaded_irq;
			drv_param.log_level = log_level;
			drv_param.wmode = wmode;
			drv_param.ot_protect = ot_protect;
			drv_param.finject = finject;

			if (copy_to_user(argp, &drv_param, sizeof(struct ssd_drv_param_info))) {
				hio_warn("%s: copy_to_user: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;
		}

		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}


#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static int ssd_block_ioctl(struct inode *inode, struct file *file, 
		unsigned int cmd, unsigned long arg)
{
	struct ssd_device *dev;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	if (!inode) {
		return -EINVAL;
	}
	dev = inode->i_bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#else
static int ssd_block_ioctl(struct block_device *bdev, fmode_t mode, 
		unsigned int cmd, unsigned long arg)
{
	struct ssd_device *dev;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	if (!bdev) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#endif

	switch (cmd) {
		case HDIO_GETGEO: {
			struct hd_geometry geo;
			geo.cylinders = (dev->hw_info.size & ~0x3f) >> 6;
			geo.heads = 4;
			geo.sectors = 16;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
			geo.start = get_start_sect(inode->i_bdev);
#else
			geo.start = get_start_sect(bdev);
#endif
			if (copy_to_user(argp, &geo, sizeof(geo))) {
				ret = -EFAULT;
				break;
			}

			break;
		}

		case BLKFLSBUF:
			ret = ssd_flush(dev);
			if (ret) {
				hio_warn("%s: ssd_flush: failed\n", dev->name);
				ret = -EFAULT;
				break;
			}
			break;

		default:
			if (!dev->slave) {
				ret = ssd_ioctl_common(dev, cmd, arg);
			} else {
				ret = -EFAULT;
			}
			break;
	}

	return ret;
}


static void ssd_free_dev(struct kref *kref)
{
	struct ssd_device *dev;

	if (!kref) {
		return;
	}

	dev = container_of(kref, struct ssd_device, kref);

	put_disk(dev->gd);

	ssd_put_index(dev->slave, dev->idx);

	kfree(dev);
}

static void ssd_put(struct ssd_device *dev)
{
	kref_put(&dev->kref, ssd_free_dev);
}

static int ssd_get(struct ssd_device *dev)
{
	kref_get(&dev->kref);
	return 0;
}

/* block device */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static int ssd_block_open(struct inode *inode, struct file *filp)
{
	struct ssd_device *dev;

	if (!inode) {
		return -EINVAL;
	}

	dev = inode->i_bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#else
static int ssd_block_open(struct block_device *bdev, fmode_t mode)
{
	struct ssd_device *dev;

	if (!bdev) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#endif

	/*if (!try_module_get(dev->owner))
		return -ENODEV;
	*/

	ssd_get(dev);

	atomic_inc(&dev->refcnt);

	return 0;
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static int ssd_block_release(struct inode *inode, struct file *filp)
{
	struct ssd_device *dev;

	if (!inode) {
		return -EINVAL;
	}

	dev = inode->i_bdev->bd_disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(3,9,0))
static int ssd_block_release(struct gendisk *disk, fmode_t mode)
{
	struct ssd_device *dev;

	if (!disk) {
		return -EINVAL;
	}

	dev = disk->private_data;
	if (!dev) {
		return -EINVAL;
	}
#else
static void ssd_block_release(struct gendisk *disk, fmode_t mode)
{
	struct ssd_device *dev;

	if (!disk) {
		return;
	}

	dev = disk->private_data;
	if (!dev) {
		return;
	}
#endif

	atomic_dec(&dev->refcnt);

	ssd_put(dev);

	//module_put(dev->owner);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,9,0))
	return 0;
#endif
}

static struct block_device_operations ssd_fops = {
	.owner		= THIS_MODULE,
	.open		= ssd_block_open,
	.release	= ssd_block_release,
	.ioctl		= ssd_block_ioctl,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16))
	.getgeo		= ssd_block_getgeo,
#endif
};

static void ssd_init_trim(ssd_device_t *dev)
{
#if (defined SSD_TRIM && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)))
	if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
		return;
	}
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, dev->rq);

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)) || (defined RHEL_MAJOR && RHEL_MAJOR >= 6))
	dev->rq->limits.discard_zeroes_data = 1;
	dev->rq->limits.discard_alignment = 4096;
	dev->rq->limits.discard_granularity = 4096;
#endif
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2_4) {
		dev->rq->limits.max_discard_sectors = dev->hw_info.sg_max_sec;
	} else {
		dev->rq->limits.max_discard_sectors = (dev->hw_info.sg_max_sec) * (dev->hw_info.cmd_max_sg);
	}
#endif
}

static void ssd_cleanup_queue(struct ssd_device *dev)
{
	ssd_wait_io(dev);

	blk_cleanup_queue(dev->rq);
	dev->rq = NULL;
}

static int ssd_init_queue(struct ssd_device *dev)
{
	dev->rq = blk_alloc_queue(GFP_KERNEL);
	if (dev->rq == NULL) {
		hio_warn("%s: alloc queue: failed\n ", dev->name);
		goto out_init_queue;
	}

	/* must be first */
	blk_queue_make_request(dev->rq, ssd_make_request);

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)) && !(defined RHEL_MAJOR && RHEL_MAJOR == 6))
	blk_queue_max_hw_segments(dev->rq, dev->hw_info.cmd_max_sg);
	blk_queue_max_phys_segments(dev->rq, dev->hw_info.cmd_max_sg);
	blk_queue_max_sectors(dev->rq, dev->hw_info.sg_max_sec);
#else
	blk_queue_max_segments(dev->rq, dev->hw_info.cmd_max_sg);
	blk_queue_max_hw_sectors(dev->rq, dev->hw_info.sg_max_sec);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
	blk_queue_hardsect_size(dev->rq, 512);
#else
	blk_queue_logical_block_size(dev->rq, 512);
#endif
	/* not work for make_request based drivers(bio) */
	blk_queue_max_segment_size(dev->rq, dev->hw_info.sg_max_sec << 9);

	blk_queue_bounce_limit(dev->rq, BLK_BOUNCE_HIGH);

	dev->rq->queuedata = dev;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	blk_queue_issue_flush_fn(dev->rq, ssd_issue_flush_fn);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, dev->rq);
#endif

	ssd_init_trim(dev);

	return 0;

out_init_queue:
	return -ENOMEM;
}

static void ssd_cleanup_blkdev(struct ssd_device *dev)
{
	del_gendisk(dev->gd);
}

static int ssd_init_blkdev(struct ssd_device *dev)
{
	if (dev->gd) {
		put_disk(dev->gd);
	}

	dev->gd = alloc_disk(ssd_minors);
	if (!dev->gd) {
		hio_warn("%s: alloc_disk fail\n", dev->name);
		goto out_alloc_gd;
	}
	dev->gd->major = dev->major;
	dev->gd->first_minor = dev->idx * ssd_minors;
	dev->gd->fops = &ssd_fops;
	dev->gd->queue = dev->rq;
	dev->gd->private_data = dev;
	dev->gd->driverfs_dev = &dev->pdev->dev;
	snprintf (dev->gd->disk_name, sizeof(dev->gd->disk_name), "%s", dev->name);

	set_capacity(dev->gd, dev->hw_info.size >> 9);

	add_disk(dev->gd);

	return 0;

out_alloc_gd:
	return -ENOMEM;
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,10))
static int ssd_ioctl(struct inode *inode, struct file *file, 
		unsigned int cmd, unsigned long arg)
#else
static long ssd_ioctl(struct file *file, 
		unsigned int cmd, unsigned long arg)
#endif
{
	struct ssd_device *dev;

	if (!file) {
		return -EINVAL;
	}

	dev = file->private_data;
	if (!dev) {
		return -EINVAL;
	}

	return (long)ssd_ioctl_common(dev, cmd, arg);
}

static int ssd_open(struct inode *inode, struct file *file)
{
	struct ssd_device *dev = NULL;
	struct ssd_device *n = NULL;
	int idx;
	int ret = -ENODEV;

	if (!inode || !file) {
		return -EINVAL;
	}

	idx = iminor(inode);

	list_for_each_entry_safe(dev, n, &ssd_list, list) {
		if (dev->idx == idx) {
			ret = 0;
			break;
		}
	}

	if (ret) {
		return ret;
	}

	file->private_data = dev;

	ssd_get(dev);

	return 0;
}

static int ssd_release(struct inode *inode, struct file *file)
{
	struct ssd_device *dev;

	if (!file) {
		return -EINVAL;
	}

	dev = file->private_data;
	if (!dev) {
		return -EINVAL;
	}

	ssd_put(dev);

	file->private_data = NULL;

	return 0;
}

static struct file_operations ssd_cfops = {
	.owner		= THIS_MODULE, 
	.open		= ssd_open, 
	.release	= ssd_release, 
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,10))
	.ioctl		= ssd_ioctl,
#else
	.unlocked_ioctl = ssd_ioctl, 
#endif
};

static void ssd_cleanup_chardev(struct ssd_device *dev)
{
	if (dev->slave) {
		return;
	}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
	class_simple_device_remove(MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx));
	devfs_remove("c%s", dev->name);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,14))
	class_device_destroy(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx));
	devfs_remove("c%s", dev->name);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,17))
	class_device_destroy(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx));
	devfs_remove("c%s", dev->name);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24))
	class_device_destroy(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx));
#else
	device_destroy(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx));
#endif
}

static int ssd_init_chardev(struct ssd_device *dev)
{
	int ret = 0;

	if (dev->slave) {
		return 0;
	}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
	ret = devfs_mk_cdev(MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), S_IFCHR|S_IRUSR|S_IWUSR, "c%s", dev->name);
	if (ret) {
		goto out;
	}
	class_simple_device_add(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
out:
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,14))
	ret = devfs_mk_cdev(MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), S_IFCHR|S_IRUSR|S_IWUSR, "c%s", dev->name);
	if (ret) {
		goto out;
	}
	class_device_create(ssd_class, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
out:
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,17))
	ret = devfs_mk_cdev(MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), S_IFCHR|S_IRUSR|S_IWUSR, "c%s", dev->name);
	if (ret) {
		goto out;
	}
	class_device_create(ssd_class, NULL, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
out:
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24))
	class_device_create(ssd_class, NULL, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	device_create(ssd_class, NULL, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), "c%s", dev->name);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
	device_create_drvdata(ssd_class, NULL, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
#else
	device_create(ssd_class, NULL, MKDEV((dev_t)dev->cmajor, (dev_t)dev->idx), NULL, "c%s", dev->name);
#endif

	return ret;
}

static int ssd_check_hw(struct ssd_device *dev)
{
	uint32_t test_data = 0x55AA5AA5;
	uint32_t read_data;

	ssd_reg32_write(dev->ctrlp + SSD_BRIDGE_TEST_REG, test_data);
	read_data = ssd_reg32_read(dev->ctrlp + SSD_BRIDGE_TEST_REG);
	if (read_data != ~(test_data)) {
		//hio_warn("%s: check bridge error: %#x\n", dev->name, read_data);
		return -1;
	}

	return 0;
}

static int ssd_check_fw(struct ssd_device *dev)
{
	uint32_t val = 0;
	int i;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_3) {
		return 0;
	}

	for (i=0; i<SSD_CONTROLLER_WAIT; i++) {
		val = ssd_reg32_read(dev->ctrlp + SSD_HW_STATUS_REG);
		if ((val & 0x1) && ((val >> 8) & 0x1)) {
			break;
		}

		msleep(SSD_INIT_WAIT);
	}

	if (!(val & 0x1)) {
		/* controller fw status */
		hio_warn("%s: controller firmware load failed: %#x\n", dev->name, val);
		return -1;
	} else if (!((val >> 8) & 0x1)) {
		/* controller state */
		hio_warn("%s: controller state error: %#x\n", dev->name, val);
		return -1;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_RELOAD_FW_REG);
	if (val) {
		dev->reload_fw = 1;
	}

	return 0;
}

static int ssd_init_fw_info(struct ssd_device *dev)
{
	uint32_t val;
	int ret = 0;

	val = ssd_reg32_read(dev->ctrlp + SSD_BRIDGE_VER_REG);
	dev->hw_info.bridge_ver = val & 0xFFF;
	if (dev->hw_info.bridge_ver < SSD_FW_MIN) {
		hio_warn("%s: bridge firmware version %03X is not supported\n", dev->name, dev->hw_info.bridge_ver);
		return -EINVAL;
	}
	hio_info("%s: bridge firmware version: %03X\n", dev->name, dev->hw_info.bridge_ver);

	ret = ssd_check_fw(dev);
	if (ret) {
		goto out;
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

static int ssd_check_clock(struct ssd_device *dev)
{
	uint32_t val;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_3) {
		return 0;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_HW_STATUS_REG);

	/* clock status */
	if (!((val >> 4 ) & 0x1)) {
		if (!test_and_set_bit(SSD_HWMON_CLOCK(SSD_CLOCK_166M_LOST), &dev->hwmon)) {
			hio_warn("%s: 166MHz clock losed: %#x\n", dev->name, val);
			ssd_gen_swlog(dev, SSD_LOG_CLK_FAULT, val);
		}
		ret = -1;
	}

	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		if (!((val >> 5 ) & 0x1)) {
			if (!test_and_set_bit(SSD_HWMON_CLOCK(SSD_CLOCK_166M_SKEW), &dev->hwmon)) {
				hio_warn("%s: 166MHz clock is skew: %#x\n", dev->name, val);
				ssd_gen_swlog(dev, SSD_LOG_CLK_FAULT, val);
			}
			ret = -1;
		}
		if (!((val >> 6 ) & 0x1)) {
			if (!test_and_set_bit(SSD_HWMON_CLOCK(SSD_CLOCK_156M_LOST), &dev->hwmon)) {
				hio_warn("%s: 156.25MHz clock lost: %#x\n", dev->name, val);
				ssd_gen_swlog(dev, SSD_LOG_CLK_FAULT, val);
			}
			ret = -1;
		}
		if (!((val >> 7 ) & 0x1)) {
			if (!test_and_set_bit(SSD_HWMON_CLOCK(SSD_CLOCK_156M_SKEW), &dev->hwmon)) {
				hio_warn("%s: 156.25MHz clock is skew: %#x\n", dev->name, val);
				ssd_gen_swlog(dev, SSD_LOG_CLK_FAULT, val);
			}
			ret = -1;
		}
	}

	return ret;
}

static int ssd_check_volt(struct ssd_device *dev)
{
	int i = 0;
	uint64_t val;
	uint32_t adc_val;
	int ret =0;
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		/* 1.0v */
		if (!test_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V0), &dev->hwmon)) {
			val = ssd_reg_read(dev->ctrlp + SSD_FPGA_1V0_REG0 + i * SSD_CTRL_REG_ZONE_SZ);
			adc_val = SSD_FPGA_VOLT_MAX(val);
			if (adc_val < SSD_FPGA_1V0_ADC_MIN || adc_val > SSD_FPGA_1V0_ADC_MAX) {
				(void)test_and_set_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V0), &dev->hwmon);
				hio_warn("%s: controller %d 1.0V fault: %d mV.\n", dev->name, i, SSD_FPGA_VOLT(adc_val));
				ssd_gen_swlog(dev, SSD_LOG_VOLT_FAULT, SSD_VOLT_LOG_DATA(SSD_FPGA_1V0, i, adc_val));
				ret = -1;
			}

			adc_val = SSD_FPGA_VOLT_MIN(val);
			if (adc_val < SSD_FPGA_1V0_ADC_MIN || adc_val > SSD_FPGA_1V0_ADC_MAX) {
				(void)test_and_set_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V0), &dev->hwmon);
				hio_warn("%s: controller %d 1.0V fault: %d mV.\n", dev->name, i, SSD_FPGA_VOLT(adc_val));
				ssd_gen_swlog(dev, SSD_LOG_VOLT_FAULT, SSD_VOLT_LOG_DATA(SSD_FPGA_1V0, i, adc_val));
				ret = -2;
			}
		}

		/* 1.8v */
		if (!test_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V8), &dev->hwmon)) {
			val = ssd_reg_read(dev->ctrlp + SSD_FPGA_1V8_REG0 + i * SSD_CTRL_REG_ZONE_SZ);
			adc_val = SSD_FPGA_VOLT_MAX(val);
			if (adc_val < SSD_FPGA_1V8_ADC_MIN || adc_val > SSD_FPGA_1V8_ADC_MAX) {
				(void)test_and_set_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V8), &dev->hwmon);
				hio_warn("%s: controller %d 1.8V fault: %d mV.\n", dev->name, i, SSD_FPGA_VOLT(adc_val));
				ssd_gen_swlog(dev, SSD_LOG_VOLT_FAULT, SSD_VOLT_LOG_DATA(SSD_FPGA_1V8, i, adc_val));
				ret = -3;
			}

			adc_val = SSD_FPGA_VOLT_MIN(val);
			if (adc_val < SSD_FPGA_1V8_ADC_MIN || adc_val > SSD_FPGA_1V8_ADC_MAX) {
				(void)test_and_set_bit(SSD_HWMON_FPGA(i, SSD_FPGA_1V8), &dev->hwmon);
				hio_warn("%s: controller %d 1.8V fault: %d mV.\n", dev->name, i, SSD_FPGA_VOLT(adc_val));
				ssd_gen_swlog(dev, SSD_LOG_VOLT_FAULT, SSD_VOLT_LOG_DATA(SSD_FPGA_1V8, i, adc_val));
				ret = -4;
			}
		}
	}

	return ret;
}

static int ssd_check_reset_sync(struct ssd_device *dev)
{
	uint32_t val;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_3) {
		return 0;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_HW_STATUS_REG);
	if (!((val >> 8) & 0x1)) {
		/* controller state */
		hio_warn("%s: controller state error: %#x\n", dev->name, val);
		return -1;
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return 0;
	}

	if (((val >> 9 ) & 0x1)) {
		hio_warn("%s: controller reset asynchronously: %#x\n", dev->name, val);
		ssd_gen_swlog(dev, SSD_LOG_CTRL_RST_SYNC, val);
		return -1;
	}

	return 0;
}

static int ssd_check_hw_bh(struct ssd_device *dev)
{
	int ret;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_3) {
		return 0;
	}

	/* clock status */
	ret = ssd_check_clock(dev);
	if (ret) {
		goto out;
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

static int ssd_check_controller(struct ssd_device *dev)
{
	int ret;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_1_3) {
		return 0;
	}

	/* sync reset */
	ret = ssd_check_reset_sync(dev);
	if (ret) {
		goto out;
	}

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

static int ssd_check_controller_bh(struct ssd_device *dev)
{
	uint32_t test_data = 0x55AA5AA5;
	uint32_t val;
	int reg_base, reg_sz;
	int init_wait = 0;
	int i;
	int ret = 0;

	if (mode != SSD_DRV_MODE_STANDARD) {
		return 0;
	}

	/* controller */
	val = ssd_reg32_read(dev->ctrlp + SSD_READY_REG);
	if (val & 0x1) {
		hio_warn("%s: controller 0 not ready\n", dev->name);
		return -1;
	}

	for (i=0; i<dev->hw_info.nr_ctrl; i++) {
		reg_base = SSD_CTRL_TEST_REG0 + i * SSD_CTRL_TEST_REG_SZ;
		ssd_reg32_write(dev->ctrlp + reg_base, test_data);
		val = ssd_reg32_read(dev->ctrlp + reg_base);
		if (val != ~(test_data)) {
			hio_warn("%s: check controller %d error: %#x\n", dev->name, i, val);
			return -1;
		}
	}

	/* clock */
	ret = ssd_check_volt(dev);
	if (ret) {
		return ret;
	}

	/* ddr */
	if (dev->protocol_info.ver > SSD_PROTOCOL_V3) {
		reg_base = SSD_PV3_RAM_STATUS_REG0;
		reg_sz = SSD_PV3_RAM_STATUS_REG_SZ;

		for (i=0; i<dev->hw_info.nr_ctrl; i++) {
check_ram_status:
			val = ssd_reg32_read(dev->ctrlp + reg_base);

			if (!((val >> 1) & 0x1)) {
				init_wait++;
				if (init_wait <= SSD_RAM_INIT_MAX_WAIT) {
					msleep(SSD_INIT_WAIT);
					goto check_ram_status;
				} else {
					hio_warn("%s: controller %d ram init failed: %#x\n", dev->name, i, val);
					ssd_gen_swlog(dev, SSD_LOG_DDR_INIT_ERR, i);
					return -1;
				}
			}

			reg_base += reg_sz;
		}
	}

	/* ch info */
	for (i=0; i<SSD_CH_INFO_MAX_WAIT; i++) {
		val = ssd_reg32_read(dev->ctrlp + SSD_CH_INFO_REG);
		if (!((val >> 31) & 0x1)) {
			break;
		}

		msleep(SSD_INIT_WAIT);
	}
	if ((val >> 31) & 0x1) {
		hio_warn("%s: channel info init failed: %#x\n", dev->name, val);
		return -1;
	}

	return 0;
}

static int ssd_init_protocol_info(struct ssd_device *dev)
{
	uint32_t val;

	val = ssd_reg32_read(dev->ctrlp + SSD_PROTOCOL_VER_REG);
	if (val == (uint32_t)-1) {
		hio_warn("%s: protocol version error: %#x\n", dev->name, val);
		return -EINVAL;
	}
	dev->protocol_info.ver = val;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		dev->protocol_info.init_state_reg = SSD_INIT_STATE_REG0;
		dev->protocol_info.init_state_reg_sz = SSD_INIT_STATE_REG_SZ;

		dev->protocol_info.chip_info_reg = SSD_CHIP_INFO_REG0;
		dev->protocol_info.chip_info_reg_sz = SSD_CHIP_INFO_REG_SZ;
	} else {
		dev->protocol_info.init_state_reg = SSD_PV3_INIT_STATE_REG0;
		dev->protocol_info.init_state_reg_sz = SSD_PV3_INIT_STATE_REG_SZ;

		dev->protocol_info.chip_info_reg = SSD_PV3_CHIP_INFO_REG0;
		dev->protocol_info.chip_info_reg_sz = SSD_PV3_CHIP_INFO_REG_SZ;
	}

	return 0;
}

static int ssd_init_hw_info(struct ssd_device *dev)
{
	uint64_t val64;
	uint32_t val;
	uint32_t nr_ctrl;
	int ret = 0;

	/* base info */
	val = ssd_reg32_read(dev->ctrlp + SSD_RESP_INFO_REG);
	dev->hw_info.resp_ptr_sz = 16 * (1U << (val & 0xFF));
	dev->hw_info.resp_msg_sz = 16 * (1U << ((val >> 8) & 0xFF));

	if (0 == dev->hw_info.resp_ptr_sz || 0 == dev->hw_info.resp_msg_sz) {
		hio_warn("%s: response info error\n", dev->name);
		ret = -EINVAL;
		goto out;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_BRIDGE_INFO_REG);
	dev->hw_info.cmd_fifo_sz = 1U << ((val >> 4) & 0xF);
	dev->hw_info.cmd_max_sg = 1U << ((val >> 8) & 0xF);
	dev->hw_info.sg_max_sec = 1U << ((val >> 12) & 0xF);
	dev->hw_info.cmd_fifo_sz_mask = dev->hw_info.cmd_fifo_sz - 1;

	if (0 == dev->hw_info.cmd_fifo_sz || 0 == dev->hw_info.cmd_max_sg || 0 == dev->hw_info.sg_max_sec) {
		hio_warn("%s: cmd info error\n", dev->name);
		ret = -EINVAL;
		goto out;
	}

	/* check hw */
	if (ssd_check_hw_bh(dev)) {
		hio_warn("%s: check hardware status failed\n", dev->name);
		ret = -EINVAL;
		goto out;
	}

	if (ssd_check_controller(dev)) {
		hio_warn("%s: check controller state failed\n", dev->name);
		ret = -EINVAL;
		goto out;
	}

	/* nr controller : read again*/
	val = ssd_reg32_read(dev->ctrlp + SSD_BRIDGE_INFO_REG);
	dev->hw_info.nr_ctrl = (val >> 16) & 0xF;

	/* nr ctrl configured */
	nr_ctrl = (val >> 20) & 0xF;
	if (0 == dev->hw_info.nr_ctrl) {
		hio_warn("%s: nr controller error: %u\n", dev->name, dev->hw_info.nr_ctrl);
		ret = -EINVAL;
		goto out;
	} else if (0 != nr_ctrl && nr_ctrl != dev->hw_info.nr_ctrl) {
		hio_warn("%s: nr controller error: configured %u but found %u\n", dev->name, nr_ctrl, dev->hw_info.nr_ctrl);
		if (mode <= SSD_DRV_MODE_STANDARD) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (ssd_check_controller_bh(dev)) {
		hio_warn("%s: check controller failed\n", dev->name);
		ret = -EINVAL;
		goto out;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_PCB_VER_REG);
	dev->hw_info.pcb_ver = (uint8_t) ((val >> 4) & 0xF) + 'A' -1;
	if ((val & 0xF) != 0xF) {
		dev->hw_info.upper_pcb_ver = (uint8_t) (val & 0xF) + 'A' -1;
	}

	if (dev->hw_info.pcb_ver < 'A' || (0 != dev->hw_info.upper_pcb_ver && dev->hw_info.upper_pcb_ver < 'A')) {
		hio_warn("%s: PCB version error: %#x %#x\n", dev->name, dev->hw_info.pcb_ver, dev->hw_info.upper_pcb_ver);
		ret = -EINVAL;
		goto out;
	}

	/* channel info */
	if (mode <= SSD_DRV_MODE_DEBUG) {
		val = ssd_reg32_read(dev->ctrlp + SSD_CH_INFO_REG);
		dev->hw_info.nr_data_ch = val & 0xFF;
		dev->hw_info.nr_ch = dev->hw_info.nr_data_ch + ((val >> 8) & 0xFF);
		dev->hw_info.nr_chip = (val >> 16) & 0xFF;

		if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
			dev->hw_info.max_ch = 1;
			while (dev->hw_info.max_ch < dev->hw_info.nr_ch) dev->hw_info.max_ch <<= 1;
		} else {
			/* set max channel 32  */
			dev->hw_info.max_ch = 32;
		}

		if (0 == dev->hw_info.nr_chip) {
			//for debug mode
			dev->hw_info.nr_chip = 1;
		}

		//xx
		dev->hw_info.id_size = SSD_NAND_ID_SZ;
		dev->hw_info.max_ce = SSD_NAND_MAX_CE;

		if (0 == dev->hw_info.nr_data_ch || 0 == dev->hw_info.nr_ch || 0 == dev->hw_info.nr_chip) {
			hio_warn("%s: channel info error: data_ch %u ch %u chip %u\n", dev->name, dev->hw_info.nr_data_ch, dev->hw_info.nr_ch, dev->hw_info.nr_chip);
			ret = -EINVAL;
			goto out;
		}
	}

	/* ram info */
	if (mode <= SSD_DRV_MODE_DEBUG) {
		val = ssd_reg32_read(dev->ctrlp + SSD_RAM_INFO_REG);
		dev->hw_info.ram_size = 0x4000000ull * (1ULL << (val & 0xF));
		dev->hw_info.ram_align = 1U << ((val >> 12) & 0xF);
		if (dev->hw_info.ram_align < SSD_RAM_ALIGN) {
			if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
				dev->hw_info.ram_align = SSD_RAM_ALIGN;
			} else {
				hio_warn("%s: ram align error: %u\n", dev->name, dev->hw_info.ram_align);
				ret = -EINVAL;
				goto out;
			}
		}
		dev->hw_info.ram_max_len = 0x1000 * (1U << ((val >> 16) & 0xF));

		if (0 == dev->hw_info.ram_size || 0 == dev->hw_info.ram_align || 0 == dev->hw_info.ram_max_len || dev->hw_info.ram_align > dev->hw_info.ram_max_len) {
			hio_warn("%s: ram info error\n", dev->name);
			ret = -EINVAL;
			goto out;
		}

		if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
			dev->hw_info.log_sz = SSD_LOG_MAX_SZ;
		} else {
			val = ssd_reg32_read(dev->ctrlp + SSD_LOG_INFO_REG);
			dev->hw_info.log_sz = 0x1000 * (1U << (val & 0xFF));
		}
		if (0 == dev->hw_info.log_sz) {
			hio_warn("%s: log size error\n", dev->name);
			ret = -EINVAL;
			goto out;
		}

		val = ssd_reg32_read(dev->ctrlp + SSD_BBT_BASE_REG);
		dev->hw_info.bbt_base = 0x40000ull * (val & 0xFFFF);
		dev->hw_info.bbt_size = 0x40000 * (((val >> 16) & 0xFFFF) + 1) / (dev->hw_info.max_ch * dev->hw_info.nr_chip);
		if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
			if (dev->hw_info.bbt_base > dev->hw_info.ram_size || 0 == dev->hw_info.bbt_size) {
				hio_warn("%s: bbt info error\n", dev->name);
				ret = -EINVAL;
				goto out;
			}
		}

		val = ssd_reg32_read(dev->ctrlp + SSD_ECT_BASE_REG);
		dev->hw_info.md_base = 0x40000ull * (val & 0xFFFF);
		if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
			dev->hw_info.md_size = 0x40000 * (((val >> 16) & 0xFFF) + 1) / (dev->hw_info.max_ch * dev->hw_info.nr_chip);
		} else {
			dev->hw_info.md_size = 0x40000 * (((val >> 16) & 0xFFF) + 1) / (dev->hw_info.nr_chip);
		}
		dev->hw_info.md_entry_sz = 8 * (1U << ((val >> 28) & 0xF));
		if (dev->protocol_info.ver >= SSD_PROTOCOL_V3) {
			if (dev->hw_info.md_base > dev->hw_info.ram_size || 0 == dev->hw_info.md_size || 
				0 == dev->hw_info.md_entry_sz || dev->hw_info.md_entry_sz > dev->hw_info.md_size) {
				hio_warn("%s: md info error\n", dev->name);
				ret = -EINVAL;
				goto out;
			}
		}

		if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
			dev->hw_info.nand_wbuff_base = dev->hw_info.ram_size + 1;
		} else {
			val = ssd_reg32_read(dev->ctrlp + SSD_NAND_BUFF_BASE);
			dev->hw_info.nand_wbuff_base = 0x8000ull * val;
		}
	}

	/* flash info */
	if (mode <= SSD_DRV_MODE_DEBUG) {
		if (dev->hw_info.nr_ctrl > 1) {
			val = ssd_reg32_read(dev->ctrlp + SSD_CTRL_VER_REG);
			dev->hw_info.ctrl_ver = val & 0xFFF;
			hio_info("%s: controller firmware version: %03X\n", dev->name, dev->hw_info.ctrl_ver);
		}

		val64 = ssd_reg_read(dev->ctrlp + SSD_FLASH_INFO_REG0);
		dev->hw_info.nand_vendor_id = ((val64 >> 56) & 0xFF);
		dev->hw_info.nand_dev_id = ((val64 >> 48) & 0xFF);

		dev->hw_info.block_count = (((val64 >> 32) & 0xFFFF) + 1);
		dev->hw_info.page_count = ((val64>>16) & 0xFFFF);
		dev->hw_info.page_size = (val64 & 0xFFFF);

		val = ssd_reg32_read(dev->ctrlp + SSD_BB_INFO_REG);
		dev->hw_info.bbf_pages = val & 0xFF;
		dev->hw_info.bbf_seek = (val >> 8) & 0x1;

		if (0 == dev->hw_info.block_count || 0 == dev->hw_info.page_count || 0 == dev->hw_info.page_size || dev->hw_info.block_count > INT_MAX) {
			hio_warn("%s: flash info error\n", dev->name);
			ret = -EINVAL;
			goto out;
		}

		//xx
		dev->hw_info.oob_size = SSD_NAND_OOB_SZ;	//(dev->hw_info.page_size) >> 5;

		val = ssd_reg32_read(dev->ctrlp + SSD_VALID_PAGES_REG);
		if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
			dev->hw_info.valid_pages = val & 0x3FF;
			dev->hw_info.max_valid_pages = (val>>20) & 0x3FF;
		} else {
			dev->hw_info.valid_pages = val & 0x7FFF;
			dev->hw_info.max_valid_pages = (val>>15) & 0x7FFF;
		}
		if (0 == dev->hw_info.valid_pages || 0 == dev->hw_info.max_valid_pages || 
			dev->hw_info.valid_pages > dev->hw_info.max_valid_pages || dev->hw_info.max_valid_pages > dev->hw_info.page_count) {
			hio_warn("%s: valid page info error: valid_pages %d, max_valid_pages %d\n", dev->name, dev->hw_info.valid_pages, dev->hw_info.max_valid_pages);
			ret = -EINVAL;
			goto out;
		}

		val = ssd_reg32_read(dev->ctrlp + SSD_RESERVED_BLKS_REG);
		dev->hw_info.reserved_blks = val & 0xFFFF;
		dev->hw_info.md_reserved_blks = (val >> 16) & 0xFF;
		if (dev->protocol_info.ver <= SSD_PROTOCOL_V3) {
			dev->hw_info.md_reserved_blks = SSD_BBT_RESERVED;
		}
		if (dev->hw_info.reserved_blks > dev->hw_info.block_count || dev->hw_info.md_reserved_blks > dev->hw_info.block_count) {
			hio_warn("%s: reserved blocks info error: reserved_blks %d, md_reserved_blks %d\n", dev->name, dev->hw_info.reserved_blks, dev->hw_info.md_reserved_blks);
			ret = -EINVAL;
			goto out;
		}
	}

	/* size */
	if (mode < SSD_DRV_MODE_DEBUG) {
		dev->hw_info.size = (uint64_t)dev->hw_info.valid_pages * dev->hw_info.page_size;
		dev->hw_info.size *= (dev->hw_info.block_count - dev->hw_info.reserved_blks);
		dev->hw_info.size *= ((uint64_t)dev->hw_info.nr_data_ch * (uint64_t)dev->hw_info.nr_chip * (uint64_t)dev->hw_info.nr_ctrl);
	}

	/* extend hardware info */
	val = ssd_reg32_read(dev->ctrlp + SSD_PCB_VER_REG);
	dev->hw_info_ext.board_type = (val >> 24) & 0xF;

	dev->hw_info_ext.form_factor = SSD_FORM_FACTOR_FHHL;
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2_1) {
		dev->hw_info_ext.form_factor = (val >> 31) & 0x1;
	}
	/*
	dev->hw_info_ext.cap_type = (val >> 28) & 0x3;
	if (SSD_BM_CAP_VINA != dev->hw_info_ext.cap_type && SSD_BM_CAP_JH != dev->hw_info_ext.cap_type) {
		dev->hw_info_ext.cap_type = SSD_BM_CAP_VINA;
	}*/

	/* power loss protect */
	val = ssd_reg32_read(dev->ctrlp + SSD_PLP_INFO_REG);
	dev->hw_info_ext.plp_type = (val & 0x3);
	if (dev->protocol_info.ver >= SSD_PROTOCOL_V3_2) {
		/* 3 or 4 cap */
		dev->hw_info_ext.cap_type = ((val >> 2)& 0x1);
	}

	/* work mode */
	val = ssd_reg32_read(dev->ctrlp + SSD_CH_INFO_REG);
	dev->hw_info_ext.work_mode = (val >> 25) & 0x1;

out:
	/* skip error if not in standard mode */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ret = 0;
	}
	return ret;
}

static void ssd_cleanup_response(struct ssd_device *dev)
{
	int resp_msg_sz = dev->hw_info.resp_msg_sz * dev->hw_info.cmd_fifo_sz * SSD_MSIX_VEC;
	int resp_ptr_sz = dev->hw_info.resp_ptr_sz * SSD_MSIX_VEC;

	pci_free_consistent(dev->pdev, resp_ptr_sz, dev->resp_ptr_base, dev->resp_ptr_base_dma);
	pci_free_consistent(dev->pdev, resp_msg_sz, dev->resp_msg_base, dev->resp_msg_base_dma);
}

static int ssd_init_response(struct ssd_device *dev)
{
	int resp_msg_sz = dev->hw_info.resp_msg_sz * dev->hw_info.cmd_fifo_sz * SSD_MSIX_VEC;
	int resp_ptr_sz = dev->hw_info.resp_ptr_sz * SSD_MSIX_VEC;

	dev->resp_msg_base = pci_alloc_consistent(dev->pdev, resp_msg_sz, &(dev->resp_msg_base_dma));
	if (!dev->resp_msg_base) {
		hio_warn("%s: unable to allocate resp msg DMA buffer\n", dev->name);
		goto out_alloc_resp_msg;
	}
	memset(dev->resp_msg_base, 0xFF, resp_msg_sz);

	dev->resp_ptr_base = pci_alloc_consistent(dev->pdev, resp_ptr_sz, &(dev->resp_ptr_base_dma));
	if (!dev->resp_ptr_base){
		hio_warn("%s: unable to allocate resp ptr DMA buffer\n", dev->name);
		goto out_alloc_resp_ptr;
	}
	memset(dev->resp_ptr_base, 0, resp_ptr_sz);
	dev->resp_idx = *(uint32_t *)(dev->resp_ptr_base) = dev->hw_info.cmd_fifo_sz * 2 - 1;

	ssd_reg_write(dev->ctrlp + SSD_RESP_FIFO_REG, dev->resp_msg_base_dma);
	ssd_reg_write(dev->ctrlp + SSD_RESP_PTR_REG, dev->resp_ptr_base_dma);

	return 0;

out_alloc_resp_ptr:
	pci_free_consistent(dev->pdev, resp_msg_sz, dev->resp_msg_base, dev->resp_msg_base_dma);
out_alloc_resp_msg:
	return -ENOMEM;
}

static int ssd_cleanup_cmd(struct ssd_device *dev)
{
	int msg_sz = ALIGN(sizeof(struct ssd_rw_msg) + (dev->hw_info.cmd_max_sg - 1) * sizeof(struct ssd_sg_entry), SSD_DMA_ALIGN);
	int i;

	for (i=0; i<(int)dev->hw_info.cmd_fifo_sz; i++) {
		kfree(dev->cmd[i].sgl);
	}
	kfree(dev->cmd);
	pci_free_consistent(dev->pdev, (msg_sz * dev->hw_info.cmd_fifo_sz), dev->msg_base, dev->msg_base_dma);
	return 0;
}

static int ssd_init_cmd(struct ssd_device *dev)
{
	int sgl_sz = sizeof(struct scatterlist) * dev->hw_info.cmd_max_sg;
	int cmd_sz = sizeof(struct ssd_cmd) * dev->hw_info.cmd_fifo_sz;
	int msg_sz = ALIGN(sizeof(struct ssd_rw_msg) + (dev->hw_info.cmd_max_sg - 1) * sizeof(struct ssd_sg_entry), SSD_DMA_ALIGN);
	int i;

	spin_lock_init(&dev->cmd_lock);

	dev->msg_base = pci_alloc_consistent(dev->pdev, (msg_sz * dev->hw_info.cmd_fifo_sz), &dev->msg_base_dma);
	if (!dev->msg_base) {
		hio_warn("%s: can not alloc cmd msg\n", dev->name);
		goto out_alloc_msg;
	}

	dev->cmd = kmalloc(cmd_sz, GFP_KERNEL);
	if (!dev->cmd) {
		hio_warn("%s: can not alloc cmd\n", dev->name);
		goto out_alloc_cmd;
	}
	memset(dev->cmd, 0, cmd_sz);

	for (i=0; i<(int)dev->hw_info.cmd_fifo_sz; i++) {
		dev->cmd[i].sgl = kmalloc(sgl_sz, GFP_KERNEL);
		if (!dev->cmd[i].sgl) {
			hio_warn("%s: can not alloc cmd sgl %d\n", dev->name, i);
			goto out_alloc_sgl;
		}

		dev->cmd[i].msg = dev->msg_base + (msg_sz * i);
		dev->cmd[i].msg_dma = dev->msg_base_dma + ((dma_addr_t)msg_sz * i);

		dev->cmd[i].dev = dev;
		dev->cmd[i].tag = i;
		dev->cmd[i].flag = 0;

		INIT_LIST_HEAD(&dev->cmd[i].list);
	}

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3) {
		dev->scmd = ssd_dispatch_cmd;
	} else {
		ssd_reg_write(dev->ctrlp + SSD_MSG_BASE_REG, dev->msg_base_dma);
		if (finject) {
			dev->scmd = ssd_send_cmd_db;
		} else {
			dev->scmd = ssd_send_cmd;
		}
	}

	return 0;

out_alloc_sgl:
	for (i--; i>=0; i--) {
		kfree(dev->cmd[i].sgl);
	}
	kfree(dev->cmd);
out_alloc_cmd:
	pci_free_consistent(dev->pdev, (msg_sz * dev->hw_info.cmd_fifo_sz), dev->msg_base, dev->msg_base_dma);
out_alloc_msg:
	return -ENOMEM;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
static irqreturn_t ssd_interrupt_check(int irq, void *dev_id)
{
	struct ssd_queue *queue = (struct ssd_queue *)dev_id;

	if (*(uint32_t *)queue->resp_ptr == queue->resp_idx) {
		return IRQ_NONE;
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t ssd_interrupt_threaded(int irq, void *dev_id)
{
	struct ssd_queue *queue = (struct ssd_queue *)dev_id;
	struct ssd_device *dev = (struct ssd_device *)queue->dev;
	struct ssd_cmd *cmd;
	union ssd_response_msq __msg;
	union ssd_response_msq *msg = &__msg;
	uint64_t *u64_msg;
	uint32_t resp_idx = queue->resp_idx;
	uint32_t new_resp_idx = *(uint32_t *)queue->resp_ptr;
	uint32_t end_resp_idx;

	if (unlikely(resp_idx == new_resp_idx)) {
		return IRQ_NONE;
	}

	end_resp_idx = new_resp_idx & queue->resp_idx_mask;

	do {
		resp_idx = (resp_idx + 1) & queue->resp_idx_mask;

		/* the resp msg */
		u64_msg = (uint64_t *)(queue->resp_msg + queue->resp_msg_sz * resp_idx);
		msg->u64_msg = *u64_msg;

		if (unlikely(msg->u64_msg == (uint64_t)(-1))) {
			hio_err("%s: empty resp msg: queue %d idx %u\n", dev->name, queue->idx, resp_idx);
			continue;
		}
		/* clear the resp msg */
		*u64_msg = (uint64_t)(-1);

		cmd = &queue->cmd[msg->resp_msg.tag];
		/*if (unlikely(!cmd->bio)) {
			printk(KERN_WARNING "%s: unknown tag %d fun %#x\n", 
				dev->name, msg->resp_msg.tag, msg->resp_msg.fun);
			continue;
		}*/

		if(unlikely(msg->resp_msg.status & (uint32_t)status_mask)) {
			cmd->errors = -EIO;
		} else {
			cmd->errors = 0;
		}
		cmd->nr_log = msg->log_resp_msg.nr_log;

		ssd_done(cmd);

		if (unlikely(msg->resp_msg.fun != SSD_FUNC_READ_LOG && msg->resp_msg.log > 0)) {
			(void)test_and_set_bit(SSD_LOG_HW, &dev->state);
			if (test_bit(SSD_INIT_WORKQ, &dev->state)) {
				queue_work(dev->workq, &dev->log_work);
			}
		}

		if (unlikely(msg->resp_msg.status)) {
			if (msg->resp_msg.fun == SSD_FUNC_READ || msg->resp_msg.fun == SSD_FUNC_WRITE) {
				hio_err("%s: I/O error %d: tag %d fun %#x\n", 
					dev->name, msg->resp_msg.status, msg->resp_msg.tag, msg->resp_msg.fun);

				/* alarm led */
				ssd_set_alarm(dev);
				queue->io_stat.nr_rwerr++;
				ssd_gen_swlog(dev, SSD_LOG_EIO, msg->u32_msg[0]);
			} else {
				hio_info("%s: CMD error %d: tag %d fun %#x\n", 
					dev->name, msg->resp_msg.status, msg->resp_msg.tag, msg->resp_msg.fun);

				ssd_gen_swlog(dev, SSD_LOG_ECMD, msg->u32_msg[0]);
			}
			queue->io_stat.nr_ioerr++;
		}

		if (msg->resp_msg.fun == SSD_FUNC_READ || 
			msg->resp_msg.fun == SSD_FUNC_NAND_READ_WOOB ||
			msg->resp_msg.fun == SSD_FUNC_NAND_READ) {

			queue->ecc_info.bitflip[msg->resp_msg.bitflip]++;
		}
	}while (resp_idx != end_resp_idx);

	queue->resp_idx = new_resp_idx;

	return IRQ_HANDLED;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
static irqreturn_t ssd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t ssd_interrupt(int irq, void *dev_id)
#endif
{
	struct ssd_queue *queue = (struct ssd_queue *)dev_id;
	struct ssd_device *dev = (struct ssd_device *)queue->dev;
	struct ssd_cmd *cmd;
	union ssd_response_msq __msg;
	union ssd_response_msq *msg = &__msg;
	uint64_t *u64_msg;
	uint32_t resp_idx = queue->resp_idx;
	uint32_t new_resp_idx = *(uint32_t *)queue->resp_ptr;
	uint32_t end_resp_idx;

	if (unlikely(resp_idx == new_resp_idx)) {
		return IRQ_NONE;
	}

#if (defined SSD_ESCAPE_IRQ)
	if (SSD_INT_MSIX != dev->int_mode) {
		dev->irq_cpu = smp_processor_id();
	}
#endif

	end_resp_idx = new_resp_idx & queue->resp_idx_mask;

	do {
		resp_idx = (resp_idx + 1) & queue->resp_idx_mask;

		/* the resp msg */
		u64_msg = (uint64_t *)(queue->resp_msg + queue->resp_msg_sz * resp_idx);
		msg->u64_msg = *u64_msg;

		if (unlikely(msg->u64_msg == (uint64_t)(-1))) {
			hio_err("%s: empty resp msg: queue %d idx %u\n", dev->name, queue->idx, resp_idx);
			continue;
		}
		/* clear the resp msg */
		*u64_msg = (uint64_t)(-1);

		cmd = &queue->cmd[msg->resp_msg.tag];
		/*if (unlikely(!cmd->bio)) {
			printk(KERN_WARNING "%s: unknown tag %d fun %#x\n", 
				dev->name, msg->resp_msg.tag, msg->resp_msg.fun);
			continue;
		}*/

		if(unlikely(msg->resp_msg.status & (uint32_t)status_mask)) {
			cmd->errors = -EIO;
		} else {
			cmd->errors = 0;
		}
		cmd->nr_log = msg->log_resp_msg.nr_log;

		ssd_done_bh(cmd);

		if (unlikely(msg->resp_msg.fun != SSD_FUNC_READ_LOG && msg->resp_msg.log > 0)) {
			(void)test_and_set_bit(SSD_LOG_HW, &dev->state);
			if (test_bit(SSD_INIT_WORKQ, &dev->state)) {
				queue_work(dev->workq, &dev->log_work);
			}
		}

		if (unlikely(msg->resp_msg.status)) {
			if (msg->resp_msg.fun == SSD_FUNC_READ || msg->resp_msg.fun == SSD_FUNC_WRITE) {				
				hio_err("%s: I/O error %d: tag %d fun %#x\n", 
					dev->name, msg->resp_msg.status, msg->resp_msg.tag, msg->resp_msg.fun);

				/* alarm led */
				ssd_set_alarm(dev);
				queue->io_stat.nr_rwerr++;
				ssd_gen_swlog(dev, SSD_LOG_EIO, msg->u32_msg[0]);
			} else {
				hio_info("%s: CMD error %d: tag %d fun %#x\n", 
					dev->name, msg->resp_msg.status, msg->resp_msg.tag, msg->resp_msg.fun);

				ssd_gen_swlog(dev, SSD_LOG_ECMD, msg->u32_msg[0]);
			}
			queue->io_stat.nr_ioerr++;
		}

		if (msg->resp_msg.fun == SSD_FUNC_READ || 
			msg->resp_msg.fun == SSD_FUNC_NAND_READ_WOOB ||
			msg->resp_msg.fun == SSD_FUNC_NAND_READ) {

			queue->ecc_info.bitflip[msg->resp_msg.bitflip]++;
		}
	}while (resp_idx != end_resp_idx);

	queue->resp_idx = new_resp_idx;

	return IRQ_HANDLED;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
static irqreturn_t ssd_interrupt_legacy(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t ssd_interrupt_legacy(int irq, void *dev_id)
#endif
{
	irqreturn_t ret;
	struct ssd_queue *queue = (struct ssd_queue *)dev_id;
	struct ssd_device *dev = (struct ssd_device *)queue->dev;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
	ret = ssd_interrupt(irq, dev_id, regs);
#else
	ret = ssd_interrupt(irq, dev_id);
#endif

	/* clear intr */ 
	if (IRQ_HANDLED == ret) {
		ssd_reg32_write(dev->ctrlp + SSD_CLEAR_INTR_REG, 1);
	}

	return ret;
}

static void ssd_reset_resp_ptr(struct ssd_device *dev)
{
	int i;

	for (i=0; i<dev->nr_queue; i++) {
		*(uint32_t *)dev->queue[i].resp_ptr = dev->queue[i].resp_idx = (dev->hw_info.cmd_fifo_sz * 2) - 1;
	}
}

static void ssd_free_irq(struct ssd_device *dev)
{
	int i;

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6))
	if (SSD_INT_MSIX == dev->int_mode) {
		for (i=0; i<dev->nr_queue; i++) {
			irq_set_affinity_hint(dev->entry[i].vector, NULL);
		}
	}
#endif

	for (i=0; i<dev->nr_queue; i++) {
		free_irq(dev->entry[i].vector, &dev->queue[i]);
	}

	if (SSD_INT_MSIX == dev->int_mode) {
		pci_disable_msix(dev->pdev);
	} else if (SSD_INT_MSI == dev->int_mode) {
		pci_disable_msi(dev->pdev);
	}

}

static int ssd_init_irq(struct ssd_device *dev)
{
#if (!defined MODULE) && (defined SSD_MSIX_AFFINITY_FORCE)
	const struct cpumask *cpu_mask;
	static int cpu_affinity = 0;
#endif
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6))
	const struct cpumask *mask = NULL;
	static int cpu = 0;
	int j;
#endif
	int i;
	unsigned long flags = 0;
	int ret = 0;

	ssd_reg32_write(dev->ctrlp + SSD_INTR_INTERVAL_REG, 0x800);

#ifdef SSD_ESCAPE_IRQ
	dev->irq_cpu = -1;
#endif

	if (int_mode >= SSD_INT_MSIX && pci_find_capability(dev->pdev, PCI_CAP_ID_MSIX)) {
		dev->nr_queue = SSD_MSIX_VEC;
		for (i=0; i<dev->nr_queue; i++) {
			dev->entry[i].entry = i;
		}
		for (;;) {
			ret = pci_enable_msix(dev->pdev, dev->entry, dev->nr_queue);
			if (ret == 0) {
				break;
			} else if (ret > 0) {
				dev->nr_queue = ret;
			} else {
				hio_warn("%s: can not enable msix\n", dev->name);
				/* alarm led */
				ssd_set_alarm(dev);
				goto out;
			}
		}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6))
		mask = (dev_to_node(&dev->pdev->dev) == -1) ? cpu_online_mask : cpumask_of_node(dev_to_node(&dev->pdev->dev));
		if ((0 == cpu) || (!cpumask_intersects(mask, cpumask_of(cpu)))) {
			cpu = cpumask_first(mask);
		}
		for (i=0; i<dev->nr_queue; i++) {
			irq_set_affinity_hint(dev->entry[i].vector, cpumask_of(cpu));
			cpu = cpumask_next(cpu, mask);
			if (cpu >= nr_cpu_ids) {
				cpu = cpumask_first(mask);
			}
		}
#endif

		dev->int_mode = SSD_INT_MSIX;
	} else if (int_mode >= SSD_INT_MSI && pci_find_capability(dev->pdev, PCI_CAP_ID_MSI)) {
		ret = pci_enable_msi(dev->pdev);
		if (ret) {
			hio_warn("%s: can not enable msi\n", dev->name);
			/* alarm led */
			ssd_set_alarm(dev);
			goto out;
		}

		dev->nr_queue = 1;
		dev->entry[0].vector = dev->pdev->irq;

		dev->int_mode = SSD_INT_MSI;
	} else {
		dev->nr_queue = 1;
		dev->entry[0].vector = dev->pdev->irq;

		dev->int_mode = SSD_INT_LEGACY;
	}

	for (i=0; i<dev->nr_queue; i++) {
		if (dev->nr_queue > 1) {
			snprintf(dev->queue[i].name, SSD_QUEUE_NAME_LEN, "%s_e100-%d", dev->name, i);
		} else {
			snprintf(dev->queue[i].name, SSD_QUEUE_NAME_LEN, "%s_e100", dev->name);
		}

		dev->queue[i].dev = dev;
		dev->queue[i].idx = i;

		dev->queue[i].resp_idx = (dev->hw_info.cmd_fifo_sz * 2) - 1;
		dev->queue[i].resp_idx_mask = dev->hw_info.cmd_fifo_sz - 1;

		dev->queue[i].resp_msg_sz = dev->hw_info.resp_msg_sz;
		dev->queue[i].resp_msg = dev->resp_msg_base + dev->hw_info.resp_msg_sz * dev->hw_info.cmd_fifo_sz * i;
		dev->queue[i].resp_ptr = dev->resp_ptr_base + dev->hw_info.resp_ptr_sz * i;
		*(uint32_t *)dev->queue[i].resp_ptr = dev->queue[i].resp_idx;

		dev->queue[i].cmd = dev->cmd;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	flags = IRQF_SHARED;
#else
	flags = SA_SHIRQ;
#endif

	for (i=0; i<dev->nr_queue; i++) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
		if (threaded_irq) {
			ret = request_threaded_irq(dev->entry[i].vector, ssd_interrupt_check, ssd_interrupt_threaded, flags, dev->queue[i].name, &dev->queue[i]);
		} else if (dev->int_mode == SSD_INT_LEGACY) {
			ret = request_irq(dev->entry[i].vector, &ssd_interrupt_legacy, flags, dev->queue[i].name, &dev->queue[i]);
		} else {
			ret = request_irq(dev->entry[i].vector, &ssd_interrupt, flags, dev->queue[i].name, &dev->queue[i]);
		}
#else
		if (dev->int_mode == SSD_INT_LEGACY) {
			ret = request_irq(dev->entry[i].vector, &ssd_interrupt_legacy, flags, dev->queue[i].name, &dev->queue[i]);
		} else {
			ret = request_irq(dev->entry[i].vector, &ssd_interrupt, flags, dev->queue[i].name, &dev->queue[i]);
		}
#endif
		if (ret) {
			hio_warn("%s: request irq failed\n", dev->name);
			/* alarm led */
			ssd_set_alarm(dev);
			goto out_request_irq;
		}

#if (!defined MODULE) && (defined SSD_MSIX_AFFINITY_FORCE)
		cpu_mask = (dev_to_node(&dev->pdev->dev) == -1) ? cpu_online_mask : cpumask_of_node(dev_to_node(&dev->pdev->dev));
		if (SSD_INT_MSIX == dev->int_mode) {
			if ((0 == cpu_affinity) || (!cpumask_intersects(mask, cpumask_of(cpu_affinity)))) {
				cpu_affinity = cpumask_first(cpu_mask);
			}

			irq_set_affinity(dev->entry[i].vector, cpumask_of(cpu_affinity));
			cpu_affinity = cpumask_next(cpu_affinity, cpu_mask);
			if (cpu_affinity >= nr_cpu_ids) {
				cpu_affinity = cpumask_first(cpu_mask);
			}
		}
#endif
	}

	return ret;

out_request_irq:
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) || (defined RHEL_MAJOR && RHEL_MAJOR == 6))
	if (SSD_INT_MSIX == dev->int_mode) {
		for (j=0; j<dev->nr_queue; j++) {
			irq_set_affinity_hint(dev->entry[j].vector, NULL);
		}
	}
#endif

	for (i--; i>=0; i--) {
		free_irq(dev->entry[i].vector, &dev->queue[i]);
	}

	if (SSD_INT_MSIX == dev->int_mode) {
		pci_disable_msix(dev->pdev);
	} else if (SSD_INT_MSI == dev->int_mode) {
		pci_disable_msi(dev->pdev);
	}

out:
	return ret;
}

static void ssd_initial_log(struct ssd_device *dev)
{
	uint32_t val;
	uint32_t speed, width;
	
	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return;
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_POWER_ON_REG);
	if (val) {
		ssd_gen_swlog(dev, SSD_LOG_POWER_ON, dev->hw_info.bridge_ver);
	}

	val = ssd_reg32_read(dev->ctrlp + SSD_PCIE_LINKSTATUS_REG);
	speed = val & 0xF;
	width = (val >> 4)& 0x3F;
	if (0x1 == speed) {
		hio_info("%s: PCIe: 2.5GT/s, x%u\n", dev->name, width);
	} else if (0x2 == speed) {
		hio_info("%s: PCIe: 5GT/s, x%u\n", dev->name, width);
	} else {
		hio_info("%s: PCIe: unknown GT/s, x%u\n", dev->name, width);
	}
	ssd_gen_swlog(dev, SSD_LOG_PCIE_LINK_STATUS, val);

	return;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_hwmon_worker(void *data)
{
	struct ssd_device *dev = (struct ssd_device *)data;
#else
static void ssd_hwmon_worker(struct work_struct *work)
{
	struct ssd_device *dev = container_of(work, struct ssd_device, hwmon_work);
#endif

	if (ssd_check_hw(dev)) {
		//hio_err("%s: check hardware failed\n", dev->name);
		return;
	}

	ssd_check_clock(dev);
	ssd_check_volt(dev);

	ssd_mon_boardvolt(dev);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_tempmon_worker(void *data)
{
	struct ssd_device *dev = (struct ssd_device *)data;
#else
static void ssd_tempmon_worker(struct work_struct *work)
{
	struct ssd_device *dev = container_of(work, struct ssd_device, tempmon_work);
#endif

	if (ssd_check_hw(dev)) {
		//hio_err("%s: check hardware failed\n", dev->name);
		return;
	}

	ssd_mon_temp(dev);
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_capmon_worker(void *data)
{
	struct ssd_device *dev = (struct ssd_device *)data;
#else
static void ssd_capmon_worker(struct work_struct *work)
{
	struct ssd_device *dev = container_of(work, struct ssd_device, capmon_work);
#endif
	uint32_t cap = 0;
	uint32_t cap_threshold = SSD_PL_CAP_THRESHOLD;
	int ret = 0;

	if (dev->protocol_info.ver < SSD_PROTOCOL_V3_2) {
		return;
	}

	if (dev->hw_info_ext.form_factor == SSD_FORM_FACTOR_FHHL && dev->hw_info.pcb_ver < 'B') {
		return;
	}

	/* fault before? */
	if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
		ret = ssd_check_pl_cap_fast(dev);
		if (ret) {
			return;
		}
	}

	/* learn */
	ret = ssd_do_cap_learn(dev, &cap);
	if (ret) {
		hio_err("%s: cap learn failed\n", dev->name);
		ssd_gen_swlog(dev, SSD_LOG_CAP_LEARN_FAULT, 0);
		return;
	}

	ssd_gen_swlog(dev, SSD_LOG_CAP_STATUS, cap);

	if (SSD_PL_CAP_CP == dev->hw_info_ext.cap_type) {
		cap_threshold = SSD_PL_CAP_CP_THRESHOLD;
	}

	//use the fw event id?
	if (cap < cap_threshold) {
		if (!test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_BATTERY_FAULT, 0);
		}
	} else if (cap >= (cap_threshold + SSD_PL_CAP_THRESHOLD_HYST)) {
		if (test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon)) {
			ssd_gen_swlog(dev, SSD_LOG_BATTERY_OK, 0);
		}
	}
}

static void ssd_routine_start(void *data)
{
	struct ssd_device *dev;

	if (!data) {
		return;
	}
	dev = data;

	dev->routine_tick++;

	if (test_bit(SSD_INIT_WORKQ, &dev->state) && !ssd_busy(dev)) {
		(void)test_and_set_bit(SSD_LOG_HW, &dev->state);
		queue_work(dev->workq, &dev->log_work);
	}

	if ((dev->routine_tick % SSD_HWMON_ROUTINE_TICK) == 0 && test_bit(SSD_INIT_WORKQ, &dev->state)) {
		queue_work(dev->workq, &dev->hwmon_work);
	}

	if ((dev->routine_tick % SSD_CAPMON_ROUTINE_TICK) == 0 && test_bit(SSD_INIT_WORKQ, &dev->state)) {
		queue_work(dev->workq, &dev->capmon_work);
	}

	if ((dev->routine_tick % SSD_CAPMON2_ROUTINE_TICK) == 0 && test_bit(SSD_HWMON_PL_CAP(SSD_PL_CAP), &dev->hwmon) && test_bit(SSD_INIT_WORKQ, &dev->state)) {
		/* CAP fault? check again */
		queue_work(dev->workq, &dev->capmon_work);
	}

	if (test_bit(SSD_INIT_WORKQ, &dev->state)) {
		queue_work(dev->workq, &dev->tempmon_work);
	}

	/* schedule routine */
	mod_timer(&dev->routine_timer, jiffies + msecs_to_jiffies(SSD_ROUTINE_INTERVAL));
}

static void ssd_cleanup_routine(struct ssd_device *dev)
{
	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return;

	(void)ssd_del_timer(&dev->routine_timer);

	(void)ssd_del_timer(&dev->bm_timer);
}

static int ssd_init_routine(struct ssd_device *dev)
{
	if (unlikely(mode != SSD_DRV_MODE_STANDARD))
		return 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	INIT_WORK(&dev->bm_work, ssd_bm_worker, dev);
	INIT_WORK(&dev->hwmon_work, ssd_hwmon_worker, dev);
	INIT_WORK(&dev->capmon_work, ssd_capmon_worker, dev);
	INIT_WORK(&dev->tempmon_work, ssd_tempmon_worker, dev);
#else
	INIT_WORK(&dev->bm_work, ssd_bm_worker);
	INIT_WORK(&dev->hwmon_work, ssd_hwmon_worker);
	INIT_WORK(&dev->capmon_work, ssd_capmon_worker);
	INIT_WORK(&dev->tempmon_work, ssd_tempmon_worker);
#endif

	/* initial log */
	ssd_initial_log(dev);

	/* schedule bm routine */
	ssd_add_timer(&dev->bm_timer, msecs_to_jiffies(SSD_BM_CAP_LEARNING_DELAY), ssd_bm_routine_start, dev);

	/* schedule routine */
	ssd_add_timer(&dev->routine_timer, msecs_to_jiffies(SSD_ROUTINE_INTERVAL), ssd_routine_start, dev);

	return 0;
}

static void 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
__devexit 
#endif
ssd_remove_one (struct pci_dev *pdev)
{
	struct ssd_device *dev;

	if (!pdev) {
		return;
	}

	dev = pci_get_drvdata(pdev);
	if (!dev) {
		return;
	}

	list_del_init(&dev->list);

	ssd_unregister_sysfs(dev);

	/* offline firstly */
	test_and_clear_bit(SSD_ONLINE, &dev->state);

	/* clean work queue first */
	if (!dev->slave) {
		test_and_clear_bit(SSD_INIT_WORKQ, &dev->state);
		ssd_cleanup_workq(dev);
	}

	/* flush cache */
	(void)ssd_flush(dev);
	(void)ssd_save_md(dev);

	/* save smart */
	if (!dev->slave) {
		ssd_save_smart(dev);
	}

	if (test_and_clear_bit(SSD_INIT_BD, &dev->state)) {
		ssd_cleanup_blkdev(dev);
	}

	if (!dev->slave) {
		ssd_cleanup_chardev(dev);
	}

	/* clean routine */
	if (!dev->slave) {
		ssd_cleanup_routine(dev);
	}

	ssd_cleanup_queue(dev);

	ssd_cleanup_tag(dev);
	ssd_cleanup_thread(dev);

	ssd_free_irq(dev);

	ssd_cleanup_dcmd(dev);
	ssd_cleanup_cmd(dev);
	ssd_cleanup_response(dev);

	if (!dev->slave) {
		ssd_cleanup_log(dev);
	}

	if (dev->reload_fw) { //reload fw
		ssd_reg32_write(dev->ctrlp + SSD_RELOAD_FW_REG, SSD_RELOAD_FW);
	}

	/* unmap physical adress */
#ifdef LINUX_SUSE_OS
	iounmap(dev->ctrlp);
#else
	pci_iounmap(pdev, dev->ctrlp);
#endif

	release_mem_region(dev->mmio_base, dev->mmio_len);

	pci_disable_device(pdev);

	pci_set_drvdata(pdev, NULL);

	ssd_put(dev);
}

static int 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
__devinit 
#endif
ssd_init_one(struct pci_dev *pdev, 
	const struct pci_device_id *ent)
{
	struct ssd_device *dev;
	int ret = 0;

	if (!pdev || !ent) {
		ret = -EINVAL;
		goto out;
	}

	dev = kmalloc(sizeof(struct ssd_device), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto out_alloc_dev;
	}
	memset(dev, 0, sizeof(struct ssd_device));

	dev->owner = THIS_MODULE;

	if (SSD_SLAVE_PORT_DEVID == ent->device) {
		dev->slave = 1;
	}

	dev->idx = ssd_get_index(dev->slave);
	if (dev->idx < 0) {
		ret = -ENOMEM;
		goto out_get_index;
	}

	if (!dev->slave) {
		snprintf(dev->name, SSD_DEV_NAME_LEN, SSD_DEV_NAME);
		ssd_set_dev_name(&dev->name[strlen(SSD_DEV_NAME)], SSD_DEV_NAME_LEN-strlen(SSD_DEV_NAME), dev->idx);
		
		dev->major = ssd_major;
		dev->cmajor = ssd_cmajor;
	} else {
		snprintf(dev->name, SSD_DEV_NAME_LEN, SSD_SDEV_NAME);
		ssd_set_dev_name(&dev->name[strlen(SSD_SDEV_NAME)], SSD_DEV_NAME_LEN-strlen(SSD_SDEV_NAME), dev->idx);
		dev->major = ssd_major_sl;
		dev->cmajor = 0;
	}

	atomic_set(&(dev->refcnt), 0);
	atomic_set(&(dev->tocnt), 0);

	mutex_init(&dev->fw_mutex);

	//xx
	mutex_init(&dev->gd_mutex);

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	kref_init(&dev->kref);

	ret = pci_enable_device(pdev);
	if (ret) {
		hio_warn("%s: can not enable device\n", dev->name);
		goto out_enable_device;
	}

	pci_set_master(pdev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
	ret = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
#else
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
#endif
	if (ret) {
		hio_warn("%s: set dma mask: failed\n", dev->name);
		goto out_set_dma_mask;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
	ret = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
#else
	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
#endif
	if (ret) {
		hio_warn("%s: set consistent dma mask: failed\n", dev->name);
		goto out_set_dma_mask;
	}

	dev->mmio_base = pci_resource_start(pdev, 0);
	dev->mmio_len = pci_resource_len(pdev, 0);

	if (!request_mem_region(dev->mmio_base, dev->mmio_len, SSD_DEV_NAME)) {
		hio_warn("%s: can not reserve MMIO region 0\n", dev->name);
		ret = -EBUSY;
		goto out_request_mem_region;
	}

	/* 2.6.9 kernel bug */
	dev->ctrlp = pci_iomap(pdev, 0, 0);
	if (!dev->ctrlp) {
		hio_warn("%s: can not remap IO region 0\n", dev->name);
		ret = -ENOMEM;
		goto out_pci_iomap;
	}

	ret = ssd_check_hw(dev);
	if (ret) {
		hio_err("%s: check hardware failed\n", dev->name);
		goto out_check_hw;
	}

	ret = ssd_init_protocol_info(dev);
	if (ret) {
		hio_err("%s: init protocol info failed\n", dev->name);
		goto out_init_protocol_info;
	}

	/* alarm led ? */
	ssd_clear_alarm(dev);

	ret = ssd_init_fw_info(dev);
	if (ret) {
		hio_err("%s: init firmware info failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_fw_info;
	}
	
	/* slave port ? */
	if (dev->slave) {
		goto init_next1;
	}

	ret = ssd_init_rom_info(dev);
	if (ret) {
		hio_err("%s: init rom info failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_rom_info;
	}

	ret = ssd_init_label(dev);
	if (ret) {
		hio_err("%s: init label failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_label;
	}

	ret = ssd_init_workq(dev);
	if (ret) {
		hio_warn("%s: init workq failed\n", dev->name);
		goto out_init_workq;
	}
	(void)test_and_set_bit(SSD_INIT_WORKQ, &dev->state);

	ret = ssd_init_log(dev);
	if (ret) {
		hio_err("%s: init log failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_log;
	}

	ret = ssd_init_smart(dev);
	if (ret) {
		hio_err("%s: init info failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_smart;
	}

init_next1:
	ret = ssd_init_hw_info(dev);
	if (ret) {
		hio_err("%s: init hardware info failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_hw_info;
	}

	/* slave port ? */
	if (dev->slave) {
		goto init_next2;
	}

	ret = ssd_init_sensor(dev);
	if (ret) {
		hio_err("%s: init sensor failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_sensor;
	}

	ret = ssd_init_pl_cap(dev);
	if (ret) {
		hio_err("%s: int pl_cap failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_init_pl_cap;
	}

init_next2:
	ret = ssd_check_init_state(dev);
	if (ret) {
		hio_err("%s: check init state failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_check_init_state;
	}

	ret = ssd_init_response(dev);
	if (ret) {
		hio_warn("%s: init resp_msg failed\n", dev->name);
		goto out_init_response;
	}

	ret = ssd_init_cmd(dev);
	if (ret) {
		hio_warn("%s: init msg failed\n", dev->name);
		goto out_init_cmd;
	}

	ret = ssd_init_dcmd(dev);
	if (ret) {
		hio_warn("%s: init cmd failed\n", dev->name);
		goto out_init_dcmd;
	}

	ret = ssd_init_irq(dev);
	if (ret) {
		hio_warn("%s: init irq failed\n", dev->name);
		goto out_init_irq;
	}

	ret = ssd_init_thread(dev);
	if (ret) {
		hio_warn("%s: init thread failed\n", dev->name);
		goto out_init_thread;
	}

	ret = ssd_init_tag(dev);
	if(ret) {
		hio_warn("%s: init tags failed\n", dev->name);
		goto out_init_tags;
	}

	/*  */
	(void)test_and_set_bit(SSD_ONLINE, &dev->state);

	ret = ssd_init_queue(dev);
	if (ret) {
		hio_warn("%s: init queue failed\n", dev->name);
		goto out_init_queue;
	}

	/* slave port ? */
	if (dev->slave) {
		goto init_next3;
	}

	ret = ssd_init_ot_protect(dev);
	if (ret) {
		hio_err("%s: int ot_protect failed\n", dev->name);
		/* alarm led */
		ssd_set_alarm(dev);
		goto out_int_ot_protect;
	}

	ret = ssd_init_wmode(dev);
	if (ret) {
		hio_warn("%s: init write mode\n", dev->name);
		goto out_init_wmode;
	}

	/* init routine after hw is ready */
	ret = ssd_init_routine(dev);
	if (ret) {
		hio_warn("%s: init routine\n", dev->name);
		goto out_init_routine;
	}

	ret = ssd_init_chardev(dev);
	if (ret) {
		hio_warn("%s: register char device failed\n", dev->name);
		goto out_init_chardev;
	}

init_next3:
	ret = ssd_init_blkdev(dev);
	if (ret) {
		hio_warn("%s: register block device failed\n", dev->name);
		goto out_init_blkdev;
	}
	(void)test_and_set_bit(SSD_INIT_BD, &dev->state);

	ret = ssd_register_sysfs(dev);
	if (ret) {
		hio_warn("%s: register sysfs failed\n", dev->name);
		goto out_register_sysfs;
	}

	dev->save_md = 1;

	list_add_tail(&dev->list, &ssd_list);

	return 0;

out_register_sysfs:
	test_and_clear_bit(SSD_INIT_BD, &dev->state);
	ssd_cleanup_blkdev(dev);
out_init_blkdev:
	/* slave port ? */
	if (!dev->slave) {
		ssd_cleanup_chardev(dev);
	}
out_init_chardev:
	/* slave port ? */
	if (!dev->slave) {
		ssd_cleanup_routine(dev);
	}
out_init_routine:
out_init_wmode:
out_int_ot_protect:
	ssd_cleanup_queue(dev);
out_init_queue:
	test_and_clear_bit(SSD_ONLINE, &dev->state);
	ssd_cleanup_tag(dev);
out_init_tags:
	ssd_cleanup_thread(dev);
out_init_thread:
	ssd_free_irq(dev);
out_init_irq:
	ssd_cleanup_dcmd(dev);
out_init_dcmd:
	ssd_cleanup_cmd(dev);
out_init_cmd:
	ssd_cleanup_response(dev);
out_init_response:
out_check_init_state:
out_init_pl_cap:
out_init_sensor:
out_init_hw_info:
out_init_smart:
	/* slave port ? */
	if (!dev->slave) {
		ssd_cleanup_log(dev);
	}
out_init_log:
	/* slave port ? */
	if (!dev->slave) {
		test_and_clear_bit(SSD_INIT_WORKQ, &dev->state);
		ssd_cleanup_workq(dev);
	}
out_init_workq:
out_init_label:
out_init_rom_info:
out_init_fw_info:
out_init_protocol_info:
out_check_hw:
#ifdef LINUX_SUSE_OS
	iounmap(dev->ctrlp);
#else
	pci_iounmap(pdev, dev->ctrlp);
#endif
out_pci_iomap:
	release_mem_region(dev->mmio_base, dev->mmio_len);
out_request_mem_region:
out_set_dma_mask:
	pci_disable_device(pdev);
out_enable_device:
	pci_set_drvdata(pdev, NULL);
out_get_index:
	kfree(dev);
out_alloc_dev:
out:
	return ret;
}

static void ssd_cleanup_tasklet(void)
{
	int i;
	for_each_online_cpu(i) {
		tasklet_kill(&per_cpu(ssd_tasklet, i));
	}
}

static int ssd_init_tasklet(void)
{
	int i;

	for_each_online_cpu(i) {
		INIT_LIST_HEAD(&per_cpu(ssd_doneq, i));

		if (finject) {
			tasklet_init(&per_cpu(ssd_tasklet, i), __ssd_done_db, 0);
		} else {
			tasklet_init(&per_cpu(ssd_tasklet, i), __ssd_done, 0);
		}
	}

	return 0;
}

static struct pci_device_id ssd_pci_tbl[] = {
	{ 0x10ee, 0x0007, PCI_ANY_ID, PCI_ANY_ID, }, /* g3 */
	{ 0x19e5, 0x0007, PCI_ANY_ID, PCI_ANY_ID, }, /* v1 */
	//{ 0x19e5, 0x0008, PCI_ANY_ID, PCI_ANY_ID, }, /* v1 sp*/
	{ 0x19e5, 0x0009, PCI_ANY_ID, PCI_ANY_ID, }, /* v2 */
	{ 0x19e5, 0x000a, PCI_ANY_ID, PCI_ANY_ID, }, /* v2 dp slave*/
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ssd_pci_tbl);

static struct pci_driver ssd_driver = {
	.name		= MODULE_NAME, 
	.id_table	= ssd_pci_tbl, 
	.probe		= ssd_init_one, 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))	
	.remove		= __devexit_p(ssd_remove_one), 
#else
	.remove		= ssd_remove_one, 
#endif
};

/* notifier block to get a notify on system shutdown/halt/reboot */
static int ssd_notify_reboot(struct notifier_block *nb, unsigned long event, void *buf)
{
	struct ssd_device *dev = NULL;
	struct ssd_device *n = NULL;

	list_for_each_entry_safe(dev, n, &ssd_list, list) {
		ssd_gen_swlog(dev, SSD_LOG_POWER_OFF, 0);
	
		(void)ssd_flush(dev);
		(void)ssd_save_md(dev);

		/* slave port ? */
		if (!dev->slave) {
			ssd_save_smart(dev);

			ssd_stop_workq(dev);

			if (dev->reload_fw) {
				ssd_reg32_write(dev->ctrlp + SSD_RELOAD_FW_REG, SSD_RELOAD_FW);
			}
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block ssd_notifier = {
	ssd_notify_reboot, NULL, 0
};

static int __init ssd_init_module(void)
{
	int ret = 0;

	hio_info("driver version: %s\n", DRIVER_VERSION);

	ret = ssd_init_index();
	if (ret) {
		hio_warn("init index failed\n");
		goto out_init_index;
	}

	ret = ssd_init_proc();
	if (ret) {
		hio_warn("init proc failed\n");
		goto out_init_proc;
	}

	ret = ssd_init_sysfs();
	if (ret) {
		hio_warn("init sysfs failed\n");
		goto out_init_sysfs;
	}

	ret = ssd_init_tasklet();
	if (ret) {
		hio_warn("init tasklet failed\n");
		goto out_init_tasklet;
	}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
	ssd_class = class_simple_create(THIS_MODULE, SSD_DEV_NAME);
#else
	ssd_class = class_create(THIS_MODULE, SSD_DEV_NAME);
#endif
	if (IS_ERR(ssd_class)) {
		ret = PTR_ERR(ssd_class);
		goto out_class_create;
	}

	if (ssd_cmajor > 0) {
		ret = register_chrdev(ssd_cmajor, SSD_CDEV_NAME, &ssd_cfops);
	} else {
		ret = ssd_cmajor = register_chrdev(ssd_cmajor, SSD_CDEV_NAME, &ssd_cfops);
	}
	if (ret < 0) {
		hio_warn("unable to register chardev major number\n");
		goto out_register_chardev;
	}

	if (ssd_major > 0) {
		ret = register_blkdev(ssd_major, SSD_DEV_NAME);
	} else {
		ret = ssd_major = register_blkdev(ssd_major, SSD_DEV_NAME);
	}
	if (ret < 0) {
		hio_warn("unable to register major number\n");
		goto out_register_blkdev;
	}

	if (ssd_major_sl > 0) {
		ret = register_blkdev(ssd_major_sl, SSD_SDEV_NAME);
	} else {
		ret = ssd_major_sl = register_blkdev(ssd_major_sl, SSD_SDEV_NAME);
	}
	if (ret < 0) {
		hio_warn("unable to register slave major number\n");
		goto out_register_blkdev_sl;
	}

	if (mode < SSD_DRV_MODE_STANDARD || mode > SSD_DRV_MODE_BASE) {
		mode = SSD_DRV_MODE_STANDARD;
	}

	/* for debug */
	if (mode != SSD_DRV_MODE_STANDARD) {
		ssd_minors = 1;
	}

	if (int_mode < SSD_INT_LEGACY || int_mode > SSD_INT_MSIX) {
		int_mode = SSD_INT_MODE_DEFAULT;
	}

	if (threaded_irq) {
		int_mode = SSD_INT_MSI;
	}

	if (log_level >= SSD_LOG_NR_LEVEL || log_level < SSD_LOG_LEVEL_INFO) {
		log_level = SSD_LOG_LEVEL_ERR;
	}

	if (wmode < SSD_WMODE_BUFFER || wmode > SSD_WMODE_DEFAULT) {
		wmode = SSD_WMODE_DEFAULT;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	ret = pci_module_init(&ssd_driver);
#else
	ret = pci_register_driver(&ssd_driver);
#endif
	if (ret) {
		hio_warn("pci init failed\n");
		goto out_pci_init;
	}

	ret = register_reboot_notifier(&ssd_notifier);
	if (ret) {
		hio_warn("register reboot notifier failed\n");
		goto out_register_reboot_notifier;
	}

	return 0;

out_register_reboot_notifier:
out_pci_init:
	pci_unregister_driver(&ssd_driver);
	unregister_blkdev(ssd_major_sl, SSD_SDEV_NAME);
out_register_blkdev_sl:
	unregister_blkdev(ssd_major, SSD_DEV_NAME);
out_register_blkdev:
	unregister_chrdev(ssd_cmajor, SSD_CDEV_NAME);
out_register_chardev:
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
	class_simple_destroy(ssd_class);
#else
	class_destroy(ssd_class);
#endif
out_class_create:
	ssd_cleanup_tasklet();
out_init_tasklet:
	ssd_cleanup_sysfs();
out_init_sysfs:
	ssd_cleanup_proc();
out_init_proc:
	ssd_cleanup_index();
out_init_index:
	return ret;

}

static void __exit ssd_cleanup_module(void)
{

	hio_info("unload driver: %s\n", DRIVER_VERSION);
	/* exiting */
	ssd_exiting = 1;

	unregister_reboot_notifier(&ssd_notifier);

	pci_unregister_driver(&ssd_driver);

	unregister_blkdev(ssd_major_sl, SSD_SDEV_NAME);
	unregister_blkdev(ssd_major, SSD_DEV_NAME);
	unregister_chrdev(ssd_cmajor, SSD_CDEV_NAME);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12))
	class_simple_destroy(ssd_class);
#else
	class_destroy(ssd_class);
#endif

	ssd_cleanup_tasklet();
	ssd_cleanup_sysfs();
	ssd_cleanup_proc();
	ssd_cleanup_index();
}

int ssd_register_event_notifier(struct block_device *bdev, ssd_event_call event_call)
{
	struct ssd_device *dev;
	struct timeval tv;
	struct ssd_log *le;
	uint64_t cur;
	int log_nr;

	if (!bdev || !event_call || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;
	dev->event_call = event_call;

	do_gettimeofday(&tv);
	cur = tv.tv_sec;

	le = (struct ssd_log *)(dev->internal_log.log);
	log_nr = dev->internal_log.nr_log;

	while (log_nr--) {
		if (le->time <= cur && le->time >= dev->uptime) {
			(void)dev->event_call(dev->gd, le->le.event, ssd_parse_log(dev, le, 0));
		}
		le++;
	}

	return 0;
}

int ssd_unregister_event_notifier(struct block_device *bdev)
{
	struct ssd_device *dev;

	if (!bdev || !(bdev->bd_disk)) {
		return -EINVAL;
	}

	dev = bdev->bd_disk->private_data;
	dev->event_call = NULL;

	return 0;
}

EXPORT_SYMBOL(ssd_get_label);
EXPORT_SYMBOL(ssd_get_version);
EXPORT_SYMBOL(ssd_set_otprotect);
EXPORT_SYMBOL(ssd_bm_status);
EXPORT_SYMBOL(ssd_submit_pbio);
EXPORT_SYMBOL(ssd_get_pciaddr);
EXPORT_SYMBOL(ssd_get_temperature);
EXPORT_SYMBOL(ssd_register_event_notifier);
EXPORT_SYMBOL(ssd_unregister_event_notifier);
EXPORT_SYMBOL(ssd_reset);
EXPORT_SYMBOL(ssd_set_wmode);



module_init(ssd_init_module);
module_exit(ssd_cleanup_module);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei SSD DEV Team");
MODULE_DESCRIPTION("Huawei SSD driver");
