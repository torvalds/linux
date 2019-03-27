/*-
 * Copyright (c) 2000 Michael Smith
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
 * 6.8 : Debugging support
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <ddb/ddb.h>
#include <ddb/db_output.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acdebug.h>

#include <dev/acpica/acpivar.h>

ACPI_STATUS
AcpiOsGetLine(char *Buffer, UINT32 BufferLength, UINT32 *BytesRead)
{
#ifdef DDB
	char *cp;

	cp = Buffer;
	if (db_readline(Buffer, BufferLength) > 0)
		while (*cp != '\0' && *cp != '\n' && *cp != '\r')
			cp++;
	*cp = '\0';
	if (BytesRead != NULL)
		*BytesRead = cp - Buffer;
	return (AE_OK);
#else
	printf("AcpiOsGetLine called but no input support");
	return (AE_NOT_EXIST);
#endif /* DDB */
}

ACPI_STATUS
AcpiOsSignal(UINT32 Function, void *Info)
{
    ACPI_SIGNAL_FATAL_INFO	*fatal;

    switch (Function) {
    case ACPI_SIGNAL_FATAL:
	fatal = (ACPI_SIGNAL_FATAL_INFO *)Info;
	printf("ACPI fatal signal, type 0x%x code 0x%x argument 0x%x",
	      fatal->Type, fatal->Code, fatal->Argument);
#ifdef ACPI_DEBUG
	kdb_enter(KDB_WHY_ACPI, "AcpiOsSignal");
#endif
	break;

    case ACPI_SIGNAL_BREAKPOINT:
#ifdef ACPI_DEBUG
	kdb_enter(KDB_WHY_ACPI, (char *)Info);
#endif
	break;

    default:
	return (AE_BAD_PARAMETER);
    }

    return (AE_OK);
}

#ifdef ACPI_DEBUGGER
void
acpi_EnterDebugger(void)
{
    static int		initted = 0;

    if (!initted) {
	printf("Initialising ACPICA debugger...\n");
	AcpiInitializeDebugger();
	initted = 1;
    }

    printf("Entering ACPICA debugger...\n");
    AcpiDbUserCommands();
}
#endif /* ACPI_DEBUGGER */
