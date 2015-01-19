#ifndef  _PPMGR_MAIN_H
#define  _PPMGR_MAIN_H
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/sysfs.h>
#include  <linux/spinlock.h>
#include <linux/kthread.h>


/**************************************************************
**																	 **
**	macro define		 												 **
**																	 **
***************************************************************/

#define PPMGR_IOC_MAGIC  'P'
#define PPMGR_IOC_2OSD0		_IOW(PPMGR_IOC_MAGIC, 0x00, unsigned int)
#define PPMGR_IOC_ENABLE_PP _IOW(PPMGR_IOC_MAGIC,0X01,unsigned int)
#define PPMGR_IOC_CONFIG_FRAME  _IOW(PPMGR_IOC_MAGIC,0X02,unsigned int)
#define PPMGR_IOC_GET_ANGLE  _IOR(PPMGR_IOC_MAGIC,0X03,unsigned int)
#define PPMGR_IOC_SET_ANGLE  _IOW(PPMGR_IOC_MAGIC,0X04,unsigned int)

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
#define PPMGR_IOC_VIEW_MODE  _IOW(PPMGR_IOC_MAGIC,0X05,unsigned int)
#define PPMGR_IOC_HOR_VER_DOUBLE  _IOW(PPMGR_IOC_MAGIC,0X06,unsigned int)
#define PPMGR_IOC_SWITCHMODE  _IOW(PPMGR_IOC_MAGIC,0X07,unsigned int)
#define PPMGR_IOC_3D_DIRECTION  _IOW(PPMGR_IOC_MAGIC,0X08,unsigned int)
#define PPMGR_IOC_3D_SCALE_DOWN  _IOW(PPMGR_IOC_MAGIC,0X09,unsigned int)
#endif
/**************************************************************
**																	 **
**	type  define		 												 **
**																	 **
***************************************************************/

typedef struct {
    int width;
    int height;
    int bpp;
    int angle;
    int format;
} frame_info_t;

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
//mode: bit0-7, process type: disable, 3d, 2d->3d, 3d->2d
#define PPMGR_3D_PROCESS_MODE_MASK                    0xff
#define PPMGR_3D_PROCESS_MODE_SHIFT                   0
#define PPMGR_3D_PROCESS_MODE_DISABLE                 0
#define PPMGR_3D_PROCESS_MODE_3D_ENABLE               1
#define PPMGR_3D_PROCESS_MODE_3D_TO_2D                2
#define PPMGR_3D_PROCESS_MODE_2D_TO_3D                3

//mode: bit 8-9, detect src format: 0-auto check, 1- lr format, 2-tb format
#define PPMGR_3D_PROCESS_SRC_FOMRAT_MASK              0x00000300
#define PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT             8
#define PPMGR_3D_PROCESS_SRC_FOMRAT_AUTO              0
#define PPMGR_3D_PROCESS_SRC_FOMRAT_LR                1
#define PPMGR_3D_PROCESS_SRC_FOMRAT_TB                2

//mode: bit 10, L-R/T-B switch flag: 0-no switch, 1-switch
#define PPMGR_3D_PROCESS_SWITCH_FLAG                  0x00000400

//mode: bit 11, 3D->2D, use L/T frame : 0-L/T, 1-R/B
#define PPMGR_3D_PROCESS_3D_TO_2D_SRC_FRAME           0x00000800

//mode: bit 12-13, horizontal/vertical double mode, for full/half format 3d src: 0-none, 1-hor double, 2-ver double
#define PPMGR_3D_PROCESS_DOUBLE_TYPE                  0x00003000
#define PPMGR_3D_PROCESS_DOUBLE_TYPE_SHIFT            12
#define PPMGR_3D_PROCESS_DOUBLE_TYPE_NONE             0
#define PPMGR_3D_PROCESS_DOUBLE_TYPE_HOR              1
#define PPMGR_3D_PROCESS_DOUBLE_TYPE_VER              2

//mode: bit 14-15 2d to 3d control
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_MASK        0x0000c000
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_SHIFT       14
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_NONE        0
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_LEFT_MOVE   1
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_RIGHT_MOVE  2

//mode: bit 16-19, 2D->3D function: 0-normal, 1- field depth
#define PPMGR_3D_PROCESS_2D_TO_3D_MASK                0x000f0000
#define PPMGR_3D_PROCESS_2D_TO_3D_SHIFT               16
#define PPMGR_3D_PROCESS_2D_TO_3D_NORMAL              0
#define PPMGR_3D_PROCESS_2D_TO_3D_FIELD_DEPTH         1

//mode: bit 20-27 2d to 3d control value
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_VALUE_MASK  0x0ff00000
#define PPMGR_3D_PROCESS_2D_TO_3D_CONTROL_VAULE_SHIFT 20

//mode: bit 28-29 3d rotate direction control
//#define PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_MASK        0x30000000
//#define PPMGR_3D_PROCESS_3D_ROTATE_DIRECTION_VAULE_SHIFT 28

//mode bit:30-31 not used

#define EXTERNAL_MODE_3D_DISABLE         PPMGR_3D_PROCESS_MODE_DISABLE  //0x00000000

#define EXTERNAL_MODE_3D_AUTO            PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_AUTO<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000001
#define EXTERNAL_MODE_3D_AUTO_SWITCH     PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_AUTO<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_SWITCH_FLAG //0x00000401
#define EXTERNAL_MODE_3D_LR              PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_LR<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000101
#define EXTERNAL_MODE_3D_LR_SWITCH       PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_LR<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_SWITCH_FLAG //0x00000501
#define EXTERNAL_MODE_3D_TB              PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_TB<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000201
#define EXTERNAL_MODE_3D_TB_SWITCH       PPMGR_3D_PROCESS_MODE_3D_ENABLE|(PPMGR_3D_PROCESS_SRC_FOMRAT_TB<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_SWITCH_FLAG //0x00000601

#define EXTERNAL_MODE_3D_TO_2D_AUTO_1    PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_AUTO<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000002
#define EXTERNAL_MODE_3D_TO_2D_AUTO_2    PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_AUTO<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_3D_TO_2D_SRC_FRAME //0x00000802
#define EXTERNAL_MODE_3D_TO_2D_L         PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_LR<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000102
#define EXTERNAL_MODE_3D_TO_2D_R         PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_LR<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_3D_TO_2D_SRC_FRAME  //0x00000902
#define EXTERNAL_MODE_3D_TO_2D_T         PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_TB<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT) //0x00000202
#define EXTERNAL_MODE_3D_TO_2D_B         PPMGR_3D_PROCESS_MODE_3D_TO_2D|(PPMGR_3D_PROCESS_SRC_FOMRAT_TB<<PPMGR_3D_PROCESS_SRC_FORMAT_SHIFT)|PPMGR_3D_PROCESS_3D_TO_2D_SRC_FRAME //0x00000a02

#define EXTERNAL_MODE_2D_TO_3D           PPMGR_3D_PROCESS_MODE_2D_TO_3D|(PPMGR_3D_PROCESS_2D_TO_3D_NORMAL<<PPMGR_3D_PROCESS_2D_TO_3D_SHIFT)  //0x00000003
#define EXTERNAL_MODE_FIELD_DEPTH        PPMGR_3D_PROCESS_MODE_2D_TO_3D|(PPMGR_3D_PROCESS_2D_TO_3D_FIELD_DEPTH<<PPMGR_3D_PROCESS_2D_TO_3D_SHIFT) //0x00010003

#define TYPE_NONE           0
#define TYPE_2D_TO_3D       1
#define TYPE_3D_LR          2
#define TYPE_3D_TB          3
#define TYPE_3D_TO_2D_LR    4
#define TYPE_3D_TO_2D_TB    5

typedef enum {
    VIEWMODE_NORMAL  = 0,
    VIEWMODE_FULL,
    VIEWMODE_4_3,
    VIEWMODE_16_9,
    VIEWMODE_1_1,
    VIEWMODE_MAX
} view_mode_t;
#endif



/*TV 3D mode*/
#define MODE_3D_ENABLE      0x00000001
#define MODE_AUTO           0x00000002
#define MODE_2D_TO_3D       0x00000004
#define MODE_LR             0x00000008
#define MODE_BT             0x00000010
#define MODE_LR_SWITCH      0x00000020
#define MODE_FIELD_DEPTH    0x00000040
#define MODE_3D_TO_2D_L     0x00000080
#define MODE_3D_TO_2D_R     0x00000100

#define LR_FORMAT_INDICATOR   0x00000200
#define BT_FORMAT_INDICATOR   0x00000400

#define TYPE_NONE           0
#define TYPE_2D_TO_3D       1
#define TYPE_LR             2
#define TYPE_BT             3
#define TYPE_LR_SWITCH      4
#define TYPE_FILED_DEPTH    5
#define TYPE_3D_TO_2D_L     6
#define TYPE_3D_TO_2D_R     7


typedef enum {
    PLATFORM_MID  = 0,
    PLATFORM_MBX,
    PLATFORM_TV,
    PLATFORM_MID_VERTICAL
} platform_type_t;
//#endif


#endif
