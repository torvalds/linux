/*-
 * Copyright (c) 2019 Stormshield.
 * Copyright (c) 2019 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>

#include <efi.h>
#include <efilib.h>
#include <Guid/ImageAuthentication.h>

#define NEED_BRSSL_H
#include "../libsecureboot-priv.h"
#include <brssl.h>

static EFI_GUID ImageSecurityDatabaseGUID = EFI_IMAGE_SECURITY_DATABASE_GUID;

static EFI_GUID efiCertX509GUID = EFI_CERT_X509_GUID;
static EFI_GUID efiCertX509Sha256GUID = EFI_CERT_X509_SHA256_GUID;
static EFI_GUID efiCertX509Sha384GUID = EFI_CERT_X509_SHA384_GUID;
static EFI_GUID efiCertX509Sha5122UID = EFI_CERT_X509_SHA512_GUID;

/*
 * Check if Secure Boot is enabled in firmware.
 * We evaluate two variables - Secure Boot and Setup Mode.
 * Secure Boot is enforced only if the first one equals 1 and the other 0.
 */
int
efi_secure_boot_enabled(void)
{
	UINT8 SecureBoot;
	UINT8 SetupMode;
	size_t length;
	EFI_STATUS status;

	length = sizeof(SecureBoot);
	status = efi_global_getenv("SecureBoot", &SecureBoot, &length);
	if (status != EFI_SUCCESS) {
		if (status == EFI_NOT_FOUND)
			return (0);

		printf("Failed to read \"SecureBoot\" variable\n");
		return (-efi_status_to_errno(status));
	}

	length = sizeof(SetupMode);
	status = efi_global_getenv("SetupMode", &SetupMode, &length);
	if (status != EFI_SUCCESS)
		SetupMode = 0;

	printf("   SecureBoot: %d, SetupMode: %d\n", SecureBoot, SetupMode);

	return (SecureBoot == 1 && SetupMode == 0);
}

/*
 * Iterate through UEFI variable and extract X509 certificates from it.
 * The EFI_* structures and related guids are defined in UEFI standard.
 */
static br_x509_certificate*
efi_get_certs(const char *name, size_t *count)
{
	br_x509_certificate *certs;
	UINT8 *database;
	EFI_SIGNATURE_LIST *list;
	EFI_SIGNATURE_DATA *entry;
	size_t db_size;
	ssize_t cert_count;
	EFI_STATUS status;

	database = NULL;
	certs = NULL;
	db_size = 0;
	cert_count = 0;

	/*
	 * Read variable length and allocate memory for it
	 */
	status = efi_getenv(&ImageSecurityDatabaseGUID, name, database, &db_size);
	if (status != EFI_BUFFER_TOO_SMALL)
		return (NULL);

	database = malloc(db_size);
	if (database == NULL)
		return (NULL);

	status = efi_getenv(&ImageSecurityDatabaseGUID, name, database, &db_size);
	if (status != EFI_SUCCESS)
		goto fail;

	for (list = (EFI_SIGNATURE_LIST*) database;
	    db_size >= list->SignatureListSize && db_size > 0;
	    db_size -= list->SignatureListSize,
	    list = (EFI_SIGNATURE_LIST*)
	    ((UINT8*)list + list->SignatureListSize)) {

		/* We are only interested in entries containing X509 certs. */
		if (memcmp(&efiCertX509GUID,
		    &list->SignatureType,
		    sizeof(EFI_GUID)) != 0) {
			continue;
		}

		entry = (EFI_SIGNATURE_DATA*)
		    ((UINT8*)list +
		    sizeof(EFI_SIGNATURE_LIST) +
		    list->SignatureHeaderSize);

		certs = realloc(certs,
		    (cert_count + 1) * sizeof(br_x509_certificate));
		if (certs == NULL) {
			cert_count = 0;
			goto fail;
		}

		certs[cert_count].data_len = list->SignatureSize - sizeof(EFI_GUID);
		certs[cert_count].data = malloc(certs[cert_count].data_len);
		if (certs[cert_count].data == NULL)
			goto fail;

		memcpy(certs[cert_count].data,
		    entry->SignatureData,
		    certs[cert_count].data_len);

		cert_count++;
	}

	*count = cert_count;

	xfree(database);
	return (certs);

fail:
	free_certificates(certs, cert_count);
	xfree(database);
	return (NULL);

}

/*
 * Extract digests from UEFI "dbx" variable.
 * UEFI standard specifies three types of digest - sha256, sha386, sha512.
 */
hash_data*
efi_get_forbidden_digests(size_t *count)
{
	UINT8 *database;
	hash_data *digests;
	EFI_SIGNATURE_LIST *list;
	EFI_SIGNATURE_DATA *entry;
	size_t db_size, header_size, hash_size;
	int digest_count, entry_count;
	EFI_STATUS status;

	db_size = 0;
	digest_count = 0;
	database = NULL;
	digests = NULL;

	status = efi_getenv(&ImageSecurityDatabaseGUID, "dbx", database, &db_size);
	if (status != EFI_BUFFER_TOO_SMALL)
		return (NULL);

	database = malloc(db_size);
	if (database == NULL)
		return (NULL);

	status = efi_getenv(&ImageSecurityDatabaseGUID, "dbx", database, &db_size);
	if (status != EFI_SUCCESS)
		goto fail;


	for (list = (EFI_SIGNATURE_LIST*) database;
	    db_size >= list->SignatureListSize && db_size > 0;
	    db_size -= list->SignatureListSize,
	    list = (EFI_SIGNATURE_LIST*)
	    ((UINT8*)list + list->SignatureListSize)) {

		/* We are only interested in entries that contain digests. */
		if (memcmp(&efiCertX509Sha256GUID, &list->SignatureType,
		    sizeof(EFI_GUID)) == 0) {
			hash_size = br_sha256_SIZE;
		} else if (memcmp(&efiCertX509Sha384GUID, &list->SignatureType,
		    sizeof(EFI_GUID)) == 0) {
			hash_size = br_sha384_SIZE;
		} else if (memcmp(&efiCertX509Sha5122UID, &list->SignatureType,
		    sizeof(EFI_GUID)) == 0) {
			hash_size = br_sha512_SIZE;
		} else {
			continue;
		}

		/*
		 * A single entry can have multiple digests
		 * of the same type for some reason.
		 */
		header_size = sizeof(EFI_SIGNATURE_LIST) + list->SignatureHeaderSize;

		/* Calculate the number of entries basing on structure size */
		entry_count = list->SignatureListSize - header_size;
		entry_count /= list->SignatureSize;
		entry = (EFI_SIGNATURE_DATA*)((UINT8*)list + header_size);
		while (entry_count-- > 0) {
			digests = realloc(digests,
			    (digest_count + 1) * sizeof(hash_data));
			if (digests == NULL) {
				digest_count = 0;
				goto fail;
			}

			digests[digest_count].data = malloc(hash_size);
			if (digests[digest_count].data == NULL)
				goto fail;

			memcpy(digests[digest_count].data,
			    entry->SignatureData,
			    hash_size);
			digests[digest_count].hash_size = hash_size;

			entry = (EFI_SIGNATURE_DATA*)(entry + list->SignatureSize);
			digest_count++;
		}
	}
	xfree(database);
	if (count != NULL)
		*count = digest_count;

	return (digests);

fail:
	while (digest_count--)
		xfree(digests[digest_count].data);

	xfree(database);
	xfree(digests);
	return (NULL);
}

/* Copy x509 certificates from db */
br_x509_certificate*
efi_get_trusted_certs(size_t *count)
{
	return (efi_get_certs("db", count));
}

/* Copy forbidden certificates from dbx */
br_x509_certificate*
efi_get_forbidden_certs(size_t *count)
{
	return (efi_get_certs("dbx", count));
}
