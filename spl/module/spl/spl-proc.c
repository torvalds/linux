/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Proc Implementation.
\*****************************************************************************/

#include <sys/systeminfo.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <linux/proc_compat.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if defined(CONSTIFY_PLUGIN) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
typedef struct ctl_table __no_const spl_ctl_table;
#else
typedef struct ctl_table spl_ctl_table;
#endif

static unsigned long table_min = 0;
static unsigned long table_max = ~0;

static struct ctl_table_header *spl_header = NULL;
static struct proc_dir_entry *proc_spl = NULL;
static struct proc_dir_entry *proc_spl_kmem = NULL;
static struct proc_dir_entry *proc_spl_kmem_slab = NULL;
struct proc_dir_entry *proc_spl_kstat = NULL;

static int
proc_copyin_string(char *kbuffer, int kbuffer_size,
                   const char *ubuffer, int ubuffer_size)
{
        int size;

        if (ubuffer_size > kbuffer_size)
                return -EOVERFLOW;

        if (copy_from_user((void *)kbuffer, (void *)ubuffer, ubuffer_size))
                return -EFAULT;

        /* strip trailing whitespace */
        size = strnlen(kbuffer, ubuffer_size);
        while (size-- >= 0)
                if (!isspace(kbuffer[size]))
                        break;

        /* empty string */
        if (size < 0)
                return -EINVAL;

        /* no space to terminate */
        if (size == kbuffer_size)
                return -EOVERFLOW;

        kbuffer[size + 1] = 0;
        return 0;
}

static int
proc_copyout_string(char *ubuffer, int ubuffer_size,
                    const char *kbuffer, char *append)
{
        /* NB if 'append' != NULL, it's a single character to append to the
         * copied out string - usually "\n", for /proc entries and
         * (i.e. a terminating zero byte) for sysctl entries
         */
        int size = MIN(strlen(kbuffer), ubuffer_size);

        if (copy_to_user(ubuffer, kbuffer, size))
                return -EFAULT;

        if (append != NULL && size < ubuffer_size) {
                if (copy_to_user(ubuffer + size, append, 1))
                        return -EFAULT;

                size++;
        }

        return size;
}

#ifdef DEBUG_KMEM
static int
proc_domemused(struct ctl_table *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val;
        spl_ctl_table dummy = *table;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

        if (write) {
                *ppos += *lenp;
        } else {
# ifdef HAVE_ATOMIC64_T
                val = atomic64_read((atomic64_t *)table->data);
# else
                val = atomic_read((atomic_t *)table->data);
# endif /* HAVE_ATOMIC64_T */
                rc = proc_doulongvec_minmax(&dummy, write, buffer, lenp, ppos);
        }

        return (rc);
}
#endif /* DEBUG_KMEM */

static int
proc_doslab(struct ctl_table *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val = 0, mask;
        spl_ctl_table dummy = *table;
        spl_kmem_cache_t *skc;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

        if (write) {
                *ppos += *lenp;
        } else {
                down_read(&spl_kmem_cache_sem);
                mask = (unsigned long)table->data;

                list_for_each_entry(skc, &spl_kmem_cache_list, skc_list) {

			/* Only use slabs of the correct kmem/vmem type */
			if (!(skc->skc_flags & mask))
				continue;

			/* Sum the specified field for selected slabs */
			switch (mask & (KMC_TOTAL | KMC_ALLOC | KMC_MAX)) {
			case KMC_TOTAL:
	                        val += skc->skc_slab_size * skc->skc_slab_total;
				break;
			case KMC_ALLOC:
	                        val += skc->skc_obj_size * skc->skc_obj_alloc;
				break;
			case KMC_MAX:
	                        val += skc->skc_obj_size * skc->skc_obj_max;
				break;
			}
                }

                up_read(&spl_kmem_cache_sem);
                rc = proc_doulongvec_minmax(&dummy, write, buffer, lenp, ppos);
        }

        return (rc);
}

static int
proc_dohostid(struct ctl_table *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int len, rc = 0;
        char *end, str[32];

        if (write) {
                /* We can't use proc_doulongvec_minmax() in the write
                 * case here because hostid while a hex value has no
                 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        return (rc);

                spl_hostid = simple_strtoul(str, &end, 16);
                if (str == end)
                        return (-EINVAL);

        } else {
                len = snprintf(str, sizeof(str), "%lx", spl_hostid);
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        return (rc);
}

static void
slab_seq_show_headers(struct seq_file *f)
{
        seq_printf(f,
            "--------------------- cache ----------"
            "---------------------------------------------  "
            "----- slab ------  "
            "---- object -----  "
            "--- emergency ---\n");
        seq_printf(f,
            "name                                  "
            "  flags      size     alloc slabsize  objsize  "
            "total alloc   max  "
            "total alloc   max  "
            "dlock alloc   max\n");
}

static int
slab_seq_show(struct seq_file *f, void *p)
{
        spl_kmem_cache_t *skc = p;

        ASSERT(skc->skc_magic == SKC_MAGIC);

	/*
	 * Backed by Linux slab see /proc/slabinfo.
	 */
	if (skc->skc_flags & KMC_SLAB)
		return (0);

        spin_lock(&skc->skc_lock);
        seq_printf(f, "%-36s  ", skc->skc_name);
        seq_printf(f, "0x%05lx %9lu %9lu %8u %8u  "
            "%5lu %5lu %5lu  %5lu %5lu %5lu  %5lu %5lu %5lu\n",
            (long unsigned)skc->skc_flags,
            (long unsigned)(skc->skc_slab_size * skc->skc_slab_total),
            (long unsigned)(skc->skc_obj_size * skc->skc_obj_alloc),
            (unsigned)skc->skc_slab_size,
            (unsigned)skc->skc_obj_size,
            (long unsigned)skc->skc_slab_total,
            (long unsigned)skc->skc_slab_alloc,
            (long unsigned)skc->skc_slab_max,
            (long unsigned)skc->skc_obj_total,
            (long unsigned)skc->skc_obj_alloc,
            (long unsigned)skc->skc_obj_max,
            (long unsigned)skc->skc_obj_deadlock,
            (long unsigned)skc->skc_obj_emergency,
            (long unsigned)skc->skc_obj_emergency_max);

        spin_unlock(&skc->skc_lock);

        return 0;
}

static void *
slab_seq_start(struct seq_file *f, loff_t *pos)
{
        struct list_head *p;
        loff_t n = *pos;

	down_read(&spl_kmem_cache_sem);
        if (!n)
                slab_seq_show_headers(f);

        p = spl_kmem_cache_list.next;
        while (n--) {
                p = p->next;
                if (p == &spl_kmem_cache_list)
                        return (NULL);
        }

        return (list_entry(p, spl_kmem_cache_t, skc_list));
}

static void *
slab_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	spl_kmem_cache_t *skc = p;

        ++*pos;
        return ((skc->skc_list.next == &spl_kmem_cache_list) ?
	       NULL : list_entry(skc->skc_list.next,spl_kmem_cache_t,skc_list));
}

static void
slab_seq_stop(struct seq_file *f, void *v)
{
	up_read(&spl_kmem_cache_sem);
}

static struct seq_operations slab_seq_ops = {
        .show  = slab_seq_show,
        .start = slab_seq_start,
        .next  = slab_seq_next,
        .stop  = slab_seq_stop,
};

static int
proc_slab_open(struct inode *inode, struct file *filp)
{
        return seq_open(filp, &slab_seq_ops);
}

static struct file_operations proc_slab_operations = {
        .open           = proc_slab_open,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = seq_release,
};

static struct ctl_table spl_kmem_table[] = {
#ifdef DEBUG_KMEM
        {
                .procname = "kmem_used",
                .data     = &kmem_alloc_used,
# ifdef HAVE_ATOMIC64_T
                .maxlen   = sizeof(atomic64_t),
# else
                .maxlen   = sizeof(atomic_t),
# endif /* HAVE_ATOMIC64_T */
                .mode     = 0444,
                .proc_handler = &proc_domemused,
        },
        {
                .procname = "kmem_max",
                .data     = &kmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
#endif /* DEBUG_KMEM */
        {
                .procname = "slab_kmem_total",
		.data     = (void *)(KMC_KMEM | KMC_TOTAL),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                .procname = "slab_kmem_alloc",
		.data     = (void *)(KMC_KMEM | KMC_ALLOC),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                .procname = "slab_kmem_max",
		.data     = (void *)(KMC_KMEM | KMC_MAX),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                .procname = "slab_vmem_total",
		.data     = (void *)(KMC_VMEM | KMC_TOTAL),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                .procname = "slab_vmem_alloc",
		.data     = (void *)(KMC_VMEM | KMC_ALLOC),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                .procname = "slab_vmem_max",
		.data     = (void *)(KMC_VMEM | KMC_MAX),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
	{0},
};

static struct ctl_table spl_kstat_table[] = {
	{0},
};

static struct ctl_table spl_table[] = {
        /* NB No .strategy entries have been provided since
         * sysctl(8) prefers to go via /proc for portability.
         */
        {
                .procname = "version",
                .data     = spl_version,
                .maxlen   = sizeof(spl_version),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
        {
                .procname = "hostid",
                .data     = &spl_hostid,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dohostid,
        },
	{
		.procname = "kmem",
		.mode     = 0555,
		.child    = spl_kmem_table,
	},
	{
		.procname = "kstat",
		.mode     = 0555,
		.child    = spl_kstat_table,
	},
        { 0 },
};

static struct ctl_table spl_dir[] = {
        {
                .procname = "spl",
                .mode     = 0555,
                .child    = spl_table,
        },
        { 0 }
};

static struct ctl_table spl_root[] = {
	{
#ifdef HAVE_CTL_NAME
	.ctl_name = CTL_KERN,
#endif
	.procname = "kernel",
	.mode = 0555,
	.child = spl_dir,
	},
	{ 0 }
};

int
spl_proc_init(void)
{
	int rc = 0;

        spl_header = register_sysctl_table(spl_root);
	if (spl_header == NULL)
		return (-EUNATCH);

	proc_spl = proc_mkdir("spl", NULL);
	if (proc_spl == NULL) {
		rc = -EUNATCH;
		goto out;
	}

        proc_spl_kmem = proc_mkdir("kmem", proc_spl);
        if (proc_spl_kmem == NULL) {
                rc = -EUNATCH;
		goto out;
	}

	proc_spl_kmem_slab = proc_create_data("slab", 0444,
		proc_spl_kmem, &proc_slab_operations, NULL);
        if (proc_spl_kmem_slab == NULL) {
		rc = -EUNATCH;
		goto out;
	}

        proc_spl_kstat = proc_mkdir("kstat", proc_spl);
        if (proc_spl_kstat == NULL) {
                rc = -EUNATCH;
		goto out;
	}
out:
	if (rc) {
		remove_proc_entry("kstat", proc_spl);
	        remove_proc_entry("slab", proc_spl_kmem);
		remove_proc_entry("kmem", proc_spl);
		remove_proc_entry("spl", NULL);
	        unregister_sysctl_table(spl_header);
	}

        return (rc);
}

void
spl_proc_fini(void)
{
	remove_proc_entry("kstat", proc_spl);
        remove_proc_entry("slab", proc_spl_kmem);
	remove_proc_entry("kmem", proc_spl);
	remove_proc_entry("spl", NULL);

        ASSERT(spl_header != NULL);
        unregister_sysctl_table(spl_header);
}
