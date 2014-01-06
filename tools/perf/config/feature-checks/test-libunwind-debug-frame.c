#include <libunwind.h>
#include <stdlib.h>

extern int
UNW_OBJ(dwarf_find_debug_frame) (int found, unw_dyn_info_t *di_debug,
				 unw_word_t ip, unw_word_t segbase,
				 const char *obj_name, unw_word_t start,
				 unw_word_t end);

#define dwarf_find_debug_frame UNW_OBJ(dwarf_find_debug_frame)

int main(void)
{
	dwarf_find_debug_frame(0, NULL, 0, 0, NULL, 0, 0);
	return 0;
}
