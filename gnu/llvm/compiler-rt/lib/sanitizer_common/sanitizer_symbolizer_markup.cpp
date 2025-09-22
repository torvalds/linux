//===-- sanitizer_symbolizer_markup.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// This generic support for offline symbolizing is based on the
// Fuchsia port.  We don't do any actual symbolization per se.
// Instead, we emit text containing raw addresses and raw linkage
// symbol names, embedded in Fuchsia's symbolization markup format.
// See the spec at:
// https://llvm.org/docs/SymbolizerMarkupFormat.html
//===----------------------------------------------------------------------===//

#include "sanitizer_symbolizer_markup.h"

#include "sanitizer_common.h"
#include "sanitizer_symbolizer.h"
#include "sanitizer_symbolizer_markup_constants.h"

namespace __sanitizer {

void MarkupStackTracePrinter::RenderData(InternalScopedString *buffer,
                                         const char *format, const DataInfo *DI,
                                         const char *strip_path_prefix) {
  RenderContext(buffer);
  buffer->AppendF(kFormatData, reinterpret_cast<void *>(DI->start));
}

bool MarkupStackTracePrinter::RenderNeedsSymbolization(const char *format) {
  return false;
}

// We don't support the stack_trace_format flag at all.
void MarkupStackTracePrinter::RenderFrame(InternalScopedString *buffer,
                                          const char *format, int frame_no,
                                          uptr address, const AddressInfo *info,
                                          bool vs_style,
                                          const char *strip_path_prefix) {
  CHECK(!RenderNeedsSymbolization(format));
  RenderContext(buffer);
  buffer->AppendF(kFormatFrame, frame_no, reinterpret_cast<void *>(address));
}

bool MarkupSymbolizerTool::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  char buffer[kFormatFunctionMax];
  internal_snprintf(buffer, sizeof(buffer), kFormatFunction,
                    reinterpret_cast<void *>(addr));
  stack->info.function = internal_strdup(buffer);
  return true;
}

bool MarkupSymbolizerTool::SymbolizeData(uptr addr, DataInfo *info) {
  info->Clear();
  info->start = addr;
  return true;
}

const char *MarkupSymbolizerTool::Demangle(const char *name) {
  static char buffer[kFormatDemangleMax];
  internal_snprintf(buffer, sizeof(buffer), kFormatDemangle, name);
  return buffer;
}

// Fuchsia's implementation of symbolizer markup doesn't need to emit contextual
// elements at this point.
// Fuchsia's logging infrastructure emits enough information about
// process memory layout that a post-processing filter can do the
// symbolization and pretty-print the markup.
#if !SANITIZER_FUCHSIA

static bool ModulesEq(const LoadedModule &module,
                      const RenderedModule &renderedModule) {
  return module.base_address() == renderedModule.base_address &&
         internal_memcmp(module.uuid(), renderedModule.uuid,
                         module.uuid_size()) == 0 &&
         internal_strcmp(module.full_name(), renderedModule.full_name) == 0;
}

static bool ModuleHasBeenRendered(
    const LoadedModule &module,
    const InternalMmapVectorNoCtor<RenderedModule> &renderedModules) {
  for (const auto &renderedModule : renderedModules)
    if (ModulesEq(module, renderedModule))
      return true;

  return false;
}

static void RenderModule(InternalScopedString *buffer,
                         const LoadedModule &module, uptr moduleId) {
  InternalScopedString buildIdBuffer;
  for (uptr i = 0; i < module.uuid_size(); i++)
    buildIdBuffer.AppendF("%02x", module.uuid()[i]);

  buffer->AppendF(kFormatModule, moduleId, module.full_name(),
                  buildIdBuffer.data());
  buffer->Append("\n");
}

static void RenderMmaps(InternalScopedString *buffer,
                        const LoadedModule &module, uptr moduleId) {
  InternalScopedString accessBuffer;

  // All module mmaps are readable at least
  for (const auto &range : module.ranges()) {
    accessBuffer.Append("r");
    if (range.writable)
      accessBuffer.Append("w");
    if (range.executable)
      accessBuffer.Append("x");

    //{{{mmap:%starting_addr:%size_in_hex:load:%moduleId:r%(w|x):%relative_addr}}}

    // module.base_address == dlpi_addr
    // range.beg == dlpi_addr + p_vaddr
    // relative address == p_vaddr == range.beg - module.base_address
    buffer->AppendF(kFormatMmap, reinterpret_cast<void *>(range.beg),
                    range.end - range.beg, static_cast<int>(moduleId),
                    accessBuffer.data(), range.beg - module.base_address());

    buffer->Append("\n");
    accessBuffer.clear();
  }
}

void MarkupStackTracePrinter::RenderContext(InternalScopedString *buffer) {
  if (renderedModules_.size() == 0)
    buffer->Append("{{{reset}}}\n");

  const auto &modules = Symbolizer::GetOrInit()->GetRefreshedListOfModules();

  for (const auto &module : modules) {
    if (ModuleHasBeenRendered(module, renderedModules_))
      continue;

    // symbolizer markup id, used to refer to this modules from other contextual
    // elements
    uptr moduleId = renderedModules_.size();

    RenderModule(buffer, module, moduleId);
    RenderMmaps(buffer, module, moduleId);

    renderedModules_.push_back({
        internal_strdup(module.full_name()),
        module.base_address(),
        {},
    });

    // kModuleUUIDSize is the size of curModule.uuid
    CHECK_GE(kModuleUUIDSize, module.uuid_size());
    internal_memcpy(renderedModules_.back().uuid, module.uuid(),
                    module.uuid_size());
  }
}
#endif  // !SANITIZER_FUCHSIA

}  // namespace __sanitizer
