//===-- SBDefines.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBDEFINES_H
#define LLDB_API_SBDEFINES_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "lldb/lldb-versioning.h"

#include <cstdio> // For FILE *

#ifndef LLDB_API
#if defined(_WIN32)
#if defined(LLDB_IN_LIBLLDB)
#define LLDB_API __declspec(dllexport)
#else
#define LLDB_API __declspec(dllimport)
#endif
#else // defined (_WIN32)
#define LLDB_API
#endif
#endif

// Don't add the deprecated attribute when generating the bindings or when
// building for anything older than C++14 which is the first version that
// supports the attribute.
#if defined(SWIG) || _cplusplus < 201402L
#undef LLDB_DEPRECATED
#undef LLDB_DEPRECATED_FIXME
#define LLDB_DEPRECATED(MSG)
#define LLDB_DEPRECATED_FIXME(MSG, FIX)
#endif

// Forward Declarations
namespace lldb {

class LLDB_API SBAddress;
class LLDB_API SBAddressRange;
class LLDB_API SBAddressRangeList;
class LLDB_API SBAttachInfo;
class LLDB_API SBBlock;
class LLDB_API SBBreakpoint;
class LLDB_API SBBreakpointList;
class LLDB_API SBBreakpointLocation;
class LLDB_API SBBreakpointName;
class LLDB_API SBBreakpointNameImpl;
class LLDB_API SBBroadcaster;
class LLDB_API SBCommand;
class LLDB_API SBCommandInterpreter;
class LLDB_API SBCommandInterpreterRunOptions;
class LLDB_API SBCommandInterpreterRunResult;
class LLDB_API SBCommandPluginInterface;
class LLDB_API SBCommandReturnObject;
class LLDB_API SBCommunication;
class LLDB_API SBCompileUnit;
class LLDB_API SBSaveCoreOptions;
class LLDB_API SBData;
class LLDB_API SBDebugger;
class LLDB_API SBDeclaration;
class LLDB_API SBEnvironment;
class LLDB_API SBError;
class LLDB_API SBEvent;
class LLDB_API SBEventList;
class LLDB_API SBExecutionContext;
class LLDB_API SBExpressionOptions;
class LLDB_API SBFile;
class LLDB_API SBFileSpec;
class LLDB_API SBFileSpecList;
class LLDB_API SBFormat;
class LLDB_API SBFrame;
class LLDB_API SBFunction;
class LLDB_API SBHostOS;
class LLDB_API SBInstruction;
class LLDB_API SBInstructionList;
class LLDB_API SBLanguageRuntime;
class LLDB_API SBLaunchInfo;
class LLDB_API SBLineEntry;
class LLDB_API SBListener;
class LLDB_API SBMemoryRegionInfo;
class LLDB_API SBMemoryRegionInfoList;
class LLDB_API SBModule;
class LLDB_API SBModuleSpec;
class LLDB_API SBModuleSpecList;
class LLDB_API SBPlatform;
class LLDB_API SBPlatformConnectOptions;
class LLDB_API SBPlatformShellCommand;
class LLDB_API SBProcess;
class LLDB_API SBProcessInfo;
class LLDB_API SBProcessInfoList;
class LLDB_API SBQueue;
class LLDB_API SBQueueItem;
class LLDB_API SBReplayOptions;
class LLDB_API SBReproducer;
class LLDB_API SBScriptObject;
class LLDB_API SBSection;
class LLDB_API SBSourceManager;
class LLDB_API SBStatisticsOptions;
class LLDB_API SBStream;
class LLDB_API SBStringList;
class LLDB_API SBStructuredData;
class LLDB_API SBSymbol;
class LLDB_API SBSymbolContext;
class LLDB_API SBSymbolContextList;
class LLDB_API SBTarget;
class LLDB_API SBThread;
class LLDB_API SBThreadCollection;
class LLDB_API SBThreadPlan;
class LLDB_API SBTrace;
class LLDB_API SBTraceCursor;
class LLDB_API SBType;
class LLDB_API SBTypeCategory;
class LLDB_API SBTypeEnumMember;
class LLDB_API SBTypeEnumMemberList;
class LLDB_API SBTypeFilter;
class LLDB_API SBTypeFormat;
class LLDB_API SBTypeMember;
class LLDB_API SBTypeMemberFunction;
class LLDB_API SBTypeNameSpecifier;
class LLDB_API SBTypeSummary;
class LLDB_API SBTypeSummaryOptions;
class LLDB_API SBTypeSynthetic;
class LLDB_API SBTypeList;
class LLDB_API SBValue;
class LLDB_API SBValueList;
class LLDB_API SBVariablesOptions;
class LLDB_API SBWatchpoint;
class LLDB_API SBWatchpointOptions;
class LLDB_API SBUnixSignals;

typedef bool (*SBBreakpointHitCallback)(void *baton, SBProcess &process,
                                        SBThread &thread,
                                        lldb::SBBreakpointLocation &location);

typedef void (*SBDebuggerDestroyCallback)(lldb::user_id_t debugger_id,
                                          void *baton);

typedef SBError (*SBPlatformLocateModuleCallback)(
    void *baton, const SBModuleSpec &module_spec, SBFileSpec &module_file_spec,
    SBFileSpec &symbol_file_spec);
}

#endif // LLDB_API_SBDEFINES_H
