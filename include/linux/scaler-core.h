#ifndef __SCALER_CORE_H__
#define __SCALER_CORE_H__
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/list.h>


struct display_edid {
	char *data;
	char *ext_data;
};

enum scaler_output_type {
	SCALER_OUT_INVALID = 0,
	SCALER_OUT_LVDS,
	SCALER_OUT_VGA,
	SCALER_OUT_RGB,
	SCALER_OUT_HDMI,
	SCALER_OUT_DP,
	SCALER_OUT_NUMS,
};

enum scaler_input_type {
	SCALER_IN_INVALID = 0,
	SCALER_IN_VGA,
	SCALER_IN_RGB,
	SCALER_IN_HDMI,
	SCALER_IN_DP,
	SCALER_IN_DVI,
	SCALER_IN_YPBPR,
	SCALER_IN_YCBCR,
	SCALER_IN_MYDP,
	SCALER_IN_IDP,
	SCALER_IN_NUMS,
};

enum scaler_bus_type {
	SCALER_BUS_TYPE_INVALID = 0,
	SCALER_BUS_TYPE_UART,
	SCALER_BUS_TYPE_I2C,
	SCALER_BUS_TYPE_SPI,
	SCALER_BUS_TYPE_NUMS,
};

/*
 * the function of scaler, for example convertor or switch 
*/
enum scaler_function_type {
	SCALER_FUNC_INVALID = 0,
	SCALER_FUNC_CONVERTOR,   //转换器
	SCALER_FUNC_SWITCH,      //切换开关多选一输出 
	SCALER_FUNC_FULL,        //全功能
	SCALER_FUNC_NUMS,
};

struct scaler_output_port {
	int id;
	int max_hres;
	int max_vres;
	int freq;
	int led_gpio;   //working led
	enum scaler_output_type type;
	struct list_head next; 
};

struct scaler_input_port {
	int id;
	int max_hres;
	int max_vres;
	int freq;       //HZ
	int led_gpio;   //working led
	enum scaler_input_type type;
	struct list_head next; 
};

struct scaler_chip_dev {
	char id;
	char name[I2C_NAME_SIZE];
	struct i2c_client *client;
	
	enum scaler_function_type func_type;

	int int_gpio;
	int reset_gpio;
	int power_gpio;
	int status_gpio;
	char reg_size;  //8bit = 1, 16bit = 2, 24bit = 3,32bit = 4.

	struct list_head iports; //config all support input type by make menuconfig
	struct list_head oports; //config all support output type by make menuconfig
	enum scaler_input_type cur_in_type;
	int cur_inport_id;
	enum scaler_output_type cur_out_type;
	int cur_outport_id;

	//init hardware(gpio etc.)
	int (*init_hw)(void);
	int (*exit_hw)(void);

	//enable chip to process image
	void (*start)(void);
	//disable chip to process image
	void (*stop)(void);
	void (*reset)(char active);
	void (*suspend)(void);
	void (*resume)(void);

	//
	int (*read)(unsigned short reg, int bytes, void *dest);
	int (*write)(unsigned short reg, int bytes, void *src);
	int (*parse_cmd)(unsigned int cmd, unsigned long arg);
	int (*update_firmware)(void *data);

	//scaler chip dev list
	struct list_head next;
};

struct scaler_platform_data {
	int int_gpio;
	int reset_gpio;
	int power_gpio;
	int status_gpio; //check chip if work on lower power mode or normal mode.

	char *firmware; 
	//function type
	enum scaler_function_type func_type;

	//config in and out 
	struct scaler_input_port  *iports;
	struct scaler_output_port *oports;
	int iport_size;
	int oport_size;

	int (*init_hw)(void);
	int (*exit_hw)(void);
};

struct scaler_device {
	struct class *class;
	struct device *dev;
	struct cdev *cdev;
	dev_t devno;

	struct display_edid edid;

	struct list_head chips;
};

struct scaler_chip_dev *alloc_scaler_chip(void);
//free chip memory and port memory
void free_scaler_chip(struct scaler_chip_dev *chip);
int init_scaler_chip(struct scaler_chip_dev *chip, struct scaler_platform_data *pdata);
//
int register_scaler_chip(struct scaler_chip_dev *chip);
int unregister_scaler_chip(struct scaler_chip_dev *chip);

#define SCALER_IOCTL_MAGIC 's'
#define SCALER_IOCTL_POWER _IOW(SCALER_IOCTL_MAGIC, 0x00, char)
#define SCALER_IOCTL_GET_CUR_INPUT _IOR(SCALER_IOCTL_MAGIC, 0x01, int)
#define SCALER_IOCTL_SET_CUR_INPUT _IOW(SCALER_IOCTL_MAGIC, 0x02, int)

#endif 
