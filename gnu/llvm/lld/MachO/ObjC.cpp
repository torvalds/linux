//===- ObjC.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjC.h"
#include "ConcatOutputSection.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "Layout.h"
#include "OutputSegment.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace lld;
using namespace lld::macho;

template <class LP> static bool objectHasObjCSection(MemoryBufferRef mb) {
  using SectionHeader = typename LP::section;

  auto *hdr =
      reinterpret_cast<const typename LP::mach_header *>(mb.getBufferStart());
  if (hdr->magic != LP::magic)
    return false;

  if (const auto *c =
          findCommand<typename LP::segment_command>(hdr, LP::segmentLCType)) {
    auto sectionHeaders = ArrayRef<SectionHeader>{
        reinterpret_cast<const SectionHeader *>(c + 1), c->nsects};
    for (const SectionHeader &secHead : sectionHeaders) {
      StringRef sectname(secHead.sectname,
                         strnlen(secHead.sectname, sizeof(secHead.sectname)));
      StringRef segname(secHead.segname,
                        strnlen(secHead.segname, sizeof(secHead.segname)));
      if ((segname == segment_names::data &&
           sectname == section_names::objcCatList) ||
          (segname == segment_names::text &&
           sectname.starts_with(section_names::swift))) {
        return true;
      }
    }
  }
  return false;
}

static bool objectHasObjCSection(MemoryBufferRef mb) {
  if (target->wordSize == 8)
    return ::objectHasObjCSection<LP64>(mb);
  else
    return ::objectHasObjCSection<ILP32>(mb);
}

bool macho::hasObjCSection(MemoryBufferRef mb) {
  switch (identify_magic(mb.getBuffer())) {
  case file_magic::macho_object:
    return objectHasObjCSection(mb);
  case file_magic::bitcode:
    return check(isBitcodeContainingObjCCategory(mb));
  default:
    return false;
  }
}

namespace {

#define FOR_EACH_CATEGORY_FIELD(DO)                                            \
  DO(Ptr, name)                                                                \
  DO(Ptr, klass)                                                               \
  DO(Ptr, instanceMethods)                                                     \
  DO(Ptr, classMethods)                                                        \
  DO(Ptr, protocols)                                                           \
  DO(Ptr, instanceProps)                                                       \
  DO(Ptr, classProps)                                                          \
  DO(uint32_t, size)

CREATE_LAYOUT_CLASS(Category, FOR_EACH_CATEGORY_FIELD);

#undef FOR_EACH_CATEGORY_FIELD

#define FOR_EACH_CLASS_FIELD(DO)                                               \
  DO(Ptr, metaClass)                                                           \
  DO(Ptr, superClass)                                                          \
  DO(Ptr, methodCache)                                                         \
  DO(Ptr, vtable)                                                              \
  DO(Ptr, roData)

CREATE_LAYOUT_CLASS(Class, FOR_EACH_CLASS_FIELD);

#undef FOR_EACH_CLASS_FIELD

#define FOR_EACH_RO_CLASS_FIELD(DO)                                            \
  DO(uint32_t, flags)                                                          \
  DO(uint32_t, instanceStart)                                                  \
  DO(Ptr, instanceSize)                                                        \
  DO(Ptr, ivarLayout)                                                          \
  DO(Ptr, name)                                                                \
  DO(Ptr, baseMethods)                                                         \
  DO(Ptr, baseProtocols)                                                       \
  DO(Ptr, ivars)                                                               \
  DO(Ptr, weakIvarLayout)                                                      \
  DO(Ptr, baseProperties)

CREATE_LAYOUT_CLASS(ROClass, FOR_EACH_RO_CLASS_FIELD);

#undef FOR_EACH_RO_CLASS_FIELD

#define FOR_EACH_LIST_HEADER(DO)                                               \
  DO(uint32_t, structSize)                                                     \
  DO(uint32_t, structCount)

CREATE_LAYOUT_CLASS(ListHeader, FOR_EACH_LIST_HEADER);

#undef FOR_EACH_LIST_HEADER

#define FOR_EACH_PROTOCOL_LIST_HEADER(DO) DO(Ptr, protocolCount)

CREATE_LAYOUT_CLASS(ProtocolListHeader, FOR_EACH_PROTOCOL_LIST_HEADER);

#undef FOR_EACH_PROTOCOL_LIST_HEADER

#define FOR_EACH_METHOD(DO)                                                    \
  DO(Ptr, name)                                                                \
  DO(Ptr, type)                                                                \
  DO(Ptr, impl)

CREATE_LAYOUT_CLASS(Method, FOR_EACH_METHOD);

#undef FOR_EACH_METHOD

enum MethodContainerKind {
  MCK_Class,
  MCK_Category,
};

struct MethodContainer {
  MethodContainerKind kind;
  const ConcatInputSection *isec;
};

enum MethodKind {
  MK_Instance,
  MK_Static,
};

struct ObjcClass {
  DenseMap<CachedHashStringRef, MethodContainer> instanceMethods;
  DenseMap<CachedHashStringRef, MethodContainer> classMethods;
};

} // namespace

class ObjcCategoryChecker {
public:
  ObjcCategoryChecker();
  void parseCategory(const ConcatInputSection *catListIsec);

private:
  void parseClass(const Defined *classSym);
  void parseMethods(const ConcatInputSection *methodsIsec,
                    const Symbol *methodContainer,
                    const ConcatInputSection *containerIsec,
                    MethodContainerKind, MethodKind);

  CategoryLayout catLayout;
  ClassLayout classLayout;
  ROClassLayout roClassLayout;
  ListHeaderLayout listHeaderLayout;
  MethodLayout methodLayout;

  DenseMap<const Symbol *, ObjcClass> classMap;
};

ObjcCategoryChecker::ObjcCategoryChecker()
    : catLayout(target->wordSize), classLayout(target->wordSize),
      roClassLayout(target->wordSize), listHeaderLayout(target->wordSize),
      methodLayout(target->wordSize) {}

// \p r must point to an offset within a CStringInputSection or a
// ConcatInputSection
static StringRef getReferentString(const Reloc &r) {
  if (auto *isec = r.referent.dyn_cast<InputSection *>())
    return cast<CStringInputSection>(isec)->getStringRefAtOffset(r.addend);

  auto *sym = cast<Defined>(r.referent.get<Symbol *>());
  auto *symIsec = sym->isec();
  auto symOffset = sym->value + r.addend;

  if (auto *s = dyn_cast_or_null<CStringInputSection>(symIsec))
    return s->getStringRefAtOffset(symOffset);

  if (isa<ConcatInputSection>(symIsec)) {
    auto strData = symIsec->data.slice(symOffset);
    const char *pszData = reinterpret_cast<const char *>(strData.data());
    return StringRef(pszData, strnlen(pszData, strData.size()));
  }

  llvm_unreachable("unknown reference section in getReferentString");
}

void ObjcCategoryChecker::parseMethods(const ConcatInputSection *methodsIsec,
                                       const Symbol *methodContainerSym,
                                       const ConcatInputSection *containerIsec,
                                       MethodContainerKind mcKind,
                                       MethodKind mKind) {
  ObjcClass &klass = classMap[methodContainerSym];
  for (const Reloc &r : methodsIsec->relocs) {
    if ((r.offset - listHeaderLayout.totalSize) % methodLayout.totalSize !=
        methodLayout.nameOffset)
      continue;

    CachedHashStringRef methodName(getReferentString(r));
    // +load methods are special: all implementations are called by the runtime
    // even if they are part of the same class. Thus there is no need to check
    // for duplicates.
    // NOTE: Instead of specifically checking for this method name, ld64 simply
    // checks whether a class / category is present in __objc_nlclslist /
    // __objc_nlcatlist respectively. This will be the case if the class /
    // category has a +load method. It skips optimizing the categories if there
    // are multiple +load methods. Since it does dupe checking as part of the
    // optimization process, this avoids spurious dupe messages around +load,
    // but it also means that legit dupe issues for other methods are ignored.
    if (mKind == MK_Static && methodName.val() == "load")
      continue;

    auto &methodMap =
        mKind == MK_Instance ? klass.instanceMethods : klass.classMethods;
    if (methodMap
            .try_emplace(methodName, MethodContainer{mcKind, containerIsec})
            .second)
      continue;

    // We have a duplicate; generate a warning message.
    const auto &mc = methodMap.lookup(methodName);
    const Reloc *nameReloc = nullptr;
    if (mc.kind == MCK_Category) {
      nameReloc = mc.isec->getRelocAt(catLayout.nameOffset);
    } else {
      assert(mc.kind == MCK_Class);
      const auto *roIsec = mc.isec->getRelocAt(classLayout.roDataOffset)
                         ->getReferentInputSection();
      nameReloc = roIsec->getRelocAt(roClassLayout.nameOffset);
    }
    StringRef containerName = getReferentString(*nameReloc);
    StringRef methPrefix = mKind == MK_Instance ? "-" : "+";

    // We should only ever encounter collisions when parsing category methods
    // (since the Class struct is parsed before any of its categories).
    assert(mcKind == MCK_Category);
    StringRef newCatName =
        getReferentString(*containerIsec->getRelocAt(catLayout.nameOffset));

    auto formatObjAndSrcFileName = [](const InputSection *section) {
      lld::macho::InputFile *inputFile = section->getFile();
      std::string result = toString(inputFile);

      auto objFile = dyn_cast_or_null<ObjFile>(inputFile);
      if (objFile && objFile->compileUnit)
        result += " (" + objFile->sourceFile() + ")";

      return result;
    };

    StringRef containerType = mc.kind == MCK_Category ? "category" : "class";
    warn("method '" + methPrefix + methodName.val() +
         "' has conflicting definitions:\n>>> defined in category " +
         newCatName + " from " + formatObjAndSrcFileName(containerIsec) +
         "\n>>> defined in " + containerType + " " + containerName + " from " +
         formatObjAndSrcFileName(mc.isec));
  }
}

void ObjcCategoryChecker::parseCategory(const ConcatInputSection *catIsec) {
  auto *classReloc = catIsec->getRelocAt(catLayout.klassOffset);
  if (!classReloc)
    return;

  auto *classSym = classReloc->referent.get<Symbol *>();
  if (auto *d = dyn_cast<Defined>(classSym))
    if (!classMap.count(d))
      parseClass(d);

  if (const auto *r = catIsec->getRelocAt(catLayout.classMethodsOffset)) {
    parseMethods(cast<ConcatInputSection>(r->getReferentInputSection()),
                 classSym, catIsec, MCK_Category, MK_Static);
  }

  if (const auto *r = catIsec->getRelocAt(catLayout.instanceMethodsOffset)) {
    parseMethods(cast<ConcatInputSection>(r->getReferentInputSection()),
                 classSym, catIsec, MCK_Category, MK_Instance);
  }
}

void ObjcCategoryChecker::parseClass(const Defined *classSym) {
  // Given a Class struct, get its corresponding Methods struct
  auto getMethodsIsec =
      [&](const InputSection *classIsec) -> ConcatInputSection * {
    if (const auto *r = classIsec->getRelocAt(classLayout.roDataOffset)) {
      if (const auto *roIsec =
              cast_or_null<ConcatInputSection>(r->getReferentInputSection())) {
        if (const auto *r =
                roIsec->getRelocAt(roClassLayout.baseMethodsOffset)) {
          if (auto *methodsIsec = cast_or_null<ConcatInputSection>(
                  r->getReferentInputSection()))
            return methodsIsec;
        }
      }
    }
    return nullptr;
  };

  const auto *classIsec = cast<ConcatInputSection>(classSym->isec());

  // Parse instance methods.
  if (const auto *instanceMethodsIsec = getMethodsIsec(classIsec))
    parseMethods(instanceMethodsIsec, classSym, classIsec, MCK_Class,
                 MK_Instance);

  // Class methods are contained in the metaclass.
  if (const auto *r = classSym->isec()->getRelocAt(classLayout.metaClassOffset))
    if (const auto *classMethodsIsec = getMethodsIsec(
            cast<ConcatInputSection>(r->getReferentInputSection())))
      parseMethods(classMethodsIsec, classSym, classIsec, MCK_Class, MK_Static);
}

void objc::checkCategories() {
  TimeTraceScope timeScope("ObjcCategoryChecker");

  ObjcCategoryChecker checker;
  for (const InputSection *isec : inputSections) {
    if (isec->getName() == section_names::objcCatList)
      for (const Reloc &r : isec->relocs) {
        auto *catIsec = cast<ConcatInputSection>(r.getReferentInputSection());
        checker.parseCategory(catIsec);
      }
  }
}

namespace {

class ObjcCategoryMerger {
  // In which language was a particular construct originally defined
  enum SourceLanguage { Unknown, ObjC, Swift };

  // Information about an input category
  struct InfoInputCategory {
    ConcatInputSection *catListIsec;
    ConcatInputSection *catBodyIsec;
    uint32_t offCatListIsec = 0;
    SourceLanguage sourceLanguage = SourceLanguage::Unknown;

    bool wasMerged = false;
  };

  // To write new (merged) categories or classes, we will try make limited
  // assumptions about the alignment and the sections the various class/category
  // info are stored in and . So we'll just reuse the same sections and
  // alignment as already used in existing (input) categories. To do this we
  // have InfoCategoryWriter which contains the various sections that the
  // generated categories will be written to.
  struct InfoWriteSection {
    bool valid = false; // Data has been successfully collected from input
    uint32_t align = 0;
    Section *inputSection;
    Reloc relocTemplate;
    OutputSection *outputSection;
  };

  struct InfoCategoryWriter {
    InfoWriteSection catListInfo;
    InfoWriteSection catBodyInfo;
    InfoWriteSection catNameInfo;
    InfoWriteSection catPtrListInfo;
  };

  // Information about a pointer list in the original categories or class(method
  // lists, protocol lists, etc)
  struct PointerListInfo {
    PointerListInfo() = default;
    PointerListInfo(const PointerListInfo &) = default;
    PointerListInfo(const char *_categoryPrefix, uint32_t _pointersPerStruct)
        : categoryPrefix(_categoryPrefix),
          pointersPerStruct(_pointersPerStruct) {}

    inline bool operator==(const PointerListInfo &cmp) const {
      return pointersPerStruct == cmp.pointersPerStruct &&
             structSize == cmp.structSize && structCount == cmp.structCount &&
             allPtrs == cmp.allPtrs;
    }

    const char *categoryPrefix;

    uint32_t pointersPerStruct = 0;

    uint32_t structSize = 0;
    uint32_t structCount = 0;

    std::vector<Symbol *> allPtrs;
  };

  // Full information describing an ObjC class . This will include all the
  // additional methods, protocols, and properties that are contained in the
  // class and all the categories that extend a particular class.
  struct ClassExtensionInfo {
    ClassExtensionInfo(CategoryLayout &_catLayout) : catLayout(_catLayout){};

    // Merged names of containers. Ex: base|firstCategory|secondCategory|...
    std::string mergedContainerName;
    std::string baseClassName;
    const Symbol *baseClass = nullptr;
    SourceLanguage baseClassSourceLanguage = SourceLanguage::Unknown;

    CategoryLayout &catLayout;

    // In case we generate new data, mark the new data as belonging to this file
    ObjFile *objFileForMergeData = nullptr;

    PointerListInfo instanceMethods = {objc::symbol_names::instanceMethods,
                                       /*pointersPerStruct=*/3};
    PointerListInfo classMethods = {objc::symbol_names::categoryClassMethods,
                                    /*pointersPerStruct=*/3};
    PointerListInfo protocols = {objc::symbol_names::categoryProtocols,
                                 /*pointersPerStruct=*/0};
    PointerListInfo instanceProps = {objc::symbol_names::listProprieties,
                                     /*pointersPerStruct=*/2};
    PointerListInfo classProps = {objc::symbol_names::klassPropList,
                                  /*pointersPerStruct=*/2};
  };

public:
  ObjcCategoryMerger(std::vector<ConcatInputSection *> &_allInputSections);
  void doMerge();
  static void doCleanup();

private:
  DenseSet<const Symbol *> collectNlCategories();
  void collectAndValidateCategoriesData();
  void
  mergeCategoriesIntoSingleCategory(std::vector<InfoInputCategory> &categories);

  void eraseISec(ConcatInputSection *isec);
  void eraseMergedCategories();

  void generateCatListForNonErasedCategories(
      MapVector<ConcatInputSection *, std::set<uint64_t>>
          catListToErasedOffsets);
  void collectSectionWriteInfoFromIsec(const InputSection *isec,
                                       InfoWriteSection &catWriteInfo);
  void collectCategoryWriterInfoFromCategory(const InfoInputCategory &catInfo);
  void parseCatInfoToExtInfo(const InfoInputCategory &catInfo,
                             ClassExtensionInfo &extInfo);

  void parseProtocolListInfo(const ConcatInputSection *isec, uint32_t secOffset,
                             PointerListInfo &ptrList,
                             SourceLanguage sourceLang);

  PointerListInfo parseProtocolListInfo(const ConcatInputSection *isec,
                                        uint32_t secOffset,
                                        SourceLanguage sourceLang);

  void parsePointerListInfo(const ConcatInputSection *isec, uint32_t secOffset,
                            PointerListInfo &ptrList);

  void emitAndLinkPointerList(Defined *parentSym, uint32_t linkAtOffset,
                              const ClassExtensionInfo &extInfo,
                              const PointerListInfo &ptrList);

  Defined *emitAndLinkProtocolList(Defined *parentSym, uint32_t linkAtOffset,
                                   const ClassExtensionInfo &extInfo,
                                   const PointerListInfo &ptrList);

  Defined *emitCategory(const ClassExtensionInfo &extInfo);
  Defined *emitCatListEntrySec(const std::string &forCategoryName,
                               const std::string &forBaseClassName,
                               ObjFile *objFile);
  Defined *emitCategoryBody(const std::string &name, const Defined *nameSym,
                            const Symbol *baseClassSym,
                            const std::string &baseClassName, ObjFile *objFile);
  Defined *emitCategoryName(const std::string &name, ObjFile *objFile);
  void createSymbolReference(Defined *refFrom, const Symbol *refTo,
                             uint32_t offset, const Reloc &relocTemplate);
  Defined *tryFindDefinedOnIsec(const InputSection *isec, uint32_t offset);
  Symbol *tryGetSymbolAtIsecOffset(const ConcatInputSection *isec,
                                   uint32_t offset);
  Defined *tryGetDefinedAtIsecOffset(const ConcatInputSection *isec,
                                     uint32_t offset);
  Defined *getClassRo(const Defined *classSym, bool getMetaRo);
  SourceLanguage getClassSymSourceLang(const Defined *classSym);
  void mergeCategoriesIntoBaseClass(const Defined *baseClass,
                                    std::vector<InfoInputCategory> &categories);
  void eraseSymbolAtIsecOffset(ConcatInputSection *isec, uint32_t offset);
  void tryEraseDefinedAtIsecOffset(const ConcatInputSection *isec,
                                   uint32_t offset);

  // Allocate a null-terminated StringRef backed by generatedSectionData
  StringRef newStringData(const char *str);
  // Allocate section data, backed by generatedSectionData
  SmallVector<uint8_t> &newSectionData(uint32_t size);

  CategoryLayout catLayout;
  ClassLayout classLayout;
  ROClassLayout roClassLayout;
  ListHeaderLayout listHeaderLayout;
  MethodLayout methodLayout;
  ProtocolListHeaderLayout protocolListHeaderLayout;

  InfoCategoryWriter infoCategoryWriter;
  std::vector<ConcatInputSection *> &allInputSections;
  // Map of base class Symbol to list of InfoInputCategory's for it
  MapVector<const Symbol *, std::vector<InfoInputCategory>> categoryMap;

  // Normally, the binary data comes from the input files, but since we're
  // generating binary data ourselves, we use the below array to store it in.
  // Need this to be 'static' so the data survives past the ObjcCategoryMerger
  // object, as the data will be read by the Writer when the final binary is
  // generated.
  static SmallVector<std::unique_ptr<SmallVector<uint8_t>>>
      generatedSectionData;
};

SmallVector<std::unique_ptr<SmallVector<uint8_t>>>
    ObjcCategoryMerger::generatedSectionData;

ObjcCategoryMerger::ObjcCategoryMerger(
    std::vector<ConcatInputSection *> &_allInputSections)
    : catLayout(target->wordSize), classLayout(target->wordSize),
      roClassLayout(target->wordSize), listHeaderLayout(target->wordSize),
      methodLayout(target->wordSize),
      protocolListHeaderLayout(target->wordSize),
      allInputSections(_allInputSections) {}

void ObjcCategoryMerger::collectSectionWriteInfoFromIsec(
    const InputSection *isec, InfoWriteSection &catWriteInfo) {

  catWriteInfo.inputSection = const_cast<Section *>(&isec->section);
  catWriteInfo.align = isec->align;
  catWriteInfo.outputSection = isec->parent;

  assert(catWriteInfo.outputSection &&
         "outputSection may not be null in collectSectionWriteInfoFromIsec.");

  if (isec->relocs.size())
    catWriteInfo.relocTemplate = isec->relocs[0];

  catWriteInfo.valid = true;
}

Symbol *
ObjcCategoryMerger::tryGetSymbolAtIsecOffset(const ConcatInputSection *isec,
                                             uint32_t offset) {
  if (!isec)
    return nullptr;
  const Reloc *reloc = isec->getRelocAt(offset);

  if (!reloc)
    return nullptr;

  Symbol *sym = reloc->referent.get<Symbol *>();

  if (reloc->addend) {
    assert(isa<Defined>(sym) && "Expected defined for non-zero addend");
    Defined *definedSym = cast<Defined>(sym);
    sym = tryFindDefinedOnIsec(definedSym->isec(),
                               definedSym->value + reloc->addend);
  }

  return sym;
}

Defined *ObjcCategoryMerger::tryFindDefinedOnIsec(const InputSection *isec,
                                                  uint32_t offset) {
  for (Defined *sym : isec->symbols)
    if ((sym->value <= offset) && (sym->value + sym->size > offset))
      return sym;

  return nullptr;
}

Defined *
ObjcCategoryMerger::tryGetDefinedAtIsecOffset(const ConcatInputSection *isec,
                                              uint32_t offset) {
  Symbol *sym = tryGetSymbolAtIsecOffset(isec, offset);
  return dyn_cast_or_null<Defined>(sym);
}

// Get the class's ro_data symbol. If getMetaRo is true, then we will return
// the meta-class's ro_data symbol. Otherwise, we will return the class
// (instance) ro_data symbol.
Defined *ObjcCategoryMerger::getClassRo(const Defined *classSym,
                                        bool getMetaRo) {
  ConcatInputSection *isec = dyn_cast<ConcatInputSection>(classSym->isec());
  if (!isec)
    return nullptr;

  if (!getMetaRo)
    return tryGetDefinedAtIsecOffset(isec, classLayout.roDataOffset +
                                               classSym->value);

  Defined *metaClass = tryGetDefinedAtIsecOffset(
      isec, classLayout.metaClassOffset + classSym->value);
  if (!metaClass)
    return nullptr;

  return tryGetDefinedAtIsecOffset(
      dyn_cast<ConcatInputSection>(metaClass->isec()),
      classLayout.roDataOffset);
}

// Given an ConcatInputSection or CStringInputSection and an offset, if there is
// a symbol(Defined) at that offset, then erase the symbol (mark it not live)
void ObjcCategoryMerger::tryEraseDefinedAtIsecOffset(
    const ConcatInputSection *isec, uint32_t offset) {
  const Reloc *reloc = isec->getRelocAt(offset);

  if (!reloc)
    return;

  Defined *sym = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
  if (!sym)
    return;

  if (auto *cisec = dyn_cast_or_null<ConcatInputSection>(sym->isec()))
    eraseISec(cisec);
  else if (auto *csisec = dyn_cast_or_null<CStringInputSection>(sym->isec())) {
    uint32_t totalOffset = sym->value + reloc->addend;
    StringPiece &piece = csisec->getStringPiece(totalOffset);
    piece.live = false;
  } else {
    llvm_unreachable("erased symbol has to be Defined or CStringInputSection");
  }
}

void ObjcCategoryMerger::collectCategoryWriterInfoFromCategory(
    const InfoInputCategory &catInfo) {

  if (!infoCategoryWriter.catListInfo.valid)
    collectSectionWriteInfoFromIsec(catInfo.catListIsec,
                                    infoCategoryWriter.catListInfo);
  if (!infoCategoryWriter.catBodyInfo.valid)
    collectSectionWriteInfoFromIsec(catInfo.catBodyIsec,
                                    infoCategoryWriter.catBodyInfo);

  if (!infoCategoryWriter.catNameInfo.valid) {
    lld::macho::Defined *catNameSym =
        tryGetDefinedAtIsecOffset(catInfo.catBodyIsec, catLayout.nameOffset);
    assert(catNameSym && "Category does not have a valid name Symbol");

    collectSectionWriteInfoFromIsec(catNameSym->isec(),
                                    infoCategoryWriter.catNameInfo);
  }

  // Collect writer info from all the category lists (we're assuming they all
  // would provide the same info)
  if (!infoCategoryWriter.catPtrListInfo.valid) {
    for (uint32_t off = catLayout.instanceMethodsOffset;
         off <= catLayout.classPropsOffset; off += target->wordSize) {
      if (Defined *ptrList =
              tryGetDefinedAtIsecOffset(catInfo.catBodyIsec, off)) {
        collectSectionWriteInfoFromIsec(ptrList->isec(),
                                        infoCategoryWriter.catPtrListInfo);
        // we've successfully collected data, so we can break
        break;
      }
    }
  }
}

// Parse a protocol list that might be linked to ConcatInputSection at a given
// offset. The format of the protocol list is different than other lists (prop
// lists, method lists) so we need to parse it differently
void ObjcCategoryMerger::parseProtocolListInfo(
    const ConcatInputSection *isec, uint32_t secOffset,
    PointerListInfo &ptrList, [[maybe_unused]] SourceLanguage sourceLang) {
  assert((isec && (secOffset + target->wordSize <= isec->data.size())) &&
         "Tried to read pointer list beyond protocol section end");

  const Reloc *reloc = isec->getRelocAt(secOffset);
  if (!reloc)
    return;

  auto *ptrListSym = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
  assert(ptrListSym && "Protocol list reloc does not have a valid Defined");

  // Theoretically protocol count can be either 32b or 64b, depending on
  // platform pointer size, but to simplify implementation we always just read
  // the lower 32b which should be good enough.
  uint32_t protocolCount = *reinterpret_cast<const uint32_t *>(
      ptrListSym->isec()->data.data() + listHeaderLayout.structSizeOffset);

  ptrList.structCount += protocolCount;
  ptrList.structSize = target->wordSize;

  [[maybe_unused]] uint32_t expectedListSize =
      (protocolCount * target->wordSize) +
      /*header(count)*/ protocolListHeaderLayout.totalSize +
      /*extra null value*/ target->wordSize;

  // On Swift, the protocol list does not have the extra (unnecessary) null
  [[maybe_unused]] uint32_t expectedListSizeSwift =
      expectedListSize - target->wordSize;

  assert(((expectedListSize == ptrListSym->isec()->data.size() &&
           sourceLang == SourceLanguage::ObjC) ||
          (expectedListSizeSwift == ptrListSym->isec()->data.size() &&
           sourceLang == SourceLanguage::Swift)) &&
         "Protocol list does not match expected size");

  uint32_t off = protocolListHeaderLayout.totalSize;
  for (uint32_t inx = 0; inx < protocolCount; ++inx) {
    const Reloc *reloc = ptrListSym->isec()->getRelocAt(off);
    assert(reloc && "No reloc found at protocol list offset");

    auto *listSym = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
    assert(listSym && "Protocol list reloc does not have a valid Defined");

    ptrList.allPtrs.push_back(listSym);
    off += target->wordSize;
  }
  assert((ptrListSym->isec()->getRelocAt(off) == nullptr) &&
         "expected null terminating protocol");
  assert(off + /*extra null value*/ target->wordSize == expectedListSize &&
         "Protocol list end offset does not match expected size");
}

// Parse a protocol list and return the PointerListInfo for it
ObjcCategoryMerger::PointerListInfo
ObjcCategoryMerger::parseProtocolListInfo(const ConcatInputSection *isec,
                                          uint32_t secOffset,
                                          SourceLanguage sourceLang) {
  PointerListInfo ptrList;
  parseProtocolListInfo(isec, secOffset, ptrList, sourceLang);
  return ptrList;
}

// Parse a pointer list that might be linked to ConcatInputSection at a given
// offset. This can be used for instance methods, class methods, instance props
// and class props since they have the same format.
void ObjcCategoryMerger::parsePointerListInfo(const ConcatInputSection *isec,
                                              uint32_t secOffset,
                                              PointerListInfo &ptrList) {
  assert(ptrList.pointersPerStruct == 2 || ptrList.pointersPerStruct == 3);
  assert(isec && "Trying to parse pointer list from null isec");
  assert(secOffset + target->wordSize <= isec->data.size() &&
         "Trying to read pointer list beyond section end");

  const Reloc *reloc = isec->getRelocAt(secOffset);
  if (!reloc)
    return;

  auto *ptrListSym = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
  assert(ptrListSym && "Reloc does not have a valid Defined");

  uint32_t thisStructSize = *reinterpret_cast<const uint32_t *>(
      ptrListSym->isec()->data.data() + listHeaderLayout.structSizeOffset);
  uint32_t thisStructCount = *reinterpret_cast<const uint32_t *>(
      ptrListSym->isec()->data.data() + listHeaderLayout.structCountOffset);
  assert(thisStructSize == ptrList.pointersPerStruct * target->wordSize);

  assert(!ptrList.structSize || (thisStructSize == ptrList.structSize));

  ptrList.structCount += thisStructCount;
  ptrList.structSize = thisStructSize;

  uint32_t expectedListSize =
      listHeaderLayout.totalSize + (thisStructSize * thisStructCount);
  assert(expectedListSize == ptrListSym->isec()->data.size() &&
         "Pointer list does not match expected size");

  for (uint32_t off = listHeaderLayout.totalSize; off < expectedListSize;
       off += target->wordSize) {
    const Reloc *reloc = ptrListSym->isec()->getRelocAt(off);
    assert(reloc && "No reloc found at pointer list offset");

    auto *listSym = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
    assert(listSym && "Reloc does not have a valid Defined");

    ptrList.allPtrs.push_back(listSym);
  }
}

// Here we parse all the information of an input category (catInfo) and
// append the parsed info into the structure which will contain all the
// information about how a class is extended (extInfo)
void ObjcCategoryMerger::parseCatInfoToExtInfo(const InfoInputCategory &catInfo,
                                               ClassExtensionInfo &extInfo) {
  const Reloc *catNameReloc =
      catInfo.catBodyIsec->getRelocAt(catLayout.nameOffset);

  // Parse name
  assert(catNameReloc && "Category does not have a reloc at 'nameOffset'");

  // is this the first category we are parsing?
  if (extInfo.mergedContainerName.empty())
    extInfo.objFileForMergeData =
        dyn_cast_or_null<ObjFile>(catInfo.catBodyIsec->getFile());
  else
    extInfo.mergedContainerName += "|";

  assert(extInfo.objFileForMergeData &&
         "Expected to already have valid objextInfo.objFileForMergeData");

  StringRef catName = getReferentString(*catNameReloc);
  extInfo.mergedContainerName += catName.str();

  // Parse base class
  if (!extInfo.baseClass) {
    Symbol *classSym =
        tryGetSymbolAtIsecOffset(catInfo.catBodyIsec, catLayout.klassOffset);
    assert(extInfo.baseClassName.empty());
    extInfo.baseClass = classSym;
    llvm::StringRef classPrefix(objc::symbol_names::klass);
    assert(classSym->getName().starts_with(classPrefix) &&
           "Base class symbol does not start with expected prefix");
    extInfo.baseClassName = classSym->getName().substr(classPrefix.size());
  } else {
    assert((extInfo.baseClass ==
            tryGetSymbolAtIsecOffset(catInfo.catBodyIsec,
                                     catLayout.klassOffset)) &&
           "Trying to parse category info into container with different base "
           "class");
  }

  parsePointerListInfo(catInfo.catBodyIsec, catLayout.instanceMethodsOffset,
                       extInfo.instanceMethods);

  parsePointerListInfo(catInfo.catBodyIsec, catLayout.classMethodsOffset,
                       extInfo.classMethods);

  parseProtocolListInfo(catInfo.catBodyIsec, catLayout.protocolsOffset,
                        extInfo.protocols, catInfo.sourceLanguage);

  parsePointerListInfo(catInfo.catBodyIsec, catLayout.instancePropsOffset,
                       extInfo.instanceProps);

  parsePointerListInfo(catInfo.catBodyIsec, catLayout.classPropsOffset,
                       extInfo.classProps);
}

// Generate a protocol list (including header) and link it into the parent at
// the specified offset.
Defined *ObjcCategoryMerger::emitAndLinkProtocolList(
    Defined *parentSym, uint32_t linkAtOffset,
    const ClassExtensionInfo &extInfo, const PointerListInfo &ptrList) {
  if (ptrList.allPtrs.empty())
    return nullptr;

  assert(ptrList.allPtrs.size() == ptrList.structCount);

  uint32_t bodySize = (ptrList.structCount * target->wordSize) +
                      /*header(count)*/ protocolListHeaderLayout.totalSize +
                      /*extra null value*/ target->wordSize;
  llvm::ArrayRef<uint8_t> bodyData = newSectionData(bodySize);

  // This theoretically can be either 32b or 64b, but writing just the first 32b
  // is good enough
  const uint32_t *ptrProtoCount = reinterpret_cast<const uint32_t *>(
      bodyData.data() + protocolListHeaderLayout.protocolCountOffset);

  *const_cast<uint32_t *>(ptrProtoCount) = ptrList.allPtrs.size();

  ConcatInputSection *listSec = make<ConcatInputSection>(
      *infoCategoryWriter.catPtrListInfo.inputSection, bodyData,
      infoCategoryWriter.catPtrListInfo.align);
  listSec->parent = infoCategoryWriter.catPtrListInfo.outputSection;
  listSec->live = true;

  listSec->parent = infoCategoryWriter.catPtrListInfo.outputSection;

  std::string symName = ptrList.categoryPrefix;
  symName += extInfo.baseClassName + "(" + extInfo.mergedContainerName + ")";

  Defined *ptrListSym = make<Defined>(
      newStringData(symName.c_str()), /*file=*/parentSym->getObjectFile(),
      listSec, /*value=*/0, bodyData.size(), /*isWeakDef=*/false,
      /*isExternal=*/false, /*isPrivateExtern=*/false, /*includeInSymtab=*/true,
      /*isReferencedDynamically=*/false, /*noDeadStrip=*/false,
      /*isWeakDefCanBeHidden=*/false);

  ptrListSym->used = true;
  parentSym->getObjectFile()->symbols.push_back(ptrListSym);
  addInputSection(listSec);

  createSymbolReference(parentSym, ptrListSym, linkAtOffset,
                        infoCategoryWriter.catBodyInfo.relocTemplate);

  uint32_t offset = protocolListHeaderLayout.totalSize;
  for (Symbol *symbol : ptrList.allPtrs) {
    createSymbolReference(ptrListSym, symbol, offset,
                          infoCategoryWriter.catPtrListInfo.relocTemplate);
    offset += target->wordSize;
  }

  return ptrListSym;
}

// Generate a pointer list (including header) and link it into the parent at the
// specified offset. This is used for instance and class methods and
// proprieties.
void ObjcCategoryMerger::emitAndLinkPointerList(
    Defined *parentSym, uint32_t linkAtOffset,
    const ClassExtensionInfo &extInfo, const PointerListInfo &ptrList) {
  if (ptrList.allPtrs.empty())
    return;

  assert(ptrList.allPtrs.size() * target->wordSize ==
         ptrList.structCount * ptrList.structSize);

  // Generate body
  uint32_t bodySize =
      listHeaderLayout.totalSize + (ptrList.structSize * ptrList.structCount);
  llvm::ArrayRef<uint8_t> bodyData = newSectionData(bodySize);

  const uint32_t *ptrStructSize = reinterpret_cast<const uint32_t *>(
      bodyData.data() + listHeaderLayout.structSizeOffset);
  const uint32_t *ptrStructCount = reinterpret_cast<const uint32_t *>(
      bodyData.data() + listHeaderLayout.structCountOffset);

  *const_cast<uint32_t *>(ptrStructSize) = ptrList.structSize;
  *const_cast<uint32_t *>(ptrStructCount) = ptrList.structCount;

  ConcatInputSection *listSec = make<ConcatInputSection>(
      *infoCategoryWriter.catPtrListInfo.inputSection, bodyData,
      infoCategoryWriter.catPtrListInfo.align);
  listSec->parent = infoCategoryWriter.catPtrListInfo.outputSection;
  listSec->live = true;

  listSec->parent = infoCategoryWriter.catPtrListInfo.outputSection;

  std::string symName = ptrList.categoryPrefix;
  symName += extInfo.baseClassName + "(" + extInfo.mergedContainerName + ")";

  Defined *ptrListSym = make<Defined>(
      newStringData(symName.c_str()), /*file=*/parentSym->getObjectFile(),
      listSec, /*value=*/0, bodyData.size(), /*isWeakDef=*/false,
      /*isExternal=*/false, /*isPrivateExtern=*/false, /*includeInSymtab=*/true,
      /*isReferencedDynamically=*/false, /*noDeadStrip=*/false,
      /*isWeakDefCanBeHidden=*/false);

  ptrListSym->used = true;
  parentSym->getObjectFile()->symbols.push_back(ptrListSym);
  addInputSection(listSec);

  createSymbolReference(parentSym, ptrListSym, linkAtOffset,
                        infoCategoryWriter.catBodyInfo.relocTemplate);

  uint32_t offset = listHeaderLayout.totalSize;
  for (Symbol *symbol : ptrList.allPtrs) {
    createSymbolReference(ptrListSym, symbol, offset,
                          infoCategoryWriter.catPtrListInfo.relocTemplate);
    offset += target->wordSize;
  }
}

// This method creates an __objc_catlist ConcatInputSection with a single slot
Defined *
ObjcCategoryMerger::emitCatListEntrySec(const std::string &forCategoryName,
                                        const std::string &forBaseClassName,
                                        ObjFile *objFile) {
  uint32_t sectionSize = target->wordSize;
  llvm::ArrayRef<uint8_t> bodyData = newSectionData(sectionSize);

  ConcatInputSection *newCatList =
      make<ConcatInputSection>(*infoCategoryWriter.catListInfo.inputSection,
                               bodyData, infoCategoryWriter.catListInfo.align);
  newCatList->parent = infoCategoryWriter.catListInfo.outputSection;
  newCatList->live = true;

  newCatList->parent = infoCategoryWriter.catListInfo.outputSection;

  std::string catSymName = "<__objc_catlist slot for merged category ";
  catSymName += forBaseClassName + "(" + forCategoryName + ")>";

  Defined *catListSym = make<Defined>(
      newStringData(catSymName.c_str()), /*file=*/objFile, newCatList,
      /*value=*/0, bodyData.size(), /*isWeakDef=*/false, /*isExternal=*/false,
      /*isPrivateExtern=*/false, /*includeInSymtab=*/false,
      /*isReferencedDynamically=*/false, /*noDeadStrip=*/false,
      /*isWeakDefCanBeHidden=*/false);

  catListSym->used = true;
  objFile->symbols.push_back(catListSym);
  addInputSection(newCatList);
  return catListSym;
}

// Here we generate the main category body and link the name and base class into
// it. We don't link any other info yet like the protocol and class/instance
// methods/props.
Defined *ObjcCategoryMerger::emitCategoryBody(const std::string &name,
                                              const Defined *nameSym,
                                              const Symbol *baseClassSym,
                                              const std::string &baseClassName,
                                              ObjFile *objFile) {
  llvm::ArrayRef<uint8_t> bodyData = newSectionData(catLayout.totalSize);

  uint32_t *ptrSize = (uint32_t *)(const_cast<uint8_t *>(bodyData.data()) +
                                   catLayout.sizeOffset);
  *ptrSize = catLayout.totalSize;

  ConcatInputSection *newBodySec =
      make<ConcatInputSection>(*infoCategoryWriter.catBodyInfo.inputSection,
                               bodyData, infoCategoryWriter.catBodyInfo.align);
  newBodySec->parent = infoCategoryWriter.catBodyInfo.outputSection;
  newBodySec->live = true;

  std::string symName =
      objc::symbol_names::category + baseClassName + "(" + name + ")";
  Defined *catBodySym = make<Defined>(
      newStringData(symName.c_str()), /*file=*/objFile, newBodySec,
      /*value=*/0, bodyData.size(), /*isWeakDef=*/false, /*isExternal=*/false,
      /*isPrivateExtern=*/false, /*includeInSymtab=*/true,
      /*isReferencedDynamically=*/false, /*noDeadStrip=*/false,
      /*isWeakDefCanBeHidden=*/false);

  catBodySym->used = true;
  objFile->symbols.push_back(catBodySym);
  addInputSection(newBodySec);

  createSymbolReference(catBodySym, nameSym, catLayout.nameOffset,
                        infoCategoryWriter.catBodyInfo.relocTemplate);

  // Create a reloc to the base class (either external or internal)
  createSymbolReference(catBodySym, baseClassSym, catLayout.klassOffset,
                        infoCategoryWriter.catBodyInfo.relocTemplate);

  return catBodySym;
}

// This writes the new category name (for the merged category) into the binary
// and returns the sybmol for it.
Defined *ObjcCategoryMerger::emitCategoryName(const std::string &name,
                                              ObjFile *objFile) {
  StringRef nameStrData = newStringData(name.c_str());
  // We use +1 below to include the null terminator
  llvm::ArrayRef<uint8_t> nameData(
      reinterpret_cast<const uint8_t *>(nameStrData.data()),
      nameStrData.size() + 1);

  auto *parentSection = infoCategoryWriter.catNameInfo.inputSection;
  CStringInputSection *newStringSec = make<CStringInputSection>(
      *infoCategoryWriter.catNameInfo.inputSection, nameData,
      infoCategoryWriter.catNameInfo.align, /*dedupLiterals=*/true);

  parentSection->subsections.push_back({0, newStringSec});

  newStringSec->splitIntoPieces();
  newStringSec->pieces[0].live = true;
  newStringSec->parent = infoCategoryWriter.catNameInfo.outputSection;
  in.cStringSection->addInput(newStringSec);
  assert(newStringSec->pieces.size() == 1);

  Defined *catNameSym = make<Defined>(
      "<merged category name>", /*file=*/objFile, newStringSec,
      /*value=*/0, nameData.size(),
      /*isWeakDef=*/false, /*isExternal=*/false, /*isPrivateExtern=*/false,
      /*includeInSymtab=*/false, /*isReferencedDynamically=*/false,
      /*noDeadStrip=*/false, /*isWeakDefCanBeHidden=*/false);

  catNameSym->used = true;
  objFile->symbols.push_back(catNameSym);
  return catNameSym;
}

// This method fully creates a new category from the given ClassExtensionInfo.
// It creates the category name, body and method/protocol/prop lists and links
// them all together. Then it creates a new __objc_catlist entry and adds the
// category to it. Calling this method will fully generate a category which will
// be available in the final binary.
Defined *ObjcCategoryMerger::emitCategory(const ClassExtensionInfo &extInfo) {
  Defined *catNameSym = emitCategoryName(extInfo.mergedContainerName,
                                         extInfo.objFileForMergeData);

  Defined *catBodySym = emitCategoryBody(
      extInfo.mergedContainerName, catNameSym, extInfo.baseClass,
      extInfo.baseClassName, extInfo.objFileForMergeData);

  Defined *catListSym =
      emitCatListEntrySec(extInfo.mergedContainerName, extInfo.baseClassName,
                          extInfo.objFileForMergeData);

  // Add the single category body to the category list at the offset 0.
  createSymbolReference(catListSym, catBodySym, /*offset=*/0,
                        infoCategoryWriter.catListInfo.relocTemplate);

  emitAndLinkPointerList(catBodySym, catLayout.instanceMethodsOffset, extInfo,
                         extInfo.instanceMethods);

  emitAndLinkPointerList(catBodySym, catLayout.classMethodsOffset, extInfo,
                         extInfo.classMethods);

  emitAndLinkProtocolList(catBodySym, catLayout.protocolsOffset, extInfo,
                          extInfo.protocols);

  emitAndLinkPointerList(catBodySym, catLayout.instancePropsOffset, extInfo,
                         extInfo.instanceProps);

  emitAndLinkPointerList(catBodySym, catLayout.classPropsOffset, extInfo,
                         extInfo.classProps);

  return catBodySym;
}

// This method merges all the categories (sharing a base class) into a single
// category.
void ObjcCategoryMerger::mergeCategoriesIntoSingleCategory(
    std::vector<InfoInputCategory> &categories) {
  assert(categories.size() > 1 && "Expected at least 2 categories");

  ClassExtensionInfo extInfo(catLayout);

  for (auto &catInfo : categories)
    parseCatInfoToExtInfo(catInfo, extInfo);

  Defined *newCatDef = emitCategory(extInfo);
  assert(newCatDef && "Failed to create a new category");

  // Suppress unsuded var warning
  (void)newCatDef;

  for (auto &catInfo : categories)
    catInfo.wasMerged = true;
}

void ObjcCategoryMerger::createSymbolReference(Defined *refFrom,
                                               const Symbol *refTo,
                                               uint32_t offset,
                                               const Reloc &relocTemplate) {
  Reloc r = relocTemplate;
  r.offset = offset;
  r.addend = 0;
  r.referent = const_cast<Symbol *>(refTo);
  refFrom->isec()->relocs.push_back(r);
}

// Get the list of categories in the '__objc_nlcatlist' section. We can't
// optimize these as they have a '+load' method that has to be called at
// runtime.
DenseSet<const Symbol *> ObjcCategoryMerger::collectNlCategories() {
  DenseSet<const Symbol *> nlCategories;

  for (InputSection *sec : allInputSections) {
    if (sec->getName() != section_names::objcNonLazyCatList)
      continue;

    for (auto &r : sec->relocs) {
      const Symbol *sym = r.referent.dyn_cast<Symbol *>();
      nlCategories.insert(sym);
    }
  }
  return nlCategories;
}

void ObjcCategoryMerger::collectAndValidateCategoriesData() {
  auto nlCategories = collectNlCategories();

  for (InputSection *sec : allInputSections) {
    if (sec->getName() != section_names::objcCatList)
      continue;
    ConcatInputSection *catListCisec = dyn_cast<ConcatInputSection>(sec);
    assert(catListCisec &&
           "__objc_catList InputSection is not a ConcatInputSection");

    for (uint32_t off = 0; off < catListCisec->getSize();
         off += target->wordSize) {
      Defined *categorySym = tryGetDefinedAtIsecOffset(catListCisec, off);
      assert(categorySym &&
             "Failed to get a valid category at __objc_catlit offset");

      if (nlCategories.count(categorySym))
        continue;

      auto *catBodyIsec = dyn_cast<ConcatInputSection>(categorySym->isec());
      assert(catBodyIsec &&
             "Category data section is not an ConcatInputSection");

      SourceLanguage eLang = SourceLanguage::Unknown;
      if (categorySym->getName().starts_with(objc::symbol_names::category))
        eLang = SourceLanguage::ObjC;
      else if (categorySym->getName().starts_with(
                   objc::symbol_names::swift_objc_category))
        eLang = SourceLanguage::Swift;
      else
        llvm_unreachable("Unexpected category symbol name");

      InfoInputCategory catInputInfo{catListCisec, catBodyIsec, off, eLang};

      // Check that the category has a reloc at 'klassOffset' (which is
      // a pointer to the class symbol)

      Symbol *classSym =
          tryGetSymbolAtIsecOffset(catBodyIsec, catLayout.klassOffset);
      assert(classSym && "Category does not have a valid base class");

      categoryMap[classSym].push_back(catInputInfo);

      collectCategoryWriterInfoFromCategory(catInputInfo);
    }
  }
}

// In the input we have multiple __objc_catlist InputSection, each of which may
// contain links to multiple categories. Of these categories, we will merge (and
// erase) only some. There will be some categories that will remain untouched
// (not erased). For these not erased categories, we generate new __objc_catlist
// entries since the parent __objc_catlist entry will be erased
void ObjcCategoryMerger::generateCatListForNonErasedCategories(
    const MapVector<ConcatInputSection *, std::set<uint64_t>>
        catListToErasedOffsets) {

  // Go through all offsets of all __objc_catlist's that we process and if there
  // are categories that we didn't process - generate a new __objc_catlist for
  // each.
  for (auto &mapEntry : catListToErasedOffsets) {
    ConcatInputSection *catListIsec = mapEntry.first;
    for (uint32_t catListIsecOffset = 0;
         catListIsecOffset < catListIsec->data.size();
         catListIsecOffset += target->wordSize) {
      // This slot was erased, we can just skip it
      if (mapEntry.second.count(catListIsecOffset))
        continue;

      Defined *nonErasedCatBody =
          tryGetDefinedAtIsecOffset(catListIsec, catListIsecOffset);
      assert(nonErasedCatBody && "Failed to relocate non-deleted category");

      // Allocate data for the new __objc_catlist slot
      llvm::ArrayRef<uint8_t> bodyData = newSectionData(target->wordSize);

      // We mark the __objc_catlist slot as belonging to the same file as the
      // category
      ObjFile *objFile = dyn_cast<ObjFile>(nonErasedCatBody->getFile());

      ConcatInputSection *listSec = make<ConcatInputSection>(
          *infoCategoryWriter.catListInfo.inputSection, bodyData,
          infoCategoryWriter.catListInfo.align);
      listSec->parent = infoCategoryWriter.catListInfo.outputSection;
      listSec->live = true;

      std::string slotSymName = "<__objc_catlist slot for category ";
      slotSymName += nonErasedCatBody->getName();
      slotSymName += ">";

      Defined *catListSlotSym = make<Defined>(
          newStringData(slotSymName.c_str()), /*file=*/objFile, listSec,
          /*value=*/0, bodyData.size(),
          /*isWeakDef=*/false, /*isExternal=*/false, /*isPrivateExtern=*/false,
          /*includeInSymtab=*/false, /*isReferencedDynamically=*/false,
          /*noDeadStrip=*/false, /*isWeakDefCanBeHidden=*/false);

      catListSlotSym->used = true;
      objFile->symbols.push_back(catListSlotSym);
      addInputSection(listSec);

      // Now link the category body into the newly created slot
      createSymbolReference(catListSlotSym, nonErasedCatBody, 0,
                            infoCategoryWriter.catListInfo.relocTemplate);
    }
  }
}

void ObjcCategoryMerger::eraseISec(ConcatInputSection *isec) {
  isec->live = false;
  for (auto &sym : isec->symbols)
    sym->used = false;
}

// This fully erases the merged categories, including their body, their names,
// their method/protocol/prop lists and the __objc_catlist entries that link to
// them.
void ObjcCategoryMerger::eraseMergedCategories() {
  // Map of InputSection to a set of offsets of the categories that were merged
  MapVector<ConcatInputSection *, std::set<uint64_t>> catListToErasedOffsets;

  for (auto &mapEntry : categoryMap) {
    for (InfoInputCategory &catInfo : mapEntry.second) {
      if (catInfo.wasMerged) {
        eraseISec(catInfo.catListIsec);
        catListToErasedOffsets[catInfo.catListIsec].insert(
            catInfo.offCatListIsec);
      }
    }
  }

  // If there were categories that we did not erase, we need to generate a new
  // __objc_catList that contains only the un-merged categories, and get rid of
  // the references to the ones we merged.
  generateCatListForNonErasedCategories(catListToErasedOffsets);

  // Erase the old method lists & names of the categories that were merged
  for (auto &mapEntry : categoryMap) {
    for (InfoInputCategory &catInfo : mapEntry.second) {
      if (!catInfo.wasMerged)
        continue;

      eraseISec(catInfo.catBodyIsec);

      // We can't erase 'catLayout.nameOffset' for either Swift or ObjC
      //   categories because the name will sometimes also be used for other
      //   purposes.
      // For Swift, see usages of 'l_.str.11.SimpleClass' in
      //   objc-category-merging-swift.s
      // For ObjC, see usages of 'l_OBJC_CLASS_NAME_.1' in
      //   objc-category-merging-erase-objc-name-test.s
      // TODO: handle the above in a smarter way

      tryEraseDefinedAtIsecOffset(catInfo.catBodyIsec,
                                  catLayout.instanceMethodsOffset);
      tryEraseDefinedAtIsecOffset(catInfo.catBodyIsec,
                                  catLayout.classMethodsOffset);
      tryEraseDefinedAtIsecOffset(catInfo.catBodyIsec,
                                  catLayout.protocolsOffset);
      tryEraseDefinedAtIsecOffset(catInfo.catBodyIsec,
                                  catLayout.classPropsOffset);
      tryEraseDefinedAtIsecOffset(catInfo.catBodyIsec,
                                  catLayout.instancePropsOffset);
    }
  }
}

void ObjcCategoryMerger::doMerge() {
  collectAndValidateCategoriesData();

  for (auto &[baseClass, catInfos] : categoryMap) {
    if (auto *baseClassDef = dyn_cast<Defined>(baseClass)) {
      // Merge all categories into the base class
      mergeCategoriesIntoBaseClass(baseClassDef, catInfos);
    } else if (catInfos.size() > 1) {
      // Merge all categories into a new, single category
      mergeCategoriesIntoSingleCategory(catInfos);
    }
  }

  // Erase all categories that were merged
  eraseMergedCategories();
}

void ObjcCategoryMerger::doCleanup() { generatedSectionData.clear(); }

StringRef ObjcCategoryMerger::newStringData(const char *str) {
  uint32_t len = strlen(str);
  uint32_t bufSize = len + 1;
  SmallVector<uint8_t> &data = newSectionData(bufSize);
  char *strData = reinterpret_cast<char *>(data.data());
  // Copy the string chars and null-terminator
  memcpy(strData, str, bufSize);
  return StringRef(strData, len);
}

SmallVector<uint8_t> &ObjcCategoryMerger::newSectionData(uint32_t size) {
  generatedSectionData.push_back(
      std::make_unique<SmallVector<uint8_t>>(size, 0));
  return *generatedSectionData.back();
}

} // namespace

void objc::mergeCategories() {
  TimeTraceScope timeScope("ObjcCategoryMerger");

  ObjcCategoryMerger merger(inputSections);
  merger.doMerge();
}

void objc::doCleanup() { ObjcCategoryMerger::doCleanup(); }

ObjcCategoryMerger::SourceLanguage
ObjcCategoryMerger::getClassSymSourceLang(const Defined *classSym) {
  if (classSym->getName().starts_with(objc::symbol_names::swift_objc_klass))
    return SourceLanguage::Swift;

  // If the symbol name matches the ObjC prefix, we don't necessarely know this
  // comes from ObjC, since Swift creates ObjC-like alias symbols for some Swift
  // classes. Ex:
  //  .globl	_OBJC_CLASS_$__TtC11MyTestClass11MyTestClass
  //  .private_extern _OBJC_CLASS_$__TtC11MyTestClass11MyTestClass
  //  .set _OBJC_CLASS_$__TtC11MyTestClass11MyTestClass, _$s11MyTestClassAACN
  //
  // So we scan for symbols with the same address and check for the Swift class
  if (classSym->getName().starts_with(objc::symbol_names::klass)) {
    for (auto &sym : classSym->originalIsec->symbols)
      if (sym->value == classSym->value)
        if (sym->getName().starts_with(objc::symbol_names::swift_objc_klass))
          return SourceLanguage::Swift;
    return SourceLanguage::ObjC;
  }

  llvm_unreachable("Unexpected class symbol name during category merging");
}
void ObjcCategoryMerger::mergeCategoriesIntoBaseClass(
    const Defined *baseClass, std::vector<InfoInputCategory> &categories) {
  assert(categories.size() >= 1 && "Expected at least one category to merge");

  // Collect all the info from the categories
  ClassExtensionInfo extInfo(catLayout);
  extInfo.baseClass = baseClass;
  extInfo.baseClassSourceLanguage = getClassSymSourceLang(baseClass);

  for (auto &catInfo : categories) {
    parseCatInfoToExtInfo(catInfo, extInfo);
  }

  // Get metadata for the base class
  Defined *metaRo = getClassRo(baseClass, /*getMetaRo=*/true);
  ConcatInputSection *metaIsec = dyn_cast<ConcatInputSection>(metaRo->isec());
  Defined *classRo = getClassRo(baseClass, /*getMetaRo=*/false);
  ConcatInputSection *classIsec = dyn_cast<ConcatInputSection>(classRo->isec());

  // Now collect the info from the base class from the various lists in the
  // class metadata

  // Protocol lists are a special case - the same protocol list is in classRo
  // and metaRo, so we only need to parse it once
  parseProtocolListInfo(classIsec, roClassLayout.baseProtocolsOffset,
                        extInfo.protocols, extInfo.baseClassSourceLanguage);

  // Check that the classRo and metaRo protocol lists are identical
  assert(parseProtocolListInfo(classIsec, roClassLayout.baseProtocolsOffset,
                               extInfo.baseClassSourceLanguage) ==
             parseProtocolListInfo(metaIsec, roClassLayout.baseProtocolsOffset,
                                   extInfo.baseClassSourceLanguage) &&
         "Category merger expects classRo and metaRo to have the same protocol "
         "list");

  parsePointerListInfo(metaIsec, roClassLayout.baseMethodsOffset,
                       extInfo.classMethods);
  parsePointerListInfo(classIsec, roClassLayout.baseMethodsOffset,
                       extInfo.instanceMethods);

  parsePointerListInfo(metaIsec, roClassLayout.basePropertiesOffset,
                       extInfo.classProps);
  parsePointerListInfo(classIsec, roClassLayout.basePropertiesOffset,
                       extInfo.instanceProps);

  // Erase the old lists - these will be generated and replaced
  eraseSymbolAtIsecOffset(metaIsec, roClassLayout.baseMethodsOffset);
  eraseSymbolAtIsecOffset(metaIsec, roClassLayout.baseProtocolsOffset);
  eraseSymbolAtIsecOffset(metaIsec, roClassLayout.basePropertiesOffset);
  eraseSymbolAtIsecOffset(classIsec, roClassLayout.baseMethodsOffset);
  eraseSymbolAtIsecOffset(classIsec, roClassLayout.baseProtocolsOffset);
  eraseSymbolAtIsecOffset(classIsec, roClassLayout.basePropertiesOffset);

  // Emit the newly merged lists - first into the meta RO then into the class RO
  // First we emit and link the protocol list into the meta RO. Then we link it
  // in the classRo as well (they're supposed to be identical)
  if (Defined *protoListSym =
          emitAndLinkProtocolList(metaRo, roClassLayout.baseProtocolsOffset,
                                  extInfo, extInfo.protocols)) {
    createSymbolReference(classRo, protoListSym,
                          roClassLayout.baseProtocolsOffset,
                          infoCategoryWriter.catBodyInfo.relocTemplate);
  }

  emitAndLinkPointerList(metaRo, roClassLayout.baseMethodsOffset, extInfo,
                         extInfo.classMethods);
  emitAndLinkPointerList(classRo, roClassLayout.baseMethodsOffset, extInfo,
                         extInfo.instanceMethods);

  emitAndLinkPointerList(metaRo, roClassLayout.basePropertiesOffset, extInfo,
                         extInfo.classProps);

  emitAndLinkPointerList(classRo, roClassLayout.basePropertiesOffset, extInfo,
                         extInfo.instanceProps);

  // Mark all the categories as merged - this will be used to erase them later
  for (auto &catInfo : categories)
    catInfo.wasMerged = true;
}

// Erase the symbol at a given offset in an InputSection
void ObjcCategoryMerger::eraseSymbolAtIsecOffset(ConcatInputSection *isec,
                                                 uint32_t offset) {
  Defined *sym = tryGetDefinedAtIsecOffset(isec, offset);
  if (!sym)
    return;

  // Remove the symbol from isec->symbols
  assert(isa<Defined>(sym) && "Can only erase a Defined");
  llvm::erase(isec->symbols, sym);

  // Remove the relocs that refer to this symbol
  auto removeAtOff = [offset](Reloc const &r) { return r.offset == offset; };
  llvm::erase_if(isec->relocs, removeAtOff);

  // Now, if the symbol fully occupies a ConcatInputSection, we can also erase
  // the whole ConcatInputSection
  if (ConcatInputSection *cisec = dyn_cast<ConcatInputSection>(sym->isec()))
    if (cisec->data.size() == sym->size)
      eraseISec(cisec);
}
