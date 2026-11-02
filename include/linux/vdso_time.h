/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VDSO_TIME_H
#define _LINUX_VDSO_TIME_H

unsigned long vdso_time_update_begin(void);
void vdso_time_update_end(unsigned long flags);

#endif /* _LINUX_VDSO_TIME_H */
