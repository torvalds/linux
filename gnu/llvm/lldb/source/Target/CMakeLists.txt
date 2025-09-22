lldb_tablegen(TargetProperties.inc -gen-lldb-property-defs
  SOURCE TargetProperties.td
  TARGET LLDBTargetPropertiesGen)

lldb_tablegen(TargetPropertiesEnum.inc -gen-lldb-property-enum-defs
  SOURCE TargetProperties.td
  TARGET LLDBTargetPropertiesEnumGen)

# TODO: Add property `NO_PLUGIN_DEPENDENCIES` to lldbTarget
add_lldb_library(lldbTarget
  ABI.cpp
  AssertFrameRecognizer.cpp
  DynamicRegisterInfo.cpp
  ExecutionContext.cpp
  InstrumentationRuntime.cpp
  InstrumentationRuntimeStopInfo.cpp
  JITLoader.cpp
  JITLoaderList.cpp
  Language.cpp
  LanguageRuntime.cpp
  Memory.cpp
  MemoryHistory.cpp
  MemoryRegionInfo.cpp
  MemoryTagMap.cpp
  ModuleCache.cpp
  OperatingSystem.cpp
  PathMappingList.cpp
  Platform.cpp
  Process.cpp
  ProcessTrace.cpp
  Queue.cpp
  QueueItem.cpp
  QueueList.cpp
  RegisterContext.cpp
  RegisterContextUnwind.cpp
  RegisterFlags.cpp
  RegisterNumber.cpp
  RemoteAwarePlatform.cpp
  SectionLoadHistory.cpp
  SectionLoadList.cpp
  StackFrame.cpp
  StackFrameList.cpp
  StackFrameRecognizer.cpp
  StackID.cpp
  Statistics.cpp
  StopInfo.cpp
  StructuredDataPlugin.cpp
  SystemRuntime.cpp
  Target.cpp
  TargetList.cpp
  Thread.cpp
  ThreadCollection.cpp
  ThreadList.cpp
  ThreadPlan.cpp
  ThreadPlanBase.cpp
  ThreadPlanCallFunction.cpp
  ThreadPlanCallFunctionUsingABI.cpp
  ThreadPlanCallOnFunctionExit.cpp
  ThreadPlanCallUserExpression.cpp
  ThreadPlanPython.cpp
  ThreadPlanRunToAddress.cpp
  ThreadPlanShouldStopHere.cpp
  ThreadPlanStepInRange.cpp
  ThreadPlanStepInstruction.cpp
  ThreadPlanStepOut.cpp
  ThreadPlanStepOverBreakpoint.cpp
  ThreadPlanStepOverRange.cpp
  ThreadPlanStepRange.cpp
  ThreadPlanStepThrough.cpp
  ThreadPlanStepUntil.cpp
  ThreadPlanTracer.cpp
  ThreadPlanStack.cpp
  ThreadSpec.cpp
  Trace.cpp
  TraceCursor.cpp
  TraceExporter.cpp
  TraceDumper.cpp
  UnixSignals.cpp
  UnwindAssembly.cpp
  UnwindLLDB.cpp
  VerboseTrapFrameRecognizer.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbUtility
    lldbPluginProcessUtility

  LINK_COMPONENTS
    Support
    MC
  )

add_dependencies(lldbTarget
  LLDBTargetPropertiesGen
  LLDBTargetPropertiesEnumGen)
