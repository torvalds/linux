#include "symbol.h"


int filename__read_build_id(const char *filename __used, void *bf __used,
			    size_t size __used)
{
	return -1;
}

int sysfs__read_build_id(const char *filename __used, void *build_id __used,
			 size_t size __used)
{
	return -1;
}

int filename__read_debuglink(const char *filename __used,
			     char *debuglink __used, size_t size __used)
{
	return -1;
}

int dso__synthesize_plt_symbols(struct dso *dso __used, char *name __used,
				struct map *map __used,
				symbol_filter_t filter __used)
{
	return 0;
}

int dso__load_sym(struct dso *dso __used, struct map *map __used,
		  const char *name __used, int fd __used,
		  symbol_filter_t filter __used, int kmodule __used,
		  int want_symtab __used)
{
	return 0;
}

void symbol__elf_init(void)
{
}
