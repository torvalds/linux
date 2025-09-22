//===-- LLDBUtils.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_LLDBUTILS_H
#define LLDB_TOOLS_LLDB_DAP_LLDBUTILS_H

#include "DAPForward.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace lldb_dap {

/// Run a list of LLDB commands in the LLDB command interpreter.
///
/// All output from every command, including the prompt + the command
/// is placed into the "strm" argument.
///
/// Each individual command can be prefixed with \b ! and/or \b ? in no
/// particular order. If \b ? is provided, then the output of that command is
/// only emitted if it fails, and if \b ! is provided, then the output is
/// emitted regardless, and \b false is returned without executing the
/// remaining commands.
///
/// \param[in] prefix
///     A string that will be printed into \a strm prior to emitting
///     the prompt + command and command output. Can be NULL.
///
/// \param[in] commands
///     An array of LLDB commands to execute.
///
/// \param[in] strm
///     The stream that will receive the prefix, prompt + command and
///     all command output.
///
/// \param[in] parse_command_directives
///     If \b false, then command prefixes like \b ! or \b ? are not parsed and
///     each command is executed verbatim.
///
/// \return
///     \b true, unless a command prefixed with \b ! fails and parsing of
///     command directives is enabled.
bool RunLLDBCommands(llvm::StringRef prefix,
                     const llvm::ArrayRef<std::string> &commands,
                     llvm::raw_ostream &strm, bool parse_command_directives);

/// Run a list of LLDB commands in the LLDB command interpreter.
///
/// All output from every command, including the prompt + the command
/// is returned in the std::string return value.
///
/// \param[in] prefix
///     A string that will be printed into \a strm prior to emitting
///     the prompt + command and command output. Can be NULL.
///
/// \param[in] commands
///     An array of LLDB commands to execute.
///
/// \param[out] required_command_failed
///     If parsing of command directives is enabled, this variable is set to
///     \b true if one of the commands prefixed with \b ! fails.
///
/// \param[in] parse_command_directives
///     If \b false, then command prefixes like \b ! or \b ? are not parsed and
///     each command is executed verbatim.
///
/// \return
///     A std::string that contains the prefix and all commands and
///     command output.
std::string RunLLDBCommands(llvm::StringRef prefix,
                            const llvm::ArrayRef<std::string> &commands,
                            bool &required_command_failed,
                            bool parse_command_directives = true);

/// Similar to the method above, but without parsing command directives.
std::string
RunLLDBCommandsVerbatim(llvm::StringRef prefix,
                        const llvm::ArrayRef<std::string> &commands);

/// Check if a thread has a stop reason.
///
/// \param[in] thread
///     The LLDB thread object to check
///
/// \return
///     \b True if the thread has a valid stop reason, \b false
///     otherwise.
bool ThreadHasStopReason(lldb::SBThread &thread);

/// Given a LLDB frame, make a frame ID that is unique to a specific
/// thread and frame.
///
/// DAP requires a Stackframe "id" to be unique, so we use the frame
/// index in the lower 32 bits and the thread index ID in the upper 32
/// bits.
///
/// \param[in] frame
///     The LLDB stack frame object generate the ID for
///
/// \return
///     A unique integer that allows us to easily find the right
///     stack frame within a thread on subsequent VS code requests.
int64_t MakeDAPFrameID(lldb::SBFrame &frame);

/// Given a DAP frame ID, convert to a LLDB thread index id.
///
/// DAP requires a Stackframe "id" to be unique, so we use the frame
/// index in the lower THREAD_INDEX_SHIFT bits and the thread index ID in
/// the upper 32 - THREAD_INDEX_SHIFT bits.
///
/// \param[in] dap_frame_id
///     The DAP frame ID to convert to a thread index ID.
///
/// \return
///     The LLDB thread index ID.
uint32_t GetLLDBThreadIndexID(uint64_t dap_frame_id);

/// Given a DAP frame ID, convert to a LLDB frame ID.
///
/// DAP requires a Stackframe "id" to be unique, so we use the frame
/// index in the lower THREAD_INDEX_SHIFT bits and the thread index ID in
/// the upper 32 - THREAD_INDEX_SHIFT bits.
///
/// \param[in] dap_frame_id
///     The DAP frame ID to convert to a frame ID.
///
/// \return
///     The LLDB frame index ID.
uint32_t GetLLDBFrameID(uint64_t dap_frame_id);

} // namespace lldb_dap

#endif
