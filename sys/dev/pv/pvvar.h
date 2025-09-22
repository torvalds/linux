/*	$OpenBSD: pvvar.h,v 1.11 2023/01/07 06:40:21 asou Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_PV_PVVAR_H_
#define _DEV_PV_PVVAR_H_

struct pvbus_req {
	size_t			 pvr_keylen;
	char			*pvr_key;
	size_t			 pvr_valuelen;
	char			*pvr_value;
};

#define PVBUSIOC_KVREAD		_IOWR('V', 1, struct pvbus_req)
#define PVBUSIOC_KVWRITE	_IOWR('V', 2, struct pvbus_req)
#define PVBUSIOC_TYPE		_IOWR('V', 3, struct pvbus_req)

#define	PVBUS_KVOP_MAXSIZE	(64 * 1024)

#ifdef _KERNEL
enum {
	PVBUS_KVM,
	PVBUS_HYPERV,
	PVBUS_VMWARE,
	PVBUS_XEN,
	PVBUS_BHYVE,
	PVBUS_OPENBSD,

	PVBUS_MAX
};

enum {
	PVBUS_KVREAD,
	PVBUS_KVWRITE,
	PVBUS_KVLS
};

struct pvbus_hv {
	uint32_t		 hv_base;
	uint32_t		 hv_features;
	uint16_t		 hv_major;
	uint16_t		 hv_minor;

	void			*hv_arg;
	int			(*hv_kvop)(void *, int, char *, char *, size_t);
	void			(*hv_init_cpu)(struct pvbus_hv *);
};

struct pvbus_softc {
	struct device		 pvbus_dev;
	struct pvbus_hv		*pvbus_hv;
};

struct pvbus_attach_args {
	const char		*pvba_busname;
};

struct bus_dma_tag;

struct pv_attach_args {
	const char		*pva_busname;
	struct pvbus_hv		*pva_hv;
	struct bus_dma_tag	*pva_dmat;
};

void	 pvbus_identify(void);
int	 pvbus_probe(void);
void	 pvbus_init_cpu(void);
void	 pvbus_reboot(struct device *);
void	 pvbus_shutdown(struct device *);

#endif /* _KERNEL */
#endif /* _DEV_PV_PVBUS_H_ */
