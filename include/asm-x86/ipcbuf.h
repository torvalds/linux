#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ipcbuf_32.h"
# else
#  include "ipcbuf_64.h"
# endif
#else
# ifdef __i386__
#  include "ipcbuf_32.h"
# else
#  include "ipcbuf_64.h"
# endif
#endif
