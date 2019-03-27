#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/assym.h>
#include <sys/resource.h>
#include <sys/systm.h>

#include <amd64/linux32/linux.h>
#include <compat/linux/linux_mib.h>

ASSYM(LINUX_SIGF_HANDLER, offsetof(struct l_sigframe, sf_handler));
ASSYM(LINUX_SIGF_SC, offsetof(struct l_sigframe, sf_sc));
ASSYM(LINUX_RT_SIGF_HANDLER, offsetof(struct l_rt_sigframe, sf_handler));
ASSYM(LINUX_RT_SIGF_UC, offsetof(struct l_rt_sigframe, sf_sc));
ASSYM(LINUX_RT_SIGF_SC, offsetof(struct l_ucontext, uc_mcontext));
ASSYM(LINUX_VERSION_CODE, LINUX_VERSION_CODE);
ASSYM(LINUX_SC_ESP, offsetof(struct l_sigcontext, sc_esp));
