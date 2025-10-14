// SPDX-License-Identifier: GPL-2.0
#include <asm/bug.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_LIBBPF_SUPPORT
#include <bpf/libbpf.h>
#include "bpf-event.h"
#include "bpf-utils.h"
#endif
#include "compress.h"
#include "env.h"
#include "namespaces.h"
#include "path.h"
#include "map.h"
#include "symbol.h"
#include "srcline.h"
#include "dso.h"
#include "dsos.h"
#include "machine.h"
#include "auxtrace.h"
#include "util.h" /* O_CLOEXEC for older systems */
#include "debug.h"
#include "string2.h"
#include "vdso.h"
#include "annotate-data.h"

static const char * const debuglink_paths[] = {
	"%.0s%s",
	"%s/%s",
	"%s/.debug/%s",
	"/usr/lib/debug%s/%s"
};

void dso__set_nsinfo(struct dso *dso, struct nsinfo *nsi)
{
	nsinfo__put(RC_CHK_ACCESS(dso)->nsinfo);
	RC_CHK_ACCESS(dso)->nsinfo = nsi;
}

char dso__symtab_origin(const struct dso *dso)
{
	static const char origin[] = {
		[DSO_BINARY_TYPE__KALLSYMS]			= 'k',
		[DSO_BINARY_TYPE__VMLINUX]			= 'v',
		[DSO_BINARY_TYPE__JAVA_JIT]			= 'j',
		[DSO_BINARY_TYPE__DEBUGLINK]			= 'l',
		[DSO_BINARY_TYPE__BUILD_ID_CACHE]		= 'B',
		[DSO_BINARY_TYPE__BUILD_ID_CACHE_DEBUGINFO]	= 'D',
		[DSO_BINARY_TYPE__FEDORA_DEBUGINFO]		= 'f',
		[DSO_BINARY_TYPE__UBUNTU_DEBUGINFO]		= 'u',
		[DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO]	= 'x',
		[DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO]	= 'o',
		[DSO_BINARY_TYPE__BUILDID_DEBUGINFO]		= 'b',
		[DSO_BINARY_TYPE__SYSTEM_PATH_DSO]		= 'd',
		[DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE]		= 'K',
		[DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP]	= 'm',
		[DSO_BINARY_TYPE__GUEST_KALLSYMS]		= 'g',
		[DSO_BINARY_TYPE__GUEST_KMODULE]		= 'G',
		[DSO_BINARY_TYPE__GUEST_KMODULE_COMP]		= 'M',
		[DSO_BINARY_TYPE__GUEST_VMLINUX]		= 'V',
		[DSO_BINARY_TYPE__GNU_DEBUGDATA]		= 'n',
	};

	if (dso == NULL || dso__symtab_type(dso) == DSO_BINARY_TYPE__NOT_FOUND)
		return '!';
	return origin[dso__symtab_type(dso)];
}

bool dso__is_object_file(const struct dso *dso)
{
	switch (dso__binary_type(dso)) {
	case DSO_BINARY_TYPE__KALLSYMS:
	case DSO_BINARY_TYPE__GUEST_KALLSYMS:
	case DSO_BINARY_TYPE__JAVA_JIT:
	case DSO_BINARY_TYPE__BPF_PROG_INFO:
	case DSO_BINARY_TYPE__BPF_IMAGE:
	case DSO_BINARY_TYPE__OOL:
		return false;
	case DSO_BINARY_TYPE__VMLINUX:
	case DSO_BINARY_TYPE__GUEST_VMLINUX:
	case DSO_BINARY_TYPE__DEBUGLINK:
	case DSO_BINARY_TYPE__BUILD_ID_CACHE:
	case DSO_BINARY_TYPE__BUILD_ID_CACHE_DEBUGINFO:
	case DSO_BINARY_TYPE__FEDORA_DEBUGINFO:
	case DSO_BINARY_TYPE__UBUNTU_DEBUGINFO:
	case DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO:
	case DSO_BINARY_TYPE__BUILDID_DEBUGINFO:
	case DSO_BINARY_TYPE__GNU_DEBUGDATA:
	case DSO_BINARY_TYPE__SYSTEM_PATH_DSO:
	case DSO_BINARY_TYPE__GUEST_KMODULE:
	case DSO_BINARY_TYPE__GUEST_KMODULE_COMP:
	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE:
	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP:
	case DSO_BINARY_TYPE__KCORE:
	case DSO_BINARY_TYPE__GUEST_KCORE:
	case DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO:
	case DSO_BINARY_TYPE__NOT_FOUND:
	default:
		return true;
	}
}

int dso__read_binary_type_filename(const struct dso *dso,
				   enum dso_binary_type type,
				   char *root_dir, char *filename, size_t size)
{
	char build_id_hex[SBUILD_ID_SIZE];
	int ret = 0;
	size_t len;

	switch (type) {
	case DSO_BINARY_TYPE__DEBUGLINK:
	{
		const char *last_slash;
		char dso_dir[PATH_MAX];
		char symfile[PATH_MAX];
		unsigned int i;

		len = __symbol__join_symfs(filename, size, dso__long_name(dso));
		last_slash = filename + len;
		while (last_slash != filename && *last_slash != '/')
			last_slash--;

		strncpy(dso_dir, filename, last_slash - filename);
		dso_dir[last_slash-filename] = '\0';

		if (!is_regular_file(filename)) {
			ret = -1;
			break;
		}

		ret = filename__read_debuglink(filename, symfile, PATH_MAX);
		if (ret)
			break;

		/* Check predefined locations where debug file might reside */
		ret = -1;
		for (i = 0; i < ARRAY_SIZE(debuglink_paths); i++) {
			snprintf(filename, size,
					debuglink_paths[i], dso_dir, symfile);
			if (is_regular_file(filename)) {
				ret = 0;
				break;
			}
		}

		break;
	}
	case DSO_BINARY_TYPE__BUILD_ID_CACHE:
		if (dso__build_id_filename(dso, filename, size, false) == NULL)
			ret = -1;
		break;

	case DSO_BINARY_TYPE__BUILD_ID_CACHE_DEBUGINFO:
		if (dso__build_id_filename(dso, filename, size, true) == NULL)
			ret = -1;
		break;

	case DSO_BINARY_TYPE__FEDORA_DEBUGINFO:
		len = __symbol__join_symfs(filename, size, "/usr/lib/debug");
		snprintf(filename + len, size - len, "%s.debug", dso__long_name(dso));
		break;

	case DSO_BINARY_TYPE__UBUNTU_DEBUGINFO:
		len = __symbol__join_symfs(filename, size, "/usr/lib/debug");
		snprintf(filename + len, size - len, "%s", dso__long_name(dso));
		break;

	case DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO:
		/*
		 * Ubuntu can mixup /usr/lib with /lib, putting debuginfo in
		 * /usr/lib/debug/lib when it is expected to be in
		 * /usr/lib/debug/usr/lib
		 */
		if (strlen(dso__long_name(dso)) < 9 ||
		    strncmp(dso__long_name(dso), "/usr/lib/", 9)) {
			ret = -1;
			break;
		}
		len = __symbol__join_symfs(filename, size, "/usr/lib/debug");
		snprintf(filename + len, size - len, "%s", dso__long_name(dso) + 4);
		break;

	case DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO:
	{
		const char *last_slash;
		size_t dir_size;

		last_slash = dso__long_name(dso) + dso__long_name_len(dso);
		while (last_slash != dso__long_name(dso) && *last_slash != '/')
			last_slash--;

		len = __symbol__join_symfs(filename, size, "");
		dir_size = last_slash - dso__long_name(dso) + 2;
		if (dir_size > (size - len)) {
			ret = -1;
			break;
		}
		len += scnprintf(filename + len, dir_size, "%s",  dso__long_name(dso));
		len += scnprintf(filename + len , size - len, ".debug%s",
								last_slash);
		break;
	}

	case DSO_BINARY_TYPE__BUILDID_DEBUGINFO:
		if (!dso__has_build_id(dso)) {
			ret = -1;
			break;
		}

		build_id__snprintf(dso__bid(dso), build_id_hex, sizeof(build_id_hex));
		len = __symbol__join_symfs(filename, size, "/usr/lib/debug/.build-id/");
		snprintf(filename + len, size - len, "%.2s/%s.debug",
			 build_id_hex, build_id_hex + 2);
		break;

	case DSO_BINARY_TYPE__VMLINUX:
	case DSO_BINARY_TYPE__GUEST_VMLINUX:
	case DSO_BINARY_TYPE__SYSTEM_PATH_DSO:
	case DSO_BINARY_TYPE__GNU_DEBUGDATA:
		__symbol__join_symfs(filename, size, dso__long_name(dso));
		break;

	case DSO_BINARY_TYPE__GUEST_KMODULE:
	case DSO_BINARY_TYPE__GUEST_KMODULE_COMP:
		path__join3(filename, size, symbol_conf.symfs,
			    root_dir, dso__long_name(dso));
		break;

	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE:
	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP:
		__symbol__join_symfs(filename, size, dso__long_name(dso));
		break;

	case DSO_BINARY_TYPE__KCORE:
	case DSO_BINARY_TYPE__GUEST_KCORE:
		snprintf(filename, size, "%s", dso__long_name(dso));
		break;

	default:
	case DSO_BINARY_TYPE__KALLSYMS:
	case DSO_BINARY_TYPE__GUEST_KALLSYMS:
	case DSO_BINARY_TYPE__JAVA_JIT:
	case DSO_BINARY_TYPE__BPF_PROG_INFO:
	case DSO_BINARY_TYPE__BPF_IMAGE:
	case DSO_BINARY_TYPE__OOL:
	case DSO_BINARY_TYPE__NOT_FOUND:
		ret = -1;
		break;
	}

	return ret;
}

enum {
	COMP_ID__NONE = 0,
};

static const struct {
	const char *fmt;
	int (*decompress)(const char *input, int output);
	bool (*is_compressed)(const char *input);
} compressions[] = {
	[COMP_ID__NONE] = { .fmt = NULL, },
#ifdef HAVE_ZLIB_SUPPORT
	{ "gz", gzip_decompress_to_file, gzip_is_compressed },
#endif
#ifdef HAVE_LZMA_SUPPORT
	{ "xz", lzma_decompress_to_file, lzma_is_compressed },
#endif
	{ NULL, NULL, NULL },
};

static int is_supported_compression(const char *ext)
{
	unsigned i;

	for (i = 1; compressions[i].fmt; i++) {
		if (!strcmp(ext, compressions[i].fmt))
			return i;
	}
	return COMP_ID__NONE;
}

bool is_kernel_module(const char *pathname, int cpumode)
{
	struct kmod_path m;
	int mode = cpumode & PERF_RECORD_MISC_CPUMODE_MASK;

	WARN_ONCE(mode != cpumode,
		  "Internal error: passing unmasked cpumode (%x) to is_kernel_module",
		  cpumode);

	switch (mode) {
	case PERF_RECORD_MISC_USER:
	case PERF_RECORD_MISC_HYPERVISOR:
	case PERF_RECORD_MISC_GUEST_USER:
		return false;
	/* Treat PERF_RECORD_MISC_CPUMODE_UNKNOWN as kernel */
	default:
		if (kmod_path__parse(&m, pathname)) {
			pr_err("Failed to check whether %s is a kernel module or not. Assume it is.",
					pathname);
			return true;
		}
	}

	return m.kmod;
}

bool dso__needs_decompress(struct dso *dso)
{
	return dso__symtab_type(dso) == DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP ||
		dso__symtab_type(dso) == DSO_BINARY_TYPE__GUEST_KMODULE_COMP;
}

int filename__decompress(const char *name, char *pathname,
			 size_t len, int comp, int *err)
{
	char tmpbuf[] = KMOD_DECOMP_NAME;
	int fd = -1;

	/*
	 * We have proper compression id for DSO and yet the file
	 * behind the 'name' can still be plain uncompressed object.
	 *
	 * The reason is behind the logic we open the DSO object files,
	 * when we try all possible 'debug' objects until we find the
	 * data. So even if the DSO is represented by 'krava.xz' module,
	 * we can end up here opening ~/.debug/....23432432/debug' file
	 * which is not compressed.
	 *
	 * To keep this transparent, we detect this and return the file
	 * descriptor to the uncompressed file.
	 */
	if (!compressions[comp].is_compressed(name))
		return open(name, O_RDONLY);

	fd = mkstemp(tmpbuf);
	if (fd < 0) {
		*err = errno;
		return -1;
	}

	if (compressions[comp].decompress(name, fd)) {
		*err = DSO_LOAD_ERRNO__DECOMPRESSION_FAILURE;
		close(fd);
		fd = -1;
	}

	if (!pathname || (fd < 0))
		unlink(tmpbuf);

	if (pathname && (fd >= 0))
		strlcpy(pathname, tmpbuf, len);

	return fd;
}

static int decompress_kmodule(struct dso *dso, const char *name,
			      char *pathname, size_t len)
{
	if (!dso__needs_decompress(dso))
		return -1;

	if (dso__comp(dso) == COMP_ID__NONE)
		return -1;

	return filename__decompress(name, pathname, len, dso__comp(dso), dso__load_errno(dso));
}

int dso__decompress_kmodule_fd(struct dso *dso, const char *name)
{
	return decompress_kmodule(dso, name, NULL, 0);
}

int dso__decompress_kmodule_path(struct dso *dso, const char *name,
				 char *pathname, size_t len)
{
	int fd = decompress_kmodule(dso, name, pathname, len);

	close(fd);
	return fd >= 0 ? 0 : -1;
}

/*
 * Parses kernel module specified in @path and updates
 * @m argument like:
 *
 *    @comp - true if @path contains supported compression suffix,
 *            false otherwise
 *    @kmod - true if @path contains '.ko' suffix in right position,
 *            false otherwise
 *    @name - if (@alloc_name && @kmod) is true, it contains strdup-ed base name
 *            of the kernel module without suffixes, otherwise strudup-ed
 *            base name of @path
 *    @ext  - if (@alloc_ext && @comp) is true, it contains strdup-ed string
 *            the compression suffix
 *
 * Returns 0 if there's no strdup error, -ENOMEM otherwise.
 */
int __kmod_path__parse(struct kmod_path *m, const char *path,
		       bool alloc_name)
{
	const char *name = strrchr(path, '/');
	const char *ext  = strrchr(path, '.');
	bool is_simple_name = false;

	memset(m, 0x0, sizeof(*m));
	name = name ? name + 1 : path;

	/*
	 * '.' is also a valid character for module name. For example:
	 * [aaa.bbb] is a valid module name. '[' should have higher
	 * priority than '.ko' suffix.
	 *
	 * The kernel names are from machine__mmap_name. Such
	 * name should belong to kernel itself, not kernel module.
	 */
	if (name[0] == '[') {
		is_simple_name = true;
		if ((strncmp(name, "[kernel.kallsyms]", 17) == 0) ||
		    (strncmp(name, "[guest.kernel.kallsyms", 22) == 0) ||
		    (strncmp(name, "[vdso]", 6) == 0) ||
		    (strncmp(name, "[vdso32]", 8) == 0) ||
		    (strncmp(name, "[vdsox32]", 9) == 0) ||
		    (strncmp(name, "[vsyscall]", 10) == 0)) {
			m->kmod = false;

		} else
			m->kmod = true;
	}

	/* No extension, just return name. */
	if ((ext == NULL) || is_simple_name) {
		if (alloc_name) {
			m->name = strdup(name);
			return m->name ? 0 : -ENOMEM;
		}
		return 0;
	}

	m->comp = is_supported_compression(ext + 1);
	if (m->comp > COMP_ID__NONE)
		ext -= 3;

	/* Check .ko extension only if there's enough name left. */
	if (ext > name)
		m->kmod = !strncmp(ext, ".ko", 3);

	if (alloc_name) {
		if (m->kmod) {
			if (asprintf(&m->name, "[%.*s]", (int) (ext - name), name) == -1)
				return -ENOMEM;
		} else {
			if (asprintf(&m->name, "%s", name) == -1)
				return -ENOMEM;
		}

		strreplace(m->name, '-', '_');
	}

	return 0;
}

void dso__set_module_info(struct dso *dso, struct kmod_path *m,
			  struct machine *machine)
{
	if (machine__is_host(machine))
		dso__set_symtab_type(dso, DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE);
	else
		dso__set_symtab_type(dso, DSO_BINARY_TYPE__GUEST_KMODULE);

	/* _KMODULE_COMP should be next to _KMODULE */
	if (m->kmod && m->comp) {
		dso__set_symtab_type(dso, dso__symtab_type(dso) + 1);
		dso__set_comp(dso, m->comp);
	}

	dso__set_is_kmod(dso);
	dso__set_short_name(dso, strdup(m->name), true);
}

/*
 * Global list of open DSOs and the counter.
 */
struct mutex _dso__data_open_lock;
static LIST_HEAD(dso__data_open);
static long dso__data_open_cnt GUARDED_BY(_dso__data_open_lock);

static void dso__data_open_lock_init(void)
{
	mutex_init(&_dso__data_open_lock);
}

static struct mutex *dso__data_open_lock(void) LOCK_RETURNED(_dso__data_open_lock)
{
	static pthread_once_t data_open_lock_once = PTHREAD_ONCE_INIT;

	pthread_once(&data_open_lock_once, dso__data_open_lock_init);

	return &_dso__data_open_lock;
}

static void dso__list_add(struct dso *dso) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	list_add_tail(&dso__data(dso)->open_entry, &dso__data_open);
#ifdef REFCNT_CHECKING
	dso__data(dso)->dso = dso__get(dso);
#endif
	/* Assume the dso is part of dsos, hence the optional reference count above. */
	assert(dso__dsos(dso));
	dso__data_open_cnt++;
}

static void dso__list_del(struct dso *dso) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	list_del_init(&dso__data(dso)->open_entry);
#ifdef REFCNT_CHECKING
	mutex_unlock(dso__data_open_lock());
	dso__put(dso__data(dso)->dso);
	mutex_lock(dso__data_open_lock());
#endif
	WARN_ONCE(dso__data_open_cnt <= 0,
		  "DSO data fd counter out of bounds.");
	dso__data_open_cnt--;
}

static void close_first_dso(void);

static int do_open(char *name) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	int fd;
	char sbuf[STRERR_BUFSIZE];

	do {
		fd = open(name, O_RDONLY|O_CLOEXEC);
		if (fd >= 0)
			return fd;

		pr_debug("dso open failed: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		if (!dso__data_open_cnt || errno != EMFILE)
			break;

		close_first_dso();
	} while (1);

	return -1;
}

char *dso__filename_with_chroot(const struct dso *dso, const char *filename)
{
	return filename_with_chroot(nsinfo__pid(dso__nsinfo_const(dso)), filename);
}

static int __open_dso(struct dso *dso, struct machine *machine)
	EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	int fd = -EINVAL;
	char *root_dir = (char *)"";
	char *name = malloc(PATH_MAX);
	bool decomp = false;

	if (!name)
		return -ENOMEM;

	mutex_lock(dso__lock(dso));
	if (machine)
		root_dir = machine->root_dir;

	if (dso__read_binary_type_filename(dso, dso__binary_type(dso),
					    root_dir, name, PATH_MAX))
		goto out;

	if (!is_regular_file(name)) {
		char *new_name;

		if (errno != ENOENT || dso__nsinfo(dso) == NULL)
			goto out;

		new_name = dso__filename_with_chroot(dso, name);
		if (!new_name)
			goto out;

		free(name);
		name = new_name;
	}

	if (dso__needs_decompress(dso)) {
		char newpath[KMOD_DECOMP_LEN];
		size_t len = sizeof(newpath);

		if (dso__decompress_kmodule_path(dso, name, newpath, len) < 0) {
			fd = -(*dso__load_errno(dso));
			goto out;
		}

		decomp = true;
		strcpy(name, newpath);
	}

	fd = do_open(name);

	if (decomp)
		unlink(name);

out:
	mutex_unlock(dso__lock(dso));
	free(name);
	return fd;
}

static void check_data_close(void);

/**
 * dso_close - Open DSO data file
 * @dso: dso object
 *
 * Open @dso's data file descriptor and updates
 * list/count of open DSO objects.
 */
static int open_dso(struct dso *dso, struct machine *machine)
	EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	int fd;
	struct nscookie nsc;

	if (dso__binary_type(dso) != DSO_BINARY_TYPE__BUILD_ID_CACHE) {
		mutex_lock(dso__lock(dso));
		nsinfo__mountns_enter(dso__nsinfo(dso), &nsc);
		mutex_unlock(dso__lock(dso));
	}
	fd = __open_dso(dso, machine);
	if (dso__binary_type(dso) != DSO_BINARY_TYPE__BUILD_ID_CACHE)
		nsinfo__mountns_exit(&nsc);

	if (fd >= 0) {
		dso__list_add(dso);
		/*
		 * Check if we crossed the allowed number
		 * of opened DSOs and close one if needed.
		 */
		check_data_close();
	}

	return fd;
}

static void close_data_fd(struct dso *dso) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	if (dso__data(dso)->fd >= 0) {
		close(dso__data(dso)->fd);
		dso__data(dso)->fd = -1;
		dso__data(dso)->file_size = 0;
		dso__list_del(dso);
	}
}

/**
 * dso_close - Close DSO data file
 * @dso: dso object
 *
 * Close @dso's data file descriptor and updates
 * list/count of open DSO objects.
 */
static void close_dso(struct dso *dso) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	close_data_fd(dso);
}

static void close_first_dso(void) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	struct dso_data *dso_data;
	struct dso *dso;

	dso_data = list_first_entry(&dso__data_open, struct dso_data, open_entry);
#ifdef REFCNT_CHECKING
	dso = dso_data->dso;
#else
	dso = container_of(dso_data, struct dso, data);
#endif
	close_dso(dso);
}

static rlim_t get_fd_limit(void)
{
	struct rlimit l;
	rlim_t limit = 0;

	/* Allow half of the current open fd limit. */
	if (getrlimit(RLIMIT_NOFILE, &l) == 0) {
		if (l.rlim_cur == RLIM_INFINITY)
			limit = l.rlim_cur;
		else
			limit = l.rlim_cur / 2;
	} else {
		pr_err("failed to get fd limit\n");
		limit = 1;
	}

	return limit;
}

static rlim_t fd_limit;

/*
 * Used only by tests/dso-data.c to reset the environment
 * for tests. I dont expect we should change this during
 * standard runtime.
 */
void reset_fd_limit(void)
{
	fd_limit = 0;
}

static bool may_cache_fd(void) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	if (!fd_limit)
		fd_limit = get_fd_limit();

	if (fd_limit == RLIM_INFINITY)
		return true;

	return fd_limit > (rlim_t) dso__data_open_cnt;
}

/*
 * Check and close LRU dso if we crossed allowed limit
 * for opened dso file descriptors. The limit is half
 * of the RLIMIT_NOFILE files opened.
*/
static void check_data_close(void) EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	bool cache_fd = may_cache_fd();

	if (!cache_fd)
		close_first_dso();
}

/**
 * dso__data_close - Close DSO data file
 * @dso: dso object
 *
 * External interface to close @dso's data file descriptor.
 */
void dso__data_close(struct dso *dso)
{
	mutex_lock(dso__data_open_lock());
	close_dso(dso);
	mutex_unlock(dso__data_open_lock());
}

static void try_to_open_dso(struct dso *dso, struct machine *machine)
	EXCLUSIVE_LOCKS_REQUIRED(_dso__data_open_lock)
{
	enum dso_binary_type binary_type_data[] = {
		DSO_BINARY_TYPE__BUILD_ID_CACHE,
		DSO_BINARY_TYPE__SYSTEM_PATH_DSO,
		DSO_BINARY_TYPE__NOT_FOUND,
	};
	int i = 0;
	struct dso_data *dso_data = dso__data(dso);

	if (dso_data->fd >= 0)
		return;

	if (dso__binary_type(dso) != DSO_BINARY_TYPE__NOT_FOUND) {
		dso_data->fd = open_dso(dso, machine);
		goto out;
	}

	do {
		dso__set_binary_type(dso, binary_type_data[i++]);

		dso_data->fd = open_dso(dso, machine);
		if (dso_data->fd >= 0)
			goto out;

	} while (dso__binary_type(dso) != DSO_BINARY_TYPE__NOT_FOUND);
out:
	if (dso_data->fd >= 0)
		dso_data->status = DSO_DATA_STATUS_OK;
	else
		dso_data->status = DSO_DATA_STATUS_ERROR;
}

/**
 * dso__data_get_fd - Get dso's data file descriptor
 * @dso: dso object
 * @machine: machine object
 *
 * External interface to find dso's file, open it and
 * returns file descriptor.  It should be paired with
 * dso__data_put_fd() if it returns non-negative value.
 */
bool dso__data_get_fd(struct dso *dso, struct machine *machine, int *fd)
{
	*fd = -1;
	if (dso__data(dso)->status == DSO_DATA_STATUS_ERROR)
		return false;

	mutex_lock(dso__data_open_lock());

	try_to_open_dso(dso, machine);

	*fd = dso__data(dso)->fd;
	if (*fd >= 0)
		return true;

	mutex_unlock(dso__data_open_lock());
	return false;
}

void dso__data_put_fd(struct dso *dso __maybe_unused)
{
	mutex_unlock(dso__data_open_lock());
}

bool dso__data_status_seen(struct dso *dso, enum dso_data_status_seen by)
{
	u32 flag = 1 << by;

	if (dso__data(dso)->status_seen & flag)
		return true;

	dso__data(dso)->status_seen |= flag;

	return false;
}

#ifdef HAVE_LIBBPF_SUPPORT
static ssize_t bpf_read(struct dso *dso, u64 offset, char *data)
{
	struct bpf_prog_info_node *node;
	ssize_t size = DSO__DATA_CACHE_SIZE;
	struct dso_bpf_prog *dso_bpf_prog = dso__bpf_prog(dso);
	u64 len;
	u8 *buf;

	node = perf_env__find_bpf_prog_info(dso_bpf_prog->env, dso_bpf_prog->id);
	if (!node || !node->info_linear) {
		dso__data(dso)->status = DSO_DATA_STATUS_ERROR;
		return -1;
	}

	len = node->info_linear->info.jited_prog_len;
	buf = (u8 *)(uintptr_t)node->info_linear->info.jited_prog_insns;

	if (offset >= len)
		return -1;

	size = (ssize_t)min(len - offset, (u64)size);
	memcpy(data, buf + offset, size);
	return size;
}

static int bpf_size(struct dso *dso)
{
	struct bpf_prog_info_node *node;
	struct dso_bpf_prog *dso_bpf_prog = dso__bpf_prog(dso);

	node = perf_env__find_bpf_prog_info(dso_bpf_prog->env, dso_bpf_prog->id);
	if (!node || !node->info_linear) {
		dso__data(dso)->status = DSO_DATA_STATUS_ERROR;
		return -1;
	}

	dso__data(dso)->file_size = node->info_linear->info.jited_prog_len;
	return 0;
}
#endif // HAVE_LIBBPF_SUPPORT

static void
dso_cache__free(struct dso *dso)
{
	struct rb_root *root = &dso__data(dso)->cache;
	struct rb_node *next = rb_first(root);

	mutex_lock(dso__lock(dso));
	while (next) {
		struct dso_cache *cache;

		cache = rb_entry(next, struct dso_cache, rb_node);
		next = rb_next(&cache->rb_node);
		rb_erase(&cache->rb_node, root);
		free(cache);
	}
	mutex_unlock(dso__lock(dso));
}

static struct dso_cache *__dso_cache__find(struct dso *dso, u64 offset)
{
	const struct rb_root *root = &dso__data(dso)->cache;
	struct rb_node * const *p = &root->rb_node;
	const struct rb_node *parent = NULL;
	struct dso_cache *cache;

	while (*p != NULL) {
		u64 end;

		parent = *p;
		cache = rb_entry(parent, struct dso_cache, rb_node);
		end = cache->offset + DSO__DATA_CACHE_SIZE;

		if (offset < cache->offset)
			p = &(*p)->rb_left;
		else if (offset >= end)
			p = &(*p)->rb_right;
		else
			return cache;
	}

	return NULL;
}

static struct dso_cache *
dso_cache__insert(struct dso *dso, struct dso_cache *new)
{
	struct rb_root *root = &dso__data(dso)->cache;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct dso_cache *cache;
	u64 offset = new->offset;

	mutex_lock(dso__lock(dso));
	while (*p != NULL) {
		u64 end;

		parent = *p;
		cache = rb_entry(parent, struct dso_cache, rb_node);
		end = cache->offset + DSO__DATA_CACHE_SIZE;

		if (offset < cache->offset)
			p = &(*p)->rb_left;
		else if (offset >= end)
			p = &(*p)->rb_right;
		else
			goto out;
	}

	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, root);

	cache = NULL;
out:
	mutex_unlock(dso__lock(dso));
	return cache;
}

static ssize_t dso_cache__memcpy(struct dso_cache *cache, u64 offset, u8 *data,
				 u64 size, bool out)
{
	u64 cache_offset = offset - cache->offset;
	u64 cache_size   = min(cache->size - cache_offset, size);

	if (out)
		memcpy(data, cache->data + cache_offset, cache_size);
	else
		memcpy(cache->data + cache_offset, data, cache_size);
	return cache_size;
}

static ssize_t file_read(struct dso *dso, struct machine *machine,
			 u64 offset, char *data)
{
	ssize_t ret;

	mutex_lock(dso__data_open_lock());

	/*
	 * dso__data(dso)->fd might be closed if other thread opened another
	 * file (dso) due to open file limit (RLIMIT_NOFILE).
	 */
	try_to_open_dso(dso, machine);

	if (dso__data(dso)->fd < 0) {
		dso__data(dso)->status = DSO_DATA_STATUS_ERROR;
		ret = -errno;
		goto out;
	}

	ret = pread(dso__data(dso)->fd, data, DSO__DATA_CACHE_SIZE, offset);
out:
	mutex_unlock(dso__data_open_lock());
	return ret;
}

static struct dso_cache *dso_cache__populate(struct dso *dso,
					     struct machine *machine,
					     u64 offset, ssize_t *ret)
{
	u64 cache_offset = offset & DSO__DATA_CACHE_MASK;
	struct dso_cache *cache;
	struct dso_cache *old;

	cache = zalloc(sizeof(*cache) + DSO__DATA_CACHE_SIZE);
	if (!cache) {
		*ret = -ENOMEM;
		return NULL;
	}
#ifdef HAVE_LIBBPF_SUPPORT
	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_PROG_INFO)
		*ret = bpf_read(dso, cache_offset, cache->data);
	else
#endif
	if (dso__binary_type(dso) == DSO_BINARY_TYPE__OOL)
		*ret = DSO__DATA_CACHE_SIZE;
	else
		*ret = file_read(dso, machine, cache_offset, cache->data);

	if (*ret <= 0) {
		free(cache);
		return NULL;
	}

	cache->offset = cache_offset;
	cache->size   = *ret;

	old = dso_cache__insert(dso, cache);
	if (old) {
		/* we lose the race */
		free(cache);
		cache = old;
	}

	return cache;
}

static struct dso_cache *dso_cache__find(struct dso *dso,
					 struct machine *machine,
					 u64 offset,
					 ssize_t *ret)
{
	struct dso_cache *cache = __dso_cache__find(dso, offset);

	return cache ? cache : dso_cache__populate(dso, machine, offset, ret);
}

static ssize_t dso_cache_io(struct dso *dso, struct machine *machine,
			    u64 offset, u8 *data, ssize_t size, bool out)
{
	struct dso_cache *cache;
	ssize_t ret = 0;

	cache = dso_cache__find(dso, machine, offset, &ret);
	if (!cache)
		return ret;

	return dso_cache__memcpy(cache, offset, data, size, out);
}

/*
 * Reads and caches dso data DSO__DATA_CACHE_SIZE size chunks
 * in the rb_tree. Any read to already cached data is served
 * by cached data. Writes update the cache only, not the backing file.
 */
static ssize_t cached_io(struct dso *dso, struct machine *machine,
			 u64 offset, u8 *data, ssize_t size, bool out)
{
	ssize_t r = 0;
	u8 *p = data;

	do {
		ssize_t ret;

		ret = dso_cache_io(dso, machine, offset, p, size, out);
		if (ret < 0)
			return ret;

		/* Reached EOF, return what we have. */
		if (!ret)
			break;

		BUG_ON(ret > size);

		r      += ret;
		p      += ret;
		offset += ret;
		size   -= ret;

	} while (size);

	return r;
}

static int file_size(struct dso *dso, struct machine *machine)
{
	int ret = 0;
	struct stat st;
	char sbuf[STRERR_BUFSIZE];

	mutex_lock(dso__data_open_lock());

	/*
	 * dso__data(dso)->fd might be closed if other thread opened another
	 * file (dso) due to open file limit (RLIMIT_NOFILE).
	 */
	try_to_open_dso(dso, machine);

	if (dso__data(dso)->fd < 0) {
		ret = -errno;
		dso__data(dso)->status = DSO_DATA_STATUS_ERROR;
		goto out;
	}

	if (fstat(dso__data(dso)->fd, &st) < 0) {
		ret = -errno;
		pr_err("dso cache fstat failed: %s\n",
		       str_error_r(errno, sbuf, sizeof(sbuf)));
		dso__data(dso)->status = DSO_DATA_STATUS_ERROR;
		goto out;
	}
	dso__data(dso)->file_size = st.st_size;

out:
	mutex_unlock(dso__data_open_lock());
	return ret;
}

int dso__data_file_size(struct dso *dso, struct machine *machine)
{
	if (dso__data(dso)->file_size)
		return 0;

	if (dso__data(dso)->status == DSO_DATA_STATUS_ERROR)
		return -1;
#ifdef HAVE_LIBBPF_SUPPORT
	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_PROG_INFO)
		return bpf_size(dso);
#endif
	return file_size(dso, machine);
}

/**
 * dso__data_size - Return dso data size
 * @dso: dso object
 * @machine: machine object
 *
 * Return: dso data size
 */
off_t dso__data_size(struct dso *dso, struct machine *machine)
{
	if (dso__data_file_size(dso, machine))
		return -1;

	/* For now just estimate dso data size is close to file size */
	return dso__data(dso)->file_size;
}

static ssize_t data_read_write_offset(struct dso *dso, struct machine *machine,
				      u64 offset, u8 *data, ssize_t size,
				      bool out)
{
	if (dso__data_file_size(dso, machine))
		return -1;

	/* Check the offset sanity. */
	if (offset > dso__data(dso)->file_size)
		return -1;

	if (offset + size < offset)
		return -1;

	return cached_io(dso, machine, offset, data, size, out);
}

/**
 * dso__data_read_offset - Read data from dso file offset
 * @dso: dso object
 * @machine: machine object
 * @offset: file offset
 * @data: buffer to store data
 * @size: size of the @data buffer
 *
 * External interface to read data from dso file offset. Open
 * dso data file and use cached_read to get the data.
 */
ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size)
{
	if (dso__data(dso)->status == DSO_DATA_STATUS_ERROR)
		return -1;

	return data_read_write_offset(dso, machine, offset, data, size, true);
}

uint16_t dso__e_machine(struct dso *dso, struct machine *machine)
{
	uint16_t e_machine = EM_NONE;
	int fd;

	switch (dso__binary_type(dso)) {
	case DSO_BINARY_TYPE__KALLSYMS:
	case DSO_BINARY_TYPE__GUEST_KALLSYMS:
	case DSO_BINARY_TYPE__VMLINUX:
	case DSO_BINARY_TYPE__GUEST_VMLINUX:
	case DSO_BINARY_TYPE__GUEST_KMODULE:
	case DSO_BINARY_TYPE__GUEST_KMODULE_COMP:
	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE:
	case DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP:
	case DSO_BINARY_TYPE__KCORE:
	case DSO_BINARY_TYPE__GUEST_KCORE:
	case DSO_BINARY_TYPE__BPF_PROG_INFO:
	case DSO_BINARY_TYPE__BPF_IMAGE:
	case DSO_BINARY_TYPE__OOL:
	case DSO_BINARY_TYPE__JAVA_JIT:
		return EM_HOST;
	case DSO_BINARY_TYPE__DEBUGLINK:
	case DSO_BINARY_TYPE__BUILD_ID_CACHE:
	case DSO_BINARY_TYPE__BUILD_ID_CACHE_DEBUGINFO:
	case DSO_BINARY_TYPE__GNU_DEBUGDATA:
	case DSO_BINARY_TYPE__SYSTEM_PATH_DSO:
	case DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO:
	case DSO_BINARY_TYPE__FEDORA_DEBUGINFO:
	case DSO_BINARY_TYPE__UBUNTU_DEBUGINFO:
	case DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO:
	case DSO_BINARY_TYPE__BUILDID_DEBUGINFO:
		break;
	case DSO_BINARY_TYPE__NOT_FOUND:
	default:
		return EM_NONE;
	}

	mutex_lock(dso__data_open_lock());

	/*
	 * dso__data(dso)->fd might be closed if other thread opened another
	 * file (dso) due to open file limit (RLIMIT_NOFILE).
	 */
	try_to_open_dso(dso, machine);
	fd = dso__data(dso)->fd;
	if (fd >= 0) {
		_Static_assert(offsetof(Elf32_Ehdr, e_machine) == 18, "Unexpected offset");
		_Static_assert(offsetof(Elf64_Ehdr, e_machine) == 18, "Unexpected offset");
		if (dso__needs_swap(dso) == DSO_SWAP__UNSET) {
			unsigned char eidata;

			if (pread(fd, &eidata, sizeof(eidata), EI_DATA) == sizeof(eidata))
				dso__swap_init(dso, eidata);
		}
		if (dso__needs_swap(dso) != DSO_SWAP__UNSET &&
		    pread(fd, &e_machine, sizeof(e_machine), 18) == sizeof(e_machine))
			e_machine = DSO__SWAP(dso, uint16_t, e_machine);
	}
	mutex_unlock(dso__data_open_lock());
	return e_machine;
}

/**
 * dso__data_read_addr - Read data from dso address
 * @dso: dso object
 * @machine: machine object
 * @add: virtual memory address
 * @data: buffer to store data
 * @size: size of the @data buffer
 *
 * External interface to read data from dso address.
 */
ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size)
{
	u64 offset = map__map_ip(map, addr);

	return dso__data_read_offset(dso, machine, offset, data, size);
}

/**
 * dso__data_write_cache_offs - Write data to dso data cache at file offset
 * @dso: dso object
 * @machine: machine object
 * @offset: file offset
 * @data: buffer to write
 * @size: size of the @data buffer
 *
 * Write into the dso file data cache, but do not change the file itself.
 */
ssize_t dso__data_write_cache_offs(struct dso *dso, struct machine *machine,
				   u64 offset, const u8 *data_in, ssize_t size)
{
	u8 *data = (u8 *)data_in; /* cast away const to use same fns for r/w */

	if (dso__data(dso)->status == DSO_DATA_STATUS_ERROR)
		return -1;

	return data_read_write_offset(dso, machine, offset, data, size, false);
}

/**
 * dso__data_write_cache_addr - Write data to dso data cache at dso address
 * @dso: dso object
 * @machine: machine object
 * @add: virtual memory address
 * @data: buffer to write
 * @size: size of the @data buffer
 *
 * External interface to write into the dso file data cache, but do not change
 * the file itself.
 */
ssize_t dso__data_write_cache_addr(struct dso *dso, struct map *map,
				   struct machine *machine, u64 addr,
				   const u8 *data, ssize_t size)
{
	u64 offset = map__map_ip(map, addr);

	return dso__data_write_cache_offs(dso, machine, offset, data, size);
}

struct map *dso__new_map(const char *name)
{
	struct map *map = NULL;
	struct dso *dso = dso__new(name);

	if (dso) {
		map = map__new2(0, dso);
		dso__put(dso);
	}

	return map;
}

struct dso *machine__findnew_kernel(struct machine *machine, const char *name,
				    const char *short_name, int dso_type)
{
	/*
	 * The kernel dso could be created by build_id processing.
	 */
	struct dso *dso = machine__findnew_dso(machine, name);

	/*
	 * We need to run this in all cases, since during the build_id
	 * processing we had no idea this was the kernel dso.
	 */
	if (dso != NULL) {
		dso__set_short_name(dso, short_name, false);
		dso__set_kernel(dso, dso_type);
	}

	return dso;
}

static void __dso__set_long_name_id(struct dso *dso, const char *name, bool name_allocated)
{
	if (dso__long_name_allocated(dso))
		free((char *)dso__long_name(dso));

	RC_CHK_ACCESS(dso)->long_name = name;
	RC_CHK_ACCESS(dso)->long_name_len = strlen(name);
	dso__set_long_name_allocated(dso, name_allocated);
}

static void dso__set_long_name_id(struct dso *dso, const char *name, bool name_allocated)
{
	struct dsos *dsos = dso__dsos(dso);

	if (name == NULL)
		return;

	if (dsos) {
		/*
		 * Need to avoid re-sorting the dsos breaking by non-atomically
		 * renaming the dso.
		 */
		down_write(&dsos->lock);
		__dso__set_long_name_id(dso, name, name_allocated);
		dsos->sorted = false;
		up_write(&dsos->lock);
	} else {
		__dso__set_long_name_id(dso, name, name_allocated);
	}
}

static int __dso_id__cmp(const struct dso_id *a, const struct dso_id *b)
{
	if (a->mmap2_valid && b->mmap2_valid) {
		if (a->maj > b->maj) return -1;
		if (a->maj < b->maj) return 1;

		if (a->min > b->min) return -1;
		if (a->min < b->min) return 1;

		if (a->ino > b->ino) return -1;
		if (a->ino < b->ino) return 1;
	}
	if (a->mmap2_ino_generation_valid && b->mmap2_ino_generation_valid) {
		if (a->ino_generation > b->ino_generation) return -1;
		if (a->ino_generation < b->ino_generation) return 1;
	}
	if (build_id__is_defined(&a->build_id) && build_id__is_defined(&b->build_id)) {
		if (a->build_id.size != b->build_id.size)
			return a->build_id.size < b->build_id.size ? -1 : 1;
		return memcmp(a->build_id.data, b->build_id.data, a->build_id.size);
	}
	return 0;
}

const struct dso_id dso_id_empty = {
	{
		.maj = 0,
		.min = 0,
		.ino = 0,
		.ino_generation = 0,
	},
	.mmap2_valid = false,
	.mmap2_ino_generation_valid = false,
	{
		.size = 0,
	}
};

void __dso__improve_id(struct dso *dso, const struct dso_id *id)
{
	struct dsos *dsos = dso__dsos(dso);
	struct dso_id *dso_id = dso__id(dso);
	bool changed = false;

	/* dsos write lock held by caller. */

	if (id->mmap2_valid && !dso_id->mmap2_valid) {
		dso_id->maj = id->maj;
		dso_id->min = id->min;
		dso_id->ino = id->ino;
		dso_id->mmap2_valid = true;
		changed = true;
	}
	if (id->mmap2_ino_generation_valid && !dso_id->mmap2_ino_generation_valid) {
		dso_id->ino_generation = id->ino_generation;
		dso_id->mmap2_ino_generation_valid = true;
		changed = true;
	}
	if (build_id__is_defined(&id->build_id) && !build_id__is_defined(&dso_id->build_id)) {
		dso_id->build_id = id->build_id;
		changed = true;
	}
	if (changed && dsos)
		dsos->sorted = false;
}

int dso_id__cmp(const struct dso_id *a, const struct dso_id *b)
{
	if (a == &dso_id_empty || b == &dso_id_empty) {
		/* There is no valid data to compare so the comparison always returns identical. */
		return 0;
	}

	return __dso_id__cmp(a, b);
}

int dso__cmp_id(struct dso *a, struct dso *b)
{
	return __dso_id__cmp(dso__id(a), dso__id(b));
}

void dso__set_long_name(struct dso *dso, const char *name, bool name_allocated)
{
	dso__set_long_name_id(dso, name, name_allocated);
}

static void __dso__set_short_name(struct dso *dso, const char *name, bool name_allocated)
{
	if (dso__short_name_allocated(dso))
		free((char *)dso__short_name(dso));

	RC_CHK_ACCESS(dso)->short_name		  = name;
	RC_CHK_ACCESS(dso)->short_name_len	  = strlen(name);
	dso__set_short_name_allocated(dso, name_allocated);
}

void dso__set_short_name(struct dso *dso, const char *name, bool name_allocated)
{
	struct dsos *dsos = dso__dsos(dso);

	if (name == NULL)
		return;

	if (dsos) {
		/*
		 * Need to avoid re-sorting the dsos breaking by non-atomically
		 * renaming the dso.
		 */
		down_write(&dsos->lock);
		__dso__set_short_name(dso, name, name_allocated);
		dsos->sorted = false;
		up_write(&dsos->lock);
	} else {
		__dso__set_short_name(dso, name, name_allocated);
	}
}

int dso__name_len(const struct dso *dso)
{
	if (!dso)
		return strlen("[unknown]");
	if (verbose > 0)
		return dso__long_name_len(dso);

	return dso__short_name_len(dso);
}

bool dso__loaded(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->loaded;
}

bool dso__sorted_by_name(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->sorted_by_name;
}

void dso__set_sorted_by_name(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->sorted_by_name = true;
}

struct dso *dso__new_id(const char *name, const struct dso_id *id)
{
	RC_STRUCT(dso) *dso = zalloc(sizeof(*dso) + strlen(name) + 1);
	struct dso *res;
	struct dso_data *data;

	if (!dso)
		return NULL;

	if (ADD_RC_CHK(res, dso)) {
		strcpy(dso->name, name);
		if (id)
			dso->id = *id;
		dso__set_long_name_id(res, dso->name, false);
		dso__set_short_name(res, dso->name, false);
		dso->symbols = RB_ROOT_CACHED;
		dso->symbol_names = NULL;
		dso->symbol_names_len = 0;
		dso->inlined_nodes = RB_ROOT_CACHED;
		dso->srclines = RB_ROOT_CACHED;
		dso->data_types = RB_ROOT;
		dso->global_vars = RB_ROOT;
		dso->data.fd = -1;
		dso->data.status = DSO_DATA_STATUS_UNKNOWN;
		dso->symtab_type = DSO_BINARY_TYPE__NOT_FOUND;
		dso->binary_type = DSO_BINARY_TYPE__NOT_FOUND;
		dso->is_64_bit = (sizeof(void *) == 8);
		dso->loaded = 0;
		dso->rel = 0;
		dso->sorted_by_name = 0;
		dso->has_srcline = 1;
		dso->a2l_fails = 1;
		dso->kernel = DSO_SPACE__USER;
		dso->is_kmod = 0;
		dso->needs_swap = DSO_SWAP__UNSET;
		dso->comp = COMP_ID__NONE;
		mutex_init(&dso->lock);
		refcount_set(&dso->refcnt, 1);
		data = &dso->data;
		data->cache = RB_ROOT;
		data->fd = -1;
		data->status = DSO_DATA_STATUS_UNKNOWN;
		INIT_LIST_HEAD(&data->open_entry);
#ifdef REFCNT_CHECKING
		data->dso = NULL; /* Set when on the open_entry list. */
#endif
	}
	return res;
}

struct dso *dso__new(const char *name)
{
	return dso__new_id(name, NULL);
}

void dso__delete(struct dso *dso)
{
	if (dso__dsos(dso))
		pr_err("DSO %s is still in rbtree when being deleted!\n", dso__long_name(dso));

	/* free inlines first, as they reference symbols */
	inlines__tree_delete(&RC_CHK_ACCESS(dso)->inlined_nodes);
	srcline__tree_delete(&RC_CHK_ACCESS(dso)->srclines);
	symbols__delete(&RC_CHK_ACCESS(dso)->symbols);
	RC_CHK_ACCESS(dso)->symbol_names_len = 0;
	zfree(&RC_CHK_ACCESS(dso)->symbol_names);
	annotated_data_type__tree_delete(dso__data_types(dso));
	global_var_type__tree_delete(dso__global_vars(dso));

	if (RC_CHK_ACCESS(dso)->short_name_allocated) {
		zfree((char **)&RC_CHK_ACCESS(dso)->short_name);
		RC_CHK_ACCESS(dso)->short_name_allocated = false;
	}

	if (RC_CHK_ACCESS(dso)->long_name_allocated) {
		zfree((char **)&RC_CHK_ACCESS(dso)->long_name);
		RC_CHK_ACCESS(dso)->long_name_allocated = false;
	}

	dso__data_close(dso);
	auxtrace_cache__free(RC_CHK_ACCESS(dso)->auxtrace_cache);
	dso_cache__free(dso);
	dso__free_a2l(dso);
	dso__free_symsrc_filename(dso);
	nsinfo__zput(RC_CHK_ACCESS(dso)->nsinfo);
	mutex_destroy(dso__lock(dso));
	RC_CHK_FREE(dso);
}

struct dso *dso__get(struct dso *dso)
{
	struct dso *result;

	if (RC_CHK_GET(result, dso))
		refcount_inc(&RC_CHK_ACCESS(dso)->refcnt);

	return result;
}

void dso__put(struct dso *dso)
{
#ifdef REFCNT_CHECKING
	if (dso && dso__data(dso) && refcount_read(&RC_CHK_ACCESS(dso)->refcnt) == 2)
		dso__data_close(dso);
#endif
	if (dso && refcount_dec_and_test(&RC_CHK_ACCESS(dso)->refcnt))
		dso__delete(dso);
	else
		RC_CHK_PUT(dso);
}

int dso__swap_init(struct dso *dso, unsigned char eidata)
{
	static unsigned int const endian = 1;

	dso__set_needs_swap(dso, DSO_SWAP__NO);

	switch (eidata) {
	case ELFDATA2LSB:
		/* We are big endian, DSO is little endian. */
		if (*(unsigned char const *)&endian != 1)
			dso__set_needs_swap(dso, DSO_SWAP__YES);
		break;

	case ELFDATA2MSB:
		/* We are little endian, DSO is big endian. */
		if (*(unsigned char const *)&endian != 0)
			dso__set_needs_swap(dso, DSO_SWAP__YES);
		break;

	default:
		pr_err("unrecognized DSO data encoding %d\n", eidata);
		return -EINVAL;
	}

	return 0;
}

void dso__set_build_id(struct dso *dso, const struct build_id *bid)
{
	dso__id(dso)->build_id = *bid;
}

bool dso__build_id_equal(const struct dso *dso, const struct build_id *bid)
{
	const struct build_id *dso_bid = dso__bid(dso);

	if (dso_bid->size > bid->size && dso_bid->size == BUILD_ID_SIZE) {
		/*
		 * For the backward compatibility, it allows a build-id has
		 * trailing zeros.
		 */
		return !memcmp(dso_bid->data, bid->data, bid->size) &&
			!memchr_inv(&dso_bid->data[bid->size], 0,
				    dso_bid->size - bid->size);
	}

	return dso_bid->size == bid->size &&
	       memcmp(dso_bid->data, bid->data, dso_bid->size) == 0;
}

void dso__read_running_kernel_build_id(struct dso *dso, struct machine *machine)
{
	char path[PATH_MAX];
	struct build_id bid = { .size = 0, };

	if (machine__is_default_guest(machine))
		return;
	sprintf(path, "%s/sys/kernel/notes", machine->root_dir);
	sysfs__read_build_id(path, &bid);
	dso__set_build_id(dso, &bid);
}

int dso__kernel_module_get_build_id(struct dso *dso,
				    const char *root_dir)
{
	char filename[PATH_MAX];
	struct build_id bid = { .size = 0, };
	/*
	 * kernel module short names are of the form "[module]" and
	 * we need just "module" here.
	 */
	const char *name = dso__short_name(dso) + 1;

	snprintf(filename, sizeof(filename),
		 "%s/sys/module/%.*s/notes/.note.gnu.build-id",
		 root_dir, (int)strlen(name) - 1, name);

	sysfs__read_build_id(filename, &bid);
	dso__set_build_id(dso, &bid);
	return 0;
}

static size_t dso__fprintf_buildid(struct dso *dso, FILE *fp)
{
	char sbuild_id[SBUILD_ID_SIZE];

	build_id__snprintf(dso__bid(dso), sbuild_id, sizeof(sbuild_id));
	return fprintf(fp, "%s", sbuild_id);
}

size_t dso__fprintf(struct dso *dso, FILE *fp)
{
	struct rb_node *nd;
	size_t ret = fprintf(fp, "dso: %s (", dso__short_name(dso));

	if (dso__short_name(dso) != dso__long_name(dso))
		ret += fprintf(fp, "%s, ", dso__long_name(dso));
	ret += fprintf(fp, "%sloaded, ", dso__loaded(dso) ? "" : "NOT ");
	ret += dso__fprintf_buildid(dso, fp);
	ret += fprintf(fp, ")\n");
	for (nd = rb_first_cached(dso__symbols(dso)); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);
		ret += symbol__fprintf(pos, fp);
	}

	return ret;
}

enum dso_type dso__type(struct dso *dso, struct machine *machine)
{
	int fd = -1;
	enum dso_type type = DSO__TYPE_UNKNOWN;

	if (dso__data_get_fd(dso, machine, &fd)) {
		type = dso__type_fd(fd);
		dso__data_put_fd(dso);
	}

	return type;
}

int dso__strerror_load(struct dso *dso, char *buf, size_t buflen)
{
	int idx, errnum = *dso__load_errno(dso);
	/*
	 * This must have a same ordering as the enum dso_load_errno.
	 */
	static const char *dso_load__error_str[] = {
	"Internal tools/perf/ library error",
	"Invalid ELF file",
	"Can not read build id",
	"Mismatching build id",
	"Decompression failure",
	};

	BUG_ON(buflen == 0);

	if (errnum >= 0) {
		const char *err = str_error_r(errnum, buf, buflen);

		if (err != buf)
			scnprintf(buf, buflen, "%s", err);

		return 0;
	}

	if (errnum <  __DSO_LOAD_ERRNO__START || errnum >= __DSO_LOAD_ERRNO__END)
		return -1;

	idx = errnum - __DSO_LOAD_ERRNO__START;
	scnprintf(buf, buflen, "%s", dso_load__error_str[idx]);
	return 0;
}

bool perf_pid_map_tid(const char *dso_name, int *tid)
{
	return sscanf(dso_name, "/tmp/perf-%d.map", tid) == 1;
}

bool is_perf_pid_map_name(const char *dso_name)
{
	int tid;

	return perf_pid_map_tid(dso_name, &tid);
}

struct find_file_offset_data {
	u64 ip;
	u64 offset;
};

/* This will be called for each PHDR in an ELF binary */
static int find_file_offset(u64 start, u64 len, u64 pgoff, void *arg)
{
	struct find_file_offset_data *data = arg;

	if (start <= data->ip && data->ip < start + len) {
		data->offset = pgoff + data->ip - start;
		return 1;
	}
	return 0;
}

static const u8 *__dso__read_symbol(struct dso *dso, const char *symfs_filename,
				    u64 start, size_t len,
				    u8 **out_buf, u64 *out_buf_len, bool *is_64bit)
{
	struct nscookie nsc;
	int fd;
	ssize_t count;
	struct find_file_offset_data data = {
		.ip = start,
	};
	u8 *code_buf = NULL;
	int saved_errno;

	nsinfo__mountns_enter(dso__nsinfo(dso), &nsc);
	fd = open(symfs_filename, O_RDONLY);
	saved_errno = errno;
	nsinfo__mountns_exit(&nsc);
	if (fd < 0) {
		errno = saved_errno;
		return NULL;
	}
	if (file__read_maps(fd, /*exe=*/true, find_file_offset, &data, is_64bit) <= 0) {
		close(fd);
		errno = ENOENT;
		return NULL;
	}
	code_buf = malloc(len);
	if (code_buf == NULL) {
		close(fd);
		errno = ENOMEM;
		return NULL;
	}
	count = pread(fd, code_buf, len, data.offset);
	saved_errno = errno;
	close(fd);
	if ((u64)count != len) {
		free(code_buf);
		errno = saved_errno;
		return NULL;
	}
	*out_buf = code_buf;
	*out_buf_len = len;
	return code_buf;
}

/*
 * Read a symbol into memory for disassembly by a library like capstone of
 * libLLVM. If memory is allocated out_buf holds it.
 */
const u8 *dso__read_symbol(struct dso *dso, const char *symfs_filename,
			   const struct map *map, const struct symbol *sym,
			   u8 **out_buf, u64 *out_buf_len, bool *is_64bit)
{
	u64 start = map__rip_2objdump(map, sym->start);
	u64 end = map__rip_2objdump(map, sym->end);
	size_t len = end - start;

	*out_buf = NULL;
	*out_buf_len = 0;
	*is_64bit = false;

	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_IMAGE) {
		/*
		 * Note, there is fallback BPF image disassembly in the objdump
		 * version but it currently does nothing.
		 */
		errno = EOPNOTSUPP;
		return NULL;
	}
	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_PROG_INFO) {
#ifdef HAVE_LIBBPF_SUPPORT
		struct bpf_prog_info_node *info_node;
		struct perf_bpil *info_linear;

		*is_64bit = sizeof(void *) == sizeof(u64);
		info_node = perf_env__find_bpf_prog_info(dso__bpf_prog(dso)->env,
							 dso__bpf_prog(dso)->id);
		if (!info_node) {
			errno = SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF;
			return NULL;
		}
		info_linear = info_node->info_linear;
		assert(len <= info_linear->info.jited_prog_len);
		*out_buf_len = len;
		return (const u8 *)(uintptr_t)(info_linear->info.jited_prog_insns);
#else
		pr_debug("No BPF program disassembly support\n");
		errno = EOPNOTSUPP;
		return NULL;
#endif
	}
	return __dso__read_symbol(dso, symfs_filename, start, len,
				  out_buf, out_buf_len, is_64bit);
}
