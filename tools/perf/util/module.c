#include "util.h"
#include "../perf.h"
#include "string.h"
#include "module.h"

#include <libelf.h>
#include <gelf.h>
#include <elf.h>
#include <dirent.h>
#include <sys/utsname.h>

static unsigned int crc32(const char *p, unsigned int len)
{
	int i;
	unsigned int crc = 0;

	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
	}
	return crc;
}

/* module section methods */

struct sec_dso *sec_dso__new_dso(const char *name)
{
	struct sec_dso *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		strcpy(self->name, name);
		self->secs = RB_ROOT;
		self->find_section = sec_dso__find_section;
	}

	return self;
}

static void sec_dso__delete_section(struct section *self)
{
	free(((void *)self));
}

void sec_dso__delete_sections(struct sec_dso *self)
{
	struct section *pos;
	struct rb_node *next = rb_first(&self->secs);

	while (next) {
		pos = rb_entry(next, struct section, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase(&pos->rb_node, &self->secs);
		sec_dso__delete_section(pos);
	}
}

void sec_dso__delete_self(struct sec_dso *self)
{
	sec_dso__delete_sections(self);
	free(self);
}

static void sec_dso__insert_section(struct sec_dso *self, struct section *sec)
{
	struct rb_node **p = &self->secs.rb_node;
	struct rb_node *parent = NULL;
	const u64 hash = sec->hash;
	struct section *s;

	while (*p != NULL) {
		parent = *p;
		s = rb_entry(parent, struct section, rb_node);
		if (hash < s->hash)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&sec->rb_node, parent, p);
	rb_insert_color(&sec->rb_node, &self->secs);
}

struct section *sec_dso__find_section(struct sec_dso *self, const char *name)
{
	struct rb_node *n;
	u64 hash;
	int len;

	if (self == NULL)
		return NULL;

	len = strlen(name);
	hash = crc32(name, len);

	n = self->secs.rb_node;

	while (n) {
		struct section *s = rb_entry(n, struct section, rb_node);

		if (hash < s->hash)
			n = n->rb_left;
		else if (hash > s->hash)
			n = n->rb_right;
		else {
			if (!strcmp(name, s->name))
				return s;
			else
				n = rb_next(&s->rb_node);
		}
	}

	return NULL;
}

static size_t sec_dso__fprintf_section(struct section *self, FILE *fp)
{
	return fprintf(fp, "name:%s vma:%llx path:%s\n",
		       self->name, self->vma, self->path);
}

size_t sec_dso__fprintf(struct sec_dso *self, FILE *fp)
{
	size_t ret = fprintf(fp, "dso: %s\n", self->name);

	struct rb_node *nd;
	for (nd = rb_first(&self->secs); nd; nd = rb_next(nd)) {
		struct section *pos = rb_entry(nd, struct section, rb_node);
		ret += sec_dso__fprintf_section(pos, fp);
	}

	return ret;
}

static struct section *section__new(const char *name, const char *path)
{
	struct section *self = calloc(1, sizeof(*self));

	if (!self)
		goto out_failure;

	self->name = calloc(1, strlen(name) + 1);
	if (!self->name)
		goto out_failure;

	self->path = calloc(1, strlen(path) + 1);
	if (!self->path)
		goto out_failure;

	strcpy(self->name, name);
	strcpy(self->path, path);
	self->hash = crc32(self->name, strlen(name));

	return self;

out_failure:
	if (self) {
		if (self->name)
			free(self->name);
		if (self->path)
			free(self->path);
		free(self);
	}

	return NULL;
}

/* module methods */

struct mod_dso *mod_dso__new_dso(const char *name)
{
	struct mod_dso *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		strcpy(self->name, name);
		self->mods = RB_ROOT;
		self->find_module = mod_dso__find_module;
	}

	return self;
}

static void mod_dso__delete_module(struct module *self)
{
	free(((void *)self));
}

void mod_dso__delete_modules(struct mod_dso *self)
{
	struct module *pos;
	struct rb_node *next = rb_first(&self->mods);

	while (next) {
		pos = rb_entry(next, struct module, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase(&pos->rb_node, &self->mods);
		mod_dso__delete_module(pos);
	}
}

void mod_dso__delete_self(struct mod_dso *self)
{
	mod_dso__delete_modules(self);
	free(self);
}

static void mod_dso__insert_module(struct mod_dso *self, struct module *mod)
{
	struct rb_node **p = &self->mods.rb_node;
	struct rb_node *parent = NULL;
	const u64 hash = mod->hash;
	struct module *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct module, rb_node);
		if (hash < m->hash)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&mod->rb_node, parent, p);
	rb_insert_color(&mod->rb_node, &self->mods);
}

struct module *mod_dso__find_module(struct mod_dso *self, const char *name)
{
	struct rb_node *n;
	u64 hash;
	int len;

	if (self == NULL)
		return NULL;

	len = strlen(name);
	hash = crc32(name, len);

	n = self->mods.rb_node;

	while (n) {
		struct module *m = rb_entry(n, struct module, rb_node);

		if (hash < m->hash)
			n = n->rb_left;
		else if (hash > m->hash)
			n = n->rb_right;
		else {
			if (!strcmp(name, m->name))
				return m;
			else
				n = rb_next(&m->rb_node);
		}
	}

	return NULL;
}

static size_t mod_dso__fprintf_module(struct module *self, FILE *fp)
{
	return fprintf(fp, "name:%s path:%s\n", self->name, self->path);
}

size_t mod_dso__fprintf(struct mod_dso *self, FILE *fp)
{
	struct rb_node *nd;
	size_t ret;

	ret = fprintf(fp, "dso: %s\n", self->name);

	for (nd = rb_first(&self->mods); nd; nd = rb_next(nd)) {
		struct module *pos = rb_entry(nd, struct module, rb_node);

		ret += mod_dso__fprintf_module(pos, fp);
	}

	return ret;
}

static struct module *module__new(const char *name, const char *path)
{
	struct module *self = calloc(1, sizeof(*self));

	if (!self)
		goto out_failure;

	self->name = calloc(1, strlen(name) + 1);
	if (!self->name)
		goto out_failure;

	self->path = calloc(1, strlen(path) + 1);
	if (!self->path)
		goto out_failure;

	strcpy(self->name, name);
	strcpy(self->path, path);
	self->hash = crc32(self->name, strlen(name));

	return self;

out_failure:
	if (self) {
		if (self->name)
			free(self->name);
		if (self->path)
			free(self->path);
		free(self);
	}

	return NULL;
}

static int mod_dso__load_sections(struct module *mod)
{
	int count = 0, path_len;
	struct dirent *entry;
	char *line = NULL;
	char *dir_path;
	DIR *dir;
	size_t n;

	path_len = strlen("/sys/module/");
	path_len += strlen(mod->name);
	path_len += strlen("/sections/");

	dir_path = calloc(1, path_len + 1);
	if (dir_path == NULL)
		goto out_failure;

	strcat(dir_path, "/sys/module/");
	strcat(dir_path, mod->name);
	strcat(dir_path, "/sections/");

	dir = opendir(dir_path);
	if (dir == NULL)
		goto out_free;

	while ((entry = readdir(dir))) {
		struct section *section;
		char *path, *vma;
		int line_len;
		FILE *file;

		if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name))
			continue;

		path = calloc(1, path_len + strlen(entry->d_name) + 1);
		if (path == NULL)
			break;
		strcat(path, dir_path);
		strcat(path, entry->d_name);

		file = fopen(path, "r");
		if (file == NULL) {
			free(path);
			break;
		}

		line_len = getline(&line, &n, file);
		if (line_len < 0) {
			free(path);
			fclose(file);
			break;
		}

		if (!line) {
			free(path);
			fclose(file);
			break;
		}

		line[--line_len] = '\0'; /* \n */

		vma = strstr(line, "0x");
		if (!vma) {
			free(path);
			fclose(file);
			break;
		}
		vma += 2;

		section = section__new(entry->d_name, path);
		if (!section) {
			fprintf(stderr, "load_sections: allocation error\n");
			free(path);
			fclose(file);
			break;
		}

		hex2u64(vma, &section->vma);
		sec_dso__insert_section(mod->sections, section);

		free(path);
		fclose(file);
		count++;
	}

	closedir(dir);
	free(line);
	free(dir_path);

	return count;

out_free:
	free(dir_path);

out_failure:
	return count;
}

static int mod_dso__load_module_paths(struct mod_dso *self)
{
	struct utsname uts;
	int count = 0, len;
	char *line = NULL;
	FILE *file;
	char *path;
	size_t n;

	if (uname(&uts) < 0)
		goto out_failure;

	len = strlen("/lib/modules/");
	len += strlen(uts.release);
	len += strlen("/modules.dep");

	path = calloc(1, len);
	if (path == NULL)
		goto out_failure;

	strcat(path, "/lib/modules/");
	strcat(path, uts.release);
	strcat(path, "/modules.dep");

	file = fopen(path, "r");
	free(path);
	if (file == NULL)
		goto out_failure;

	while (!feof(file)) {
		char *path, *name, *tmp;
		struct module *module;
		int line_len, len;

		line_len = getline(&line, &n, file);
		if (line_len < 0)
			break;

		if (!line)
			goto out_failure;

		line[--line_len] = '\0'; /* \n */

		path = strtok(line, ":");
		if (!path)
			goto out_failure;

		name = strdup(path);
		name = strtok(name, "/");

		tmp = name;

		while (tmp) {
			tmp = strtok(NULL, "/");
			if (tmp)
				name = tmp;
		}
		name = strsep(&name, ".");

		/* Quirk: replace '-' with '_' in sound modules */
		for (len = strlen(name); len; len--) {
			if (*(name+len) == '-')
				*(name+len) = '_';
		}

		module = module__new(name, path);
		if (!module) {
			fprintf(stderr, "load_module_paths: allocation error\n");
			goto out_failure;
		}
		mod_dso__insert_module(self, module);

		module->sections = sec_dso__new_dso("sections");
		if (!module->sections) {
			fprintf(stderr, "load_module_paths: allocation error\n");
			goto out_failure;
		}

		module->active = mod_dso__load_sections(module);

		if (module->active > 0)
			count++;
	}

	free(line);
	fclose(file);

	return count;

out_failure:
	return -1;
}

int mod_dso__load_modules(struct mod_dso *dso)
{
	int err;

	err = mod_dso__load_module_paths(dso);

	return err;
}
