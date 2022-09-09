// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include "event-parse.h"
#include "event-parse-local.h"
#include "event-utils.h"
#include "trace-seq.h"

#define LOCAL_PLUGIN_DIR ".local/lib/traceevent/plugins/"

static struct registered_plugin_options {
	struct registered_plugin_options	*next;
	struct tep_plugin_option		*options;
} *registered_options;

static struct trace_plugin_options {
	struct trace_plugin_options	*next;
	char				*plugin;
	char				*option;
	char				*value;
} *trace_plugin_options;

struct tep_plugin_list {
	struct tep_plugin_list	*next;
	char			*name;
	void			*handle;
};

struct tep_plugins_dir {
	struct tep_plugins_dir		*next;
	char				*path;
	enum tep_plugin_load_priority	prio;
};

static void lower_case(char *str)
{
	if (!str)
		return;
	for (; *str; str++)
		*str = tolower(*str);
}

static int update_option_value(struct tep_plugin_option *op, const char *val)
{
	char *op_val;

	if (!val) {
		/* toggle, only if option is boolean */
		if (op->value)
			/* Warn? */
			return 0;
		op->set ^= 1;
		return 0;
	}

	/*
	 * If the option has a value then it takes a string
	 * otherwise the option is a boolean.
	 */
	if (op->value) {
		op->value = val;
		return 0;
	}

	/* Option is boolean, must be either "1", "0", "true" or "false" */

	op_val = strdup(val);
	if (!op_val)
		return -1;
	lower_case(op_val);

	if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0)
		op->set = 1;
	else if (strcmp(val, "0") == 0 || strcmp(val, "false") == 0)
		op->set = 0;
	free(op_val);

	return 0;
}

/**
 * tep_plugin_list_options - get list of plugin options
 *
 * Returns an array of char strings that list the currently registered
 * plugin options in the format of <plugin>:<option>. This list can be
 * used by toggling the option.
 *
 * Returns NULL if there's no options registered. On error it returns
 * INVALID_PLUGIN_LIST_OPTION
 *
 * Must be freed with tep_plugin_free_options_list().
 */
char **tep_plugin_list_options(void)
{
	struct registered_plugin_options *reg;
	struct tep_plugin_option *op;
	char **list = NULL;
	char *name;
	int count = 0;

	for (reg = registered_options; reg; reg = reg->next) {
		for (op = reg->options; op->name; op++) {
			char *alias = op->plugin_alias ? op->plugin_alias : op->file;
			char **temp = list;
			int ret;

			ret = asprintf(&name, "%s:%s", alias, op->name);
			if (ret < 0)
				goto err;

			list = realloc(list, count + 2);
			if (!list) {
				list = temp;
				free(name);
				goto err;
			}
			list[count++] = name;
			list[count] = NULL;
		}
	}
	return list;

 err:
	while (--count >= 0)
		free(list[count]);
	free(list);

	return INVALID_PLUGIN_LIST_OPTION;
}

void tep_plugin_free_options_list(char **list)
{
	int i;

	if (!list)
		return;

	if (list == INVALID_PLUGIN_LIST_OPTION)
		return;

	for (i = 0; list[i]; i++)
		free(list[i]);

	free(list);
}

static int
update_option(const char *file, struct tep_plugin_option *option)
{
	struct trace_plugin_options *op;
	char *plugin;
	int ret = 0;

	if (option->plugin_alias) {
		plugin = strdup(option->plugin_alias);
		if (!plugin)
			return -1;
	} else {
		char *p;
		plugin = strdup(file);
		if (!plugin)
			return -1;
		p = strstr(plugin, ".");
		if (p)
			*p = '\0';
	}

	/* first look for named options */
	for (op = trace_plugin_options; op; op = op->next) {
		if (!op->plugin)
			continue;
		if (strcmp(op->plugin, plugin) != 0)
			continue;
		if (strcmp(op->option, option->name) != 0)
			continue;

		ret = update_option_value(option, op->value);
		if (ret)
			goto out;
		break;
	}

	/* first look for unnamed options */
	for (op = trace_plugin_options; op; op = op->next) {
		if (op->plugin)
			continue;
		if (strcmp(op->option, option->name) != 0)
			continue;

		ret = update_option_value(option, op->value);
		break;
	}

 out:
	free(plugin);
	return ret;
}

/**
 * tep_plugin_add_options - Add a set of options by a plugin
 * @name: The name of the plugin adding the options
 * @options: The set of options being loaded
 *
 * Sets the options with the values that have been added by user.
 */
int tep_plugin_add_options(const char *name,
			   struct tep_plugin_option *options)
{
	struct registered_plugin_options *reg;

	reg = malloc(sizeof(*reg));
	if (!reg)
		return -1;
	reg->next = registered_options;
	reg->options = options;
	registered_options = reg;

	while (options->name) {
		update_option(name, options);
		options++;
	}
	return 0;
}

/**
 * tep_plugin_remove_options - remove plugin options that were registered
 * @options: Options to removed that were registered with tep_plugin_add_options
 */
void tep_plugin_remove_options(struct tep_plugin_option *options)
{
	struct registered_plugin_options **last;
	struct registered_plugin_options *reg;

	for (last = &registered_options; *last; last = &(*last)->next) {
		if ((*last)->options == options) {
			reg = *last;
			*last = reg->next;
			free(reg);
			return;
		}
	}
}

static int parse_option_name(char **option, char **plugin)
{
	char *p;

	*plugin = NULL;

	if ((p = strstr(*option, ":"))) {
		*plugin = *option;
		*p = '\0';
		*option = strdup(p + 1);
		if (!*option)
			return -1;
	}
	return 0;
}

static struct tep_plugin_option *
find_registered_option(const char *plugin, const char *option)
{
	struct registered_plugin_options *reg;
	struct tep_plugin_option *op;
	const char *op_plugin;

	for (reg = registered_options; reg; reg = reg->next) {
		for (op = reg->options; op->name; op++) {
			if (op->plugin_alias)
				op_plugin = op->plugin_alias;
			else
				op_plugin = op->file;

			if (plugin && strcmp(plugin, op_plugin) != 0)
				continue;
			if (strcmp(option, op->name) != 0)
				continue;

			return op;
		}
	}

	return NULL;
}

static int process_option(const char *plugin, const char *option, const char *val)
{
	struct tep_plugin_option *op;

	op = find_registered_option(plugin, option);
	if (!op)
		return 0;

	return update_option_value(op, val);
}

/**
 * tep_plugin_add_option - add an option/val pair to set plugin options
 * @name: The name of the option (format: <plugin>:<option> or just <option>)
 * @val: (optional) the value for the option
 *
 * Modify a plugin option. If @val is given than the value of the option
 * is set (note, some options just take a boolean, so @val must be either
 * "1" or "0" or "true" or "false").
 */
int tep_plugin_add_option(const char *name, const char *val)
{
	struct trace_plugin_options *op;
	char *option_str;
	char *plugin;

	option_str = strdup(name);
	if (!option_str)
		return -ENOMEM;

	if (parse_option_name(&option_str, &plugin) < 0)
		return -ENOMEM;

	/* If the option exists, update the val */
	for (op = trace_plugin_options; op; op = op->next) {
		/* Both must be NULL or not NULL */
		if ((!plugin || !op->plugin) && plugin != op->plugin)
			continue;
		if (plugin && strcmp(plugin, op->plugin) != 0)
			continue;
		if (strcmp(op->option, option_str) != 0)
			continue;

		/* update option */
		free(op->value);
		if (val) {
			op->value = strdup(val);
			if (!op->value)
				goto out_free;
		} else
			op->value = NULL;

		/* plugin and option_str don't get freed at the end */
		free(plugin);
		free(option_str);

		plugin = op->plugin;
		option_str = op->option;
		break;
	}

	/* If not found, create */
	if (!op) {
		op = malloc(sizeof(*op));
		if (!op)
			goto out_free;
		memset(op, 0, sizeof(*op));
		op->plugin = plugin;
		op->option = option_str;
		if (val) {
			op->value = strdup(val);
			if (!op->value) {
				free(op);
				goto out_free;
			}
		}
		op->next = trace_plugin_options;
		trace_plugin_options = op;
	}

	return process_option(plugin, option_str, val);

out_free:
	free(plugin);
	free(option_str);
	return -ENOMEM;
}

static void print_op_data(struct trace_seq *s, const char *name,
			  const char *op)
{
	if (op)
		trace_seq_printf(s, "%8s:\t%s\n", name, op);
}

/**
 * tep_plugin_print_options - print out the registered plugin options
 * @s: The trace_seq descriptor to write the plugin options into
 *
 * Writes a list of options into trace_seq @s.
 */
void tep_plugin_print_options(struct trace_seq *s)
{
	struct registered_plugin_options *reg;
	struct tep_plugin_option *op;

	for (reg = registered_options; reg; reg = reg->next) {
		if (reg != registered_options)
			trace_seq_printf(s, "============\n");
		for (op = reg->options; op->name; op++) {
			if (op != reg->options)
				trace_seq_printf(s, "------------\n");
			print_op_data(s, "file", op->file);
			print_op_data(s, "plugin", op->plugin_alias);
			print_op_data(s, "option", op->name);
			print_op_data(s, "desc", op->description);
			print_op_data(s, "value", op->value);
			trace_seq_printf(s, "%8s:\t%d\n", "set", op->set);
		}
	}
}

/**
 * tep_print_plugins - print out the list of plugins loaded
 * @s: the trace_seq descripter to write to
 * @prefix: The prefix string to add before listing the option name
 * @suffix: The suffix string ot append after the option name
 * @list: The list of plugins (usually returned by tep_load_plugins()
 *
 * Writes to the trace_seq @s the list of plugins (files) that is
 * returned by tep_load_plugins(). Use @prefix and @suffix for formating:
 * @prefix = "  ", @suffix = "\n".
 */
void tep_print_plugins(struct trace_seq *s,
		       const char *prefix, const char *suffix,
		       const struct tep_plugin_list *list)
{
	while (list) {
		trace_seq_printf(s, "%s%s%s", prefix, list->name, suffix);
		list = list->next;
	}
}

static void
load_plugin(struct tep_handle *tep, const char *path,
	    const char *file, void *data)
{
	struct tep_plugin_list **plugin_list = data;
	struct tep_plugin_option *options;
	tep_plugin_load_func func;
	struct tep_plugin_list *list;
	const char *alias;
	char *plugin;
	void *handle;
	int ret;

	ret = asprintf(&plugin, "%s/%s", path, file);
	if (ret < 0) {
		warning("could not allocate plugin memory\n");
		return;
	}

	handle = dlopen(plugin, RTLD_NOW | RTLD_GLOBAL);
	if (!handle) {
		warning("could not load plugin '%s'\n%s\n",
			plugin, dlerror());
		goto out_free;
	}

	alias = dlsym(handle, TEP_PLUGIN_ALIAS_NAME);
	if (!alias)
		alias = file;

	options = dlsym(handle, TEP_PLUGIN_OPTIONS_NAME);
	if (options) {
		while (options->name) {
			ret = update_option(alias, options);
			if (ret < 0)
				goto out_free;
			options++;
		}
	}

	func = dlsym(handle, TEP_PLUGIN_LOADER_NAME);
	if (!func) {
		warning("could not find func '%s' in plugin '%s'\n%s\n",
			TEP_PLUGIN_LOADER_NAME, plugin, dlerror());
		goto out_free;
	}

	list = malloc(sizeof(*list));
	if (!list) {
		warning("could not allocate plugin memory\n");
		goto out_free;
	}

	list->next = *plugin_list;
	list->handle = handle;
	list->name = plugin;
	*plugin_list = list;

	pr_stat("registering plugin: %s", plugin);
	func(tep);
	return;

 out_free:
	free(plugin);
}

static void
load_plugins_dir(struct tep_handle *tep, const char *suffix,
		 const char *path,
		 void (*load_plugin)(struct tep_handle *tep,
				     const char *path,
				     const char *name,
				     void *data),
		 void *data)
{
	struct dirent *dent;
	struct stat st;
	DIR *dir;
	int ret;

	ret = stat(path, &st);
	if (ret < 0)
		return;

	if (!S_ISDIR(st.st_mode))
		return;

	dir = opendir(path);
	if (!dir)
		return;

	while ((dent = readdir(dir))) {
		const char *name = dent->d_name;

		if (strcmp(name, ".") == 0 ||
		    strcmp(name, "..") == 0)
			continue;

		/* Only load plugins that end in suffix */
		if (strcmp(name + (strlen(name) - strlen(suffix)), suffix) != 0)
			continue;

		load_plugin(tep, path, name, data);
	}

	closedir(dir);
}

/**
 * tep_load_plugins_hook - call a user specified callback to load a plugin
 * @tep: handler to traceevent context
 * @suffix: filter only plugin files with given suffix
 * @load_plugin: user specified callback, called for each plugin file
 * @data: custom context, passed to @load_plugin
 *
 * Searches for traceevent plugin files and calls @load_plugin for each
 * The order of plugins search is:
 *  - Directories, specified in @tep->plugins_dir and priority TEP_PLUGIN_FIRST
 *  - Directory, specified at compile time with PLUGIN_TRACEEVENT_DIR
 *  - Directory, specified by environment variable TRACEEVENT_PLUGIN_DIR
 *  - In user's home: ~/.local/lib/traceevent/plugins/
 *  - Directories, specified in @tep->plugins_dir and priority TEP_PLUGIN_LAST
 *
 */
void tep_load_plugins_hook(struct tep_handle *tep, const char *suffix,
			   void (*load_plugin)(struct tep_handle *tep,
					       const char *path,
					       const char *name,
					       void *data),
			   void *data)
{
	struct tep_plugins_dir *dir = NULL;
	char *home;
	char *path;
	char *envdir;
	int ret;

	if (tep && tep->flags & TEP_DISABLE_PLUGINS)
		return;

	if (tep)
		dir = tep->plugins_dir;
	while (dir) {
		if (dir->prio == TEP_PLUGIN_FIRST)
			load_plugins_dir(tep, suffix, dir->path,
					 load_plugin, data);
		dir = dir->next;
	}

	/*
	 * If a system plugin directory was defined,
	 * check that first.
	 */
#ifdef PLUGIN_DIR
	if (!tep || !(tep->flags & TEP_DISABLE_SYS_PLUGINS))
		load_plugins_dir(tep, suffix, PLUGIN_DIR,
				 load_plugin, data);
#endif

	/*
	 * Next let the environment-set plugin directory
	 * override the system defaults.
	 */
	envdir = getenv("TRACEEVENT_PLUGIN_DIR");
	if (envdir)
		load_plugins_dir(tep, suffix, envdir, load_plugin, data);

	/*
	 * Now let the home directory override the environment
	 * or system defaults.
	 */
	home = getenv("HOME");
	if (!home)
		return;

	ret = asprintf(&path, "%s/%s", home, LOCAL_PLUGIN_DIR);
	if (ret < 0) {
		warning("could not allocate plugin memory\n");
		return;
	}

	load_plugins_dir(tep, suffix, path, load_plugin, data);

	if (tep)
		dir = tep->plugins_dir;
	while (dir) {
		if (dir->prio == TEP_PLUGIN_LAST)
			load_plugins_dir(tep, suffix, dir->path,
					 load_plugin, data);
		dir = dir->next;
	}

	free(path);
}

struct tep_plugin_list*
tep_load_plugins(struct tep_handle *tep)
{
	struct tep_plugin_list *list = NULL;

	tep_load_plugins_hook(tep, ".so", load_plugin, &list);
	return list;
}

/**
 * tep_add_plugin_path - Add a new plugin directory.
 * @tep: Trace event handler.
 * @path: Path to a directory. All plugin files in that
 *	  directory will be loaded.
 *@prio: Load priority of the plugins in that directory.
 *
 * Returns -1 in case of an error, 0 otherwise.
 */
int tep_add_plugin_path(struct tep_handle *tep, char *path,
			enum tep_plugin_load_priority prio)
{
	struct tep_plugins_dir *dir;

	if (!tep || !path)
		return -1;

	dir = calloc(1, sizeof(*dir));
	if (!dir)
		return -1;

	dir->path = strdup(path);
	if (!dir->path) {
		free(dir);
		return -1;
	}
	dir->prio = prio;
	dir->next = tep->plugins_dir;
	tep->plugins_dir = dir;

	return 0;
}

__hidden void free_tep_plugin_paths(struct tep_handle *tep)
{
	struct tep_plugins_dir *dir;

	if (!tep)
		return;

	dir = tep->plugins_dir;
	while (dir) {
		tep->plugins_dir = tep->plugins_dir->next;
		free(dir->path);
		free(dir);
		dir = tep->plugins_dir;
	}
}

void
tep_unload_plugins(struct tep_plugin_list *plugin_list, struct tep_handle *tep)
{
	tep_plugin_unload_func func;
	struct tep_plugin_list *list;

	while (plugin_list) {
		list = plugin_list;
		plugin_list = list->next;
		func = dlsym(list->handle, TEP_PLUGIN_UNLOADER_NAME);
		if (func)
			func(tep);
		dlclose(list->handle);
		free(list->name);
		free(list);
	}
}
