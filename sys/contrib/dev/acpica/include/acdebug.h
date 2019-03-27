/******************************************************************************
 *
 * Name: acdebug.h - ACPI/AML debugger
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

#ifndef __ACDEBUG_H__
#define __ACDEBUG_H__

/* The debugger is used in conjunction with the disassembler most of time */

#ifdef ACPI_DISASSEMBLER
#include <contrib/dev/acpica/include/acdisasm.h>
#endif


#define ACPI_DEBUG_BUFFER_SIZE      0x4000      /* 16K buffer for return objects */
#define ACPI_DEBUG_LENGTH_FORMAT    " (%.4X bits, %.3X bytes)"

typedef struct acpi_db_command_info
{
    const char              *Name;          /* Command Name */
    UINT8                   MinArgs;        /* Minimum arguments required */

} ACPI_DB_COMMAND_INFO;

typedef struct acpi_db_command_help
{
    UINT8                   LineCount;      /* Number of help lines */
    char                    *Invocation;    /* Command Invocation */
    char                    *Description;   /* Command Description */

} ACPI_DB_COMMAND_HELP;

typedef struct acpi_db_argument_info
{
    const char              *Name;          /* Argument Name */

} ACPI_DB_ARGUMENT_INFO;

typedef struct acpi_db_execute_walk
{
    UINT32                  Count;
    UINT32                  MaxCount;

} ACPI_DB_EXECUTE_WALK;


#define PARAM_LIST(pl)                  pl

#define EX_NO_SINGLE_STEP               1
#define EX_SINGLE_STEP                  2


/*
 * dbxface - external debugger interfaces
 */
ACPI_DBR_DEPENDENT_RETURN_OK (
ACPI_STATUS
AcpiDbSingleStep (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  OpType))

ACPI_DBR_DEPENDENT_RETURN_VOID (
void
AcpiDbSignalBreakPoint (
    ACPI_WALK_STATE         *WalkState))


/*
 * dbcmds - debug commands and output routines
 */
ACPI_NAMESPACE_NODE *
AcpiDbConvertToNode (
    char                    *InString);

void
AcpiDbDisplayTableInfo (
    char                    *TableArg);

void
AcpiDbDisplayTemplate (
    char                    *BufferArg);

void
AcpiDbUnloadAcpiTable (
    char                    *Name);

void
AcpiDbSendNotify (
    char                    *Name,
    UINT32                  Value);

void
AcpiDbDisplayInterfaces (
    char                    *ActionArg,
    char                    *InterfaceNameArg);

ACPI_STATUS
AcpiDbSleep (
    char                    *ObjectArg);

void
AcpiDbTrace (
    char                    *EnableArg,
    char                    *MethodArg,
    char                    *OnceArg);

void
AcpiDbDisplayLocks (
    void);

void
AcpiDbDisplayResources (
    char                    *ObjectArg);

ACPI_HW_DEPENDENT_RETURN_VOID (
void
AcpiDbDisplayGpes (
    void))

void
AcpiDbDisplayHandlers (
    void);

ACPI_HW_DEPENDENT_RETURN_VOID (
void
AcpiDbGenerateGpe (
    char                    *GpeArg,
    char                    *BlockArg))

ACPI_HW_DEPENDENT_RETURN_VOID (
void
AcpiDbGenerateSci (
    void))

void
AcpiDbExecuteTest (
    char                    *TypeArg);


/*
 * dbconvert - miscellaneous conversion routines
 */
ACPI_STATUS
AcpiDbHexCharToValue (
    int                     HexChar,
    UINT8                   *ReturnValue);

ACPI_STATUS
AcpiDbConvertToPackage (
    char                    *String,
    ACPI_OBJECT             *Object);

ACPI_STATUS
AcpiDbConvertToObject (
    ACPI_OBJECT_TYPE        Type,
    char                    *String,
    ACPI_OBJECT             *Object);

UINT8 *
AcpiDbEncodePldBuffer (
    ACPI_PLD_INFO           *PldInfo);

void
AcpiDbDumpPldBuffer (
    ACPI_OBJECT             *ObjDesc);


/*
 * dbmethod - control method commands
 */
void
AcpiDbSetMethodBreakpoint (
    char                    *Location,
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op);

void
AcpiDbSetMethodCallBreakpoint (
    ACPI_PARSE_OBJECT       *Op);

void
AcpiDbSetMethodData (
    char                    *TypeArg,
    char                    *IndexArg,
    char                    *ValueArg);

ACPI_STATUS
AcpiDbDisassembleMethod (
    char                    *Name);

void
AcpiDbDisassembleAml (
    char                    *Statements,
    ACPI_PARSE_OBJECT       *Op);

void
AcpiDbEvaluatePredefinedNames (
    void);


/*
 * dbnames - namespace commands
 */
void
AcpiDbSetScope (
    char                    *Name);

void
AcpiDbDumpNamespace (
    char                    *StartArg,
    char                    *DepthArg);

void
AcpiDbDumpNamespacePaths (
    void);

void
AcpiDbDumpNamespaceByOwner (
    char                    *OwnerArg,
    char                    *DepthArg);

ACPI_STATUS
AcpiDbFindNameInNamespace (
    char                    *NameArg);

void
AcpiDbCheckPredefinedNames (
    void);

ACPI_STATUS
AcpiDbDisplayObjects (
    char                    *ObjTypeArg,
    char                    *DisplayCountArg);

void
AcpiDbCheckIntegrity (
    void);

void
AcpiDbFindReferences (
    char                    *ObjectArg);

void
AcpiDbGetBusInfo (
    void);


/*
 * dbdisply - debug display commands
 */
void
AcpiDbDisplayMethodInfo (
    ACPI_PARSE_OBJECT       *Op);

void
AcpiDbDecodeAndDisplayObject (
    char                    *Target,
    char                    *OutputType);

ACPI_DBR_DEPENDENT_RETURN_VOID (
void
AcpiDbDisplayResultObject (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState))

ACPI_STATUS
AcpiDbDisplayAllMethods (
    char                    *DisplayCountArg);

void
AcpiDbDisplayArguments (
    void);

void
AcpiDbDisplayLocals (
    void);

void
AcpiDbDisplayResults (
    void);

void
AcpiDbDisplayCallingTree (
    void);

void
AcpiDbDisplayObjectType (
    char                    *ObjectArg);

ACPI_DBR_DEPENDENT_RETURN_VOID (
void
AcpiDbDisplayArgumentObject (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState))


/*
 * dbexec - debugger control method execution
 */
void
AcpiDbExecute (
    char                    *Name,
    char                    **Args,
    ACPI_OBJECT_TYPE        *Types,
    UINT32                  Flags);

void
AcpiDbCreateExecutionThread (
    char                    *MethodNameArg,
    char                    **Arguments,
    ACPI_OBJECT_TYPE        *Types);

void
AcpiDbCreateExecutionThreads (
    char                    *NumThreadsArg,
    char                    *NumLoopsArg,
    char                    *MethodNameArg);

void
AcpiDbDeleteObjects (
    UINT32                  Count,
    ACPI_OBJECT             *Objects);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
UINT32
AcpiDbGetCacheInfo (
    ACPI_MEMORY_LIST        *Cache);
#endif


/*
 * dbfileio - Debugger file I/O commands
 */
ACPI_OBJECT_TYPE
AcpiDbMatchArgument (
    char                    *UserArgument,
    ACPI_DB_ARGUMENT_INFO   *Arguments);

void
AcpiDbCloseDebugFile (
    void);

void
AcpiDbOpenDebugFile (
    char                    *Name);

ACPI_STATUS
AcpiDbLoadAcpiTable (
    char                    *Filename);

ACPI_STATUS
AcpiDbLoadTables (
    ACPI_NEW_TABLE_DESC     *ListHead);


/*
 * dbhistry - debugger HISTORY command
 */
void
AcpiDbAddToHistory (
    char                    *CommandLine);

void
AcpiDbDisplayHistory (
    void);

char *
AcpiDbGetFromHistory (
    char                    *CommandNumArg);

char *
AcpiDbGetHistoryByIndex (
    UINT32                  CommanddNum);


/*
 * dbinput - user front-end to the AML debugger
 */
ACPI_STATUS
AcpiDbCommandDispatch (
    char                    *InputBuffer,
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op);

void ACPI_SYSTEM_XFACE
AcpiDbExecuteThread (
    void                    *Context);

ACPI_STATUS
AcpiDbUserCommands (
    void);

char *
AcpiDbGetNextToken (
    char                    *String,
    char                    **Next,
    ACPI_OBJECT_TYPE        *ReturnType);


/*
 * dbobject
 */
void
AcpiDbDecodeInternalObject (
    ACPI_OPERAND_OBJECT     *ObjDesc);

void
AcpiDbDisplayInternalObject (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState);

void
AcpiDbDecodeArguments (
    ACPI_WALK_STATE         *WalkState);

void
AcpiDbDecodeLocals (
    ACPI_WALK_STATE         *WalkState);

void
AcpiDbDumpMethodInfo (
    ACPI_STATUS             Status,
    ACPI_WALK_STATE         *WalkState);


/*
 * dbstats - Generation and display of ACPI table statistics
 */
void
AcpiDbGenerateStatistics (
    ACPI_PARSE_OBJECT       *Root,
    BOOLEAN                 IsMethod);

ACPI_STATUS
AcpiDbDisplayStatistics (
    char                    *TypeArg);


/*
 * dbutils - AML debugger utilities
 */
void
AcpiDbSetOutputDestination (
    UINT32                  Where);

void
AcpiDbDumpExternalObject (
    ACPI_OBJECT             *ObjDesc,
    UINT32                  Level);

void
AcpiDbPrepNamestring (
    char                    *Name);

ACPI_NAMESPACE_NODE *
AcpiDbLocalNsLookup (
    char                    *Name);

void
AcpiDbUint32ToHexString (
    UINT32                  Value,
    char                    *Buffer);

#endif  /* __ACDEBUG_H__ */
