/*	$OpenBSD: efi.c,v 1.4 2025/09/16 12:18:10 hshoexer Exp $	*/
/*
 * Copyright (c) 2022 3mdeb <contact@3mdeb.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/efi/efi.h>
#include <dev/efi/efiio.h>
#include <machine/efivar.h>

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL, CD_COCOVM
};

int	efiioc_get_table(struct efi_softc *sc, void *);
int	efiioc_var_get(struct efi_softc *sc, void *);
int	efiioc_var_next(struct efi_softc *sc, void *);
int	efiioc_var_set(struct efi_softc *sc, void *);
int	efi_adapt_error(EFI_STATUS);

EFI_GET_VARIABLE efi_get_variable;
EFI_SET_VARIABLE efi_set_variable;
EFI_GET_NEXT_VARIABLE_NAME efi_get_next_variable_name;

int
efiopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (efi_cd.cd_ndevs > 0 ? 0 : ENXIO);
}

int
eficlose(dev_t dev, int flag, int mode, struct proc *p)
{
	return 0;
}

int
efiioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];
	int error;

	if (sc->sc_rs == NULL || sc->sc_pm == NULL)
		return ENOTTY;

	switch (cmd) {
	case EFIIOC_GET_TABLE:
		error = efiioc_get_table(sc, data);
		break;
	case EFIIOC_VAR_GET:
		error = efiioc_var_get(sc, data);
		break;
	case EFIIOC_VAR_NEXT:
		error = efiioc_var_next(sc, data);
		break;
	case EFIIOC_VAR_SET:
		error = efiioc_var_set(sc, data);
		break;
	default:
		error = ENOTTY;
		break;
	}

	return error;
}

int
efiioc_get_table(struct efi_softc *sc, void *data)
{
	EFI_GUID esrt_guid = EFI_SYSTEM_RESOURCE_TABLE_GUID;
	struct efi_get_table_ioc *ioc = data;
	char *buf = NULL;
	int error;

	/* Only ESRT is supported at the moment. */
	if (memcmp(&ioc->uuid, &esrt_guid, sizeof(ioc->uuid)) != 0)
		return EINVAL;

	/* ESRT might not be present. */
	if (sc->sc_esrt == NULL)
		return ENXIO;

	if (efi_enter_check(sc)) {
		free(buf, M_TEMP, ioc->table_len);
		return ENOSYS;
	}

	ioc->table_len = sizeof(*sc->sc_esrt) +
	    sizeof(EFI_SYSTEM_RESOURCE_ENTRY) * sc->sc_esrt->FwResourceCount;

	/* Return table length to userspace. */
	if (ioc->buf == NULL) {
		efi_leave(sc);
		return 0;
	}

	/* Refuse to copy only part of the table. */
	if (ioc->buf_len < ioc->table_len) {
		efi_leave(sc);
		return EINVAL;
	}

	buf = malloc(ioc->table_len, M_TEMP, M_WAITOK);
	memcpy(buf, sc->sc_esrt, ioc->table_len);

	efi_leave(sc);

	error = copyout(buf, ioc->buf, ioc->table_len);
	free(buf, M_TEMP, ioc->table_len);

	return error;
}

int
efiioc_var_get(struct efi_softc *sc, void *data)
{
	struct efi_var_ioc *ioc = data;
	void *value = NULL;
	efi_char *name = NULL;
	size_t valuesize = ioc->datasize;
	EFI_STATUS status;
	int error;

	if (valuesize > 0)
		value = malloc(valuesize, M_TEMP, M_WAITOK);
	name = malloc(ioc->namesize, M_TEMP, M_WAITOK);
	error = copyin(ioc->name, name, ioc->namesize);
	if (error != 0)
		goto leave;

	/* NULL-terminated name must fit into namesize bytes. */
	if (name[ioc->namesize / sizeof(*name) - 1] != 0) {
		error = EINVAL;
		goto leave;
	}

	if (efi_get_variable) {
		status = efi_get_variable(name, (EFI_GUID *)&ioc->vendor,
					&ioc->attrib, &ioc->datasize, value);
	} else {
		if (efi_enter_check(sc)) {
			error = ENOSYS;
			goto leave;
		}
		status = sc->sc_rs->GetVariable(name, (EFI_GUID *)&ioc->vendor,
		    &ioc->attrib, &ioc->datasize, value);
		efi_leave(sc);
	}

	if (status == EFI_BUFFER_TOO_SMALL) {
		/*
		 * Return size of the value, which was set by EFI RT,
		 * reporting no error to match FreeBSD's behaviour.
		 */
		ioc->data = NULL;
		goto leave;
	}

	error = efi_adapt_error(status);
	if (error == 0)
		error = copyout(value, ioc->data, ioc->datasize);

leave:
	free(value, M_TEMP, valuesize);
	free(name, M_TEMP, ioc->namesize);
	return error;
}

int
efiioc_var_next(struct efi_softc *sc, void *data)
{
	struct efi_var_ioc *ioc = data;
	efi_char *name;
	size_t namesize = ioc->namesize;
	EFI_STATUS status;
	int error;

	name = malloc(namesize, M_TEMP, M_WAITOK);
	error = copyin(ioc->name, name, namesize);
	if (error)
		goto leave;

	if (efi_get_next_variable_name) {
		status = efi_get_next_variable_name(&ioc->namesize,
		    name, (EFI_GUID *)&ioc->vendor);
	} else {
		if (efi_enter_check(sc)) {
			error = ENOSYS;
			goto leave;
		}
		status = sc->sc_rs->GetNextVariableName(&ioc->namesize,
		    name, (EFI_GUID *)&ioc->vendor);
		efi_leave(sc);
	}

	if (status == EFI_BUFFER_TOO_SMALL) {
		/*
		 * Return size of the name, which was set by EFI RT,
		 * reporting no error to match FreeBSD's behaviour.
		 */
		ioc->name = NULL;
		goto leave;
	}

	error = efi_adapt_error(status);
	if (error == 0)
		error = copyout(name, ioc->name, ioc->namesize);

leave:
	free(name, M_TEMP, namesize);
	return error;
}

int
efiioc_var_set(struct efi_softc *sc, void *data)
{
	struct efi_var_ioc *ioc = data;
	void *value = NULL;
	efi_char *name = NULL;
	EFI_STATUS status;
	int error;

	/* Zero datasize means variable deletion. */
	if (ioc->datasize > 0) {
		value = malloc(ioc->datasize, M_TEMP, M_WAITOK);
		error = copyin(ioc->data, value, ioc->datasize);
		if (error)
			goto leave;
	}

	name = malloc(ioc->namesize, M_TEMP, M_WAITOK);
	error = copyin(ioc->name, name, ioc->namesize);
	if (error)
		goto leave;

	/* NULL-terminated name must fit into namesize bytes. */
	if (name[ioc->namesize / sizeof(*name) - 1] != 0) {
		error = EINVAL;
		goto leave;
	}

	if (securelevel > 0) {
		error = EPERM;
		goto leave;
	}

	if (efi_set_variable) {
		status = efi_set_variable(name, (EFI_GUID *)&ioc->vendor,
		    ioc->attrib, ioc->datasize, value);
	} else {
		if (efi_enter_check(sc)) {
			error = ENOSYS;
			goto leave;
		}
		status = sc->sc_rs->SetVariable(name, (EFI_GUID *)&ioc->vendor,
		    ioc->attrib, ioc->datasize, value);
		efi_leave(sc);
	}

	error = efi_adapt_error(status);

leave:
	free(value, M_TEMP, ioc->datasize);
	free(name, M_TEMP, ioc->namesize);
	return error;
}

int
efi_adapt_error(EFI_STATUS status)
{
	switch (status) {
	case EFI_SUCCESS:
		return 0;
	case EFI_DEVICE_ERROR:
		return EIO;
	case EFI_INVALID_PARAMETER:
		return EINVAL;
	case EFI_NOT_FOUND:
		return ENOENT;
	case EFI_OUT_OF_RESOURCES:
		return EAGAIN;
	case EFI_SECURITY_VIOLATION:
		return EPERM;
	case EFI_UNSUPPORTED:
		return ENOSYS;
	case EFI_WRITE_PROTECTED:
		return EROFS;
	default:
		return EIO;
	}
}
