//===-- SymbolVendorMacOSX.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolVendorMacOSX.h"

#include <cstring>

#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/XML.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolVendorMacOSX)

// SymbolVendorMacOSX constructor
SymbolVendorMacOSX::SymbolVendorMacOSX(const lldb::ModuleSP &module_sp)
    : SymbolVendor(module_sp) {}

static bool UUIDsMatch(Module *module, ObjectFile *ofile,
                       lldb_private::Stream *feedback_strm) {
  if (module && ofile) {
    // Make sure the UUIDs match
    lldb_private::UUID dsym_uuid = ofile->GetUUID();
    if (!dsym_uuid) {
      if (feedback_strm) {
        feedback_strm->PutCString(
            "warning: failed to get the uuid for object file: '");
        ofile->GetFileSpec().Dump(feedback_strm->AsRawOstream());
        feedback_strm->PutCString("\n");
      }
      return false;
    }

    if (dsym_uuid == module->GetUUID())
      return true;

    // Emit some warning messages since the UUIDs do not match!
    if (feedback_strm) {
      feedback_strm->PutCString(
          "warning: UUID mismatch detected between modules:\n    ");
      module->GetUUID().Dump(*feedback_strm);
      feedback_strm->PutChar(' ');
      module->GetFileSpec().Dump(feedback_strm->AsRawOstream());
      feedback_strm->PutCString("\n    ");
      dsym_uuid.Dump(*feedback_strm);
      feedback_strm->PutChar(' ');
      ofile->GetFileSpec().Dump(feedback_strm->AsRawOstream());
      feedback_strm->EOL();
    }
  }
  return false;
}

void SymbolVendorMacOSX::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolVendorMacOSX::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolVendorMacOSX::GetPluginDescriptionStatic() {
  return "Symbol vendor for MacOSX that looks for dSYM files that match "
         "executables.";
}

// CreateInstance
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
SymbolVendor *
SymbolVendorMacOSX::CreateInstance(const lldb::ModuleSP &module_sp,
                                   lldb_private::Stream *feedback_strm) {
  if (!module_sp)
    return NULL;

  ObjectFile *obj_file =
      llvm::dyn_cast_or_null<ObjectFileMachO>(module_sp->GetObjectFile());
  if (!obj_file)
    return NULL;

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat,
                     "SymbolVendorMacOSX::CreateInstance (module = %s)",
                     module_sp->GetFileSpec().GetPath().c_str());
  SymbolVendorMacOSX *symbol_vendor = new SymbolVendorMacOSX(module_sp);
  if (symbol_vendor) {
    char path[PATH_MAX];
    path[0] = '\0';

    // Try and locate the dSYM file on Mac OS X
    static Timer::Category func_cat2(
        "SymbolVendorMacOSX::CreateInstance() locate dSYM");
    Timer scoped_timer2(
        func_cat2,
        "SymbolVendorMacOSX::CreateInstance (module = %s) locate dSYM",
        module_sp->GetFileSpec().GetPath().c_str());

    // First check to see if the module has a symbol file in mind already. If
    // it does, then we MUST use that.
    FileSpec dsym_fspec(module_sp->GetSymbolFileFileSpec());

    ObjectFileSP dsym_objfile_sp;
    // On Darwin, we store the debug information either in object files,
    // using the debug map to tie them to the executable, or in a dSYM.  We
    // pass through this routine both for binaries and for .o files, but in the
    // latter case there will never be an external debug file.  So we shouldn't
    // do all the stats needed to find it.
    if (!dsym_fspec && module_sp->GetObjectFile()->CalculateType() !=
        ObjectFile::eTypeObjectFile) {
      // No symbol file was specified in the module, lets try and find one
      // ourselves.
      FileSpec file_spec = obj_file->GetFileSpec();
      if (!file_spec)
        file_spec = module_sp->GetFileSpec();

      ModuleSpec module_spec(file_spec, module_sp->GetArchitecture());
      module_spec.GetUUID() = module_sp->GetUUID();
      FileSpecList search_paths = Target::GetDefaultDebugFileSearchPaths();
      dsym_fspec =
          PluginManager::LocateExecutableSymbolFile(module_spec, search_paths);
      if (module_spec.GetSourceMappingList().GetSize())
        module_sp->GetSourceMappingList().Append(
            module_spec.GetSourceMappingList(), true);
    }

    if (dsym_fspec) {
      // Compute dSYM root.
      std::string dsym_root = dsym_fspec.GetPath();
      const size_t pos = dsym_root.find("/Contents/Resources/");
      dsym_root = pos != std::string::npos ? dsym_root.substr(0, pos) : "";

      DataBufferSP dsym_file_data_sp;
      lldb::offset_t dsym_file_data_offset = 0;
      dsym_objfile_sp =
          ObjectFile::FindPlugin(module_sp, &dsym_fspec, 0,
                                 FileSystem::Instance().GetByteSize(dsym_fspec),
                                 dsym_file_data_sp, dsym_file_data_offset);
      // Important to save the dSYM FileSpec so we don't call
      // PluginManager::LocateExecutableSymbolFile a second time while trying to
      // add the symbol ObjectFile to this Module.
      if (dsym_objfile_sp && !module_sp->GetSymbolFileFileSpec()) {
        module_sp->SetSymbolFileFileSpec(dsym_fspec);
      }
      if (UUIDsMatch(module_sp.get(), dsym_objfile_sp.get(), feedback_strm)) {
        // We need a XML parser if we hope to parse a plist...
        if (XMLDocument::XMLEnabled()) {
          if (module_sp->GetSourceMappingList().IsEmpty()) {
            lldb_private::UUID dsym_uuid = dsym_objfile_sp->GetUUID();
            if (dsym_uuid) {
              std::string uuid_str = dsym_uuid.GetAsString();
              if (!uuid_str.empty() && !dsym_root.empty()) {
                char dsym_uuid_plist_path[PATH_MAX];
                snprintf(dsym_uuid_plist_path, sizeof(dsym_uuid_plist_path),
                         "%s/Contents/Resources/%s.plist", dsym_root.c_str(),
                         uuid_str.c_str());
                FileSpec dsym_uuid_plist_spec(dsym_uuid_plist_path);
                if (FileSystem::Instance().Exists(dsym_uuid_plist_spec)) {
                  ApplePropertyList plist(dsym_uuid_plist_path);
                  if (plist) {
                    std::string DBGBuildSourcePath;
                    std::string DBGSourcePath;

                    // DBGSourcePathRemapping is a dictionary in the plist
                    // with keys which are DBGBuildSourcePath file paths and
                    // values which are DBGSourcePath file paths

                    StructuredData::ObjectSP plist_sp =
                        plist.GetStructuredData();
                    if (plist_sp.get() && plist_sp->GetAsDictionary() &&
                        plist_sp->GetAsDictionary()->HasKey(
                            "DBGSourcePathRemapping") &&
                        plist_sp->GetAsDictionary()
                            ->GetValueForKey("DBGSourcePathRemapping")
                            ->GetAsDictionary()) {

                      // If DBGVersion 1 or DBGVersion missing, ignore
                      // DBGSourcePathRemapping. If DBGVersion 2, strip last two
                      // components of path remappings from
                      //                  entries to fix an issue with a
                      //                  specific set of DBGSourcePathRemapping
                      //                  entries that lldb worked with.
                      // If DBGVersion 3, trust & use the source path remappings
                      // as-is.
                      //

                      bool new_style_source_remapping_dictionary = false;
                      bool do_truncate_remapping_names = false;
                      std::string original_DBGSourcePath_value = DBGSourcePath;
                      if (plist_sp->GetAsDictionary()->HasKey("DBGVersion")) {
                        std::string version_string =
                            std::string(plist_sp->GetAsDictionary()
                                            ->GetValueForKey("DBGVersion")
                                            ->GetStringValue(""));
                        if (!version_string.empty() &&
                            isdigit(version_string[0])) {
                          int version_number = atoi(version_string.c_str());
                          if (version_number > 1) {
                            new_style_source_remapping_dictionary = true;
                          }
                          if (version_number == 2) {
                            do_truncate_remapping_names = true;
                          }
                        }
                      }

                      StructuredData::Dictionary *remappings_dict =
                          plist_sp->GetAsDictionary()
                              ->GetValueForKey("DBGSourcePathRemapping")
                              ->GetAsDictionary();
                      remappings_dict->ForEach(
                          [&module_sp, new_style_source_remapping_dictionary,
                           original_DBGSourcePath_value,
                           do_truncate_remapping_names](
                              llvm::StringRef key,
                              StructuredData::Object *object) -> bool {
                            if (object && object->GetAsString()) {

                              // key is DBGBuildSourcePath
                              // object is DBGSourcePath
                              std::string DBGSourcePath =
                                  std::string(object->GetStringValue());
                              if (!new_style_source_remapping_dictionary &&
                                  !original_DBGSourcePath_value.empty()) {
                                DBGSourcePath = original_DBGSourcePath_value;
                              }
                              module_sp->GetSourceMappingList().Append(
                                  key, DBGSourcePath, true);
                              // With version 2 of DBGSourcePathRemapping, we
                              // can chop off the last two filename parts
                              // from the source remapping and get a more
                              // general source remapping that still works.
                              // Add this as another option in addition to
                              // the full source path remap.
                              if (do_truncate_remapping_names) {
                                FileSpec build_path(key);
                                FileSpec source_path(DBGSourcePath.c_str());
                                build_path.RemoveLastPathComponent();
                                build_path.RemoveLastPathComponent();
                                source_path.RemoveLastPathComponent();
                                source_path.RemoveLastPathComponent();
                                module_sp->GetSourceMappingList().Append(
                                    build_path.GetPath(), source_path.GetPath(),
                                    true);
                              }
                            }
                            return true;
                          });
                    }

                    // If we have a DBGBuildSourcePath + DBGSourcePath pair,
                    // append those to the source path remappings.

                    plist.GetValueAsString("DBGBuildSourcePath",
                                           DBGBuildSourcePath);
                    plist.GetValueAsString("DBGSourcePath", DBGSourcePath);
                    if (!DBGBuildSourcePath.empty() && !DBGSourcePath.empty()) {
                      module_sp->GetSourceMappingList().Append(
                          DBGBuildSourcePath, DBGSourcePath, true);
                    }
                  }
                }
              }
            }
          }
        }

        symbol_vendor->AddSymbolFileRepresentation(dsym_objfile_sp);
        return symbol_vendor;
      }
    }

    // Just create our symbol vendor using the current objfile as this is
    // either an executable with no dSYM (that we could locate), an executable
    // with a dSYM that has a UUID that doesn't match.
    symbol_vendor->AddSymbolFileRepresentation(obj_file->shared_from_this());
  }
  return symbol_vendor;
}
