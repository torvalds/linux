#ifndef _UAPI_LINUX_SIGNAL_H
#define _UAPI_LINUX_SIGNAL_H

#include <asm/signal.h>
#include <asm/siginfo.h>

#define SS_ONSTACK	1
#define SS_DISABLE	2

/* mask for all SS_xxx flags */
#define SS_FLAG_BITS	0

#endif /* _UAPI_LINUX_SIGNAL_H */
