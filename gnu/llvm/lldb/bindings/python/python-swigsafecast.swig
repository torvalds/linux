namespace lldb_private {
namespace python {

PythonObject ToSWIGHelper(void *obj, swig_type_info *info) {
  return {PyRefType::Owned, SWIG_NewPointerObj(obj, info, SWIG_POINTER_OWN)};
}

PythonObject SWIGBridge::ToSWIGWrapper(std::unique_ptr<lldb::SBValue> value_sb) {
  return ToSWIGHelper(value_sb.release(), SWIGTYPE_p_lldb__SBValue);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ValueObjectSP value_sp) {
  return ToSWIGWrapper(std::unique_ptr<lldb::SBValue>(new lldb::SBValue(value_sp)));
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::TargetSP target_sp) {
  return ToSWIGHelper(new lldb::SBTarget(std::move(target_sp)),
                      SWIGTYPE_p_lldb__SBTarget);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ProcessSP process_sp) {
  return ToSWIGHelper(new lldb::SBProcess(std::move(process_sp)),
                      SWIGTYPE_p_lldb__SBProcess);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ThreadPlanSP thread_plan_sp) {
  return ToSWIGHelper(new lldb::SBThreadPlan(std::move(thread_plan_sp)),
                      SWIGTYPE_p_lldb__SBThreadPlan);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::BreakpointSP breakpoint_sp) {
  return ToSWIGHelper(new lldb::SBBreakpoint(std::move(breakpoint_sp)),
                      SWIGTYPE_p_lldb__SBBreakpoint);
}

PythonObject SWIGBridge::ToSWIGWrapper(const Status& status) {
  return ToSWIGHelper(new lldb::SBError(status), SWIGTYPE_p_lldb__SBError);
}

PythonObject SWIGBridge::ToSWIGWrapper(std::unique_ptr<lldb::SBStructuredData> data_sb) {
  return ToSWIGHelper(data_sb.release(), SWIGTYPE_p_lldb__SBStructuredData);
}

PythonObject SWIGBridge::ToSWIGWrapper(const StructuredDataImpl &data_impl) {
  return ToSWIGWrapper(std::unique_ptr<lldb::SBStructuredData>(new lldb::SBStructuredData(data_impl)));
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ThreadSP thread_sp) {
  return ToSWIGHelper(new lldb::SBThread(std::move(thread_sp)),
                      SWIGTYPE_p_lldb__SBThread);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::StackFrameSP frame_sp) {
  return ToSWIGHelper(new lldb::SBFrame(std::move(frame_sp)),
                      SWIGTYPE_p_lldb__SBFrame);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::DebuggerSP debugger_sp) {
  return ToSWIGHelper(new lldb::SBDebugger(std::move(debugger_sp)),
                      SWIGTYPE_p_lldb__SBDebugger);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::WatchpointSP watchpoint_sp) {
  return ToSWIGHelper(new lldb::SBWatchpoint(std::move(watchpoint_sp)),
                      SWIGTYPE_p_lldb__SBWatchpoint);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::BreakpointLocationSP bp_loc_sp) {
  return ToSWIGHelper(new lldb::SBBreakpointLocation(std::move(bp_loc_sp)),
                      SWIGTYPE_p_lldb__SBBreakpointLocation);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ExecutionContextRefSP ctx_sp) {
  return ToSWIGHelper(new lldb::SBExecutionContext(std::move(ctx_sp)),
                      SWIGTYPE_p_lldb__SBExecutionContext);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::TypeImplSP type_impl_sp) {
  return ToSWIGHelper(new lldb::SBType(type_impl_sp), SWIGTYPE_p_lldb__SBType);
}

PythonObject SWIGBridge::ToSWIGWrapper(const TypeSummaryOptions &summary_options) {
  return ToSWIGHelper(new lldb::SBTypeSummaryOptions(summary_options),
                      SWIGTYPE_p_lldb__SBTypeSummaryOptions);
}

PythonObject SWIGBridge::ToSWIGWrapper(const SymbolContext &sym_ctx) {
  return ToSWIGHelper(new lldb::SBSymbolContext(sym_ctx),
                      SWIGTYPE_p_lldb__SBSymbolContext);
}

PythonObject SWIGBridge::ToSWIGWrapper(lldb::ProcessLaunchInfoSP launch_info_sp) {
   return ToSWIGHelper(new lldb::ProcessLaunchInfoSP(std::move(launch_info_sp)),
                       SWIGTYPE_p_lldb__SBLaunchInfo);
 }

 PythonObject SWIGBridge::ToSWIGWrapper(lldb::ProcessAttachInfoSP attach_info_sp) {
   return ToSWIGHelper(new lldb::ProcessAttachInfoSP(std::move(attach_info_sp)),
                       SWIGTYPE_p_lldb__SBAttachInfo);
 }

PythonObject SWIGBridge::ToSWIGWrapper(lldb::DataExtractorSP data_sp) {
  return ToSWIGHelper(new lldb::DataExtractorSP(std::move(data_sp)),
                      SWIGTYPE_p_lldb__SBData);
}

ScopedPythonObject<lldb::SBCommandReturnObject>
SWIGBridge::ToSWIGWrapper(CommandReturnObject &cmd_retobj) {
  return ScopedPythonObject<lldb::SBCommandReturnObject>(
      new lldb::SBCommandReturnObject(cmd_retobj),
      SWIGTYPE_p_lldb__SBCommandReturnObject);
}

PythonObject SWIGBridge::ToSWIGWrapper(const Stream *s) {
  return ToSWIGHelper(new lldb::SBStream(), SWIGTYPE_p_lldb__SBStream);
}

PythonObject SWIGBridge::ToSWIGWrapper(std::shared_ptr<lldb::SBStream> stream_sb) {
  return ToSWIGHelper(stream_sb.get(), SWIGTYPE_p_lldb__SBStream);
}

PythonObject SWIGBridge::ToSWIGWrapper(Event *event) {
  return ToSWIGHelper(new lldb::SBEvent(event), SWIGTYPE_p_lldb__SBEvent);
}

PythonObject SWIGBridge::ToSWIGWrapper(
    std::unique_ptr<lldb::SBFileSpec> file_spec_sb) {
  return ToSWIGHelper(file_spec_sb.release(), SWIGTYPE_p_lldb__SBFileSpec);
}

PythonObject SWIGBridge::ToSWIGWrapper(
    std::unique_ptr<lldb::SBModuleSpec> module_spec_sb) {
  return ToSWIGHelper(module_spec_sb.release(), SWIGTYPE_p_lldb__SBModuleSpec);
}

} // namespace python
} // namespace lldb_private
