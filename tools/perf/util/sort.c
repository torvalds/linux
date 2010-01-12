#include "sort.h"

regex_t		parent_regex;
char		default_parent_pattern[] = "^sys_|^do_page_fault";
char		*parent_pattern = default_parent_pattern;
char		default_sort_order[] = "comm,dso,symbol";
char		*sort_order = default_sort_order;
int		sort__need_collapse = 0;
int		sort__has_parent = 0;

enum sort_type	sort__first_dimension;

unsigned int dsos__col_width;
unsigned int comms__col_width;
unsigned int threads__col_width;
static unsigned int parent_symbol__col_width;
char * field_sep;

LIST_HEAD(hist_entry__sort_list);

struct sort_entry sort_thread = {
	.header = "Command:  Pid",
	.cmp	= sort__thread_cmp,
	.print	= sort__thread_print,
	.width	= &threads__col_width,
};

struct sort_entry sort_comm = {
	.header		= "Command",
	.cmp		= sort__comm_cmp,
	.collapse	= sort__comm_collapse,
	.print		= sort__comm_print,
	.width		= &comms__col_width,
};

struct sort_entry sort_dso = {
	.header = "Shared Object",
	.cmp	= sort__dso_cmp,
	.print	= sort__dso_print,
	.width	= &dsos__col_width,
};

struct sort_entry sort_sym = {
	.header = "Symbol",
	.cmp	= sort__sym_cmp,
	.print	= sort__sym_print,
};

struct sort_entry sort_parent = {
	.header = "Parent symbol",
	.cmp	= sort__parent_cmp,
	.print	= sort__parent_print,
	.width	= &parent_symbol__col_width,
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
};

int64_t cmp_null(void *l, void *r)
{
	if (!l && !r)
		return 0;
	else if (!l)
		return -1;
	else
		return 1;
}

/* --sort pid */

int64_t
sort__thread_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

int repsep_fprintf(FILE *fp, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	if (!field_sep)
		n = vfprintf(fp, fmt, ap);
	else {
		char *bf = NULL;
		n = vasprintf(&bf, fmt, ap);
		if (n > 0) {
			char *sep = bf;

			while (1) {
				sep = strchr(sep, *field_sep);
				if (sep == NULL)
					break;
				*sep = '.';
			}
		}
		fputs(bf, fp);
		free(bf);
	}
	va_end(ap);
	return n;
}

size_t
sort__thread_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%*s:%5d", width - 6,
			      self->thread->comm ?: "", self->thread->pid);
}

size_t
sort__comm_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%*s", width, self->thread->comm);
}

/* --sort dso */

int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct dso *dso_l = left->map ? left->map->dso : NULL;
	struct dso *dso_r = right->map ? right->map->dso : NULL;
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

size_t
sort__dso_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	if (self->map && self->map->dso) {
		const char *dso_name = !verbose ? self->map->dso->short_name :
						  self->map->dso->long_name;
		return repsep_fprintf(fp, "%-*s", width, dso_name);
	}

	return repsep_fprintf(fp, "%*llx", width, (u64)self->ip);
}

/* --sort symbol */

int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	u64 ip_l, ip_r;

	if (left->sym == right->sym)
		return 0;

	ip_l = left->sym ? left->sym->start : left->ip;
	ip_r = right->sym ? right->sym->start : right->ip;

	return (int64_t)(ip_r - ip_l);
}


size_t
sort__sym_print(FILE *fp, struct hist_entry *self, unsigned int width __used)
{
	size_t ret = 0;

	if (verbose) {
		char o = self->map ? dso__symtab_origin(self->map->dso) : '!';
		ret += repsep_fprintf(fp, "%#018llx %c ", (u64)self->ip, o);
	}

	ret += repsep_fprintf(fp, "[%c] ", self->level);
	if (self->sym)
		ret += repsep_fprintf(fp, "%s", self->sym->name);
	else
		ret += repsep_fprintf(fp, "%#016llx", (u64)self->ip);

	return ret;
}

/* --sort comm */

int64_t
sort__comm_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return right->thread->pid - left->thread->pid;
}

int64_t
sort__comm_collapse(struct hist_entry *left, struct hist_entry *right)
{
	char *comm_l = left->thread->comm;
	char *comm_r = right->thread->comm;

	if (!comm_l || !comm_r)
		return cmp_null(comm_l, comm_r);

	return strcmp(comm_l, comm_r);
}

/* --sort parent */

int64_t
sort__parent_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct symbol *sym_l = left->parent;
	struct symbol *sym_r = right->parent;

	if (!sym_l || !sym_r)
		return cmp_null(sym_l, sym_r);

	return strcmp(sym_l->name, sym_r->name);
}

size_t
sort__parent_print(FILE *fp, struct hist_entry *self, unsigned int width)
{
	return repsep_fprintf(fp, "%-*s", width,
			      self->parent ? self->parent->name : "[other]");
}

int sort_dimension__add(const char *tok)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sort_dimensions); i++) {
		struct sort_dimension *sd = &sort_dimensions[i];

		if (sd->taken)
			continue;

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sd->entry->collapse)
			sort__need_collapse = 1;

		if (sd->entry == &sort_parent) {
			int ret = regcomp(&parent_regex, parent_pattern, REG_EXTENDED);
			if (ret) {
				char err[BUFSIZ];

				regerror(ret, &parent_regex, err, sizeof(err));
				fprintf(stderr, "Invalid regex: %s\n%s",
					parent_pattern, err);
				exit(-1);
			}
			sort__has_parent = 1;
		}

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
