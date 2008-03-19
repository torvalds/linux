#ifdef CONFIG_X86_32
# include "io_32.h"
#else
# include "io_64.h"
#endif
extern int ioremap_change_attr(unsigned long vaddr, unsigned long size,
				unsigned long prot_val);

