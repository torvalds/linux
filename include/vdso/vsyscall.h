/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_VSYSCALL_H
#define __VDSO_VSYSCALL_H

#ifndef __ASSEMBLER__

#include <asm/vdso/vsyscall.h>

unsigned long vdso_update_begin(void);
void vdso_update_end(unsigned long flags);

#endif /* !__ASSEMBLER__ */

#endif /* __VDSO_VSYSCALL_H */
