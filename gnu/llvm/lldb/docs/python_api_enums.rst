..
  This is a sub page of the Python API docs and linked from the main API page.
  The page isn't in any toctree, so silence the sphinx warnings by marking it as orphan.

:orphan:

Python API enumerators and constants
====================================

.. py:currentmodule:: lldb

Constants
*********

Generic register numbers
------------------------

.. py:data:: LLDB_REGNUM_GENERIC_PC

   Program counter.

.. py:data:: LLDB_REGNUM_GENERIC_SP

   Stack pointer.
.. py:data:: LLDB_REGNUM_GENERIC_FP

   Frame pointer.

.. py:data:: LLDB_REGNUM_GENERIC_RA

   Return address.

.. py:data:: LLDB_REGNUM_GENERIC_FLAGS

   Processor flags register.

.. py:data:: LLDB_REGNUM_GENERIC_ARG1

   The register that would contain pointer size or less argument 1 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG2

   The register that would contain pointer size or less argument 2 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG3

   The register that would contain pointer size or less argument 3 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG4

   The register that would contain pointer size or less argument 4 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG5

   The register that would contain pointer size or less argument 5 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG6

   The register that would contain pointer size or less argument 6 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG7

   The register that would contain pointer size or less argument 7 (if any).

.. py:data:: LLDB_REGNUM_GENERIC_ARG8

   The register that would contain pointer size or less argument 8 (if any).


Invalid value definitions
-------------------------

.. py:data:: LLDB_INVALID_BREAK_ID
.. py:data:: LLDB_INVALID_WATCH_ID
.. py:data:: LLDB_INVALID_ADDRESS
.. py:data:: LLDB_INVALID_INDEX32
.. py:data:: LLDB_INVALID_IVAR_OFFSET
.. py:data:: LLDB_INVALID_IMAGE_TOKEN
.. py:data:: LLDB_INVALID_MODULE_VERSION
.. py:data:: LLDB_INVALID_REGNUM
.. py:data:: LLDB_INVALID_UID
.. py:data:: LLDB_INVALID_PROCESS_ID
.. py:data:: LLDB_INVALID_THREAD_ID
.. py:data:: LLDB_INVALID_FRAME_ID
.. py:data:: LLDB_INVALID_SIGNAL_NUMBER
.. py:data:: LLDB_INVALID_OFFSET
.. py:data:: LLDB_INVALID_LINE_NUMBER
.. py:data:: LLDB_INVALID_QUEUE_ID

CPU types
---------

.. py:data:: LLDB_ARCH_DEFAULT
.. py:data:: LLDB_ARCH_DEFAULT_32BIT
.. py:data:: LLDB_ARCH_DEFAULT_64BIT
.. py:data:: LLDB_INVALID_CPUTYPE


Option set definitions
----------------------

.. py:data:: LLDB_MAX_NUM_OPTION_SETS
.. py:data:: LLDB_OPT_SET_ALL
.. py:data:: LLDB_OPT_SET_1
.. py:data:: LLDB_OPT_SET_2
.. py:data:: LLDB_OPT_SET_3
.. py:data:: LLDB_OPT_SET_4
.. py:data:: LLDB_OPT_SET_5
.. py:data:: LLDB_OPT_SET_6
.. py:data:: LLDB_OPT_SET_7
.. py:data:: LLDB_OPT_SET_8
.. py:data:: LLDB_OPT_SET_9
.. py:data:: LLDB_OPT_SET_10
.. py:data:: LLDB_OPT_SET_11

Miscellaneous constants
------------------------

.. py:data:: LLDB_GENERIC_ERROR
.. py:data:: LLDB_DEFAULT_BREAK_SIZE
.. py:data:: LLDB_WATCH_TYPE_READ
.. py:data:: LLDB_WATCH_TYPE_WRITE


Enumerators
***********


.. _State:

State
-----

.. py:data:: eStateInvalid
.. py:data:: eStateUnloaded

   Process is object is valid, but not currently loaded.

.. py:data:: eStateConnected

   Process is connected to remote debug services, but not
   launched or attached to anything yet.

.. py:data:: eStateAttaching

   Process is in the process of launching.

.. py:data:: eStateLaunching

   Process is in the process of launching.

.. py:data:: eStateStopped

   Process or thread is stopped and can be examined.

.. py:data:: eStateRunning

   Process or thread is running and can't be examined.

.. py:data:: eStateStepping

   Process or thread is in the process of stepping and can
   not be examined.

.. py:data:: eStateCrashed

   Process or thread has crashed and can be examined.

.. py:data:: eStateDetached

   Process has been detached and can't be examined.

.. py:data:: eStateExited

   Process has exited and can't be examined.

.. py:data:: eStateSuspended

   Process or thread is in a suspended state as far
   as the debugger is concerned while other processes
   or threads get the chance to run.


.. _LaunchFlag:

LaunchFlag
----------

.. py:data:: eLaunchFlagNone
.. py:data:: eLaunchFlagExec

   Exec when launching and turn the calling process into a new process.

.. py:data:: eLaunchFlagDebug

   Stop as soon as the process launches to allow the process to be debugged.

.. py:data:: eLaunchFlagStopAtEntry

   Stop at the program entry point instead of auto-continuing when launching or attaching at entry point.

.. py:data:: eLaunchFlagDisableASLR

   Disable Address Space Layout Randomization.

.. py:data:: eLaunchFlagDisableSTDIO

   Disable stdio for inferior process (e.g. for a GUI app).

.. py:data:: eLaunchFlagLaunchInTTY

   Launch the process in a new TTY if supported by the host.

.. py:data:: eLaunchFlagLaunchInShell

   Launch the process inside a shell to get shell expansion.

.. py:data:: eLaunchFlagLaunchInSeparateProcessGroup

   Launch the process in a separate process group if you are going to hand the process off (e.g. to debugserver)

.. py:data:: eLaunchFlagDontSetExitStatus

   set this flag so lldb & the handee don't race to set its exit status.

.. py:data:: eLaunchFlagDetachOnError

   If set, then the client stub should detach rather than killing  the debugee
   if it loses connection with lldb.

.. py:data:: eLaunchFlagShellExpandArguments

   Perform shell-style argument expansion

.. py:data:: eLaunchFlagCloseTTYOnExit

   Close the open TTY on exit

.. py:data:: eLaunchFlagInheritTCCFromParent

   Don't make the inferior responsible for its own TCC
   permissions but instead inherit them from its parent.


.. _RunMode:

RunMode
-------
.. py:data:: eOnlyThisThread
.. py:data:: eAllThreads
.. py:data:: eOnlyDuringStepping


.. _ByteOrder:

ByteOrder
---------

.. py:data:: eByteOrderInvalid
.. py:data:: eByteOrderBig
.. py:data:: eByteOrderPDP
.. py:data:: eByteOrderLittle


.. _Encoding:

Encoding
--------

.. py:data:: eEncodingInvalid
.. py:data:: eEncodingUint
.. py:data:: eEncodingSint
.. py:data:: eEncodingIEEE754
.. py:data:: eEncodingVector


.. _Format:

Format
------

.. py:data:: eFormatDefault
.. py:data:: eFormatInvalid
.. py:data:: eFormatBoolean
.. py:data:: eFormatBinary
.. py:data:: eFormatBytes
.. py:data:: eFormatBytesWithASCII
.. py:data:: eFormatChar
.. py:data:: eFormatCharPrintable
.. py:data:: eFormatComplex
.. py:data:: eFormatComplexFloat
.. py:data:: eFormatCString
.. py:data:: eFormatDecimal
.. py:data:: eFormatEnum
.. py:data:: eFormatHex
.. py:data:: eFormatHexUppercase
.. py:data:: eFormatFloat
.. py:data:: eFormatOctal
.. py:data:: eFormatOSType
.. py:data:: eFormatUnicode16
.. py:data:: eFormatUnicode32
.. py:data:: eFormatUnsigned
.. py:data:: eFormatPointer
.. py:data:: eFormatVectorOfChar
.. py:data:: eFormatVectorOfSInt8
.. py:data:: eFormatVectorOfUInt8
.. py:data:: eFormatVectorOfSInt16
.. py:data:: eFormatVectorOfUInt16
.. py:data:: eFormatVectorOfSInt32
.. py:data:: eFormatVectorOfUInt32
.. py:data:: eFormatVectorOfSInt64
.. py:data:: eFormatVectorOfUInt64
.. py:data:: eFormatVectorOfFloat16
.. py:data:: eFormatVectorOfFloat32
.. py:data:: eFormatVectorOfFloat64
.. py:data:: eFormatVectorOfUInt128
.. py:data:: eFormatComplexInteger
.. py:data:: eFormatCharArray
.. py:data:: eFormatAddressInfo
.. py:data:: eFormatHexFloat
.. py:data:: eFormatInstruction
.. py:data:: eFormatVoid
.. py:data:: eFormatUnicode8


.. _DescriptionLevel:

DescriptionLevel
----------------

.. py:data:: eDescriptionLevelBrief
.. py:data:: eDescriptionLevelFull
.. py:data:: eDescriptionLevelVerbose
.. py:data:: eDescriptionLevelInitial


.. _ScriptLanguage:

ScriptLanguage
--------------

.. py:data:: eScriptLanguageNone
.. py:data:: eScriptLanguagePython
.. py:data:: eScriptLanguageLua
.. py:data:: eScriptLanguageUnknown
.. py:data:: eScriptLanguageDefault


.. _RegisterKind:

RegisterKind
------------

.. py:data:: eRegisterKindEHFrame
.. py:data:: eRegisterKindDWARF
.. py:data:: eRegisterKindGeneric
.. py:data:: eRegisterKindProcessPlugin
.. py:data:: eRegisterKindLLDB


.. _StopReason:

StopReason
----------

.. py:data:: eStopReasonInvalid
.. py:data:: eStopReasonNone
.. py:data:: eStopReasonTrace
.. py:data:: eStopReasonBreakpoint
.. py:data:: eStopReasonWatchpoint
.. py:data:: eStopReasonSignal
.. py:data:: eStopReasonException
.. py:data:: eStopReasonExec
.. py:data:: eStopReasonFork
.. py:data:: eStopReasonVFork
.. py:data:: eStopReasonVForkDone
.. py:data:: eStopReasonPlanComplete
.. py:data:: eStopReasonThreadExiting
.. py:data:: eStopReasonInstrumentation


.. _ReturnStatus:

ReturnStatus
------------

.. py:data:: eReturnStatusInvalid
.. py:data:: eReturnStatusSuccessFinishNoResult
.. py:data:: eReturnStatusSuccessFinishResult
.. py:data:: eReturnStatusSuccessContinuingNoResult
.. py:data:: eReturnStatusSuccessContinuingResult
.. py:data:: eReturnStatusStarted
.. py:data:: eReturnStatusFailed
.. py:data:: eReturnStatusQuit


.. _Expression:

Expression
----------

The results of expression evaluation.

.. py:data:: eExpressionCompleted
.. py:data:: eExpressionSetupError
.. py:data:: eExpressionParseError
.. py:data:: eExpressionDiscarded
.. py:data:: eExpressionInterrupted
.. py:data:: eExpressionHitBreakpoint
.. py:data:: eExpressionTimedOut
.. py:data:: eExpressionResultUnavailable
.. py:data:: eExpressionStoppedForDebug
.. py:data:: eExpressionThreadVanished


.. _SearchDepth:

SearchDepth
-----------

.. py:data:: eSearchDepthInvalid
.. py:data:: eSearchDepthTarget
.. py:data:: eSearchDepthModule
.. py:data:: eSearchDepthCompUnit
.. py:data:: eSearchDepthFunction
.. py:data:: eSearchDepthBlock
.. py:data:: eSearchDepthAddress


.. _ConnectionStatus:

ConnectionStatus
----------------

.. py:data:: eConnectionStatusSuccess

   Success.

.. py:data:: eConnectionStatusEndOfFile

   End-of-file encountered.

.. py:data:: eConnectionStatusError

   Error encountered.

.. py:data:: eConnectionStatusTimedOut

   Request timed out.

.. py:data:: eConnectionStatusNoConnection

   No connection.

.. py:data:: eConnectionStatusLostConnection

   Lost connection while connected to a  valid connection.

.. py:data:: eConnectionStatusInterrupted

   Interrupted read.


.. _ErrorType:

ErrorType
---------

.. py:data:: eErrorTypeInvalid
.. py:data:: eErrorTypeGeneric

   Generic errors that can be any value.

.. py:data:: eErrorTypeMachKernel

   Mach kernel error codes.

.. py:data:: eErrorTypePOSIX

   POSIX error codes.

.. py:data:: eErrorTypeExpression

   These are from the ExpressionResults enum.

.. py:data:: eErrorTypeWin32

   Standard Win32 error codes.


.. _ValueType:

ValueType
---------

.. py:data:: eValueTypeInvalid
.. py:data:: eValueTypeVariableGlobal

   Global variable.

.. py:data:: eValueTypeVariableStatic

   Static variable.

.. py:data:: eValueTypeVariableArgument

   Function argument variable.

.. py:data:: eValueTypeVariableLocal

   Function local variable.

.. py:data:: eValueTypeRegister

   Stack frame register.

.. py:data:: eValueTypeRegisterSet

   A collection of stack frame register values.

.. py:data:: eValueTypeConstResult

   Constant result variables.

.. py:data:: eValueTypeVariableThreadLocal

   Thread local storage variable.


.. _InputReaderGranularity:

InputReaderGranularity
----------------------

Token size/granularities for Input Readers.

.. py:data:: eInputReaderGranularityInvalid
.. py:data:: eInputReaderGranularityByte
.. py:data:: eInputReaderGranularityWord
.. py:data:: eInputReaderGranularityLine
.. py:data:: eInputReaderGranularityAll


.. _SymbolContextItem:

SymbolContextItem
-----------------

These mask bits allow a common interface for queries that can
limit the amount of information that gets parsed to only the
information that is requested. These bits also can indicate what
actually did get resolved during query function calls.

Each definition corresponds to one of the member variables
in this class, and requests that that item be resolved, or
indicates that the member did get resolved.

.. py:data:: eSymbolContextTarget

   Set when target is requested from a query, or was located
   in query results.

.. py:data:: eSymbolContextModule

   Set when module is requested from a query, or was located
   in query results.

.. py:data:: eSymbolContextCompUnit

   Set when compilation unit is requested from a query, or was
   located in query results.

.. py:data:: eSymbolContextFunction

   Set when function is requested from a query, or was located
   in query results.

.. py:data:: eSymbolContextBlock

   Set when the deepest block is requested from a query, or
   was located in query results.

.. py:data:: eSymbolContextLineEntry

   Set when line entry is requested from a query, or was
   located in query results.

.. py:data:: eSymbolContextSymbol

   Set when symbol is requested from a query, or was located
   in query results

.. py:data:: eSymbolContextEverything

   Indicates to try and lookup everything up during a routine
   symbol context query.

.. py:data:: eSymbolContextVariable

   Set when global or static variable is requested from a
   query, or was located in query results.
   eSymbolContextVariable is potentially expensive to lookup so
   it isn't included in eSymbolContextEverything which stops it
   from being used during frame PC lookups and many other
   potential address to symbol context lookups.


.. _Permissions:

Permissions
-----------
.. py:data:: ePermissionsWritable
.. py:data:: ePermissionsReadable
.. py:data:: ePermissionsExecutable


.. _InputReader:

InputReader
-----------

.. py:data:: eInputReaderActivate

   Reader is newly pushed onto the reader stack.

.. py:data:: eInputReaderAsynchronousOutputWritten

   An async output event occurred; the reader may want to do something.

.. py:data:: eInputReaderReactivate

   Reader is on top of the stack again after another  reader was popped off.

.. py:data:: eInputReaderDeactivate

   Another reader was pushed on the stack.

.. py:data:: eInputReaderGotToken

   Reader got one of its tokens (granularity).

.. py:data:: eInputReaderInterrupt

   Reader received an interrupt signal (probably from  a control-c).

.. py:data:: eInputReaderEndOfFile

   Reader received an EOF char (probably from a control-d).

.. py:data:: eInputReaderDone

   Reader was just popped off the stack and is done.


.. _BreakpointEventType:

BreakpointEventType
-------------------

.. py:data:: eBreakpointEventTypeInvalidType
.. py:data:: eBreakpointEventTypeAdded
.. py:data:: eBreakpointEventTypeRemoved
.. py:data:: eBreakpointEventTypeLocationsAdded
.. py:data:: eBreakpointEventTypeLocationsRemoved
.. py:data:: eBreakpointEventTypeLocationsResolved
.. py:data:: eBreakpointEventTypeEnabled
.. py:data:: eBreakpointEventTypeDisabled
.. py:data:: eBreakpointEventTypeCommandChanged
.. py:data:: eBreakpointEventTypeConditionChanged
.. py:data:: eBreakpointEventTypeIgnoreChanged
.. py:data:: eBreakpointEventTypeThreadChanged
.. py:data:: eBreakpointEventTypeAutoContinueChanged


.. _WatchpointEventType:

WatchpointEventType
-------------------

.. py:data:: eWatchpointEventTypeInvalidType
.. py:data:: eWatchpointEventTypeAdded
.. py:data:: eWatchpointEventTypeRemoved
.. py:data:: eWatchpointEventTypeEnabled
.. py:data:: eWatchpointEventTypeDisabled
.. py:data:: eWatchpointEventTypeCommandChanged
.. py:data:: eWatchpointEventTypeConditionChanged
.. py:data:: eWatchpointEventTypeIgnoreChanged
.. py:data:: eWatchpointEventTypeThreadChanged
.. py:data:: eWatchpointEventTypeTypeChanged


.. _LanguageType:

LanguageType
------------

.. py:data:: eLanguageTypeUnknown
.. py:data:: eLanguageTypeC89
.. py:data:: eLanguageTypeC
.. py:data:: eLanguageTypeAda83
.. py:data:: eLanguageTypeC_plus_plus
.. py:data:: eLanguageTypeCobol74
.. py:data:: eLanguageTypeCobol85
.. py:data:: eLanguageTypeFortran77
.. py:data:: eLanguageTypeFortran90
.. py:data:: eLanguageTypePascal83
.. py:data:: eLanguageTypeModula2
.. py:data:: eLanguageTypeJava
.. py:data:: eLanguageTypeC99
.. py:data:: eLanguageTypeAda95
.. py:data:: eLanguageTypeFortran95
.. py:data:: eLanguageTypePLI
.. py:data:: eLanguageTypeObjC
.. py:data:: eLanguageTypeObjC_plus_plus
.. py:data:: eLanguageTypeUPC
.. py:data:: eLanguageTypeD
.. py:data:: eLanguageTypePython
.. py:data:: eLanguageTypeOpenCL
.. py:data:: eLanguageTypeGo
.. py:data:: eLanguageTypeModula3
.. py:data:: eLanguageTypeHaskell
.. py:data:: eLanguageTypeC_plus_plus_03
.. py:data:: eLanguageTypeC_plus_plus_11
.. py:data:: eLanguageTypeOCaml
.. py:data:: eLanguageTypeRust
.. py:data:: eLanguageTypeC11
.. py:data:: eLanguageTypeSwift
.. py:data:: eLanguageTypeJulia
.. py:data:: eLanguageTypeDylan
.. py:data:: eLanguageTypeC_plus_plus_14
.. py:data:: eLanguageTypeFortran03
.. py:data:: eLanguageTypeFortran08
.. py:data:: eLanguageTypeMipsAssembler
.. py:data:: eLanguageTypeMojo
.. py:data:: eLanguageTypeExtRenderScript
.. py:data:: eNumLanguageTypes


.. _InstrumentationRuntimeType:

InstrumentationRuntimeType
--------------------------

.. py:data:: eInstrumentationRuntimeTypeAddressSanitizer
.. py:data:: eInstrumentationRuntimeTypeThreadSanitizer
.. py:data:: eInstrumentationRuntimeTypeUndefinedBehaviorSanitizer
.. py:data:: eInstrumentationRuntimeTypeMainThreadChecker
.. py:data:: eInstrumentationRuntimeTypeSwiftRuntimeReporting
.. py:data:: eNumInstrumentationRuntimeTypes


.. _DynamicValueType:

DynamicValueType
----------------

.. py:data:: eNoDynamicValues
.. py:data:: eDynamicCanRunTarget
.. py:data:: eDynamicDontRunTarget


.. _StopShowColumn:

StopShowColumn
--------------

.. py:data:: eStopShowColumnAnsiOrCaret
.. py:data:: eStopShowColumnAnsi
.. py:data:: eStopShowColumnCaret
.. py:data:: eStopShowColumnNone


.. _AccessType:

AccessType
----------

.. py:data:: eAccessNone
.. py:data:: eAccessPublic
.. py:data:: eAccessPrivate
.. py:data:: eAccessProtected
.. py:data:: eAccessPackage


.. _CommandArgumentType:

CommandArgumentType
-------------------

.. py:data:: eArgTypeAddress
.. py:data:: eArgTypeAddressOrExpression
.. py:data:: eArgTypeAliasName
.. py:data:: eArgTypeAliasOptions
.. py:data:: eArgTypeArchitecture
.. py:data:: eArgTypeBoolean
.. py:data:: eArgTypeBreakpointID
.. py:data:: eArgTypeBreakpointIDRange
.. py:data:: eArgTypeBreakpointName
.. py:data:: eArgTypeByteSize
.. py:data:: eArgTypeClassName
.. py:data:: eArgTypeCommandName
.. py:data:: eArgTypeCount
.. py:data:: eArgTypeDescriptionVerbosity
.. py:data:: eArgTypeDirectoryName
.. py:data:: eArgTypeDisassemblyFlavor
.. py:data:: eArgTypeEndAddress
.. py:data:: eArgTypeExpression
.. py:data:: eArgTypeExpressionPath
.. py:data:: eArgTypeExprFormat
.. py:data:: eArgTypeFileLineColumn
.. py:data:: eArgTypeFilename
.. py:data:: eArgTypeFormat
.. py:data:: eArgTypeFrameIndex
.. py:data:: eArgTypeFullName
.. py:data:: eArgTypeFunctionName
.. py:data:: eArgTypeFunctionOrSymbol
.. py:data:: eArgTypeGDBFormat
.. py:data:: eArgTypeHelpText
.. py:data:: eArgTypeIndex
.. py:data:: eArgTypeLanguage
.. py:data:: eArgTypeLineNum
.. py:data:: eArgTypeLogCategory
.. py:data:: eArgTypeLogChannel
.. py:data:: eArgTypeMethod
.. py:data:: eArgTypeName
.. py:data:: eArgTypeNewPathPrefix
.. py:data:: eArgTypeNumLines
.. py:data:: eArgTypeNumberPerLine
.. py:data:: eArgTypeOffset
.. py:data:: eArgTypeOldPathPrefix
.. py:data:: eArgTypeOneLiner
.. py:data:: eArgTypePath
.. py:data:: eArgTypePermissionsNumber
.. py:data:: eArgTypePermissionsString
.. py:data:: eArgTypePid
.. py:data:: eArgTypePlugin
.. py:data:: eArgTypeProcessName
.. py:data:: eArgTypePythonClass
.. py:data:: eArgTypePythonFunction
.. py:data:: eArgTypePythonScript
.. py:data:: eArgTypeQueueName
.. py:data:: eArgTypeRegisterName
.. py:data:: eArgTypeRegularExpression
.. py:data:: eArgTypeRunArgs
.. py:data:: eArgTypeRunMode
.. py:data:: eArgTypeScriptedCommandSynchronicity
.. py:data:: eArgTypeScriptLang
.. py:data:: eArgTypeSearchWord
.. py:data:: eArgTypeSelector
.. py:data:: eArgTypeSettingIndex
.. py:data:: eArgTypeSettingKey
.. py:data:: eArgTypeSettingPrefix
.. py:data:: eArgTypeSettingVariableName
.. py:data:: eArgTypeShlibName
.. py:data:: eArgTypeSourceFile
.. py:data:: eArgTypeSortOrder
.. py:data:: eArgTypeStartAddress
.. py:data:: eArgTypeSummaryString
.. py:data:: eArgTypeSymbol
.. py:data:: eArgTypeThreadID
.. py:data:: eArgTypeThreadIndex
.. py:data:: eArgTypeThreadName
.. py:data:: eArgTypeTypeName
.. py:data:: eArgTypeUnsignedInteger
.. py:data:: eArgTypeUnixSignal
.. py:data:: eArgTypeVarName
.. py:data:: eArgTypeValue
.. py:data:: eArgTypeWidth
.. py:data:: eArgTypeNone
.. py:data:: eArgTypePlatform
.. py:data:: eArgTypeWatchpointID
.. py:data:: eArgTypeWatchpointIDRange
.. py:data:: eArgTypeWatchType
.. py:data:: eArgRawInput
.. py:data:: eArgTypeCommand
.. py:data:: eArgTypeColumnNum
.. py:data:: eArgTypeModuleUUID
.. py:data:: eArgTypeLastArg
.. py:data:: eArgTypeCompletionType

.. _SymbolType:

SymbolType
----------

.. py:data:: eSymbolTypeAny
.. py:data:: eSymbolTypeInvalid
.. py:data:: eSymbolTypeAbsolute
.. py:data:: eSymbolTypeCode
.. py:data:: eSymbolTypeResolver
.. py:data:: eSymbolTypeData
.. py:data:: eSymbolTypeTrampoline
.. py:data:: eSymbolTypeRuntime
.. py:data:: eSymbolTypeException
.. py:data:: eSymbolTypeSourceFile
.. py:data:: eSymbolTypeHeaderFile
.. py:data:: eSymbolTypeObjectFile
.. py:data:: eSymbolTypeCommonBlock
.. py:data:: eSymbolTypeBlock
.. py:data:: eSymbolTypeLocal
.. py:data:: eSymbolTypeParam
.. py:data:: eSymbolTypeVariable
.. py:data:: eSymbolTypeVariableType
.. py:data:: eSymbolTypeLineEntry
.. py:data:: eSymbolTypeLineHeader
.. py:data:: eSymbolTypeScopeBegin
.. py:data:: eSymbolTypeScopeEnd
.. py:data:: eSymbolTypeAdditional
.. py:data:: eSymbolTypeCompiler
.. py:data:: eSymbolTypeInstrumentation
.. py:data:: eSymbolTypeUndefined
.. py:data:: eSymbolTypeObjCClass
.. py:data:: eSymbolTypeObjCMetaClass
.. py:data:: eSymbolTypeObjCIVar
.. py:data:: eSymbolTypeReExported


.. _SectionType:

SectionType
-----------

.. py:data:: eSectionTypeInvalid
.. py:data:: eSectionTypeCode
.. py:data:: eSectionTypeContainer
.. py:data:: eSectionTypeData
.. py:data:: eSectionTypeDataCString
.. py:data:: eSectionTypeDataCStringPointers
.. py:data:: eSectionTypeDataSymbolAddress
.. py:data:: eSectionTypeData4
.. py:data:: eSectionTypeData8
.. py:data:: eSectionTypeData16
.. py:data:: eSectionTypeDataPointers
.. py:data:: eSectionTypeDebug
.. py:data:: eSectionTypeZeroFill
.. py:data:: eSectionTypeDataObjCMessageRefs
.. py:data:: eSectionTypeDataObjCCFStrings
.. py:data:: eSectionTypeDWARFDebugAbbrev
.. py:data:: eSectionTypeDWARFDebugAddr
.. py:data:: eSectionTypeDWARFDebugAranges
.. py:data:: eSectionTypeDWARFDebugCuIndex
.. py:data:: eSectionTypeDWARFDebugFrame
.. py:data:: eSectionTypeDWARFDebugInfo
.. py:data:: eSectionTypeDWARFDebugLine
.. py:data:: eSectionTypeDWARFDebugLoc
.. py:data:: eSectionTypeDWARFDebugMacInfo
.. py:data:: eSectionTypeDWARFDebugMacro
.. py:data:: eSectionTypeDWARFDebugPubNames
.. py:data:: eSectionTypeDWARFDebugPubTypes
.. py:data:: eSectionTypeDWARFDebugRanges
.. py:data:: eSectionTypeDWARFDebugStr
.. py:data:: eSectionTypeDWARFDebugStrOffsets
.. py:data:: eSectionTypeDWARFAppleNames
.. py:data:: eSectionTypeDWARFAppleTypes
.. py:data:: eSectionTypeDWARFAppleNamespaces
.. py:data:: eSectionTypeDWARFAppleObjC
.. py:data:: eSectionTypeELFSymbolTable
.. py:data:: eSectionTypeELFDynamicSymbols
.. py:data:: eSectionTypeELFRelocationEntries
.. py:data:: eSectionTypeELFDynamicLinkInfo
.. py:data:: eSectionTypeEHFrame
.. py:data:: eSectionTypeARMexidx
.. py:data:: eSectionTypeARMextab
.. py:data:: eSectionTypeCompactUnwind
.. py:data:: eSectionTypeGoSymtab
.. py:data:: eSectionTypeAbsoluteAddress
.. py:data:: eSectionTypeDWARFGNUDebugAltLink
.. py:data:: eSectionTypeDWARFDebugTypes
.. py:data:: eSectionTypeDWARFDebugNames
.. py:data:: eSectionTypeOther
.. py:data:: eSectionTypeDWARFDebugLineStr
.. py:data:: eSectionTypeDWARFDebugRngLists
.. py:data:: eSectionTypeDWARFDebugLocLists
.. py:data:: eSectionTypeDWARFDebugAbbrevDwo
.. py:data:: eSectionTypeDWARFDebugInfoDwo
.. py:data:: eSectionTypeDWARFDebugStrDwo
.. py:data:: eSectionTypeDWARFDebugStrOffsetsDwo
.. py:data:: eSectionTypeDWARFDebugTypesDwo
.. py:data:: eSectionTypeDWARFDebugRngListsDwo
.. py:data:: eSectionTypeDWARFDebugLocDwo
.. py:data:: eSectionTypeDWARFDebugLocListsDwo
.. py:data:: eSectionTypeDWARFDebugTuIndex


.. _EmulatorInstructionOption:

EmulatorInstructionOption
-------------------------

.. py:data:: eEmulateInstructionOptionNone
.. py:data:: eEmulateInstructionOptionAutoAdvancePC
.. py:data:: eEmulateInstructionOptionIgnoreConditions


.. _FunctionNameType:

FunctionNameType
----------------

.. py:data:: eFunctionNameTypeNone
.. py:data:: eFunctionNameTypeAuto
.. py:data:: eFunctionNameTypeFull
.. py:data:: eFunctionNameTypeBase
.. py:data:: eFunctionNameTypeMethod
.. py:data:: eFunctionNameTypeSelector
.. py:data:: eFunctionNameTypeAny


.. _BasicType:

BasicType
---------

.. py:data:: eBasicTypeInvalid
.. py:data:: eBasicTypeVoid
.. py:data:: eBasicTypeChar
.. py:data:: eBasicTypeSignedChar
.. py:data:: eBasicTypeUnsignedChar
.. py:data:: eBasicTypeWChar
.. py:data:: eBasicTypeSignedWChar
.. py:data:: eBasicTypeUnsignedWChar
.. py:data:: eBasicTypeChar16
.. py:data:: eBasicTypeChar32
.. py:data:: eBasicTypeChar8
.. py:data:: eBasicTypeShort
.. py:data:: eBasicTypeUnsignedShort
.. py:data:: eBasicTypeInt
.. py:data:: eBasicTypeUnsignedInt
.. py:data:: eBasicTypeLong
.. py:data:: eBasicTypeUnsignedLong
.. py:data:: eBasicTypeLongLong
.. py:data:: eBasicTypeUnsignedLongLong
.. py:data:: eBasicTypeInt128
.. py:data:: eBasicTypeUnsignedInt128
.. py:data:: eBasicTypeBool
.. py:data:: eBasicTypeHalf
.. py:data:: eBasicTypeFloat
.. py:data:: eBasicTypeDouble
.. py:data:: eBasicTypeLongDouble
.. py:data:: eBasicTypeFloatComplex
.. py:data:: eBasicTypeDoubleComplex
.. py:data:: eBasicTypeLongDoubleComplex
.. py:data:: eBasicTypeObjCID
.. py:data:: eBasicTypeObjCClass
.. py:data:: eBasicTypeObjCSel
.. py:data:: eBasicTypeNullPtr
.. py:data:: eBasicTypeOther


.. _TraceType:

TraceType
---------

.. py:data:: eTraceTypeNone
.. py:data:: eTraceTypeProcessorTrace


.. _StructuredDataType:

StructuredDataType
------------------

.. py:data:: eStructuredDataTypeInvalid
.. py:data:: eStructuredDataTypeNull
.. py:data:: eStructuredDataTypeGeneric
.. py:data:: eStructuredDataTypeArray
.. py:data:: eStructuredDataTypeInteger
.. py:data:: eStructuredDataTypeFloat
.. py:data:: eStructuredDataTypeBoolean
.. py:data:: eStructuredDataTypeString
.. py:data:: eStructuredDataTypeDictionary


.. _TypeClass:

TypeClass
---------

.. py:data:: eTypeClassInvalid
.. py:data:: eTypeClassArray
.. py:data:: eTypeClassBlockPointer
.. py:data:: eTypeClassBuiltin
.. py:data:: eTypeClassClass
.. py:data:: eTypeClassFloat
.. py:data:: eTypeClassComplexInteger
.. py:data:: eTypeClassComplexFloat
.. py:data:: eTypeClassFunction
.. py:data:: eTypeClassMemberPointer
.. py:data:: eTypeClassObjCObject
.. py:data:: eTypeClassObjCInterface
.. py:data:: eTypeClassObjCObjectPointer
.. py:data:: eTypeClassPointer
.. py:data:: eTypeClassReference
.. py:data:: eTypeClassStruct
.. py:data:: eTypeClassTypedef
.. py:data:: eTypeClassUnion
.. py:data:: eTypeClassVector
.. py:data:: eTypeClassOther
.. py:data:: eTypeClassAny


.. _TemplateArgument:

TemplateArgument
----------------

.. py:data:: eTemplateArgumentKindNull
.. py:data:: eTemplateArgumentKindType
.. py:data:: eTemplateArgumentKindDeclaration
.. py:data:: eTemplateArgumentKindIntegral
.. py:data:: eTemplateArgumentKindTemplate
.. py:data:: eTemplateArgumentKindTemplateExpansion
.. py:data:: eTemplateArgumentKindExpression
.. py:data:: eTemplateArgumentKindPack
.. py:data:: eTemplateArgumentKindNullPtr
.. py:data:: eTemplateArgumentKindUncommonValue


.. _TypeOption:

TypeOption
----------

Options that can be set for a formatter to alter its behavior. Not
all of these are applicable to all formatter types.

.. py:data:: eTypeOptionNone
.. py:data:: eTypeOptionCascade
.. py:data:: eTypeOptionSkipPointers
.. py:data:: eTypeOptionSkipReferences
.. py:data:: eTypeOptionHideChildren
.. py:data:: eTypeOptionHideValue
.. py:data:: eTypeOptionShowOneLiner
.. py:data:: eTypeOptionHideNames
.. py:data:: eTypeOptionNonCacheable
.. py:data:: eTypeOptionHideEmptyAggregates
.. py:data:: eTypeOptionFrontEndWantsDereference



.. _FrameCompare:

FrameCompare
------------

This is the return value for frame comparisons.  If you are comparing frame
A to frame B the following cases arise:

   1) When frame A pushes frame B (or a frame that ends up pushing
      B) A is Older than B.

   2) When frame A pushed frame B (or if frameA is on the stack
      but B is not) A is Younger than B.

   3) When frame A and frame B have the same StackID, they are
      Equal.

   4) When frame A and frame B have the same immediate parent
      frame, but are not equal, the comparison yields SameParent.

   5) If the two frames are on different threads or processes the
      comparison is Invalid.

   6) If for some reason we can't figure out what went on, we
      return Unknown.

.. py:data:: eFrameCompareInvalid
.. py:data:: eFrameCompareUnknown
.. py:data:: eFrameCompareEqual
.. py:data:: eFrameCompareSameParent
.. py:data:: eFrameCompareYounger
.. py:data:: eFrameCompareOlder


.. _FilePermissions:

FilePermissions
---------------

.. py:data:: eFilePermissionsUserRead
.. py:data:: eFilePermissionsUserWrite
.. py:data:: eFilePermissionsUserExecute
.. py:data:: eFilePermissionsGroupRead
.. py:data:: eFilePermissionsGroupWrite
.. py:data:: eFilePermissionsGroupExecute
.. py:data:: eFilePermissionsWorldRead
.. py:data:: eFilePermissionsWorldWrite
.. py:data:: eFilePermissionsWorldExecute
.. py:data:: eFilePermissionsUserRW
.. py:data:: eFileFilePermissionsUserRX
.. py:data:: eFilePermissionsUserRWX
.. py:data:: eFilePermissionsGroupRW
.. py:data:: eFilePermissionsGroupRX
.. py:data:: eFilePermissionsGroupRWX
.. py:data:: eFilePermissionsWorldRW
.. py:data:: eFilePermissionsWorldRX
.. py:data:: eFilePermissionsWorldRWX
.. py:data:: eFilePermissionsEveryoneR
.. py:data:: eFilePermissionsEveryoneW
.. py:data:: eFilePermissionsEveryoneX
.. py:data:: eFilePermissionsEveryoneRW
.. py:data:: eFilePermissionsEveryoneRX
.. py:data:: eFilePermissionsEveryoneRWX
.. py:data:: eFilePermissionsFileDefault = eFilePermissionsUserRW,
.. py:data:: eFilePermissionsDirectoryDefault


.. _QueueItem:

QueueItem
---------
.. py:data:: eQueueItemKindUnknown
.. py:data:: eQueueItemKindFunction
.. py:data:: eQueueItemKindBlock


.. _QueueKind:

QueueKind
---------

libdispatch aka Grand Central Dispatch (GCD) queues can be either
serial (executing on one thread) or concurrent (executing on
multiple threads).

.. py:data:: eQueueKindUnknown
.. py:data:: eQueueKindSerial
.. py:data:: eQueueKindConcurrent


.. _ExpressionEvaluationPhase:

ExpressionEvaluationPhase
-------------------------

These are the cancellable stages of expression evaluation, passed
to the expression evaluation callback, so that you can interrupt
expression evaluation at the various points in its lifecycle.

.. py:data:: eExpressionEvaluationParse
.. py:data:: eExpressionEvaluationIRGen
.. py:data:: eExpressionEvaluationExecution
.. py:data:: eExpressionEvaluationComplete


.. _WatchpointKind:

WatchpointKind
--------------

Indicates what types of events cause the watchpoint to fire. Used by Native
-Protocol-related classes.

.. py:data:: eWatchpointKindWrite
.. py:data:: eWatchpointKindRead


.. _GdbSignal:

GdbSignal
---------

.. py:data:: eGdbSignalBadAccess
.. py:data:: eGdbSignalBadInstruction
.. py:data:: eGdbSignalArithmetic
.. py:data:: eGdbSignalEmulation
.. py:data:: eGdbSignalSoftware
.. py:data:: eGdbSignalBreakpoint

.. _PathType:

PathType
--------

Used with `SBHostOS.GetLLDBPath` to find files that are
related to LLDB on the current host machine. Most files are
relative to LLDB or are in known locations.

.. py:data:: ePathTypeLLDBShlibDir

   The directory where the lldb.so (unix) or LLDB mach-o file in
   LLDB.framework (MacOSX) exists.

.. py:data:: ePathTypeSupportExecutableDir

   Find LLDB support executable directory (debugserver, etc).

.. py:data:: ePathTypeHeaderDir

   Find LLDB header file directory.

.. py:data:: ePathTypePythonDir

   Find Python modules (PYTHONPATH) directory.

.. py:data:: ePathTypeLLDBSystemPlugins

   System plug-ins directory

.. py:data:: ePathTypeLLDBUserPlugins

   User plug-ins directory

.. py:data:: ePathTypeLLDBTempSystemDir

   The LLDB temp directory for this system that will be cleaned up on exit.

.. py:data:: ePathTypeGlobalLLDBTempSystemDir

   The LLDB temp directory for this system, NOT cleaned up on a process
   exit.

.. py:data:: ePathTypeClangDir

   Find path to Clang builtin headers.


.. _MemberFunctionKind:

MemberFunctionKind
------------------

.. py:data:: eMemberFunctionKindUnknown
.. py:data:: eMemberFunctionKindConstructor

   A function used to create instances.

.. py:data:: eMemberFunctionKindDestructor

   A function used to tear down existing instances.

.. py:data:: eMemberFunctionKindInstanceMethod

   A function that applies to a specific instance.

.. py:data:: eMemberFunctionKindStaticMethod

   A function that applies to a type rather than any instance,


.. _TypeFlags:

TypeFlags
---------

.. py:data:: eTypeHasChildren
.. py:data:: eTypeIsArray
.. py:data:: eTypeIsBuiltIn
.. py:data:: eTypeIsCPlusPlus
.. py:data:: eTypeIsFuncPrototype
.. py:data:: eTypeIsObjC
.. py:data:: eTypeIsReference
.. py:data:: eTypeIsTemplate
.. py:data:: eTypeIsVector
.. py:data:: eTypeIsInteger
.. py:data:: eTypeIsComplex
.. py:data:: eTypeInstanceIsPointer


.. _CommandFlags:

CommandFlags
---------------

.. py:data:: eCommandRequiresTarget
.. py:data:: eCommandRequiresProcess
.. py:data:: eCommandRequiresThread
.. py:data:: eCommandRequiresFrame
.. py:data:: eCommandRequiresRegContext
.. py:data:: eCommandTryTargetAPILock
.. py:data:: eCommandProcessMustBeLaunched
.. py:data:: eCommandProcessMustBePaused
.. py:data:: eCommandProcessMustBeTraced


.. _TypeSummary:

TypeSummary
-----------

Whether a summary should cap how much data it returns to users or not.

.. py:data:: eTypeSummaryCapped
.. py:data:: eTypeSummaryUncapped


.. _CommandInterpreterResult:

CommandInterpreterResult
------------------------

The result from a command interpreter run.

.. py:data:: eCommandInterpreterResultSuccess

   Command interpreter finished successfully.

.. py:data:: eCommandInterpreterResultInferiorCrash

   Stopped because the corresponding option was set and the inferior
   crashed.

.. py:data:: eCommandInterpreterResultCommandError

   Stopped because the corresponding option was set and a command returned
   an error.

.. py:data:: eCommandInterpreterResultQuitRequested

   Stopped because quit was requested.


.. _WatchPointValueKind:

WatchPointValueKind
-------------------

The type of value that the watchpoint was created to monitor.

.. py:data:: eWatchPointValueKindInvalid

   Invalid kind.

.. py:data:: eWatchPointValueKindVariable

   Watchpoint was created watching a variable

.. py:data:: eWatchPointValueKindExpression

   Watchpoint was created watching the result of an expression that was
   evaluated at creation time.
