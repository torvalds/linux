/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BP_AUTO_H
#define _BP_AUTO_H
#include <linux/miscdevice.h>
#include <linux/wakelock.h>

struct bp_private_data;
#define BP_DEV_NAME	"voice_modem"
#define BP_UNKNOW_DATA	-1
#define BP_OFF		0
#define BP_ON		1
#define BP_SUSPEND	2
#define BP_WAKE		3

#define BP_IOCTL_BASE 0x1a

#define BP_IOCTL_RESET 		_IOW(BP_IOCTL_BASE, 0x01, int)
#define BP_IOCTL_POWOFF 	_IOW(BP_IOCTL_BASE, 0x02, int)
#define BP_IOCTL_POWON 		_IOW(BP_IOCTL_BASE, 0x03, int)

#define BP_IOCTL_WRITE_STATUS 	_IOW(BP_IOCTL_BASE, 0x04, int)
#define BP_IOCTL_GET_STATUS 	_IOR(BP_IOCTL_BASE, 0x05, int)
#define BP_IOCTL_SET_PVID 	_IOW(BP_IOCTL_BASE, 0x06, int)
#define BP_IOCTL_GET_BPID 	_IOR(BP_IOCTL_BASE, 0x07, int)
#define BP_IOCTL_GET_IMEI 	_IOR(BP_IOCTL_BASE, 0x08, int)

enum bp_id{
	BP_ID_INVALID = 0,//no bp
		
	BP_ID_MT6229,	//1 USI MT6229 WCDMA
	BP_ID_MU509,	//2 huawei MU509 WCDMA
	BP_ID_MI700,	//3 thinkwill MI700 WCDMA
	BP_ID_MW100,	//4 thinkwill MW100 WCDMA
	BP_ID_TD8801,	//5 spreadtrum SC8803 TD-SCDMA
	BP_ID_SC6610,	//6 spreadtrum SC6610 GSM
	BP_ID_M51,	//7 RDA GSM
	BP_ID_MT6250,   //8 ZINN M50  EDGE
	BP_ID_C66A,     //9 ZHIGUAN C66A GSM
	BP_ID_SEW290,   //10 SCV SEW290 WCDMA
	BP_ID_U5501,    //11 LONGSUNG U5501 WCDMA
	BP_ID_U7501,    //12 LONGSUNG U7501 WCDMA/HSPA+
	BP_ID_AW706,    //13 ANICARE AW706 EDGE
	BP_ID_A85XX,    //14 LONGSUNG A8520/A8530 GSM
    BP_ID_E1230S,    //15 huawei E1230S

	BP_ID_NUM,  
};



enum bp_bus_type{
	BP_BUS_TYPE_INVALID = 0,
		
	BP_BUS_TYPE_UART,
	BP_BUS_TYPE_SPI,
	BP_BUS_TYPE_USB,
	BP_BUS_TYPE_SDIO,
	BP_BUS_TYPE_USB_UART,
	BP_BUS_TYPE_SPI_UART,
	BP_BUS_TYPE_SDIO_UART,
	
	BP_BUS_TYPE_NUM_ID,
};

struct bp_platform_data {	
	int board_id;
	int bp_id;
	int (*init_platform_hw)(void);		
	int (*exit_platform_hw)(void);	
	int (*get_bp_id)(void);
	int bp_power;
	int bp_en;
	int bp_reset;
	int ap_ready;
	int bp_ready;
	int ap_wakeup_bp;
	int bp_wakeup_ap;
	int bp_assert;
	int bp_usb_en;
	int bp_uart_en;
	
	int gpio_valid;
};


struct bp_operate {
	char *name;	//bp name can be null
	int bp_id;	//bp id the value must be one of enum bp_id
	int bp_bus;	// bp bus the value must be one of enum bp_bus_type
	
	int bp_pid;	// the pid of usb device if used usb else the value is BP_UNKNOW_DATA
	int bp_vid;	// the vid of usb device if used usb else the value is BP_UNKNOW_DATA
	int bp_power;//bp power if used GPIO value else the  is BP_UNKNOW_DATA
	int bp_en;//bp power key if used GPIO value else the  is BP_UNKNOW_DATA
	int bp_reset;//bo reset if used GPIO value else the  is BP_UNKNOW_DATA
	int ap_ready;//bp ready  if used GPIO value else the  is BP_UNKNOW_DATA
	int bp_ready;// bp ready  if used GPIO value else the  is BP_UNKNOW_DATA
	int ap_wakeup_bp; //ap wakeup bp  if used GPIO value else the  is BP_UNKNOW_DATA
	int bp_wakeup_ap;// bp wakeup ap  if used GPIO value else the  is BP_UNKNOW_DATA
	int bp_assert;
	int bp_usb_en;//not used
	int bp_uart_en;//not used
	int trig;//if 1:used board gpio define else used bp driver
	int irq;

	int (*active)(struct bp_private_data *bp, int enable);
	int (*init)(struct bp_private_data *bp);
	int (*reset)(struct bp_private_data *bp);
	int (*ap_wake_bp)(struct bp_private_data *bp, int wake);
	int (*bp_wake_ap)(struct bp_private_data *bp);
	int (*shutdown)(struct bp_private_data *bp);
	int (*read_status)(struct bp_private_data *bp);
	int (*write_status)(struct bp_private_data *bp);
	int (*suspend)(struct bp_private_data *bp);
	int (*resume)(struct bp_private_data *bp);		
	char *misc_name;
	struct miscdevice *private_miscdev;
	
};


struct bp_private_data {	
	struct device *dev;
	int status;
	int suspend_status;
	struct wake_lock bp_wakelock;
	struct delayed_work wakeup_work;	/*report second event*/
	struct bp_platform_data *pdata;
	struct bp_operate *ops; 
	struct file_operations fops;
	struct miscdevice miscdev;
	struct file_operations id_fops;
	struct miscdevice id_miscdev;

};

extern int bp_register_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void));


extern int bp_unregister_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void));


#endif

