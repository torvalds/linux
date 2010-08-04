/* interface for the pm_qos_power infrastructure of the linux kernel.
 *
 * Mark Gross <mgross@linux.intel.com>
 */
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/miscdevice.h>

#define PM_QOS_RESERVED 0
#define PM_QOS_CPU_DMA_LATENCY 1
#define PM_QOS_NETWORK_LATENCY 2
#define PM_QOS_NETWORK_THROUGHPUT 3

#define PM_QOS_NUM_CLASSES 4
#define PM_QOS_DEFAULT_VALUE -1

struct pm_qos_request_list;

struct pm_qos_request_list *pm_qos_add_request(int pm_qos_class, s32 value);
void pm_qos_update_request(struct pm_qos_request_list *pm_qos_req,
		s32 new_value);
void pm_qos_remove_request(struct pm_qos_request_list *pm_qos_req);

int pm_qos_request(int pm_qos_class);
int pm_qos_add_notifier(int pm_qos_class, struct notifier_block *notifier);
int pm_qos_remove_notifier(int pm_qos_class, struct notifier_block *notifier);

