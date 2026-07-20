/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_PCMCIA_H
#define LINUX_DEVICE_ID_PCMCIA_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* PCMCIA */

#define PCMCIA_DEV_ID_MATCH_MANF_ID	0x0001
#define PCMCIA_DEV_ID_MATCH_CARD_ID	0x0002
#define PCMCIA_DEV_ID_MATCH_FUNC_ID	0x0004
#define PCMCIA_DEV_ID_MATCH_FUNCTION	0x0008
#define PCMCIA_DEV_ID_MATCH_PROD_ID1	0x0010
#define PCMCIA_DEV_ID_MATCH_PROD_ID2	0x0020
#define PCMCIA_DEV_ID_MATCH_PROD_ID3	0x0040
#define PCMCIA_DEV_ID_MATCH_PROD_ID4	0x0080
#define PCMCIA_DEV_ID_MATCH_DEVICE_NO	0x0100
#define PCMCIA_DEV_ID_MATCH_FAKE_CIS	0x0200
#define PCMCIA_DEV_ID_MATCH_ANONYMOUS	0x0400

struct pcmcia_device_id {
	__u16		match_flags;

	__u16		manf_id;
	__u16		card_id;

	__u8		func_id;

	/* for real multi-function devices */
	__u8		function;

	/* for pseudo multi-function devices */
	__u8		device_no;

	__u32		prod_id_hash[4];

	/* not matched against in kernelspace */
	const char *	prod_id[4];

	/* not matched against */
	kernel_ulong_t	driver_info;
	char *		cisfile;
};

#endif /* ifndef LINUX_DEVICE_ID_PCMCIA_H */
