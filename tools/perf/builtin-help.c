/*
 * builtin-help.c
 *
 * Builtin help command
 */
#include "perf.h"
#include "util/cache.h"
#include "builtin.h"
#include "util/exec_cmd.h"
#include "common-cmds.h"
#include "util/parse-options.h"
#include "util/run-command.h"
#include "util/help.h"

static struct man_viewer_list {
	struct man_viewer_list *next;
	char name[FLEX_ARRAY];
} *man_viewer_list;

static struct man_viewer_info_list {
	struct man_viewer_info_list *next;
	const char *info;
	char name[FLEX_ARRAY];
} *man_viewer_info_list;

enum help_format {
	HELP_FORMAT_MAN,
	HELP_FORMAT_INFO,
	HELP_FORMAT_WEB,
};

static int show_all = 0;
static enum help_format help_format = HELP_FORMAT_MAN;
static struct option builtin_help_options[] = {
	OPT_BOOLEAN('a', "all", &show_all, "print all available commands"),
	OPT_SET_INT('m', "man", &help_format, "show man page", HELP_FORMAT_MAN),
	OPT_SET_INT('w', "web", &help_format, "show manual in web browser",
			HELP_FORMAT_WEB),
	OPT_SET_INT('i', "info", &help_format, "show info page",
			HELP_FORMAT_INFO),
	OPT_END(),
};

static const char * const builtin_help_usage[] = {
	"perf help [--all] [--man|--web|--info] [command]",
	NULL
};

static enum help_format parse_help_format(const char *format)
{
	if (!strcmp(format, "man"))
		return HELP_FORMAT_MAN;
	if (!strcmp(format, "info"))
		return HELP_FORMAT_INFO;
	if (!strcmp(format, "web") || !strcmp(format, "html"))
		return HELP_FORMAT_WEB;
	die("unrecognized help format '%s'", format);
}

static const char *get_man_viewer_info(const char *name)
{
	struct man_viewer_info_list *viewer;

	for (viewer = man_viewer_info_list; viewer; viewer = viewer->next) {
		if (!strcasecmp(name, viewer->name))
			return viewer->info;
	}
	return NULL;
}

static int check_emacsclient_version(void)
{
	struct strbuf buffer = STRBUF_INIT;
	struct child_process ec_process;
	const char *argv_ec[] = { "emacsclient", "--version", NULL };
	int version;

	/* emacsclient prints its version number on stderr */
	memset(&ec_process, 0, sizeof(ec_process));
	ec_process.argv = argv_ec;
	ec_process.err = -1;
	ec_process.stdout_to_stderr = 1;
	if (start_command(&ec_process)) {
		fprintf(stderr, "Failed to start emacsclient.\n");
		return -1;
	}
	strbuf_read(&buffer, ec_process.err, 20);
	close(ec_process.err);

	/*
	 * Don't bother checking return value, because "emacsclient --version"
	 * seems to always exits with code 1.
	 */
	finish_command(&ec_process);

	if (prefixcmp(buffer.buf, "emacsclient")) {
		fprintf(stderr, "Failed to parse emacsclient version.\n");
		strbuf_release(&buffer);
		return -1;
	}

	strbuf_remove(&buffer, 0, strlen("emacsclient"));
	version = atoi(buffer.buf);

	if (version < 22) {
		fprintf(stderr,
			"emacsclient version '%d' too old (< 22).\n",
			version);
		strbuf_release(&buffer);
		return -1;
	}

	strbuf_release(&buffer);
	return 0;
}

static void exec_woman_emacs(const char *path, const char *page)
{
	if (!check_emacsclient_version()) {
		/* This works only with emacsclient version >= 22. */
		struct strbuf man_page = STRBUF_INIT;

		if (!path)
			path = "emacsclient";
		strbuf_addf(&man_page, "(woman \"%s\")", page);
		execlp(path, "emacsclient", "-e", man_page.buf, NULL);
		warning("failed to exec '%s': %s", path, strerror(errno));
	}
}

static void exec_man_konqueror(const char *path, const char *page)
{
	const char *display = getenv("DISPLAY");
	if (display && *display) {
		struct strbuf man_page = STRBUF_INIT;
		const char *filename = "kfmclient";

		/* It's simpler to launch konqueror using kfmclient. */
		if (path) {
			const char *file = strrchr(path, '/');
			if (file && !strcmp(file + 1, "konqueror")) {
				char *new = strdup(path);
				char *dest = strrchr(new, '/');

				/* strlen("konqueror") == strlen("kfmclient") */
				strcpy(dest + 1, "kfmclient");
				path = new;
			}
			if (file)
				filename = file;
		} else
			path = "kfmclient";
		strbuf_addf(&man_page, "man:%s(1)", page);
		execlp(path, filename, "newTab", man_page.buf, NULL);
		warning("failed to exec '%s': %s", path, strerror(errno));
	}
}

static void exec_man_man(const char *path, const char *page)
{
	if (!path)
		path = "man";
	execlp(path, "man", page, NULL);
	warning("failed to exec '%s': %s", path, strerror(errno));
}

static void exec_man_cmd(const char *cmd, const char *page)
{
	struct strbuf shell_cmd = STRBUF_INIT;
	strbuf_addf(&shell_cmd, "%s %s", cmd, page);
	execl("/bin/sh", "sh", "-c", shell_cmd.buf, NULL);
	warning("failed to exec '%s': %s", cmd, strerror(errno));
}

static void add_man_viewer(const char *name)
{
	struct man_viewer_list **p = &man_viewer_list;
	size_t len = strlen(name);

	while (*p)
		p = &((*p)->next);
	*p = calloc(1, (sizeof(**p) + len + 1));
	strncpy((*p)->name, name, len);
}

static int supported_man_viewer(const char *name, size_t len)
{
	return (!strncasecmp("man", name, len) ||
		!strncasecmp("woman", name, len) ||
		!strncasecmp("konqueror", name, len));
}

static void do_add_man_viewer_info(const char *name,
				   size_t len,
				   const char *value)
{
	struct man_viewer_info_list *new = calloc(1, sizeof(*new) + len + 1);

	strncpy(new->name, name, len);
	new->info = strdup(value);
	new->next = man_viewer_info_list;
	man_viewer_info_list = new;
}

static int add_man_viewer_path(const char *name,
			       size_t len,
			       const char *value)
{
	if (supported_man_viewer(name, len))
		do_add_man_viewer_info(name, len, value);
	else
		warning("'%s': path for unsupported man viewer.\n"
			"Please consider using 'man.<tool>.cmd' instead.",
			name);

	return 0;
}

static int add_man_viewer_cmd(const char *name,
			      size_t len,
			      const char *value)
{
	if (supported_man_viewer(name, len))
		warning("'%s': cmd for supported man viewer.\n"
			"Please consider using 'man.<tool>.path' instead.",
			name);
	else
		do_add_man_viewer_info(name, len, value);

	return 0;
}

static int add_man_viewer_info(const char *var, const char *value)
{
	const char *name = var + 4;
	const char *subkey = strrchr(name, '.');

	if (!subkey)
		return error("Config with no key for man viewer: %s", name);

	if (!strcmp(subkey, ".path")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_path(name, subkey - name, value);
	}
	if (!strcmp(subkey, ".cmd")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_cmd(name, subkey - name, value);
	}

	warning("'%s': unsupported man viewer sub key.", subkey);
	return 0;
}

static int perf_help_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_format = parse_help_format(value);
		return 0;
	}
	if (!strcmp(var, "man.viewer")) {
		if (!value)
			return config_error_nonbool(var);
		add_man_viewer(value);
		return 0;
	}
	if (!prefixcmp(var, "man."))
		return add_man_viewer_info(var, value);

	return perf_default_config(var, value, cb);
}

static struct cmdnames main_cmds, other_cmds;

void list_common_cmds_help(void)
{
	unsigned int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		if (longest < strlen(common_cmds[i].name))
			longest = strlen(common_cmds[i].name);
	}

	puts(" The most commonly used perf commands are:");
	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		printf("   %s   ", common_cmds[i].name);
		mput_char(' ', longest - strlen(common_cmds[i].name));
		puts(common_cmds[i].help);
	}
}

static int is_perf_command(const char *s)
{
	return is_in_cmdlist(&main_cmds, s) ||
		is_in_cmdlist(&other_cmds, s);
}

static const char *prepend(const char *prefix, const char *cmd)
{
	size_t pre_len = strlen(prefix);
	size_t cmd_len = strlen(cmd);
	char *p = malloc(pre_len + cmd_len + 1);
	memcpy(p, prefix, pre_len);
	strcpy(p + pre_len, cmd);
	return p;
}

static const char *cmd_to_page(const char *perf_cmd)
{
	if (!perf_cmd)
		return "perf";
	else if (!prefixcmp(perf_cmd, "perf"))
		return perf_cmd;
	else if (is_perf_command(perf_cmd))
		return prepend("perf-", perf_cmd);
	else
		return prepend("perf-", perf_cmd);
}

static void setup_man_path(void)
{
	struct strbuf new_path = STRBUF_INIT;
	const char *old_path = getenv("MANPATH");

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	strbuf_addstr(&new_path, system_path(PERF_MAN_PATH));
	strbuf_addch(&new_path, ':');
	if (old_path)
		strbuf_addstr(&new_path, old_path);

	setenv("MANPATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static void exec_viewer(const char *name, const char *page)
{
	const char *info = get_man_viewer_info(name);

	if (!strcasecmp(name, "man"))
		exec_man_man(info, page);
	else if (!strcasecmp(name, "woman"))
		exec_woman_emacs(info, page);
	else if (!strcasecmp(name, "konqueror"))
		exec_man_konqueror(info, page);
	else if (info)
		exec_man_cmd(info, page);
	else
		warning("'%s': unknown man viewer.", name);
}

static void show_man_page(const char *perf_cmd)
{
	struct man_viewer_list *viewer;
	const char *page = cmd_to_page(perf_cmd);
	const char *fallback = getenv("PERF_MAN_VIEWER");

	setup_man_path();
	for (viewer = man_viewer_list; viewer; viewer = viewer->next)
		exec_viewer(viewer->name, page); /* will return when unable */

	if (fallback)
		exec_viewer(fallback, page);
	exec_viewer("man", page);
	die("no man viewer handled the request");
}

static void show_info_page(const char *perf_cmd)
{
	const char *page = cmd_to_page(perf_cmd);
	setenv("INFOPATH", system_path(PERF_INFO_PATH), 1);
	execlp("info", "info", "perfman", page, NULL);
}

static void get_html_page_path(struct strbuf *page_path, const char *page)
{
	struct stat st;
	const char *html_path = system_path(PERF_HTML_PATH);

	/* Check that we have a perf documentation directory. */
	if (stat(mkpath("%s/perf.html", html_path), &st)
	    || !S_ISREG(st.st_mode))
		die("'%s': not a documentation directory.", html_path);

	strbuf_init(page_path, 0);
	strbuf_addf(page_path, "%s/%s.html", html_path, page);
}

/*
 * If open_html is not defined in a platform-specific way (see for
 * example compat/mingw.h), we use the script web--browse to display
 * HTML.
 */
#ifndef open_html
static void open_html(const char *path)
{
	execl_perf_cmd("web--browse", "-c", "help.browser", path, NULL);
}
#endif

static void show_html_page(const char *perf_cmd)
{
	const char *page = cmd_to_page(perf_cmd);
	struct strbuf page_path; /* it leaks but we exec bellow */

	get_html_page_path(&page_path, page);

	open_html(page_path.buf);
}

int cmd_help(int argc, const char **argv, const char *prefix __used)
{
	const char *alias;

	load_command_list("perf-", &main_cmds, &other_cmds);

	perf_config(perf_help_config, NULL);

	argc = parse_options(argc, argv, builtin_help_options,
			builtin_help_usage, 0);

	if (show_all) {
		printf("\n usage: %s\n\n", perf_usage_string);
		list_commands("perf commands", &main_cmds, &other_cmds);
		printf(" %s\n\n", perf_more_info_string);
		return 0;
	}

	if (!argv[0]) {
		printf("\n usage: %s\n\n", perf_usage_string);
		list_common_cmds_help();
		printf("\n %s\n\n", perf_more_info_string);
		return 0;
	}

	alias = alias_lookup(argv[0]);
	if (alias && !is_perf_command(argv[0])) {
		printf("`perf %s' is aliased to `%s'\n", argv[0], alias);
		return 0;
	}

	switch (help_format) {
	case HELP_FORMAT_MAN:
		show_man_page(argv[0]);
		break;
	case HELP_FORMAT_INFO:
		show_info_page(argv[0]);
		break;
	case HELP_FORMAT_WEB:
		show_html_page(argv[0]);
	default:
		break;
	}

	return 0;
}
