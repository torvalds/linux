/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ENGINE_TYPES__
#define __INTEL_ENGINE_TYPES__

#include <linux/average.h>
#include <linux/hashtable.h>
#include <linux/irq_work.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/rbtree.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "i915_gem.h"
#include "i915_pmu.h"
#include "i915_priolist_types.h"
#include "i915_selftest.h"
#include "intel_sseu.h"
#include "intel_timeline_types.h"
#include "intel_uncore.h"
#include "intel_wakeref.h"
#include "intel_workarounds_types.h"

/* HW Engine class + instance */
#define RENDER_CLASS		0
#define VIDEO_DECODE_CLASS	1
#define VIDEO_ENHANCEMENT_CLASS	2
#define COPY_ENGINE_CLASS	3
#define OTHER_CLASS		4
#define COMPUTE_CLASS		5
#define MAX_ENGINE_CLASS	5
#define MAX_ENGINE_INSTANCE	8

#define I915_MAX_SLICES	3
#define I915_MAX_SUBSLICES 8

#define I915_CMD_HASH_ORDER 9

struct dma_fence;
struct drm_i915_gem_object;
struct drm_i915_reg_table;
struct i915_gem_context;
struct i915_request;
struct i915_sched_attr;
struct i915_sched_engine;
struct intel_gt;
struct intel_ring;
struct intel_uncore;
struct intel_breadcrumbs;
struct intel_engine_cs;
struct i915_perf_group;

typedef u32 intel_engine_mask_t;
#define ALL_ENGINES ((intel_engine_mask_t)~0ul)
#define VIRTUAL_ENGINES BIT(BITS_PER_TYPE(intel_engine_mask_t) - 1)

struct intel_hw_status_page {
	struct list_head timelines;
	struct i915_vma *vma;
	u32 *addr;
};

struct intel_instdone {
	u32 instdone;
	/* The following exist only in the RCS engine */
	u32 slice_common;
	u32 slice_common_extra[2];
	u32 sampler[GEN_MAX_GSLICES][I915_MAX_SUBSLICES];
	u32 row[GEN_MAX_GSLICES][I915_MAX_SUBSLICES];

	/* Added in XeHPG */
	u32 geom_svg[GEN_MAX_GSLICES][I915_MAX_SUBSLICES];
};

/*
 * we use a single page to load ctx workarounds so all of these
 * values are referred in terms of dwords
 *
 * struct i915_wa_ctx_bb:
 *  offset: specifies batch starting position, also helpful in case
 *    if we want to have multiple batches at different offsets based on
 *    some criteria. It is not a requirement at the moment but provides
 *    an option for future use.
 *  size: size of the batch in DWORDS
 */
struct i915_ctx_workarounds {
	struct i915_wa_ctx_bb {
		u32 offset;
		u32 size;
	} indirect_ctx, per_ctx;
	struct i915_vma *vma;
};

#define I915_MAX_VCS	8
#define I915_MAX_VECS	4
#define I915_MAX_SFC	(I915_MAX_VCS / 2)
#define I915_MAX_CCS	4
#define I915_MAX_RCS	1
#define I915_MAX_BCS	9

/*
 * Engine IDs definitions.
 * Keep instances of the same type engine together.
 */
enum intel_engine_id {
	RCS0 = 0,
	BCS0,
	BCS1,
	BCS2,
	BCS3,
	BCS4,
	BCS5,
	BCS6,
	BCS7,
	BCS8,
#define _BCS(n) (BCS0 + (n))
	VCS0,
	VCS1,
	VCS2,
	VCS3,
	VCS4,
	VCS5,
	VCS6,
	VCS7,
#define _VCS(n) (VCS0 + (n))
	VECS0,
	VECS1,
	VECS2,
	VECS3,
#define _VECS(n) (VECS0 + (n))
	CCS0,
	CCS1,
	CCS2,
	CCS3,
#define _CCS(n) (CCS0 + (n))
	GSC0,
	I915_NUM_ENGINES
#define INVALID_ENGINE ((enum intel_engine_id)-1)
};

/* A simple estimator for the round-trip latency of an engine */
DECLARE_EWMA(_engine_latency, 6, 4)

struct st_preempt_hang {
	struct completion completion;
	unsigned int count;
};

/**
 * struct intel_engine_execlists - execlist submission queue and port state
 *
 * The struct intel_engine_execlists represents the combined logical state of
 * driver and the hardware state for execlist mode of submission.
 */
struct intel_engine_execlists {
	/**
	 * @timer: kick the current context if its timeslice expires
	 */
	struct timeout timer;

	/**
	 * @preempt: reset the current context if it fails to give way
	 */
	struct timeout preempt;

	/**
	 * @preempt_target: active request at the time of the preemption request
	 *
	 * We force a preemption to occur if the pending contexts have not
	 * been promoted to active upon receipt of the CS ack event within
	 * the timeout. This timeout maybe chosen based on the target,
	 * using a very short timeout if the context is no longer schedulable.
	 * That short timeout may not be applicable to other contexts, so
	 * if a context switch should happen within before the preemption
	 * timeout, we may shoot early at an innocent context. To prevent this,
	 * we record which context was active at the time of the preemption
	 * request and only reset that context upon the timeout.
	 */
	const struct i915_request *preempt_target;

	/**
	 * @ccid: identifier for contexts submitted to this engine
	 */
	u32 ccid;

	/**
	 * @yield: CCID at the time of the last semaphore-wait interrupt.
	 *
	 * Instead of leaving a semaphore busy-spinning on an engine, we would
	 * like to switch to another ready context, i.e. yielding the semaphore
	 * timeslice.
	 */
	u32 yield;

	/**
	 * @error_interrupt: CS Master EIR
	 *
	 * The CS generates an interrupt when it detects an error. We capture
	 * the first error interrupt, record the EIR and schedule the tasklet.
	 * In the tasklet, we process the pending CS events to ensure we have
	 * the guilty request, and then reset the engine.
	 *
	 * Low 16b are used by HW, with the upper 16b used as the enabling mask.
	 * Reserve the upper 16b for tracking internal errors.
	 */
	u32 error_interrupt;
#define ERROR_CSB	BIT(31)
#define ERROR_PREEMPT	BIT(30)

	/**
	 * @reset_ccid: Active CCID [EXECLISTS_STATUS_HI] at the time of reset
	 */
	u32 reset_ccid;

	/**
	 * @submit_reg: gen-specific execlist submission register
	 * set to the ExecList Submission Port (elsp) register pre-Gen11 and to
	 * the ExecList Submission Queue Contents register array for Gen11+
	 */
	u32 __iomem *submit_reg;

	/**
	 * @ctrl_reg: the enhanced execlists control register, used to load the
	 * submit queue on the HW and to request preemptions to idle
	 */
	u32 __iomem *ctrl_reg;

#define EXECLIST_MAX_PORTS 2
	/**
	 * @active: the currently known context executing on HW
	 */
	struct i915_request * const *active;
	/**
	 * @inflight: the set of contexts submitted and acknowleged by HW
	 *
	 * The set of inflight contexts is managed by reading CS events
	 * from the HW. On a context-switch event (not preemption), we
	 * know the HW has transitioned from port0 to port1, and we
	 * advance our inflight/active tracking accordingly.
	 */
	struct i915_request *inflight[EXECLIST_MAX_PORTS + 1 /* sentinel */];
	/**
	 * @pending: the next set of contexts submitted to ELSP
	 *
	 * We store the array of contexts that we submit to HW (via ELSP) and
	 * promote them to the inflight array once HW has signaled the
	 * preemption or idle-to-active event.
	 */
	struct i915_request *pending[EXECLIST_MAX_PORTS + 1];

	/**
	 * @port_mask: number of execlist ports - 1
	 */
	unsigned int port_mask;

	/**
	 * @virtual: Queue of requets on a virtual engine, sorted by priority.
	 * Each RB entry is a struct i915_priolist containing a list of requests
	 * of the same priority.
	 */
	struct rb_root_cached virtual;

	/**
	 * @csb_write: control register for Context Switch buffer
	 *
	 * Note this register may be either mmio or HWSP shadow.
	 */
	u32 *csb_write;

	/**
	 * @csb_status: status array for Context Switch buffer
	 *
	 * Note these register may be either mmio or HWSP shadow.
	 */
	u64 *csb_status;

	/**
	 * @csb_size: context status buffer FIFO size
	 */
	u8 csb_size;

	/**
	 * @csb_head: context status buffer head
	 */
	u8 csb_head;

	/* private: selftest */
	I915_SELFTEST_DECLARE(struct st_preempt_hang preempt_hang;)
};

#define INTEL_ENGINE_CS_MAX_NAME 8

struct intel_engine_execlists_stats {
	/**
	 * @active: Number of contexts currently scheduled in.
	 */
	unsigned int active;

	/**
	 * @lock: Lock protecting the below fields.
	 */
	seqcount_t lock;

	/**
	 * @total: Total time this engine was busy.
	 *
	 * Accumulated time not counting the most recent block in cases where
	 * engine is currently busy (active > 0).
	 */
	ktime_t total;

	/**
	 * @start: Timestamp of the last idle to active transition.
	 *
	 * Idle is defined as active == 0, active is active > 0.
	 */
	ktime_t start;
};

struct intel_engine_guc_stats {
	/**
	 * @running: Active state of the engine when busyness was last sampled.
	 */
	bool running;

	/**
	 * @prev_total: Previous value of total runtime clock cycles.
	 */
	u32 prev_total;

	/**
	 * @total_gt_clks: Total gt clock cycles this engine was busy.
	 */
	u64 total_gt_clks;

	/**
	 * @start_gt_clk: GT clock time of last idle to active transition.
	 */
	u64 start_gt_clk;

	/**
	 * @total: The last value of total returned
	 */
	u64 total;
};

union intel_engine_tlb_inv_reg {
	i915_reg_t	reg;
	i915_mcr_reg_t	mcr_reg;
};

struct intel_engine_tlb_inv {
	bool mcr;
	union intel_engine_tlb_inv_reg reg;
	u32 request;
	u32 done;
};

struct intel_engine_cs {
	struct drm_i915_private *i915;
	struct intel_gt *gt;
	struct intel_uncore *uncore;
	char name[INTEL_ENGINE_CS_MAX_NAME];

	enum intel_engine_id id;
	enum intel_engine_id legacy_idx;

	unsigned int guc_id;

	intel_engine_mask_t mask;
	u32 reset_domain;
	/**
	 * @logical_mask: logical mask of engine, reported to user space via
	 * query IOCTL and used to communicate with the GuC in logical space.
	 * The logical instance of a physical engine can change based on product
	 * and fusing.
	 */
	intel_engine_mask_t logical_mask;

	u8 class;
	u8 instance;

	u16 uabi_class;
	u16 uabi_instance;

	u32 uabi_capabilities;
	u32 context_size;
	u32 mmio_base;

	struct intel_engine_tlb_inv tlb_inv;

	/*
	 * Some w/a require forcewake to be held (which prevents RC6) while
	 * a particular engine is active. If so, we set fw_domain to which
	 * domains need to be held for the duration of request activity,
	 * and 0 if none. We try to limit the duration of the hold as much
	 * as possible.
	 */
	enum forcewake_domains fw_domain;
	unsigned int fw_active;

	unsigned long context_tag;

	/*
	 * The type evolves during initialization, see related comment for
	 * struct drm_i915_private's uabi_engines member.
	 */
	union {
		struct llist_node uabi_llist;
		struct list_head uabi_list;
		struct rb_node uabi_node;
	};

	struct intel_sseu sseu;

	struct i915_sched_engine *sched_engine;

	/* keep a request in reserve for a [pm] barrier under oom */
	struct i915_request *request_pool;

	struct intel_context *hung_ce;

	struct llist_head barrier_tasks;

	struct intel_context *kernel_context; /* pinned */
	struct intel_context *bind_context; /* pinned, only for BCS0 */
	/* mark the bind context's availability status */
	bool bind_context_ready;

	/**
	 * pinned_contexts_list: List of pinned contexts. This list is only
	 * assumed to be manipulated during driver load- or unload time and
	 * does therefore not have any additional protection.
	 */
	struct list_head pinned_contexts_list;

	intel_engine_mask_t saturated; /* submitting semaphores too late? */

	struct {
		struct delayed_work work;
		struct i915_request *systole;
		unsigned long blocked;
	} heartbeat;

	unsigned long serial;

	unsigned long wakeref_serial;
	intel_wakeref_t wakeref_track;
	struct intel_wakeref wakeref;

#ifdef __linux__
	struct file *default_state;
#else
	struct uvm_object *default_state;
#endif

	struct {
		struct intel_ring *ring;
		struct intel_timeline *timeline;
	} legacy;

	/*
	 * We track the average duration of the idle pulse on parking the
	 * engine to keep an estimate of the how the fast the engine is
	 * under ideal conditions.
	 */
	struct ewma__engine_latency latency;

	/* Keep track of all the seqno used, a trail of breadcrumbs */
	struct intel_breadcrumbs *breadcrumbs;

	struct intel_engine_pmu {
		/**
		 * @enable: Bitmask of enable sample events on this engine.
		 *
		 * Bits correspond to sample event types, for instance
		 * I915_SAMPLE_QUEUED is bit 0 etc.
		 */
		u32 enable;
		/**
		 * @enable_count: Reference count for the enabled samplers.
		 *
		 * Index number corresponds to @enum drm_i915_pmu_engine_sample.
		 */
		unsigned int enable_count[I915_ENGINE_SAMPLE_COUNT];
		/**
		 * @sample: Counter values for sampling events.
		 *
		 * Our internal timer stores the current counters in this field.
		 *
		 * Index number corresponds to @enum drm_i915_pmu_engine_sample.
		 */
		struct i915_pmu_sample sample[I915_ENGINE_SAMPLE_COUNT];
	} pmu;

	struct intel_hw_status_page status_page;
	struct i915_ctx_workarounds wa_ctx;
	struct i915_wa_list ctx_wa_list;
	struct i915_wa_list wa_list;
	struct i915_wa_list whitelist;

	u32             irq_keep_mask; /* always keep these interrupts */
	u32		irq_enable_mask; /* bitmask to enable ring interrupt */
	void		(*irq_enable)(struct intel_engine_cs *engine);
	void		(*irq_disable)(struct intel_engine_cs *engine);
	void		(*irq_handler)(struct intel_engine_cs *engine, u16 iir);

	void		(*sanitize)(struct intel_engine_cs *engine);
	int		(*resume)(struct intel_engine_cs *engine);

	struct {
		void (*prepare)(struct intel_engine_cs *engine);

		void (*rewind)(struct intel_engine_cs *engine, bool stalled);
		void (*cancel)(struct intel_engine_cs *engine);

		void (*finish)(struct intel_engine_cs *engine);
	} reset;

	void		(*park)(struct intel_engine_cs *engine);
	void		(*unpark)(struct intel_engine_cs *engine);

	void		(*bump_serial)(struct intel_engine_cs *engine);

	void		(*set_default_submission)(struct intel_engine_cs *engine);

	const struct intel_context_ops *cops;

	int		(*request_alloc)(struct i915_request *rq);

	int		(*emit_flush)(struct i915_request *request, u32 mode);
#define EMIT_INVALIDATE	BIT(0)
#define EMIT_FLUSH	BIT(1)
#define EMIT_BARRIER	(EMIT_INVALIDATE | EMIT_FLUSH)
	int		(*emit_bb_start)(struct i915_request *rq,
					 u64 offset, u32 length,
					 unsigned int dispatch_flags);
#define I915_DISPATCH_SECURE BIT(0)
#define I915_DISPATCH_PINNED BIT(1)
	int		 (*emit_init_breadcrumb)(struct i915_request *rq);
	u32		*(*emit_fini_breadcrumb)(struct i915_request *rq,
						 u32 *cs);
	unsigned int	emit_fini_breadcrumb_dw;

	/* Pass the request to the hardware queue (e.g. directly into
	 * the legacy ringbuffer or to the end of an execlist).
	 *
	 * This is called from an atomic context with irqs disabled; must
	 * be irq safe.
	 */
	void		(*submit_request)(struct i915_request *rq);

	void		(*release)(struct intel_engine_cs *engine);

	/*
	 * Add / remove request from engine active tracking
	 */
	void		(*add_active_request)(struct i915_request *rq);
	void		(*remove_active_request)(struct i915_request *rq);

	/*
	 * Get engine busyness and the time at which the busyness was sampled.
	 */
	ktime_t		(*busyness)(struct intel_engine_cs *engine,
				    ktime_t *now);

	struct intel_engine_execlists execlists;

	/*
	 * Keep track of completed timelines on this engine for early
	 * retirement with the goal of quickly enabling powersaving as
	 * soon as the engine is idle.
	 */
	struct intel_timeline *retire;
	struct work_struct retire_work;

	/* status_notifier: list of callbacks for context-switch changes */
#ifdef notyet
	struct atomic_notifier_head context_status_notifier;
#endif

#define I915_ENGINE_USING_CMD_PARSER BIT(0)
#define I915_ENGINE_SUPPORTS_STATS   BIT(1)
#define I915_ENGINE_HAS_PREEMPTION   BIT(2)
#define I915_ENGINE_HAS_SEMAPHORES   BIT(3)
#define I915_ENGINE_HAS_TIMESLICES   BIT(4)
#define I915_ENGINE_IS_VIRTUAL       BIT(5)
#define I915_ENGINE_HAS_RELATIVE_MMIO BIT(6)
#define I915_ENGINE_REQUIRES_CMD_PARSER BIT(7)
#define I915_ENGINE_WANT_FORCED_PREEMPTION BIT(8)
#define I915_ENGINE_HAS_RCS_REG_STATE  BIT(9)
#define I915_ENGINE_HAS_EU_PRIORITY    BIT(10)
#define I915_ENGINE_FIRST_RENDER_COMPUTE BIT(11)
#define I915_ENGINE_USES_WA_HOLD_SWITCHOUT BIT(12)
	unsigned int flags;

	/*
	 * Table of commands the command parser needs to know about
	 * for this engine.
	 */
	DECLARE_HASHTABLE(cmd_hash, I915_CMD_HASH_ORDER);

	/*
	 * Table of registers allowed in commands that read/write registers.
	 */
	const struct drm_i915_reg_table *reg_tables;
	int reg_table_count;

	/*
	 * Returns the bitmask for the length field of the specified command.
	 * Return 0 for an unrecognized/invalid command.
	 *
	 * If the command parser finds an entry for a command in the engine's
	 * cmd_tables, it gets the command's length based on the table entry.
	 * If not, it calls this function to determine the per-engine length
	 * field encoding for the command (i.e. different opcode ranges use
	 * certain bits to encode the command length in the header).
	 */
	u32 (*get_cmd_length_mask)(u32 cmd_header);

	struct {
		union {
			struct intel_engine_execlists_stats execlists;
			struct intel_engine_guc_stats guc;
		};

		/**
		 * @rps: Utilisation at last RPS sampling.
		 */
		ktime_t rps;
	} stats;

	struct {
		unsigned long heartbeat_interval_ms;
		unsigned long max_busywait_duration_ns;
		unsigned long preempt_timeout_ms;
		unsigned long stop_timeout_ms;
		unsigned long timeslice_duration_ms;
	} props, defaults;

	I915_SELFTEST_DECLARE(struct fault_attr reset_timeout);

	/*
	 * The perf group maps to one OA unit which controls one OA buffer. All
	 * reports corresponding to this engine will be reported to this OA
	 * buffer. An engine will map to a single OA unit, but a single OA unit
	 * can generate reports for multiple engines.
	 */
	struct i915_perf_group *oa_group;
};

static inline bool
intel_engine_using_cmd_parser(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_USING_CMD_PARSER;
}

static inline bool
intel_engine_requires_cmd_parser(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_REQUIRES_CMD_PARSER;
}

static inline bool
intel_engine_supports_stats(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_SUPPORTS_STATS;
}

static inline bool
intel_engine_has_preemption(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_HAS_PREEMPTION;
}

static inline bool
intel_engine_has_semaphores(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_HAS_SEMAPHORES;
}

static inline bool
intel_engine_has_timeslices(const struct intel_engine_cs *engine)
{
	if (!CONFIG_DRM_I915_TIMESLICE_DURATION)
		return false;

	return engine->flags & I915_ENGINE_HAS_TIMESLICES;
}

static inline bool
intel_engine_is_virtual(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_IS_VIRTUAL;
}

static inline bool
intel_engine_has_relative_mmio(const struct intel_engine_cs * const engine)
{
	return engine->flags & I915_ENGINE_HAS_RELATIVE_MMIO;
}

/* Wa_14014475959:dg2 */
/* Wa_16019325821 */
/* Wa_14019159160 */
static inline bool
intel_engine_uses_wa_hold_switchout(struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_USES_WA_HOLD_SWITCHOUT;
}

#endif /* __INTEL_ENGINE_TYPES_H__ */
