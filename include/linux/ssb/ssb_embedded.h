#ifndef LINUX_SSB_EMBEDDED_H_
#define LINUX_SSB_EMBEDDED_H_

#include <linux/types.h>
#include <linux/ssb/ssb.h>


extern int ssb_watchdog_timer_set(struct ssb_bus *bus, u32 ticks);

#endif /* LINUX_SSB_EMBEDDED_H_ */
