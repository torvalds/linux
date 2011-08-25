#ifndef _LINUX_PM_QOS_H
#define _LINUX_PM_QOS_H
/* interface for the pm_qos_power infrastructure of the linux kernel.
 *
 * Mark Gross <mgross@linux.intel.com>
 */
#include <linux/plist.h>
#include <linux/notifier.h>
#include <linux/miscdevice.h>

#define PM_QOS_RESERVED 0
#define PM_QOS_CPU_DMA_LATENCY 1
#define PM_QOS_NETWORK_LATENCY 2
#define PM_QOS_NETWORK_THROUGHPUT 3

#define PM_QOS_NUM_CLASSES 4
#define PM_QOS_DEFAULT_VALUE -1

#define PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE	(2000 * USEC_PER_SEC)
#define PM_QOS_NETWORK_LAT_DEFAULT_VALUE	(2000 * USEC_PER_SEC)
#define PM_QOS_NETWORK_THROUGHPUT_DEFAULT_VALUE	0

struct pm_qos_request {
	struct plist_node node;
	int pm_qos_class;
};

#ifdef CONFIG_PM
void pm_qos_add_request(struct pm_qos_request *req, int pm_qos_class,
			s32 value);
void pm_qos_update_request(struct pm_qos_request *req,
			   s32 new_value);
void pm_qos_remove_request(struct pm_qos_request *req);

int pm_qos_request(int pm_qos_class);
int pm_qos_add_notifier(int pm_qos_class, struct notifier_block *notifier);
int pm_qos_remove_notifier(int pm_qos_class, struct notifier_block *notifier);
int pm_qos_request_active(struct pm_qos_request *req);
#else
static inline void pm_qos_add_request(struct pm_qos_request *req,
				      int pm_qos_class, s32 value)
			{ return; }
static inline void pm_qos_update_request(struct pm_qos_request *req,
					 s32 new_value)
			{ return; }
static inline void pm_qos_remove_request(struct pm_qos_request *req)
			{ return; }

static inline int pm_qos_request(int pm_qos_class)
			{ return 0; }
static inline int pm_qos_add_notifier(int pm_qos_class,
				      struct notifier_block *notifier)
			{ return 0; }
static inline int pm_qos_remove_notifier(int pm_qos_class,
					 struct notifier_block *notifier)
			{ return 0; }
static inline int pm_qos_request_active(struct pm_qos_request *req)
			{ return 0; }
#endif

#endif
