//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C Touchscreen driver (platform data struct)
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef __TOUCH_PDATA_H__
#define __TOUCH_PDATA_H__

//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
#endif

#include <linux/interrupt.h>
//[*]--------------------------------------------------------------------------------------------------[*]
#define	I2C_TOUCH_NAME		"odroid-ts"

#define	I2C_SEND_MAX_SIZE	512				// I2C Send/Receive data max size

//[*]--------------------------------------------------------------------------------------------------[*]
// Button struct (1 = press, 0 = release)
//[*]--------------------------------------------------------------------------------------------------[*]
typedef struct	button__t	{
	unsigned char	bt0_press	:1;		// lsb
	unsigned char	bt1_press	:1;		
	unsigned char	bt2_press	:1;
	unsigned char	bt3_press	:1;
	unsigned char	bt4_press	:1;
	unsigned char	bt5_press	:1;
	unsigned char	bt6_press	:1;
	unsigned char	bt7_press	:1;		// msb
}	__attribute__ ((packed))	button_t;

typedef union	button__u	{
	unsigned char	ubyte;
	button_t		bits;
}	__attribute__ ((packed))	button_u;

//[*]--------------------------------------------------------------------------------------------------[*]
// Touch Event type define
//[*]--------------------------------------------------------------------------------------------------[*]
#define	TS_EVENT_UNKNOWN	0x00
#define	TS_EVENT_PRESS		0x01
#define	TS_EVENT_MOVE		0x02
#define	TS_EVENT_RELEASE	0x03

//[*]--------------------------------------------------------------------------------------------------[*]
typedef	struct	finger__t	{
	unsigned int	status;			// true : ts data updated, false : no update data
	unsigned int	event;			// ts event type
	unsigned int	id;				// ts received id
	unsigned int	x;				// ts data x
	unsigned int	y;				// ts data y
	unsigned int	area;			// ts finger area
	unsigned int	pressure;		// ts finger pressure
}	__attribute__ ((packed))	finger_t;

//[*]--------------------------------------------------------------------------------------------------[*]
struct touch {
	int					irq;
	struct i2c_client 	*client;
	struct touch_pdata	*pdata;
	struct input_dev	*input;
	char				phys[32];

	// finger data
	finger_t			*finger;

	// sysfs control flags
	unsigned char		disabled;		// interrupt status
	unsigned char		fw_version;
	
	unsigned char		*fw_buf;
	unsigned int		fw_size;
	int					fw_status;

	// irq func used
	struct workqueue_struct		*work_queue;
	struct work_struct  		work;
	
	// noise filter work
	struct delayed_work		filter_dwork;

#if	defined(CONFIG_HAS_EARLYSUSPEND)
	struct	early_suspend		power;
#endif	
};

//[*]--------------------------------------------------------------------------------------------------[*]
struct 	i2c_client;
struct 	input_dev;
struct 	device;

//[*]--------------------------------------------------------------------------------------------------[*]
#define	IRQ_MODE_THREAD		0
#define	IRQ_MODE_NORMAL		1
#define	IRQ_MODE_POLLING	2

//[*]--------------------------------------------------------------------------------------------------[*]
// IRQ type & trigger action
//[*]--------------------------------------------------------------------------------------------------[*]
//
// IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING, IRQF_TRIGGER_HIGH, IRQF_TRIGGER_LOW
// IRQF_DISABLED, IRQF_SHARED, IRQF_IRQPOLL, IRQF_ONESHOT, IRQF_NO_THREAD
//
//[*]--------------------------------------------------------------------------------------------------[*]
struct touch_pdata	{
	char	*name;				/* input drv name */

	int 	irq_gpio;			/* irq gpio define */
	int 	reset_gpio;			/* reset gpio define */
	int 	reset_level;		/* reset level setting (1 = High reset, 0 = Low reset) */

	int		irq_mode;			/* IRQ_MODE_THREAD, IRQ_MODE_NORMAL, IRQ_MODE_POLLING */
	int 	irq_flags;			/* irq flags setup : Therad irq mode(IRQF_TRIGGER_HIGH | IRQF_ONESHOT) */

	int		abs_max_x, abs_max_y;
	int 	abs_min_x, abs_min_y;
	
	int 	area_max, area_min;
	int		press_max, press_min;
	int		id_max, id_min;
	
	int		vendor, product, version;

	int		max_fingers;
	
	int		*keycode, keycnt;

	//--------------------------------------------
	// Control function 
	//--------------------------------------------
	void 		(*gpio_init)		(void);	/* gpio early-init function */

	irqreturn_t	(*irq_func)			(int irq, void *handle);
	void		(*touch_work)		(struct touch *ts);
	
	void 		(*report)			(struct touch *ts);
	void		(*key_report)		(struct touch *ts, unsigned char button_data);

	int			(*early_probe)		(struct touch *ts);
	int			(*probe)			(struct touch *ts);
	void		(*enable)			(struct touch *ts);
	void		(*disable)			(struct touch *ts);
	int 		(*input_open)		(struct input_dev *input);
	void		(*input_close)		(struct input_dev *input);

	void		(*event_clear)		(struct touch *ts);

#ifdef	CONFIG_HAS_EARLYSUSPEND
	void		(*resume)			(struct early_suspend *h);
	void		(*suspend)			(struct early_suspend *h);
#endif

	//--------------------------------------------
	// I2C control function
	//--------------------------------------------
	int		(*i2c_write)			(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
	int		(*i2c_read)				(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);

	//--------------------------------------------
	// Firmware update control function
	//--------------------------------------------
	char	*fw_filename;
	int		fw_filesize;
	
	int		(*i2c_boot_write)		(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
	int		(*i2c_boot_read)		(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
	int		(*fw_control)			(struct touch *ts, unsigned int fw_status);
	int		(*flash_firmware)		(struct device *dev, const char *fw_name);

	//--------------------------------------------
	// Calibration control func
	//--------------------------------------------
	int		(*calibration)			(struct touch *ts);
};

//[*]--------------------------------------------------------------------------------------------------[*]
#endif /* __TOUCH_PDATA_H__ */
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]

