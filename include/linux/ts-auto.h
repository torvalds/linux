#ifndef __TS_AUTO_H
#define __TS_AUTO_H
#include <linux/miscdevice.h>

#define TS_ENABLE	1
#define	TS_DISABLE	0
#define TS_UNKNOW_DATA	-1
#define	TS_MAX_POINT	20
#define	TS_MAX_VER_LEN	64

struct ts_private_data;

enum ts_bus_type{
	TS_BUS_TYPE_INVALID = 0,
		
	TS_BUS_TYPE_I2C,
	TS_BUS_TYPE_SPI,
	TS_BUS_TYPE_SERIAL,
	
	TS_BUS_TYPE_NUM_ID,
};

enum ts_id {
	TS_ID_INVALID = 0,
		
	TS_ID_FT5306,
	TS_ID_CT360,
	TS_ID_GT8110,
	TS_ID_GT828,
	TS_ID_GT8005,
	
	TS_NUM_ID,
};

struct point_data {
	int status;
	int id;
	int x;
	int y;
	int press;
	int last_status;
};

struct ts_event {
  int  touch_point;
  struct point_data point[TS_MAX_POINT];
};


/* Platform data for the auto touchscreen */
struct ts_platform_data {
	unsigned char  slave_addr;
	int irq;
	int power_pin;
	int reset_pin;

	int (*init_platform_hw)(void);	
};

struct ts_max_pixel{
	int max_x;
	int max_y;
};

struct ts_operate {
	char *name;
	char slave_addr;
	int ts_id;
	int bus_type;
	struct ts_max_pixel pixel;
	int reg_size;
	int id_reg;
	int id_data;
	int version_reg;
	char *version_data;
	int version_len;	//<64
	int read_reg;
	int read_len;
	int trig;	//intterupt trigger
	int max_point;
	int xy_swap;
	int x_revert;
	int y_revert;
	int range[2];
	int irq_enable;         //if irq_enable=1 then use irq else use polling  
	int poll_delay_ms;      //polling
	int gpio_level_no_int;
	int (*active)(struct ts_private_data *ts, int enable);
	int (*init)(struct ts_private_data *ts);	
	int (*check_irq)(struct ts_private_data *ts);
	int (*report)(struct ts_private_data *ts);
	int (*firmware)(struct ts_private_data *ts);
	int (*suspend)(struct ts_private_data *ts);
	int (*resume)(struct ts_private_data *ts);	
	struct miscdevice *misc_dev;
};


struct ts_private_data {
	struct mutex io_lock;
	struct device *dev;
	int (*read_dev)(struct ts_private_data *ts, unsigned short reg,
			int bytes, void *dest, int reg_size);
	int (*write_dev)(struct ts_private_data *ts, unsigned short reg,
			 int bytes, void *src, int reg_size);
	void *control_data;
	int irq;
	//struct i2c_client *client;	
	struct input_dev *input_dev;
	struct ts_event	event;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
	struct delayed_work poll_work;	/*poll at last*/	
	char ts_data[40];		//max support40 bytes data
	struct mutex data_mutex;
	struct mutex ts_lock;
	int devid;
	struct i2c_device_id *i2c_id;
	struct ts_platform_data *pdata;
	struct ts_operate *ops; 
	struct file_operations fops;
	struct miscdevice miscdev;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct 	early_suspend early_suspend;
#endif
};

extern int ts_device_init(struct ts_private_data *ts, int type, int irq);
extern void ts_device_exit(struct ts_private_data *ts);
extern int ts_register_slave(struct ts_private_data *ts,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void));
extern int ts_unregister_slave(struct ts_private_data *ts,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void));
extern int ts_reg_read(struct ts_private_data *ts, unsigned short reg);
extern int ts_reg_write(struct ts_private_data *ts, unsigned short reg,
		     unsigned short val);
extern int ts_bulk_read(struct ts_private_data *ts, unsigned short reg,
		     int count, unsigned char *buf);
extern int ts_bulk_read_normal(struct ts_private_data *ts, int count, unsigned char *buf, int rate);
extern int ts_bulk_write(struct ts_private_data *ts, unsigned short reg,
		     int count, unsigned char *buf);
extern int ts_bulk_write_normal(struct ts_private_data *ts, int count, unsigned char *buf, int rate);
extern int ts_set_bits(struct ts_private_data *ts, unsigned short reg,
		    unsigned short mask, unsigned short val);
extern int ts_device_suspend(struct ts_private_data *ts);

extern int ts_device_resume(struct ts_private_data *ts);

#endif
