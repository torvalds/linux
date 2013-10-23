#include "sort.h"
#include "hist.h"
#include "symbol.h"

regex_t		parent_regex;
const char	default_parent_pattern[] = "^sys_|^do_page_fault";
const char	*parent_pattern = default_parent_pattern;
const char	default_sort_order[] = "comm,dso,symbol";
const char	*sort_order = default_sort_order;
regex_t		ignore_callees_regex;
int		have_ignore_callees = 0;
int		sort__need_collapse = 0;
int		sort__has_parent = 0;
int		sort__has_sym = 0;
enum sort_mode	sort__mode = SORT_MODE__NORMAL;

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
	return right->thread->tid - left->thread->tid;
}

static int hist_entry__thread_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%*s:%5d", width - 6,
			      self->thread->comm ?: "", self->thread->tid);
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
	return right->thread->tid - left->thread->tid;
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

static int64_t
sort__dso_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return _sort__dso_cmp(left->ms.map, right->ms.map);
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

struct sort_entry sort_dso = {
	.se_header	= "Shared Object",
	.se_cmp		= sort__dso_cmp,
	.se_snprintf	= hist_entry__dso_snprintf,
	.se_width_idx	= HISTC_DSO,
};

/* --sort symbol */

static int64_t _sort__sym_cmp(struct symbol *sym_l, struct symbol *sym_r)
{
	u64 ip_l, ip_r;

	if (!sym_l || !sym_r)
		return cmp_null(sym_l, sym_r);

	if (sym_l == sym_r)
		return 0;

	ip_l = sym_l->start;
	ip_r = sym_r->start;

	return (int64_t)(ip_r - ip_l);
}

static int64_t
sort__sym_cmp(struct hist_entry *left, struct hist_entry *right)
{
	int64_t ret;

	if (!left->ms.sym && !right->ms.sym)
		return right->level - left->level;

	/*
	 * comparing symbol address alone is not enough since it's a
	 * relative address within a dso.
	 */
	ret = sort__dso_cmp(left, right);
	if (ret != 0)
		return ret;

	return _sort__sym_cmp(left->ms.sym, right->ms.sym);
}

static int _hist_entry__sym_snprintf(struct map *map, struct symbol *sym,
				     u64 ip, char level, char *bf, size_t size,
				     unsigned int width)
{
	size_t ret = 0;

	if (verbose) {
		char o = map ? dso__symtab_origin(map->dso) : '!';
		ret += repsep_snprintf(bf, size, "%-#*llx %c ",
				       BITS_PER_LONG / 4 + 2, ip, o);
	}

	ret += repsep_snprintf(bf + ret, size - ret, "[%c] ", level);
	if (sym && map) {
		if (map->type == MAP__VARIABLE) {
			ret += repsep_snprintf(bf + ret, size - ret, "%s", sym->name);
			ret += repsep_snprintf(bf + ret, size - ret, "+0x%llx",
					ip - map->unmap_ip(map, sym->start));
			ret += repsep_snprintf(bf + ret, size - ret, "%-*s",
				       width - ret, "");
		} else {
			ret += repsep_snprintf(bf + ret, size - ret, "%-*s",
					       width - ret,
					       sym->name);
		}
	} else {
		size_t len = BITS_PER_LONG / 4;
		ret += repsep_snprintf(bf + ret, size - ret, "%-#.*llx",
				       len, ip);
		ret += repsep_snprintf(bf + ret, size - ret, "%-*s",
				       width - ret, "");
	}

	return ret;
}

static int hist_entry__sym_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__sym_snprintf(self->ms.map, self->ms.sym, self->ip,
					 self->level, bf, size, width);
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
	if (!left->srcline) {
		if (!left->ms.map)
			left->srcline = SRCLINE_UNKNOWN;
		else {
			struct map *map = left->ms.map;
			left->srcline = get_srcline(map->dso,
					    map__rip_2objdump(map, left->ip));
		}
	}
	if (!right->srcline) {
		if (!right->ms.map)
			right->srcline = SRCLINE_UNKNOWN;
		else {
			struct map *map = right->ms.map;
			right->srcline = get_srcline(map->dso,
					    map__rip_2objdump(map, right->ip));
		}
	}
	return strcmp(left->srcline, right->srcline);
}

static int hist_entry__srcline_snprintf(struct hist_entry *self, char *bf,
					size_t size,
					unsigned int width __maybe_unused)
{
	return repsep_snprintf(bf, size, "%s", self->srcline);
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
	return repsep_snprintf(bf, size, "%*d", width, self->cpu);
}

struct sort_entry sort_cpu = {
	.se_header      = "CPU",
	.se_cmp	        = sort__cpu_cmp,
	.se_snprintf    = hist_entry__cpu_snprintf,
	.se_width_idx	= HISTC_CPU,
};

/* sort keys for branch stacks */

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

	return _sort__sym_cmp(from_l->sym, from_r->sym);
}

static int64_t
sort__sym_to_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct addr_map_symbol *to_l = &left->branch_info->to;
	struct addr_map_symbol *to_r = &right->branch_info->to;

	if (!to_l->sym && !to_r->sym)
		return right->level - left->level;

	return _sort__sym_cmp(to_l->sym, to_r->sym);
}

static int hist_entry__sym_from_snprintf(struct hist_entry *self, char *bf,
					 size_t size, unsigned int width)
{
	struct addr_map_symbol *from = &self->branch_info->from;
	return _hist_entry__sym_snprintf(from->map, from->sym, from->addr,
					 self->level, bf, size, width);

}

static int hist_entry__sym_to_snprintf(struct hist_entry *self, char *bf,
				       size_t size, unsigned int width)
{
	struct addr_map_symbol *to = &self->branch_info->to;
	return _hist_entry__sym_snprintf(to->map, to->sym, to->addr,
					 self->level, bf, size, width);

}

struct sort_entry sort_dso_from = {
	.se_header	= "Source Shared Object",
	.se_cmp		= sort__dso_from_cmp,
	.se_snprintf	= hist_entry__dso_from_snprintf,
	.se_width_idx	= HISTC_DSO_FROM,
};

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

/* --sort daddr_sym */
static int64_t
sort__daddr_cmp(struct hist_entry *left, struct hist_entry *right)
{
	uint64_t l = 0, r = 0;

	if (left->mem_info)
		l = left->mem_info->daddr.addr;
	if (right->mem_info)
		r = right->mem_info->daddr.addr;

	return (int64_t)(r - l);
}

static int hist_entry__daddr_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	uint64_t addr = 0;
	struct map *map = NULL;
	struct symbol *sym = NULL;

	if (self->mem_info) {
		addr = self->mem_info->daddr.addr;
		map = self->mem_info->daddr.map;
		sym = self->mem_info->daddr.sym;
	}
	return _hist_entry__sym_snprintf(map, sym, addr, self->level, bf, size,
					 width);
}

static int64_t
sort__dso_daddr_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct map *map_l = NULL;
	struct map *map_r = NULL;

	if (left->mem_info)
		map_l = left->mem_info->daddr.map;
	if (right->mem_info)
		map_r = right->mem_info->daddr.map;

	return _sort__dso_cmp(map_l, map_r);
}

static int hist_entry__dso_daddr_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	struct map *map = NULL;

	if (self->mem_info)
		map = self->mem_info->daddr.map;

	return _hist_entry__dso_snprintf(map, bf, size, width);
}

static int64_t
sort__locked_cmp(struct hist_entry *left, struct hist_entry *right)
{
	union perf_mem_data_src data_src_l;
	union perf_mem_data_src data_src_r;

	if (left->mem_info)
		data_src_l = left->mem_info->data_src;
	else
		data_src_l.mem_lock = PERF_MEM_LOCK_NA;

	if (right->mem_info)
		data_src_r = right->mem_info->data_src;
	else
		data_src_r.mem_lock = PERF_MEM_LOCK_NA;

	return (int64_t)(data_src_r.mem_lock - data_src_l.mem_lock);
}

static int hist_entry__locked_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	const char *out;
	u64 mask = PERF_MEM_LOCK_NA;

	if (self->mem_info)
		mask = self->mem_info->data_src.mem_lock;

	if (mask & PERF_MEM_LOCK_NA)
		out = "N/A";
	else if (mask & PERF_MEM_LOCK_LOCKED)
		out = "Yes";
	else
		out = "No";

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

static int64_t
sort__tlb_cmp(struct hist_entry *left, struct hist_entry *right)
{
	union perf_mem_data_src data_src_l;
	union perf_mem_data_src data_src_r;

	if (left->mem_info)
		data_src_l = left->mem_info->data_src;
	else
		data_src_l.mem_dtlb = PERF_MEM_TLB_NA;

	if (right->mem_info)
		data_src_r = right->mem_info->data_src;
	else
		data_src_r.mem_dtlb = PERF_MEM_TLB_NA;

	return (int64_t)(data_src_r.mem_dtlb - data_src_l.mem_dtlb);
}

static const char * const tlb_access[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"L2",
	"Walker",
	"Fault",
};
#define NUM_TLB_ACCESS (sizeof(tlb_access)/sizeof(const char *))

static int hist_entry__tlb_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t l = 0, i;
	u64 m = PERF_MEM_TLB_NA;
	u64 hit, miss;

	out[0] = '\0';

	if (self->mem_info)
		m = self->mem_info->data_src.mem_dtlb;

	hit = m & PERF_MEM_TLB_HIT;
	miss = m & PERF_MEM_TLB_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_TLB_HIT|PERF_MEM_TLB_MISS);

	for (i = 0; m && i < NUM_TLB_ACCESS; i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		strncat(out, tlb_access[i], sz - l);
		l += strlen(tlb_access[i]);
	}
	if (*out == '\0')
		strcpy(out, "N/A");
	if (hit)
		strncat(out, " hit", sz - l);
	if (miss)
		strncat(out, " miss", sz - l);

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

static int64_t
sort__lvl_cmp(struct hist_entry *left, struct hist_entry *right)
{
	union perf_mem_data_src data_src_l;
	union perf_mem_data_src data_src_r;

	if (left->mem_info)
		data_src_l = left->mem_info->data_src;
	else
		data_src_l.mem_lvl = PERF_MEM_LVL_NA;

	if (right->mem_info)
		data_src_r = right->mem_info->data_src;
	else
		data_src_r.mem_lvl = PERF_MEM_LVL_NA;

	return (int64_t)(data_src_r.mem_lvl - data_src_l.mem_lvl);
}

static const char * const mem_lvl[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"LFB",
	"L2",
	"L3",
	"Local RAM",
	"Remote RAM (1 hop)",
	"Remote RAM (2 hops)",
	"Remote Cache (1 hop)",
	"Remote Cache (2 hops)",
	"I/O",
	"Uncached",
};
#define NUM_MEM_LVL (sizeof(mem_lvl)/sizeof(const char *))

static int hist_entry__lvl_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t i, l = 0;
	u64 m =  PERF_MEM_LVL_NA;
	u64 hit, miss;

	if (self->mem_info)
		m  = self->mem_info->data_src.mem_lvl;

	out[0] = '\0';

	hit = m & PERF_MEM_LVL_HIT;
	miss = m & PERF_MEM_LVL_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_LVL_HIT|PERF_MEM_LVL_MISS);

	for (i = 0; m && i < NUM_MEM_LVL; i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		strncat(out, mem_lvl[i], sz - l);
		l += strlen(mem_lvl[i]);
	}
	if (*out == '\0')
		strcpy(out, "N/A");
	if (hit)
		strncat(out, " hit", sz - l);
	if (miss)
		strncat(out, " miss", sz - l);

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

static int64_t
sort__snoop_cmp(struct hist_entry *left, struct hist_entry *right)
{
	union perf_mem_data_src data_src_l;
	union perf_mem_data_src data_src_r;

	if (left->mem_info)
		data_src_l = left->mem_info->data_src;
	else
		data_src_l.mem_snoop = PERF_MEM_SNOOP_NA;

	if (right->mem_info)
		data_src_r = right->mem_info->data_src;
	else
		data_src_r.mem_snoop = PERF_MEM_SNOOP_NA;

	return (int64_t)(data_src_r.mem_snoop - data_src_l.mem_snoop);
}

static const char * const snoop_access[] = {
	"N/A",
	"None",
	"Miss",
	"Hit",
	"HitM",
};
#define NUM_SNOOP_ACCESS (sizeof(snoop_access)/sizeof(const char *))

static int hist_entry__snoop_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t i, l = 0;
	u64 m = PERF_MEM_SNOOP_NA;

	out[0] = '\0';

	if (self->mem_info)
		m = self->mem_info->data_src.mem_snoop;

	for (i = 0; m && i < NUM_SNOOP_ACCESS; i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		strncat(out, snoop_access[i], sz - l);
		l += strlen(snoop_access[i]);
	}

	if (*out == '\0')
		strcpy(out, "N/A");

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

struct sort_entry sort_mispredict = {
	.se_header	= "Branch Mispredicted",
	.se_cmp		= sort__mispredict_cmp,
	.se_snprintf	= hist_entry__mispredict_snprintf,
	.se_width_idx	= HISTC_MISPREDICT,
};

static u64 he_weight(struct hist_entry *he)
{
	return he->stat.nr_events ? he->stat.weight / he->stat.nr_events : 0;
}

static int64_t
sort__local_weight_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return he_weight(left) - he_weight(right);
}

static int hist_entry__local_weight_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*llu", width, he_weight(self));
}

struct sort_entry sort_local_weight = {
	.se_header	= "Local Weight",
	.se_cmp		= sort__local_weight_cmp,
	.se_snprintf	= hist_entry__local_weight_snprintf,
	.se_width_idx	= HISTC_LOCAL_WEIGHT,
};

static int64_t
sort__global_weight_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return left->stat.weight - right->stat.weight;
}

static int hist_entry__global_weight_snprintf(struct hist_entry *self, char *bf,
					      size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*llu", width, self->stat.weight);
}

struct sort_entry sort_global_weight = {
	.se_header	= "Weight",
	.se_cmp		= sort__global_weight_cmp,
	.se_snprintf	= hist_entry__global_weight_snprintf,
	.se_width_idx	= HISTC_GLOBAL_WEIGHT,
};

struct sort_entry sort_mem_daddr_sym = {
	.se_header	= "Data Symbol",
	.se_cmp		= sort__daddr_cmp,
	.se_snprintf	= hist_entry__daddr_snprintf,
	.se_width_idx	= HISTC_MEM_DADDR_SYMBOL,
};

struct sort_entry sort_mem_daddr_dso = {
	.se_header	= "Data Object",
	.se_cmp		= sort__dso_daddr_cmp,
	.se_snprintf	= hist_entry__dso_daddr_snprintf,
	.se_width_idx	= HISTC_MEM_DADDR_SYMBOL,
};

struct sort_entry sort_mem_locked = {
	.se_header	= "Locked",
	.se_cmp		= sort__locked_cmp,
	.se_snprintf	= hist_entry__locked_snprintf,
	.se_width_idx	= HISTC_MEM_LOCKED,
};

struct sort_entry sort_mem_tlb = {
	.se_header	= "TLB access",
	.se_cmp		= sort__tlb_cmp,
	.se_snprintf	= hist_entry__tlb_snprintf,
	.se_width_idx	= HISTC_MEM_TLB,
};

struct sort_entry sort_mem_lvl = {
	.se_header	= "Memory access",
	.se_cmp		= sort__lvl_cmp,
	.se_snprintf	= hist_entry__lvl_snprintf,
	.se_width_idx	= HISTC_MEM_LVL,
};

struct sort_entry sort_mem_snoop = {
	.se_header	= "Snoop",
	.se_cmp		= sort__snoop_cmp,
	.se_snprintf	= hist_entry__snoop_snprintf,
	.se_width_idx	= HISTC_MEM_SNOOP,
};

static int64_t
sort__abort_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return left->branch_info->flags.abort !=
		right->branch_info->flags.abort;
}

static int hist_entry__abort_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	static const char *out = ".";

	if (self->branch_info->flags.abort)
		out = "A";
	return repsep_snprintf(bf, size, "%-*s", width, out);
}

struct sort_entry sort_abort = {
	.se_header	= "Transaction abort",
	.se_cmp		= sort__abort_cmp,
	.se_snprintf	= hist_entry__abort_snprintf,
	.se_width_idx	= HISTC_ABORT,
};

static int64_t
sort__in_tx_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return left->branch_info->flags.in_tx !=
		right->branch_info->flags.in_tx;
}

static int hist_entry__in_tx_snprintf(struct hist_entry *self, char *bf,
				    size_t size, unsigned int width)
{
	static const char *out = ".";

	if (self->branch_info->flags.in_tx)
		out = "T";

	return repsep_snprintf(bf, size, "%-*s", width, out);
}

struct sort_entry sort_in_tx = {
	.se_header	= "Branch in transaction",
	.se_cmp		= sort__in_tx_cmp,
	.se_snprintf	= hist_entry__in_tx_snprintf,
	.se_width_idx	= HISTC_IN_TX,
};

static int64_t
sort__transaction_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return left->transaction - right->transaction;
}

static inline char *add_str(char *p, const char *str)
{
	strcpy(p, str);
	return p + strlen(str);
}

static struct txbit {
	unsigned flag;
	const char *name;
	int skip_for_len;
} txbits[] = {
	{ PERF_TXN_ELISION,        "EL ",        0 },
	{ PERF_TXN_TRANSACTION,    "TX ",        1 },
	{ PERF_TXN_SYNC,           "SYNC ",      1 },
	{ PERF_TXN_ASYNC,          "ASYNC ",     0 },
	{ PERF_TXN_RETRY,          "RETRY ",     0 },
	{ PERF_TXN_CONFLICT,       "CON ",       0 },
	{ PERF_TXN_CAPACITY_WRITE, "CAP-WRITE ", 1 },
	{ PERF_TXN_CAPACITY_READ,  "CAP-READ ",  0 },
	{ 0, NULL, 0 }
};

int hist_entry__transaction_len(void)
{
	int i;
	int len = 0;

	for (i = 0; txbits[i].name; i++) {
		if (!txbits[i].skip_for_len)
			len += strlen(txbits[i].name);
	}
	len += 4; /* :XX<space> */
	return len;
}

static int hist_entry__transaction_snprintf(struct hist_entry *self, char *bf,
					    size_t size, unsigned int width)
{
	u64 t = self->transaction;
	char buf[128];
	char *p = buf;
	int i;

	buf[0] = 0;
	for (i = 0; txbits[i].name; i++)
		if (txbits[i].flag & t)
			p = add_str(p, txbits[i].name);
	if (t && !(t & (PERF_TXN_SYNC|PERF_TXN_ASYNC)))
		p = add_str(p, "NEITHER ");
	if (t & PERF_TXN_ABORT_MASK) {
		sprintf(p, ":%" PRIx64,
			(t & PERF_TXN_ABORT_MASK) >>
			PERF_TXN_ABORT_SHIFT);
		p += strlen(p);
	}

	return repsep_snprintf(bf, size, "%-*s", width, buf);
}

struct sort_entry sort_transaction = {
	.se_header	= "Transaction                ",
	.se_cmp		= sort__transaction_cmp,
	.se_snprintf	= hist_entry__transaction_snprintf,
	.se_width_idx	= HISTC_TRANSACTION,
};

struct sort_dimension {
	const char		*name;
	struct sort_entry	*entry;
	int			taken;
};

#define DIM(d, n, func) [d] = { .name = n, .entry = &(func) }

static struct sort_dimension common_sort_dimensions[] = {
	DIM(SORT_PID, "pid", sort_thread),
	DIM(SORT_COMM, "comm", sort_comm),
	DIM(SORT_DSO, "dso", sort_dso),
	DIM(SORT_SYM, "symbol", sort_sym),
	DIM(SORT_PARENT, "parent", sort_parent),
	DIM(SORT_CPU, "cpu", sort_cpu),
	DIM(SORT_SRCLINE, "srcline", sort_srcline),
	DIM(SORT_LOCAL_WEIGHT, "local_weight", sort_local_weight),
	DIM(SORT_GLOBAL_WEIGHT, "weight", sort_global_weight),
	DIM(SORT_TRANSACTION, "transaction", sort_transaction),
};

#undef DIM

#define DIM(d, n, func) [d - __SORT_BRANCH_STACK] = { .name = n, .entry = &(func) }

static struct sort_dimension bstack_sort_dimensions[] = {
	DIM(SORT_DSO_FROM, "dso_from", sort_dso_from),
	DIM(SORT_DSO_TO, "dso_to", sort_dso_to),
	DIM(SORT_SYM_FROM, "symbol_from", sort_sym_from),
	DIM(SORT_SYM_TO, "symbol_to", sort_sym_to),
	DIM(SORT_MISPREDICT, "mispredict", sort_mispredict),
	DIM(SORT_IN_TX, "in_tx", sort_in_tx),
	DIM(SORT_ABORT, "abort", sort_abort),
};

#undef DIM

#define DIM(d, n, func) [d - __SORT_MEMORY_MODE] = { .name = n, .entry = &(func) }

static struct sort_dimension memory_sort_dimensions[] = {
	DIM(SORT_MEM_DADDR_SYMBOL, "symbol_daddr", sort_mem_daddr_sym),
	DIM(SORT_MEM_DADDR_DSO, "dso_daddr", sort_mem_daddr_dso),
	DIM(SORT_MEM_LOCKED, "locked", sort_mem_locked),
	DIM(SORT_MEM_TLB, "tlb", sort_mem_tlb),
	DIM(SORT_MEM_LVL, "mem", sort_mem_lvl),
	DIM(SORT_MEM_SNOOP, "snoop", sort_mem_snoop),
};

#undef DIM

static void __sort_dimension__add(struct sort_dimension *sd, enum sort_type idx)
{
	if (sd->taken)
		return;

	if (sd->entry->se_collapse)
		sort__need_collapse = 1;

	if (list_empty(&hist_entry__sort_list))
		sort__first_dimension = idx;

	list_add_tail(&sd->entry->list, &hist_entry__sort_list);
	sd->taken = 1;
}

int sort_dimension__add(const char *tok)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(common_sort_dimensions); i++) {
		struct sort_dimension *sd = &common_sort_dimensions[i];

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
		} else if (sd->entry == &sort_sym) {
			sort__has_sym = 1;
		}

		__sort_dimension__add(sd, i);
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(bstack_sort_dimensions); i++) {
		struct sort_dimension *sd = &bstack_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sort__mode != SORT_MODE__BRANCH)
			return -EINVAL;

		if (sd->entry == &sort_sym_from || sd->entry == &sort_sym_to)
			sort__has_sym = 1;

		__sort_dimension__add(sd, i + __SORT_BRANCH_STACK);
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(memory_sort_dimensions); i++) {
		struct sort_dimension *sd = &memory_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sort__mode != SORT_MODE__MEMORY)
			return -EINVAL;

		if (sd->entry == &sort_mem_daddr_sym)
			sort__has_sym = 1;

		__sort_dimension__add(sd, i + __SORT_MEMORY_MODE);
		return 0;
	}

	return -ESRCH;
}

int setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);
	int ret = 0;

	if (str == NULL) {
		error("Not enough memory to setup sort keys");
		return -ENOMEM;
	}

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		ret = sort_dimension__add(tok);
		if (ret == -EINVAL) {
			error("Invalid --sort key: `%s'", tok);
			break;
		} else if (ret == -ESRCH) {
			error("Unknown --sort key: `%s'", tok);
			break;
		}
	}

	free(str);
	return ret;
}

static void sort_entry__setup_elide(struct sort_entry *self,
				    struct strlist *list,
				    const char *list_name, FILE *fp)
{
	if (list && strlist__nr_entries(list) == 1) {
		if (fp != NULL)
			fprintf(fp, "# %s: %s\n", list_name,
				strlist__entry(list, 0)->s);
		self->elide = true;
	}
}

void sort__setup_elide(FILE *output)
{
	sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
				"dso", output);
	sort_entry__setup_elide(&sort_comm, symbol_conf.comm_list,
				"comm", output);
	sort_entry__setup_elide(&sort_sym, symbol_conf.sym_list,
				"symbol", output);

	if (sort__mode == SORT_MODE__BRANCH) {
		sort_entry__setup_elide(&sort_dso_from,
					symbol_conf.dso_from_list,
					"dso_from", output);
		sort_entry__setup_elide(&sort_dso_to,
					symbol_conf.dso_to_list,
					"dso_to", output);
		sort_entry__setup_elide(&sort_sym_from,
					symbol_conf.sym_from_list,
					"sym_from", output);
		sort_entry__setup_elide(&sort_sym_to,
					symbol_conf.sym_to_list,
					"sym_to", output);
	} else if (sort__mode == SORT_MODE__MEMORY) {
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"symbol_daddr", output);
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"dso_daddr", output);
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"mem", output);
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"local_weight", output);
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"tlb", output);
		sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list,
					"snoop", output);
	}

}
