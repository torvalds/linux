#ifndef  LOGO_DEV_OSD_H
#define LOGO_DEV_OSD_H

#define DisableVideoLayer() \
    do { aml_clr_reg32_mask(P_VPP_MISC, \
	VPP_VD1_PREBLEND | VPP_VD1_POSTBLEND);  \
    } while (0)

#ifdef CONFIG_AM_HDMI_ONLY
#define PARA_HDMI_ONLY    15

typedef  struct {
	char *name;
	int   info;	
}hdmi_only_info_t;
#endif

#endif
