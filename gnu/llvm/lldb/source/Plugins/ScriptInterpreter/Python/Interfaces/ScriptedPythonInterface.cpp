//===-- ScriptedPythonInterface.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "lldb/Utility/Log.h"
#include "lldb/lldb-enumerations.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "../lldb-python.h"

#include "../ScriptInterpreterPythonImpl.h"
#include "ScriptedPythonInterface.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

ScriptedPythonInterface::ScriptedPythonInterface(
    ScriptInterpreterPythonImpl &interpreter)
    : ScriptedInterface(), m_interpreter(interpreter) {}

template <>
StructuredData::ArraySP
ScriptedPythonInterface::ExtractValueFromPythonObject<StructuredData::ArraySP>(
    python::PythonObject &p, Status &error) {
  python::PythonList result_list(python::PyRefType::Borrowed, p.get());
  return result_list.CreateStructuredArray();
}

template <>
StructuredData::DictionarySP
ScriptedPythonInterface::ExtractValueFromPythonObject<
    StructuredData::DictionarySP>(python::PythonObject &p, Status &error) {
  python::PythonDictionary result_dict(python::PyRefType::Borrowed, p.get());
  return result_dict.CreateStructuredDictionary();
}

template <>
Status ScriptedPythonInterface::ExtractValueFromPythonObject<Status>(
    python::PythonObject &p, Status &error) {
  if (lldb::SBError *sb_error = reinterpret_cast<lldb::SBError *>(
          python::LLDBSWIGPython_CastPyObjectToSBError(p.get())))
    return m_interpreter.GetStatusFromSBError(*sb_error);
  error.SetErrorString("Couldn't cast lldb::SBError to lldb::Status.");

  return {};
}

template <>
Event *ScriptedPythonInterface::ExtractValueFromPythonObject<Event *>(
    python::PythonObject &p, Status &error) {
  if (lldb::SBEvent *sb_event = reinterpret_cast<lldb::SBEvent *>(
          python::LLDBSWIGPython_CastPyObjectToSBEvent(p.get())))
    return m_interpreter.GetOpaqueTypeFromSBEvent(*sb_event);
  error.SetErrorString("Couldn't cast lldb::SBEvent to lldb_private::Event.");

  return nullptr;
}

template <>
lldb::StreamSP
ScriptedPythonInterface::ExtractValueFromPythonObject<lldb::StreamSP>(
    python::PythonObject &p, Status &error) {
  if (lldb::SBStream *sb_stream = reinterpret_cast<lldb::SBStream *>(
          python::LLDBSWIGPython_CastPyObjectToSBStream(p.get())))
    return m_interpreter.GetOpaqueTypeFromSBStream(*sb_stream);
  error.SetErrorString("Couldn't cast lldb::SBStream to lldb_private::Stream.");

  return nullptr;
}

template <>
lldb::DataExtractorSP
ScriptedPythonInterface::ExtractValueFromPythonObject<lldb::DataExtractorSP>(
    python::PythonObject &p, Status &error) {
  lldb::SBData *sb_data = reinterpret_cast<lldb::SBData *>(
      python::LLDBSWIGPython_CastPyObjectToSBData(p.get()));

  if (!sb_data) {
    error.SetErrorString(
        "Couldn't cast lldb::SBData to lldb::DataExtractorSP.");
    return nullptr;
  }

  return m_interpreter.GetDataExtractorFromSBData(*sb_data);
}

template <>
lldb::BreakpointSP
ScriptedPythonInterface::ExtractValueFromPythonObject<lldb::BreakpointSP>(
    python::PythonObject &p, Status &error) {
  lldb::SBBreakpoint *sb_breakpoint = reinterpret_cast<lldb::SBBreakpoint *>(
      python::LLDBSWIGPython_CastPyObjectToSBBreakpoint(p.get()));

  if (!sb_breakpoint) {
    error.SetErrorString(
        "Couldn't cast lldb::SBBreakpoint to lldb::BreakpointSP.");
    return nullptr;
  }

  return m_interpreter.GetOpaqueTypeFromSBBreakpoint(*sb_breakpoint);
}

template <>
lldb::ProcessAttachInfoSP ScriptedPythonInterface::ExtractValueFromPythonObject<
    lldb::ProcessAttachInfoSP>(python::PythonObject &p, Status &error) {
  lldb::SBAttachInfo *sb_attach_info = reinterpret_cast<lldb::SBAttachInfo *>(
      python::LLDBSWIGPython_CastPyObjectToSBAttachInfo(p.get()));

  if (!sb_attach_info) {
    error.SetErrorString(
        "Couldn't cast lldb::SBAttachInfo to lldb::ProcessAttachInfoSP.");
    return nullptr;
  }

  return m_interpreter.GetOpaqueTypeFromSBAttachInfo(*sb_attach_info);
}

template <>
lldb::ProcessLaunchInfoSP ScriptedPythonInterface::ExtractValueFromPythonObject<
    lldb::ProcessLaunchInfoSP>(python::PythonObject &p, Status &error) {
  lldb::SBLaunchInfo *sb_launch_info = reinterpret_cast<lldb::SBLaunchInfo *>(
      python::LLDBSWIGPython_CastPyObjectToSBLaunchInfo(p.get()));

  if (!sb_launch_info) {
    error.SetErrorString(
        "Couldn't cast lldb::SBLaunchInfo to lldb::ProcessLaunchInfoSP.");
    return nullptr;
  }

  return m_interpreter.GetOpaqueTypeFromSBLaunchInfo(*sb_launch_info);
}

template <>
std::optional<MemoryRegionInfo>
ScriptedPythonInterface::ExtractValueFromPythonObject<
    std::optional<MemoryRegionInfo>>(python::PythonObject &p, Status &error) {

  lldb::SBMemoryRegionInfo *sb_mem_reg_info =
      reinterpret_cast<lldb::SBMemoryRegionInfo *>(
          python::LLDBSWIGPython_CastPyObjectToSBMemoryRegionInfo(p.get()));

  if (!sb_mem_reg_info) {
    error.SetErrorString(
        "Couldn't cast lldb::SBMemoryRegionInfo to lldb::MemoryRegionInfoSP.");
    return {};
  }

  return m_interpreter.GetOpaqueTypeFromSBMemoryRegionInfo(*sb_mem_reg_info);
}

#endif
