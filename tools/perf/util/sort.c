#include "sort.h"
#include "hist.h"

regex_t		parent_regex;
const char	default_parent_pattern[] = "^sys_|^do_page_fault";
const char	*parent_pattern = default_parent_pattern;
const char	default_sort_order[] = "comm,dso,symbol";
const char	*sort_order = default_sort_order;
int		sort__need_collapse = 0;
int		sort__has_parent = 0;

enum sort_type	sort__first_dimension;

char * field_sep;

LIST_HEAD(hist_entry__sort_list);

static int repsep_snprintf(char *bf, size_t size, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vsnprintf(bf, size, fmt, ap);
	if (field_sep && n > 0) {
		char *sep = bf;

		while (1) {
			sep = strchr(sep, *field_sep);
			if (sep == NULL)
				break;
			*sep = '.';
		}
	}
	va_end(ap);

	if (n >= (int)size)
		return size - 1;
	return n;
}

static int64_t cmp_null(void *l, void *r)
{
	if (!l && !r)
		return 0;
	else if (!l)
		return -1;
	else
		return 1;
}

/* --sort pid */

static int64_t
sort__thread_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static int hist_entry__thread_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%*s:%5d", width,
			      self->thread->comm ?: "", self->thread->pid);
}

struct sort_entry sort_thread = {
	.se_header	= "Command:  Pid",
	.se_cmp		= sort__thread_cmp,
	.se_snprintf	= hist_entry__thread_snprintf,
	.se_width_idx	= HISTC_THREAD,
};

/* --sort comm */

static int64_t
sort__comm_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

static int64_t
sort__comm_collapse(struct hist_entry *left, struct hist_entry *right)
{
	char *comm_l = left->thread->comm;
	char *comm_r = right->thread->comm;

	if (!comm_l || !comm_r)
		return cmp_null(comm_l, comm_r);

	return strcmp(comm_l, comm_r);
}

static int hist_entry__comm_snprintf(struct hist_entry *self, char *bf,
				     size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%*s", width, self->thread->comm);
}

struct sort_entry sort_comm = {
	.se_header	= "Command",
	.se_cmp		= sort__comm_cmp,
	.se_collapse	= sort__comm_collapse,
	.se_snprintf	= hist_entry__comm_snprintf,
	.se_width_idx	= HISTC_COMM,
};

/* --sort dso */

static int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct dso *dso_l = left->ms.map ? left->ms.map->dso : NULL;
	struct dso *dso_r = right->ms.map ? right->ms.map->dso : NULL;
	const char *dso_name_l, *dso_name_r;

	if (!dso_l || !dso_r)
		return cmp_null(dso_l, dso_r);

	if (verbose) {
		dso_name_l = dso_l->long_name;
		dso_name_r = dso_r->long_name;
	} else {
		dso_name_l = dso_l->short_name;
		dso_name_r = dso_r->short_name;
	}

	return strcmp(dso_name_l, dso_name_r);
}

static int hist_entry__dso_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	if (self->ms.map && self->ms.map->dso) {
		const char *dso_name = !verbose ? self->ms.map->dso->short_name :
						  self->ms.map->dso->long_name;
		return repsep_snprintf(bf, size, "%-*s", width, dso_name);
	}

	return repsep_snprintf(bf, size, "%-*s", width, "[unknown]");
}

struct sort_entry sort_dso = {
	.se_header	= "Shared Object",
	.se_cmp		= sort__dso_cmp,
	.se_snprintf	= hist_entry__dso_snprintf,
	.se_width_idx	= HISTC_DSO,
};

/* --sort symbol */

static int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	u64 ip_l, ip_r;

	if (!left->ms.sym && !right->ms.sym)
		return right->level - left->level;

	if (!left->ms.sym || !right->ms.sym)
		return cmp_null(left->ms.sym, right->ms.sym);

	if (left->ms.sym == right->ms.sym)
		return 0;

	ip_l = left->ms.sym->start;
	ip_r = right->ms.sym->start;

	return (int64_t)(ip_r - ip_l);
}

static int hist_entry__sym_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width __used)
{
	size_t ret = 0;

	if (verbose) {
		char o = self->ms.map ? dso__symtab_origin(self->ms.map->dso) : '!';
		ret += repsep_snprintf(bf, size, "%-#*llx %c ",
				       BITS_PER_LONG / 4, self->ip, o);
	}

	if (!sort_dso.elide)
		ret += repsep_snprintf(bf + ret, size - ret, "[%c] ", self->level);

	if (self->ms.sym)
		ret += repsep_snprintf(bf + ret, size - ret, "%s",
				       self->ms.sym->name);
	else
		ret += repsep_snprintf(bf + ret, size - ret, "%-#*llx",
				       BITS_PER_LONG / 4, self->ip);

	return ret;
}

struct sort_entry sort_sym = {
	.se_header	= "Symbol",
	.se_cmp		= sort__sym_cmp,
	.se_snprintf	= hist_entry__sym_snprintf,
	.se_width_idx	= HISTC_SYMBOL,
};

/* --sort parent */

static int64_t
sort__parent_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct symbol *sym_l = left->parent;
	struct symbol *sym_r = right->parent;

	if (!sym_l || !sym_r)
		return cmp_null(sym_l, sym_r);

	return strcmp(sym_l->name, sym_r->name);
}

static int hist_entry__parent_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*s", width,
			      self->parent ? self->parent->name : "[other]");
}

struct sort_entry sort_parent = {
	.se_header	= "Parent symbol",
	.se_cmp		= sort__parent_cmp,
	.se_snprintf	= hist_entry__parent_snprintf,
	.se_width_idx	= HISTC_PARENT,
};

/* --sort cpu */

static int64_t
sort__cpu_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->cpu - left->cpu;
}

static int hist_entry__cpu_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*d", width, self->cpu);
}

struct sort_entry sort_cpu = {
	.se_header      = "CPU",
	.se_cmp	        = sort__cpu_cmp,
	.se_snprintf    = hist_entry__cpu_snprintf,
	.se_width_idx	= HISTC_CPU,
};

struct sort_dimension {
	const char		*name;
	struct sort_entry	*entry;
	int			taken;
};

static struct sort_dimension sort_dimensions[] = {
	{ .name = "pid",	.entry = &sort_thread,	},
	{ .name = "comm",	.entry = &sort_comm,	},
	{ .name = "dso",	.entry = &sort_dso,	},
	{ .name = "symbol",	.entry = &sort_sym,	},
	{ .name = "parent",	.entry = &sort_parent,	},
	{ .name = "cpu",	.entry = &sort_cpu,	},
};

int sort_dimension__add(const char *tok)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sort_dimensions); i++) {
		struct sort_dimension *sd = &sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sd->entry == &sort_parent) {
			int ret = regcomp(&parent_regex, parent_pattern, REG_EXTENDED);
			if (ret) {
				char err[BUFSIZ];

				regerror(ret, &parent_regex, err, sizeof(err));
				pr_err("Invalid regex: %s\n%s", parent_pattern, err);
				return -EINVAL;
			}
			sort__has_parent = 1;
		}

		if (sd->taken)
			return 0;

		if (sd->entry->se_collapse)
			sort__need_collapse = 1;

		if (list_empty(&hist_entry__sort_list)) {
			if (!strcmp(sd->name, "pid"))
				sort__first_dimension = SORT_PID;
			else if (!strcmp(sd->name, "comm"))
				sort__first_dimension = SORT_COMM;
			else if (!strcmp(sd->name, "dso"))
				sort__first_dimension = SORT_DSO;
			else if (!strcmp(sd->name, "symbol"))
				sort__first_dimension = SORT_SYM;
			else if (!strcmp(sd->name, "parent"))
				sort__first_dimension = SORT_PARENT;
			else if (!strcmp(sd->name, "cpu"))
				sort__first_dimension = SORT_CPU;
		}

		list_add_tail(&sd->entry->list, &hist_entry__sort_list);
		sd->taken = 1;

		return 0;
	}

	return -ESRCH;
}

void setup_sorting(const char * const usagestr[], const struct option *opts)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok) < 0) {
			error("Unknown --sort key: `%s'", tok);
			usage_with_options(usagestr, opts);
		}
	}

	free(str);
}

void sort_entry__setup_elide(struct sort_entry *self, struct strlist *list,
			     const char *list_name, FILE *fp)
{
	if (list && strlist__nr_entries(list) == 1) {
		if (fp != NULL)
			fprintf(fp, "# %s: %s\n", list_name,
				strlist__entry(list, 0)->s);
		self->elide = true;
	}
}
