/* medusa/l1/inode.h, (C) 2002 Milan Pikula
 *
 * struct inode extension: this structure is appended to in-kernel data,
 * and we define it separately just to make l1 code shorter.
 *
 * for another data structure - kobject, describing inode for upper layers - 
 * see l2/kobject_file.[ch].
 */

#ifndef _MEDUSA_L1_INODE_H
#define _MEDUSA_L1_INODE_H

//#include <linux/config.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/capability.h>
#include <linux/medusa/l3/model.h>

struct medusa_l1_inode_s {
	MEDUSA_OBJECT_VARS;
	__u32 user;
#ifdef CONFIG_MEDUSA_FILE_CAPABILITIES
       kernel_cap_t icap, pcap, ecap;  /* support for POSIX file capabilities */
#endif /* CONFIG_MEDUSA_FILE_CAPABILITIES */

       /* for kobject_file.c - don't touch! */
       struct inode * next_live;
       int use_count;
	   char *fuck_path;
};

#endif
