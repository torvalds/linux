#ifndef _SCREEN_H
#define _SCREEN_H

#define LVDS_8BIT_1     0
#define LVDS_8BIT_2     1
#define LVDS_8BIT_3     2
#define LVDS_6BIT       3
/*      	lvds connect config       
 *                                        
 *          	LVDS_8BIT_1    LVDS_8BIT_2     LVDS_8BIT_3     LVDS_6BIT
----------------------------------------------------------------------
	TX0	R0		R2		R2		R0
	TX1	R1		R3		R3		R1
	TX2	R2		R4		R4		R2
Y	TX3	R3		R5		R5		R3
0	TX4	R4		R6		R6		R4
	TX6	R5		R7		R7		R5	
	TX7	G0		G2		G2		G0
----------------------------------------------------------------------
	TX8	G1		G3		G3		G1
	TX9	G2		G4		G4		G2
Y	TX12   	G3		G5		G5		G3
1	TX13   	G4		G6		G6		G4
 	TX14   	G5		G7		G7		G5
	TX15   	B0		B2		B2		B0
	TX18   	B1		B3		B3		B1
----------------------------------------------------------------------
	TX19	B2		B4		B4		B2
	TX20   	B3		B5		B5		B3
	TX21   	B4		B6		B6		B4
Y	TX22   	B5		B7		B7		B5
2	TX24   	HSYNC		HSYNC		HSYNC		HSYNC
	TX25	VSYNC		VSYNC		VSYNC		VSYNC
	TX26	ENABLE		ENABLE		ENABLE		ENABLE
----------------------------------------------------------------------    
	TX27	R6		R0		GND		GND
	TX5	R7		R1		GND		GND
	TX10   	G6		G0		GND		GND
Y	TX11   	G7		G1		GND		GND
3	TX16   	B6		B0		GND		GND
	TX17   	B7		B1		GND		GND
	TX23   	RSVD		RSVD		RSVD		RSVD
----------------------------------------------------------------------
*/

typedef enum _SCREEN_TYPE {
	SCREEN_NULL = 0,
	SCREEN_RGB,
	SCREEN_LVDS,
	SCREEN_MCU,
	SCREEN_TVOUT,
	SCREEN_HDMI,
	SCREEN_MIPI,
} SCREEN_TYPE;

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

enum rk_disp_prop{       //display device property
    PRMRY = 1,                     //primary display device ,like LCD screen
    EXTEND,                        //extend display device ,like hdmi ,tv out
};

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


/* Screen description */
typedef struct rk29fb_screen {
	/* screen type & hardware connect format & out face */
	u16 type;
	u16 lvds_format;  //lvds data format
	u16 face;
	u8 lcdc_id;    //which output interface the screeen connect to
	u8 screen_id; //screen number

	/* Screen size */
	u16 x_res;
	u16 y_res;
	u16 width;
	u16 height;

	u32 mode;
	/* Timing */
	u32 pixclock;
	u32 fps;
	u16 left_margin;
	u16 right_margin;
	u16 hsync_len;
	u16 upper_margin;
	u16 lower_margin;
	u16 vsync_len;
	u8  ft;	//the time need to display one frame,in ms
	int *dsp_lut; //display lut 
	struct rk29fb_screen *ext_screen;
#if defined(CONFIG_HDMI_DUAL_DISP) || defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
    /* Scaler mode Timing */
	u32 s_pixclock;
	u16 s_left_margin;
	u16 s_right_margin;
	u16 s_hsync_len;
	u16 s_upper_margin;
	u16 s_lower_margin;
	u16 s_vsync_len; 
	u16 s_hsync_st;
	u16 s_vsync_st;
	bool s_den_inv;
	bool s_hv_sync_inv;
	bool s_clk_inv;
#endif

#if defined(CONFIG_MFD_RK616)
	u32 pll_cfg_val;  //bellow are for jettaB
	u32 frac;
	u16 scl_vst;
	u16 scl_hst;
	u16 vif_vst;
	u16 vif_hst;
#endif
	u8 hdmi_resolution;
	    /* mcu need */
	u8 mcu_wrperiod;
	u8 mcu_usefmk;
	u8 mcu_frmrate;

		/* Pin polarity */
	u8 pin_hsync;
	u8 pin_vsync;
	u8 pin_den;
	u8 pin_dclk;
	u32 lcdc_aclk;
	u8 pin_dispon;

	/* Swap rule */
	u8 swap_gb;
	u8 swap_rg;
	u8 swap_rb;
	u8 swap_delta;
	u8 swap_dumy;

	int xpos;  //horizontal display start position on the sceen ,then can be changed by application
	int ypos;
	int xsize; //horizontal and vertical display size on he screen,they can be changed by application
	int ysize;
	/* Operation function*/
	int (*init)(void);
	int (*standby)(u8 enable);
	int (*refresh)(u8 arg);
	int (*scandir)(u16 dir);
	int (*disparea)(u8 area);
	int (*sscreen_get)(struct rk29fb_screen *screen, u8 resolution);
	int (*sscreen_set)(struct rk29fb_screen *screen, bool type);// 1: use scaler 0:bypass
} rk_screen;

struct rk29fb_info {
	u32 fb_id;
	enum rk_disp_prop prop;		//display device property,like PRMRY,EXTEND
	u32 mcu_fmk_pin;
	struct rk29lcd_info *lcd_info;
	int (*io_init)(struct rk29_fb_setting_info *fb_setting);
	int (*io_deinit)(void);
	int (*io_enable)(void);
	int (*io_disable)(void);
	void (*set_screen_info)(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info );
};

#ifndef CONFIG_DISPLAY_SUPPORT
static inline void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info) {}
static inline size_t get_fb_size(void) { return 0;}
#else
extern void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info);
extern size_t get_fb_size(void);
#endif
extern void set_tv_info(struct rk29fb_screen *screen);
extern void set_hdmi_info(struct rk29fb_screen *screen);

#endif
