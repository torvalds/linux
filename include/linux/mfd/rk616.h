#ifndef _RK616_H_
#define _RK616_H_

#include <linux/types.h>
#include <linux/i2c.h>


#define VIF0_REG0 		0x0000
#define VIF0_DDR_CLK_EN		(1<<3)
#define VIF0_DDR_PHASEN_EN	(1<<2)  //negative edge first en
#define VIF0_DDR_MODE_EN	(1<<1)
#define VIF0_EN			(1<<0)

#define VIF0_REG1 	0x0004
#define VIF0_REG2 	0x0008
#define VIF0_REG3 	0x000C
#define VIF0_REG4 	0x0010
#define VIF0_REG5 	0x0014
#define VIF1_REG0 	0x0018
#define VIF1_REG1 	0x001C
#define VIF1_REG2 	0x0020
#define VIF1_REG3 	0x0024
#define VIF1_REG4 	0x0028
#define VIF1_REG5 	0x002C
#define SCL_REG0  	0x0030
#define SCL_EN          (1<<0)

#define SCL_REG1  	0x0034
#define SCL_REG2  	0x0038
#define SCL_REG3  	0x003C
#define SCL_REG4  	0x0040
#define SCL_REG5  	0x0044
#define SCL_REG6  	0x0048
#define SCL_REG7  	0x004C
#define SCL_REG8  	0x0050
#define FRC_REG   	0x0054
#define CRU_CLKSEL0_CON 0x0058
#define CRU_CLKSEL1_CON 0x005C
#define CRU_CODEC_DIV	0x0060
#define CRU_CLKSE2_CON  0x0064
#define CRU_PLL0_CON0   0x0068
#define CRU_PLL0_CON1   0x006C
#define CRU_PLL0_CON2   0x0070
#define CRU_PLL1_CON0   0x0074
#define CRU_PLL1_CON1   0x0078
#define CRU_PLL1_CON2   0x007C
#define CRU_I2C_CON0    0x0080
#define CRU_LVDS_CON0   0x0084
#define CRU_IO_CON0    	0x0088
#define CRU_IO_CON1   	0x008C
#define CRU_PCM2IS2_CON0	0x0090
#define CRU_PCM2IS2_CON1	0x0094
#define CRU_PCM2IS2_CON2	0x0098
#define CRU_CFGMISC_CON		0x009C















struct rk616_platform_data {
	int (*power_init)(void);
	int scl_rate;
};
struct mfd_rk616 {
	struct mutex reg_lock;

	struct device *dev;
	unsigned int irq_base;
	struct rk616_platform_data *pdata;
	struct i2c_client *client;
	int (*read_dev)(struct mfd_rk616 *rk616, u16 reg,u32 *pval);
	int (*write_dev)(struct mfd_rk616 *rk616,u16 reg,u32 *pval);
};
#endif

