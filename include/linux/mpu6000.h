/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  $
 */

/**
 * @defgroup
 * @brief
 *
 * @{
 *      @file     mpu6000.h
 *      @brief
 */

#ifndef __MPU6000_H_
#define __MPU6000_H_

#define MPU_NAME "mpu6000"
#define DEFAULT_MPU_SLAVEADDR       0x68

/*==== M_HW REGISTER SET ====*/
enum {
	MPUREG_XG_OFFS_TC = 0,			/* 0x00 */
	MPUREG_YG_OFFS_TC,			/* 0x00 */
	MPUREG_ZG_OFFS_TC,			/* 0x00 */
	MPUREG_X_FINE_GAIN,			/* 0x00 */
	MPUREG_Y_FINE_GAIN,			/* 0x00 */
	MPUREG_Z_FINE_GAIN,			/* 0x00 */
	MPUREG_XA_OFFS_H,			/* 0x00 */
	MPUREG_XA_OFFS_L_TC,			/* 0x00 */
	MPUREG_YA_OFFS_H,			/* 0x00 */
	MPUREG_YA_OFFS_L_TC,			/* 0x00 */
	MPUREG_ZA_OFFS_H,			/* 0x00 */
	MPUREG_ZA_OFFS_L_TC,	/* 0xB */
	MPUREG_0C_RSVD,			/* 0x00 */
	MPUREG_0D_RSVD,			/* 0x00 */
	MPUREG_0E_RSVD,			/* 0x00 */
	MPUREG_0F_RSVD,			/* 0x00 */
	MPUREG_10_RSVD,			/* 0x00 */
	MPUREG_11_RSVD,			/* 0x00 */
	MPUREG_12_RSVD,			/* 0x00 */
	MPUREG_XG_OFFS_USRH,			/* 0x00 */
	MPUREG_XG_OFFS_USRL,			/* 0x00 */
	MPUREG_YG_OFFS_USRH,			/* 0x00 */
	MPUREG_YG_OFFS_USRL,			/* 0x00 */
	MPUREG_ZG_OFFS_USRH,			/* 0x00 */
	MPUREG_ZG_OFFS_USRL,			/* 0x00 */
	MPUREG_SMPLRT_DIV,	/* 0x19 */
	MPUREG_CONFIG,		/* 0x1A ==> DLPF_FS_SYNC */
	MPUREG_GYRO_CONFIG,			/* 0x00 */
	MPUREG_ACCEL_CONFIG,			/* 0x00 */
	MPUREG_ACCEL_FF_THR,			/* 0x00 */
	MPUREG_ACCEL_FF_DUR,			/* 0x00 */
	MPUREG_ACCEL_MOT_THR,			/* 0x00 */
	MPUREG_ACCEL_MOT_DUR,			/* 0x00 */
	MPUREG_ACCEL_ZRMOT_THR,			/* 0x00 */
	MPUREG_ACCEL_ZRMOT_DUR,			/* 0x00 */
	MPUREG_FIFO_EN,		/* 0x23 */
	MPUREG_I2C_MST_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV0_ADDR,	/* 0x25 */
	MPUREG_I2C_SLV0_REG,			/* 0x00 */
	MPUREG_I2C_SLV0_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV1_ADDR,	/* 0x28 */
	MPUREG_I2C_SLV1_REG_PASSWORD,			/* 0x00 */
	MPUREG_I2C_SLV1_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV2_ADDR,	/* 0x2B */
	MPUREG_I2C_SLV2_REG,			/* 0x00 */
	MPUREG_I2C_SLV2_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV3_ADDR,	/* 0x2E */
	MPUREG_I2C_SLV3_REG,			/* 0x00 */
	MPUREG_I2C_SLV3_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV4_ADDR,	/* 0x31 */
	MPUREG_I2C_SLV4_REG,			/* 0x00 */
	MPUREG_I2C_SLV4_DO,			/* 0x00 */
	MPUREG_I2C_SLV4_CTRL,			/* 0x00 */
	MPUREG_I2C_SLV4_DI,			/* 0x00 */
	MPUREG_I2C_MST_STATUS,	/* 0x36 */
	MPUREG_INT_PIN_CFG,	/* 0x37 ==> -* INT_CFG */
	MPUREG_INT_ENABLE,	/* 0x38 ==> / */
	MPUREG_DMP_INT_STATUS,	/* 0x39 */
	MPUREG_INT_STATUS,	/* 0x3A */
	MPUREG_ACCEL_XOUT_H,	/* 0x3B */
	MPUREG_ACCEL_XOUT_L,			/* 0x00 */
	MPUREG_ACCEL_YOUT_H,			/* 0x00 */
	MPUREG_ACCEL_YOUT_L,			/* 0x00 */
	MPUREG_ACCEL_ZOUT_H,			/* 0x00 */
	MPUREG_ACCEL_ZOUT_L,			/* 0x00 */
	MPUREG_TEMP_OUT_H,	/* 0x41 */
	MPUREG_TEMP_OUT_L,			/* 0x00 */
	MPUREG_GYRO_XOUT_H,	/* 0x43 */
	MPUREG_GYRO_XOUT_L,			/* 0x00 */
	MPUREG_GYRO_YOUT_H,			/* 0x00 */
	MPUREG_GYRO_YOUT_L,			/* 0x00 */
	MPUREG_GYRO_ZOUT_H,			/* 0x00 */
	MPUREG_GYRO_ZOUT_L,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_00,	/* 0x49 */
	MPUREG_EXT_SLV_SENS_DATA_01,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_02,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_03,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_04,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_05,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_06,	/* 0x4F */
	MPUREG_EXT_SLV_SENS_DATA_07,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_08,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_09,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_10,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_11,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_12,	/* 0x55 */
	MPUREG_EXT_SLV_SENS_DATA_13,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_14,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_15,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_16,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_17,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_18,	/* 0x5B */
	MPUREG_EXT_SLV_SENS_DATA_19,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_20,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_21,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_22,			/* 0x00 */
	MPUREG_EXT_SLV_SENS_DATA_23,			/* 0x00 */
	ACCEL_INTEL_STATUS,	/* 0x61 */
	MPUREG_62_RSVD,			/* 0x00 */
	MPUREG_63_RSVD,			/* 0x00 */
	MPUREG_64_RSVD,			/* 0x00 */
	MPUREG_65_RSVD,			/* 0x00 */
	MPUREG_66_RSVD,			/* 0x00 */
	MPUREG_67_RSVD,			/* 0x00 */
	SIGNAL_PATH_RESET,	/* 0x68 */
	ACCEL_INTEL_CTRL,	/* 0x69 */
	MPUREG_USER_CTRL,	/* 0x6A */
	MPUREG_PWR_MGMT_1,	/* 0x6B */
	MPUREG_PWR_MGMT_2,			/* 0x00 */
	MPUREG_BANK_SEL,	/* 0x6D */
	MPUREG_MEM_START_ADDR,	/* 0x6E */
	MPUREG_MEM_R_W,		/* 0x6F */
	MPUREG_PRGM_STRT_ADDRH,			/* 0x00 */
	MPUREG_PRGM_STRT_ADDRL,			/* 0x00 */
	MPUREG_FIFO_COUNTH,	/* 0x72 */
	MPUREG_FIFO_COUNTL,			/* 0x00 */
	MPUREG_FIFO_R_W,	/* 0x74 */
	MPUREG_WHOAMI,		/* 0x75,117 */

	NUM_OF_MPU_REGISTERS	/* = 0x76,118 */
};

/*==== M_HW MEMORY ====*/
enum MPU_MEMORY_BANKS {
	MEM_RAM_BANK_0 = 0,
	MEM_RAM_BANK_1,
	MEM_RAM_BANK_2,
	MEM_RAM_BANK_3,
	MEM_RAM_BANK_4,
	MEM_RAM_BANK_5,
	MEM_RAM_BANK_6,
	MEM_RAM_BANK_7,
	MEM_RAM_BANK_8,
	MEM_RAM_BANK_9,
	MEM_RAM_BANK_10,
	MEM_RAM_BANK_11,
	MPU_MEM_NUM_RAM_BANKS,
	MPU_MEM_OTP_BANK_0 = 16
};


/*==== M_HW parameters ====*/

#define NUM_REGS            (NUM_OF_MPU_REGISTERS)
#define START_SENS_REGS     (0x3B)
#define NUM_SENS_REGS       (0x60-START_SENS_REGS+1)

/*---- MPU Memory ----*/
#define NUM_BANKS           (MPU_MEM_NUM_RAM_BANKS)
#define BANK_SIZE           (256)
#define MEM_SIZE            (NUM_BANKS*BANK_SIZE)
#define MPU_MEM_BANK_SIZE   (BANK_SIZE)	/*alternative name */

#define FIFO_HW_SIZE        (1024)

#define NUM_EXT_SLAVES      (4)


/*==== BITS FOR M_HW ====*/

/*---- M_HW 'FIFO_EN' register (23) ----*/
#define	BIT_TEMP_OUT				0x80
#define	BIT_GYRO_XOUT				0x40
#define	BIT_GYRO_YOUT				0x20
#define	BIT_GYRO_ZOUT				0x10
#define	BIT_ACCEL				0x08
#define	BIT_SLV_2				0x04
#define	BIT_SLV_1				0x02
#define	BIT_SLV_0				0x01
/*---- M_HW 'CONFIG' register (1A) ----*/
/*NONE                              		0xC0 */
#define	BITS_EXT_SYNC_SET			0x38
#define	BITS_DLPF_CFG				0x07
/*---- M_HW 'GYRO_CONFIG' register (1B) ----*/
/* voluntarily modified label from BITS_FS_SEL to
 * BITS_GYRO_FS_SEL to avoid confusion with MPU
 */
#define BITS_GYRO_FS_SEL            		0x18
/*NONE                              		0x07 */
/*---- M_HW 'ACCEL_CONFIG' register (1C) ----*/
#define BITS_ACCEL_FS_SEL           		0x18
#define BITS_ACCEL_HPF              		0x07
/*---- M_HW 'I2C_MST_CTRL' register (24) ----*/
#define BIT_MULT_MST_DIS            		0x80
#define BIT_WAIT_FOR_ES             		0x40
#define BIT_I2C_MST_VDDIO           		0x20
/*NONE                              		0x10 */
#define BITS_I2C_MST_CLK            		0x0F
/*---- M_HW 'I2C_SLV?_CTRL' register (27,2A,2D,30) ----*/
#define BIT_SLV_ENABLE              		0x80
#define BIT_SLV_BYTE_SW             		0x40
/*NONE                              		0x20 */
#define BIT_SLV_GRP                 		0x10
#define BITS_SLV_LENG               		0x0F
/*---- M_HW 'I2C_SLV4_ADDR' register (31) ----*/
#define BIT_I2C_SLV4_RNW            		0x80
/*---- M_HW 'I2C_SLV4_CTRL' register (34) ----*/
#define BIT_I2C_SLV4_EN             		0x80
#define BIT_SLV4_DONE_INT_EN        		0x40
/*NONE                              		0x3F */
/*---- M_HW 'I2C_MST_STATUS' register (36) ----*/
#define BIT_PASSTHROUGH             		0x80
#define BIT_I2C_SLV4_DONE           		0x40
#define BIT_I2C_LOST_ARB            		0x20
#define BIT_I2C_SLV4_NACK           		0x10
#define BIT_I2C_SLV3_NACK           		0x08
#define BIT_I2C_SLV2_NACK           		0x04
#define BIT_I2C_SLV1_NACK           		0x02
#define BIT_I2C_SLV0_NACK           		0x01
/*---- M_HW 'INT_PIN_CFG' register (37) ----*/
#define	BIT_ACTL				0x80
#define BIT_ACTL_LOW				0x80
#define BIT_ACTL_HIGH				0x00
#define	BIT_OPEN				0x40
#define	BIT_LATCH_INT_EN			0x20
#define	BIT_INT_ANYRD_2CLEAR			0x10
#define	BIT_ACTL_FSYNC				0x08
#define	BIT_FSYNC_INT_EN 			0x04
#define	BIT_BYPASS_EN    			0x02
#define	BIT_CLKOUT_EN				0x01
/*---- M_HW 'INT_ENABLE' register (38) ----*/
#define	BIT_FF_EN				0x80
#define	BIT_MOT_EN				0x40
#define	BIT_ZMOT_EN				0x20
#define	BIT_FIFO_OVERFLOW_EN			0x10
#define BIT_I2C_MST_INT_EN			0x08
#define	BIT_PLL_RDY_EN				0x04
#define BIT_DMP_INT_EN				0x02
#define	BIT_RAW_RDY_EN				0x01
/*---- M_HW 'DMP_INT_STATUS' register (39) ----*/
/*NONE                                          0x80 */
/*NONE                                          0x40 */
#define	BIT_DMP_INT_5				0x20
#define	BIT_DMP_INT_4				0x10
#define	BIT_DMP_INT_3				0x08
#define	BIT_DMP_INT_2 				0x04
#define	BIT_DMP_INT_1				0x02
#define	BIT_DMP_INT_0				0x01
/*---- M_HW 'INT_STATUS' register (3A) ----*/
#define	BIT_FF_INT				0x80
#define	BIT_MOT_INT				0x40
#define	BIT_ZMOT_INT				0x20
#define	BIT_FIFO_OVERFLOW_INT			0x10
#define	BIT_I2C_MST_INT				0x08
#define	BIT_PLL_RDY_INT 			0x04
#define BIT_DMP_INT				0x02
#define	BIT_RAW_DATA_RDY_INT			0x01
/*---- M_HW 'BANK_SEL' register (6D) ----*/
#define	BIT_PRFTCH_EN				0x40
#define	BIT_CFG_USER_BANK			0x20
#define	BITS_MEM_SEL				0x1f
/*---- M_HW 'USER_CTRL' register (6A) ----*/
#define	BIT_DMP_EN				0x80
#define	BIT_FIFO_EN				0x40
#define	BIT_I2C_MST_EN				0x20
#define	BIT_I2C_IF_DIS				0x10
#define	BIT_DMP_RST				0x08
#define	BIT_FIFO_RST				0x04
#define	BIT_I2C_MST_RST				0x02
#define	BIT_SIG_COND_RST			0x01
/*---- M_HW 'PWR_MGMT_1' register (6B) ----*/
#define	BIT_H_RESET				0x80
#define BITS_PWRSEL                 		0x70
#define BIT_WKUP_INT                		0x08
#define BITS_CLKSEL				0x07
/*---- M_HW 'PWR_MGMT_2' register (6C) ----*/
#define BITS_LPA_WAKE_CTRL          		0xC0
#define	BIT_STBY_XA				0x20
#define	BIT_STBY_YA				0x10
#define	BIT_STBY_ZA				0x08
#define	BIT_STBY_XG				0x04
#define	BIT_STBY_YG				0x02
#define	BIT_STBY_ZG				0x01

/* although it has 6, this refers to the gyros */
#define MPU_NUM_AXES     (3)

#define ACCEL_MOT_THR_LSB (32) /* mg */
#define ACCEL_MOT_DUR_LSB (1)
#define ACCEL_ZRMOT_THR_LSB_CONVERSION(mg) ((mg *1000)/255)
#define ACCEL_ZRMOT_DUR_LSB (64)

/*----------------------------------------------------------------------------*/
/*---- Alternative names to take care of conflicts with current mpu3050.h ----*/
/*----------------------------------------------------------------------------*/

/*-- registers --*/
#define MPUREG_DLPF_FS_SYNC        MPUREG_CONFIG		/* 0x1A */

#define MPUREG_PRODUCT_ID          MPUREG_WHOAMI		/* 0x75  HACK!*/
#define MPUREG_PWR_MGM             MPUREG_PWR_MGMT_1		/* 0x6B */
#define MPUREG_FIFO_EN1            MPUREG_FIFO_EN		/* 0x23 */
#define MPUREG_DMP_CFG_1           MPUREG_PRGM_STRT_ADDRH	/* 0x70 */
#define MPUREG_DMP_CFG_2           MPUREG_PRGM_STRT_ADDRL	/* 0x71 */
#define MPUREG_INT_CFG             MPUREG_INT_ENABLE		/* 0x38 */
#define MPUREG_X_OFFS_USRH         MPUREG_XG_OFFS_USRH		/* 0x13 */
#define MPUREG_WHO_AM_I            MPUREG_WHOAMI		/* 0x75 */
#define MPUREG_23_RSVD             MPUREG_EXT_SLV_SENS_DATA_00	/* 0x49 */
#define MPUREG_AUX_SLV_ADDR        MPUREG_I2C_SLV0_ADDR		/* 0x25 */
#define MPUREG_ACCEL_BURST_ADDR    MPUREG_I2C_SLV0_REG		/* 0x26 */

/*-- bits --*/
/* 'USER_CTRL' register */
#define BIT_AUX_IF_EN               BIT_I2C_MST_EN
#define BIT_AUX_RD_LENG             BIT_I2C_MST_EN
#define BIT_IME_IF_RST              BIT_I2C_MST_RST
#define BIT_GYRO_RST                BIT_SIG_COND_RST
/* 'INT_ENABLE' register */
#define BIT_RAW_RDY                 BIT_RAW_DATA_RDY_INT
#define BIT_MPU_RDY_EN              BIT_PLL_RDY_EN
/* 'INT_STATUS' register */
#define BIT_INT_STATUS_FIFO_OVERLOW BIT_FIFO_OVERFLOW_INT



/*---- M_HW Silicon Revisions ----*/
#define MPU_SILICON_REV_A1           1	/* M_HW A1 Device */
#define MPU_SILICON_REV_B1           2	/* M_HW B1 Device */

/*---- structure containing control variables used by MLDL ----*/
/*---- MPU clock source settings ----*/
/*---- MPU filter selections ----*/
enum mpu_filter {
	MPU_FILTER_256HZ_NOLPF2 = 0,
	MPU_FILTER_188HZ,
	MPU_FILTER_98HZ,
	MPU_FILTER_42HZ,
	MPU_FILTER_20HZ,
	MPU_FILTER_10HZ,
	MPU_FILTER_5HZ,
	MPU_FILTER_2100HZ_NOLPF,
	NUM_MPU_FILTER
};

enum mpu_fullscale {
	MPU_FS_250DPS = 0,
	MPU_FS_500DPS,
	MPU_FS_1000DPS,
	MPU_FS_2000DPS,
	NUM_MPU_FS
};

enum mpu_clock_sel {
	MPU_CLK_SEL_INTERNAL = 0,
	MPU_CLK_SEL_PLLGYROX,
	MPU_CLK_SEL_PLLGYROY,
	MPU_CLK_SEL_PLLGYROZ,
	MPU_CLK_SEL_PLLEXT32K,
	MPU_CLK_SEL_PLLEXT19M,
	MPU_CLK_SEL_RESERVED,
	MPU_CLK_SEL_STOP,
	NUM_CLK_SEL
};

enum mpu_ext_sync {
	MPU_EXT_SYNC_NONE = 0,
	MPU_EXT_SYNC_TEMP,
	MPU_EXT_SYNC_GYROX,
	MPU_EXT_SYNC_GYROY,
	MPU_EXT_SYNC_GYROZ,
	MPU_EXT_SYNC_ACCELX,
	MPU_EXT_SYNC_ACCELY,
	MPU_EXT_SYNC_ACCELZ,
	NUM_MPU_EXT_SYNC
};

#define DLPF_FS_SYNC_VALUE(ext_sync, full_scale, lpf) \
    ((ext_sync << 5) | (full_scale << 3) | lpf)

#endif				/* __IMU6000_H_ */
