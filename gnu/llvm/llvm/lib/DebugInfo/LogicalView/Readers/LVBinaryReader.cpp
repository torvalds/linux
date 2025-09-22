//===-- LVBinaryReader.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVBinaryReader class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "BinaryReader"

// Function names extracted from the object symbol table.
void LVSymbolTable::add(StringRef Name, LVScope *Function,
                        LVSectionIndex SectionIndex) {
  std::string SymbolName(Name);
  if (SymbolNames.find(SymbolName) == SymbolNames.end()) {
    SymbolNames.emplace(
        std::piecewise_construct, std::forward_as_tuple(SymbolName),
        std::forward_as_tuple(Function, 0, SectionIndex, false));
  } else {
    // Update a recorded entry with its logical scope and section index.
    SymbolNames[SymbolName].Scope = Function;
    if (SectionIndex)
      SymbolNames[SymbolName].SectionIndex = SectionIndex;
  }

  if (Function && SymbolNames[SymbolName].IsComdat)
    Function->setIsComdat();

  LLVM_DEBUG({ print(dbgs()); });
}

void LVSymbolTable::add(StringRef Name, LVAddress Address,
                        LVSectionIndex SectionIndex, bool IsComdat) {
  std::string SymbolName(Name);
  if (SymbolNames.find(SymbolName) == SymbolNames.end())
    SymbolNames.emplace(
        std::piecewise_construct, std::forward_as_tuple(SymbolName),
        std::forward_as_tuple(nullptr, Address, SectionIndex, IsComdat));
  else
    // Update a recorded symbol name with its logical scope.
    SymbolNames[SymbolName].Address = Address;

  LVScope *Function = SymbolNames[SymbolName].Scope;
  if (Function && IsComdat)
    Function->setIsComdat();
  LLVM_DEBUG({ print(dbgs()); });
}

LVSectionIndex LVSymbolTable::update(LVScope *Function) {
  LVSectionIndex SectionIndex = getReader().getDotTextSectionIndex();
  StringRef Name = Function->getLinkageName();
  if (Name.empty())
    Name = Function->getName();
  std::string SymbolName(Name);

  if (SymbolName.empty() || (SymbolNames.find(SymbolName) == SymbolNames.end()))
    return SectionIndex;

  // Update a recorded entry with its logical scope, only if the scope has
  // ranges. That is the case when in DWARF there are 2 DIEs connected via
  // the DW_AT_specification.
  if (Function->getHasRanges()) {
    SymbolNames[SymbolName].Scope = Function;
    SectionIndex = SymbolNames[SymbolName].SectionIndex;
  } else {
    SectionIndex = UndefinedSectionIndex;
  }

  if (SymbolNames[SymbolName].IsComdat)
    Function->setIsComdat();

  LLVM_DEBUG({ print(dbgs()); });
  return SectionIndex;
}

const LVSymbolTableEntry &LVSymbolTable::getEntry(StringRef Name) {
  static LVSymbolTableEntry Empty = LVSymbolTableEntry();
  LVSymbolNames::iterator Iter = SymbolNames.find(std::string(Name));
  return Iter != SymbolNames.end() ? Iter->second : Empty;
}
LVAddress LVSymbolTable::getAddress(StringRef Name) {
  LVSymbolNames::iterator Iter = SymbolNames.find(std::string(Name));
  return Iter != SymbolNames.end() ? Iter->second.Address : 0;
}
LVSectionIndex LVSymbolTable::getIndex(StringRef Name) {
  LVSymbolNames::iterator Iter = SymbolNames.find(std::string(Name));
  return Iter != SymbolNames.end() ? Iter->second.SectionIndex
                                   : getReader().getDotTextSectionIndex();
}
bool LVSymbolTable::getIsComdat(StringRef Name) {
  LVSymbolNames::iterator Iter = SymbolNames.find(std::string(Name));
  return Iter != SymbolNames.end() ? Iter->second.IsComdat : false;
}

void LVSymbolTable::print(raw_ostream &OS) {
  OS << "Symbol Table\n";
  for (LVSymbolNames::reference Entry : SymbolNames) {
    LVSymbolTableEntry &SymbolName = Entry.second;
    LVScope *Scope = SymbolName.Scope;
    LVOffset Offset = Scope ? Scope->getOffset() : 0;
    OS << "Index: " << hexValue(SymbolName.SectionIndex, 5)
       << " Comdat: " << (SymbolName.IsComdat ? "Y" : "N")
       << " Scope: " << hexValue(Offset)
       << " Address: " << hexValue(SymbolName.Address)
       << " Name: " << Entry.first << "\n";
  }
}

void LVBinaryReader::addToSymbolTable(StringRef Name, LVScope *Function,
                                      LVSectionIndex SectionIndex) {
  SymbolTable.add(Name, Function, SectionIndex);
}
void LVBinaryReader::addToSymbolTable(StringRef Name, LVAddress Address,
                                      LVSectionIndex SectionIndex,
                                      bool IsComdat) {
  SymbolTable.add(Name, Address, SectionIndex, IsComdat);
}
LVSectionIndex LVBinaryReader::updateSymbolTable(LVScope *Function) {
  return SymbolTable.update(Function);
}

const LVSymbolTableEntry &LVBinaryReader::getSymbolTableEntry(StringRef Name) {
  return SymbolTable.getEntry(Name);
}
LVAddress LVBinaryReader::getSymbolTableAddress(StringRef Name) {
  return SymbolTable.getAddress(Name);
}
LVSectionIndex LVBinaryReader::getSymbolTableIndex(StringRef Name) {
  return SymbolTable.getIndex(Name);
}
bool LVBinaryReader::getSymbolTableIsComdat(StringRef Name) {
  return SymbolTable.getIsComdat(Name);
}

void LVBinaryReader::mapVirtualAddress(const object::ObjectFile &Obj) {
  for (const object::SectionRef &Section : Obj.sections()) {
    LLVM_DEBUG({
      Expected<StringRef> SectionNameOrErr = Section.getName();
      StringRef Name;
      if (!SectionNameOrErr)
        consumeError(SectionNameOrErr.takeError());
      else
        Name = *SectionNameOrErr;
      dbgs() << "Index: " << format_decimal(Section.getIndex(), 3) << ", "
             << "Address: " << hexValue(Section.getAddress()) << ", "
             << "Size: " << hexValue(Section.getSize()) << ", "
             << "Name: " << Name << "\n";
      dbgs() << "isCompressed:   " << Section.isCompressed() << ", "
             << "isText:         " << Section.isText() << ", "
             << "isData:         " << Section.isData() << ", "
             << "isBSS:          " << Section.isBSS() << ", "
             << "isVirtual:      " << Section.isVirtual() << "\n";
      dbgs() << "isBitcode:      " << Section.isBitcode() << ", "
             << "isStripped:     " << Section.isStripped() << ", "
             << "isBerkeleyText: " << Section.isBerkeleyText() << ", "
             << "isBerkeleyData: " << Section.isBerkeleyData() << ", "
             << "isDebugSection: " << Section.isDebugSection() << "\n";
      dbgs() << "\n";
    });

    if (!Section.isText() || Section.isVirtual() || !Section.getSize())
      continue;

    // Record section information required for symbol resolution.
    // Note: The section index returned by 'getIndex()' is one based.
    Sections.emplace(Section.getIndex(), Section);
    addSectionAddress(Section);

    // Identify the ".text" section.
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (!SectionNameOrErr) {
      consumeError(SectionNameOrErr.takeError());
      continue;
    }
    if (*SectionNameOrErr == ".text" || *SectionNameOrErr == "CODE" ||
        *SectionNameOrErr == ".code") {
      DotTextSectionIndex = Section.getIndex();
      // If the object is WebAssembly, update the address offset that
      // will be added to DWARF DW_AT_* attributes.
      if (Obj.isWasm())
        WasmCodeSectionOffset = Section.getAddress();
    }
  }

  // Process the symbol table.
  mapRangeAddress(Obj);

  LLVM_DEBUG({
    dbgs() << "\nSections Information:\n";
    for (LVSections::reference Entry : Sections) {
      LVSectionIndex SectionIndex = Entry.first;
      const object::SectionRef Section = Entry.second;
      Expected<StringRef> SectionNameOrErr = Section.getName();
      if (!SectionNameOrErr)
        consumeError(SectionNameOrErr.takeError());
      dbgs() << "\nIndex: " << format_decimal(SectionIndex, 3)
             << " Name: " << *SectionNameOrErr << "\n"
             << "Size: " << hexValue(Section.getSize()) << "\n"
             << "VirtualAddress: " << hexValue(VirtualAddress) << "\n"
             << "SectionAddress: " << hexValue(Section.getAddress()) << "\n";
    }
    dbgs() << "\nObject Section Information:\n";
    for (LVSectionAddresses::const_reference Entry : SectionAddresses)
      dbgs() << "[" << hexValue(Entry.first) << ":"
             << hexValue(Entry.first + Entry.second.getSize())
             << "] Size: " << hexValue(Entry.second.getSize()) << "\n";
  });
}

void LVBinaryReader::mapVirtualAddress(const object::COFFObjectFile &COFFObj) {
  ErrorOr<uint64_t> ImageBase = COFFObj.getImageBase();
  if (ImageBase)
    ImageBaseAddress = ImageBase.get();

  LLVM_DEBUG({
    dbgs() << "ImageBaseAddress: " << hexValue(ImageBaseAddress) << "\n";
  });

  uint32_t Flags = COFF::IMAGE_SCN_CNT_CODE | COFF::IMAGE_SCN_LNK_COMDAT;

  for (const object::SectionRef &Section : COFFObj.sections()) {
    if (!Section.isText() || Section.isVirtual() || !Section.getSize())
      continue;

    const object::coff_section *COFFSection = COFFObj.getCOFFSection(Section);
    VirtualAddress = COFFSection->VirtualAddress;
    bool IsComdat = (COFFSection->Characteristics & Flags) == Flags;

    // Record section information required for symbol resolution.
    // Note: The section index returned by 'getIndex()' is zero based.
    Sections.emplace(Section.getIndex() + 1, Section);
    addSectionAddress(Section);

    // Additional initialization on the specific object format.
    mapRangeAddress(COFFObj, Section, IsComdat);
  }

  LLVM_DEBUG({
    dbgs() << "\nSections Information:\n";
    for (LVSections::reference Entry : Sections) {
      LVSectionIndex SectionIndex = Entry.first;
      const object::SectionRef Section = Entry.second;
      const object::coff_section *COFFSection = COFFObj.getCOFFSection(Section);
      Expected<StringRef> SectionNameOrErr = Section.getName();
      if (!SectionNameOrErr)
        consumeError(SectionNameOrErr.takeError());
      dbgs() << "\nIndex: " << format_decimal(SectionIndex, 3)
             << " Name: " << *SectionNameOrErr << "\n"
             << "Size: " << hexValue(Section.getSize()) << "\n"
             << "VirtualAddress: " << hexValue(VirtualAddress) << "\n"
             << "SectionAddress: " << hexValue(Section.getAddress()) << "\n"
             << "PointerToRawData: " << hexValue(COFFSection->PointerToRawData)
             << "\n"
             << "SizeOfRawData: " << hexValue(COFFSection->SizeOfRawData)
             << "\n";
    }
    dbgs() << "\nObject Section Information:\n";
    for (LVSectionAddresses::const_reference Entry : SectionAddresses)
      dbgs() << "[" << hexValue(Entry.first) << ":"
             << hexValue(Entry.first + Entry.second.getSize())
             << "] Size: " << hexValue(Entry.second.getSize()) << "\n";
  });
}

Error LVBinaryReader::loadGenericTargetInfo(StringRef TheTriple,
                                            StringRef TheFeatures) {
  std::string TargetLookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(std::string(TheTriple), TargetLookupError);
  if (!TheTarget)
    return createStringError(errc::invalid_argument, TargetLookupError.c_str());

  // Register information.
  MCRegisterInfo *RegisterInfo = TheTarget->createMCRegInfo(TheTriple);
  if (!RegisterInfo)
    return createStringError(errc::invalid_argument,
                             "no register info for target " + TheTriple);
  MRI.reset(RegisterInfo);

  // Assembler properties and features.
  MCTargetOptions MCOptions;
  MCAsmInfo *AsmInfo(TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  if (!AsmInfo)
    return createStringError(errc::invalid_argument,
                             "no assembly info for target " + TheTriple);
  MAI.reset(AsmInfo);

  // Target subtargets.
  StringRef CPU;
  MCSubtargetInfo *SubtargetInfo(
      TheTarget->createMCSubtargetInfo(TheTriple, CPU, TheFeatures));
  if (!SubtargetInfo)
    return createStringError(errc::invalid_argument,
                             "no subtarget info for target " + TheTriple);
  STI.reset(SubtargetInfo);

  // Instructions Info.
  MCInstrInfo *InstructionInfo(TheTarget->createMCInstrInfo());
  if (!InstructionInfo)
    return createStringError(errc::invalid_argument,
                             "no instruction info for target " + TheTriple);
  MII.reset(InstructionInfo);

  MC = std::make_unique<MCContext>(Triple(TheTriple), MAI.get(), MRI.get(),
                                   STI.get());

  // Assembler.
  MCDisassembler *DisAsm(TheTarget->createMCDisassembler(*STI, *MC));
  if (!DisAsm)
    return createStringError(errc::invalid_argument,
                             "no disassembler for target " + TheTriple);
  MD.reset(DisAsm);

  MCInstPrinter *InstructionPrinter(TheTarget->createMCInstPrinter(
      Triple(TheTriple), AsmInfo->getAssemblerDialect(), *MAI, *MII, *MRI));
  if (!InstructionPrinter)
    return createStringError(errc::invalid_argument,
                             "no target assembly language printer for target " +
                                 TheTriple);
  MIP.reset(InstructionPrinter);
  InstructionPrinter->setPrintImmHex(true);

  return Error::success();
}

Expected<std::pair<uint64_t, object::SectionRef>>
LVBinaryReader::getSection(LVScope *Scope, LVAddress Address,
                           LVSectionIndex SectionIndex) {
  // Return the 'text' section with the code for this logical scope.
  // COFF: SectionIndex is zero. Use 'SectionAddresses' data.
  // ELF: SectionIndex is the section index in the file.
  if (SectionIndex) {
    LVSections::iterator Iter = Sections.find(SectionIndex);
    if (Iter == Sections.end()) {
      return createStringError(errc::invalid_argument,
                               "invalid section index for: '%s'",
                               Scope->getName().str().c_str());
    }
    const object::SectionRef Section = Iter->second;
    return std::make_pair(Section.getAddress(), Section);
  }

  // Ensure a valid starting address for the public names.
  LVSectionAddresses::const_iterator Iter =
      SectionAddresses.upper_bound(Address);
  if (Iter == SectionAddresses.begin())
    return createStringError(errc::invalid_argument,
                             "invalid section address for: '%s'",
                             Scope->getName().str().c_str());

  // Get section that contains the code for this function.
  Iter = SectionAddresses.lower_bound(Address);
  if (Iter != SectionAddresses.begin())
    --Iter;
  return std::make_pair(Iter->first, Iter->second);
}

void LVBinaryReader::addSectionRange(LVSectionIndex SectionIndex,
                                     LVScope *Scope) {
  LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
  ScopesWithRanges->addEntry(Scope);
}

void LVBinaryReader::addSectionRange(LVSectionIndex SectionIndex,
                                     LVScope *Scope, LVAddress LowerAddress,
                                     LVAddress UpperAddress) {
  LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
  ScopesWithRanges->addEntry(Scope, LowerAddress, UpperAddress);
}

LVRange *LVBinaryReader::getSectionRanges(LVSectionIndex SectionIndex) {
  // Check if we already have a mapping for this section index.
  LVSectionRanges::iterator IterSection = SectionRanges.find(SectionIndex);
  if (IterSection == SectionRanges.end())
    IterSection =
        SectionRanges.emplace(SectionIndex, std::make_unique<LVRange>()).first;
  LVRange *Range = IterSection->second.get();
  assert(Range && "Range is null.");
  return Range;
}

Error LVBinaryReader::createInstructions(LVScope *Scope,
                                         LVSectionIndex SectionIndex,
                                         const LVNameInfo &NameInfo) {
  assert(Scope && "Scope is null.");

  // Skip stripped functions.
  if (Scope->getIsDiscarded())
    return Error::success();

  // Find associated address and size for the given function entry point.
  LVAddress Address = NameInfo.first;
  uint64_t Size = NameInfo.second;

  LLVM_DEBUG({
    dbgs() << "\nPublic Name instructions: '" << Scope->getName() << "' / '"
           << Scope->getLinkageName() << "'\n"
           << "DIE Offset: " << hexValue(Scope->getOffset()) << " Range: ["
           << hexValue(Address) << ":" << hexValue(Address + Size) << "]\n";
  });

  Expected<std::pair<uint64_t, const object::SectionRef>> SectionOrErr =
      getSection(Scope, Address, SectionIndex);
  if (!SectionOrErr)
    return SectionOrErr.takeError();
  const object::SectionRef Section = (*SectionOrErr).second;
  uint64_t SectionAddress = (*SectionOrErr).first;

  Expected<StringRef> SectionContentsOrErr = Section.getContents();
  if (!SectionContentsOrErr)
    return SectionOrErr.takeError();

  // There are cases where the section size is smaller than the [LowPC,HighPC]
  // range; it causes us to decode invalid addresses. The recorded size in the
  // logical scope is one less than the real size.
  LLVM_DEBUG({
    dbgs() << " Size: " << hexValue(Size)
           << ", Section Size: " << hexValue(Section.getSize()) << "\n";
  });
  Size = std::min(Size + 1, Section.getSize());

  ArrayRef<uint8_t> Bytes = arrayRefFromStringRef(*SectionContentsOrErr);
  uint64_t Offset = Address - SectionAddress;
  uint8_t const *Begin = Bytes.data() + Offset;
  uint8_t const *End = Bytes.data() + Offset + Size;

  LLVM_DEBUG({
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (!SectionNameOrErr)
      consumeError(SectionNameOrErr.takeError());
    else
      dbgs() << "Section Index: " << hexValue(Section.getIndex()) << " ["
             << hexValue((uint64_t)Section.getAddress()) << ":"
             << hexValue((uint64_t)Section.getAddress() + Section.getSize(), 10)
             << "] Name: '" << *SectionNameOrErr << "'\n"
             << "Begin: " << hexValue((uint64_t)Begin)
             << ", End: " << hexValue((uint64_t)End) << "\n";
  });

  // Address for first instruction line.
  LVAddress FirstAddress = Address;
  auto InstructionsSP = std::make_unique<LVLines>();
  LVLines &Instructions = *InstructionsSP;
  DiscoveredLines.emplace_back(std::move(InstructionsSP));

  while (Begin < End) {
    MCInst Instruction;
    uint64_t BytesConsumed = 0;
    SmallVector<char, 64> InsnStr;
    raw_svector_ostream Annotations(InsnStr);
    MCDisassembler::DecodeStatus const S =
        MD->getInstruction(Instruction, BytesConsumed,
                           ArrayRef<uint8_t>(Begin, End), Address, outs());
    switch (S) {
    case MCDisassembler::Fail:
      LLVM_DEBUG({ dbgs() << "Invalid instruction\n"; });
      if (BytesConsumed == 0)
        // Skip invalid bytes
        BytesConsumed = 1;
      break;
    case MCDisassembler::SoftFail:
      LLVM_DEBUG({ dbgs() << "Potentially undefined instruction:"; });
      [[fallthrough]];
    case MCDisassembler::Success: {
      std::string Buffer;
      raw_string_ostream Stream(Buffer);
      StringRef AnnotationsStr = Annotations.str();
      MIP->printInst(&Instruction, Address, AnnotationsStr, *STI, Stream);
      LLVM_DEBUG({
        std::string BufferCodes;
        raw_string_ostream StreamCodes(BufferCodes);
        StreamCodes << format_bytes(
            ArrayRef<uint8_t>(Begin, Begin + BytesConsumed), std::nullopt, 16,
            16);
        dbgs() << "[" << hexValue((uint64_t)Begin) << "] "
               << "Size: " << format_decimal(BytesConsumed, 2) << " ("
               << formatv("{0}",
                          fmt_align(StreamCodes.str(), AlignStyle::Left, 32))
               << ") " << hexValue((uint64_t)Address) << ": " << Stream.str()
               << "\n";
      });
      // Here we add logical lines to the Instructions. Later on,
      // the 'processLines()' function will move each created logical line
      // to its enclosing logical scope, using the debug ranges information
      // and they will be released when its scope parent is deleted.
      LVLineAssembler *Line = createLineAssembler();
      Line->setAddress(Address);
      Line->setName(StringRef(Stream.str()).trim());
      Instructions.push_back(Line);
      break;
    }
    }
    Address += BytesConsumed;
    Begin += BytesConsumed;
  }

  LLVM_DEBUG({
    size_t Index = 0;
    dbgs() << "\nSectionIndex: " << format_decimal(SectionIndex, 3)
           << " Scope DIE: " << hexValue(Scope->getOffset()) << "\n"
           << "Address: " << hexValue(FirstAddress)
           << format(" - Collected instructions lines: %d\n",
                     Instructions.size());
    for (const LVLine *Line : Instructions)
      dbgs() << format_decimal(++Index, 5) << ": "
             << hexValue(Line->getOffset()) << ", (" << Line->getName()
             << ")\n";
  });

  // The scope in the assembler names is linked to its own instructions.
  ScopeInstructions.add(SectionIndex, Scope, &Instructions);
  AssemblerMappings.add(SectionIndex, FirstAddress, Scope);

  return Error::success();
}

Error LVBinaryReader::createInstructions(LVScope *Function,
                                         LVSectionIndex SectionIndex) {
  if (!options().getPrintInstructions())
    return Error::success();

  LVNameInfo Name = CompileUnit->findPublicName(Function);
  if (Name.first != LVAddress(UINT64_MAX))
    return createInstructions(Function, SectionIndex, Name);

  return Error::success();
}

Error LVBinaryReader::createInstructions() {
  if (!options().getPrintInstructions())
    return Error::success();

  LLVM_DEBUG({
    size_t Index = 1;
    dbgs() << "\nPublic Names (Scope):\n";
    for (LVPublicNames::const_reference Name : CompileUnit->getPublicNames()) {
      LVScope *Scope = Name.first;
      const LVNameInfo &NameInfo = Name.second;
      LVAddress Address = NameInfo.first;
      uint64_t Size = NameInfo.second;
      dbgs() << format_decimal(Index++, 5) << ": "
             << "DIE Offset: " << hexValue(Scope->getOffset()) << " Range: ["
             << hexValue(Address) << ":" << hexValue(Address + Size) << "] "
             << "Name: '" << Scope->getName() << "' / '"
             << Scope->getLinkageName() << "'\n";
    }
  });

  // For each public name in the current compile unit, create the line
  // records that represent the executable instructions.
  for (LVPublicNames::const_reference Name : CompileUnit->getPublicNames()) {
    LVScope *Scope = Name.first;
    // The symbol table extracted from the object file always contains a
    // non-empty name (linkage name). However, the logical scope does not
    // guarantee to have a name for the linkage name (main is one case).
    // For those cases, set the linkage name the same as the name.
    if (!Scope->getLinkageNameIndex())
      Scope->setLinkageName(Scope->getName());
    LVSectionIndex SectionIndex = getSymbolTableIndex(Scope->getLinkageName());
    if (Error Err = createInstructions(Scope, SectionIndex, Name.second))
      return Err;
  }

  return Error::success();
}

// During the traversal of the debug information sections, we created the
// logical lines representing the disassembled instructions from the text
// section and the logical lines representing the line records from the
// debug line section. Using the ranges associated with the logical scopes,
// we will allocate those logical lines to their logical scopes.
void LVBinaryReader::processLines(LVLines *DebugLines,
                                  LVSectionIndex SectionIndex,
                                  LVScope *Function) {
  assert(DebugLines && "DebugLines is null.");

  // Just return if this compilation unit does not have any line records
  // and no instruction lines were created.
  if (DebugLines->empty() && !options().getPrintInstructions())
    return;

  // Merge the debug lines and instruction lines using their text address;
  // the logical line representing the debug line record is followed by the
  // line(s) representing the disassembled instructions, whose addresses are
  // equal or greater that the line address and less than the address of the
  // next debug line record.
  LLVM_DEBUG({
    size_t Index = 1;
    size_t PerLine = 4;
    dbgs() << format("\nProcess debug lines: %d\n", DebugLines->size());
    for (const LVLine *Line : *DebugLines) {
      dbgs() << format_decimal(Index, 5) << ": " << hexValue(Line->getOffset())
             << ", (" << Line->getLineNumber() << ")"
             << ((Index % PerLine) ? "  " : "\n");
      ++Index;
    }
    dbgs() << ((Index % PerLine) ? "\n" : "");
  });

  bool TraverseLines = true;
  LVLines::iterator Iter = DebugLines->begin();
  while (TraverseLines && Iter != DebugLines->end()) {
    uint64_t DebugAddress = (*Iter)->getAddress();

    // Get the function with an entry point that matches this line and
    // its associated assembler entries. In the case of COMDAT, the input
    // 'Function' is not null. Use it to find its address ranges.
    LVScope *Scope = Function;
    if (!Function) {
      Scope = AssemblerMappings.find(SectionIndex, DebugAddress);
      if (!Scope) {
        ++Iter;
        continue;
      }
    }

    // Get the associated instructions for the found 'Scope'.
    LVLines InstructionLines;
    LVLines *Lines = ScopeInstructions.find(SectionIndex, Scope);
    if (Lines)
      InstructionLines = std::move(*Lines);

    LLVM_DEBUG({
      size_t Index = 0;
      dbgs() << "\nSectionIndex: " << format_decimal(SectionIndex, 3)
             << " Scope DIE: " << hexValue(Scope->getOffset()) << "\n"
             << format("Process instruction lines: %d\n",
                       InstructionLines.size());
      for (const LVLine *Line : InstructionLines)
        dbgs() << format_decimal(++Index, 5) << ": "
               << hexValue(Line->getOffset()) << ", (" << Line->getName()
               << ")\n";
    });

    // Continue with next debug line if there are not instructions lines.
    if (InstructionLines.empty()) {
      ++Iter;
      continue;
    }

    for (LVLine *InstructionLine : InstructionLines) {
      uint64_t InstructionAddress = InstructionLine->getAddress();
      LLVM_DEBUG({
        dbgs() << "Instruction address: " << hexValue(InstructionAddress)
               << "\n";
      });
      if (TraverseLines) {
        while (Iter != DebugLines->end()) {
          DebugAddress = (*Iter)->getAddress();
          LLVM_DEBUG({
            bool IsDebug = (*Iter)->getIsLineDebug();
            dbgs() << "Line " << (IsDebug ? "dbg:" : "ins:") << " ["
                   << hexValue(DebugAddress) << "]";
            if (IsDebug)
              dbgs() << format(" %d", (*Iter)->getLineNumber());
            dbgs() << "\n";
          });
          // Instruction address before debug line.
          if (InstructionAddress < DebugAddress) {
            LLVM_DEBUG({
              dbgs() << "Inserted instruction address: "
                     << hexValue(InstructionAddress) << " before line: "
                     << format("%d", (*Iter)->getLineNumber()) << " ["
                     << hexValue(DebugAddress) << "]\n";
            });
            Iter = DebugLines->insert(Iter, InstructionLine);
            // The returned iterator points to the inserted instruction.
            // Skip it and point to the line acting as reference.
            ++Iter;
            break;
          }
          ++Iter;
        }
        if (Iter == DebugLines->end()) {
          // We have reached the end of the source lines and the current
          // instruction line address is greater than the last source line.
          TraverseLines = false;
          DebugLines->push_back(InstructionLine);
        }
      } else {
        DebugLines->push_back(InstructionLine);
      }
    }
  }

  LLVM_DEBUG({
    dbgs() << format("Lines after merge: %d\n", DebugLines->size());
    size_t Index = 0;
    for (const LVLine *Line : *DebugLines) {
      dbgs() << format_decimal(++Index, 5) << ": "
             << hexValue(Line->getOffset()) << ", ("
             << ((Line->getIsLineDebug())
                     ? Line->lineNumberAsStringStripped(/*ShowZero=*/true)
                     : Line->getName())
             << ")\n";
    }
  });

  // If this compilation unit does not have line records, traverse its scopes
  // and take any collected instruction lines as the working set in order
  // to move them to their associated scope.
  if (DebugLines->empty()) {
    if (const LVScopes *Scopes = CompileUnit->getScopes())
      for (LVScope *Scope : *Scopes) {
        LVLines *Lines = ScopeInstructions.find(Scope);
        if (Lines) {

          LLVM_DEBUG({
            size_t Index = 0;
            dbgs() << "\nSectionIndex: " << format_decimal(SectionIndex, 3)
                   << " Scope DIE: " << hexValue(Scope->getOffset()) << "\n"
                   << format("Instruction lines: %d\n", Lines->size());
            for (const LVLine *Line : *Lines)
              dbgs() << format_decimal(++Index, 5) << ": "
                     << hexValue(Line->getOffset()) << ", (" << Line->getName()
                     << ")\n";
          });

          if (Scope->getIsArtificial()) {
            // Add the instruction lines to their artificial scope.
            for (LVLine *Line : *Lines)
              Scope->addElement(Line);
          } else {
            DebugLines->append(*Lines);
          }
          Lines->clear();
        }
      }
  }

  LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
  ScopesWithRanges->startSearch();

  // Process collected lines.
  LVScope *Scope;
  for (LVLine *Line : *DebugLines) {
    // Using the current line address, get its associated lexical scope and
    // add the line information to it.
    Scope = ScopesWithRanges->getEntry(Line->getAddress());
    if (!Scope) {
      // If missing scope, use the compile unit.
      Scope = CompileUnit;
      LLVM_DEBUG({
        dbgs() << "Adding line to CU: " << hexValue(Line->getOffset()) << ", ("
               << ((Line->getIsLineDebug())
                       ? Line->lineNumberAsStringStripped(/*ShowZero=*/true)
                       : Line->getName())
               << ")\n";
      });
    }

    // Add line object to scope.
    Scope->addElement(Line);

    // Report any line zero.
    if (options().getWarningLines() && Line->getIsLineDebug() &&
        !Line->getLineNumber())
      CompileUnit->addLineZero(Line);

    // Some compilers generate ranges in the compile unit; other compilers
    // only DW_AT_low_pc/DW_AT_high_pc. In order to correctly map global
    // variables, we need to generate the map ranges for the compile unit.
    // If we use the ranges stored at the scope level, there are cases where
    // the address referenced by a symbol location, is not in the enclosing
    // scope, but in an outer one. By using the ranges stored in the compile
    // unit, we can catch all those addresses.
    if (Line->getIsLineDebug())
      CompileUnit->addMapping(Line, SectionIndex);

    // Resolve any given pattern.
    patterns().resolvePatternMatch(Line);
  }

  ScopesWithRanges->endSearch();
}

void LVBinaryReader::processLines(LVLines *DebugLines,
                                  LVSectionIndex SectionIndex) {
  assert(DebugLines && "DebugLines is null.");
  if (DebugLines->empty() && !ScopeInstructions.findMap(SectionIndex))
    return;

  // If the Compile Unit does not contain comdat functions, use the whole
  // set of debug lines, as the addresses don't have conflicts.
  if (!CompileUnit->getHasComdatScopes()) {
    processLines(DebugLines, SectionIndex, nullptr);
    return;
  }

  // Find the indexes for the lines whose address is zero.
  std::vector<size_t> AddressZero;
  LVLines::iterator It =
      std::find_if(std::begin(*DebugLines), std::end(*DebugLines),
                   [](LVLine *Line) { return !Line->getAddress(); });
  while (It != std::end(*DebugLines)) {
    AddressZero.emplace_back(std::distance(std::begin(*DebugLines), It));
    It = std::find_if(std::next(It), std::end(*DebugLines),
                      [](LVLine *Line) { return !Line->getAddress(); });
  }

  // If the set of debug lines does not contain any line with address zero,
  // use the whole set. It means we are dealing with an initialization
  // section from a fully linked binary.
  if (AddressZero.empty()) {
    processLines(DebugLines, SectionIndex, nullptr);
    return;
  }

  // The Compile unit contains comdat functions. Traverse the collected
  // debug lines and identify logical groups based on their start and
  // address. Each group starts with a zero address.
  // Begin, End, Address, IsDone.
  using LVBucket = std::tuple<size_t, size_t, LVAddress, bool>;
  std::vector<LVBucket> Buckets;

  LVAddress Address;
  size_t Begin = 0;
  size_t End = 0;
  size_t Index = 0;
  for (Index = 0; Index < AddressZero.size() - 1; ++Index) {
    Begin = AddressZero[Index];
    End = AddressZero[Index + 1] - 1;
    Address = (*DebugLines)[End]->getAddress();
    Buckets.emplace_back(Begin, End, Address, false);
  }

  // Add the last bucket.
  if (Index) {
    Begin = AddressZero[Index];
    End = DebugLines->size() - 1;
    Address = (*DebugLines)[End]->getAddress();
    Buckets.emplace_back(Begin, End, Address, false);
  }

  LLVM_DEBUG({
    dbgs() << "\nDebug Lines buckets: " << Buckets.size() << "\n";
    for (LVBucket &Bucket : Buckets) {
      dbgs() << "Begin: " << format_decimal(std::get<0>(Bucket), 5) << ", "
             << "End: " << format_decimal(std::get<1>(Bucket), 5) << ", "
             << "Address: " << hexValue(std::get<2>(Bucket)) << "\n";
    }
  });

  // Traverse the sections and buckets looking for matches on the section
  // sizes. In the unlikely event of different buckets with the same size
  // process them in order and mark them as done.
  LVLines Group;
  for (LVSections::reference Entry : Sections) {
    LVSectionIndex SectionIndex = Entry.first;
    const object::SectionRef Section = Entry.second;
    uint64_t Size = Section.getSize();
    LLVM_DEBUG({
      dbgs() << "\nSection Index: " << format_decimal(SectionIndex, 3)
             << " , Section Size: " << hexValue(Section.getSize())
             << " , Section Address: " << hexValue(Section.getAddress())
             << "\n";
    });

    for (LVBucket &Bucket : Buckets) {
      if (std::get<3>(Bucket))
        // Already done for previous section.
        continue;
      if (Size == std::get<2>(Bucket)) {
        // We have a match on the section size.
        Group.clear();
        LVLines::iterator IterStart = DebugLines->begin() + std::get<0>(Bucket);
        LVLines::iterator IterEnd =
            DebugLines->begin() + std::get<1>(Bucket) + 1;
        for (LVLines::iterator Iter = IterStart; Iter < IterEnd; ++Iter)
          Group.push_back(*Iter);
        processLines(&Group, SectionIndex, /*Function=*/nullptr);
        std::get<3>(Bucket) = true;
        break;
      }
    }
  }
}

// Traverse the scopes for the given 'Function' looking for any inlined
// scopes with inlined lines, which are found in 'CUInlineeLines'.
void LVBinaryReader::includeInlineeLines(LVSectionIndex SectionIndex,
                                         LVScope *Function) {
  SmallVector<LVInlineeLine::iterator> InlineeIters;
  std::function<void(LVScope * Parent)> FindInlinedScopes =
      [&](LVScope *Parent) {
        if (const LVScopes *Scopes = Parent->getScopes())
          for (LVScope *Scope : *Scopes) {
            LVInlineeLine::iterator Iter = CUInlineeLines.find(Scope);
            if (Iter != CUInlineeLines.end())
              InlineeIters.push_back(Iter);
            FindInlinedScopes(Scope);
          }
      };

  // Find all inlined scopes for the given 'Function'.
  FindInlinedScopes(Function);
  for (LVInlineeLine::iterator InlineeIter : InlineeIters) {
    LVScope *Scope = InlineeIter->first;
    addToSymbolTable(Scope->getLinkageName(), Scope, SectionIndex);

    // TODO: Convert this into a reference.
    LVLines *InlineeLines = InlineeIter->second.get();
    LLVM_DEBUG({
      dbgs() << "Inlined lines for: " << Scope->getName() << "\n";
      for (const LVLine *Line : *InlineeLines)
        dbgs() << "[" << hexValue(Line->getAddress()) << "] "
               << Line->getLineNumber() << "\n";
      dbgs() << format("Debug lines: %d\n", CULines.size());
      for (const LVLine *Line : CULines)
        dbgs() << "Line address: " << hexValue(Line->getOffset()) << ", ("
               << Line->getLineNumber() << ")\n";
      ;
    });

    // The inlined lines must be merged using its address, in order to keep
    // the real order of the instructions. The inlined lines are mixed with
    // the other non-inlined lines.
    if (InlineeLines->size()) {
      // First address of inlinee code.
      uint64_t InlineeStart = (InlineeLines->front())->getAddress();
      LVLines::iterator Iter = std::find_if(
          CULines.begin(), CULines.end(), [&](LVLine *Item) -> bool {
            return Item->getAddress() == InlineeStart;
          });
      if (Iter != CULines.end()) {
        // 'Iter' points to the line where the inlined function is called.
        // Emulate the DW_AT_call_line attribute.
        Scope->setCallLineNumber((*Iter)->getLineNumber());
        // Mark the referenced line as the start of the inlined function.
        // Skip the first line during the insertion, as the address and
        // line number as the same. Otherwise we have to erase and insert.
        (*Iter)->setLineNumber((*InlineeLines->begin())->getLineNumber());
        ++Iter;
        CULines.insert(Iter, InlineeLines->begin() + 1, InlineeLines->end());
      }
    }

    // Remove this set of lines from the container; each inlined function
    // creates an unique set of lines. Remove only the created container.
    CUInlineeLines.erase(InlineeIter);
    InlineeLines->clear();
  }
  LLVM_DEBUG({
    dbgs() << "Merged Inlined lines for: " << Function->getName() << "\n";
    dbgs() << format("Debug lines: %d\n", CULines.size());
    for (const LVLine *Line : CULines)
      dbgs() << "Line address: " << hexValue(Line->getOffset()) << ", ("
             << Line->getLineNumber() << ")\n";
    ;
  });
}

void LVBinaryReader::print(raw_ostream &OS) const {
  OS << "LVBinaryReader\n";
  LLVM_DEBUG(dbgs() << "PrintReader\n");
}
