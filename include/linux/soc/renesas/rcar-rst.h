/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SOC_RENESAS_RCAR_RST_H__
#define __LINUX_SOC_RENESAS_RCAR_RST_H__

#ifdef CONFIG_RST_RCAR
int rcar_rst_read_mode_pins(u32 *mode);
#else
static inline int rcar_rst_read_mode_pins(u32 *mode) { return -ENODEV; }
#endif

#endif /* __LINUX_SOC_RENESAS_RCAR_RST_H__ */
