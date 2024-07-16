// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#include "test_fuse_bpf.h"

SEC("maps") struct fuse_bpf_map test_map = {
	BPF_MAP_TYPE_ARRAY,
	sizeof(uint32_t),
	sizeof(uint32_t),
	1000,
};

SEC("maps") struct fuse_bpf_map test_map2 = {
	BPF_MAP_TYPE_HASH,
	sizeof(uint32_t),
	sizeof(uint64_t),
	76,
};

SEC("test_daemon") int trace_daemon(struct fuse_bpf_args *fa)
{
	uint64_t uid_gid = bpf_get_current_uid_gid();
	uint32_t uid = uid_gid & 0xffffffff;
	uint64_t pid_tgid = bpf_get_current_pid_tgid();
	uint32_t pid = pid_tgid & 0xffffffff;
	uint32_t key = 23;
	uint32_t *pvalue;

	pvalue = bpf_map_lookup_elem(&test_map, &key);
	if (pvalue) {
		uint32_t value = *pvalue;

		bpf_printk("pid %u uid %u value %u", pid, uid, value);
		value++;
		bpf_map_update_elem(&test_map, &key,  &value, BPF_ANY);
	}

	switch (fa->opcode) {
	case FUSE_ACCESS | FUSE_PREFILTER: {
		bpf_printk("Access: %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_GETATTR | FUSE_PREFILTER: {
		const struct fuse_getattr_in *fgi = fa->in_args[0].value;

		bpf_printk("Get Attr %d", fgi->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_SETATTR | FUSE_PREFILTER: {
		const struct fuse_setattr_in *fsi = fa->in_args[0].value;

		bpf_printk("Set Attr %d", fsi->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_OPENDIR | FUSE_PREFILTER: {
		bpf_printk("Open Dir: %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa->in_args[0].value;

		bpf_printk("Read Dir: fh: %lu", fri->fh, fri->offset);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("Lookup: %lx %s", fa->nodeid, name);
		if (fa->nodeid == 1)
			return FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING;
		else
			return FUSE_BPF_BACKING;
	}

	case FUSE_MKNOD | FUSE_PREFILTER: {
		const struct fuse_mknod_in *fmi = fa->in_args[0].value;
		const char *name = fa->in_args[1].value;

		bpf_printk("mknod %s %x %x", name,  fmi->rdev | fmi->mode, fmi->umask);
		return FUSE_BPF_BACKING;
	}

	case FUSE_MKDIR | FUSE_PREFILTER: {
		const struct fuse_mkdir_in *fmi = fa->in_args[0].value;
		const char *name = fa->in_args[1].value;

		bpf_printk("mkdir: %s %x %x", name, fmi->mode, fmi->umask);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RMDIR | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("rmdir: %s", name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RENAME | FUSE_PREFILTER: {
		const char *oldname = fa->in_args[1].value;
		const char *newname = fa->in_args[2].value;

		bpf_printk("rename from %s", oldname);
		bpf_printk("rename to %s", newname);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RENAME2 | FUSE_PREFILTER: {
		const struct fuse_rename2_in *fri = fa->in_args[0].value;
		uint32_t flags = fri->flags;
		const char *oldname = fa->in_args[1].value;
		const char *newname = fa->in_args[2].value;

		bpf_printk("rename(%x) from %s", flags, oldname);
		bpf_printk("rename to %s", newname);
		return FUSE_BPF_BACKING;
	}

	case FUSE_UNLINK | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("unlink: %s", name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LINK | FUSE_PREFILTER: {
		const struct fuse_link_in *fli = fa->in_args[0].value;
		const char *dst_name = fa->in_args[1].value;

		bpf_printk("Link: %d %s", fli->oldnodeid, dst_name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_SYMLINK | FUSE_PREFILTER: {
		const char *link_name = fa->in_args[0].value;
		const char *link_dest = fa->in_args[1].value;

		bpf_printk("symlink from %s", link_name);
		bpf_printk("symlink to %s", link_dest);
		return FUSE_BPF_BACKING;
	}

	case FUSE_READLINK | FUSE_PREFILTER: {
		const char *link_name = fa->in_args[0].value;

		bpf_printk("readlink from %s", link_name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RELEASE | FUSE_PREFILTER: {
		const struct fuse_release_in *fri = fa->in_args[0].value;

		bpf_printk("Release: %d", fri->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RELEASEDIR | FUSE_PREFILTER: {
		const struct fuse_release_in *fri = fa->in_args[0].value;

		bpf_printk("Release Dir: %d", fri->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_CREATE | FUSE_PREFILTER: {
		bpf_printk("Create %s", fa->in_args[1].value);
		return FUSE_BPF_BACKING;
	}

	case FUSE_OPEN | FUSE_PREFILTER: {
		bpf_printk("Open: %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_READ | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa->in_args[0].value;

		bpf_printk("Read: fh: %lu, offset %lu, size %lu",
			   fri->fh, fri->offset, fri->size);
		return FUSE_BPF_BACKING;
	}

	case FUSE_WRITE | FUSE_PREFILTER: {
		const struct fuse_write_in *fwi = fa->in_args[0].value;

		bpf_printk("Write: fh: %lu, offset %lu, size %lu",
			   fwi->fh, fwi->offset, fwi->size);
		return FUSE_BPF_BACKING;
	}

	case FUSE_FLUSH | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa->in_args[0].value;

		bpf_printk("Flush %d", ffi->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_FALLOCATE | FUSE_PREFILTER: {
		const struct fuse_fallocate_in *ffa = fa->in_args[0].value;

		bpf_printk("Fallocate %d %lu", ffa->fh, ffa->length);
		return FUSE_BPF_BACKING;
	}

	case FUSE_GETXATTR | FUSE_PREFILTER: {
		const char *name = fa->in_args[1].value;

		bpf_printk("Getxattr %d %s", fa->nodeid, name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LISTXATTR | FUSE_PREFILTER: {
		const char *name = fa->in_args[1].value;

		bpf_printk("Listxattr %d %s", fa->nodeid, name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_SETXATTR | FUSE_PREFILTER: {
		const char *name = fa->in_args[1].value;

		bpf_printk("Setxattr %d %s", fa->nodeid, name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_STATFS | FUSE_PREFILTER: {
		bpf_printk("statfs %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LSEEK | FUSE_PREFILTER: {
		const struct fuse_lseek_in *fli = fa->in_args[0].value;

		bpf_printk("lseek type:%d, offset:%lld", fli->whence, fli->offset);
		return FUSE_BPF_BACKING;
	}

	default:
		if (fa->opcode & FUSE_PREFILTER)
			bpf_printk("prefilter *** UNKNOWN *** opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else if (fa->opcode & FUSE_POSTFILTER)
			bpf_printk("postfilter *** UNKNOWN *** opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else
			bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
		return FUSE_BPF_BACKING;
	}
}
