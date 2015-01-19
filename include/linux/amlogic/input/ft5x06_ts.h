#ifndef __LINUX_FT5X0X_TS_H__
#define __LINUX_FT5X0X_TS_H__


#define PRESS_MAX       255

#define FT5X0X_NAME	"ft5x06"//"synaptics_i2c_rmi"//"synaptics-rmi-ts"// 

enum ft5x0x_ts_regs {
	FT5X0X_REG_THGROUP					= 0x80,
	FT5X0X_REG_THPEAK						= 0x81,
//	FT5X0X_REG_THCAL						= 0x82,
//	FT5X0X_REG_THWATER					= 0x83,
//	FT5X0X_REG_THTEMP					= 0x84,
//	FT5X0X_REG_THDIFF						= 0x85,				
//	FT5X0X_REG_CTRL						= 0x86,
	FT5X0X_REG_TIMEENTERMONITOR			= 0x87,
	FT5X0X_REG_PERIODACTIVE				= 0x88,
	FT5X0X_REG_PERIODMONITOR			= 0x89,
//	FT5X0X_REG_HEIGHT_B					= 0x8a,
//	FT5X0X_REG_MAX_FRAME					= 0x8b,
//	FT5X0X_REG_DIST_MOVE					= 0x8c,
//	FT5X0X_REG_DIST_POINT				= 0x8d,
//	FT5X0X_REG_FEG_FRAME					= 0x8e,
//	FT5X0X_REG_SINGLE_CLICK_OFFSET		= 0x8f,
//	FT5X0X_REG_DOUBLE_CLICK_TIME_MIN	= 0x90,
//	FT5X0X_REG_SINGLE_CLICK_TIME			= 0x91,
//	FT5X0X_REG_LEFT_RIGHT_OFFSET		= 0x92,
//	FT5X0X_REG_UP_DOWN_OFFSET			= 0x93,
//	FT5X0X_REG_DISTANCE_LEFT_RIGHT		= 0x94,
//	FT5X0X_REG_DISTANCE_UP_DOWN		= 0x95,
//	FT5X0X_REG_ZOOM_DIS_SQR				= 0x96,
//	FT5X0X_REG_RADIAN_VALUE				=0x97,
//	FT5X0X_REG_MAX_X_HIGH                       	= 0x98,
//	FT5X0X_REG_MAX_X_LOW             			= 0x99,
//	FT5X0X_REG_MAX_Y_HIGH            			= 0x9a,
//	FT5X0X_REG_MAX_Y_LOW             			= 0x9b,
//	FT5X0X_REG_K_X_HIGH            			= 0x9c,
//	FT5X0X_REG_K_X_LOW             			= 0x9d,
//	FT5X0X_REG_K_Y_HIGH            			= 0x9e,
//	FT5X0X_REG_K_Y_LOW             			= 0x9f,
	FT5X0X_REG_AUTO_CLB_MODE			= 0xa0,
//	FT5X0X_REG_LIB_VERSION_H 				= 0xa1,
//	FT5X0X_REG_LIB_VERSION_L 				= 0xa2,		
//	FT5X0X_REG_CIPHER						= 0xa3,
//	FT5X0X_REG_MODE						= 0xa4,
	FT5X0X_REG_PMODE						= 0xa5,	/* Power Consume Mode		*/	
	FT5X0X_REG_FIRMID						= 0xa6,
//	FT5X0X_REG_STATE						= 0xa7,
//	FT5X0X_REG_FT5201ID					= 0xa8,
	FT5X0X_REG_ERR						= 0xa9,
	FT5X0X_REG_CLB						= 0xaa,
};

//FT5X0X_REG_PMODE
#define PMODE_ACTIVE        0x00
#define PMODE_MONITOR       0x01
#define PMODE_STANDBY       0x02
#define PMODE_HIBERNATE     0x03

//Focaltech IC type
#define IC_FT5X06	0	/*ft5206,ft5306, ft5406*/
#define IC_FT5606	1	/*ft5506, ft5606*/
#define IC_FT5316	2
#define IC_FT6208	3

/*upgrade config of FT5606*/
#define FT5606_UPGRADE_AA_DELAY 		50
#define FT5606_UPGRADE_55_DELAY 		10
#define FT5606_UPGRADE_ID_1			0x79
#define FT5606_UPGRADE_ID_2			0x06
#define FT5606_UPGRADE_READID_DELAY 	100
#define FT5606_UPGRADE_EARSE_DELAY	2000


/*upgrade config of FT5316*/
#define FT5316_UPGRADE_AA_DELAY 		50
#define FT5316_UPGRADE_55_DELAY 		30
#define FT5316_UPGRADE_ID_1			0x79
#define FT5316_UPGRADE_ID_2			0x07
#define FT5316_UPGRADE_READID_DELAY 	1
#define FT5316_UPGRADE_EARSE_DELAY	1500


/*upgrade config of FT5x06(x=2,3,4)*/
#define FT5X06_UPGRADE_AA_DELAY 		50
#define FT5X06_UPGRADE_55_DELAY 		30
#define FT5X06_UPGRADE_ID_1			0x79
#define FT5X06_UPGRADE_ID_2			0x03
#define FT5X06_UPGRADE_READID_DELAY 	1
#define FT5X06_UPGRADE_EARSE_DELAY	2000

/*upgrade config of FT6208*/
#define FT6208_UPGRADE_AA_DELAY 		60
#define FT6208_UPGRADE_55_DELAY 		10
#define FT6208_UPGRADE_ID_1			0x79
#define FT6208_UPGRADE_ID_2			0x05
#define FT6208_UPGRADE_READID_DELAY 	10
#define FT6208_UPGRADE_EARSE_DELAY	2000

#define FTS_UPGRADE_LOOP	10

struct Upgrade_Info {
	u16 delay_aa;		/*delay of write FT_UPGRADE_AA */
	u16 delay_55;		/*delay of write FT_UPGRADE_55 */
	u8 upgrade_id_1;	/*upgrade id 1 */
	u8 upgrade_id_2;	/*upgrade id 2 */
	u16 delay_readid;	/*delay of read id */
	u16 delay_earse_flash; /*delay of earse flash*/
};

//	#ifndef ABS_MT_TOUCH_MAJOR
//	#define ABS_MT_TOUCH_MAJOR	0x30	/* touching ellipse */
//	#define ABS_MT_TOUCH_MINOR	0x31	/* (omit if circular) */
//	#define ABS_MT_WIDTH_MAJOR	0x32	/* approaching ellipse */
//	#define ABS_MT_WIDTH_MINOR	0x33	/* (omit if circular) */
//	#define ABS_MT_ORIENTATION	0x34	/* Ellipse orientation */
//	#define ABS_MT_POSITION_X	0x35	/* Center X ellipse position */
//	#define ABS_MT_POSITION_Y	0x36	/* Center Y ellipse position */
//	#define ABS_MT_TOOL_TYPE	0x37	/* Type of touching device */
//	#define ABS_MT_BLOB_ID		0x38	/* Group set of pkts as blob */
//	#endif /* ABS_MT_TOUCH_MAJOR */

struct tp_key {
	int key;
	s16 x1;
	s16 x2;
	s16 y1;
	s16 y2;
};

#endif
