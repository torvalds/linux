/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUILDID_H
#define _LINUX_BUILDID_H

#include <linux/types.h>

#define BUILD_ID_SIZE_MAX 20

struct vm_area_struct;
int build_id_parse(struct vm_area_struct *vma, unsigned char *build_id, __u32 *size);
int build_id_parse_nofault(struct vm_area_struct *vma, unsigned char *build_id, __u32 *size);
int build_id_parse_buf(const void *buf, unsigned char *build_id, u32 buf_size);

#if IS_ENABLED(CONFIG_STACKTRACE_BUILD_ID) || IS_ENABLED(CONFIG_VMCORE_INFO)
extern unsigned char vmlinux_build_id[BUILD_ID_SIZE_MAX];
void init_vmlinux_build_id(void);
#else
static inline void init_vmlinux_build_id(void) { }
#endif

struct freader {
	void *buf;
	u32 buf_sz;
	int err;
	union {
		struct {
			struct file *file;
			struct folio *folio;
			void *addr;
			loff_t folio_off;
			bool may_fault;
		};
		struct {
			const char *data;
			u64 data_sz;
		};
	};
};

void freader_init_from_file(struct freader *r, void *buf, u32 buf_sz,
			    struct file *file, bool may_fault);
void freader_init_from_mem(struct freader *r, const char *data, u64 data_sz);
const void *freader_fetch(struct freader *r, loff_t file_off, size_t sz);
void freader_cleanup(struct freader *r);

#endif
