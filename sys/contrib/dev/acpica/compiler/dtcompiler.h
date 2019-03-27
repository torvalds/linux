/******************************************************************************
 *
 * Module Name: dtcompiler.h - header for data table compiler
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

#define __DTCOMPILER_H__

#ifndef _DTCOMPILER
#define _DTCOMPILER

#include <contrib/dev/acpica/include/acdisasm.h>


#define ASL_FIELD_CACHE_SIZE            512
#define ASL_SUBTABLE_CACHE_SIZE         128


#undef DT_EXTERN

#ifdef _DECLARE_DT_GLOBALS
#define DT_EXTERN
#define DT_INIT_GLOBAL(a,b)         (a)=(b)
#else
#define DT_EXTERN                   extern
#define DT_INIT_GLOBAL(a,b)         (a)
#endif


/* Types for individual fields (one per input line) */

#define DT_FIELD_TYPE_STRING            0
#define DT_FIELD_TYPE_INTEGER           1
#define DT_FIELD_TYPE_BUFFER            2
#define DT_FIELD_TYPE_PCI_PATH          3
#define DT_FIELD_TYPE_FLAG              4
#define DT_FIELD_TYPE_FLAGS_INTEGER     5
#define DT_FIELD_TYPE_INLINE_SUBTABLE   6
#define DT_FIELD_TYPE_UUID              7
#define DT_FIELD_TYPE_UNICODE           8
#define DT_FIELD_TYPE_DEVICE_PATH       9
#define DT_FIELD_TYPE_LABEL             10


/*
 * Structure used for each individual field within an ACPI table
 */
typedef struct dt_field
{
    char                    *Name;       /* Field name (from name : value) */
    char                    *Value;      /* Field value (from name : value) */
    UINT32                  StringLength;/* Length of Value */
    struct dt_field         *Next;       /* Next field */
    struct dt_field         *NextLabel;  /* If field is a label, next label */
    UINT32                  Line;        /* Line number for this field */
    UINT32                  ByteOffset;  /* Offset in source file for field */
    UINT32                  NameColumn;  /* Start column for field name */
    UINT32                  Column;      /* Start column for field value */
    UINT32                  TableOffset; /* Binary offset within ACPI table */
    UINT8                   Flags;

} DT_FIELD;

/* Flags for above */

#define DT_FIELD_NOT_ALLOCATED      1


/*
 * Structure used for individual subtables within an ACPI table
 */
typedef struct dt_subtable
{
    struct dt_subtable      *Parent;
    struct dt_subtable      *Child;
    struct dt_subtable      *Peer;
    struct dt_subtable      *StackTop;
    UINT8                   *Buffer;
    UINT8                   *LengthField;
    char                    *Name;
    UINT32                  Length;
    UINT32                  TotalLength;
    UINT32                  SizeOfLengthField;
    UINT16                  Depth;
    UINT8                   Flags;

} DT_SUBTABLE;


/*
 * Globals
 */

/* List of all field names and values from the input source */

DT_EXTERN DT_FIELD          DT_INIT_GLOBAL (*AslGbl_FieldList, NULL);

/* List of all compiled tables and subtables */

DT_EXTERN DT_SUBTABLE       DT_INIT_GLOBAL (*AslGbl_RootTable, NULL);

/* Stack for subtables */

DT_EXTERN DT_SUBTABLE       DT_INIT_GLOBAL (*AslGbl_SubtableStack, NULL);

/* List for defined labels */

DT_EXTERN DT_FIELD          DT_INIT_GLOBAL (*AslGbl_LabelList, NULL);

/* Current offset within the binary output table */

DT_EXTERN UINT32            DT_INIT_GLOBAL (AslGbl_CurrentTableOffset, 0);

/* Local caches */

DT_EXTERN UINT32            DT_INIT_GLOBAL (AslGbl_SubtableCount, 0);
DT_EXTERN ASL_CACHE_INFO    DT_INIT_GLOBAL (*AslGbl_SubtableCacheList, NULL);
DT_EXTERN DT_SUBTABLE       DT_INIT_GLOBAL (*AslGbl_SubtableCacheNext, NULL);
DT_EXTERN DT_SUBTABLE       DT_INIT_GLOBAL (*AslGbl_SubtableCacheLast, NULL);

DT_EXTERN UINT32            DT_INIT_GLOBAL (AslGbl_FieldCount, 0);
DT_EXTERN ASL_CACHE_INFO    DT_INIT_GLOBAL (*AslGbl_FieldCacheList, NULL);
DT_EXTERN DT_FIELD          DT_INIT_GLOBAL (*AslGbl_FieldCacheNext, NULL);
DT_EXTERN DT_FIELD          DT_INIT_GLOBAL (*AslGbl_FieldCacheLast, NULL);


/* dtcompiler - main module */

ACPI_STATUS
DtCompileTable (
    DT_FIELD                **Field,
    ACPI_DMTABLE_INFO       *Info,
    DT_SUBTABLE             **RetSubtable);

ACPI_STATUS
DtCompileTwoSubtables (
    void                    **List,
    ACPI_DMTABLE_INFO       *TableInfo1,
    ACPI_DMTABLE_INFO       *TableInfo2);

ACPI_STATUS
DtCompilePadding (
    UINT32                  Length,
    DT_SUBTABLE             **RetSubtable);


/* dtio - binary and text input/output */

UINT32
DtGetNextLine (
    FILE                    *Handle,
    UINT32                  Flags);

/* Flags for DtGetNextLine */

#define DT_ALLOW_MULTILINE_QUOTES   0x01


DT_FIELD *
DtScanFile (
    FILE                    *Handle);

void
DtOutputBinary (
    DT_SUBTABLE             *RootTable);

void
DtDumpSubtableList (
    void);

void
DtDumpFieldList (
    DT_FIELD                *Field);

void
DtWriteFieldToListing (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  Length);

void
DtWriteTableToListing (
    void);


/* dtsubtable - compile subtables */

void
DtCreateSubtable (
    UINT8                   *Buffer,
    UINT32                  Length,
    DT_SUBTABLE             **RetSubtable);

UINT32
DtGetSubtableLength (
    DT_FIELD                *Field,
    ACPI_DMTABLE_INFO       *Info);

void
DtSetSubtableLength (
    DT_SUBTABLE             *Subtable);

void
DtPushSubtable (
    DT_SUBTABLE             *Subtable);

void
DtPopSubtable (
    void);

DT_SUBTABLE *
DtPeekSubtable (
    void);

void
DtInsertSubtable (
    DT_SUBTABLE             *ParentTable,
    DT_SUBTABLE             *Subtable);

DT_SUBTABLE *
DtGetNextSubtable (
    DT_SUBTABLE             *ParentTable,
    DT_SUBTABLE             *ChildTable);

DT_SUBTABLE *
DtGetParentSubtable (
    DT_SUBTABLE             *Subtable);


/* dtexpress - Integer expressions and labels */

ACPI_STATUS
DtResolveIntegerExpression (
    DT_FIELD                *Field,
    UINT64                  *ReturnValue);

UINT64
DtDoOperator (
    UINT64                  LeftValue,
    UINT32                  Operator,
    UINT64                  RightValue);

UINT64
DtResolveLabel (
    char                    *LabelString);

void
DtDetectAllLabels (
    DT_FIELD                *FieldList);


/* dtfield - Compile individual fields within a table */

void
DtCompileOneField (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength,
    UINT8                   Type,
    UINT8                   Flags);

void
DtCompileInteger (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength,
    UINT8                   Flags);

UINT32
DtCompileBuffer (
    UINT8                   *Buffer,
    char                    *Value,
    DT_FIELD                *Field,
    UINT32                  ByteLength);

void
DtCompileFlag (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    ACPI_DMTABLE_INFO       *Info);


/* dtparser - lex/yacc files */

UINT64
DtEvaluateExpression (
    char                    *ExprString);

int
DtInitLexer (
    char                    *String);

void
DtTerminateLexer (
    void);

char *
DtGetOpName (
    UINT32                  ParseOpcode);


/* dtutils - Miscellaneous utilities */

typedef
void (*DT_WALK_CALLBACK) (
    DT_SUBTABLE             *Subtable,
    void                    *Context,
    void                    *ReturnValue);

void
DtWalkTableTree (
    DT_SUBTABLE             *StartTable,
    DT_WALK_CALLBACK        UserFunction,
    void                    *Context,
    void                    *ReturnValue);

void
DtError (
    UINT8                   Level,
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage);

void
DtNameError (
    UINT8                   Level,
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage);

void
DtFatal (
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage);

UINT64
DtDoConstant (
    char                    *String);

char*
DtGetFieldValue (
    DT_FIELD                *Field);

UINT8
DtGetFieldType (
    ACPI_DMTABLE_INFO       *Info);

UINT32
DtGetBufferLength (
    char                    *Buffer);

UINT32
DtGetFieldLength (
    DT_FIELD                *Field,
    ACPI_DMTABLE_INFO       *Info);

void
DtSetTableChecksum (
    UINT8                   *ChecksumPointer);

void
DtSetTableLength(
    void);


/* dttable - individual table compilation */

ACPI_STATUS
DtCompileFacs (
    DT_FIELD                **PFieldList);

ACPI_STATUS
DtCompileRsdp (
    DT_FIELD                **PFieldList);

ACPI_STATUS
DtCompileAsf (
    void                    **PFieldList);

ACPI_STATUS
DtCompileCpep (
    void                    **PFieldList);

ACPI_STATUS
DtCompileCsrt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileDbg2 (
    void                    **PFieldList);

ACPI_STATUS
DtCompileDmar (
    void                    **PFieldList);

ACPI_STATUS
DtCompileDrtm (
    void                    **PFieldList);

ACPI_STATUS
DtCompileEinj (
    void                    **PFieldList);

ACPI_STATUS
DtCompileErst (
    void                    **PFieldList);

ACPI_STATUS
DtCompileFadt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileFpdt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileGtdt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileHest (
    void                    **PFieldList);

ACPI_STATUS
DtCompileHmat (
    void                    **PFieldList);

ACPI_STATUS
DtCompileIort (
    void                    **PFieldList);

ACPI_STATUS
DtCompileIvrs (
    void                    **PFieldList);

ACPI_STATUS
DtCompileLpit (
    void                    **PFieldList);

ACPI_STATUS
DtCompileMadt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileMcfg (
    void                    **PFieldList);

ACPI_STATUS
DtCompileMpst (
    void                    **PFieldList);

ACPI_STATUS
DtCompileMsct (
    void                    **PFieldList);

ACPI_STATUS
DtCompileMtmr (
    void                    **PFieldList);

ACPI_STATUS
DtCompileNfit (
    void                    **PFieldList);

ACPI_STATUS
DtCompilePcct (
    void                    **PFieldList);

ACPI_STATUS
DtCompilePdtt (
    void                    **PFieldList);

ACPI_STATUS
DtCompilePmtt (
    void                    **PFieldList);

ACPI_STATUS
DtCompilePptt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileRsdt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileS3pt (
    DT_FIELD                **PFieldList);

ACPI_STATUS
DtCompileSdev (
    void                    **PFieldList);

ACPI_STATUS
DtCompileSlic (
    void                    **PFieldList);

ACPI_STATUS
DtCompileSlit (
    void                    **PFieldList);

ACPI_STATUS
DtCompileSrat (
    void                    **PFieldList);

ACPI_STATUS
DtCompileStao (
    void                    **PFieldList);

ACPI_STATUS
DtCompileTcpa (
    void                    **PFieldList);

ACPI_STATUS
DtCompileTpm2 (
    void                    **PFieldList);

ACPI_STATUS
DtCompileUefi (
    void                    **PFieldList);

ACPI_STATUS
DtCompileVrtc (
    void                    **PFieldList);

ACPI_STATUS
DtCompileWdat (
    void                    **PFieldList);

ACPI_STATUS
DtCompileWpbt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileXsdt (
    void                    **PFieldList);

ACPI_STATUS
DtCompileGeneric (
    void                    **PFieldList,
    char                    *TermFieldName,
    UINT32                  *PFieldLength);

ACPI_DMTABLE_INFO *
DtGetGenericTableInfo (
    char                    *Name);

/* ACPI Table templates */

extern const unsigned char  TemplateAsf[];
extern const unsigned char  TemplateBoot[];
extern const unsigned char  TemplateBert[];
extern const unsigned char  TemplateBgrt[];
extern const unsigned char  TemplateCpep[];
extern const unsigned char  TemplateCsrt[];
extern const unsigned char  TemplateDbg2[];
extern const unsigned char  TemplateDbgp[];
extern const unsigned char  TemplateDmar[];
extern const unsigned char  TemplateDrtm[];
extern const unsigned char  TemplateEcdt[];
extern const unsigned char  TemplateEinj[];
extern const unsigned char  TemplateErst[];
extern const unsigned char  TemplateFadt[];
extern const unsigned char  TemplateFpdt[];
extern const unsigned char  TemplateGtdt[];
extern const unsigned char  TemplateHest[];
extern const unsigned char  TemplateHmat[];
extern const unsigned char  TemplateHpet[];
extern const unsigned char  TemplateIort[];
extern const unsigned char  TemplateIvrs[];
extern const unsigned char  TemplateLpit[];
extern const unsigned char  TemplateMadt[];
extern const unsigned char  TemplateMcfg[];
extern const unsigned char  TemplateMchi[];
extern const unsigned char  TemplateMpst[];
extern const unsigned char  TemplateMsct[];
extern const unsigned char  TemplateMsdm[];
extern const unsigned char  TemplateMtmr[];
extern const unsigned char  TemplateNfit[];
extern const unsigned char  TemplatePcct[];
extern const unsigned char  TemplatePdtt[];
extern const unsigned char  TemplatePmtt[];
extern const unsigned char  TemplatePptt[];
extern const unsigned char  TemplateRasf[];
extern const unsigned char  TemplateRsdt[];
extern const unsigned char  TemplateS3pt[];
extern const unsigned char  TemplateSbst[];
extern const unsigned char  TemplateSdei[];
extern const unsigned char  TemplateSdev[];
extern const unsigned char  TemplateSlic[];
extern const unsigned char  TemplateSlit[];
extern const unsigned char  TemplateSpcr[];
extern const unsigned char  TemplateSpmi[];
extern const unsigned char  TemplateSrat[];
extern const unsigned char  TemplateStao[];
extern const unsigned char  TemplateTcpa[];
extern const unsigned char  TemplateTpm2[];
extern const unsigned char  TemplateUefi[];
extern const unsigned char  TemplateVrtc[];
extern const unsigned char  TemplateWaet[];
extern const unsigned char  TemplateWdat[];
extern const unsigned char  TemplateWddt[];
extern const unsigned char  TemplateWdrt[];
extern const unsigned char  TemplateWpbt[];
extern const unsigned char  TemplateWsmt[];
extern const unsigned char  TemplateXenv[];
extern const unsigned char  TemplateXsdt[];

#endif
