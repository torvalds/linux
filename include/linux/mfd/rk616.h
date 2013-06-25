#ifndef _RK616_H_
#define _RK616_H_

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/rk_fb.h>
#include <linux/clk.h>
#include <linux/delay.h>

#if defined(CONFIG_RK616_DEBUG)
#define rk616_dbg(dev, format, arg...)		\
	dev_info(dev , format , ## arg)
#else
#define rk616_dbg(dev, format, arg...)	do{}while(0)
#endif

#define VIF0_REG0 		0x0000
#define VIF0_DDR_CLK_EN		(1<<3)
#define VIF0_DDR_PHASEN_EN	(1<<2)  //negative edge first en
#define VIF0_DDR_MODE_EN	(1<<1)
#define VIF0_EN			(1<<0)

#define VIF0_REG1 		0x0004
#define VIF0_REG2 		0x0008
#define VIF0_REG3 		0x000C
#define VIF0_REG4 		0x0010
#define VIF0_REG5 		0x0014
#define VIF1_REG0 		0x0018
#define VIF1_REG1 		0x001C
#define VIF1_REG2 		0x0020
#define VIF1_REG3 		0x0024
#define VIF1_REG4 		0x0028
#define VIF1_REG5 		0x002C
#define SCL_REG0  		0x0030
#define SCL_EN          	(1<<0)

#define SCL_REG1  		0x0034
#define SCL_REG2  		0x0038
#define SCL_REG3  		0x003C
#define SCL_REG4  		0x0040
#define SCL_REG5  		0x0044
#define SCL_REG6  		0x0048
#define SCL_REG7  		0x004C
#define SCL_REG8  		0x0050
#define FRC_REG   		0x0054
#define FRC_DEN_INV		(1<<6)
#define FRC_SYNC_INV		(1<<5)
#define FRC_DCLK_INV		(1<<4)
#define FRC_OUT_ZERO		(1<<3)
#define FRC_RGB18_MODE		(1<<2)
#define FRC_HIFRC_MODE		(1<<1)
#define FRC_DITHER_EN		(1<<0)

#define CRU_CLKSEL0_CON 	0x0058
#define PLL1_CLK_SEL_MASK	(0x3<<24)
#define PLL0_CLK_SEL_MASK	(0x3<<22)
#define LCD1_CLK_DIV_MASK	(0x7<<19)
#define LCD0_CLK_DIV_MASK	(0x7<<16)
#define PLL1_CLK_SEL(x)  	(((x)&3)<<8)
#define PLL0_CLK_SEL(x)  	(((x)&3)<<6)
#define LCD0_DCLK		0
#define LCD1_DCLK		1
#define MCLK_12M		2
#define LCD1_CLK_DIV(x) 	(((x)&7)<<3)
#define LCD0_CLK_DIV(x) 	(((x)&7)<<0)

#define CRU_CLKSEL1_CON 	0x005C
#define SCLK_SEL_MASK		(1<<19)
#define CODEC_MCLK_SEL_MASK	(3<<16)
#define LCDC_CLK_GATE		(1<<12)
#define LCDC1_CLK_GATE		(1<<11)
#define MIPI_CLK_GATE		(1<<10)
#define LVDS_CLK_GATE		(1<<9)
#define HDMI_CLK_GATE		(1<<8)
#define SCL_CLK_DIV(x)		(((x)&7)<<5)
#define SCL_CLK_GATE		(1<<4)
#define SCLK_SEL(x)		(((x)&1)<<3)
#define SCLK_SEL_PLL0		0
#define SCLK_SEL_PLL1		1
#define CODEC_CLK_GATE		(1<<2)
#define CODEC_MCLK_SEL(x)	(((x)&3)<<0)
#define CODEC_MCLK_SEL_PLL0	0
#define CODEC_MCLK_SEL_PLL1	1
#define CODEC_MCLK_SEL_12M	2

#define CRU_CODEC_DIV		0x0060

#define CRU_CLKSEL2_CON  	0x0064
#define SCL_IN_SEL_MASK		(1<<31)
#define DITHER_IN_SEL_MASK	(1<<30)
#define HDMI_IN_SEL_MASK	(3<<28)
#define VIF1_CLK_DIV_MASK	(7<<25)
#define VIF0_CLK_DIV_MASK	(7<<19)
#define VIF1_CLKIN_SEL_MASK	(1<<22)
#define VIF0_CLKIN_SEL_MASK	(1<<16)
#define SCL_IN_SEL(x)		(((x)&1)<<15)
#define SCL_SEL_VIF0           	0
#define SCL_SEL_VIF1           	1
#define DITHER_IN_SEL(x)	(((x)&1)<<14)
#define DITHER_SEL_VIF0		0
#define DITHER_SEL_SCL		1

#define HDMI_IN_SEL(x)		(((x)&3)<<12)
#define HDMI_CLK_SEL_VIF1	0
#define HDMI_CLK_SEL_SCL	1
#define HDMI_CLK_SEL_VIF0	2
#define VIF1_CLK_DIV(x) 	(((x)&7)<<9)
#define VIF1_CLK_GATE		(1<<8)
#define VIF1_CLK_BYPASS		(1<<7)
#define VIF1_CLKIN_SEL(x)	(((x)&1)<<6)
#define VIF_CLKIN_SEL_PLL0	0
#define VIF_CLKIN_SEL_PLL1	1
#define VIF0_CLK_DIV(x)		(((x)&7)<<3)
#define VIF0_CLK_GATE		(1<<2)
#define VIF0_CLK_BYPASS		(1<<1)
#define VIF0_CLKIN_SEL(x)	(((x)&1)<<0)


#define CRU_PLL0_CON0   	0x0068
#define PLL0_POSTDIV1_MASK	(7<<28)
#define PLL0_FBDIV_MASK		(0xfff << 16)
#define PLL0_BYPASS		(1<<15)
#define PLL0_POSTDIV1(x) 	(((x)&7)<<12)
#define PLL0_FBDIV(x)	  	(((x)&0xfff)<<0)

#define CRU_PLL0_CON1   	0x006C
#define PLL0_DIV_MODE_MASK	(1<<25)
#define PLL0_POSTDIV2_MASK	(7<<22)
#define PLL0_REFDIV_MASK	(0x3f<<16)
#define PLL0_LOCK		(1<<15)
#define PLL0_PWR_DN		(1<<10)
#define PLL0_DIV_MODE(x)	(((x)&1)<<9)
#define PLL0_POSTDIV2(x)	(((x)&7)<<6)
#define PLL0_REFDIV(x)		(((x)&0x3f)<<0)

#define CRU_PLL0_CON2   	0x0070
#define PLL0_FOUT4_PWR_DN	(1<<27)
#define PLL0_FOUTVCO_PWR_DN	(1<<26)
#define PLL0_POSTDIV_PWR_DN	(1<<25)
#define PLL0_DAC_PWR_DN		(1<<24)
#define PLL0_FRAC(x)		(((x)&0xffffff)<<0)

#define CRU_PLL1_CON0   	0x0074
#define PLL1_POSTDIV1_MASK	(7<<28)
#define PLL1_FBDIV_MASK		(0xfff << 16)
#define PLL1_BYPASS		(1<<15)
#define PLL1_POSTDIV1(x) 	(((x)&7)<<12)
#define PLL1_FBDIV(x)	  	(((x)&0xfff)<<0)

#define CRU_PLL1_CON1   	0x0078
#define PLL1_POSTDIV2_MASK	(7<<22)
#define PLL1_REFDIV_MASK	(0x3f<<16)
#define PLL1_LOCK		(1<<15)
#define PLL1_PWR_DN		(1<<10)
#define PLL1_DIV_MODE		(1<<9)
#define PLL1_POSTDIV2(x)	(((x)&7)<<6)
#define PLL1_REFDIV(x)		(((x)&0x3f)<<0)

#define CRU_PLL1_CON2   	0x007C
#define PLL1_FOUT4_PWR_DN	(1<<27)
#define PLL1_FOUTVCO_PWR_DN	(1<<26)
#define PLL1_POSTDIV_PWR_DN	(1<<25)
#define PLL1_DAC_PWR_DN		(1<<24)
#define PLL1_FRAC(x)		(((x)&0xffffff)<<0)

#define CRU_I2C_CON0    	0x0080

#define CRU_LVDS_CON0   	0x0084
#define LVDS_HBP_ODD_MASK	(0x1<<30)
#define LVDS_OUT_FORMAT_MASK	(3<<16)
#define LVDS_HBP_ODD(x)		(((x)&1)<<14)
#define LVDS_DCLK_INV	  	(1<<13)
#define LVDS_CH1_LOAD	  	(1<<12)
#define LVDS_CH0_LOAD	  	(1<<11)
#define LVDS_CH1TTL_EN 		(1<<10)
#define LVDS_CH0TTL_EN 		(1<<9)
#define LVDS_CH1_PWR_EN	    	(1<<8)
#define LVDS_CH0_PWR_EN	    	(1<<7)
#define LVDS_CBG_PWR_EN	    	(1<<6)
#define LVDS_PLL_PWR_DN	    	(1<<5)
#define LVDS_START_CH_SEL   	(1<<4)
#define LVDS_CH_SEL	    	(1<<3)
#define LVDS_MSB_SEL	    	(1<<2)
#define LVDS_OUT_FORMAT(x)	(((x)&3)<<0)


#define CRU_IO_CON0    		0x0088
#define I2S1_OUT_DISABLE	(1<<13)
#define I2S0_OUT_DISABLE	(1<<12)
#define LVDS_OUT_EN		(1<<11)
#define LCD1_INPUT_EN		(1<<10)
#define LVDS_RGBIO_PD_DISABLE	(1<<9)
#define LCD1_IO_PD_DISABLE	(1<<8)
#define LCD0_IO_PD_DISABLE	(1<<7)
#define HDMI_IO_PU_DISABLE	(1<<6)
#define SPDIF_IO_PD_DISABLE	(1<<5)
#define I2S1_PD_DISABLE		(1<<4)
#define I2S0_PD_DISABLE		(1<<3)
#define I2C_PU_DISABLE		(1<<2)
#define INT_IO_PU		(1<<1)
#define CLKIN_PU		(1<<0)


#define CRU_IO_CON1   		0x008C
#define LVDS_RGBIO_SI_EN	(1<<9)  //shmitt input enable
#define LCD1_SI_EN		(1<<8)
#define LCD0_SI_EN		(1<<7)
#define HDMI_SI_EN		(1<<6)
#define SPDIF_SI_EN		(1<<5)
#define I2S1_SI_EN		(1<<4)
#define I2S0_SI_EN		(1<<3)
#define I2C_SI_EN		(1<<2)
#define INT_SI_EN		(1<<1)
#define CLKIN_SI_EN		(1<<0)
#define CRU_PCM2IS2_CON0	0x0090
#define CRU_PCM2IS2_CON1	0x0094
#define CRU_PCM2IS2_CON2	0x0098
#define CRU_CFGMISC_CON		0x009C


enum lcd_port_func{       // the function of lcd ports(lcd0,lcd1),the lcd0 only can be used as input or unused
	UNUSED,             // the lcd1 can be used as input,output or unused
	INPUT,
	OUTPUT,
};

enum lvds_mode {
	RGB,
	LVDS,
};
struct rk616_platform_data {
	int (*power_init)(void);
	int (*power_deinit)(void);
	int scl_rate;
	enum lcd_port_func lcd0_func;
	enum lcd_port_func lcd1_func;
	int lvds_ch_nr;			//the number of used  lvds channel 
	int hdmi_irq;
	int spk_ctl_gpio;
	int hp_ctl_gpio;
};

struct rk616_route {
	u16 vif0_bypass;
	u8  vif0_en;
	u16 vif0_clk_sel;
	u16 vif1_bypass;
	u8  vif1_en;
	u16 vif1_clk_sel;
	u16 sclin_sel;
	u8  scl_en;
	u8  scl_bypass;
	u16 dither_sel;
	u16 hdmi_sel;
	u16 pll0_clk_sel;
	u16 pll1_clk_sel;
	u16 sclk_sel;
	u8  lcd1_input;
	u8  lvds_en;
	enum lvds_mode lvds_mode;                //RGB or LVDS
	int lvds_ch_nr;		//the number of used  lvds channel 
};

struct mfd_rk616 {
	struct mutex reg_lock;

	struct device *dev;
	unsigned int irq_base;
	struct rk616_platform_data *pdata;
	struct rk616_route route;  //display path router
	struct i2c_client *client;
	struct clk *mclk;
	u64 pll0_rate;
	u64 pll1_rate;
	struct dentry *debugfs_dir;
	int (*read_dev)(struct mfd_rk616 *rk616,u16 reg,u32 *pval);
	int (*write_dev)(struct mfd_rk616 *rk616,u16 reg,u32 *pval);
};

extern int rk616_set_vif(struct mfd_rk616 * rk616,rk_screen * screen,bool connect);
extern int rk616_display_router_cfg(struct mfd_rk616 *rk616,rk_screen *screen,bool enable);



#endif

