//===-- SnippetFile.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SnippetFile.h"
#include "BenchmarkRunner.h"
#include "Error.h"
#include "Target.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include <string>

#ifdef __linux__
#include <unistd.h>
#endif // __linux__

namespace llvm {
namespace exegesis {
namespace {

// An MCStreamer that reads a BenchmarkCode definition from a file.
class BenchmarkCodeStreamer : public MCStreamer, public AsmCommentConsumer {
public:
  explicit BenchmarkCodeStreamer(
      MCContext *Context, const DenseMap<StringRef, unsigned> &RegNameToRegNo,
      BenchmarkCode *Result)
      : MCStreamer(*Context), RegNameToRegNo(RegNameToRegNo), Result(Result) {}

  // Implementation of the MCStreamer interface. We only care about
  // instructions.
  void emitInstruction(const MCInst &Instruction,
                       const MCSubtargetInfo &STI) override {
    Result->Key.Instructions.push_back(Instruction);
  }

  // Implementation of the AsmCommentConsumer.
  void HandleComment(SMLoc Loc, StringRef CommentText) override {
    CommentText = CommentText.trim();
    if (!CommentText.consume_front("LLVM-EXEGESIS-"))
      return;
    if (CommentText.consume_front("DEFREG")) {
      // LLVM-EXEGESIS-DEFREF <reg> <hex_value>
      RegisterValue RegVal;
      SmallVector<StringRef, 2> Parts;
      CommentText.split(Parts, ' ', /*unlimited splits*/ -1,
                        /*do not keep empty strings*/ false);
      if (Parts.size() != 2) {
        errs() << "invalid comment 'LLVM-EXEGESIS-DEFREG " << CommentText
               << "', expected two parameters <REG> <HEX_VALUE>\n";
        ++InvalidComments;
        return;
      }
      if (!(RegVal.Register = findRegisterByName(Parts[0].trim()))) {
        errs() << "unknown register '" << Parts[0]
               << "' in 'LLVM-EXEGESIS-DEFREG " << CommentText << "'\n";
        ++InvalidComments;
        return;
      }
      const StringRef HexValue = Parts[1].trim();
      RegVal.Value = APInt(
          /* each hex digit is 4 bits */ HexValue.size() * 4, HexValue, 16);
      Result->Key.RegisterInitialValues.push_back(std::move(RegVal));
      return;
    }
    if (CommentText.consume_front("LIVEIN")) {
      // LLVM-EXEGESIS-LIVEIN <reg>
      const auto RegName = CommentText.ltrim();
      if (unsigned Reg = findRegisterByName(RegName))
        Result->LiveIns.push_back(Reg);
      else {
        errs() << "unknown register '" << RegName
               << "' in 'LLVM-EXEGESIS-LIVEIN " << CommentText << "'\n";
        ++InvalidComments;
      }
      return;
    }
    if (CommentText.consume_front("MEM-DEF")) {
      // LLVM-EXEGESIS-MEM-DEF <name> <size> <value>
      SmallVector<StringRef, 3> Parts;
      CommentText.split(Parts, ' ', -1, false);
      if (Parts.size() != 3) {
        errs() << "invalid comment 'LLVM-EXEGESIS-MEM-DEF " << CommentText
               << "', expected three parameters <NAME> <SIZE> <VALUE>";
        ++InvalidComments;
        return;
      }
      const StringRef HexValue = Parts[2].trim();
      MemoryValue MemVal;
      MemVal.SizeBytes = std::stol(Parts[1].trim().str());
      if (HexValue.size() % 2 != 0) {
        errs() << "invalid comment 'LLVM-EXEGESIS-MEM-DEF " << CommentText
               << "', expected <VALUE> to contain a whole number of bytes";
      }
      MemVal.Value = APInt(HexValue.size() * 4, HexValue, 16);
      MemVal.Index = Result->Key.MemoryValues.size();
      Result->Key.MemoryValues[Parts[0].trim().str()] = MemVal;
      return;
    }
    if (CommentText.consume_front("MEM-MAP")) {
      // LLVM-EXEGESIS-MEM-MAP <value name> <address>
      SmallVector<StringRef, 2> Parts;
      CommentText.split(Parts, ' ', -1, false);
      if (Parts.size() != 2) {
        errs() << "invalid comment 'LLVM-EXEGESIS-MEM-MAP " << CommentText
               << "', expected two parameters <VALUE NAME> <ADDRESS>";
        ++InvalidComments;
        return;
      }
      MemoryMapping MemMap;
      MemMap.MemoryValueName = Parts[0].trim().str();
      MemMap.Address = std::stol(Parts[1].trim().str());

#ifdef __linux__
      // Validate that the annotation is a multiple of the platform's page
      // size.
      if (MemMap.Address % getpagesize() != 0) {
        errs() << "invalid comment 'LLVM-EXEGESIS-MEM-MAP " << CommentText
               << "', expected <ADDRESS> to be a multiple of the platform page "
                  "size.";
        ++InvalidComments;
        return;
      }
#endif // __linux__

      // validate that the annotation refers to an already existing memory
      // definition
      auto MemValIT = Result->Key.MemoryValues.find(Parts[0].trim().str());
      if (MemValIT == Result->Key.MemoryValues.end()) {
        errs() << "invalid comment 'LLVM-EXEGESIS-MEM-MAP " << CommentText
               << "', expected <VALUE NAME> to contain the name of an already "
                  "specified memory definition";
        ++InvalidComments;
        return;
      }
      Result->Key.MemoryMappings.push_back(std::move(MemMap));
      return;
    }
    if (CommentText.consume_front("SNIPPET-ADDRESS")) {
      // LLVM-EXEGESIS-SNIPPET-ADDRESS <address>
      if (!to_integer<intptr_t>(CommentText.trim(), Result->Key.SnippetAddress,
                                16)) {
        errs() << "invalid comment 'LLVM-EXEGESIS-SNIPPET-ADDRESS "
               << CommentText
               << "', expected <ADDRESS> to contain a valid integer in "
                  "hexadecimal format";
        ++InvalidComments;
        return;
      }

#ifdef __linux__
      // Validate that the address in the annotation is a multiple of the
      // platform's page size.
      if (Result->Key.SnippetAddress % getpagesize() != 0) {
        errs() << "invalid comment 'LLVM-EXEGESIS-SNIPPET-ADDRESS "
               << CommentText
               << ", expected <ADDRESS> to be a multiple of the platform page "
                  "size.";
        ++InvalidComments;
        return;
      }
#endif // __linux__

      return;
    }
    if (CommentText.consume_front("LOOP-REGISTER")) {
      // LLVM-EXEGESIS-LOOP-REGISTER <loop register>
      unsigned LoopRegister;

      if (!(LoopRegister = findRegisterByName(CommentText.trim()))) {
        errs() << "unknown register '" << CommentText
               << "' in 'LLVM-EXEGESIS-LOOP-REGISTER " << CommentText << "'\n";
        ++InvalidComments;
        return;
      }

      Result->Key.LoopRegister = LoopRegister;
      return;
    }
  }

  unsigned numInvalidComments() const { return InvalidComments; }

private:
  // We only care about instructions, we don't implement this part of the API.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override {}
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return false;
  }
  void emitValueToAlignment(Align Alignment, int64_t Value, unsigned ValueSize,
                            unsigned MaxBytesToEmit) override {}
  void emitZerofill(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                    Align ByteAlignment, SMLoc Loc) override {}

  unsigned findRegisterByName(const StringRef RegName) const {
    auto Iter = RegNameToRegNo.find(RegName);
    if (Iter != RegNameToRegNo.end())
      return Iter->second;
    errs() << "'" << RegName
           << "' is not a valid register name for the target\n";
    return 0;
  }

  const DenseMap<StringRef, unsigned> &RegNameToRegNo;
  BenchmarkCode *const Result;
  unsigned InvalidComments = 0;
};

} // namespace

// Reads code snippets from file `Filename`.
Expected<std::vector<BenchmarkCode>> readSnippets(const LLVMState &State,
                                                  StringRef Filename) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(Filename);
  if (std::error_code EC = BufferPtr.getError()) {
    return make_error<Failure>("cannot read snippet: " + Filename + ": " +
                               EC.message());
  }
  SourceMgr SM;
  SM.AddNewSourceBuffer(std::move(BufferPtr.get()), SMLoc());

  BenchmarkCode Result;

  // Ensure that there is a default loop register value specified.
  Result.Key.LoopRegister =
      State.getExegesisTarget().getDefaultLoopCounterRegister(
          State.getTargetMachine().getTargetTriple());

  const TargetMachine &TM = State.getTargetMachine();
  MCContext Context(TM.getTargetTriple(), TM.getMCAsmInfo(),
                    TM.getMCRegisterInfo(), TM.getMCSubtargetInfo());
  std::unique_ptr<MCObjectFileInfo> ObjectFileInfo(
      TM.getTarget().createMCObjectFileInfo(Context, /*PIC=*/false));
  Context.setObjectFileInfo(ObjectFileInfo.get());
  Context.initInlineSourceManager();
  BenchmarkCodeStreamer Streamer(&Context, State.getRegNameToRegNoMapping(),
                                 &Result);

  std::string Error;
  raw_string_ostream ErrorStream(Error);
  formatted_raw_ostream InstPrinterOStream(ErrorStream);
  const std::unique_ptr<MCInstPrinter> InstPrinter(
      TM.getTarget().createMCInstPrinter(
          TM.getTargetTriple(), TM.getMCAsmInfo()->getAssemblerDialect(),
          *TM.getMCAsmInfo(), *TM.getMCInstrInfo(), *TM.getMCRegisterInfo()));
  // The following call will take care of calling Streamer.setTargetStreamer.
  TM.getTarget().createAsmTargetStreamer(Streamer, InstPrinterOStream,
                                         InstPrinter.get());
  if (!Streamer.getTargetStreamer())
    return make_error<Failure>("cannot create target asm streamer");

  const std::unique_ptr<MCAsmParser> AsmParser(
      createMCAsmParser(SM, Context, Streamer, *TM.getMCAsmInfo()));
  if (!AsmParser)
    return make_error<Failure>("cannot create asm parser");
  AsmParser->getLexer().setCommentConsumer(&Streamer);

  const std::unique_ptr<MCTargetAsmParser> TargetAsmParser(
      TM.getTarget().createMCAsmParser(*TM.getMCSubtargetInfo(), *AsmParser,
                                       *TM.getMCInstrInfo(),
                                       MCTargetOptions()));

  if (!TargetAsmParser)
    return make_error<Failure>("cannot create target asm parser");
  AsmParser->setTargetParser(*TargetAsmParser);

  if (AsmParser->Run(false))
    return make_error<Failure>("cannot parse asm file");
  if (Streamer.numInvalidComments())
    return make_error<Failure>(Twine("found ")
                                   .concat(Twine(Streamer.numInvalidComments()))
                                   .concat(" invalid LLVM-EXEGESIS comments"));
  return std::vector<BenchmarkCode>{std::move(Result)};
}

} // namespace exegesis
} // namespace llvm
