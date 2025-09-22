//===- llvm/lib/CodeGen/AsmPrinter/CodeViewDebug.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing Microsoft CodeView debug info.
//
//===----------------------------------------------------------------------===//

#include "CodeViewDebug.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/DebugInfo/CodeView/ContinuationRecordBuilder.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeTableCollection.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbackPipeline.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <limits>

using namespace llvm;
using namespace llvm::codeview;

namespace {
class CVMCAdapter : public CodeViewRecordStreamer {
public:
  CVMCAdapter(MCStreamer &OS, TypeCollection &TypeTable)
      : OS(&OS), TypeTable(TypeTable) {}

  void emitBytes(StringRef Data) override { OS->emitBytes(Data); }

  void emitIntValue(uint64_t Value, unsigned Size) override {
    OS->emitIntValueInHex(Value, Size);
  }

  void emitBinaryData(StringRef Data) override { OS->emitBinaryData(Data); }

  void AddComment(const Twine &T) override { OS->AddComment(T); }

  void AddRawComment(const Twine &T) override { OS->emitRawComment(T); }

  bool isVerboseAsm() override { return OS->isVerboseAsm(); }

  std::string getTypeName(TypeIndex TI) override {
    std::string TypeName;
    if (!TI.isNoneType()) {
      if (TI.isSimple())
        TypeName = std::string(TypeIndex::simpleTypeName(TI));
      else
        TypeName = std::string(TypeTable.getTypeName(TI));
    }
    return TypeName;
  }

private:
  MCStreamer *OS = nullptr;
  TypeCollection &TypeTable;
};
} // namespace

static CPUType mapArchToCVCPUType(Triple::ArchType Type) {
  switch (Type) {
  case Triple::ArchType::x86:
    return CPUType::Pentium3;
  case Triple::ArchType::x86_64:
    return CPUType::X64;
  case Triple::ArchType::thumb:
    // LLVM currently doesn't support Windows CE and so thumb
    // here is indiscriminately mapped to ARMNT specifically.
    return CPUType::ARMNT;
  case Triple::ArchType::aarch64:
    return CPUType::ARM64;
  default:
    report_fatal_error("target architecture doesn't map to a CodeView CPUType");
  }
}

CodeViewDebug::CodeViewDebug(AsmPrinter *AP)
    : DebugHandlerBase(AP), OS(*Asm->OutStreamer), TypeTable(Allocator) {}

StringRef CodeViewDebug::getFullFilepath(const DIFile *File) {
  std::string &Filepath = FileToFilepathMap[File];
  if (!Filepath.empty())
    return Filepath;

  StringRef Dir = File->getDirectory(), Filename = File->getFilename();

  // If this is a Unix-style path, just use it as is. Don't try to canonicalize
  // it textually because one of the path components could be a symlink.
  if (Dir.starts_with("/") || Filename.starts_with("/")) {
    if (llvm::sys::path::is_absolute(Filename, llvm::sys::path::Style::posix))
      return Filename;
    Filepath = std::string(Dir);
    if (Dir.back() != '/')
      Filepath += '/';
    Filepath += Filename;
    return Filepath;
  }

  // Clang emits directory and relative filename info into the IR, but CodeView
  // operates on full paths.  We could change Clang to emit full paths too, but
  // that would increase the IR size and probably not needed for other users.
  // For now, just concatenate and canonicalize the path here.
  if (Filename.find(':') == 1)
    Filepath = std::string(Filename);
  else
    Filepath = (Dir + "\\" + Filename).str();

  // Canonicalize the path.  We have to do it textually because we may no longer
  // have access the file in the filesystem.
  // First, replace all slashes with backslashes.
  std::replace(Filepath.begin(), Filepath.end(), '/', '\\');

  // Remove all "\.\" with "\".
  size_t Cursor = 0;
  while ((Cursor = Filepath.find("\\.\\", Cursor)) != std::string::npos)
    Filepath.erase(Cursor, 2);

  // Replace all "\XXX\..\" with "\".  Don't try too hard though as the original
  // path should be well-formatted, e.g. start with a drive letter, etc.
  Cursor = 0;
  while ((Cursor = Filepath.find("\\..\\", Cursor)) != std::string::npos) {
    // Something's wrong if the path starts with "\..\", abort.
    if (Cursor == 0)
      break;

    size_t PrevSlash = Filepath.rfind('\\', Cursor - 1);
    if (PrevSlash == std::string::npos)
      // Something's wrong, abort.
      break;

    Filepath.erase(PrevSlash, Cursor + 3 - PrevSlash);
    // The next ".." might be following the one we've just erased.
    Cursor = PrevSlash;
  }

  // Remove all duplicate backslashes.
  Cursor = 0;
  while ((Cursor = Filepath.find("\\\\", Cursor)) != std::string::npos)
    Filepath.erase(Cursor, 1);

  return Filepath;
}

unsigned CodeViewDebug::maybeRecordFile(const DIFile *F) {
  StringRef FullPath = getFullFilepath(F);
  unsigned NextId = FileIdMap.size() + 1;
  auto Insertion = FileIdMap.insert(std::make_pair(FullPath, NextId));
  if (Insertion.second) {
    // We have to compute the full filepath and emit a .cv_file directive.
    ArrayRef<uint8_t> ChecksumAsBytes;
    FileChecksumKind CSKind = FileChecksumKind::None;
    if (F->getChecksum()) {
      std::string Checksum = fromHex(F->getChecksum()->Value);
      void *CKMem = OS.getContext().allocate(Checksum.size(), 1);
      memcpy(CKMem, Checksum.data(), Checksum.size());
      ChecksumAsBytes = ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(CKMem), Checksum.size());
      switch (F->getChecksum()->Kind) {
      case DIFile::CSK_MD5:
        CSKind = FileChecksumKind::MD5;
        break;
      case DIFile::CSK_SHA1:
        CSKind = FileChecksumKind::SHA1;
        break;
      case DIFile::CSK_SHA256:
        CSKind = FileChecksumKind::SHA256;
        break;
      }
    }
    bool Success = OS.emitCVFileDirective(NextId, FullPath, ChecksumAsBytes,
                                          static_cast<unsigned>(CSKind));
    (void)Success;
    assert(Success && ".cv_file directive failed");
  }
  return Insertion.first->second;
}

CodeViewDebug::InlineSite &
CodeViewDebug::getInlineSite(const DILocation *InlinedAt,
                             const DISubprogram *Inlinee) {
  auto SiteInsertion = CurFn->InlineSites.insert({InlinedAt, InlineSite()});
  InlineSite *Site = &SiteInsertion.first->second;
  if (SiteInsertion.second) {
    unsigned ParentFuncId = CurFn->FuncId;
    if (const DILocation *OuterIA = InlinedAt->getInlinedAt())
      ParentFuncId =
          getInlineSite(OuterIA, InlinedAt->getScope()->getSubprogram())
              .SiteFuncId;

    Site->SiteFuncId = NextFuncId++;
    OS.emitCVInlineSiteIdDirective(
        Site->SiteFuncId, ParentFuncId, maybeRecordFile(InlinedAt->getFile()),
        InlinedAt->getLine(), InlinedAt->getColumn(), SMLoc());
    Site->Inlinee = Inlinee;
    InlinedSubprograms.insert(Inlinee);
    auto InlineeIdx = getFuncIdForSubprogram(Inlinee);

    if (InlinedAt->getInlinedAt() == nullptr)
      CurFn->Inlinees.insert(InlineeIdx);
  }
  return *Site;
}

static StringRef getPrettyScopeName(const DIScope *Scope) {
  StringRef ScopeName = Scope->getName();
  if (!ScopeName.empty())
    return ScopeName;

  switch (Scope->getTag()) {
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
    return "<unnamed-tag>";
  case dwarf::DW_TAG_namespace:
    return "`anonymous namespace'";
  default:
    return StringRef();
  }
}

const DISubprogram *CodeViewDebug::collectParentScopeNames(
    const DIScope *Scope, SmallVectorImpl<StringRef> &QualifiedNameComponents) {
  const DISubprogram *ClosestSubprogram = nullptr;
  while (Scope != nullptr) {
    if (ClosestSubprogram == nullptr)
      ClosestSubprogram = dyn_cast<DISubprogram>(Scope);

    // If a type appears in a scope chain, make sure it gets emitted. The
    // frontend will be responsible for deciding if this should be a forward
    // declaration or a complete type.
    if (const auto *Ty = dyn_cast<DICompositeType>(Scope))
      DeferredCompleteTypes.push_back(Ty);

    StringRef ScopeName = getPrettyScopeName(Scope);
    if (!ScopeName.empty())
      QualifiedNameComponents.push_back(ScopeName);
    Scope = Scope->getScope();
  }
  return ClosestSubprogram;
}

static std::string formatNestedName(ArrayRef<StringRef> QualifiedNameComponents,
                                    StringRef TypeName) {
  std::string FullyQualifiedName;
  for (StringRef QualifiedNameComponent :
       llvm::reverse(QualifiedNameComponents)) {
    FullyQualifiedName.append(std::string(QualifiedNameComponent));
    FullyQualifiedName.append("::");
  }
  FullyQualifiedName.append(std::string(TypeName));
  return FullyQualifiedName;
}

struct CodeViewDebug::TypeLoweringScope {
  TypeLoweringScope(CodeViewDebug &CVD) : CVD(CVD) { ++CVD.TypeEmissionLevel; }
  ~TypeLoweringScope() {
    // Don't decrement TypeEmissionLevel until after emitting deferred types, so
    // inner TypeLoweringScopes don't attempt to emit deferred types.
    if (CVD.TypeEmissionLevel == 1)
      CVD.emitDeferredCompleteTypes();
    --CVD.TypeEmissionLevel;
  }
  CodeViewDebug &CVD;
};

std::string CodeViewDebug::getFullyQualifiedName(const DIScope *Scope,
                                                 StringRef Name) {
  // Ensure types in the scope chain are emitted as soon as possible.
  // This can create otherwise a situation where S_UDTs are emitted while
  // looping in emitDebugInfoForUDTs.
  TypeLoweringScope S(*this);
  SmallVector<StringRef, 5> QualifiedNameComponents;
  collectParentScopeNames(Scope, QualifiedNameComponents);
  return formatNestedName(QualifiedNameComponents, Name);
}

std::string CodeViewDebug::getFullyQualifiedName(const DIScope *Ty) {
  const DIScope *Scope = Ty->getScope();
  return getFullyQualifiedName(Scope, getPrettyScopeName(Ty));
}

TypeIndex CodeViewDebug::getScopeIndex(const DIScope *Scope) {
  // No scope means global scope and that uses the zero index.
  //
  // We also use zero index when the scope is a DISubprogram
  // to suppress the emission of LF_STRING_ID for the function,
  // which can trigger a link-time error with the linker in
  // VS2019 version 16.11.2 or newer.
  // Note, however, skipping the debug info emission for the DISubprogram
  // is a temporary fix. The root issue here is that we need to figure out
  // the proper way to encode a function nested in another function
  // (as introduced by the Fortran 'contains' keyword) in CodeView.
  if (!Scope || isa<DIFile>(Scope) || isa<DISubprogram>(Scope))
    return TypeIndex();

  assert(!isa<DIType>(Scope) && "shouldn't make a namespace scope for a type");

  // Check if we've already translated this scope.
  auto I = TypeIndices.find({Scope, nullptr});
  if (I != TypeIndices.end())
    return I->second;

  // Build the fully qualified name of the scope.
  std::string ScopeName = getFullyQualifiedName(Scope);
  StringIdRecord SID(TypeIndex(), ScopeName);
  auto TI = TypeTable.writeLeafType(SID);
  return recordTypeIndexForDINode(Scope, TI);
}

static StringRef removeTemplateArgs(StringRef Name) {
  // Remove template args from the display name. Assume that the template args
  // are the last thing in the name.
  if (Name.empty() || Name.back() != '>')
    return Name;

  int OpenBrackets = 0;
  for (int i = Name.size() - 1; i >= 0; --i) {
    if (Name[i] == '>')
      ++OpenBrackets;
    else if (Name[i] == '<') {
      --OpenBrackets;
      if (OpenBrackets == 0)
        return Name.substr(0, i);
    }
  }
  return Name;
}

TypeIndex CodeViewDebug::getFuncIdForSubprogram(const DISubprogram *SP) {
  assert(SP);

  // Check if we've already translated this subprogram.
  auto I = TypeIndices.find({SP, nullptr});
  if (I != TypeIndices.end())
    return I->second;

  // The display name includes function template arguments. Drop them to match
  // MSVC. We need to have the template arguments in the DISubprogram name
  // because they are used in other symbol records, such as S_GPROC32_IDs.
  StringRef DisplayName = removeTemplateArgs(SP->getName());

  const DIScope *Scope = SP->getScope();
  TypeIndex TI;
  if (const auto *Class = dyn_cast_or_null<DICompositeType>(Scope)) {
    // If the scope is a DICompositeType, then this must be a method. Member
    // function types take some special handling, and require access to the
    // subprogram.
    TypeIndex ClassType = getTypeIndex(Class);
    MemberFuncIdRecord MFuncId(ClassType, getMemberFunctionType(SP, Class),
                               DisplayName);
    TI = TypeTable.writeLeafType(MFuncId);
  } else {
    // Otherwise, this must be a free function.
    TypeIndex ParentScope = getScopeIndex(Scope);
    FuncIdRecord FuncId(ParentScope, getTypeIndex(SP->getType()), DisplayName);
    TI = TypeTable.writeLeafType(FuncId);
  }

  return recordTypeIndexForDINode(SP, TI);
}

static bool isNonTrivial(const DICompositeType *DCTy) {
  return ((DCTy->getFlags() & DINode::FlagNonTrivial) == DINode::FlagNonTrivial);
}

static FunctionOptions
getFunctionOptions(const DISubroutineType *Ty,
                   const DICompositeType *ClassTy = nullptr,
                   StringRef SPName = StringRef("")) {
  FunctionOptions FO = FunctionOptions::None;
  const DIType *ReturnTy = nullptr;
  if (auto TypeArray = Ty->getTypeArray()) {
    if (TypeArray.size())
      ReturnTy = TypeArray[0];
  }

  // Add CxxReturnUdt option to functions that return nontrivial record types
  // or methods that return record types.
  if (auto *ReturnDCTy = dyn_cast_or_null<DICompositeType>(ReturnTy))
    if (isNonTrivial(ReturnDCTy) || ClassTy)
      FO |= FunctionOptions::CxxReturnUdt;

  // DISubroutineType is unnamed. Use DISubprogram's i.e. SPName in comparison.
  if (ClassTy && isNonTrivial(ClassTy) && SPName == ClassTy->getName()) {
    FO |= FunctionOptions::Constructor;

  // TODO: put the FunctionOptions::ConstructorWithVirtualBases flag.

  }
  return FO;
}

TypeIndex CodeViewDebug::getMemberFunctionType(const DISubprogram *SP,
                                               const DICompositeType *Class) {
  // Always use the method declaration as the key for the function type. The
  // method declaration contains the this adjustment.
  if (SP->getDeclaration())
    SP = SP->getDeclaration();
  assert(!SP->getDeclaration() && "should use declaration as key");

  // Key the MemberFunctionRecord into the map as {SP, Class}. It won't collide
  // with the MemberFuncIdRecord, which is keyed in as {SP, nullptr}.
  auto I = TypeIndices.find({SP, Class});
  if (I != TypeIndices.end())
    return I->second;

  // Make sure complete type info for the class is emitted *after* the member
  // function type, as the complete class type is likely to reference this
  // member function type.
  TypeLoweringScope S(*this);
  const bool IsStaticMethod = (SP->getFlags() & DINode::FlagStaticMember) != 0;

  FunctionOptions FO = getFunctionOptions(SP->getType(), Class, SP->getName());
  TypeIndex TI = lowerTypeMemberFunction(
      SP->getType(), Class, SP->getThisAdjustment(), IsStaticMethod, FO);
  return recordTypeIndexForDINode(SP, TI, Class);
}

TypeIndex CodeViewDebug::recordTypeIndexForDINode(const DINode *Node,
                                                  TypeIndex TI,
                                                  const DIType *ClassTy) {
  auto InsertResult = TypeIndices.insert({{Node, ClassTy}, TI});
  (void)InsertResult;
  assert(InsertResult.second && "DINode was already assigned a type index");
  return TI;
}

unsigned CodeViewDebug::getPointerSizeInBytes() {
  return MMI->getModule()->getDataLayout().getPointerSizeInBits() / 8;
}

void CodeViewDebug::recordLocalVariable(LocalVariable &&Var,
                                        const LexicalScope *LS) {
  if (const DILocation *InlinedAt = LS->getInlinedAt()) {
    // This variable was inlined. Associate it with the InlineSite.
    const DISubprogram *Inlinee = Var.DIVar->getScope()->getSubprogram();
    InlineSite &Site = getInlineSite(InlinedAt, Inlinee);
    Site.InlinedLocals.emplace_back(std::move(Var));
  } else {
    // This variable goes into the corresponding lexical scope.
    ScopeVariables[LS].emplace_back(std::move(Var));
  }
}

static void addLocIfNotPresent(SmallVectorImpl<const DILocation *> &Locs,
                               const DILocation *Loc) {
  if (!llvm::is_contained(Locs, Loc))
    Locs.push_back(Loc);
}

void CodeViewDebug::maybeRecordLocation(const DebugLoc &DL,
                                        const MachineFunction *MF) {
  // Skip this instruction if it has the same location as the previous one.
  if (!DL || DL == PrevInstLoc)
    return;

  const DIScope *Scope = DL->getScope();
  if (!Scope)
    return;

  // Skip this line if it is longer than the maximum we can record.
  LineInfo LI(DL.getLine(), DL.getLine(), /*IsStatement=*/true);
  if (LI.getStartLine() != DL.getLine() || LI.isAlwaysStepInto() ||
      LI.isNeverStepInto())
    return;

  ColumnInfo CI(DL.getCol(), /*EndColumn=*/0);
  if (CI.getStartColumn() != DL.getCol())
    return;

  if (!CurFn->HaveLineInfo)
    CurFn->HaveLineInfo = true;
  unsigned FileId = 0;
  if (PrevInstLoc.get() && PrevInstLoc->getFile() == DL->getFile())
    FileId = CurFn->LastFileId;
  else
    FileId = CurFn->LastFileId = maybeRecordFile(DL->getFile());
  PrevInstLoc = DL;

  unsigned FuncId = CurFn->FuncId;
  if (const DILocation *SiteLoc = DL->getInlinedAt()) {
    const DILocation *Loc = DL.get();

    // If this location was actually inlined from somewhere else, give it the ID
    // of the inline call site.
    FuncId =
        getInlineSite(SiteLoc, Loc->getScope()->getSubprogram()).SiteFuncId;

    // Ensure we have links in the tree of inline call sites.
    bool FirstLoc = true;
    while ((SiteLoc = Loc->getInlinedAt())) {
      InlineSite &Site =
          getInlineSite(SiteLoc, Loc->getScope()->getSubprogram());
      if (!FirstLoc)
        addLocIfNotPresent(Site.ChildSites, Loc);
      FirstLoc = false;
      Loc = SiteLoc;
    }
    addLocIfNotPresent(CurFn->ChildSites, Loc);
  }

  OS.emitCVLocDirective(FuncId, FileId, DL.getLine(), DL.getCol(),
                        /*PrologueEnd=*/false, /*IsStmt=*/false,
                        DL->getFilename(), SMLoc());
}

void CodeViewDebug::emitCodeViewMagicVersion() {
  OS.emitValueToAlignment(Align(4));
  OS.AddComment("Debug section magic");
  OS.emitInt32(COFF::DEBUG_SECTION_MAGIC);
}

static SourceLanguage MapDWLangToCVLang(unsigned DWLang) {
  switch (DWLang) {
  case dwarf::DW_LANG_C:
  case dwarf::DW_LANG_C89:
  case dwarf::DW_LANG_C99:
  case dwarf::DW_LANG_C11:
    return SourceLanguage::C;
  case dwarf::DW_LANG_C_plus_plus:
  case dwarf::DW_LANG_C_plus_plus_03:
  case dwarf::DW_LANG_C_plus_plus_11:
  case dwarf::DW_LANG_C_plus_plus_14:
    return SourceLanguage::Cpp;
  case dwarf::DW_LANG_Fortran77:
  case dwarf::DW_LANG_Fortran90:
  case dwarf::DW_LANG_Fortran95:
  case dwarf::DW_LANG_Fortran03:
  case dwarf::DW_LANG_Fortran08:
    return SourceLanguage::Fortran;
  case dwarf::DW_LANG_Pascal83:
    return SourceLanguage::Pascal;
  case dwarf::DW_LANG_Cobol74:
  case dwarf::DW_LANG_Cobol85:
    return SourceLanguage::Cobol;
  case dwarf::DW_LANG_Java:
    return SourceLanguage::Java;
  case dwarf::DW_LANG_D:
    return SourceLanguage::D;
  case dwarf::DW_LANG_Swift:
    return SourceLanguage::Swift;
  case dwarf::DW_LANG_Rust:
    return SourceLanguage::Rust;
  case dwarf::DW_LANG_ObjC:
    return SourceLanguage::ObjC;
  case dwarf::DW_LANG_ObjC_plus_plus:
    return SourceLanguage::ObjCpp;
  default:
    // There's no CodeView representation for this language, and CV doesn't
    // have an "unknown" option for the language field, so we'll use MASM,
    // as it's very low level.
    return SourceLanguage::Masm;
  }
}

void CodeViewDebug::beginModule(Module *M) {
  // If module doesn't have named metadata anchors or COFF debug section
  // is not available, skip any debug info related stuff.
  if (!MMI->hasDebugInfo() ||
      !Asm->getObjFileLowering().getCOFFDebugSymbolsSection()) {
    Asm = nullptr;
    return;
  }

  TheCPU = mapArchToCVCPUType(Triple(M->getTargetTriple()).getArch());

  // Get the current source language.
  const MDNode *Node = *M->debug_compile_units_begin();
  const auto *CU = cast<DICompileUnit>(Node);

  CurrentSourceLanguage = MapDWLangToCVLang(CU->getSourceLanguage());

  collectGlobalVariableInfo();

  // Check if we should emit type record hashes.
  ConstantInt *GH =
      mdconst::extract_or_null<ConstantInt>(M->getModuleFlag("CodeViewGHash"));
  EmitDebugGlobalHashes = GH && !GH->isZero();
}

void CodeViewDebug::endModule() {
  if (!Asm || !MMI->hasDebugInfo())
    return;

  // The COFF .debug$S section consists of several subsections, each starting
  // with a 4-byte control code (e.g. 0xF1, 0xF2, etc) and then a 4-byte length
  // of the payload followed by the payload itself.  The subsections are 4-byte
  // aligned.

  // Use the generic .debug$S section, and make a subsection for all the inlined
  // subprograms.
  switchToDebugSectionForSymbol(nullptr);

  MCSymbol *CompilerInfo = beginCVSubsection(DebugSubsectionKind::Symbols);
  emitObjName();
  emitCompilerInformation();
  endCVSubsection(CompilerInfo);

  emitInlineeLinesSubsection();

  // Emit per-function debug information.
  for (auto &P : FnDebugInfo)
    if (!P.first->isDeclarationForLinker())
      emitDebugInfoForFunction(P.first, *P.second);

  // Get types used by globals without emitting anything.
  // This is meant to collect all static const data members so they can be
  // emitted as globals.
  collectDebugInfoForGlobals();

  // Emit retained types.
  emitDebugInfoForRetainedTypes();

  // Emit global variable debug information.
  setCurrentSubprogram(nullptr);
  emitDebugInfoForGlobals();

  // Switch back to the generic .debug$S section after potentially processing
  // comdat symbol sections.
  switchToDebugSectionForSymbol(nullptr);

  // Emit UDT records for any types used by global variables.
  if (!GlobalUDTs.empty()) {
    MCSymbol *SymbolsEnd = beginCVSubsection(DebugSubsectionKind::Symbols);
    emitDebugInfoForUDTs(GlobalUDTs);
    endCVSubsection(SymbolsEnd);
  }

  // This subsection holds a file index to offset in string table table.
  OS.AddComment("File index to string table offset subsection");
  OS.emitCVFileChecksumsDirective();

  // This subsection holds the string table.
  OS.AddComment("String table");
  OS.emitCVStringTableDirective();

  // Emit S_BUILDINFO, which points to LF_BUILDINFO. Put this in its own symbol
  // subsection in the generic .debug$S section at the end. There is no
  // particular reason for this ordering other than to match MSVC.
  emitBuildInfo();

  // Emit type information and hashes last, so that any types we translate while
  // emitting function info are included.
  emitTypeInformation();

  if (EmitDebugGlobalHashes)
    emitTypeGlobalHashes();

  clear();
}

static void
emitNullTerminatedSymbolName(MCStreamer &OS, StringRef S,
                             unsigned MaxFixedRecordLength = 0xF00) {
  // The maximum CV record length is 0xFF00. Most of the strings we emit appear
  // after a fixed length portion of the record. The fixed length portion should
  // always be less than 0xF00 (3840) bytes, so truncate the string so that the
  // overall record size is less than the maximum allowed.
  SmallString<32> NullTerminatedString(
      S.take_front(MaxRecordLength - MaxFixedRecordLength - 1));
  NullTerminatedString.push_back('\0');
  OS.emitBytes(NullTerminatedString);
}

void CodeViewDebug::emitTypeInformation() {
  if (TypeTable.empty())
    return;

  // Start the .debug$T or .debug$P section with 0x4.
  OS.switchSection(Asm->getObjFileLowering().getCOFFDebugTypesSection());
  emitCodeViewMagicVersion();

  TypeTableCollection Table(TypeTable.records());
  TypeVisitorCallbackPipeline Pipeline;

  // To emit type record using Codeview MCStreamer adapter
  CVMCAdapter CVMCOS(OS, Table);
  TypeRecordMapping typeMapping(CVMCOS);
  Pipeline.addCallbackToPipeline(typeMapping);

  std::optional<TypeIndex> B = Table.getFirst();
  while (B) {
    // This will fail if the record data is invalid.
    CVType Record = Table.getType(*B);

    Error E = codeview::visitTypeRecord(Record, *B, Pipeline);

    if (E) {
      logAllUnhandledErrors(std::move(E), errs(), "error: ");
      llvm_unreachable("produced malformed type record");
    }

    B = Table.getNext(*B);
  }
}

void CodeViewDebug::emitTypeGlobalHashes() {
  if (TypeTable.empty())
    return;

  // Start the .debug$H section with the version and hash algorithm, currently
  // hardcoded to version 0, SHA1.
  OS.switchSection(Asm->getObjFileLowering().getCOFFGlobalTypeHashesSection());

  OS.emitValueToAlignment(Align(4));
  OS.AddComment("Magic");
  OS.emitInt32(COFF::DEBUG_HASHES_SECTION_MAGIC);
  OS.AddComment("Section Version");
  OS.emitInt16(0);
  OS.AddComment("Hash Algorithm");
  OS.emitInt16(uint16_t(GlobalTypeHashAlg::BLAKE3));

  TypeIndex TI(TypeIndex::FirstNonSimpleIndex);
  for (const auto &GHR : TypeTable.hashes()) {
    if (OS.isVerboseAsm()) {
      // Emit an EOL-comment describing which TypeIndex this hash corresponds
      // to, as well as the stringified SHA1 hash.
      SmallString<32> Comment;
      raw_svector_ostream CommentOS(Comment);
      CommentOS << formatv("{0:X+} [{1}]", TI.getIndex(), GHR);
      OS.AddComment(Comment);
      ++TI;
    }
    assert(GHR.Hash.size() == 8);
    StringRef S(reinterpret_cast<const char *>(GHR.Hash.data()),
                GHR.Hash.size());
    OS.emitBinaryData(S);
  }
}

void CodeViewDebug::emitObjName() {
  MCSymbol *CompilerEnd = beginSymbolRecord(SymbolKind::S_OBJNAME);

  StringRef PathRef(Asm->TM.Options.ObjectFilenameForDebug);
  llvm::SmallString<256> PathStore(PathRef);

  if (PathRef.empty() || PathRef == "-") {
    // Don't emit the filename if we're writing to stdout or to /dev/null.
    PathRef = {};
  } else {
    PathRef = PathStore;
  }

  OS.AddComment("Signature");
  OS.emitIntValue(0, 4);

  OS.AddComment("Object name");
  emitNullTerminatedSymbolName(OS, PathRef);

  endSymbolRecord(CompilerEnd);
}

namespace {
struct Version {
  int Part[4];
};
} // end anonymous namespace

// Takes a StringRef like "clang 4.0.0.0 (other nonsense 123)" and parses out
// the version number.
static Version parseVersion(StringRef Name) {
  Version V = {{0}};
  int N = 0;
  for (const char C : Name) {
    if (isdigit(C)) {
      V.Part[N] *= 10;
      V.Part[N] += C - '0';
      V.Part[N] =
          std::min<int>(V.Part[N], std::numeric_limits<uint16_t>::max());
    } else if (C == '.') {
      ++N;
      if (N >= 4)
        return V;
    } else if (N > 0)
      return V;
  }
  return V;
}

void CodeViewDebug::emitCompilerInformation() {
  MCSymbol *CompilerEnd = beginSymbolRecord(SymbolKind::S_COMPILE3);
  uint32_t Flags = 0;

  // The low byte of the flags indicates the source language.
  Flags = CurrentSourceLanguage;
  // TODO:  Figure out which other flags need to be set.
  if (MMI->getModule()->getProfileSummary(/*IsCS*/ false) != nullptr) {
    Flags |= static_cast<uint32_t>(CompileSym3Flags::PGO);
  }
  using ArchType = llvm::Triple::ArchType;
  ArchType Arch = Triple(MMI->getModule()->getTargetTriple()).getArch();
  if (Asm->TM.Options.Hotpatch || Arch == ArchType::thumb ||
      Arch == ArchType::aarch64) {
    Flags |= static_cast<uint32_t>(CompileSym3Flags::HotPatch);
  }

  OS.AddComment("Flags and language");
  OS.emitInt32(Flags);

  OS.AddComment("CPUType");
  OS.emitInt16(static_cast<uint64_t>(TheCPU));

  NamedMDNode *CUs = MMI->getModule()->getNamedMetadata("llvm.dbg.cu");
  const MDNode *Node = *CUs->operands().begin();
  const auto *CU = cast<DICompileUnit>(Node);

  StringRef CompilerVersion = CU->getProducer();
  Version FrontVer = parseVersion(CompilerVersion);
  OS.AddComment("Frontend version");
  for (int N : FrontVer.Part) {
    OS.emitInt16(N);
  }

  // Some Microsoft tools, like Binscope, expect a backend version number of at
  // least 8.something, so we'll coerce the LLVM version into a form that
  // guarantees it'll be big enough without really lying about the version.
  int Major = 1000 * LLVM_VERSION_MAJOR +
              10 * LLVM_VERSION_MINOR +
              LLVM_VERSION_PATCH;
  // Clamp it for builds that use unusually large version numbers.
  Major = std::min<int>(Major, std::numeric_limits<uint16_t>::max());
  Version BackVer = {{ Major, 0, 0, 0 }};
  OS.AddComment("Backend version");
  for (int N : BackVer.Part)
    OS.emitInt16(N);

  OS.AddComment("Null-terminated compiler version string");
  emitNullTerminatedSymbolName(OS, CompilerVersion);

  endSymbolRecord(CompilerEnd);
}

static TypeIndex getStringIdTypeIdx(GlobalTypeTableBuilder &TypeTable,
                                    StringRef S) {
  StringIdRecord SIR(TypeIndex(0x0), S);
  return TypeTable.writeLeafType(SIR);
}

static std::string flattenCommandLine(ArrayRef<std::string> Args,
                                      StringRef MainFilename) {
  std::string FlatCmdLine;
  raw_string_ostream OS(FlatCmdLine);
  bool PrintedOneArg = false;
  if (!StringRef(Args[0]).contains("-cc1")) {
    llvm::sys::printArg(OS, "-cc1", /*Quote=*/true);
    PrintedOneArg = true;
  }
  for (unsigned i = 0; i < Args.size(); i++) {
    StringRef Arg = Args[i];
    if (Arg.empty())
      continue;
    if (Arg == "-main-file-name" || Arg == "-o") {
      i++; // Skip this argument and next one.
      continue;
    }
    if (Arg.starts_with("-object-file-name") || Arg == MainFilename)
      continue;
    // Skip fmessage-length for reproduciability.
    if (Arg.starts_with("-fmessage-length"))
      continue;
    if (PrintedOneArg)
      OS << " ";
    llvm::sys::printArg(OS, Arg, /*Quote=*/true);
    PrintedOneArg = true;
  }
  OS.flush();
  return FlatCmdLine;
}

void CodeViewDebug::emitBuildInfo() {
  // First, make LF_BUILDINFO. It's a sequence of strings with various bits of
  // build info. The known prefix is:
  // - Absolute path of current directory
  // - Compiler path
  // - Main source file path, relative to CWD or absolute
  // - Type server PDB file
  // - Canonical compiler command line
  // If frontend and backend compilation are separated (think llc or LTO), it's
  // not clear if the compiler path should refer to the executable for the
  // frontend or the backend. Leave it blank for now.
  TypeIndex BuildInfoArgs[BuildInfoRecord::MaxArgs] = {};
  NamedMDNode *CUs = MMI->getModule()->getNamedMetadata("llvm.dbg.cu");
  const MDNode *Node = *CUs->operands().begin(); // FIXME: Multiple CUs.
  const auto *CU = cast<DICompileUnit>(Node);
  const DIFile *MainSourceFile = CU->getFile();
  BuildInfoArgs[BuildInfoRecord::CurrentDirectory] =
      getStringIdTypeIdx(TypeTable, MainSourceFile->getDirectory());
  BuildInfoArgs[BuildInfoRecord::SourceFile] =
      getStringIdTypeIdx(TypeTable, MainSourceFile->getFilename());
  // FIXME: PDB is intentionally blank unless we implement /Zi type servers.
  BuildInfoArgs[BuildInfoRecord::TypeServerPDB] =
      getStringIdTypeIdx(TypeTable, "");
  if (Asm->TM.Options.MCOptions.Argv0 != nullptr) {
    BuildInfoArgs[BuildInfoRecord::BuildTool] =
        getStringIdTypeIdx(TypeTable, Asm->TM.Options.MCOptions.Argv0);
    BuildInfoArgs[BuildInfoRecord::CommandLine] = getStringIdTypeIdx(
        TypeTable, flattenCommandLine(Asm->TM.Options.MCOptions.CommandLineArgs,
                                      MainSourceFile->getFilename()));
  }
  BuildInfoRecord BIR(BuildInfoArgs);
  TypeIndex BuildInfoIndex = TypeTable.writeLeafType(BIR);

  // Make a new .debug$S subsection for the S_BUILDINFO record, which points
  // from the module symbols into the type stream.
  MCSymbol *BISubsecEnd = beginCVSubsection(DebugSubsectionKind::Symbols);
  MCSymbol *BIEnd = beginSymbolRecord(SymbolKind::S_BUILDINFO);
  OS.AddComment("LF_BUILDINFO index");
  OS.emitInt32(BuildInfoIndex.getIndex());
  endSymbolRecord(BIEnd);
  endCVSubsection(BISubsecEnd);
}

void CodeViewDebug::emitInlineeLinesSubsection() {
  if (InlinedSubprograms.empty())
    return;

  OS.AddComment("Inlinee lines subsection");
  MCSymbol *InlineEnd = beginCVSubsection(DebugSubsectionKind::InlineeLines);

  // We emit the checksum info for files.  This is used by debuggers to
  // determine if a pdb matches the source before loading it.  Visual Studio,
  // for instance, will display a warning that the breakpoints are not valid if
  // the pdb does not match the source.
  OS.AddComment("Inlinee lines signature");
  OS.emitInt32(unsigned(InlineeLinesSignature::Normal));

  for (const DISubprogram *SP : InlinedSubprograms) {
    assert(TypeIndices.count({SP, nullptr}));
    TypeIndex InlineeIdx = TypeIndices[{SP, nullptr}];

    OS.addBlankLine();
    unsigned FileId = maybeRecordFile(SP->getFile());
    OS.AddComment("Inlined function " + SP->getName() + " starts at " +
                  SP->getFilename() + Twine(':') + Twine(SP->getLine()));
    OS.addBlankLine();
    OS.AddComment("Type index of inlined function");
    OS.emitInt32(InlineeIdx.getIndex());
    OS.AddComment("Offset into filechecksum table");
    OS.emitCVFileChecksumOffsetDirective(FileId);
    OS.AddComment("Starting line number");
    OS.emitInt32(SP->getLine());
  }

  endCVSubsection(InlineEnd);
}

void CodeViewDebug::emitInlinedCallSite(const FunctionInfo &FI,
                                        const DILocation *InlinedAt,
                                        const InlineSite &Site) {
  assert(TypeIndices.count({Site.Inlinee, nullptr}));
  TypeIndex InlineeIdx = TypeIndices[{Site.Inlinee, nullptr}];

  // SymbolRecord
  MCSymbol *InlineEnd = beginSymbolRecord(SymbolKind::S_INLINESITE);

  OS.AddComment("PtrParent");
  OS.emitInt32(0);
  OS.AddComment("PtrEnd");
  OS.emitInt32(0);
  OS.AddComment("Inlinee type index");
  OS.emitInt32(InlineeIdx.getIndex());

  unsigned FileId = maybeRecordFile(Site.Inlinee->getFile());
  unsigned StartLineNum = Site.Inlinee->getLine();

  OS.emitCVInlineLinetableDirective(Site.SiteFuncId, FileId, StartLineNum,
                                    FI.Begin, FI.End);

  endSymbolRecord(InlineEnd);

  emitLocalVariableList(FI, Site.InlinedLocals);

  // Recurse on child inlined call sites before closing the scope.
  for (const DILocation *ChildSite : Site.ChildSites) {
    auto I = FI.InlineSites.find(ChildSite);
    assert(I != FI.InlineSites.end() &&
           "child site not in function inline site map");
    emitInlinedCallSite(FI, ChildSite, I->second);
  }

  // Close the scope.
  emitEndSymbolRecord(SymbolKind::S_INLINESITE_END);
}

void CodeViewDebug::switchToDebugSectionForSymbol(const MCSymbol *GVSym) {
  // If we have a symbol, it may be in a section that is COMDAT. If so, find the
  // comdat key. A section may be comdat because of -ffunction-sections or
  // because it is comdat in the IR.
  MCSectionCOFF *GVSec =
      GVSym ? dyn_cast<MCSectionCOFF>(&GVSym->getSection()) : nullptr;
  const MCSymbol *KeySym = GVSec ? GVSec->getCOMDATSymbol() : nullptr;

  MCSectionCOFF *DebugSec = cast<MCSectionCOFF>(
      Asm->getObjFileLowering().getCOFFDebugSymbolsSection());
  DebugSec = OS.getContext().getAssociativeCOFFSection(DebugSec, KeySym);

  OS.switchSection(DebugSec);

  // Emit the magic version number if this is the first time we've switched to
  // this section.
  if (ComdatDebugSections.insert(DebugSec).second)
    emitCodeViewMagicVersion();
}

// Emit an S_THUNK32/S_END symbol pair for a thunk routine.
// The only supported thunk ordinal is currently the standard type.
void CodeViewDebug::emitDebugInfoForThunk(const Function *GV,
                                          FunctionInfo &FI,
                                          const MCSymbol *Fn) {
  std::string FuncName =
      std::string(GlobalValue::dropLLVMManglingEscape(GV->getName()));
  const ThunkOrdinal ordinal = ThunkOrdinal::Standard; // Only supported kind.

  OS.AddComment("Symbol subsection for " + Twine(FuncName));
  MCSymbol *SymbolsEnd = beginCVSubsection(DebugSubsectionKind::Symbols);

  // Emit S_THUNK32
  MCSymbol *ThunkRecordEnd = beginSymbolRecord(SymbolKind::S_THUNK32);
  OS.AddComment("PtrParent");
  OS.emitInt32(0);
  OS.AddComment("PtrEnd");
  OS.emitInt32(0);
  OS.AddComment("PtrNext");
  OS.emitInt32(0);
  OS.AddComment("Thunk section relative address");
  OS.emitCOFFSecRel32(Fn, /*Offset=*/0);
  OS.AddComment("Thunk section index");
  OS.emitCOFFSectionIndex(Fn);
  OS.AddComment("Code size");
  OS.emitAbsoluteSymbolDiff(FI.End, Fn, 2);
  OS.AddComment("Ordinal");
  OS.emitInt8(unsigned(ordinal));
  OS.AddComment("Function name");
  emitNullTerminatedSymbolName(OS, FuncName);
  // Additional fields specific to the thunk ordinal would go here.
  endSymbolRecord(ThunkRecordEnd);

  // Local variables/inlined routines are purposely omitted here.  The point of
  // marking this as a thunk is so Visual Studio will NOT stop in this routine.

  // Emit S_PROC_ID_END
  emitEndSymbolRecord(SymbolKind::S_PROC_ID_END);

  endCVSubsection(SymbolsEnd);
}

void CodeViewDebug::emitDebugInfoForFunction(const Function *GV,
                                             FunctionInfo &FI) {
  // For each function there is a separate subsection which holds the PC to
  // file:line table.
  const MCSymbol *Fn = Asm->getSymbol(GV);
  assert(Fn);

  // Switch to the to a comdat section, if appropriate.
  switchToDebugSectionForSymbol(Fn);

  std::string FuncName;
  auto *SP = GV->getSubprogram();
  assert(SP);
  setCurrentSubprogram(SP);

  if (SP->isThunk()) {
    emitDebugInfoForThunk(GV, FI, Fn);
    return;
  }

  // If we have a display name, build the fully qualified name by walking the
  // chain of scopes.
  if (!SP->getName().empty())
    FuncName = getFullyQualifiedName(SP->getScope(), SP->getName());

  // If our DISubprogram name is empty, use the mangled name.
  if (FuncName.empty())
    FuncName = std::string(GlobalValue::dropLLVMManglingEscape(GV->getName()));

  // Emit FPO data, but only on 32-bit x86. No other platforms use it.
  if (Triple(MMI->getModule()->getTargetTriple()).getArch() == Triple::x86)
    OS.emitCVFPOData(Fn);

  // Emit a symbol subsection, required by VS2012+ to find function boundaries.
  OS.AddComment("Symbol subsection for " + Twine(FuncName));
  MCSymbol *SymbolsEnd = beginCVSubsection(DebugSubsectionKind::Symbols);
  {
    SymbolKind ProcKind = GV->hasLocalLinkage() ? SymbolKind::S_LPROC32_ID
                                                : SymbolKind::S_GPROC32_ID;
    MCSymbol *ProcRecordEnd = beginSymbolRecord(ProcKind);

    // These fields are filled in by tools like CVPACK which run after the fact.
    OS.AddComment("PtrParent");
    OS.emitInt32(0);
    OS.AddComment("PtrEnd");
    OS.emitInt32(0);
    OS.AddComment("PtrNext");
    OS.emitInt32(0);
    // This is the important bit that tells the debugger where the function
    // code is located and what's its size:
    OS.AddComment("Code size");
    OS.emitAbsoluteSymbolDiff(FI.End, Fn, 4);
    OS.AddComment("Offset after prologue");
    OS.emitInt32(0);
    OS.AddComment("Offset before epilogue");
    OS.emitInt32(0);
    OS.AddComment("Function type index");
    OS.emitInt32(getFuncIdForSubprogram(GV->getSubprogram()).getIndex());
    OS.AddComment("Function section relative address");
    OS.emitCOFFSecRel32(Fn, /*Offset=*/0);
    OS.AddComment("Function section index");
    OS.emitCOFFSectionIndex(Fn);
    OS.AddComment("Flags");
    ProcSymFlags ProcFlags = ProcSymFlags::HasOptimizedDebugInfo;
    if (FI.HasFramePointer)
      ProcFlags |= ProcSymFlags::HasFP;
    if (GV->hasFnAttribute(Attribute::NoReturn))
      ProcFlags |= ProcSymFlags::IsNoReturn;
    if (GV->hasFnAttribute(Attribute::NoInline))
      ProcFlags |= ProcSymFlags::IsNoInline;
    OS.emitInt8(static_cast<uint8_t>(ProcFlags));
    // Emit the function display name as a null-terminated string.
    OS.AddComment("Function name");
    // Truncate the name so we won't overflow the record length field.
    emitNullTerminatedSymbolName(OS, FuncName);
    endSymbolRecord(ProcRecordEnd);

    MCSymbol *FrameProcEnd = beginSymbolRecord(SymbolKind::S_FRAMEPROC);
    // Subtract out the CSR size since MSVC excludes that and we include it.
    OS.AddComment("FrameSize");
    OS.emitInt32(FI.FrameSize - FI.CSRSize);
    OS.AddComment("Padding");
    OS.emitInt32(0);
    OS.AddComment("Offset of padding");
    OS.emitInt32(0);
    OS.AddComment("Bytes of callee saved registers");
    OS.emitInt32(FI.CSRSize);
    OS.AddComment("Exception handler offset");
    OS.emitInt32(0);
    OS.AddComment("Exception handler section");
    OS.emitInt16(0);
    OS.AddComment("Flags (defines frame register)");
    OS.emitInt32(uint32_t(FI.FrameProcOpts));
    endSymbolRecord(FrameProcEnd);

    emitInlinees(FI.Inlinees);
    emitLocalVariableList(FI, FI.Locals);
    emitGlobalVariableList(FI.Globals);
    emitLexicalBlockList(FI.ChildBlocks, FI);

    // Emit inlined call site information. Only emit functions inlined directly
    // into the parent function. We'll emit the other sites recursively as part
    // of their parent inline site.
    for (const DILocation *InlinedAt : FI.ChildSites) {
      auto I = FI.InlineSites.find(InlinedAt);
      assert(I != FI.InlineSites.end() &&
             "child site not in function inline site map");
      emitInlinedCallSite(FI, InlinedAt, I->second);
    }

    for (auto Annot : FI.Annotations) {
      MCSymbol *Label = Annot.first;
      MDTuple *Strs = cast<MDTuple>(Annot.second);
      MCSymbol *AnnotEnd = beginSymbolRecord(SymbolKind::S_ANNOTATION);
      OS.emitCOFFSecRel32(Label, /*Offset=*/0);
      // FIXME: Make sure we don't overflow the max record size.
      OS.emitCOFFSectionIndex(Label);
      OS.emitInt16(Strs->getNumOperands());
      for (Metadata *MD : Strs->operands()) {
        // MDStrings are null terminated, so we can do EmitBytes and get the
        // nice .asciz directive.
        StringRef Str = cast<MDString>(MD)->getString();
        assert(Str.data()[Str.size()] == '\0' && "non-nullterminated MDString");
        OS.emitBytes(StringRef(Str.data(), Str.size() + 1));
      }
      endSymbolRecord(AnnotEnd);
    }

    for (auto HeapAllocSite : FI.HeapAllocSites) {
      const MCSymbol *BeginLabel = std::get<0>(HeapAllocSite);
      const MCSymbol *EndLabel = std::get<1>(HeapAllocSite);
      const DIType *DITy = std::get<2>(HeapAllocSite);
      MCSymbol *HeapAllocEnd = beginSymbolRecord(SymbolKind::S_HEAPALLOCSITE);
      OS.AddComment("Call site offset");
      OS.emitCOFFSecRel32(BeginLabel, /*Offset=*/0);
      OS.AddComment("Call site section index");
      OS.emitCOFFSectionIndex(BeginLabel);
      OS.AddComment("Call instruction length");
      OS.emitAbsoluteSymbolDiff(EndLabel, BeginLabel, 2);
      OS.AddComment("Type index");
      OS.emitInt32(getCompleteTypeIndex(DITy).getIndex());
      endSymbolRecord(HeapAllocEnd);
    }

    if (SP != nullptr)
      emitDebugInfoForUDTs(LocalUDTs);

    emitDebugInfoForJumpTables(FI);

    // We're done with this function.
    emitEndSymbolRecord(SymbolKind::S_PROC_ID_END);
  }
  endCVSubsection(SymbolsEnd);

  // We have an assembler directive that takes care of the whole line table.
  OS.emitCVLinetableDirective(FI.FuncId, Fn, FI.End);
}

CodeViewDebug::LocalVarDef
CodeViewDebug::createDefRangeMem(uint16_t CVRegister, int Offset) {
  LocalVarDef DR;
  DR.InMemory = -1;
  DR.DataOffset = Offset;
  assert(DR.DataOffset == Offset && "truncation");
  DR.IsSubfield = 0;
  DR.StructOffset = 0;
  DR.CVRegister = CVRegister;
  return DR;
}

void CodeViewDebug::collectVariableInfoFromMFTable(
    DenseSet<InlinedEntity> &Processed) {
  const MachineFunction &MF = *Asm->MF;
  const TargetSubtargetInfo &TSI = MF.getSubtarget();
  const TargetFrameLowering *TFI = TSI.getFrameLowering();
  const TargetRegisterInfo *TRI = TSI.getRegisterInfo();

  for (const MachineFunction::VariableDbgInfo &VI :
       MF.getInStackSlotVariableDbgInfo()) {
    if (!VI.Var)
      continue;
    assert(VI.Var->isValidLocationForIntrinsic(VI.Loc) &&
           "Expected inlined-at fields to agree");

    Processed.insert(InlinedEntity(VI.Var, VI.Loc->getInlinedAt()));
    LexicalScope *Scope = LScopes.findLexicalScope(VI.Loc);

    // If variable scope is not found then skip this variable.
    if (!Scope)
      continue;

    // If the variable has an attached offset expression, extract it.
    // FIXME: Try to handle DW_OP_deref as well.
    int64_t ExprOffset = 0;
    bool Deref = false;
    if (VI.Expr) {
      // If there is one DW_OP_deref element, use offset of 0 and keep going.
      if (VI.Expr->getNumElements() == 1 &&
          VI.Expr->getElement(0) == llvm::dwarf::DW_OP_deref)
        Deref = true;
      else if (!VI.Expr->extractIfOffset(ExprOffset))
        continue;
    }

    // Get the frame register used and the offset.
    Register FrameReg;
    StackOffset FrameOffset =
        TFI->getFrameIndexReference(*Asm->MF, VI.getStackSlot(), FrameReg);
    uint16_t CVReg = TRI->getCodeViewRegNum(FrameReg);

    assert(!FrameOffset.getScalable() &&
           "Frame offsets with a scalable component are not supported");

    // Calculate the label ranges.
    LocalVarDef DefRange =
        createDefRangeMem(CVReg, FrameOffset.getFixed() + ExprOffset);

    LocalVariable Var;
    Var.DIVar = VI.Var;

    for (const InsnRange &Range : Scope->getRanges()) {
      const MCSymbol *Begin = getLabelBeforeInsn(Range.first);
      const MCSymbol *End = getLabelAfterInsn(Range.second);
      End = End ? End : Asm->getFunctionEnd();
      Var.DefRanges[DefRange].emplace_back(Begin, End);
    }

    if (Deref)
      Var.UseReferenceType = true;

    recordLocalVariable(std::move(Var), Scope);
  }
}

static bool canUseReferenceType(const DbgVariableLocation &Loc) {
  return !Loc.LoadChain.empty() && Loc.LoadChain.back() == 0;
}

static bool needsReferenceType(const DbgVariableLocation &Loc) {
  return Loc.LoadChain.size() == 2 && Loc.LoadChain.back() == 0;
}

void CodeViewDebug::calculateRanges(
    LocalVariable &Var, const DbgValueHistoryMap::Entries &Entries) {
  const TargetRegisterInfo *TRI = Asm->MF->getSubtarget().getRegisterInfo();

  // Calculate the definition ranges.
  for (auto I = Entries.begin(), E = Entries.end(); I != E; ++I) {
    const auto &Entry = *I;
    if (!Entry.isDbgValue())
      continue;
    const MachineInstr *DVInst = Entry.getInstr();
    assert(DVInst->isDebugValue() && "Invalid History entry");
    // FIXME: Find a way to represent constant variables, since they are
    // relatively common.
    std::optional<DbgVariableLocation> Location =
        DbgVariableLocation::extractFromMachineInstruction(*DVInst);
    if (!Location)
    {
      // When we don't have a location this is usually because LLVM has
      // transformed it into a constant and we only have an llvm.dbg.value. We
      // can't represent these well in CodeView since S_LOCAL only works on
      // registers and memory locations. Instead, we will pretend this to be a
      // constant value to at least have it show up in the debugger.
      auto Op = DVInst->getDebugOperand(0);
      if (Op.isImm())
        Var.ConstantValue = APSInt(APInt(64, Op.getImm()), false);
      continue;
    }

    // CodeView can only express variables in register and variables in memory
    // at a constant offset from a register. However, for variables passed
    // indirectly by pointer, it is common for that pointer to be spilled to a
    // stack location. For the special case of one offseted load followed by a
    // zero offset load (a pointer spilled to the stack), we change the type of
    // the local variable from a value type to a reference type. This tricks the
    // debugger into doing the load for us.
    if (Var.UseReferenceType) {
      // We're using a reference type. Drop the last zero offset load.
      if (canUseReferenceType(*Location))
        Location->LoadChain.pop_back();
      else
        continue;
    } else if (needsReferenceType(*Location)) {
      // This location can't be expressed without switching to a reference type.
      // Start over using that.
      Var.UseReferenceType = true;
      Var.DefRanges.clear();
      calculateRanges(Var, Entries);
      return;
    }

    // We can only handle a register or an offseted load of a register.
    if (Location->Register == 0 || Location->LoadChain.size() > 1)
      continue;

    // Codeview can only express byte-aligned offsets, ensure that we have a
    // byte-boundaried location.
    if (Location->FragmentInfo)
      if (Location->FragmentInfo->OffsetInBits % 8)
        continue;

    LocalVarDef DR;
    DR.CVRegister = TRI->getCodeViewRegNum(Location->Register);
    DR.InMemory = !Location->LoadChain.empty();
    DR.DataOffset =
        !Location->LoadChain.empty() ? Location->LoadChain.back() : 0;
    if (Location->FragmentInfo) {
      DR.IsSubfield = true;
      DR.StructOffset = Location->FragmentInfo->OffsetInBits / 8;
    } else {
      DR.IsSubfield = false;
      DR.StructOffset = 0;
    }

    // Compute the label range.
    const MCSymbol *Begin = getLabelBeforeInsn(Entry.getInstr());
    const MCSymbol *End;
    if (Entry.getEndIndex() != DbgValueHistoryMap::NoEntry) {
      auto &EndingEntry = Entries[Entry.getEndIndex()];
      End = EndingEntry.isDbgValue()
                ? getLabelBeforeInsn(EndingEntry.getInstr())
                : getLabelAfterInsn(EndingEntry.getInstr());
    } else
      End = Asm->getFunctionEnd();

    // If the last range end is our begin, just extend the last range.
    // Otherwise make a new range.
    SmallVectorImpl<std::pair<const MCSymbol *, const MCSymbol *>> &R =
        Var.DefRanges[DR];
    if (!R.empty() && R.back().second == Begin)
      R.back().second = End;
    else
      R.emplace_back(Begin, End);

    // FIXME: Do more range combining.
  }
}

void CodeViewDebug::collectVariableInfo(const DISubprogram *SP) {
  DenseSet<InlinedEntity> Processed;
  // Grab the variable info that was squirreled away in the MMI side-table.
  collectVariableInfoFromMFTable(Processed);

  for (const auto &I : DbgValues) {
    InlinedEntity IV = I.first;
    if (Processed.count(IV))
      continue;
    const DILocalVariable *DIVar = cast<DILocalVariable>(IV.first);
    const DILocation *InlinedAt = IV.second;

    // Instruction ranges, specifying where IV is accessible.
    const auto &Entries = I.second;

    LexicalScope *Scope = nullptr;
    if (InlinedAt)
      Scope = LScopes.findInlinedScope(DIVar->getScope(), InlinedAt);
    else
      Scope = LScopes.findLexicalScope(DIVar->getScope());
    // If variable scope is not found then skip this variable.
    if (!Scope)
      continue;

    LocalVariable Var;
    Var.DIVar = DIVar;

    calculateRanges(Var, Entries);
    recordLocalVariable(std::move(Var), Scope);
  }
}

void CodeViewDebug::beginFunctionImpl(const MachineFunction *MF) {
  const TargetSubtargetInfo &TSI = MF->getSubtarget();
  const TargetRegisterInfo *TRI = TSI.getRegisterInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  const Function &GV = MF->getFunction();
  auto Insertion = FnDebugInfo.insert({&GV, std::make_unique<FunctionInfo>()});
  assert(Insertion.second && "function already has info");
  CurFn = Insertion.first->second.get();
  CurFn->FuncId = NextFuncId++;
  CurFn->Begin = Asm->getFunctionBegin();

  // The S_FRAMEPROC record reports the stack size, and how many bytes of
  // callee-saved registers were used. For targets that don't use a PUSH
  // instruction (AArch64), this will be zero.
  CurFn->CSRSize = MFI.getCVBytesOfCalleeSavedRegisters();
  CurFn->FrameSize = MFI.getStackSize();
  CurFn->OffsetAdjustment = MFI.getOffsetAdjustment();
  CurFn->HasStackRealignment = TRI->hasStackRealignment(*MF);

  // For this function S_FRAMEPROC record, figure out which codeview register
  // will be the frame pointer.
  CurFn->EncodedParamFramePtrReg = EncodedFramePtrReg::None; // None.
  CurFn->EncodedLocalFramePtrReg = EncodedFramePtrReg::None; // None.
  if (CurFn->FrameSize > 0) {
    if (!TSI.getFrameLowering()->hasFP(*MF)) {
      CurFn->EncodedLocalFramePtrReg = EncodedFramePtrReg::StackPtr;
      CurFn->EncodedParamFramePtrReg = EncodedFramePtrReg::StackPtr;
    } else {
      CurFn->HasFramePointer = true;
      // If there is an FP, parameters are always relative to it.
      CurFn->EncodedParamFramePtrReg = EncodedFramePtrReg::FramePtr;
      if (CurFn->HasStackRealignment) {
        // If the stack needs realignment, locals are relative to SP or VFRAME.
        CurFn->EncodedLocalFramePtrReg = EncodedFramePtrReg::StackPtr;
      } else {
        // Otherwise, locals are relative to EBP, and we probably have VLAs or
        // other stack adjustments.
        CurFn->EncodedLocalFramePtrReg = EncodedFramePtrReg::FramePtr;
      }
    }
  }

  // Compute other frame procedure options.
  FrameProcedureOptions FPO = FrameProcedureOptions::None;
  if (MFI.hasVarSizedObjects())
    FPO |= FrameProcedureOptions::HasAlloca;
  if (MF->exposesReturnsTwice())
    FPO |= FrameProcedureOptions::HasSetJmp;
  // FIXME: Set HasLongJmp if we ever track that info.
  if (MF->hasInlineAsm())
    FPO |= FrameProcedureOptions::HasInlineAssembly;
  if (GV.hasPersonalityFn()) {
    if (isAsynchronousEHPersonality(
            classifyEHPersonality(GV.getPersonalityFn())))
      FPO |= FrameProcedureOptions::HasStructuredExceptionHandling;
    else
      FPO |= FrameProcedureOptions::HasExceptionHandling;
  }
  if (GV.hasFnAttribute(Attribute::InlineHint))
    FPO |= FrameProcedureOptions::MarkedInline;
  if (GV.hasFnAttribute(Attribute::Naked))
    FPO |= FrameProcedureOptions::Naked;
  if (MFI.hasStackProtectorIndex()) {
    FPO |= FrameProcedureOptions::SecurityChecks;
    if (GV.hasFnAttribute(Attribute::StackProtectStrong) ||
        GV.hasFnAttribute(Attribute::StackProtectReq)) {
      FPO |= FrameProcedureOptions::StrictSecurityChecks;
    }
  } else if (!GV.hasStackProtectorFnAttr()) {
    // __declspec(safebuffers) disables stack guards.
    FPO |= FrameProcedureOptions::SafeBuffers;
  }
  FPO |= FrameProcedureOptions(uint32_t(CurFn->EncodedLocalFramePtrReg) << 14U);
  FPO |= FrameProcedureOptions(uint32_t(CurFn->EncodedParamFramePtrReg) << 16U);
  if (Asm->TM.getOptLevel() != CodeGenOptLevel::None && !GV.hasOptSize() &&
      !GV.hasOptNone())
    FPO |= FrameProcedureOptions::OptimizedForSpeed;
  if (GV.hasProfileData()) {
    FPO |= FrameProcedureOptions::ValidProfileCounts;
    FPO |= FrameProcedureOptions::ProfileGuidedOptimization;
  }
  // FIXME: Set GuardCfg when it is implemented.
  CurFn->FrameProcOpts = FPO;

  OS.emitCVFuncIdDirective(CurFn->FuncId);

  // Find the end of the function prolog.  First known non-DBG_VALUE and
  // non-frame setup location marks the beginning of the function body.
  // FIXME: is there a simpler a way to do this? Can we just search
  // for the first instruction of the function, not the last of the prolog?
  DebugLoc PrologEndLoc;
  bool EmptyPrologue = true;
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (!MI.isMetaInstruction() && !MI.getFlag(MachineInstr::FrameSetup) &&
          MI.getDebugLoc()) {
        PrologEndLoc = MI.getDebugLoc();
        break;
      } else if (!MI.isMetaInstruction()) {
        EmptyPrologue = false;
      }
    }
  }

  // Record beginning of function if we have a non-empty prologue.
  if (PrologEndLoc && !EmptyPrologue) {
    DebugLoc FnStartDL = PrologEndLoc.getFnDebugLoc();
    maybeRecordLocation(FnStartDL, MF);
  }

  // Find heap alloc sites and emit labels around them.
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (MI.getHeapAllocMarker()) {
        requestLabelBeforeInsn(&MI);
        requestLabelAfterInsn(&MI);
      }
    }
  }

  // Mark branches that may potentially be using jump tables with labels.
  bool isThumb = Triple(MMI->getModule()->getTargetTriple()).getArch() ==
                 llvm::Triple::ArchType::thumb;
  discoverJumpTableBranches(MF, isThumb);
}

static bool shouldEmitUdt(const DIType *T) {
  if (!T)
    return false;

  // MSVC does not emit UDTs for typedefs that are scoped to classes.
  if (T->getTag() == dwarf::DW_TAG_typedef) {
    if (DIScope *Scope = T->getScope()) {
      switch (Scope->getTag()) {
      case dwarf::DW_TAG_structure_type:
      case dwarf::DW_TAG_class_type:
      case dwarf::DW_TAG_union_type:
        return false;
      default:
          // do nothing.
          ;
      }
    }
  }

  while (true) {
    if (!T || T->isForwardDecl())
      return false;

    const DIDerivedType *DT = dyn_cast<DIDerivedType>(T);
    if (!DT)
      return true;
    T = DT->getBaseType();
  }
  return true;
}

void CodeViewDebug::addToUDTs(const DIType *Ty) {
  // Don't record empty UDTs.
  if (Ty->getName().empty())
    return;
  if (!shouldEmitUdt(Ty))
    return;

  SmallVector<StringRef, 5> ParentScopeNames;
  const DISubprogram *ClosestSubprogram =
      collectParentScopeNames(Ty->getScope(), ParentScopeNames);

  std::string FullyQualifiedName =
      formatNestedName(ParentScopeNames, getPrettyScopeName(Ty));

  if (ClosestSubprogram == nullptr) {
    GlobalUDTs.emplace_back(std::move(FullyQualifiedName), Ty);
  } else if (ClosestSubprogram == CurrentSubprogram) {
    LocalUDTs.emplace_back(std::move(FullyQualifiedName), Ty);
  }

  // TODO: What if the ClosestSubprogram is neither null or the current
  // subprogram?  Currently, the UDT just gets dropped on the floor.
  //
  // The current behavior is not desirable.  To get maximal fidelity, we would
  // need to perform all type translation before beginning emission of .debug$S
  // and then make LocalUDTs a member of FunctionInfo
}

TypeIndex CodeViewDebug::lowerType(const DIType *Ty, const DIType *ClassTy) {
  // Generic dispatch for lowering an unknown type.
  switch (Ty->getTag()) {
  case dwarf::DW_TAG_array_type:
    return lowerTypeArray(cast<DICompositeType>(Ty));
  case dwarf::DW_TAG_typedef:
    return lowerTypeAlias(cast<DIDerivedType>(Ty));
  case dwarf::DW_TAG_base_type:
    return lowerTypeBasic(cast<DIBasicType>(Ty));
  case dwarf::DW_TAG_pointer_type:
    if (cast<DIDerivedType>(Ty)->getName() == "__vtbl_ptr_type")
      return lowerTypeVFTableShape(cast<DIDerivedType>(Ty));
    [[fallthrough]];
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_rvalue_reference_type:
    return lowerTypePointer(cast<DIDerivedType>(Ty));
  case dwarf::DW_TAG_ptr_to_member_type:
    return lowerTypeMemberPointer(cast<DIDerivedType>(Ty));
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_volatile_type:
  // TODO: add support for DW_TAG_atomic_type here
    return lowerTypeModifier(cast<DIDerivedType>(Ty));
  case dwarf::DW_TAG_subroutine_type:
    if (ClassTy) {
      // The member function type of a member function pointer has no
      // ThisAdjustment.
      return lowerTypeMemberFunction(cast<DISubroutineType>(Ty), ClassTy,
                                     /*ThisAdjustment=*/0,
                                     /*IsStaticMethod=*/false);
    }
    return lowerTypeFunction(cast<DISubroutineType>(Ty));
  case dwarf::DW_TAG_enumeration_type:
    return lowerTypeEnum(cast<DICompositeType>(Ty));
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
    return lowerTypeClass(cast<DICompositeType>(Ty));
  case dwarf::DW_TAG_union_type:
    return lowerTypeUnion(cast<DICompositeType>(Ty));
  case dwarf::DW_TAG_string_type:
    return lowerTypeString(cast<DIStringType>(Ty));
  case dwarf::DW_TAG_unspecified_type:
    if (Ty->getName() == "decltype(nullptr)")
      return TypeIndex::NullptrT();
    return TypeIndex::None();
  default:
    // Use the null type index.
    return TypeIndex();
  }
}

TypeIndex CodeViewDebug::lowerTypeAlias(const DIDerivedType *Ty) {
  TypeIndex UnderlyingTypeIndex = getTypeIndex(Ty->getBaseType());
  StringRef TypeName = Ty->getName();

  addToUDTs(Ty);

  if (UnderlyingTypeIndex == TypeIndex(SimpleTypeKind::Int32Long) &&
      TypeName == "HRESULT")
    return TypeIndex(SimpleTypeKind::HResult);
  if (UnderlyingTypeIndex == TypeIndex(SimpleTypeKind::UInt16Short) &&
      TypeName == "wchar_t")
    return TypeIndex(SimpleTypeKind::WideCharacter);

  return UnderlyingTypeIndex;
}

TypeIndex CodeViewDebug::lowerTypeArray(const DICompositeType *Ty) {
  const DIType *ElementType = Ty->getBaseType();
  TypeIndex ElementTypeIndex = getTypeIndex(ElementType);
  // IndexType is size_t, which depends on the bitness of the target.
  TypeIndex IndexType = getPointerSizeInBytes() == 8
                            ? TypeIndex(SimpleTypeKind::UInt64Quad)
                            : TypeIndex(SimpleTypeKind::UInt32Long);

  uint64_t ElementSize = getBaseTypeSize(ElementType) / 8;

  // Add subranges to array type.
  DINodeArray Elements = Ty->getElements();
  for (int i = Elements.size() - 1; i >= 0; --i) {
    const DINode *Element = Elements[i];
    assert(Element->getTag() == dwarf::DW_TAG_subrange_type);

    const DISubrange *Subrange = cast<DISubrange>(Element);
    int64_t Count = -1;

    // If Subrange has a Count field, use it.
    // Otherwise, if it has an upperboud, use (upperbound - lowerbound + 1),
    // where lowerbound is from the LowerBound field of the Subrange,
    // or the language default lowerbound if that field is unspecified.
    if (auto *CI = dyn_cast_if_present<ConstantInt *>(Subrange->getCount()))
      Count = CI->getSExtValue();
    else if (auto *UI = dyn_cast_if_present<ConstantInt *>(
                 Subrange->getUpperBound())) {
      // Fortran uses 1 as the default lowerbound; other languages use 0.
      int64_t Lowerbound = (moduleIsInFortran()) ? 1 : 0;
      auto *LI = dyn_cast_if_present<ConstantInt *>(Subrange->getLowerBound());
      Lowerbound = (LI) ? LI->getSExtValue() : Lowerbound;
      Count = UI->getSExtValue() - Lowerbound + 1;
    }

    // Forward declarations of arrays without a size and VLAs use a count of -1.
    // Emit a count of zero in these cases to match what MSVC does for arrays
    // without a size. MSVC doesn't support VLAs, so it's not clear what we
    // should do for them even if we could distinguish them.
    if (Count == -1)
      Count = 0;

    // Update the element size and element type index for subsequent subranges.
    ElementSize *= Count;

    // If this is the outermost array, use the size from the array. It will be
    // more accurate if we had a VLA or an incomplete element type size.
    uint64_t ArraySize =
        (i == 0 && ElementSize == 0) ? Ty->getSizeInBits() / 8 : ElementSize;

    StringRef Name = (i == 0) ? Ty->getName() : "";
    ArrayRecord AR(ElementTypeIndex, IndexType, ArraySize, Name);
    ElementTypeIndex = TypeTable.writeLeafType(AR);
  }

  return ElementTypeIndex;
}

// This function lowers a Fortran character type (DIStringType).
// Note that it handles only the character*n variant (using SizeInBits
// field in DIString to describe the type size) at the moment.
// Other variants (leveraging the StringLength and StringLengthExp
// fields in DIStringType) remain TBD.
TypeIndex CodeViewDebug::lowerTypeString(const DIStringType *Ty) {
  TypeIndex CharType = TypeIndex(SimpleTypeKind::NarrowCharacter);
  uint64_t ArraySize = Ty->getSizeInBits() >> 3;
  StringRef Name = Ty->getName();
  // IndexType is size_t, which depends on the bitness of the target.
  TypeIndex IndexType = getPointerSizeInBytes() == 8
                            ? TypeIndex(SimpleTypeKind::UInt64Quad)
                            : TypeIndex(SimpleTypeKind::UInt32Long);

  // Create a type of character array of ArraySize.
  ArrayRecord AR(CharType, IndexType, ArraySize, Name);

  return TypeTable.writeLeafType(AR);
}

TypeIndex CodeViewDebug::lowerTypeBasic(const DIBasicType *Ty) {
  TypeIndex Index;
  dwarf::TypeKind Kind;
  uint32_t ByteSize;

  Kind = static_cast<dwarf::TypeKind>(Ty->getEncoding());
  ByteSize = Ty->getSizeInBits() / 8;

  SimpleTypeKind STK = SimpleTypeKind::None;
  switch (Kind) {
  case dwarf::DW_ATE_address:
    // FIXME: Translate
    break;
  case dwarf::DW_ATE_boolean:
    switch (ByteSize) {
    case 1:  STK = SimpleTypeKind::Boolean8;   break;
    case 2:  STK = SimpleTypeKind::Boolean16;  break;
    case 4:  STK = SimpleTypeKind::Boolean32;  break;
    case 8:  STK = SimpleTypeKind::Boolean64;  break;
    case 16: STK = SimpleTypeKind::Boolean128; break;
    }
    break;
  case dwarf::DW_ATE_complex_float:
    // The CodeView size for a complex represents the size of
    // an individual component.
    switch (ByteSize) {
    case 4:  STK = SimpleTypeKind::Complex16;  break;
    case 8:  STK = SimpleTypeKind::Complex32;  break;
    case 16: STK = SimpleTypeKind::Complex64;  break;
    case 20: STK = SimpleTypeKind::Complex80;  break;
    case 32: STK = SimpleTypeKind::Complex128; break;
    }
    break;
  case dwarf::DW_ATE_float:
    switch (ByteSize) {
    case 2:  STK = SimpleTypeKind::Float16;  break;
    case 4:  STK = SimpleTypeKind::Float32;  break;
    case 6:  STK = SimpleTypeKind::Float48;  break;
    case 8:  STK = SimpleTypeKind::Float64;  break;
    case 10: STK = SimpleTypeKind::Float80;  break;
    case 16: STK = SimpleTypeKind::Float128; break;
    }
    break;
  case dwarf::DW_ATE_signed:
    switch (ByteSize) {
    case 1:  STK = SimpleTypeKind::SignedCharacter; break;
    case 2:  STK = SimpleTypeKind::Int16Short;      break;
    case 4:  STK = SimpleTypeKind::Int32;           break;
    case 8:  STK = SimpleTypeKind::Int64Quad;       break;
    case 16: STK = SimpleTypeKind::Int128Oct;       break;
    }
    break;
  case dwarf::DW_ATE_unsigned:
    switch (ByteSize) {
    case 1:  STK = SimpleTypeKind::UnsignedCharacter; break;
    case 2:  STK = SimpleTypeKind::UInt16Short;       break;
    case 4:  STK = SimpleTypeKind::UInt32;            break;
    case 8:  STK = SimpleTypeKind::UInt64Quad;        break;
    case 16: STK = SimpleTypeKind::UInt128Oct;        break;
    }
    break;
  case dwarf::DW_ATE_UTF:
    switch (ByteSize) {
    case 1: STK = SimpleTypeKind::Character8; break;
    case 2: STK = SimpleTypeKind::Character16; break;
    case 4: STK = SimpleTypeKind::Character32; break;
    }
    break;
  case dwarf::DW_ATE_signed_char:
    if (ByteSize == 1)
      STK = SimpleTypeKind::SignedCharacter;
    break;
  case dwarf::DW_ATE_unsigned_char:
    if (ByteSize == 1)
      STK = SimpleTypeKind::UnsignedCharacter;
    break;
  default:
    break;
  }

  // Apply some fixups based on the source-level type name.
  // Include some amount of canonicalization from an old naming scheme Clang
  // used to use for integer types (in an outdated effort to be compatible with
  // GCC's debug info/GDB's behavior, which has since been addressed).
  if (STK == SimpleTypeKind::Int32 &&
      (Ty->getName() == "long int" || Ty->getName() == "long"))
    STK = SimpleTypeKind::Int32Long;
  if (STK == SimpleTypeKind::UInt32 && (Ty->getName() == "long unsigned int" ||
                                        Ty->getName() == "unsigned long"))
    STK = SimpleTypeKind::UInt32Long;
  if (STK == SimpleTypeKind::UInt16Short &&
      (Ty->getName() == "wchar_t" || Ty->getName() == "__wchar_t"))
    STK = SimpleTypeKind::WideCharacter;
  if ((STK == SimpleTypeKind::SignedCharacter ||
       STK == SimpleTypeKind::UnsignedCharacter) &&
      Ty->getName() == "char")
    STK = SimpleTypeKind::NarrowCharacter;

  return TypeIndex(STK);
}

TypeIndex CodeViewDebug::lowerTypePointer(const DIDerivedType *Ty,
                                          PointerOptions PO) {
  TypeIndex PointeeTI = getTypeIndex(Ty->getBaseType());

  // Pointers to simple types without any options can use SimpleTypeMode, rather
  // than having a dedicated pointer type record.
  if (PointeeTI.isSimple() && PO == PointerOptions::None &&
      PointeeTI.getSimpleMode() == SimpleTypeMode::Direct &&
      Ty->getTag() == dwarf::DW_TAG_pointer_type) {
    SimpleTypeMode Mode = Ty->getSizeInBits() == 64
                              ? SimpleTypeMode::NearPointer64
                              : SimpleTypeMode::NearPointer32;
    return TypeIndex(PointeeTI.getSimpleKind(), Mode);
  }

  PointerKind PK =
      Ty->getSizeInBits() == 64 ? PointerKind::Near64 : PointerKind::Near32;
  PointerMode PM = PointerMode::Pointer;
  switch (Ty->getTag()) {
  default: llvm_unreachable("not a pointer tag type");
  case dwarf::DW_TAG_pointer_type:
    PM = PointerMode::Pointer;
    break;
  case dwarf::DW_TAG_reference_type:
    PM = PointerMode::LValueReference;
    break;
  case dwarf::DW_TAG_rvalue_reference_type:
    PM = PointerMode::RValueReference;
    break;
  }

  if (Ty->isObjectPointer())
    PO |= PointerOptions::Const;

  PointerRecord PR(PointeeTI, PK, PM, PO, Ty->getSizeInBits() / 8);
  return TypeTable.writeLeafType(PR);
}

static PointerToMemberRepresentation
translatePtrToMemberRep(unsigned SizeInBytes, bool IsPMF, unsigned Flags) {
  // SizeInBytes being zero generally implies that the member pointer type was
  // incomplete, which can happen if it is part of a function prototype. In this
  // case, use the unknown model instead of the general model.
  if (IsPMF) {
    switch (Flags & DINode::FlagPtrToMemberRep) {
    case 0:
      return SizeInBytes == 0 ? PointerToMemberRepresentation::Unknown
                              : PointerToMemberRepresentation::GeneralFunction;
    case DINode::FlagSingleInheritance:
      return PointerToMemberRepresentation::SingleInheritanceFunction;
    case DINode::FlagMultipleInheritance:
      return PointerToMemberRepresentation::MultipleInheritanceFunction;
    case DINode::FlagVirtualInheritance:
      return PointerToMemberRepresentation::VirtualInheritanceFunction;
    }
  } else {
    switch (Flags & DINode::FlagPtrToMemberRep) {
    case 0:
      return SizeInBytes == 0 ? PointerToMemberRepresentation::Unknown
                              : PointerToMemberRepresentation::GeneralData;
    case DINode::FlagSingleInheritance:
      return PointerToMemberRepresentation::SingleInheritanceData;
    case DINode::FlagMultipleInheritance:
      return PointerToMemberRepresentation::MultipleInheritanceData;
    case DINode::FlagVirtualInheritance:
      return PointerToMemberRepresentation::VirtualInheritanceData;
    }
  }
  llvm_unreachable("invalid ptr to member representation");
}

TypeIndex CodeViewDebug::lowerTypeMemberPointer(const DIDerivedType *Ty,
                                                PointerOptions PO) {
  assert(Ty->getTag() == dwarf::DW_TAG_ptr_to_member_type);
  bool IsPMF = isa<DISubroutineType>(Ty->getBaseType());
  TypeIndex ClassTI = getTypeIndex(Ty->getClassType());
  TypeIndex PointeeTI =
      getTypeIndex(Ty->getBaseType(), IsPMF ? Ty->getClassType() : nullptr);
  PointerKind PK = getPointerSizeInBytes() == 8 ? PointerKind::Near64
                                                : PointerKind::Near32;
  PointerMode PM = IsPMF ? PointerMode::PointerToMemberFunction
                         : PointerMode::PointerToDataMember;

  assert(Ty->getSizeInBits() / 8 <= 0xff && "pointer size too big");
  uint8_t SizeInBytes = Ty->getSizeInBits() / 8;
  MemberPointerInfo MPI(
      ClassTI, translatePtrToMemberRep(SizeInBytes, IsPMF, Ty->getFlags()));
  PointerRecord PR(PointeeTI, PK, PM, PO, SizeInBytes, MPI);
  return TypeTable.writeLeafType(PR);
}

/// Given a DWARF calling convention, get the CodeView equivalent. If we don't
/// have a translation, use the NearC convention.
static CallingConvention dwarfCCToCodeView(unsigned DwarfCC) {
  switch (DwarfCC) {
  case dwarf::DW_CC_normal:             return CallingConvention::NearC;
  case dwarf::DW_CC_BORLAND_msfastcall: return CallingConvention::NearFast;
  case dwarf::DW_CC_BORLAND_thiscall:   return CallingConvention::ThisCall;
  case dwarf::DW_CC_BORLAND_stdcall:    return CallingConvention::NearStdCall;
  case dwarf::DW_CC_BORLAND_pascal:     return CallingConvention::NearPascal;
  case dwarf::DW_CC_LLVM_vectorcall:    return CallingConvention::NearVector;
  }
  return CallingConvention::NearC;
}

TypeIndex CodeViewDebug::lowerTypeModifier(const DIDerivedType *Ty) {
  ModifierOptions Mods = ModifierOptions::None;
  PointerOptions PO = PointerOptions::None;
  bool IsModifier = true;
  const DIType *BaseTy = Ty;
  while (IsModifier && BaseTy) {
    // FIXME: Need to add DWARF tags for __unaligned and _Atomic
    switch (BaseTy->getTag()) {
    case dwarf::DW_TAG_const_type:
      Mods |= ModifierOptions::Const;
      PO |= PointerOptions::Const;
      break;
    case dwarf::DW_TAG_volatile_type:
      Mods |= ModifierOptions::Volatile;
      PO |= PointerOptions::Volatile;
      break;
    case dwarf::DW_TAG_restrict_type:
      // Only pointer types be marked with __restrict. There is no known flag
      // for __restrict in LF_MODIFIER records.
      PO |= PointerOptions::Restrict;
      break;
    default:
      IsModifier = false;
      break;
    }
    if (IsModifier)
      BaseTy = cast<DIDerivedType>(BaseTy)->getBaseType();
  }

  // Check if the inner type will use an LF_POINTER record. If so, the
  // qualifiers will go in the LF_POINTER record. This comes up for types like
  // 'int *const' and 'int *__restrict', not the more common cases like 'const
  // char *'.
  if (BaseTy) {
    switch (BaseTy->getTag()) {
    case dwarf::DW_TAG_pointer_type:
    case dwarf::DW_TAG_reference_type:
    case dwarf::DW_TAG_rvalue_reference_type:
      return lowerTypePointer(cast<DIDerivedType>(BaseTy), PO);
    case dwarf::DW_TAG_ptr_to_member_type:
      return lowerTypeMemberPointer(cast<DIDerivedType>(BaseTy), PO);
    default:
      break;
    }
  }

  TypeIndex ModifiedTI = getTypeIndex(BaseTy);

  // Return the base type index if there aren't any modifiers. For example, the
  // metadata could contain restrict wrappers around non-pointer types.
  if (Mods == ModifierOptions::None)
    return ModifiedTI;

  ModifierRecord MR(ModifiedTI, Mods);
  return TypeTable.writeLeafType(MR);
}

TypeIndex CodeViewDebug::lowerTypeFunction(const DISubroutineType *Ty) {
  SmallVector<TypeIndex, 8> ReturnAndArgTypeIndices;
  for (const DIType *ArgType : Ty->getTypeArray())
    ReturnAndArgTypeIndices.push_back(getTypeIndex(ArgType));

  // MSVC uses type none for variadic argument.
  if (ReturnAndArgTypeIndices.size() > 1 &&
      ReturnAndArgTypeIndices.back() == TypeIndex::Void()) {
    ReturnAndArgTypeIndices.back() = TypeIndex::None();
  }
  TypeIndex ReturnTypeIndex = TypeIndex::Void();
  ArrayRef<TypeIndex> ArgTypeIndices = std::nullopt;
  if (!ReturnAndArgTypeIndices.empty()) {
    auto ReturnAndArgTypesRef = ArrayRef(ReturnAndArgTypeIndices);
    ReturnTypeIndex = ReturnAndArgTypesRef.front();
    ArgTypeIndices = ReturnAndArgTypesRef.drop_front();
  }

  ArgListRecord ArgListRec(TypeRecordKind::ArgList, ArgTypeIndices);
  TypeIndex ArgListIndex = TypeTable.writeLeafType(ArgListRec);

  CallingConvention CC = dwarfCCToCodeView(Ty->getCC());

  FunctionOptions FO = getFunctionOptions(Ty);
  ProcedureRecord Procedure(ReturnTypeIndex, CC, FO, ArgTypeIndices.size(),
                            ArgListIndex);
  return TypeTable.writeLeafType(Procedure);
}

TypeIndex CodeViewDebug::lowerTypeMemberFunction(const DISubroutineType *Ty,
                                                 const DIType *ClassTy,
                                                 int ThisAdjustment,
                                                 bool IsStaticMethod,
                                                 FunctionOptions FO) {
  // Lower the containing class type.
  TypeIndex ClassType = getTypeIndex(ClassTy);

  DITypeRefArray ReturnAndArgs = Ty->getTypeArray();

  unsigned Index = 0;
  SmallVector<TypeIndex, 8> ArgTypeIndices;
  TypeIndex ReturnTypeIndex = TypeIndex::Void();
  if (ReturnAndArgs.size() > Index) {
    ReturnTypeIndex = getTypeIndex(ReturnAndArgs[Index++]);
  }

  // If the first argument is a pointer type and this isn't a static method,
  // treat it as the special 'this' parameter, which is encoded separately from
  // the arguments.
  TypeIndex ThisTypeIndex;
  if (!IsStaticMethod && ReturnAndArgs.size() > Index) {
    if (const DIDerivedType *PtrTy =
            dyn_cast_or_null<DIDerivedType>(ReturnAndArgs[Index])) {
      if (PtrTy->getTag() == dwarf::DW_TAG_pointer_type) {
        ThisTypeIndex = getTypeIndexForThisPtr(PtrTy, Ty);
        Index++;
      }
    }
  }

  while (Index < ReturnAndArgs.size())
    ArgTypeIndices.push_back(getTypeIndex(ReturnAndArgs[Index++]));

  // MSVC uses type none for variadic argument.
  if (!ArgTypeIndices.empty() && ArgTypeIndices.back() == TypeIndex::Void())
    ArgTypeIndices.back() = TypeIndex::None();

  ArgListRecord ArgListRec(TypeRecordKind::ArgList, ArgTypeIndices);
  TypeIndex ArgListIndex = TypeTable.writeLeafType(ArgListRec);

  CallingConvention CC = dwarfCCToCodeView(Ty->getCC());

  MemberFunctionRecord MFR(ReturnTypeIndex, ClassType, ThisTypeIndex, CC, FO,
                           ArgTypeIndices.size(), ArgListIndex, ThisAdjustment);
  return TypeTable.writeLeafType(MFR);
}

TypeIndex CodeViewDebug::lowerTypeVFTableShape(const DIDerivedType *Ty) {
  unsigned VSlotCount =
      Ty->getSizeInBits() / (8 * Asm->MAI->getCodePointerSize());
  SmallVector<VFTableSlotKind, 4> Slots(VSlotCount, VFTableSlotKind::Near);

  VFTableShapeRecord VFTSR(Slots);
  return TypeTable.writeLeafType(VFTSR);
}

static MemberAccess translateAccessFlags(unsigned RecordTag, unsigned Flags) {
  switch (Flags & DINode::FlagAccessibility) {
  case DINode::FlagPrivate:   return MemberAccess::Private;
  case DINode::FlagPublic:    return MemberAccess::Public;
  case DINode::FlagProtected: return MemberAccess::Protected;
  case 0:
    // If there was no explicit access control, provide the default for the tag.
    return RecordTag == dwarf::DW_TAG_class_type ? MemberAccess::Private
                                                 : MemberAccess::Public;
  }
  llvm_unreachable("access flags are exclusive");
}

static MethodOptions translateMethodOptionFlags(const DISubprogram *SP) {
  if (SP->isArtificial())
    return MethodOptions::CompilerGenerated;

  // FIXME: Handle other MethodOptions.

  return MethodOptions::None;
}

static MethodKind translateMethodKindFlags(const DISubprogram *SP,
                                           bool Introduced) {
  if (SP->getFlags() & DINode::FlagStaticMember)
    return MethodKind::Static;

  switch (SP->getVirtuality()) {
  case dwarf::DW_VIRTUALITY_none:
    break;
  case dwarf::DW_VIRTUALITY_virtual:
    return Introduced ? MethodKind::IntroducingVirtual : MethodKind::Virtual;
  case dwarf::DW_VIRTUALITY_pure_virtual:
    return Introduced ? MethodKind::PureIntroducingVirtual
                      : MethodKind::PureVirtual;
  default:
    llvm_unreachable("unhandled virtuality case");
  }

  return MethodKind::Vanilla;
}

static TypeRecordKind getRecordKind(const DICompositeType *Ty) {
  switch (Ty->getTag()) {
  case dwarf::DW_TAG_class_type:
    return TypeRecordKind::Class;
  case dwarf::DW_TAG_structure_type:
    return TypeRecordKind::Struct;
  default:
    llvm_unreachable("unexpected tag");
  }
}

/// Return ClassOptions that should be present on both the forward declaration
/// and the defintion of a tag type.
static ClassOptions getCommonClassOptions(const DICompositeType *Ty) {
  ClassOptions CO = ClassOptions::None;

  // MSVC always sets this flag, even for local types. Clang doesn't always
  // appear to give every type a linkage name, which may be problematic for us.
  // FIXME: Investigate the consequences of not following them here.
  if (!Ty->getIdentifier().empty())
    CO |= ClassOptions::HasUniqueName;

  // Put the Nested flag on a type if it appears immediately inside a tag type.
  // Do not walk the scope chain. Do not attempt to compute ContainsNestedClass
  // here. That flag is only set on definitions, and not forward declarations.
  const DIScope *ImmediateScope = Ty->getScope();
  if (ImmediateScope && isa<DICompositeType>(ImmediateScope))
    CO |= ClassOptions::Nested;

  // Put the Scoped flag on function-local types. MSVC puts this flag for enum
  // type only when it has an immediate function scope. Clang never puts enums
  // inside DILexicalBlock scopes. Enum types, as generated by clang, are
  // always in function, class, or file scopes.
  if (Ty->getTag() == dwarf::DW_TAG_enumeration_type) {
    if (ImmediateScope && isa<DISubprogram>(ImmediateScope))
      CO |= ClassOptions::Scoped;
  } else {
    for (const DIScope *Scope = ImmediateScope; Scope != nullptr;
         Scope = Scope->getScope()) {
      if (isa<DISubprogram>(Scope)) {
        CO |= ClassOptions::Scoped;
        break;
      }
    }
  }

  return CO;
}

void CodeViewDebug::addUDTSrcLine(const DIType *Ty, TypeIndex TI) {
  switch (Ty->getTag()) {
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
    break;
  default:
    return;
  }

  if (const auto *File = Ty->getFile()) {
    StringIdRecord SIDR(TypeIndex(0x0), getFullFilepath(File));
    TypeIndex SIDI = TypeTable.writeLeafType(SIDR);

    UdtSourceLineRecord USLR(TI, SIDI, Ty->getLine());
    TypeTable.writeLeafType(USLR);
  }
}

TypeIndex CodeViewDebug::lowerTypeEnum(const DICompositeType *Ty) {
  ClassOptions CO = getCommonClassOptions(Ty);
  TypeIndex FTI;
  unsigned EnumeratorCount = 0;

  if (Ty->isForwardDecl()) {
    CO |= ClassOptions::ForwardReference;
  } else {
    ContinuationRecordBuilder ContinuationBuilder;
    ContinuationBuilder.begin(ContinuationRecordKind::FieldList);
    for (const DINode *Element : Ty->getElements()) {
      // We assume that the frontend provides all members in source declaration
      // order, which is what MSVC does.
      if (auto *Enumerator = dyn_cast_or_null<DIEnumerator>(Element)) {
        // FIXME: Is it correct to always emit these as unsigned here?
        EnumeratorRecord ER(MemberAccess::Public,
                            APSInt(Enumerator->getValue(), true),
                            Enumerator->getName());
        ContinuationBuilder.writeMemberType(ER);
        EnumeratorCount++;
      }
    }
    FTI = TypeTable.insertRecord(ContinuationBuilder);
  }

  std::string FullName = getFullyQualifiedName(Ty);

  EnumRecord ER(EnumeratorCount, CO, FTI, FullName, Ty->getIdentifier(),
                getTypeIndex(Ty->getBaseType()));
  TypeIndex EnumTI = TypeTable.writeLeafType(ER);

  addUDTSrcLine(Ty, EnumTI);

  return EnumTI;
}

//===----------------------------------------------------------------------===//
// ClassInfo
//===----------------------------------------------------------------------===//

struct llvm::ClassInfo {
  struct MemberInfo {
    const DIDerivedType *MemberTypeNode;
    uint64_t BaseOffset;
  };
  // [MemberInfo]
  using MemberList = std::vector<MemberInfo>;

  using MethodsList = TinyPtrVector<const DISubprogram *>;
  // MethodName -> MethodsList
  using MethodsMap = MapVector<MDString *, MethodsList>;

  /// Base classes.
  std::vector<const DIDerivedType *> Inheritance;

  /// Direct members.
  MemberList Members;
  // Direct overloaded methods gathered by name.
  MethodsMap Methods;

  TypeIndex VShapeTI;

  std::vector<const DIType *> NestedTypes;
};

void CodeViewDebug::clear() {
  assert(CurFn == nullptr);
  FileIdMap.clear();
  FnDebugInfo.clear();
  FileToFilepathMap.clear();
  LocalUDTs.clear();
  GlobalUDTs.clear();
  TypeIndices.clear();
  CompleteTypeIndices.clear();
  ScopeGlobals.clear();
  CVGlobalVariableOffsets.clear();
}

void CodeViewDebug::collectMemberInfo(ClassInfo &Info,
                                      const DIDerivedType *DDTy) {
  if (!DDTy->getName().empty()) {
    Info.Members.push_back({DDTy, 0});

    // Collect static const data members with values.
    if ((DDTy->getFlags() & DINode::FlagStaticMember) ==
        DINode::FlagStaticMember) {
      if (DDTy->getConstant() && (isa<ConstantInt>(DDTy->getConstant()) ||
                                  isa<ConstantFP>(DDTy->getConstant())))
        StaticConstMembers.push_back(DDTy);
    }

    return;
  }

  // An unnamed member may represent a nested struct or union. Attempt to
  // interpret the unnamed member as a DICompositeType possibly wrapped in
  // qualifier types. Add all the indirect fields to the current record if that
  // succeeds, and drop the member if that fails.
  assert((DDTy->getOffsetInBits() % 8) == 0 && "Unnamed bitfield member!");
  uint64_t Offset = DDTy->getOffsetInBits();
  const DIType *Ty = DDTy->getBaseType();
  bool FullyResolved = false;
  while (!FullyResolved) {
    switch (Ty->getTag()) {
    case dwarf::DW_TAG_const_type:
    case dwarf::DW_TAG_volatile_type:
      // FIXME: we should apply the qualifier types to the indirect fields
      // rather than dropping them.
      Ty = cast<DIDerivedType>(Ty)->getBaseType();
      break;
    default:
      FullyResolved = true;
      break;
    }
  }

  const DICompositeType *DCTy = dyn_cast<DICompositeType>(Ty);
  if (!DCTy)
    return;

  ClassInfo NestedInfo = collectClassInfo(DCTy);
  for (const ClassInfo::MemberInfo &IndirectField : NestedInfo.Members)
    Info.Members.push_back(
        {IndirectField.MemberTypeNode, IndirectField.BaseOffset + Offset});
}

ClassInfo CodeViewDebug::collectClassInfo(const DICompositeType *Ty) {
  ClassInfo Info;
  // Add elements to structure type.
  DINodeArray Elements = Ty->getElements();
  for (auto *Element : Elements) {
    // We assume that the frontend provides all members in source declaration
    // order, which is what MSVC does.
    if (!Element)
      continue;
    if (auto *SP = dyn_cast<DISubprogram>(Element)) {
      Info.Methods[SP->getRawName()].push_back(SP);
    } else if (auto *DDTy = dyn_cast<DIDerivedType>(Element)) {
      if (DDTy->getTag() == dwarf::DW_TAG_member) {
        collectMemberInfo(Info, DDTy);
      } else if (DDTy->getTag() == dwarf::DW_TAG_inheritance) {
        Info.Inheritance.push_back(DDTy);
      } else if (DDTy->getTag() == dwarf::DW_TAG_pointer_type &&
                 DDTy->getName() == "__vtbl_ptr_type") {
        Info.VShapeTI = getTypeIndex(DDTy);
      } else if (DDTy->getTag() == dwarf::DW_TAG_typedef) {
        Info.NestedTypes.push_back(DDTy);
      } else if (DDTy->getTag() == dwarf::DW_TAG_friend) {
        // Ignore friend members. It appears that MSVC emitted info about
        // friends in the past, but modern versions do not.
      }
    } else if (auto *Composite = dyn_cast<DICompositeType>(Element)) {
      Info.NestedTypes.push_back(Composite);
    }
    // Skip other unrecognized kinds of elements.
  }
  return Info;
}

static bool shouldAlwaysEmitCompleteClassType(const DICompositeType *Ty) {
  // This routine is used by lowerTypeClass and lowerTypeUnion to determine
  // if a complete type should be emitted instead of a forward reference.
  return Ty->getName().empty() && Ty->getIdentifier().empty() &&
      !Ty->isForwardDecl();
}

TypeIndex CodeViewDebug::lowerTypeClass(const DICompositeType *Ty) {
  // Emit the complete type for unnamed structs.  C++ classes with methods
  // which have a circular reference back to the class type are expected to
  // be named by the front-end and should not be "unnamed".  C unnamed
  // structs should not have circular references.
  if (shouldAlwaysEmitCompleteClassType(Ty)) {
    // If this unnamed complete type is already in the process of being defined
    // then the description of the type is malformed and cannot be emitted
    // into CodeView correctly so report a fatal error.
    auto I = CompleteTypeIndices.find(Ty);
    if (I != CompleteTypeIndices.end() && I->second == TypeIndex())
      report_fatal_error("cannot debug circular reference to unnamed type");
    return getCompleteTypeIndex(Ty);
  }

  // First, construct the forward decl.  Don't look into Ty to compute the
  // forward decl options, since it might not be available in all TUs.
  TypeRecordKind Kind = getRecordKind(Ty);
  ClassOptions CO =
      ClassOptions::ForwardReference | getCommonClassOptions(Ty);
  std::string FullName = getFullyQualifiedName(Ty);
  ClassRecord CR(Kind, 0, CO, TypeIndex(), TypeIndex(), TypeIndex(), 0,
                 FullName, Ty->getIdentifier());
  TypeIndex FwdDeclTI = TypeTable.writeLeafType(CR);
  if (!Ty->isForwardDecl())
    DeferredCompleteTypes.push_back(Ty);
  return FwdDeclTI;
}

TypeIndex CodeViewDebug::lowerCompleteTypeClass(const DICompositeType *Ty) {
  // Construct the field list and complete type record.
  TypeRecordKind Kind = getRecordKind(Ty);
  ClassOptions CO = getCommonClassOptions(Ty);
  TypeIndex FieldTI;
  TypeIndex VShapeTI;
  unsigned FieldCount;
  bool ContainsNestedClass;
  std::tie(FieldTI, VShapeTI, FieldCount, ContainsNestedClass) =
      lowerRecordFieldList(Ty);

  if (ContainsNestedClass)
    CO |= ClassOptions::ContainsNestedClass;

  // MSVC appears to set this flag by searching any destructor or method with
  // FunctionOptions::Constructor among the emitted members. Clang AST has all
  // the members, however special member functions are not yet emitted into
  // debug information. For now checking a class's non-triviality seems enough.
  // FIXME: not true for a nested unnamed struct.
  if (isNonTrivial(Ty))
    CO |= ClassOptions::HasConstructorOrDestructor;

  std::string FullName = getFullyQualifiedName(Ty);

  uint64_t SizeInBytes = Ty->getSizeInBits() / 8;

  ClassRecord CR(Kind, FieldCount, CO, FieldTI, TypeIndex(), VShapeTI,
                 SizeInBytes, FullName, Ty->getIdentifier());
  TypeIndex ClassTI = TypeTable.writeLeafType(CR);

  addUDTSrcLine(Ty, ClassTI);

  addToUDTs(Ty);

  return ClassTI;
}

TypeIndex CodeViewDebug::lowerTypeUnion(const DICompositeType *Ty) {
  // Emit the complete type for unnamed unions.
  if (shouldAlwaysEmitCompleteClassType(Ty))
    return getCompleteTypeIndex(Ty);

  ClassOptions CO =
      ClassOptions::ForwardReference | getCommonClassOptions(Ty);
  std::string FullName = getFullyQualifiedName(Ty);
  UnionRecord UR(0, CO, TypeIndex(), 0, FullName, Ty->getIdentifier());
  TypeIndex FwdDeclTI = TypeTable.writeLeafType(UR);
  if (!Ty->isForwardDecl())
    DeferredCompleteTypes.push_back(Ty);
  return FwdDeclTI;
}

TypeIndex CodeViewDebug::lowerCompleteTypeUnion(const DICompositeType *Ty) {
  ClassOptions CO = ClassOptions::Sealed | getCommonClassOptions(Ty);
  TypeIndex FieldTI;
  unsigned FieldCount;
  bool ContainsNestedClass;
  std::tie(FieldTI, std::ignore, FieldCount, ContainsNestedClass) =
      lowerRecordFieldList(Ty);

  if (ContainsNestedClass)
    CO |= ClassOptions::ContainsNestedClass;

  uint64_t SizeInBytes = Ty->getSizeInBits() / 8;
  std::string FullName = getFullyQualifiedName(Ty);

  UnionRecord UR(FieldCount, CO, FieldTI, SizeInBytes, FullName,
                 Ty->getIdentifier());
  TypeIndex UnionTI = TypeTable.writeLeafType(UR);

  addUDTSrcLine(Ty, UnionTI);

  addToUDTs(Ty);

  return UnionTI;
}

std::tuple<TypeIndex, TypeIndex, unsigned, bool>
CodeViewDebug::lowerRecordFieldList(const DICompositeType *Ty) {
  // Manually count members. MSVC appears to count everything that generates a
  // field list record. Each individual overload in a method overload group
  // contributes to this count, even though the overload group is a single field
  // list record.
  unsigned MemberCount = 0;
  ClassInfo Info = collectClassInfo(Ty);
  ContinuationRecordBuilder ContinuationBuilder;
  ContinuationBuilder.begin(ContinuationRecordKind::FieldList);

  // Create base classes.
  for (const DIDerivedType *I : Info.Inheritance) {
    if (I->getFlags() & DINode::FlagVirtual) {
      // Virtual base.
      unsigned VBPtrOffset = I->getVBPtrOffset();
      // FIXME: Despite the accessor name, the offset is really in bytes.
      unsigned VBTableIndex = I->getOffsetInBits() / 4;
      auto RecordKind = (I->getFlags() & DINode::FlagIndirectVirtualBase) == DINode::FlagIndirectVirtualBase
                            ? TypeRecordKind::IndirectVirtualBaseClass
                            : TypeRecordKind::VirtualBaseClass;
      VirtualBaseClassRecord VBCR(
          RecordKind, translateAccessFlags(Ty->getTag(), I->getFlags()),
          getTypeIndex(I->getBaseType()), getVBPTypeIndex(), VBPtrOffset,
          VBTableIndex);

      ContinuationBuilder.writeMemberType(VBCR);
      MemberCount++;
    } else {
      assert(I->getOffsetInBits() % 8 == 0 &&
             "bases must be on byte boundaries");
      BaseClassRecord BCR(translateAccessFlags(Ty->getTag(), I->getFlags()),
                          getTypeIndex(I->getBaseType()),
                          I->getOffsetInBits() / 8);
      ContinuationBuilder.writeMemberType(BCR);
      MemberCount++;
    }
  }

  // Create members.
  for (ClassInfo::MemberInfo &MemberInfo : Info.Members) {
    const DIDerivedType *Member = MemberInfo.MemberTypeNode;
    TypeIndex MemberBaseType = getTypeIndex(Member->getBaseType());
    StringRef MemberName = Member->getName();
    MemberAccess Access =
        translateAccessFlags(Ty->getTag(), Member->getFlags());

    if (Member->isStaticMember()) {
      StaticDataMemberRecord SDMR(Access, MemberBaseType, MemberName);
      ContinuationBuilder.writeMemberType(SDMR);
      MemberCount++;
      continue;
    }

    // Virtual function pointer member.
    if ((Member->getFlags() & DINode::FlagArtificial) &&
        Member->getName().starts_with("_vptr$")) {
      VFPtrRecord VFPR(getTypeIndex(Member->getBaseType()));
      ContinuationBuilder.writeMemberType(VFPR);
      MemberCount++;
      continue;
    }

    // Data member.
    uint64_t MemberOffsetInBits =
        Member->getOffsetInBits() + MemberInfo.BaseOffset;
    if (Member->isBitField()) {
      uint64_t StartBitOffset = MemberOffsetInBits;
      if (const auto *CI =
              dyn_cast_or_null<ConstantInt>(Member->getStorageOffsetInBits())) {
        MemberOffsetInBits = CI->getZExtValue() + MemberInfo.BaseOffset;
      }
      StartBitOffset -= MemberOffsetInBits;
      BitFieldRecord BFR(MemberBaseType, Member->getSizeInBits(),
                         StartBitOffset);
      MemberBaseType = TypeTable.writeLeafType(BFR);
    }
    uint64_t MemberOffsetInBytes = MemberOffsetInBits / 8;
    DataMemberRecord DMR(Access, MemberBaseType, MemberOffsetInBytes,
                         MemberName);
    ContinuationBuilder.writeMemberType(DMR);
    MemberCount++;
  }

  // Create methods
  for (auto &MethodItr : Info.Methods) {
    StringRef Name = MethodItr.first->getString();

    std::vector<OneMethodRecord> Methods;
    for (const DISubprogram *SP : MethodItr.second) {
      TypeIndex MethodType = getMemberFunctionType(SP, Ty);
      bool Introduced = SP->getFlags() & DINode::FlagIntroducedVirtual;

      unsigned VFTableOffset = -1;
      if (Introduced)
        VFTableOffset = SP->getVirtualIndex() * getPointerSizeInBytes();

      Methods.push_back(OneMethodRecord(
          MethodType, translateAccessFlags(Ty->getTag(), SP->getFlags()),
          translateMethodKindFlags(SP, Introduced),
          translateMethodOptionFlags(SP), VFTableOffset, Name));
      MemberCount++;
    }
    assert(!Methods.empty() && "Empty methods map entry");
    if (Methods.size() == 1)
      ContinuationBuilder.writeMemberType(Methods[0]);
    else {
      // FIXME: Make this use its own ContinuationBuilder so that
      // MethodOverloadList can be split correctly.
      MethodOverloadListRecord MOLR(Methods);
      TypeIndex MethodList = TypeTable.writeLeafType(MOLR);

      OverloadedMethodRecord OMR(Methods.size(), MethodList, Name);
      ContinuationBuilder.writeMemberType(OMR);
    }
  }

  // Create nested classes.
  for (const DIType *Nested : Info.NestedTypes) {
    NestedTypeRecord R(getTypeIndex(Nested), Nested->getName());
    ContinuationBuilder.writeMemberType(R);
    MemberCount++;
  }

  TypeIndex FieldTI = TypeTable.insertRecord(ContinuationBuilder);
  return std::make_tuple(FieldTI, Info.VShapeTI, MemberCount,
                         !Info.NestedTypes.empty());
}

TypeIndex CodeViewDebug::getVBPTypeIndex() {
  if (!VBPType.getIndex()) {
    // Make a 'const int *' type.
    ModifierRecord MR(TypeIndex::Int32(), ModifierOptions::Const);
    TypeIndex ModifiedTI = TypeTable.writeLeafType(MR);

    PointerKind PK = getPointerSizeInBytes() == 8 ? PointerKind::Near64
                                                  : PointerKind::Near32;
    PointerMode PM = PointerMode::Pointer;
    PointerOptions PO = PointerOptions::None;
    PointerRecord PR(ModifiedTI, PK, PM, PO, getPointerSizeInBytes());
    VBPType = TypeTable.writeLeafType(PR);
  }

  return VBPType;
}

TypeIndex CodeViewDebug::getTypeIndex(const DIType *Ty, const DIType *ClassTy) {
  // The null DIType is the void type. Don't try to hash it.
  if (!Ty)
    return TypeIndex::Void();

  // Check if we've already translated this type. Don't try to do a
  // get-or-create style insertion that caches the hash lookup across the
  // lowerType call. It will update the TypeIndices map.
  auto I = TypeIndices.find({Ty, ClassTy});
  if (I != TypeIndices.end())
    return I->second;

  TypeLoweringScope S(*this);
  TypeIndex TI = lowerType(Ty, ClassTy);
  return recordTypeIndexForDINode(Ty, TI, ClassTy);
}

codeview::TypeIndex
CodeViewDebug::getTypeIndexForThisPtr(const DIDerivedType *PtrTy,
                                      const DISubroutineType *SubroutineTy) {
  assert(PtrTy->getTag() == dwarf::DW_TAG_pointer_type &&
         "this type must be a pointer type");

  PointerOptions Options = PointerOptions::None;
  if (SubroutineTy->getFlags() & DINode::DIFlags::FlagLValueReference)
    Options = PointerOptions::LValueRefThisPointer;
  else if (SubroutineTy->getFlags() & DINode::DIFlags::FlagRValueReference)
    Options = PointerOptions::RValueRefThisPointer;

  // Check if we've already translated this type.  If there is no ref qualifier
  // on the function then we look up this pointer type with no associated class
  // so that the TypeIndex for the this pointer can be shared with the type
  // index for other pointers to this class type.  If there is a ref qualifier
  // then we lookup the pointer using the subroutine as the parent type.
  auto I = TypeIndices.find({PtrTy, SubroutineTy});
  if (I != TypeIndices.end())
    return I->second;

  TypeLoweringScope S(*this);
  TypeIndex TI = lowerTypePointer(PtrTy, Options);
  return recordTypeIndexForDINode(PtrTy, TI, SubroutineTy);
}

TypeIndex CodeViewDebug::getTypeIndexForReferenceTo(const DIType *Ty) {
  PointerRecord PR(getTypeIndex(Ty),
                   getPointerSizeInBytes() == 8 ? PointerKind::Near64
                                                : PointerKind::Near32,
                   PointerMode::LValueReference, PointerOptions::None,
                   Ty->getSizeInBits() / 8);
  return TypeTable.writeLeafType(PR);
}

TypeIndex CodeViewDebug::getCompleteTypeIndex(const DIType *Ty) {
  // The null DIType is the void type. Don't try to hash it.
  if (!Ty)
    return TypeIndex::Void();

  // Look through typedefs when getting the complete type index. Call
  // getTypeIndex on the typdef to ensure that any UDTs are accumulated and are
  // emitted only once.
  if (Ty->getTag() == dwarf::DW_TAG_typedef)
    (void)getTypeIndex(Ty);
  while (Ty->getTag() == dwarf::DW_TAG_typedef)
    Ty = cast<DIDerivedType>(Ty)->getBaseType();

  // If this is a non-record type, the complete type index is the same as the
  // normal type index. Just call getTypeIndex.
  switch (Ty->getTag()) {
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
    break;
  default:
    return getTypeIndex(Ty);
  }

  const auto *CTy = cast<DICompositeType>(Ty);

  TypeLoweringScope S(*this);

  // Make sure the forward declaration is emitted first. It's unclear if this
  // is necessary, but MSVC does it, and we should follow suit until we can show
  // otherwise.
  // We only emit a forward declaration for named types.
  if (!CTy->getName().empty() || !CTy->getIdentifier().empty()) {
    TypeIndex FwdDeclTI = getTypeIndex(CTy);

    // Just use the forward decl if we don't have complete type info. This
    // might happen if the frontend is using modules and expects the complete
    // definition to be emitted elsewhere.
    if (CTy->isForwardDecl())
      return FwdDeclTI;
  }

  // Check if we've already translated the complete record type.
  // Insert the type with a null TypeIndex to signify that the type is currently
  // being lowered.
  auto InsertResult = CompleteTypeIndices.insert({CTy, TypeIndex()});
  if (!InsertResult.second)
    return InsertResult.first->second;

  TypeIndex TI;
  switch (CTy->getTag()) {
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
    TI = lowerCompleteTypeClass(CTy);
    break;
  case dwarf::DW_TAG_union_type:
    TI = lowerCompleteTypeUnion(CTy);
    break;
  default:
    llvm_unreachable("not a record");
  }

  // Update the type index associated with this CompositeType.  This cannot
  // use the 'InsertResult' iterator above because it is potentially
  // invalidated by map insertions which can occur while lowering the class
  // type above.
  CompleteTypeIndices[CTy] = TI;
  return TI;
}

/// Emit all the deferred complete record types. Try to do this in FIFO order,
/// and do this until fixpoint, as each complete record type typically
/// references
/// many other record types.
void CodeViewDebug::emitDeferredCompleteTypes() {
  SmallVector<const DICompositeType *, 4> TypesToEmit;
  while (!DeferredCompleteTypes.empty()) {
    std::swap(DeferredCompleteTypes, TypesToEmit);
    for (const DICompositeType *RecordTy : TypesToEmit)
      getCompleteTypeIndex(RecordTy);
    TypesToEmit.clear();
  }
}

void CodeViewDebug::emitLocalVariableList(const FunctionInfo &FI,
                                          ArrayRef<LocalVariable> Locals) {
  // Get the sorted list of parameters and emit them first.
  SmallVector<const LocalVariable *, 6> Params;
  for (const LocalVariable &L : Locals)
    if (L.DIVar->isParameter())
      Params.push_back(&L);
  llvm::sort(Params, [](const LocalVariable *L, const LocalVariable *R) {
    return L->DIVar->getArg() < R->DIVar->getArg();
  });
  for (const LocalVariable *L : Params)
    emitLocalVariable(FI, *L);

  // Next emit all non-parameters in the order that we found them.
  for (const LocalVariable &L : Locals) {
    if (!L.DIVar->isParameter()) {
      if (L.ConstantValue) {
        // If ConstantValue is set we will emit it as a S_CONSTANT instead of a
        // S_LOCAL in order to be able to represent it at all.
        const DIType *Ty = L.DIVar->getType();
        APSInt Val(*L.ConstantValue);
        emitConstantSymbolRecord(Ty, Val, std::string(L.DIVar->getName()));
      } else {
        emitLocalVariable(FI, L);
      }
    }
  }
}

void CodeViewDebug::emitLocalVariable(const FunctionInfo &FI,
                                      const LocalVariable &Var) {
  // LocalSym record, see SymbolRecord.h for more info.
  MCSymbol *LocalEnd = beginSymbolRecord(SymbolKind::S_LOCAL);

  LocalSymFlags Flags = LocalSymFlags::None;
  if (Var.DIVar->isParameter())
    Flags |= LocalSymFlags::IsParameter;
  if (Var.DefRanges.empty())
    Flags |= LocalSymFlags::IsOptimizedOut;

  OS.AddComment("TypeIndex");
  TypeIndex TI = Var.UseReferenceType
                     ? getTypeIndexForReferenceTo(Var.DIVar->getType())
                     : getCompleteTypeIndex(Var.DIVar->getType());
  OS.emitInt32(TI.getIndex());
  OS.AddComment("Flags");
  OS.emitInt16(static_cast<uint16_t>(Flags));
  // Truncate the name so we won't overflow the record length field.
  emitNullTerminatedSymbolName(OS, Var.DIVar->getName());
  endSymbolRecord(LocalEnd);

  // Calculate the on disk prefix of the appropriate def range record. The
  // records and on disk formats are described in SymbolRecords.h. BytePrefix
  // should be big enough to hold all forms without memory allocation.
  SmallString<20> BytePrefix;
  for (const auto &Pair : Var.DefRanges) {
    LocalVarDef DefRange = Pair.first;
    const auto &Ranges = Pair.second;
    BytePrefix.clear();
    if (DefRange.InMemory) {
      int Offset = DefRange.DataOffset;
      unsigned Reg = DefRange.CVRegister;

      // 32-bit x86 call sequences often use PUSH instructions, which disrupt
      // ESP-relative offsets. Use the virtual frame pointer, VFRAME or $T0,
      // instead. In frames without stack realignment, $T0 will be the CFA.
      if (RegisterId(Reg) == RegisterId::ESP) {
        Reg = unsigned(RegisterId::VFRAME);
        Offset += FI.OffsetAdjustment;
      }

      // If we can use the chosen frame pointer for the frame and this isn't a
      // sliced aggregate, use the smaller S_DEFRANGE_FRAMEPOINTER_REL record.
      // Otherwise, use S_DEFRANGE_REGISTER_REL.
      EncodedFramePtrReg EncFP = encodeFramePtrReg(RegisterId(Reg), TheCPU);
      if (!DefRange.IsSubfield && EncFP != EncodedFramePtrReg::None &&
          (bool(Flags & LocalSymFlags::IsParameter)
               ? (EncFP == FI.EncodedParamFramePtrReg)
               : (EncFP == FI.EncodedLocalFramePtrReg))) {
        DefRangeFramePointerRelHeader DRHdr;
        DRHdr.Offset = Offset;
        OS.emitCVDefRangeDirective(Ranges, DRHdr);
      } else {
        uint16_t RegRelFlags = 0;
        if (DefRange.IsSubfield) {
          RegRelFlags = DefRangeRegisterRelSym::IsSubfieldFlag |
                        (DefRange.StructOffset
                         << DefRangeRegisterRelSym::OffsetInParentShift);
        }
        DefRangeRegisterRelHeader DRHdr;
        DRHdr.Register = Reg;
        DRHdr.Flags = RegRelFlags;
        DRHdr.BasePointerOffset = Offset;
        OS.emitCVDefRangeDirective(Ranges, DRHdr);
      }
    } else {
      assert(DefRange.DataOffset == 0 && "unexpected offset into register");
      if (DefRange.IsSubfield) {
        DefRangeSubfieldRegisterHeader DRHdr;
        DRHdr.Register = DefRange.CVRegister;
        DRHdr.MayHaveNoName = 0;
        DRHdr.OffsetInParent = DefRange.StructOffset;
        OS.emitCVDefRangeDirective(Ranges, DRHdr);
      } else {
        DefRangeRegisterHeader DRHdr;
        DRHdr.Register = DefRange.CVRegister;
        DRHdr.MayHaveNoName = 0;
        OS.emitCVDefRangeDirective(Ranges, DRHdr);
      }
    }
  }
}

void CodeViewDebug::emitLexicalBlockList(ArrayRef<LexicalBlock *> Blocks,
                                         const FunctionInfo& FI) {
  for (LexicalBlock *Block : Blocks)
    emitLexicalBlock(*Block, FI);
}

/// Emit an S_BLOCK32 and S_END record pair delimiting the contents of a
/// lexical block scope.
void CodeViewDebug::emitLexicalBlock(const LexicalBlock &Block,
                                     const FunctionInfo& FI) {
  MCSymbol *RecordEnd = beginSymbolRecord(SymbolKind::S_BLOCK32);
  OS.AddComment("PtrParent");
  OS.emitInt32(0); // PtrParent
  OS.AddComment("PtrEnd");
  OS.emitInt32(0); // PtrEnd
  OS.AddComment("Code size");
  OS.emitAbsoluteSymbolDiff(Block.End, Block.Begin, 4);   // Code Size
  OS.AddComment("Function section relative address");
  OS.emitCOFFSecRel32(Block.Begin, /*Offset=*/0); // Func Offset
  OS.AddComment("Function section index");
  OS.emitCOFFSectionIndex(FI.Begin); // Func Symbol
  OS.AddComment("Lexical block name");
  emitNullTerminatedSymbolName(OS, Block.Name);           // Name
  endSymbolRecord(RecordEnd);

  // Emit variables local to this lexical block.
  emitLocalVariableList(FI, Block.Locals);
  emitGlobalVariableList(Block.Globals);

  // Emit lexical blocks contained within this block.
  emitLexicalBlockList(Block.Children, FI);

  // Close the lexical block scope.
  emitEndSymbolRecord(SymbolKind::S_END);
}

/// Convenience routine for collecting lexical block information for a list
/// of lexical scopes.
void CodeViewDebug::collectLexicalBlockInfo(
        SmallVectorImpl<LexicalScope *> &Scopes,
        SmallVectorImpl<LexicalBlock *> &Blocks,
        SmallVectorImpl<LocalVariable> &Locals,
        SmallVectorImpl<CVGlobalVariable> &Globals) {
  for (LexicalScope *Scope : Scopes)
    collectLexicalBlockInfo(*Scope, Blocks, Locals, Globals);
}

/// Populate the lexical blocks and local variable lists of the parent with
/// information about the specified lexical scope.
void CodeViewDebug::collectLexicalBlockInfo(
    LexicalScope &Scope,
    SmallVectorImpl<LexicalBlock *> &ParentBlocks,
    SmallVectorImpl<LocalVariable> &ParentLocals,
    SmallVectorImpl<CVGlobalVariable> &ParentGlobals) {
  if (Scope.isAbstractScope())
    return;

  // Gather information about the lexical scope including local variables,
  // global variables, and address ranges.
  bool IgnoreScope = false;
  auto LI = ScopeVariables.find(&Scope);
  SmallVectorImpl<LocalVariable> *Locals =
      LI != ScopeVariables.end() ? &LI->second : nullptr;
  auto GI = ScopeGlobals.find(Scope.getScopeNode());
  SmallVectorImpl<CVGlobalVariable> *Globals =
      GI != ScopeGlobals.end() ? GI->second.get() : nullptr;
  const DILexicalBlock *DILB = dyn_cast<DILexicalBlock>(Scope.getScopeNode());
  const SmallVectorImpl<InsnRange> &Ranges = Scope.getRanges();

  // Ignore lexical scopes which do not contain variables.
  if (!Locals && !Globals)
    IgnoreScope = true;

  // Ignore lexical scopes which are not lexical blocks.
  if (!DILB)
    IgnoreScope = true;

  // Ignore scopes which have too many address ranges to represent in the
  // current CodeView format or do not have a valid address range.
  //
  // For lexical scopes with multiple address ranges you may be tempted to
  // construct a single range covering every instruction where the block is
  // live and everything in between.  Unfortunately, Visual Studio only
  // displays variables from the first matching lexical block scope.  If the
  // first lexical block contains exception handling code or cold code which
  // is moved to the bottom of the routine creating a single range covering
  // nearly the entire routine, then it will hide all other lexical blocks
  // and the variables they contain.
  if (Ranges.size() != 1 || !getLabelAfterInsn(Ranges.front().second))
    IgnoreScope = true;

  if (IgnoreScope) {
    // This scope can be safely ignored and eliminating it will reduce the
    // size of the debug information. Be sure to collect any variable and scope
    // information from the this scope or any of its children and collapse them
    // into the parent scope.
    if (Locals)
      ParentLocals.append(Locals->begin(), Locals->end());
    if (Globals)
      ParentGlobals.append(Globals->begin(), Globals->end());
    collectLexicalBlockInfo(Scope.getChildren(),
                            ParentBlocks,
                            ParentLocals,
                            ParentGlobals);
    return;
  }

  // Create a new CodeView lexical block for this lexical scope.  If we've
  // seen this DILexicalBlock before then the scope tree is malformed and
  // we can handle this gracefully by not processing it a second time.
  auto BlockInsertion = CurFn->LexicalBlocks.insert({DILB, LexicalBlock()});
  if (!BlockInsertion.second)
    return;

  // Create a lexical block containing the variables and collect the
  // lexical block information for the children.
  const InsnRange &Range = Ranges.front();
  assert(Range.first && Range.second);
  LexicalBlock &Block = BlockInsertion.first->second;
  Block.Begin = getLabelBeforeInsn(Range.first);
  Block.End = getLabelAfterInsn(Range.second);
  assert(Block.Begin && "missing label for scope begin");
  assert(Block.End && "missing label for scope end");
  Block.Name = DILB->getName();
  if (Locals)
    Block.Locals = std::move(*Locals);
  if (Globals)
    Block.Globals = std::move(*Globals);
  ParentBlocks.push_back(&Block);
  collectLexicalBlockInfo(Scope.getChildren(),
                          Block.Children,
                          Block.Locals,
                          Block.Globals);
}

void CodeViewDebug::endFunctionImpl(const MachineFunction *MF) {
  const Function &GV = MF->getFunction();
  assert(FnDebugInfo.count(&GV));
  assert(CurFn == FnDebugInfo[&GV].get());

  collectVariableInfo(GV.getSubprogram());

  // Build the lexical block structure to emit for this routine.
  if (LexicalScope *CFS = LScopes.getCurrentFunctionScope())
    collectLexicalBlockInfo(*CFS,
                            CurFn->ChildBlocks,
                            CurFn->Locals,
                            CurFn->Globals);

  // Clear the scope and variable information from the map which will not be
  // valid after we have finished processing this routine.  This also prepares
  // the map for the subsequent routine.
  ScopeVariables.clear();

  // Don't emit anything if we don't have any line tables.
  // Thunks are compiler-generated and probably won't have source correlation.
  if (!CurFn->HaveLineInfo && !GV.getSubprogram()->isThunk()) {
    FnDebugInfo.erase(&GV);
    CurFn = nullptr;
    return;
  }

  // Find heap alloc sites and add to list.
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (MDNode *MD = MI.getHeapAllocMarker()) {
        CurFn->HeapAllocSites.push_back(std::make_tuple(getLabelBeforeInsn(&MI),
                                                        getLabelAfterInsn(&MI),
                                                        dyn_cast<DIType>(MD)));
      }
    }
  }

  bool isThumb = Triple(MMI->getModule()->getTargetTriple()).getArch() ==
                 llvm::Triple::ArchType::thumb;
  collectDebugInfoForJumpTables(MF, isThumb);

  CurFn->Annotations = MF->getCodeViewAnnotations();

  CurFn->End = Asm->getFunctionEnd();

  CurFn = nullptr;
}

// Usable locations are valid with non-zero line numbers. A line number of zero
// corresponds to optimized code that doesn't have a distinct source location.
// In this case, we try to use the previous or next source location depending on
// the context.
static bool isUsableDebugLoc(DebugLoc DL) {
  return DL && DL.getLine() != 0;
}

void CodeViewDebug::beginInstruction(const MachineInstr *MI) {
  DebugHandlerBase::beginInstruction(MI);

  // Ignore DBG_VALUE and DBG_LABEL locations and function prologue.
  if (!Asm || !CurFn || MI->isDebugInstr() ||
      MI->getFlag(MachineInstr::FrameSetup))
    return;

  // If the first instruction of a new MBB has no location, find the first
  // instruction with a location and use that.
  DebugLoc DL = MI->getDebugLoc();
  if (!isUsableDebugLoc(DL) && MI->getParent() != PrevInstBB) {
    for (const auto &NextMI : *MI->getParent()) {
      if (NextMI.isDebugInstr())
        continue;
      DL = NextMI.getDebugLoc();
      if (isUsableDebugLoc(DL))
        break;
    }
    // FIXME: Handle the case where the BB has no valid locations. This would
    // probably require doing a real dataflow analysis.
  }
  PrevInstBB = MI->getParent();

  // If we still don't have a debug location, don't record a location.
  if (!isUsableDebugLoc(DL))
    return;

  maybeRecordLocation(DL, Asm->MF);
}

MCSymbol *CodeViewDebug::beginCVSubsection(DebugSubsectionKind Kind) {
  MCSymbol *BeginLabel = MMI->getContext().createTempSymbol(),
           *EndLabel = MMI->getContext().createTempSymbol();
  OS.emitInt32(unsigned(Kind));
  OS.AddComment("Subsection size");
  OS.emitAbsoluteSymbolDiff(EndLabel, BeginLabel, 4);
  OS.emitLabel(BeginLabel);
  return EndLabel;
}

void CodeViewDebug::endCVSubsection(MCSymbol *EndLabel) {
  OS.emitLabel(EndLabel);
  // Every subsection must be aligned to a 4-byte boundary.
  OS.emitValueToAlignment(Align(4));
}

static StringRef getSymbolName(SymbolKind SymKind) {
  for (const EnumEntry<SymbolKind> &EE : getSymbolTypeNames())
    if (EE.Value == SymKind)
      return EE.Name;
  return "";
}

MCSymbol *CodeViewDebug::beginSymbolRecord(SymbolKind SymKind) {
  MCSymbol *BeginLabel = MMI->getContext().createTempSymbol(),
           *EndLabel = MMI->getContext().createTempSymbol();
  OS.AddComment("Record length");
  OS.emitAbsoluteSymbolDiff(EndLabel, BeginLabel, 2);
  OS.emitLabel(BeginLabel);
  if (OS.isVerboseAsm())
    OS.AddComment("Record kind: " + getSymbolName(SymKind));
  OS.emitInt16(unsigned(SymKind));
  return EndLabel;
}

void CodeViewDebug::endSymbolRecord(MCSymbol *SymEnd) {
  // MSVC does not pad out symbol records to four bytes, but LLVM does to avoid
  // an extra copy of every symbol record in LLD. This increases object file
  // size by less than 1% in the clang build, and is compatible with the Visual
  // C++ linker.
  OS.emitValueToAlignment(Align(4));
  OS.emitLabel(SymEnd);
}

void CodeViewDebug::emitEndSymbolRecord(SymbolKind EndKind) {
  OS.AddComment("Record length");
  OS.emitInt16(2);
  if (OS.isVerboseAsm())
    OS.AddComment("Record kind: " + getSymbolName(EndKind));
  OS.emitInt16(uint16_t(EndKind)); // Record Kind
}

void CodeViewDebug::emitDebugInfoForUDTs(
    const std::vector<std::pair<std::string, const DIType *>> &UDTs) {
#ifndef NDEBUG
  size_t OriginalSize = UDTs.size();
#endif
  for (const auto &UDT : UDTs) {
    const DIType *T = UDT.second;
    assert(shouldEmitUdt(T));
    MCSymbol *UDTRecordEnd = beginSymbolRecord(SymbolKind::S_UDT);
    OS.AddComment("Type");
    OS.emitInt32(getCompleteTypeIndex(T).getIndex());
    assert(OriginalSize == UDTs.size() &&
           "getCompleteTypeIndex found new UDTs!");
    emitNullTerminatedSymbolName(OS, UDT.first);
    endSymbolRecord(UDTRecordEnd);
  }
}

void CodeViewDebug::collectGlobalVariableInfo() {
  DenseMap<const DIGlobalVariableExpression *, const GlobalVariable *>
      GlobalMap;
  for (const GlobalVariable &GV : MMI->getModule()->globals()) {
    SmallVector<DIGlobalVariableExpression *, 1> GVEs;
    GV.getDebugInfo(GVEs);
    for (const auto *GVE : GVEs)
      GlobalMap[GVE] = &GV;
  }

  NamedMDNode *CUs = MMI->getModule()->getNamedMetadata("llvm.dbg.cu");
  for (const MDNode *Node : CUs->operands()) {
    const auto *CU = cast<DICompileUnit>(Node);
    for (const auto *GVE : CU->getGlobalVariables()) {
      const DIGlobalVariable *DIGV = GVE->getVariable();
      const DIExpression *DIE = GVE->getExpression();
      // Don't emit string literals in CodeView, as the only useful parts are
      // generally the filename and line number, which isn't possible to output
      // in CodeView. String literals should be the only unnamed GlobalVariable
      // with debug info.
      if (DIGV->getName().empty()) continue;

      if ((DIE->getNumElements() == 2) &&
          (DIE->getElement(0) == dwarf::DW_OP_plus_uconst))
        // Record the constant offset for the variable.
        //
        // A Fortran common block uses this idiom to encode the offset
        // of a variable from the common block's starting address.
        CVGlobalVariableOffsets.insert(
            std::make_pair(DIGV, DIE->getElement(1)));

      // Emit constant global variables in a global symbol section.
      if (GlobalMap.count(GVE) == 0 && DIE->isConstant()) {
        CVGlobalVariable CVGV = {DIGV, DIE};
        GlobalVariables.emplace_back(std::move(CVGV));
      }

      const auto *GV = GlobalMap.lookup(GVE);
      if (!GV || GV->isDeclarationForLinker())
        continue;

      DIScope *Scope = DIGV->getScope();
      SmallVector<CVGlobalVariable, 1> *VariableList;
      if (Scope && isa<DILocalScope>(Scope)) {
        // Locate a global variable list for this scope, creating one if
        // necessary.
        auto Insertion = ScopeGlobals.insert(
            {Scope, std::unique_ptr<GlobalVariableList>()});
        if (Insertion.second)
          Insertion.first->second = std::make_unique<GlobalVariableList>();
        VariableList = Insertion.first->second.get();
      } else if (GV->hasComdat())
        // Emit this global variable into a COMDAT section.
        VariableList = &ComdatVariables;
      else
        // Emit this global variable in a single global symbol section.
        VariableList = &GlobalVariables;
      CVGlobalVariable CVGV = {DIGV, GV};
      VariableList->emplace_back(std::move(CVGV));
    }
  }
}

void CodeViewDebug::collectDebugInfoForGlobals() {
  for (const CVGlobalVariable &CVGV : GlobalVariables) {
    const DIGlobalVariable *DIGV = CVGV.DIGV;
    const DIScope *Scope = DIGV->getScope();
    getCompleteTypeIndex(DIGV->getType());
    getFullyQualifiedName(Scope, DIGV->getName());
  }

  for (const CVGlobalVariable &CVGV : ComdatVariables) {
    const DIGlobalVariable *DIGV = CVGV.DIGV;
    const DIScope *Scope = DIGV->getScope();
    getCompleteTypeIndex(DIGV->getType());
    getFullyQualifiedName(Scope, DIGV->getName());
  }
}

void CodeViewDebug::emitDebugInfoForGlobals() {
  // First, emit all globals that are not in a comdat in a single symbol
  // substream. MSVC doesn't like it if the substream is empty, so only open
  // it if we have at least one global to emit.
  switchToDebugSectionForSymbol(nullptr);
  if (!GlobalVariables.empty() || !StaticConstMembers.empty()) {
    OS.AddComment("Symbol subsection for globals");
    MCSymbol *EndLabel = beginCVSubsection(DebugSubsectionKind::Symbols);
    emitGlobalVariableList(GlobalVariables);
    emitStaticConstMemberList();
    endCVSubsection(EndLabel);
  }

  // Second, emit each global that is in a comdat into its own .debug$S
  // section along with its own symbol substream.
  for (const CVGlobalVariable &CVGV : ComdatVariables) {
    const GlobalVariable *GV = cast<const GlobalVariable *>(CVGV.GVInfo);
    MCSymbol *GVSym = Asm->getSymbol(GV);
    OS.AddComment("Symbol subsection for " +
                  Twine(GlobalValue::dropLLVMManglingEscape(GV->getName())));
    switchToDebugSectionForSymbol(GVSym);
    MCSymbol *EndLabel = beginCVSubsection(DebugSubsectionKind::Symbols);
    // FIXME: emitDebugInfoForGlobal() doesn't handle DIExpressions.
    emitDebugInfoForGlobal(CVGV);
    endCVSubsection(EndLabel);
  }
}

void CodeViewDebug::emitDebugInfoForRetainedTypes() {
  NamedMDNode *CUs = MMI->getModule()->getNamedMetadata("llvm.dbg.cu");
  for (const MDNode *Node : CUs->operands()) {
    for (auto *Ty : cast<DICompileUnit>(Node)->getRetainedTypes()) {
      if (DIType *RT = dyn_cast<DIType>(Ty)) {
        getTypeIndex(RT);
        // FIXME: Add to global/local DTU list.
      }
    }
  }
}

// Emit each global variable in the specified array.
void CodeViewDebug::emitGlobalVariableList(ArrayRef<CVGlobalVariable> Globals) {
  for (const CVGlobalVariable &CVGV : Globals) {
    // FIXME: emitDebugInfoForGlobal() doesn't handle DIExpressions.
    emitDebugInfoForGlobal(CVGV);
  }
}

void CodeViewDebug::emitConstantSymbolRecord(const DIType *DTy, APSInt &Value,
                                             const std::string &QualifiedName) {
  MCSymbol *SConstantEnd = beginSymbolRecord(SymbolKind::S_CONSTANT);
  OS.AddComment("Type");
  OS.emitInt32(getTypeIndex(DTy).getIndex());

  OS.AddComment("Value");

  // Encoded integers shouldn't need more than 10 bytes.
  uint8_t Data[10];
  BinaryStreamWriter Writer(Data, llvm::endianness::little);
  CodeViewRecordIO IO(Writer);
  cantFail(IO.mapEncodedInteger(Value));
  StringRef SRef((char *)Data, Writer.getOffset());
  OS.emitBinaryData(SRef);

  OS.AddComment("Name");
  emitNullTerminatedSymbolName(OS, QualifiedName);
  endSymbolRecord(SConstantEnd);
}

void CodeViewDebug::emitStaticConstMemberList() {
  for (const DIDerivedType *DTy : StaticConstMembers) {
    const DIScope *Scope = DTy->getScope();

    APSInt Value;
    if (const ConstantInt *CI =
            dyn_cast_or_null<ConstantInt>(DTy->getConstant()))
      Value = APSInt(CI->getValue(),
                     DebugHandlerBase::isUnsignedDIType(DTy->getBaseType()));
    else if (const ConstantFP *CFP =
                 dyn_cast_or_null<ConstantFP>(DTy->getConstant()))
      Value = APSInt(CFP->getValueAPF().bitcastToAPInt(), true);
    else
      llvm_unreachable("cannot emit a constant without a value");

    emitConstantSymbolRecord(DTy->getBaseType(), Value,
                             getFullyQualifiedName(Scope, DTy->getName()));
  }
}

static bool isFloatDIType(const DIType *Ty) {
  if (isa<DICompositeType>(Ty))
    return false;

  if (auto *DTy = dyn_cast<DIDerivedType>(Ty)) {
    dwarf::Tag T = (dwarf::Tag)Ty->getTag();
    if (T == dwarf::DW_TAG_pointer_type ||
        T == dwarf::DW_TAG_ptr_to_member_type ||
        T == dwarf::DW_TAG_reference_type ||
        T == dwarf::DW_TAG_rvalue_reference_type)
      return false;
    assert(DTy->getBaseType() && "Expected valid base type");
    return isFloatDIType(DTy->getBaseType());
  }

  auto *BTy = cast<DIBasicType>(Ty);
  return (BTy->getEncoding() == dwarf::DW_ATE_float);
}

void CodeViewDebug::emitDebugInfoForGlobal(const CVGlobalVariable &CVGV) {
  const DIGlobalVariable *DIGV = CVGV.DIGV;

  const DIScope *Scope = DIGV->getScope();
  // For static data members, get the scope from the declaration.
  if (const auto *MemberDecl = dyn_cast_or_null<DIDerivedType>(
          DIGV->getRawStaticDataMemberDeclaration()))
    Scope = MemberDecl->getScope();
  // For static local variables and Fortran, the scoping portion is elided
  // in its name so that we can reference the variable in the command line
  // of the VS debugger.
  std::string QualifiedName =
      (moduleIsInFortran() || (Scope && isa<DILocalScope>(Scope)))
          ? std::string(DIGV->getName())
          : getFullyQualifiedName(Scope, DIGV->getName());

  if (const GlobalVariable *GV =
          dyn_cast_if_present<const GlobalVariable *>(CVGV.GVInfo)) {
    // DataSym record, see SymbolRecord.h for more info. Thread local data
    // happens to have the same format as global data.
    MCSymbol *GVSym = Asm->getSymbol(GV);
    SymbolKind DataSym = GV->isThreadLocal()
                             ? (DIGV->isLocalToUnit() ? SymbolKind::S_LTHREAD32
                                                      : SymbolKind::S_GTHREAD32)
                             : (DIGV->isLocalToUnit() ? SymbolKind::S_LDATA32
                                                      : SymbolKind::S_GDATA32);
    MCSymbol *DataEnd = beginSymbolRecord(DataSym);
    OS.AddComment("Type");
    OS.emitInt32(getCompleteTypeIndex(DIGV->getType()).getIndex());
    OS.AddComment("DataOffset");

    uint64_t Offset = 0;
    if (CVGlobalVariableOffsets.contains(DIGV))
      // Use the offset seen while collecting info on globals.
      Offset = CVGlobalVariableOffsets[DIGV];
    OS.emitCOFFSecRel32(GVSym, Offset);

    OS.AddComment("Segment");
    OS.emitCOFFSectionIndex(GVSym);
    OS.AddComment("Name");
    const unsigned LengthOfDataRecord = 12;
    emitNullTerminatedSymbolName(OS, QualifiedName, LengthOfDataRecord);
    endSymbolRecord(DataEnd);
  } else {
    const DIExpression *DIE = cast<const DIExpression *>(CVGV.GVInfo);
    assert(DIE->isConstant() &&
           "Global constant variables must contain a constant expression.");

    // Use unsigned for floats.
    bool isUnsigned = isFloatDIType(DIGV->getType())
                          ? true
                          : DebugHandlerBase::isUnsignedDIType(DIGV->getType());
    APSInt Value(APInt(/*BitWidth=*/64, DIE->getElement(1)), isUnsigned);
    emitConstantSymbolRecord(DIGV->getType(), Value, QualifiedName);
  }
}

void forEachJumpTableBranch(
    const MachineFunction *MF, bool isThumb,
    const std::function<void(const MachineJumpTableInfo &, const MachineInstr &,
                             int64_t)> &Callback) {
  auto JTI = MF->getJumpTableInfo();
  if (JTI && !JTI->isEmpty()) {
#ifndef NDEBUG
    auto UsedJTs = llvm::SmallBitVector(JTI->getJumpTables().size());
#endif
    for (const auto &MBB : *MF) {
      // Search for indirect branches...
      const auto LastMI = MBB.getFirstTerminator();
      if (LastMI != MBB.end() && LastMI->isIndirectBranch()) {
        if (isThumb) {
          // ... that directly use jump table operands.
          // NOTE: ARM uses pattern matching to lower its BR_JT SDNode to
          // machine instructions, hence inserting a JUMP_TABLE_DEBUG_INFO node
          // interferes with this process *but* the resulting pseudo-instruction
          // uses a Jump Table operand, so extract the jump table index directly
          // from that.
          for (const auto &MO : LastMI->operands()) {
            if (MO.isJTI()) {
              unsigned Index = MO.getIndex();
#ifndef NDEBUG
              UsedJTs.set(Index);
#endif
              Callback(*JTI, *LastMI, Index);
              break;
            }
          }
        } else {
          // ... that have jump table debug info.
          // NOTE: The debug info is inserted as a JUMP_TABLE_DEBUG_INFO node
          // when lowering the BR_JT SDNode to an indirect branch.
          for (auto I = MBB.instr_rbegin(), E = MBB.instr_rend(); I != E; ++I) {
            if (I->isJumpTableDebugInfo()) {
              unsigned Index = I->getOperand(0).getImm();
#ifndef NDEBUG
              UsedJTs.set(Index);
#endif
              Callback(*JTI, *LastMI, Index);
              break;
            }
          }
        }
      }
    }
#ifndef NDEBUG
    assert(UsedJTs.all() &&
           "Some of jump tables were not used in a debug info instruction");
#endif
  }
}

void CodeViewDebug::discoverJumpTableBranches(const MachineFunction *MF,
                                              bool isThumb) {
  forEachJumpTableBranch(
      MF, isThumb,
      [this](const MachineJumpTableInfo &, const MachineInstr &BranchMI,
             int64_t) { requestLabelBeforeInsn(&BranchMI); });
}

void CodeViewDebug::collectDebugInfoForJumpTables(const MachineFunction *MF,
                                                  bool isThumb) {
  forEachJumpTableBranch(
      MF, isThumb,
      [this, MF](const MachineJumpTableInfo &JTI, const MachineInstr &BranchMI,
                 int64_t JumpTableIndex) {
        // For label-difference jump tables, find the base expression.
        // Otherwise the jump table uses an absolute address (so no base
        // is required).
        const MCSymbol *Base;
        uint64_t BaseOffset = 0;
        const MCSymbol *Branch = getLabelBeforeInsn(&BranchMI);
        JumpTableEntrySize EntrySize;
        switch (JTI.getEntryKind()) {
        case MachineJumpTableInfo::EK_Custom32:
        case MachineJumpTableInfo::EK_GPRel32BlockAddress:
        case MachineJumpTableInfo::EK_GPRel64BlockAddress:
          llvm_unreachable(
              "EK_Custom32, EK_GPRel32BlockAddress, and "
              "EK_GPRel64BlockAddress should never be emitted for COFF");
        case MachineJumpTableInfo::EK_BlockAddress:
          // Each entry is an absolute address.
          EntrySize = JumpTableEntrySize::Pointer;
          Base = nullptr;
          break;
        case MachineJumpTableInfo::EK_Inline:
        case MachineJumpTableInfo::EK_LabelDifference32:
        case MachineJumpTableInfo::EK_LabelDifference64:
          // Ask the AsmPrinter.
          std::tie(Base, BaseOffset, Branch, EntrySize) =
              Asm->getCodeViewJumpTableInfo(JumpTableIndex, &BranchMI, Branch);
          break;
        }

        CurFn->JumpTables.push_back(
            {EntrySize, Base, BaseOffset, Branch,
             MF->getJTISymbol(JumpTableIndex, MMI->getContext()),
             JTI.getJumpTables()[JumpTableIndex].MBBs.size()});
      });
}

void CodeViewDebug::emitDebugInfoForJumpTables(const FunctionInfo &FI) {
  for (auto JumpTable : FI.JumpTables) {
    MCSymbol *JumpTableEnd = beginSymbolRecord(SymbolKind::S_ARMSWITCHTABLE);
    if (JumpTable.Base) {
      OS.AddComment("Base offset");
      OS.emitCOFFSecRel32(JumpTable.Base, JumpTable.BaseOffset);
      OS.AddComment("Base section index");
      OS.emitCOFFSectionIndex(JumpTable.Base);
    } else {
      OS.AddComment("Base offset");
      OS.emitInt32(0);
      OS.AddComment("Base section index");
      OS.emitInt16(0);
    }
    OS.AddComment("Switch type");
    OS.emitInt16(static_cast<uint16_t>(JumpTable.EntrySize));
    OS.AddComment("Branch offset");
    OS.emitCOFFSecRel32(JumpTable.Branch, /*Offset=*/0);
    OS.AddComment("Table offset");
    OS.emitCOFFSecRel32(JumpTable.Table, /*Offset=*/0);
    OS.AddComment("Branch section index");
    OS.emitCOFFSectionIndex(JumpTable.Branch);
    OS.AddComment("Table section index");
    OS.emitCOFFSectionIndex(JumpTable.Table);
    OS.AddComment("Entries count");
    OS.emitInt32(JumpTable.TableSize);
    endSymbolRecord(JumpTableEnd);
  }
}

void CodeViewDebug::emitInlinees(
    const SmallSet<codeview::TypeIndex, 1> &Inlinees) {
  // Divide the list of inlinees into chunks such that each chunk fits within
  // one record.
  constexpr size_t ChunkSize =
      (MaxRecordLength - sizeof(SymbolKind) - sizeof(uint32_t)) /
      sizeof(uint32_t);

  SmallVector<TypeIndex> SortedInlinees{Inlinees.begin(), Inlinees.end()};
  llvm::sort(SortedInlinees);

  size_t CurrentIndex = 0;
  while (CurrentIndex < SortedInlinees.size()) {
    auto Symbol = beginSymbolRecord(SymbolKind::S_INLINEES);
    auto CurrentChunkSize =
        std::min(ChunkSize, SortedInlinees.size() - CurrentIndex);
    OS.AddComment("Count");
    OS.emitInt32(CurrentChunkSize);

    const size_t CurrentChunkEnd = CurrentIndex + CurrentChunkSize;
    for (; CurrentIndex < CurrentChunkEnd; ++CurrentIndex) {
      OS.AddComment("Inlinee");
      OS.emitInt32(SortedInlinees[CurrentIndex].getIndex());
    }
    endSymbolRecord(Symbol);
  }
}
