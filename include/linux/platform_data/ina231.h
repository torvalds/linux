//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C INA231 Sensor driver (platform data struct)
//  2013.07.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef __INA231_H__
#define __INA231_H__

//[*]--------------------------------------------------------------------------------------------------[*]
#define INA231_I2C_NAME "INA231"

struct 	i2c_client;
struct 	misc_dev;

//[*]--------------------------------------------------------------------------------------------------[*]
// INA231 Register define
//[*]--------------------------------------------------------------------------------------------------[*]
#define REG_CONFIG          0x00    // R/W
#define REG_SHUNT_VOLT      0x01    // R
#define REG_BUS_VOLT        0x02    // R
#define REG_POWER           0x03    // R
#define REG_CURRENT         0x04    // R
#define REG_CALIBRATION     0x05    // R/W
#define REG_ALERT_EN        0x06    // R/W
#define REG_ALERT_LIMIT     0x07    // R/W

#define FIX_uV_LSB          1250    // fix lsb value 0.00125 V

// ex) CONVERSION_DELAY(eVBUS_CT_140uS, eVSH_CT_150uS, eAVG_1); return  uSec
#define CONVERSION_DELAY(x, y, z)   ((x + y) * z)

#define INA231_CONFIG(x)            ((0x4000 | x) & 0xFFFF)
//[*]--------------------------------------------------------------------------------------------------[*]
enum    {
    eAVG_CON_1      = 1,
    eAVG_CON_4      = 4,
    eAVG_CON_16     = 16,
    eAVG_CON_64     = 64,
    eAVG_CON_128    = 128,
    eAVG_CON_256    = 256,
    eAVG_CON_512    = 512,
    eAVG_CON_1024   = 1024,
};

enum    {
    eAVG_1 = 0,
    eAVG_4,
    eAVG_16,
    eAVG_64,
    eAVG_128,
    eAVG_256,
    eAVG_512,
    eAVG_1024,
};

#define AVG_BIT(x)   (x << 9)

//[*]--------------------------------------------------------------------------------------------------[*]
enum    {
    eVBUS_CON_140uS  = 140,
    eVBUS_CON_204uS  = 204,
    eVBUS_CON_332uS  = 332,
    eVBUS_CON_588uS  = 588,
    eVBUS_CON_1100uS = 1100,
    eVBUS_CON_2116uS = 2116,
    eVBUS_CON_4156uS = 4156,
    eVBUS_CON_8244uS = 8244,
};

enum    {
    eVBUS_CT_140uS = 0,
    eVBUS_CT_204uS,
    eVBUS_CT_332uS,
    eVBUS_CT_588uS,
    eVBUS_CT_1100uS,
    eVBUS_CT_2116uS,
    eVBUS_CT_4156uS,
    eVBUS_CT_8244uS,
};

#define VBUS_CT(x)  (x << 6)

//[*]--------------------------------------------------------------------------------------------------[*]
enum    {
    eVSH_CON_140uS  = 140, 
    eVSH_CON_204uS  = 204, 
    eVSH_CON_332uS  = 332, 
    eVSH_CON_588uS  = 588, 
    eVSH_CON_1100uS = 1100,
    eVSH_CON_2116uS = 2116,
    eVSH_CON_4156uS = 4156,
    eVSH_CON_8244uS = 8244,
};

enum    {
    eVSH_CT_140uS = 0,
    eVSH_CT_204uS,
    eVSH_CT_332uS,
    eVSH_CT_588uS,
    eVSH_CT_1100uS,
    eVSH_CT_2116uS,
    eVSH_CT_4156uS,
    eVSH_CT_8244uS,
};

#define VSH_CT(x)  (x << 3)

//[*]--------------------------------------------------------------------------------------------------[*]
enum    {
    ePOWER_DOWN1 = 0,
    eSHUNT_VOLT_TRIGGER,
    eBUS_VOLT_TRIGGER,
    eSHUNT_BUS_VOLT_TRIGGER,
    ePOWER_DOWN2,
    eSHUNT_VOLT_CONTINUOUS,
    eBUS_VOLT_CONTINUOUS,
    eSHUNT_BUS_VOLT_CONTINUOUS,
};

#define MODE_SET(x)  (x)

//[*]--------------------------------------------------------------------------------------------------[*]
struct ina231_pd    {
    unsigned char   *name;
    unsigned short  config;
    unsigned int    max_A;
    unsigned int    shunt_R_mohm;     // unit = m ohm
	unsigned int    update_period;    // unit = usec

	unsigned int    enable;
};

//[*]--------------------------------------------------------------------------------------------------[*]
struct ina231_sensor    {
	struct i2c_client 	    *client;
	struct ina231_pd	    *pd;
	struct miscdevice       *misc;

    unsigned short          reg_calibration;	                            
	unsigned short          reg_bus_volt;
	unsigned short          reg_current;
	
	unsigned int            cur_lsb_uA;
	
	unsigned int            cur_uA;
	unsigned int            cur_uV;
	unsigned int            cur_uW;
	
	unsigned int            max_uA;
	unsigned int            max_uV;
	unsigned int            max_uW;

	struct hrtimer          timer;
	unsigned int            timer_sec, timer_nsec;
	struct work_struct  	work;
	struct workqueue_struct	*wq;

	struct mutex            mutex;

#if	defined(CONFIG_HAS_EARLYSUSPEND)
	struct	early_suspend		power;
#endif	
    
};

//[*]--------------------------------------------------------------------------------------------------[*]
#endif  // INA231
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
