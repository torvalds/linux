/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_SIGNAL_H
#define _UAPI_LINUX_SIGNAL_H

#include <asm/signal.h>
#include <asm/siginfo.h>

#define SS_ONSTACK	1
#define SS_DISABLE	2

/* bit-flags */
#define SS_AUTODISARM	(1U << 31)	/* disable sas during sighandling */
/* mask for all SS_xxx flags */
#define SS_FLAG_BITS	SS_AUTODISARM

#endif /* _UAPI_LINUX_SIGNAL_H */
