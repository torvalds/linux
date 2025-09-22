/* Public domain. */

#ifndef _LINUX_MUX_CONSUMER_H
#define _LINUX_MUX_CONSUMER_H

struct mux_control;

static inline struct mux_control *
devm_mux_control_get(struct device *dev, const char *name)
{
	return NULL;
}

static inline int
mux_control_select(struct mux_control *mux, u_int state)
{
	return 0;
}

#endif
