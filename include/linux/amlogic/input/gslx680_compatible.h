#ifndef _GSLX680_COMPATIBLE_H_
#define _GSLX680_COMPATIBLE_H_

#include "linux/amlogic/input/common.h"
#include "linux/amlogic/input/aml_gsl_common.h"

//#define GSL_DEBUG
//#define HAVE_TOUCH_KEY
//#define STRETCH_FRAME
#define SLEEP_CLEAR_POINT
#define FILTER_POINT
#define REPORT_DATA_ANDROID_4_0
#define GSL_NOID_VERSION
#define GSL_FW_FILE

#define GSLX680_CONFIG_MAX	1024
#define GSLX680_FW_MAX 			8192

#define GSLX680_I2C_NAME 		"gslx680_compatible"
#define GSLX680_I2C_ADDR 		0x40

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG	0xe0
#define GSL_PAGE_REG		0xf0

#define PRESS_MAX    		255
#define MAX_FINGERS 		10
#define MAX_CONTACTS 		10
#define DMA_TRANS_LEN		0x20

#define CHIP_3680B			1
#define CHIP_3680A			2
#define CHIP_3670				3
#define CHIP_1680E			130
#define CHIP_UNKNOWN		255

int SCREEN_MAX_X = 0;
int SCREEN_MAX_Y = 0;

#ifdef GSL_DEBUG 
#define print_info(fmt, args...)	\
	do{                           	\
			printk(fmt, ##args);				\
	}while(0)
#else
#define print_info touch_dbg
#endif

#ifdef FILTER_POINT
#define FILTER_MAX		9
#endif

#ifdef STRETCH_FRAME
#define CTP_MAX_X 		SCREEN_MAX_Y
#define CTP_MAX_Y 		SCREEN_MAX_X
#define X_STRETCH_MAX	(CTP_MAX_X/10)	/*X方向 拉伸的分辨率，一般设一个通道的分辨率*/
#define Y_STRETCH_MAX	(CTP_MAX_Y/15)	/*Y方向 拉伸的分辨率，一般设一个通道的分辨率*/
#define XL_RATIO_1	25	/*X方向 左边拉伸的分辨率第一级比例，百分比*/
#define XL_RATIO_2	45	/*X方向 左边拉伸的分辨率第二级比例，百分比*/
#define XR_RATIO_1	35	/*X方向 右边拉伸的分辨率第一级比例，百分比*/
#define XR_RATIO_2	55	/*X方向 右边拉伸的分辨率第二级比例，百分比*/
#define YL_RATIO_1	30	/*Y方向 左边拉伸的分辨率第一级比例，百分比*/
#define YL_RATIO_2	45	/*Y方向 左边拉伸的分辨率第二级比例，百分比*/
#define YR_RATIO_1	40	/*Y方向 右边拉伸的分辨率第一级比例，百分比*/
#define YR_RATIO_2	65	/*Y方向 右边拉伸的分辨率第二级比例，百分比*/
#define X_STRETCH_CUST	(CTP_MAX_X/10)	/*X方向 自定义拉伸的分辨率，一般设一个通道的分辨率*/
#define Y_STRETCH_CUST	(CTP_MAX_Y/15)	/*Y方向 自定义拉伸的分辨率，一般设一个通道的分辨率*/
#define X_RATIO_CUST	10	/*X方向 自定义拉伸的分辨率比例，百分比*/
#define Y_RATIO_CUST	10	/*Y方向 自定义拉伸的分辨率比例，百分比*/
#endif

extern struct touch_pdata *ts_com;

#ifndef GSL_FW_FILE
Warning: Please add config&firmware data of your project into following arrays.
static unsigned int GSLX680_CONFIG[] = {
};
static struct fw_data GSLX680_FW[] = {
};
#endif
#endif
