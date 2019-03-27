/*-
 * Copyright (c) 2017 Eric McCorkle
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
 *
 * $FreeBSD$
 */

#include <stdbool.h>

#include <efi.h>
#include <efilib.h>

#include "efi_driver_utils.h"

static EFI_GUID DriverBindingProtocolGUID = DRIVER_BINDING_PROTOCOL;

EFI_STATUS
connect_controllers(EFI_GUID *filter)
{
        EFI_STATUS status;
        EFI_HANDLE *handles;
        UINTN nhandles, i, hsize;

        nhandles = 0;
        hsize = 0;
        status = BS->LocateHandle(ByProtocol, filter, NULL,
                     &hsize, NULL);

        if(status != EFI_BUFFER_TOO_SMALL) {
                return (status);
        }

        handles = malloc(hsize);
        nhandles = hsize / sizeof(EFI_HANDLE);

        status = BS->LocateHandle(ByProtocol, filter, NULL,
                     &hsize, handles);

        if(EFI_ERROR(status)) {
                return (status);
        }

        for(i = 0; i < nhandles; i++) {
                BS->ConnectController(handles[i], NULL, NULL, true);
        }

        free(handles);

        return (status);
}

EFI_STATUS
install_driver(EFI_DRIVER_BINDING *driver)
{
        EFI_STATUS status;

        driver->ImageHandle = IH;
        driver->DriverBindingHandle = NULL;
        status = BS->InstallMultipleProtocolInterfaces(
            &(driver->DriverBindingHandle),
            &DriverBindingProtocolGUID, driver,
            NULL);

        if (EFI_ERROR(status)) {
                printf("Failed to install driver (%ld)!\n",
                    EFI_ERROR_CODE(status));
        }

        return (status);
}
