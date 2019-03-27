/*-
 * Copyright (c) 2017 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_EFIVAR_DP_H_
#define	_EFIVAR_DP_H_

/*
 * "Linux compatible" efivar-dp.h header. At the moment, it's really a
 * very thin, minimal interface.
 */

/*
 * Generic EFI_DEVICE_PATH, spelled the Linux way. We use this
 * interface to the outside world and type-pun to the EFI EDK2 code
 * we use to implement it.
 */
typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
} __packed efidp_header;

/* NB: Linux has shadow types for all dp type */

typedef union {
        efidp_header header;
} efidp_data;
typedef efidp_data *efidp;
typedef const efidp_data *const_efidp;

/** format a device path into UEFI standard conforming output.
 *
 * NB: FreeBSD's implementation is taken from EDK2, while Linux's
 * was hand-rolled. There may be differences as a result.
 */
ssize_t efidp_format_device_path(char *buf, size_t len, const_efidp dp,
    ssize_t max);
ssize_t efidp_format_device_path_node(char *buf, size_t len, const_efidp dp);
ssize_t efidp_parse_device_path(char *path, efidp out, size_t max);
char * efidp_extract_file_path(const_efidp dp);

size_t efidp_size(const_efidp);

int efivar_device_path_to_unix_path(const_efidp dp, char **dev, char **relpath, char **abspath);
int efivar_unix_path_to_device_path(const char *path, efidp *dp);

#endif /* _EFIVAR_DP_H_ */
