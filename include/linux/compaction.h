#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/* Return values for compact_zone() */
#define COMPACT_CONTINUE	0
#define COMPACT_PARTIAL		1
#define COMPACT_COMPLETE	2

#ifdef CONFIG_COMPACTION
extern int sysctl_compact_memory;
extern int sysctl_compaction_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *length, loff_t *ppos);
#endif /* CONFIG_COMPACTION */

#endif /* _LINUX_COMPACTION_H */
