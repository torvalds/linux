//===-- RegisterFlagsDetector_arm64.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERFLAGSDETECTOR_ARM64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERFLAGSDETECTOR_ARM64_H

#include "lldb/Target/RegisterFlags.h"
#include "llvm/ADT/StringRef.h"
#include <functional>

namespace lldb_private {

struct RegisterInfo;

/// This class manages the storage and detection of register field information.
/// The same register may have different fields on different CPUs. This class
/// abstracts out the field detection process so we can use it on live processes
/// and core files.
///
/// The way to use this class is:
/// * Make an instance somewhere that will last as long as the debug session
///   (because your final register info will point to this instance).
/// * Read hardware capabilities from a core note, binary, prctl, etc.
/// * Pass those to DetectFields.
/// * Call UpdateRegisterInfo with your RegisterInfo to add pointers
///   to the detected fields for all registers listed in this class.
///
/// This must be done in that order, and you should ensure that if multiple
/// threads will reference the information, a mutex is used to make sure only
/// one calls DetectFields.
class Arm64RegisterFlagsDetector {
public:
  /// For the registers listed in this class, detect which fields are
  /// present. Must be called before UpdateRegisterInfos.
  /// If called more than once, fields will be redetected each time from
  /// scratch. If the target would not have this register at all, the list of
  /// fields will be left empty.
  void DetectFields(uint64_t hwcap, uint64_t hwcap2);

  /// Add the field information of any registers named in this class,
  /// to the relevant RegisterInfo instances. Note that this will be done
  /// with a pointer to the instance of this class that you call this on, so
  /// the lifetime of that instance must be at least that of the register info.
  void UpdateRegisterInfo(const RegisterInfo *reg_info, uint32_t num_regs);

  /// Returns true if field detection has been run at least once.
  bool HasDetected() const { return m_has_detected; }

private:
  using Fields = std::vector<RegisterFlags::Field>;
  using DetectorFn = std::function<Fields(uint64_t, uint64_t)>;

  static Fields DetectCPSRFields(uint64_t hwcap, uint64_t hwcap2);
  static Fields DetectFPSRFields(uint64_t hwcap, uint64_t hwcap2);
  static Fields DetectFPCRFields(uint64_t hwcap, uint64_t hwcap2);
  static Fields DetectMTECtrlFields(uint64_t hwcap, uint64_t hwcap2);
  static Fields DetectSVCRFields(uint64_t hwcap, uint64_t hwcap2);

  struct RegisterEntry {
    RegisterEntry(llvm::StringRef name, unsigned size, DetectorFn detector)
        : m_name(name), m_flags(std::string(name) + "_flags", size, {}),
          m_detector(detector) {}

    llvm::StringRef m_name;
    RegisterFlags m_flags;
    DetectorFn m_detector;
  } m_registers[5] = {
      RegisterEntry("cpsr", 4, DetectCPSRFields),
      RegisterEntry("fpsr", 4, DetectFPSRFields),
      RegisterEntry("fpcr", 4, DetectFPCRFields),
      RegisterEntry("mte_ctrl", 8, DetectMTECtrlFields),
      RegisterEntry("svcr", 8, DetectSVCRFields),
  };

  // Becomes true once field detection has been run for all registers.
  bool m_has_detected = false;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERFLAGSDETECTOR_ARM64_H
