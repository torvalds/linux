//===-- TapiUniversal.h - Text-based Dynamic Library Stub -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the TapiUniversal interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_TAPIUNIVERSAL_H
#define LLVM_OBJECT_TAPIUNIVERSAL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"

namespace llvm {
namespace object {

class TapiFile;

class TapiUniversal : public Binary {
public:
  class ObjectForArch {
    const TapiUniversal *Parent;
    int Index;

  public:
    ObjectForArch(const TapiUniversal *Parent, int Index)
        : Parent(Parent), Index(Index) {}

    ObjectForArch getNext() const { return ObjectForArch(Parent, Index + 1); }

    bool operator==(const ObjectForArch &Other) const {
      return (Parent == Other.Parent) && (Index == Other.Index);
    }

    uint32_t getCPUType() const {
      auto Result =
          MachO::getCPUTypeFromArchitecture(Parent->Libraries[Index].Arch);
      return Result.first;
    }

    uint32_t getCPUSubType() const {
      auto Result =
          MachO::getCPUTypeFromArchitecture(Parent->Libraries[Index].Arch);
      return Result.second;
    }

    StringRef getArchFlagName() const {
      return MachO::getArchitectureName(Parent->Libraries[Index].Arch);
    }

    std::string getInstallName() const {
      return std::string(Parent->Libraries[Index].InstallName);
    }

    bool isTopLevelLib() const {
      return Parent->ParsedFile->getInstallName() == getInstallName();
    }

    Expected<std::unique_ptr<TapiFile>> getAsObjectFile() const;
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

    object_iterator &operator++() { // Preincrement
      Obj = Obj.getNext();
      return *this;
    }
  };

  TapiUniversal(MemoryBufferRef Source, Error &Err);
  static Expected<std::unique_ptr<TapiUniversal>>
  create(MemoryBufferRef Source);
  ~TapiUniversal() override;

  object_iterator begin_objects() const { return ObjectForArch(this, 0); }
  object_iterator end_objects() const {
    return ObjectForArch(this, Libraries.size());
  }

  iterator_range<object_iterator> objects() const {
    return make_range(begin_objects(), end_objects());
  }

  const MachO::InterfaceFile &getInterfaceFile() { return *ParsedFile; }

  uint32_t getNumberOfObjects() const { return Libraries.size(); }

  static bool classof(const Binary *v) { return v->isTapiUniversal(); }

private:
  struct Library {
    StringRef InstallName;
    MachO::Architecture Arch;
  };

  std::unique_ptr<MachO::InterfaceFile> ParsedFile;
  std::vector<Library> Libraries;
};

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_TAPIUNIVERSAL_H
