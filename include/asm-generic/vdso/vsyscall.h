/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_VSYSCALL_H
#define __ASM_GENERIC_VSYSCALL_H

#ifndef __ASSEMBLY__

#ifndef __arch_get_k_vdso_data
static __always_inline struct vdso_data *__arch_get_k_vdso_data(void)
{
	return NULL;
}
#endif /* __arch_get_k_vdso_data */

#ifndef __arch_update_vsyscall
static __always_inline void __arch_update_vsyscall(struct vdso_data *vdata)
{
}
#endif /* __arch_update_vsyscall */

#ifndef __arch_sync_vdso_data
static __always_inline void __arch_sync_vdso_data(struct vdso_data *vdata)
{
}
#endif /* __arch_sync_vdso_data */

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_GENERIC_VSYSCALL_H */
