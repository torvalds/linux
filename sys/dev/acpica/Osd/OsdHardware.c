/*-
 * Copyright (c) 2000, 2001 Michael Smith
 * Copyright (c) 2000 BSDi
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

/*
 * 6.7 : Hardware Abstraction
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <contrib/dev/acpica/include/acpi.h>

#include <machine/iodev.h>
#include <machine/pci_cfgreg.h>

extern int	acpi_susp_bounce;

ACPI_STATUS
AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue)
{

	/* If testing device suspend only, back out of everything here. */
	if (acpi_susp_bounce)
		return (AE_CTRL_TERMINATE);

	return (AE_OK);
}

/*
 * ACPICA's rather gung-ho approach to hardware resource ownership is a little
 * troublesome insofar as there is no easy way for us to know in advance
 * exactly which I/O resources it's going to want to use.
 *
 * In order to deal with this, we ignore resource ownership entirely, and simply
 * use the native I/O space accessor functionality.  This is Evil, but it works.
 */

ACPI_STATUS
AcpiOsReadPort(ACPI_IO_ADDRESS InPort, UINT32 *Value, UINT32 Width)
{

    switch (Width) {
    case 8:
	*Value = iodev_read_1(InPort);
	break;
    case 16:
	*Value = iodev_read_2(InPort);
	break;
    case 32:
	*Value = iodev_read_4(InPort);
	break;
    }

    return (AE_OK);
}

ACPI_STATUS
AcpiOsWritePort(ACPI_IO_ADDRESS OutPort, UINT32	Value, UINT32 Width)
{

    switch (Width) {
    case 8:
	iodev_write_1(OutPort, Value);
	break;
    case 16:
	iodev_write_2(OutPort, Value);
	break;
    case 32:
	iodev_write_4(OutPort, Value);
	break;
    }

    return (AE_OK);
}

ACPI_STATUS
AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register, UINT64 *Value,
    UINT32 Width)
{

#ifdef __aarch64__
    /* ARM64TODO: Add pci support */
    return (AE_SUPPORT);
#else
    if (Width == 64)
	return (AE_SUPPORT);

    if (!pci_cfgregopen())
	return (AE_NOT_EXIST);

    *(UINT64 *)Value = pci_cfgregread(PciId->Bus, PciId->Device,
	PciId->Function, Register, Width / 8);

    return (AE_OK);
#endif
}


ACPI_STATUS
AcpiOsWritePciConfiguration (ACPI_PCI_ID *PciId, UINT32 Register,
    UINT64 Value, UINT32 Width)
{

#ifdef __aarch64__
    /* ARM64TODO: Add pci support */
    return (AE_SUPPORT);
#else
    if (Width == 64)
	return (AE_SUPPORT);

    if (!pci_cfgregopen())
    	return (AE_NOT_EXIST);

    pci_cfgregwrite(PciId->Bus, PciId->Device, PciId->Function, Register,
	Value, Width / 8);

    return (AE_OK);
#endif
}
