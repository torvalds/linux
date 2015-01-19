#ifndef _TOUCH_H_
#define _TOUCH_H_

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/input/mt.h>
#include <linux/vmalloc.h>
#include <linux/hrtimer.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_OF
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of.h>
#else
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#endif

#define aml_gpio_request(gpio) amlogic_gpio_request(gpio, ts_com->owner)
#define aml_gpio_free(gpio) amlogic_gpio_free(gpio, ts_com->owner)
#define aml_gpio_direction_input(gpio) amlogic_gpio_direction_input(gpio, ts_com->owner)
#define aml_gpio_direction_output(gpio, val) amlogic_gpio_direction_output(gpio, val, ts_com->owner)
#define aml_get_value(gpio) amlogic_get_value(gpio, ts_com->owner)
#define aml_set_value(gpio,val) amlogic_set_value(gpio, val, ts_com->owner)
#define aml_gpio_to_irq(gpio, irq, irq_edge) 	amlogic_gpio_to_irq(gpio, ts_com->owner, AML_GPIO_IRQ((irq),FILTER_NUM7,irq_edge))

#define touch_dbg(fmt, args...)  { if(ts_com->printk_enable_flag) \
					printk("[%s]: " fmt, ts_com->owner, ## args); }
					
#define UPGRADE_TOUCH "upgrade_touch"
#define MAX_TOUCH 10

#define AML_I2C_BUS_AO 0
#define AML_I2C_BUS_A 1
#define AML_I2C_BUS_B 2

struct touch_pdata {
  int ic_type; /* see Focaltech IC type */
  int irq;
  int gpio_interrupt;
  int gpio_reset;
  int gpio_power;
  int xres;
  int yres;
  int pol; 
  int irq_edge;
	int max_num;
  unsigned bus_type;
  unsigned reg;
  unsigned auto_update_fw;
	char *owner;
	char fw_file[255];
	char config_file[255];
	
	int printk_enable_flag;
	void(*hardware_reset)(struct touch_pdata *);
	void(*software_reset)(struct touch_pdata *);
	void(*read_version)(char*);
	void(*upgrade_touch)(void);
	
	struct cdev upgrade_cdev;
	dev_t upgrade_no;
	struct device *dev;
	struct task_struct *upgrade_task;

	struct tp_key *tp_key;
	int tp_key_num;
	int select_gpio_num;
	int select_fw_gpio[10];
	char *fw_select[10];
};

typedef enum
{
    ERR_NO,
    ERR_NO_NODE,
    ERR_GET_DATA,
    ERR_GPIO_REQ
}GET_DT_ERR_TYPE;

typedef int (*fill_buf_t)(void *priv, int idx, int data);

int touch_open_fw(char *fw);
int touch_read_fw(int offset, int length, char *buf);
int touch_close_fw(void);
void set_reset_pin(struct touch_pdata *pdata, u8 on);
void set_power_pin(struct touch_pdata *pdata, u8 on);
GET_DT_ERR_TYPE get_dt_data(struct device_node* of_node, struct touch_pdata *pdata);
int create_init(struct device dev, struct touch_pdata *pdata);
GET_DT_ERR_TYPE request_touch_gpio(struct touch_pdata *pdata);
void free_touch_gpio(struct touch_pdata *pdata);
void destroy_remove(struct device dev, struct touch_pdata *pdata);
int get_gpio_fw(struct touch_pdata *pdata);
int get_data_from_text_file(char *text_file, fill_buf_t fill_buf, void *priv);

#endif
