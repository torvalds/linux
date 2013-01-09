#ifndef __ANX6345_H_
#define __ANX6345_H_

#include<linux/rk_screen.h>
#include<linux/earlysuspend.h>
#include<linux/anx9805.h>

#define ANX6345_SCL_RATE (100*1000)

#define MAX_REG     	0xf0
#define MAX_BUF_CNT 	6


#define DP_TX_PORT0_ADDR 0x70
#define HDMI_TX_PORT0_ADDR 0x72

/***************************************************************/
//  DEV_ADDR = 0x7A or 0x7B , MIPI Rx Registers
#define MIPI_ANALOG_PWD_CTRL0				 0x00
#define MIPI_ANALOG_PWD_CTRL1			        0x01
#define MIPI_ANALOG_PWD_CTRL2			        0x02

#define MIPI_MISC_CTRL                         0x03 

#define MIPI_TIMING_REG0                    0x04 
#define MIPI_TIMING_REG1                    0x05 
#define MIPI_TIMING_REG2                    0x06 
#define MIPI_TIMING_REG3                    0x07
#define MIPI_TIMING_REG4                    0x08 
#define MIPI_TIMING_REG5                    0x09 
#define MIPI_TIMING_REG6                    0x0a 

#define MIPI_HS_JITTER_REG                 0x0B

#define MIPI_VID_STABLE_CNT               0x0C 

#define MIPI_ANALOG_CTRL0                  0x0D 
#define MIPI_ANALOG_CTRL1                  0x0E
#define MIPI_ANALOG_CTRL2                  0x0F 

#define MIPI_PRBS_REG                           0x10 
#define MIPI_PROTOCOL_STATE               0x11 


//End for DEV_addr 0x7A/0x7E

/***************************************************************/
//  DEV_ADDR = 0x70 or 0x78 , Displayport mode and HDCP registers
#define SP_TX_HDCP_STATUS							  				0x00
#define SP_TX_HDCP_AUTH_PASS						  			0x02//bit position

#define SP_TX_HDCP_CONTROL_0_REG                  		0x01
#define SP_TX_HDCP_CONTROL_0_STORE_AN            0x80//bit position
#define SP_TX_HDCP_CONTROL_0_RX_REPEATER   	0x40//bit position
#define SP_TX_HDCP_CONTROL_0_RE_AUTH              0x20//bit position
#define SP_TX_HDCP_CONTROL_0_SW_AUTH_OK       0x10//bit position
#define SP_TX_HDCP_CONTROL_0_HARD_AUTH_EN   0x08//bit position
#define SP_TX_HDCP_CONTROL_0_HDCP_ENC_EN      0x04//bit position
#define SP_TX_HDCP_CONTROL_0_BKSV_SRM_PASS  0x02//bit position
#define SP_TX_HDCP_CONTROL_0_KSVLIST_VLD        0x01//bit position


#define SP_TX_HDCP_CONTROL_1_REG                  		0x02
#define SP_TX_HDCP_CONTROL_1_DDC_NO_STOP      			0x20//bit position
#define SP_TX_HDCP_CONTROL_1_DDC_NO_ACK        			0x10//bit position
#define SP_TX_HDCP_CONTROL_1_EDDC_NO_ACK          		0x08//bit position
//#define SP_TX_HDCP_CONTROL_1_HDCP_EMB_SCREEN_EN   		0x04//bit position
#define SP_TX_HDCP_CONTROL_1_RCV_11_EN                  0x02//bit position
#define SP_TX_HDCP_CONTROL_1_HDCP_11_EN           		0x01//bit position

#define SP_TX_HDCP_LINK_CHK_FRAME_NUM				 	0x03
#define SP_TX_HDCP_CONTROL_2_REG						0x04

#define SP_TX_HDCP_AKSV0								0x05
#define SP_TX_HDCP_AKSV1								0x06
#define SP_TX_HDCP_AKSV2								0x07
#define SP_TX_HDCP_AKSV3								0x08
#define SP_TX_HDCP_AKSV4								0x09

//AKSV
#define SP_TX_HDCP_AN0									0x0A
#define SP_TX_HDCP_AN1									0x0B
#define SP_TX_HDCP_AN2									0x0C
#define SP_TX_HDCP_AN3									0x0D
#define SP_TX_HDCP_AN4									0x0E
#define SP_TX_HDCP_AN5									0x0F
#define SP_TX_HDCP_AN6									0x10
#define SP_TX_HDCP_AN7									0x11

//BKSV
#define SP_TX_HDCP_BKSV0								0x12
#define SP_TX_HDCP_BKSV1								0x13
#define SP_TX_HDCP_BKSV2								0x14
#define SP_TX_HDCP_BKSV3								0x15
#define SP_TX_HDCP_BKSV4								0x16

#define SP_TX_HDCP_R0_L									0x17
#define SP_TX_HDCP_R0_H									0x18

#define M_VID_0 0xC0
#define M_VID_1 0xC1
#define M_VID_2 0xC2
#define N_VID_0 0xC3
#define N_VID_1 0xC4
#define N_VID_2 0xC5

#define SP_TX_HDCP_R0_WAIT_Timer					 0x40



#define SP_TX_SYS_CTRL1_REG           					0x80
//#define SP_TX_SYS_CTRL1_PD_IO         					0x80    // bit position
//#define SP_TX_SYS_CTRL1_PD_VID        					0x40    // bit position
//#define SP_TX_SYS_CTRL1_PD_LINK       					0x20    // bit position
//#define SP_TX_SYS_CTRL1_PD_TOTAL      					0x10    // bit position
//#define SP_TX_SYS_CTRL1_MODE_SEL      					0x08    // bit position
#define SP_TX_SYS_CTRL1_DET_STA       					0x04    // bit position
#define SP_TX_SYS_CTRL1_FORCE_DET     					0x02    // bit position
#define SP_TX_SYS_CTRL1_DET_CTRL      					0x01    // bit position

#define SP_TX_SYS_CTRL2_REG           					0x81
// #define SP_TX_SYS_CTRL2_ENHANCED 	  					0x08	  //bit position
#define SP_TX_SYS_CTRL2_CHA_STA       					0x04    // bit position
#define SP_TX_SYS_CTRL2_FORCE_CHA     					0x02    // bit position
#define SP_TX_SYS_CTRL2_CHA_CTRL      					0x01    // bit position

#define SP_TX_SYS_CTRL3_REG           					0x82
#define SP_TX_SYS_CTRL3_HPD_STATUS    					0x40    // bit position
#define SP_TX_SYS_CTRL3_F_HPD         					0x20    // bit position
#define SP_TX_SYS_CTRL3_HPD_CTRL      					0x10    // bit position
#define SP_TX_SYS_CTRL3_STRM_VALID    					0x04    // bit position
#define SP_TX_SYS_CTRL3_F_VALID       					0x02    // bit position
#define SP_TX_SYS_CTRL3_VALID_CTRL    					0x01    // bit position

#define SP_TX_SYS_CTRL4_REG			  					0x83
#define SP_TX_SYS_CTRL4_ENHANCED 	  					0x08//bit position

#define SP_TX_VID_CTRL				  					0x84

#define SP_TX_AUD_CTRL									0x87
#define SP_TX_AUD_CTRL_AUD_EN							0x01


#define SP_TX_PKT_EN_REG              					0x90
#define SP_TX_PKT_AUD_UP								0x80  // bit position
#define SP_TX_PKT_AVI_UD              					0x40  // bit position
#define SP_TX_PKT_MPEG_UD             					0x20  // bit position    
#define SP_TX_PKT_SPD_UD              					0x10  // bit position   
#define SP_TX_PKT_AUD_EN								0x08  // bit position=
#define SP_TX_PKT_AVI_EN              					0x04  // bit position          
#define SP_TX_PKT_MPEG_EN             					0x02  // bit position     
#define SP_TX_PKT_SPD_EN              					0x01  // bit position       


#define SP_TX_HDCP_CTRL 												0x92

#define SP_TX_LINK_BW_SET_REG         				 0xA0
#define SP_TX_LANE_COUNT_SET_REG      				 0xA1

#define SP_TX_TRAINING_PTN_SET_REG                   0xA2
#define SP_TX_SCRAMBLE_DISABLE						 0x20//bit 5

#define SP_TX_TRAINING_LANE0_SET_REG                 				0xA3
#define SP_TX_TRAINING_LANE0_SET_MAX_PRE_REACH        0x20        // bit position
#define SP_TX_TRAINING_LANE0_SET_MAX_DRIVE_REACH     0x04        // bit position

#define SP_TX_TRAINING_LANE1_SET_REG                0xA4


#define SSC_CTRL_REG1					 0xA7
#define SPREAD_AMP						 0x10//bit 4
#define MODULATION_FREQ					 0x01//bit 0


#define SP_TX_LINK_TRAINING_CTRL_REG                0xA8
#define SP_TX_LINK_TRAINING_CTRL_EN                 0x01        // bit position


#define SP_TX_DEBUG_REG1							0xB0
#define SP_TX_DEBUG_HPD_POLLING_DET						0x40//bit position
#define SP_TX_DEBUG_HPD_POLLING_EN						0x20//bit position
#define SP_TX_DEBUG_PLL_LOCK						0x10//bit position


#define SP_TX_LINK_DEBUG_REG                        0xB8
#define SP_TX_LINK_DEBUG_INSERT_ER                  0x02        // bit position
#define SP_TX_LINK_DEBUG_PRBS31_EN                  0x01        // bit position

#define SP_TX_SINK_COUNT_REG                0xB9

#define SP_TX_LINK_STATUS_REG1                               0xBB

#define SP_TX_SINK_STATUS_REG                                   0xBE
#define SP_TX_SINK_STATUS_SINK_STATUS_1          	0x02        // bit position
#define SP_TX_SINK_STATUS_SINK_STATUS_0          	0x01        // bit position


//#define SP_TX_LINK_TEST_COUNT                     0xC0


#define SP_TX_PLL_CTRL_REG											0xC7	
#define SP_TX_PLL_CTRL_PLL_PD           						0x80        // bit position
#define SP_TX_PLL_CTRL_PLL_RESET        					0x40        // bit position 
//#define SP_TX_PLL_CTRL_CPREG_BLEED      					0x08        // bit position 

#define SP_TX_ANALOG_POWER_DOWN_REG                   			0xC8
#define SP_TX_ANALOG_POWER_DOWN_MACRO_PD              	0x20        // bit position 
#define SP_TX_ANALOG_POWER_DOWN_AUX_PD                		0x10        // bit position 
//#define SP_TX_ANALOG_POWER_DOWN_CH3_PD                		0x08        // bit position 
//#define SP_TX_ANALOG_POWER_DOWN_CH2_PD                		0x04        // bit position 
#define SP_TX_ANALOG_POWER_DOWN_CH1_PD                		0x02        // bit position 
#define SP_TX_ANALOG_POWER_DOWN_CH0_PD                		0x01        // bit position 


#define SP_TX_ANALOG_TEST_REG                         					0xC9
#define SP_TX_ANALOG_TEST_MACRO_RST                   				0x20       // bit position 
#define SP_TX_ANALOG_TEST_PLL_TEST                    				0x10       // bit position 
#define SP_TX_ANALOG_TEST_CH3_TEST                    				0x08       // bit position 
#define SP_TX_ANALOG_TEST_CH2_TEST                    				0x04       // bit position 
#define SP_TX_ANALOG_TEST_CH1_TEST                    				0x02       // bit position 
#define SP_TX_ANALOG_TEST_CH0_TEST                    				0x01       // bit position 

#define SP_TX_GNS_CTRL_REG                            							0xCD
#define SP_EQ_LOOP_CNT											0x40//bit position
#define SP_TX_VIDEO_MAP_CTRL                 			                            0x02       // bit position 
#define SP_TX_RS_CTRL                        					              	0x01       // bit position 

#define SP_TX_DOWN_SPREADING_CTRL1                                               0xD0   //guochuncheng
#define SP_TX_DOWN_SPREADING_CTRL2                                               0xD1
#define SP_TX_DOWN_SPREADING_CTRL3                                               0xD2
#define SP_TX_SSC_D_CTRL                                                             0x40       //bit position
#define SP_TX_FS_CTRL_TH_CTRL                                                   0x20       //bit position

#define SP_TX_M_CALCU_CTRL												0xD9
#define M_GEN_CLK_SEL													0x01//bit 0


#define SP_TX_EXTRA_ADDR_REG											0xCE
#define SP_TX_I2C_STRETCH_CTRL_REG                                                              0xDB
#define SP_TX_AUX_STATUS            										0xE0
#define SP_TX_DEFER_CTRL_REG            									0xE2
#define SP_TXL_DEFER_CTRL_EN  					                     		       0x80       // bit position 

#define SP_TX_BUF_DATA_COUNT_REG											0xE4
#define SP_TX_AUX_CTRL_REG              										0xE5
#define SP_TX_MOT_BIT													0x04//bit 2

#define SP_TX_AUX_ADDR_7_0_REG          									0xE6
#define SP_TX_AUX_ADDR_15_8_REG         									0xE7
#define SP_TX_AUX_ADDR_19_16_REG        									0xE8

#define SP_TX_AUX_CTRL_REG2                                                 0xE9
#define SP_TX_ADDR_ONLY_BIT													0x02//bit 1

#define SP_TX_BUF_DATA_0_REG                          0xf0
#define SP_TX_BUF_DATA_1_REG                          0xf1
#define SP_TX_BUF_DATA_2_REG                          0xf2
#define SP_TX_BUF_DATA_3_REG                          0xf3
#define SP_TX_BUF_DATA_4_REG                          0xf4
#define SP_TX_BUF_DATA_5_REG                          0xf5
#define SP_TX_BUF_DATA_6_REG                          0xf6
#define SP_TX_BUF_DATA_7_REG                          0xf7
#define SP_TX_BUF_DATA_8_REG                          0xf8
#define SP_TX_BUF_DATA_9_REG                          0xf9
#define SP_TX_BUF_DATA_10_REG                         0xfa
#define SP_TX_BUF_DATA_11_REG                         0xfb
#define SP_TX_BUF_DATA_12_REG                         0xfc
#define SP_TX_BUF_DATA_13_REG                         0xfd
#define SP_TX_BUF_DATA_14_REG                         0xfe
#define SP_TX_BUF_DATA_15_REG                         0xff

//End for Address 0x70 or 0x78

/***************************************************************/
//  DEV_ADDR = 0x72 or 0x76, System control registers
#define SP_TX_VND_IDL_REG             	0x00
#define SP_TX_VND_IDH_REG             	0x01
#define SP_TX_DEV_IDL_REG             	0x02
#define SP_TX_DEV_IDH_REG             	0x03
#define SP_TX_DEV_REV_REG             	0x04

#define SP_POWERD_CTRL_REG			  	0x05
#define SP_POWERD_REGISTER_REG			0x80// bit position
//#define SP_POWERD_MISC_REG			  	0x40// bit position
#define SP_POWERD_IO_REG			  	0x20// bit position
#define SP_POWERD_AUDIO_REG				0x10// bit position
#define SP_POWERD_VIDEO_REG			  	0x08// bit position
#define SP_POWERD_LINK_REG			  	0x04// bit position
#define SP_POWERD_TOTAL_REG			  	0x02// bit position
#define SP_MODE_SEL_REG				  	0x01// bit position

#define SP_TX_RST_CTRL_REG            	0x06
#define SP_TX_RST_MISC_REG 			  	0x80	// bit position
#define SP_TX_RST_VIDCAP_REG		  	0x40	// bit position
#define SP_TX_RST_VIDFIF_REG          	0x20    // bit position
#define SP_TX_RST_AUDFIF_REG          	0x10    // bit position
#define SP_TX_RST_AUDCAP_REG         	0x08    // bit position
#define SP_TX_RST_HDCP_REG            	0x04    // bit position
#define SP_TX_RST_SW_RST             	0x02    // bit position
#define SP_TX_RST_HW_RST             	0x01    // bit position

#define SP_TX_RST_CTRL2_REG				0x07
#define SP_TX_RST_SSC					0x80//bit position
#define SP_TX_AC_MODE					0x40//bit position
//#define SP_TX_DDC_RST					0x10//bit position
//#define SP_TX_TMDS_BIST_RST				0x08//bit position
#define SP_TX_AUX_RST					0x04//bit position
#define SP_TX_SERDES_FIFO_RST			0x02//bit position
#define SP_TX_I2C_REG_RST				0x01//bit position


#define SP_TX_VID_CTRL1_REG           	0x08
#define SP_TX_VID_CTRL1_VID_EN       0x80    // bit position
#define SP_TX_VID_CTRL1_VID_MUTE   0x40    // bit position
#define SP_TX_VID_CTRL1_DE_GEN      0x20    // bit position
#define SP_TX_VID_CTRL1_DEMUX        0x10    // bit position
#define SP_TX_VID_CTRL1_IN_BIT		  	0x04    // bit position
#define SP_TX_VID_CTRL1_DDRCTRL		0x02    // bit position
#define SP_TX_VID_CTRL1_EDGE		  		0x01    // bit position

#define SP_TX_VID_CTRL2_REG           	0x09
#define SP_TX_VID_CTRL1_YCBIT_SEL  		0x04    // bit position

#define SP_TX_VID_CTRL3_REG           	0x0A

#define SP_TX_VID_CTRL4_REG           	0x0B
#define SP_TX_VID_CTRL4_E_SYNC_EN	  	0x80	  //bit position
#define SP_TX_VID_CTRL4_EX_E_SYNC    0x40    // bit position
#define SP_TX_VID_CTRL4_BIST          		0x08    // bit position
#define SP_TX_VID_CTRL4_BIST_WIDTH   0x04        // bit position

#define SP_TX_VID_CTRL5_REG           		0x0C

#define SP_TX_VID_CTRL6_REG           		0x0D
#define SP_TX_VID_UPSAMPLE					0x02//bit position

#define SP_TX_VID_CTRL7_REG           		0x0E
#define SP_TX_VID_CTRL8_REG           		0x0F
#define SP_TX_VID_CTRL9_REG           		0x10

#define SP_TX_VID_CTRL10_REG           	0x11
#define SP_TX_VID_CTRL10_INV_F         	0x08    // bit position
#define SP_TX_VID_CTRL10_I_SCAN        	0x04    // bit position
#define SP_TX_VID_CTRL10_VSYNC_POL   0x02    // bit position
#define SP_TX_VID_CTRL10_HSYNC_POL   0x01    // bit position

#define SP_TX_TOTAL_LINEL_REG         0x12
#define SP_TX_TOTAL_LINEH_REG         0x13
#define SP_TX_ACT_LINEL_REG           0x14
#define SP_TX_ACT_LINEH_REG           0x15
#define SP_TX_VF_PORCH_REG            0x16
#define SP_TX_VSYNC_CFG_REG           0x17
#define SP_TX_VB_PORCH_REG            0x18
#define SP_TX_TOTAL_PIXELL_REG        0x19
#define SP_TX_TOTAL_PIXELH_REG        0x1A
#define SP_TX_ACT_PIXELL_REG          0x1B
#define SP_TX_ACT_PIXELH_REG          0x1C
#define SP_TX_HF_PORCHL_REG           0x1D
#define SP_TX_HF_PORCHH_REG           0x1E
#define SP_TX_HSYNC_CFGL_REG          0x1F
#define SP_TX_HSYNC_CFGH_REG          0x20
#define SP_TX_HB_PORCHL_REG           0x21
#define SP_TX_HB_PORCHH_REG           0x22

#define SP_TX_VID_STATUS						0x23

#define SP_TX_TOTAL_LINE_STA_L        0x24
#define SP_TX_TOTAL_LINE_STA_H        0x25
#define SP_TX_ACT_LINE_STA_L          0x26
#define SP_TX_ACT_LINE_STA_H          0x27
#define SP_TX_V_F_PORCH_STA           0x28
#define SP_TX_V_SYNC_STA              0x29
#define SP_TX_V_B_PORCH_STA           0x2A
#define SP_TX_TOTAL_PIXEL_STA_L       0x2B
#define SP_TX_TOTAL_PIXEL_STA_H       0x2C
#define SP_TX_ACT_PIXEL_STA_L         0x2D
#define SP_TX_ACT_PIXEL_STA_H         0x2E
#define SP_TX_H_F_PORCH_STA_L         0x2F
#define SP_TX_H_F_PORCH_STA_H         0x30
#define SP_TX_H_SYNC_STA_L            0x31
#define SP_TX_H_SYNC_STA_H            0x32
#define SP_TX_H_B_PORCH_STA_L         0x33
#define SP_TX_H_B_PORCH_STA_H         0x34

#define SP_TX_Video_Interface_BIST    0x35

#define SPDIF_AUDIO_CTRL0			0x36
#define SPDIF_AUDIO_CTRL0_SPDIF_IN  0x80 // bit position

#define SPDIF_AUDIO_STATUS0			0x38
#define SPDIF_AUDIO_STATUS0_CLK_DET 0x80
#define SPDIF_AUDIO_STATUS0_AUD_DET 0x01

#define SPDIF_AUDIO_STATUS1 0x39

#define AUDIO_BIST_CTRL 0x3c
#define AUDIO_BIST_EN 0x01

//#define AUDIO_BIST_CHANNEL_STATUS1 0xd0
//#define AUDIO_BIST_CHANNEL_STATUS2 0xd1
//#define AUDIO_BIST_CHANNEL_STATUS3 0xd2
//#define AUDIO_BIST_CHANNEL_STATUS4 0xd3
//#define AUDIO_BIST_CHANNEL_STATUS5 0xd4

#define SP_TX_VIDEO_BIT_CTRL_0_REG                    0x40
#define SP_TX_VIDEO_BIT_CTRL_1_REG                    0x41
#define SP_TX_VIDEO_BIT_CTRL_2_REG                    0x42
#define SP_TX_VIDEO_BIT_CTRL_3_REG                    0x43
#define SP_TX_VIDEO_BIT_CTRL_4_REG                    0x44
#define SP_TX_VIDEO_BIT_CTRL_5_REG                    0x45
#define SP_TX_VIDEO_BIT_CTRL_6_REG                    0x46
#define SP_TX_VIDEO_BIT_CTRL_7_REG                    0x47
#define SP_TX_VIDEO_BIT_CTRL_8_REG                    0x48
#define SP_TX_VIDEO_BIT_CTRL_9_REG                    0x49
#define SP_TX_VIDEO_BIT_CTRL_10_REG                   0x4a
#define SP_TX_VIDEO_BIT_CTRL_11_REG                   0x4b
#define SP_TX_VIDEO_BIT_CTRL_12_REG                   0x4c
#define SP_TX_VIDEO_BIT_CTRL_13_REG                   0x4d
#define SP_TX_VIDEO_BIT_CTRL_14_REG                   0x4e
#define SP_TX_VIDEO_BIT_CTRL_15_REG                   0x4f
#define SP_TX_VIDEO_BIT_CTRL_16_REG                   0x50
#define SP_TX_VIDEO_BIT_CTRL_17_REG                   0x51
#define SP_TX_VIDEO_BIT_CTRL_18_REG                   0x52
#define SP_TX_VIDEO_BIT_CTRL_19_REG                   0x53
#define SP_TX_VIDEO_BIT_CTRL_20_REG                   0x54
#define SP_TX_VIDEO_BIT_CTRL_21_REG                   0x55
#define SP_TX_VIDEO_BIT_CTRL_22_REG                   0x56
#define SP_TX_VIDEO_BIT_CTRL_23_REG                   0x57
#define SP_TX_VIDEO_BIT_CTRL_24_REG                   0x58
#define SP_TX_VIDEO_BIT_CTRL_25_REG                   0x59
#define SP_TX_VIDEO_BIT_CTRL_26_REG                   0x5a
#define SP_TX_VIDEO_BIT_CTRL_27_REG                   0x5b
#define SP_TX_VIDEO_BIT_CTRL_28_REG                   0x5c
#define SP_TX_VIDEO_BIT_CTRL_29_REG                   0x5d
#define SP_TX_VIDEO_BIT_CTRL_30_REG                   0x5e
#define SP_TX_VIDEO_BIT_CTRL_31_REG                   0x5f
#define SP_TX_VIDEO_BIT_CTRL_32_REG                   0x60
#define SP_TX_VIDEO_BIT_CTRL_33_REG                   0x61
#define SP_TX_VIDEO_BIT_CTRL_34_REG                   0x62
#define SP_TX_VIDEO_BIT_CTRL_35_REG                   0x63
#define SP_TX_VIDEO_BIT_CTRL_36_REG                   0x64
#define SP_TX_VIDEO_BIT_CTRL_37_REG                   0x65
#define SP_TX_VIDEO_BIT_CTRL_38_REG                   0x66
#define SP_TX_VIDEO_BIT_CTRL_39_REG                   0x67
#define SP_TX_VIDEO_BIT_CTRL_40_REG                   0x68
#define SP_TX_VIDEO_BIT_CTRL_41_REG                   0x69
#define SP_TX_VIDEO_BIT_CTRL_42_REG                   0x6a
#define SP_TX_VIDEO_BIT_CTRL_43_REG                   0x6b
#define SP_TX_VIDEO_BIT_CTRL_44_REG                   0x6c
#define SP_TX_VIDEO_BIT_CTRL_45_REG                   0x6d
#define SP_TX_VIDEO_BIT_CTRL_46_REG                   0x6e
#define SP_TX_VIDEO_BIT_CTRL_47_REG                   0x6f

//AVI info frame
#define SP_TX_AVI_TYPE              0x70
#define SP_TX_AVI_VER               0x71
#define SP_TX_AVI_LEN               0x72
#define SP_TX_AVI_DB0		     0x73
#define SP_TX_AVI_DB1               0x74
#define SP_TX_AVI_DB2               0x75
#define SP_TX_AVI_DB3               0x76
#define SP_TX_AVI_DB4               0x77
#define SP_TX_AVI_DB5               0x78
#define SP_TX_AVI_DB6               0x79
#define SP_TX_AVI_DB7               0x7A
#define SP_TX_AVI_DB8               0x7B
#define SP_TX_AVI_DB9               0x7C
#define SP_TX_AVI_DB10              0x7D
#define SP_TX_AVI_DB11              0x7E
#define SP_TX_AVI_DB12              0x7F
#define SP_TX_AVI_DB13              0x80
#define SP_TX_AVI_DB14              0x81
#define SP_TX_AVI_DB15              0x82

//Audio info frame
#define SP_TX_AUD_TYPE			 0x83
#define SP_TX_AUD_VER			 0x84
#define SP_TX_AUD_LEN			 0x85
#define SP_TX_AUD_DB0			 0x86
#define SP_TX_AUD_DB1			 0x87
#define SP_TX_AUD_DB2			 0x88
#define SP_TX_AUD_DB3			 0x89
#define SP_TX_AUD_DB4			 0x8A
#define SP_TX_AUD_DB5			 0x8B
#define SP_TX_AUD_DB6			 0x8C
#define SP_TX_AUD_DB7			 0x8D
#define SP_TX_AUD_DB8			 0x8E
#define SP_TX_AUD_DB9			 0x8F
#define SP_TX_AUD_DB10			 0x90

//SPD info frame
#define SP_TX_SPD_TYPE                0x91
#define SP_TX_SPD_VER                 0x92
#define SP_TX_SPD_LEN                 0x93
#define SP_TX_SPD_DATA0		0x94
#define SP_TX_SPD_DATA1               0x95
#define SP_TX_SPD_DATA2               0x96
#define SP_TX_SPD_DATA3               0x97
#define SP_TX_SPD_DATA4               0x98
#define SP_TX_SPD_DATA5               0x99
#define SP_TX_SPD_DATA6               0x9A
#define SP_TX_SPD_DATA7               0x9B
#define SP_TX_SPD_DATA8               0x9C
#define SP_TX_SPD_DATA9               0x9D
#define SP_TX_SPD_DATA10              0x9E
#define SP_TX_SPD_DATA11              0x9F
#define SP_TX_SPD_DATA12              0xA0
#define SP_TX_SPD_DATA13              0xA1
#define SP_TX_SPD_DATA14              0xA2
#define SP_TX_SPD_DATA15              0xA3
#define SP_TX_SPD_DATA16              0xA4
#define SP_TX_SPD_DATA17              0xA5
#define SP_TX_SPD_DATA18              0xA6
#define SP_TX_SPD_DATA19              0xA7
#define SP_TX_SPD_DATA20              0xA8
#define SP_TX_SPD_DATA21              0xA9
#define SP_TX_SPD_DATA22              0xAA
#define SP_TX_SPD_DATA23              0xAB
#define SP_TX_SPD_DATA24              0xAC
#define SP_TX_SPD_DATA25              0xAD
#define SP_TX_SPD_DATA26              0xAE
#define SP_TX_SPD_DATA27              0xAF

//Mpeg source info frame
#define SP_TX_MPEG_TYPE               0xB0
#define SP_TX_MPEG_VER                0xB1
#define SP_TX_MPEG_LEN                0xB2
#define SP_TX_MPEG_DATA0              0xB3
#define SP_TX_MPEG_DATA1              0xB4
#define SP_TX_MPEG_DATA2              0xB5
#define SP_TX_MPEG_DATA3              0xB6
#define SP_TX_MPEG_DATA4              0xB7
#define SP_TX_MPEG_DATA5              0xB8
#define SP_TX_MPEG_DATA6              0xB9
#define SP_TX_MPEG_DATA7              0xBA
#define SP_TX_MPEG_DATA8              0xBB
#define SP_TX_MPEG_DATA9              0xBC
#define SP_TX_MPEG_DATA10             0xBD
#define SP_TX_MPEG_DATA11            0xBE
#define SP_TX_MPEG_DATA12            0xBF
#define SP_TX_MPEG_DATA13            0xC0
#define SP_TX_MPEG_DATA14            0xC1
#define SP_TX_MPEG_DATA15            0xC2
#define SP_TX_MPEG_DATA16            0xC3
#define SP_TX_MPEG_DATA17            0xC4
#define SP_TX_MPEG_DATA18            0xC5
#define SP_TX_MPEG_DATA19            0xC6
#define SP_TX_MPEG_DATA20            0xC7
#define SP_TX_MPEG_DATA21            0xC8
#define SP_TX_MPEG_DATA22            0xC9
#define SP_TX_MPEG_DATA23            0xCA
#define SP_TX_MPEG_DATA24            0xCB
#define SP_TX_MPEG_DATA25            0xCC
#define SP_TX_MPEG_DATA26            0xCD
#define SP_TX_MPEG_DATA27            0xCE

//#define GNSS_CTRL_REG				0xCD
//#define ENABLE_SSC_FILTER			0x80//bit 

//#define SSC_D_VALUE					 0xD0
//#define SSC_CTRL_REG2					 0xD1

#define ANALOG_DEBUG_REG1			0xDC
#define ANALOG_SEL_BG				0x40//bit 4
#define ANALOG_SWING_A_30PER		0x08//bit 3

#define ANALOG_DEBUG_REG2			0xDD
#define ANALOG_24M_SEL				0x08//bit 3
//#define ANALOG_FILTER_ENABLED		0x10//bit 4


#define ANALOG_DEBUG_REG3			0xDE

#define PLL_FILTER_CTRL1			0xDF
#define PD_RING_OSC					0x40//bit 6

#define PLL_FILTER_CTRL2			0xE0
#define PLL_FILTER_CTRL3			0xE1
#define PLL_FILTER_CTRL4			0xE2
#define PLL_FILTER_CTRL5			0xE3
#define PLL_FILTER_CTRL6			0xE4

#define SP_TX_I2S_CTRL			0xE6
#define SP_TX_I2S_FMT			0xE7
#define SP_TX_I2S_CH_Status1			0xD0
#define SP_TX_I2S_CH_Status2			0xD1
#define SP_TX_I2S_CH_Status3			0xD2
#define SP_TX_I2S_CH_Status4			0xD3
#define SP_TX_I2S_CH_Status5			0xD4

//interrupt
#define SP_COMMON_INT_STATUS1     0xF1
#define SP_COMMON_INT1_PLL_LOCK_CHG 	0x40//bit position
#define SP_COMMON_INT1_VIDEO_FORMAT_CHG 0x08//bit position
#define SP_COMMON_INT1_AUDIO_CLK_CHG	0x04//bit position
#define SP_COMMON_INT1_VIDEO_CLOCK_CHG  0x02//bit position


#define SP_COMMON_INT_STATUS2	  0xF2
#define SP_COMMON_INT2_AUTHCHG	  0x02 //bit position
#define SP_COMMON_INT2_AUTHDONE	  0x01 //bit position

#define SP_COMMON_INT_STATUS3	  0xF3
#define SP_COMMON_INT3_AFIFO_UNDER	0x80//bit position
#define SP_COMMON_INT3_AFIFO_OVER	0x40//bit position

#define SP_COMMON_INT_STATUS4	    0xF4
#define SP_COMMON_INT4_PLUG                0x01   // bit position
#define SP_COMMON_INT4_ESYNC_ERR          0x10   // bit position
#define SP_COMMON_INT4_HPDLOST		0x02   //bit position
#define SP_COMMON_INT4_HPD_CHANGE   0x04   //bit position


#define SP_TX_INT_STATUS1		  0xF7
#define SP_TX_INT_STATUS1_HPD	  0x40 //bit position
#define SP_TX_INT_STATUS1_TRAINING_Finish       0x20   // bit position
#define SP_TX_INT_STATUS1_POLLING_ERR        0x10   // bit position

#define SP_TX_INT_SINK_CHG		  0x08//bit position

//interrupt mask
#define SP_COMMON_INT_MASK1			  0xF8
#define SP_COMMON_INT_MASK2			  0xF9
#define SP_COMMON_INT_MASK3			  0xFA
#define SP_COMMON_INT_MASK4			  0xFB
#define SP_INT_MASK					  					0xFE
#define SP_TX_INT_CTRL_REG            		0xFF
//End for dev_addr 0x72 or 0x76

/***************************************************************/
/***************************************************************/

//DPCD regs
#define DPCD_DPCD_REV                                                0x00
#define DPCD_MAX_LINK_RATE                                      0x01
#define DPCD_MAX_LANE_COUNT                                   0x02
#define DPCD_MAX_DOWNSPREAD                                  0x03
#define DPCD_NORP                                                        0x04
#define DPCD_DOWNSTREAMPORT_PRESENT                  0x05

#define DPCD_RECEIVE_PORT0_CAP_0                        0x08
#define DPCD_RECEIVE_PORT0_CAP_1                        0x09
#define DPCD_RECEIVE_PORT0_CAP_2                        0x0a
#define DPCD_RECEIVE_PORT0_CAP_3                        0x0b

#define DPCD_LINK_BW_SET                                      0x00
#define DPCD_LANE_COUNT_SET                                0x01
#define DPCD_TRAINING_PATTERN_SET                     0x02
#define DPCD_TRAINNIG_LANE0_SET                         0x03
#define DPCD_TRAINNIG_LANE1_SET                         0x04
#define DPCD_TRAINNIG_LANE2_SET                         0x05
#define DPCD_TRAINNIG_LANE3_SET                         0x06
#define DPCD_DOWNSPREAD_CTRL                            0x07

#define DPCD_SINK_COUNT                                          0x00
#define DPCD_DEVICE_SERVICE_IRQ_VECTOR              0x01
#define DPCD_LANE0_1_STATUS                                   0x02
#define DPCD_LANE2_3_STATUS                                   0x03
#define DPCD_LANE_ALIGN_STATUS_UPDATED            0x04
#define DPCD_SINK_STATUS                                          0x05
#define DPCD_ADJUST_REQUEST_LANE0_1                     0x06
#define DPCD_ADJUST_REQUEST_LANE2_3                     0x07
#define DPCD_TRAINING_SCORE_LANE0                       0x08
#define DPCD_TRAINING_SCORE_LANE1                       0x09
#define DPCD_TRAINING_SCORE_LANE2                       0x0a
#define DPCD_TRAINING_SCORE_LANE3                       0x0b

#define DPCD_TEST_REQUEST                               0x18
#define DPCD_TEST_LINK_RATE                             0x19

#define DPCD_TEST_LANE_COUNT                            0x20

#define DPCD_TEST_Response                              0x60
#define TEST_ACK                                                  0x01
#define DPCD_TEST_EDID_Checksum_Write                   0x04//bit position

#define DPCD_TEST_EDID_Checksum                         0x61


#define DPCD_SPECIFIC_INTERRUPT                               0x10
#define DPCD_USER_COMM1                                             0x22//define for downstream HDMI Rx sense detection


struct  anx6345_platform_data {
	unsigned int dvdd33_en_pin;
	int 	     dvdd33_en_val;
	unsigned int dvdd18_en_pin;
	int 	     dvdd18_en_val;
	unsigned int edp_rst_pin;
	int (*power_ctl)(void);
};

struct edp_anx6345 {
	struct i2c_client *client;
	struct anx6345_platform_data *pdata;
	int (*edp_anx_init)(struct i2c_client *client);
	rk_screen screen;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 
};

#endif
