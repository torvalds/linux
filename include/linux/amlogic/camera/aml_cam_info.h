#ifndef __AML_CAM_DEV__
#define __AML_CAM_DEV__
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/i2c-aml.h>
#include <mach/gpio.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <linux/amlogic/tvin/tvin.h>
#include <linux/amlogic/camera/flashlight.h>

#define FRONT_CAM	0
#define BACK_CAM	1

typedef enum resolution_size {
	SIZE_NULL = 0,
	SIZE_176X144,	//4:3
	SIZE_320X240,	//4:3
	SIZE_352X288,   //4:3
	SIZE_640X480,	//0.3M	4:3
	SIZE_720X405,	//0.3M	16:9
	SIZE_800X600,	//0.5M	4:3
	SIZE_960X540,	//0.5M	16:9
	SIZE_1024X576,	//0.6M	16:9
	SIZE_960X720,	//0.7M	4:3
	SIZE_1024X768,	//0.8M	4:3
	SIZE_1280X720,	//0.9M	16:9
	SIZE_1152X864,	//1M	4:3
	SIZE_1366X768,	//1M	16:9
	SIZE_1280X960,	//1.2M	4:3
	SIZE_1400X1050,	//1.5M	4:3
	SIZE_1600X900,	//1.5M	16:9
	SIZE_1600X1200,	//2M	4:3
	SIZE_1920X1080,	//2M	16:9
	SIZE_1792X1344,	//2.4M	4:3
	SIZE_2048X1152,	//2.4M	16:9
	SIZE_2048X1536,	//3.2M	4:3
	SIZE_2304X1728,	//4M	4:3
	SIZE_2560X1440,	//4M	16:9
	SIZE_2592X1944,	//5M	4:3
	SIZE_3072X1728,	//5M	16:9
	SIZE_2816X2112,	//6M	4:3
	SIZE_3264X1836, //6m    16:9 
	SIZE_3072X2304,	//7M	4:3
	SIZE_3200X2400,	//7.5M	4:3
	SIZE_3264X2448,	//8M	4:3
	SIZE_3840X2160,	//8M	16:9
	SIZE_3456X2592,	//9M	4:3
	SIZE_3600X2700,	//9.5M	4:3
	SIZE_4096X2304,	//9.5M	16:9
	SIZE_3672X2754,	//10M	4:3
	SIZE_3840X2880,	//11M	4:3
	SIZE_4000X3000,	//12M	4:3
	SIZE_4608X2592,	//12M	16:9
	SIZE_4096X3072,	//12.5M	4:3
	SIZE_4800X3200,	//15M	4:3
	SIZE_5120X2880,	//15M	16:9
	SIZE_5120X3840,	//20M	4:3
	SIZE_6400X4800,	//30M	4:3
} resolution_size_t;

typedef int(*aml_cam_probe_fun_t)(struct i2c_adapter *);

typedef struct {
	struct list_head info_entry;
	const char* name;
	unsigned i2c_bus_num;
	unsigned pwdn_act;
	unsigned front_back; /* front is 0, back is 1 */
	unsigned m_flip;
	unsigned v_flip;
	unsigned flash;
	unsigned auto_focus;
	unsigned i2c_addr;
	const char* motor_driver;
	const char* resolution;
	const char* version;
	unsigned mclk;
	unsigned flash_support;
	unsigned flash_ctrl_level;
	unsigned torch_support;
	unsigned torch_ctrl_level;
	unsigned vcm_mode;
	unsigned spread_spectrum;
	bt_path_t bt_path;
	cam_interface_t         interface;
	clk_channel_t           clk_channel;
	gpio_t pwdn_pin;
	gpio_t rst_pin;
	gpio_t flash_ctrl_pin;
	gpio_t torch_ctrl_pin;
	resolution_size_t max_cap_size;
	tvin_color_fmt_t bayer_fmt;
	const char* config;
}aml_cam_info_t;

typedef struct aml_camera_i2c_fig_s{
    unsigned short   addr;
    unsigned char    val;
} aml_camera_i2c_fig_t;

typedef struct aml_camera_i2c_fig0_s{
    unsigned short   addr;
    unsigned short    val;
} aml_camera_i2c_fig0_t;

typedef struct aml_camera_i2c_fig1_s{
    unsigned char   addr;
    unsigned char    val;
} aml_camera_i2c_fig1_t;

extern void aml_cam_init(aml_cam_info_t* cam_dev);
extern void aml_cam_uninit(aml_cam_info_t* cam_dev);
extern void aml_cam_flash(aml_cam_info_t* cam_dev, int is_on);
extern void aml_cam_torch(aml_cam_info_t* cam_dev, int is_on);
extern int aml_cam_info_reg(aml_cam_info_t* cam_info);
extern int aml_cam_info_unreg(aml_cam_info_t* cam_info);


#endif /* __AML_CAM_DEV__ */
