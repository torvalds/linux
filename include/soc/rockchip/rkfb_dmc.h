/*
 * Rockchip devfb driver will probe earlier than devfreq, so it needs to register
 * dmc_notify after than rk3399 dmc driver.
*/

#if defined(CONFIG_LCDC_RK322X)
int vop_register_dmc(void);
#else
static inline int vop_register_dmc(void) { return 0;};
#endif
