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
#include <linux/spinlock.h>

/* Defines used for the flags field in the struct generic_pm_domain */
#define GENPD_FLAG_PM_CLK	(1U << 0) /* PM domain uses PM clk */
#define GENPD_FLAG_IRQ_SAFE	(1U << 1) /* PM domain operates in atomic */
#define GENPD_FLAG_ALWAYS_ON	(1U << 2) /* PM domain is always powered on */

enum gpd_status {
	GPD_STATE_ACTIVE = 0,	/* PM domain is active */
	GPD_STATE_POWER_OFF,	/* PM domain is off */
};

struct dev_power_governor {
	bool (*power_down_ok)(struct dev_pm_domain *domain);
	bool (*suspend_ok)(struct device *dev);
};

struct gpd_dev_ops {
	int (*start)(struct device *dev);
	int (*stop)(struct device *dev);
	bool (*active_wakeup)(struct device *dev);
};

struct genpd_power_state {
	s64 power_off_latency_ns;
	s64 power_on_latency_ns;
	s64 residency_ns;
	struct fwnode_handle *fwnode;
};

struct genpd_lock_ops;

struct generic_pm_domain {
	struct dev_pm_domain domain;	/* PM domain operations */
	struct list_head gpd_list_node;	/* Node in the global PM domains list */
	struct list_head master_links;	/* Links with PM domain as a master */
	struct list_head slave_links;	/* Links with PM domain as a slave */
	struct list_head dev_list;	/* List of devices */
	struct dev_power_governor *gov;
	struct work_struct power_off_work;
	struct fwnode_handle *provider;	/* Identity of the domain provider */
	bool has_provider;
	const char *name;
	atomic_t sd_count;	/* Number of subdomains with power "on" */
	enum gpd_status status;	/* Current state of the domain */
	unsigned int device_count;	/* Number of devices */
	unsigned int suspended_count;	/* System suspend device counter */
	unsigned int prepared_count;	/* Suspend counter of prepared devices */
	int (*power_off)(struct generic_pm_domain *domain);
	int (*power_on)(struct generic_pm_domain *domain);
	struct gpd_dev_ops dev_ops;
	s64 max_off_time_ns;	/* Maximum allowed "suspended" time. */
	bool max_off_time_changed;
	bool cached_power_down_ok;
	int (*attach_dev)(struct generic_pm_domain *domain,
			  struct device *dev);
	void (*detach_dev)(struct generic_pm_domain *domain,
			   struct device *dev);
	unsigned int flags;		/* Bit field of configs for genpd */
	struct genpd_power_state *states;
	unsigned int state_count; /* number of states */
	unsigned int state_idx; /* state that genpd will go to when off */
	void *free; /* Free the state that was allocated for default */
	const struct genpd_lock_ops *lock_ops;
	union {
		struct mutex mlock;
		struct {
			spinlock_t slock;
			unsigned long lock_flags;
		};
	};

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
	s64 suspend_latency_ns;
	s64 resume_latency_ns;
	s64 effective_constraint_ns;
	bool constraint_changed;
	bool cached_suspend_ok;
};

struct pm_domain_data {
	struct list_head list_node;
	struct device *dev;
};

struct generic_pm_domain_data {
	struct pm_domain_data base;
	struct gpd_timing_data td;
	struct notifier_block nb;
	void *data;
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

extern int __pm_genpd_add_device(struct generic_pm_domain *genpd,
				 struct device *dev,
				 struct gpd_timing_data *td);

extern int pm_genpd_remove_device(struct generic_pm_domain *genpd,
				  struct device *dev);
extern int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
				  struct generic_pm_domain *new_subdomain);
extern int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
				     struct generic_pm_domain *target);
extern int pm_genpd_init(struct generic_pm_domain *genpd,
			 struct dev_power_governor *gov, bool is_off);
extern int pm_genpd_remove(struct generic_pm_domain *genpd);

extern struct dev_power_governor simple_qos_governor;
extern struct dev_power_governor pm_domain_always_on_gov;
#else

static inline struct generic_pm_domain_data *dev_gpd_data(struct device *dev)
{
	return ERR_PTR(-ENOSYS);
}
static inline int __pm_genpd_add_device(struct generic_pm_domain *genpd,
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
static inline int pm_genpd_init(struct generic_pm_domain *genpd,
				struct dev_power_governor *gov, bool is_off)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove(struct generic_pm_domain *genpd)
{
	return -ENOTSUPP;
}

#define simple_qos_governor		(*(struct dev_power_governor *)(NULL))
#define pm_domain_always_on_gov		(*(struct dev_power_governor *)(NULL))
#endif

static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return __pm_genpd_add_device(genpd, dev, NULL);
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

typedef struct generic_pm_domain *(*genpd_xlate_t)(struct of_phandle_args *args,
						   void *data);

struct genpd_onecell_data {
	struct generic_pm_domain **domains;
	unsigned int num_domains;
	genpd_xlate_t xlate;
};

#ifdef CONFIG_PM_GENERIC_DOMAINS_OF
int of_genpd_add_provider_simple(struct device_node *np,
				 struct generic_pm_domain *genpd);
int of_genpd_add_provider_onecell(struct device_node *np,
				  struct genpd_onecell_data *data);
void of_genpd_del_provider(struct device_node *np);
extern int of_genpd_add_device(struct of_phandle_args *args,
			       struct device *dev);
extern int of_genpd_add_subdomain(struct of_phandle_args *parent,
				  struct of_phandle_args *new_subdomain);
extern struct generic_pm_domain *of_genpd_remove_last(struct device_node *np);
extern int of_genpd_parse_idle_states(struct device_node *dn,
			struct genpd_power_state **states, int *n);

int genpd_dev_pm_attach(struct device *dev);
#else /* !CONFIG_PM_GENERIC_DOMAINS_OF */
static inline int of_genpd_add_provider_simple(struct device_node *np,
					struct generic_pm_domain *genpd)
{
	return -ENOTSUPP;
}

static inline int of_genpd_add_provider_onecell(struct device_node *np,
					struct genpd_onecell_data *data)
{
	return -ENOTSUPP;
}

static inline void of_genpd_del_provider(struct device_node *np) {}

static inline int of_genpd_add_device(struct of_phandle_args *args,
				      struct device *dev)
{
	return -ENODEV;
}

static inline int of_genpd_add_subdomain(struct of_phandle_args *parent,
					 struct of_phandle_args *new_subdomain)
{
	return -ENODEV;
}

static inline int of_genpd_parse_idle_states(struct device_node *dn,
			struct genpd_power_state **states, int *n)
{
	return -ENODEV;
}

static inline int genpd_dev_pm_attach(struct device *dev)
{
	return -ENODEV;
}

static inline
struct generic_pm_domain *of_genpd_remove_last(struct device_node *np)
{
	return ERR_PTR(-ENOTSUPP);
}
#endif /* CONFIG_PM_GENERIC_DOMAINS_OF */

#ifdef CONFIG_PM
extern int dev_pm_domain_attach(struct device *dev, bool power_on);
extern void dev_pm_domain_detach(struct device *dev, bool power_off);
extern void dev_pm_domain_set(struct device *dev, struct dev_pm_domain *pd);
#else
static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return -ENODEV;
}
static inline void dev_pm_domain_detach(struct device *dev, bool power_off) {}
static inline void dev_pm_domain_set(struct device *dev,
				     struct dev_pm_domain *pd) {}
#endif

#endif /* _LINUX_PM_DOMAIN_H */
