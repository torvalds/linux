#include <sys/mman.h>
#include "sort.h"
#include "hist.h"
#include "comm.h"
#include "symbol.h"
#include "evsel.h"

regex_t		parent_regex;
const char	default_parent_pattern[] = "^sys_|^do_page_fault";
const char	*parent_pattern = default_parent_pattern;
const char	default_sort_order[] = "comm,dso,symbol";
const char	default_branch_sort_order[] = "comm,dso_from,symbol_from,dso_to,symbol_to";
const char	default_mem_sort_order[] = "local_weight,mem,sym,dso,symbol_daddr,dso_daddr,snoop,tlb,locked";
const char	default_top_sort_order[] = "dso,symbol";
const char	default_diff_sort_order[] = "dso,symbol";
const char	*sort_order;
const char	*field_order;
regex_t		ignore_callees_regex;
int		have_ignore_callees = 0;
int		sort__need_collapse = 0;
int		sort__has_parent = 0;
int		sort__has_sym = 0;
int		sort__has_dso = 0;
enum sort_mode	sort__mode = SORT_MODE__NORMAL;


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

static int64_t cmp_null(const void *l, const void *r)
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

static int hist_entry__thread_snprintf(struct hist_entry *he, char *bf,
				       size_t size, unsigned int width)
{
	const char *comm = thread__comm_str(he->thread);

	width = max(7U, width) - 6;
	return repsep_snprintf(bf, size, "%5d:%-*.*s", he->thread->tid,
			       width, width, comm ?: "");
}

struct sort_entry sort_thread = {
	.se_header	= "  Pid:Command",
	.se_cmp		= sort__thread_cmp,
	.se_snprintf	= hist_entry__thread_snprintf,
	.se_width_idx	= HISTC_THREAD,
};

/* --sort comm */

static int64_t
sort__comm_cmp(struct hist_entry *left, struct hist_entry *right)
{
	/* Compare the addr that should be unique among comm */
	return comm__str(right->comm) - comm__str(left->comm);
}

static int64_t
sort__comm_collapse(struct hist_entry *left, struct hist_entry *right)
{
	/* Compare the addr that should be unique among comm */
	return comm__str(right->comm) - comm__str(left->comm);
}

static int64_t
sort__comm_sort(struct hist_entry *left, struct hist_entry *right)
{
	return strcmp(comm__str(right->comm), comm__str(left->comm));
}

static int hist_entry__comm_snprintf(struct hist_entry *he, char *bf,
				     size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*.*s", width, width, comm__str(he->comm));
}

struct sort_entry sort_comm = {
	.se_header	= "Command",
	.se_cmp		= sort__comm_cmp,
	.se_collapse	= sort__comm_collapse,
	.se_sort	= sort__comm_sort,
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
		return cmp_null(dso_r, dso_l);

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
	return _sort__dso_cmp(right->ms.map, left->ms.map);
}

static int _hist_entry__dso_snprintf(struct map *map, char *bf,
				     size_t size, unsigned int width)
{
	if (map && map->dso) {
		const char *dso_name = !verbose ? map->dso->short_name :
			map->dso->long_name;
		return repsep_snprintf(bf, size, "%-*.*s", width, width, dso_name);
	}

	return repsep_snprintf(bf, size, "%-*.*s", width, width, "[unknown]");
}

static int hist_entry__dso_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(he->ms.map, bf, size, width);
}

struct sort_entry sort_dso = {
	.se_header	= "Shared Object",
	.se_cmp		= sort__dso_cmp,
	.se_snprintf	= hist_entry__dso_snprintf,
	.se_width_idx	= HISTC_DSO,
};

/* --sort symbol */

static int64_t _sort__addr_cmp(u64 left_ip, u64 right_ip)
{
	return (int64_t)(right_ip - left_ip);
}

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
		return _sort__addr_cmp(left->ip, right->ip);

	/*
	 * comparing symbol address alone is not enough since it's a
	 * relative address within a dso.
	 */
	if (!sort__has_dso) {
		ret = sort__dso_cmp(left, right);
		if (ret != 0)
			return ret;
	}

	return _sort__sym_cmp(left->ms.sym, right->ms.sym);
}

static int64_t
sort__sym_sort(struct hist_entry *left, struct hist_entry *right)
{
	if (!left->ms.sym || !right->ms.sym)
		return cmp_null(left->ms.sym, right->ms.sym);

	return strcmp(right->ms.sym->name, left->ms.sym->name);
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

	if (ret > width)
		bf[width] = '\0';

	return width;
}

static int hist_entry__sym_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__sym_snprintf(he->ms.map, he->ms.sym, he->ip,
					 he->level, bf, size, width);
}

struct sort_entry sort_sym = {
	.se_header	= "Symbol",
	.se_cmp		= sort__sym_cmp,
	.se_sort	= sort__sym_sort,
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
	return strcmp(right->srcline, left->srcline);
}

static int hist_entry__srcline_snprintf(struct hist_entry *he, char *bf,
					size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%*.*-s", width, width, he->srcline);
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

	return strcmp(sym_r->name, sym_l->name);
}

static int hist_entry__parent_snprintf(struct hist_entry *he, char *bf,
				       size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*.*s", width, width,
			      he->parent ? he->parent->name : "[other]");
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

static int hist_entry__cpu_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%*.*d", width, width, he->cpu);
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

static int hist_entry__dso_from_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(he->branch_info->from.map,
					 bf, size, width);
}

static int64_t
sort__dso_to_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return _sort__dso_cmp(left->branch_info->to.map,
			      right->branch_info->to.map);
}

static int hist_entry__dso_to_snprintf(struct hist_entry *he, char *bf,
				       size_t size, unsigned int width)
{
	return _hist_entry__dso_snprintf(he->branch_info->to.map,
					 bf, size, width);
}

static int64_t
sort__sym_from_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct addr_map_symbol *from_l = &left->branch_info->from;
	struct addr_map_symbol *from_r = &right->branch_info->from;

	if (!from_l->sym && !from_r->sym)
		return _sort__addr_cmp(from_l->addr, from_r->addr);

	return _sort__sym_cmp(from_l->sym, from_r->sym);
}

static int64_t
sort__sym_to_cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct addr_map_symbol *to_l = &left->branch_info->to;
	struct addr_map_symbol *to_r = &right->branch_info->to;

	if (!to_l->sym && !to_r->sym)
		return _sort__addr_cmp(to_l->addr, to_r->addr);

	return _sort__sym_cmp(to_l->sym, to_r->sym);
}

static int hist_entry__sym_from_snprintf(struct hist_entry *he, char *bf,
					 size_t size, unsigned int width)
{
	struct addr_map_symbol *from = &he->branch_info->from;
	return _hist_entry__sym_snprintf(from->map, from->sym, from->addr,
					 he->level, bf, size, width);

}

static int hist_entry__sym_to_snprintf(struct hist_entry *he, char *bf,
				       size_t size, unsigned int width)
{
	struct addr_map_symbol *to = &he->branch_info->to;
	return _hist_entry__sym_snprintf(to->map, to->sym, to->addr,
					 he->level, bf, size, width);

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

static int hist_entry__mispredict_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width){
	static const char *out = "N/A";

	if (he->branch_info->flags.predicted)
		out = "N";
	else if (he->branch_info->flags.mispred)
		out = "Y";

	return repsep_snprintf(bf, size, "%-*.*s", width, width, out);
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

static int hist_entry__daddr_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	uint64_t addr = 0;
	struct map *map = NULL;
	struct symbol *sym = NULL;

	if (he->mem_info) {
		addr = he->mem_info->daddr.addr;
		map = he->mem_info->daddr.map;
		sym = he->mem_info->daddr.sym;
	}
	return _hist_entry__sym_snprintf(map, sym, addr, he->level, bf, size,
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

static int hist_entry__dso_daddr_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	struct map *map = NULL;

	if (he->mem_info)
		map = he->mem_info->daddr.map;

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

static int hist_entry__locked_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	const char *out;
	u64 mask = PERF_MEM_LOCK_NA;

	if (he->mem_info)
		mask = he->mem_info->data_src.mem_lock;

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

static int hist_entry__tlb_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t l = 0, i;
	u64 m = PERF_MEM_TLB_NA;
	u64 hit, miss;

	out[0] = '\0';

	if (he->mem_info)
		m = he->mem_info->data_src.mem_dtlb;

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

static int hist_entry__lvl_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t i, l = 0;
	u64 m =  PERF_MEM_LVL_NA;
	u64 hit, miss;

	if (he->mem_info)
		m  = he->mem_info->data_src.mem_lvl;

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

static int hist_entry__snoop_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	char out[64];
	size_t sz = sizeof(out) - 1; /* -1 for null termination */
	size_t i, l = 0;
	u64 m = PERF_MEM_SNOOP_NA;

	out[0] = '\0';

	if (he->mem_info)
		m = he->mem_info->data_src.mem_snoop;

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

static inline  u64 cl_address(u64 address)
{
	/* return the cacheline of the address */
	return (address & ~(cacheline_size - 1));
}

static int64_t
sort__dcacheline_cmp(struct hist_entry *left, struct hist_entry *right)
{
	u64 l, r;
	struct map *l_map, *r_map;

	if (!left->mem_info)  return -1;
	if (!right->mem_info) return 1;

	/* group event types together */
	if (left->cpumode > right->cpumode) return -1;
	if (left->cpumode < right->cpumode) return 1;

	l_map = left->mem_info->daddr.map;
	r_map = right->mem_info->daddr.map;

	/* if both are NULL, jump to sort on al_addr instead */
	if (!l_map && !r_map)
		goto addr;

	if (!l_map) return -1;
	if (!r_map) return 1;

	if (l_map->maj > r_map->maj) return -1;
	if (l_map->maj < r_map->maj) return 1;

	if (l_map->min > r_map->min) return -1;
	if (l_map->min < r_map->min) return 1;

	if (l_map->ino > r_map->ino) return -1;
	if (l_map->ino < r_map->ino) return 1;

	if (l_map->ino_generation > r_map->ino_generation) return -1;
	if (l_map->ino_generation < r_map->ino_generation) return 1;

	/*
	 * Addresses with no major/minor numbers are assumed to be
	 * anonymous in userspace.  Sort those on pid then address.
	 *
	 * The kernel and non-zero major/minor mapped areas are
	 * assumed to be unity mapped.  Sort those on address.
	 */

	if ((left->cpumode != PERF_RECORD_MISC_KERNEL) &&
	    (!(l_map->flags & MAP_SHARED)) &&
	    !l_map->maj && !l_map->min && !l_map->ino &&
	    !l_map->ino_generation) {
		/* userspace anonymous */

		if (left->thread->pid_ > right->thread->pid_) return -1;
		if (left->thread->pid_ < right->thread->pid_) return 1;
	}

addr:
	/* al_addr does all the right addr - start + offset calculations */
	l = cl_address(left->mem_info->daddr.al_addr);
	r = cl_address(right->mem_info->daddr.al_addr);

	if (l > r) return -1;
	if (l < r) return 1;

	return 0;
}

static int hist_entry__dcacheline_snprintf(struct hist_entry *he, char *bf,
					  size_t size, unsigned int width)
{

	uint64_t addr = 0;
	struct map *map = NULL;
	struct symbol *sym = NULL;
	char level = he->level;

	if (he->mem_info) {
		addr = cl_address(he->mem_info->daddr.al_addr);
		map = he->mem_info->daddr.map;
		sym = he->mem_info->daddr.sym;

		/* print [s] for shared data mmaps */
		if ((he->cpumode != PERF_RECORD_MISC_KERNEL) &&
		     map && (map->type == MAP__VARIABLE) &&
		    (map->flags & MAP_SHARED) &&
		    (map->maj || map->min || map->ino ||
		     map->ino_generation))
			level = 's';
		else if (!map)
			level = 'X';
	}
	return _hist_entry__sym_snprintf(map, sym, addr, level, bf, size,
					 width);
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

static int hist_entry__local_weight_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*llu", width, he_weight(he));
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

static int hist_entry__global_weight_snprintf(struct hist_entry *he, char *bf,
					      size_t size, unsigned int width)
{
	return repsep_snprintf(bf, size, "%-*llu", width, he->stat.weight);
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

struct sort_entry sort_mem_dcacheline = {
	.se_header	= "Data Cacheline",
	.se_cmp		= sort__dcacheline_cmp,
	.se_snprintf	= hist_entry__dcacheline_snprintf,
	.se_width_idx	= HISTC_MEM_DCACHELINE,
};

static int64_t
sort__abort_cmp(struct hist_entry *left, struct hist_entry *right)
{
	return left->branch_info->flags.abort !=
		right->branch_info->flags.abort;
}

static int hist_entry__abort_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	static const char *out = ".";

	if (he->branch_info->flags.abort)
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

static int hist_entry__in_tx_snprintf(struct hist_entry *he, char *bf,
				    size_t size, unsigned int width)
{
	static const char *out = ".";

	if (he->branch_info->flags.in_tx)
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

static int hist_entry__transaction_snprintf(struct hist_entry *he, char *bf,
					    size_t size, unsigned int width)
{
	u64 t = he->transaction;
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
	DIM(SORT_MEM_DCACHELINE, "dcacheline", sort_mem_dcacheline),
};

#undef DIM

struct hpp_dimension {
	const char		*name;
	struct perf_hpp_fmt	*fmt;
	int			taken;
};

#define DIM(d, n) { .name = n, .fmt = &perf_hpp__format[d], }

static struct hpp_dimension hpp_sort_dimensions[] = {
	DIM(PERF_HPP__OVERHEAD, "overhead"),
	DIM(PERF_HPP__OVERHEAD_SYS, "overhead_sys"),
	DIM(PERF_HPP__OVERHEAD_US, "overhead_us"),
	DIM(PERF_HPP__OVERHEAD_GUEST_SYS, "overhead_guest_sys"),
	DIM(PERF_HPP__OVERHEAD_GUEST_US, "overhead_guest_us"),
	DIM(PERF_HPP__OVERHEAD_ACC, "overhead_children"),
	DIM(PERF_HPP__SAMPLES, "sample"),
	DIM(PERF_HPP__PERIOD, "period"),
};

#undef DIM

struct hpp_sort_entry {
	struct perf_hpp_fmt hpp;
	struct sort_entry *se;
};

bool perf_hpp__same_sort_entry(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	struct hpp_sort_entry *hse_a;
	struct hpp_sort_entry *hse_b;

	if (!perf_hpp__is_sort_entry(a) || !perf_hpp__is_sort_entry(b))
		return false;

	hse_a = container_of(a, struct hpp_sort_entry, hpp);
	hse_b = container_of(b, struct hpp_sort_entry, hpp);

	return hse_a->se == hse_b->se;
}

void perf_hpp__reset_sort_width(struct perf_hpp_fmt *fmt, struct hists *hists)
{
	struct hpp_sort_entry *hse;

	if (!perf_hpp__is_sort_entry(fmt))
		return;

	hse = container_of(fmt, struct hpp_sort_entry, hpp);
	hists__new_col_len(hists, hse->se->se_width_idx, strlen(fmt->name));
}

static int __sort__hpp_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			      struct perf_evsel *evsel)
{
	struct hpp_sort_entry *hse;
	size_t len = fmt->user_len;

	hse = container_of(fmt, struct hpp_sort_entry, hpp);

	if (!len)
		len = hists__col_len(&evsel->hists, hse->se->se_width_idx);

	return scnprintf(hpp->buf, hpp->size, "%-*.*s", len, len, fmt->name);
}

static int __sort__hpp_width(struct perf_hpp_fmt *fmt,
			     struct perf_hpp *hpp __maybe_unused,
			     struct perf_evsel *evsel)
{
	struct hpp_sort_entry *hse;
	size_t len = fmt->user_len;

	hse = container_of(fmt, struct hpp_sort_entry, hpp);

	if (!len)
		len = hists__col_len(&evsel->hists, hse->se->se_width_idx);

	return len;
}

static int __sort__hpp_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			     struct hist_entry *he)
{
	struct hpp_sort_entry *hse;
	size_t len = fmt->user_len;

	hse = container_of(fmt, struct hpp_sort_entry, hpp);

	if (!len)
		len = hists__col_len(he->hists, hse->se->se_width_idx);

	return hse->se->se_snprintf(he, hpp->buf, hpp->size, len);
}

static struct hpp_sort_entry *
__sort_dimension__alloc_hpp(struct sort_dimension *sd)
{
	struct hpp_sort_entry *hse;

	hse = malloc(sizeof(*hse));
	if (hse == NULL) {
		pr_err("Memory allocation failed\n");
		return NULL;
	}

	hse->se = sd->entry;
	hse->hpp.name = sd->entry->se_header;
	hse->hpp.header = __sort__hpp_header;
	hse->hpp.width = __sort__hpp_width;
	hse->hpp.entry = __sort__hpp_entry;
	hse->hpp.color = NULL;

	hse->hpp.cmp = sd->entry->se_cmp;
	hse->hpp.collapse = sd->entry->se_collapse ? : sd->entry->se_cmp;
	hse->hpp.sort = sd->entry->se_sort ? : hse->hpp.collapse;

	INIT_LIST_HEAD(&hse->hpp.list);
	INIT_LIST_HEAD(&hse->hpp.sort_list);
	hse->hpp.elide = false;
	hse->hpp.len = 0;
	hse->hpp.user_len = 0;

	return hse;
}

bool perf_hpp__is_sort_entry(struct perf_hpp_fmt *format)
{
	return format->header == __sort__hpp_header;
}

static int __sort_dimension__add_hpp_sort(struct sort_dimension *sd)
{
	struct hpp_sort_entry *hse = __sort_dimension__alloc_hpp(sd);

	if (hse == NULL)
		return -1;

	perf_hpp__register_sort_field(&hse->hpp);
	return 0;
}

static int __sort_dimension__add_hpp_output(struct sort_dimension *sd)
{
	struct hpp_sort_entry *hse = __sort_dimension__alloc_hpp(sd);

	if (hse == NULL)
		return -1;

	perf_hpp__column_register(&hse->hpp);
	return 0;
}

static int __sort_dimension__add(struct sort_dimension *sd)
{
	if (sd->taken)
		return 0;

	if (__sort_dimension__add_hpp_sort(sd) < 0)
		return -1;

	if (sd->entry->se_collapse)
		sort__need_collapse = 1;

	sd->taken = 1;

	return 0;
}

static int __hpp_dimension__add(struct hpp_dimension *hd)
{
	if (!hd->taken) {
		hd->taken = 1;

		perf_hpp__register_sort_field(hd->fmt);
	}
	return 0;
}

static int __sort_dimension__add_output(struct sort_dimension *sd)
{
	if (sd->taken)
		return 0;

	if (__sort_dimension__add_hpp_output(sd) < 0)
		return -1;

	sd->taken = 1;
	return 0;
}

static int __hpp_dimension__add_output(struct hpp_dimension *hd)
{
	if (!hd->taken) {
		hd->taken = 1;

		perf_hpp__column_register(hd->fmt);
	}
	return 0;
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
		} else if (sd->entry == &sort_dso) {
			sort__has_dso = 1;
		}

		return __sort_dimension__add(sd);
	}

	for (i = 0; i < ARRAY_SIZE(hpp_sort_dimensions); i++) {
		struct hpp_dimension *hd = &hpp_sort_dimensions[i];

		if (strncasecmp(tok, hd->name, strlen(tok)))
			continue;

		return __hpp_dimension__add(hd);
	}

	for (i = 0; i < ARRAY_SIZE(bstack_sort_dimensions); i++) {
		struct sort_dimension *sd = &bstack_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		if (sort__mode != SORT_MODE__BRANCH)
			return -EINVAL;

		if (sd->entry == &sort_sym_from || sd->entry == &sort_sym_to)
			sort__has_sym = 1;

		__sort_dimension__add(sd);
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

		__sort_dimension__add(sd);
		return 0;
	}

	return -ESRCH;
}

static const char *get_default_sort_order(void)
{
	const char *default_sort_orders[] = {
		default_sort_order,
		default_branch_sort_order,
		default_mem_sort_order,
		default_top_sort_order,
		default_diff_sort_order,
	};

	BUG_ON(sort__mode >= ARRAY_SIZE(default_sort_orders));

	return default_sort_orders[sort__mode];
}

static int setup_sort_order(void)
{
	char *new_sort_order;

	/*
	 * Append '+'-prefixed sort order to the default sort
	 * order string.
	 */
	if (!sort_order || is_strict_order(sort_order))
		return 0;

	if (sort_order[1] == '\0') {
		error("Invalid --sort key: `+'");
		return -EINVAL;
	}

	/*
	 * We allocate new sort_order string, but we never free it,
	 * because it's checked over the rest of the code.
	 */
	if (asprintf(&new_sort_order, "%s,%s",
		     get_default_sort_order(), sort_order + 1) < 0) {
		error("Not enough memory to set up --sort");
		return -ENOMEM;
	}

	sort_order = new_sort_order;
	return 0;
}

static int __setup_sorting(void)
{
	char *tmp, *tok, *str;
	const char *sort_keys;
	int ret = 0;

	ret = setup_sort_order();
	if (ret)
		return ret;

	sort_keys = sort_order;
	if (sort_keys == NULL) {
		if (is_strict_order(field_order)) {
			/*
			 * If user specified field order but no sort order,
			 * we'll honor it and not add default sort orders.
			 */
			return 0;
		}

		sort_keys = get_default_sort_order();
	}

	str = strdup(sort_keys);
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

void perf_hpp__set_elide(int idx, bool elide)
{
	struct perf_hpp_fmt *fmt;
	struct hpp_sort_entry *hse;

	perf_hpp__for_each_format(fmt) {
		if (!perf_hpp__is_sort_entry(fmt))
			continue;

		hse = container_of(fmt, struct hpp_sort_entry, hpp);
		if (hse->se->se_width_idx == idx) {
			fmt->elide = elide;
			break;
		}
	}
}

static bool __get_elide(struct strlist *list, const char *list_name, FILE *fp)
{
	if (list && strlist__nr_entries(list) == 1) {
		if (fp != NULL)
			fprintf(fp, "# %s: %s\n", list_name,
				strlist__entry(list, 0)->s);
		return true;
	}
	return false;
}

static bool get_elide(int idx, FILE *output)
{
	switch (idx) {
	case HISTC_SYMBOL:
		return __get_elide(symbol_conf.sym_list, "symbol", output);
	case HISTC_DSO:
		return __get_elide(symbol_conf.dso_list, "dso", output);
	case HISTC_COMM:
		return __get_elide(symbol_conf.comm_list, "comm", output);
	default:
		break;
	}

	if (sort__mode != SORT_MODE__BRANCH)
		return false;

	switch (idx) {
	case HISTC_SYMBOL_FROM:
		return __get_elide(symbol_conf.sym_from_list, "sym_from", output);
	case HISTC_SYMBOL_TO:
		return __get_elide(symbol_conf.sym_to_list, "sym_to", output);
	case HISTC_DSO_FROM:
		return __get_elide(symbol_conf.dso_from_list, "dso_from", output);
	case HISTC_DSO_TO:
		return __get_elide(symbol_conf.dso_to_list, "dso_to", output);
	default:
		break;
	}

	return false;
}

void sort__setup_elide(FILE *output)
{
	struct perf_hpp_fmt *fmt;
	struct hpp_sort_entry *hse;

	perf_hpp__for_each_format(fmt) {
		if (!perf_hpp__is_sort_entry(fmt))
			continue;

		hse = container_of(fmt, struct hpp_sort_entry, hpp);
		fmt->elide = get_elide(hse->se->se_width_idx, output);
	}

	/*
	 * It makes no sense to elide all of sort entries.
	 * Just revert them to show up again.
	 */
	perf_hpp__for_each_format(fmt) {
		if (!perf_hpp__is_sort_entry(fmt))
			continue;

		if (!fmt->elide)
			return;
	}

	perf_hpp__for_each_format(fmt) {
		if (!perf_hpp__is_sort_entry(fmt))
			continue;

		fmt->elide = false;
	}
}

static int output_field_add(char *tok)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(common_sort_dimensions); i++) {
		struct sort_dimension *sd = &common_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		return __sort_dimension__add_output(sd);
	}

	for (i = 0; i < ARRAY_SIZE(hpp_sort_dimensions); i++) {
		struct hpp_dimension *hd = &hpp_sort_dimensions[i];

		if (strncasecmp(tok, hd->name, strlen(tok)))
			continue;

		return __hpp_dimension__add_output(hd);
	}

	for (i = 0; i < ARRAY_SIZE(bstack_sort_dimensions); i++) {
		struct sort_dimension *sd = &bstack_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		return __sort_dimension__add_output(sd);
	}

	for (i = 0; i < ARRAY_SIZE(memory_sort_dimensions); i++) {
		struct sort_dimension *sd = &memory_sort_dimensions[i];

		if (strncasecmp(tok, sd->name, strlen(tok)))
			continue;

		return __sort_dimension__add_output(sd);
	}

	return -ESRCH;
}

static void reset_dimensions(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(common_sort_dimensions); i++)
		common_sort_dimensions[i].taken = 0;

	for (i = 0; i < ARRAY_SIZE(hpp_sort_dimensions); i++)
		hpp_sort_dimensions[i].taken = 0;

	for (i = 0; i < ARRAY_SIZE(bstack_sort_dimensions); i++)
		bstack_sort_dimensions[i].taken = 0;

	for (i = 0; i < ARRAY_SIZE(memory_sort_dimensions); i++)
		memory_sort_dimensions[i].taken = 0;
}

bool is_strict_order(const char *order)
{
	return order && (*order != '+');
}

static int __setup_output_field(void)
{
	char *tmp, *tok, *str, *strp;
	int ret = -EINVAL;

	if (field_order == NULL)
		return 0;

	reset_dimensions();

	strp = str = strdup(field_order);
	if (str == NULL) {
		error("Not enough memory to setup output fields");
		return -ENOMEM;
	}

	if (!is_strict_order(field_order))
		strp++;

	if (!strlen(strp)) {
		error("Invalid --fields key: `+'");
		goto out;
	}

	for (tok = strtok_r(strp, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		ret = output_field_add(tok);
		if (ret == -EINVAL) {
			error("Invalid --fields key: `%s'", tok);
			break;
		} else if (ret == -ESRCH) {
			error("Unknown --fields key: `%s'", tok);
			break;
		}
	}

out:
	free(str);
	return ret;
}

int setup_sorting(void)
{
	int err;

	err = __setup_sorting();
	if (err < 0)
		return err;

	if (parent_pattern != default_parent_pattern) {
		err = sort_dimension__add("parent");
		if (err < 0)
			return err;
	}

	reset_dimensions();

	/*
	 * perf diff doesn't use default hpp output fields.
	 */
	if (sort__mode != SORT_MODE__DIFF)
		perf_hpp__init();

	err = __setup_output_field();
	if (err < 0)
		return err;

	/* copy sort keys to output fields */
	perf_hpp__setup_output_field();
	/* and then copy output fields to sort keys */
	perf_hpp__append_sort_keys();

	return 0;
}

void reset_output_field(void)
{
	sort__need_collapse = 0;
	sort__has_parent = 0;
	sort__has_sym = 0;
	sort__has_dso = 0;

	field_order = NULL;
	sort_order = NULL;

	reset_dimensions();
	perf_hpp__reset_output_field();
}
