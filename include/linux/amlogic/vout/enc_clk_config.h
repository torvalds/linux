#ifndef __ENC_CLK_CONFIG_H__
#define __ENC_CLK_CONFIG_H__

typedef enum viu_type {
    VIU_ENCL = 0,
    VIU_ENCI,
    VIU_ENCP,
    VIU_ENCT,
} viu_type_e;

extern int set_viu_path(unsigned viu_channel_sel, viu_type_e viu_type_sel);
extern void set_enci_clk(unsigned clk);
extern void set_encp_clk(unsigned clk);
extern void set_vmode_clk(vmode_t mode);


typedef struct enc_clk_val{
    vmode_t mode;
    unsigned hpll_clk_out;
    unsigned hpll_hdmi_od;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    unsigned hpll_lvds_od;
#endif
    unsigned viu_path;
    viu_type_e viu_type;
    unsigned vid_pll_div;
    unsigned clk_final_div;
    unsigned hdmi_tx_pixel_div;
    unsigned encp_div;
    unsigned enci_div;
    unsigned enct_div;
    unsigned encl_div;
    unsigned vdac0_div;
    unsigned vdac1_div;
    unsigned unused;    // prevent compile error\r
}enc_clk_val_t;


#endif
