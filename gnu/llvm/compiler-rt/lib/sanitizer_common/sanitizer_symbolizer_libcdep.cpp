//===-- sanitizer_symbolizer_libcdep.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_allocator_internal.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"
#include "sanitizer_symbolizer_internal.h"

namespace __sanitizer {

Symbolizer *Symbolizer::GetOrInit() {
  SpinMutexLock l(&init_mu_);
  if (symbolizer_)
    return symbolizer_;
  symbolizer_ = PlatformInit();
  CHECK(symbolizer_);
  return symbolizer_;
}

// See sanitizer_symbolizer_markup.cpp.
#if !SANITIZER_SYMBOLIZER_MARKUP

const char *ExtractToken(const char *str, const char *delims, char **result) {
  uptr prefix_len = internal_strcspn(str, delims);
  *result = (char*)InternalAlloc(prefix_len + 1);
  internal_memcpy(*result, str, prefix_len);
  (*result)[prefix_len] = '\0';
  const char *prefix_end = str + prefix_len;
  if (*prefix_end != '\0') prefix_end++;
  return prefix_end;
}

const char *ExtractInt(const char *str, const char *delims, int *result) {
  char *buff = nullptr;
  const char *ret = ExtractToken(str, delims, &buff);
  if (buff) {
    *result = (int)internal_atoll(buff);
  }
  InternalFree(buff);
  return ret;
}

const char *ExtractUptr(const char *str, const char *delims, uptr *result) {
  char *buff = nullptr;
  const char *ret = ExtractToken(str, delims, &buff);
  if (buff) {
    *result = (uptr)internal_atoll(buff);
  }
  InternalFree(buff);
  return ret;
}

const char *ExtractSptr(const char *str, const char *delims, sptr *result) {
  char *buff = nullptr;
  const char *ret = ExtractToken(str, delims, &buff);
  if (buff) {
    *result = (sptr)internal_atoll(buff);
  }
  InternalFree(buff);
  return ret;
}

const char *ExtractTokenUpToDelimiter(const char *str, const char *delimiter,
                                      char **result) {
  const char *found_delimiter = internal_strstr(str, delimiter);
  uptr prefix_len =
      found_delimiter ? found_delimiter - str : internal_strlen(str);
  *result = (char *)InternalAlloc(prefix_len + 1);
  internal_memcpy(*result, str, prefix_len);
  (*result)[prefix_len] = '\0';
  const char *prefix_end = str + prefix_len;
  if (*prefix_end != '\0') prefix_end += internal_strlen(delimiter);
  return prefix_end;
}

SymbolizedStack *Symbolizer::SymbolizePC(uptr addr) {
  Lock l(&mu_);
  SymbolizedStack *res = SymbolizedStack::New(addr);
  auto *mod = FindModuleForAddress(addr);
  if (!mod)
    return res;
  // Always fill data about module name and offset.
  res->info.FillModuleInfo(*mod);
  for (auto &tool : tools_) {
    SymbolizerScope sym_scope(this);
    if (tool.SymbolizePC(addr, res)) {
      return res;
    }
  }
  return res;
}

bool Symbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  Lock l(&mu_);
  const char *module_name = nullptr;
  uptr module_offset;
  ModuleArch arch;
  if (!FindModuleNameAndOffsetForAddress(addr, &module_name, &module_offset,
                                         &arch))
    return false;
  info->Clear();
  info->module = internal_strdup(module_name);
  info->module_offset = module_offset;
  info->module_arch = arch;
  for (auto &tool : tools_) {
    SymbolizerScope sym_scope(this);
    if (tool.SymbolizeData(addr, info)) {
      return true;
    }
  }
  return false;
}

bool Symbolizer::SymbolizeFrame(uptr addr, FrameInfo *info) {
  Lock l(&mu_);
  const char *module_name = nullptr;
  if (!FindModuleNameAndOffsetForAddress(
          addr, &module_name, &info->module_offset, &info->module_arch))
    return false;
  info->module = internal_strdup(module_name);
  for (auto &tool : tools_) {
    SymbolizerScope sym_scope(this);
    if (tool.SymbolizeFrame(addr, info)) {
      return true;
    }
  }
  return false;
}

bool Symbolizer::GetModuleNameAndOffsetForPC(uptr pc, const char **module_name,
                                             uptr *module_address) {
  Lock l(&mu_);
  const char *internal_module_name = nullptr;
  ModuleArch arch;
  if (!FindModuleNameAndOffsetForAddress(pc, &internal_module_name,
                                         module_address, &arch))
    return false;

  if (module_name)
    *module_name = module_names_.GetOwnedCopy(internal_module_name);
  return true;
}

void Symbolizer::Flush() {
  Lock l(&mu_);
  for (auto &tool : tools_) {
    SymbolizerScope sym_scope(this);
    tool.Flush();
  }
}

const char *Symbolizer::Demangle(const char *name) {
  CHECK(name);
  Lock l(&mu_);
  for (auto &tool : tools_) {
    SymbolizerScope sym_scope(this);
    if (const char *demangled = tool.Demangle(name))
      return demangled;
  }
  if (const char *demangled = PlatformDemangle(name))
    return demangled;
  return name;
}

bool Symbolizer::FindModuleNameAndOffsetForAddress(uptr address,
                                                   const char **module_name,
                                                   uptr *module_offset,
                                                   ModuleArch *module_arch) {
  const LoadedModule *module = FindModuleForAddress(address);
  if (!module)
    return false;
  *module_name = module->full_name();
  *module_offset = address - module->base_address();
  *module_arch = module->arch();
  return true;
}

void Symbolizer::RefreshModules() {
  modules_.init();
  fallback_modules_.fallbackInit();
  RAW_CHECK(modules_.size() > 0);
  modules_fresh_ = true;
}

const ListOfModules &Symbolizer::GetRefreshedListOfModules() {
  if (!modules_fresh_)
    RefreshModules();

  return modules_;
}

static const LoadedModule *SearchForModule(const ListOfModules &modules,
                                           uptr address) {
  for (uptr i = 0; i < modules.size(); i++) {
    if (modules[i].containsAddress(address)) {
      return &modules[i];
    }
  }
  return nullptr;
}

const LoadedModule *Symbolizer::FindModuleForAddress(uptr address) {
  bool modules_were_reloaded = false;
  if (!modules_fresh_) {
    RefreshModules();
    modules_were_reloaded = true;
  }
  const LoadedModule *module = SearchForModule(modules_, address);
  if (module) return module;

  // dlopen/dlclose interceptors invalidate the module list, but when
  // interception is disabled, we need to retry if the lookup fails in
  // case the module list changed.
#if !SANITIZER_INTERCEPT_DLOPEN_DLCLOSE
  if (!modules_were_reloaded) {
    RefreshModules();
    module = SearchForModule(modules_, address);
    if (module) return module;
  }
#endif

  if (fallback_modules_.size()) {
    module = SearchForModule(fallback_modules_, address);
  }
  return module;
}

// For now we assume the following protocol:
// For each request of the form
//   <module_name> <module_offset>
// passed to STDIN, external symbolizer prints to STDOUT response:
//   <function_name>
//   <file_name>:<line_number>:<column_number>
//   <function_name>
//   <file_name>:<line_number>:<column_number>
//   ...
//   <empty line>
class LLVMSymbolizerProcess final : public SymbolizerProcess {
 public:
  explicit LLVMSymbolizerProcess(const char *path)
      : SymbolizerProcess(path, /*use_posix_spawn=*/SANITIZER_APPLE) {}

 private:
  bool ReachedEndOfOutput(const char *buffer, uptr length) const override {
    // Empty line marks the end of llvm-symbolizer output.
    return length >= 2 && buffer[length - 1] == '\n' &&
           buffer[length - 2] == '\n';
  }

  // When adding a new architecture, don't forget to also update
  // script/asan_symbolize.py and sanitizer_common.h.
  void GetArgV(const char *path_to_binary,
               const char *(&argv)[kArgVMax]) const override {
#if defined(__x86_64h__)
    const char* const kSymbolizerArch = "--default-arch=x86_64h";
#elif defined(__x86_64__)
    const char* const kSymbolizerArch = "--default-arch=x86_64";
#elif defined(__i386__)
    const char* const kSymbolizerArch = "--default-arch=i386";
#elif SANITIZER_LOONGARCH64
    const char *const kSymbolizerArch = "--default-arch=loongarch64";
#elif SANITIZER_RISCV64
    const char *const kSymbolizerArch = "--default-arch=riscv64";
#elif defined(__aarch64__)
    const char* const kSymbolizerArch = "--default-arch=arm64";
#elif defined(__arm__)
    const char* const kSymbolizerArch = "--default-arch=arm";
#elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const char* const kSymbolizerArch = "--default-arch=powerpc64";
#elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const char* const kSymbolizerArch = "--default-arch=powerpc64le";
#elif defined(__s390x__)
    const char* const kSymbolizerArch = "--default-arch=s390x";
#elif defined(__s390__)
    const char* const kSymbolizerArch = "--default-arch=s390";
#else
    const char* const kSymbolizerArch = "--default-arch=unknown";
#endif

    const char *const demangle_flag =
        common_flags()->demangle ? "--demangle" : "--no-demangle";
    const char *const inline_flag =
        common_flags()->symbolize_inline_frames ? "--inlines" : "--no-inlines";
    int i = 0;
    argv[i++] = path_to_binary;
    argv[i++] = demangle_flag;
    argv[i++] = inline_flag;
    argv[i++] = kSymbolizerArch;
    argv[i++] = nullptr;
    CHECK_LE(i, kArgVMax);
  }
};

LLVMSymbolizer::LLVMSymbolizer(const char *path, LowLevelAllocator *allocator)
    : symbolizer_process_(new(*allocator) LLVMSymbolizerProcess(path)) {}

// Parse a <file>:<line>[:<column>] buffer. The file path may contain colons on
// Windows, so extract tokens from the right hand side first. The column info is
// also optional.
static const char *ParseFileLineInfo(AddressInfo *info, const char *str) {
  char *file_line_info = nullptr;
  str = ExtractToken(str, "\n", &file_line_info);
  CHECK(file_line_info);

  if (uptr size = internal_strlen(file_line_info)) {
    char *back = file_line_info + size - 1;
    for (int i = 0; i < 2; ++i) {
      while (back > file_line_info && IsDigit(*back)) --back;
      if (*back != ':' || !IsDigit(back[1])) break;
      info->column = info->line;
      info->line = internal_atoll(back + 1);
      // Truncate the string at the colon to keep only filename.
      *back = '\0';
      --back;
    }
    ExtractToken(file_line_info, "", &info->file);
  }

  InternalFree(file_line_info);
  return str;
}

// Parses one or more two-line strings in the following format:
//   <function_name>
//   <file_name>:<line_number>[:<column_number>]
// Used by LLVMSymbolizer, Addr2LinePool and InternalSymbolizer, since all of
// them use the same output format.
void ParseSymbolizePCOutput(const char *str, SymbolizedStack *res) {
  bool top_frame = true;
  SymbolizedStack *last = res;
  while (true) {
    char *function_name = nullptr;
    str = ExtractToken(str, "\n", &function_name);
    CHECK(function_name);
    if (function_name[0] == '\0') {
      // There are no more frames.
      InternalFree(function_name);
      break;
    }
    SymbolizedStack *cur;
    if (top_frame) {
      cur = res;
      top_frame = false;
    } else {
      cur = SymbolizedStack::New(res->info.address);
      cur->info.FillModuleInfo(res->info.module, res->info.module_offset,
                               res->info.module_arch);
      last->next = cur;
      last = cur;
    }

    AddressInfo *info = &cur->info;
    info->function = function_name;
    str = ParseFileLineInfo(info, str);

    // Functions and filenames can be "??", in which case we write 0
    // to address info to mark that names are unknown.
    if (0 == internal_strcmp(info->function, "??")) {
      InternalFree(info->function);
      info->function = 0;
    }
    if (info->file && 0 == internal_strcmp(info->file, "??")) {
      InternalFree(info->file);
      info->file = 0;
    }
  }
}

// Parses a two- or three-line string in the following format:
//   <symbol_name>
//   <start_address> <size>
//   <filename>:<column>
// Used by LLVMSymbolizer and InternalSymbolizer. LLVMSymbolizer added support
// for symbolizing the third line in D123538, but we support the older two-line
// information as well.
void ParseSymbolizeDataOutput(const char *str, DataInfo *info) {
  str = ExtractToken(str, "\n", &info->name);
  str = ExtractUptr(str, " ", &info->start);
  str = ExtractUptr(str, "\n", &info->size);
  // Note: If the third line isn't present, these calls will set info.{file,
  // line} to empty strings.
  str = ExtractToken(str, ":", &info->file);
  str = ExtractUptr(str, "\n", &info->line);
}

void ParseSymbolizeFrameOutput(const char *str,
                               InternalMmapVector<LocalInfo> *locals) {
  if (internal_strncmp(str, "??", 2) == 0)
    return;

  while (*str) {
    LocalInfo local;
    str = ExtractToken(str, "\n", &local.function_name);
    str = ExtractToken(str, "\n", &local.name);

    AddressInfo addr;
    str = ParseFileLineInfo(&addr, str);
    local.decl_file = addr.file;
    local.decl_line = addr.line;

    local.has_frame_offset = internal_strncmp(str, "??", 2) != 0;
    str = ExtractSptr(str, " ", &local.frame_offset);

    local.has_size = internal_strncmp(str, "??", 2) != 0;
    str = ExtractUptr(str, " ", &local.size);

    local.has_tag_offset = internal_strncmp(str, "??", 2) != 0;
    str = ExtractUptr(str, "\n", &local.tag_offset);

    locals->push_back(local);
  }
}

bool LLVMSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  AddressInfo *info = &stack->info;
  const char *buf = FormatAndSendCommand(
      "CODE", info->module, info->module_offset, info->module_arch);
  if (!buf)
    return false;
  ParseSymbolizePCOutput(buf, stack);
  return true;
}

bool LLVMSymbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  const char *buf = FormatAndSendCommand(
      "DATA", info->module, info->module_offset, info->module_arch);
  if (!buf)
    return false;
  ParseSymbolizeDataOutput(buf, info);
  info->start += (addr - info->module_offset); // Add the base address.
  return true;
}

bool LLVMSymbolizer::SymbolizeFrame(uptr addr, FrameInfo *info) {
  const char *buf = FormatAndSendCommand(
      "FRAME", info->module, info->module_offset, info->module_arch);
  if (!buf)
    return false;
  ParseSymbolizeFrameOutput(buf, &info->locals);
  return true;
}

const char *LLVMSymbolizer::FormatAndSendCommand(const char *command_prefix,
                                                 const char *module_name,
                                                 uptr module_offset,
                                                 ModuleArch arch) {
  CHECK(module_name);
  int size_needed = 0;
  if (arch == kModuleArchUnknown)
    size_needed = internal_snprintf(buffer_, kBufferSize, "%s \"%s\" 0x%zx\n",
                                    command_prefix, module_name, module_offset);
  else
    size_needed = internal_snprintf(buffer_, kBufferSize,
                                    "%s \"%s:%s\" 0x%zx\n", command_prefix,
                                    module_name, ModuleArchToString(arch),
                                    module_offset);

  if (size_needed >= static_cast<int>(kBufferSize)) {
    Report("WARNING: Command buffer too small");
    return nullptr;
  }

  return symbolizer_process_->SendCommand(buffer_);
}

SymbolizerProcess::SymbolizerProcess(const char *path, bool use_posix_spawn)
    : path_(path),
      input_fd_(kInvalidFd),
      output_fd_(kInvalidFd),
      times_restarted_(0),
      failed_to_start_(false),
      reported_invalid_path_(false),
      use_posix_spawn_(use_posix_spawn) {
  CHECK(path_);
  CHECK_NE(path_[0], '\0');
}

static bool IsSameModule(const char* path) {
  if (const char* ProcessName = GetProcessName()) {
    if (const char* SymbolizerName = StripModuleName(path)) {
      return !internal_strcmp(ProcessName, SymbolizerName);
    }
  }
  return false;
}

const char *SymbolizerProcess::SendCommand(const char *command) {
  if (failed_to_start_)
    return nullptr;
  if (IsSameModule(path_)) {
    Report("WARNING: Symbolizer was blocked from starting itself!\n");
    failed_to_start_ = true;
    return nullptr;
  }
  for (; times_restarted_ < kMaxTimesRestarted; times_restarted_++) {
    // Start or restart symbolizer if we failed to send command to it.
    if (const char *res = SendCommandImpl(command))
      return res;
    Restart();
  }
  if (!failed_to_start_) {
    Report("WARNING: Failed to use and restart external symbolizer!\n");
    failed_to_start_ = true;
  }
  return nullptr;
}

const char *SymbolizerProcess::SendCommandImpl(const char *command) {
  if (input_fd_ == kInvalidFd || output_fd_ == kInvalidFd)
      return nullptr;
  if (!WriteToSymbolizer(command, internal_strlen(command)))
      return nullptr;
  if (!ReadFromSymbolizer())
    return nullptr;
  return buffer_.data();
}

bool SymbolizerProcess::Restart() {
  if (input_fd_ != kInvalidFd)
    CloseFile(input_fd_);
  if (output_fd_ != kInvalidFd)
    CloseFile(output_fd_);
  return StartSymbolizerSubprocess();
}

bool SymbolizerProcess::ReadFromSymbolizer() {
  buffer_.clear();
  constexpr uptr max_length = 1024;
  bool ret = true;
  do {
    uptr just_read = 0;
    uptr size_before = buffer_.size();
    buffer_.resize(size_before + max_length);
    buffer_.resize(buffer_.capacity());
    bool ret = ReadFromFile(input_fd_, &buffer_[size_before],
                            buffer_.size() - size_before, &just_read);

    if (!ret)
      just_read = 0;

    buffer_.resize(size_before + just_read);

    // We can't read 0 bytes, as we don't expect external symbolizer to close
    // its stdout.
    if (just_read == 0) {
      Report("WARNING: Can't read from symbolizer at fd %d\n", input_fd_);
      ret = false;
      break;
    }
  } while (!ReachedEndOfOutput(buffer_.data(), buffer_.size()));
  buffer_.push_back('\0');
  return ret;
}

bool SymbolizerProcess::WriteToSymbolizer(const char *buffer, uptr length) {
  if (length == 0)
    return true;
  uptr write_len = 0;
  bool success = WriteToFile(output_fd_, buffer, length, &write_len);
  if (!success || write_len != length) {
    Report("WARNING: Can't write to symbolizer at fd %d\n", output_fd_);
    return false;
  }
  return true;
}

#endif  // !SANITIZER_SYMBOLIZER_MARKUP

}  // namespace __sanitizer
