/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __LIBBPF_STR_ERROR_H
#define __LIBBPF_STR_ERROR_H

#define STRERR_BUFSIZE  128

char *libbpf_strerror_r(int err, char *dst, int len);

#endif /* __LIBBPF_STR_ERROR_H */
