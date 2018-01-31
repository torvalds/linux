/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_ROCKCHIP_PVTM_H
#define __SOC_ROCKCHIP_PVTM_H

#ifdef CONFIG_ROCKCHIP_PVTM
u32 rockchip_get_pvtm_value(unsigned int ch, unsigned int sub_ch,
			    unsigned int time_us);
#else
static inline u32 rockchip_get_pvtm_value(unsigned int ch, unsigned int sub_ch,
					  unsigned int time_us)
{
	return 0;
}
#endif

#endif /* __SOC_ROCKCHIP_PVTM_H */
