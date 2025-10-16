/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_VSYSCALL_H
#define __ASM_GENERIC_VSYSCALL_H

#ifndef __ASSEMBLY__

#ifndef __arch_get_vdso_u_time_data
static __always_inline const struct vdso_time_data *__arch_get_vdso_u_time_data(void)
{
	return &vdso_u_time_data;
}
#endif

#ifndef __arch_get_vdso_u_rng_data
static __always_inline const struct vdso_rng_data *__arch_get_vdso_u_rng_data(void)
{
	return &vdso_u_rng_data;
}
#endif

#ifndef __arch_update_vdso_clock
static __always_inline void __arch_update_vdso_clock(struct vdso_clock *vc)
{
}
#endif /* __arch_update_vdso_clock */

#ifndef __arch_sync_vdso_time_data
static __always_inline void __arch_sync_vdso_time_data(struct vdso_time_data *vdata)
{
}
#endif /* __arch_sync_vdso_time_data */

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_GENERIC_VSYSCALL_H */
