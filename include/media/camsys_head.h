/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RKCAMSYS_HEAR_H__
#define __RKCAMSYS_HEAR_H__

#include <linux/ioctl.h>

/*
*               C A M S Y S   H E A D   F I L E   V E R S I O N 
*
*v0.0.1:
*        1) test version;
*v0.0.2:
*        1) modify camsys_irqcnnt_t;
*v0.0.3:
*        1) add support cif phy for marvin;
*v0.0.4:
*        1) add clock information in struct camsys_devio_name_s;
*v0.0.5:
*        1) add pwren control
*v0.6.0:
*        1) add support mipi phy configuration;
*        2) add support io domain and mclk driver strength configuration;
*v0.7.0:
		 1) add flash_trigger_out control
*v0.8.0:
		 1) support isp iommu
*v0.9.0:
         1) add dev_name in struct camsys_devio_name_s;
*v0.a.0:
         1) support external flash IC
*v0.b.0:
	1) add CamSys_SensorBit0_CifBit4 in enum camsys_cifio_e.
*v0.c.0:
	1) support sensor powerup sequence configurable.
*v0.d.0:
	1) powerup sequence type moved to common_head.h.
*v0.e.0:
	1) add fs_id, fe_id and some reserved bytes in struct camsys_irqsta_s.
*v0.f.0:
	1) add pid in struct camsys_irqsta_s.
*v1.0.0:
	1) add enum camsys_mipiphy_dir_e.
*/

#define CAMSYS_HEAD_VERSION           KERNEL_VERSION(1, 0x0, 0)

#define CAMSYS_MARVIN_DEVNAME         "camsys_marvin"
#define CAMSYS_CIF0_DEVNAME           "camsys_cif0"
#define CAMSYS_CIF1_DEVNAME           "camsys_cif1"

#define CAMSYS_NAME_LEN               32

#define CAMSYS_DEVID_MARVIN           0x00000001
#define CAMSYS_DEVID_CIF_0            0x00000002
#define CAMSYS_DEVID_CIF_1            0x00000004
#define CAMSYS_DEVID_INTERNAL         0x000000FF

#define CAMSYS_DEVID_SENSOR_1A        0x01000000
#define CAMSYS_DEVID_SENSOR_1B        0x02000000
#define CAMSYS_DEVID_SENSOR_2         0x04000000
#define CAMSYS_DEVID_EXTERNAL         0xFF000000
#define CAMSYS_DEVID_EXTERNAL_NUM     8

#define CAMSYS_DEVCFG_FLASHLIGHT      0x00000001
#define CAMSYS_DEVCFG_PREFLASHLIGHT   0x00000002
#define CAMSYS_DEVCFG_SHUTTER         0x00000004

typedef struct camsys_irqsta_s {
    unsigned int ris;                 //Raw interrupt status
    unsigned int mis;                 //Masked interrupt status
	unsigned int fs_id; // frame number from Frame Start (FS) short packet
	unsigned int fe_id; // frame number from Frame End (FE) short packet
	int pid;
	unsigned int reserved[3];
} camsys_irqsta_t;

typedef struct camsys_irqcnnt_s {
    int          pid;
    unsigned int timeout;             //us

    unsigned int mis;
	unsigned int icr;
} camsys_irqcnnt_t;

typedef enum camsys_mmap_type_e {     //this type can be filled in mmap offset argument      
    CamSys_Mmap_RegisterMem,
    CamSys_Mmap_I2cMem,

    CamSys_Mmap_End
} camsys_mmap_type_t;

typedef struct camsys_querymem_s {
    camsys_mmap_type_t      mem_type;
    unsigned long           mem_offset;

    unsigned int            mem_size;
} camsys_querymem_t;

typedef struct camsys_i2c_info_s {
    unsigned char     bus_num;
    unsigned short    slave_addr;
    unsigned int      reg_addr;       //i2c device register address
    unsigned int      reg_size;       //register address size
    unsigned int      val;
    unsigned int      val_size;       //register value size
    unsigned int      i2cbuf_directly;
    unsigned int      i2cbuf_bytes;   
    unsigned int      speed;          //100000 == 100KHz
} camsys_i2c_info_t;

typedef struct camsys_reginfo_s {
    unsigned int      dev_mask;
    unsigned int      reg_offset;
    unsigned int      val;
} camsys_reginfo_t;

typedef enum camsys_sysctrl_ops_e {

    CamSys_Vdd_Start_Tag,
    CamSys_Avdd,
    CamSys_Dovdd,
    CamSys_Dvdd,
    CamSys_Afvdd,
    CamSys_Vdd_End_Tag,

    CamSys_Gpio_Start_Tag,    
    CamSys_PwrDn,
    CamSys_Rst,
    CamSys_AfPwr,
    CamSys_AfPwrDn,
    CamSys_PwrEn,    
    CamSys_Gpio_End_Tag,

    CamSys_Clk_Start_Tag,    
    CamSys_ClkIn,
    CamSys_Clk_End_Tag,

    CamSys_Phy_Start_Tag,    
    CamSys_Phy,
    CamSys_Phy_End_Tag,
    CamSys_Flash_Trigger_Start_Tag, 
    CamSys_Flash_Trigger,
    CamSys_Flash_Trigger_End_Tag,
    CamSys_IOMMU
    
} camsys_sysctrl_ops_t;

typedef struct camsys_regulator_info_s {
    unsigned char     name[CAMSYS_NAME_LEN];
    int               min_uv;
    int               max_uv;
} camsys_regulator_info_t;

typedef struct camsys_gpio_info_s {
    unsigned char     name[CAMSYS_NAME_LEN];
    unsigned int      active;
} camsys_gpio_info_t;

typedef struct camsys_iommu_s{
    int client_fd;
    int map_fd;
    unsigned long linear_addr;
    unsigned long len;
}camsys_iommu_t;

typedef struct camsys_sysctrl_s {
    unsigned int              dev_mask;
    camsys_sysctrl_ops_t      ops;
    unsigned int              on;

    unsigned int              rev[20];
} camsys_sysctrl_t;

typedef struct camsys_flash_info_s {
    unsigned char     fl_drv_name[CAMSYS_NAME_LEN];
    camsys_gpio_info_t        fl; //fl_trig
    camsys_gpio_info_t        fl_en;
} camsys_flash_info_t;

enum camsys_mipiphy_dir_e {
	CamSys_Mipiphy_Rx = 0,
	CamSys_Mipiphy_Tx = 1,
};

typedef struct camsys_mipiphy_s {
    unsigned int                data_en_bit;        // data lane enable bit;
    unsigned int                bit_rate;           // Mbps/lane
    unsigned int                phy_index;          // phy0,phy1
	enum camsys_mipiphy_dir_e   dir;            // direction
} camsys_mipiphy_t;

typedef enum camsys_fmt_e {
    CamSys_Fmt_Yuv420_8b = 0x18,
    CamSys_Fmt_Yuv420_10b = 0x19,
    CamSys_Fmt_LegacyYuv420_8b = 0x19,

    CamSys_Fmt_Yuv422_8b = 0x1e,
    CamSys_Fmt_Yuv422_10b = 0x1f,

    CamSys_Fmt_Raw_6b = 0x28,
    CamSys_Fmt_Raw_7b = 0x29,
    CamSys_Fmt_Raw_8b = 0x2a,
    CamSys_Fmt_Raw_10b = 0x2b,
    CamSys_Fmt_Raw_12b = 0x2c,
    CamSys_Fmt_Raw_14b = 0x2d,
} camsys_fmt_t;

typedef enum camsys_cifio_e {
    CamSys_SensorBit0_CifBit0 = 0x00,
    CamSys_SensorBit0_CifBit2 = 0x01,
    CamSys_SensorBit0_CifBit4 = 0x02,
} camsys_cifio_t;

typedef struct camsys_cifphy_s {
    unsigned int                cif_num; 
    camsys_fmt_t                fmt;
    camsys_cifio_t              cifio;
    
} camsys_cifphy_t;

typedef enum camsys_phy_type_e {
    CamSys_Phy_Mipi,
    CamSys_Phy_Cif,

    CamSys_Phy_end
} camsys_phy_type_t;

typedef struct camsys_extdev_phy_s {
    camsys_phy_type_t           type;
    union {
        camsys_mipiphy_t            mipi;
        camsys_cifphy_t             cif;
    } info;
    
} camsys_extdev_phy_t;

typedef struct camsys_extdev_clk_s {
    unsigned int in_rate;
    unsigned int driver_strength;             //0 - 3
} camsys_extdev_clk_t;

typedef struct camsys_devio_name_s {
    unsigned char               dev_name[CAMSYS_NAME_LEN];
    unsigned int                dev_id;
    
    camsys_regulator_info_t     avdd;         // sensor avdd power regulator name
    camsys_regulator_info_t     dovdd;        // sensor dovdd power regulator name
    camsys_regulator_info_t     dvdd;         // sensor dvdd power regulator name    "NC" describe no regulator
    camsys_regulator_info_t     afvdd; 

    camsys_gpio_info_t          pwrdn;        // standby gpio name
    camsys_gpio_info_t          rst;          // hard reset gpio name 
    camsys_gpio_info_t          afpwr;        // auto focus vcm driver ic power gpio name
    camsys_gpio_info_t          afpwrdn;      // auto focus vcm driver ic standby gpio 
    camsys_gpio_info_t          pwren;        // power enable gpio name  


    camsys_flash_info_t         fl;

    camsys_extdev_phy_t         phy;
    camsys_extdev_clk_t         clk;
    
    unsigned int                dev_cfg;     // function bit mask configuration 
} camsys_devio_name_t;

typedef struct camsys_version_s {
    unsigned int drv_ver;
    unsigned int head_ver;
} camsys_version_t;

/*
 *	I O C T L   C O D E S   F O R    R O C K C H I P S   C A M S Y S   D E V I C E S
 *
 */
#define CAMSYS_IOC_MAGIC  'M'
#define CAMSYS_IOC_MAXNR  14

#define CAMSYS_VERCHK            _IOR(CAMSYS_IOC_MAGIC,  0, camsys_version_t)

#define CAMSYS_I2CRD             _IOWR(CAMSYS_IOC_MAGIC,  1, camsys_i2c_info_t)
#define CAMSYS_I2CWR             _IOW(CAMSYS_IOC_MAGIC,  2, camsys_i2c_info_t)

#define CAMSYS_SYSCTRL           _IOW(CAMSYS_IOC_MAGIC,  3, camsys_sysctrl_t) 
#define CAMSYS_REGRD             _IOWR(CAMSYS_IOC_MAGIC,  4, camsys_reginfo_t)
#define CAMSYS_REGWR             _IOW(CAMSYS_IOC_MAGIC,  5, camsys_reginfo_t)
#define CAMSYS_REGISTER_DEVIO    _IOW(CAMSYS_IOC_MAGIC,  6, camsys_devio_name_t)
#define CAMSYS_DEREGISTER_DEVIO  _IOW(CAMSYS_IOC_MAGIC,  7, unsigned int)
#define CAMSYS_IRQCONNECT        _IOW(CAMSYS_IOC_MAGIC,  8, camsys_irqcnnt_t)
#define CAMSYS_IRQWAIT           _IOR(CAMSYS_IOC_MAGIC,  9, camsys_irqsta_t)
#define CAMSYS_IRQDISCONNECT     _IOW(CAMSYS_IOC_MAGIC,   10, camsys_irqcnnt_t)

#define CAMSYS_QUREYMEM          _IOR(CAMSYS_IOC_MAGIC,  11, camsys_querymem_t)
#define CAMSYS_QUREYIOMMU        _IOW(CAMSYS_IOC_MAGIC,  12, int)
#endif
