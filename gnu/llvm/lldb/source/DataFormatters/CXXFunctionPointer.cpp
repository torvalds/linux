//===-- CXXFunctionPointer.cpp---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/CXXFunctionPointer.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"

#include <string>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

bool lldb_private::formatters::CXXFunctionPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  std::string destination;
  StreamString sstr;
  AddressType func_ptr_address_type = eAddressTypeInvalid;
  addr_t func_ptr_address = valobj.GetPointerValue(&func_ptr_address_type);
  if (func_ptr_address != 0 && func_ptr_address != LLDB_INVALID_ADDRESS) {
    switch (func_ptr_address_type) {
    case eAddressTypeInvalid:
    case eAddressTypeFile:
    case eAddressTypeHost:
      break;

    case eAddressTypeLoad: {
      ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

      Address so_addr;
      Target *target = exe_ctx.GetTargetPtr();
      if (target && !target->GetSectionLoadList().IsEmpty()) {
        target->GetSectionLoadList().ResolveLoadAddress(func_ptr_address,
                                                        so_addr);
        if (so_addr.GetSection() == nullptr) {
          // If we have an address that doesn't correspond to any symbol,
          // it might have authentication bits.  Strip them & see if it
          // now points to a symbol -- if so, do the SymbolContext lookup
          // based on the stripped address.
          // If we find a symbol with the ptrauth bits stripped, print the
          // raw value into the stream, and replace the Address with the
          // one that points to a symbol for a fuller description.
          if (Process *process = exe_ctx.GetProcessPtr()) {
            if (ABISP abi_sp = process->GetABI()) {
              addr_t fixed_addr = abi_sp->FixCodeAddress(func_ptr_address);
              if (fixed_addr != func_ptr_address) {
                Address test_address;
                test_address.SetLoadAddress(fixed_addr, target);
                if (test_address.GetSection() != nullptr) {
                  int addrsize = target->GetArchitecture().GetAddressByteSize();
                  sstr.Printf("actual=0x%*.*" PRIx64 " ", addrsize * 2,
                              addrsize * 2, fixed_addr);
                  so_addr = test_address;
                }
              }
            }
          }
        }

        if (so_addr.IsValid()) {
          so_addr.Dump(&sstr, exe_ctx.GetBestExecutionContextScope(),
                       Address::DumpStyleResolvedDescription,
                       Address::DumpStyleSectionNameOffset);
        }
      }
    } break;
    }
  }
  if (sstr.GetSize() > 0) {
    if (valobj.GetValueType() == lldb::eValueTypeVTableEntry)
      stream.PutCString(sstr.GetData());
    else
      stream.Printf("(%s)", sstr.GetData());
    return true;
  } else
    return false;
}
