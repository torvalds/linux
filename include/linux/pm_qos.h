#ifndef _LINUX_PM_QOS_H
#define _LINUX_PM_QOS_H
/* interface for the pm_qos_power infrastructure of the linux kernel.
 *
 * Mark Gross <mgross@linux.intel.com>
 */
#include <linux/plist.h>
#include <linux/notifier.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/workqueue.h>

enum {
	PM_QOS_RESERVED = 0,
	PM_QOS_CPU_DMA_LATENCY,
	PM_QOS_NETWORK_LATENCY,
	PM_QOS_NETWORK_THROUGHPUT,

	/* insert new class ID */
	PM_QOS_NUM_CLASSES,
};

#define PM_QOS_DEFAULT_VALUE -1

#define PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE	(2000 * USEC_PER_SEC)
#define PM_QOS_NETWORK_LAT_DEFAULT_VALUE	(2000 * USEC_PER_SEC)
#define PM_QOS_NETWORK_THROUGHPUT_DEFAULT_VALUE	0
#define PM_QOS_DEV_LAT_DEFAULT_VALUE		0

struct pm_qos_request {
	struct plist_node node;
	int pm_qos_class;
	struct delayed_work work; /* for pm_qos_update_request_timeout */
};

struct dev_pm_qos_request {
	struct plist_node node;
	struct device *dev;
};

enum pm_qos_type {
	PM_QOS_UNITIALIZED,
	PM_QOS_MAX,		/* return the largest value */
	PM_QOS_MIN		/* return the smallest value */
};

/*
 * Note: The lockless read path depends on the CPU accessing
 * target_value atomically.  Atomic access is only guaranteed on all CPU
 * types linux supports for 32 bit quantites
 */
struct pm_qos_constraints {
	struct plist_head list;
	s32 target_value;	/* Do not change to 64 bit */
	s32 default_value;
	enum pm_qos_type type;
	struct blocking_notifier_head *notifiers;
};

/* Action requested to pm_qos_update_target */
enum pm_qos_req_action {
	PM_QOS_ADD_REQ,		/* Add a new request */
	PM_QOS_UPDATE_REQ,	/* Update an existing request */
	PM_QOS_REMOVE_REQ	/* Remove an existing request */
};

static inline int dev_pm_qos_request_active(struct dev_pm_qos_request *req)
{
	return req->dev != NULL;
}

int pm_qos_update_target(struct pm_qos_constraints *c, struct plist_node *node,
			 enum pm_qos_req_action action, int value);
void pm_qos_add_request(struct pm_qos_request *req, int pm_qos_class,
			s32 value);
void pm_qos_update_request(struct pm_qos_request *req,
			   s32 new_value);
void pm_qos_update_request_timeout(struct pm_qos_request *req,
				   s32 new_value, unsigned long timeout_us);
void pm_qos_remove_request(struct pm_qos_request *req);

int pm_qos_request(int pm_qos_class);
int pm_qos_add_notifier(int pm_qos_class, struct notifier_block *notifier);
int pm_qos_remove_notifier(int pm_qos_class, struct notifier_block *notifier);
int pm_qos_request_active(struct pm_qos_request *req);
s32 pm_qos_read_value(struct pm_qos_constraints *c);

#ifdef CONFIG_PM
s32 __dev_pm_qos_read_value(struct device *dev);
s32 dev_pm_qos_read_value(struct device *dev);
int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req,
			   s32 value);
int dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value);
int dev_pm_qos_remove_request(struct dev_pm_qos_request *req);
int dev_pm_qos_add_notifier(struct device *dev,
			    struct notifier_block *notifier);
int dev_pm_qos_remove_notifier(struct device *dev,
			       struct notifier_block *notifier);
int dev_pm_qos_add_global_notifier(struct notifier_block *notifier);
int dev_pm_qos_remove_global_notifier(struct notifier_block *notifier);
void dev_pm_qos_constraints_init(struct device *dev);
void dev_pm_qos_constraints_destroy(struct device *dev);
int dev_pm_qos_add_ancestor_request(struct device *dev,
				    struct dev_pm_qos_request *req, s32 value);
#else
static inline s32 __dev_pm_qos_read_value(struct device *dev)
			{ return 0; }
static inline s32 dev_pm_qos_read_value(struct device *dev)
			{ return 0; }
static inline int dev_pm_qos_add_request(struct device *dev,
					 struct dev_pm_qos_request *req,
					 s32 value)
			{ return 0; }
static inline int dev_pm_qos_update_request(struct dev_pm_qos_request *req,
					    s32 new_value)
			{ return 0; }
static inline int dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
			{ return 0; }
static inline int dev_pm_qos_add_notifier(struct device *dev,
					  struct notifier_block *notifier)
			{ return 0; }
static inline int dev_pm_qos_remove_notifier(struct device *dev,
					     struct notifier_block *notifier)
			{ return 0; }
static inline int dev_pm_qos_add_global_notifier(
					struct notifier_block *notifier)
			{ return 0; }
static inline int dev_pm_qos_remove_global_notifier(
					struct notifier_block *notifier)
			{ return 0; }
static inline void dev_pm_qos_constraints_init(struct device *dev)
{
	dev->power.power_state = PMSG_ON;
}
static inline void dev_pm_qos_constraints_destroy(struct device *dev)
{
	dev->power.power_state = PMSG_INVALID;
}
static inline int dev_pm_qos_add_ancestor_request(struct device *dev,
				    struct dev_pm_qos_request *req, s32 value)
			{ return 0; }
#endif

#ifdef CONFIG_PM_RUNTIME
int dev_pm_qos_expose_latency_limit(struct device *dev, s32 value);
void dev_pm_qos_hide_latency_limit(struct device *dev);
#else
static inline int dev_pm_qos_expose_latency_limit(struct device *dev, s32 value)
			{ return 0; }
static inline void dev_pm_qos_hide_latency_limit(struct device *dev) {}
#endif

#endif
