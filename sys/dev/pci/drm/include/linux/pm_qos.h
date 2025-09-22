/* Public domain. */

#ifndef _LINUX_PM_QOS_H
#define _LINUX_PM_QOS_H

struct pm_qos_request {
};

#define PM_QOS_DEFAULT_VALUE	-1

static inline void
cpu_latency_qos_update_request(struct pm_qos_request *r, int v)
{
}

static inline void
cpu_latency_qos_add_request(struct pm_qos_request *r, int v)
{
}

static inline void
cpu_latency_qos_remove_request(struct pm_qos_request *r)
{
}

static inline bool
cpu_latency_qos_request_active(struct pm_qos_request *r)
{
	return false;
}

#endif
