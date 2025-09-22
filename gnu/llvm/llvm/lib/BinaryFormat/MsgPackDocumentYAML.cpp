//===-- MsgPackDocumentYAML.cpp - MsgPack Document YAML interface -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// This file implements YAMLIO on a msgpack::Document.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;
using namespace msgpack;

namespace {

// Struct used to represent scalar node. (MapDocNode and ArrayDocNode already
// exist in MsgPackDocument.h.)
struct ScalarDocNode : DocNode {
  ScalarDocNode(DocNode N) : DocNode(N) {}

  /// Get the YAML tag for this ScalarDocNode. This normally returns ""; it only
  /// returns something else if the result of toString would be ambiguous, e.g.
  /// a string that parses as a number or boolean.
  StringRef getYAMLTag() const;
};

} // namespace

/// Convert this DocNode to a string, assuming it is scalar.
std::string DocNode::toString() const {
  std::string S;
  raw_string_ostream OS(S);
  switch (getKind()) {
  case msgpack::Type::String:
    OS << Raw;
    break;
  case msgpack::Type::Nil:
    break;
  case msgpack::Type::Boolean:
    OS << (Bool ? "true" : "false");
    break;
  case msgpack::Type::Int:
    OS << Int;
    break;
  case msgpack::Type::UInt:
    if (getDocument()->getHexMode())
      OS << format("%#llx", (unsigned long long)UInt);
    else
      OS << UInt;
    break;
  case msgpack::Type::Float:
    OS << Float;
    break;
  default:
    llvm_unreachable("not scalar");
    break;
  }
  return OS.str();
}

/// Convert the StringRef and use it to set this DocNode (assuming scalar). If
/// it is a string, copy the string into the Document's strings list so we do
/// not rely on S having a lifetime beyond this call. Tag is "" or a YAML tag.
StringRef DocNode::fromString(StringRef S, StringRef Tag) {
  if (Tag == "tag:yaml.org,2002:str")
    Tag = "";
  if (Tag == "!int" || Tag == "") {
    // Try unsigned int then signed int.
    *this = getDocument()->getNode(uint64_t(0));
    StringRef Err = yaml::ScalarTraits<uint64_t>::input(S, nullptr, getUInt());
    if (Err != "") {
      *this = getDocument()->getNode(int64_t(0));
      Err = yaml::ScalarTraits<int64_t>::input(S, nullptr, getInt());
    }
    if (Err == "" || Tag != "")
      return Err;
  }
  if (Tag == "!nil") {
    *this = getDocument()->getNode();
    return "";
  }
  if (Tag == "!bool" || Tag == "") {
    *this = getDocument()->getNode(false);
    StringRef Err = yaml::ScalarTraits<bool>::input(S, nullptr, getBool());
    if (Err == "" || Tag != "")
      return Err;
  }
  if (Tag == "!float" || Tag == "") {
    *this = getDocument()->getNode(0.0);
    StringRef Err = yaml::ScalarTraits<double>::input(S, nullptr, getFloat());
    if (Err == "" || Tag != "")
      return Err;
  }
  assert((Tag == "!str" || Tag == "") && "unsupported tag");
  std::string V;
  StringRef Err = yaml::ScalarTraits<std::string>::input(S, nullptr, V);
  if (Err == "")
    *this = getDocument()->getNode(V, /*Copy=*/true);
  return Err;
}

/// Get the YAML tag for this ScalarDocNode. This normally returns ""; it only
/// returns something else if the result of toString would be ambiguous, e.g.
/// a string that parses as a number or boolean.
StringRef ScalarDocNode::getYAMLTag() const {
  if (getKind() == msgpack::Type::Nil)
    return "!nil";
  // Try converting both ways and see if we get the same kind. If not, we need
  // a tag.
  ScalarDocNode N = getDocument()->getNode();
  N.fromString(toString(), "");
  if (N.getKind() == getKind())
    return "";
  // Tolerate signedness of int changing, as tags do not differentiate between
  // them anyway.
  if (N.getKind() == msgpack::Type::UInt && getKind() == msgpack::Type::Int)
    return "";
  if (N.getKind() == msgpack::Type::Int && getKind() == msgpack::Type::UInt)
    return "";
  // We do need a tag.
  switch (getKind()) {
  case msgpack::Type::String:
    return "!str";
  case msgpack::Type::Int:
    return "!int";
  case msgpack::Type::UInt:
    return "!int";
  case msgpack::Type::Boolean:
    return "!bool";
  case msgpack::Type::Float:
    return "!float";
  default:
    llvm_unreachable("unrecognized kind");
  }
}

namespace llvm {
namespace yaml {

/// YAMLIO for DocNode
template <> struct PolymorphicTraits<DocNode> {

  static NodeKind getKind(const DocNode &N) {
    switch (N.getKind()) {
    case msgpack::Type::Map:
      return NodeKind::Map;
    case msgpack::Type::Array:
      return NodeKind::Sequence;
    default:
      return NodeKind::Scalar;
    }
  }

  static MapDocNode &getAsMap(DocNode &N) { return N.getMap(/*Convert=*/true); }

  static ArrayDocNode &getAsSequence(DocNode &N) {
    N.getArray(/*Convert=*/true);
    return *static_cast<ArrayDocNode *>(&N);
  }

  static ScalarDocNode &getAsScalar(DocNode &N) {
    return *static_cast<ScalarDocNode *>(&N);
  }
};

/// YAMLIO for ScalarDocNode
template <> struct TaggedScalarTraits<ScalarDocNode> {

  static void output(const ScalarDocNode &S, void *Ctxt, raw_ostream &OS,
                     raw_ostream &TagOS) {
    TagOS << S.getYAMLTag();
    OS << S.toString();
  }

  static StringRef input(StringRef Str, StringRef Tag, void *Ctxt,
                         ScalarDocNode &S) {
    return S.fromString(Str, Tag);
  }

  static QuotingType mustQuote(const ScalarDocNode &S, StringRef ScalarStr) {
    switch (S.getKind()) {
    case Type::Int:
      return ScalarTraits<int64_t>::mustQuote(ScalarStr);
    case Type::UInt:
      return ScalarTraits<uint64_t>::mustQuote(ScalarStr);
    case Type::Nil:
      return ScalarTraits<StringRef>::mustQuote(ScalarStr);
    case Type::Boolean:
      return ScalarTraits<bool>::mustQuote(ScalarStr);
    case Type::Float:
      return ScalarTraits<double>::mustQuote(ScalarStr);
    case Type::Binary:
    case Type::String:
      return ScalarTraits<std::string>::mustQuote(ScalarStr);
    default:
      llvm_unreachable("unrecognized ScalarKind");
    }
  }
};

/// YAMLIO for MapDocNode
template <> struct CustomMappingTraits<MapDocNode> {

  static void inputOne(IO &IO, StringRef Key, MapDocNode &M) {
    ScalarDocNode KeyObj = M.getDocument()->getNode();
    KeyObj.fromString(Key, "");
    IO.mapRequired(Key.str().c_str(), M.getMap()[KeyObj]);
  }

  static void output(IO &IO, MapDocNode &M) {
    for (auto I : M.getMap()) {
      IO.mapRequired(I.first.toString().c_str(), I.second);
    }
  }
};

/// YAMLIO for ArrayNode
template <> struct SequenceTraits<ArrayDocNode> {

  static size_t size(IO &IO, ArrayDocNode &A) { return A.size(); }

  static DocNode &element(IO &IO, ArrayDocNode &A, size_t Index) {
    return A[Index];
  }
};

} // namespace yaml
} // namespace llvm

/// Convert MsgPack Document to YAML text.
void msgpack::Document::toYAML(raw_ostream &OS) {
  yaml::Output Yout(OS);
  Yout << getRoot();
}

/// Read YAML text into the MsgPack document. Returns false on failure.
bool msgpack::Document::fromYAML(StringRef S) {
  clear();
  yaml::Input Yin(S);
  Yin >> getRoot();
  return !Yin.error();
}

