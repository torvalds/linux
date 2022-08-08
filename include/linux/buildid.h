/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUILDID_H
#define _LINUX_BUILDID_H

#include <linux/mm_types.h>

#define BUILD_ID_SIZE_MAX 20

int build_id_parse(struct vm_area_struct *vma, unsigned char *build_id,
		   __u32 *size);

#endif
