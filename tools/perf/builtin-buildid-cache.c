/*
 * builtin-buildid-cache.c
 *
 * Builtin buildid-cache command: Manages build-id cache
 *
 * Copyright (C) 2010, Red Hat Inc.
 * Copyright (C) 2010, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "builtin.h"
#include "perf.h"
#include "util/cache.h"
#include "util/debug.h"
#include "util/header.h"
#include <subcmd/parse-options.h>
#include "util/strlist.h"
#include "util/build-id.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/time-utils.h"

static int build_id_cache__kcore_buildid(const char *proc_dir, char *sbuildid)
{
	char root_dir[PATH_MAX];
	char *p;

	strlcpy(root_dir, proc_dir, sizeof(root_dir));

	p = strrchr(root_dir, '/');
	if (!p)
		return -1;
	*p = '\0';
	return sysfs__sprintf_build_id(root_dir, sbuildid);
}

static int build_id_cache__kcore_dir(char *dir, size_t sz)
{
	return fetch_current_timestamp(dir, sz);
}

static bool same_kallsyms_reloc(const char *from_dir, char *to_dir)
{
	char from[PATH_MAX];
	char to[PATH_MAX];
	const char *name;
	u64 addr1 = 0, addr2 = 0;
	int i, err = -1;

	scnprintf(from, sizeof(from), "%s/kallsyms", from_dir);
	scnprintf(to, sizeof(to), "%s/kallsyms", to_dir);

	for (i = 0; (name = ref_reloc_sym_names[i]) != NULL; i++) {
		err = kallsyms__get_function_start(from, name, &addr1);
		if (!err)
			break;
	}

	if (err)
		return false;

	if (kallsyms__get_function_start(to, name, &addr2))
		return false;

	return addr1 == addr2;
}

static int build_id_cache__kcore_existing(const char *from_dir, char *to_dir,
					  size_t to_dir_sz)
{
	char from[PATH_MAX];
	char to[PATH_MAX];
	char to_subdir[PATH_MAX];
	struct dirent *dent;
	int ret = -1;
	DIR *d;

	d = opendir(to_dir);
	if (!d)
		return -1;

	scnprintf(from, sizeof(from), "%s/modules", from_dir);

	while (1) {
		dent = readdir(d);
		if (!dent)
			break;
		if (dent->d_type != DT_DIR)
			continue;
		scnprintf(to, sizeof(to), "%s/%s/modules", to_dir,
			  dent->d_name);
		scnprintf(to_subdir, sizeof(to_subdir), "%s/%s",
			  to_dir, dent->d_name);
		if (!compare_proc_modules(from, to) &&
		    same_kallsyms_reloc(from_dir, to_subdir)) {
			strlcpy(to_dir, to_subdir, to_dir_sz);
			ret = 0;
			break;
		}
	}

	closedir(d);

	return ret;
}

static int build_id_cache__add_kcore(const char *filename, bool force)
{
	char dir[32], sbuildid[SBUILD_ID_SIZE];
	char from_dir[PATH_MAX], to_dir[PATH_MAX];
	char *p;

	strlcpy(from_dir, filename, sizeof(from_dir));

	p = strrchr(from_dir, '/');
	if (!p || strcmp(p + 1, "kcore"))
		return -1;
	*p = '\0';

	if (build_id_cache__kcore_buildid(from_dir, sbuildid) < 0)
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/%s/%s",
		  buildid_dir, DSO__NAME_KCORE, sbuildid);

	if (!force &&
	    !build_id_cache__kcore_existing(from_dir, to_dir, sizeof(to_dir))) {
		pr_debug("same kcore found in %s\n", to_dir);
		return 0;
	}

	if (build_id_cache__kcore_dir(dir, sizeof(dir)))
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/%s/%s/%s",
		  buildid_dir, DSO__NAME_KCORE, sbuildid, dir);

	if (mkdir_p(to_dir, 0755))
		return -1;

	if (kcore_copy(from_dir, to_dir)) {
		/* Remove YYYYmmddHHMMSShh directory */
		if (!rmdir(to_dir)) {
			p = strrchr(to_dir, '/');
			if (p)
				*p = '\0';
			/* Try to remove buildid directory */
			if (!rmdir(to_dir)) {
				p = strrchr(to_dir, '/');
				if (p)
					*p = '\0';
				/* Try to remove [kernel.kcore] directory */
				rmdir(to_dir);
			}
		}
		return -1;
	}

	pr_debug("kcore added to build-id cache directory %s\n", to_dir);

	return 0;
}

static int build_id_cache__add_file(const char *filename)
{
	char sbuild_id[SBUILD_ID_SIZE];
	u8 build_id[BUILD_ID_SIZE];
	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__add_s(sbuild_id, filename,
				    false, false);
	pr_debug("Adding %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");
	return err;
}

static int build_id_cache__remove_file(const char *filename)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[SBUILD_ID_SIZE];

	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__remove_s(sbuild_id);
	pr_debug("Removing %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");

	return err;
}

static int build_id_cache__purge_path(const char *pathname)
{
	struct strlist *list;
	struct str_node *pos;
	int err;

	err = build_id_cache__list_build_ids(pathname, &list);
	if (err)
		goto out;

	strlist__for_each_entry(pos, list) {
		err = build_id_cache__remove_s(pos->s);
		pr_debug("Removing %s %s: %s\n", pos->s, pathname,
			 err ? "FAIL" : "Ok");
		if (err)
			break;
	}
	strlist__delete(list);

out:
	pr_debug("Purging %s: %s\n", pathname, err ? "FAIL" : "Ok");

	return err;
}

static bool dso__missing_buildid_cache(struct dso *dso, int parm __maybe_unused)
{
	char filename[PATH_MAX];
	u8 build_id[BUILD_ID_SIZE];

	if (dso__build_id_filename(dso, filename, sizeof(filename)) &&
	    filename__read_build_id(filename, build_id,
				    sizeof(build_id)) != sizeof(build_id)) {
		if (errno == ENOENT)
			return false;

		pr_warning("Problems with %s file, consider removing it from the cache\n",
			   filename);
	} else if (memcmp(dso->build_id, build_id, sizeof(dso->build_id))) {
		pr_warning("Problems with %s file, consider removing it from the cache\n",
			   filename);
	}

	return true;
}

static int build_id_cache__fprintf_missing(struct perf_session *session, FILE *fp)
{
	perf_session__fprintf_dsos_buildid(session, fp, dso__missing_buildid_cache, 0);
	return 0;
}

static int build_id_cache__update_file(const char *filename)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[SBUILD_ID_SIZE];

	int err = 0;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	if (build_id_cache__cached(sbuild_id))
		err = build_id_cache__remove_s(sbuild_id);

	if (!err)
		err = build_id_cache__add_s(sbuild_id, filename, false, false);

	pr_debug("Updating %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");

	return err;
}

int cmd_buildid_cache(int argc, const char **argv)
{
	struct strlist *list;
	struct str_node *pos;
	int ret = 0;
	bool force = false;
	char const *add_name_list_str = NULL,
		   *remove_name_list_str = NULL,
		   *purge_name_list_str = NULL,
		   *missing_filename = NULL,
		   *update_name_list_str = NULL,
		   *kcore_filename = NULL;
	char sbuf[STRERR_BUFSIZE];

	struct perf_data_file file = {
		.mode  = PERF_DATA_MODE_READ,
	};
	struct perf_session *session = NULL;

	const struct option buildid_cache_options[] = {
	OPT_STRING('a', "add", &add_name_list_str,
		   "file list", "file(s) to add"),
	OPT_STRING('k', "kcore", &kcore_filename,
		   "file", "kcore file to add"),
	OPT_STRING('r', "remove", &remove_name_list_str, "file list",
		    "file(s) to remove"),
	OPT_STRING('p', "purge", &purge_name_list_str, "path list",
		    "path(s) to remove (remove old caches too)"),
	OPT_STRING('M', "missing", &missing_filename, "file",
		   "to find missing build ids in the cache"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING('u', "update", &update_name_list_str, "file list",
		    "file(s) to update"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	const char * const buildid_cache_usage[] = {
		"perf buildid-cache [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, buildid_cache_options,
			     buildid_cache_usage, 0);

	if (argc || (!add_name_list_str && !kcore_filename &&
		     !remove_name_list_str && !purge_name_list_str &&
		     !missing_filename && !update_name_list_str))
		usage_with_options(buildid_cache_usage, buildid_cache_options);

	if (missing_filename) {
		file.path = missing_filename;
		file.force = force;

		session = perf_session__new(&file, false, NULL);
		if (session == NULL)
			return -1;
	}

	if (symbol__init(session ? &session->header.env : NULL) < 0)
		goto out;

	setup_pager();

	if (add_name_list_str) {
		list = strlist__new(add_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__add_file(pos->s)) {
					if (errno == EEXIST) {
						pr_debug("%s already in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't add %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (remove_name_list_str) {
		list = strlist__new(remove_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__remove_file(pos->s)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (purge_name_list_str) {
		list = strlist__new(purge_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__purge_path(pos->s)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (missing_filename)
		ret = build_id_cache__fprintf_missing(session, stdout);

	if (update_name_list_str) {
		list = strlist__new(update_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__update_file(pos->s)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't update %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (kcore_filename && build_id_cache__add_kcore(kcore_filename, force))
		pr_warning("Couldn't add %s\n", kcore_filename);

out:
	perf_session__delete(session);

	return ret;
}
