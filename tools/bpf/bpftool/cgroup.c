// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2017 Facebook
// Author: Roman Gushchin <guro@fb.com>

#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/btf.h>

#include "main.h"

static const int cgroup_attach_types[] = {
	BPF_CGROUP_INET_INGRESS,
	BPF_CGROUP_INET_EGRESS,
	BPF_CGROUP_INET_SOCK_CREATE,
	BPF_CGROUP_INET_SOCK_RELEASE,
	BPF_CGROUP_INET4_BIND,
	BPF_CGROUP_INET6_BIND,
	BPF_CGROUP_INET4_POST_BIND,
	BPF_CGROUP_INET6_POST_BIND,
	BPF_CGROUP_INET4_CONNECT,
	BPF_CGROUP_INET6_CONNECT,
	BPF_CGROUP_UNIX_CONNECT,
	BPF_CGROUP_INET4_GETPEERNAME,
	BPF_CGROUP_INET6_GETPEERNAME,
	BPF_CGROUP_UNIX_GETPEERNAME,
	BPF_CGROUP_INET4_GETSOCKNAME,
	BPF_CGROUP_INET6_GETSOCKNAME,
	BPF_CGROUP_UNIX_GETSOCKNAME,
	BPF_CGROUP_UDP4_SENDMSG,
	BPF_CGROUP_UDP6_SENDMSG,
	BPF_CGROUP_UNIX_SENDMSG,
	BPF_CGROUP_UDP4_RECVMSG,
	BPF_CGROUP_UDP6_RECVMSG,
	BPF_CGROUP_UNIX_RECVMSG,
	BPF_CGROUP_SOCK_OPS,
	BPF_CGROUP_DEVICE,
	BPF_CGROUP_SYSCTL,
	BPF_CGROUP_GETSOCKOPT,
	BPF_CGROUP_SETSOCKOPT,
	BPF_LSM_CGROUP
};

#define HELP_SPEC_ATTACH_FLAGS						\
	"ATTACH_FLAGS := { multi | override }"

#define HELP_SPEC_ATTACH_TYPES						\
	"       ATTACH_TYPE := { cgroup_inet_ingress | cgroup_inet_egress |\n" \
	"                        cgroup_inet_sock_create | cgroup_sock_ops |\n" \
	"                        cgroup_device | cgroup_inet4_bind |\n" \
	"                        cgroup_inet6_bind | cgroup_inet4_post_bind |\n" \
	"                        cgroup_inet6_post_bind | cgroup_inet4_connect |\n" \
	"                        cgroup_inet6_connect | cgroup_unix_connect |\n" \
	"                        cgroup_inet4_getpeername | cgroup_inet6_getpeername |\n" \
	"                        cgroup_unix_getpeername | cgroup_inet4_getsockname |\n" \
	"                        cgroup_inet6_getsockname | cgroup_unix_getsockname |\n" \
	"                        cgroup_udp4_sendmsg | cgroup_udp6_sendmsg |\n" \
	"                        cgroup_unix_sendmsg | cgroup_udp4_recvmsg |\n" \
	"                        cgroup_udp6_recvmsg | cgroup_unix_recvmsg |\n" \
	"                        cgroup_sysctl | cgroup_getsockopt |\n" \
	"                        cgroup_setsockopt | cgroup_inet_sock_release }"

static unsigned int query_flags;
static struct btf *btf_vmlinux;
static __u32 btf_vmlinux_id;

static enum bpf_attach_type parse_attach_type(const char *str)
{
	const char *attach_type_str;
	enum bpf_attach_type type;

	for (type = 0; ; type++) {
		attach_type_str = libbpf_bpf_attach_type_str(type);
		if (!attach_type_str)
			break;
		if (!strcmp(str, attach_type_str))
			return type;
	}

	/* Also check traditionally used attach type strings. For these we keep
	 * allowing prefixed usage.
	 */
	for (type = 0; ; type++) {
		attach_type_str = bpf_attach_type_input_str(type);
		if (!attach_type_str)
			break;
		if (is_prefix(str, attach_type_str))
			return type;
	}

	return __MAX_BPF_ATTACH_TYPE;
}

static void guess_vmlinux_btf_id(__u32 attach_btf_obj_id)
{
	struct bpf_btf_info btf_info = {};
	__u32 btf_len = sizeof(btf_info);
	char name[16] = {};
	int err;
	int fd;

	btf_info.name = ptr_to_u64(name);
	btf_info.name_len = sizeof(name);

	fd = bpf_btf_get_fd_by_id(attach_btf_obj_id);
	if (fd < 0)
		return;

	err = bpf_btf_get_info_by_fd(fd, &btf_info, &btf_len);
	if (err)
		goto out;

	if (btf_info.kernel_btf && strncmp(name, "vmlinux", sizeof(name)) == 0)
		btf_vmlinux_id = btf_info.id;

out:
	close(fd);
}

static int show_bpf_prog(int id, enum bpf_attach_type attach_type,
			 const char *attach_flags_str,
			 int level)
{
	char prog_name[MAX_PROG_FULL_NAME];
	const char *attach_btf_name = NULL;
	struct bpf_prog_info info = {};
	const char *attach_type_str;
	__u32 info_len = sizeof(info);
	int prog_fd;

	prog_fd = bpf_prog_get_fd_by_id(id);
	if (prog_fd < 0)
		return -1;

	if (bpf_prog_get_info_by_fd(prog_fd, &info, &info_len)) {
		close(prog_fd);
		return -1;
	}

	attach_type_str = libbpf_bpf_attach_type_str(attach_type);

	if (btf_vmlinux) {
		if (!btf_vmlinux_id)
			guess_vmlinux_btf_id(info.attach_btf_obj_id);

		if (btf_vmlinux_id == info.attach_btf_obj_id &&
		    info.attach_btf_id < btf__type_cnt(btf_vmlinux)) {
			const struct btf_type *t =
				btf__type_by_id(btf_vmlinux, info.attach_btf_id);
			attach_btf_name =
				btf__name_by_offset(btf_vmlinux, t->name_off);
		}
	}

	get_prog_full_name(&info, prog_fd, prog_name, sizeof(prog_name));
	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_uint_field(json_wtr, "id", info.id);
		if (attach_type_str)
			jsonw_string_field(json_wtr, "attach_type", attach_type_str);
		else
			jsonw_uint_field(json_wtr, "attach_type", attach_type);
		if (!(query_flags & BPF_F_QUERY_EFFECTIVE))
			jsonw_string_field(json_wtr, "attach_flags", attach_flags_str);
		jsonw_string_field(json_wtr, "name", prog_name);
		if (attach_btf_name)
			jsonw_string_field(json_wtr, "attach_btf_name", attach_btf_name);
		jsonw_uint_field(json_wtr, "attach_btf_obj_id", info.attach_btf_obj_id);
		jsonw_uint_field(json_wtr, "attach_btf_id", info.attach_btf_id);
		jsonw_end_object(json_wtr);
	} else {
		printf("%s%-8u ", level ? "    " : "", info.id);
		if (attach_type_str)
			printf("%-15s", attach_type_str);
		else
			printf("type %-10u", attach_type);
		if (query_flags & BPF_F_QUERY_EFFECTIVE)
			printf(" %-15s", prog_name);
		else
			printf(" %-15s %-15s", attach_flags_str, prog_name);
		if (attach_btf_name)
			printf(" %-15s", attach_btf_name);
		else if (info.attach_btf_id)
			printf(" attach_btf_obj_id=%u attach_btf_id=%u",
			       info.attach_btf_obj_id, info.attach_btf_id);
		printf("\n");
	}

	close(prog_fd);
	return 0;
}

static int count_attached_bpf_progs(int cgroup_fd, enum bpf_attach_type type)
{
	__u32 prog_cnt = 0;
	int ret;

	ret = bpf_prog_query(cgroup_fd, type, query_flags, NULL,
			     NULL, &prog_cnt);
	if (ret)
		return -1;

	return prog_cnt;
}

static int cgroup_has_attached_progs(int cgroup_fd)
{
	unsigned int i = 0;
	bool no_prog = true;

	for (i = 0; i < ARRAY_SIZE(cgroup_attach_types); i++) {
		int count = count_attached_bpf_progs(cgroup_fd, cgroup_attach_types[i]);

		if (count < 0)
			return -1;

		if (count > 0) {
			no_prog = false;
			break;
		}
	}

	return no_prog ? 0 : 1;
}

static int show_effective_bpf_progs(int cgroup_fd, enum bpf_attach_type type,
				    int level)
{
	LIBBPF_OPTS(bpf_prog_query_opts, p);
	__u32 prog_ids[1024] = {0};
	__u32 iter;
	int ret;

	p.query_flags = query_flags;
	p.prog_cnt = ARRAY_SIZE(prog_ids);
	p.prog_ids = prog_ids;

	ret = bpf_prog_query_opts(cgroup_fd, type, &p);
	if (ret)
		return ret;

	if (p.prog_cnt == 0)
		return 0;

	for (iter = 0; iter < p.prog_cnt; iter++)
		show_bpf_prog(prog_ids[iter], type, NULL, level);

	return 0;
}

static int show_attached_bpf_progs(int cgroup_fd, enum bpf_attach_type type,
				   int level)
{
	LIBBPF_OPTS(bpf_prog_query_opts, p);
	__u32 prog_attach_flags[1024] = {0};
	const char *attach_flags_str;
	__u32 prog_ids[1024] = {0};
	char buf[32];
	__u32 iter;
	int ret;

	p.query_flags = query_flags;
	p.prog_cnt = ARRAY_SIZE(prog_ids);
	p.prog_ids = prog_ids;
	p.prog_attach_flags = prog_attach_flags;

	ret = bpf_prog_query_opts(cgroup_fd, type, &p);
	if (ret)
		return ret;

	if (p.prog_cnt == 0)
		return 0;

	for (iter = 0; iter < p.prog_cnt; iter++) {
		__u32 attach_flags;

		attach_flags = prog_attach_flags[iter] ?: p.attach_flags;

		switch (attach_flags) {
		case BPF_F_ALLOW_MULTI:
			attach_flags_str = "multi";
			break;
		case BPF_F_ALLOW_OVERRIDE:
			attach_flags_str = "override";
			break;
		case 0:
			attach_flags_str = "";
			break;
		default:
			snprintf(buf, sizeof(buf), "unknown(%x)", attach_flags);
			attach_flags_str = buf;
		}

		show_bpf_prog(prog_ids[iter], type,
			      attach_flags_str, level);
	}

	return 0;
}

static int show_bpf_progs(int cgroup_fd, enum bpf_attach_type type,
			  int level)
{
	return query_flags & BPF_F_QUERY_EFFECTIVE ?
	       show_effective_bpf_progs(cgroup_fd, type, level) :
	       show_attached_bpf_progs(cgroup_fd, type, level);
}

static int do_show(int argc, char **argv)
{
	enum bpf_attach_type type;
	int has_attached_progs;
	const char *path;
	int cgroup_fd;
	int ret = -1;

	query_flags = 0;

	if (!REQ_ARGS(1))
		return -1;
	path = GET_ARG();

	while (argc) {
		if (is_prefix(*argv, "effective")) {
			if (query_flags & BPF_F_QUERY_EFFECTIVE) {
				p_err("duplicated argument: %s", *argv);
				return -1;
			}
			query_flags |= BPF_F_QUERY_EFFECTIVE;
			NEXT_ARG();
		} else {
			p_err("expected no more arguments, 'effective', got: '%s'?",
			      *argv);
			return -1;
		}
	}

	cgroup_fd = open(path, O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s", path);
		goto exit;
	}

	has_attached_progs = cgroup_has_attached_progs(cgroup_fd);
	if (has_attached_progs < 0) {
		p_err("can't query bpf programs attached to %s: %s",
		      path, strerror(errno));
		goto exit_cgroup;
	} else if (!has_attached_progs) {
		ret = 0;
		goto exit_cgroup;
	}

	if (json_output)
		jsonw_start_array(json_wtr);
	else if (query_flags & BPF_F_QUERY_EFFECTIVE)
		printf("%-8s %-15s %-15s\n", "ID", "AttachType", "Name");
	else
		printf("%-8s %-15s %-15s %-15s\n", "ID", "AttachType",
		       "AttachFlags", "Name");

	btf_vmlinux = libbpf_find_kernel_btf();
	for (type = 0; type < __MAX_BPF_ATTACH_TYPE; type++) {
		/*
		 * Not all attach types may be supported, so it's expected,
		 * that some requests will fail.
		 * If we were able to get the show for at least one
		 * attach type, let's return 0.
		 */
		if (show_bpf_progs(cgroup_fd, type, 0) == 0)
			ret = 0;
	}

	if (json_output)
		jsonw_end_array(json_wtr);

exit_cgroup:
	close(cgroup_fd);
exit:
	return ret;
}

/*
 * To distinguish nftw() errors and do_show_tree_fn() errors
 * and avoid duplicating error messages, let's return -2
 * from do_show_tree_fn() in case of error.
 */
#define NFTW_ERR		-1
#define SHOW_TREE_FN_ERR	-2
static int do_show_tree_fn(const char *fpath, const struct stat *sb,
			   int typeflag, struct FTW *ftw)
{
	enum bpf_attach_type type;
	int has_attached_progs;
	int cgroup_fd;

	if (typeflag != FTW_D)
		return 0;

	cgroup_fd = open(fpath, O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s: %s", fpath, strerror(errno));
		return SHOW_TREE_FN_ERR;
	}

	has_attached_progs = cgroup_has_attached_progs(cgroup_fd);
	if (has_attached_progs < 0) {
		p_err("can't query bpf programs attached to %s: %s",
		      fpath, strerror(errno));
		close(cgroup_fd);
		return SHOW_TREE_FN_ERR;
	} else if (!has_attached_progs) {
		close(cgroup_fd);
		return 0;
	}

	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_string_field(json_wtr, "cgroup", fpath);
		jsonw_name(json_wtr, "programs");
		jsonw_start_array(json_wtr);
	} else {
		printf("%s\n", fpath);
	}

	btf_vmlinux = libbpf_find_kernel_btf();
	for (type = 0; type < __MAX_BPF_ATTACH_TYPE; type++)
		show_bpf_progs(cgroup_fd, type, ftw->level);

	if (errno == EINVAL)
		/* Last attach type does not support query.
		 * Do not report an error for this, especially because batch
		 * mode would stop processing commands.
		 */
		errno = 0;

	if (json_output) {
		jsonw_end_array(json_wtr);
		jsonw_end_object(json_wtr);
	}

	close(cgroup_fd);

	return 0;
}

static char *find_cgroup_root(void)
{
	struct mntent *mnt;
	FILE *f;

	f = fopen("/proc/mounts", "r");
	if (f == NULL)
		return NULL;

	while ((mnt = getmntent(f))) {
		if (strcmp(mnt->mnt_type, "cgroup2") == 0) {
			fclose(f);
			return strdup(mnt->mnt_dir);
		}
	}

	fclose(f);
	return NULL;
}

static int do_show_tree(int argc, char **argv)
{
	char *cgroup_root, *cgroup_alloced = NULL;
	int ret;

	query_flags = 0;

	if (!argc) {
		cgroup_alloced = find_cgroup_root();
		if (!cgroup_alloced) {
			p_err("cgroup v2 isn't mounted");
			return -1;
		}
		cgroup_root = cgroup_alloced;
	} else {
		cgroup_root = GET_ARG();

		while (argc) {
			if (is_prefix(*argv, "effective")) {
				if (query_flags & BPF_F_QUERY_EFFECTIVE) {
					p_err("duplicated argument: %s", *argv);
					return -1;
				}
				query_flags |= BPF_F_QUERY_EFFECTIVE;
				NEXT_ARG();
			} else {
				p_err("expected no more arguments, 'effective', got: '%s'?",
				      *argv);
				return -1;
			}
		}
	}

	if (json_output)
		jsonw_start_array(json_wtr);
	else if (query_flags & BPF_F_QUERY_EFFECTIVE)
		printf("%s\n"
		       "%-8s %-15s %-15s\n",
		       "CgroupPath",
		       "ID", "AttachType", "Name");
	else
		printf("%s\n"
		       "%-8s %-15s %-15s %-15s\n",
		       "CgroupPath",
		       "ID", "AttachType", "AttachFlags", "Name");

	switch (nftw(cgroup_root, do_show_tree_fn, 1024, FTW_MOUNT)) {
	case NFTW_ERR:
		p_err("can't iterate over %s: %s", cgroup_root,
		      strerror(errno));
		ret = -1;
		break;
	case SHOW_TREE_FN_ERR:
		ret = -1;
		break;
	default:
		ret = 0;
	}

	if (json_output)
		jsonw_end_array(json_wtr);

	free(cgroup_alloced);

	return ret;
}

static int do_attach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int cgroup_fd, prog_fd;
	int attach_flags = 0;
	int ret = -1;
	int i;

	if (argc < 4) {
		p_err("too few parameters for cgroup attach");
		goto exit;
	}

	cgroup_fd = open(argv[0], O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s", argv[0]);
		goto exit;
	}

	attach_type = parse_attach_type(argv[1]);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type");
		goto exit_cgroup;
	}

	argc -= 2;
	argv = &argv[2];
	prog_fd = prog_parse_fd(&argc, &argv);
	if (prog_fd < 0)
		goto exit_cgroup;

	for (i = 0; i < argc; i++) {
		if (is_prefix(argv[i], "multi")) {
			attach_flags |= BPF_F_ALLOW_MULTI;
		} else if (is_prefix(argv[i], "override")) {
			attach_flags |= BPF_F_ALLOW_OVERRIDE;
		} else {
			p_err("unknown option: %s", argv[i]);
			goto exit_cgroup;
		}
	}

	if (bpf_prog_attach(prog_fd, cgroup_fd, attach_type, attach_flags)) {
		p_err("failed to attach program");
		goto exit_prog;
	}

	if (json_output)
		jsonw_null(json_wtr);

	ret = 0;

exit_prog:
	close(prog_fd);
exit_cgroup:
	close(cgroup_fd);
exit:
	return ret;
}

static int do_detach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int prog_fd, cgroup_fd;
	int ret = -1;

	if (argc < 4) {
		p_err("too few parameters for cgroup detach");
		goto exit;
	}

	cgroup_fd = open(argv[0], O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s", argv[0]);
		goto exit;
	}

	attach_type = parse_attach_type(argv[1]);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type");
		goto exit_cgroup;
	}

	argc -= 2;
	argv = &argv[2];
	prog_fd = prog_parse_fd(&argc, &argv);
	if (prog_fd < 0)
		goto exit_cgroup;

	if (bpf_prog_detach2(prog_fd, cgroup_fd, attach_type)) {
		p_err("failed to detach program");
		goto exit_prog;
	}

	if (json_output)
		jsonw_null(json_wtr);

	ret = 0;

exit_prog:
	close(prog_fd);
exit_cgroup:
	close(cgroup_fd);
exit:
	return ret;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s { show | list } CGROUP [**effective**]\n"
		"       %1$s %2$s tree [CGROUP_ROOT] [**effective**]\n"
		"       %1$s %2$s attach CGROUP ATTACH_TYPE PROG [ATTACH_FLAGS]\n"
		"       %1$s %2$s detach CGROUP ATTACH_TYPE PROG\n"
		"       %1$s %2$s help\n"
		"\n"
		HELP_SPEC_ATTACH_TYPES "\n"
		"       " HELP_SPEC_ATTACH_FLAGS "\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       " HELP_SPEC_OPTIONS " |\n"
		"                    {-f|--bpffs} }\n"
		"",
		bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "tree",       do_show_tree },
	{ "attach",	do_attach },
	{ "detach",	do_detach },
	{ "help",	do_help },
	{ 0 }
};

int do_cgroup(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
