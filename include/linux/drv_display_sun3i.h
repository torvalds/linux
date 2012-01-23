#ifndef __DRV_DISPLAY_H__
#define __DRV_DISPLAY_H__

#ifndef __BSP_DRV_DISPLAY_H__
#define __BSP_DRV_DISPLAY_H__

#include "types.h"
#define __bool signed char

typedef struct {__u8  alpha;__u8 red;__u8 green; __u8 blue; }__disp_color_t;
typedef struct {__s32 x; __s32 y; __u32 width; __u32 height;}__disp_rect_t;
typedef struct {__u32 width;__u32 height;                   }__disp_rectsz_t;
typedef struct {__s32 x; __s32 y;                           }__disp_pos_t;


typedef enum
{
    DISP_FORMAT_1BPP        =0x0,
    DISP_FORMAT_2BPP        =0x1,
    DISP_FORMAT_4BPP        =0x2,
    DISP_FORMAT_8BPP        =0x3,
    DISP_FORMAT_RGB655      =0x4,
    DISP_FORMAT_RGB565      =0x5,
    DISP_FORMAT_RGB556      =0x6,
    DISP_FORMAT_ARGB1555    =0x7,
    DISP_FORMAT_RGBA5551    =0x8,
    DISP_FORMAT_RGB888      =0x9,
    DISP_FORMAT_ARGB8888    =0xa,

    DISP_FORMAT_YUV444      =0xb,
    DISP_FORMAT_YUV422      =0xc,
    DISP_FORMAT_YUV420      =0xd,
    DISP_FORMAT_YUV411      =0xe,
    DISP_FORMAT_CSIRGB      =0xf,
}__disp_pixel_fmt_t;


typedef enum
{
    DISP_MOD_INTERLEAVED        =0x1,   //interleaved,1个地址
    DISP_MOD_NON_MB_PLANAR      =0x0,   //无宏块平面模式,3个地址,RGB/YUV每个channel分别存放
    DISP_MOD_NON_MB_UV_COMBINED =0x2,   //无宏块UV打包模式,2个地址,Y和UV分别存放
    DISP_MOD_MB_PLANAR          =0x4,   //宏块平面模式,3个地址,RGB/YUV每个channel分别存放
    DISP_MOD_MB_UV_COMBINED     =0x6,   //宏块UV打包模式 ,2个地址,Y和UV分别存放
}__disp_pixel_mod_t;

typedef enum
{
//for interleave argb8888
    DISP_SEQ_ARGB   =0x0,//A在高位
    DISP_SEQ_BGRA   =0x2,

//for nterleaved yuv422
    DISP_SEQ_UYVY   =0x3,
    DISP_SEQ_YUYV   =0x4,
    DISP_SEQ_VYUY   =0x5,
    DISP_SEQ_YVYU   =0x6,

//for interleaved yuv444
    DISP_SEQ_AYUV   =0x7,
    DISP_SEQ_VUYA   =0x8,

//for uv_combined yuv420
    DISP_SEQ_UVUV   =0x9,
    DISP_SEQ_VUVU   =0xa,

//for 16bpp rgb
    DISP_SEQ_P10    = 0xd,//p1在高位
    DISP_SEQ_P01    = 0xe,//p0在高位

//for planar format or 8bpp rgb
    DISP_SEQ_P3210  = 0xf,//p3在高位
    DISP_SEQ_P0123  = 0x10,//p0在高位

//for 4bpp rgb
    DISP_SEQ_P76543210  = 0x11,
    DISP_SEQ_P67452301  = 0x12,
    DISP_SEQ_P10325476  = 0x13,
    DISP_SEQ_P01234567  = 0x14,

//for 2bpp rgb
    DISP_SEQ_2BPP_BIG_BIG       = 0x15,//15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    DISP_SEQ_2BPP_BIG_LITTER    = 0x16,//12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3
    DISP_SEQ_2BPP_LITTER_BIG    = 0x17,//3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12
    DISP_SEQ_2BPP_LITTER_LITTER = 0x18,//0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15

//for 1bpp rgb
    DISP_SEQ_1BPP_BIG_BIG       = 0x19,//31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    DISP_SEQ_1BPP_BIG_LITTER    = 0x1a,//24,25,26,27,28,29,30,31,16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7
    DISP_SEQ_1BPP_LITTER_BIG    = 0x1b,//7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8,23,22,21,20,19,18,17,16,31,30,29,28,27,26,25,24
    DISP_SEQ_1BPP_LITTER_LITTER = 0x1c,//0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
}__disp_pixel_seq_t;

typedef enum
{
    DISP_BT601  = 0,
    DISP_BT709  = 1,
    DISP_YCC    = 2,
    DISP_VXYCC  = 3,
}__disp_cs_mode_t;


typedef enum
{
    DISP_OUTPUT_TYPE_NONE   = 0,
    DISP_OUTPUT_TYPE_LCD    = 1,
    DISP_OUTPUT_TYPE_TV     = 2,
    DISP_OUTPUT_TYPE_HDMI   = 4,
    DISP_OUTPUT_TYPE_VGA    = 8,
}__disp_output_type_t;

typedef enum
{
    DISP_TV_NONE    = 0,
    DISP_TV_CVBS    = 1,
    DISP_TV_YPBPR   = 2,
    DISP_TV_SVIDEO  = 4,
}__disp_tv_output_t;

typedef enum
{
    DISP_TV_MOD_480I                = 0,
    DISP_TV_MOD_576I                = 1,
    DISP_TV_MOD_480P                = 2,
    DISP_TV_MOD_576P                = 3,
    DISP_TV_MOD_720P_50HZ           = 4,
    DISP_TV_MOD_720P_60HZ           = 5,
    DISP_TV_MOD_1080I_50HZ          = 6,
    DISP_TV_MOD_1080I_60HZ          = 7,
    DISP_TV_MOD_1080P_24HZ          = 8,
    DISP_TV_MOD_1080P_50HZ          = 9,
    DISP_TV_MOD_1080P_60HZ          = 0xa,
    DISP_TV_MOD_PAL                 = 0xb,
    DISP_TV_MOD_PAL_SVIDEO          = 0xc,
    DISP_TV_MOD_PAL_CVBS_SVIDEO     = 0xd,
    DISP_TV_MOD_NTSC                = 0xe,
    DISP_TV_MOD_NTSC_SVIDEO         = 0xf,
    DISP_TV_MOD_NTSC_CVBS_SVIDEO    = 0x10,
    DISP_TV_MOD_PAL_M               = 0x11,
    DISP_TV_MOD_PAL_M_SVIDEO        = 0x12,
    DISP_TV_MOD_PAL_M_CVBS_SVIDEO   = 0x13,
    DISP_TV_MOD_PAL_NC              = 0x14,
    DISP_TV_MOD_PAL_NC_SVIDEO       = 0x15,
    DISP_TV_MOD_PAL_NC_CVBS_SVIDEO  = 0x16,
}__disp_tv_mode_t;

typedef enum
{
    DISP_TV_DAC_SRC_COMPOSITE = 0,
    DISP_TV_DAC_SRC_LUMA = 1,
    DISP_TV_DAC_SRC_CHROMA = 2,
    DISP_TV_DAC_SRC_Y = 4,
    DISP_TV_DAC_SRC_PB = 5,
    DISP_TV_DAC_SRC_PR = 6,
    DISP_TV_DAC_SRC_NONE = 7,
}__disp_tv_dac_source;

typedef enum
{
    DISP_VGA_H1680_V1050    = 0,
    DISP_VGA_H1440_V900     = 1,
    DISP_VGA_H1360_V768     = 2,
    DISP_VGA_H1280_V1024    = 3,
    DISP_VGA_H1024_V768     = 4,
    DISP_VGA_H800_V600      = 5,
    DISP_VGA_H640_V480      = 6,
    DISP_VGA_H1440_V900_RB  = 7,//not supported yet
    DISP_VGA_H1680_V1050_RB = 8,//not supported yet
    DISP_VGA_H1920_V1080_RB = 9,
    DISP_VGA_H1920_V1080    = 0xa,
}__disp_vga_mode_t;


typedef enum
{
    DISP_LCDC_SRC_DE_CH1    = 0,
    DISP_LCDC_SRC_DE_CH2    = 1,
    DISP_LCDC_SRC_DMA       = 2,
    DISP_LCDC_SRC_WHITE     = 3,
    DISP_LCDC_SRC_BLACK     = 4,
    DISP_LCDC_SRC_BLUT      = 5,
}__disp_lcdc_src_t;


typedef enum
{
    DISP_LCD_BRIGHT_LEVEL0  = 0,
    DISP_LCD_BRIGHT_LEVEL1  = 1,
    DISP_LCD_BRIGHT_LEVEL2  = 2,
    DISP_LCD_BRIGHT_LEVEL3  = 3,
    DISP_LCD_BRIGHT_LEVEL4  = 4,
    DISP_LCD_BRIGHT_LEVEL5  = 5,
    DISP_LCD_BRIGHT_LEVEL6  = 6,
    DISP_LCD_BRIGHT_LEVEL7  = 7,
    DISP_LCD_BRIGHT_LEVEL8  = 8,
    DISP_LCD_BRIGHT_LEVEL9  = 9,
    DISP_LCD_BRIGHT_LEVEL10 = 0xa,
    DISP_LCD_BRIGHT_LEVEL11 = 0xb,
    DISP_LCD_BRIGHT_LEVEL12 = 0xc,
    DISP_LCD_BRIGHT_LEVEL13 = 0xd,
    DISP_LCD_BRIGHT_LEVEL14 = 0xe,
    DISP_LCD_BRIGHT_LEVEL15 = 0xf,
}__disp_lcd_bright_t;

typedef enum
{
    DISP_LAYER_WORK_MODE_NORMAL     = 0,    //normal work mode
    DISP_LAYER_WORK_MODE_PALETTE    = 1,    //palette work mode
    DISP_LAYER_WORK_MODE_INTER_BUF  = 2,    //internal frame buffer work mode
    DISP_LAYER_WORK_MODE_GAMMA      = 3,    //gamma correction work mode
    DISP_LAYER_WORK_MODE_SCALER     = 4,    //scaler work mode
}__disp_layer_work_mode_t;


typedef enum
{
    DISP_VIDEO_NATUAL       = 0,
    DISP_VIDEO_SOFT         = 1,
    DISP_VIDEO_VERYSOFT     = 2,
    DISP_VIDEO_SHARP        = 3,
    DISP_VIDEO_VERYSHARP    = 4
}__disp_video_smooth_t;

typedef enum
{
    DISP_HWC_MOD_H32_V32_8BPP = 0,
    DISP_HWC_MOD_H64_V64_2BPP = 1,
    DISP_HWC_MOD_H64_V32_4BPP = 2,
    DISP_HWC_MOD_H32_V64_4BPP = 3,
}__disp_hwc_mode_t;

typedef enum
{
    DISP_EXIT_MODE_CLEAN_ALL    = 0,
    DISP_EXIT_MODE_CLEAN_PARTLY = 1,//only clean interrupt temply
}__disp_exit_mode_t;

typedef struct
{
    __u32               addr[3];    // frame buffer的内容地址，对于rgb类型，只有addr[0]有效
    __disp_rectsz_t     size;//单位是pixel
    __disp_pixel_fmt_t  format;
    __disp_pixel_seq_t  seq;
    __disp_pixel_mod_t  mode;
    __bool              br_swap;    // blue red color swap flag, FALSE:RGB; TRUE:BGR,only used in rgb format
    __disp_cs_mode_t    cs_mode;    //color space
}__disp_fb_t;

typedef struct
{
    __disp_layer_work_mode_t    mode;       //layer work mode
    __u8                        pipe;       //layer pipe,0/1,if in scaler mode, scaler0 must be pipe0, scaler1 must be pipe1
    __u8                        prio;       //layer priority,can get layer prio,but never set layer prio,从顶至顶,优先级由低至高
    __bool                      alpha_en;   //layer global alpha enable
    __u16                       alpha_val;  //layer global alpha value
    __bool                      ck_enable;  //layer color key enable
    __disp_rect_t               src_win;    // framebuffer source window,only care x,y if is not scaler mode
    __disp_rect_t               scn_win;    // screen window
    __disp_fb_t                 fb;         //framebuffer
}__disp_layer_info_t;



typedef struct
{
    __disp_color_t   ck_max;
    __disp_color_t   ck_min;
    __u32             red_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
    __u32             green_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
    __u32             blue_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
}__disp_colorkey_t;


typedef struct
{
    __s32   id;
    __u32   addr[3];
    __bool  interlace;
    __bool  top_field_first;
    __u32   frame_rate; // *FRAME_RATE_BASE(现在定为1000)
    __u32   flag_addr;//dit maf flag address
    __u32   flag_stride;//dit maf flag line stride
    __bool  maf_valid;
    __bool  pre_frame_valid;
}__disp_video_fb_t;

typedef struct
{
    __bool maf_enable;
    __bool pre_frame_enable;
}__disp_dit_info_t;

typedef struct
{
    __disp_hwc_mode_t     pat_mode;
    __u32                 addr;
}__disp_hwc_pattern_t;

typedef struct
{
    __disp_fb_t     input_fb;
    __disp_rect_t   source_regn;
    __disp_fb_t     output_fb;
    //__disp_rect_t   out_regn;
}__disp_scaler_para_t;


typedef struct
{
    __disp_fb_t       fb;
    __disp_rect_t   src_win;//source region,only care x,y because of not scaler
    __disp_rect_t   scn_win;// sceen region
}__disp_sprite_block_para_t;



typedef struct
{
	__disp_layer_work_mode_t mode;
	__u32 width;//not used
	__u32 height;//not used
	__u32 line_length;//not used
	__u32 smem_len;
	__u32 ch1_offset;
	__u32 ch2_offset;
	__u32 b_double_buffer;//not used
}__disp_fb_create_para_t;


#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum tag_DISP_CMD
{
//----disp global----
    DISP_CMD_RESERVE0 = 0x00,
    DISP_CMD_RESERVE1 = 0x01,
    DISP_CMD_SET_BKCOLOR = 0x3f,//fail when the value is 0x02,why???
    DISP_CMD_GET_BKCOLOR = 0x03,
    DISP_CMD_SET_COLORKEY = 0x04,
    DISP_CMD_GET_COLORKEY = 0x05,
    DISP_CMD_SET_PALETTE_TBL = 0x06,
    DISP_CMD_GET_PALETTE_TBL = 0x07,
    DISP_CMD_SCN_GET_WIDTH = 0x08,
    DISP_CMD_SCN_GET_HEIGHT = 0x09,
    DISP_CMD_GET_OUTPUT_TYPE = 0x0a,
    DISP_CMD_SET_EXIT_MODE = 0x0c,
    DISP_CMD_SET_GAMMA_TABLE = 0x0d,
    DISP_CMD_GAMMA_CORRECTION_ON = 0x0e,
    DISP_CMD_GAMMA_CORRECTION_OFF = 0x0f,
    DISP_CMD_START_CMD_CACHE =0x10,
    DISP_CMD_EXECUTE_CMD_AND_STOP_CACHE = 0x11,
    DISP_CMD_SET_BRIGHT = 0x12,
    DISP_CMD_SET_CONTRAST = 0x13,
    DISP_CMD_SET_SATURATION = 0x14,
    DISP_CMD_GET_BRIGHT = 0x16,
    DISP_CMD_GET_CONTRAST = 0x17,
    DISP_CMD_GET_SATURATION = 0x18,
    DISP_CMD_ENHANCE_ON = 0x1a,
    DISP_CMD_ENHANCE_OFF = 0x1b,
    DISP_CMD_GET_ENHANCE_EN = 0x1c,

//----layer----
    DISP_CMD_LAYER_REQUEST = 0x40,
    DISP_CMD_LAYER_RELEASE = 0x41,
    DISP_CMD_LAYER_OPEN = 0x42,
    DISP_CMD_LAYER_CLOSE = 0x43,
    DISP_CMD_LAYER_SET_FB = 0x44,
    DISP_CMD_LAYER_GET_FB = 0x45,
    DISP_CMD_LAYER_SET_SRC_WINDOW = 0x46,
    DISP_CMD_LAYER_GET_SRC_WINDOW = 0x47,
    DISP_CMD_LAYER_SET_SCN_WINDOW = 0x48,
    DISP_CMD_LAYER_GET_SCN_WINDOW = 0x49,
    DISP_CMD_LAYER_SET_PARA = 0x4a,
    DISP_CMD_LAYER_GET_PARA = 0x4b,
    DISP_CMD_LAYER_ALPHA_ON = 0x4c,
    DISP_CMD_LAYER_ALPHA_OFF = 0x4d,
    DISP_CMD_LAYER_GET_ALPHA_EN = 0x4e,
    DISP_CMD_LAYER_SET_ALPHA_VALUE = 0x4f,
    DISP_CMD_LAYER_GET_ALPHA_VALUE = 0x50,
    DISP_CMD_LAYER_CK_ON = 0x51,
    DISP_CMD_LAYER_CK_OFF = 0x52,
    DISP_CMD_LAYER_GET_CK_EN = 0x53,
    DISP_CMD_LAYER_SET_PIPE = 0x54,
    DISP_CMD_LAYER_GET_PIPE = 0x55,
    DISP_CMD_LAYER_TOP = 0x56,
    DISP_CMD_LAYER_BOTTOM = 0x57,
    DISP_CMD_LAYER_GET_PRIO = 0x58,
    DISP_CMD_LAYER_SET_SMOOTH = 0x59,
    DISP_CMD_LAYER_GET_SMOOTH = 0x5a,
    DISP_CMD_LAYER_SET_BRIGHT = 0x5b,//亮度
    DISP_CMD_LAYER_SET_CONTRAST = 0x5c,//对比度
    DISP_CMD_LAYER_SET_SATURATION = 0x5d,//饱和度
    DISP_CMD_LAYER_SET_HUE = 0x5e,//色调,色度
    DISP_CMD_LAYER_GET_BRIGHT = 0x5f,
    DISP_CMD_LAYER_GET_CONTRAST = 0x60,
    DISP_CMD_LAYER_GET_SATURATION = 0x61,
    DISP_CMD_LAYER_GET_HUE = 0x62,
    DISP_CMD_LAYER_ENHANCE_ON = 0x63,
    DISP_CMD_LAYER_ENHANCE_OFF = 0x64,
    DISP_CMD_LAYER_GET_ENHANCE_EN = 0x65,

//----scaler----
    DISP_CMD_SCALER_REQUEST = 0x80,
    DISP_CMD_SCALER_RELEASE = 0x81,
    DISP_CMD_SCALER_EXECUTE = 0x82,

//----hwc----
    DISP_CMD_HWC_OPEN = 0xc0,
    DISP_CMD_HWC_CLOSE = 0xc1,
    DISP_CMD_HWC_SET_POS = 0xc2,
    DISP_CMD_HWC_GET_POS = 0xc3,
    DISP_CMD_HWC_SET_FB = 0xc4,
    DISP_CMD_HWC_SET_PALETTE_TABLE = 0xc5,

//----video----
    DISP_CMD_VIDEO_START = 0x100,
    DISP_CMD_VIDEO_STOP = 0x101,
    DISP_CMD_VIDEO_SET_FB = 0x102,
    DISP_CMD_VIDEO_GET_FRAME_ID = 0x103,
    DISP_CMD_VIDEO_GET_DIT_INFO = 0x104,

//----lcd----
    DISP_CMD_LCD_ON = 0x140,
    DISP_CMD_LCD_OFF = 0x141,
    DISP_CMD_LCD_SET_BRIGHTNESS = 0x142,
    DISP_CMD_LCD_GET_BRIGHTNESS = 0x143,
    DISP_CMD_LCD_SET_COLOR = 0x144,
    DISP_CMD_LCD_GET_COLOR = 0x145,
    DISP_CMD_LCD_CPUIF_XY_SWITCH = 0x146,
    DISP_CMD_LCD_CHECK_OPEN_FINISH = 0x14a,
    DISP_CMD_LCD_CHECK_CLOSE_FINISH = 0x14b,
    DISP_CMD_LCD_SET_SRC = 0x14c,

//----tv----
    DISP_CMD_TV_ON = 0x180,
    DISP_CMD_TV_OFF = 0x181,
    DISP_CMD_TV_SET_MODE = 0x182,
    DISP_CMD_TV_GET_MODE = 0x183,
    DISP_CMD_TV_AUTOCHECK_ON = 0x184,
    DISP_CMD_TV_AUTOCHECK_OFF = 0x185,
    DISP_CMD_TV_GET_INTERFACE = 0x186,
    DISP_CMD_TV_SET_SRC = 0x187,
    DISP_CMD_TV_GET_DAC_STATUS = 0x188,
    DISP_CMD_TV_SET_DAC_SOURCE = 0x189,
    DISP_CMD_TV_GET_DAC_SOURCE = 0x18a,

//----hdmi----
    DISP_CMD_HDMI_ON = 0x1c0,
    DISP_CMD_HDMI_OFF = 0x1c1,
    DISP_CMD_HDMI_SET_MODE = 0x1c2,
    DISP_CMD_HDMI_GET_MODE = 0x1c3,
    DISP_CMD_HDMI_SUPPORT_MODE = 0x1c4,
    DISP_CMD_HDMI_GET_HPD_STATUS = 0x1c5,
	DISP_CMD_HDMI_SET_SRC = 0x1c6,

//----vga----
    DISP_CMD_VGA_ON = 0x200,
    DISP_CMD_VGA_OFF = 0x201,
    DISP_CMD_VGA_SET_MODE = 0x202,
    DISP_CMD_VGA_GET_MODE = 0x203,
	DISP_CMD_VGA_SET_SRC = 0x204,

//----sprite----
    DISP_CMD_SPRITE_OPEN = 0x240,
    DISP_CMD_SPRITE_CLOSE = 0x241,
    DISP_CMD_SPRITE_SET_FORMAT = 0x242,
    DISP_CMD_SPRITE_GLOBAL_ALPHA_ENABLE = 0x243,
    DISP_CMD_SPRITE_GLOBAL_ALPHA_DISABLE = 0x244,
    DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_ENABLE = 0x252,
    DISP_CMD_SPRITE_SET_GLOBAL_ALPHA_VALUE = 0x245,
    DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_VALUE = 0x253,
    DISP_CMD_SPRITE_SET_ORDER = 0x246,
    DISP_CMD_SPRITE_GET_TOP_BLOCK = 0x250,
    DISP_CMD_SPRITE_GET_BOTTOM_BLOCK = 0x251,
    DISP_CMD_SPRITE_SET_PALETTE_TBL = 0x247,
    DISP_CMD_SPRITE_GET_BLOCK_NUM = 0x259,
    DISP_CMD_SPRITE_BLOCK_REQUEST = 0x248,
    DISP_CMD_SPRITE_BLOCK_RELEASE = 0x249,
    DISP_CMD_SPRITE_BLOCK_OPEN = 0x257,
    DISP_CMD_SPRITE_BLOCK_CLOSE = 0x258,
    DISP_CMD_SPRITE_BLOCK_SET_SOURCE_WINDOW = 0x25a,
    DISP_CMD_SPRITE_BLOCK_GET_SOURCE_WINDOW = 0x25b,
    DISP_CMD_SPRITE_BLOCK_SET_SCREEN_WINDOW = 0x24a,
    DISP_CMD_SPRITE_BLOCK_GET_SCREEN_WINDOW = 0x24c,
    DISP_CMD_SPRITE_BLOCK_SET_FB = 0x24b,
    DISP_CMD_SPRITE_BLOCK_GET_FB = 0x24d,
    DISP_CMD_SPRITE_BLOCK_SET_PARA = 0x25c,
    DISP_CMD_SPRITE_BLOCK_GET_PARA = 0x25d,
    DISP_CMD_SPRITE_BLOCK_SET_TOP = 0x24e,
    DISP_CMD_SPRITE_BLOCK_SET_BOTTOM = 0x24f,
    DISP_CMD_SPRITE_BLOCK_GET_PREV_BLOCK = 0x254,
    DISP_CMD_SPRITE_BLOCK_GET_NEXT_BLOCK = 0x255,
    DISP_CMD_SPRITE_BLOCK_GET_PRIO = 0x256,

//----framebuffer----
	DISP_CMD_FB_REQUEST = 0x280,
	DISP_CMD_FB_RELEASE = 0x281,
//---for Displayer Test --------
	DISP_CMD_MEM_REQUEST = 0x2c0,
	DISP_CMD_MEM_RELASE = 0x2c1,
	DISP_CMD_MEM_GETADR = 0x2c2,
	DISP_CMD_MEM_SELIDX = 0x2c3,

	DISP_CMD_SUSPEND = 0x2c4,
	DISP_CMD_RELEASE = 0x2c5,


}__disp_cmd_t;

#define FBIOGET_LAYER_HDL 0x4700

#endif
