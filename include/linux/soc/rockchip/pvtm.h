/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_ROCKCHIP_PVTM_H
#define __SOC_ROCKCHIP_PVTM_H

#if IS_ENABLED(CONFIG_ROCKCHIP_PVTM)
u32 rockchip_get_pvtm_value(unsigned int id, unsigned int ring_sel,
			    unsigned int time_us);
#else
static inline u32 rockchip_get_pvtm_value(unsigned int id,
					  unsigned int ring_sel,
					  unsigned int time_us)
{
	return 0;
}
#endif

#endif /* __SOC_ROCKCHIP_PVTM_H */
