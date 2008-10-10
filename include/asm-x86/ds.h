/*
 * Debug Store (DS) support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for branch trace store (BTS) and
 * precise-event based sampling (PEBS).
 *
 * It manages:
 * - per-thread and per-cpu allocation of BTS and PEBS
 * - buffer memory allocation (optional)
 * - buffer overflow handling
 * - buffer access
 *
 * It assumes:
 * - get_task_struct on all parameter tasks
 * - current is allowed to trace parameter tasks
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2007-2008
 */

#ifndef ASM_X86__DS_H
#define ASM_X86__DS_H

#ifdef CONFIG_X86_DS

#include <linux/types.h>
#include <linux/init.h>


struct task_struct;

/*
 * Request BTS or PEBS
 *
 * Due to alignement constraints, the actual buffer may be slightly
 * smaller than the requested or provided buffer.
 *
 * Returns 0 on success; -Eerrno otherwise
 *
 * task: the task to request recording for;
 *       NULL for per-cpu recording on the current cpu
 * base: the base pointer for the (non-pageable) buffer;
 *       NULL if buffer allocation requested
 * size: the size of the requested or provided buffer
 * ovfl: pointer to a function to be called on buffer overflow;
 *       NULL if cyclic buffer requested
 */
typedef void (*ds_ovfl_callback_t)(struct task_struct *);
extern int ds_request_bts(struct task_struct *task, void *base, size_t size,
			  ds_ovfl_callback_t ovfl);
extern int ds_request_pebs(struct task_struct *task, void *base, size_t size,
			   ds_ovfl_callback_t ovfl);

/*
 * Release BTS or PEBS resources
 *
 * Frees buffers allocated on ds_request.
 *
 * Returns 0 on success; -Eerrno otherwise
 *
 * task: the task to release resources for;
 *       NULL to release resources for the current cpu
 */
extern int ds_release_bts(struct task_struct *task);
extern int ds_release_pebs(struct task_struct *task);

/*
 * Return the (array) index of the write pointer.
 * (assuming an array of BTS/PEBS records)
 *
 * Returns -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * pos (out): if not NULL, will hold the result
 */
extern int ds_get_bts_index(struct task_struct *task, size_t *pos);
extern int ds_get_pebs_index(struct task_struct *task, size_t *pos);

/*
 * Return the (array) index one record beyond the end of the array.
 * (assuming an array of BTS/PEBS records)
 *
 * Returns -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * pos (out): if not NULL, will hold the result
 */
extern int ds_get_bts_end(struct task_struct *task, size_t *pos);
extern int ds_get_pebs_end(struct task_struct *task, size_t *pos);

/*
 * Provide a pointer to the BTS/PEBS record at parameter index.
 * (assuming an array of BTS/PEBS records)
 *
 * The pointer points directly into the buffer. The user is
 * responsible for copying the record.
 *
 * Returns the size of a single record on success; -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * index: the index of the requested record
 * record (out): pointer to the requested record
 */
extern int ds_access_bts(struct task_struct *task,
			 size_t index, const void **record);
extern int ds_access_pebs(struct task_struct *task,
			  size_t index, const void **record);

/*
 * Write one or more BTS/PEBS records at the write pointer index and
 * advance the write pointer.
 *
 * If size is not a multiple of the record size, trailing bytes are
 * zeroed out.
 *
 * May result in one or more overflow notifications.
 *
 * If called during overflow handling, that is, with index >=
 * interrupt threshold, the write will wrap around.
 *
 * An overflow notification is given if and when the interrupt
 * threshold is reached during or after the write.
 *
 * Returns the number of bytes written or -Eerrno.
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * buffer: the buffer to write
 * size: the size of the buffer
 */
extern int ds_write_bts(struct task_struct *task,
			const void *buffer, size_t size);
extern int ds_write_pebs(struct task_struct *task,
			 const void *buffer, size_t size);

/*
 * Same as ds_write_bts/pebs, but omit ownership checks.
 *
 * This is needed to have some other task than the owner of the
 * BTS/PEBS buffer or the parameter task itself write into the
 * respective buffer.
 */
extern int ds_unchecked_write_bts(struct task_struct *task,
				  const void *buffer, size_t size);
extern int ds_unchecked_write_pebs(struct task_struct *task,
				   const void *buffer, size_t size);

/*
 * Reset the write pointer of the BTS/PEBS buffer.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 */
extern int ds_reset_bts(struct task_struct *task);
extern int ds_reset_pebs(struct task_struct *task);

/*
 * Clear the BTS/PEBS buffer and reset the write pointer.
 * The entire buffer will be zeroed out.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 */
extern int ds_clear_bts(struct task_struct *task);
extern int ds_clear_pebs(struct task_struct *task);

/*
 * Provide the PEBS counter reset value.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * value (out): the counter reset value
 */
extern int ds_get_pebs_reset(struct task_struct *task, u64 *value);

/*
 * Set the PEBS counter reset value.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * task: the task to access;
 *       NULL to access the current cpu
 * value: the new counter reset value
 */
extern int ds_set_pebs_reset(struct task_struct *task, u64 value);

/*
 * Initialization
 */
struct cpuinfo_x86;
extern void __cpuinit ds_init_intel(struct cpuinfo_x86 *);



/*
 * The DS context - part of struct thread_struct.
 */
struct ds_context {
	/* pointer to the DS configuration; goes into MSR_IA32_DS_AREA */
	unsigned char *ds;
	/* the owner of the BTS and PEBS configuration, respectively */
	struct task_struct *owner[2];
	/* buffer overflow notification function for BTS and PEBS */
	ds_ovfl_callback_t callback[2];
	/* the original buffer address */
	void *buffer[2];
	/* the number of allocated pages for on-request allocated buffers */
	unsigned int pages[2];
	/* use count */
	unsigned long count;
	/* a pointer to the context location inside the thread_struct
	 * or the per_cpu context array */
	struct ds_context **this;
	/* a pointer to the task owning this context, or NULL, if the
	 * context is owned by a cpu */
	struct task_struct *task;
};

/* called by exit_thread() to free leftover contexts */
extern void ds_free(struct ds_context *context);

#else /* CONFIG_X86_DS */

#define ds_init_intel(config) do {} while (0)

#endif /* CONFIG_X86_DS */
#endif /* ASM_X86__DS_H */
