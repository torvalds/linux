//===- MinidumpYAML.h - Minidump YAMLIO implementation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_MINIDUMPYAML_H
#define LLVM_OBJECTYAML_MINIDUMPYAML_H

#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Object/Minidump.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace MinidumpYAML {

/// The base class for all minidump streams. The "Type" of the stream
/// corresponds to the Stream Type field in the minidump file. The "Kind" field
/// specifies how are we going to treat it. For highly specialized streams (e.g.
/// SystemInfo), there is a 1:1 mapping between Types and Kinds, but in general
/// one stream Kind can be used to represent multiple stream Types (e.g. any
/// unrecognised stream Type will be handled via RawContentStream). The mapping
/// from Types to Kinds is fixed and given by the static getKind function.
struct Stream {
  enum class StreamKind {
    Exception,
    MemoryInfoList,
    MemoryList,
    ModuleList,
    RawContent,
    SystemInfo,
    TextContent,
    ThreadList,
  };

  Stream(StreamKind Kind, minidump::StreamType Type) : Kind(Kind), Type(Type) {}
  virtual ~Stream(); // anchor

  const StreamKind Kind;
  const minidump::StreamType Type;

  /// Get the stream Kind used for representing streams of a given Type.
  static StreamKind getKind(minidump::StreamType Type);

  /// Create an empty stream of the given Type.
  static std::unique_ptr<Stream> create(minidump::StreamType Type);

  /// Create a stream from the given stream directory entry.
  static Expected<std::unique_ptr<Stream>>
  create(const minidump::Directory &StreamDesc,
         const object::MinidumpFile &File);
};

namespace detail {
/// A stream representing a list of abstract entries in a minidump stream. Its
/// instantiations can be used to represent the ModuleList stream and other
/// streams with a similar structure.
template <typename EntryT> struct ListStream : public Stream {
  using entry_type = EntryT;

  std::vector<entry_type> Entries;

  explicit ListStream(std::vector<entry_type> Entries = {})
      : Stream(EntryT::Kind, EntryT::Type), Entries(std::move(Entries)) {}

  static bool classof(const Stream *S) { return S->Kind == EntryT::Kind; }
};

/// A structure containing all data belonging to a single minidump module.
struct ParsedModule {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::ModuleList;
  static constexpr minidump::StreamType Type = minidump::StreamType::ModuleList;

  minidump::Module Entry;
  std::string Name;
  yaml::BinaryRef CvRecord;
  yaml::BinaryRef MiscRecord;
};

/// A structure containing all data belonging to a single minidump thread.
struct ParsedThread {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::ThreadList;
  static constexpr minidump::StreamType Type = minidump::StreamType::ThreadList;

  minidump::Thread Entry;
  yaml::BinaryRef Stack;
  yaml::BinaryRef Context;
};

/// A structure containing all data describing a single memory region.
struct ParsedMemoryDescriptor {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::MemoryList;
  static constexpr minidump::StreamType Type = minidump::StreamType::MemoryList;

  minidump::MemoryDescriptor Entry;
  yaml::BinaryRef Content;
};
} // namespace detail

using ModuleListStream = detail::ListStream<detail::ParsedModule>;
using ThreadListStream = detail::ListStream<detail::ParsedThread>;
using MemoryListStream = detail::ListStream<detail::ParsedMemoryDescriptor>;

/// ExceptionStream minidump stream.
struct ExceptionStream : public Stream {
  minidump::ExceptionStream MDExceptionStream;
  yaml::BinaryRef ThreadContext;

  ExceptionStream()
      : Stream(StreamKind::Exception, minidump::StreamType::Exception),
        MDExceptionStream({}) {}

  explicit ExceptionStream(const minidump::ExceptionStream &MDExceptionStream,
                           ArrayRef<uint8_t> ThreadContext)
      : Stream(StreamKind::Exception, minidump::StreamType::Exception),
        MDExceptionStream(MDExceptionStream), ThreadContext(ThreadContext) {}

  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::Exception;
  }
};

/// A structure containing the list of MemoryInfo entries comprising a
/// MemoryInfoList stream.
struct MemoryInfoListStream : public Stream {
  std::vector<minidump::MemoryInfo> Infos;

  MemoryInfoListStream()
      : Stream(StreamKind::MemoryInfoList,
               minidump::StreamType::MemoryInfoList) {}

  explicit MemoryInfoListStream(
      iterator_range<object::MinidumpFile::MemoryInfoIterator> Range)
      : Stream(StreamKind::MemoryInfoList,
               minidump::StreamType::MemoryInfoList),
        Infos(Range.begin(), Range.end()) {}

  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::MemoryInfoList;
  }
};

/// A minidump stream represented as a sequence of hex bytes. This is used as a
/// fallback when no other stream kind is suitable.
struct RawContentStream : public Stream {
  yaml::BinaryRef Content;
  yaml::Hex32 Size;

  RawContentStream(minidump::StreamType Type, ArrayRef<uint8_t> Content = {})
      : Stream(StreamKind::RawContent, Type), Content(Content),
        Size(Content.size()) {}

  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::RawContent;
  }
};

/// SystemInfo minidump stream.
struct SystemInfoStream : public Stream {
  minidump::SystemInfo Info;
  std::string CSDVersion;

  SystemInfoStream()
      : Stream(StreamKind::SystemInfo, minidump::StreamType::SystemInfo) {
    memset(&Info, 0, sizeof(Info));
  }

  explicit SystemInfoStream(const minidump::SystemInfo &Info,
                            std::string CSDVersion)
      : Stream(StreamKind::SystemInfo, minidump::StreamType::SystemInfo),
        Info(Info), CSDVersion(std::move(CSDVersion)) {}

  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::SystemInfo;
  }
};

/// A StringRef, which is printed using YAML block notation.
LLVM_YAML_STRONG_TYPEDEF(StringRef, BlockStringRef)

/// A minidump stream containing textual data (typically, the contents of a
/// /proc/<pid> file on linux).
struct TextContentStream : public Stream {
  BlockStringRef Text;

  TextContentStream(minidump::StreamType Type, StringRef Text = {})
      : Stream(StreamKind::TextContent, Type), Text(Text) {}

  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::TextContent;
  }
};

/// The top level structure representing a minidump object, consisting of a
/// minidump header, and zero or more streams. To construct an Object from a
/// minidump file, use the static create function. To serialize to/from yaml,
/// use the appropriate streaming operator on a yaml stream.
struct Object {
  Object() = default;
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  Object(Object &&) = default;
  Object &operator=(Object &&) = default;

  Object(const minidump::Header &Header,
         std::vector<std::unique_ptr<Stream>> Streams)
      : Header(Header), Streams(std::move(Streams)) {}

  /// The minidump header.
  minidump::Header Header;

  /// The list of streams in this minidump object.
  std::vector<std::unique_ptr<Stream>> Streams;

  static Expected<Object> create(const object::MinidumpFile &File);
};

} // namespace MinidumpYAML

namespace yaml {
template <> struct BlockScalarTraits<MinidumpYAML::BlockStringRef> {
  static void output(const MinidumpYAML::BlockStringRef &Text, void *,
                     raw_ostream &OS) {
    OS << Text;
  }

  static StringRef input(StringRef Scalar, void *,
                         MinidumpYAML::BlockStringRef &Text) {
    Text = Scalar;
    return "";
  }
};

template <> struct MappingTraits<std::unique_ptr<MinidumpYAML::Stream>> {
  static void mapping(IO &IO, std::unique_ptr<MinidumpYAML::Stream> &S);
  static std::string validate(IO &IO, std::unique_ptr<MinidumpYAML::Stream> &S);
};

template <> struct MappingContextTraits<minidump::MemoryDescriptor, BinaryRef> {
  static void mapping(IO &IO, minidump::MemoryDescriptor &Memory,
                      BinaryRef &Content);
};

} // namespace yaml

} // namespace llvm

LLVM_YAML_DECLARE_BITSET_TRAITS(llvm::minidump::MemoryProtection)
LLVM_YAML_DECLARE_BITSET_TRAITS(llvm::minidump::MemoryState)
LLVM_YAML_DECLARE_BITSET_TRAITS(llvm::minidump::MemoryType)

LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::minidump::ProcessorArchitecture)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::minidump::OSPlatform)
LLVM_YAML_DECLARE_ENUM_TRAITS(llvm::minidump::StreamType)

LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::CPUInfo::ArmInfo)
LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::CPUInfo::OtherInfo)
LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::CPUInfo::X86Info)
LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::Exception)
LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::MemoryInfo)
LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::minidump::VSFixedFileInfo)

LLVM_YAML_DECLARE_MAPPING_TRAITS(
    llvm::MinidumpYAML::MemoryListStream::entry_type)
LLVM_YAML_DECLARE_MAPPING_TRAITS(
    llvm::MinidumpYAML::ModuleListStream::entry_type)
LLVM_YAML_DECLARE_MAPPING_TRAITS(
    llvm::MinidumpYAML::ThreadListStream::entry_type)

LLVM_YAML_IS_SEQUENCE_VECTOR(std::unique_ptr<llvm::MinidumpYAML::Stream>)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MinidumpYAML::MemoryListStream::entry_type)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MinidumpYAML::ModuleListStream::entry_type)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::MinidumpYAML::ThreadListStream::entry_type)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::minidump::MemoryInfo)

LLVM_YAML_DECLARE_MAPPING_TRAITS(llvm::MinidumpYAML::Object)

#endif // LLVM_OBJECTYAML_MINIDUMPYAML_H
