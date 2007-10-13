#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "elf_32.h"
# else
#  include "elf_64.h"
# endif
#else
# ifdef __i386__
#  include "elf_32.h"
# else
#  include "elf_64.h"
# endif
#endif
