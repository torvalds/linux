#ifndef __TS_AUTO_H
#define __TS_AUTO_H
#include <linux/miscdevice.h>

#define TS_ENABLE	1
#define	TS_DISABLE	0
#define TS_UNKNOW_DATA	-1
#define	TS_MAX_POINT	20
#define	TS_MAX_VER_LEN	64


enum ts_id {
	TS_ID_INVALID = 0,
		
	TS_ID_FT5306,
	TS_ID_CT360,
	TS_ID_GT8110,
	TS_ID_GT828,
	
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
	int id_i2c;
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
	int (*active)(struct i2c_client *client, int enable);
	int (*init)(struct i2c_client *client);	
	int (*check_irq)(struct i2c_client *client);
	int (*report)(struct i2c_client *client);
	int (*firmware)(struct i2c_client *client);
	int (*suspend)(struct i2c_client *client);
	int (*resume)(struct i2c_client *client);	
	struct miscdevice *misc_dev;

};


struct ts_private_data {
	struct i2c_client *client;	
	struct input_dev *input_dev;
	struct ts_event	event;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
	struct delayed_work poll_work;	/*poll at last*/	
	char ts_data[40];		//max support40 bytes data
	struct mutex data_mutex;
	struct mutex ts_mutex;
	struct mutex i2c_mutex;
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


extern int ts_register_slave(struct i2c_client *client,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void));


extern int ts_unregister_slave(struct i2c_client *client,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void));

extern int ts_rx_data(struct i2c_client *client, char *rxData, int length);
extern int ts_tx_data(struct i2c_client *client, char *txData, int length);
extern int ts_rx_data_word(struct i2c_client *client, char *rxData, int length);
extern int ts_write_reg(struct i2c_client *client, int addr, int value);
extern int ts_read_reg(struct i2c_client *client, int addr);
extern int ts_tx_data_normal(struct i2c_client *client, char *buf, int num);
extern int ts_rx_data_normal(struct i2c_client *client, char *buf, int num);
extern int ts_write_reg_normal(struct i2c_client *client, char value);
extern int ts_read_reg_normal(struct i2c_client *client);

#endif
