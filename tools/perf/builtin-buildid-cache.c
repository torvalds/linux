/*
 * builtin-buildid-cache.c
 *
 * Builtin buildid-cache command: Manages build-id cache
 *
 * Copyright (C) 2010, Red Hat Inc.
 * Copyright (C) 2010, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "builtin.h"
#include "perf.h"
#include "util/cache.h"
#include "util/debug.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/strlist.h"
#include "util/build-id.h"
#include "util/session.h"
#include "util/symbol.h"

static int build_id_cache__add_file(const char *filename, const char *debugdir)
{
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];
	u8 build_id[BUILD_ID_SIZE];
	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__add_s(sbuild_id, debugdir, filename,
				    false, false);
	if (verbose)
		pr_info("Adding %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");
	return err;
}

static int build_id_cache__remove_file(const char *filename,
				       const char *debugdir)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__remove_s(sbuild_id, debugdir);
	if (verbose)
		pr_info("Removing %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");

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

static int build_id_cache__fprintf_missing(const char *filename, bool force, FILE *fp)
{
	struct perf_session *session = perf_session__new(filename, O_RDONLY,
							 force, false, NULL);
	if (session == NULL)
		return -1;

	perf_session__fprintf_dsos_buildid(session, fp, dso__missing_buildid_cache, 0);
	perf_session__delete(session);

	return 0;
}

static int build_id_cache__update_file(const char *filename,
				       const char *debugdir)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__remove_s(sbuild_id, debugdir);
	if (!err) {
		err = build_id_cache__add_s(sbuild_id, debugdir, filename,
					    false, false);
	}
	if (verbose)
		pr_info("Updating %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");

	return err;
}

int cmd_buildid_cache(int argc, const char **argv,
		      const char *prefix __maybe_unused)
{
	struct strlist *list;
	struct str_node *pos;
	int ret = 0;
	bool force = false;
	char debugdir[PATH_MAX];
	char const *add_name_list_str = NULL,
		   *remove_name_list_str = NULL,
		   *missing_filename = NULL,
		   *update_name_list_str = NULL;

	const struct option buildid_cache_options[] = {
	OPT_STRING('a', "add", &add_name_list_str,
		   "file list", "file(s) to add"),
	OPT_STRING('r', "remove", &remove_name_list_str, "file list",
		    "file(s) to remove"),
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

	if (symbol__init() < 0)
		return -1;

	setup_pager();

	snprintf(debugdir, sizeof(debugdir), "%s", buildid_dir);

	if (add_name_list_str) {
		list = strlist__new(true, add_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__add_file(pos->s, debugdir)) {
					if (errno == EEXIST) {
						pr_debug("%s already in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't add %s: %s\n",
						   pos->s, strerror(errno));
				}

			strlist__delete(list);
		}
	}

	if (remove_name_list_str) {
		list = strlist__new(true, remove_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__remove_file(pos->s, debugdir)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, strerror(errno));
				}

			strlist__delete(list);
		}
	}

	if (missing_filename)
		ret = build_id_cache__fprintf_missing(missing_filename, force, stdout);

	if (update_name_list_str) {
		list = strlist__new(true, update_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__update_file(pos->s, debugdir)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't update %s: %s\n",
						   pos->s, strerror(errno));
				}

			strlist__delete(list);
		}
	}

	return ret;
}
