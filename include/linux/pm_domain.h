/*
 * pm_domain.h - Definitions and headers related to device power domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#ifndef _LINUX_PM_DOMAIN_H
#define _LINUX_PM_DOMAIN_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/cpuidle.h>

enum gpd_status {
	GPD_STATE_ACTIVE = 0,	/* PM domain is active */
	GPD_STATE_WAIT_MASTER,	/* PM domain's master is being waited for */
	GPD_STATE_BUSY,		/* Something is happening to the PM domain */
	GPD_STATE_REPEAT,	/* Power off in progress, to be repeated */
	GPD_STATE_POWER_OFF,	/* PM domain is off */
};

struct dev_power_governor {
	bool (*power_down_ok)(struct dev_pm_domain *domain);
	bool (*stop_ok)(struct device *dev);
};

struct gpd_dev_ops {
	int (*start)(struct device *dev);
	int (*stop)(struct device *dev);
	int (*save_state)(struct device *dev);
	int (*restore_state)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*thaw)(struct device *dev);
	bool (*active_wakeup)(struct device *dev);
};

struct gpd_cpu_data {
	unsigned int saved_exit_latency;
	struct cpuidle_state *idle_state;
};

struct generic_pm_domain {
	struct dev_pm_domain domain;	/* PM domain operations */
	struct list_head gpd_list_node;	/* Node in the global PM domains list */
	struct list_head master_links;	/* Links with PM domain as a master */
	struct list_head slave_links;	/* Links with PM domain as a slave */
	struct list_head dev_list;	/* List of devices */
	struct mutex lock;
	struct dev_power_governor *gov;
	struct work_struct power_off_work;
	char *name;
	unsigned int in_progress;	/* Number of devices being suspended now */
	atomic_t sd_count;	/* Number of subdomains with power "on" */
	enum gpd_status status;	/* Current state of the domain */
	wait_queue_head_t status_wait_queue;
	struct task_struct *poweroff_task;	/* Powering off task */
	unsigned int resume_count;	/* Number of devices being resumed */
	unsigned int device_count;	/* Number of devices */
	unsigned int suspended_count;	/* System suspend device counter */
	unsigned int prepared_count;	/* Suspend counter of prepared devices */
	bool suspend_power_off;	/* Power status before system suspend */
	bool dev_irq_safe;	/* Device callbacks are IRQ-safe */
	int (*power_off)(struct generic_pm_domain *domain);
	s64 power_off_latency_ns;
	int (*power_on)(struct generic_pm_domain *domain);
	s64 power_on_latency_ns;
	struct gpd_dev_ops dev_ops;
	s64 max_off_time_ns;	/* Maximum allowed "suspended" time. */
	bool max_off_time_changed;
	bool cached_power_down_ok;
	struct device_node *of_node; /* Node in device tree */
	struct gpd_cpu_data *cpu_data;
};

static inline struct generic_pm_domain *pd_to_genpd(struct dev_pm_domain *pd)
{
	return container_of(pd, struct generic_pm_domain, domain);
}

struct gpd_link {
	struct generic_pm_domain *master;
	struct list_head master_node;
	struct generic_pm_domain *slave;
	struct list_head slave_node;
};

struct gpd_timing_data {
	s64 stop_latency_ns;
	s64 start_latency_ns;
	s64 save_state_latency_ns;
	s64 restore_state_latency_ns;
	s64 effective_constraint_ns;
	bool constraint_changed;
	bool cached_stop_ok;
};

struct generic_pm_domain_data {
	struct pm_domain_data base;
	struct gpd_dev_ops ops;
	struct gpd_timing_data td;
	struct notifier_block nb;
	struct mutex lock;
	unsigned int refcount;
	bool need_restore;
	bool syscore;
};

#ifdef CONFIG_PM_GENERIC_DOMAINS
static inline struct generic_pm_domain_data *to_gpd_data(struct pm_domain_data *pdd)
{
	return container_of(pdd, struct generic_pm_domain_data, base);
}

static inline struct generic_pm_domain_data *dev_gpd_data(struct device *dev)
{
	return to_gpd_data(dev->power.subsys_data->domain_data);
}

extern struct dev_power_governor simple_qos_governor;

extern struct generic_pm_domain *dev_to_genpd(struct device *dev);
extern int __pm_genpd_add_device(struct generic_pm_domain *genpd,
				 struct device *dev,
				 struct gpd_timing_data *td);

extern int __pm_genpd_of_add_device(struct device_node *genpd_node,
				    struct device *dev,
				    struct gpd_timing_data *td);

static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return __pm_genpd_add_device(genpd, dev, NULL);
}

static inline int pm_genpd_of_add_device(struct device_node *genpd_node,
					 struct device *dev)
{
	return __pm_genpd_of_add_device(genpd_node, dev, NULL);
}

extern int pm_genpd_remove_device(struct generic_pm_domain *genpd,
				  struct device *dev);
extern void pm_genpd_dev_syscore(struct device *dev, bool val);
extern void pm_genpd_dev_need_restore(struct device *dev, bool val);
extern int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
				  struct generic_pm_domain *new_subdomain);
extern int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
				     struct generic_pm_domain *target);
extern int pm_genpd_add_callbacks(struct device *dev,
				  struct gpd_dev_ops *ops,
				  struct gpd_timing_data *td);
extern int __pm_genpd_remove_callbacks(struct device *dev, bool clear_td);
extern int genpd_attach_cpuidle(struct generic_pm_domain *genpd, int state);
extern int genpd_detach_cpuidle(struct generic_pm_domain *genpd);
extern void pm_genpd_init(struct generic_pm_domain *genpd,
			  struct dev_power_governor *gov, bool is_off);

extern int pm_genpd_poweron(struct generic_pm_domain *genpd);

extern bool default_stop_ok(struct device *dev);

extern struct dev_power_governor pm_domain_always_on_gov;
#else

static inline struct generic_pm_domain_data *dev_gpd_data(struct device *dev)
{
	return ERR_PTR(-ENOSYS);
}
static inline struct generic_pm_domain *dev_to_genpd(struct device *dev)
{
	return ERR_PTR(-ENOSYS);
}
static inline int __pm_genpd_add_device(struct generic_pm_domain *genpd,
					struct device *dev,
					struct gpd_timing_data *td)
{
	return -ENOSYS;
}
static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_device(struct generic_pm_domain *genpd,
					 struct device *dev)
{
	return -ENOSYS;
}
static inline void pm_genpd_dev_syscore(struct device *dev, bool val) {}
static inline void pm_genpd_dev_need_restore(struct device *dev, bool val) {}
static inline int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
					 struct generic_pm_domain *new_sd)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
					    struct generic_pm_domain *target)
{
	return -ENOSYS;
}
static inline int pm_genpd_add_callbacks(struct device *dev,
					 struct gpd_dev_ops *ops,
					 struct gpd_timing_data *td)
{
	return -ENOSYS;
}
static inline int __pm_genpd_remove_callbacks(struct device *dev, bool clear_td)
{
	return -ENOSYS;
}
static inline int genpd_attach_cpuidle(struct generic_pm_domain *genpd, int st)
{
	return -ENOSYS;
}
static inline int genpd_detach_cpuidle(struct generic_pm_domain *genpd)
{
	return -ENOSYS;
}
static inline void pm_genpd_init(struct generic_pm_domain *genpd,
				 struct dev_power_governor *gov, bool is_off)
{
}
static inline int pm_genpd_poweron(struct generic_pm_domain *genpd)
{
	return -ENOSYS;
}
static inline bool default_stop_ok(struct device *dev)
{
	return false;
}
#define simple_qos_governor NULL
#define pm_domain_always_on_gov NULL
#endif

static inline int pm_genpd_remove_callbacks(struct device *dev)
{
	return __pm_genpd_remove_callbacks(dev, true);
}

#ifdef CONFIG_PM_GENERIC_DOMAINS_RUNTIME
extern void genpd_queue_power_off_work(struct generic_pm_domain *genpd);
extern void pm_genpd_poweroff_unused(void);
#else
static inline void genpd_queue_power_off_work(struct generic_pm_domain *gpd) {}
static inline void pm_genpd_poweroff_unused(void) {}
#endif

#ifdef CONFIG_PM_GENERIC_DOMAINS_SLEEP
extern void pm_genpd_syscore_switch(struct device *dev, bool suspend);
#else
static inline void pm_genpd_syscore_switch(struct device *dev, bool suspend) {}
#endif

static inline void pm_genpd_syscore_poweroff(struct device *dev)
{
	pm_genpd_syscore_switch(dev, true);
}

static inline void pm_genpd_syscore_poweron(struct device *dev)
{
	pm_genpd_syscore_switch(dev, false);
}

#endif /* _LINUX_PM_DOMAIN_H */
