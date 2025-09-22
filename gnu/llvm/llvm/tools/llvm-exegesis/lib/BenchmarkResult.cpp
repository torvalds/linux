//===-- BenchmarkResult.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BenchmarkResult.h"
#include "BenchmarkRunner.h"
#include "Error.h"
#include "ValidationEvent.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

static constexpr const char kIntegerPrefix[] = "i_0x";
static constexpr const char kDoublePrefix[] = "f_";
static constexpr const char kInvalidOperand[] = "INVALID";

namespace llvm {

namespace {

// A mutable struct holding an LLVMState that can be passed through the
// serialization process to encode/decode registers and instructions.
struct YamlContext {
  YamlContext(const exegesis::LLVMState &State)
      : State(&State), ErrorStream(LastError),
        OpcodeNameToOpcodeIdx(State.getOpcodeNameToOpcodeIdxMapping()),
        RegNameToRegNo(State.getRegNameToRegNoMapping()) {}

  void serializeMCInst(const MCInst &MCInst, raw_ostream &OS) {
    OS << getInstrName(MCInst.getOpcode());
    for (const auto &Op : MCInst) {
      OS << ' ';
      serializeMCOperand(Op, OS);
    }
  }

  void deserializeMCInst(StringRef String, MCInst &Value) {
    SmallVector<StringRef, 16> Pieces;
    String.split(Pieces, " ", /* MaxSplit */ -1, /* KeepEmpty */ false);
    if (Pieces.empty()) {
      ErrorStream << "Unknown Instruction: '" << String << "'\n";
      return;
    }
    bool ProcessOpcode = true;
    for (StringRef Piece : Pieces) {
      if (ProcessOpcode)
        Value.setOpcode(getInstrOpcode(Piece));
      else
        Value.addOperand(deserializeMCOperand(Piece));
      ProcessOpcode = false;
    }
  }

  std::string &getLastError() { return ErrorStream.str(); }

  raw_string_ostream &getErrorStream() { return ErrorStream; }

  StringRef getRegName(unsigned RegNo) {
    // Special case: RegNo 0 is NoRegister. We have to deal with it explicitly.
    if (RegNo == 0)
      return kNoRegister;
    const StringRef RegName = State->getRegInfo().getName(RegNo);
    if (RegName.empty())
      ErrorStream << "No register with enum value '" << RegNo << "'\n";
    return RegName;
  }

  std::optional<unsigned> getRegNo(StringRef RegName) {
    auto Iter = RegNameToRegNo.find(RegName);
    if (Iter != RegNameToRegNo.end())
      return Iter->second;
    ErrorStream << "No register with name '" << RegName << "'\n";
    return std::nullopt;
  }

private:
  void serializeIntegerOperand(raw_ostream &OS, int64_t Value) {
    OS << kIntegerPrefix;
    OS.write_hex(bit_cast<uint64_t>(Value));
  }

  bool tryDeserializeIntegerOperand(StringRef String, int64_t &Value) {
    if (!String.consume_front(kIntegerPrefix))
      return false;
    return !String.consumeInteger(16, Value);
  }

  void serializeFPOperand(raw_ostream &OS, double Value) {
    OS << kDoublePrefix << format("%la", Value);
  }

  bool tryDeserializeFPOperand(StringRef String, double &Value) {
    if (!String.consume_front(kDoublePrefix))
      return false;
    char *EndPointer = nullptr;
    Value = strtod(String.begin(), &EndPointer);
    return EndPointer == String.end();
  }

  void serializeMCOperand(const MCOperand &MCOperand, raw_ostream &OS) {
    if (MCOperand.isReg()) {
      OS << getRegName(MCOperand.getReg());
    } else if (MCOperand.isImm()) {
      serializeIntegerOperand(OS, MCOperand.getImm());
    } else if (MCOperand.isDFPImm()) {
      serializeFPOperand(OS, bit_cast<double>(MCOperand.getDFPImm()));
    } else {
      OS << kInvalidOperand;
    }
  }

  MCOperand deserializeMCOperand(StringRef String) {
    assert(!String.empty());
    int64_t IntValue = 0;
    double DoubleValue = 0;
    if (tryDeserializeIntegerOperand(String, IntValue))
      return MCOperand::createImm(IntValue);
    if (tryDeserializeFPOperand(String, DoubleValue))
      return MCOperand::createDFPImm(bit_cast<uint64_t>(DoubleValue));
    if (auto RegNo = getRegNo(String))
      return MCOperand::createReg(*RegNo);
    if (String != kInvalidOperand)
      ErrorStream << "Unknown Operand: '" << String << "'\n";
    return {};
  }

  StringRef getInstrName(unsigned InstrNo) {
    const StringRef InstrName = State->getInstrInfo().getName(InstrNo);
    if (InstrName.empty())
      ErrorStream << "No opcode with enum value '" << InstrNo << "'\n";
    return InstrName;
  }

  unsigned getInstrOpcode(StringRef InstrName) {
    auto Iter = OpcodeNameToOpcodeIdx.find(InstrName);
    if (Iter != OpcodeNameToOpcodeIdx.end())
      return Iter->second;
    ErrorStream << "No opcode with name '" << InstrName << "'\n";
    return 0;
  }

  const exegesis::LLVMState *State;
  std::string LastError;
  raw_string_ostream ErrorStream;
  const DenseMap<StringRef, unsigned> &OpcodeNameToOpcodeIdx;
  const DenseMap<StringRef, unsigned> &RegNameToRegNo;
};
} // namespace

// Defining YAML traits for IO.
namespace yaml {

static YamlContext &getTypedContext(void *Ctx) {
  return *reinterpret_cast<YamlContext *>(Ctx);
}

// std::vector<MCInst> will be rendered as a list.
template <> struct SequenceElementTraits<MCInst> {
  static const bool flow = false;
};

template <> struct ScalarTraits<MCInst> {

  static void output(const MCInst &Value, void *Ctx, raw_ostream &Out) {
    getTypedContext(Ctx).serializeMCInst(Value, Out);
  }

  static StringRef input(StringRef Scalar, void *Ctx, MCInst &Value) {
    YamlContext &Context = getTypedContext(Ctx);
    Context.deserializeMCInst(Scalar, Value);
    return Context.getLastError();
  }

  // By default strings are quoted only when necessary.
  // We force the use of single quotes for uniformity.
  static QuotingType mustQuote(StringRef) { return QuotingType::Single; }

  static const bool flow = true;
};

// std::vector<exegesis::Measure> will be rendered as a list.
template <> struct SequenceElementTraits<exegesis::BenchmarkMeasure> {
  static const bool flow = false;
};

template <>
struct CustomMappingTraits<std::map<exegesis::ValidationEvent, int64_t>> {
  static void inputOne(IO &Io, StringRef KeyStr,
                       std::map<exegesis::ValidationEvent, int64_t> &VI) {
    Expected<exegesis::ValidationEvent> Key =
        exegesis::getValidationEventByName(KeyStr);
    if (!Key) {
      Io.setError("Key is not a valid validation event");
      return;
    }
    Io.mapRequired(KeyStr.str().c_str(), VI[*Key]);
  }

  static void output(IO &Io, std::map<exegesis::ValidationEvent, int64_t> &VI) {
    for (auto &IndividualVI : VI) {
      Io.mapRequired(exegesis::getValidationEventName(IndividualVI.first),
                     IndividualVI.second);
    }
  }
};

// exegesis::Measure is rendererd as a flow instead of a list.
// e.g. { "key": "the key", "value": 0123 }
template <> struct MappingTraits<exegesis::BenchmarkMeasure> {
  static void mapping(IO &Io, exegesis::BenchmarkMeasure &Obj) {
    Io.mapRequired("key", Obj.Key);
    if (!Io.outputting()) {
      // For backward compatibility, interpret debug_string as a key.
      Io.mapOptional("debug_string", Obj.Key);
    }
    Io.mapRequired("value", Obj.PerInstructionValue);
    Io.mapOptional("per_snippet_value", Obj.PerSnippetValue);
    Io.mapOptional("validation_counters", Obj.ValidationCounters);
  }
  static const bool flow = true;
};

template <>
struct ScalarEnumerationTraits<exegesis::Benchmark::ModeE> {
  static void enumeration(IO &Io,
                          exegesis::Benchmark::ModeE &Value) {
    Io.enumCase(Value, "", exegesis::Benchmark::Unknown);
    Io.enumCase(Value, "latency", exegesis::Benchmark::Latency);
    Io.enumCase(Value, "uops", exegesis::Benchmark::Uops);
    Io.enumCase(Value, "inverse_throughput",
                exegesis::Benchmark::InverseThroughput);
  }
};

// std::vector<exegesis::RegisterValue> will be rendered as a list.
template <> struct SequenceElementTraits<exegesis::RegisterValue> {
  static const bool flow = false;
};

template <> struct ScalarTraits<exegesis::RegisterValue> {
  static constexpr const unsigned kRadix = 16;
  static constexpr const bool kSigned = false;

  static void output(const exegesis::RegisterValue &RV, void *Ctx,
                     raw_ostream &Out) {
    YamlContext &Context = getTypedContext(Ctx);
    Out << Context.getRegName(RV.Register) << "=0x"
        << toString(RV.Value, kRadix, kSigned);
  }

  static StringRef input(StringRef String, void *Ctx,
                         exegesis::RegisterValue &RV) {
    SmallVector<StringRef, 2> Pieces;
    String.split(Pieces, "=0x", /* MaxSplit */ -1,
                 /* KeepEmpty */ false);
    YamlContext &Context = getTypedContext(Ctx);
    std::optional<unsigned> RegNo;
    if (Pieces.size() == 2 && (RegNo = Context.getRegNo(Pieces[0]))) {
      RV.Register = *RegNo;
      const unsigned BitsNeeded = APInt::getBitsNeeded(Pieces[1], kRadix);
      RV.Value = APInt(BitsNeeded, Pieces[1], kRadix);
    } else {
      Context.getErrorStream()
          << "Unknown initial register value: '" << String << "'";
    }
    return Context.getLastError();
  }

  static QuotingType mustQuote(StringRef) { return QuotingType::Single; }

  static const bool flow = true;
};

template <>
struct MappingContextTraits<exegesis::BenchmarkKey, YamlContext> {
  static void mapping(IO &Io, exegesis::BenchmarkKey &Obj,
                      YamlContext &Context) {
    Io.setContext(&Context);
    Io.mapRequired("instructions", Obj.Instructions);
    Io.mapOptional("config", Obj.Config);
    Io.mapRequired("register_initial_values", Obj.RegisterInitialValues);
  }
};

template <>
struct MappingContextTraits<exegesis::Benchmark, YamlContext> {
  struct NormalizedBinary {
    NormalizedBinary(IO &io) {}
    NormalizedBinary(IO &, std::vector<uint8_t> &Data) : Binary(Data) {}
    std::vector<uint8_t> denormalize(IO &) {
      std::vector<uint8_t> Data;
      std::string Str;
      raw_string_ostream OSS(Str);
      Binary.writeAsBinary(OSS);
      OSS.flush();
      Data.assign(Str.begin(), Str.end());
      return Data;
    }

    BinaryRef Binary;
  };

  static void mapping(IO &Io, exegesis::Benchmark &Obj,
                      YamlContext &Context) {
    Io.mapRequired("mode", Obj.Mode);
    Io.mapRequired("key", Obj.Key, Context);
    Io.mapRequired("cpu_name", Obj.CpuName);
    Io.mapRequired("llvm_triple", Obj.LLVMTriple);
    // Optionally map num_repetitions and min_instructions to the same
    // value to preserve backwards compatibility.
    // TODO(boomanaiden154): Move min_instructions to mapRequired and
    // remove num_repetitions once num_repetitions is ready to be removed
    // completely.
    if (Io.outputting())
      Io.mapRequired("min_instructions", Obj.MinInstructions);
    else {
      Io.mapOptional("num_repetitions", Obj.MinInstructions);
      Io.mapOptional("min_instructions", Obj.MinInstructions);
    }
    Io.mapRequired("measurements", Obj.Measurements);
    Io.mapRequired("error", Obj.Error);
    Io.mapOptional("info", Obj.Info);
    // AssembledSnippet
    MappingNormalization<NormalizedBinary, std::vector<uint8_t>> BinaryString(
        Io, Obj.AssembledSnippet);
    Io.mapOptional("assembled_snippet", BinaryString->Binary);
  }
};

template <> struct MappingTraits<exegesis::Benchmark::TripleAndCpu> {
  static void mapping(IO &Io,
                      exegesis::Benchmark::TripleAndCpu &Obj) {
    assert(!Io.outputting() && "can only read TripleAndCpu");
    // Read triple.
    Io.mapRequired("llvm_triple", Obj.LLVMTriple);
    Io.mapRequired("cpu_name", Obj.CpuName);
    // Drop everything else.
  }
};

} // namespace yaml

namespace exegesis {

Expected<std::set<Benchmark::TripleAndCpu>>
Benchmark::readTriplesAndCpusFromYamls(MemoryBufferRef Buffer) {
  // We're only mapping a field, drop other fields and silence the corresponding
  // warnings.
  yaml::Input Yin(
      Buffer, nullptr, +[](const SMDiagnostic &, void *Context) {});
  Yin.setAllowUnknownKeys(true);
  std::set<TripleAndCpu> Result;
  yaml::EmptyContext Context;
  while (Yin.setCurrentDocument()) {
    TripleAndCpu TC;
    yamlize(Yin, TC, /*unused*/ true, Context);
    if (Yin.error())
      return errorCodeToError(Yin.error());
    Result.insert(TC);
    Yin.nextDocument();
  }
  return Result;
}

Expected<Benchmark>
Benchmark::readYaml(const LLVMState &State, MemoryBufferRef Buffer) {
  yaml::Input Yin(Buffer);
  YamlContext Context(State);
  Benchmark Benchmark;
  if (Yin.setCurrentDocument())
    yaml::yamlize(Yin, Benchmark, /*unused*/ true, Context);
  if (!Context.getLastError().empty())
    return make_error<Failure>(Context.getLastError());
  return std::move(Benchmark);
}

Expected<std::vector<Benchmark>>
Benchmark::readYamls(const LLVMState &State,
                                MemoryBufferRef Buffer) {
  yaml::Input Yin(Buffer);
  YamlContext Context(State);
  std::vector<Benchmark> Benchmarks;
  while (Yin.setCurrentDocument()) {
    Benchmarks.emplace_back();
    yamlize(Yin, Benchmarks.back(), /*unused*/ true, Context);
    if (Yin.error())
      return errorCodeToError(Yin.error());
    if (!Context.getLastError().empty())
      return make_error<Failure>(Context.getLastError());
    Yin.nextDocument();
  }
  return std::move(Benchmarks);
}

Error Benchmark::writeYamlTo(const LLVMState &State,
                                        raw_ostream &OS) {
  auto Cleanup = make_scope_exit([&] { OS.flush(); });
  yaml::Output Yout(OS, nullptr /*Ctx*/, 200 /*WrapColumn*/);
  YamlContext Context(State);
  Yout.beginDocuments();
  yaml::yamlize(Yout, *this, /*unused*/ true, Context);
  if (!Context.getLastError().empty())
    return make_error<Failure>(Context.getLastError());
  Yout.endDocuments();
  return Error::success();
}

Error Benchmark::readYamlFrom(const LLVMState &State,
                                         StringRef InputContent) {
  yaml::Input Yin(InputContent);
  YamlContext Context(State);
  if (Yin.setCurrentDocument())
    yaml::yamlize(Yin, *this, /*unused*/ true, Context);
  if (!Context.getLastError().empty())
    return make_error<Failure>(Context.getLastError());
  return Error::success();
}

void PerInstructionStats::push(const BenchmarkMeasure &BM) {
  if (Key.empty())
    Key = BM.Key;
  assert(Key == BM.Key);
  ++NumValues;
  SumValues += BM.PerInstructionValue;
  MaxValue = std::max(MaxValue, BM.PerInstructionValue);
  MinValue = std::min(MinValue, BM.PerInstructionValue);
}

bool operator==(const BenchmarkMeasure &A, const BenchmarkMeasure &B) {
  return std::tie(A.Key, A.PerInstructionValue, A.PerSnippetValue) ==
         std::tie(B.Key, B.PerInstructionValue, B.PerSnippetValue);
}

} // namespace exegesis
} // namespace llvm
