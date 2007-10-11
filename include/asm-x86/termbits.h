#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "termbits_32.h"
# else
#  include "termbits_64.h"
# endif
#else
# ifdef __i386__
#  include "termbits_32.h"
# else
#  include "termbits_64.h"
# endif
#endif
