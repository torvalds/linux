//===- TensorSpec.cpp - tensor type abstraction ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation file for the abstraction of a tensor type, and JSON loading
// utils.
//
//===----------------------------------------------------------------------===//
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/config.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cassert>
#include <numeric>

using namespace llvm;

namespace llvm {

#define TFUTILS_GETDATATYPE_IMPL(T, E)                                         \
  template <> TensorType TensorSpec::getDataType<T>() { return TensorType::E; }

SUPPORTED_TENSOR_TYPES(TFUTILS_GETDATATYPE_IMPL)

#undef TFUTILS_GETDATATYPE_IMPL

static std::array<std::string, static_cast<size_t>(TensorType::Total)>
    TensorTypeNames{"INVALID",
#define TFUTILS_GETNAME_IMPL(T, _) #T,
                    SUPPORTED_TENSOR_TYPES(TFUTILS_GETNAME_IMPL)
#undef TFUTILS_GETNAME_IMPL
    };

StringRef toString(TensorType TT) {
  return TensorTypeNames[static_cast<size_t>(TT)];
}

void TensorSpec::toJSON(json::OStream &OS) const {
  OS.object([&]() {
    OS.attribute("name", name());
    OS.attribute("type", toString(type()));
    OS.attribute("port", port());
    OS.attributeArray("shape", [&]() {
      for (size_t D : shape())
        OS.value(static_cast<int64_t>(D));
    });
  });
}

TensorSpec::TensorSpec(const std::string &Name, int Port, TensorType Type,
                       size_t ElementSize, const std::vector<int64_t> &Shape)
    : Name(Name), Port(Port), Type(Type), Shape(Shape),
      ElementCount(std::accumulate(Shape.begin(), Shape.end(), 1,
                                   std::multiplies<int64_t>())),
      ElementSize(ElementSize) {}

std::optional<TensorSpec> getTensorSpecFromJSON(LLVMContext &Ctx,
                                                const json::Value &Value) {
  auto EmitError =
      [&](const llvm::Twine &Message) -> std::optional<TensorSpec> {
    std::string S;
    llvm::raw_string_ostream OS(S);
    OS << Value;
    Ctx.emitError("Unable to parse JSON Value as spec (" + Message + "): " + S);
    return std::nullopt;
  };
  // FIXME: accept a Path as a parameter, and use it for error reporting.
  json::Path::Root Root("tensor_spec");
  json::ObjectMapper Mapper(Value, Root);
  if (!Mapper)
    return EmitError("Value is not a dict");

  std::string TensorName;
  int TensorPort = -1;
  std::string TensorType;
  std::vector<int64_t> TensorShape;

  if (!Mapper.map<std::string>("name", TensorName))
    return EmitError("'name' property not present or not a string");
  if (!Mapper.map<std::string>("type", TensorType))
    return EmitError("'type' property not present or not a string");
  if (!Mapper.map<int>("port", TensorPort))
    return EmitError("'port' property not present or not an int");
  if (!Mapper.map<std::vector<int64_t>>("shape", TensorShape))
    return EmitError("'shape' property not present or not an int array");

#define PARSE_TYPE(T, E)                                                       \
  if (TensorType == #T)                                                        \
    return TensorSpec::createSpec<T>(TensorName, TensorShape, TensorPort);
  SUPPORTED_TENSOR_TYPES(PARSE_TYPE)
#undef PARSE_TYPE
  return std::nullopt;
}

std::string tensorValueToString(const char *Buffer, const TensorSpec &Spec) {
  switch (Spec.type()) {
#define _IMR_DBG_PRINTER(T, N)                                                 \
  case TensorType::N: {                                                        \
    const T *TypedBuff = reinterpret_cast<const T *>(Buffer);                  \
    auto R = llvm::make_range(TypedBuff, TypedBuff + Spec.getElementCount());  \
    return llvm::join(                                                         \
        llvm::map_range(R, [](T V) { return std::to_string(V); }), ",");       \
  }
    SUPPORTED_TENSOR_TYPES(_IMR_DBG_PRINTER)
#undef _IMR_DBG_PRINTER
  case TensorType::Total:
  case TensorType::Invalid:
    llvm_unreachable("invalid tensor type");
  }
  // To appease warnings about not all control paths returning a value.
  return "";
}

} // namespace llvm
