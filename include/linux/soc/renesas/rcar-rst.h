#ifndef __LINUX_SOC_RENESAS_RCAR_RST_H__
#define __LINUX_SOC_RENESAS_RCAR_RST_H__

#if defined(CONFIG_ARCH_RCAR_GEN1) || defined(CONFIG_ARCH_RCAR_GEN2) || \
    defined(CONFIG_ARCH_R8A7795) || defined(CONFIG_ARCH_R8A7796)
int rcar_rst_read_mode_pins(u32 *mode);
#else
static inline int rcar_rst_read_mode_pins(u32 *mode) { return -ENODEV; }
#endif

#endif /* __LINUX_SOC_RENESAS_RCAR_RST_H__ */
