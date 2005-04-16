#include <linux/config.h>
#define acornfb_valid_pixrate(var) (var->pixclock >= 39325 && var->pixclock <= 40119)

static inline void
acornfb_vidc20_find_rates(struct vidc_timing *vidc,
			  struct fb_var_screeninfo *var)
{
	u_int bandwidth;
  
	vidc->control |= VIDC20_CTRL_PIX_CK;

	/* Calculate bandwidth */
	bandwidth = var->pixclock * 8 / var->bits_per_pixel;

	/* Encode bandwidth as VIDC20 setting */
	if (bandwidth > 16667*2)
		vidc->control |= VIDC20_CTRL_FIFO_16;
	else if (bandwidth > 13333*2)
		vidc->control |= VIDC20_CTRL_FIFO_20;
	else if (bandwidth > 11111*2)
		vidc->control |= VIDC20_CTRL_FIFO_24;
	else
		vidc->control |= VIDC20_CTRL_FIFO_28;

	vidc->pll_ctl  = 0x2020;
}

#ifdef CONFIG_CHRONTEL_7003
#define acornfb_default_control()	VIDC20_CTRL_PIX_HCLK
#else
#define acornfb_default_control()	VIDC20_CTRL_PIX_VCLK
#endif

#define acornfb_default_econtrol()	VIDC20_ECTL_DAC | VIDC20_ECTL_REG(3) | VIDC20_ECTL_ECK
