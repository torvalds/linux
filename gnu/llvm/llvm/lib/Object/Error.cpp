//===- Error.cpp - system_error extensions for Object -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines a new error_category for the Object library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Error.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace object;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class _object_error_category : public std::error_category {
public:
  const char* name() const noexcept override;
  std::string message(int ev) const override;
};
}

const char *_object_error_category::name() const noexcept {
  return "llvm.object";
}

std::string _object_error_category::message(int EV) const {
  object_error E = static_cast<object_error>(EV);
  switch (E) {
  case object_error::arch_not_found:
    return "No object file for requested architecture";
  case object_error::invalid_file_type:
    return "The file was not recognized as a valid object file";
  case object_error::parse_failed:
    return "Invalid data was encountered while parsing the file";
  case object_error::unexpected_eof:
    return "The end of the file was unexpectedly encountered";
  case object_error::string_table_non_null_end:
    return "String table must end with a null terminator";
  case object_error::invalid_section_index:
    return "Invalid section index";
  case object_error::bitcode_section_not_found:
    return "Bitcode section not found in object file";
  case object_error::invalid_symbol_index:
    return "Invalid symbol index";
  case object_error::section_stripped:
    return "Section has been stripped from the object file";
  }
  llvm_unreachable("An enumerator of object_error does not have a message "
                   "defined.");
}

void BinaryError::anchor() {}
char BinaryError::ID = 0;
char GenericBinaryError::ID = 0;

GenericBinaryError::GenericBinaryError(const Twine &Msg) : Msg(Msg.str()) {}

GenericBinaryError::GenericBinaryError(const Twine &Msg,
                                       object_error ECOverride)
    : Msg(Msg.str()) {
  setErrorCode(make_error_code(ECOverride));
}

void GenericBinaryError::log(raw_ostream &OS) const {
  OS << Msg;
}

const std::error_category &object::object_category() {
  static _object_error_category error_category;
  return error_category;
}

llvm::Error llvm::object::isNotObjectErrorInvalidFileType(llvm::Error Err) {
  return handleErrors(std::move(Err), [](std::unique_ptr<ECError> M) -> Error {
    // Try to handle 'M'. If successful, return a success value from
    // the handler.
    if (M->convertToErrorCode() == object_error::invalid_file_type)
      return Error::success();

    // We failed to handle 'M' - return it from the handler.
    // This value will be passed back from catchErrors and
    // wind up in Err2, where it will be returned from this function.
    return Error(std::move(M));
  });
}
