//===- MachOUniversal.h - Mach-O universal binaries -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Mach-O fat/universal binaries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MACHOUNIVERSAL_H
#define LLVM_OBJECT_MACHOUNIVERSAL_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachO.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
class StringRef;
class LLVMContext;

namespace object {
class Archive;
class IRObjectFile;

class MachOUniversalBinary : public Binary {
  virtual void anchor();

  uint32_t Magic;
  uint32_t NumberOfObjects;
public:
  static constexpr uint32_t MaxSectionAlignment = 15; /* 2**15 or 0x8000 */

  class ObjectForArch {
    const MachOUniversalBinary *Parent;
    /// Index of object in the universal binary.
    uint32_t Index;
    /// Descriptor of the object.
    MachO::fat_arch Header;
    MachO::fat_arch_64 Header64;

  public:
    ObjectForArch(const MachOUniversalBinary *Parent, uint32_t Index);

    void clear() {
      Parent = nullptr;
      Index = 0;
    }

    bool operator==(const ObjectForArch &Other) const {
      return (Parent == Other.Parent) && (Index == Other.Index);
    }

    ObjectForArch getNext() const { return ObjectForArch(Parent, Index + 1); }
    uint32_t getCPUType() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.cputype;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.cputype;
    }
    uint32_t getCPUSubType() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.cpusubtype;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.cpusubtype;
    }
    uint64_t getOffset() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.offset;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.offset;
    }
    uint64_t getSize() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.size;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.size;
    }
    uint32_t getAlign() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.align;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.align;
    }
    uint32_t getReserved() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return 0;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.reserved;
    }
    Triple getTriple() const {
      return MachOObjectFile::getArchTriple(getCPUType(), getCPUSubType());
    }
    std::string getArchFlagName() const {
      const char *McpuDefault, *ArchFlag;
      MachOObjectFile::getArchTriple(getCPUType(), getCPUSubType(),
                                     &McpuDefault, &ArchFlag);
      return ArchFlag ? ArchFlag : std::string();
    }

    Expected<std::unique_ptr<MachOObjectFile>> getAsObjectFile() const;
    Expected<std::unique_ptr<IRObjectFile>>
    getAsIRObject(LLVMContext &Ctx) const;

    Expected<std::unique_ptr<Archive>> getAsArchive() const;
  };

  class object_iterator {
    ObjectForArch Obj;
  public:
    object_iterator(const ObjectForArch &Obj) : Obj(Obj) {}
    const ObjectForArch *operator->() const { return &Obj; }
    const ObjectForArch &operator*() const { return Obj; }

    bool operator==(const object_iterator &Other) const {
      return Obj == Other.Obj;
    }
    bool operator!=(const object_iterator &Other) const {
      return !(*this == Other);
    }

    object_iterator& operator++() {  // Preincrement
      Obj = Obj.getNext();
      return *this;
    }
  };

  MachOUniversalBinary(MemoryBufferRef Souce, Error &Err);
  static Expected<std::unique_ptr<MachOUniversalBinary>>
  create(MemoryBufferRef Source);

  object_iterator begin_objects() const {
    return ObjectForArch(this, 0);
  }
  object_iterator end_objects() const {
    return ObjectForArch(nullptr, 0);
  }

  iterator_range<object_iterator> objects() const {
    return make_range(begin_objects(), end_objects());
  }

  uint32_t getMagic() const { return Magic; }
  uint32_t getNumberOfObjects() const { return NumberOfObjects; }

  // Cast methods.
  static bool classof(Binary const *V) {
    return V->isMachOUniversalBinary();
  }

  Expected<ObjectForArch>
  getObjectForArch(StringRef ArchName) const;

  Expected<std::unique_ptr<MachOObjectFile>>
  getMachOObjectForArch(StringRef ArchName) const;

  Expected<std::unique_ptr<IRObjectFile>>
  getIRObjectForArch(StringRef ArchName, LLVMContext &Ctx) const;

  Expected<std::unique_ptr<Archive>>
  getArchiveForArch(StringRef ArchName) const;
};

}
}

#endif
