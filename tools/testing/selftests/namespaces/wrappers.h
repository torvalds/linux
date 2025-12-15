// SPDX-License-Identifier: GPL-2.0

#include <linux/nsfs.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __SELFTESTS_NAMESPACES_WRAPPERS_H__
#define __SELFTESTS_NAMESPACES_WRAPPERS_H__

#ifndef __NR_listns
	#if defined __alpha__
		#define __NR_listns 580
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_listns 4470
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_listns 6470
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_listns 5470
		#endif
	#else
		#define __NR_listns 470
	#endif
#endif

static inline int sys_listns(const struct ns_id_req *req, __u64 *ns_ids,
			     size_t nr_ns_ids, unsigned int flags)
{
	return syscall(__NR_listns, req, ns_ids, nr_ns_ids, flags);
}

#endif /* __SELFTESTS_NAMESPACES_WRAPPERS_H__ */
