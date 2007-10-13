#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "termios_32.h"
# else
#  include "termios_64.h"
# endif
#else
# ifdef __i386__
#  include "termios_32.h"
# else
#  include "termios_64.h"
# endif
#endif
