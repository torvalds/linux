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

/* Defines used for the flags field in the struct generic_pm_domain */
#define GENPD_FLAG_PM_CLK	(1U << 0) /* PM domain uses PM clk */

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
	bool (*active_wakeup)(struct device *dev);
};

struct gpd_cpuidle_data {
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
	const char *name;
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
	int (*power_off)(struct generic_pm_domain *domain);
	s64 power_off_latency_ns;
	int (*power_on)(struct generic_pm_domain *domain);
	s64 power_on_latency_ns;
	struct gpd_dev_ops dev_ops;
	s64 max_off_time_ns;	/* Maximum allowed "suspended" time. */
	bool max_off_time_changed;
	bool cached_power_down_ok;
	struct gpd_cpuidle_data *cpuidle_data;
	int (*attach_dev)(struct generic_pm_domain *domain,
			  struct device *dev);
	void (*detach_dev)(struct generic_pm_domain *domain,
			   struct device *dev);
	unsigned int flags;		/* Bit field of configs for genpd */
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

struct pm_domain_data {
	struct list_head list_node;
	struct device *dev;
};

struct generic_pm_domain_data {
	struct pm_domain_data base;
	struct gpd_timing_data td;
	struct notifier_block nb;
	struct mutex lock;
	unsigned int refcount;
	int need_restore;
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

extern struct generic_pm_domain *dev_to_genpd(struct device *dev);
extern int __pm_genpd_add_device(struct generic_pm_domain *genpd,
				 struct device *dev,
				 struct gpd_timing_data *td);

extern int __pm_genpd_name_add_device(const char *domain_name,
				      struct device *dev,
				      struct gpd_timing_data *td);

extern int pm_genpd_remove_device(struct generic_pm_domain *genpd,
				  struct device *dev);
extern void pm_genpd_dev_need_restore(struct device *dev, bool val);
extern int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
				  struct generic_pm_domain *new_subdomain);
extern int pm_genpd_add_subdomain_names(const char *master_name,
					const char *subdomain_name);
extern int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
				     struct generic_pm_domain *target);
extern int pm_genpd_attach_cpuidle(struct generic_pm_domain *genpd, int state);
extern int pm_genpd_name_attach_cpuidle(const char *name, int state);
extern int pm_genpd_detach_cpuidle(struct generic_pm_domain *genpd);
extern int pm_genpd_name_detach_cpuidle(const char *name);
extern void pm_genpd_init(struct generic_pm_domain *genpd,
			  struct dev_power_governor *gov, bool is_off);

extern int pm_genpd_poweron(struct generic_pm_domain *genpd);
extern int pm_genpd_name_poweron(const char *domain_name);
extern void pm_genpd_poweroff_unused(void);

extern struct dev_power_governor simple_qos_governor;
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
static inline int __pm_genpd_name_add_device(const char *domain_name,
					     struct device *dev,
					     struct gpd_timing_data *td)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_device(struct generic_pm_domain *genpd,
					 struct device *dev)
{
	return -ENOSYS;
}
static inline void pm_genpd_dev_need_restore(struct device *dev, bool val) {}
static inline int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
					 struct generic_pm_domain *new_sd)
{
	return -ENOSYS;
}
static inline int pm_genpd_add_subdomain_names(const char *master_name,
					       const char *subdomain_name)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
					    struct generic_pm_domain *target)
{
	return -ENOSYS;
}
static inline int pm_genpd_attach_cpuidle(struct generic_pm_domain *genpd, int st)
{
	return -ENOSYS;
}
static inline int pm_genpd_name_attach_cpuidle(const char *name, int state)
{
	return -ENOSYS;
}
static inline int pm_genpd_detach_cpuidle(struct generic_pm_domain *genpd)
{
	return -ENOSYS;
}
static inline int pm_genpd_name_detach_cpuidle(const char *name)
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
static inline int pm_genpd_name_poweron(const char *domain_name)
{
	return -ENOSYS;
}
static inline void pm_genpd_poweroff_unused(void) {}
#define simple_qos_governor NULL
#define pm_domain_always_on_gov NULL
#endif

static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return __pm_genpd_add_device(genpd, dev, NULL);
}

static inline int pm_genpd_name_add_device(const char *domain_name,
					   struct device *dev)
{
	return __pm_genpd_name_add_device(domain_name, dev, NULL);
}

#ifdef CONFIG_PM_GENERIC_DOMAINS_SLEEP
extern void pm_genpd_syscore_poweroff(struct device *dev);
extern void pm_genpd_syscore_poweron(struct device *dev);
#else
static inline void pm_genpd_syscore_poweroff(struct device *dev) {}
static inline void pm_genpd_syscore_poweron(struct device *dev) {}
#endif

/* OF PM domain providers */
struct of_device_id;

struct genpd_onecell_data {
	struct generic_pm_domain **domains;
	unsigned int num_domains;
};

typedef struct generic_pm_domain *(*genpd_xlate_t)(struct of_phandle_args *args,
						void *data);

#ifdef CONFIG_PM_GENERIC_DOMAINS_OF
int __of_genpd_add_provider(struct device_node *np, genpd_xlate_t xlate,
			void *data);
void of_genpd_del_provider(struct device_node *np);

struct generic_pm_domain *__of_genpd_xlate_simple(
					struct of_phandle_args *genpdspec,
					void *data);
struct generic_pm_domain *__of_genpd_xlate_onecell(
					struct of_phandle_args *genpdspec,
					void *data);

int genpd_dev_pm_attach(struct device *dev);
#else /* !CONFIG_PM_GENERIC_DOMAINS_OF */
static inline int __of_genpd_add_provider(struct device_node *np,
					genpd_xlate_t xlate, void *data)
{
	return 0;
}
static inline void of_genpd_del_provider(struct device_node *np) {}

#define __of_genpd_xlate_simple		NULL
#define __of_genpd_xlate_onecell	NULL

static inline int genpd_dev_pm_attach(struct device *dev)
{
	return -ENODEV;
}
#endif /* CONFIG_PM_GENERIC_DOMAINS_OF */

static inline int of_genpd_add_provider_simple(struct device_node *np,
					struct generic_pm_domain *genpd)
{
	return __of_genpd_add_provider(np, __of_genpd_xlate_simple, genpd);
}
static inline int of_genpd_add_provider_onecell(struct device_node *np,
					struct genpd_onecell_data *data)
{
	return __of_genpd_add_provider(np, __of_genpd_xlate_onecell, data);
}

#ifdef CONFIG_PM
extern int dev_pm_domain_attach(struct device *dev, bool power_on);
extern void dev_pm_domain_detach(struct device *dev, bool power_off);
#else
static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return -ENODEV;
}
static inline void dev_pm_domain_detach(struct device *dev, bool power_off) {}
#endif

#endif /* _LINUX_PM_DOMAIN_H */
