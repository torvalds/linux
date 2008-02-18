/* interface for the pm_qos_power infrastructure of the linux kernel.
 *
 * Mark Gross
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

int pm_qos_add_requirement(int qos, char *name, s32 value);
int pm_qos_update_requirement(int qos, char *name, s32 new_value);
void pm_qos_remove_requirement(int qos, char *name);

int pm_qos_requirement(int qos);

int pm_qos_add_notifier(int qos, struct notifier_block *notifier);
int pm_qos_remove_notifier(int qos, struct notifier_block *notifier);

