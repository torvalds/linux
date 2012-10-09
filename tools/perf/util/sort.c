#include "sort.h"
#include "hist.h"

regex_t		parent_regex;
const char	default_parent_pattern[] = "^sys_|^do_page_fault";
const char	*parent_pattern = default_parent_pattern;
const char	default_sort_order[] = "comm,dso,symbol";
const char	*sort_order = default_sort_order;
int		sort__need_collapse = 0;
int		sort__has_parent = 0;
int		sort__has_sym = 0;
int		sort__branch_mode = -1; /* -1 = means not set */

enum sort_type	sort__first_dimension;

LIST_HEAD(hist_entry__sort_list);

static int repsep_snprintf(char *bf, size_t size, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vsnprintf(bf, size, fmt, ap);
	if (symbol_conf.field_sep && n > 0) {
		char *sep = bf;

		while (1) {
			sep = strchr(sep, *symbol_conf.field_sep);
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

static int64_t _sort__dso_cmp(struct map *map_l, struct map *map_r)
{
	struct dso *dso_l = map_l ? map_l->dso : NULL;
	struct dso *dso_r = map_r ? map_r->dso : NULL;
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
	return _sort__dso_cmp(left->ms.map, right->ms.map);
}


static int64_t _sort__sym_cmp(struct symbol *sym_l, struct symbol *sym_r,
			      u64 ip_l, u64 ip_r)
{
	if (!sym_l || !sym_r)
		return cmp_null(sym_l, sym_r);

	if (sym_l == sym_r)
		return 0;

	if (sym_l)
		ip_l = sym_l->start;
	if (sym_r)
		ip_r = sym_r->start;

	return (int64_t)(ip_r - ip_l);
}

static int _hist_entry__dso_snprintf(struct map *map, char *bf,
				     size_t size, unsigned int width)
{
	if (map && map->dso) {
		const char *dso_name = !verbose ? map->dso->short_name :
			map->dso->long_name;
		return repsep_snprintf(bf, size, "%-*s", width, dso_name);
	}

	return repsep_snprintf(bf, size, "%-*s", width, "[unknown]");
}

static int hist_entry__dso_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(self->ms.map, bf, size, width);
}

static int _hist_entry__sym_snprintf(struct map *map, struct symbol *sym,
				     u64 ip, char level, char *bf, size_t size,
				     unsigned int width __maybe_unused)
{
	size_t ret = 0;

	if (verbose) {
		char o = map ? dso__symtab_origin(map->dso) : '!';
		ret += repsep_snprintf(bf, size, "%-#*llx %c ",
				       BITS_PER_LONG / 4, ip, o);
	}

	ret += repsep_snprintf(bf + ret, size - ret, "[%c] ", level);
	if (sym)
		ret += repsep_snprintf(bf + ret, size - ret, "%-*s",
				       width - ret,
				       sym->name);
	else {
		size_t len = BITS_PER_LONG / 4;
		ret += repsep_snprintf(bf + ret, size - ret, "%-#.*llx",
				       len, ip);
		ret += repsep_snprintf(bf + ret, size - ret, "%-*s",
				       width - ret, "");
	}

	return ret;
}


struct sort_entry sort_dso = {
	.se_header	= "Shared Object",
	.se_cmp		= sort__dso_cmp,
	.se_snprintf	= hist_entry__dso_snprintf,
	.se_width_idx	= HISTC_DSO,
};

static int hist_entry__sym_snprintf(struct hist_entry *self, char *bf,
				    size_t size,
				    unsigned int width __maybe_unused)
{
	return _hist_entry__sym_snprintf(self->ms.map, self->ms.sym, self->ip,
					 self->level, bf, size, width);
}

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

	return _sort__sym_cmp(left->ms.sym, right->ms.sym, ip_l, ip_r);
}

struct sort_entry sort_sym = {
	.se_header	= "Symbol",
	.se_cmp		= sort__sym_cmp,
	.se_snprintf	= hist_entry__sym_snprintf,
	.se_width_idx	= HISTC_SYMBOL,
};

/* --sort srcline */

static int64_t
sort__srcline_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return (int64_t)(right->ip - left->ip);
}

static int hist_entry__srcline_snprintf(struct hist_entry *self, char *bf,
					size_t size,
					unsigned int width __maybe_unused)
{
	FILE *fp;
	char cmd[PATH_MAX + 2], *path = self->srcline, *nl;
	size_t line_len;

	if (path != NULL)
		goto out_path;

	snprintf(cmd, sizeof(cmd), "addr2line -e %s %016" PRIx64,
		 self->ms.map->dso->long_name, self->ip);
	fp = popen(cmd, "r");
	if (!fp)
		goto out_ip;

	if (getline(&path, &line_len, fp) < 0 || !line_len)
		goto out_ip;
	fclose(fp);
	self->srcline = strdup(path);
	if (self->srcline == NULL)
		goto out_ip;

	nl = strchr(self->srcline, '\n');
	if (nl != NULL)
		*nl = '\0';
	path = self->srcline;
out_path:
	return repsep_snprintf(bf, size, "%s", path);
out_ip:
	return repsep_snprintf(bf, size, "%-#*llx", BITS_PER_LONG / 4, self->ip);
}

struct sort_entry sort_srcline = {
	.se_header	= "Source:Line",
	.se_cmp		= sort__srcline_cmp,
	.se_snprintf	= hist_entry__srcline_snprintf,
	.se_width_idx	= HISTC_SRCLINE,
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

static int64_t
sort__dso_from_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return _sort__dso_cmp(left->branch_info->from.map,
			      right->branch_info->from.map);
}

static int hist_entry__dso_from_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(self->branch_info->from.map,
					 bf, size, width);
}

struct sort_entry sort_dso_from = {
	.se_header	= "Source Shared Object",
	.se_cmp		= sort__dso_from_cmp,
	.se_snprintf	= hist_entry__dso_from_snprintf,
	.se_width_idx	= HISTC_DSO_FROM,
};

static int64_t
sort__dso_to_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return _sort__dso_cmp(left->branch_info->to.map,
			      right->branch_info->to.map);
}

static int hist_entry__dso_to_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(self->branch_info->to.map,
					 bf, size, width);
}

static int64_t
sort__sym_from_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct addr_map_symbol *from_l = &left->branch_info->from;
	struct addr_map_symbol *from_r = &right->branch_info->from;

	if (!from_l->sym && !from_r->sym)
		return right->level - left->level;

	return _sort__sym_cmp(from_l->sym, from_r->sym, from_l->addr,
			     from_r->addr);
}

static int64_t
sort__sym_to_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct addr_map_symbol *to_l = &left->branch_info->to;
	struct addr_map_symbol *to_r = &right->branch_info->to;

	if (!to_l->sym && !to_r->sym)
		return right->level - left->level;

	return _sort__sym_cmp(to_l->sym, to_r->sym, to_l->addr, to_r->addr);
}

static int hist_entry__sym_from_snprintf(struct hist_entry *self, char *bf,
					size_t size,
					unsigned int width __maybe_unused)
{
	struct addr_map_symbol *from = &self->branch_info->from;
	return _hist_entry__sym_snprintf(from->map, from->sym, from->addr,
					 self->level, bf, size, width);

}

static int hist_entry__sym_to_snprintf(struct hist_entry *self, char *bf,
				       size_t size,
				       unsigned int width __maybe_unused)
{
	struct addr_map_symbol *to = &self->branch_info->to;
	return _hist_entry__sym_snprintf(to->map, to->sym, to->addr,
					 self->level, bf, size, width);

}

struct sort_entry sort_dso_to = {
	.se_header	= "Target Shared Object",
	.se_cmp		= sort__dso_to_cmp,
	.se_snprintf	= hist_entry__dso_to_snprintf,
	.se_width_idx	= HISTC_DSO_TO,
};

struct sort_entry sort_sym_from = {
	.se_header	= "Source Symbol",
	.se_cmp		= sort__sym_from_cmp,
	.se_snprintf	= hist_entry__sym_from_snprintf,
	.se_width_idx	= HISTC_SYMBOL_FROM,
};

struct sort_entry sort_sym_to = {
	.se_header	= "Target Symbol",
	.se_cmp		= sort__sym_to_cmp,
	.se_snprintf	= hist_entry__sym_to_snprintf,
	.se_width_idx	= HISTC_SYMBOL_TO,
};

static int64_t
sort__mispredict_cmp(struct hist_entry *left, struct hist_entry *right)
{
	const unsigned char mp = left->branch_info->flags.mispred !=
					right->branch_info->flags.mispred;
	const unsigned char p = left->branch_info->flags.predicted !=
					right->branch_info->flags.predicted;

	return mp || p;
}

static int hist_entry__mispredict_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width){
	static const char *out = "N/A";

	if (self->branch_info->flags.predicted)
		out = "N";
	else if (self->branch_info->flags.mispred)
		out = "Y";

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

struct sort_entry sort_mispredict = {
	.se_header	= "Branch Mispredicted",
	.se_cmp		= sort__mispredict_cmp,
	.se_snprintf	= hist_entry__mispredict_snprintf,
	.se_width_idx	= HISTC_MISPREDICT,
};

struct sort_dimension {
	const char		*name;
	struct sort_entry	*entry;
	int			taken;
};

#define DIM(d, n, func) [d] = { .name = n, .entry = &(func) }

static struct sort_dimension sort_dimensions[] = {
	DIM(SORT_PID, "pid", sort_thread),
	DIM(SORT_COMM, "comm", sort_comm),
	DIM(SORT_DSO, "dso", sort_dso),
	DIM(SORT_DSO_FROM, "dso_from", sort_dso_from),
	DIM(SORT_DSO_TO, "dso_to", sort_dso_to),
	DIM(SORT_SYM, "symbol", sort_sym),
	DIM(SORT_SYM_TO, "symbol_from", sort_sym_from),
	DIM(SORT_SYM_FROM, "symbol_to", sort_sym_to),
	DIM(SORT_PARENT, "parent", sort_parent),
	DIM(SORT_CPU, "cpu", sort_cpu),
	DIM(SORT_MISPREDICT, "mispredict", sort_mispredict),
	DIM(SORT_SRCLINE, "srcline", sort_srcline),
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
		} else if (sd->entry == &sort_sym ||
			   sd->entry == &sort_sym_from ||
			   sd->entry == &sort_sym_to) {
			sort__has_sym = 1;
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
			else if (!strcmp(sd->name, "symbol_from"))
				sort__first_dimension = SORT_SYM_FROM;
			else if (!strcmp(sd->name, "symbol_to"))
				sort__first_dimension = SORT_SYM_TO;
			else if (!strcmp(sd->name, "dso_from"))
				sort__first_dimension = SORT_DSO_FROM;
			else if (!strcmp(sd->name, "dso_to"))
				sort__first_dimension = SORT_DSO_TO;
			else if (!strcmp(sd->name, "mispredict"))
				sort__first_dimension = SORT_MISPREDICT;
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
