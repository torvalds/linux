/**
 * @file oprofile.h
 *
 * API for machine-specific interrupts to interface
 * to oprofile.
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROFILE_H
#define OPROFILE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
 
struct super_block;
struct dentry;
struct file_operations;
struct pt_regs;
 
/* Operations structure to be filled in */
struct oprofile_operations {
	/* create any necessary configuration files in the oprofile fs.
	 * Optional. */
	int (*create_files)(struct super_block * sb, struct dentry * root);
	/* Do any necessary interrupt setup. Optional. */
	int (*setup)(void);
	/* Do any necessary interrupt shutdown. Optional. */
	void (*shutdown)(void);
	/* Start delivering interrupts. */
	int (*start)(void);
	/* Stop delivering interrupts. */
	void (*stop)(void);
	/* Initiate a stack backtrace. Optional. */
	void (*backtrace)(struct pt_regs * const regs, unsigned int depth);
	/* CPU identification string. */
	char * cpu_type;
};

/**
 * One-time initialisation. *ops must be set to a filled-in
 * operations structure. This is called even in timer interrupt
 * mode so an arch can set a backtrace callback.
 *
 * If an error occurs, the fields should be left untouched.
 */
int oprofile_arch_init(struct oprofile_operations * ops);
 
/**
 * One-time exit/cleanup for the arch.
 */
void oprofile_arch_exit(void);

/**
 * Add a sample. This may be called from any context. Pass
 * smp_processor_id() as cpu.
 */
void oprofile_add_sample(struct pt_regs * const regs, unsigned long event);

/**
 * Add an extended sample.  Use this when the PC is not from the regs, and
 * we cannot determine if we're in kernel mode from the regs.
 *
 * This function does perform a backtrace.
 *
 */
void oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
				unsigned long event, int is_kernel);

/* Use this instead when the PC value is not from the regs. Doesn't
 * backtrace. */
void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event);

/* add a backtrace entry, to be called from the ->backtrace callback */
void oprofile_add_trace(unsigned long eip);


/**
 * Create a file of the given name as a child of the given root, with
 * the specified file operations.
 */
int oprofilefs_create_file(struct super_block * sb, struct dentry * root,
	char const * name, const struct file_operations * fops);

int oprofilefs_create_file_perm(struct super_block * sb, struct dentry * root,
	char const * name, const struct file_operations * fops, int perm);
 
/** Create a file for read/write access to an unsigned long. */
int oprofilefs_create_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);
 
/** Create a file for read-only access to an unsigned long. */
int oprofilefs_create_ro_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);
 
/** Create a file for read-only access to an atomic_t. */
int oprofilefs_create_ro_atomic(struct super_block * sb, struct dentry * root,
	char const * name, atomic_t * val);
 
/** create a directory */
struct dentry * oprofilefs_mkdir(struct super_block * sb, struct dentry * root,
	char const * name);

/**
 * Write the given asciz string to the given user buffer @buf, updating *offset
 * appropriately. Returns bytes written or -EFAULT.
 */
ssize_t oprofilefs_str_to_user(char const * str, char __user * buf, size_t count, loff_t * offset);

/**
 * Convert an unsigned long value into ASCII and copy it to the user buffer @buf,
 * updating *offset appropriately. Returns bytes written or -EFAULT.
 */
ssize_t oprofilefs_ulong_to_user(unsigned long val, char __user * buf, size_t count, loff_t * offset);

/**
 * Read an ASCII string for a number from a userspace buffer and fill *val on success.
 * Returns 0 on success, < 0 on error.
 */
int oprofilefs_ulong_from_user(unsigned long * val, char const __user * buf, size_t count);

/** lock for read/write safety */
extern spinlock_t oprofilefs_lock;
 
#endif /* OPROFILE_H */
