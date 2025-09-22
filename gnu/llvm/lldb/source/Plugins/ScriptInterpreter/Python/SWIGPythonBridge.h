//===-- ScriptInterpreterPython.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SWIGPYTHONBRIDGE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SWIGPYTHONBRIDGE_H

#include <optional>
#include <string>

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "lldb-python.h"

#include "Plugins/ScriptInterpreter/Python/PythonDataObjects.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"

namespace lldb {
class SBEvent;
class SBCommandReturnObject;
class SBValue;
class SBStream;
class SBStructuredData;
class SBFileSpec;
class SBModuleSpec;
class SBStringList;
} // namespace lldb

namespace lldb_private {
namespace python {

typedef struct swig_type_info swig_type_info;

python::PythonObject ToSWIGHelper(void *obj, swig_type_info *info);

/// A class that automatically clears an SB object when it goes out of scope.
/// Use for cases where the SB object points to a temporary/unowned entity.
template <typename T> class ScopedPythonObject : PythonObject {
public:
  ScopedPythonObject(T *sb, swig_type_info *info)
      : PythonObject(ToSWIGHelper(sb, info)), m_sb(sb) {}
  ~ScopedPythonObject() {
    if (m_sb)
      *m_sb = T();
  }
  ScopedPythonObject(ScopedPythonObject &&rhs)
      : PythonObject(std::move(rhs)), m_sb(std::exchange(rhs.m_sb, nullptr)) {}
  ScopedPythonObject(const ScopedPythonObject &) = delete;
  ScopedPythonObject &operator=(const ScopedPythonObject &) = delete;
  ScopedPythonObject &operator=(ScopedPythonObject &&) = delete;

  const PythonObject &obj() const { return *this; }

private:
  T *m_sb;
};

// TODO: We may want to support other languages in the future w/ SWIG (we
// already support Lua right now, for example). We could create a generic
// SWIGBridge class and have this one specialize it, something like this:
//
// <typename T>
// class SWIGBridge {
//   static T ToSWIGWrapper(...);
// };
//
// class SWIGPythonBridge : public SWIGBridge<PythonObject> {
//   template<> static PythonObject ToSWIGWrapper(...);
// };
//
// And we should be able to more easily support things like Lua
class SWIGBridge {
public:
  static PythonObject ToSWIGWrapper(std::unique_ptr<lldb::SBValue> value_sb);
  static PythonObject ToSWIGWrapper(lldb::ValueObjectSP value_sp);
  static PythonObject ToSWIGWrapper(lldb::TargetSP target_sp);
  static PythonObject ToSWIGWrapper(lldb::ProcessSP process_sp);
  static PythonObject ToSWIGWrapper(lldb::ThreadPlanSP thread_plan_sp);
  static PythonObject ToSWIGWrapper(lldb::BreakpointSP breakpoint_sp);
  static PythonObject ToSWIGWrapper(const Status &status);
  static PythonObject ToSWIGWrapper(const StructuredDataImpl &data_impl);
  static PythonObject ToSWIGWrapper(lldb::ThreadSP thread_sp);
  static PythonObject ToSWIGWrapper(lldb::StackFrameSP frame_sp);
  static PythonObject ToSWIGWrapper(lldb::DebuggerSP debugger_sp);
  static PythonObject ToSWIGWrapper(lldb::WatchpointSP watchpoint_sp);
  static PythonObject ToSWIGWrapper(lldb::BreakpointLocationSP bp_loc_sp);
  static PythonObject ToSWIGWrapper(lldb::TypeImplSP type_impl_sp);
  static PythonObject ToSWIGWrapper(lldb::ExecutionContextRefSP ctx_sp);
  static PythonObject ToSWIGWrapper(const TypeSummaryOptions &summary_options);
  static PythonObject ToSWIGWrapper(const SymbolContext &sym_ctx);
  static PythonObject ToSWIGWrapper(const Stream *stream);
  static PythonObject ToSWIGWrapper(std::shared_ptr<lldb::SBStream> stream_sb);
  static PythonObject ToSWIGWrapper(Event *event);

  static PythonObject ToSWIGWrapper(lldb::ProcessAttachInfoSP attach_info_sp);
  static PythonObject ToSWIGWrapper(lldb::ProcessLaunchInfoSP launch_info_sp);
  static PythonObject ToSWIGWrapper(lldb::DataExtractorSP data_extractor_sp);

  static PythonObject
  ToSWIGWrapper(std::unique_ptr<lldb::SBStructuredData> data_sb);
  static PythonObject
  ToSWIGWrapper(std::unique_ptr<lldb::SBFileSpec> file_spec_sb);
  static PythonObject
  ToSWIGWrapper(std::unique_ptr<lldb::SBModuleSpec> module_spec_sb);

  static python::ScopedPythonObject<lldb::SBCommandReturnObject>
  ToSWIGWrapper(CommandReturnObject &cmd_retobj);
  // These prototypes are the Pythonic implementations of the required
  // callbacks. Although these are scripting-language specific, their definition
  // depends on the public API.

  static llvm::Expected<bool> LLDBSwigPythonBreakpointCallbackFunction(
      const char *python_function_name, const char *session_dictionary_name,
      const lldb::StackFrameSP &sb_frame,
      const lldb::BreakpointLocationSP &sb_bp_loc,
      const lldb_private::StructuredDataImpl &args_impl);

  static bool LLDBSwigPythonWatchpointCallbackFunction(
      const char *python_function_name, const char *session_dictionary_name,
      const lldb::StackFrameSP &sb_frame, const lldb::WatchpointSP &sb_wp);

  static bool
  LLDBSwigPythonFormatterCallbackFunction(const char *python_function_name,
                                          const char *session_dictionary_name,
                                          lldb::TypeImplSP type_impl_sp);

  static bool LLDBSwigPythonCallTypeScript(
      const char *python_function_name, const void *session_dictionary,
      const lldb::ValueObjectSP &valobj_sp, void **pyfunct_wrapper,
      const lldb::TypeSummaryOptionsSP &options_sp, std::string &retval);

  static python::PythonObject
  LLDBSwigPythonCreateSyntheticProvider(const char *python_class_name,
                                        const char *session_dictionary_name,
                                        const lldb::ValueObjectSP &valobj_sp);

  static python::PythonObject
  LLDBSwigPythonCreateCommandObject(const char *python_class_name,
                                    const char *session_dictionary_name,
                                    lldb::DebuggerSP debugger_sp);

  static python::PythonObject LLDBSwigPythonCreateScriptedBreakpointResolver(
      const char *python_class_name, const char *session_dictionary_name,
      const StructuredDataImpl &args, const lldb::BreakpointSP &bkpt_sp);

  static unsigned int
  LLDBSwigPythonCallBreakpointResolver(void *implementor,
                                       const char *method_name,
                                       lldb_private::SymbolContext *sym_ctx);

  static python::PythonObject LLDBSwigPythonCreateScriptedStopHook(
      lldb::TargetSP target_sp, const char *python_class_name,
      const char *session_dictionary_name, const StructuredDataImpl &args,
      lldb_private::Status &error);

  static bool
  LLDBSwigPythonStopHookCallHandleStop(void *implementor,
                                       lldb::ExecutionContextRefSP exc_ctx,
                                       lldb::StreamSP stream);

  static size_t LLDBSwigPython_CalculateNumChildren(PyObject *implementor,
                                                    uint32_t max);

  static PyObject *LLDBSwigPython_GetChildAtIndex(PyObject *implementor,
                                                  uint32_t idx);

  static int LLDBSwigPython_GetIndexOfChildWithName(PyObject *implementor,
                                                    const char *child_name);

  static lldb::ValueObjectSP
  LLDBSWIGPython_GetValueObjectSPFromSBValue(void *data);

  static bool LLDBSwigPython_UpdateSynthProviderInstance(PyObject *implementor);

  static bool
  LLDBSwigPython_MightHaveChildrenSynthProviderInstance(PyObject *implementor);

  static PyObject *
  LLDBSwigPython_GetValueSynthProviderInstance(PyObject *implementor);

  static bool
  LLDBSwigPythonCallCommand(const char *python_function_name,
                            const char *session_dictionary_name,
                            lldb::DebuggerSP debugger, const char *args,
                            lldb_private::CommandReturnObject &cmd_retobj,
                            lldb::ExecutionContextRefSP exe_ctx_ref_sp);

  static bool
  LLDBSwigPythonCallCommandObject(PyObject *implementor,
                                  lldb::DebuggerSP debugger, const char *args,
                                  lldb_private::CommandReturnObject &cmd_retobj,
                                  lldb::ExecutionContextRefSP exe_ctx_ref_sp);
  static bool
  LLDBSwigPythonCallParsedCommandObject(PyObject *implementor,
                                  lldb::DebuggerSP debugger,  
                                  StructuredDataImpl &args_impl,
                                  lldb_private::CommandReturnObject &cmd_retobj,
                                  lldb::ExecutionContextRefSP exe_ctx_ref_sp);

  static std::optional<std::string>
  LLDBSwigPythonGetRepeatCommandForScriptedCommand(PyObject *implementor,
                                                   std::string &command);

  static bool LLDBSwigPythonCallModuleInit(const char *python_module_name,
                                           const char *session_dictionary_name,
                                           lldb::DebuggerSP debugger);

  static python::PythonObject
  LLDBSWIGPythonCreateOSPlugin(const char *python_class_name,
                               const char *session_dictionary_name,
                               const lldb::ProcessSP &process_sp);

  static python::PythonObject
  LLDBSWIGPython_CreateFrameRecognizer(const char *python_class_name,
                                       const char *session_dictionary_name);

  static PyObject *
  LLDBSwigPython_GetRecognizedArguments(PyObject *implementor,
                                        const lldb::StackFrameSP &frame_sp);

  static bool LLDBSWIGPythonRunScriptKeywordProcess(
      const char *python_function_name, const char *session_dictionary_name,
      const lldb::ProcessSP &process, std::string &output);

  static std::optional<std::string>
  LLDBSWIGPythonRunScriptKeywordThread(const char *python_function_name,
                                       const char *session_dictionary_name,
                                       lldb::ThreadSP thread);

  static bool LLDBSWIGPythonRunScriptKeywordTarget(
      const char *python_function_name, const char *session_dictionary_name,
      const lldb::TargetSP &target, std::string &output);

  static std::optional<std::string>
  LLDBSWIGPythonRunScriptKeywordFrame(const char *python_function_name,
                                      const char *session_dictionary_name,
                                      lldb::StackFrameSP frame);

  static bool LLDBSWIGPythonRunScriptKeywordValue(
      const char *python_function_name, const char *session_dictionary_name,
      const lldb::ValueObjectSP &value, std::string &output);

  static void *
  LLDBSWIGPython_GetDynamicSetting(void *module, const char *setting,
                                   const lldb::TargetSP &target_sp);
};

void *LLDBSWIGPython_CastPyObjectToSBData(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBBreakpoint(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBAttachInfo(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBLaunchInfo(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBError(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBEvent(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBStream(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBValue(PyObject *data);
void *LLDBSWIGPython_CastPyObjectToSBMemoryRegionInfo(PyObject *data);
} // namespace python

} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SWIGPYTHONBRIDGE_H
