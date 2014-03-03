#ifndef __RK610_CONTROL_H_
#define __RK610_CONTROL_H_

#define INVALID_GPIO -1
//#define RK610_DEBUG

#ifdef RK610_DEBUG
#define RK610_DBG(dev, format, arg...)		\
do{\
		dev_printk(KERN_INFO , dev , format , ## arg);\
}while(0)
#else
#define RK610_DBG(dev, format, arg...)
#endif
#define RK610_ERR(dev, format, arg...)		\
do{\
		dev_printk(KERN_ERR , dev , format , ## arg);\
}while(0)

#define RK610_CONTROL_REG_C_PLL_CON0	0x00
#define RK610_CONTROL_REG_C_PLL_CON1	0x01
#define RK610_CONTROL_REG_C_PLL_CON2	0x02
#define RK610_CONTROL_REG_C_PLL_CON3	0x03
#define RK610_CONTROL_REG_C_PLL_CON4	0x04
#define RK610_CONTROL_REG_C_PLL_CON5	0x05
	#define C_PLL_DISABLE_FRAC		1 << 0
	#define C_PLL_BYPSS_ENABLE		1 << 1
	#define C_PLL_POWER_ON			1 << 2
	#define C_PLL_LOCLED			1 << 7
	
#define RK610_CONTROL_REG_TVE_CON		0x29
	#define TVE_CONTROL_VDAC_R_BYPASS_ENABLE	1 << 7
	#define TVE_CONTROL_VDAC_R_BYPASS_DISABLE	0 << 7
	#define TVE_CONTROL_CVBS_3_CHANNEL_ENALBE	1 << 6
	#define TVE_CONTROL_CVBS_3_CHANNEL_DISALBE	0 << 5
enum {
	INPUT_DATA_FORMAT_RGB888 = 0,
	INPUT_DATA_FORMAT_RGB666,
	INPUT_DATA_FORMAT_RGB565,
	INPUT_DATA_FORMAT_YUV
};
	#define RGB2CCIR_INPUT_DATA_FORMAT(n)	n << 4
	
	#define RGB2CCIR_RGB_SWAP_ENABLE		1 << 3
	#define RGB2CCIR_RGB_SWAP_DISABLE		0 << 3
	
	#define RGB2CCIR_INPUT_INTERLACE		1 << 2
	#define RGB2CCIR_INPUT_PROGRESSIVE		0 << 2
	
	#define RGB2CCIR_CVBS_PAL				0 << 1
	#define RGB2CCIR_CVBS_NTSC				1 << 1
	
	#define RGB2CCIR_DISABLE				0
	#define RGB2CCIR_ENABLE					1
	
#define RK610_CONTROL_REG_CCIR_RESET	0x2a

#define RK610_CONTROL_REG_CLOCK_CON0	0x2b
#define RK610_CONTROL_REG_CLOCK_CON1	0x2c
	#define CLOCK_CON1_I2S_CLK_CODEC_PLL	1 << 5
	#define CLOCK_CON1_I2S_DVIDER_MASK		0x1F
#define RK610_CONTROL_REG_CODEC_CON		0x2d
	#define CODEC_CON_BIT_HDMI_BLCK_INTERANL		1<<4
	#define CODEC_CON_BIT_DAC_LRCL_OUTPUT_DISABLE	1<<3
	#define CODEC_CON_BIT_ADC_LRCK_OUTPUT_DISABLE	1<<2
	#define CODEC_CON_BIT_INTERAL_CODEC_DISABLE		1<<0

#define RK610_CONTROL_REG_I2C_CON		0x2e

/********************************************************************
**                          结构定义                                *
********************************************************************/
/* RK610的寄存器结构 */
/* CODEC PLL REG */
#define C_PLL_CON0      0x00
#define C_PLL_CON1      0x01
#define C_PLL_CON2      0x02
#define C_PLL_CON3      0x03
#define C_PLL_CON4      0x04
#define C_PLL_CON5      0x05

/*  SCALER PLL REG */
#define S_PLL_CON0      0x06
#define S_PLL_CON1      0x07
#define S_PLL_CON2      0x08

/*  LVDS REG */
#define LVDS_CON0       0x09
#define LVDS_CON1       0x0a

/*  LCD1 REG */
#define LCD1_CON        0x0b

/*  SCALER REG  */
#define SCL_CON0        0x0c
#define SCL_CON1        0x0d
#define SCL_CON2        0x0e
#define SCL_CON3        0x0f
#define SCL_CON4        0x10
#define SCL_CON5        0x11
#define SCL_CON6        0x12
#define SCL_CON7        0x13
#define SCL_CON8        0x14
#define SCL_CON9        0x15
#define SCL_CON10       0x16
#define SCL_CON11       0x17
#define SCL_CON12       0x18
#define SCL_CON13       0x19
#define SCL_CON14       0x1a
#define SCL_CON15       0x1b
#define SCL_CON16       0x1c
#define SCL_CON17       0x1d
#define SCL_CON18       0x1e
#define SCL_CON19       0x1f
#define SCL_CON20       0x20
#define SCL_CON21       0x21
#define SCL_CON22       0x22
#define SCL_CON23       0x23
#define SCL_CON24       0x24
#define SCL_CON25       0x25
#define SCL_CON26       0x26
#define SCL_CON27       0x27
#define SCL_CON28       0x28

/*  TVE REG  */
#define TVE_CON         0x29

/*  CCIR REG    */
#define CCIR_RESET      0X2a

/*  CLOCK REG    */
#define CLOCK_CON0      0X2b
#define CLOCK_CON1      0X2c

/*  CODEC REG    */
#define CODEC_CON       0x2e
#define I2C_CON         0x2f


struct rk610_core_info{
    struct i2c_client *client;
    struct device *dev;

    struct dentry *debugfs_dir;
    void *lcd_pdata;
	struct clk *i2s_clk;
	int reset_gpio;
};

extern int rk610_control_send_byte(const char reg, const char data);

#endif /*end of __RK610_CONTROL_H_*/
