#ifndef LINUX_SSB_EMBEDDED_H_
#define LINUX_SSB_EMBEDDED_H_

#include <linux/types.h>
#include <linux/ssb/ssb.h>


extern int ssb_watchdog_timer_set(struct ssb_bus *bus, u32 ticks);

/* Generic GPIO API */
u32 ssb_gpio_in(struct ssb_bus *bus, u32 mask);
u32 ssb_gpio_out(struct ssb_bus *bus, u32 mask, u32 value);
u32 ssb_gpio_outen(struct ssb_bus *bus, u32 mask, u32 value);
u32 ssb_gpio_control(struct ssb_bus *bus, u32 mask, u32 value);
u32 ssb_gpio_intmask(struct ssb_bus *bus, u32 mask, u32 value);
u32 ssb_gpio_polarity(struct ssb_bus *bus, u32 mask, u32 value);

#endif /* LINUX_SSB_EMBEDDED_H_ */
