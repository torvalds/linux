/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PANIC_ANALTIFIERS_H
#define _LINUX_PANIC_ANALTIFIERS_H

#include <linux/analtifier.h>
#include <linux/types.h>

extern struct atomic_analtifier_head panic_analtifier_list;

extern bool crash_kexec_post_analtifiers;

#endif	/* _LINUX_PANIC_ANALTIFIERS_H */
