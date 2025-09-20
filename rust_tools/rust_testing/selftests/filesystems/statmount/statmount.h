/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __STATMOUNT_H
#define __STATMOUNT_H

#include <stdint.h>
#include <linux/mount.h>
#include <asm/unistd.h>

#ifndef __NR_statmount
	#if defined __alpha__
		#define __NR_statmount 567
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_statmount 4457
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_statmount 6457
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_statmount 5457
		#endif
	#else
		#define __NR_statmount 457
	#endif
#endif

#ifndef __NR_listmount
	#if defined __alpha__
		#define __NR_listmount 568
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_listmount 4458
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_listmount 6458
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_listmount 5458
		#endif
	#else
		#define __NR_listmount 458
	#endif
#endif

static inline int statmount(uint64_t mnt_id, uint64_t mnt_ns_id, uint64_t mask,
			    struct statmount *buf, size_t bufsize,
			    unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = mask,
	};

	if (mnt_ns_id) {
		req.size = MNT_ID_REQ_SIZE_VER1;
		req.mnt_ns_id = mnt_ns_id;
	}

	return syscall(__NR_statmount, &req, buf, bufsize, flags);
}

static inline ssize_t listmount(uint64_t mnt_id, uint64_t mnt_ns_id,
			 uint64_t last_mnt_id, uint64_t list[], size_t num,
			 unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = last_mnt_id,
	};

	if (mnt_ns_id) {
		req.size = MNT_ID_REQ_SIZE_VER1;
		req.mnt_ns_id = mnt_ns_id;
	}

	return syscall(__NR_listmount, &req, list, num, flags);
}

#endif /* __STATMOUNT_H */
