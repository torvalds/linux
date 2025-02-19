/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pm_domain.h - Definitions and headers related to device power domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 */

#ifndef _LINUX_PM_DOMAIN_H
#define _LINUX_PM_DOMAIN_H

#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/cpumask_types.h>
#include <linux/time64.h>

/*
 * Flags to control the behaviour when attaching a device to its PM domains.
 *
 * PD_FLAG_NO_DEV_LINK:		As the default behaviour creates a device-link
 *				for every PM domain that gets attached, this
 *				flag can be used to skip that.
 *
 * PD_FLAG_DEV_LINK_ON:		Add the DL_FLAG_RPM_ACTIVE to power-on the
 *				supplier and its PM domain when creating the
 *				device-links.
 *
 * PD_FLAG_REQUIRED_OPP:	Assign required_devs for the required OPPs. The
 *				index of the required OPP must correspond to the
 *				index in the array of the pd_names. If pd_names
 *				isn't specified, the index just follows the
 *				index for the attached PM domain.
 *
 */
#define PD_FLAG_NO_DEV_LINK		BIT(0)
#define PD_FLAG_DEV_LINK_ON		BIT(1)
#define PD_FLAG_REQUIRED_OPP		BIT(2)

struct dev_pm_domain_attach_data {
	const char * const *pd_names;
	const u32 num_pd_names;
	const u32 pd_flags;
};

struct dev_pm_domain_list {
	struct device **pd_devs;
	struct device_link **pd_links;
	u32 *opp_tokens;
	u32 num_pds;
};

/*
 * Flags to control the behaviour of a genpd.
 *
 * These flags may be set in the struct generic_pm_domain's flags field by a
 * genpd backend driver. The flags must be set before it calls pm_genpd_init(),
 * which initializes a genpd.
 *
 * GENPD_FLAG_PM_CLK:		Instructs genpd to use the PM clk framework,
 *				while powering on/off attached devices.
 *
 * GENPD_FLAG_IRQ_SAFE:		This informs genpd that its backend callbacks,
 *				->power_on|off(), doesn't sleep. Hence, these
 *				can be invoked from within atomic context, which
 *				enables genpd to power on/off the PM domain,
 *				even when pm_runtime_is_irq_safe() returns true,
 *				for any of its attached devices. Note that, a
 *				genpd having this flag set, requires its
 *				masterdomains to also have it set.
 *
 * GENPD_FLAG_ALWAYS_ON:	Instructs genpd to always keep the PM domain
 *				powered on.
 *
 * GENPD_FLAG_ACTIVE_WAKEUP:	Instructs genpd to keep the PM domain powered
 *				on, in case any of its attached devices is used
 *				in the wakeup path to serve system wakeups.
 *
 * GENPD_FLAG_CPU_DOMAIN:	Instructs genpd that it should expect to get
 *				devices attached, which may belong to CPUs or
 *				possibly have subdomains with CPUs attached.
 *				This flag enables the genpd backend driver to
 *				deploy idle power management support for CPUs
 *				and groups of CPUs. Note that, the backend
 *				driver must then comply with the so called,
 *				last-man-standing algorithm, for the CPUs in the
 *				PM domain.
 *
 * GENPD_FLAG_RPM_ALWAYS_ON:	Instructs genpd to always keep the PM domain
 *				powered on except for system suspend.
 *
 * GENPD_FLAG_MIN_RESIDENCY:	Enable the genpd governor to consider its
 *				components' next wakeup when determining the
 *				optimal idle state.
 *
 * GENPD_FLAG_OPP_TABLE_FW:	The genpd provider supports performance states,
 *				but its corresponding OPP tables are not
 *				described in DT, but are given directly by FW.
 *
 * GENPD_FLAG_DEV_NAME_FW:	Instructs genpd to generate an unique device name
 *				using ida. It is used by genpd providers which
 *				get their genpd-names directly from FW.
 */
#define GENPD_FLAG_PM_CLK	 (1U << 0)
#define GENPD_FLAG_IRQ_SAFE	 (1U << 1)
#define GENPD_FLAG_ALWAYS_ON	 (1U << 2)
#define GENPD_FLAG_ACTIVE_WAKEUP (1U << 3)
#define GENPD_FLAG_CPU_DOMAIN	 (1U << 4)
#define GENPD_FLAG_RPM_ALWAYS_ON (1U << 5)
#define GENPD_FLAG_MIN_RESIDENCY (1U << 6)
#define GENPD_FLAG_OPP_TABLE_FW	 (1U << 7)
#define GENPD_FLAG_DEV_NAME_FW	 (1U << 8)

enum gpd_status {
	GENPD_STATE_ON = 0,	/* PM domain is on */
	GENPD_STATE_OFF,	/* PM domain is off */
};

enum genpd_notication {
	GENPD_NOTIFY_PRE_OFF = 0,
	GENPD_NOTIFY_OFF,
	GENPD_NOTIFY_PRE_ON,
	GENPD_NOTIFY_ON,
};

struct dev_power_governor {
	bool (*power_down_ok)(struct dev_pm_domain *domain);
	bool (*suspend_ok)(struct device *dev);
};

struct gpd_dev_ops {
	int (*start)(struct device *dev);
	int (*stop)(struct device *dev);
};

struct genpd_governor_data {
	s64 max_off_time_ns;
	bool max_off_time_changed;
	ktime_t next_wakeup;
	ktime_t next_hrtimer;
	bool cached_power_down_ok;
	bool cached_power_down_state_idx;
};

struct genpd_power_state {
	const char *name;
	s64 power_off_latency_ns;
	s64 power_on_latency_ns;
	s64 residency_ns;
	u64 usage;
	u64 rejected;
	struct fwnode_handle *fwnode;
	u64 idle_time;
	void *data;
};

struct genpd_lock_ops;
struct opp_table;

struct generic_pm_domain {
	struct device dev;
	struct dev_pm_domain domain;	/* PM domain operations */
	struct list_head gpd_list_node;	/* Node in the global PM domains list */
	struct list_head parent_links;	/* Links with PM domain as a parent */
	struct list_head child_links;	/* Links with PM domain as a child */
	struct list_head dev_list;	/* List of devices */
	struct dev_power_governor *gov;
	struct genpd_governor_data *gd;	/* Data used by a genpd governor. */
	struct work_struct power_off_work;
	struct fwnode_handle *provider;	/* Identity of the domain provider */
	bool has_provider;
	const char *name;
	atomic_t sd_count;	/* Number of subdomains with power "on" */
	enum gpd_status status;	/* Current state of the domain */
	unsigned int device_count;	/* Number of devices */
	unsigned int device_id;		/* unique device id */
	unsigned int suspended_count;	/* System suspend device counter */
	unsigned int prepared_count;	/* Suspend counter of prepared devices */
	unsigned int performance_state;	/* Aggregated max performance state */
	cpumask_var_t cpus;		/* A cpumask of the attached CPUs */
	bool synced_poweroff;		/* A consumer needs a synced poweroff */
	int (*power_off)(struct generic_pm_domain *domain);
	int (*power_on)(struct generic_pm_domain *domain);
	struct raw_notifier_head power_notifiers; /* Power on/off notifiers */
	struct opp_table *opp_table;	/* OPP table of the genpd */
	int (*set_performance_state)(struct generic_pm_domain *genpd,
				     unsigned int state);
	struct gpd_dev_ops dev_ops;
	int (*set_hwmode_dev)(struct generic_pm_domain *domain,
			      struct device *dev, bool enable);
	bool (*get_hwmode_dev)(struct generic_pm_domain *domain,
			      struct device *dev);
	int (*attach_dev)(struct generic_pm_domain *domain,
			  struct device *dev);
	void (*detach_dev)(struct generic_pm_domain *domain,
			   struct device *dev);
	unsigned int flags;		/* Bit field of configs for genpd */
	struct genpd_power_state *states;
	void (*free_states)(struct genpd_power_state *states,
			    unsigned int state_count);
	unsigned int state_count; /* number of states */
	unsigned int state_idx; /* state that genpd will go to when off */
	u64 on_time;
	u64 accounting_time;
	const struct genpd_lock_ops *lock_ops;
	union {
		struct mutex mlock;
		struct {
			spinlock_t slock;
			unsigned long lock_flags;
		};
		struct {
			raw_spinlock_t raw_slock;
			unsigned long raw_lock_flags;
		};
	};
};

static inline struct generic_pm_domain *pd_to_genpd(struct dev_pm_domain *pd)
{
	return container_of(pd, struct generic_pm_domain, domain);
}

struct gpd_link {
	struct generic_pm_domain *parent;
	struct list_head parent_node;
	struct generic_pm_domain *child;
	struct list_head child_node;

	/* Sub-domain's per-master domain performance state */
	unsigned int performance_state;
	unsigned int prev_performance_state;
};

struct gpd_timing_data {
	s64 suspend_latency_ns;
	s64 resume_latency_ns;
	s64 effective_constraint_ns;
	ktime_t	next_wakeup;
	bool constraint_changed;
	bool cached_suspend_ok;
};

struct pm_domain_data {
	struct list_head list_node;
	struct device *dev;
};

struct generic_pm_domain_data {
	struct pm_domain_data base;
	struct gpd_timing_data *td;
	struct notifier_block nb;
	struct notifier_block *power_nb;
	int cpu;
	unsigned int performance_state;
	unsigned int default_pstate;
	unsigned int rpm_pstate;
	unsigned int opp_token;
	bool hw_mode;
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

int pm_genpd_add_device(struct generic_pm_domain *genpd, struct device *dev);
int pm_genpd_remove_device(struct device *dev);
int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
			   struct generic_pm_domain *subdomain);
int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
			      struct generic_pm_domain *subdomain);
int pm_genpd_init(struct generic_pm_domain *genpd,
		  struct dev_power_governor *gov, bool is_off);
int pm_genpd_remove(struct generic_pm_domain *genpd);
struct device *dev_to_genpd_dev(struct device *dev);
int dev_pm_genpd_set_performance_state(struct device *dev, unsigned int state);
int dev_pm_genpd_add_notifier(struct device *dev, struct notifier_block *nb);
int dev_pm_genpd_remove_notifier(struct device *dev);
void dev_pm_genpd_set_next_wakeup(struct device *dev, ktime_t next);
ktime_t dev_pm_genpd_get_next_hrtimer(struct device *dev);
void dev_pm_genpd_synced_poweroff(struct device *dev);
int dev_pm_genpd_set_hwmode(struct device *dev, bool enable);
bool dev_pm_genpd_get_hwmode(struct device *dev);

extern struct dev_power_governor simple_qos_governor;
extern struct dev_power_governor pm_domain_always_on_gov;
#ifdef CONFIG_CPU_IDLE
extern struct dev_power_governor pm_domain_cpu_gov;
#endif
#else

static inline struct generic_pm_domain_data *dev_gpd_data(struct device *dev)
{
	return ERR_PTR(-ENOSYS);
}
static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_device(struct device *dev)
{
	return -ENOSYS;
}
static inline int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
					 struct generic_pm_domain *subdomain)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
					    struct generic_pm_domain *subdomain)
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
	return -EOPNOTSUPP;
}

static inline struct device *dev_to_genpd_dev(struct device *dev)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int dev_pm_genpd_set_performance_state(struct device *dev,
						     unsigned int state)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_genpd_add_notifier(struct device *dev,
					    struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_genpd_remove_notifier(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_genpd_set_next_wakeup(struct device *dev, ktime_t next)
{ }

static inline ktime_t dev_pm_genpd_get_next_hrtimer(struct device *dev)
{
	return KTIME_MAX;
}
static inline void dev_pm_genpd_synced_poweroff(struct device *dev)
{ }

static inline int dev_pm_genpd_set_hwmode(struct device *dev, bool enable)
{
	return -EOPNOTSUPP;
}

static inline bool dev_pm_genpd_get_hwmode(struct device *dev)
{
	return false;
}

#define simple_qos_governor		(*(struct dev_power_governor *)(NULL))
#define pm_domain_always_on_gov		(*(struct dev_power_governor *)(NULL))
#endif

#ifdef CONFIG_PM_GENERIC_DOMAINS_SLEEP
void dev_pm_genpd_suspend(struct device *dev);
void dev_pm_genpd_resume(struct device *dev);
#else
static inline void dev_pm_genpd_suspend(struct device *dev) {}
static inline void dev_pm_genpd_resume(struct device *dev) {}
#endif

/* OF PM domain providers */
struct of_device_id;

typedef struct generic_pm_domain *(*genpd_xlate_t)(const struct of_phandle_args *args,
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
int of_genpd_add_device(const struct of_phandle_args *args, struct device *dev);
int of_genpd_add_subdomain(const struct of_phandle_args *parent_spec,
			   const struct of_phandle_args *subdomain_spec);
int of_genpd_remove_subdomain(const struct of_phandle_args *parent_spec,
			      const struct of_phandle_args *subdomain_spec);
struct generic_pm_domain *of_genpd_remove_last(struct device_node *np);
int of_genpd_parse_idle_states(struct device_node *dn,
			       struct genpd_power_state **states, int *n);

int genpd_dev_pm_attach(struct device *dev);
struct device *genpd_dev_pm_attach_by_id(struct device *dev,
					 unsigned int index);
struct device *genpd_dev_pm_attach_by_name(struct device *dev,
					   const char *name);
#else /* !CONFIG_PM_GENERIC_DOMAINS_OF */
static inline int of_genpd_add_provider_simple(struct device_node *np,
					struct generic_pm_domain *genpd)
{
	return -EOPNOTSUPP;
}

static inline int of_genpd_add_provider_onecell(struct device_node *np,
					struct genpd_onecell_data *data)
{
	return -EOPNOTSUPP;
}

static inline void of_genpd_del_provider(struct device_node *np) {}

static inline int of_genpd_add_device(const struct of_phandle_args *args,
				      struct device *dev)
{
	return -ENODEV;
}

static inline int of_genpd_add_subdomain(const struct of_phandle_args *parent_spec,
					 const struct of_phandle_args *subdomain_spec)
{
	return -ENODEV;
}

static inline int of_genpd_remove_subdomain(const struct of_phandle_args *parent_spec,
					    const struct of_phandle_args *subdomain_spec)
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
	return 0;
}

static inline struct device *genpd_dev_pm_attach_by_id(struct device *dev,
						       unsigned int index)
{
	return NULL;
}

static inline struct device *genpd_dev_pm_attach_by_name(struct device *dev,
							 const char *name)
{
	return NULL;
}

static inline
struct generic_pm_domain *of_genpd_remove_last(struct device_node *np)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif /* CONFIG_PM_GENERIC_DOMAINS_OF */

#ifdef CONFIG_PM
int dev_pm_domain_attach(struct device *dev, bool power_on);
struct device *dev_pm_domain_attach_by_id(struct device *dev,
					  unsigned int index);
struct device *dev_pm_domain_attach_by_name(struct device *dev,
					    const char *name);
int dev_pm_domain_attach_list(struct device *dev,
			      const struct dev_pm_domain_attach_data *data,
			      struct dev_pm_domain_list **list);
int devm_pm_domain_attach_list(struct device *dev,
			       const struct dev_pm_domain_attach_data *data,
			       struct dev_pm_domain_list **list);
void dev_pm_domain_detach(struct device *dev, bool power_off);
void dev_pm_domain_detach_list(struct dev_pm_domain_list *list);
int dev_pm_domain_start(struct device *dev);
void dev_pm_domain_set(struct device *dev, struct dev_pm_domain *pd);
int dev_pm_domain_set_performance_state(struct device *dev, unsigned int state);
#else
static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return 0;
}
static inline struct device *dev_pm_domain_attach_by_id(struct device *dev,
							unsigned int index)
{
	return NULL;
}
static inline struct device *dev_pm_domain_attach_by_name(struct device *dev,
							  const char *name)
{
	return NULL;
}
static inline int dev_pm_domain_attach_list(struct device *dev,
				const struct dev_pm_domain_attach_data *data,
				struct dev_pm_domain_list **list)
{
	return 0;
}

static inline int devm_pm_domain_attach_list(struct device *dev,
					     const struct dev_pm_domain_attach_data *data,
					     struct dev_pm_domain_list **list)
{
	return 0;
}

static inline void dev_pm_domain_detach(struct device *dev, bool power_off) {}
static inline void dev_pm_domain_detach_list(struct dev_pm_domain_list *list) {}
static inline int dev_pm_domain_start(struct device *dev)
{
	return 0;
}
static inline void dev_pm_domain_set(struct device *dev,
				     struct dev_pm_domain *pd) {}
static inline int dev_pm_domain_set_performance_state(struct device *dev,
						      unsigned int state)
{
	return 0;
}
#endif

#endif /* _LINUX_PM_DOMAIN_H */
