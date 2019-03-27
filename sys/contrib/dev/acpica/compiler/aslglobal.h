/******************************************************************************
 *
 * Module Name: aslglobal.h - Global variable definitions
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

#ifndef __ASLGLOBAL_H
#define __ASLGLOBAL_H


/*
 * Global variables. Defined in aslmain.c only, externed in all other files
 */

#undef ASL_EXTERN

#ifdef _DECLARE_GLOBALS
#define ASL_EXTERN
#define ASL_INIT_GLOBAL(a,b)        (a)=(b)
#else
#define ASL_EXTERN                  extern
#define ASL_INIT_GLOBAL(a,b)        (a)
#endif


#ifdef _DECLARE_GLOBALS
UINT32                              AslGbl_ExceptionCount[ASL_NUM_REPORT_LEVELS] = {0,0,0,0,0,0};

/* Table below must match ASL_FILE_TYPES in asltypes.h */

ASL_FILE_INFO                       AslGbl_Files [ASL_NUM_FILES] =
{
    {NULL, NULL, "stdout:       ", "Standard Output"},
    {NULL, NULL, "stderr:       ", "Standard Error"},
    {NULL, NULL, "Table Input:  ", "Source Input"},
    {NULL, NULL, "Binary Output:", "AML Output"},
    {NULL, NULL, "Source Output:", "Source Output"},
    {NULL, NULL, "Preprocessor: ", "Preprocessor Output"},
    {NULL, NULL, "Preprocessor: ", "Preprocessor Temp File"},
    {NULL, NULL, "Listing File: ", "Listing Output"},
    {NULL, NULL, "Hex Dump:     ", "Hex Table Output"},
    {NULL, NULL, "Namespace:    ", "Namespace Output"},
    {NULL, NULL, "Debug File:   ", "Debug Output"},
    {NULL, NULL, "ASM Source:   ", "Assembly Code Output"},
    {NULL, NULL, "C Source:     ", "C Code Output"},
    {NULL, NULL, "ASM Include:  ", "Assembly Header Output"},
    {NULL, NULL, "C Include:    ", "C Header Output"},
    {NULL, NULL, "Offset Table: ", "C Offset Table Output"},
    {NULL, NULL, "Device Map:   ", "Device Map Output"},
    {NULL, NULL, "Cross Ref:    ", "Cross-reference Output"},
    {NULL, NULL, "Converter db :", "Converter debug Output"}
};

/* Table below must match the defines with the same names in actypes.h */

const char                          *AslGbl_OpFlagNames[ACPI_NUM_OP_FLAGS] =
{
    "OP_VISITED",
    "OP_AML_PACKAGE",
    "OP_IS_TARGET",
    "OP_IS_RESOURCE_DESC",
    "OP_IS_RESOURCE_FIELD",
    "OP_HAS_NO_EXIT",
    "OP_IF_HAS_NO_EXIT",
    "OP_NAME_INTERNALIZED",
    "OP_METHOD_NO_RETVAL",
    "OP_METHOD_SOME_NO_RETVAL",
    "OP_RESULT_NOT_USED",
    "OP_METHOD_TYPED",
    "OP_COULD_NOT_REDUCE",
    "OP_COMPILE_TIME_CONST",
    "OP_IS_TERM_ARG",
    "OP_WAS_ONES_OP",
    "OP_IS_NAME_DECLARATION",
    "OP_COMPILER_EMITTED",
    "OP_IS_DUPLICATE",
    "OP_IS_RESOURCE_DATA",
    "OP_IS_NULL_RETURN",
    "OP_NOT_FOUND_DURING_LOAD"
};

#else
extern UINT32                       AslGbl_ExceptionCount[ASL_NUM_REPORT_LEVELS];
extern ASL_FILE_INFO                AslGbl_Files [ASL_NUM_FILES];
extern const char                   *AslGbl_OpFlagNames[ACPI_NUM_OP_FLAGS];
#endif


/*
 * Parser and other externals
 */
extern int                          yydebug;
extern FILE                         *AslCompilerin;
extern int                          DtParserdebug;
extern int                          PrParserdebug;
extern const ASL_MAPPING_ENTRY      AslKeywordMapping[];
extern char                         *AslCompilertext;

/*
 * Older versions of Bison won't emit this external in the generated header.
 * Newer versions do emit the external, so we don't need to do it.
 */
#ifndef ASLCOMPILER_ASLCOMPILERPARSE_H
extern int                  AslCompilerdebug;
#endif


#define ASL_DEFAULT_LINE_BUFFER_SIZE    (1024 * 32) /* 32K */
#define ASL_MSG_BUFFER_SIZE             (1024 * 128) /* 128k */
#define ASL_STRING_BUFFER_SIZE          (1024 * 32) /* 32k */
#define ASL_MAX_DISABLED_MESSAGES       32
#define ASL_MAX_EXPECTED_MESSAGES       32
#define ASL_MAX_ELEVATED_MESSAGES       32
#define HEX_TABLE_LINE_SIZE             8
#define HEX_LISTING_LINE_SIZE           8


/* Source code buffers and pointers for error reporting */

ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_CurrentLineBuffer, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_LineBufPtr, NULL);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_LineBufferSize, ASL_DEFAULT_LINE_BUFFER_SIZE);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentColumn, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentLineNumber, 1);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_LogicalLineNumber, 1);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentLineOffset, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_OriginalInputFileSize, 0);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_SyntaxError, 0);

/* Exception reporting */

ASL_EXTERN ASL_ERROR_MSG            ASL_INIT_GLOBAL (*AslGbl_ErrorLog,NULL);
ASL_EXTERN ASL_ERROR_MSG            ASL_INIT_GLOBAL (*AslGbl_NextError,NULL);

/* Option flags */

ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoCompile, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoSignon, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_PreprocessOnly, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_PreprocessFlag, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DisassembleAll, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_UseDefaultAmlFilename, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_MapfileFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_NsOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_PreprocessorOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_KeepPreprocessorTempFile, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DebugFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_CrossReferenceOutput, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_AsmOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_C_OutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_C_OffsetTableFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_AsmIncludeOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_C_IncludeOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_ListingFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_IgnoreErrors, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_SourceOutputFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_ParseOnlyFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_CompileTimesFlag, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_FoldConstants, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_VerboseErrors, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_NoErrors, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_WarningsAsErrors, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_NoResourceChecking, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_IntegerOptimizationFlag, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_ReferenceOptimizationFlag, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DisplayRemarks, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DisplayWarnings, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DisplayOptimizations, FALSE);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_WarningLevel, ASL_WARNING);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_UseOriginalCompilerId, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_VerboseTemplates, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoTemplates, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_CompileGeneric, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_AllExceptionsDisabled, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_PruneParseTree, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoTypechecking, TRUE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_EnableReferenceTypechecking, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoExternalsInPlace, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_DoAslConversion, FALSE);
ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_OptimizeTrivialParseNodes, TRUE);


#define HEX_OUTPUT_NONE             0
#define HEX_OUTPUT_C                1
#define HEX_OUTPUT_ASM              2
#define HEX_OUTPUT_ASL              3

ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_HexOutputFlag, HEX_OUTPUT_NONE);


/* Files */

ASL_EXTERN BOOLEAN                  ASL_INIT_GLOBAL (AslGbl_HasIncludeFiles, FALSE);
ASL_EXTERN char                     *AslGbl_DirectoryPath;
ASL_EXTERN char                     *AslGbl_CurrentInputFilename;
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_IncludeFilename, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_OutputFilenamePrefix, NULL);
ASL_EXTERN ASL_INCLUDE_DIR          ASL_INIT_GLOBAL (*AslGbl_IncludeDirList, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_ExternalRefFilename, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_PreviousIncludeFilename, NULL);

/* Statistics */

ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_InputByteCount, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_InputFieldCount, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_NsLookupCount, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalKeywords, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalNamedObjects, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalExecutableOpcodes, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalParseNodes, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalMethods, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalAllocations, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalAllocated, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TotalFolds, 0);

/* Local caches */

ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_ParseOpCount, 0);
ASL_EXTERN ASL_CACHE_INFO           ASL_INIT_GLOBAL (*AslGbl_ParseOpCacheList, NULL);
ASL_EXTERN ACPI_PARSE_OBJECT        ASL_INIT_GLOBAL (*AslGbl_ParseOpCacheNext, NULL);
ASL_EXTERN ACPI_PARSE_OBJECT        ASL_INIT_GLOBAL (*AslGbl_ParseOpCacheLast, NULL);

ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_StringCount, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_StringSize, 0);
ASL_EXTERN ASL_CACHE_INFO           ASL_INIT_GLOBAL (*AslGbl_StringCacheList, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_StringCacheNext, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_StringCacheLast, NULL);

/* Map file */

ASL_EXTERN ACPI_GPIO_INFO           ASL_INIT_GLOBAL (*AslGbl_GpioList, NULL);
ASL_EXTERN ACPI_SERIAL_INFO         ASL_INIT_GLOBAL (*AslGbl_SerialList, NULL);

/* Misc */

ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_RevisionOverride, 0);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_TempCount, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_TableLength, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_SourceLine, 0);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_FileType, 0);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_Signature, NULL);
ASL_EXTERN ACPI_PARSE_OBJECT        ASL_INIT_GLOBAL (*AslGbl_ParseTreeRoot, NULL);
ASL_EXTERN ACPI_PARSE_OBJECT        ASL_INIT_GLOBAL (*AslGbl_ExternalsListHead, NULL);
ASL_EXTERN ASL_LISTING_NODE         ASL_INIT_GLOBAL (*AslGbl_ListingNode, NULL);
ASL_EXTERN ACPI_PARSE_OBJECT        *AslGbl_FirstLevelInsertionNode;

ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentHexColumn, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentAmlOffset, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_CurrentLine, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_DisabledMessagesIndex, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_ExpectedMessagesIndex, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_ElevatedMessagesIndex, 0);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_HexBytesWereWritten, FALSE);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_NumNamespaceObjects, 0);
ASL_EXTERN UINT32                   ASL_INIT_GLOBAL (AslGbl_ReservedMethods, 0);
ASL_EXTERN UINT8                    ASL_INIT_GLOBAL (AslGbl_PruneDepth, 0);
ASL_EXTERN UINT16                   ASL_INIT_GLOBAL (AslGbl_PruneType, 0);
ASL_EXTERN ASL_FILE_NODE            ASL_INIT_GLOBAL (*AslGbl_IncludeFileStack, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_TableSignature, "NO_SIG");
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_TableId, "NO_ID");

/* Specific to the -q option */

ASL_EXTERN ASL_COMMENT_STATE        AslGbl_CommentState;

/*
 * Determines if an inline comment should be saved in the InlineComment or NodeEndComment
 *  field of ACPI_PARSE_OBJECT.
 */
ASL_EXTERN ACPI_COMMENT_NODE        ASL_INIT_GLOBAL (*AslGbl_CommentListHead, NULL);
ASL_EXTERN ACPI_COMMENT_NODE        ASL_INIT_GLOBAL (*AslGbl_CommentListTail, NULL);
ASL_EXTERN char                     ASL_INIT_GLOBAL (*AslGbl_InlineCommentBuffer, NULL);

/* Static structures */

ASL_EXTERN ASL_ANALYSIS_WALK_INFO   AslGbl_AnalysisWalkInfo;
ASL_EXTERN ACPI_TABLE_HEADER        AslGbl_TableHeader;

/* Event timing */

#define ASL_NUM_EVENTS              24
ASL_EXTERN ASL_EVENT_INFO           AslGbl_Events[ASL_NUM_EVENTS];
ASL_EXTERN UINT8                    AslGbl_NextEvent;
ASL_EXTERN UINT8                    AslGbl_NamespaceEvent;

/* Scratch buffers */

ASL_EXTERN UINT8                    AslGbl_AmlBuffer[HEX_LISTING_LINE_SIZE];
ASL_EXTERN char                     AslGbl_MsgBuffer[ASL_MSG_BUFFER_SIZE];
ASL_EXTERN char                     AslGbl_StringBuffer[ASL_STRING_BUFFER_SIZE];
ASL_EXTERN char                     AslGbl_StringBuffer2[ASL_STRING_BUFFER_SIZE];
ASL_EXTERN UINT32                   AslGbl_DisabledMessages[ASL_MAX_DISABLED_MESSAGES];
ASL_EXTERN ASL_EXPECTED_MESSAGE     AslGbl_ExpectedMessages[ASL_MAX_EXPECTED_MESSAGES];
ASL_EXTERN UINT32                   AslGbl_ElevatedMessages[ASL_MAX_ELEVATED_MESSAGES];


#endif /* __ASLGLOBAL_H */
