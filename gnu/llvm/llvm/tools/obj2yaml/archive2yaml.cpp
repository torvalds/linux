//===------ utils/archive2yaml.cpp - obj2yaml conversion tool ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/ObjectYAML/ArchiveYAML.h"

using namespace llvm;

namespace {

class ArchiveDumper {
public:
  Expected<ArchYAML::Archive *> dump(MemoryBufferRef Source) {
    StringRef Buffer = Source.getBuffer();
    assert(file_magic::archive == identify_magic(Buffer));

    std::unique_ptr<ArchYAML::Archive> Obj =
        std::make_unique<ArchYAML::Archive>();

    StringRef Magic = "!<arch>\n";
    if (!Buffer.starts_with(Magic))
      return createStringError(std::errc::not_supported,
                               "only regular archives are supported");
    Obj->Magic = Magic;
    Buffer = Buffer.drop_front(Magic.size());

    Obj->Members.emplace();
    while (!Buffer.empty()) {
      uint64_t Offset = Buffer.data() - Source.getBuffer().data();
      if (Buffer.size() < sizeof(ArchiveHeader))
        return createStringError(
            std::errc::illegal_byte_sequence,
            "unable to read the header of a child at offset 0x%" PRIx64,
            Offset);

      const ArchiveHeader &Hdr =
          *reinterpret_cast<const ArchiveHeader *>(Buffer.data());
      Buffer = Buffer.drop_front(sizeof(ArchiveHeader));

      auto ToString = [](ArrayRef<char> V) {
        // We don't want to dump excessive spaces.
        return StringRef(V.data(), V.size()).rtrim(' ');
      };

      ArchYAML::Archive::Child C;
      C.Fields["Name"].Value = ToString(Hdr.Name);
      C.Fields["LastModified"].Value = ToString(Hdr.LastModified);
      C.Fields["UID"].Value = ToString(Hdr.UID);
      C.Fields["GID"].Value = ToString(Hdr.GID);
      C.Fields["AccessMode"].Value = ToString(Hdr.AccessMode);
      StringRef SizeStr = ToString(Hdr.Size);
      C.Fields["Size"].Value = SizeStr;
      C.Fields["Terminator"].Value = ToString(Hdr.Terminator);

      uint64_t Size;
      if (SizeStr.getAsInteger(10, Size))
        return createStringError(
            std::errc::illegal_byte_sequence,
            "unable to read the size of a child at offset 0x%" PRIx64
            " as integer: \"%s\"",
            Offset, SizeStr.str().c_str());
      if (Buffer.size() < Size)
        return createStringError(
            std::errc::illegal_byte_sequence,
            "unable to read the data of a child at offset 0x%" PRIx64
            " of size %" PRId64 ": the remaining archive size is %zu",
            Offset, Size, Buffer.size());
      if (!Buffer.empty())
        C.Content = arrayRefFromStringRef(Buffer.take_front(Size));

      const bool HasPaddingByte = (Size & 1) && Buffer.size() > Size;
      if (HasPaddingByte)
        C.PaddingByte = Buffer[Size];

      Obj->Members->push_back(C);
      // If the size is odd, consume a padding byte.
      Buffer = Buffer.drop_front(HasPaddingByte ? Size + 1 : Size);
    }

    return Obj.release();
  }

private:
  struct ArchiveHeader {
    char Name[16];
    char LastModified[12];
    char UID[6];
    char GID[6];
    char AccessMode[8];
    char Size[10];
    char Terminator[2];
  };
};

} // namespace

Error archive2yaml(raw_ostream &Out, MemoryBufferRef Source) {
  ArchiveDumper Dumper;
  Expected<ArchYAML::Archive *> YAMLOrErr = Dumper.dump(Source);
  if (!YAMLOrErr)
    return YAMLOrErr.takeError();

  std::unique_ptr<ArchYAML::Archive> YAML(YAMLOrErr.get());
  yaml::Output Yout(Out);
  Yout << *YAML;

  return Error::success();
}
