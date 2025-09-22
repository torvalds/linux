//===-- AddressResolverFileLine.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_ADDRESSRESOLVERFILELINE_H
#define LLDB_CORE_ADDRESSRESOLVERFILELINE_H

#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/SourceLocationSpec.h"
#include "lldb/lldb-defines.h"

#include <cstdint>

namespace lldb_private {
class Address;
class Stream;
class SymbolContext;

/// \class AddressResolverFileLine AddressResolverFileLine.h
/// "lldb/Core/AddressResolverFileLine.h" This class finds address for source
/// file and line.  Optionally, it will look for inlined instances of the file
/// and line specification.

class AddressResolverFileLine : public AddressResolver {
public:
  AddressResolverFileLine(SourceLocationSpec location_spec);

  ~AddressResolverFileLine() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context,
                                          Address *addr) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

protected:
  SourceLocationSpec m_src_location_spec;

private:
  AddressResolverFileLine(const AddressResolverFileLine &) = delete;
  const AddressResolverFileLine &
  operator=(const AddressResolverFileLine &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_ADDRESSRESOLVERFILELINE_H
