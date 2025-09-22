//===-- Diagnostics.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Diagnostics.h"
#include "lldb/Utility/LLDBAssert.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace lldb_private;
using namespace lldb;
using namespace llvm;

static constexpr size_t g_num_log_messages = 100;

void Diagnostics::Initialize() {
  lldbassert(!InstanceImpl() && "Already initialized.");
  InstanceImpl().emplace();
}

void Diagnostics::Terminate() {
  lldbassert(InstanceImpl() && "Already terminated.");
  InstanceImpl().reset();
}

bool Diagnostics::Enabled() { return InstanceImpl().operator bool(); }

std::optional<Diagnostics> &Diagnostics::InstanceImpl() {
  static std::optional<Diagnostics> g_diagnostics;
  return g_diagnostics;
}

Diagnostics &Diagnostics::Instance() { return *InstanceImpl(); }

Diagnostics::Diagnostics() : m_log_handler(g_num_log_messages) {}

Diagnostics::~Diagnostics() {}

Diagnostics::CallbackID Diagnostics::AddCallback(Callback callback) {
  std::lock_guard<std::mutex> guard(m_callbacks_mutex);
  CallbackID id = m_callback_id++;
  m_callbacks.emplace_back(id, callback);
  return id;
}

void Diagnostics::RemoveCallback(CallbackID id) {
  std::lock_guard<std::mutex> guard(m_callbacks_mutex);
  llvm::erase_if(m_callbacks,
                 [id](const CallbackEntry &e) { return e.id == id; });
}

bool Diagnostics::Dump(raw_ostream &stream) {
  Expected<FileSpec> diagnostics_dir = CreateUniqueDirectory();
  if (!diagnostics_dir) {
    stream << "unable to create diagnostic dir: "
           << toString(diagnostics_dir.takeError()) << '\n';
    return false;
  }

  return Dump(stream, *diagnostics_dir);
}

bool Diagnostics::Dump(raw_ostream &stream, const FileSpec &dir) {
  stream << "LLDB diagnostics will be written to " << dir.GetPath() << "\n";
  stream << "Please include the directory content when filing a bug report\n";

  if (Error error = Create(dir)) {
    stream << toString(std::move(error)) << '\n';
    return false;
  }

  return true;
}

llvm::Expected<FileSpec> Diagnostics::CreateUniqueDirectory() {
  SmallString<128> diagnostics_dir;
  std::error_code ec =
      sys::fs::createUniqueDirectory("diagnostics", diagnostics_dir);
  if (ec)
    return errorCodeToError(ec);
  return FileSpec(diagnostics_dir.str());
}

Error Diagnostics::Create(const FileSpec &dir) {
  if (Error err = DumpDiangosticsLog(dir))
    return err;

  for (CallbackEntry e : m_callbacks) {
    if (Error err = e.callback(dir))
      return err;
  }

  return Error::success();
}

llvm::Error Diagnostics::DumpDiangosticsLog(const FileSpec &dir) const {
  FileSpec log_file = dir.CopyByAppendingPathComponent("diagnostics.log");
  std::error_code ec;
  llvm::raw_fd_ostream stream(log_file.GetPath(), ec, llvm::sys::fs::OF_None);
  if (ec)
    return errorCodeToError(ec);
  m_log_handler.Dump(stream);
  return Error::success();
}

void Diagnostics::Report(llvm::StringRef message) {
  m_log_handler.Emit(message);
}
