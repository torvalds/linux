#include <libunwind.h>
#include <stdlib.h>

extern int UNW_OBJ(dwarf_search_unwind_table) (unw_addr_space_t as,
                                      unw_word_t ip,
                                      unw_dyn_info_t *di,
                                      unw_proc_info_t *pi,
                                      int need_unwind_info, void *arg);


#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)

static unw_accessors_t accessors;

int main(void)
{
	unw_addr_space_t addr_space;

	addr_space = unw_create_addr_space(&accessors, 0);
	if (addr_space)
		return 0;

	unw_init_remote(NULL, addr_space, NULL);
	dwarf_search_unwind_table(addr_space, 0, NULL, NULL, 0, NULL);

	return 0;
}
