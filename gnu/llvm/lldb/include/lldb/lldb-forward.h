//===-- lldb-forward.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_LLDB_FORWARD_H
#define LLDB_LLDB_FORWARD_H

#include <memory>

// lldb forward declarations
namespace lldb_private {

class ABI;
class ASTResultSynthesizer;
class ASTStructExtractor;
class Address;
class AddressRange;
class AddressRanges;
class AddressRangeList;
class AddressResolver;
class ArchSpec;
class Architecture;
class Args;
class ArmUnwindInfo;
class Baton;
class Block;
class Breakpoint;
class BreakpointID;
class BreakpointIDList;
class BreakpointList;
class BreakpointLocation;
class BreakpointLocationCollection;
class BreakpointLocationList;
class BreakpointName;
class BreakpointOptionGroup;
class BreakpointOptions;
class BreakpointPrecondition;
class BreakpointResolver;
class BreakpointSite;
class BroadcastEventSpec;
class Broadcaster;
class BroadcasterManager;
class CXXSyntheticChildren;
struct CacheSignature;
class CallFrameInfo;
class CommandInterpreter;
class CommandInterpreterRunOptions;
class CommandObject;
class CommandObjectMultiword;
class CommandReturnObject;
class Communication;
class CompactUnwindInfo;
class CompileUnit;
class CompilerDecl;
class CompilerDeclContext;
class CompilerType;
class Connection;
class ConnectionFileDescriptor;
class ConstString;
class ConstStringTable;
class DWARFCallFrameInfo;
class DWARFDataExtractor;
class DWARFExpression;
class DWARFExpressionList;
class DataBuffer;
class WritableDataBuffer;
class DataBufferHeap;
class DataEncoder;
class DataExtractor;
class DataFileCache;
class Debugger;
class Declaration;
class DiagnosticManager;
class Disassembler;
class DumpValueObjectOptions;
class DynamicCheckerFunctions;
class DynamicLoader;
class Editline;
class EmulateInstruction;
class Environment;
class EvaluateExpressionOptions;
class Event;
class EventData;
class EventDataStructuredData;
class ExecutionContext;
class ExecutionContextRef;
class ExecutionContextScope;
class Expression;
class ExpressionTypeSystemHelper;
class ExpressionVariable;
class ExpressionVariableList;
class File;
class FileSpec;
class FileSpecList;
class Flags;
namespace FormatEntity {
struct Entry;
} // namespace FormatEntity
class FormatManager;
class FormattersMatchCandidate;
class FuncUnwinders;
class Function;
class FunctionCaller;
class FunctionInfo;
class IOHandler;
class IOObject;
class IRExecutionUnit;
class InlineFunctionInfo;
class Instruction;
class InstructionList;
class InstrumentationRuntime;
class JITLoader;
class JITLoaderList;
class Language;
class LanguageCategory;
class LanguageRuntime;
class LineTable;
class Listener;
class Log;
class Mangled;
class Materializer;
class MemoryHistory;
class MemoryRegionInfo;
class MemoryRegionInfos;
class Module;
class ModuleList;
class ModuleSpec;
class ModuleSpecList;
class ObjectContainer;
class ObjectFile;
class ObjectFileJITDelegate;
class OperatingSystem;
class OperatingSystemInterface;
class OptionGroup;
class OptionGroupOptions;
class OptionGroupPlatform;
class OptionValue;
class OptionValueArch;
class OptionValueArgs;
class OptionValueArray;
class OptionValueBoolean;
class OptionValueChar;
class OptionValueDictionary;
class OptionValueEnumeration;
class OptionValueFileSpec;
class OptionValueFileSpecList;
class OptionValueFormat;
class OptionValueFormatEntity;
class OptionValueLanguage;
class OptionValuePathMappings;
class OptionValueProperties;
class OptionValueRegex;
class OptionValueSInt64;
class OptionValueString;
class OptionValueUInt64;
class OptionValueUUID;
class Options;
class PathMappingList;
class PersistentExpressionState;
class Platform;
class Process;
class ProcessAttachInfo;
class ProcessLaunchInfo;
class ProcessInfo;
class ProcessInstanceInfo;
class ProcessInstanceInfoMatch;
class ProcessLaunchInfo;
class ProcessModID;
class Property;
class Queue;
class QueueImpl;
class QueueItem;
class REPL;
class RecognizedStackFrame;
class RegisterCheckpoint;
class RegisterContext;
class RegisterTypeBuilder;
class RegisterValue;
class RegularExpression;
class RichManglingContext;
class SaveCoreOptions;
class Scalar;
class ScriptInterpreter;
class ScriptInterpreterLocker;
class ScriptedMetadata;
class ScriptedPlatformInterface;
class ScriptedProcessInterface;
class ScriptedThreadInterface;
class ScriptedThreadPlanInterface;
class ScriptedSyntheticChildren;
class SearchFilter;
class Section;
class SectionList;
class SectionLoadHistory;
class SectionLoadList;
class Settings;
class SourceManager;
class SourceManagerImpl;
class StackFrame;
class StackFrameList;
class StackFrameRecognizer;
class StackFrameRecognizerManager;
class StackID;
class Status;
class StopInfo;
class Stoppoint;
class StoppointCallbackContext;
class Stream;
class StreamFile;
class StreamString;
class StringList;
class StringTableReader;
class StructuredDataImpl;
class StructuredDataPlugin;
class SupportFile;
class Symbol;
class SymbolContext;
class SymbolContextList;
class SymbolContextScope;
class SymbolContextSpecifier;
class SymbolFile;
class SymbolFileType;
class SymbolLocator;
class SymbolVendor;
class Symtab;
class SyntheticChildren;
class SyntheticChildrenFrontEnd;
class SystemRuntime;
class Target;
class TargetList;
class TargetProperties;
class Thread;
class ThreadCollection;
class ThreadList;
class ThreadPlan;
class ThreadPlanBase;
class ThreadPlanRunToAddress;
class ThreadPlanStepInstruction;
class ThreadPlanStepOut;
class ThreadPlanStepOverBreakpoint;
class ThreadPlanStepRange;
class ThreadPlanStepThrough;
class ThreadPlanTracer;
class ThreadSpec;
class ThreadPostMortemTrace;
class ThreadedCommunication;
class Trace;
class TraceCursor;
class TraceExporter;
class Type;
class TypeAndOrName;
class TypeCategoryImpl;
class TypeCategoryMap;
class TypeEnumMemberImpl;
class TypeEnumMemberListImpl;
class TypeFilterImpl;
class TypeFormatImpl;
class TypeImpl;
class TypeList;
class TypeListImpl;
class TypeMap;
class TypeQuery;
class TypeMemberFunctionImpl;
class TypeMemberImpl;
class TypeNameSpecifierImpl;
class TypeResults;
class TypeSummaryImpl;
class TypeSummaryOptions;
class TypeSystem;
class TypeSystemClang;
class UUID;
class UnixSignals;
class Unwind;
class UnwindAssembly;
class UnwindPlan;
class UnwindTable;
class UserExpression;
class UtilityFunction;
class VMRange;
class Value;
class ValueList;
class ValueObject;
class ValueObjectChild;
class ValueObjectConstResult;
class ValueObjectConstResultChild;
class ValueObjectConstResultImpl;
class ValueObjectList;
class ValueObjectPrinter;
class Variable;
class VariableList;
class Watchpoint;
class WatchpointList;
class WatchpointOptions;
class WatchpointResource;
class WatchpointResourceCollection;
class WatchpointSetOptions;
struct CompilerContext;
struct LineEntry;
struct PropertyDefinition;
struct ScriptSummaryFormat;
struct StatisticsOptions;
struct StringSummaryFormat;
template <unsigned N> class StreamBuffer;

} // namespace lldb_private

// lldb forward declarations
namespace lldb {

typedef std::shared_ptr<lldb_private::ABI> ABISP;
typedef std::unique_ptr<lldb_private::AddressRange> AddressRangeUP;
typedef std::shared_ptr<lldb_private::Baton> BatonSP;
typedef std::shared_ptr<lldb_private::Block> BlockSP;
typedef std::shared_ptr<lldb_private::Breakpoint> BreakpointSP;
typedef std::weak_ptr<lldb_private::Breakpoint> BreakpointWP;
typedef std::shared_ptr<lldb_private::BreakpointSite> BreakpointSiteSP;
typedef std::shared_ptr<lldb_private::BreakpointLocation> BreakpointLocationSP;
typedef std::weak_ptr<lldb_private::BreakpointLocation> BreakpointLocationWP;
typedef std::shared_ptr<lldb_private::BreakpointPrecondition>
    BreakpointPreconditionSP;
typedef std::shared_ptr<lldb_private::BreakpointResolver> BreakpointResolverSP;
typedef std::shared_ptr<lldb_private::Broadcaster> BroadcasterSP;
typedef std::shared_ptr<lldb_private::BroadcasterManager> BroadcasterManagerSP;
typedef std::weak_ptr<lldb_private::BroadcasterManager> BroadcasterManagerWP;
typedef std::shared_ptr<lldb_private::UserExpression> UserExpressionSP;
typedef std::shared_ptr<lldb_private::CommandObject> CommandObjectSP;
typedef std::shared_ptr<lldb_private::Connection> ConnectionSP;
typedef std::shared_ptr<lldb_private::CompileUnit> CompUnitSP;
typedef std::shared_ptr<lldb_private::DataBuffer> DataBufferSP;
typedef std::shared_ptr<lldb_private::WritableDataBuffer> WritableDataBufferSP;
typedef std::shared_ptr<lldb_private::DataExtractor> DataExtractorSP;
typedef std::shared_ptr<lldb_private::Debugger> DebuggerSP;
typedef std::weak_ptr<lldb_private::Debugger> DebuggerWP;
typedef std::shared_ptr<lldb_private::Disassembler> DisassemblerSP;
typedef std::unique_ptr<lldb_private::DynamicCheckerFunctions>
    DynamicCheckerFunctionsUP;
typedef std::unique_ptr<lldb_private::DynamicLoader> DynamicLoaderUP;
typedef std::shared_ptr<lldb_private::Event> EventSP;
typedef std::shared_ptr<lldb_private::EventData> EventDataSP;
typedef std::shared_ptr<lldb_private::EventDataStructuredData>
    EventDataStructuredDataSP;
typedef std::shared_ptr<lldb_private::ExecutionContextRef>
    ExecutionContextRefSP;
typedef std::shared_ptr<lldb_private::ExpressionVariable> ExpressionVariableSP;
typedef std::unique_ptr<lldb_private::File> FileUP;
typedef std::shared_ptr<lldb_private::File> FileSP;
typedef std::shared_ptr<lldb_private::FormatEntity::Entry> FormatEntrySP;
typedef std::shared_ptr<lldb_private::Function> FunctionSP;
typedef std::shared_ptr<lldb_private::FuncUnwinders> FuncUnwindersSP;
typedef std::shared_ptr<lldb_private::InlineFunctionInfo> InlineFunctionInfoSP;
typedef std::shared_ptr<lldb_private::Instruction> InstructionSP;
typedef std::shared_ptr<lldb_private::InstrumentationRuntime>
    InstrumentationRuntimeSP;
typedef std::shared_ptr<lldb_private::IOHandler> IOHandlerSP;
typedef std::shared_ptr<lldb_private::IOObject> IOObjectSP;
typedef std::shared_ptr<lldb_private::IRExecutionUnit> IRExecutionUnitSP;
typedef std::shared_ptr<lldb_private::JITLoader> JITLoaderSP;
typedef std::unique_ptr<lldb_private::JITLoaderList> JITLoaderListUP;
typedef std::shared_ptr<lldb_private::LanguageRuntime> LanguageRuntimeSP;
typedef std::unique_ptr<lldb_private::SystemRuntime> SystemRuntimeUP;
typedef std::shared_ptr<lldb_private::Listener> ListenerSP;
typedef std::weak_ptr<lldb_private::Listener> ListenerWP;
typedef std::shared_ptr<lldb_private::MemoryHistory> MemoryHistorySP;
typedef std::unique_ptr<lldb_private::MemoryRegionInfo> MemoryRegionInfoUP;
typedef std::shared_ptr<lldb_private::MemoryRegionInfo> MemoryRegionInfoSP;
typedef std::shared_ptr<lldb_private::Module> ModuleSP;
typedef std::weak_ptr<lldb_private::Module> ModuleWP;
typedef std::shared_ptr<lldb_private::ObjectFile> ObjectFileSP;
typedef std::shared_ptr<lldb_private::ObjectContainer> ObjectContainerSP;
typedef std::shared_ptr<lldb_private::ObjectFileJITDelegate>
    ObjectFileJITDelegateSP;
typedef std::weak_ptr<lldb_private::ObjectFileJITDelegate>
    ObjectFileJITDelegateWP;
typedef std::unique_ptr<lldb_private::OperatingSystem> OperatingSystemUP;
typedef std::shared_ptr<lldb_private::OperatingSystemInterface>
    OperatingSystemInterfaceSP;
typedef std::shared_ptr<lldb_private::OptionValue> OptionValueSP;
typedef std::weak_ptr<lldb_private::OptionValue> OptionValueWP;
typedef std::shared_ptr<lldb_private::OptionValueProperties>
    OptionValuePropertiesSP;
typedef std::shared_ptr<lldb_private::Platform> PlatformSP;
typedef std::shared_ptr<lldb_private::Process> ProcessSP;
typedef std::shared_ptr<lldb_private::ProcessAttachInfo> ProcessAttachInfoSP;
typedef std::shared_ptr<lldb_private::ProcessLaunchInfo> ProcessLaunchInfoSP;
typedef std::weak_ptr<lldb_private::Process> ProcessWP;
typedef std::shared_ptr<lldb_private::RegisterCheckpoint> RegisterCheckpointSP;
typedef std::shared_ptr<lldb_private::RegisterContext> RegisterContextSP;
typedef std::shared_ptr<lldb_private::RegisterTypeBuilder>
    RegisterTypeBuilderSP;
typedef std::shared_ptr<lldb_private::RegularExpression> RegularExpressionSP;
typedef std::shared_ptr<lldb_private::Queue> QueueSP;
typedef std::weak_ptr<lldb_private::Queue> QueueWP;
typedef std::shared_ptr<lldb_private::QueueItem> QueueItemSP;
typedef std::shared_ptr<lldb_private::REPL> REPLSP;
typedef std::shared_ptr<lldb_private::RecognizedStackFrame>
    RecognizedStackFrameSP;
typedef std::shared_ptr<lldb_private::ScriptSummaryFormat>
    ScriptSummaryFormatSP;
typedef std::shared_ptr<lldb_private::ScriptInterpreter> ScriptInterpreterSP;
typedef std::shared_ptr<lldb_private::ScriptedMetadata> ScriptedMetadataSP;
typedef std::unique_ptr<lldb_private::ScriptedPlatformInterface>
    ScriptedPlatformInterfaceUP;
typedef std::unique_ptr<lldb_private::ScriptedProcessInterface>
    ScriptedProcessInterfaceUP;
typedef std::shared_ptr<lldb_private::ScriptedThreadInterface>
    ScriptedThreadInterfaceSP;
typedef std::shared_ptr<lldb_private::ScriptedThreadPlanInterface>
    ScriptedThreadPlanInterfaceSP;
typedef std::shared_ptr<lldb_private::Section> SectionSP;
typedef std::unique_ptr<lldb_private::SectionList> SectionListUP;
typedef std::weak_ptr<lldb_private::Section> SectionWP;
typedef std::shared_ptr<lldb_private::SectionLoadList> SectionLoadListSP;
typedef std::shared_ptr<lldb_private::SearchFilter> SearchFilterSP;
typedef std::unique_ptr<lldb_private::SourceManager> SourceManagerUP;
typedef std::shared_ptr<lldb_private::StackFrame> StackFrameSP;
typedef std::weak_ptr<lldb_private::StackFrame> StackFrameWP;
typedef std::shared_ptr<lldb_private::StackFrameList> StackFrameListSP;
typedef std::shared_ptr<lldb_private::StackFrameRecognizer>
    StackFrameRecognizerSP;
typedef std::unique_ptr<lldb_private::StackFrameRecognizerManager>
    StackFrameRecognizerManagerUP;
typedef std::shared_ptr<lldb_private::StopInfo> StopInfoSP;
typedef std::shared_ptr<lldb_private::Stream> StreamSP;
typedef std::shared_ptr<lldb_private::StreamFile> StreamFileSP;
typedef std::shared_ptr<lldb_private::StringSummaryFormat>
    StringTypeSummaryImplSP;
typedef std::unique_ptr<lldb_private::StructuredDataImpl> StructuredDataImplUP;
typedef std::shared_ptr<lldb_private::StructuredDataPlugin>
    StructuredDataPluginSP;
typedef std::weak_ptr<lldb_private::StructuredDataPlugin>
    StructuredDataPluginWP;
typedef std::shared_ptr<lldb_private::SymbolFileType> SymbolFileTypeSP;
typedef std::shared_ptr<lldb_private::SymbolContextSpecifier>
    SymbolContextSpecifierSP;
typedef std::unique_ptr<lldb_private::SymbolVendor> SymbolVendorUP;
typedef std::shared_ptr<lldb_private::SyntheticChildren> SyntheticChildrenSP;
typedef std::shared_ptr<lldb_private::SyntheticChildrenFrontEnd>
    SyntheticChildrenFrontEndSP;
typedef std::shared_ptr<lldb_private::Target> TargetSP;
typedef std::weak_ptr<lldb_private::Target> TargetWP;
typedef std::shared_ptr<lldb_private::Thread> ThreadSP;
typedef std::weak_ptr<lldb_private::Thread> ThreadWP;
typedef std::shared_ptr<lldb_private::ThreadCollection> ThreadCollectionSP;
typedef std::shared_ptr<lldb_private::ThreadPlan> ThreadPlanSP;
typedef std::shared_ptr<lldb_private::ThreadPostMortemTrace>
    ThreadPostMortemTraceSP;
typedef std::weak_ptr<lldb_private::ThreadPlan> ThreadPlanWP;
typedef std::shared_ptr<lldb_private::ThreadPlanTracer> ThreadPlanTracerSP;
typedef std::shared_ptr<lldb_private::Trace> TraceSP;
typedef std::unique_ptr<lldb_private::TraceExporter> TraceExporterUP;
typedef std::shared_ptr<lldb_private::TraceCursor> TraceCursorSP;
typedef std::shared_ptr<lldb_private::Type> TypeSP;
typedef std::weak_ptr<lldb_private::Type> TypeWP;
typedef std::shared_ptr<lldb_private::TypeCategoryImpl> TypeCategoryImplSP;
typedef std::shared_ptr<lldb_private::TypeImpl> TypeImplSP;
typedef std::shared_ptr<lldb_private::TypeMemberFunctionImpl>
    TypeMemberFunctionImplSP;
typedef std::shared_ptr<lldb_private::TypeEnumMemberImpl> TypeEnumMemberImplSP;
typedef std::shared_ptr<lldb_private::TypeFilterImpl> TypeFilterImplSP;
typedef std::shared_ptr<lldb_private::TypeSystem> TypeSystemSP;
typedef std::shared_ptr<lldb_private::TypeSystemClang> TypeSystemClangSP;
typedef std::weak_ptr<lldb_private::TypeSystem> TypeSystemWP;
typedef std::shared_ptr<lldb_private::TypeFormatImpl> TypeFormatImplSP;
typedef std::shared_ptr<lldb_private::TypeNameSpecifierImpl>
    TypeNameSpecifierImplSP;
typedef std::shared_ptr<lldb_private::TypeSummaryImpl> TypeSummaryImplSP;
typedef std::shared_ptr<lldb_private::TypeSummaryOptions> TypeSummaryOptionsSP;
typedef std::shared_ptr<lldb_private::ScriptedSyntheticChildren>
    ScriptedSyntheticChildrenSP;
typedef std::shared_ptr<lldb_private::SupportFile> SupportFileSP;
typedef std::shared_ptr<lldb_private::UnixSignals> UnixSignalsSP;
typedef std::weak_ptr<lldb_private::UnixSignals> UnixSignalsWP;
typedef std::shared_ptr<lldb_private::UnwindAssembly> UnwindAssemblySP;
typedef std::shared_ptr<lldb_private::UnwindPlan> UnwindPlanSP;
typedef std::shared_ptr<lldb_private::ValueObject> ValueObjectSP;
typedef std::shared_ptr<lldb_private::Value> ValueSP;
typedef std::shared_ptr<lldb_private::Variable> VariableSP;
typedef std::shared_ptr<lldb_private::VariableList> VariableListSP;
typedef std::shared_ptr<lldb_private::ValueObjectList> ValueObjectListSP;
typedef std::shared_ptr<lldb_private::Watchpoint> WatchpointSP;
typedef std::shared_ptr<lldb_private::WatchpointResource> WatchpointResourceSP;

} // namespace lldb

#endif // LLDB_LLDB_FORWARD_H
