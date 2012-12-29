#ifndef _DP_TX_Reg_def_H
#define _DP_TX_Reg_def_H
/***************************************************************/
#include <linux/rk_screen.h>
#include<linux/earlysuspend.h>
#define RK_EDP_SCL_RATE (100*1000)

#define MAX_REG     0xf0
#define MAX_BUF_CNT 6

#define DP_TX_PORT0_ADDR 0x70
#define HDMI_TX_PORT0_ADDR 0x72
#define DP_TX_VND_IDL_REG             	0x00
#define DP_TX_VND_IDH_REG             	0x01
#define DP_TX_DEV_IDL_REG             	0x02
#define DP_TX_DEV_IDH_REG             	0x03
#define DP_POWERD_CTRL_REG			  	0x05

#define DP_TX_VID_CTRL1_REG           	0x08
#define DP_TX_VID_CTRL1_VID_EN     		0x80    // bit position
#define DP_POWERD_TOTAL_REG			  	0x02// bit position
#define DP_POWERD_AUDIO_REG				0x10// bit position

#define DP_TX_RST_CTRL_REG            	0x06
#define DP_TX_RST_CTRL2_REG				0x07
#define DP_TX_RST_HW_RST             	0x01    // bit position
#define DP_TX_AUX_RST					0x04//bit position
#define DP_TX_RST_SW_RST             	0x02    // bit position
#define DP_TX_PLL_CTRL_REG				0xC7
#define DP_TX_EXTRA_ADDR_REG			0xCE
#define DP_TX_PLL_FILTER_CTRL3			0xE1
#define DP_TX_PLL_CTRL3					0xE6
#define DP_TX_AC_MODE					0x40//bit position
#define ANALOG_DEBUG_REG1				0xDC
#define ANALOG_DEBUG_REG3				0xDE
#define DP_TX_PLL_FILTER_CTRL1		 	0xDF
#define DP_TX_PLL_FILTER_CTRL3			0xE1
#define DP_TX_PLL_FILTER_CTRL       	0xE2
#define DP_TX_LINK_DEBUG_REG            0xB8
#define DP_TX_GNS_CTRL_REG              0xCD
#define DP_TX_AUX_CTRL_REG2             0xE9
#define DP_TX_BUF_DATA_COUNT_REG		0xE4
#define DP_TX_AUX_CTRL_REG              0xE5
#define DP_TX_AUX_ADDR_7_0_REG          0xE6
#define DP_TX_AUX_ADDR_15_8_REG         0xE7
#define DP_TX_AUX_ADDR_19_16_REG        0xE8
#define DP_TX_BUF_DATA_0_REG            0xf0
#define DP_TX_SYS_CTRL4_REG			  	0x83
#define DP_TX_SYS_CTRL4_ENHANCED 	  	0x08//bit position
#define DP_TX_LINK_BW_SET_REG         	0xA0
#define DP_TX_LANE_COUNT_SET_REG      	0xA1
#define DP_TX_LINK_TRAINING_CTRL_REG    0xA8
#define DP_TX_LINK_TRAINING_CTRL_EN     0x01// bit position
#define DP_TX_TRAINING_LANE0_SET_REG    0xA3
#define DP_TX_TRAINING_LANE1_SET_REG    0xA4
#define DP_TX_TRAINING_LANE2_SET_REG    0xA5
#define DP_TX_TRAINING_LANE3_SET_REG    0xA6
#define DP_TX_SYS_CTRL1_REG           	0x80
#define DP_TX_SYS_CTRL1_DET_STA       	0x04// bit position
#define DP_TX_SYS_CTRL2_REG           	0x81
#define DP_TX_SYS_CTRL3_REG           	0x82
#define DP_TX_SYS_CTRL2_CHA_STA       	0x04// bit position
#define DP_TX_VID_CTRL2_REG           	0x09
#define DP_TX_TOTAL_LINEL_REG         	0x12
#define DP_TX_TOTAL_LINEH_REG         	0x13
#define DP_TX_ACT_LINEL_REG           	0x14
#define DP_TX_ACT_LINEH_REG           	0x15
#define DP_TX_VF_PORCH_REG            	0x16
#define DP_TX_VSYNC_CFG_REG           	0x17
#define DP_TX_VB_PORCH_REG            	0x18
#define DP_TX_TOTAL_PIXELL_REG        	0x19
#define DP_TX_TOTAL_PIXELH_REG        	0x1A
#define DP_TX_ACT_PIXELL_REG          	0x1B
#define DP_TX_ACT_PIXELH_REG          	0x1C
#define DP_TX_HF_PORCHL_REG           	0x1D
#define DP_TX_HF_PORCHH_REG           	0x1E
#define DP_TX_HSYNC_CFGL_REG          	0x1F
#define DP_TX_HSYNC_CFGH_REG          	0x20
#define DP_TX_HB_PORCHL_REG           	0x21
#define DP_TX_HB_PORCHH_REG           	0x22
#define DP_TX_VID_CTRL10_REG           	0x11
#define DP_TX_VID_CTRL4_REG           	0x0B
#define DP_TX_VID_CTRL4_E_SYNC_EN	  	0x80//bit position
#define DP_TX_VID_CTRL10_I_SCAN        	0x04// bit position
#define DP_TX_VID_CTRL10_VSYNC_POL   	0x02// bit position
#define DP_TX_VID_CTRL10_HSYNC_POL   	0x01// bit position
#define DP_TX_VID_CTRL4_BIST_WIDTH   	0x04// bit position
#define DP_TX_VID_CTRL4_BIST          	0x08// bit position


typedef enum
{
    COLOR_6,
    COLOR_8,
    COLOR_10,
    COLOR_12
}VIP_COLOR_DEPTH;

struct rk_edp_platform_data {
	unsigned int dvdd33_en_pin;
	int 	     dvdd33_en_val;
	unsigned int dvdd18_en_pin;
	int 	     dvdd18_en_val;
	unsigned int edp_rst_pin;
	int (*power_ctl)(void);
};

struct rk_edp {
	struct i2c_client *client;
	struct rk_edp_platform_data *pdata;
	rk_screen screen;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 
};

#endif




