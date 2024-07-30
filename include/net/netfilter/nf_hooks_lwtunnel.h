#include <linux/sysctl.h>
#include <linux/types.h>

#ifdef CONFIG_SYSCTL
int nf_hooks_lwtunnel_sysctl_handler(const struct ctl_table *table, int write,
				     void *buffer, size_t *lenp, loff_t *ppos);
#endif
