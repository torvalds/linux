/*-
 * Copyright (c) 2003 Nate Lawson
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/sbuf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

/*
 * Package manipulation convenience functions
 */

int
acpi_PkgInt(ACPI_OBJECT *res, int idx, UINT64 *dst)
{
    ACPI_OBJECT		*obj;

    obj = &res->Package.Elements[idx];
    if (obj->Type != ACPI_TYPE_INTEGER)
	return (EINVAL);
    *dst = obj->Integer.Value;

    return (0);
}

int
acpi_PkgInt32(ACPI_OBJECT *res, int idx, uint32_t *dst)
{
    UINT64		tmp;
    int			error;

    error = acpi_PkgInt(res, idx, &tmp);
    if (error == 0)
	*dst = (uint32_t)tmp;

    return (error);
}

int
acpi_PkgStr(ACPI_OBJECT *res, int idx, void *dst, size_t size)
{
    ACPI_OBJECT		*obj;
    void		*ptr;
    size_t		 length;

    obj = &res->Package.Elements[idx];
    if (obj == NULL)
	return (EINVAL);

    switch (obj->Type) {
    case ACPI_TYPE_STRING:
	ptr = obj->String.Pointer;
	length = obj->String.Length;
	break;
    case ACPI_TYPE_BUFFER:
	ptr = obj->Buffer.Pointer;
	length = obj->Buffer.Length;
	break;
    default:
	return (EINVAL);
    }

    /* Make sure string will fit, including terminating NUL */
    if (++length > size)
	return (E2BIG);

    strlcpy(dst, ptr, length);
    return (0);
}

int
acpi_PkgGas(device_t dev, ACPI_OBJECT *res, int idx, int *type, int *rid,
    struct resource **dst, u_int flags)
{
    ACPI_GENERIC_ADDRESS gas;
    ACPI_OBJECT *obj;

    obj = &res->Package.Elements[idx];
    if (obj == NULL || obj->Type != ACPI_TYPE_BUFFER ||
	obj->Buffer.Length < sizeof(ACPI_GENERIC_ADDRESS) + 3)
	return (EINVAL);

    memcpy(&gas, obj->Buffer.Pointer + 3, sizeof(gas));

    return (acpi_bus_alloc_gas(dev, type, rid, &gas, dst, flags));
}

int
acpi_PkgFFH_IntelCpu(ACPI_OBJECT *res, int idx, int *vendor, int *class,
    uint64_t *address, int *accsize)
{
    ACPI_GENERIC_ADDRESS gas;
    ACPI_OBJECT *obj;

    obj = &res->Package.Elements[idx];
    if (obj == NULL || obj->Type != ACPI_TYPE_BUFFER ||
	obj->Buffer.Length < sizeof(ACPI_GENERIC_ADDRESS) + 3)
	return (EINVAL);

    memcpy(&gas, obj->Buffer.Pointer + 3, sizeof(gas));
    if (gas.SpaceId != ACPI_ADR_SPACE_FIXED_HARDWARE)
	return (ERESTART);
    *vendor = gas.BitWidth;
    *class = gas.BitOffset;
    *address = gas.Address;
    *accsize = gas.AccessWidth;
    return (0);
}

ACPI_HANDLE
acpi_GetReference(ACPI_HANDLE scope, ACPI_OBJECT *obj)
{
    ACPI_HANDLE h;

    if (obj == NULL)
	return (NULL);

    switch (obj->Type) {
    case ACPI_TYPE_LOCAL_REFERENCE:
    case ACPI_TYPE_ANY:
	h = obj->Reference.Handle;
	break;
    case ACPI_TYPE_STRING:
	/*
	 * The String object usually contains a fully-qualified path, so
	 * scope can be NULL.
	 *
	 * XXX This may not always be the case.
	 */
	if (ACPI_FAILURE(AcpiGetHandle(scope, obj->String.Pointer, &h)))
	    h = NULL;
	break;
    default:
	h = NULL;
	break;
    }

    return (h);
}
