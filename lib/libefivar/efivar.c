/*-
 * Copyright (c) 2016 Netflix, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <efivar.h>
#include <sys/efiio.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efichar.h"

static int efi_fd = -2;

#define Z { 0, 0, 0, 0, 0, { 0 } }

const efi_guid_t efi_guid_empty = Z;

static struct uuid_table guid_tbl [] =
{
	{ "00000000-0000-0000-0000-000000000000", "zero", Z },
	{ "093e0fae-a6c4-4f50-9f1b-d41e2b89c19a", "sha512", Z },
	{ "0abba7dc-e516-4167-bbf5-4d9d1c739416", "redhat", Z },
	{ "0b6e5233-a65c-44c9-9407-d9ab83bfc8bd", "sha224", Z },
	{ "126a762d-5758-4fca-8531-201a7f57f850", "lenovo_boot_menu", Z },
	{ "3bd2a492-96c0-4079-b420-fcf98ef103ed", "x509_sha256", Z },
	{ "3c5766e8-269c-4e34-aa14-ed776e85b3b6", "rsa2048", Z },
	{ "3CC24E96-22C7-41D8-8863-8E39DCDCC2CF", "lenovo", Z },
	{ "3f7e615b-0d45-4f80-88dc-26b234958560", "lenovo_diag", Z },
	{ "446dbf63-2502-4cda-bcfa-2465d2b0fe9d", "x509_sha512", Z },
	{ "4aafd29d-68df-49ee-8aa9-347d375665a7", "pkcs7_cert", Z },
	{ "605dab50-e046-4300-abb6-3dd810dd8b23", "shim", Z },
	{ "665d3f60-ad3e-4cad-8e26-db46eee9f1b5", "lenovo_rescue", Z },
	{ "67f8444f-8743-48f1-a328-1eaab8736080", "rsa2048_sha1", Z },
	{ "7076876e-80c2-4ee6-aad2-28b349a6865b", "x509_sha384", Z },
	{ "721c8b66-426c-4e86-8e99-3457c46ab0b9", "lenovo_setup", Z },
	{ "77fa9abd-0359-4d32-bd60-28f4e78f784b", "microsoft", Z },
	{ "7FACC7B6-127F-4E9C-9C5D-080F98994345", "lenovo_2", Z },
	{ "826ca512-cf10-4ac9-b187-be01496631bd", "sha1", Z },
	{ "82988420-7467-4490-9059-feb448dd1963", "lenovo_me_config", Z },
	{ "8be4df61-93ca-11d2-aa0d-00e098032b8c", "global", Z },
	{ "a5c059a1-94e4-4aa7-87b5-ab155c2bf072", "x509_cert", Z },
	{ "a7717414-c616-4977-9420-844712a735bf", "rsa2048_sha256_cert", Z },
	{ "a7d8d9a6-6ab0-4aeb-ad9d-163e59a7a380", "lenovo_diag_splash", Z },
	{ "ade9e48f-9cb8-98e6-31af-b4e6009e2fe3", "redhat_2", Z },
	{ "bc7838d2-0f82-4d60-8316-c068ee79d25b", "lenovo_msg", Z },
	{ "c1c41626-504c-4092-aca9-41f936934328", "sha256", Z },
	{ "c57ad6b7-0515-40a8-9d21-551652854e37", "shell", Z },
	{ "d719b2cb-3d3a-4596-a3bc-dad00e67656f", "security", Z },
	{ "e2b36190-879b-4a3d-ad8d-f2e7bba32784", "rsa2048_sha256", Z },
	{ "ff3e5307-9fd0-48c9-85f1-8ad56c701e01", "sha384", Z },
	{ "f46ee6f4-4785-43a3-923d-7f786c3c8479", "lenovo_startup_interrupt", Z },
	{ "ffffffff-ffff-ffff-ffff-ffffffffffff", "zzignore-this-guid", Z },
};
#undef Z

static void
efi_guid_tbl_compile(void)
{
	size_t i;
	uint32_t status;
	static int done = 0;

	if (done)
		return;
	for (i = 0; i < nitems(guid_tbl); i++) {
		uuid_from_string(guid_tbl[i].uuid_str, &guid_tbl[i].guid,
		    &status);
		/* all f's is a bad version, so ignore that error */
		if (status != uuid_s_ok && status != uuid_s_bad_version)
			fprintf(stderr, "Can't convert %s to a uuid for %s: %d\n",
			    guid_tbl[i].uuid_str, guid_tbl[i].name, (int)status);
	}
	done = 1;
}

int
efi_known_guid(struct uuid_table **tbl)
{

	*tbl = guid_tbl;
	return (nitems(guid_tbl));
}

static int
efi_open_dev(void)
{

	if (efi_fd == -2)
		efi_fd = open("/dev/efi", O_RDWR);
	if (efi_fd < 0)
		efi_fd = -1;
	else
		efi_guid_tbl_compile();
	return (efi_fd);
}

static void
efi_var_reset(struct efi_var_ioc *var)
{
	var->name = NULL;
	var->namesize = 0;
	memset(&var->vendor, 0, sizeof(var->vendor));
	var->attrib = 0;
	var->data = NULL;
	var->datasize = 0;
}

static int
rv_to_linux_rv(int rv)
{
	if (rv == 0)
		rv = 1;
	else
		rv = -errno;
	return (rv);
}

int
efi_append_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes)
{

	return efi_set_variable(guid, name, data, data_size,
	    attributes | EFI_VARIABLE_APPEND_WRITE);
}

int
efi_del_variable(efi_guid_t guid, const char *name)
{

	/* data_size of 0 deletes the variable */
	return efi_set_variable(guid, name, NULL, 0, 0);
}

int
efi_get_variable(efi_guid_t guid, const char *name,
    uint8_t **data, size_t *data_size, uint32_t *attributes)
{
	struct efi_var_ioc var;
	int rv;
	static uint8_t buf[1024*32];

	if (efi_open_dev() == -1)
		return -1;

	efi_var_reset(&var);
	rv = utf8_to_ucs2(name, &var.name, &var.namesize);
	if (rv != 0)
		goto errout;
	var.vendor = guid;
	var.data = buf;
	var.datasize = sizeof(buf);
	rv = ioctl(efi_fd, EFIIOC_VAR_GET, &var);
	if (data_size != NULL)
		*data_size = var.datasize;
	if (data != NULL)
		*data = buf;
	if (attributes != NULL)
		*attributes = var.attrib;
errout:
	free(var.name);

	return rv_to_linux_rv(rv);
}

int
efi_get_variable_attributes(efi_guid_t guid, const char *name,
    uint32_t *attributes)
{
	/* Make sure this construct works -- I think it will fail */

	return efi_get_variable(guid, name, NULL, NULL, attributes);
}

int
efi_get_variable_size(efi_guid_t guid, const char *name,
    size_t *size)
{

	/* XXX check to make sure this matches the linux value */

	*size = 0;
	return efi_get_variable(guid, name, NULL, size, NULL);
}

int
efi_get_next_variable_name(efi_guid_t **guid, char **name)
{
	struct efi_var_ioc var;
	int rv;
	static efi_char *buf;
	static size_t buflen = 256 * sizeof(efi_char);
	static efi_guid_t retguid;
	size_t size;

	if (efi_open_dev() == -1)
		return -1;

	/*
	 * Always allocate enough for an extra NUL on the end, but don't tell
	 * the IOCTL about it so we can NUL terminate the name before converting
	 * it to UTF8.
	 */
	if (buf == NULL)
		buf = malloc(buflen + sizeof(efi_char));

again:
	efi_var_reset(&var);
	var.name = buf;
	var.namesize = buflen;
	if (*name == NULL) {
		*buf = 0;
		/* GUID zeroed in var_reset */
	} else {
		rv = utf8_to_ucs2(*name, &var.name, &size);
		if (rv != 0)
			goto errout;
		var.vendor = **guid;
	}
	rv = ioctl(efi_fd, EFIIOC_VAR_NEXT, &var);
	if (rv == 0 && var.name == NULL) {
		/*
		 * Variable name not long enough, so allocate more space for the
		 * name and try again. As above, mind the NUL we add.
		 */
		void *new = realloc(buf, var.namesize + sizeof(efi_char));
		if (new == NULL) {
			rv = -1;
			errno = ENOMEM;
			goto done;
		}
		buflen = var.namesize;
		buf = new;
		goto again;
	}

	if (rv == 0) {
		free(*name);			/* Free last name, to avoid leaking */
		*name = NULL;			/* Force ucs2_to_utf8 to malloc new space */
		var.name[var.namesize / sizeof(efi_char)] = 0;	/* EFI doesn't NUL terminate */
		rv = ucs2_to_utf8(var.name, name);
		if (rv != 0)
			goto errout;
		retguid = var.vendor;
		*guid = &retguid;
	}
errout:

	/* XXX The linux interface expects name to be a static buffer -- fix or leak memory? */
	/* XXX for the moment, we free just before we'd leak, but still leak last one */
done:
	if (rv != 0 && errno == ENOENT) {
		errno = 0;
		free(*name);			/* Free last name, to avoid leaking */
		return 0;
	}

	return (rv_to_linux_rv(rv));
}

int
efi_guid_cmp(const efi_guid_t *guid1, const efi_guid_t *guid2)
{
	uint32_t status;

	return uuid_compare(guid1, guid2, &status);
}

int
efi_guid_is_zero(const efi_guid_t *guid)
{
	uint32_t status;

	return uuid_is_nil(guid, &status);
}

int
efi_guid_to_name(efi_guid_t *guid, char **name)
{
	size_t i;
	uint32_t status;

	efi_guid_tbl_compile();
	for (i = 0; i < nitems(guid_tbl); i++) {
		if (uuid_equal(guid, &guid_tbl[i].guid, &status)) {
			*name = strdup(guid_tbl[i].name);
			return (0);
		}
	}
	return (efi_guid_to_str(guid, name));
}

int
efi_guid_to_symbol(efi_guid_t *guid __unused, char **symbol __unused)
{

	/*
	 * Unsure what this is used for, efibootmgr doesn't use it.
	 * Leave unimplemented for now.
	 */
	return -1;
}

int
efi_guid_to_str(const efi_guid_t *guid, char **sp)
{
	uint32_t status;

	/* knows efi_guid_t is a typedef of uuid_t */
	uuid_to_string(guid, sp, &status);

	return (status == uuid_s_ok ? 0 : -1);
}

int
efi_name_to_guid(const char *name, efi_guid_t *guid)
{
	size_t i;

	efi_guid_tbl_compile();
	for (i = 0; i < nitems(guid_tbl); i++) {
		if (strcmp(name, guid_tbl[i].name) == 0) {
			*guid = guid_tbl[i].guid;
			return (0);
		}
	}
	return (efi_str_to_guid(name, guid));
}

int
efi_set_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes)
{
	struct efi_var_ioc var;
	int rv;

	if (efi_open_dev() == -1)
		return -1;

	efi_var_reset(&var);
	rv = utf8_to_ucs2(name, &var.name, &var.namesize);
	if (rv != 0)
		goto errout;
	var.vendor = guid;
	var.data = data;
	var.datasize = data_size;
	var.attrib = attributes;
	rv = ioctl(efi_fd, EFIIOC_VAR_SET, &var);
errout:
	free(var.name);

	return rv;
}

int
efi_str_to_guid(const char *s, efi_guid_t *guid)
{
	uint32_t status;

	/* knows efi_guid_t is a typedef of uuid_t */
	uuid_from_string(s, guid, &status);

	return (status == uuid_s_ok ? 0 : -1);
}

int
efi_variables_supported(void)
{

	return efi_open_dev() != -1;
}
