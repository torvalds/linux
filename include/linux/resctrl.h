/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RESCTRL_H
#define _RESCTRL_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pid.h>

#ifdef CONFIG_PROC_CPU_RESCTRL

int proc_resctrl_show(struct seq_file *m,
		      struct pid_namespace *ns,
		      struct pid *pid,
		      struct task_struct *tsk);

#endif

/* max value for struct rdt_domain's mbps_val */
#define MBA_MAX_MBPS   U32_MAX

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
 * Event IDs, the values match those used to program IA32_QM_EVTSEL before
 * reading IA32_QM_CTR on RDT systems.
 */
enum resctrl_event_id {
	QOS_L3_OCCUP_EVENT_ID		= 0x01,
	QOS_L3_MBM_TOTAL_EVENT_ID	= 0x02,
	QOS_L3_MBM_LOCAL_EVENT_ID	= 0x03,
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

/**
 * struct rdt_domain - group of CPUs sharing a resctrl resource
 * @list:		all instances of this resource
 * @id:			unique id for this instance
 * @cpu_mask:		which CPUs share this resource
 * @rmid_busy_llc:	bitmap of which limbo RMIDs are above threshold
 * @mbm_total:		saved state for MBM total bandwidth
 * @mbm_local:		saved state for MBM local bandwidth
 * @mbm_over:		worker to periodically read MBM h/w counters
 * @cqm_limbo:		worker to periodically read CQM h/w counters
 * @mbm_work_cpu:	worker CPU for MBM h/w counters
 * @cqm_work_cpu:	worker CPU for CQM h/w counters
 * @plr:		pseudo-locked region (if any) associated with domain
 * @staged_config:	parsed configuration to be applied
 * @mbps_val:		When mba_sc is enabled, this holds the array of user
 *			specified control values for mba_sc in MBps, indexed
 *			by closid
 */
struct rdt_domain {
	struct list_head		list;
	int				id;
	struct cpumask			cpu_mask;
	unsigned long			*rmid_busy_llc;
	struct mbm_state		*mbm_total;
	struct mbm_state		*mbm_local;
	struct delayed_work		mbm_over;
	struct delayed_work		cqm_limbo;
	int				mbm_work_cpu;
	int				cqm_work_cpu;
	struct pseudo_lock_region	*plr;
	struct resctrl_staged_config	staged_config[CDP_NUM_TYPES];
	u32				*mbps_val;
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
	u32				bw_gran;
	u32				delay_linear;
	bool				arch_needs_linear;
	enum membw_throttle_mode	throttle_mode;
	bool				mba_sc;
	u32				*mb_map;
};

struct rdt_parse_data;
struct resctrl_schema;

/**
 * struct rdt_resource - attributes of a resctrl resource
 * @rid:		The index of the resource
 * @alloc_capable:	Is allocation available on this machine
 * @mon_capable:	Is monitor feature available on this machine
 * @num_rmid:		Number of RMIDs available
 * @cache_level:	Which cache level defines scope of this resource
 * @cache:		Cache allocation related data
 * @membw:		If the component has bandwidth controls, their properties.
 * @domains:		All domains for this resource
 * @name:		Name to use in "schemata" file.
 * @data_width:		Character width of data when displaying
 * @default_ctrl:	Specifies default cache cbm or memory B/W percent.
 * @format_str:		Per resource format string to show domain value
 * @parse_ctrlval:	Per resource function pointer to parse control values
 * @evt_list:		List of monitoring events
 * @fflags:		flags to choose base and info files
 * @cdp_capable:	Is the CDP feature available on this resource
 */
struct rdt_resource {
	int			rid;
	bool			alloc_capable;
	bool			mon_capable;
	int			num_rmid;
	int			cache_level;
	struct resctrl_cache	cache;
	struct resctrl_membw	membw;
	struct list_head	domains;
	char			*name;
	int			data_width;
	u32			default_ctrl;
	const char		*format_str;
	int			(*parse_ctrlval)(struct rdt_parse_data *data,
						 struct resctrl_schema *s,
						 struct rdt_domain *d);
	struct list_head	evt_list;
	unsigned long		fflags;
	bool			cdp_capable;
};

/**
 * struct resctrl_schema - configuration abilities of a resource presented to
 *			   user-space
 * @list:	Member of resctrl_schema_all.
 * @name:	The name to use in the "schemata" file.
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
	enum resctrl_conf_type		conf_type;
	struct rdt_resource		*res;
	u32				num_closid;
};

/* The number of closid supported by this resource regardless of CDP */
u32 resctrl_arch_get_num_closid(struct rdt_resource *r);
int resctrl_arch_update_domains(struct rdt_resource *r, u32 closid);

/*
 * Update the ctrl_val and apply this config right now.
 * Must be called on one of the domain's CPUs.
 */
int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val);

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_domain *d,
			    u32 closid, enum resctrl_conf_type type);
int resctrl_online_domain(struct rdt_resource *r, struct rdt_domain *d);
void resctrl_offline_domain(struct rdt_resource *r, struct rdt_domain *d);

/**
 * resctrl_arch_rmid_read() - Read the eventid counter corresponding to rmid
 *			      for this resource and domain.
 * @r:			resource that the counter should be read from.
 * @d:			domain that the counter should be read from.
 * @rmid:		rmid of the counter to read.
 * @eventid:		eventid to read, e.g. L3 occupancy.
 * @val:		result of the counter read in bytes.
 *
 * Call from process context on a CPU that belongs to domain @d.
 *
 * Return:
 * 0 on success, or -EIO, -EINVAL etc on error.
 */
int resctrl_arch_rmid_read(struct rdt_resource *r, struct rdt_domain *d,
			   u32 rmid, enum resctrl_event_id eventid, u64 *val);

/**
 * resctrl_arch_reset_rmid() - Reset any private state associated with rmid
 *			       and eventid.
 * @r:		The domain's resource.
 * @d:		The rmid's domain.
 * @rmid:	The rmid whose counter values should be reset.
 * @eventid:	The eventid whose counter values should be reset.
 *
 * This can be called from any CPU.
 */
void resctrl_arch_reset_rmid(struct rdt_resource *r, struct rdt_domain *d,
			     u32 rmid, enum resctrl_event_id eventid);

/**
 * resctrl_arch_reset_rmid_all() - Reset all private state associated with
 *				   all rmids and eventids.
 * @r:		The resctrl resource.
 * @d:		The domain for which all architectural counter state will
 *		be cleared.
 *
 * This can be called from any CPU.
 */
void resctrl_arch_reset_rmid_all(struct rdt_resource *r, struct rdt_domain *d);

extern unsigned int resctrl_rmid_realloc_threshold;
extern unsigned int resctrl_rmid_realloc_limit;

#endif /* _RESCTRL_H */
