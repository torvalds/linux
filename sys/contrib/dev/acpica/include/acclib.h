/******************************************************************************
 *
 * Name: acclib.h -- C library support. Prototypes for the (optional) local
 *                   implementations of required C library functions.
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

#ifndef _ACCLIB_H
#define _ACCLIB_H


/*
 * Prototypes and macros for local implementations of C library functions
 */

/* is* functions. The AcpiGbl_Ctypes array is defined in utclib.c */

extern const UINT8 AcpiGbl_Ctypes[];

#define _ACPI_XA     0x00    /* extra alphabetic - not supported */
#define _ACPI_XS     0x40    /* extra space */
#define _ACPI_BB     0x00    /* BEL, BS, etc. - not supported */
#define _ACPI_CN     0x20    /* CR, FF, HT, NL, VT */
#define _ACPI_DI     0x04    /* '0'-'9' */
#define _ACPI_LO     0x02    /* 'a'-'z' */
#define _ACPI_PU     0x10    /* punctuation */
#define _ACPI_SP     0x08    /* space, tab, CR, LF, VT, FF */
#define _ACPI_UP     0x01    /* 'A'-'Z' */
#define _ACPI_XD     0x80    /* '0'-'9', 'A'-'F', 'a'-'f' */

#define isdigit(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_DI))
#define isspace(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_SP))
#define isxdigit(c) (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_XD))
#define isupper(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_UP))
#define islower(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_LO))
#define isprint(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP | _ACPI_DI | _ACPI_XS | _ACPI_PU))
#define isalpha(c)  (AcpiGbl_Ctypes[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP))

/* Error code */

#define EPERM            1 /* Operation not permitted */
#define ENOENT           2 /* No such file or directory */
#define EINTR            4 /* Interrupted system call */
#define EIO              5 /* I/O error */
#define EBADF            9 /* Bad file number */
#define EAGAIN          11 /* Try again */
#define ENOMEM          12 /* Out of memory */
#define EACCES          13 /* Permission denied */
#define EFAULT          14 /* Bad address */
#define EBUSY           16 /* Device or resource busy */
#define EEXIST          17 /* File exists */
#define ENODEV          19 /* No such device */
#define EINVAL          22 /* Invalid argument */
#define EPIPE           32 /* Broken pipe */
#define ERANGE          34 /* Math result not representable */

/* Strings */

char *
strcat (
    char                    *DstString,
    const char              *SrcString);

char *
strchr (
    const char              *String,
    int                     ch);

char *
strpbrk (
    const char              *String,
    const char              *Delimiters);

char *
strtok (
    char                    *String,
    const char              *Delimiters);

char *
strcpy (
    char                    *DstString,
    const char              *SrcString);

int
strcmp (
    const char              *String1,
    const char              *String2);

ACPI_SIZE
strlen (
    const char              *String);

char *
strncat (
    char                    *DstString,
    const char              *SrcString,
    ACPI_SIZE               Count);

int
strncmp (
    const char              *String1,
    const char              *String2,
    ACPI_SIZE               Count);

char *
strncpy (
    char                    *DstString,
    const char              *SrcString,
    ACPI_SIZE               Count);

char *
strstr (
    char                    *String1,
    char                    *String2);


/* Conversion */

UINT32
strtoul (
    const char              *String,
    char                    **Terminator,
    UINT32                  Base);


/* Memory */

int
memcmp (
    void                    *Buffer1,
    void                    *Buffer2,
    ACPI_SIZE               Count);

void *
memcpy (
    void                    *Dest,
    const void              *Src,
    ACPI_SIZE               Count);

void *
memmove (
    void                    *Dest,
    const void              *Src,
    ACPI_SIZE               Count);

void *
memset (
    void                    *Dest,
    int                     Value,
    ACPI_SIZE               Count);


/* upper/lower case */

int
tolower (
    int                     c);

int
toupper (
    int                     c);

/*
 * utprint - printf/vprintf output functions
 */
const char *
AcpiUtScanNumber (
    const char              *String,
    UINT64                  *NumberPtr);

const char *
AcpiUtPrintNumber (
    char                    *String,
    UINT64                  Number);

int
vsnprintf (
    char                    *String,
    ACPI_SIZE               Size,
    const char              *Format,
    va_list                 Args);

int
snprintf (
    char                    *String,
    ACPI_SIZE               Size,
    const char              *Format,
    ...);

int
sprintf (
    char                    *String,
    const char              *Format,
    ...);

#ifdef ACPI_APPLICATION
#define SEEK_SET            0
#define SEEK_CUR            1
#define SEEK_END            2

/*
 * NOTE: Currently we only need to update errno for file IOs. Other
 *       Clibrary invocations in ACPICA do not make decisions according to
 *       the errno.
 */
extern int errno;

#ifndef EOF
#define EOF                 (-1)
#endif

#define putchar(c)          fputc(stdout, c)
#define getchar(c)          fgetc(stdin)

int
vprintf (
    const char              *Format,
    va_list                 Args);

int
printf (
    const char              *Format,
    ...);

int
vfprintf (
    FILE                    *File,
    const char              *Format,
    va_list                 Args);

int
fprintf (
    FILE                    *File,
    const char              *Format,
    ...);

FILE *
fopen (
    const char              *Path,
    const char              *Modes);

void
fclose (
    FILE                    *File);

int
fread (
    void                    *Buffer,
    ACPI_SIZE               Size,
    ACPI_SIZE               Count,
    FILE                    *File);

int
fwrite (
    void                    *Buffer,
    ACPI_SIZE               Size,
    ACPI_SIZE               Count,
    FILE                    *File);

int
fseek (
    FILE                    *File,
    long                    Offset,
    int                     From);

long
ftell (
    FILE                    *File);

int
fgetc (
    FILE                    *File);

int
fputc (
    FILE                    *File,
    char                    c);

char *
fgets (
    char                    *s,
    ACPI_SIZE               Size,
    FILE                    *File);
#endif

#endif /* _ACCLIB_H */
