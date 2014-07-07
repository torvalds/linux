#ifndef _SCREEN_H
#define _SCREEN_H

typedef enum _REFRESH_STAGE {
    REFRESH_PRE = 0,
    REFRESH_END,

} REFRESH_STAGE;


typedef enum _MCU_IOCTL {
    MCU_WRCMD = 0,
    MCU_WRDATA,
    MCU_SETBYPASS,

} MCU_IOCTL;


typedef enum _MCU_STATUS {
    MS_IDLE = 0,
    MS_MCU,
    MS_EBOOK,
    MS_EWAITSTART,
    MS_EWAITEND,
    MS_EEND,

} MCU_STATUS;

struct rk29_fb_setting_info {
	u8 data_num;
	u8 vsync_en;
	u8 den_en;
	u8 mcu_fmk_en;
	u8 disp_on_en;
	u8 standby_en;
};

struct rk29lcd_info {
	u32 lcd_id;
	u32 txd_pin;
	u32 clk_pin;
	u32 cs_pin;
	u32	reset_pin;
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*io_enable)(void);
	int (*io_disable)(void);
};

struct overscan {
	unsigned char left;
	unsigned char top;
	unsigned char right;
	unsigned char bottom;
};

/* Screen description 
*type:LVDS,RGB,MIPI,MCU
*lvds_fromat:lvds data format,set it if the screen is lvds
*face:thi display output face,18bit,24bit,etc
*ft: the time need to display one frame time
*/
struct rk_screen {
	u16 type;
	u16 lvds_format; 
	u16 face;
	u8 lcdc_id;   
	u8 screen_id; 
	struct fb_videomode mode;
	u32 post_dsp_stx;
	u32 post_dsp_sty;
	u32 post_xsize;
	u32 post_ysize;
	u16 x_mirror;
	u16 y_mirror;
	int interlace;
	int pixelrepeat; //For 480i/576i format, pixel is repeated twice.
	u16 width;
	u16 height;
	u8  ft;
	int *dsp_lut; 

#if defined(CONFIG_MFD_RK616)
	u32 pll_cfg_val;  //bellow are for jettaB
	u32 frac;
	u16 scl_vst;
	u16 scl_hst;
	u16 vif_vst;
	u16 vif_hst;
#endif
	u8 hdmi_resolution;
	u8 mcu_wrperiod;
	u8 mcu_usefmk;
	u8 mcu_frmrate;

	u8 pin_hsync;
	u8 pin_vsync;
	u8 pin_den;
	u8 pin_dclk;

	/* Swap rule */
	u8 swap_gb;
	u8 swap_rg;
	u8 swap_rb;
	u8 swap_delta;
	u8 swap_dumy;
	
#if defined(CONFIG_MIPI_DSI)
	/* MIPI DSI */
	u8 dsi_lane;
	u8 dsi_video_mode;
	u32 hs_tx_clk;
#endif

	int xpos;  //horizontal display start position on the sceen ,then can be changed by application
	int ypos;
	int xsize; //horizontal and vertical display size on he screen,they can be changed by application
	int ysize;
	struct overscan overscan;
	struct rk_screen *ext_screen;
	/* Operation function*/
	int (*init)(void);
	int (*standby)(u8 enable);
	int (*refresh)(u8 arg);
	int (*scandir)(u16 dir);
	int (*disparea)(u8 area);
	int (*sscreen_get)(struct rk_screen *screen, u8 resolution);
	int (*sscreen_set)(struct rk_screen *screen, bool type);// 1: use scaler 0:bypass
};

struct rk29fb_info {
	u32 fb_id;
	int prop;		//display device property,like PRMRY,EXTEND
	u32 mcu_fmk_pin;
	struct rk29lcd_info *lcd_info;
	int (*io_init)(struct rk29_fb_setting_info *fb_setting);
	int (*io_deinit)(void);
	int (*io_enable)(void);
	int (*io_disable)(void);
	void (*set_screen_info)(struct rk_screen *screen, struct rk29lcd_info *lcd_info );
};

extern void set_lcd_info(struct rk_screen *screen, struct rk29lcd_info *lcd_info);
extern size_t get_fb_size(void);

extern void set_tv_info(struct rk_screen *screen);
extern void set_hdmi_info(struct rk_screen *screen);

#endif
