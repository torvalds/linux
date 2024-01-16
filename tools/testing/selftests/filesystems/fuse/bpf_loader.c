// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include "test_fuse.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/xattr.h>

#include <linux/unistd.h>

#include <include/uapi/linux/fuse.h>
#include <include/uapi/linux/bpf.h>

struct _test_options test_options;

struct s s(const char *s1)
{
	struct s s = {0};

	if (!s1)
		return s;

	s.s = malloc(strlen(s1) + 1);
	if (!s.s)
		return s;

	strcpy(s.s, s1);
	return s;
}

struct s sn(const char *s1, const char *s2)
{
	struct s s = {0};

	if (!s1)
		return s;

	s.s = malloc(s2 - s1 + 1);
	if (!s.s)
		return s;

	strncpy(s.s, s1, s2 - s1);
	s.s[s2 - s1] = 0;
	return s;
}

int s_cmp(struct s s1, struct s s2)
{
	int result = -1;

	if (!s1.s || !s2.s)
		goto out;
	result = strcmp(s1.s, s2.s);
out:
	free(s1.s);
	free(s2.s);
	return result;
}

struct s s_cat(struct s s1, struct s s2)
{
	struct s s = {0};

	if (!s1.s || !s2.s)
		goto out;

	s.s = malloc(strlen(s1.s) + strlen(s2.s) + 1);
	if (!s.s)
		goto out;

	strcpy(s.s, s1.s);
	strcat(s.s, s2.s);
out:
	free(s1.s);
	free(s2.s);
	return s;
}

struct s s_splitleft(struct s s1, char c)
{
	struct s s = {0};
	char *split;

	if (!s1.s)
		return s;

	split = strchr(s1.s, c);
	if (split)
		s = sn(s1.s, split);

	free(s1.s);
	return s;
}

struct s s_splitright(struct s s1, char c)
{
	struct s s2 = {0};
	char *split;

	if (!s1.s)
		return s2;

	split = strchr(s1.s, c);
	if (split)
		s2 = s(split + 1);

	free(s1.s);
	return s2;
}

struct s s_word(struct s s1, char c, size_t n)
{
	while (n--)
		s1 = s_splitright(s1, c);
	return s_splitleft(s1, c);
}

struct s s_path(struct s s1, struct s s2)
{
	return s_cat(s_cat(s1, s("/")), s2);
}

struct s s_pathn(size_t n, struct s s1, ...)
{
	va_list argp;

	va_start(argp, s1);
	while (--n)
		s1 = s_path(s1, va_arg(argp, struct s));
	va_end(argp);
	return s1;
}

int s_link(struct s src_pathname, struct s dst_pathname)
{
	int res;

	if (src_pathname.s && dst_pathname.s) {
		res = link(src_pathname.s, dst_pathname.s);
	} else {
		res = -1;
		errno = ENOMEM;
	}

	free(src_pathname.s);
	free(dst_pathname.s);
	return res;
}

int s_symlink(struct s src_pathname, struct s dst_pathname)
{
	int res;

	if (src_pathname.s && dst_pathname.s) {
		res = symlink(src_pathname.s, dst_pathname.s);
	} else {
		res = -1;
		errno = ENOMEM;
	}

	free(src_pathname.s);
	free(dst_pathname.s);
	return res;
}


int s_mkdir(struct s pathname, mode_t mode)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = mkdir(pathname.s, mode);
	free(pathname.s);
	return res;
}

int s_rmdir(struct s pathname)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = rmdir(pathname.s);
	free(pathname.s);
	return res;
}

int s_unlink(struct s pathname)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = unlink(pathname.s);
	free(pathname.s);
	return res;
}

int s_open(struct s pathname, int flags, ...)
{
	va_list ap;
	int res;

	va_start(ap, flags);
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	if (flags & (O_CREAT | O_TMPFILE))
		res = open(pathname.s, flags, va_arg(ap, mode_t));
	else
		res = open(pathname.s, flags);

	free(pathname.s);
	va_end(ap);
	return res;
}

int s_openat(int dirfd, struct s pathname, int flags, ...)
{
	va_list ap;
	int res;

	va_start(ap, flags);
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	if (flags & (O_CREAT | O_TMPFILE))
		res = openat(dirfd, pathname.s, flags, va_arg(ap, mode_t));
	else
		res = openat(dirfd, pathname.s, flags);

	free(pathname.s);
	va_end(ap);
	return res;
}

int s_creat(struct s pathname, mode_t mode)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = open(pathname.s, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	free(pathname.s);
	return res;
}

int s_mkfifo(struct s pathname, mode_t mode)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = mknod(pathname.s, S_IFIFO | mode, 0);
	free(pathname.s);
	return res;
}

int s_stat(struct s pathname, struct stat *st)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = stat(pathname.s, st);
	free(pathname.s);
	return res;
}

int s_statfs(struct s pathname, struct statfs *st)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = statfs(pathname.s, st);
	free(pathname.s);
	return res;
}

DIR *s_opendir(struct s pathname)
{
	DIR *res;

	res = opendir(pathname.s);
	free(pathname.s);
	return res;
}

int s_getxattr(struct s pathname, const char name[], void *value, size_t size,
	       ssize_t *ret_size)
{
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	*ret_size = getxattr(pathname.s, name, value, size);
	free(pathname.s);
	return *ret_size >= 0 ? 0 : -1;
}

int s_listxattr(struct s pathname, void *list, size_t size, ssize_t *ret_size)
{
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	*ret_size = listxattr(pathname.s, list, size);
	free(pathname.s);
	return *ret_size >= 0 ? 0 : -1;
}

int s_setxattr(struct s pathname, const char name[], const void *value, size_t size, int flags)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = setxattr(pathname.s, name, value, size, flags);
	free(pathname.s);
	return res;
}

int s_removexattr(struct s pathname, const char name[])
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = removexattr(pathname.s, name);
	free(pathname.s);
	return res;
}

int s_rename(struct s oldpathname, struct s newpathname)
{
	int res;

	if (!oldpathname.s || !newpathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = rename(oldpathname.s, newpathname.s);
	free(oldpathname.s);
	free(newpathname.s);
	return res;
}

int s_mount(struct s source, struct s target, struct s filesystem,
	    unsigned long mountflags, struct s data)
{
	int res;

	res = mount(source.s, target.s, filesystem.s, mountflags, data.s);
	free(source.s);
	free(target.s);
	free(filesystem.s);
	free(data.s);

	return res;
}

int s_umount(struct s target)
{
	int res;

	res = umount(target.s);
	free(target.s);
	return res;
}

int s_fuse_attr(struct s pathname, struct fuse_attr *fuse_attr_out)
{

	struct stat st;
	int result = TEST_FAILURE;

	TESTSYSCALL(s_stat(pathname, &st));

	fuse_attr_out->ino = st.st_ino;
	fuse_attr_out->mode = st.st_mode;
	fuse_attr_out->nlink = st.st_nlink;
	fuse_attr_out->uid = st.st_uid;
	fuse_attr_out->gid = st.st_gid;
	fuse_attr_out->rdev = st.st_rdev;
	fuse_attr_out->size = st.st_size;
	fuse_attr_out->blksize = st.st_blksize;
	fuse_attr_out->blocks = st.st_blocks;
	fuse_attr_out->atime = st.st_atime;
	fuse_attr_out->mtime = st.st_mtime;
	fuse_attr_out->ctime = st.st_ctime;
	fuse_attr_out->atimensec = UINT32_MAX;
	fuse_attr_out->mtimensec = UINT32_MAX;
	fuse_attr_out->ctimensec = UINT32_MAX;

	result = TEST_SUCCESS;
out:
	return result;
}

struct s tracing_folder(void)
{
	struct s trace = {0};
	FILE *mounts = NULL;
	char *line = NULL;
	size_t size = 0;

	TEST(mounts = fopen("/proc/mounts", "re"), mounts);
	while (getline(&line, &size, mounts) != -1) {
		if (!s_cmp(s_word(sn(line, line + size), ' ', 2),
			   s("tracefs"))) {
			trace = s_word(sn(line, line + size), ' ', 1);
			break;
		}

		if (!s_cmp(s_word(sn(line, line + size), ' ', 2), s("debugfs")))
			trace = s_path(s_word(sn(line, line + size), ' ', 1),
				       s("tracing"));
	}

out:
	free(line);
	fclose(mounts);
	return trace;
}

int tracing_on(void)
{
	int result = TEST_FAILURE;
	int tracing_on = -1;

	TEST(tracing_on = s_open(s_path(tracing_folder(), s("tracing_on")),
				 O_WRONLY | O_CLOEXEC),
	     tracing_on != -1);
	TESTEQUAL(write(tracing_on, "1", 1), 1);
	result = TEST_SUCCESS;
out:
	close(tracing_on);
	return result;
}

char *concat_file_name(const char *dir, const char *file)
{
	char full_name[FILENAME_MAX] = "";

	if (snprintf(full_name, ARRAY_SIZE(full_name), "%s/%s", dir, file) < 0)
		return NULL;
	return strdup(full_name);
}

char *setup_mount_dir(const char *name)
{
	struct stat st;
	char *current_dir = getcwd(NULL, 0);
	char *mount_dir = concat_file_name(current_dir, name);

	free(current_dir);
	if (stat(mount_dir, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return mount_dir;

		ksft_print_msg("%s is a file, not a dir.\n", mount_dir);
		return NULL;
	}

	if (mkdir(mount_dir, 0777)) {
		ksft_print_msg("Can't create mount dir.");
		return NULL;
	}

	return mount_dir;
}

int delete_dir_tree(const char *dir_path, bool remove_root)
{
	DIR *dir = NULL;
	struct dirent *dp;
	int result = 0;

	dir = opendir(dir_path);
	if (!dir) {
		result = -errno;
		goto out;
	}

	while ((dp = readdir(dir))) {
		char *full_path;

		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		full_path = concat_file_name(dir_path, dp->d_name);
		if (dp->d_type == DT_DIR)
			result = delete_dir_tree(full_path, true);
		else
			result = unlink(full_path);
		free(full_path);
		if (result)
			goto out;
	}

out:
	if (dir)
		closedir(dir);
	if (!result && remove_root)
		rmdir(dir_path);
	return result;
}

static int mount_fuse_maybe_init(const char *mount_dir, int bpf_fd, int dir_fd,
			     int *fuse_dev_ptr, bool init)
{
	int result = TEST_FAILURE;
	int fuse_dev = -1;
	char options[FILENAME_MAX];
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER];

	DECL_FUSE_IN(init);

	TEST(fuse_dev = open("/dev/fuse", O_RDWR | O_CLOEXEC), fuse_dev != -1);
	snprintf(options, FILENAME_MAX, "fd=%d,user_id=0,group_id=0,rootmode=0040000",
		 fuse_dev);
	if (bpf_fd != -1)
		snprintf(options + strlen(options),
			 sizeof(options) - strlen(options),
			 ",root_bpf=%d", bpf_fd);
	if (dir_fd != -1)
		snprintf(options + strlen(options),
			 sizeof(options) - strlen(options),
			 ",root_dir=%d", dir_fd);
	TESTSYSCALL(mount("ABC", mount_dir, "fuse", 0, options));

	if (init) {
		TESTFUSEIN(FUSE_INIT, init_in);
		TESTEQUAL(init_in->major, FUSE_KERNEL_VERSION);
		TESTEQUAL(init_in->minor, FUSE_KERNEL_MINOR_VERSION);
		TESTFUSEOUT1(fuse_init_out, ((struct fuse_init_out) {
			.major = FUSE_KERNEL_VERSION,
			.minor = FUSE_KERNEL_MINOR_VERSION,
			.max_readahead = 4096,
			.flags = 0,
			.max_background = 0,
			.congestion_threshold = 0,
			.max_write = 4096,
			.time_gran = 1000,
			.max_pages = 12,
			.map_alignment = 4096,
		}));
	}

	if (fuse_dev_ptr)
		*fuse_dev_ptr = fuse_dev;
	else
		TESTSYSCALL(close(fuse_dev));
	fuse_dev = -1;
	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	return result;
}

int mount_fuse(const char *mount_dir, int bpf_fd, int dir_fd, int *fuse_dev_ptr)
{
	return mount_fuse_maybe_init(mount_dir, bpf_fd, dir_fd, fuse_dev_ptr,
				     true);
}

int mount_fuse_no_init(const char *mount_dir, int bpf_fd, int dir_fd,
		       int *fuse_dev_ptr)
{
	return mount_fuse_maybe_init(mount_dir, bpf_fd, dir_fd, fuse_dev_ptr,
				     false);
}

struct fuse_bpf_map {
	unsigned int map_type;
	size_t key_size;
	size_t value_size;
	unsigned int max_entries;
};

static int install_maps(Elf_Data *maps, int maps_index, Elf *elf,
			Elf_Data *symbols, int symbol_index,
			struct map_relocation **mr, size_t *map_count)
{
	int result = TEST_FAILURE;
	int i;
	GElf_Sym symbol;

	TESTNE((void *)symbols, NULL);

	for (i = 0; i < symbols->d_size / sizeof(symbol); ++i) {
		TESTNE((void *)gelf_getsym(symbols, i, &symbol), 0);
		if (symbol.st_shndx == maps_index) {
			struct fuse_bpf_map *map;
			union bpf_attr attr;
			int map_fd;

			map = (struct fuse_bpf_map *)
				((char *)maps->d_buf + symbol.st_value);

			attr = (union bpf_attr) {
				.map_type = map->map_type,
				.key_size = map->key_size,
				.value_size = map->value_size,
				.max_entries = map->max_entries,
			};

			TEST(*mr = realloc(*mr, ++*map_count *
					   sizeof(struct fuse_bpf_map)),
			     *mr);
			TEST(map_fd = syscall(__NR_bpf, BPF_MAP_CREATE,
					      &attr, sizeof(attr)),
			     map_fd != -1);
			(*mr)[*map_count - 1] = (struct map_relocation) {
				.name = strdup(elf_strptr(elf, symbol_index,
							  symbol.st_name)),
				.fd = map_fd,
				.value = symbol.st_value,
			};
		}
	}

	result = TEST_SUCCESS;
out:
	return result;
}

static inline int relocate_maps(GElf_Shdr *rel_header, Elf_Data *rel_data,
			 Elf_Data *prog_data, Elf_Data *symbol_data,
			 struct map_relocation *map_relocations,
			 size_t map_count)
{
	int result = TEST_FAILURE;
	int i;
	struct bpf_insn *insns = (struct bpf_insn *) prog_data->d_buf;

	for (i = 0; i < rel_header->sh_size / rel_header->sh_entsize; ++i) {
		GElf_Sym sym;
		GElf_Rel rel;
		unsigned int insn_idx;
		int map_idx;

		gelf_getrel(rel_data, i, &rel);
		insn_idx = rel.r_offset / sizeof(struct bpf_insn);
		insns[insn_idx].src_reg = BPF_PSEUDO_MAP_FD;

		gelf_getsym(symbol_data, GELF_R_SYM(rel.r_info), &sym);
		for (map_idx = 0; map_idx < map_count; map_idx++) {
			if (map_relocations[map_idx].value == sym.st_value) {
				insns[insn_idx].imm =
					map_relocations[map_idx].fd;
				break;
			}
		}
		TESTNE(map_idx, map_count);
	}

	result = TEST_SUCCESS;
out:
	return result;
}

int install_elf_bpf(const char *file, const char *section, int *fd,
		    struct map_relocation **map_relocations, size_t *map_count)
{
	int result = TEST_FAILURE;
	char path[PATH_MAX] = {};
	char *last_slash;
	int filter_fd = -1;
	union bpf_attr bpf_attr;
	static char log[1 << 20];
	Elf *elf = NULL;
	GElf_Ehdr ehdr;
	Elf_Data *data_prog = NULL, *data_maps = NULL, *data_symbols = NULL;
	int maps_index, symbol_index, prog_index;
	int i;
	int bpf_prog_type_fuse_fd = -1;
	char buffer[10] = {0};
	int bpf_prog_type_fuse;

	TESTNE(readlink("/proc/self/exe", path, PATH_MAX), -1);
	TEST(last_slash = strrchr(path, '/'), last_slash);
	strcpy(last_slash + 1, file);
	TEST(filter_fd = open(path, O_RDONLY | O_CLOEXEC), filter_fd != -1);
	TESTNE(elf_version(EV_CURRENT), EV_NONE);
	TEST(elf = elf_begin(filter_fd, ELF_C_READ, NULL), elf);
	TESTEQUAL((void *) gelf_getehdr(elf, &ehdr), &ehdr);
	for (i = 1; i < ehdr.e_shnum; i++) {
		char *shname;
		GElf_Shdr shdr;
		Elf_Scn *scn;

		TEST(scn = elf_getscn(elf, i), scn);
		TESTEQUAL((void *)gelf_getshdr(scn, &shdr), &shdr);
		TEST(shname = elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name),
		     shname);

		if (!strcmp(shname, "maps")) {
			TEST(data_maps = elf_getdata(scn, 0), data_maps);
			maps_index = i;
		} else if (shdr.sh_type == SHT_SYMTAB) {
			TEST(data_symbols = elf_getdata(scn, 0), data_symbols);
			symbol_index = shdr.sh_link;
		} else if (!strcmp(shname, section)) {
			TEST(data_prog = elf_getdata(scn, 0), data_prog);
			prog_index = i;
		}
	}
	TESTNE((void *) data_prog, NULL);

	if (data_maps)
		TESTEQUAL(install_maps(data_maps, maps_index, elf,
				       data_symbols, symbol_index,
				       map_relocations, map_count), 0);

	/* Now relocate maps */
	for (i = 1; i < ehdr.e_shnum; i++) {
		GElf_Shdr rel_header;
		Elf_Scn *scn;
		Elf_Data *rel_data;

		TEST(scn = elf_getscn(elf, i), scn);
		TESTEQUAL((void *)gelf_getshdr(scn, &rel_header),
			&rel_header);
		if (rel_header.sh_type != SHT_REL)
			continue;
		TEST(rel_data = elf_getdata(scn, 0), rel_data);

		if (rel_header.sh_info != prog_index)
			continue;
		TESTEQUAL(relocate_maps(&rel_header, rel_data,
					data_prog, data_symbols,
					*map_relocations, *map_count),
			  0);
	}

	TEST(bpf_prog_type_fuse_fd = open("/sys/fs/fuse/bpf_prog_type_fuse",
					  O_RDONLY | O_CLOEXEC),
	     bpf_prog_type_fuse_fd != -1);
	TESTGE(read(bpf_prog_type_fuse_fd, buffer, sizeof(buffer)), 1);
	TEST(bpf_prog_type_fuse = strtol(buffer, NULL, 10),
	     bpf_prog_type_fuse != 0);

	bpf_attr = (union bpf_attr) {
		.prog_type = bpf_prog_type_fuse,
		.insn_cnt = data_prog->d_size / 8,
		.insns = ptr_to_u64(data_prog->d_buf),
		.license = ptr_to_u64("GPL"),
		.log_buf = test_options.verbose ? ptr_to_u64(log) : 0,
		.log_size = test_options.verbose ? sizeof(log) : 0,
		.log_level = test_options.verbose ? 2 : 0,
	};
	*fd = syscall(__NR_bpf, BPF_PROG_LOAD, &bpf_attr, sizeof(bpf_attr));
	if (test_options.verbose)
		ksft_print_msg("%s\n", log);
	if (*fd == -1 && errno == ENOSPC)
		ksft_print_msg("bpf log size too small!\n");
	TESTNE(*fd, -1);

	result = TEST_SUCCESS;
out:
	close(filter_fd);
	close(bpf_prog_type_fuse_fd);
	return result;
}


