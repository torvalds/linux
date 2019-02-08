/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_FASTRPC_H__
#define __QCOM_FASTRPC_H__

#include <linux/types.h>

#define FASTRPC_IOCTL_INVOKE		_IOWR('R', 3, struct fastrpc_invoke)

struct fastrpc_invoke_args {
	__u64 ptr;
	__u64 length;
	__s32 fd;
	__u32 reserved;
};

struct fastrpc_invoke {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

#endif /* __QCOM_FASTRPC_H__ */
