/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RESCTRL_H
#define _RESCTRL_H

#include <linux/cacheinfo.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/resctrl_types.h>

/* CLOSID, RMID value used by the default control group */
#define RESCTRL_RESERVED_CLOSID		0
#define RESCTRL_RESERVED_RMID		0

#define RESCTRL_PICK_ANY_CPU		-1

#ifdef CONFIG_PROC_CPU_RESCTRL

int proc_resctrl_show(struct seq_file *m,
		      struct pid_namespace *ns,
		      struct pid *pid,
		      struct task_struct *tsk);

#endif

/* max value for struct rdt_domain's mbps_val */
#define MBA_MAX_MBPS   U32_MAX

/* Walk all possible resources, with variants for only controls or monitors. */
#define for_each_rdt_resource(_r)						\
	for ((_r) = resctrl_arch_get_resource(0);				\
	     (_r) && (_r)->rid < RDT_NUM_RESOURCES;				\
	     (_r) = resctrl_arch_get_resource((_r)->rid + 1))

#define for_each_capable_rdt_resource(r)				      \
	for_each_rdt_resource((r))					      \
		if ((r)->alloc_capable || (r)->mon_capable)

#define for_each_alloc_capable_rdt_resource(r)				      \
	for_each_rdt_resource((r))					      \
		if ((r)->alloc_capable)

#define for_each_mon_capable_rdt_resource(r)				      \
	for_each_rdt_resource((r))					      \
		if ((r)->mon_capable)

/**
 * enum resctrl_conf_type - The type of configuration.
 * @CDP_NONE:	No prioritisation, both code and data are controlled or monitored.
 * @CDP_CODE:	Configuration applies to instruction fetches.
 * @CDP_DATA:	Configuration applies to reads and writes.
 */
enum resctrl_conf_type {
	CDP_NONE,
	CDP_CODE,
	CDP_DATA,
};

#define CDP_NUM_TYPES	(CDP_DATA + 1)

/*
 * struct pseudo_lock_region - pseudo-lock region information
 * @s:			Resctrl schema for the resource to which this
 *			pseudo-locked region belongs
 * @closid:		The closid that this pseudo-locked region uses
 * @d:			RDT domain to which this pseudo-locked region
 *			belongs
 * @cbm:		bitmask of the pseudo-locked region
 * @lock_thread_wq:	waitqueue used to wait on the pseudo-locking thread
 *			completion
 * @thread_done:	variable used by waitqueue to test if pseudo-locking
 *			thread completed
 * @cpu:		core associated with the cache on which the setup code
 *			will be run
 * @line_size:		size of the cache lines
 * @size:		size of pseudo-locked region in bytes
 * @kmem:		the kernel memory associated with pseudo-locked region
 * @minor:		minor number of character device associated with this
 *			region
 * @debugfs_dir:	pointer to this region's directory in the debugfs
 *			filesystem
 * @pm_reqs:		Power management QoS requests related to this region
 */
struct pseudo_lock_region {
	struct resctrl_schema	*s;
	u32			closid;
	struct rdt_ctrl_domain	*d;
	u32			cbm;
	wait_queue_head_t	lock_thread_wq;
	int			thread_done;
	int			cpu;
	unsigned int		line_size;
	unsigned int		size;
	void			*kmem;
	unsigned int		minor;
	struct dentry		*debugfs_dir;
	struct list_head	pm_reqs;
};

/**
 * struct resctrl_staged_config - parsed configuration to be applied
 * @new_ctrl:		new ctrl value to be loaded
 * @have_new_ctrl:	whether the user provided new_ctrl is valid
 */
struct resctrl_staged_config {
	u32			new_ctrl;
	bool			have_new_ctrl;
};

enum resctrl_domain_type {
	RESCTRL_CTRL_DOMAIN,
	RESCTRL_MON_DOMAIN,
};

/**
 * struct rdt_domain_hdr - common header for different domain types
 * @list:		all instances of this resource
 * @id:			unique id for this instance
 * @type:		type of this instance
 * @cpu_mask:		which CPUs share this resource
 */
struct rdt_domain_hdr {
	struct list_head		list;
	int				id;
	enum resctrl_domain_type	type;
	struct cpumask			cpu_mask;
};

/**
 * struct rdt_ctrl_domain - group of CPUs sharing a resctrl control resource
 * @hdr:		common header for different domain types
 * @plr:		pseudo-locked region (if any) associated with domain
 * @staged_config:	parsed configuration to be applied
 * @mbps_val:		When mba_sc is enabled, this holds the array of user
 *			specified control values for mba_sc in MBps, indexed
 *			by closid
 */
struct rdt_ctrl_domain {
	struct rdt_domain_hdr		hdr;
	struct pseudo_lock_region	*plr;
	struct resctrl_staged_config	staged_config[CDP_NUM_TYPES];
	u32				*mbps_val;
};

/**
 * struct rdt_mon_domain - group of CPUs sharing a resctrl monitor resource
 * @hdr:		common header for different domain types
 * @ci:			cache info for this domain
 * @rmid_busy_llc:	bitmap of which limbo RMIDs are above threshold
 * @mbm_total:		saved state for MBM total bandwidth
 * @mbm_local:		saved state for MBM local bandwidth
 * @mbm_over:		worker to periodically read MBM h/w counters
 * @cqm_limbo:		worker to periodically read CQM h/w counters
 * @mbm_work_cpu:	worker CPU for MBM h/w counters
 * @cqm_work_cpu:	worker CPU for CQM h/w counters
 */
struct rdt_mon_domain {
	struct rdt_domain_hdr		hdr;
	struct cacheinfo		*ci;
	unsigned long			*rmid_busy_llc;
	struct mbm_state		*mbm_total;
	struct mbm_state		*mbm_local;
	struct delayed_work		mbm_over;
	struct delayed_work		cqm_limbo;
	int				mbm_work_cpu;
	int				cqm_work_cpu;
};

/**
 * struct resctrl_cache - Cache allocation related data
 * @cbm_len:		Length of the cache bit mask
 * @min_cbm_bits:	Minimum number of consecutive bits to be set.
 *			The value 0 means the architecture can support
 *			zero CBM.
 * @shareable_bits:	Bitmask of shareable resource with other
 *			executing entities
 * @arch_has_sparse_bitmasks:	True if a bitmask like f00f is valid.
 * @arch_has_per_cpu_cfg:	True if QOS_CFG register for this cache
 *				level has CPU scope.
 */
struct resctrl_cache {
	unsigned int	cbm_len;
	unsigned int	min_cbm_bits;
	unsigned int	shareable_bits;
	bool		arch_has_sparse_bitmasks;
	bool		arch_has_per_cpu_cfg;
};

/**
 * enum membw_throttle_mode - System's memory bandwidth throttling mode
 * @THREAD_THROTTLE_UNDEFINED:	Not relevant to the system
 * @THREAD_THROTTLE_MAX:	Memory bandwidth is throttled at the core
 *				always using smallest bandwidth percentage
 *				assigned to threads, aka "max throttling"
 * @THREAD_THROTTLE_PER_THREAD:	Memory bandwidth is throttled at the thread
 */
enum membw_throttle_mode {
	THREAD_THROTTLE_UNDEFINED = 0,
	THREAD_THROTTLE_MAX,
	THREAD_THROTTLE_PER_THREAD,
};

/**
 * struct resctrl_membw - Memory bandwidth allocation related data
 * @min_bw:		Minimum memory bandwidth percentage user can request
 * @max_bw:		Maximum memory bandwidth value, used as the reset value
 * @bw_gran:		Granularity at which the memory bandwidth is allocated
 * @delay_linear:	True if memory B/W delay is in linear scale
 * @arch_needs_linear:	True if we can't configure non-linear resources
 * @throttle_mode:	Bandwidth throttling mode when threads request
 *			different memory bandwidths
 * @mba_sc:		True if MBA software controller(mba_sc) is enabled
 * @mb_map:		Mapping of memory B/W percentage to memory B/W delay
 */
struct resctrl_membw {
	u32				min_bw;
	u32				max_bw;
	u32				bw_gran;
	u32				delay_linear;
	bool				arch_needs_linear;
	enum membw_throttle_mode	throttle_mode;
	bool				mba_sc;
	u32				*mb_map;
};

struct resctrl_schema;

enum resctrl_scope {
	RESCTRL_L2_CACHE = 2,
	RESCTRL_L3_CACHE = 3,
	RESCTRL_L3_NODE,
};

/**
 * enum resctrl_schema_fmt - The format user-space provides for a schema.
 * @RESCTRL_SCHEMA_BITMAP:	The schema is a bitmap in hex.
 * @RESCTRL_SCHEMA_RANGE:	The schema is a decimal number.
 */
enum resctrl_schema_fmt {
	RESCTRL_SCHEMA_BITMAP,
	RESCTRL_SCHEMA_RANGE,
};

/**
 * struct rdt_resource - attributes of a resctrl resource
 * @rid:		The index of the resource
 * @alloc_capable:	Is allocation available on this machine
 * @mon_capable:	Is monitor feature available on this machine
 * @num_rmid:		Number of RMIDs available
 * @ctrl_scope:		Scope of this resource for control functions
 * @mon_scope:		Scope of this resource for monitor functions
 * @cache:		Cache allocation related data
 * @membw:		If the component has bandwidth controls, their properties.
 * @ctrl_domains:	RCU list of all control domains for this resource
 * @mon_domains:	RCU list of all monitor domains for this resource
 * @name:		Name to use in "schemata" file.
 * @schema_fmt:		Which format string and parser is used for this schema.
 * @evt_list:		List of monitoring events
 * @mbm_cfg_mask:	Bandwidth sources that can be tracked when bandwidth
 *			monitoring events can be configured.
 * @cdp_capable:	Is the CDP feature available on this resource
 */
struct rdt_resource {
	int			rid;
	bool			alloc_capable;
	bool			mon_capable;
	int			num_rmid;
	enum resctrl_scope	ctrl_scope;
	enum resctrl_scope	mon_scope;
	struct resctrl_cache	cache;
	struct resctrl_membw	membw;
	struct list_head	ctrl_domains;
	struct list_head	mon_domains;
	char			*name;
	enum resctrl_schema_fmt	schema_fmt;
	struct list_head	evt_list;
	unsigned int		mbm_cfg_mask;
	bool			cdp_capable;
};

/*
 * Get the resource that exists at this level. If the level is not supported
 * a dummy/not-capable resource can be returned. Levels >= RDT_NUM_RESOURCES
 * will return NULL.
 */
struct rdt_resource *resctrl_arch_get_resource(enum resctrl_res_level l);

/**
 * struct resctrl_schema - configuration abilities of a resource presented to
 *			   user-space
 * @list:	Member of resctrl_schema_all.
 * @name:	The name to use in the "schemata" file.
 * @fmt_str:	Format string to show domain value.
 * @conf_type:	Whether this schema is specific to code/data.
 * @res:	The resource structure exported by the architecture to describe
 *		the hardware that is configured by this schema.
 * @num_closid:	The number of closid that can be used with this schema. When
 *		features like CDP are enabled, this will be lower than the
 *		hardware supports for the resource.
 */
struct resctrl_schema {
	struct list_head		list;
	char				name[8];
	const char			*fmt_str;
	enum resctrl_conf_type		conf_type;
	struct rdt_resource		*res;
	u32				num_closid;
};

struct resctrl_cpu_defaults {
	u32 closid;
	u32 rmid;
};

struct resctrl_mon_config_info {
	struct rdt_resource	*r;
	struct rdt_mon_domain	*d;
	u32			evtid;
	u32			mon_config;
};

/**
 * resctrl_arch_sync_cpu_closid_rmid() - Refresh this CPU's CLOSID and RMID.
 *					 Call via IPI.
 * @info:	If non-NULL, a pointer to a struct resctrl_cpu_defaults
 *		specifying the new CLOSID and RMID for tasks in the default
 *		resctrl ctrl and mon group when running on this CPU.  If NULL,
 *		this CPU is not re-assigned to a different default group.
 *
 * Propagates reassignment of CPUs and/or tasks to different resctrl groups
 * when requested by the resctrl core code.
 *
 * This function records the per-cpu defaults specified by @info (if any),
 * and then reconfigures the CPU's hardware CLOSID and RMID for subsequent
 * execution based on @current, in the same way as during a task switch.
 */
void resctrl_arch_sync_cpu_closid_rmid(void *info);

/**
 * resctrl_get_default_ctrl() - Return the default control value for this
 *                              resource.
 * @r:		The resource whose default control type is queried.
 */
static inline u32 resctrl_get_default_ctrl(struct rdt_resource *r)
{
	switch (r->schema_fmt) {
	case RESCTRL_SCHEMA_BITMAP:
		return BIT_MASK(r->cache.cbm_len) - 1;
	case RESCTRL_SCHEMA_RANGE:
		return r->membw.max_bw;
	}

	return WARN_ON_ONCE(1);
}

/* The number of closid supported by this resource regardless of CDP */
u32 resctrl_arch_get_num_closid(struct rdt_resource *r);
u32 resctrl_arch_system_num_rmid_idx(void);
int resctrl_arch_update_domains(struct rdt_resource *r, u32 closid);

__init bool resctrl_arch_is_evt_configurable(enum resctrl_event_id evt);

/**
 * resctrl_arch_mon_event_config_write() - Write the config for an event.
 * @config_info: struct resctrl_mon_config_info describing the resource, domain
 *		 and event.
 *
 * Reads resource, domain and eventid from @config_info and writes the
 * event config_info->mon_config into hardware.
 *
 * Called via IPI to reach a CPU that is a member of the specified domain.
 */
void resctrl_arch_mon_event_config_write(void *config_info);

/**
 * resctrl_arch_mon_event_config_read() - Read the config for an event.
 * @config_info: struct resctrl_mon_config_info describing the resource, domain
 *		 and event.
 *
 * Reads resource, domain and eventid from @config_info and reads the
 * hardware config value into config_info->mon_config.
 *
 * Called via IPI to reach a CPU that is a member of the specified domain.
 */
void resctrl_arch_mon_event_config_read(void *config_info);

/* For use by arch code to remap resctrl's smaller CDP CLOSID range */
static inline u32 resctrl_get_config_index(u32 closid,
					   enum resctrl_conf_type type)
{
	switch (type) {
	default:
	case CDP_NONE:
		return closid;
	case CDP_CODE:
		return closid * 2 + 1;
	case CDP_DATA:
		return closid * 2;
	}
}

/*
 * Update the ctrl_val and apply this config right now.
 * Must be called on one of the domain's CPUs.
 */
int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val);

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type type);
int resctrl_online_ctrl_domain(struct rdt_resource *r, struct rdt_ctrl_domain *d);
int resctrl_online_mon_domain(struct rdt_resource *r, struct rdt_mon_domain *d);
void resctrl_offline_ctrl_domain(struct rdt_resource *r, struct rdt_ctrl_domain *d);
void resctrl_offline_mon_domain(struct rdt_resource *r, struct rdt_mon_domain *d);
void resctrl_online_cpu(unsigned int cpu);
void resctrl_offline_cpu(unsigned int cpu);

/**
 * resctrl_arch_rmid_read() - Read the eventid counter corresponding to rmid
 *			      for this resource and domain.
 * @r:			resource that the counter should be read from.
 * @d:			domain that the counter should be read from.
 * @closid:		closid that matches the rmid. Depending on the architecture, the
 *			counter may match traffic of both @closid and @rmid, or @rmid
 *			only.
 * @rmid:		rmid of the counter to read.
 * @eventid:		eventid to read, e.g. L3 occupancy.
 * @val:		result of the counter read in bytes.
 * @arch_mon_ctx:	An architecture specific value from
 *			resctrl_arch_mon_ctx_alloc(), for MPAM this identifies
 *			the hardware monitor allocated for this read request.
 *
 * Some architectures need to sleep when first programming some of the counters.
 * (specifically: arm64's MPAM cache occupancy counters can return 'not ready'
 *  for a short period of time). Call from a non-migrateable process context on
 * a CPU that belongs to domain @d. e.g. use smp_call_on_cpu() or
 * schedule_work_on(). This function can be called with interrupts masked,
 * e.g. using smp_call_function_any(), but may consistently return an error.
 *
 * Return:
 * 0 on success, or -EIO, -EINVAL etc on error.
 */
int resctrl_arch_rmid_read(struct rdt_resource *r, struct rdt_mon_domain *d,
			   u32 closid, u32 rmid, enum resctrl_event_id eventid,
			   u64 *val, void *arch_mon_ctx);

/**
 * resctrl_arch_rmid_read_context_check()  - warn about invalid contexts
 *
 * When built with CONFIG_DEBUG_ATOMIC_SLEEP generate a warning when
 * resctrl_arch_rmid_read() is called with preemption disabled.
 *
 * The contract with resctrl_arch_rmid_read() is that if interrupts
 * are unmasked, it can sleep. This allows NOHZ_FULL systems to use an
 * IPI, (and fail if the call needed to sleep), while most of the time
 * the work is scheduled, allowing the call to sleep.
 */
static inline void resctrl_arch_rmid_read_context_check(void)
{
	if (!irqs_disabled())
		might_sleep();
}

/**
 * resctrl_find_domain() - Search for a domain id in a resource domain list.
 * @h:		The domain list to search.
 * @id:		The domain id to search for.
 * @pos:	A pointer to position in the list id should be inserted.
 *
 * Search the domain list to find the domain id. If the domain id is
 * found, return the domain. NULL otherwise.  If the domain id is not
 * found (and NULL returned) then the first domain with id bigger than
 * the input id can be returned to the caller via @pos.
 */
struct rdt_domain_hdr *resctrl_find_domain(struct list_head *h, int id,
					   struct list_head **pos);

/**
 * resctrl_arch_reset_rmid() - Reset any private state associated with rmid
 *			       and eventid.
 * @r:		The domain's resource.
 * @d:		The rmid's domain.
 * @closid:	closid that matches the rmid. Depending on the architecture, the
 *		counter may match traffic of both @closid and @rmid, or @rmid only.
 * @rmid:	The rmid whose counter values should be reset.
 * @eventid:	The eventid whose counter values should be reset.
 *
 * This can be called from any CPU.
 */
void resctrl_arch_reset_rmid(struct rdt_resource *r, struct rdt_mon_domain *d,
			     u32 closid, u32 rmid,
			     enum resctrl_event_id eventid);

/**
 * resctrl_arch_reset_rmid_all() - Reset all private state associated with
 *				   all rmids and eventids.
 * @r:		The resctrl resource.
 * @d:		The domain for which all architectural counter state will
 *		be cleared.
 *
 * This can be called from any CPU.
 */
void resctrl_arch_reset_rmid_all(struct rdt_resource *r, struct rdt_mon_domain *d);

/**
 * resctrl_arch_reset_all_ctrls() - Reset the control for each CLOSID to its
 *				    default.
 * @r:		The resctrl resource to reset.
 *
 * This can be called from any CPU.
 */
void resctrl_arch_reset_all_ctrls(struct rdt_resource *r);

extern unsigned int resctrl_rmid_realloc_threshold;
extern unsigned int resctrl_rmid_realloc_limit;

int __init resctrl_init(void);
void __exit resctrl_exit(void);

#endif /* _RESCTRL_H */
