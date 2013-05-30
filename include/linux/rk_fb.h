/* drivers/video/rk_fb.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK30_FB_H
#define __ARCH_ARM_MACH_RK30_FB_H

#include <linux/fb.h>
#include <linux/platform_device.h>
#include<linux/completion.h>
#include<linux/spinlock.h>
#include<asm/atomic.h>
#include<mach/board.h>
#include<linux/rk_screen.h>

#define RK30_MAX_LCDC_SUPPORT	4
#define RK30_MAX_LAYER_SUPPORT	4
#define RK_MAX_FB_SUPPORT       8



#define FB0_IOCTL_STOP_TIMER_FLUSH		0x6001
#define FB0_IOCTL_SET_PANEL				0x6002

#ifdef CONFIG_FB_WIMO
#define FB_WIMO_FLAG
#endif
#ifdef FB_WIMO_FLAG
#define FB0_IOCTL_SET_BUF				0x6017
#define FB0_IOCTL_COPY_CURBUF				0x6018
#define FB0_IOCTL_CLOSE_BUF				0x6019
#endif

#define RK_FBIOGET_PANEL_SIZE		0x5001
#define RK_FBIOSET_YUV_ADDR		0x5002
#define RK_FBIOGET_SCREEN_STATE    	0X4620
#define RK_FBIOGET_16OR32    		0X4621
#define RK_FBIOGET_IDLEFBUff_16OR32    	0X4622
#define RK_FBIOSET_COMPOSE_LAYER_COUNTS    0X4623

#define RK_FBIOSET_ROTATE            	0x5003
#define RK_FB_IOCTL_SET_I2P_ODD_ADDR       0x5005
#define RK_FB_IOCTL_SET_I2P_EVEN_ADDR      0x5006
#define RK_FBIOSET_OVERLAY_STATE     	0x5018
#define RK_FBIOGET_OVERLAY_STATE   	0X4619
#define RK_FBIOSET_ENABLE		0x5019	
#define RK_FBIOGET_ENABLE		0x5020
#define RK_FBIOSET_CONFIG_DONE		0x4628
#define RK_FBIOSET_VSYNC_ENABLE		0x4629
#define RK_FBIOPUT_NUM_BUFFERS 		0x4625
#define RK_FBIOPUT_COLOR_KEY_CFG	0x4626


/********************************************************************
**          display output interface supported by rockchip lcdc                       *
********************************************************************/
/* */
#define OUT_P888            0   //24bit screen,connect to lcdc D0~D23
#define OUT_P666            1   //18bit screen,connect to lcdc D0~D17
#define OUT_P565            2 
#define OUT_S888x           4
#define OUT_CCIR656         6
#define OUT_S888            8
#define OUT_S888DUMY        12
#define OUT_P16BPP4         24
#define OUT_D888_P666       0x21 //18bit screen,connect to lcdc D2~D7, D10~D15, D18~D23
#define OUT_D888_P565       0x22

/**
 * pixel format definitions,this is copy from android/system/core/include/system/graphics.h
 */

enum {
    HAL_PIXEL_FORMAT_RGBA_8888          = 1,
    HAL_PIXEL_FORMAT_RGBX_8888          = 2,
    HAL_PIXEL_FORMAT_RGB_888            = 3,
    HAL_PIXEL_FORMAT_RGB_565            = 4,
    HAL_PIXEL_FORMAT_BGRA_8888          = 5,
    HAL_PIXEL_FORMAT_RGBA_5551          = 6,
    HAL_PIXEL_FORMAT_RGBA_4444          = 7,

    /* 0x8 - 0xFF range unavailable */

    /*
     * 0x100 - 0x1FF
     *
     * This range is reserved for pixel formats that are specific to the HAL
     * implementation.  Implementations can use any value in this range to
     * communicate video pixel formats between their HAL modules.  These formats
     * must not have an alpha channel.  Additionally, an EGLimage created from a
     * gralloc buffer of one of these formats must be supported for use with the
     * GL_OES_EGL_image_external OpenGL ES extension.
     */

    /*
     * Android YUV format:
     *
     * This format is exposed outside of the HAL to software decoders and
     * applications.  EGLImageKHR must support it in conjunction with the
     * OES_EGL_image_external extension.
     *
     * YV12 is a 4:2:0 YCrCb planar format comprised of a WxH Y plane followed
     * by (W/2) x (H/2) Cr and Cb planes.
     *
     * This format assumes
     * - an even width
     * - an even height
     * - a horizontal stride multiple of 16 pixels
     * - a vertical stride equal to the height
     *
     *   y_size = stride * height
     *   c_size = ALIGN(stride/2, 16) * height/2
     *   size = y_size + c_size * 2
     *   cr_offset = y_size
     *   cb_offset = y_size + c_size
     *
     */
    HAL_PIXEL_FORMAT_YV12   = 0x32315659, // YCrCb 4:2:0 Planar



    /* Legacy formats (deprecated), used by ImageFormat.java */
    HAL_PIXEL_FORMAT_YCbCr_422_SP       = 0x10, // NV16
    HAL_PIXEL_FORMAT_YCrCb_420_SP       = 0x11, // NV21
    HAL_PIXEL_FORMAT_YCbCr_422_I        = 0x14, // YUY2
    HAL_PIXEL_FORMAT_YCrCb_NV12         = 0x20, // YUY2
    HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO   = 0x21, // YUY2
    HAL_PIXEL_FORMAT_YCrCb_444          = 0x22, //yuv444


};


//display data format
enum data_format{
	ARGB888 = 0,
	RGB888,
	RGB565,
	YUV420 = 4,
	YUV422,
	YUV444,
	XRGB888,
	XBGR888,
	ABGR888,
};

enum fb_win_map_order{
	FB_DEFAULT_ORDER	   = 0,
	FB0_WIN2_FB1_WIN1_FB2_WIN0 = 12,
	FB0_WIN1_FB1_WIN2_FB2_WIN0 = 21, 
	FB0_WIN2_FB1_WIN0_FB2_WIN1 = 102,
	FB0_WIN0_FB1_WIN2_FB2_WIN1 = 120,
	FB0_WIN0_FB1_WIN1_FB2_WIN2 = 210,
	FB0_WIN1_FB1_WIN0_FB2_WIN2 = 201,       
};

struct rk_fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

struct rk_fb_vsync {
	wait_queue_head_t	wait;
	ktime_t			timestamp;
	bool			active;
	int			irq_refcount;
	struct mutex		irq_lock;
	struct task_struct	*thread;
};

struct color_key_cfg {
	u32 win0_color_key_cfg;
	u32 win1_color_key_cfg;
	u32 win2_color_key_cfg;
};

typedef enum _TRSP_MODE
{
    TRSP_CLOSE = 0,
    TRSP_FMREG,
    TRSP_FMREGEX,
    TRSP_FMRAM,
    TRSP_FMRAMEX,
    TRSP_MASK,
    TRSP_INVAL
} TRSP_MODE;

struct layer_par {
	char name[5];
	int id;
	bool state; 	//on or off
	u32	pseudo_pal[16];
	u32 y_offset;       //yuv/rgb offset  -->LCDC_WINx_YRGB_MSTx
	u32 c_offset;     //cb cr offset--->LCDC_WINx_CBR_MSTx
	u32 xpos;         //start point in panel  --->LCDC_WINx_DSP_ST
	u32 ypos;
	u16 xsize;        // display window width/height  -->LCDC_WINx_DSP_INFO
	u16 ysize;          
	u16 xact;        //origin display window size -->LCDC_WINx_ACT_INFO
	u16 yact;
	u16 xvir;       //virtual width/height     -->LCDC_WINx_VIR
	u16 yvir;
	unsigned long smem_start;
	unsigned long cbr_start;  // Cbr memory start address
	enum data_format format;

	bool support_3d;
    	u32 scale_yrgb_x;
	u32 scale_yrgb_y;
	u32 scale_cbcr_x;
	u32 scale_cbcr_y;
	u32 dsp_stx;
	u32 dsp_sty;
	u32 vir_stride;
	u32 y_addr;
	u32 uv_addr;
	u8 fmt_cfg;
	u8 swap_rb;
	u32 reserved;
};


struct rk_lcdc_device_driver{
	char name[6];
	int id;
	struct device  *dev;
	
	struct layer_par *layer_par[RK_MAX_FB_SUPPORT];
	struct layer_par *def_layer_par;
	int num_layer;
	int num_buf;				//the num_of buffer
	int fb_index_base;                     //the first fb index of the lcdc device
	rk_screen *screen0;		      //some platform have only one lcdc,but extend
	rk_screen *screen1;		      //two display devices for dual display,such as rk2918,rk2928
	rk_screen *cur_screen;		     //screen0 is primary screen ,like lcd panel,screen1 is  extend screen,like hdmi
	u32 pixclock;

	
        char fb0_win_id;
        char fb1_win_id;
        char fb2_win_id;
        struct mutex fb_win_id_mutex;
	
	struct completion  frame_done;		  //sync for pan_display,whe we set a new frame address to lcdc register,we must make sure the frame begain to display
	spinlock_t  cpl_lock; 			 //lock for completion  frame done
	int first_frame ;
	struct rk_fb_vsync	 vsync_info;
	int wait_fs;				//wait for new frame start in kernel
	
	struct rk29fb_info *screen_ctr_info;
	int (*open)(struct rk_lcdc_device_driver *dev_drv,int layer_id,bool open);
	int (*init_lcdc)(struct rk_lcdc_device_driver *dev_drv);
	int (*ioctl)(struct rk_lcdc_device_driver *dev_drv, unsigned int cmd,unsigned long arg,int layer_id);
	int (*suspend)(struct rk_lcdc_device_driver *dev_drv);
	int (*resume)(struct rk_lcdc_device_driver *dev_drv);
	int (*blank)(struct rk_lcdc_device_driver *dev_drv,int layer_id,int blank_mode);
	int (*set_par)(struct rk_lcdc_device_driver *dev_drv,int layer_id);
	int (*pan_display)(struct rk_lcdc_device_driver *dev_drv,int layer_id);
	int (*lcdc_reg_update)(struct rk_lcdc_device_driver *dev_drv);
	ssize_t (*get_disp_info)(struct rk_lcdc_device_driver *dev_drv,char *buf,int layer_id);
	int (*load_screen)(struct rk_lcdc_device_driver *dev_drv, bool initscreen);
	int (*get_layer_state)(struct rk_lcdc_device_driver *dev_drv,int layer_id);
	int (*ovl_mgr)(struct rk_lcdc_device_driver *dev_drv,int swap,bool set);  //overlay manager
	int (*fps_mgr)(struct rk_lcdc_device_driver *dev_drv,int fps,bool set);
	int (*fb_get_layer)(struct rk_lcdc_device_driver *dev_drv,const char *id);                                      //find layer for fb
	int (*fb_layer_remap)(struct rk_lcdc_device_driver *dev_drv,enum fb_win_map_order order);
	int (*set_dsp_lut)(struct rk_lcdc_device_driver *dev_drv,int *lut);
	int (*read_dsp_lut)(struct rk_lcdc_device_driver *dev_drv,int *lut);
	int (*lcdc_hdmi_process)(struct rk_lcdc_device_driver *dev_drv,int mode); //some lcdc need to some process in hdmi mode
	int (*lcdc_rst)(struct rk_lcdc_device_driver *dev_drv);
	
};

struct rk_fb_inf {
	struct rk29fb_info * mach_info;     //lcd io control info
	struct fb_info *fb[RK_MAX_FB_SUPPORT];
	int num_fb;

	struct rk_lcdc_device_driver *lcdc_dev_drv[RK30_MAX_LCDC_SUPPORT];
	int num_lcdc;

	int video_mode;  //when play video set it to 1
	struct workqueue_struct *workqueue;
	struct delayed_work delay_work;
};
extern int rk_fb_register(struct rk_lcdc_device_driver *dev_drv,
	struct rk_lcdc_device_driver *def_drv,int id);
extern int rk_fb_unregister(struct rk_lcdc_device_driver *dev_drv);
extern int get_fb_layer_id(struct fb_fix_screeninfo *fix);
extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name);
extern rk_screen * rk_fb_get_prmry_screen(void);
u32 rk_fb_get_prmry_screen_pixclock(void);

extern int rk_fb_switch_screen(rk_screen *screen ,int enable ,int lcdc_id);
extern int rk_fb_disp_scale(u8 scale_x, u8 scale_y,u8 lcdc_id);
extern int rkfb_create_sysfs(struct fb_info *fbi);
extern char * get_format_string(enum data_format,char *fmt);

static int inline rk_fb_calc_fps(rk_screen *screen,u32 pixclock)
{
	int x, y;
	unsigned long long hz;
	if(!screen)
	{
		printk(KERN_ERR "%s:null screen!\n",__func__);
		return 0;
	}
	x = screen->x_res + screen->left_margin + screen->right_margin + screen->hsync_len;
	y = screen->y_res + screen->upper_margin + screen->lower_margin + screen->vsync_len;

	hz = 1000000000000ULL;		/* 1e12 picoseconds per second */

	hz += (x * y) / 2;
	do_div(hz, x * y);		/* divide by x * y with rounding */

	hz += pixclock / 2;
	do_div(hz,pixclock);		/* divide by pixclock with rounding */

	return  hz;
}

static int inline __rk_platform_add_display_devices(struct platform_device *fb,
	struct platform_device *lcdc0,struct platform_device *lcdc1,
	struct platform_device *bl)
{
	struct rk29fb_info *lcdc0_screen_info = NULL;
	struct rk29fb_info *lcdc1_screen_info = NULL;
	struct platform_device *prmry_lcdc = NULL;
	struct platform_device *extend_lcdc = NULL;
	
	if(!fb)
	{
		printk(KERN_ERR "warning:no rockchip fb device!\n");
	}
	else
	{
		platform_device_register(fb);
	}

	if((!lcdc0)&&(!lcdc1))
	{
		printk(KERN_ERR "warning:no lcdc device!\n");
	}
	else
	{
		if(lcdc0)
		{
			lcdc0_screen_info = lcdc0->dev.platform_data;
			if(lcdc0_screen_info->prop == PRMRY)
			{
				prmry_lcdc = lcdc0;
				printk(KERN_INFO "lcdc0 is used as primary display device contoller!\n");
			}
			else
			{
				extend_lcdc = lcdc0;
				printk(KERN_INFO "lcdc0 is used as external display device contoller!\n");
			}
				
			
		}
		else
		{
			printk(KERN_INFO "warning:lcdc0 not add to system!\n");
		}
		
		if(lcdc1)
		{
			lcdc1_screen_info = lcdc1->dev.platform_data;
			if(lcdc1_screen_info->prop == PRMRY)
			{
				prmry_lcdc = lcdc1;
				printk(KERN_INFO "lcdc1 is used as primary display device controller!\n");
			}
			else
			{
				extend_lcdc = lcdc1;
				printk(KERN_INFO "lcdc1 is used as external display device controller!\n");
			}
				
			
		}
		else
		{
			printk(KERN_INFO "warning:lcdc1 not add to system!\n");
		}
	}

	if(prmry_lcdc)
		platform_device_register(prmry_lcdc);
	if(extend_lcdc)
		platform_device_register(extend_lcdc);
	if(bl)
		platform_device_register(bl);

	return 0;
}

#endif
