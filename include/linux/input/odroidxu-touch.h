//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C Touchscreen driver (platform data struct)
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef __ODROIDXU_TOUCH_H__
#define __ODROIDXU_TOUCH_H__

//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#define	IRQ_MODE_EVENT

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_ANDROID_PARANOID_NETWORK)
    #define SOFT_AVR_FILTER_ENABLE
#endif

#if defined(SOFT_AVR_FILTER_ENABLE)
    #define SOFT_AVR_COUNT              10
    #define SOFT_AVR_MOVE_TOL_X         20   // First move tol
    #define SOFT_AVR_MOVE_TOL_Y         20   // First move tol
    #define SOFT_AVR_ENABLE_SPEED       5
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_MOVE_TOL		0x01		// 0x53 Register (0x00 ~ 0xFF : 0x10 default)
#define	X_TRACKING			0x80		// 0x54 Register (0x00 ~ 0xFF : 0x80 default)
#define	Y_TRACKING			0x80		// 0x55 Register (0x00 ~ 0xFF : 0x80 default)

#if defined(SOFT_AVR_FILTER_ENABLE)
    #define	MOVE_AVR_FILTER		0x00		// 0x56 Register (0x00 ~ 0x03 : 0x00 default)
#else    
    //#define	MOVE_AVR_FILTER		0x01		// 0x56 Register (0x00 ~ 0x03 : 0x00 default)
    #define	MOVE_AVR_FILTER		0x02		// 0x56 Register (0x00 ~ 0x03 : 0x00 default)
    //#define	MOVE_AVR_FILTER		0x03		// 0x56 Register (0x00 ~ 0x03 : 0x00 default)
#endif

#define	SCAN_MODE			0x01		// 0x26 Register (0x00 ~ ?? : 0x02 default)
//#define	SCAN_MODE			0x0F		// 0x26 Register (0x00 ~ ?? : 0x02 default)

//[*]--------------------------------------------------------------------------------------------------[*]
#define MAX_FINGERS			10
#define	RESOULATION			64

#define DRIVE_LINE_COUNT	23
#define SENSE_LINE_COUNT	38

//#define	DRIVE_DATA_MAX		(DRIVE_LINE_COUNT * RESOULATION)
//#define	SENSE_DATA_MAX		(SENSE_LINE_COUNT * RESOULATION)

#define	DRIVE_DATA_MAX		800
#define	SENSE_DATA_MAX		1280

#define	TRANSPOSE_XY		0x04
#define	INVERT_Y			0x01
#define	INVERT_X			0x02

// 0x65 Register (0x00 ~ 0x07 : 0x00 default)
#define	ORIENTATION			0x00

#define	TRACKING_ID_MAX		16

#define	PRESSURE_MAX		16

//[*]--------------------------------------------------------------------------------------------------[*]
// Register Define
//[*]--------------------------------------------------------------------------------------------------[*]
#define	NOP						0x00	
#define	DEVICE_ID				0x02	
#define	VERSION_ID				0x03	
#define	SLEEP_OUT_REG			0x04	
#define	SLEEP_IN_REG			0x05
#define	DRIVE_NO_REG			0x06
#define	SENSE_NO_REG			0x07

//[*]--------------------------------------------------------------------------------------------------[*]
#define	DRIVE_LINE0_REG			0x08
#define	DRIVE_LINE1_REG			0x09
#define	DRIVE_LINE2_REG			0x0A
#define	DRIVE_LINE3_REG			0x0B
#define	DRIVE_LINE4_REG			0x0C
#define	DRIVE_LINE5_REG			0x0D
#define	DRIVE_LINE6_REG			0x0E
#define	DRIVE_LINE7_REG			0x0F
#define	DRIVE_LINE8_REG			0x10
#define	DRIVE_LINE9_REG			0x11
#define	DRIVE_LINE10_REG		0x12
#define	DRIVE_LINE11_REG		0x13
#define	DRIVE_LINE12_REG		0x14
#define	DRIVE_LINE13_REG		0x15
#define	DRIVE_LINE14_REG		0x16
#define	DRIVE_LINE15_REG		0x17
#define	DRIVE_LINE16_REG		0x18
#define	DRIVE_LINE17_REG		0x19
#define	DRIVE_LINE18_REG		0x1A
#define	DRIVE_LINE19_REG		0x1B
#define	DRIVE_LINE20_REG		0x1C
#define	DRIVE_LINE21_REG		0x1D
#define	DRIVE_LINE22_REG		0x1E

//[*]--------------------------------------------------------------------------------------------------[*]
#define	WOP_MODE_REG			0x25
#define	ROP_MODE_REG			0x26
#define	DOWN_TIME_REG			0x27
#define	FRAME_ESC_REG			0x28
#define	SCAN_FRAME_REG			0x2A

//[*]--------------------------------------------------------------------------------------------------[*]
#define	MEDIAN_FILTER_SEL_REG	0x2C
#define	INT_GAIN_REG			0x2F
#define	START_INT_REG			0x30
#define	END_INT_REG				0x31

//[*]--------------------------------------------------------------------------------------------------[*]
#define	MIN_AREA_REG			0x33
#define	MIN_LEVEL_REG			0x34
#define	MIN_WEIGHT_REG			0x35
#define	MAX_AREA_REG			0x36
#define	SEG_DEPTH_REG			0x37
#define	CG_METHOD_REG			0x39
#define	HYBRID_SELECT_REG		0x3A
#define	INT_BYPASS_REG			0x3C
#define	FILTER_SEL_REG			0x3D
#define	CALIBRATE_OFF_REG		0x3E

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_MOVE_TOL_REG		0x53
#define	X_TRACKING_TOL_REG		0x54
#define	Y_TRACKING_TOL_REG		0x55
#define	MOV_AVG_FILTER_REG		0x56
#define	ORIENTATION_REG			0x65
#define	X_SCALING_REG			0x66
#define	Y_SCALING_REG			0x67
#define	X_OFFSET_REG			0x68
#define	Y_OFFSET_REG			0x69

//[*]--------------------------------------------------------------------------------------------------[*]
#define	TOUCH_STATUS			0x79

typedef	struct	status__t	{
	unsigned short	fifo_valid		:1;
	unsigned short	fifo_overflow	:1;
	unsigned short	large_object	:1;
	unsigned short	abnomal_status	:1;
	unsigned short	fingers			:10;
	unsigned short	reserved		:2;
}	__attribute__ ((packed))	status_t;

typedef union	status__u	{
	unsigned char	byte[sizeof(status_t)];
	status_t		bits;
}	__attribute__ ((packed))	status_u;

//[*]--------------------------------------------------------------------------------------------------[*]
#define BUTTON_STATUS		   	0xB9

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_MASK_REG			0x7A
#define	IRQ_MASK_REG			0x7B

//[*]--------------------------------------------------------------------------------------------------[*]
#define	FINGER00_REG			0x7C
#define	FINGER01_REG			0x7D
#define	FINGER02_REG			0x7E
#define	FINGER03_REG			0x7F
#define	FINGER04_REG			0x80
#define	FINGER05_REG			0x81
#define	FINGER06_REG			0x82
#define	FINGER07_REG			0x83
#define	FINGER08_REG			0x84
#define	FINGER09_REG			0x85

typedef	struct	finger_data__t	{	
	unsigned char	speed		:4;	// LSB
	unsigned char	pressure	:4;
	unsigned char	msb_y		:4;
	unsigned char	msb_x		:4;
	unsigned char	lsb_y		:8;
	unsigned char	lsb_x		:8;	// MSB
}	__attribute__ ((packed))	finger_data_t;

typedef union	finger_data__u	{
	unsigned char	byte[sizeof(finger_data_t)];
	finger_data_t	bits;
}	__attribute__ ((packed))	finger_data_u;

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_STACK				0x86

typedef	struct	event_stack__t	{	
	unsigned char	speed		:4;	// LSB
	unsigned char	pressure	:4;
	unsigned char	msb_y		:4;
	unsigned char	msb_x		:4;
	unsigned char	lsb_y		:8;
	unsigned char	lsb_x		:8;
	unsigned char	event		:4;
	unsigned char	number		:4;	// MSB
}	__attribute__ ((packed))	event_stack_t;

typedef union	event_stack__u	{
	unsigned char	byte[sizeof(event_stack_t)];
	event_stack_t	bits;
}	__attribute__ ((packed))	event_stack_u;

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_FIFO_SCLR			0x87
#define	TOUCH_IRQ_MODE			0x89		// 0 -> event, 1 -> frame
//[*]--------------------------------------------------------------------------------------------------[*]
#define	INIT_RST				0xA2
#define	DRIVE_LEVEL_REG			0xD5
#define	ADC_RANGE_SEL_REG		0xD7
#define	BIAS_RES				0xD8
#define	INTG_CAP_REG			0xDB

//[*]--------------------------------------------------------------------------------------------------[*]
#define	EVENT_UNKNOWN 			0x00
#define	EVENT_PRESS				0x03
#define	EVENT_MOVE				0x04
#define	EVENT_RELEASE			0x05

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
//
// ODROID-XU Control Function define
//
//[*]--------------------------------------------------------------------------------------------------[*]
extern	int 			odroidxu_calibration	(struct touch *ts);
extern	int 			odroidxu_i2c_read	    (struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
extern	void			odroidxu_work		    (struct touch *ts);
extern	void			odroidxu_enable		    (struct touch *ts);
extern	void			odroidxu_disable		(struct touch *ts);
extern	int				odroidxu_early_probe	(struct touch *ts);
extern	int				odroidxu_probe		    (struct touch *ts);

//[*]--------------------------------------------------------------------------------------------------[*]
#endif /* __ODROIDXU_TOUCH_H__ */
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
