#ifndef _BP_AUTO_H
#define _BP_AUTO_H
#include <linux/miscdevice.h>
#include <linux/wakelock.h>

struct bp_private_data;

#define BP_UNKNOW_DATA	-1
#define BP_OFF		0
#define BP_ON		1
#define BP_SUSPEND	2
#define BP_WAKE		3

#define BP_IOCTL_BASE 'm'

#define BP_IOCTL_RESET 		_IOW(BP_IOCTL_BASE, 0x00, int)
#define BP_IOCTL_POWON 		_IOW(BP_IOCTL_BASE, 0x01, int)
#define BP_IOCTL_POWOFF 	_IOW(BP_IOCTL_BASE, 0x02, int)
#define BP_IOCTL_WRITE_STATUS 	_IOW(BP_IOCTL_BASE, 0x03, int)
#define BP_IOCTL_GET_STATUS 	_IOR(BP_IOCTL_BASE, 0x04, int)
#define BP_IOCTL_SET_PVID 	_IOW(BP_IOCTL_BASE, 0x05, int)
#define BP_IOCTL_GET_BPID 	_IOR(BP_IOCTL_BASE, 0x06, int)

enum bp_id{
	BP_ID_INVALID = 0,
		
	BP_ID_MT6229,
	BP_ID_MU509,
	BP_ID_MI700,
	BP_ID_MW100,
	BP_ID_TD8801,
	
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

	int bp_power;
	int bp_en;
	int bp_reset;
	int ap_ready;
	int bp_ready;
	int ap_wakeup_bp;
	int bp_wakeup_ap;	
	int bp_usb_en;
	int bp_uart_en;
	
	int gpio_valid;
};


struct bp_operate {
	char *name;
	int bp_id;
	int bp_bus;
	
	int bp_pid;	
	int bp_vid;
	int bp_power;
	int bp_en;
	int bp_reset;
	int ap_ready;
	int bp_ready;
	int ap_wakeup_bp;
	int bp_wakeup_ap;	
	int bp_usb_en;
	int bp_uart_en;
	int trig;
	
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
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct 	early_suspend early_suspend;
#endif
};

extern int bp_register_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void));


extern int bp_unregister_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void));


#endif

