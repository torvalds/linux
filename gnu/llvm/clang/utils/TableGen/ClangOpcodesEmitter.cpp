//=== ClangOpcodesEmitter.cpp - constexpr interpreter opcodes ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit Clang AST node tables
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;

namespace {
class ClangOpcodesEmitter {
  RecordKeeper &Records;
  unsigned NumTypes;

public:
  ClangOpcodesEmitter(RecordKeeper &R)
      : Records(R), NumTypes(Records.getAllDerivedDefinitions("Type").size()) {}

  void run(raw_ostream &OS);

private:
  /// Emits the opcode name for the opcode enum.
  /// The name is obtained by concatenating the name with the list of types.
  void EmitEnum(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the switch case and the invocation in the interpreter.
  void EmitInterp(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the disassembler.
  void EmitDisasm(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the byte code emitter method.
  void EmitEmitter(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the prototype.
  void EmitProto(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the prototype to dispatch from a type.
  void EmitGroup(raw_ostream &OS, StringRef N, const Record *R);

  /// Emits the evaluator method.
  void EmitEval(raw_ostream &OS, StringRef N, const Record *R);

  void PrintTypes(raw_ostream &OS, ArrayRef<const Record *> Types);
};

void Enumerate(const Record *R, StringRef N,
               std::function<void(ArrayRef<const Record *>, Twine)> &&F) {
  llvm::SmallVector<const Record *, 2> TypePath;
  const auto *Types = R->getValueAsListInit("Types");

  std::function<void(size_t, const Twine &)> Rec;
  Rec = [&TypePath, Types, &Rec, &F](size_t I, const Twine &ID) {
    if (I >= Types->size()) {
      F(TypePath, ID);
      return;
    }

    if (const auto *TypeClass = dyn_cast<DefInit>(Types->getElement(I))) {
      for (const auto *Type :
           TypeClass->getDef()->getValueAsListOfDefs("Types")) {
        TypePath.push_back(Type);
        Rec(I + 1, ID + Type->getName());
        TypePath.pop_back();
      }
    } else {
      PrintFatalError("Expected a type class");
    }
  };
  Rec(0, N);
}

} // namespace

void ClangOpcodesEmitter::run(raw_ostream &OS) {
  for (const auto *Opcode : Records.getAllDerivedDefinitions("Opcode")) {
    // The name is the record name, unless overriden.
    StringRef N = Opcode->getValueAsString("Name");
    if (N.empty())
      N = Opcode->getName();

    EmitEnum(OS, N, Opcode);
    EmitInterp(OS, N, Opcode);
    EmitDisasm(OS, N, Opcode);
    EmitProto(OS, N, Opcode);
    EmitGroup(OS, N, Opcode);
    EmitEmitter(OS, N, Opcode);
    EmitEval(OS, N, Opcode);
  }
}

void ClangOpcodesEmitter::EmitEnum(raw_ostream &OS, StringRef N,
                                   const Record *R) {
  OS << "#ifdef GET_OPCODE_NAMES\n";
  Enumerate(R, N, [&OS](ArrayRef<const Record *>, const Twine &ID) {
    OS << "OP_" << ID << ",\n";
  });
  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitInterp(raw_ostream &OS, StringRef N,
                                     const Record *R) {
  OS << "#ifdef GET_INTERP\n";

  Enumerate(R, N,
            [this, R, &OS, &N](ArrayRef<const Record *> TS, const Twine &ID) {
              bool CanReturn = R->getValueAsBit("CanReturn");
              bool ChangesPC = R->getValueAsBit("ChangesPC");
              const auto &Args = R->getValueAsListOfDefs("Args");

              OS << "case OP_" << ID << ": {\n";

              if (CanReturn)
                OS << "  bool DoReturn = (S.Current == StartFrame);\n";

              // Emit calls to read arguments.
              for (size_t I = 0, N = Args.size(); I < N; ++I) {
                const auto *Arg = Args[I];
                bool AsRef = Arg->getValueAsBit("AsRef");

                if (AsRef)
                  OS << "  const auto &V" << I;
                else
                  OS << "  const auto V" << I;
                OS << " = ";
                OS << "ReadArg<" << Arg->getValueAsString("Name")
                   << ">(S, PC);\n";
              }

              // Emit a call to the template method and pass arguments.
              OS << "  if (!" << N;
              PrintTypes(OS, TS);
              OS << "(S";
              if (ChangesPC)
                OS << ", PC";
              else
                OS << ", OpPC";
              if (CanReturn)
                OS << ", Result";
              for (size_t I = 0, N = Args.size(); I < N; ++I)
                OS << ", V" << I;
              OS << "))\n";
              OS << "    return false;\n";

              // Bail out if interpreter returned.
              if (CanReturn) {
                OS << "  if (!S.Current || S.Current->isRoot())\n";
                OS << "    return true;\n";

                OS << "  if (DoReturn)\n";
                OS << "    return true;\n";
              }

              OS << "  continue;\n";
              OS << "}\n";
            });
  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitDisasm(raw_ostream &OS, StringRef N,
                                     const Record *R) {
  OS << "#ifdef GET_DISASM\n";
  Enumerate(R, N, [R, &OS](ArrayRef<const Record *>, const Twine &ID) {
    OS << "case OP_" << ID << ":\n";
    OS << "  PrintName(\"" << ID << "\");\n";
    OS << "  OS << \"\\t\"";

    for (const auto *Arg : R->getValueAsListOfDefs("Args")) {
      OS << " << ReadArg<" << Arg->getValueAsString("Name") << ">(P, PC)";
      OS << " << \" \"";
    }

    OS << " << \"\\n\";\n";
    OS << "  continue;\n";
  });
  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitEmitter(raw_ostream &OS, StringRef N,
                                      const Record *R) {
  if (R->getValueAsBit("HasCustomLink"))
    return;

  OS << "#ifdef GET_LINK_IMPL\n";
  Enumerate(R, N, [R, &OS](ArrayRef<const Record *>, const Twine &ID) {
    const auto &Args = R->getValueAsListOfDefs("Args");

    // Emit the list of arguments.
    OS << "bool ByteCodeEmitter::emit" << ID << "(";
    for (size_t I = 0, N = Args.size(); I < N; ++I) {
      const auto *Arg = Args[I];
      bool AsRef = Arg->getValueAsBit("AsRef");
      auto Name = Arg->getValueAsString("Name");

      OS << (AsRef ? "const " : " ") << Name << " " << (AsRef ? "&" : "") << "A"
         << I << ", ";
    }
    OS << "const SourceInfo &L) {\n";

    // Emit a call to write the opcodes.
    OS << "  return emitOp<";
    for (size_t I = 0, N = Args.size(); I < N; ++I) {
      if (I != 0)
        OS << ", ";
      OS << Args[I]->getValueAsString("Name");
    }
    OS << ">(OP_" << ID;
    for (size_t I = 0, N = Args.size(); I < N; ++I)
      OS << ", A" << I;
    OS << ", L);\n";
    OS << "}\n";
  });
  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitProto(raw_ostream &OS, StringRef N,
                                    const Record *R) {
  OS << "#if defined(GET_EVAL_PROTO) || defined(GET_LINK_PROTO)\n";
  auto Args = R->getValueAsListOfDefs("Args");
  Enumerate(R, N, [&OS, &Args](ArrayRef<const Record *> TS, const Twine &ID) {
    OS << "bool emit" << ID << "(";
    for (size_t I = 0, N = Args.size(); I < N; ++I) {
      const auto *Arg = Args[I];
      bool AsRef = Arg->getValueAsBit("AsRef");
      auto Name = Arg->getValueAsString("Name");

      OS << (AsRef ? "const " : " ") << Name << " " << (AsRef ? "&" : "")
         << ", ";
    }
    OS << "const SourceInfo &);\n";
  });

  // Emit a template method for custom emitters to have less to implement.
  auto TypeCount = R->getValueAsListInit("Types")->size();
  if (R->getValueAsBit("HasCustomEval") && TypeCount) {
    OS << "#if defined(GET_EVAL_PROTO)\n";
    OS << "template<";
    for (size_t I = 0; I < TypeCount; ++I) {
      if (I != 0)
        OS << ", ";
      OS << "PrimType";
    }
    OS << ">\n";
    OS << "bool emit" << N << "(";
    for (const auto *Arg : Args)
      OS << Arg->getValueAsString("Name") << ", ";
    OS << "const SourceInfo &);\n";
    OS << "#endif\n";
  }

  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitGroup(raw_ostream &OS, StringRef N,
                                    const Record *R) {
  if (!R->getValueAsBit("HasGroup"))
    return;

  const auto *Types = R->getValueAsListInit("Types");
  const auto &Args = R->getValueAsListOfDefs("Args");

  Twine EmitFuncName = "emit" + N;

  // Emit the prototype of the group emitter in the header.
  OS << "#if defined(GET_EVAL_PROTO) || defined(GET_LINK_PROTO)\n";
  OS << "[[nodiscard]] bool " << EmitFuncName << "(";
  for (size_t I = 0, N = Types->size(); I < N; ++I)
    OS << "PrimType, ";
  for (auto *Arg : Args)
    OS << Arg->getValueAsString("Name") << ", ";
  OS << "const SourceInfo &I);\n";
  OS << "#endif\n";

  // Emit the dispatch implementation in the source.
  OS << "#if defined(GET_EVAL_IMPL) || defined(GET_LINK_IMPL)\n";
  OS << "bool\n";
  OS << "#if defined(GET_EVAL_IMPL)\n";
  OS << "EvalEmitter\n";
  OS << "#else\n";
  OS << "ByteCodeEmitter\n";
  OS << "#endif\n";
  OS << "::" << EmitFuncName << "(";
  for (size_t I = 0, N = Types->size(); I < N; ++I)
    OS << "PrimType T" << I << ", ";
  for (size_t I = 0, N = Args.size(); I < N; ++I) {
    const auto *Arg = Args[I];
    bool AsRef = Arg->getValueAsBit("AsRef");
    auto Name = Arg->getValueAsString("Name");

    OS << (AsRef ? "const " : " ") << Name << " " << (AsRef ? "&" : "") << "A"
       << I << ", ";
  }
  OS << "const SourceInfo &I) {\n";

  std::function<void(size_t, const Twine &)> Rec;
  llvm::SmallVector<const Record *, 2> TS;
  Rec = [this, &Rec, &OS, Types, &Args, R, &TS, N,
         EmitFuncName](size_t I, const Twine &ID) {
    if (I >= Types->size()) {
      // Print a call to the emitter method.
      // Custom evaluator methods dispatch to template methods.
      if (R->getValueAsBit("HasCustomEval")) {
        OS << "#ifdef GET_LINK_IMPL\n";
        OS << "    return emit" << ID << "\n";
        OS << "#else\n";
        OS << "    return emit" << N;
        PrintTypes(OS, TS);
        OS << "\n#endif\n";
        OS << "      ";
      } else {
        OS << "    return emit" << ID;
      }

      OS << "(";
      for (size_t I = 0; I < Args.size(); ++I) {
        OS << "A" << I << ", ";
      }
      OS << "I);\n";
      return;
    }

    // Print a switch statement selecting T.
    if (auto *TypeClass = dyn_cast<DefInit>(Types->getElement(I))) {
      OS << "  switch (T" << I << ") {\n";
      auto Cases = TypeClass->getDef()->getValueAsListOfDefs("Types");
      for (auto *Case : Cases) {
        OS << "  case PT_" << Case->getName() << ":\n";
        TS.push_back(Case);
        Rec(I + 1, ID + Case->getName());
        TS.pop_back();
      }
      // Emit a default case if not all types are present.
      if (Cases.size() < NumTypes)
        OS << "  default: llvm_unreachable(\"invalid type: " << EmitFuncName
           << "\");\n";
      OS << "  }\n";
      OS << "  llvm_unreachable(\"invalid enum value\");\n";
    } else {
      PrintFatalError("Expected a type class");
    }
  };
  Rec(0, N);

  OS << "}\n";
  OS << "#endif\n";
}

void ClangOpcodesEmitter::EmitEval(raw_ostream &OS, StringRef N,
                                   const Record *R) {
  if (R->getValueAsBit("HasCustomEval"))
    return;

  OS << "#ifdef GET_EVAL_IMPL\n";
  Enumerate(R, N,
            [this, R, &N, &OS](ArrayRef<const Record *> TS, const Twine &ID) {
              auto Args = R->getValueAsListOfDefs("Args");

              OS << "bool EvalEmitter::emit" << ID << "(";
              for (size_t I = 0, N = Args.size(); I < N; ++I) {
                const auto *Arg = Args[I];
                bool AsRef = Arg->getValueAsBit("AsRef");
                auto Name = Arg->getValueAsString("Name");

                OS << (AsRef ? "const " : " ") << Name << " "
                   << (AsRef ? "&" : "") << "A" << I << ", ";
              }
              OS << "const SourceInfo &L) {\n";
              OS << "  if (!isActive()) return true;\n";
              OS << "  CurrentSource = L;\n";

              OS << "  return " << N;
              PrintTypes(OS, TS);
              OS << "(S, OpPC";
              for (size_t I = 0, N = Args.size(); I < N; ++I)
                OS << ", A" << I;
              OS << ");\n";
              OS << "}\n";
            });

  OS << "#endif\n";
}

void ClangOpcodesEmitter::PrintTypes(raw_ostream &OS,
                                     ArrayRef<const Record *> Types) {
  if (Types.empty())
    return;
  OS << "<";
  for (size_t I = 0, N = Types.size(); I < N; ++I) {
    if (I != 0)
      OS << ", ";
    OS << "PT_" << Types[I]->getName();
  }
  OS << ">";
}

void clang::EmitClangOpcodes(RecordKeeper &Records, raw_ostream &OS) {
  ClangOpcodesEmitter(Records).run(OS);
}
