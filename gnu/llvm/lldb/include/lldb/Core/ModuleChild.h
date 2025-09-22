//===-- ModuleChild.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_MODULECHILD_H
#define LLDB_CORE_MODULECHILD_H

#include "lldb/lldb-forward.h"

namespace lldb_private {

/// \class ModuleChild ModuleChild.h "lldb/Core/ModuleChild.h"
/// A mix in class that contains a pointer back to the module
///        that owns the object which inherits from it.
class ModuleChild {
public:
  /// Construct with owning module.
  ///
  /// \param[in] module_sp
  ///     The module that owns the object that inherits from this
  ///     class.
  ModuleChild(const lldb::ModuleSP &module_sp);

  /// Destructor.
  ~ModuleChild();

  /// Assignment operator.
  ///
  /// \param[in] rhs
  ///     A const ModuleChild class reference to copy.
  ///
  /// \return
  ///     A const reference to this object.
  const ModuleChild &operator=(const ModuleChild &rhs);

  /// Get const accessor for the module pointer.
  ///
  /// \return
  ///     A const pointer to the module that owns the object that
  ///     inherits from this class.
  lldb::ModuleSP GetModule() const;

  /// Set accessor for the module pointer.
  ///
  /// \param[in] module_sp
  ///     A new module that owns the object that inherits from this
  ///     class.
  void SetModule(const lldb::ModuleSP &module_sp);

protected:
  /// The Module that owns the object that inherits from this class.
  lldb::ModuleWP m_module_wp;
};

} // namespace lldb_private

#endif // LLDB_CORE_MODULECHILD_H
