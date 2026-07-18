/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_EISA_H
#define LINUX_DEVICE_ID_EISA_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

/* EISA */

#define EISA_SIG_LEN   8

/* The EISA signature, in ASCII form, null terminated */
struct eisa_device_id {
	char          sig[EISA_SIG_LEN];
	kernel_ulong_t driver_data;
};

#define EISA_DEVICE_MODALIAS_FMT "eisa:s%s"

#endif /* ifndef LINUX_DEVICE_ID_EISA_H */
