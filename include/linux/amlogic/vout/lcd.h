/*
 * LCD Panel
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2012 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AML_PANEL_LCD_H
#define __AML_PANEL_LCD_H

/* for POL_CNTL_ADDR */
   #define LCD_DCLK_SEL             14    //FOR DCLK OUTPUT
   #define LCD_TCON_VSYNC_SEL_DVI   11	 // FOR RGB format DVI output
   #define LCD_TCON_HSYNC_SEL_DVI   10	 // FOR RGB format DVI output
   #define LCD_TCON_DE_SEL_DVI      9	 // FOR RGB format DVI output
   #define LCD_CPH3_POL             8
   #define LCD_CPH2_POL             7
   #define LCD_CPH1_POL             6
   #define LCD_TCON_DE_SEL          5
   #define LCD_TCON_VS_SEL          4
   #define LCD_TCON_HS_SEL          3
   #define LCD_DE_POL               2
   #define LCD_VS_POL               1
   #define LCD_HS_POL               0

/* for DITH_CNTL_ADDR */
   #define LCD_DITH10_EN            10
   #define LCD_DITH8_EN             9
   #define LCD_DITH_MD              8
   /// 7:4
   #define LCD_DITH10_CNTL_MSB      7
   /// 7:4
   #define LCD_DITH10_CNTL          4
   /// 3:0
   #define LCD_DITH8_CNTL_MSB       3
   /// 3:0
   #define LCD_DITH8_CNTL           0

/* for INV_CNT_ADDR */
   #define LCD_INV_EN               4
   #define LCD_INV_CNT_MSB          3
   #define LCD_INV_CNT              0

/* for TCON_MISC_SEL_ADDR */
   #define LCD_STH2_SEL             12
   #define LCD_STH1_SEL             11
   #define LCD_OEH_SEL              10
   #define LCD_VCOM_SEL             9
   #define LCD_DB_LINE_SW           8
   #define LCD_CPV2_SEL             7
   #define LCD_CPV1_SEL             6
   #define LCD_STV2_SEL             5
   #define LCD_STV1_SEL             4
   #define LCD_OEV_UNITE            3
   #define LCD_OEV3_SEL             2
   #define LCD_OEV2_SEL             1
   #define LCD_OEV1_SEL             0

/* for DUAL_PORT_CNTL_ADDR */
   #define LCD_ANALOG_SEL_CPH3      8
   #define LCD_ANALOG_3PHI_CLK_SEL  7
   #define LCD_LVDS_SEL54           6
   #define LCD_LVDS_SEL27           5
   #define LCD_TTL_SEL              4
   #define LCD_DUAL_PIXEL           3
   #define LCD_PORT_SWP             2
   #define LCD_RGB_SWP              1
   #define LCD_BIT_SWP              0

/* for LVDS_PACK_CNTL_ADDR */
   #define LCD_LD_CNT_MSB           7
   #define LCD_LD_CNT               5
   #define LCD_PN_SWP               4
   #define LCD_RES                  3
   #define LCD_LVDS_PORT_SWP        2
   #define LCD_PACK_RVS             1
   #define LCD_PACK_LITTLE          0

typedef enum
{
    LCD_NULL = 0,
    LCD_DIGITAL_TTL,
    LCD_DIGITAL_LVDS,
    LCD_DIGITAL_MINILVDS,
    LCD_TYPE_MAX,
} Lcd_Type_t;

typedef struct {
    int channel_num;
    int hv_sel;
    int tcon_1st_hs_addr;
    int tcon_1st_he_addr;
    int tcon_1st_vs_addr;
    int tcon_1st_ve_addr;
    int tcon_2nd_hs_addr;
    int tcon_2nd_he_addr;
    int tcon_2nd_vs_addr;
    int tcon_2nd_ve_addr;
} Mlvds_Tcon_Config_t;

typedef struct {
    unsigned int lvds_prem_ctl;
    unsigned int lvds_swing_ctl;
    unsigned int lvds_vcm_ctl;
    unsigned int lvds_ref_ctl;
	unsigned int lvds_phy_ctl0;
	unsigned int lvds_fifo_wr_mode;
} Lvds_Phy_Control_t;

typedef struct {
    int mlvds_insert_start;
    int total_line_clk;
    int test_dual_gate;
    //int test_bit_num;
    int test_pair_num;
//    int set_mlvds_pinmux;
    int phase_select;
    int TL080_phase;
    //Mlvds_Tcon_Config_t *mlvds_tcon_config;    //Point to TCON0~7
    //Lvds_Phy_Control_t *lvds_phy_control;
    int scan_function;
} Mlvds_Config_t;

typedef struct {
    int lvds_repack;
    int pn_swap;
    int dual_port;
    int port_reverse;
    //int bit_num;
    //Lvds_Phy_Control_t *lvds_phy_control;
} Lvds_Config_t;

// Refer to LCD Spec
typedef struct {
    u16 h_active;   	// Horizontal display area
    u16 v_active;     	// Vertical display area
    u16 h_period;       // Horizontal total period time
    u16 v_period;       // Vertical total period time
    u16 screen_ratio_width;      // screen aspect ratio width
    u16 screen_ratio_height;     // screen aspect ratio height
    u32 screen_actual_width;/* screen physical width in "mm" unit */
    u32 screen_actual_height;/* screen physical height in "mm" unit */

    Lcd_Type_t lcd_type;  // only support 3 kinds of digital panel, not include analog I/F
    u16 lcd_bits;         // 6 or 8 bits
}Lcd_Basic_t;

typedef struct {
	//u16 clk_source;		 /*video pll clock, must be multiple of 12, from 384~744*/
    u32 pll_ctrl;        /* video PLL settings */
    u32 div_ctrl;        /* video pll div settings */
    u32 clk_ctrl;        /* video clock settings */  //[19:16]ss_ctrl, [12]pll_sel, [8]div_sel, [4]vclk_sel, [3:0]xd
    u16 sync_duration_num;
    u16 sync_duration_den;

    u16 video_on_pixel;
    u16 video_on_line;

    u16 sth1_hs_addr;
    u16 sth1_he_addr;
    u16 sth1_vs_addr;
    u16 sth1_ve_addr;

    u16 oeh_hs_addr;
    u16 oeh_he_addr;
    u16 oeh_vs_addr;
    u16 oeh_ve_addr;

    u16 vcom_hswitch_addr;
    u16 vcom_vs_addr;
    u16 vcom_ve_addr;

    u16 cpv1_hs_addr;
    u16 cpv1_he_addr;
    u16 cpv1_vs_addr;
    u16 cpv1_ve_addr;

    u16 stv1_hs_addr;
    u16 stv1_he_addr;
    u16 stv1_vs_addr;
    u16 stv1_ve_addr;

    u16 oev1_hs_addr;
    u16 oev1_he_addr;
    u16 oev1_vs_addr;
    u16 oev1_ve_addr;

    u16 pol_cntl_addr;
    u16 inv_cnt_addr;
    u16 tcon_misc_sel_addr;
    u16 dual_port_cntl_addr;
} Lcd_Timing_t;

typedef struct {
    u16 clock_enable_delay;
    u16 clock_disable_delay;
    u16 pwm_enable_delay;
    u16 pwm_disable_delay;
    u16 panel_power_on_delay;
    u16 panel_power_off_delay;
    u16 backlight_power_on_delay;
    u16 backlight_power_off_delay;
} Lcd_Sequence_t;

typedef struct {
    Lvds_Config_t *lvds_config;
    Mlvds_Config_t *mlvds_config;
    Mlvds_Tcon_Config_t *mlvds_tcon_config;    //Point to TCON0~7
    Lvds_Phy_Control_t *lvds_phy_control;
} Lvds_Mlvds_Config_t;

typedef struct {
    Lcd_Basic_t lcd_basic;
    Lcd_Timing_t lcd_timing;
    Lcd_Sequence_t lcd_sequence;
    Lvds_Mlvds_Config_t lvds_mlvds_config;
} Lcd_Config_t;

typedef struct Ext_Lcd_Config_s{
	u16 h_active;
	u16 v_active;
	u16 h_period;
	u16 v_period;
	u16 video_on_pixel;
	u16 video_on_line;

	u16 lcd_bits;
	u16 lvds_repack_ctl;
	u16 pn_swap;
	u16 dual_port;
	u16 port_reverse;

	u16 lvds_prem_ctl;
	u16 lvds_swing_ctl;
	u16 lvds_vcm_ctl;
	u16 lvds_ref_ctl;
	u16 lvds_phy_ctl0;
	u16 lvds_fifo_wr_mode;

	u32 pll_ctrl;
	u32 div_ctrl;

	u16 sth1_hs_val;
	u16 sth1_he_val;
	u16 stv1_hs_val;
	u16 stv1_he_val;
	u16 stv1_vs_val;
	u16 stv1_ve_val;

	u16 clock_enable_delay;
	u16 clock_disable_delay;
	u16 pwm_enable_delay;
	u16 pwm_disable_delay;
	u16 panel_power_on_delay;
	u16 panel_power_off_delay;
	u16 backlight_power_on_delay;
	u16 backlight_power_off_delay;
} Ext_Lcd_Config_t;
#endif /* __AML_PANEL_LCD_H */
