//===-- DynamicLoaderFreeBSDKernel.cpp
//------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"

#include "DynamicLoaderFreeBSDKernel.h"
#include <memory>
#include <mutex>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(DynamicLoaderFreeBSDKernel)

void DynamicLoaderFreeBSDKernel::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInit);
}

void DynamicLoaderFreeBSDKernel::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef DynamicLoaderFreeBSDKernel::GetPluginDescriptionStatic() {
  return "The Dynamic Loader Plugin For FreeBSD Kernel";
}

static bool is_kernel(Module *module) {
  if (!module)
    return false;

  ObjectFile *objfile = module->GetObjectFile();
  if (!objfile)
    return false;
  if (objfile->GetType() != ObjectFile::eTypeExecutable)
    return false;
  if (objfile->GetStrata() != ObjectFile::eStrataUnknown &&
      objfile->GetStrata() != ObjectFile::eStrataKernel)
    return false;

  return true;
}

static bool is_kmod(Module *module) {
  if (!module)
    return false;
  if (!module->GetObjectFile())
    return false;
  ObjectFile *objfile = module->GetObjectFile();
  if (objfile->GetType() != ObjectFile::eTypeObjectFile &&
      objfile->GetType() != ObjectFile::eTypeSharedLibrary)
    return false;

  return true;
}

static bool is_reloc(Module *module) {
  if (!module)
    return false;
  if (!module->GetObjectFile())
    return false;
  ObjectFile *objfile = module->GetObjectFile();
  if (objfile->GetType() != ObjectFile::eTypeObjectFile)
    return false;

  return true;
}

// Instantiate Function of the FreeBSD Kernel Dynamic Loader Plugin called when
// Register the Plugin
DynamicLoader *
DynamicLoaderFreeBSDKernel::CreateInstance(lldb_private::Process *process,
                                           bool force) {
  // Check the environment when the plugin is not force loaded
  Module *exec = process->GetTarget().GetExecutableModulePointer();
  if (exec && !is_kernel(exec)) {
    return nullptr;
  }
  if (!force) {
    // Check if the target is kernel
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (!triple_ref.isOSFreeBSD()) {
      return nullptr;
    }
  }

  // At this point we have checked the target is a FreeBSD kernel and all we
  // have to do is to find the kernel address
  const addr_t kernel_address = FindFreeBSDKernel(process);

  if (CheckForKernelImageAtAddress(process, kernel_address).IsValid())
    return new DynamicLoaderFreeBSDKernel(process, kernel_address);

  return nullptr;
}

addr_t
DynamicLoaderFreeBSDKernel::FindFreeBSDKernel(lldb_private::Process *process) {
  addr_t kernel_addr = process->GetImageInfoAddress();
  if (kernel_addr == LLDB_INVALID_ADDRESS)
    kernel_addr = FindKernelAtLoadAddress(process);
  return kernel_addr;
}

// Get the kernel address if the kernel is not loaded with a slide
addr_t DynamicLoaderFreeBSDKernel::FindKernelAtLoadAddress(
    lldb_private::Process *process) {
  Module *exe_module = process->GetTarget().GetExecutableModulePointer();

  if (!is_kernel(exe_module))
    return LLDB_INVALID_ADDRESS;

  ObjectFile *exe_objfile = exe_module->GetObjectFile();

  if (!exe_objfile->GetBaseAddress().IsValid())
    return LLDB_INVALID_ADDRESS;

  if (CheckForKernelImageAtAddress(
          process, exe_objfile->GetBaseAddress().GetFileAddress())
          .IsValid())
    return exe_objfile->GetBaseAddress().GetFileAddress();

  return LLDB_INVALID_ADDRESS;
}

// Read ELF header from memry and return
bool DynamicLoaderFreeBSDKernel::ReadELFHeader(Process *process,
                                               lldb::addr_t addr,
                                               llvm::ELF::Elf32_Ehdr &header,
                                               bool *read_error) {
  Status error;
  if (read_error)
    *read_error = false;

  if (process->ReadMemory(addr, &header, sizeof(header), error) !=
      sizeof(header)) {
    if (read_error)
      *read_error = true;
    return false;
  }

  if (!header.checkMagic())
    return false;

  return true;
}

// Check the correctness of Kernel and return UUID
lldb_private::UUID DynamicLoaderFreeBSDKernel::CheckForKernelImageAtAddress(
    Process *process, lldb::addr_t addr, bool *read_error) {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  if (addr == LLDB_INVALID_ADDRESS) {
    if (read_error)
      *read_error = true;
    return UUID();
  }

  LLDB_LOGF(log,
            "DynamicLoaderFreeBSDKernel::CheckForKernelImageAtAddress: "
            "looking for kernel binary at 0x%" PRIx64,
            addr);

  llvm::ELF::Elf32_Ehdr header;
  if (!ReadELFHeader(process, addr, header)) {
    *read_error = true;
    return UUID();
  }

  // Check header type
  if (header.e_type != llvm::ELF::ET_EXEC)
    return UUID();

  ModuleSP memory_module_sp =
      process->ReadModuleFromMemory(FileSpec("temp_freebsd_kernel"), addr);

  if (!memory_module_sp.get()) {
    *read_error = true;
    return UUID();
  }

  ObjectFile *exe_objfile = memory_module_sp->GetObjectFile();
  if (exe_objfile == nullptr) {
    LLDB_LOGF(log,
              "DynamicLoaderFreeBSDKernel::CheckForKernelImageAtAddress "
              "found a binary at 0x%" PRIx64
              " but could not create an object file from memory",
              addr);
    return UUID();
  }

  // In here, I should check is_kernel for memory_module_sp
  // However, the ReadModuleFromMemory reads wrong section so that this check
  // will failed
  ArchSpec kernel_arch(llvm::ELF::convertEMachineToArchName(header.e_machine));

  if (!process->GetTarget().GetArchitecture().IsCompatibleMatch(kernel_arch))
    process->GetTarget().SetArchitecture(kernel_arch);

  std::string uuid_str;
  if (memory_module_sp->GetUUID().IsValid()) {
    uuid_str = "with UUID ";
    uuid_str += memory_module_sp->GetUUID().GetAsString();
  } else {
    uuid_str = "and no LC_UUID found in load commands ";
  }
  LLDB_LOGF(log,
            "DynamicLoaderFreeBSDKernel::CheckForKernelImageAtAddress: "
            "kernel binary image found at 0x%" PRIx64 " with arch '%s' %s",
            addr, kernel_arch.GetTriple().str().c_str(), uuid_str.c_str());

  return memory_module_sp->GetUUID();
}

void DynamicLoaderFreeBSDKernel::DebuggerInit(
    lldb_private::Debugger &debugger) {}

DynamicLoaderFreeBSDKernel::DynamicLoaderFreeBSDKernel(Process *process,
                                                       addr_t kernel_address)
    : DynamicLoader(process), m_process(process),
      m_linker_file_list_struct_addr(LLDB_INVALID_ADDRESS),
      m_linker_file_head_addr(LLDB_INVALID_ADDRESS),
      m_kernel_load_address(kernel_address), m_mutex() {
  process->SetCanRunCode(false);
}

DynamicLoaderFreeBSDKernel::~DynamicLoaderFreeBSDKernel() { Clear(true); }

void DynamicLoaderFreeBSDKernel::Update() {
  LoadKernelModules();
  SetNotificationBreakPoint();
}

// Create in memory Module at the load address
bool DynamicLoaderFreeBSDKernel::KModImageInfo::ReadMemoryModule(
    lldb_private::Process *process) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  if (m_memory_module_sp)
    return true;
  if (m_load_address == LLDB_INVALID_ADDRESS)
    return false;

  FileSpec file_spec(m_name);

  ModuleSP memory_module_sp;

  llvm::ELF::Elf32_Ehdr elf_eheader;
  size_t size_to_read = 512;

  if (ReadELFHeader(process, m_load_address, elf_eheader)) {
    if (elf_eheader.e_ident[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS32) {
      size_to_read = sizeof(llvm::ELF::Elf32_Ehdr) +
                     elf_eheader.e_phnum * elf_eheader.e_phentsize;
    } else if (elf_eheader.e_ident[llvm::ELF::EI_CLASS] ==
               llvm::ELF::ELFCLASS64) {
      llvm::ELF::Elf64_Ehdr elf_eheader;
      Status error;
      if (process->ReadMemory(m_load_address, &elf_eheader, sizeof(elf_eheader),
                              error) == sizeof(elf_eheader))
        size_to_read = sizeof(llvm::ELF::Elf64_Ehdr) +
                       elf_eheader.e_phnum * elf_eheader.e_phentsize;
    }
  }

  memory_module_sp =
      process->ReadModuleFromMemory(file_spec, m_load_address, size_to_read);

  if (!memory_module_sp)
    return false;

  bool this_is_kernel = is_kernel(memory_module_sp.get());

  if (!m_uuid.IsValid() && memory_module_sp->GetUUID().IsValid())
    m_uuid = memory_module_sp->GetUUID();

  m_memory_module_sp = memory_module_sp;
  m_is_kernel = this_is_kernel;

  // The kernel binary is from memory
  if (this_is_kernel) {
    LLDB_LOGF(log, "KextImageInfo::ReadMemoryModule read the kernel binary out "
                   "of memory");

    if (memory_module_sp->GetArchitecture().IsValid())
      process->GetTarget().SetArchitecture(memory_module_sp->GetArchitecture());
  }

  return true;
}

bool DynamicLoaderFreeBSDKernel::KModImageInfo::LoadImageUsingMemoryModule(
    lldb_private::Process *process) {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  if (IsLoaded())
    return true;

  Target &target = process->GetTarget();

  if (IsKernel() && m_uuid.IsValid()) {
    Stream &s = target.GetDebugger().GetOutputStream();
    s.Printf("Kernel UUID: %s\n", m_uuid.GetAsString().c_str());
    s.Printf("Load Address: 0x%" PRIx64 "\n", m_load_address);
  }

  // Test if the module is loaded into the taget,
  // maybe the module is loaded manually by user by doing target module add
  // So that we have to create the module manually
  if (!m_module_sp) {
    const ModuleList &target_images = target.GetImages();
    m_module_sp = target_images.FindModule(m_uuid);

    // Search in the file system
    if (!m_module_sp) {
      ModuleSpec module_spec(FileSpec(GetPath()), target.GetArchitecture());
      if (IsKernel()) {
        Status error;
        if (PluginManager::DownloadObjectAndSymbolFile(module_spec, error,
                                                       true)) {
          if (FileSystem::Instance().Exists(module_spec.GetFileSpec()))
            m_module_sp = std::make_shared<Module>(module_spec.GetFileSpec(),
                                                   target.GetArchitecture());
        }
      }

      if (!m_module_sp)
        m_module_sp = target.GetOrCreateModule(module_spec, true);
      if (IsKernel() && !m_module_sp) {
        Stream &s = target.GetDebugger().GetOutputStream();
        s.Printf("WARNING: Unable to locate kernel binary on the debugger "
                 "system.\n");
      }
    }

    if (m_module_sp) {
      // If the file is not kernel or kmod, the target should be loaded once and
      // don't reload again
      if (!IsKernel() && !is_kmod(m_module_sp.get())) {
        ModuleSP existing_module_sp = target.GetImages().FindModule(m_uuid);
        if (existing_module_sp &&
            existing_module_sp->IsLoadedInTarget(&target)) {
          LLDB_LOGF(log,
                    "'%s' with UUID %s is not a kmod or kernel, and is "
                    "already registered in target, not loading.",
                    m_name.c_str(), m_uuid.GetAsString().c_str());
          return true;
        }
      }
      m_uuid = m_module_sp->GetUUID();

      // or append to the images
      target.GetImages().AppendIfNeeded(m_module_sp, false);
    }
  }

  // If this file is relocatable kernel module(x86_64), adjust it's
  // section(PT_LOAD segment) and return Because the kernel module's load
  // address is the text section. lldb cannot create full memory module upon
  // relocatable file So what we do is to set the load address only.
  if (is_kmod(m_module_sp.get()) && is_reloc(m_module_sp.get())) {
    m_stop_id = process->GetStopID();
    bool changed = false;
    m_module_sp->SetLoadAddress(target, m_load_address, true, changed);
    return true;
  }

  if (m_module_sp)
    ReadMemoryModule(process);

  // Calculate the slides of in memory module
  if (!m_memory_module_sp || !m_module_sp) {
    m_module_sp.reset();
    return false;
  }

  ObjectFile *ondisk_object_file = m_module_sp->GetObjectFile();
  ObjectFile *memory_object_file = m_memory_module_sp->GetObjectFile();

  if (!ondisk_object_file || !memory_object_file)
    m_module_sp.reset();

  // Find the slide address
  addr_t fixed_slide = LLDB_INVALID_ADDRESS;
  if (llvm::dyn_cast<ObjectFileELF>(memory_object_file)) {
    addr_t load_address = memory_object_file->GetBaseAddress().GetFileAddress();

    if (load_address != LLDB_INVALID_ADDRESS &&
        m_load_address != load_address) {
      fixed_slide = m_load_address - load_address;
      LLDB_LOGF(log,
                "kmod %s in-memory LOAD vmaddr is not correct, using a "
                "fixed slide of 0x%" PRIx64,
                m_name.c_str(), fixed_slide);
    }
  }

  SectionList *ondisk_section_list = ondisk_object_file->GetSectionList();
  SectionList *memory_section_list = memory_object_file->GetSectionList();

  if (memory_section_list && ondisk_object_file) {
    const uint32_t num_ondisk_sections = ondisk_section_list->GetSize();
    uint32_t num_load_sections = 0;

    for (uint32_t section_idx = 0; section_idx < num_ondisk_sections;
         ++section_idx) {
      SectionSP on_disk_section_sp =
          ondisk_section_list->GetSectionAtIndex(section_idx);

      if (!on_disk_section_sp)
        continue;
      if (fixed_slide != LLDB_INVALID_ADDRESS) {
        target.SetSectionLoadAddress(on_disk_section_sp,
                                     on_disk_section_sp->GetFileAddress() +
                                         fixed_slide);

      } else {
        const Section *memory_section =
            memory_section_list
                ->FindSectionByName(on_disk_section_sp->GetName())
                .get();
        if (memory_section) {
          target.SetSectionLoadAddress(on_disk_section_sp,
                                       memory_section->GetFileAddress());
          ++num_load_sections;
        }
      }
    }

    if (num_load_sections)
      m_stop_id = process->GetStopID();
    else
      m_module_sp.reset();
  } else {
    m_module_sp.reset();
  }

  if (IsLoaded() && m_module_sp && IsKernel()) {
    Stream &s = target.GetDebugger().GetOutputStream();
    ObjectFile *kernel_object_file = m_module_sp->GetObjectFile();
    if (kernel_object_file) {
      addr_t file_address =
          kernel_object_file->GetBaseAddress().GetFileAddress();
      if (m_load_address != LLDB_INVALID_ADDRESS &&
          file_address != LLDB_INVALID_ADDRESS) {
        s.Printf("Kernel slide 0x%" PRIx64 " in memory.\n",
                 m_load_address - file_address);
        s.Printf("Loaded kernel file %s\n",
                 m_module_sp->GetFileSpec().GetPath().c_str());
      }
    }
    s.Flush();
  }

  return IsLoaded();
}

// This function is work for kernel file, others it wil reset load address and
// return false
bool DynamicLoaderFreeBSDKernel::KModImageInfo::LoadImageUsingFileAddress(
    lldb_private::Process *process) {
  if (IsLoaded())
    return true;

  if (m_module_sp) {
    bool changed = false;
    if (m_module_sp->SetLoadAddress(process->GetTarget(), 0, true, changed))
      m_stop_id = process->GetStopID();
  }

  return false;
}

// Get the head of found_list
bool DynamicLoaderFreeBSDKernel::ReadKmodsListHeader() {
  std::lock_guard<decltype(m_mutex)> guard(m_mutex);

  if (m_linker_file_list_struct_addr.IsValid()) {
    // Get tqh_first struct element from linker_files
    Status error;
    addr_t address = m_process->ReadPointerFromMemory(
        m_linker_file_list_struct_addr.GetLoadAddress(&m_process->GetTarget()),
        error);
    if (address != LLDB_INVALID_ADDRESS && error.Success()) {
      m_linker_file_head_addr = Address(address);
    } else {
      m_linker_file_list_struct_addr.Clear();
      return false;
    }

    if (!m_linker_file_head_addr.IsValid() ||
        m_linker_file_head_addr.GetFileAddress() == 0) {
      m_linker_file_list_struct_addr.Clear();
      return false;
    }
  }
  return true;
}

// Parse Kmod info in found_list
bool DynamicLoaderFreeBSDKernel::ParseKmods(Address linker_files_head_addr) {
  std::lock_guard<decltype(m_mutex)> guard(m_mutex);
  KModImageInfo::collection_type linker_files_list;
  Log *log = GetLog(LLDBLog::DynamicLoader);

  if (!ReadAllKmods(linker_files_head_addr, linker_files_list))
    return false;
  LLDB_LOGF(
      log,
      "Kmod-changed breakpoint hit, there are %zu kernel modules currently.\n",
      linker_files_list.size());

  ModuleList &modules = m_process->GetTarget().GetImages();
  ModuleList remove_modules;
  ModuleList add_modules;

  for (ModuleSP module : modules.Modules()) {
    if (is_kernel(module.get()))
      continue;
    if (is_kmod(module.get()))
      remove_modules.AppendIfNeeded(module);
  }

  m_process->GetTarget().ModulesDidUnload(remove_modules, false);

  for (KModImageInfo &image_info : linker_files_list) {
    if (m_kld_name_to_uuid.find(image_info.GetName()) !=
        m_kld_name_to_uuid.end())
      image_info.SetUUID(m_kld_name_to_uuid[image_info.GetName()]);
    bool failed_to_load = false;
    if (!image_info.LoadImageUsingMemoryModule(m_process)) {
      image_info.LoadImageUsingFileAddress(m_process);
      failed_to_load = true;
    } else {
      m_linker_files_list.push_back(image_info);
      m_kld_name_to_uuid[image_info.GetName()] = image_info.GetUUID();
    }

    if (!failed_to_load)
      add_modules.AppendIfNeeded(image_info.GetModule());
  }
  m_process->GetTarget().ModulesDidLoad(add_modules);
  return true;
}

// Read all kmod from a given arrays of list
bool DynamicLoaderFreeBSDKernel::ReadAllKmods(
    Address linker_files_head_addr,
    KModImageInfo::collection_type &kmods_list) {

  // Get offset of next member and load address symbol
  static ConstString kld_off_address_symbol_name("kld_off_address");
  static ConstString kld_off_next_symbol_name("kld_off_next");
  static ConstString kld_off_filename_symbol_name("kld_off_filename");
  static ConstString kld_off_pathname_symbol_name("kld_off_pathname");
  const Symbol *kld_off_address_symbol =
      m_kernel_image_info.GetModule()->FindFirstSymbolWithNameAndType(
          kld_off_address_symbol_name, eSymbolTypeData);
  const Symbol *kld_off_next_symbol =
      m_kernel_image_info.GetModule()->FindFirstSymbolWithNameAndType(
          kld_off_next_symbol_name, eSymbolTypeData);
  const Symbol *kld_off_filename_symbol =
      m_kernel_image_info.GetModule()->FindFirstSymbolWithNameAndType(
          kld_off_filename_symbol_name, eSymbolTypeData);
  const Symbol *kld_off_pathname_symbol =
      m_kernel_image_info.GetModule()->FindFirstSymbolWithNameAndType(
          kld_off_pathname_symbol_name, eSymbolTypeData);

  if (!kld_off_address_symbol || !kld_off_next_symbol ||
      !kld_off_filename_symbol || !kld_off_pathname_symbol)
    return false;

  Status error;
  const int32_t kld_off_address = m_process->ReadSignedIntegerFromMemory(
      kld_off_address_symbol->GetAddress().GetLoadAddress(
          &m_process->GetTarget()),
      4, 0, error);
  if (error.Fail())
    return false;
  const int32_t kld_off_next = m_process->ReadSignedIntegerFromMemory(
      kld_off_next_symbol->GetAddress().GetLoadAddress(&m_process->GetTarget()),
      4, 0, error);
  if (error.Fail())
    return false;
  const int32_t kld_off_filename = m_process->ReadSignedIntegerFromMemory(
      kld_off_filename_symbol->GetAddress().GetLoadAddress(
          &m_process->GetTarget()),
      4, 0, error);
  if (error.Fail())
    return false;

  const int32_t kld_off_pathname = m_process->ReadSignedIntegerFromMemory(
      kld_off_pathname_symbol->GetAddress().GetLoadAddress(
          &m_process->GetTarget()),
      4, 0, error);
  if (error.Fail())
    return false;

  // Parse KMods
  addr_t kld_load_addr(LLDB_INVALID_ADDRESS);
  char kld_filename[255];
  char kld_pathname[255];
  addr_t current_kld =
      linker_files_head_addr.GetLoadAddress(&m_process->GetTarget());

  while (current_kld != 0) {
    addr_t kld_filename_addr =
        m_process->ReadPointerFromMemory(current_kld + kld_off_filename, error);
    if (error.Fail())
      return false;
    addr_t kld_pathname_addr =
        m_process->ReadPointerFromMemory(current_kld + kld_off_pathname, error);
    if (error.Fail())
      return false;

    m_process->ReadCStringFromMemory(kld_filename_addr, kld_filename,
                                     sizeof(kld_filename), error);
    if (error.Fail())
      return false;
    m_process->ReadCStringFromMemory(kld_pathname_addr, kld_pathname,
                                     sizeof(kld_pathname), error);
    if (error.Fail())
      return false;
    kld_load_addr =
        m_process->ReadPointerFromMemory(current_kld + kld_off_address, error);
    if (error.Fail())
      return false;

    kmods_list.emplace_back();
    KModImageInfo &kmod_info = kmods_list.back();
    kmod_info.SetName(kld_filename);
    kmod_info.SetLoadAddress(kld_load_addr);
    kmod_info.SetPath(kld_pathname);

    current_kld =
        m_process->ReadPointerFromMemory(current_kld + kld_off_next, error);
    if (kmod_info.GetName() == "kernel")
      kmods_list.pop_back();
    if (error.Fail())
      return false;
  }

  return true;
}

// Read all kmods
void DynamicLoaderFreeBSDKernel::ReadAllKmods() {
  std::lock_guard<decltype(m_mutex)> guard(m_mutex);

  if (ReadKmodsListHeader()) {
    if (m_linker_file_head_addr.IsValid()) {
      if (!ParseKmods(m_linker_file_head_addr))
        m_linker_files_list.clear();
    }
  }
}

// Load all Kernel Modules
void DynamicLoaderFreeBSDKernel::LoadKernelModules() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderFreeBSDKernel::LoadKernelModules "
                 "Start loading Kernel Module");

  // Initialize Kernel Image Information at the first time
  if (m_kernel_image_info.GetLoadAddress() == LLDB_INVALID_ADDRESS) {
    ModuleSP module_sp = m_process->GetTarget().GetExecutableModule();
    if (is_kernel(module_sp.get())) {
      m_kernel_image_info.SetModule(module_sp);
      m_kernel_image_info.SetIsKernel(true);
    }

    // Set name for kernel
    llvm::StringRef kernel_name("freebsd_kernel");
    module_sp = m_kernel_image_info.GetModule();
    if (module_sp.get() && module_sp->GetObjectFile() &&
        !module_sp->GetObjectFile()->GetFileSpec().GetFilename().IsEmpty())
      kernel_name = module_sp->GetObjectFile()
                        ->GetFileSpec()
                        .GetFilename()
                        .GetStringRef();
    m_kernel_image_info.SetName(kernel_name.data());

    if (m_kernel_image_info.GetLoadAddress() == LLDB_INVALID_ADDRESS) {
      m_kernel_image_info.SetLoadAddress(m_kernel_load_address);
    }

    // Build In memory Module
    if (m_kernel_image_info.GetLoadAddress() != LLDB_INVALID_ADDRESS) {
      // If the kernel is not loaded in the memory, use file to load
      if (!m_kernel_image_info.LoadImageUsingMemoryModule(m_process))
        m_kernel_image_info.LoadImageUsingFileAddress(m_process);
    }
  }

  LoadOperatingSystemPlugin(false);

  if (!m_kernel_image_info.IsLoaded() || !m_kernel_image_info.GetModule()) {
    m_kernel_image_info.Clear();
    return;
  }

  static ConstString modlist_symbol_name("linker_files");

  const Symbol *symbol =
      m_kernel_image_info.GetModule()->FindFirstSymbolWithNameAndType(
          modlist_symbol_name, lldb::eSymbolTypeData);

  if (symbol) {
    m_linker_file_list_struct_addr = symbol->GetAddress();
    ReadAllKmods();
  } else {
    LLDB_LOGF(log, "DynamicLoaderFreeBSDKernel::LoadKernelModules "
                   "cannot file modlist symbol");
  }
}

// Update symbol when use kldload by setting callback function on kldload
void DynamicLoaderFreeBSDKernel::SetNotificationBreakPoint() {}

// Hook called when attach to a process
void DynamicLoaderFreeBSDKernel::DidAttach() {
  PrivateInitialize(m_process);
  Update();
}

// Hook called after attach to a process
void DynamicLoaderFreeBSDKernel::DidLaunch() {
  PrivateInitialize(m_process);
  Update();
}

// Clear all member except kernel address
void DynamicLoaderFreeBSDKernel::Clear(bool clear_process) {
  std::lock_guard<decltype(m_mutex)> guard(m_mutex);
  if (clear_process)
    m_process = nullptr;
  m_linker_file_head_addr.Clear();
  m_linker_file_list_struct_addr.Clear();
  m_kernel_image_info.Clear();
  m_linker_files_list.clear();
}

// Reinitialize class
void DynamicLoaderFreeBSDKernel::PrivateInitialize(Process *process) {
  Clear(true);
  m_process = process;
}

ThreadPlanSP DynamicLoaderFreeBSDKernel::GetStepThroughTrampolinePlan(
    lldb_private::Thread &thread, bool stop_others) {
  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOGF(log, "DynamicLoaderFreeBSDKernel::GetStepThroughTrampolinePlan is "
                 "not yet implemented.");
  return {};
}

Status DynamicLoaderFreeBSDKernel::CanLoadImage() {
  Status error("shared object cannot be loaded into kernel");
  return error;
}
