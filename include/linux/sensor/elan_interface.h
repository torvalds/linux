#ifndef __ELAN_TP_H__
#define __ELAN_TP_H__

#include <linux/input.h>

#define ELAN_LS_6801 "elan-epl6801"
#define ELAN_LS_6803 "elan-epl6803"
#define ELAN_LS_6804 "elan-epl6804"
#define ELAN_LS_6814 "elan_epl6814"
#define ELAN_LS_6881 "elan-epl6881"

#define ELAN_8232_I2C_NAME "elan-touch"
#define ELAN_FS_8713 "elan-forcesensor" 
#define ELAN_EKTF2000_NAME "elan-ektf-touchpad"
#define ELAN_OFN_8713 "elan-ofn-sensor"
#define ELAN_TP_1058 "elan-ekt1058-touchpad"
#define ELAN_TP_2154 "elan-ekt2154-touchpad"
#define ELAN_USB_FS "elan-usb-fs"
#define ELAN_USB_COMBO "elan-usb-combo"

#define ELAN_IOCTL_MAGIC 'c'
#define ELAN_EKT8713_IOCTL_GET_AFLAG _IOR(ELAN_IOCTL_MAGIC, 1, int *)
#define ELAN_EKT8713_IOCTL_GET_OFLAG _IOR(ELAN_IOCTL_MAGIC, 2, int *)
#define ELAN_EKT8713_IOCTL_ENABLE_AFLAG _IOW(ELAN_IOCTL_MAGIC, 3, int *)
#define ELAN_EKT8713_IOCTL_ENABLE_OFLAG _IOW(ELAN_IOCTL_MAGIC, 4, int *)
#define ELAN_EKT8713_IOCTL_GETDATA _IOR(ELAN_IOCTL_MAGIC, 5, int *)

#define ELAN_EPL6801_IOCTL_GET_PFLAG _IOR(ELAN_IOCTL_MAGIC, 1, int *)
#define ELAN_EPL6801_IOCTL_GET_LFLAG _IOR(ELAN_IOCTL_MAGIC, 2, int *)
#define ELAN_EPL6801_IOCTL_ENABLE_PFLAG _IOW(ELAN_IOCTL_MAGIC, 3, int *)
#define ELAN_EPL6801_IOCTL_ENABLE_LFLAG _IOW(ELAN_IOCTL_MAGIC, 4, int *)
#define ELAN_EPL6801_IOCTL_GETDATA _IOR(ELAN_IOCTL_MAGIC, 5, int *)

#define ELAN_EPL6800_IOCTL_GET_PFLAG _IOR(ELAN_IOCTL_MAGIC, 1, int *)
#define ELAN_EPL6800_IOCTL_GET_LFLAG _IOR(ELAN_IOCTL_MAGIC, 2, int *)
#define ELAN_EPL6800_IOCTL_ENABLE_PFLAG _IOW(ELAN_IOCTL_MAGIC, 3, int *)
#define ELAN_EPL6800_IOCTL_ENABLE_LFLAG _IOW(ELAN_IOCTL_MAGIC, 4, int *)
#define ELAN_EPL6800_IOCTL_GETDATA _IOR(ELAN_IOCTL_MAGIC, 5, int *)

/*for epl6801 command */
#define REG_0			0X00
#define REG_1			0X01
#define REG_2			0X02
#define REG_3			0X03
#define REG_4			0X04
#define REG_5			0X05
#define REG_6			0X06
#define REG_7			0X07
#define REG_8			0X08
#define REG_9			0X09
#define REG_10			0X0A
#define REG_11			0X0B
#define REG_12			0X0C
#define REG_13			0X0D
#define REG_14			0X0E
#define REG_15			0X0F
#define REG_16			0X10
#define REG_17			0X11
#define REG_18			0X12
#define REG_19			0X13
#define REG_20			0X14
#define REG_21			0X15

#define W_SINGLE_BYTE		0X00
#define W_TWO_BYTE		0X01
#define W_THREE_BYTE		0X02
#define W_FOUR_BYTE		0X03
#define W_FIVE_BYTE		0X04
#define W_SIX_BYTE		0X05
#define W_SEVEN_BYTE		0X06
#define W_EIGHT_BYTE		0X07

#define R_SINGLE_BYTE		0X00
#define R_TWO_BYTE		0X01
#define R_THREE_BYTE		0X02
#define R_FOUR_BYTE		0X03
#define R_FIVE_BYTE		0X04
#define R_SIX_BYTE		0X05
#define R_SEVEN_BYTE		0X06
#define R_EIGHT_BYTE		0X07

#define EPL_SENSING_1_TIME	(0 << 5) 
#define EPL_SENSING_2_TIME	(1 << 5)
#define EPL_SENSING_4_TIME	(2 << 5)
#define EPL_SENSING_8_TIME	(3 << 5)
#define EPL_SENSING_16_TIME	(4 << 5)
#define EPL_SENSING_32_TIME	(5 << 5)
#define EPL_SENSING_64_TIME	(6 << 5)
#define EPL_SENSING_128_TIME	(7 << 5)
#define EPL_C_SENSING_MODE	(0 << 4)
#define EPL_S_SENSING_MODE	(1 << 4)
#define EPL_ALS_MODE		(0 << 2)
#define EPL_PS_MODE		(1 << 2)
#define EPL_TEMP_MODE 		(2 << 2)
#define EPL_H_GAIN		(0)
#define EPL_M_GAIN		(1)
#define EPL_L_GAIN		(3)
#define EPL_AUTO_GAIN		(2)


#define EPL_8BIT_ADC		0
#define EPL_10BIT_ADC		1
#define EPL_12BIT_ADC		2
#define EPL_14BIT_ADC		3


#define EPL_C_RESET				0x00
#define EPL_C_START_RUN		0x04
#define EPL_C_P_UP				0x04
#define EPL_C_P_DOWN			0x06
#define EPL_DATA_LOCK_ONLY		0x01
#define EPL_DATA_LOCK			0x05
#define EPL_DATA_UNLOCK		0x04

#define EPL_GO_MID				0x3E
#define EPL_GO_LOW				0x3E


#define EPL_INT_BINARY			0
#define EPL_INT_DISABLE			2
#define EPL_INT_ACTIVE_LOW		3
#define EPL_INT_FRAME_ENABLE		4

#define EPL_PST_1_TIME		(0 << 2)
#define EPL_PST_4_TIME		(1 << 2)
#define EPL_PST_8_TIME		(2 << 2)
#define EPL_PST_16_TIME		(3 << 2)






struct elan_forcesensor_platform_data 
{
	uint16_t version;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	int intr_gpio;
	int (*power)(int on);
};

struct elan_touchpad_platform_Data
{
	int trackid_min;
	int trackid_max;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	int intr_gpio;
};


struct elan_epl_platform_data 
{
	int intr_gpio;
	int (*power)(int on);
	//int (*power)(int, uint8_t);
};


void elan_fs_set_display_resolution(int width,int height);
void elan_fs_do_forcesensor(int mode,int x, int y, int z,int ck, struct input_dev *input);
void elan_fs_HandleEvent(int index,char mode,int dx,int dy,int dz,int ck,struct input_dev *input,int gsensorflag,int *changemodeflag,int *zaxis_forcevalue,int *zaxis_forcewindow_flag);

void elan_tp_set_touchpad_resource(long width,long height);
void elan_tp_do_touchpad(struct input_dev *input,struct input_dev *input_touch,long currentfingercnt,long* x,long* y);
void elan_tp_virtualwindow_size(long lefttop_x,long lefttop_y,long rightbottom_x,long rightbottom_y);
long elan_tp_get_virtualwindow_zoom(void);
void elan_tp_clear_virtualwindow_zoom(void);
long elan_tp_EnableTriggerGesture(void);
long elan_tp_get_changemode(void);
void elan_tp_clear_changemode(void);
void elan_tp_initialize(void);
void elan_tp_setmode(long mode);
long elan_tp_getmode(void);
#endif
