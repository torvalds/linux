// SPDX-License-Identifier: GPL-2.0
//
// kselftest configuration helpers for the hw specific configuration
//
// Original author: Jaroslav Kysela <perex@perex.cz>
// Copyright (c) 2022 Red Hat Inc.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <dirent.h>
#include <regex.h>
#include <sys/stat.h>

#include "../kselftest.h"
#include "alsa-local.h"

#define SYSFS_ROOT "/sys"

struct card_data {
	int card;
	snd_config_t *config;
	const char *filename;
	struct card_data *next;
};

static struct card_data *conf_cards;

static const char *alsa_config =
"ctl.hw {\n"
"	@args [ CARD ]\n"
"	@args.CARD.type string\n"
"	type hw\n"
"	card $CARD\n"
"}\n"
"pcm.hw {\n"
"	@args [ CARD DEV SUBDEV ]\n"
"	@args.CARD.type string\n"
"	@args.DEV.type integer\n"
"	@args.SUBDEV.type integer\n"
"	type hw\n"
"	card $CARD\n"
"	device $DEV\n"
"	subdevice $SUBDEV\n"
"}\n"
;

#ifdef SND_LIB_VER
#if SND_LIB_VERSION >= SND_LIB_VER(1, 2, 6)
#define LIB_HAS_LOAD_STRING
#endif
#endif

#ifndef LIB_HAS_LOAD_STRING
static int snd_config_load_string(snd_config_t **config, const char *s,
				  size_t size)
{
	snd_input_t *input;
	snd_config_t *dst;
	int err;

	assert(config && s);
	if (size == 0)
		size = strlen(s);
	err = snd_input_buffer_open(&input, s, size);
	if (err < 0)
		return err;
	err = snd_config_top(&dst);
	if (err < 0) {
		snd_input_close(input);
		return err;
	}
	err = snd_config_load(dst, input);
	snd_input_close(input);
	if (err < 0) {
		snd_config_delete(dst);
		return err;
	}
	*config = dst;
	return 0;
}
#endif

snd_config_t *get_alsalib_config(void)
{
	snd_config_t *config;
	int err;

	err = snd_config_load_string(&config, alsa_config, strlen(alsa_config));
	if (err < 0) {
		ksft_print_msg("Unable to parse custom alsa-lib configuration: %s\n",
			       snd_strerror(err));
		ksft_exit_fail();
	}
	return config;
}

static struct card_data *conf_data_by_card(int card, bool msg)
{
	struct card_data *conf;

	for (conf = conf_cards; conf; conf = conf->next) {
		if (conf->card == card) {
			if (msg)
				ksft_print_msg("using hw card config %s for card %d\n",
					       conf->filename, card);
			return conf;
		}
	}
	return NULL;
}

static int dump_config_tree(snd_config_t *top)
{
	snd_output_t *out;
	int err;

	err = snd_output_stdio_attach(&out, stdout, 0);
	if (err < 0)
		ksft_exit_fail_msg("stdout attach\n");
	if (snd_config_save(top, out))
		ksft_exit_fail_msg("config save\n");
	snd_output_close(out);
}

snd_config_t *conf_load_from_file(const char *filename)
{
	snd_config_t *dst;
	snd_input_t *input;
	int err;

	err = snd_input_stdio_open(&input, filename, "r");
	if (err < 0)
		ksft_exit_fail_msg("Unable to parse filename %s\n", filename);
	err = snd_config_top(&dst);
	if (err < 0)
		ksft_exit_fail_msg("Out of memory\n");
	err = snd_config_load(dst, input);
	snd_input_close(input);
	if (err < 0)
		ksft_exit_fail_msg("Unable to parse filename %s\n", filename);
	return dst;
}

static char *sysfs_get(const char *sysfs_root, const char *id)
{
	char path[PATH_MAX], link[PATH_MAX + 1];
	struct stat sb;
	ssize_t len;
	char *e;
	int fd;

	if (id[0] == '/')
		id++;
	snprintf(path, sizeof(path), "%s/%s", sysfs_root, id);
	if (lstat(path, &sb) != 0)
		return NULL;
	if (S_ISLNK(sb.st_mode)) {
		len = readlink(path, link, sizeof(link) - 1);
		if (len <= 0) {
			ksft_exit_fail_msg("sysfs: cannot read link '%s': %s\n",
					   path, strerror(errno));
			return NULL;
		}
		link[len] = '\0';
		e = strrchr(link, '/');
		if (e)
			return strdup(e + 1);
		return NULL;
	}
	if (S_ISDIR(sb.st_mode))
		return NULL;
	if ((sb.st_mode & S_IRUSR) == 0)
		return NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return NULL;
		ksft_exit_fail_msg("sysfs: open failed for '%s': %s\n",
				   path, strerror(errno));
	}
	len = read(fd, path, sizeof(path)-1);
	close(fd);
	if (len < 0)
		ksft_exit_fail_msg("sysfs: unable to read value '%s': %s\n",
				   path, errno);
	while (len > 0 && path[len-1] == '\n')
		len--;
	path[len] = '\0';
	e = strdup(path);
	if (e == NULL)
		ksft_exit_fail_msg("Out of memory\n");
	return e;
}

static bool sysfs_match(const char *sysfs_root, snd_config_t *config)
{
	snd_config_t *node, *path_config, *regex_config;
	snd_config_iterator_t i, next;
	const char *path_string, *regex_string, *v;
	regex_t re;
	regmatch_t match[1];
	int iter = 0, ret;

	snd_config_for_each(i, next, config) {
		node = snd_config_iterator_entry(i);
		if (snd_config_search(node, "path", &path_config))
			ksft_exit_fail_msg("Missing path field in the sysfs block\n");
		if (snd_config_search(node, "regex", &regex_config))
			ksft_exit_fail_msg("Missing regex field in the sysfs block\n");
		if (snd_config_get_string(path_config, &path_string))
			ksft_exit_fail_msg("Path field in the sysfs block is not a string\n");
		if (snd_config_get_string(regex_config, &regex_string))
			ksft_exit_fail_msg("Regex field in the sysfs block is not a string\n");
		iter++;
		v = sysfs_get(sysfs_root, path_string);
		if (!v)
			return false;
		if (regcomp(&re, regex_string, REG_EXTENDED))
			ksft_exit_fail_msg("Wrong regex '%s'\n", regex_string);
		ret = regexec(&re, v, 1, match, 0);
		regfree(&re);
		if (ret)
			return false;
	}
	return iter > 0;
}

static bool test_filename1(int card, const char *filename, const char *sysfs_card_root)
{
	struct card_data *data, *data2;
	snd_config_t *config, *sysfs_config, *card_config, *sysfs_card_config, *node;
	snd_config_iterator_t i, next;

	config = conf_load_from_file(filename);
	if (snd_config_search(config, "sysfs", &sysfs_config) ||
	    snd_config_get_type(sysfs_config) != SND_CONFIG_TYPE_COMPOUND)
		ksft_exit_fail_msg("Missing global sysfs block in filename %s\n", filename);
	if (snd_config_search(config, "card", &card_config) ||
	    snd_config_get_type(card_config) != SND_CONFIG_TYPE_COMPOUND)
		ksft_exit_fail_msg("Missing global card block in filename %s\n", filename);
	if (!sysfs_match(SYSFS_ROOT, sysfs_config))
		return false;
	snd_config_for_each(i, next, card_config) {
		node = snd_config_iterator_entry(i);
		if (snd_config_search(node, "sysfs", &sysfs_card_config) ||
		    snd_config_get_type(sysfs_card_config) != SND_CONFIG_TYPE_COMPOUND)
			ksft_exit_fail_msg("Missing card sysfs block in filename %s\n", filename);
		if (!sysfs_match(sysfs_card_root, sysfs_card_config))
			continue;
		data = malloc(sizeof(*data));
		if (!data)
			ksft_exit_fail_msg("Out of memory\n");
		data2 = conf_data_by_card(card, false);
		if (data2)
			ksft_exit_fail_msg("Duplicate card '%s' <-> '%s'\n", filename, data2->filename);
		data->card = card;
		data->filename = filename;
		data->config = node;
		data->next = conf_cards;
		conf_cards = data;
		return true;
	}
	return false;
}

static bool test_filename(const char *filename)
{
	char fn[128];
	int card;

	for (card = 0; card < 32; card++) {
		snprintf(fn, sizeof(fn), "%s/class/sound/card%d", SYSFS_ROOT, card);
		if (access(fn, R_OK) == 0 && test_filename1(card, filename, fn))
			return true;
	}
	return false;
}

static int filename_filter(const struct dirent *dirent)
{
	size_t flen;

	if (dirent == NULL)
		return 0;
	if (dirent->d_type == DT_DIR)
		return 0;
	flen = strlen(dirent->d_name);
	if (flen <= 5)
		return 0;
	if (strncmp(&dirent->d_name[flen-5], ".conf", 5) == 0)
		return 1;
	return 0;
}

void conf_load(void)
{
	const char *fn = "conf.d";
	struct dirent **namelist;
	int n, j;

	n = scandir(fn, &namelist, filename_filter, alphasort);
	if (n < 0)
		ksft_exit_fail_msg("scandir: %s\n", strerror(errno));
	for (j = 0; j < n; j++) {
		size_t sl = strlen(fn) + strlen(namelist[j]->d_name) + 2;
		char *filename = malloc(sl);
		if (filename == NULL)
			ksft_exit_fail_msg("Out of memory\n");
		sprintf(filename, "%s/%s", fn, namelist[j]->d_name);
		if (test_filename(filename))
			filename = NULL;
		free(filename);
		free(namelist[j]);
	}
	free(namelist);
}

void conf_free(void)
{
	struct card_data *conf;

	while (conf_cards) {
		conf = conf_cards;
		conf_cards = conf->next;
		snd_config_delete(conf->config);
	}
}

snd_config_t *conf_by_card(int card)
{
	struct card_data *conf;

	conf = conf_data_by_card(card, true);
	if (conf)
		return conf->config;
	return NULL;
}

static int conf_get_by_keys(snd_config_t *root, const char *key1,
			    const char *key2, snd_config_t **result)
{
	int ret;

	if (key1) {
		ret = snd_config_search(root, key1, &root);
		if (ret != -ENOENT && ret < 0)
			return ret;
	}
	if (key2)
		ret = snd_config_search(root, key2, &root);
	if (ret >= 0)
		*result = root;
	return ret;
}

snd_config_t *conf_get_subtree(snd_config_t *root, const char *key1, const char *key2)
{
	int ret;

	if (!root)
		return NULL;
	ret = conf_get_by_keys(root, key1, key2, &root);
	if (ret == -ENOENT)
		return NULL;
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	return root;
}

int conf_get_count(snd_config_t *root, const char *key1, const char *key2)
{
	snd_config_t *cfg;
	snd_config_iterator_t i, next;
	int count, ret;

	if (!root)
		return -1;
	ret = conf_get_by_keys(root, key1, key2, &cfg);
	if (ret == -ENOENT)
		return -1;
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	if (snd_config_get_type(cfg) != SND_CONFIG_TYPE_COMPOUND)
		ksft_exit_fail_msg("key '%s'.'%s' is not a compound\n", key1, key2);
	count = 0;
	snd_config_for_each(i, next, cfg)
		count++;
	return count;
}

const char *conf_get_string(snd_config_t *root, const char *key1, const char *key2, const char *def)
{
	snd_config_t *cfg;
	const char *s;
	int ret;

	if (!root)
		return def;
	ret = conf_get_by_keys(root, key1, key2, &cfg);
	if (ret == -ENOENT)
		return def;
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	if (snd_config_get_string(cfg, &s))
		ksft_exit_fail_msg("key '%s'.'%s' is not a string\n", key1, key2);
	return s;
}

long conf_get_long(snd_config_t *root, const char *key1, const char *key2, long def)
{
	snd_config_t *cfg;
	long l;
	int ret;

	if (!root)
		return def;
	ret = conf_get_by_keys(root, key1, key2, &cfg);
	if (ret == -ENOENT)
		return def;
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	if (snd_config_get_integer(cfg, &l))
		ksft_exit_fail_msg("key '%s'.'%s' is not an integer\n", key1, key2);
	return l;
}

int conf_get_bool(snd_config_t *root, const char *key1, const char *key2, int def)
{
	snd_config_t *cfg;
	long l;
	int ret;

	if (!root)
		return def;
	ret = conf_get_by_keys(root, key1, key2, &cfg);
	if (ret == -ENOENT)
		return def;
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	ret = snd_config_get_bool(cfg);
	if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' is not an bool\n", key1, key2);
	return !!ret;
}

void conf_get_string_array(snd_config_t *root, const char *key1, const char *key2,
			   const char **array, int array_size, const char *def)
{
	snd_config_t *cfg;
	char buf[16];
	int ret, index;

	ret = conf_get_by_keys(root, key1, key2, &cfg);
	if (ret == -ENOENT)
		cfg = NULL;
	else if (ret < 0)
		ksft_exit_fail_msg("key '%s'.'%s' search error: %s\n", key1, key2, snd_strerror(ret));
	for (index = 0; index < array_size; index++) {
		if (cfg == NULL) {
			array[index] = def;
		} else {
			sprintf(buf, "%i", index);
			array[index] = conf_get_string(cfg, buf, NULL, def);
		}
	}
}
