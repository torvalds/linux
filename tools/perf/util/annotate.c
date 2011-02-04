/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-annotate.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "util.h"
#include "build-id.h"
#include "color.h"
#include "cache.h"
#include "symbol.h"
#include "debug.h"
#include "annotate.h"

static int symbol__alloc_hist(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	const int size = (sizeof(*notes->histogram) +
			  (sym->end - sym->start) * sizeof(u64));

	notes->histogram = zalloc(size);
	return notes->histogram == NULL ? -1 : 0;
}

int symbol__inc_addr_samples(struct symbol *sym, struct map *map, u64 addr)
{
	unsigned int sym_size, offset;
	struct annotation *notes;
	struct sym_hist *h;

	if (!sym || !map)
		return 0;

	notes = symbol__annotation(sym);
	if (notes->histogram == NULL && symbol__alloc_hist(sym) < 0)
		return -ENOMEM;

	sym_size = sym->end - sym->start;
	offset = addr - sym->start;

	pr_debug3("%s: addr=%#" PRIx64 "\n", __func__, map->unmap_ip(map, addr));

	if (offset >= sym_size)
		return 0;

	h = notes->histogram;
	h->sum++;
	h->addr[offset]++;

	pr_debug3("%#" PRIx64 " %s: period++ [addr: %#" PRIx64 ", %#" PRIx64
		  "] => %" PRIu64 "\n", sym->start, sym->name,
		  addr, addr - sym->start, h->addr[offset]);
	return 0;
}

static struct objdump_line *objdump_line__new(s64 offset, char *line, size_t privsize)
{
	struct objdump_line *self = malloc(sizeof(*self) + privsize);

	if (self != NULL) {
		self->offset = offset;
		self->line = line;
	}

	return self;
}

void objdump_line__free(struct objdump_line *self)
{
	free(self->line);
	free(self);
}

static void objdump__add_line(struct list_head *head, struct objdump_line *line)
{
	list_add_tail(&line->node, head);
}

struct objdump_line *objdump__get_next_ip_line(struct list_head *head,
					       struct objdump_line *pos)
{
	list_for_each_entry_continue(pos, head, node)
		if (pos->offset >= 0)
			return pos;

	return NULL;
}

static void objdump_line__print(struct objdump_line *oline,
				struct list_head *head,
				struct symbol *sym, u64 len)
{
	static const char *prev_line;
	static const char *prev_color;

	if (oline->offset != -1) {
		const char *path = NULL;
		unsigned int hits = 0;
		double percent = 0.0;
		const char *color;
		struct annotation *notes = symbol__annotation(sym);
		struct source_line *src_line = notes->src_line;
		struct sym_hist *h = notes->histogram;
		s64 offset = oline->offset;
		struct objdump_line *next = objdump__get_next_ip_line(head, oline);

		while (offset < (s64)len &&
		       (next == NULL || offset < next->offset)) {
			if (src_line) {
				if (path == NULL)
					path = src_line[offset].path;
				percent += src_line[offset].percent;
			} else
				hits += h->addr[offset];

			++offset;
		}

		if (src_line == NULL && h->sum)
			percent = 100.0 * hits / h->sum;

		color = get_percent_color(percent);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored addr with the same filename:line
		 */
		if (path) {
			if (!prev_line || strcmp(prev_line, path)
				       || color != prev_color) {
				color_fprintf(stdout, color, " %s", path);
				prev_line = path;
				prev_color = color;
			}
		}

		color_fprintf(stdout, color, " %7.2f", percent);
		printf(" :	");
		color_fprintf(stdout, PERF_COLOR_BLUE, "%s\n", oline->line);
	} else {
		if (!*oline->line)
			printf("         :\n");
		else
			printf("         :	%s\n", oline->line);
	}
}

static int symbol__parse_objdump_line(struct symbol *sym, struct map *map, FILE *file,
				      struct list_head *head, size_t privsize)
{
	struct objdump_line *objdump_line;
	char *line = NULL, *tmp, *tmp2, *c;
	size_t line_len;
	s64 line_ip, offset = -1;

	if (getline(&line, &line_len, file) < 0)
		return -1;

	if (!line)
		return -1;

	while (line_len != 0 && isspace(line[line_len - 1]))
		line[--line_len] = '\0';

	c = strchr(line, '\n');
	if (c)
		*c = 0;

	line_ip = -1;

	/*
	 * Strip leading spaces:
	 */
	tmp = line;
	while (*tmp) {
		if (*tmp != ' ')
			break;
		tmp++;
	}

	if (*tmp) {
		/*
		 * Parse hexa addresses followed by ':'
		 */
		line_ip = strtoull(tmp, &tmp2, 16);
		if (*tmp2 != ':' || tmp == tmp2 || tmp2[1] == '\0')
			line_ip = -1;
	}

	if (line_ip != -1) {
		u64 start = map__rip_2objdump(map, sym->start),
		    end = map__rip_2objdump(map, sym->end);

		offset = line_ip - start;
		if (offset < 0 || (u64)line_ip > end)
			offset = -1;
	}

	objdump_line = objdump_line__new(offset, line, privsize);
	if (objdump_line == NULL) {
		free(line);
		return -1;
	}
	objdump__add_line(head, objdump_line);

	return 0;
}

int symbol__annotate(struct symbol *sym, struct map *map,
		     struct list_head *head, size_t privsize)
{
	struct dso *dso = map->dso;
	char *filename = dso__build_id_filename(dso, NULL, 0);
	bool free_filename = true;
	char command[PATH_MAX * 2];
	FILE *file;
	int err = 0;
	u64 len;
	char symfs_filename[PATH_MAX];

	if (filename) {
		snprintf(symfs_filename, sizeof(symfs_filename), "%s%s",
			 symbol_conf.symfs, filename);
	}

	if (filename == NULL) {
		if (dso->has_build_id) {
			pr_err("Can't annotate %s: not enough memory\n",
			       sym->name);
			return -ENOMEM;
		}
		goto fallback;
	} else if (readlink(symfs_filename, command, sizeof(command)) < 0 ||
		   strstr(command, "[kernel.kallsyms]") ||
		   access(symfs_filename, R_OK)) {
		free(filename);
fallback:
		/*
		 * If we don't have build-ids or the build-id file isn't in the
		 * cache, or is just a kallsyms file, well, lets hope that this
		 * DSO is the same as when 'perf record' ran.
		 */
		filename = dso->long_name;
		snprintf(symfs_filename, sizeof(symfs_filename), "%s%s",
			 symbol_conf.symfs, filename);
		free_filename = false;
	}

	if (dso->origin == DSO__ORIG_KERNEL) {
		if (dso->annotate_warned)
			goto out_free_filename;
		err = -ENOENT;
		dso->annotate_warned = 1;
		pr_err("Can't annotate %s: No vmlinux file was found in the "
		       "path\n", sym->name);
		goto out_free_filename;
	}

	pr_debug("%s: filename=%s, sym=%s, start=%#" PRIx64 ", end=%#" PRIx64 "\n", __func__,
		 filename, sym->name, map->unmap_ip(map, sym->start),
		 map->unmap_ip(map, sym->end));

	len = sym->end - sym->start;

	pr_debug("annotating [%p] %30s : [%p] %30s\n",
		 dso, dso->long_name, sym, sym->name);

	snprintf(command, sizeof(command),
		 "objdump --start-address=0x%016" PRIx64
		 " --stop-address=0x%016" PRIx64 " -dS -C %s|grep -v %s|expand",
		 map__rip_2objdump(map, sym->start),
		 map__rip_2objdump(map, sym->end),
		 symfs_filename, filename);

	pr_debug("Executing: %s\n", command);

	file = popen(command, "r");
	if (!file)
		goto out_free_filename;

	while (!feof(file))
		if (symbol__parse_objdump_line(sym, map, file, head, privsize) < 0)
			break;

	pclose(file);
out_free_filename:
	if (free_filename)
		free(filename);
	return err;
}

static void insert_source_line(struct rb_root *root, struct source_line *src_line)
{
	struct source_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct source_line, node);

		if (src_line->percent > iter->percent)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&src_line->node, parent, p);
	rb_insert_color(&src_line->node, root);
}

static void symbol__free_source_line(struct symbol *sym, int len)
{
	struct annotation *notes = symbol__annotation(sym);
	struct source_line *src_line = notes->src_line;
	int i;

	for (i = 0; i < len; i++)
		free(src_line[i].path);

	free(src_line);
	notes->src_line = NULL;
}

/* Get the filename:line for the colored entries */
static int symbol__get_source_line(struct symbol *sym, struct map *map,
				   struct rb_root *root, int len,
				   const char *filename)
{
	u64 start;
	int i;
	char cmd[PATH_MAX * 2];
	struct source_line *src_line;
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = notes->histogram;

	if (!h->sum)
		return 0;

	src_line = notes->src_line = calloc(len, sizeof(struct source_line));
	if (!notes->src_line)
		return -1;

	start = map->unmap_ip(map, sym->start);

	for (i = 0; i < len; i++) {
		char *path = NULL;
		size_t line_len;
		u64 offset;
		FILE *fp;

		src_line[i].percent = 100.0 * h->addr[i] / h->sum;
		if (src_line[i].percent <= 0.5)
			continue;

		offset = start + i;
		sprintf(cmd, "addr2line -e %s %016" PRIx64, filename, offset);
		fp = popen(cmd, "r");
		if (!fp)
			continue;

		if (getline(&path, &line_len, fp) < 0 || !line_len)
			goto next;

		src_line[i].path = malloc(sizeof(char) * line_len + 1);
		if (!src_line[i].path)
			goto next;

		strcpy(src_line[i].path, path);
		insert_source_line(root, &src_line[i]);

	next:
		pclose(fp);
	}

	return 0;
}

static void print_summary(struct rb_root *root, const char *filename)
{
	struct source_line *src_line;
	struct rb_node *node;

	printf("\nSorted summary for file %s\n", filename);
	printf("----------------------------------------------\n\n");

	if (RB_EMPTY_ROOT(root)) {
		printf(" Nothing higher than %1.1f%%\n", MIN_GREEN);
		return;
	}

	node = rb_first(root);
	while (node) {
		double percent;
		const char *color;
		char *path;

		src_line = rb_entry(node, struct source_line, node);
		percent = src_line->percent;
		color = get_percent_color(percent);
		path = src_line->path;

		color_fprintf(stdout, color, " %7.2f %s", percent, path);
		node = rb_next(node);
	}
}

static void symbol__annotate_hits(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = notes->histogram;
	u64 len = sym->end - sym->start, offset;

	for (offset = 0; offset < len; ++offset)
		if (h->addr[offset] != 0)
			printf("%*" PRIx64 ": %" PRIu64 "\n", BITS_PER_LONG / 2,
			       sym->start + offset, h->addr[offset]);
	printf("%*s: %" PRIu64 "\n", BITS_PER_LONG / 2, "h->sum", h->sum);
}

int symbol__tty_annotate(struct symbol *sym, struct map *map, bool print_lines,
			 bool full_paths)
{
	struct dso *dso = map->dso;
	const char *filename = dso->long_name, *d_filename;
	struct rb_root source_line = RB_ROOT;
	struct objdump_line *pos, *n;
	LIST_HEAD(head);
	u64 len;

	if (symbol__annotate(sym, map, &head, 0) < 0)
		return -1;

	if (full_paths)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = sym->end - sym->start;

	if (print_lines) {
		symbol__get_source_line(sym, map, &source_line, len, filename);
		print_summary(&source_line, filename);
	}

	printf("\n\n------------------------------------------------\n");
	printf(" Percent |	Source code & Disassembly of %s\n", d_filename);
	printf("------------------------------------------------\n");

	if (verbose)
		symbol__annotate_hits(sym);

	list_for_each_entry_safe(pos, n, &head, node) {
		objdump_line__print(pos, &head, sym, len);
		list_del(&pos->node);
		objdump_line__free(pos);
	}

	if (print_lines)
		symbol__free_source_line(sym, len);

	return 0;
}
