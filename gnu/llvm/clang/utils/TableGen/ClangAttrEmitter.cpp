//===- ClangAttrEmitter.cpp - Generate Clang attribute handling =-*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit Clang attribute processing code
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "ASTTableGen.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

class FlattenedSpelling {
  std::string V, N, NS;
  bool K = false;
  const Record &OriginalSpelling;

public:
  FlattenedSpelling(const std::string &Variety, const std::string &Name,
                    const std::string &Namespace, bool KnownToGCC,
                    const Record &OriginalSpelling)
      : V(Variety), N(Name), NS(Namespace), K(KnownToGCC),
        OriginalSpelling(OriginalSpelling) {}
  explicit FlattenedSpelling(const Record &Spelling)
      : V(std::string(Spelling.getValueAsString("Variety"))),
        N(std::string(Spelling.getValueAsString("Name"))),
        OriginalSpelling(Spelling) {
    assert(V != "GCC" && V != "Clang" &&
           "Given a GCC spelling, which means this hasn't been flattened!");
    if (V == "CXX11" || V == "C23" || V == "Pragma")
      NS = std::string(Spelling.getValueAsString("Namespace"));
  }

  const std::string &variety() const { return V; }
  const std::string &name() const { return N; }
  const std::string &nameSpace() const { return NS; }
  bool knownToGCC() const { return K; }
  const Record &getSpellingRecord() const { return OriginalSpelling; }
};

} // end anonymous namespace

static std::vector<FlattenedSpelling>
GetFlattenedSpellings(const Record &Attr) {
  std::vector<Record *> Spellings = Attr.getValueAsListOfDefs("Spellings");
  std::vector<FlattenedSpelling> Ret;

  for (const auto &Spelling : Spellings) {
    StringRef Variety = Spelling->getValueAsString("Variety");
    StringRef Name = Spelling->getValueAsString("Name");
    if (Variety == "GCC") {
      Ret.emplace_back("GNU", std::string(Name), "", true, *Spelling);
      Ret.emplace_back("CXX11", std::string(Name), "gnu", true, *Spelling);
      if (Spelling->getValueAsBit("AllowInC"))
        Ret.emplace_back("C23", std::string(Name), "gnu", true, *Spelling);
    } else if (Variety == "Clang") {
      Ret.emplace_back("GNU", std::string(Name), "", false, *Spelling);
      Ret.emplace_back("CXX11", std::string(Name), "clang", false, *Spelling);
      if (Spelling->getValueAsBit("AllowInC"))
        Ret.emplace_back("C23", std::string(Name), "clang", false, *Spelling);
    } else
      Ret.push_back(FlattenedSpelling(*Spelling));
  }

  return Ret;
}

static std::string ReadPCHRecord(StringRef type) {
  return StringSwitch<std::string>(type)
      .EndsWith("Decl *", "Record.readDeclAs<" +
                              std::string(type.data(), 0, type.size() - 1) +
                              ">()")
      .Case("TypeSourceInfo *", "Record.readTypeSourceInfo()")
      .Case("Expr *", "Record.readExpr()")
      .Case("IdentifierInfo *", "Record.readIdentifier()")
      .Case("StringRef", "Record.readString()")
      .Case("ParamIdx", "ParamIdx::deserialize(Record.readInt())")
      .Case("OMPTraitInfo *", "Record.readOMPTraitInfo()")
      .Default("Record.readInt()");
}

// Get a type that is suitable for storing an object of the specified type.
static StringRef getStorageType(StringRef type) {
  return StringSwitch<StringRef>(type)
    .Case("StringRef", "std::string")
    .Default(type);
}

// Assumes that the way to get the value is SA->getname()
static std::string WritePCHRecord(StringRef type, StringRef name) {
  return "Record." +
         StringSwitch<std::string>(type)
             .EndsWith("Decl *", "AddDeclRef(" + std::string(name) + ");\n")
             .Case("TypeSourceInfo *",
                   "AddTypeSourceInfo(" + std::string(name) + ");\n")
             .Case("Expr *", "AddStmt(" + std::string(name) + ");\n")
             .Case("IdentifierInfo *",
                   "AddIdentifierRef(" + std::string(name) + ");\n")
             .Case("StringRef", "AddString(" + std::string(name) + ");\n")
             .Case("ParamIdx",
                   "push_back(" + std::string(name) + ".serialize());\n")
             .Case("OMPTraitInfo *",
                   "writeOMPTraitInfo(" + std::string(name) + ");\n")
             .Default("push_back(" + std::string(name) + ");\n");
}

// Normalize attribute name by removing leading and trailing
// underscores. For example, __foo, foo__, __foo__ would
// become foo.
static StringRef NormalizeAttrName(StringRef AttrName) {
  AttrName.consume_front("__");
  AttrName.consume_back("__");
  return AttrName;
}

// Normalize the name by removing any and all leading and trailing underscores.
// This is different from NormalizeAttrName in that it also handles names like
// _pascal and __pascal.
static StringRef NormalizeNameForSpellingComparison(StringRef Name) {
  return Name.trim("_");
}

// Normalize the spelling of a GNU attribute (i.e. "x" in "__attribute__((x))"),
// removing "__" if it appears at the beginning and end of the attribute's name.
static StringRef NormalizeGNUAttrSpelling(StringRef AttrSpelling) {
  if (AttrSpelling.starts_with("__") && AttrSpelling.ends_with("__")) {
    AttrSpelling = AttrSpelling.substr(2, AttrSpelling.size() - 4);
  }

  return AttrSpelling;
}

typedef std::vector<std::pair<std::string, const Record *>> ParsedAttrMap;

static ParsedAttrMap getParsedAttrList(const RecordKeeper &Records,
                                       ParsedAttrMap *Dupes = nullptr,
                                       bool SemaOnly = true) {
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  std::set<std::string> Seen;
  ParsedAttrMap R;
  for (const auto *Attr : Attrs) {
    if (!SemaOnly || Attr->getValueAsBit("SemaHandler")) {
      std::string AN;
      if (Attr->isSubClassOf("TargetSpecificAttr") &&
          !Attr->isValueUnset("ParseKind")) {
        AN = std::string(Attr->getValueAsString("ParseKind"));

        // If this attribute has already been handled, it does not need to be
        // handled again.
        if (Seen.find(AN) != Seen.end()) {
          if (Dupes)
            Dupes->push_back(std::make_pair(AN, Attr));
          continue;
        }
        Seen.insert(AN);
      } else
        AN = NormalizeAttrName(Attr->getName()).str();

      R.push_back(std::make_pair(AN, Attr));
    }
  }
  return R;
}

namespace {

  class Argument {
    std::string lowerName, upperName;
    StringRef attrName;
    bool isOpt;
    bool Fake;

  public:
    Argument(StringRef Arg, StringRef Attr)
        : lowerName(std::string(Arg)), upperName(lowerName), attrName(Attr),
          isOpt(false), Fake(false) {
      if (!lowerName.empty()) {
        lowerName[0] = std::tolower(lowerName[0]);
        upperName[0] = std::toupper(upperName[0]);
      }
      // Work around MinGW's macro definition of 'interface' to 'struct'. We
      // have an attribute argument called 'Interface', so only the lower case
      // name conflicts with the macro definition.
      if (lowerName == "interface")
        lowerName = "interface_";
    }
    Argument(const Record &Arg, StringRef Attr)
        : Argument(Arg.getValueAsString("Name"), Attr) {}
    virtual ~Argument() = default;

    StringRef getLowerName() const { return lowerName; }
    StringRef getUpperName() const { return upperName; }
    StringRef getAttrName() const { return attrName; }

    bool isOptional() const { return isOpt; }
    void setOptional(bool set) { isOpt = set; }

    bool isFake() const { return Fake; }
    void setFake(bool fake) { Fake = fake; }

    // These functions print the argument contents formatted in different ways.
    virtual void writeAccessors(raw_ostream &OS) const = 0;
    virtual void writeAccessorDefinitions(raw_ostream &OS) const {}
    virtual void writeASTVisitorTraversal(raw_ostream &OS) const {}
    virtual void writeCloneArgs(raw_ostream &OS) const = 0;
    virtual void writeTemplateInstantiationArgs(raw_ostream &OS) const = 0;
    virtual void writeTemplateInstantiation(raw_ostream &OS) const {}
    virtual void writeCtorBody(raw_ostream &OS) const {}
    virtual void writeCtorInitializers(raw_ostream &OS) const = 0;
    virtual void writeCtorDefaultInitializers(raw_ostream &OS) const = 0;
    virtual void writeCtorParameters(raw_ostream &OS) const = 0;
    virtual void writeDeclarations(raw_ostream &OS) const = 0;
    virtual void writePCHReadArgs(raw_ostream &OS) const = 0;
    virtual void writePCHReadDecls(raw_ostream &OS) const = 0;
    virtual void writePCHWrite(raw_ostream &OS) const = 0;
    virtual std::string getIsOmitted() const { return "false"; }
    virtual void writeValue(raw_ostream &OS) const = 0;
    virtual void writeDump(raw_ostream &OS) const = 0;
    virtual void writeDumpChildren(raw_ostream &OS) const {}
    virtual void writeHasChildren(raw_ostream &OS) const { OS << "false"; }

    virtual bool isEnumArg() const { return false; }
    virtual bool isVariadicEnumArg() const { return false; }
    virtual bool isVariadic() const { return false; }

    virtual void writeImplicitCtorArgs(raw_ostream &OS) const {
      OS << getUpperName();
    }
  };

  class SimpleArgument : public Argument {
    std::string type;

  public:
    SimpleArgument(const Record &Arg, StringRef Attr, std::string T)
        : Argument(Arg, Attr), type(std::move(T)) {}

    std::string getType() const { return type; }

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  " << type << " get" << getUpperName() << "() const {\n";
      OS << "    return " << getLowerName() << ";\n";
      OS << "  }";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "A->get" << getUpperName() << "()";
    }

    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "(" << getUpperName() << ")";
    }

    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "()";
    }

    void writeCtorParameters(raw_ostream &OS) const override {
      OS << type << " " << getUpperName();
    }

    void writeDeclarations(raw_ostream &OS) const override {
      OS << type << " " << getLowerName() << ";";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      std::string read = ReadPCHRecord(type);
      OS << "    " << type << " " << getLowerName() << " = " << read << ";\n";
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    "
         << WritePCHRecord(type,
                           "SA->get" + std::string(getUpperName()) + "()");
    }

    std::string getIsOmitted() const override {
      auto IsOneOf = [](StringRef subject, auto... list) {
        return ((subject == list) || ...);
      };

      if (IsOneOf(type, "IdentifierInfo *", "Expr *"))
        return "!get" + getUpperName().str() + "()";
      if (IsOneOf(type, "TypeSourceInfo *"))
        return "!get" + getUpperName().str() + "Loc()";
      if (IsOneOf(type, "ParamIdx"))
        return "!get" + getUpperName().str() + "().isValid()";

      assert(IsOneOf(type, "unsigned", "int", "bool", "FunctionDecl *",
                     "VarDecl *"));
      return "false";
    }

    void writeValue(raw_ostream &OS) const override {
      if (type == "FunctionDecl *")
        OS << "\" << get" << getUpperName()
           << "()->getNameInfo().getAsString() << \"";
      else if (type == "IdentifierInfo *")
        // Some non-optional (comma required) identifier arguments can be the
        // empty string but are then recorded as a nullptr.
        OS << "\" << (get" << getUpperName() << "() ? get" << getUpperName()
           << "()->getName() : \"\") << \"";
      else if (type == "VarDecl *")
        OS << "\" << get" << getUpperName() << "()->getName() << \"";
      else if (type == "TypeSourceInfo *")
        OS << "\" << get" << getUpperName() << "().getAsString() << \"";
      else if (type == "ParamIdx")
        OS << "\" << get" << getUpperName() << "().getSourceIndex() << \"";
      else
        OS << "\" << get" << getUpperName() << "() << \"";
    }

    void writeDump(raw_ostream &OS) const override {
      if (StringRef(type).ends_with("Decl *")) {
        OS << "    OS << \" \";\n";
        OS << "    dumpBareDeclRef(SA->get" << getUpperName() << "());\n";
      } else if (type == "IdentifierInfo *") {
        // Some non-optional (comma required) identifier arguments can be the
        // empty string but are then recorded as a nullptr.
        OS << "    if (SA->get" << getUpperName() << "())\n"
           << "      OS << \" \" << SA->get" << getUpperName()
           << "()->getName();\n";
      } else if (type == "TypeSourceInfo *") {
        if (isOptional())
          OS << "    if (SA->get" << getUpperName() << "Loc())";
        OS << "    OS << \" \" << SA->get" << getUpperName()
           << "().getAsString();\n";
      } else if (type == "bool") {
        OS << "    if (SA->get" << getUpperName() << "()) OS << \" "
           << getUpperName() << "\";\n";
      } else if (type == "int" || type == "unsigned") {
        OS << "    OS << \" \" << SA->get" << getUpperName() << "();\n";
      } else if (type == "ParamIdx") {
        if (isOptional())
          OS << "    if (SA->get" << getUpperName() << "().isValid())\n  ";
        OS << "    OS << \" \" << SA->get" << getUpperName()
           << "().getSourceIndex();\n";
      } else if (type == "OMPTraitInfo *") {
        OS << "    OS << \" \" << SA->get" << getUpperName() << "();\n";
      } else {
        llvm_unreachable("Unknown SimpleArgument type!");
      }
    }
  };

  class DefaultSimpleArgument : public SimpleArgument {
    int64_t Default;

  public:
    DefaultSimpleArgument(const Record &Arg, StringRef Attr,
                          std::string T, int64_t Default)
      : SimpleArgument(Arg, Attr, T), Default(Default) {}

    void writeAccessors(raw_ostream &OS) const override {
      SimpleArgument::writeAccessors(OS);

      OS << "\n\n  static const " << getType() << " Default" << getUpperName()
         << " = ";
      if (getType() == "bool")
        OS << (Default != 0 ? "true" : "false");
      else
        OS << Default;
      OS << ";";
    }
  };

  class StringArgument : public Argument {
  public:
    StringArgument(const Record &Arg, StringRef Attr)
      : Argument(Arg, Attr)
    {}

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  llvm::StringRef get" << getUpperName() << "() const {\n";
      OS << "    return llvm::StringRef(" << getLowerName() << ", "
         << getLowerName() << "Length);\n";
      OS << "  }\n";
      OS << "  unsigned get" << getUpperName() << "Length() const {\n";
      OS << "    return " << getLowerName() << "Length;\n";
      OS << "  }\n";
      OS << "  void set" << getUpperName()
         << "(ASTContext &C, llvm::StringRef S) {\n";
      OS << "    " << getLowerName() << "Length = S.size();\n";
      OS << "    this->" << getLowerName() << " = new (C, 1) char ["
         << getLowerName() << "Length];\n";
      OS << "    if (!S.empty())\n";
      OS << "      std::memcpy(this->" << getLowerName() << ", S.data(), "
         << getLowerName() << "Length);\n";
      OS << "  }";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << "get" << getUpperName() << "()";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "A->get" << getUpperName() << "()";
    }

    void writeCtorBody(raw_ostream &OS) const override {
      OS << "    if (!" << getUpperName() << ".empty())\n";
      OS << "      std::memcpy(" << getLowerName() << ", " << getUpperName()
         << ".data(), " << getLowerName() << "Length);\n";
    }

    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "Length(" << getUpperName() << ".size()),"
         << getLowerName() << "(new (Ctx, 1) char[" << getLowerName()
         << "Length])";
    }

    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "Length(0)," << getLowerName() << "(nullptr)";
    }

    void writeCtorParameters(raw_ostream &OS) const override {
      OS << "llvm::StringRef " << getUpperName();
    }

    void writeDeclarations(raw_ostream &OS) const override {
      OS << "unsigned " << getLowerName() << "Length;\n";
      OS << "char *" << getLowerName() << ";";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    std::string " << getLowerName()
         << "= Record.readString();\n";
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.AddString(SA->get" << getUpperName() << "());\n";
    }

    void writeValue(raw_ostream &OS) const override {
      OS << "\\\"\" << get" << getUpperName() << "() << \"\\\"";
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    OS << \" \\\"\" << SA->get" << getUpperName()
         << "() << \"\\\"\";\n";
    }
  };

  class AlignedArgument : public Argument {
  public:
    AlignedArgument(const Record &Arg, StringRef Attr)
      : Argument(Arg, Attr)
    {}

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  bool is" << getUpperName() << "Dependent() const;\n";
      OS << "  bool is" << getUpperName() << "ErrorDependent() const;\n";

      OS << "  unsigned get" << getUpperName() << "(ASTContext &Ctx) const;\n";

      OS << "  bool is" << getUpperName() << "Expr() const {\n";
      OS << "    return is" << getLowerName() << "Expr;\n";
      OS << "  }\n";

      OS << "  Expr *get" << getUpperName() << "Expr() const {\n";
      OS << "    assert(is" << getLowerName() << "Expr);\n";
      OS << "    return " << getLowerName() << "Expr;\n";
      OS << "  }\n";

      OS << "  TypeSourceInfo *get" << getUpperName() << "Type() const {\n";
      OS << "    assert(!is" << getLowerName() << "Expr);\n";
      OS << "    return " << getLowerName() << "Type;\n";
      OS << "  }";

      OS << "  std::optional<unsigned> getCached" << getUpperName()
         << "Value() const {\n";
      OS << "    return " << getLowerName() << "Cache;\n";
      OS << "  }";

      OS << "  void setCached" << getUpperName()
         << "Value(unsigned AlignVal) {\n";
      OS << "    " << getLowerName() << "Cache = AlignVal;\n";
      OS << "  }";
    }

    void writeAccessorDefinitions(raw_ostream &OS) const override {
      OS << "bool " << getAttrName() << "Attr::is" << getUpperName()
         << "Dependent() const {\n";
      OS << "  if (is" << getLowerName() << "Expr)\n";
      OS << "    return " << getLowerName() << "Expr && (" << getLowerName()
         << "Expr->isValueDependent() || " << getLowerName()
         << "Expr->isTypeDependent());\n";
      OS << "  else\n";
      OS << "    return " << getLowerName()
         << "Type->getType()->isDependentType();\n";
      OS << "}\n";

      OS << "bool " << getAttrName() << "Attr::is" << getUpperName()
         << "ErrorDependent() const {\n";
      OS << "  if (is" << getLowerName() << "Expr)\n";
      OS << "    return " << getLowerName() << "Expr && " << getLowerName()
         << "Expr->containsErrors();\n";
      OS << "  return " << getLowerName()
         << "Type->getType()->containsErrors();\n";
      OS << "}\n";
    }

    void writeASTVisitorTraversal(raw_ostream &OS) const override {
      StringRef Name = getUpperName();
      OS << "  if (A->is" << Name << "Expr()) {\n"
         << "    if (!getDerived().TraverseStmt(A->get" << Name << "Expr()))\n"
         << "      return false;\n"
         << "  } else if (auto *TSI = A->get" << Name << "Type()) {\n"
         << "    if (!getDerived().TraverseTypeLoc(TSI->getTypeLoc()))\n"
         << "      return false;\n"
         << "  }\n";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << "is" << getLowerName() << "Expr, is" << getLowerName()
         << "Expr ? static_cast<void*>(" << getLowerName()
         << "Expr) : " << getLowerName()
         << "Type";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      // FIXME: move the definition in Sema::InstantiateAttrs to here.
      // In the meantime, aligned attributes are cloned.
    }

    void writeCtorBody(raw_ostream &OS) const override {
      OS << "    if (is" << getLowerName() << "Expr)\n";
      OS << "       " << getLowerName() << "Expr = reinterpret_cast<Expr *>("
         << getUpperName() << ");\n";
      OS << "    else\n";
      OS << "       " << getLowerName()
         << "Type = reinterpret_cast<TypeSourceInfo *>(" << getUpperName()
         << ");\n";
    }

    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << "is" << getLowerName() << "Expr(Is" << getUpperName() << "Expr)";
    }

    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << "is" << getLowerName() << "Expr(false)";
    }

    void writeCtorParameters(raw_ostream &OS) const override {
      OS << "bool Is" << getUpperName() << "Expr, void *" << getUpperName();
    }

    void writeImplicitCtorArgs(raw_ostream &OS) const override {
      OS << "Is" << getUpperName() << "Expr, " << getUpperName();
    }

    void writeDeclarations(raw_ostream &OS) const override {
      OS << "bool is" << getLowerName() << "Expr;\n";
      OS << "union {\n";
      OS << "Expr *" << getLowerName() << "Expr;\n";
      OS << "TypeSourceInfo *" << getLowerName() << "Type;\n";
      OS << "};\n";
      OS << "std::optional<unsigned> " << getLowerName() << "Cache;\n";
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << "is" << getLowerName() << "Expr, " << getLowerName() << "Ptr";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    bool is" << getLowerName() << "Expr = Record.readInt();\n";
      OS << "    void *" << getLowerName() << "Ptr;\n";
      OS << "    if (is" << getLowerName() << "Expr)\n";
      OS << "      " << getLowerName() << "Ptr = Record.readExpr();\n";
      OS << "    else\n";
      OS << "      " << getLowerName()
         << "Ptr = Record.readTypeSourceInfo();\n";
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.push_back(SA->is" << getUpperName() << "Expr());\n";
      OS << "    if (SA->is" << getUpperName() << "Expr())\n";
      OS << "      Record.AddStmt(SA->get" << getUpperName() << "Expr());\n";
      OS << "    else\n";
      OS << "      Record.AddTypeSourceInfo(SA->get" << getUpperName()
         << "Type());\n";
    }

    std::string getIsOmitted() const override {
      return "!((is" + getLowerName().str() + "Expr && " +
             getLowerName().str() + "Expr) || (!is" + getLowerName().str() +
             "Expr && " + getLowerName().str() + "Type))";
    }

    void writeValue(raw_ostream &OS) const override {
      OS << "\";\n";
      OS << "    if (is" << getLowerName() << "Expr && " << getLowerName()
         << "Expr)";
      OS << "      " << getLowerName()
         << "Expr->printPretty(OS, nullptr, Policy);\n";
      OS << "    if (!is" << getLowerName() << "Expr && " << getLowerName()
         << "Type)";
      OS << "      " << getLowerName()
         << "Type->getType().print(OS, Policy);\n";
      OS << "    OS << \"";
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    if (!SA->is" << getUpperName() << "Expr())\n";
      OS << "      dumpType(SA->get" << getUpperName()
         << "Type()->getType());\n";
    }

    void writeDumpChildren(raw_ostream &OS) const override {
      OS << "    if (SA->is" << getUpperName() << "Expr())\n";
      OS << "      Visit(SA->get" << getUpperName() << "Expr());\n";
    }

    void writeHasChildren(raw_ostream &OS) const override {
      OS << "SA->is" << getUpperName() << "Expr()";
    }
  };

  class VariadicArgument : public Argument {
    std::string Type, ArgName, ArgSizeName, RangeName;

  protected:
    // Assumed to receive a parameter: raw_ostream OS.
    virtual void writeValueImpl(raw_ostream &OS) const {
      OS << "    OS << Val;\n";
    }
    // Assumed to receive a parameter: raw_ostream OS.
    virtual void writeDumpImpl(raw_ostream &OS) const {
      OS << "      OS << \" \" << Val;\n";
    }

  public:
    VariadicArgument(const Record &Arg, StringRef Attr, std::string T)
        : Argument(Arg, Attr), Type(std::move(T)),
          ArgName(getLowerName().str() + "_"), ArgSizeName(ArgName + "Size"),
          RangeName(std::string(getLowerName())) {}

    VariadicArgument(StringRef Arg, StringRef Attr, std::string T)
        : Argument(Arg, Attr), Type(std::move(T)),
          ArgName(getLowerName().str() + "_"), ArgSizeName(ArgName + "Size"),
          RangeName(std::string(getLowerName())) {}

    const std::string &getType() const { return Type; }
    const std::string &getArgName() const { return ArgName; }
    const std::string &getArgSizeName() const { return ArgSizeName; }
    bool isVariadic() const override { return true; }

    void writeAccessors(raw_ostream &OS) const override {
      std::string IteratorType = getLowerName().str() + "_iterator";
      std::string BeginFn = getLowerName().str() + "_begin()";
      std::string EndFn = getLowerName().str() + "_end()";

      OS << "  typedef " << Type << "* " << IteratorType << ";\n";
      OS << "  " << IteratorType << " " << BeginFn << " const {"
         << " return " << ArgName << "; }\n";
      OS << "  " << IteratorType << " " << EndFn << " const {"
         << " return " << ArgName << " + " << ArgSizeName << "; }\n";
      OS << "  unsigned " << getLowerName() << "_size() const {"
         << " return " << ArgSizeName << "; }\n";
      OS << "  llvm::iterator_range<" << IteratorType << "> " << RangeName
         << "() const { return llvm::make_range(" << BeginFn << ", " << EndFn
         << "); }\n";
    }

    void writeSetter(raw_ostream &OS) const {
      OS << "  void set" << getUpperName() << "(ASTContext &Ctx, ";
      writeCtorParameters(OS);
      OS << ") {\n";
      OS << "    " << ArgSizeName << " = " << getUpperName() << "Size;\n";
      OS << "    " << ArgName << " = new (Ctx, 16) " << getType() << "["
         << ArgSizeName << "];\n";
      OS << "  ";
      writeCtorBody(OS);
      OS << "  }\n";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << ArgName << ", " << ArgSizeName;
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      // This isn't elegant, but we have to go through public methods...
      OS << "A->" << getLowerName() << "_begin(), "
         << "A->" << getLowerName() << "_size()";
    }

    void writeASTVisitorTraversal(raw_ostream &OS) const override {
      // FIXME: Traverse the elements.
    }

    void writeCtorBody(raw_ostream &OS) const override {
      OS << "  std::copy(" << getUpperName() << ", " << getUpperName() << " + "
         << ArgSizeName << ", " << ArgName << ");\n";
    }

    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << ArgSizeName << "(" << getUpperName() << "Size), "
         << ArgName << "(new (Ctx, 16) " << getType() << "["
         << ArgSizeName << "])";
    }

    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << ArgSizeName << "(0), " << ArgName << "(nullptr)";
    }

    void writeCtorParameters(raw_ostream &OS) const override {
      OS << getType() << " *" << getUpperName() << ", unsigned "
         << getUpperName() << "Size";
    }

    void writeImplicitCtorArgs(raw_ostream &OS) const override {
      OS << getUpperName() << ", " << getUpperName() << "Size";
    }

    void writeDeclarations(raw_ostream &OS) const override {
      OS << "  unsigned " << ArgSizeName << ";\n";
      OS << "  " << getType() << " *" << ArgName << ";";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    unsigned " << getLowerName() << "Size = Record.readInt();\n";
      OS << "    SmallVector<" << getType() << ", 4> "
         << getLowerName() << ";\n";
      OS << "    " << getLowerName() << ".reserve(" << getLowerName()
         << "Size);\n";

      // If we can't store the values in the current type (if it's something
      // like StringRef), store them in a different type and convert the
      // container afterwards.
      std::string StorageType = std::string(getStorageType(getType()));
      std::string StorageName = std::string(getLowerName());
      if (StorageType != getType()) {
        StorageName += "Storage";
        OS << "    SmallVector<" << StorageType << ", 4> "
           << StorageName << ";\n";
        OS << "    " << StorageName << ".reserve(" << getLowerName()
           << "Size);\n";
      }

      OS << "    for (unsigned i = 0; i != " << getLowerName() << "Size; ++i)\n";
      std::string read = ReadPCHRecord(Type);
      OS << "      " << StorageName << ".push_back(" << read << ");\n";

      if (StorageType != getType()) {
        OS << "    for (unsigned i = 0; i != " << getLowerName() << "Size; ++i)\n";
        OS << "      " << getLowerName() << ".push_back("
           << StorageName << "[i]);\n";
      }
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << getLowerName() << ".data(), " << getLowerName() << "Size";
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.push_back(SA->" << getLowerName() << "_size());\n";
      OS << "    for (auto &Val : SA->" << RangeName << "())\n";
      OS << "      " << WritePCHRecord(Type, "Val");
    }

    void writeValue(raw_ostream &OS) const override {
      OS << "\";\n";
      OS << "  for (const auto &Val : " << RangeName << "()) {\n"
         << "    DelimitAttributeArgument(OS, IsFirstArgument);\n";
      writeValueImpl(OS);
      OS << "  }\n";
      OS << "  OS << \"";
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    for (const auto &Val : SA->" << RangeName << "())\n";
      writeDumpImpl(OS);
    }
  };

  class VariadicOMPInteropInfoArgument : public VariadicArgument {
  public:
    VariadicOMPInteropInfoArgument(const Record &Arg, StringRef Attr)
        : VariadicArgument(Arg, Attr, "OMPInteropInfo") {}

    void writeDump(raw_ostream &OS) const override {
      OS << "    for (" << getAttrName() << "Attr::" << getLowerName()
         << "_iterator I = SA->" << getLowerName() << "_begin(), E = SA->"
         << getLowerName() << "_end(); I != E; ++I) {\n";
      OS << "      if (I->IsTarget && I->IsTargetSync)\n";
      OS << "        OS << \" Target_TargetSync\";\n";
      OS << "      else if (I->IsTarget)\n";
      OS << "        OS << \" Target\";\n";
      OS << "      else\n";
      OS << "        OS << \" TargetSync\";\n";
      OS << "    }\n";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    unsigned " << getLowerName() << "Size = Record.readInt();\n";
      OS << "    SmallVector<OMPInteropInfo, 4> " << getLowerName() << ";\n";
      OS << "    " << getLowerName() << ".reserve(" << getLowerName()
         << "Size);\n";
      OS << "    for (unsigned I = 0, E = " << getLowerName() << "Size; ";
      OS << "I != E; ++I) {\n";
      OS << "      bool IsTarget = Record.readBool();\n";
      OS << "      bool IsTargetSync = Record.readBool();\n";
      OS << "      " << getLowerName()
         << ".emplace_back(IsTarget, IsTargetSync);\n";
      OS << "    }\n";
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.push_back(SA->" << getLowerName() << "_size());\n";
      OS << "    for (" << getAttrName() << "Attr::" << getLowerName()
         << "_iterator I = SA->" << getLowerName() << "_begin(), E = SA->"
         << getLowerName() << "_end(); I != E; ++I) {\n";
      OS << "      Record.writeBool(I->IsTarget);\n";
      OS << "      Record.writeBool(I->IsTargetSync);\n";
      OS << "    }\n";
    }
  };

  class VariadicParamIdxArgument : public VariadicArgument {
  public:
    VariadicParamIdxArgument(const Record &Arg, StringRef Attr)
        : VariadicArgument(Arg, Attr, "ParamIdx") {}

  public:
    void writeValueImpl(raw_ostream &OS) const override {
      OS << "    OS << Val.getSourceIndex();\n";
    }

    void writeDumpImpl(raw_ostream &OS) const override {
      OS << "      OS << \" \" << Val.getSourceIndex();\n";
    }
  };

  struct VariadicParamOrParamIdxArgument : public VariadicArgument {
    VariadicParamOrParamIdxArgument(const Record &Arg, StringRef Attr)
        : VariadicArgument(Arg, Attr, "int") {}
  };

  // Unique the enums, but maintain the original declaration ordering.
  std::vector<StringRef>
  uniqueEnumsInOrder(const std::vector<StringRef> &enums) {
    std::vector<StringRef> uniques;
    SmallDenseSet<StringRef, 8> unique_set;
    for (const auto &i : enums) {
      if (unique_set.insert(i).second)
        uniques.push_back(i);
    }
    return uniques;
  }

  class EnumArgument : public Argument {
    std::string fullType;
    StringRef shortType;
    std::vector<StringRef> values, enums, uniques;
    bool isExternal;
    bool isCovered;

  public:
    EnumArgument(const Record &Arg, StringRef Attr)
        : Argument(Arg, Attr), values(Arg.getValueAsListOfStrings("Values")),
          enums(Arg.getValueAsListOfStrings("Enums")),
          uniques(uniqueEnumsInOrder(enums)),
          isExternal(Arg.getValueAsBit("IsExternalType")),
          isCovered(Arg.getValueAsBit("IsCovered")) {
      StringRef Type = Arg.getValueAsString("Type");
      shortType = isExternal ? Type.rsplit("::").second : Type;
      // If shortType didn't contain :: at all rsplit will give us an empty
      // string.
      if (shortType.empty())
        shortType = Type;
      fullType = isExternal ? Type : (getAttrName() + "Attr::" + Type).str();

      // FIXME: Emit a proper error
      assert(!uniques.empty());
    }

    bool isEnumArg() const override { return true; }

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  " << fullType << " get" << getUpperName() << "() const {\n";
      OS << "    return " << getLowerName() << ";\n";
      OS << "  }";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "A->get" << getUpperName() << "()";
    }
    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "(" << getUpperName() << ")";
    }
    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "(" << fullType << "(0))";
    }
    void writeCtorParameters(raw_ostream &OS) const override {
      OS << fullType << " " << getUpperName();
    }
    void writeDeclarations(raw_ostream &OS) const override {
      if (!isExternal) {
        auto i = uniques.cbegin(), e = uniques.cend();
        // The last one needs to not have a comma.
        --e;

        OS << "public:\n";
        OS << "  enum " << shortType << " {\n";
        for (; i != e; ++i)
          OS << "    " << *i << ",\n";
        OS << "    " << *e << "\n";
        OS << "  };\n";
      }

      OS << "private:\n";
      OS << "  " << fullType << " " << getLowerName() << ";";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    " << fullType << " " << getLowerName() << "(static_cast<"
         << fullType << ">(Record.readInt()));\n";
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "Record.push_back(static_cast<uint64_t>(SA->get" << getUpperName()
         << "()));\n";
    }

    void writeValue(raw_ostream &OS) const override {
      // FIXME: this isn't 100% correct -- some enum arguments require printing
      // as a string literal, while others require printing as an identifier.
      // Tablegen currently does not distinguish between the two forms.
      OS << "\\\"\" << " << getAttrName() << "Attr::Convert" << shortType
         << "ToStr(get" << getUpperName() << "()) << \"\\\"";
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    switch(SA->get" << getUpperName() << "()) {\n";
      for (const auto &I : uniques) {
        OS << "    case " << fullType << "::" << I << ":\n";
        OS << "      OS << \" " << I << "\";\n";
        OS << "      break;\n";
      }
      if (!isCovered) {
        OS << "    default:\n";
        OS << "      llvm_unreachable(\"Invalid attribute value\");\n";
      }
      OS << "    }\n";
    }

    void writeConversion(raw_ostream &OS, bool Header) const {
      if (Header) {
        OS << "  static bool ConvertStrTo" << shortType << "(StringRef Val, "
           << fullType << " &Out);\n";
        OS << "  static const char *Convert" << shortType << "ToStr("
           << fullType << " Val);\n";
        return;
      }

      OS << "bool " << getAttrName() << "Attr::ConvertStrTo" << shortType
         << "(StringRef Val, " << fullType << " &Out) {\n";
      OS << "  std::optional<" << fullType << "> "
         << "R = llvm::StringSwitch<std::optional<" << fullType << ">>(Val)\n";
      for (size_t I = 0; I < enums.size(); ++I) {
        OS << "    .Case(\"" << values[I] << "\", ";
        OS << fullType << "::" << enums[I] << ")\n";
      }
      OS << "    .Default(std::optional<" << fullType << ">());\n";
      OS << "  if (R) {\n";
      OS << "    Out = *R;\n      return true;\n    }\n";
      OS << "  return false;\n";
      OS << "}\n\n";

      // Mapping from enumeration values back to enumeration strings isn't
      // trivial because some enumeration values have multiple named
      // enumerators, such as type_visibility(internal) and
      // type_visibility(hidden) both mapping to TypeVisibilityAttr::Hidden.
      OS << "const char *" << getAttrName() << "Attr::Convert" << shortType
         << "ToStr(" << fullType << " Val) {\n"
         << "  switch(Val) {\n";
      SmallDenseSet<StringRef, 8> Uniques;
      for (size_t I = 0; I < enums.size(); ++I) {
        if (Uniques.insert(enums[I]).second)
          OS << "  case " << fullType << "::" << enums[I] << ": return \""
             << values[I] << "\";\n";
      }
      if (!isCovered) {
        OS << "  default: llvm_unreachable(\"Invalid attribute value\");\n";
      }
      OS << "  }\n"
         << "  llvm_unreachable(\"No enumerator with that value\");\n"
         << "}\n";
    }
  };

  class VariadicEnumArgument: public VariadicArgument {
    std::string fullType;
    StringRef shortType;
    std::vector<StringRef> values, enums, uniques;
    bool isExternal;
    bool isCovered;

  protected:
    void writeValueImpl(raw_ostream &OS) const override {
      // FIXME: this isn't 100% correct -- some enum arguments require printing
      // as a string literal, while others require printing as an identifier.
      // Tablegen currently does not distinguish between the two forms.
      OS << "    OS << \"\\\"\" << " << getAttrName() << "Attr::Convert"
         << shortType << "ToStr(Val)"
         << "<< \"\\\"\";\n";
    }

  public:
    VariadicEnumArgument(const Record &Arg, StringRef Attr)
        : VariadicArgument(Arg, Attr,
                           std::string(Arg.getValueAsString("Type"))),
          values(Arg.getValueAsListOfStrings("Values")),
          enums(Arg.getValueAsListOfStrings("Enums")),
          uniques(uniqueEnumsInOrder(enums)),
          isExternal(Arg.getValueAsBit("IsExternalType")),
          isCovered(Arg.getValueAsBit("IsCovered")) {
      StringRef Type = Arg.getValueAsString("Type");
      shortType = isExternal ? Type.rsplit("::").second : Type;
      // If shortType didn't contain :: at all rsplit will give us an empty
      // string.
      if (shortType.empty())
        shortType = Type;
      fullType = isExternal ? Type : (getAttrName() + "Attr::" + Type).str();

      // FIXME: Emit a proper error
      assert(!uniques.empty());
    }

    bool isVariadicEnumArg() const override { return true; }

    void writeDeclarations(raw_ostream &OS) const override {
      if (!isExternal) {
        auto i = uniques.cbegin(), e = uniques.cend();
        // The last one needs to not have a comma.
        --e;

        OS << "public:\n";
        OS << "  enum " << shortType << " {\n";
        for (; i != e; ++i)
          OS << "    " << *i << ",\n";
        OS << "    " << *e << "\n";
        OS << "  };\n";
      }
      OS << "private:\n";

      VariadicArgument::writeDeclarations(OS);
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    for (" << getAttrName() << "Attr::" << getLowerName()
         << "_iterator I = SA->" << getLowerName() << "_begin(), E = SA->"
         << getLowerName() << "_end(); I != E; ++I) {\n";
      OS << "      switch(*I) {\n";
      for (const auto &UI : uniques) {
        OS << "    case " << fullType << "::" << UI << ":\n";
        OS << "      OS << \" " << UI << "\";\n";
        OS << "      break;\n";
      }
      if (!isCovered) {
        OS << "    default:\n";
        OS << "      llvm_unreachable(\"Invalid attribute value\");\n";
      }
      OS << "      }\n";
      OS << "    }\n";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    unsigned " << getLowerName() << "Size = Record.readInt();\n";
      OS << "    SmallVector<" << fullType << ", 4> " << getLowerName()
         << ";\n";
      OS << "    " << getLowerName() << ".reserve(" << getLowerName()
         << "Size);\n";
      OS << "    for (unsigned i = " << getLowerName() << "Size; i; --i)\n";
      OS << "      " << getLowerName() << ".push_back("
         << "static_cast<" << fullType << ">(Record.readInt()));\n";
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.push_back(SA->" << getLowerName() << "_size());\n";
      OS << "    for (" << getAttrName() << "Attr::" << getLowerName()
         << "_iterator i = SA->" << getLowerName() << "_begin(), e = SA->"
         << getLowerName() << "_end(); i != e; ++i)\n";
      OS << "      " << WritePCHRecord(fullType, "(*i)");
    }

    void writeConversion(raw_ostream &OS, bool Header) const {
      if (Header) {
        OS << "  static bool ConvertStrTo" << shortType << "(StringRef Val, "
           << fullType << " &Out);\n";
        OS << "  static const char *Convert" << shortType << "ToStr("
           << fullType << " Val);\n";
        return;
      }

      OS << "bool " << getAttrName() << "Attr::ConvertStrTo" << shortType
         << "(StringRef Val, ";
      OS << fullType << " &Out) {\n";
      OS << "  std::optional<" << fullType
         << "> R = llvm::StringSwitch<std::optional<";
      OS << fullType << ">>(Val)\n";
      for (size_t I = 0; I < enums.size(); ++I) {
        OS << "    .Case(\"" << values[I] << "\", ";
        OS << fullType << "::" << enums[I] << ")\n";
      }
      OS << "    .Default(std::optional<" << fullType << ">());\n";
      OS << "  if (R) {\n";
      OS << "    Out = *R;\n      return true;\n    }\n";
      OS << "  return false;\n";
      OS << "}\n\n";

      OS << "const char *" << getAttrName() << "Attr::Convert" << shortType
         << "ToStr(" << fullType << " Val) {\n"
         << "  switch(Val) {\n";
      SmallDenseSet<StringRef, 8> Uniques;
      for (size_t I = 0; I < enums.size(); ++I) {
        if (Uniques.insert(enums[I]).second)
          OS << "  case " << fullType << "::" << enums[I] << ": return \""
             << values[I] << "\";\n";
      }
      if (!isCovered) {
        OS << "  default: llvm_unreachable(\"Invalid attribute value\");\n";
      }
      OS << "  }\n"
         << "  llvm_unreachable(\"No enumerator with that value\");\n"
         << "}\n";
    }
  };

  class VersionArgument : public Argument {
  public:
    VersionArgument(const Record &Arg, StringRef Attr)
      : Argument(Arg, Attr)
    {}

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  VersionTuple get" << getUpperName() << "() const {\n";
      OS << "    return " << getLowerName() << ";\n";
      OS << "  }\n";
      OS << "  void set" << getUpperName()
         << "(ASTContext &C, VersionTuple V) {\n";
      OS << "    " << getLowerName() << " = V;\n";
      OS << "  }";
    }

    void writeCloneArgs(raw_ostream &OS) const override {
      OS << "get" << getUpperName() << "()";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "A->get" << getUpperName() << "()";
    }

    void writeCtorInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "(" << getUpperName() << ")";
    }

    void writeCtorDefaultInitializers(raw_ostream &OS) const override {
      OS << getLowerName() << "()";
    }

    void writeCtorParameters(raw_ostream &OS) const override {
      OS << "VersionTuple " << getUpperName();
    }

    void writeDeclarations(raw_ostream &OS) const override {
      OS << "VersionTuple " << getLowerName() << ";\n";
    }

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    VersionTuple " << getLowerName()
         << "= Record.readVersionTuple();\n";
    }

    void writePCHReadArgs(raw_ostream &OS) const override {
      OS << getLowerName();
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    Record.AddVersionTuple(SA->get" << getUpperName() << "());\n";
    }

    void writeValue(raw_ostream &OS) const override {
      OS << getLowerName() << "=\" << get" << getUpperName() << "() << \"";
    }

    void writeDump(raw_ostream &OS) const override {
      OS << "    OS << \" \" << SA->get" << getUpperName() << "();\n";
    }
  };

  class ExprArgument : public SimpleArgument {
  public:
    ExprArgument(const Record &Arg, StringRef Attr)
      : SimpleArgument(Arg, Attr, "Expr *")
    {}

    void writeASTVisitorTraversal(raw_ostream &OS) const override {
      OS << "  if (!"
         << "getDerived().TraverseStmt(A->get" << getUpperName() << "()))\n";
      OS << "    return false;\n";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "tempInst" << getUpperName();
    }

    void writeTemplateInstantiation(raw_ostream &OS) const override {
      OS << "      " << getType() << " tempInst" << getUpperName() << ";\n";
      OS << "      {\n";
      OS << "        EnterExpressionEvaluationContext "
         << "Unevaluated(S, Sema::ExpressionEvaluationContext::Unevaluated);\n";
      OS << "        ExprResult " << "Result = S.SubstExpr("
         << "A->get" << getUpperName() << "(), TemplateArgs);\n";
      OS << "        if (Result.isInvalid())\n";
      OS << "          return nullptr;\n";
      OS << "        tempInst" << getUpperName() << " = Result.get();\n";
      OS << "      }\n";
    }

    void writeValue(raw_ostream &OS) const override {
      OS << "\";\n";
      OS << "    get" << getUpperName()
         << "()->printPretty(OS, nullptr, Policy);\n";
      OS << "    OS << \"";
    }

    void writeDump(raw_ostream &OS) const override {}

    void writeDumpChildren(raw_ostream &OS) const override {
      OS << "    Visit(SA->get" << getUpperName() << "());\n";
    }

    void writeHasChildren(raw_ostream &OS) const override { OS << "true"; }
  };

  class VariadicExprArgument : public VariadicArgument {
  public:
    VariadicExprArgument(const Record &Arg, StringRef Attr)
      : VariadicArgument(Arg, Attr, "Expr *")
    {}

    VariadicExprArgument(StringRef ArgName, StringRef Attr)
        : VariadicArgument(ArgName, Attr, "Expr *") {}

    void writeASTVisitorTraversal(raw_ostream &OS) const override {
      OS << "  {\n";
      OS << "    " << getType() << " *I = A->" << getLowerName()
         << "_begin();\n";
      OS << "    " << getType() << " *E = A->" << getLowerName()
         << "_end();\n";
      OS << "    for (; I != E; ++I) {\n";
      OS << "      if (!getDerived().TraverseStmt(*I))\n";
      OS << "        return false;\n";
      OS << "    }\n";
      OS << "  }\n";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "tempInst" << getUpperName() << ", "
         << "A->" << getLowerName() << "_size()";
    }

    void writeTemplateInstantiation(raw_ostream &OS) const override {
      OS << "      auto *tempInst" << getUpperName()
         << " = new (C, 16) " << getType()
         << "[A->" << getLowerName() << "_size()];\n";
      OS << "      {\n";
      OS << "        EnterExpressionEvaluationContext "
         << "Unevaluated(S, Sema::ExpressionEvaluationContext::Unevaluated);\n";
      OS << "        " << getType() << " *TI = tempInst" << getUpperName()
         << ";\n";
      OS << "        " << getType() << " *I = A->" << getLowerName()
         << "_begin();\n";
      OS << "        " << getType() << " *E = A->" << getLowerName()
         << "_end();\n";
      OS << "        for (; I != E; ++I, ++TI) {\n";
      OS << "          ExprResult Result = S.SubstExpr(*I, TemplateArgs);\n";
      OS << "          if (Result.isInvalid())\n";
      OS << "            return nullptr;\n";
      OS << "          *TI = Result.get();\n";
      OS << "        }\n";
      OS << "      }\n";
    }

    void writeDump(raw_ostream &OS) const override {}

    void writeDumpChildren(raw_ostream &OS) const override {
      OS << "    for (" << getAttrName() << "Attr::" << getLowerName()
         << "_iterator I = SA->" << getLowerName() << "_begin(), E = SA->"
         << getLowerName() << "_end(); I != E; ++I)\n";
      OS << "      Visit(*I);\n";
    }

    void writeHasChildren(raw_ostream &OS) const override {
      OS << "SA->" << getLowerName() << "_begin() != "
         << "SA->" << getLowerName() << "_end()";
    }
  };

  class VariadicIdentifierArgument : public VariadicArgument {
  public:
    VariadicIdentifierArgument(const Record &Arg, StringRef Attr)
      : VariadicArgument(Arg, Attr, "IdentifierInfo *")
    {}
  };

  class VariadicStringArgument : public VariadicArgument {
  public:
    VariadicStringArgument(const Record &Arg, StringRef Attr)
      : VariadicArgument(Arg, Attr, "StringRef")
    {}

    void writeCtorBody(raw_ostream &OS) const override {
      OS << "  for (size_t I = 0, E = " << getArgSizeName() << "; I != E;\n"
            "       ++I) {\n"
            "    StringRef Ref = " << getUpperName() << "[I];\n"
            "    if (!Ref.empty()) {\n"
            "      char *Mem = new (Ctx, 1) char[Ref.size()];\n"
            "      std::memcpy(Mem, Ref.data(), Ref.size());\n"
            "      " << getArgName() << "[I] = StringRef(Mem, Ref.size());\n"
            "    }\n"
            "  }\n";
    }

    void writeValueImpl(raw_ostream &OS) const override {
      OS << "    OS << \"\\\"\" << Val << \"\\\"\";\n";
    }
  };

  class TypeArgument : public SimpleArgument {
  public:
    TypeArgument(const Record &Arg, StringRef Attr)
      : SimpleArgument(Arg, Attr, "TypeSourceInfo *")
    {}

    void writeAccessors(raw_ostream &OS) const override {
      OS << "  QualType get" << getUpperName() << "() const {\n";
      OS << "    return " << getLowerName() << "->getType();\n";
      OS << "  }";
      OS << "  " << getType() << " get" << getUpperName() << "Loc() const {\n";
      OS << "    return " << getLowerName() << ";\n";
      OS << "  }";
    }

    void writeASTVisitorTraversal(raw_ostream &OS) const override {
      OS << "  if (auto *TSI = A->get" << getUpperName() << "Loc())\n";
      OS << "    if (!getDerived().TraverseTypeLoc(TSI->getTypeLoc()))\n";
      OS << "      return false;\n";
    }

    void writeTemplateInstantiation(raw_ostream &OS) const override {
      OS << "      " << getType() << " tempInst" << getUpperName() << " =\n";
      OS << "        S.SubstType(A->get" << getUpperName() << "Loc(), "
         << "TemplateArgs, A->getLoc(), A->getAttrName());\n";
      OS << "      if (!tempInst" << getUpperName() << ")\n";
      OS << "        return nullptr;\n";
    }

    void writeTemplateInstantiationArgs(raw_ostream &OS) const override {
      OS << "tempInst" << getUpperName();
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    "
         << WritePCHRecord(getType(),
                           "SA->get" + std::string(getUpperName()) + "Loc()");
    }
  };

  class WrappedAttr : public SimpleArgument {
  public:
    WrappedAttr(const Record &Arg, StringRef Attr)
        : SimpleArgument(Arg, Attr, "Attr *") {}

    void writePCHReadDecls(raw_ostream &OS) const override {
      OS << "    Attr *" << getLowerName() << " = Record.readAttr();";
    }

    void writePCHWrite(raw_ostream &OS) const override {
      OS << "    AddAttr(SA->get" << getUpperName() << "());";
    }

    void writeDump(raw_ostream &OS) const override {}

    void writeDumpChildren(raw_ostream &OS) const override {
      OS << "    Visit(SA->get" << getUpperName() << "());\n";
    }

    void writeHasChildren(raw_ostream &OS) const override { OS << "true"; }
  };

  } // end anonymous namespace

static std::unique_ptr<Argument>
createArgument(const Record &Arg, StringRef Attr,
               const Record *Search = nullptr) {
  if (!Search)
    Search = &Arg;

  std::unique_ptr<Argument> Ptr;
  llvm::StringRef ArgName = Search->getName();

  if (ArgName == "AlignedArgument")
    Ptr = std::make_unique<AlignedArgument>(Arg, Attr);
  else if (ArgName == "EnumArgument")
    Ptr = std::make_unique<EnumArgument>(Arg, Attr);
  else if (ArgName == "ExprArgument")
    Ptr = std::make_unique<ExprArgument>(Arg, Attr);
  else if (ArgName == "DeclArgument")
    Ptr = std::make_unique<SimpleArgument>(
        Arg, Attr, (Arg.getValueAsDef("Kind")->getName() + "Decl *").str());
  else if (ArgName == "IdentifierArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "IdentifierInfo *");
  else if (ArgName == "DefaultBoolArgument")
    Ptr = std::make_unique<DefaultSimpleArgument>(
        Arg, Attr, "bool", Arg.getValueAsBit("Default"));
  else if (ArgName == "BoolArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "bool");
  else if (ArgName == "DefaultIntArgument")
    Ptr = std::make_unique<DefaultSimpleArgument>(
        Arg, Attr, "int", Arg.getValueAsInt("Default"));
  else if (ArgName == "IntArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "int");
  else if (ArgName == "StringArgument")
    Ptr = std::make_unique<StringArgument>(Arg, Attr);
  else if (ArgName == "TypeArgument")
    Ptr = std::make_unique<TypeArgument>(Arg, Attr);
  else if (ArgName == "UnsignedArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "unsigned");
  else if (ArgName == "VariadicUnsignedArgument")
    Ptr = std::make_unique<VariadicArgument>(Arg, Attr, "unsigned");
  else if (ArgName == "VariadicStringArgument")
    Ptr = std::make_unique<VariadicStringArgument>(Arg, Attr);
  else if (ArgName == "VariadicEnumArgument")
    Ptr = std::make_unique<VariadicEnumArgument>(Arg, Attr);
  else if (ArgName == "VariadicExprArgument")
    Ptr = std::make_unique<VariadicExprArgument>(Arg, Attr);
  else if (ArgName == "VariadicParamIdxArgument")
    Ptr = std::make_unique<VariadicParamIdxArgument>(Arg, Attr);
  else if (ArgName == "VariadicParamOrParamIdxArgument")
    Ptr = std::make_unique<VariadicParamOrParamIdxArgument>(Arg, Attr);
  else if (ArgName == "ParamIdxArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "ParamIdx");
  else if (ArgName == "VariadicIdentifierArgument")
    Ptr = std::make_unique<VariadicIdentifierArgument>(Arg, Attr);
  else if (ArgName == "VersionArgument")
    Ptr = std::make_unique<VersionArgument>(Arg, Attr);
  else if (ArgName == "WrappedAttr")
    Ptr = std::make_unique<WrappedAttr>(Arg, Attr);
  else if (ArgName == "OMPTraitInfoArgument")
    Ptr = std::make_unique<SimpleArgument>(Arg, Attr, "OMPTraitInfo *");
  else if (ArgName == "VariadicOMPInteropInfoArgument")
    Ptr = std::make_unique<VariadicOMPInteropInfoArgument>(Arg, Attr);

  if (!Ptr) {
    // Search in reverse order so that the most-derived type is handled first.
    ArrayRef<std::pair<Record*, SMRange>> Bases = Search->getSuperClasses();
    for (const auto &Base : llvm::reverse(Bases)) {
      if ((Ptr = createArgument(Arg, Attr, Base.first)))
        break;
    }
  }

  if (Ptr && Arg.getValueAsBit("Optional"))
    Ptr->setOptional(true);

  if (Ptr && Arg.getValueAsBit("Fake"))
    Ptr->setFake(true);

  return Ptr;
}

static void writeAvailabilityValue(raw_ostream &OS) {
  OS << "\" << getPlatform()->getName();\n"
     << "  if (getStrict()) OS << \", strict\";\n"
     << "  if (!getIntroduced().empty()) OS << \", introduced=\" << getIntroduced();\n"
     << "  if (!getDeprecated().empty()) OS << \", deprecated=\" << getDeprecated();\n"
     << "  if (!getObsoleted().empty()) OS << \", obsoleted=\" << getObsoleted();\n"
     << "  if (getUnavailable()) OS << \", unavailable\";\n"
     << "  OS << \"";
}

static void writeDeprecatedAttrValue(raw_ostream &OS, std::string &Variety) {
  OS << "\\\"\" << getMessage() << \"\\\"\";\n";
  // Only GNU deprecated has an optional fixit argument at the second position.
  if (Variety == "GNU")
     OS << "    if (!getReplacement().empty()) OS << \", \\\"\""
           " << getReplacement() << \"\\\"\";\n";
  OS << "    OS << \"";
}

static void writeGetSpellingFunction(const Record &R, raw_ostream &OS) {
  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(R);

  OS << "const char *" << R.getName() << "Attr::getSpelling() const {\n";
  if (Spellings.empty()) {
    OS << "  return \"(No spelling)\";\n}\n\n";
    return;
  }

  OS << "  switch (getAttributeSpellingListIndex()) {\n"
        "  default:\n"
        "    llvm_unreachable(\"Unknown attribute spelling!\");\n"
        "    return \"(No spelling)\";\n";

  for (unsigned I = 0; I < Spellings.size(); ++I)
    OS << "  case " << I << ":\n"
          "    return \"" << Spellings[I].name() << "\";\n";
  // End of the switch statement.
  OS << "  }\n";
  // End of the getSpelling function.
  OS << "}\n\n";
}

static void
writePrettyPrintFunction(const Record &R,
                         const std::vector<std::unique_ptr<Argument>> &Args,
                         raw_ostream &OS) {
  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(R);

  OS << "void " << R.getName() << "Attr::printPretty("
    << "raw_ostream &OS, const PrintingPolicy &Policy) const {\n";

  if (Spellings.empty()) {
    OS << "}\n\n";
    return;
  }

  OS << "  bool IsFirstArgument = true; (void)IsFirstArgument;\n"
     << "  unsigned TrailingOmittedArgs = 0; (void)TrailingOmittedArgs;\n"
     << "  switch (getAttributeSpellingListIndex()) {\n"
     << "  default:\n"
     << "    llvm_unreachable(\"Unknown attribute spelling!\");\n"
     << "    break;\n";

  for (unsigned I = 0; I < Spellings.size(); ++ I) {
    llvm::SmallString<16> Prefix;
    llvm::SmallString<8> Suffix;
    // The actual spelling of the name and namespace (if applicable)
    // of an attribute without considering prefix and suffix.
    llvm::SmallString<64> Spelling;
    std::string Name = Spellings[I].name();
    std::string Variety = Spellings[I].variety();

    if (Variety == "GNU") {
      Prefix = "__attribute__((";
      Suffix = "))";
    } else if (Variety == "CXX11" || Variety == "C23") {
      Prefix = "[[";
      Suffix = "]]";
      std::string Namespace = Spellings[I].nameSpace();
      if (!Namespace.empty()) {
        Spelling += Namespace;
        Spelling += "::";
      }
    } else if (Variety == "Declspec") {
      Prefix = "__declspec(";
      Suffix = ")";
    } else if (Variety == "Microsoft") {
      Prefix = "[";
      Suffix = "]";
    } else if (Variety == "Keyword") {
      Prefix = "";
      Suffix = "";
    } else if (Variety == "Pragma") {
      Prefix = "#pragma ";
      Suffix = "\n";
      std::string Namespace = Spellings[I].nameSpace();
      if (!Namespace.empty()) {
        Spelling += Namespace;
        Spelling += " ";
      }
    } else if (Variety == "HLSLAnnotation") {
      Prefix = ":";
      Suffix = "";
    } else {
      llvm_unreachable("Unknown attribute syntax variety!");
    }

    Spelling += Name;

    OS << "  case " << I << " : {\n"
       << "    OS << \"" << Prefix << Spelling << "\";\n";

    if (Variety == "Pragma") {
      OS << "    printPrettyPragma(OS, Policy);\n";
      OS << "    OS << \"\\n\";";
      OS << "    break;\n";
      OS << "  }\n";
      continue;
    }

    if (Spelling == "availability") {
      OS << "    OS << \"(";
      writeAvailabilityValue(OS);
      OS << ")\";\n";
    } else if (Spelling == "deprecated" || Spelling == "gnu::deprecated") {
      OS << "    OS << \"(";
      writeDeprecatedAttrValue(OS, Variety);
      OS << ")\";\n";
    } else {
      // To avoid printing parentheses around an empty argument list or
      // printing spurious commas at the end of an argument list, we need to
      // determine where the last provided non-fake argument is.
      bool FoundNonOptArg = false;
      for (const auto &arg : llvm::reverse(Args)) {
        if (arg->isFake())
          continue;
        if (FoundNonOptArg)
          continue;
        // FIXME: arg->getIsOmitted() == "false" means we haven't implemented
        // any way to detect whether the argument was omitted.
        if (!arg->isOptional() || arg->getIsOmitted() == "false") {
          FoundNonOptArg = true;
          continue;
        }
        OS << "    if (" << arg->getIsOmitted() << ")\n"
           << "      ++TrailingOmittedArgs;\n";
      }
      unsigned ArgIndex = 0;
      for (const auto &arg : Args) {
        if (arg->isFake())
          continue;
        std::string IsOmitted = arg->getIsOmitted();
        if (arg->isOptional() && IsOmitted != "false")
          OS << "    if (!(" << IsOmitted << ")) {\n";
        // Variadic arguments print their own leading comma.
        if (!arg->isVariadic())
          OS << "    DelimitAttributeArgument(OS, IsFirstArgument);\n";
        OS << "    OS << \"";
        arg->writeValue(OS);
        OS << "\";\n";
        if (arg->isOptional() && IsOmitted != "false")
          OS << "    }\n";
        ++ArgIndex;
      }
      if (ArgIndex != 0)
        OS << "    if (!IsFirstArgument)\n"
           << "      OS << \")\";\n";
    }
    OS << "    OS << \"" << Suffix << "\";\n"
       << "    break;\n"
       << "  }\n";
  }

  // End of the switch statement.
  OS << "}\n";
  // End of the print function.
  OS << "}\n\n";
}

/// Return the index of a spelling in a spelling list.
static unsigned
getSpellingListIndex(const std::vector<FlattenedSpelling> &SpellingList,
                     const FlattenedSpelling &Spelling) {
  assert(!SpellingList.empty() && "Spelling list is empty!");

  for (unsigned Index = 0; Index < SpellingList.size(); ++Index) {
    const FlattenedSpelling &S = SpellingList[Index];
    if (S.variety() != Spelling.variety())
      continue;
    if (S.nameSpace() != Spelling.nameSpace())
      continue;
    if (S.name() != Spelling.name())
      continue;

    return Index;
  }

  llvm_unreachable("Unknown spelling!");
}

static void writeAttrAccessorDefinition(const Record &R, raw_ostream &OS) {
  std::vector<Record*> Accessors = R.getValueAsListOfDefs("Accessors");
  if (Accessors.empty())
    return;

  const std::vector<FlattenedSpelling> SpellingList = GetFlattenedSpellings(R);
  assert(!SpellingList.empty() &&
         "Attribute with empty spelling list can't have accessors!");
  for (const auto *Accessor : Accessors) {
    const StringRef Name = Accessor->getValueAsString("Name");
    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(*Accessor);

    OS << "  bool " << Name
       << "() const { return getAttributeSpellingListIndex() == ";
    for (unsigned Index = 0; Index < Spellings.size(); ++Index) {
      OS << getSpellingListIndex(SpellingList, Spellings[Index]);
      if (Index != Spellings.size() - 1)
        OS << " ||\n    getAttributeSpellingListIndex() == ";
      else
        OS << "; }\n";
    }
  }
}

static bool
SpellingNamesAreCommon(const std::vector<FlattenedSpelling>& Spellings) {
  assert(!Spellings.empty() && "An empty list of spellings was provided");
  std::string FirstName =
      std::string(NormalizeNameForSpellingComparison(Spellings.front().name()));
  for (const auto &Spelling : llvm::drop_begin(Spellings)) {
    std::string Name =
        std::string(NormalizeNameForSpellingComparison(Spelling.name()));
    if (Name != FirstName)
      return false;
  }
  return true;
}

typedef std::map<unsigned, std::string> SemanticSpellingMap;
static std::string
CreateSemanticSpellings(const std::vector<FlattenedSpelling> &Spellings,
                        SemanticSpellingMap &Map) {
  // The enumerants are automatically generated based on the variety,
  // namespace (if present) and name for each attribute spelling. However,
  // care is taken to avoid trampling on the reserved namespace due to
  // underscores.
  std::string Ret("  enum Spelling {\n");
  std::set<std::string> Uniques;
  unsigned Idx = 0;

  // If we have a need to have this many spellings we likely need to add an
  // extra bit to the SpellingIndex in AttributeCommonInfo, then increase the
  // value of SpellingNotCalculated there and here.
  assert(Spellings.size() < 15 &&
         "Too many spellings, would step on SpellingNotCalculated in "
         "AttributeCommonInfo");
  for (auto I = Spellings.begin(), E = Spellings.end(); I != E; ++I, ++Idx) {
    const FlattenedSpelling &S = *I;
    const std::string &Variety = S.variety();
    const std::string &Spelling = S.name();
    const std::string &Namespace = S.nameSpace();
    std::string EnumName;

    EnumName += (Variety + "_");
    if (!Namespace.empty())
      EnumName += (NormalizeNameForSpellingComparison(Namespace).str() +
      "_");
    EnumName += NormalizeNameForSpellingComparison(Spelling);

    // Even if the name is not unique, this spelling index corresponds to a
    // particular enumerant name that we've calculated.
    Map[Idx] = EnumName;

    // Since we have been stripping underscores to avoid trampling on the
    // reserved namespace, we may have inadvertently created duplicate
    // enumerant names. These duplicates are not considered part of the
    // semantic spelling, and can be elided.
    if (Uniques.find(EnumName) != Uniques.end())
      continue;

    Uniques.insert(EnumName);
    if (I != Spellings.begin())
      Ret += ",\n";
    // Duplicate spellings are not considered part of the semantic spelling
    // enumeration, but the spelling index and semantic spelling values are
    // meant to be equivalent, so we must specify a concrete value for each
    // enumerator.
    Ret += "    " + EnumName + " = " + llvm::utostr(Idx);
  }
  Ret += ",\n  SpellingNotCalculated = 15\n";
  Ret += "\n  };\n\n";
  return Ret;
}

void WriteSemanticSpellingSwitch(const std::string &VarName,
                                 const SemanticSpellingMap &Map,
                                 raw_ostream &OS) {
  OS << "  switch (" << VarName << ") {\n    default: "
    << "llvm_unreachable(\"Unknown spelling list index\");\n";
  for (const auto &I : Map)
    OS << "    case " << I.first << ": return " << I.second << ";\n";
  OS << "  }\n";
}

// Note: these values need to match the values used by LateAttrParseKind in
// `Attr.td`
enum class LateAttrParseKind { Never = 0, Standard = 1, ExperimentalExt = 2 };

static LateAttrParseKind getLateAttrParseKind(const Record *Attr) {
  // This function basically does
  // `Attr->getValueAsDef("LateParsed")->getValueAsInt("Kind")` but does a bunch
  // of sanity checking to ensure that `LateAttrParseMode` in `Attr.td` is in
  // sync with the `LateAttrParseKind` enum in this source file.

  static constexpr StringRef LateParsedStr = "LateParsed";
  static constexpr StringRef LateAttrParseKindStr = "LateAttrParseKind";
  static constexpr StringRef KindFieldStr = "Kind";

  auto *LAPK = Attr->getValueAsDef(LateParsedStr);

  // Typecheck the `LateParsed` field.
  SmallVector<Record *, 1> SuperClasses;
  LAPK->getDirectSuperClasses(SuperClasses);
  if (SuperClasses.size() != 1)
    PrintFatalError(Attr, "Field `" + llvm::Twine(LateParsedStr) +
                              "`should only have one super class");

  if (SuperClasses[0]->getName() != LateAttrParseKindStr)
    PrintFatalError(Attr, "Field `" + llvm::Twine(LateParsedStr) +
                              "`should only have type `" +
                              llvm::Twine(LateAttrParseKindStr) +
                              "` but found type `" +
                              SuperClasses[0]->getName() + "`");

  // Get Kind and verify the enum name matches the name in `Attr.td`.
  unsigned Kind = LAPK->getValueAsInt(KindFieldStr);
  switch (LateAttrParseKind(Kind)) {
#define CASE(X)                                                                \
  case LateAttrParseKind::X:                                                   \
    if (LAPK->getName().compare("LateAttrParse" #X) != 0) {                    \
      PrintFatalError(Attr,                                                    \
                      "Field `" + llvm::Twine(LateParsedStr) + "` set to `" +  \
                          LAPK->getName() +                                    \
                          "` but this converts to `LateAttrParseKind::" +      \
                          llvm::Twine(#X) + "`");                              \
    }                                                                          \
    return LateAttrParseKind::X;

    CASE(Never)
    CASE(Standard)
    CASE(ExperimentalExt)
#undef CASE
  }

  // The Kind value is completely invalid
  auto KindValueStr = llvm::utostr(Kind);
  PrintFatalError(Attr, "Field `" + llvm::Twine(LateParsedStr) + "` set to `" +
                            LAPK->getName() + "` has unexpected `" +
                            llvm::Twine(KindFieldStr) + "` value of " +
                            KindValueStr);
}

// Emits the LateParsed property for attributes.
static void emitClangAttrLateParsedListImpl(RecordKeeper &Records,
                                            raw_ostream &OS,
                                            LateAttrParseKind LateParseMode) {
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");

  for (const auto *Attr : Attrs) {
    if (LateAttrParseKind LateParsed = getLateAttrParseKind(Attr);
        LateParsed != LateParseMode)
      continue;

    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(*Attr);

    // FIXME: Handle non-GNU attributes
    for (const auto &I : Spellings) {
      if (I.variety() != "GNU")
        continue;
      OS << ".Case(\"" << I.name() << "\", 1)\n";
    }
  }
}

static void emitClangAttrLateParsedList(RecordKeeper &Records,
                                        raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_LATE_PARSED_LIST)\n";
  emitClangAttrLateParsedListImpl(Records, OS, LateAttrParseKind::Standard);
  OS << "#endif // CLANG_ATTR_LATE_PARSED_LIST\n\n";
}

static void emitClangAttrLateParsedExperimentalList(RecordKeeper &Records,
                                                    raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_LATE_PARSED_EXPERIMENTAL_EXT_LIST)\n";
  emitClangAttrLateParsedListImpl(Records, OS,
                                  LateAttrParseKind::ExperimentalExt);
  OS << "#endif // CLANG_ATTR_LATE_PARSED_EXPERIMENTAL_EXT_LIST\n\n";
}

static bool hasGNUorCXX11Spelling(const Record &Attribute) {
  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(Attribute);
  for (const auto &I : Spellings) {
    if (I.variety() == "GNU" || I.variety() == "CXX11")
      return true;
  }
  return false;
}

namespace {

struct AttributeSubjectMatchRule {
  const Record *MetaSubject;
  const Record *Constraint;

  AttributeSubjectMatchRule(const Record *MetaSubject, const Record *Constraint)
      : MetaSubject(MetaSubject), Constraint(Constraint) {
    assert(MetaSubject && "Missing subject");
  }

  bool isSubRule() const { return Constraint != nullptr; }

  std::vector<Record *> getSubjects() const {
    return (Constraint ? Constraint : MetaSubject)
        ->getValueAsListOfDefs("Subjects");
  }

  std::vector<Record *> getLangOpts() const {
    if (Constraint) {
      // Lookup the options in the sub-rule first, in case the sub-rule
      // overrides the rules options.
      std::vector<Record *> Opts = Constraint->getValueAsListOfDefs("LangOpts");
      if (!Opts.empty())
        return Opts;
    }
    return MetaSubject->getValueAsListOfDefs("LangOpts");
  }

  // Abstract rules are used only for sub-rules
  bool isAbstractRule() const { return getSubjects().empty(); }

  StringRef getName() const {
    return (Constraint ? Constraint : MetaSubject)->getValueAsString("Name");
  }

  bool isNegatedSubRule() const {
    assert(isSubRule() && "Not a sub-rule");
    return Constraint->getValueAsBit("Negated");
  }

  std::string getSpelling() const {
    std::string Result = std::string(MetaSubject->getValueAsString("Name"));
    if (isSubRule()) {
      Result += '(';
      if (isNegatedSubRule())
        Result += "unless(";
      Result += getName();
      if (isNegatedSubRule())
        Result += ')';
      Result += ')';
    }
    return Result;
  }

  std::string getEnumValueName() const {
    SmallString<128> Result;
    Result += "SubjectMatchRule_";
    Result += MetaSubject->getValueAsString("Name");
    if (isSubRule()) {
      Result += "_";
      if (isNegatedSubRule())
        Result += "not_";
      Result += Constraint->getValueAsString("Name");
    }
    if (isAbstractRule())
      Result += "_abstract";
    return std::string(Result);
  }

  std::string getEnumValue() const { return "attr::" + getEnumValueName(); }

  static const char *EnumName;
};

const char *AttributeSubjectMatchRule::EnumName = "attr::SubjectMatchRule";

struct PragmaClangAttributeSupport {
  std::vector<AttributeSubjectMatchRule> Rules;

  class RuleOrAggregateRuleSet {
    std::vector<AttributeSubjectMatchRule> Rules;
    bool IsRule;
    RuleOrAggregateRuleSet(ArrayRef<AttributeSubjectMatchRule> Rules,
                           bool IsRule)
        : Rules(Rules), IsRule(IsRule) {}

  public:
    bool isRule() const { return IsRule; }

    const AttributeSubjectMatchRule &getRule() const {
      assert(IsRule && "not a rule!");
      return Rules[0];
    }

    ArrayRef<AttributeSubjectMatchRule> getAggregateRuleSet() const {
      return Rules;
    }

    static RuleOrAggregateRuleSet
    getRule(const AttributeSubjectMatchRule &Rule) {
      return RuleOrAggregateRuleSet(Rule, /*IsRule=*/true);
    }
    static RuleOrAggregateRuleSet
    getAggregateRuleSet(ArrayRef<AttributeSubjectMatchRule> Rules) {
      return RuleOrAggregateRuleSet(Rules, /*IsRule=*/false);
    }
  };
  llvm::DenseMap<const Record *, RuleOrAggregateRuleSet> SubjectsToRules;

  PragmaClangAttributeSupport(RecordKeeper &Records);

  bool isAttributedSupported(const Record &Attribute);

  void emitMatchRuleList(raw_ostream &OS);

  void generateStrictConformsTo(const Record &Attr, raw_ostream &OS);

  void generateParsingHelpers(raw_ostream &OS);
};

} // end anonymous namespace

static bool isSupportedPragmaClangAttributeSubject(const Record &Subject) {
  // FIXME: #pragma clang attribute does not currently support statement
  // attributes, so test whether the subject is one that appertains to a
  // declaration node. However, it may be reasonable for support for statement
  // attributes to be added.
  if (Subject.isSubClassOf("DeclNode") || Subject.isSubClassOf("DeclBase") ||
      Subject.getName() == "DeclBase")
    return true;

  if (Subject.isSubClassOf("SubsetSubject"))
    return isSupportedPragmaClangAttributeSubject(
        *Subject.getValueAsDef("Base"));

  return false;
}

static bool doesDeclDeriveFrom(const Record *D, const Record *Base) {
  const Record *CurrentBase = D->getValueAsOptionalDef(BaseFieldName);
  if (!CurrentBase)
    return false;
  if (CurrentBase == Base)
    return true;
  return doesDeclDeriveFrom(CurrentBase, Base);
}

PragmaClangAttributeSupport::PragmaClangAttributeSupport(
    RecordKeeper &Records) {
  std::vector<Record *> MetaSubjects =
      Records.getAllDerivedDefinitions("AttrSubjectMatcherRule");
  auto MapFromSubjectsToRules = [this](const Record *SubjectContainer,
                                       const Record *MetaSubject,
                                       const Record *Constraint) {
    Rules.emplace_back(MetaSubject, Constraint);
    std::vector<Record *> ApplicableSubjects =
        SubjectContainer->getValueAsListOfDefs("Subjects");
    for (const auto *Subject : ApplicableSubjects) {
      bool Inserted =
          SubjectsToRules
              .try_emplace(Subject, RuleOrAggregateRuleSet::getRule(
                                        AttributeSubjectMatchRule(MetaSubject,
                                                                  Constraint)))
              .second;
      if (!Inserted) {
        PrintFatalError("Attribute subject match rules should not represent"
                        "same attribute subjects.");
      }
    }
  };
  for (const auto *MetaSubject : MetaSubjects) {
    MapFromSubjectsToRules(MetaSubject, MetaSubject, /*Constraints=*/nullptr);
    std::vector<Record *> Constraints =
        MetaSubject->getValueAsListOfDefs("Constraints");
    for (const auto *Constraint : Constraints)
      MapFromSubjectsToRules(Constraint, MetaSubject, Constraint);
  }

  std::vector<Record *> Aggregates =
      Records.getAllDerivedDefinitions("AttrSubjectMatcherAggregateRule");
  std::vector<Record *> DeclNodes =
    Records.getAllDerivedDefinitions(DeclNodeClassName);
  for (const auto *Aggregate : Aggregates) {
    Record *SubjectDecl = Aggregate->getValueAsDef("Subject");

    // Gather sub-classes of the aggregate subject that act as attribute
    // subject rules.
    std::vector<AttributeSubjectMatchRule> Rules;
    for (const auto *D : DeclNodes) {
      if (doesDeclDeriveFrom(D, SubjectDecl)) {
        auto It = SubjectsToRules.find(D);
        if (It == SubjectsToRules.end())
          continue;
        if (!It->second.isRule() || It->second.getRule().isSubRule())
          continue; // Assume that the rule will be included as well.
        Rules.push_back(It->second.getRule());
      }
    }

    bool Inserted =
        SubjectsToRules
            .try_emplace(SubjectDecl,
                         RuleOrAggregateRuleSet::getAggregateRuleSet(Rules))
            .second;
    if (!Inserted) {
      PrintFatalError("Attribute subject match rules should not represent"
                      "same attribute subjects.");
    }
  }
}

static PragmaClangAttributeSupport &
getPragmaAttributeSupport(RecordKeeper &Records) {
  static PragmaClangAttributeSupport Instance(Records);
  return Instance;
}

void PragmaClangAttributeSupport::emitMatchRuleList(raw_ostream &OS) {
  OS << "#ifndef ATTR_MATCH_SUB_RULE\n";
  OS << "#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, "
        "IsNegated) "
     << "ATTR_MATCH_RULE(Value, Spelling, IsAbstract)\n";
  OS << "#endif\n";
  for (const auto &Rule : Rules) {
    OS << (Rule.isSubRule() ? "ATTR_MATCH_SUB_RULE" : "ATTR_MATCH_RULE") << '(';
    OS << Rule.getEnumValueName() << ", \"" << Rule.getSpelling() << "\", "
       << Rule.isAbstractRule();
    if (Rule.isSubRule())
      OS << ", "
         << AttributeSubjectMatchRule(Rule.MetaSubject, nullptr).getEnumValue()
         << ", " << Rule.isNegatedSubRule();
    OS << ")\n";
  }
  OS << "#undef ATTR_MATCH_SUB_RULE\n";
}

bool PragmaClangAttributeSupport::isAttributedSupported(
    const Record &Attribute) {
  // If the attribute explicitly specified whether to support #pragma clang
  // attribute, use that setting.
  bool Unset;
  bool SpecifiedResult =
    Attribute.getValueAsBitOrUnset("PragmaAttributeSupport", Unset);
  if (!Unset)
    return SpecifiedResult;

  // Opt-out rules:

  // An attribute requires delayed parsing (LateParsed is on).
  switch (getLateAttrParseKind(&Attribute)) {
  case LateAttrParseKind::Never:
    break;
  case LateAttrParseKind::Standard:
    return false;
  case LateAttrParseKind::ExperimentalExt:
    // This is only late parsed in certain parsing contexts when
    // `LangOpts.ExperimentalLateParseAttributes` is true. Information about the
    // parsing context and `LangOpts` is not available in this method so just
    // opt this attribute out.
    return false;
  }

  // An attribute has no GNU/CXX11 spelling
  if (!hasGNUorCXX11Spelling(Attribute))
    return false;
  // An attribute subject list has a subject that isn't covered by one of the
  // subject match rules or has no subjects at all.
  if (Attribute.isValueUnset("Subjects"))
    return false;
  const Record *SubjectObj = Attribute.getValueAsDef("Subjects");
  std::vector<Record *> Subjects = SubjectObj->getValueAsListOfDefs("Subjects");
  bool HasAtLeastOneValidSubject = false;
  for (const auto *Subject : Subjects) {
    if (!isSupportedPragmaClangAttributeSubject(*Subject))
      continue;
    if (!SubjectsToRules.contains(Subject))
      return false;
    HasAtLeastOneValidSubject = true;
  }
  return HasAtLeastOneValidSubject;
}

static std::string GenerateTestExpression(ArrayRef<Record *> LangOpts) {
  std::string Test;

  for (auto *E : LangOpts) {
    if (!Test.empty())
      Test += " || ";

    const StringRef Code = E->getValueAsString("CustomCode");
    if (!Code.empty()) {
      Test += "(";
      Test += Code;
      Test += ")";
      if (!E->getValueAsString("Name").empty()) {
        PrintWarning(
            E->getLoc(),
            "non-empty 'Name' field ignored because 'CustomCode' was supplied");
      }
    } else {
      Test += "LangOpts.";
      Test += E->getValueAsString("Name");
    }
  }

  if (Test.empty())
    return "true";

  return Test;
}

void
PragmaClangAttributeSupport::generateStrictConformsTo(const Record &Attr,
                                                      raw_ostream &OS) {
  if (!isAttributedSupported(Attr) || Attr.isValueUnset("Subjects"))
    return;
  // Generate a function that constructs a set of matching rules that describe
  // to which declarations the attribute should apply to.
  OS << "void getPragmaAttributeMatchRules("
     << "llvm::SmallVectorImpl<std::pair<"
     << AttributeSubjectMatchRule::EnumName
     << ", bool>> &MatchRules, const LangOptions &LangOpts) const override {\n";
  const Record *SubjectObj = Attr.getValueAsDef("Subjects");
  std::vector<Record *> Subjects = SubjectObj->getValueAsListOfDefs("Subjects");
  for (const auto *Subject : Subjects) {
    if (!isSupportedPragmaClangAttributeSubject(*Subject))
      continue;
    auto It = SubjectsToRules.find(Subject);
    assert(It != SubjectsToRules.end() &&
           "This attribute is unsupported by #pragma clang attribute");
    for (const auto &Rule : It->getSecond().getAggregateRuleSet()) {
      // The rule might be language specific, so only subtract it from the given
      // rules if the specific language options are specified.
      std::vector<Record *> LangOpts = Rule.getLangOpts();
      OS << "  MatchRules.push_back(std::make_pair(" << Rule.getEnumValue()
         << ", /*IsSupported=*/" << GenerateTestExpression(LangOpts)
         << "));\n";
    }
  }
  OS << "}\n\n";
}

void PragmaClangAttributeSupport::generateParsingHelpers(raw_ostream &OS) {
  // Generate routines that check the names of sub-rules.
  OS << "std::optional<attr::SubjectMatchRule> "
        "defaultIsAttributeSubjectMatchSubRuleFor(StringRef, bool) {\n";
  OS << "  return std::nullopt;\n";
  OS << "}\n\n";

  llvm::MapVector<const Record *, std::vector<AttributeSubjectMatchRule>>
      SubMatchRules;
  for (const auto &Rule : Rules) {
    if (!Rule.isSubRule())
      continue;
    SubMatchRules[Rule.MetaSubject].push_back(Rule);
  }

  for (const auto &SubMatchRule : SubMatchRules) {
    OS << "std::optional<attr::SubjectMatchRule> "
          "isAttributeSubjectMatchSubRuleFor_"
       << SubMatchRule.first->getValueAsString("Name")
       << "(StringRef Name, bool IsUnless) {\n";
    OS << "  if (IsUnless)\n";
    OS << "    return "
          "llvm::StringSwitch<std::optional<attr::SubjectMatchRule>>(Name).\n";
    for (const auto &Rule : SubMatchRule.second) {
      if (Rule.isNegatedSubRule())
        OS << "    Case(\"" << Rule.getName() << "\", " << Rule.getEnumValue()
           << ").\n";
    }
    OS << "    Default(std::nullopt);\n";
    OS << "  return "
          "llvm::StringSwitch<std::optional<attr::SubjectMatchRule>>(Name).\n";
    for (const auto &Rule : SubMatchRule.second) {
      if (!Rule.isNegatedSubRule())
        OS << "  Case(\"" << Rule.getName() << "\", " << Rule.getEnumValue()
           << ").\n";
    }
    OS << "  Default(std::nullopt);\n";
    OS << "}\n\n";
  }

  // Generate the function that checks for the top-level rules.
  OS << "std::pair<std::optional<attr::SubjectMatchRule>, "
        "std::optional<attr::SubjectMatchRule> (*)(StringRef, "
        "bool)> isAttributeSubjectMatchRule(StringRef Name) {\n";
  OS << "  return "
        "llvm::StringSwitch<std::pair<std::optional<attr::SubjectMatchRule>, "
        "std::optional<attr::SubjectMatchRule> (*) (StringRef, "
        "bool)>>(Name).\n";
  for (const auto &Rule : Rules) {
    if (Rule.isSubRule())
      continue;
    std::string SubRuleFunction;
    if (SubMatchRules.count(Rule.MetaSubject))
      SubRuleFunction =
          ("isAttributeSubjectMatchSubRuleFor_" + Rule.getName()).str();
    else
      SubRuleFunction = "defaultIsAttributeSubjectMatchSubRuleFor";
    OS << "  Case(\"" << Rule.getName() << "\", std::make_pair("
       << Rule.getEnumValue() << ", " << SubRuleFunction << ")).\n";
  }
  OS << "  Default(std::make_pair(std::nullopt, "
        "defaultIsAttributeSubjectMatchSubRuleFor));\n";
  OS << "}\n\n";

  // Generate the function that checks for the submatch rules.
  OS << "const char *validAttributeSubjectMatchSubRules("
     << AttributeSubjectMatchRule::EnumName << " Rule) {\n";
  OS << "  switch (Rule) {\n";
  for (const auto &SubMatchRule : SubMatchRules) {
    OS << "  case "
       << AttributeSubjectMatchRule(SubMatchRule.first, nullptr).getEnumValue()
       << ":\n";
    OS << "  return \"'";
    bool IsFirst = true;
    for (const auto &Rule : SubMatchRule.second) {
      if (!IsFirst)
        OS << ", '";
      IsFirst = false;
      if (Rule.isNegatedSubRule())
        OS << "unless(";
      OS << Rule.getName();
      if (Rule.isNegatedSubRule())
        OS << ')';
      OS << "'";
    }
    OS << "\";\n";
  }
  OS << "  default: return nullptr;\n";
  OS << "  }\n";
  OS << "}\n\n";
}

template <typename Fn>
static void forEachUniqueSpelling(const Record &Attr, Fn &&F) {
  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(Attr);
  SmallDenseSet<StringRef, 8> Seen;
  for (const FlattenedSpelling &S : Spellings) {
    if (Seen.insert(S.name()).second)
      F(S);
  }
}

static bool isTypeArgument(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
         Arg->getSuperClasses().back().first->getName() == "TypeArgument";
}

/// Emits the first-argument-is-type property for attributes.
static void emitClangAttrTypeArgList(RecordKeeper &Records, raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_TYPE_ARG_LIST)\n";
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");

  for (const auto *Attr : Attrs) {
    // Determine whether the first argument is a type.
    std::vector<Record *> Args = Attr->getValueAsListOfDefs("Args");
    if (Args.empty())
      continue;

    if (!isTypeArgument(Args[0]))
      continue;

    // All these spellings take a single type argument.
    forEachUniqueSpelling(*Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", " << "true" << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_TYPE_ARG_LIST\n\n";
}

/// Emits the parse-arguments-in-unevaluated-context property for
/// attributes.
static void emitClangAttrArgContextList(RecordKeeper &Records, raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_ARG_CONTEXT_LIST)\n";
  ParsedAttrMap Attrs = getParsedAttrList(Records);
  for (const auto &I : Attrs) {
    const Record &Attr = *I.second;

    if (!Attr.getValueAsBit("ParseArgumentsAsUnevaluated"))
      continue;

    // All these spellings take are parsed unevaluated.
    forEachUniqueSpelling(Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", " << "true" << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_ARG_CONTEXT_LIST\n\n";
}

static bool isIdentifierArgument(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
    llvm::StringSwitch<bool>(Arg->getSuperClasses().back().first->getName())
    .Case("IdentifierArgument", true)
    .Case("EnumArgument", true)
    .Case("VariadicEnumArgument", true)
    .Default(false);
}

static bool isVariadicIdentifierArgument(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
         llvm::StringSwitch<bool>(
             Arg->getSuperClasses().back().first->getName())
             .Case("VariadicIdentifierArgument", true)
             .Case("VariadicParamOrParamIdxArgument", true)
             .Default(false);
}

static bool isVariadicExprArgument(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
         llvm::StringSwitch<bool>(
             Arg->getSuperClasses().back().first->getName())
             .Case("VariadicExprArgument", true)
             .Default(false);
}

static bool isStringLiteralArgument(const Record *Arg) {
  if (Arg->getSuperClasses().empty())
    return false;
  StringRef ArgKind = Arg->getSuperClasses().back().first->getName();
  if (ArgKind == "EnumArgument")
    return Arg->getValueAsBit("IsString");
  return ArgKind == "StringArgument";
}

static bool isVariadicStringLiteralArgument(const Record *Arg) {
  if (Arg->getSuperClasses().empty())
    return false;
  StringRef ArgKind = Arg->getSuperClasses().back().first->getName();
  if (ArgKind == "VariadicEnumArgument")
    return Arg->getValueAsBit("IsString");
  return ArgKind == "VariadicStringArgument";
}

static void emitClangAttrVariadicIdentifierArgList(RecordKeeper &Records,
                                                   raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_VARIADIC_IDENTIFIER_ARG_LIST)\n";
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  for (const auto *A : Attrs) {
    // Determine whether the first argument is a variadic identifier.
    std::vector<Record *> Args = A->getValueAsListOfDefs("Args");
    if (Args.empty() || !isVariadicIdentifierArgument(Args[0]))
      continue;

    // All these spellings take an identifier argument.
    forEachUniqueSpelling(*A, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", "
         << "true"
         << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_VARIADIC_IDENTIFIER_ARG_LIST\n\n";
}

static bool GenerateTargetSpecificAttrChecks(const Record *R,
                                             std::vector<StringRef> &Arches,
                                             std::string &Test,
                                             std::string *FnName);

// Emits the list of arguments that should be parsed as unevaluated string
// literals for each attribute.
static void emitClangAttrUnevaluatedStringLiteralList(RecordKeeper &Records,
                                                      raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_STRING_LITERAL_ARG_LIST)\n";

  auto MakeMask = [](ArrayRef<Record *> Args) {
    uint32_t Bits = 0;
    assert(Args.size() <= 32 && "unsupported number of arguments in attribute");
    for (uint32_t N = 0; N < Args.size(); ++N) {
      Bits |= (isStringLiteralArgument(Args[N]) << N);
      // If we have a variadic string argument, set all the remaining bits to 1
      if (isVariadicStringLiteralArgument(Args[N])) {
        Bits |= maskTrailingZeros<decltype(Bits)>(N);
        break;
      }
    }
    return Bits;
  };

  auto AddMaskWithTargetCheck = [](const Record *Attr, uint32_t Mask,
                                   std::string &MaskStr) {
    const Record *T = Attr->getValueAsDef("Target");
    std::vector<StringRef> Arches = T->getValueAsListOfStrings("Arches");
    std::string Test;
    GenerateTargetSpecificAttrChecks(T, Arches, Test, nullptr);
    MaskStr.append(Test + " ? " + std::to_string(Mask) + " : ");
  };

  ParsedAttrMap Dupes;
  ParsedAttrMap Attrs = getParsedAttrList(Records, &Dupes, /*SemaOnly=*/false);
  for (const auto &[AttrName, Attr] : Attrs) {
    std::string MaskStr;
    if (Attr->isSubClassOf("TargetSpecificAttr") &&
        !Attr->isValueUnset("ParseKind")) {
      if (uint32_t Mask = MakeMask(Attr->getValueAsListOfDefs("Args")))
        AddMaskWithTargetCheck(Attr, Mask, MaskStr);
      StringRef ParseKind = Attr->getValueAsString("ParseKind");
      for (const auto &[DupeParseKind, DupAttr] : Dupes) {
        if (DupeParseKind != ParseKind)
          continue;
        if (uint32_t Mask = MakeMask(DupAttr->getValueAsListOfDefs("Args")))
          AddMaskWithTargetCheck(DupAttr, Mask, MaskStr);
      }
      if (!MaskStr.empty())
        MaskStr.append("0");
    } else {
      if (uint32_t Mask = MakeMask(Attr->getValueAsListOfDefs("Args")))
        MaskStr = std::to_string(Mask);
    }

    if (MaskStr.empty())
      continue;

    // All these spellings have at least one string literal has argument.
    forEachUniqueSpelling(*Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", " << MaskStr << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_STRING_LITERAL_ARG_LIST\n\n";
}

// Emits the first-argument-is-identifier property for attributes.
static void emitClangAttrIdentifierArgList(RecordKeeper &Records, raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_IDENTIFIER_ARG_LIST)\n";
  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr");

  for (const auto *Attr : Attrs) {
    // Determine whether the first argument is an identifier.
    std::vector<Record *> Args = Attr->getValueAsListOfDefs("Args");
    if (Args.empty() || !isIdentifierArgument(Args[0]))
      continue;

    // All these spellings take an identifier argument.
    forEachUniqueSpelling(*Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", " << "true" << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_IDENTIFIER_ARG_LIST\n\n";
}

// Emits the indexed-argument-is-identifier property for attributes.
static void emitClangAttrStrictIdentifierArgAtIndexList(RecordKeeper &Records,
                                                        raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_STRICT_IDENTIFIER_ARG_AT_INDEX_LIST)\n";
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");

  for (const auto *Attr : Attrs) {
    if (!Attr->getValueAsBit("StrictEnumParameters"))
      continue;
    // Determine whether the first argument is an identifier.
    std::vector<Record *> Args = Attr->getValueAsListOfDefs("Args");
    uint64_t enumAtIndex = 0;
    for (size_t i = 0; i < Args.size(); i++) {
      enumAtIndex |= ((uint64_t)isIdentifierArgument(Args[0])) << i;
    }
    if (!enumAtIndex)
      continue;

    // All these spellings take an identifier argument.
    forEachUniqueSpelling(*Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", " << enumAtIndex << "ull)\n";
    });
  }
  OS << "#endif // CLANG_ATTR_STRICT_IDENTIFIER_ARG_AT_INDEX_LIST\n\n";
}

static bool keywordThisIsaIdentifierInArgument(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
         llvm::StringSwitch<bool>(
             Arg->getSuperClasses().back().first->getName())
             .Case("VariadicParamOrParamIdxArgument", true)
             .Default(false);
}

static void emitClangAttrThisIsaIdentifierArgList(RecordKeeper &Records,
                                                  raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_THIS_ISA_IDENTIFIER_ARG_LIST)\n";
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  for (const auto *A : Attrs) {
    // Determine whether the first argument is a variadic identifier.
    std::vector<Record *> Args = A->getValueAsListOfDefs("Args");
    if (Args.empty() || !keywordThisIsaIdentifierInArgument(Args[0]))
      continue;

    // All these spellings take an identifier argument.
    forEachUniqueSpelling(*A, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", "
         << "true"
         << ")\n";
    });
  }
  OS << "#endif // CLANG_ATTR_THIS_ISA_IDENTIFIER_ARG_LIST\n\n";
}

static void emitClangAttrAcceptsExprPack(RecordKeeper &Records,
                                         raw_ostream &OS) {
  OS << "#if defined(CLANG_ATTR_ACCEPTS_EXPR_PACK)\n";
  ParsedAttrMap Attrs = getParsedAttrList(Records);
  for (const auto &I : Attrs) {
    const Record &Attr = *I.second;

    if (!Attr.getValueAsBit("AcceptsExprPack"))
      continue;

    forEachUniqueSpelling(Attr, [&](const FlattenedSpelling &S) {
      OS << ".Case(\"" << S.name() << "\", true)\n";
    });
  }
  OS << "#endif // CLANG_ATTR_ACCEPTS_EXPR_PACK\n\n";
}

static bool isRegularKeywordAttribute(const FlattenedSpelling &S) {
  return (S.variety() == "Keyword" &&
          !S.getSpellingRecord().getValueAsBit("HasOwnParseRules"));
}

static void emitFormInitializer(raw_ostream &OS,
                                const FlattenedSpelling &Spelling,
                                StringRef SpellingIndex) {
  bool IsAlignas =
      (Spelling.variety() == "Keyword" && Spelling.name() == "alignas");
  OS << "{AttributeCommonInfo::AS_" << Spelling.variety() << ", "
     << SpellingIndex << ", " << (IsAlignas ? "true" : "false")
     << " /*IsAlignas*/, "
     << (isRegularKeywordAttribute(Spelling) ? "true" : "false")
     << " /*IsRegularKeywordAttribute*/}";
}

static void emitAttributes(RecordKeeper &Records, raw_ostream &OS,
                           bool Header) {
  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr");
  ParsedAttrMap AttrMap = getParsedAttrList(Records);

  // Helper to print the starting character of an attribute argument. If there
  // hasn't been an argument yet, it prints an opening parenthese; otherwise it
  // prints a comma.
  OS << "static inline void DelimitAttributeArgument("
     << "raw_ostream& OS, bool& IsFirst) {\n"
     << "  if (IsFirst) {\n"
     << "    IsFirst = false;\n"
     << "    OS << \"(\";\n"
     << "  } else\n"
     << "    OS << \", \";\n"
     << "}\n";

  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;

    // FIXME: Currently, documentation is generated as-needed due to the fact
    // that there is no way to allow a generated project "reach into" the docs
    // directory (for instance, it may be an out-of-tree build). However, we want
    // to ensure that every attribute has a Documentation field, and produce an
    // error if it has been neglected. Otherwise, the on-demand generation which
    // happens server-side will fail. This code is ensuring that functionality,
    // even though this Emitter doesn't technically need the documentation.
    // When attribute documentation can be generated as part of the build
    // itself, this code can be removed.
    (void)R.getValueAsListOfDefs("Documentation");

    if (!R.getValueAsBit("ASTNode"))
      continue;

    ArrayRef<std::pair<Record *, SMRange>> Supers = R.getSuperClasses();
    assert(!Supers.empty() && "Forgot to specify a superclass for the attr");
    std::string SuperName;
    bool Inheritable = false;
    for (const auto &Super : llvm::reverse(Supers)) {
      const Record *R = Super.first;
      if (R->getName() != "TargetSpecificAttr" &&
          R->getName() != "DeclOrTypeAttr" && SuperName.empty())
        SuperName = std::string(R->getName());
      if (R->getName() == "InheritableAttr")
        Inheritable = true;
    }

    if (Header)
      OS << "class " << R.getName() << "Attr : public " << SuperName << " {\n";
    else
      OS << "\n// " << R.getName() << "Attr implementation\n\n";

    std::vector<Record*> ArgRecords = R.getValueAsListOfDefs("Args");
    std::vector<std::unique_ptr<Argument>> Args;
    Args.reserve(ArgRecords.size());

    bool AttrAcceptsExprPack = Attr->getValueAsBit("AcceptsExprPack");
    if (AttrAcceptsExprPack) {
      for (size_t I = 0; I < ArgRecords.size(); ++I) {
        const Record *ArgR = ArgRecords[I];
        if (isIdentifierArgument(ArgR) || isVariadicIdentifierArgument(ArgR) ||
            isTypeArgument(ArgR))
          PrintFatalError(Attr->getLoc(),
                          "Attributes accepting packs cannot also "
                          "have identifier or type arguments.");
        // When trying to determine if value-dependent expressions can populate
        // the attribute without prior instantiation, the decision is made based
        // on the assumption that only the last argument is ever variadic.
        if (I < (ArgRecords.size() - 1) && isVariadicExprArgument(ArgR))
          PrintFatalError(Attr->getLoc(),
                          "Attributes accepting packs can only have the last "
                          "argument be variadic.");
      }
    }

    bool HasOptArg = false;
    bool HasFakeArg = false;
    for (const auto *ArgRecord : ArgRecords) {
      Args.emplace_back(createArgument(*ArgRecord, R.getName()));
      if (Header) {
        Args.back()->writeDeclarations(OS);
        OS << "\n\n";
      }

      // For these purposes, fake takes priority over optional.
      if (Args.back()->isFake()) {
        HasFakeArg = true;
      } else if (Args.back()->isOptional()) {
        HasOptArg = true;
      }
    }

    std::unique_ptr<VariadicExprArgument> DelayedArgs = nullptr;
    if (AttrAcceptsExprPack) {
      DelayedArgs =
          std::make_unique<VariadicExprArgument>("DelayedArgs", R.getName());
      if (Header) {
        DelayedArgs->writeDeclarations(OS);
        OS << "\n\n";
      }
    }

    if (Header)
      OS << "public:\n";

    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(R);

    // If there are zero or one spellings, all spelling-related functionality
    // can be elided. If all of the spellings share the same name, the spelling
    // functionality can also be elided.
    bool ElideSpelling = (Spellings.size() <= 1) ||
                         SpellingNamesAreCommon(Spellings);

    // This maps spelling index values to semantic Spelling enumerants.
    SemanticSpellingMap SemanticToSyntacticMap;

    std::string SpellingEnum;
    if (Spellings.size() > 1)
      SpellingEnum = CreateSemanticSpellings(Spellings, SemanticToSyntacticMap);
    if (Header)
      OS << SpellingEnum;

    const auto &ParsedAttrSpellingItr = llvm::find_if(
        AttrMap, [R](const std::pair<std::string, const Record *> &P) {
          return &R == P.second;
        });

    // Emit CreateImplicit factory methods.
    auto emitCreate = [&](bool Implicit, bool DelayedArgsOnly, bool emitFake) {
      if (Header)
        OS << "  static ";
      OS << R.getName() << "Attr *";
      if (!Header)
        OS << R.getName() << "Attr::";
      OS << "Create";
      if (Implicit)
        OS << "Implicit";
      if (DelayedArgsOnly)
        OS << "WithDelayedArgs";
      OS << "(";
      OS << "ASTContext &Ctx";
      if (!DelayedArgsOnly) {
        for (auto const &ai : Args) {
          if (ai->isFake() && !emitFake)
            continue;
          OS << ", ";
          ai->writeCtorParameters(OS);
        }
      } else {
        OS << ", ";
        DelayedArgs->writeCtorParameters(OS);
      }
      OS << ", const AttributeCommonInfo &CommonInfo";
      OS << ")";
      if (Header) {
        OS << ";\n";
        return;
      }

      OS << " {\n";
      OS << "  auto *A = new (Ctx) " << R.getName();
      OS << "Attr(Ctx, CommonInfo";

      if (!DelayedArgsOnly) {
        for (auto const &ai : Args) {
          if (ai->isFake() && !emitFake)
            continue;
          OS << ", ";
          ai->writeImplicitCtorArgs(OS);
        }
      }
      OS << ");\n";
      if (Implicit) {
        OS << "  A->setImplicit(true);\n";
      }
      if (Implicit || ElideSpelling) {
        OS << "  if (!A->isAttributeSpellingListCalculated() && "
              "!A->getAttrName())\n";
        OS << "    A->setAttributeSpellingListIndex(0);\n";
      }
      if (DelayedArgsOnly) {
        OS << "  A->setDelayedArgs(Ctx, ";
        DelayedArgs->writeImplicitCtorArgs(OS);
        OS << ");\n";
      }
      OS << "  return A;\n}\n\n";
    };

    auto emitCreateNoCI = [&](bool Implicit, bool DelayedArgsOnly,
                              bool emitFake) {
      if (Header)
        OS << "  static ";
      OS << R.getName() << "Attr *";
      if (!Header)
        OS << R.getName() << "Attr::";
      OS << "Create";
      if (Implicit)
        OS << "Implicit";
      if (DelayedArgsOnly)
        OS << "WithDelayedArgs";
      OS << "(";
      OS << "ASTContext &Ctx";
      if (!DelayedArgsOnly) {
        for (auto const &ai : Args) {
          if (ai->isFake() && !emitFake)
            continue;
          OS << ", ";
          ai->writeCtorParameters(OS);
        }
      } else {
        OS << ", ";
        DelayedArgs->writeCtorParameters(OS);
      }
      OS << ", SourceRange Range";
      if (Header)
        OS << " = {}";
      if (Spellings.size() > 1) {
        OS << ", Spelling S";
        if (Header)
          OS << " = " << SemanticToSyntacticMap[0];
      }
      OS << ")";
      if (Header) {
        OS << ";\n";
        return;
      }

      OS << " {\n";
      OS << "  AttributeCommonInfo I(Range, ";

      if (ParsedAttrSpellingItr != std::end(AttrMap))
        OS << "AT_" << ParsedAttrSpellingItr->first;
      else
        OS << "NoSemaHandlerAttribute";

      if (Spellings.size() == 0) {
        OS << ", AttributeCommonInfo::Form::Implicit()";
      } else if (Spellings.size() == 1) {
        OS << ", ";
        emitFormInitializer(OS, Spellings[0], "0");
      } else {
        OS << ", [&]() {\n";
        OS << "    switch (S) {\n";
        std::set<std::string> Uniques;
        unsigned Idx = 0;
        for (auto I = Spellings.begin(), E = Spellings.end(); I != E;
             ++I, ++Idx) {
          const FlattenedSpelling &S = *I;
          const auto &Name = SemanticToSyntacticMap[Idx];
          if (Uniques.insert(Name).second) {
            OS << "    case " << Name << ":\n";
            OS << "      return AttributeCommonInfo::Form";
            emitFormInitializer(OS, S, Name);
            OS << ";\n";
          }
        }
        OS << "    default:\n";
        OS << "      llvm_unreachable(\"Unknown attribute spelling!\");\n"
           << "      return AttributeCommonInfo::Form";
        emitFormInitializer(OS, Spellings[0], "0");
        OS << ";\n"
           << "    }\n"
           << "  }()";
      }

      OS << ");\n";
      OS << "  return Create";
      if (Implicit)
        OS << "Implicit";
      if (DelayedArgsOnly)
        OS << "WithDelayedArgs";
      OS << "(Ctx";
      if (!DelayedArgsOnly) {
        for (auto const &ai : Args) {
          if (ai->isFake() && !emitFake)
            continue;
          OS << ", ";
          ai->writeImplicitCtorArgs(OS);
        }
      } else {
        OS << ", ";
        DelayedArgs->writeImplicitCtorArgs(OS);
      }
      OS << ", I);\n";
      OS << "}\n\n";
    };

    auto emitCreates = [&](bool DelayedArgsOnly, bool emitFake) {
      emitCreate(true, DelayedArgsOnly, emitFake);
      emitCreate(false, DelayedArgsOnly, emitFake);
      emitCreateNoCI(true, DelayedArgsOnly, emitFake);
      emitCreateNoCI(false, DelayedArgsOnly, emitFake);
    };

    if (Header)
      OS << "  // Factory methods\n";

    // Emit a CreateImplicit that takes all the arguments.
    emitCreates(false, true);

    // Emit a CreateImplicit that takes all the non-fake arguments.
    if (HasFakeArg)
      emitCreates(false, false);

    // Emit a CreateWithDelayedArgs that takes only the dependent argument
    // expressions.
    if (DelayedArgs)
      emitCreates(true, false);

    // Emit constructors.
    auto emitCtor = [&](bool emitOpt, bool emitFake, bool emitNoArgs) {
      auto shouldEmitArg = [=](const std::unique_ptr<Argument> &arg) {
        if (emitNoArgs)
          return false;
        if (arg->isFake())
          return emitFake;
        if (arg->isOptional())
          return emitOpt;
        return true;
      };
      if (Header)
        OS << "  ";
      else
        OS << R.getName() << "Attr::";
      OS << R.getName()
         << "Attr(ASTContext &Ctx, const AttributeCommonInfo &CommonInfo";
      OS << '\n';
      for (auto const &ai : Args) {
        if (!shouldEmitArg(ai))
          continue;
        OS << "              , ";
        ai->writeCtorParameters(OS);
        OS << "\n";
      }

      OS << "             )";
      if (Header) {
        OS << ";\n";
        return;
      }
      OS << "\n  : " << SuperName << "(Ctx, CommonInfo, ";
      OS << "attr::" << R.getName() << ", ";

      // Handle different late parsing modes.
      OS << "/*IsLateParsed=*/";
      switch (getLateAttrParseKind(&R)) {
      case LateAttrParseKind::Never:
        OS << "false";
        break;
      case LateAttrParseKind::ExperimentalExt:
        // Currently no clients need to know the distinction between `Standard`
        // and `ExperimentalExt` so treat `ExperimentalExt` just like
        // `Standard` for now.
      case LateAttrParseKind::Standard:
        // Note: This is misleading. `IsLateParsed` doesn't mean the
        // attribute was actually late parsed. Instead it means the attribute in
        // `Attr.td` is marked as being late parsed. Maybe it should be called
        // `IsLateParseable`?
        OS << "true";
        break;
      }

      if (Inheritable) {
        OS << ", "
           << (R.getValueAsBit("InheritEvenIfAlreadyPresent") ? "true"
                                                              : "false");
      }
      OS << ")\n";

      for (auto const &ai : Args) {
        OS << "              , ";
        if (!shouldEmitArg(ai)) {
          ai->writeCtorDefaultInitializers(OS);
        } else {
          ai->writeCtorInitializers(OS);
        }
        OS << "\n";
      }
      if (DelayedArgs) {
        OS << "              , ";
        DelayedArgs->writeCtorDefaultInitializers(OS);
        OS << "\n";
      }

      OS << "  {\n";

      for (auto const &ai : Args) {
        if (!shouldEmitArg(ai))
          continue;
        ai->writeCtorBody(OS);
      }
      OS << "}\n\n";
    };

    if (Header)
      OS << "\n  // Constructors\n";

    // Emit a constructor that includes all the arguments.
    // This is necessary for cloning.
    emitCtor(true, true, false);

    // Emit a constructor that takes all the non-fake arguments.
    if (HasFakeArg)
      emitCtor(true, false, false);

    // Emit a constructor that takes all the non-fake, non-optional arguments.
    if (HasOptArg)
      emitCtor(false, false, false);

    // Emit constructors that takes no arguments if none already exists.
    // This is used for delaying arguments.
    bool HasRequiredArgs =
        llvm::count_if(Args, [=](const std::unique_ptr<Argument> &arg) {
          return !arg->isFake() && !arg->isOptional();
        });
    if (DelayedArgs && HasRequiredArgs)
      emitCtor(false, false, true);

    if (Header) {
      OS << '\n';
      OS << "  " << R.getName() << "Attr *clone(ASTContext &C) const;\n";
      OS << "  void printPretty(raw_ostream &OS,\n"
         << "                   const PrintingPolicy &Policy) const;\n";
      OS << "  const char *getSpelling() const;\n";
    }

    if (!ElideSpelling) {
      assert(!SemanticToSyntacticMap.empty() && "Empty semantic mapping list");
      if (Header)
        OS << "  Spelling getSemanticSpelling() const;\n";
      else {
        OS << R.getName() << "Attr::Spelling " << R.getName()
           << "Attr::getSemanticSpelling() const {\n";
        WriteSemanticSpellingSwitch("getAttributeSpellingListIndex()",
                                    SemanticToSyntacticMap, OS);
        OS << "}\n";
      }
    }

    if (Header)
      writeAttrAccessorDefinition(R, OS);

    for (auto const &ai : Args) {
      if (Header) {
        ai->writeAccessors(OS);
      } else {
        ai->writeAccessorDefinitions(OS);
      }
      OS << "\n\n";

      // Don't write conversion routines for fake arguments.
      if (ai->isFake()) continue;

      if (ai->isEnumArg())
        static_cast<const EnumArgument *>(ai.get())->writeConversion(OS,
                                                                     Header);
      else if (ai->isVariadicEnumArg())
        static_cast<const VariadicEnumArgument *>(ai.get())->writeConversion(
            OS, Header);
    }

    if (Header) {
      if (DelayedArgs) {
        DelayedArgs->writeAccessors(OS);
        DelayedArgs->writeSetter(OS);
      }

      OS << R.getValueAsString("AdditionalMembers");
      OS << "\n\n";

      OS << "  static bool classof(const Attr *A) { return A->getKind() == "
         << "attr::" << R.getName() << "; }\n";

      OS << "};\n\n";
    } else {
      if (DelayedArgs)
        DelayedArgs->writeAccessorDefinitions(OS);

      OS << R.getName() << "Attr *" << R.getName()
         << "Attr::clone(ASTContext &C) const {\n";
      OS << "  auto *A = new (C) " << R.getName() << "Attr(C, *this";
      for (auto const &ai : Args) {
        OS << ", ";
        ai->writeCloneArgs(OS);
      }
      OS << ");\n";
      OS << "  A->Inherited = Inherited;\n";
      OS << "  A->IsPackExpansion = IsPackExpansion;\n";
      OS << "  A->setImplicit(Implicit);\n";
      if (DelayedArgs) {
        OS << "  A->setDelayedArgs(C, ";
        DelayedArgs->writeCloneArgs(OS);
        OS << ");\n";
      }
      OS << "  return A;\n}\n\n";

      writePrettyPrintFunction(R, Args, OS);
      writeGetSpellingFunction(R, OS);
    }
  }
}
// Emits the class definitions for attributes.
void clang::EmitClangAttrClass(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute classes' definitions", OS, Records);

  OS << "#ifndef LLVM_CLANG_ATTR_CLASSES_INC\n";
  OS << "#define LLVM_CLANG_ATTR_CLASSES_INC\n\n";

  emitAttributes(Records, OS, true);

  OS << "#endif // LLVM_CLANG_ATTR_CLASSES_INC\n";
}

// Emits the class method definitions for attributes.
void clang::EmitClangAttrImpl(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute classes' member function definitions", OS,
                       Records);

  emitAttributes(Records, OS, false);

  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");

  // Instead of relying on virtual dispatch we just create a huge dispatch
  // switch. This is both smaller and faster than virtual functions.
  auto EmitFunc = [&](const char *Method) {
    OS << "  switch (getKind()) {\n";
    for (const auto *Attr : Attrs) {
      const Record &R = *Attr;
      if (!R.getValueAsBit("ASTNode"))
        continue;

      OS << "  case attr::" << R.getName() << ":\n";
      OS << "    return cast<" << R.getName() << "Attr>(this)->" << Method
         << ";\n";
    }
    OS << "  }\n";
    OS << "  llvm_unreachable(\"Unexpected attribute kind!\");\n";
    OS << "}\n\n";
  };

  OS << "const char *Attr::getSpelling() const {\n";
  EmitFunc("getSpelling()");

  OS << "Attr *Attr::clone(ASTContext &C) const {\n";
  EmitFunc("clone(C)");

  OS << "void Attr::printPretty(raw_ostream &OS, "
        "const PrintingPolicy &Policy) const {\n";
  EmitFunc("printPretty(OS, Policy)");
}

static void emitAttrList(raw_ostream &OS, StringRef Class,
                         const std::vector<Record*> &AttrList) {
  for (auto Cur : AttrList) {
    OS << Class << "(" << Cur->getName() << ")\n";
  }
}

// Determines if an attribute has a Pragma spelling.
static bool AttrHasPragmaSpelling(const Record *R) {
  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(*R);
  return llvm::any_of(Spellings, [](const FlattenedSpelling &S) {
    return S.variety() == "Pragma";
  });
}

namespace {

  struct AttrClassDescriptor {
    const char * const MacroName;
    const char * const TableGenName;
  };

} // end anonymous namespace

static const AttrClassDescriptor AttrClassDescriptors[] = {
  { "ATTR", "Attr" },
  { "TYPE_ATTR", "TypeAttr" },
  { "STMT_ATTR", "StmtAttr" },
  { "DECL_OR_STMT_ATTR", "DeclOrStmtAttr" },
  { "INHERITABLE_ATTR", "InheritableAttr" },
  { "DECL_OR_TYPE_ATTR", "DeclOrTypeAttr" },
  { "INHERITABLE_PARAM_ATTR", "InheritableParamAttr" },
  { "PARAMETER_ABI_ATTR", "ParameterABIAttr" },
  { "HLSL_ANNOTATION_ATTR", "HLSLAnnotationAttr"}
};

static void emitDefaultDefine(raw_ostream &OS, StringRef name,
                              const char *superName) {
  OS << "#ifndef " << name << "\n";
  OS << "#define " << name << "(NAME) ";
  if (superName) OS << superName << "(NAME)";
  OS << "\n#endif\n\n";
}

namespace {

  /// A class of attributes.
  struct AttrClass {
    const AttrClassDescriptor &Descriptor;
    Record *TheRecord;
    AttrClass *SuperClass = nullptr;
    std::vector<AttrClass*> SubClasses;
    std::vector<Record*> Attrs;

    AttrClass(const AttrClassDescriptor &Descriptor, Record *R)
      : Descriptor(Descriptor), TheRecord(R) {}

    void emitDefaultDefines(raw_ostream &OS) const {
      // Default the macro unless this is a root class (i.e. Attr).
      if (SuperClass) {
        emitDefaultDefine(OS, Descriptor.MacroName,
                          SuperClass->Descriptor.MacroName);
      }
    }

    void emitUndefs(raw_ostream &OS) const {
      OS << "#undef " << Descriptor.MacroName << "\n";
    }

    void emitAttrList(raw_ostream &OS) const {
      for (auto SubClass : SubClasses) {
        SubClass->emitAttrList(OS);
      }

      ::emitAttrList(OS, Descriptor.MacroName, Attrs);
    }

    void classifyAttrOnRoot(Record *Attr) {
      bool result = classifyAttr(Attr);
      assert(result && "failed to classify on root"); (void) result;
    }

    void emitAttrRange(raw_ostream &OS) const {
      OS << "ATTR_RANGE(" << Descriptor.TableGenName
         << ", " << getFirstAttr()->getName()
         << ", " << getLastAttr()->getName() << ")\n";
    }

  private:
    bool classifyAttr(Record *Attr) {
      // Check all the subclasses.
      for (auto SubClass : SubClasses) {
        if (SubClass->classifyAttr(Attr))
          return true;
      }

      // It's not more specific than this class, but it might still belong here.
      if (Attr->isSubClassOf(TheRecord)) {
        Attrs.push_back(Attr);
        return true;
      }

      return false;
    }

    Record *getFirstAttr() const {
      if (!SubClasses.empty())
        return SubClasses.front()->getFirstAttr();
      return Attrs.front();
    }

    Record *getLastAttr() const {
      if (!Attrs.empty())
        return Attrs.back();
      return SubClasses.back()->getLastAttr();
    }
  };

  /// The entire hierarchy of attribute classes.
  class AttrClassHierarchy {
    std::vector<std::unique_ptr<AttrClass>> Classes;

  public:
    AttrClassHierarchy(RecordKeeper &Records) {
      // Find records for all the classes.
      for (auto &Descriptor : AttrClassDescriptors) {
        Record *ClassRecord = Records.getClass(Descriptor.TableGenName);
        AttrClass *Class = new AttrClass(Descriptor, ClassRecord);
        Classes.emplace_back(Class);
      }

      // Link up the hierarchy.
      for (auto &Class : Classes) {
        if (AttrClass *SuperClass = findSuperClass(Class->TheRecord)) {
          Class->SuperClass = SuperClass;
          SuperClass->SubClasses.push_back(Class.get());
        }
      }

#ifndef NDEBUG
      for (auto i = Classes.begin(), e = Classes.end(); i != e; ++i) {
        assert((i == Classes.begin()) == ((*i)->SuperClass == nullptr) &&
               "only the first class should be a root class!");
      }
#endif
    }

    void emitDefaultDefines(raw_ostream &OS) const {
      for (auto &Class : Classes) {
        Class->emitDefaultDefines(OS);
      }
    }

    void emitUndefs(raw_ostream &OS) const {
      for (auto &Class : Classes) {
        Class->emitUndefs(OS);
      }
    }

    void emitAttrLists(raw_ostream &OS) const {
      // Just start from the root class.
      Classes[0]->emitAttrList(OS);
    }

    void emitAttrRanges(raw_ostream &OS) const {
      for (auto &Class : Classes)
        Class->emitAttrRange(OS);
    }

    void classifyAttr(Record *Attr) {
      // Add the attribute to the root class.
      Classes[0]->classifyAttrOnRoot(Attr);
    }

  private:
    AttrClass *findClassByRecord(Record *R) const {
      for (auto &Class : Classes) {
        if (Class->TheRecord == R)
          return Class.get();
      }
      return nullptr;
    }

    AttrClass *findSuperClass(Record *R) const {
      // TableGen flattens the superclass list, so we just need to walk it
      // in reverse.
      auto SuperClasses = R->getSuperClasses();
      for (signed i = 0, e = SuperClasses.size(); i != e; ++i) {
        auto SuperClass = findClassByRecord(SuperClasses[e - i - 1].first);
        if (SuperClass) return SuperClass;
      }
      return nullptr;
    }
  };

} // end anonymous namespace

namespace clang {

// Emits the enumeration list for attributes.
void EmitClangAttrList(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("List of all attributes that Clang recognizes", OS,
                       Records);

  AttrClassHierarchy Hierarchy(Records);

  // Add defaulting macro definitions.
  Hierarchy.emitDefaultDefines(OS);
  emitDefaultDefine(OS, "PRAGMA_SPELLING_ATTR", nullptr);

  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  std::vector<Record *> PragmaAttrs;
  for (auto *Attr : Attrs) {
    if (!Attr->getValueAsBit("ASTNode"))
      continue;

    // Add the attribute to the ad-hoc groups.
    if (AttrHasPragmaSpelling(Attr))
      PragmaAttrs.push_back(Attr);

    // Place it in the hierarchy.
    Hierarchy.classifyAttr(Attr);
  }

  // Emit the main attribute list.
  Hierarchy.emitAttrLists(OS);

  // Emit the ad hoc groups.
  emitAttrList(OS, "PRAGMA_SPELLING_ATTR", PragmaAttrs);

  // Emit the attribute ranges.
  OS << "#ifdef ATTR_RANGE\n";
  Hierarchy.emitAttrRanges(OS);
  OS << "#undef ATTR_RANGE\n";
  OS << "#endif\n";

  Hierarchy.emitUndefs(OS);
  OS << "#undef PRAGMA_SPELLING_ATTR\n";
}

// Emits the enumeration list for attributes.
void EmitClangAttrSubjectMatchRuleList(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader(
      "List of all attribute subject matching rules that Clang recognizes", OS,
      Records);
  PragmaClangAttributeSupport &PragmaAttributeSupport =
      getPragmaAttributeSupport(Records);
  emitDefaultDefine(OS, "ATTR_MATCH_RULE", nullptr);
  PragmaAttributeSupport.emitMatchRuleList(OS);
  OS << "#undef ATTR_MATCH_RULE\n";
}

// Emits the code to read an attribute from a precompiled header.
void EmitClangAttrPCHRead(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute deserialization code", OS, Records);

  Record *InhClass = Records.getClass("InheritableAttr");
  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr"),
                       ArgRecords;
  std::vector<std::unique_ptr<Argument>> Args;
  std::unique_ptr<VariadicExprArgument> DelayedArgs;

  OS << "  switch (Kind) {\n";
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;

    OS << "  case attr::" << R.getName() << ": {\n";
    if (R.isSubClassOf(InhClass))
      OS << "    bool isInherited = Record.readInt();\n";
    OS << "    bool isImplicit = Record.readInt();\n";
    OS << "    bool isPackExpansion = Record.readInt();\n";
    DelayedArgs = nullptr;
    if (Attr->getValueAsBit("AcceptsExprPack")) {
      DelayedArgs =
          std::make_unique<VariadicExprArgument>("DelayedArgs", R.getName());
      DelayedArgs->writePCHReadDecls(OS);
    }
    ArgRecords = R.getValueAsListOfDefs("Args");
    Args.clear();
    for (const auto *Arg : ArgRecords) {
      Args.emplace_back(createArgument(*Arg, R.getName()));
      Args.back()->writePCHReadDecls(OS);
    }
    OS << "    New = new (Context) " << R.getName() << "Attr(Context, Info";
    for (auto const &ri : Args) {
      OS << ", ";
      ri->writePCHReadArgs(OS);
    }
    OS << ");\n";
    if (R.isSubClassOf(InhClass))
      OS << "    cast<InheritableAttr>(New)->setInherited(isInherited);\n";
    OS << "    New->setImplicit(isImplicit);\n";
    OS << "    New->setPackExpansion(isPackExpansion);\n";
    if (DelayedArgs) {
      OS << "    cast<" << R.getName()
         << "Attr>(New)->setDelayedArgs(Context, ";
      DelayedArgs->writePCHReadArgs(OS);
      OS << ");\n";
    }
    OS << "    break;\n";
    OS << "  }\n";
  }
  OS << "  }\n";
}

// Emits the code to write an attribute to a precompiled header.
void EmitClangAttrPCHWrite(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute serialization code", OS, Records);

  Record *InhClass = Records.getClass("InheritableAttr");
  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr"), Args;

  OS << "  switch (A->getKind()) {\n";
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;
    OS << "  case attr::" << R.getName() << ": {\n";
    Args = R.getValueAsListOfDefs("Args");
    if (R.isSubClassOf(InhClass) || !Args.empty())
      OS << "    const auto *SA = cast<" << R.getName()
         << "Attr>(A);\n";
    if (R.isSubClassOf(InhClass))
      OS << "    Record.push_back(SA->isInherited());\n";
    OS << "    Record.push_back(A->isImplicit());\n";
    OS << "    Record.push_back(A->isPackExpansion());\n";
    if (Attr->getValueAsBit("AcceptsExprPack"))
      VariadicExprArgument("DelayedArgs", R.getName()).writePCHWrite(OS);

    for (const auto *Arg : Args)
      createArgument(*Arg, R.getName())->writePCHWrite(OS);
    OS << "    break;\n";
    OS << "  }\n";
  }
  OS << "  }\n";
}

} // namespace clang

// Helper function for GenerateTargetSpecificAttrChecks that alters the 'Test'
// parameter with only a single check type, if applicable.
static bool GenerateTargetSpecificAttrCheck(const Record *R, std::string &Test,
                                            std::string *FnName,
                                            StringRef ListName,
                                            StringRef CheckAgainst,
                                            StringRef Scope) {
  if (!R->isValueUnset(ListName)) {
    Test += " && (";
    std::vector<StringRef> Items = R->getValueAsListOfStrings(ListName);
    for (auto I = Items.begin(), E = Items.end(); I != E; ++I) {
      StringRef Part = *I;
      Test += CheckAgainst;
      Test += " == ";
      Test += Scope;
      Test += Part;
      if (I + 1 != E)
        Test += " || ";
      if (FnName)
        *FnName += Part;
    }
    Test += ")";
    return true;
  }
  return false;
}

// Generate a conditional expression to check if the current target satisfies
// the conditions for a TargetSpecificAttr record, and append the code for
// those checks to the Test string. If the FnName string pointer is non-null,
// append a unique suffix to distinguish this set of target checks from other
// TargetSpecificAttr records.
static bool GenerateTargetSpecificAttrChecks(const Record *R,
                                             std::vector<StringRef> &Arches,
                                             std::string &Test,
                                             std::string *FnName) {
  bool AnyTargetChecks = false;

  // It is assumed that there will be an llvm::Triple object
  // named "T" and a TargetInfo object named "Target" within
  // scope that can be used to determine whether the attribute exists in
  // a given target.
  Test += "true";
  // If one or more architectures is specified, check those.  Arches are handled
  // differently because GenerateTargetRequirements needs to combine the list
  // with ParseKind.
  if (!Arches.empty()) {
    AnyTargetChecks = true;
    Test += " && (";
    for (auto I = Arches.begin(), E = Arches.end(); I != E; ++I) {
      StringRef Part = *I;
      Test += "T.getArch() == llvm::Triple::";
      Test += Part;
      if (I + 1 != E)
        Test += " || ";
      if (FnName)
        *FnName += Part;
    }
    Test += ")";
  }

  // If the attribute is specific to particular OSes, check those.
  AnyTargetChecks |= GenerateTargetSpecificAttrCheck(
      R, Test, FnName, "OSes", "T.getOS()", "llvm::Triple::");

  // If one or more object formats is specified, check those.
  AnyTargetChecks |=
      GenerateTargetSpecificAttrCheck(R, Test, FnName, "ObjectFormats",
                                      "T.getObjectFormat()", "llvm::Triple::");

  // If custom code is specified, emit it.
  StringRef Code = R->getValueAsString("CustomCode");
  if (!Code.empty()) {
    AnyTargetChecks = true;
    Test += " && (";
    Test += Code;
    Test += ")";
  }

  return AnyTargetChecks;
}

static void GenerateHasAttrSpellingStringSwitch(
    const std::vector<std::pair<const Record *, FlattenedSpelling>> &Attrs,
    raw_ostream &OS, const std::string &Variety,
    const std::string &Scope = "") {
  for (const auto &[Attr, Spelling] : Attrs) {
    // C++11-style attributes have specific version information associated with
    // them. If the attribute has no scope, the version information must not
    // have the default value (1), as that's incorrect. Instead, the unscoped
    // attribute version information should be taken from the SD-6 standing
    // document, which can be found at:
    // https://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations
    //
    // C23-style attributes have the same kind of version information
    // associated with them. The unscoped attribute version information should
    // be taken from the specification of the attribute in the C Standard.
    //
    // Clang-specific attributes have the same kind of version information
    // associated with them. This version is typically the default value (1).
    // These version values are clang-specific and should typically be
    // incremented once the attribute changes its syntax and/or semantics in a
    // a way that is impactful to the end user.
    int Version = 1;

    assert(Spelling.variety() == Variety);
    std::string Name = "";
    if (Spelling.nameSpace().empty() || Scope == Spelling.nameSpace()) {
      Name = Spelling.name();
      Version = static_cast<int>(
          Spelling.getSpellingRecord().getValueAsInt("Version"));
      // Verify that explicitly specified CXX11 and C23 spellings (i.e.
      // not inferred from Clang/GCC spellings) have a version that's
      // different from the default (1).
      bool RequiresValidVersion =
          (Variety == "CXX11" || Variety == "C23") &&
          Spelling.getSpellingRecord().getValueAsString("Variety") == Variety;
      if (RequiresValidVersion && Scope.empty() && Version == 1)
        PrintError(Spelling.getSpellingRecord().getLoc(),
                   "Standard attributes must have "
                   "valid version information.");
    }

    std::string Test;
    if (Attr->isSubClassOf("TargetSpecificAttr")) {
      const Record *R = Attr->getValueAsDef("Target");
      std::vector<StringRef> Arches = R->getValueAsListOfStrings("Arches");
      GenerateTargetSpecificAttrChecks(R, Arches, Test, nullptr);
    } else if (!Attr->getValueAsListOfDefs("TargetSpecificSpellings").empty()) {
      // Add target checks if this spelling is target-specific.
      const std::vector<Record *> TargetSpellings =
          Attr->getValueAsListOfDefs("TargetSpecificSpellings");
      for (const auto &TargetSpelling : TargetSpellings) {
        // Find spelling that matches current scope and name.
        for (const auto &Spelling : GetFlattenedSpellings(*TargetSpelling)) {
          if (Scope == Spelling.nameSpace() && Name == Spelling.name()) {
            const Record *Target = TargetSpelling->getValueAsDef("Target");
            std::vector<StringRef> Arches =
                Target->getValueAsListOfStrings("Arches");
            GenerateTargetSpecificAttrChecks(Target, Arches, Test,
                                             /*FnName=*/nullptr);
            break;
          }
        }
      }
    }

    std::string TestStr = !Test.empty()
                              ? Test + " ? " + llvm::itostr(Version) + " : 0"
                              : llvm::itostr(Version);
    if (Scope.empty() || Scope == Spelling.nameSpace())
      OS << "    .Case(\"" << Spelling.name() << "\", " << TestStr << ")\n";
  }
  OS << "    .Default(0);\n";
}

namespace clang {

// Emits list of regular keyword attributes with info about their arguments.
void EmitClangRegularKeywordAttributeInfo(RecordKeeper &Records,
                                          raw_ostream &OS) {
  emitSourceFileHeader(
      "A list of regular keyword attributes generated from the attribute"
      " definitions",
      OS);
  // Assume for now that the same token is not used in multiple regular
  // keyword attributes.
  for (auto *R : Records.getAllDerivedDefinitions("Attr"))
    for (const auto &S : GetFlattenedSpellings(*R)) {
      if (!isRegularKeywordAttribute(S))
        continue;
      std::vector<Record *> Args = R->getValueAsListOfDefs("Args");
      bool HasArgs = llvm::any_of(
          Args, [](const Record *Arg) { return !Arg->getValueAsBit("Fake"); });

      OS << "KEYWORD_ATTRIBUTE("
         << S.getSpellingRecord().getValueAsString("Name") << ", "
         << (HasArgs ? "true" : "false") << ", )\n";
    }
  OS << "#undef KEYWORD_ATTRIBUTE\n";
}

// Emits the list of spellings for attributes.
void EmitClangAttrHasAttrImpl(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Code to implement the __has_attribute logic", OS,
                       Records);

  // Separate all of the attributes out into four group: generic, C++11, GNU,
  // and declspecs. Then generate a big switch statement for each of them.
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  std::vector<std::pair<const Record *, FlattenedSpelling>> Declspec, Microsoft,
      GNU, Pragma, HLSLAnnotation;
  std::map<std::string,
           std::vector<std::pair<const Record *, FlattenedSpelling>>>
      CXX, C23;

  // Walk over the list of all attributes, and split them out based on the
  // spelling variety.
  for (auto *R : Attrs) {
    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(*R);
    for (const auto &SI : Spellings) {
      const std::string &Variety = SI.variety();
      if (Variety == "GNU")
        GNU.emplace_back(R, SI);
      else if (Variety == "Declspec")
        Declspec.emplace_back(R, SI);
      else if (Variety == "Microsoft")
        Microsoft.emplace_back(R, SI);
      else if (Variety == "CXX11")
        CXX[SI.nameSpace()].emplace_back(R, SI);
      else if (Variety == "C23")
        C23[SI.nameSpace()].emplace_back(R, SI);
      else if (Variety == "Pragma")
        Pragma.emplace_back(R, SI);
      else if (Variety == "HLSLAnnotation")
        HLSLAnnotation.emplace_back(R, SI);
    }
  }

  OS << "const llvm::Triple &T = Target.getTriple();\n";
  OS << "switch (Syntax) {\n";
  OS << "case AttributeCommonInfo::Syntax::AS_GNU:\n";
  OS << "  return llvm::StringSwitch<int>(Name)\n";
  GenerateHasAttrSpellingStringSwitch(GNU, OS, "GNU");
  OS << "case AttributeCommonInfo::Syntax::AS_Declspec:\n";
  OS << "  return llvm::StringSwitch<int>(Name)\n";
  GenerateHasAttrSpellingStringSwitch(Declspec, OS, "Declspec");
  OS << "case AttributeCommonInfo::Syntax::AS_Microsoft:\n";
  OS << "  return llvm::StringSwitch<int>(Name)\n";
  GenerateHasAttrSpellingStringSwitch(Microsoft, OS, "Microsoft");
  OS << "case AttributeCommonInfo::Syntax::AS_Pragma:\n";
  OS << "  return llvm::StringSwitch<int>(Name)\n";
  GenerateHasAttrSpellingStringSwitch(Pragma, OS, "Pragma");
  OS << "case AttributeCommonInfo::Syntax::AS_HLSLAnnotation:\n";
  OS << "  return llvm::StringSwitch<int>(Name)\n";
  GenerateHasAttrSpellingStringSwitch(HLSLAnnotation, OS, "HLSLAnnotation");
  auto fn = [&OS](const char *Spelling,
                  const std::map<
                      std::string,
                      std::vector<std::pair<const Record *, FlattenedSpelling>>>
                      &List) {
    OS << "case AttributeCommonInfo::Syntax::AS_" << Spelling << ": {\n";
    // C++11-style attributes are further split out based on the Scope.
    for (auto I = List.cbegin(), E = List.cend(); I != E; ++I) {
      if (I != List.cbegin())
        OS << " else ";
      if (I->first.empty())
        OS << "if (ScopeName == \"\") {\n";
      else
        OS << "if (ScopeName == \"" << I->first << "\") {\n";
      OS << "  return llvm::StringSwitch<int>(Name)\n";
      GenerateHasAttrSpellingStringSwitch(I->second, OS, Spelling, I->first);
      OS << "}";
    }
    OS << "\n} break;\n";
  };
  fn("CXX11", CXX);
  fn("C23", C23);
  OS << "case AttributeCommonInfo::Syntax::AS_Keyword:\n";
  OS << "case AttributeCommonInfo::Syntax::AS_ContextSensitiveKeyword:\n";
  OS << "  llvm_unreachable(\"hasAttribute not supported for keyword\");\n";
  OS << "  return 0;\n";
  OS << "case AttributeCommonInfo::Syntax::AS_Implicit:\n";
  OS << "  llvm_unreachable (\"hasAttribute not supported for "
        "AS_Implicit\");\n";
  OS << "  return 0;\n";

  OS << "}\n";
}

void EmitClangAttrSpellingListIndex(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Code to translate different attribute spellings into "
                       "internal identifiers",
                       OS, Records);

  OS << "  switch (getParsedKind()) {\n";
  OS << "    case IgnoredAttribute:\n";
  OS << "    case UnknownAttribute:\n";
  OS << "    case NoSemaHandlerAttribute:\n";
  OS << "      llvm_unreachable(\"Ignored/unknown shouldn't get here\");\n";

  ParsedAttrMap Attrs = getParsedAttrList(Records);
  for (const auto &I : Attrs) {
    const Record &R = *I.second;
    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(R);
    OS << "  case AT_" << I.first << ": {\n";
    for (unsigned I = 0; I < Spellings.size(); ++ I) {
      OS << "    if (Name == \"" << Spellings[I].name() << "\" && "
         << "getSyntax() == AttributeCommonInfo::AS_" << Spellings[I].variety()
         << " && Scope == \"" << Spellings[I].nameSpace() << "\")\n"
         << "        return " << I << ";\n";
    }

    OS << "    break;\n";
    OS << "  }\n";
  }

  OS << "  }\n";
  OS << "  return 0;\n";
}

// Emits code used by RecursiveASTVisitor to visit attributes
void EmitClangAttrASTVisitor(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Used by RecursiveASTVisitor to visit attributes.", OS,
                       Records);

  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr");

  // Write method declarations for Traverse* methods.
  // We emit this here because we only generate methods for attributes that
  // are declared as ASTNodes.
  OS << "#ifdef ATTR_VISITOR_DECLS_ONLY\n\n";
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;
    OS << "  bool Traverse"
       << R.getName() << "Attr(" << R.getName() << "Attr *A);\n";
    OS << "  bool Visit"
       << R.getName() << "Attr(" << R.getName() << "Attr *A) {\n"
       << "    return true; \n"
       << "  }\n";
  }
  OS << "\n#else // ATTR_VISITOR_DECLS_ONLY\n\n";

  // Write individual Traverse* methods for each attribute class.
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;

    OS << "template <typename Derived>\n"
       << "bool VISITORCLASS<Derived>::Traverse"
       << R.getName() << "Attr(" << R.getName() << "Attr *A) {\n"
       << "  if (!getDerived().VisitAttr(A))\n"
       << "    return false;\n"
       << "  if (!getDerived().Visit" << R.getName() << "Attr(A))\n"
       << "    return false;\n";

    std::vector<Record*> ArgRecords = R.getValueAsListOfDefs("Args");
    for (const auto *Arg : ArgRecords)
      createArgument(*Arg, R.getName())->writeASTVisitorTraversal(OS);

    if (Attr->getValueAsBit("AcceptsExprPack"))
      VariadicExprArgument("DelayedArgs", R.getName())
          .writeASTVisitorTraversal(OS);

    OS << "  return true;\n";
    OS << "}\n\n";
  }

  // Write generic Traverse routine
  OS << "template <typename Derived>\n"
     << "bool VISITORCLASS<Derived>::TraverseAttr(Attr *A) {\n"
     << "  if (!A)\n"
     << "    return true;\n"
     << "\n"
     << "  switch (A->getKind()) {\n";

  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;

    OS << "    case attr::" << R.getName() << ":\n"
       << "      return getDerived().Traverse" << R.getName() << "Attr("
       << "cast<" << R.getName() << "Attr>(A));\n";
  }
  OS << "  }\n";  // end switch
  OS << "  llvm_unreachable(\"bad attribute kind\");\n";
  OS << "}\n";  // end function
  OS << "#endif  // ATTR_VISITOR_DECLS_ONLY\n";
}

void EmitClangAttrTemplateInstantiateHelper(const std::vector<Record *> &Attrs,
                                            raw_ostream &OS,
                                            bool AppliesToDecl) {

  OS << "  switch (At->getKind()) {\n";
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;
    OS << "    case attr::" << R.getName() << ": {\n";
    bool ShouldClone = R.getValueAsBit("Clone") &&
                       (!AppliesToDecl ||
                        R.getValueAsBit("MeaningfulToClassTemplateDefinition"));

    if (!ShouldClone) {
      OS << "      return nullptr;\n";
      OS << "    }\n";
      continue;
    }

    OS << "      const auto *A = cast<"
       << R.getName() << "Attr>(At);\n";
    bool TDependent = R.getValueAsBit("TemplateDependent");

    if (!TDependent) {
      OS << "      return A->clone(C);\n";
      OS << "    }\n";
      continue;
    }

    std::vector<Record*> ArgRecords = R.getValueAsListOfDefs("Args");
    std::vector<std::unique_ptr<Argument>> Args;
    Args.reserve(ArgRecords.size());

    for (const auto *ArgRecord : ArgRecords)
      Args.emplace_back(createArgument(*ArgRecord, R.getName()));

    for (auto const &ai : Args)
      ai->writeTemplateInstantiation(OS);

    OS << "      return new (C) " << R.getName() << "Attr(C, *A";
    for (auto const &ai : Args) {
      OS << ", ";
      ai->writeTemplateInstantiationArgs(OS);
    }
    OS << ");\n"
       << "    }\n";
  }
  OS << "  } // end switch\n"
     << "  llvm_unreachable(\"Unknown attribute!\");\n"
     << "  return nullptr;\n";
}

// Emits code to instantiate dependent attributes on templates.
void EmitClangAttrTemplateInstantiate(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Template instantiation code for attributes", OS,
                       Records);

  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr");

  OS << "namespace clang {\n"
     << "namespace sema {\n\n"
     << "Attr *instantiateTemplateAttribute(const Attr *At, ASTContext &C, "
     << "Sema &S,\n"
     << "        const MultiLevelTemplateArgumentList &TemplateArgs) {\n";
  EmitClangAttrTemplateInstantiateHelper(Attrs, OS, /*AppliesToDecl*/false);
  OS << "}\n\n"
     << "Attr *instantiateTemplateAttributeForDecl(const Attr *At,\n"
     << " ASTContext &C, Sema &S,\n"
     << "        const MultiLevelTemplateArgumentList &TemplateArgs) {\n";
  EmitClangAttrTemplateInstantiateHelper(Attrs, OS, /*AppliesToDecl*/true);
  OS << "}\n\n"
     << "} // end namespace sema\n"
     << "} // end namespace clang\n";
}

// Emits the list of parsed attributes.
void EmitClangAttrParsedAttrList(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("List of all attributes that Clang recognizes", OS,
                       Records);

  OS << "#ifndef PARSED_ATTR\n";
  OS << "#define PARSED_ATTR(NAME) NAME\n";
  OS << "#endif\n\n";

  ParsedAttrMap Names = getParsedAttrList(Records);
  for (const auto &I : Names) {
    OS << "PARSED_ATTR(" << I.first << ")\n";
  }
}

static bool isArgVariadic(const Record &R, StringRef AttrName) {
  return createArgument(R, AttrName)->isVariadic();
}

static void emitArgInfo(const Record &R, raw_ostream &OS) {
  // This function will count the number of arguments specified for the
  // attribute and emit the number of required arguments followed by the
  // number of optional arguments.
  std::vector<Record *> Args = R.getValueAsListOfDefs("Args");
  unsigned ArgCount = 0, OptCount = 0, ArgMemberCount = 0;
  bool HasVariadic = false;
  for (const auto *Arg : Args) {
    // If the arg is fake, it's the user's job to supply it: general parsing
    // logic shouldn't need to know anything about it.
    if (Arg->getValueAsBit("Fake"))
      continue;
    Arg->getValueAsBit("Optional") ? ++OptCount : ++ArgCount;
    ++ArgMemberCount;
    if (!HasVariadic && isArgVariadic(*Arg, R.getName()))
      HasVariadic = true;
  }

  // If there is a variadic argument, we will set the optional argument count
  // to its largest value. Since it's currently a 4-bit number, we set it to 15.
  OS << "    /*NumArgs=*/" << ArgCount << ",\n";
  OS << "    /*OptArgs=*/" << (HasVariadic ? 15 : OptCount) << ",\n";
  OS << "    /*NumArgMembers=*/" << ArgMemberCount << ",\n";
}

static std::string GetDiagnosticSpelling(const Record &R) {
  std::string Ret = std::string(R.getValueAsString("DiagSpelling"));
  if (!Ret.empty())
    return Ret;

  // If we couldn't find the DiagSpelling in this object, we can check to see
  // if the object is one that has a base, and if it is, loop up to the Base
  // member recursively.
  if (auto Base = R.getValueAsOptionalDef(BaseFieldName))
    return GetDiagnosticSpelling(*Base);

  return "";
}

static std::string CalculateDiagnostic(const Record &S) {
  // If the SubjectList object has a custom diagnostic associated with it,
  // return that directly.
  const StringRef CustomDiag = S.getValueAsString("CustomDiag");
  if (!CustomDiag.empty())
    return ("\"" + Twine(CustomDiag) + "\"").str();

  std::vector<std::string> DiagList;
  std::vector<Record *> Subjects = S.getValueAsListOfDefs("Subjects");
  for (const auto *Subject : Subjects) {
    const Record &R = *Subject;
    // Get the diagnostic text from the Decl or Stmt node given.
    std::string V = GetDiagnosticSpelling(R);
    if (V.empty()) {
      PrintError(R.getLoc(),
                 "Could not determine diagnostic spelling for the node: " +
                     R.getName() + "; please add one to DeclNodes.td");
    } else {
      // The node may contain a list of elements itself, so split the elements
      // by a comma, and trim any whitespace.
      SmallVector<StringRef, 2> Frags;
      llvm::SplitString(V, Frags, ",");
      for (auto Str : Frags) {
        DiagList.push_back(std::string(Str.trim()));
      }
    }
  }

  if (DiagList.empty()) {
    PrintFatalError(S.getLoc(),
                    "Could not deduce diagnostic argument for Attr subjects");
    return "";
  }

  // FIXME: this is not particularly good for localization purposes and ideally
  // should be part of the diagnostics engine itself with some sort of list
  // specifier.

  // A single member of the list can be returned directly.
  if (DiagList.size() == 1)
    return '"' + DiagList.front() + '"';

  if (DiagList.size() == 2)
    return '"' + DiagList[0] + " and " + DiagList[1] + '"';

  // If there are more than two in the list, we serialize the first N - 1
  // elements with a comma. This leaves the string in the state: foo, bar,
  // baz (but misses quux). We can then add ", and " for the last element
  // manually.
  std::string Diag = llvm::join(DiagList.begin(), DiagList.end() - 1, ", ");
  return '"' + Diag + ", and " + *(DiagList.end() - 1) + '"';
}

static std::string GetSubjectWithSuffix(const Record *R) {
  const std::string &B = std::string(R->getName());
  if (B == "DeclBase")
    return "Decl";
  return B + "Decl";
}

static std::string functionNameForCustomAppertainsTo(const Record &Subject) {
  return "is" + Subject.getName().str();
}

static void GenerateCustomAppertainsTo(const Record &Subject, raw_ostream &OS) {
  std::string FnName = functionNameForCustomAppertainsTo(Subject);

  // If this code has already been generated, we don't need to do anything.
  static std::set<std::string> CustomSubjectSet;
  auto I = CustomSubjectSet.find(FnName);
  if (I != CustomSubjectSet.end())
    return;

  // This only works with non-root Decls.
  Record *Base = Subject.getValueAsDef(BaseFieldName);

  // Not currently support custom subjects within custom subjects.
  if (Base->isSubClassOf("SubsetSubject")) {
    PrintFatalError(Subject.getLoc(),
                    "SubsetSubjects within SubsetSubjects is not supported");
    return;
  }

  OS << "static bool " << FnName << "(const Decl *D) {\n";
  OS << "  if (const auto *S = dyn_cast<";
  OS << GetSubjectWithSuffix(Base);
  OS << ">(D))\n";
  OS << "    return " << Subject.getValueAsString("CheckCode") << ";\n";
  OS << "  return false;\n";
  OS << "}\n\n";

  CustomSubjectSet.insert(FnName);
}

static void GenerateAppertainsTo(const Record &Attr, raw_ostream &OS) {
  // If the attribute does not contain a Subjects definition, then use the
  // default appertainsTo logic.
  if (Attr.isValueUnset("Subjects"))
    return;

  const Record *SubjectObj = Attr.getValueAsDef("Subjects");
  std::vector<Record *> Subjects = SubjectObj->getValueAsListOfDefs("Subjects");

  // If the list of subjects is empty, it is assumed that the attribute
  // appertains to everything.
  if (Subjects.empty())
    return;

  bool Warn = SubjectObj->getValueAsDef("Diag")->getValueAsBit("Warn");

  // Split the subjects into declaration subjects and statement subjects.
  // FIXME: subset subjects are added to the declaration list until there are
  // enough statement attributes with custom subject needs to warrant
  // the implementation effort.
  std::vector<Record *> DeclSubjects, StmtSubjects;
  llvm::copy_if(
      Subjects, std::back_inserter(DeclSubjects), [](const Record *R) {
        return R->isSubClassOf("SubsetSubject") || !R->isSubClassOf("StmtNode");
      });
  llvm::copy_if(Subjects, std::back_inserter(StmtSubjects),
                [](const Record *R) { return R->isSubClassOf("StmtNode"); });

  // We should have sorted all of the subjects into two lists.
  // FIXME: this assertion will be wrong if we ever add type attribute subjects.
  assert(DeclSubjects.size() + StmtSubjects.size() == Subjects.size());

  if (DeclSubjects.empty()) {
    // If there are no decl subjects but there are stmt subjects, diagnose
    // trying to apply a statement attribute to a declaration.
    if (!StmtSubjects.empty()) {
      OS << "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &AL, ";
      OS << "const Decl *D) const override {\n";
      OS << "  S.Diag(AL.getLoc(), diag::err_attribute_invalid_on_decl)\n";
      OS << "    << AL << AL.isRegularKeywordAttribute() << "
            "D->getLocation();\n";
      OS << "  return false;\n";
      OS << "}\n\n";
    }
  } else {
    // Otherwise, generate an appertainsTo check specific to this attribute
    // which checks all of the given subjects against the Decl passed in.
    OS << "bool diagAppertainsToDecl(Sema &S, ";
    OS << "const ParsedAttr &Attr, const Decl *D) const override {\n";
    OS << "  if (";
    for (auto I = DeclSubjects.begin(), E = DeclSubjects.end(); I != E; ++I) {
      // If the subject has custom code associated with it, use the generated
      // function for it. The function cannot be inlined into this check (yet)
      // because it requires the subject to be of a specific type, and were that
      // information inlined here, it would not support an attribute with
      // multiple custom subjects.
      if ((*I)->isSubClassOf("SubsetSubject"))
        OS << "!" << functionNameForCustomAppertainsTo(**I) << "(D)";
      else
        OS << "!isa<" << GetSubjectWithSuffix(*I) << ">(D)";

      if (I + 1 != E)
        OS << " && ";
    }
    OS << ") {\n";
    OS << "    S.Diag(Attr.getLoc(), diag::";
    OS << (Warn ? "warn_attribute_wrong_decl_type_str"
                : "err_attribute_wrong_decl_type_str");
    OS << ")\n";
    OS << "      << Attr << Attr.isRegularKeywordAttribute() << ";
    OS << CalculateDiagnostic(*SubjectObj) << ";\n";
    OS << "    return false;\n";
    OS << "  }\n";
    OS << "  return true;\n";
    OS << "}\n\n";
  }

  if (StmtSubjects.empty()) {
    // If there are no stmt subjects but there are decl subjects, diagnose
    // trying to apply a declaration attribute to a statement.
    if (!DeclSubjects.empty()) {
      OS << "bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL, ";
      OS << "const Stmt *St) const override {\n";
      OS << "  S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)\n";
      OS << "    << AL << AL.isRegularKeywordAttribute() << "
            "St->getBeginLoc();\n";
      OS << "  return false;\n";
      OS << "}\n\n";
    }
  } else {
    // Now, do the same for statements.
    OS << "bool diagAppertainsToStmt(Sema &S, ";
    OS << "const ParsedAttr &Attr, const Stmt *St) const override {\n";
    OS << "  if (";
    for (auto I = StmtSubjects.begin(), E = StmtSubjects.end(); I != E; ++I) {
      OS << "!isa<" << (*I)->getName() << ">(St)";
      if (I + 1 != E)
        OS << " && ";
    }
    OS << ") {\n";
    OS << "    S.Diag(Attr.getLoc(), diag::";
    OS << (Warn ? "warn_attribute_wrong_decl_type_str"
                : "err_attribute_wrong_decl_type_str");
    OS << ")\n";
    OS << "      << Attr << Attr.isRegularKeywordAttribute() << ";
    OS << CalculateDiagnostic(*SubjectObj) << ";\n";
    OS << "    return false;\n";
    OS << "  }\n";
    OS << "  return true;\n";
    OS << "}\n\n";
  }
}

// Generates the mutual exclusion checks. The checks for parsed attributes are
// written into OS and the checks for merging declaration attributes are
// written into MergeOS.
static void GenerateMutualExclusionsChecks(const Record &Attr,
                                           const RecordKeeper &Records,
                                           raw_ostream &OS,
                                           raw_ostream &MergeDeclOS,
                                           raw_ostream &MergeStmtOS) {
  // Find all of the definitions that inherit from MutualExclusions and include
  // the given attribute in the list of exclusions to generate the
  // diagMutualExclusion() check.
  std::vector<Record *> ExclusionsList =
      Records.getAllDerivedDefinitions("MutualExclusions");

  // We don't do any of this magic for type attributes yet.
  if (Attr.isSubClassOf("TypeAttr"))
    return;

  // This means the attribute is either a statement attribute, a decl
  // attribute, or both; find out which.
  bool CurAttrIsStmtAttr =
      Attr.isSubClassOf("StmtAttr") || Attr.isSubClassOf("DeclOrStmtAttr");
  bool CurAttrIsDeclAttr =
      !CurAttrIsStmtAttr || Attr.isSubClassOf("DeclOrStmtAttr");

  std::vector<std::string> DeclAttrs, StmtAttrs;

  for (const Record *Exclusion : ExclusionsList) {
    std::vector<Record *> MutuallyExclusiveAttrs =
        Exclusion->getValueAsListOfDefs("Exclusions");
    auto IsCurAttr = [Attr](const Record *R) {
      return R->getName() == Attr.getName();
    };
    if (llvm::any_of(MutuallyExclusiveAttrs, IsCurAttr)) {
      // This list of exclusions includes the attribute we're looking for, so
      // add the exclusive attributes to the proper list for checking.
      for (const Record *AttrToExclude : MutuallyExclusiveAttrs) {
        if (IsCurAttr(AttrToExclude))
          continue;

        if (CurAttrIsStmtAttr)
          StmtAttrs.push_back((AttrToExclude->getName() + "Attr").str());
        if (CurAttrIsDeclAttr)
          DeclAttrs.push_back((AttrToExclude->getName() + "Attr").str());
      }
    }
  }

  // If there are any decl or stmt attributes, silence -Woverloaded-virtual
  // warnings for them both.
  if (!DeclAttrs.empty() || !StmtAttrs.empty())
    OS << "  using ParsedAttrInfo::diagMutualExclusion;\n\n";

  // If we discovered any decl or stmt attributes to test for, generate the
  // predicates for them now.
  if (!DeclAttrs.empty()) {
    // Generate the ParsedAttrInfo subclass logic for declarations.
    OS << "  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL, "
       << "const Decl *D) const override {\n";
    for (const std::string &A : DeclAttrs) {
      OS << "    if (const auto *A = D->getAttr<" << A << ">()) {\n";
      OS << "      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)"
         << " << AL << A << (AL.isRegularKeywordAttribute() ||"
         << " A->isRegularKeywordAttribute());\n";
      OS << "      S.Diag(A->getLocation(), diag::note_conflicting_attribute);";
      OS << "      \nreturn false;\n";
      OS << "    }\n";
    }
    OS << "    return true;\n";
    OS << "  }\n\n";

    // Also generate the declaration attribute merging logic if the current
    // attribute is one that can be inheritted on a declaration. It is assumed
    // this code will be executed in the context of a function with parameters:
    // Sema &S, Decl *D, Attr *A and that returns a bool (false on diagnostic,
    // true on success).
    if (Attr.isSubClassOf("InheritableAttr")) {
      MergeDeclOS << "  if (const auto *Second = dyn_cast<"
                  << (Attr.getName() + "Attr").str() << ">(A)) {\n";
      for (const std::string &A : DeclAttrs) {
        MergeDeclOS << "    if (const auto *First = D->getAttr<" << A
                    << ">()) {\n";
        MergeDeclOS << "      S.Diag(First->getLocation(), "
                    << "diag::err_attributes_are_not_compatible) << First << "
                    << "Second << (First->isRegularKeywordAttribute() || "
                    << "Second->isRegularKeywordAttribute());\n";
        MergeDeclOS << "      S.Diag(Second->getLocation(), "
                    << "diag::note_conflicting_attribute);\n";
        MergeDeclOS << "      return false;\n";
        MergeDeclOS << "    }\n";
      }
      MergeDeclOS << "    return true;\n";
      MergeDeclOS << "  }\n";
    }
  }

  // Statement attributes are a bit different from declarations. With
  // declarations, each attribute is added to the declaration as it is
  // processed, and so you can look on the Decl * itself to see if there is a
  // conflicting attribute. Statement attributes are processed as a group
  // because AttributedStmt needs to tail-allocate all of the attribute nodes
  // at once. This means we cannot check whether the statement already contains
  // an attribute to check for the conflict. Instead, we need to check whether
  // the given list of semantic attributes contain any conflicts. It is assumed
  // this code will be executed in the context of a function with parameters:
  // Sema &S, const SmallVectorImpl<const Attr *> &C. The code will be within a
  // loop which loops over the container C with a loop variable named A to
  // represent the current attribute to check for conflicts.
  //
  // FIXME: it would be nice not to walk over the list of potential attributes
  // to apply to the statement more than once, but statements typically don't
  // have long lists of attributes on them, so re-walking the list should not
  // be an expensive operation.
  if (!StmtAttrs.empty()) {
    MergeStmtOS << "    if (const auto *Second = dyn_cast<"
                << (Attr.getName() + "Attr").str() << ">(A)) {\n";
    MergeStmtOS << "      auto Iter = llvm::find_if(C, [](const Attr *Check) "
                << "{ return isa<";
    interleave(
        StmtAttrs, [&](const std::string &Name) { MergeStmtOS << Name; },
        [&] { MergeStmtOS << ", "; });
    MergeStmtOS << ">(Check); });\n";
    MergeStmtOS << "      if (Iter != C.end()) {\n";
    MergeStmtOS << "        S.Diag((*Iter)->getLocation(), "
                << "diag::err_attributes_are_not_compatible) << *Iter << "
                << "Second << ((*Iter)->isRegularKeywordAttribute() || "
                << "Second->isRegularKeywordAttribute());\n";
    MergeStmtOS << "        S.Diag(Second->getLocation(), "
                << "diag::note_conflicting_attribute);\n";
    MergeStmtOS << "        return false;\n";
    MergeStmtOS << "      }\n";
    MergeStmtOS << "    }\n";
  }
}

static void
emitAttributeMatchRules(PragmaClangAttributeSupport &PragmaAttributeSupport,
                        raw_ostream &OS) {
  OS << "static bool checkAttributeMatchRuleAppliesTo(const Decl *D, "
     << AttributeSubjectMatchRule::EnumName << " rule) {\n";
  OS << "  switch (rule) {\n";
  for (const auto &Rule : PragmaAttributeSupport.Rules) {
    if (Rule.isAbstractRule()) {
      OS << "  case " << Rule.getEnumValue() << ":\n";
      OS << "    assert(false && \"Abstract matcher rule isn't allowed\");\n";
      OS << "    return false;\n";
      continue;
    }
    std::vector<Record *> Subjects = Rule.getSubjects();
    assert(!Subjects.empty() && "Missing subjects");
    OS << "  case " << Rule.getEnumValue() << ":\n";
    OS << "    return ";
    for (auto I = Subjects.begin(), E = Subjects.end(); I != E; ++I) {
      // If the subject has custom code associated with it, use the function
      // that was generated for GenerateAppertainsTo to check if the declaration
      // is valid.
      if ((*I)->isSubClassOf("SubsetSubject"))
        OS << functionNameForCustomAppertainsTo(**I) << "(D)";
      else
        OS << "isa<" << GetSubjectWithSuffix(*I) << ">(D)";

      if (I + 1 != E)
        OS << " || ";
    }
    OS << ";\n";
  }
  OS << "  }\n";
  OS << "  llvm_unreachable(\"Invalid match rule\");\nreturn false;\n";
  OS << "}\n\n";
}

static void GenerateLangOptRequirements(const Record &R,
                                        raw_ostream &OS) {
  // If the attribute has an empty or unset list of language requirements,
  // use the default handler.
  std::vector<Record *> LangOpts = R.getValueAsListOfDefs("LangOpts");
  if (LangOpts.empty())
    return;

  OS << "bool acceptsLangOpts(const LangOptions &LangOpts) const override {\n";
  OS << "  return " << GenerateTestExpression(LangOpts) << ";\n";
  OS << "}\n\n";
}

static void GenerateTargetRequirements(const Record &Attr,
                                       const ParsedAttrMap &Dupes,
                                       raw_ostream &OS) {
  // If the attribute is not a target specific attribute, use the default
  // target handler.
  if (!Attr.isSubClassOf("TargetSpecificAttr"))
    return;

  // Get the list of architectures to be tested for.
  const Record *R = Attr.getValueAsDef("Target");
  std::vector<StringRef> Arches = R->getValueAsListOfStrings("Arches");

  // If there are other attributes which share the same parsed attribute kind,
  // such as target-specific attributes with a shared spelling, collapse the
  // duplicate architectures. This is required because a shared target-specific
  // attribute has only one ParsedAttr::Kind enumeration value, but it
  // applies to multiple target architectures. In order for the attribute to be
  // considered valid, all of its architectures need to be included.
  if (!Attr.isValueUnset("ParseKind")) {
    const StringRef APK = Attr.getValueAsString("ParseKind");
    for (const auto &I : Dupes) {
      if (I.first == APK) {
        std::vector<StringRef> DA =
            I.second->getValueAsDef("Target")->getValueAsListOfStrings(
                "Arches");
        Arches.insert(Arches.end(), DA.begin(), DA.end());
      }
    }
  }

  std::string FnName = "isTarget";
  std::string Test;
  bool UsesT = GenerateTargetSpecificAttrChecks(R, Arches, Test, &FnName);

  OS << "bool existsInTarget(const TargetInfo &Target) const override {\n";
  if (UsesT)
    OS << "  const llvm::Triple &T = Target.getTriple(); (void)T;\n";
  OS << "  return " << Test << ";\n";
  OS << "}\n\n";
}

static void
GenerateSpellingTargetRequirements(const Record &Attr,
                                   const std::vector<Record *> &TargetSpellings,
                                   raw_ostream &OS) {
  // If there are no target specific spellings, use the default target handler.
  if (TargetSpellings.empty())
    return;

  std::string Test;
  bool UsesT = false;
  const std::vector<FlattenedSpelling> SpellingList =
      GetFlattenedSpellings(Attr);
  for (unsigned TargetIndex = 0; TargetIndex < TargetSpellings.size();
       ++TargetIndex) {
    const auto &TargetSpelling = TargetSpellings[TargetIndex];
    std::vector<FlattenedSpelling> Spellings =
        GetFlattenedSpellings(*TargetSpelling);

    Test += "((SpellingListIndex == ";
    for (unsigned Index = 0; Index < Spellings.size(); ++Index) {
      Test +=
          llvm::itostr(getSpellingListIndex(SpellingList, Spellings[Index]));
      if (Index != Spellings.size() - 1)
        Test += " ||\n    SpellingListIndex == ";
      else
        Test += ") && ";
    }

    const Record *Target = TargetSpelling->getValueAsDef("Target");
    std::vector<StringRef> Arches = Target->getValueAsListOfStrings("Arches");
    std::string FnName = "isTargetSpelling";
    UsesT |= GenerateTargetSpecificAttrChecks(Target, Arches, Test, &FnName);
    Test += ")";
    if (TargetIndex != TargetSpellings.size() - 1)
      Test += " || ";
  }

  OS << "bool spellingExistsInTarget(const TargetInfo &Target,\n";
  OS << "                            const unsigned SpellingListIndex) const "
        "override {\n";
  if (UsesT)
    OS << "  const llvm::Triple &T = Target.getTriple(); (void)T;\n";
  OS << "  return " << Test << ";\n", OS << "}\n\n";
}

static void GenerateSpellingIndexToSemanticSpelling(const Record &Attr,
                                                    raw_ostream &OS) {
  // If the attribute does not have a semantic form, we can bail out early.
  if (!Attr.getValueAsBit("ASTNode"))
    return;

  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(Attr);

  // If there are zero or one spellings, or all of the spellings share the same
  // name, we can also bail out early.
  if (Spellings.size() <= 1 || SpellingNamesAreCommon(Spellings))
    return;

  // Generate the enumeration we will use for the mapping.
  SemanticSpellingMap SemanticToSyntacticMap;
  std::string Enum = CreateSemanticSpellings(Spellings, SemanticToSyntacticMap);
  std::string Name = Attr.getName().str() + "AttrSpellingMap";

  OS << "unsigned spellingIndexToSemanticSpelling(";
  OS << "const ParsedAttr &Attr) const override {\n";
  OS << Enum;
  OS << "  unsigned Idx = Attr.getAttributeSpellingListIndex();\n";
  WriteSemanticSpellingSwitch("Idx", SemanticToSyntacticMap, OS);
  OS << "}\n\n";
}

static void GenerateHandleDeclAttribute(const Record &Attr, raw_ostream &OS) {
  // Only generate if Attr can be handled simply.
  if (!Attr.getValueAsBit("SimpleHandler"))
    return;

  // Generate a function which just converts from ParsedAttr to the Attr type.
  OS << "AttrHandling handleDeclAttribute(Sema &S, Decl *D,";
  OS << "const ParsedAttr &Attr) const override {\n";
  OS << "  D->addAttr(::new (S.Context) " << Attr.getName();
  OS << "Attr(S.Context, Attr));\n";
  OS << "  return AttributeApplied;\n";
  OS << "}\n\n";
}

static bool isParamExpr(const Record *Arg) {
  return !Arg->getSuperClasses().empty() &&
         llvm::StringSwitch<bool>(
             Arg->getSuperClasses().back().first->getName())
             .Case("ExprArgument", true)
             .Case("VariadicExprArgument", true)
             .Default(false);
}

void GenerateIsParamExpr(const Record &Attr, raw_ostream &OS) {
  OS << "bool isParamExpr(size_t N) const override {\n";
  OS << "  return ";
  auto Args = Attr.getValueAsListOfDefs("Args");
  for (size_t I = 0; I < Args.size(); ++I)
    if (isParamExpr(Args[I]))
      OS << "(N == " << I << ") || ";
  OS << "false;\n";
  OS << "}\n\n";
}

void GenerateHandleAttrWithDelayedArgs(RecordKeeper &Records, raw_ostream &OS) {
  OS << "static void handleAttrWithDelayedArgs(Sema &S, Decl *D, ";
  OS << "const ParsedAttr &Attr) {\n";
  OS << "  SmallVector<Expr *, 4> ArgExprs;\n";
  OS << "  ArgExprs.reserve(Attr.getNumArgs());\n";
  OS << "  for (unsigned I = 0; I < Attr.getNumArgs(); ++I) {\n";
  OS << "    assert(!Attr.isArgIdent(I));\n";
  OS << "    ArgExprs.push_back(Attr.getArgAsExpr(I));\n";
  OS << "  }\n";
  OS << "  clang::Attr *CreatedAttr = nullptr;\n";
  OS << "  switch (Attr.getKind()) {\n";
  OS << "  default:\n";
  OS << "    llvm_unreachable(\"Attribute cannot hold delayed arguments.\");\n";
  ParsedAttrMap Attrs = getParsedAttrList(Records);
  for (const auto &I : Attrs) {
    const Record &R = *I.second;
    if (!R.getValueAsBit("AcceptsExprPack"))
      continue;
    OS << "  case ParsedAttr::AT_" << I.first << ": {\n";
    OS << "    CreatedAttr = " << R.getName() << "Attr::CreateWithDelayedArgs";
    OS << "(S.Context, ArgExprs.data(), ArgExprs.size(), Attr);\n";
    OS << "    break;\n";
    OS << "  }\n";
  }
  OS << "  }\n";
  OS << "  D->addAttr(CreatedAttr);\n";
  OS << "}\n\n";
}

static bool IsKnownToGCC(const Record &Attr) {
  // Look at the spellings for this subject; if there are any spellings which
  // claim to be known to GCC, the attribute is known to GCC.
  return llvm::any_of(
      GetFlattenedSpellings(Attr),
      [](const FlattenedSpelling &S) { return S.knownToGCC(); });
}

/// Emits the parsed attribute helpers
void EmitClangAttrParsedAttrImpl(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Parsed attribute helpers", OS, Records);

  OS << "#if !defined(WANT_DECL_MERGE_LOGIC) && "
     << "!defined(WANT_STMT_MERGE_LOGIC)\n";
  PragmaClangAttributeSupport &PragmaAttributeSupport =
      getPragmaAttributeSupport(Records);

  // Get the list of parsed attributes, and accept the optional list of
  // duplicates due to the ParseKind.
  ParsedAttrMap Dupes;
  ParsedAttrMap Attrs = getParsedAttrList(Records, &Dupes);

  // Generate all of the custom appertainsTo functions that the attributes
  // will be using.
  for (const auto &I : Attrs) {
    const Record &Attr = *I.second;
    if (Attr.isValueUnset("Subjects"))
      continue;
    const Record *SubjectObj = Attr.getValueAsDef("Subjects");
    for (auto Subject : SubjectObj->getValueAsListOfDefs("Subjects"))
      if (Subject->isSubClassOf("SubsetSubject"))
        GenerateCustomAppertainsTo(*Subject, OS);
  }

  // This stream is used to collect all of the declaration attribute merging
  // logic for performing mutual exclusion checks. This gets emitted at the
  // end of the file in a helper function of its own.
  std::string DeclMergeChecks, StmtMergeChecks;
  raw_string_ostream MergeDeclOS(DeclMergeChecks), MergeStmtOS(StmtMergeChecks);

  // Generate a ParsedAttrInfo struct for each of the attributes.
  for (auto I = Attrs.begin(), E = Attrs.end(); I != E; ++I) {
    // TODO: If the attribute's kind appears in the list of duplicates, that is
    // because it is a target-specific attribute that appears multiple times.
    // It would be beneficial to test whether the duplicates are "similar
    // enough" to each other to not cause problems. For instance, check that
    // the spellings are identical, and custom parsing rules match, etc.

    // We need to generate struct instances based off ParsedAttrInfo from
    // ParsedAttr.cpp.
    const std::string &AttrName = I->first;
    const Record &Attr = *I->second;
    auto Spellings = GetFlattenedSpellings(Attr);
    if (!Spellings.empty()) {
      OS << "static constexpr ParsedAttrInfo::Spelling " << I->first
         << "Spellings[] = {\n";
      for (const auto &S : Spellings) {
        const std::string &RawSpelling = S.name();
        std::string Spelling;
        if (!S.nameSpace().empty())
          Spelling += S.nameSpace() + "::";
        if (S.variety() == "GNU")
          Spelling += NormalizeGNUAttrSpelling(RawSpelling);
        else
          Spelling += RawSpelling;
        OS << "  {AttributeCommonInfo::AS_" << S.variety();
        OS << ", \"" << Spelling << "\"},\n";
      }
      OS << "};\n";
    }

    std::vector<std::string> ArgNames;
    for (const auto &Arg : Attr.getValueAsListOfDefs("Args")) {
      bool UnusedUnset;
      if (Arg->getValueAsBitOrUnset("Fake", UnusedUnset))
        continue;
      ArgNames.push_back(Arg->getValueAsString("Name").str());
      for (const auto &Class : Arg->getSuperClasses()) {
        if (Class.first->getName().starts_with("Variadic")) {
          ArgNames.back().append("...");
          break;
        }
      }
    }
    if (!ArgNames.empty()) {
      OS << "static constexpr const char *" << I->first << "ArgNames[] = {\n";
      for (const auto &N : ArgNames)
        OS << '"' << N << "\",";
      OS << "};\n";
    }

    OS << "struct ParsedAttrInfo" << I->first
       << " final : public ParsedAttrInfo {\n";
    OS << "  constexpr ParsedAttrInfo" << I->first << "() : ParsedAttrInfo(\n";
    OS << "    /*AttrKind=*/ParsedAttr::AT_" << AttrName << ",\n";
    emitArgInfo(Attr, OS);
    OS << "    /*HasCustomParsing=*/";
    OS << Attr.getValueAsBit("HasCustomParsing") << ",\n";
    OS << "    /*AcceptsExprPack=*/";
    OS << Attr.getValueAsBit("AcceptsExprPack") << ",\n";
    OS << "    /*IsTargetSpecific=*/";
    OS << Attr.isSubClassOf("TargetSpecificAttr") << ",\n";
    OS << "    /*IsType=*/";
    OS << (Attr.isSubClassOf("TypeAttr") || Attr.isSubClassOf("DeclOrTypeAttr"))
       << ",\n";
    OS << "    /*IsStmt=*/";
    OS << (Attr.isSubClassOf("StmtAttr") || Attr.isSubClassOf("DeclOrStmtAttr"))
       << ",\n";
    OS << "    /*IsKnownToGCC=*/";
    OS << IsKnownToGCC(Attr) << ",\n";
    OS << "    /*IsSupportedByPragmaAttribute=*/";
    OS << PragmaAttributeSupport.isAttributedSupported(*I->second) << ",\n";
    if (!Spellings.empty())
      OS << "    /*Spellings=*/" << I->first << "Spellings,\n";
    else
      OS << "    /*Spellings=*/{},\n";
    if (!ArgNames.empty())
      OS << "    /*ArgNames=*/" << I->first << "ArgNames";
    else
      OS << "    /*ArgNames=*/{}";
    OS << ") {}\n";
    GenerateAppertainsTo(Attr, OS);
    GenerateMutualExclusionsChecks(Attr, Records, OS, MergeDeclOS, MergeStmtOS);
    GenerateLangOptRequirements(Attr, OS);
    GenerateTargetRequirements(Attr, Dupes, OS);
    GenerateSpellingTargetRequirements(
        Attr, Attr.getValueAsListOfDefs("TargetSpecificSpellings"), OS);
    GenerateSpellingIndexToSemanticSpelling(Attr, OS);
    PragmaAttributeSupport.generateStrictConformsTo(*I->second, OS);
    GenerateHandleDeclAttribute(Attr, OS);
    GenerateIsParamExpr(Attr, OS);
    OS << "static const ParsedAttrInfo" << I->first << " Instance;\n";
    OS << "};\n";
    OS << "const ParsedAttrInfo" << I->first << " ParsedAttrInfo" << I->first
       << "::Instance;\n";
  }

  OS << "static const ParsedAttrInfo *AttrInfoMap[] = {\n";
  for (auto I = Attrs.begin(), E = Attrs.end(); I != E; ++I) {
    OS << "&ParsedAttrInfo" << I->first << "::Instance,\n";
  }
  OS << "};\n\n";

  // Generate function for handling attributes with delayed arguments
  GenerateHandleAttrWithDelayedArgs(Records, OS);

  // Generate the attribute match rules.
  emitAttributeMatchRules(PragmaAttributeSupport, OS);

  OS << "#elif defined(WANT_DECL_MERGE_LOGIC)\n\n";

  // Write out the declaration merging check logic.
  OS << "static bool DiagnoseMutualExclusions(Sema &S, const NamedDecl *D, "
     << "const Attr *A) {\n";
  OS << DeclMergeChecks;
  OS << "  return true;\n";
  OS << "}\n\n";

  OS << "#elif defined(WANT_STMT_MERGE_LOGIC)\n\n";

  // Write out the statement merging check logic.
  OS << "static bool DiagnoseMutualExclusions(Sema &S, "
     << "const SmallVectorImpl<const Attr *> &C) {\n";
  OS << "  for (const Attr *A : C) {\n";
  OS << StmtMergeChecks;
  OS << "  }\n";
  OS << "  return true;\n";
  OS << "}\n\n";

  OS << "#endif\n";
}

// Emits the kind list of parsed attributes
void EmitClangAttrParsedAttrKinds(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute name matcher", OS, Records);

  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  std::vector<StringMatcher::StringPair> GNU, Declspec, Microsoft, CXX11,
      Keywords, Pragma, C23, HLSLAnnotation;
  std::set<std::string> Seen;
  for (const auto *A : Attrs) {
    const Record &Attr = *A;

    bool SemaHandler = Attr.getValueAsBit("SemaHandler");
    bool Ignored = Attr.getValueAsBit("Ignored");
    if (SemaHandler || Ignored) {
      // Attribute spellings can be shared between target-specific attributes,
      // and can be shared between syntaxes for the same attribute. For
      // instance, an attribute can be spelled GNU<"interrupt"> for an ARM-
      // specific attribute, or MSP430-specific attribute. Additionally, an
      // attribute can be spelled GNU<"dllexport"> and Declspec<"dllexport">
      // for the same semantic attribute. Ultimately, we need to map each of
      // these to a single AttributeCommonInfo::Kind value, but the
      // StringMatcher class cannot handle duplicate match strings. So we
      // generate a list of string to match based on the syntax, and emit
      // multiple string matchers depending on the syntax used.
      std::string AttrName;
      if (Attr.isSubClassOf("TargetSpecificAttr") &&
          !Attr.isValueUnset("ParseKind")) {
        AttrName = std::string(Attr.getValueAsString("ParseKind"));
        if (!Seen.insert(AttrName).second)
          continue;
      } else
        AttrName = NormalizeAttrName(StringRef(Attr.getName())).str();

      std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(Attr);
      for (const auto &S : Spellings) {
        const std::string &RawSpelling = S.name();
        std::vector<StringMatcher::StringPair> *Matches = nullptr;
        std::string Spelling;
        const std::string &Variety = S.variety();
        if (Variety == "CXX11") {
          Matches = &CXX11;
          if (!S.nameSpace().empty())
            Spelling += S.nameSpace() + "::";
        } else if (Variety == "C23") {
          Matches = &C23;
          if (!S.nameSpace().empty())
            Spelling += S.nameSpace() + "::";
        } else if (Variety == "GNU")
          Matches = &GNU;
        else if (Variety == "Declspec")
          Matches = &Declspec;
        else if (Variety == "Microsoft")
          Matches = &Microsoft;
        else if (Variety == "Keyword")
          Matches = &Keywords;
        else if (Variety == "Pragma")
          Matches = &Pragma;
        else if (Variety == "HLSLAnnotation")
          Matches = &HLSLAnnotation;

        assert(Matches && "Unsupported spelling variety found");

        if (Variety == "GNU")
          Spelling += NormalizeGNUAttrSpelling(RawSpelling);
        else
          Spelling += RawSpelling;

        if (SemaHandler)
          Matches->push_back(StringMatcher::StringPair(
              Spelling, "return AttributeCommonInfo::AT_" + AttrName + ";"));
        else
          Matches->push_back(StringMatcher::StringPair(
              Spelling, "return AttributeCommonInfo::IgnoredAttribute;"));
      }
    }
  }

  OS << "static AttributeCommonInfo::Kind getAttrKind(StringRef Name, ";
  OS << "AttributeCommonInfo::Syntax Syntax) {\n";
  OS << "  if (AttributeCommonInfo::AS_GNU == Syntax) {\n";
  StringMatcher("Name", GNU, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_Declspec == Syntax) {\n";
  StringMatcher("Name", Declspec, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_Microsoft == Syntax) {\n";
  StringMatcher("Name", Microsoft, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_CXX11 == Syntax) {\n";
  StringMatcher("Name", CXX11, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_C23 == Syntax) {\n";
  StringMatcher("Name", C23, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_Keyword == Syntax || ";
  OS << "AttributeCommonInfo::AS_ContextSensitiveKeyword == Syntax) {\n";
  StringMatcher("Name", Keywords, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_Pragma == Syntax) {\n";
  StringMatcher("Name", Pragma, OS).Emit();
  OS << "  } else if (AttributeCommonInfo::AS_HLSLAnnotation == Syntax) {\n";
  StringMatcher("Name", HLSLAnnotation, OS).Emit();
  OS << "  }\n";
  OS << "  return AttributeCommonInfo::UnknownAttribute;\n"
     << "}\n";
}

// Emits the code to dump an attribute.
void EmitClangAttrTextNodeDump(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute text node dumper", OS, Records);

  std::vector<Record*> Attrs = Records.getAllDerivedDefinitions("Attr"), Args;
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;

    // If the attribute has a semantically-meaningful name (which is determined
    // by whether there is a Spelling enumeration for it), then write out the
    // spelling used for the attribute.

    std::string FunctionContent;
    llvm::raw_string_ostream SS(FunctionContent);

    std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(R);
    if (Spellings.size() > 1 && !SpellingNamesAreCommon(Spellings))
      SS << "    OS << \" \" << A->getSpelling();\n";

    Args = R.getValueAsListOfDefs("Args");
    for (const auto *Arg : Args)
      createArgument(*Arg, R.getName())->writeDump(SS);

    if (Attr->getValueAsBit("AcceptsExprPack"))
      VariadicExprArgument("DelayedArgs", R.getName()).writeDump(OS);

    if (SS.tell()) {
      OS << "  void Visit" << R.getName() << "Attr(const " << R.getName()
         << "Attr *A) {\n";
      if (!Args.empty())
        OS << "    const auto *SA = cast<" << R.getName()
           << "Attr>(A); (void)SA;\n";
      OS << FunctionContent;
      OS << "  }\n";
    }
  }
}

void EmitClangAttrNodeTraverse(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Attribute text node traverser", OS, Records);

  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr"), Args;
  for (const auto *Attr : Attrs) {
    const Record &R = *Attr;
    if (!R.getValueAsBit("ASTNode"))
      continue;

    std::string FunctionContent;
    llvm::raw_string_ostream SS(FunctionContent);

    Args = R.getValueAsListOfDefs("Args");
    for (const auto *Arg : Args)
      createArgument(*Arg, R.getName())->writeDumpChildren(SS);
    if (Attr->getValueAsBit("AcceptsExprPack"))
      VariadicExprArgument("DelayedArgs", R.getName()).writeDumpChildren(SS);
    if (SS.tell()) {
      OS << "  void Visit" << R.getName() << "Attr(const " << R.getName()
         << "Attr *A) {\n";
      if (!Args.empty())
        OS << "    const auto *SA = cast<" << R.getName()
           << "Attr>(A); (void)SA;\n";
      OS << FunctionContent;
      OS << "  }\n";
    }
  }
}

void EmitClangAttrParserStringSwitches(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Parser-related llvm::StringSwitch cases", OS, Records);
  emitClangAttrArgContextList(Records, OS);
  emitClangAttrIdentifierArgList(Records, OS);
  emitClangAttrUnevaluatedStringLiteralList(Records, OS);
  emitClangAttrVariadicIdentifierArgList(Records, OS);
  emitClangAttrThisIsaIdentifierArgList(Records, OS);
  emitClangAttrAcceptsExprPack(Records, OS);
  emitClangAttrTypeArgList(Records, OS);
  emitClangAttrLateParsedList(Records, OS);
  emitClangAttrLateParsedExperimentalList(Records, OS);
  emitClangAttrStrictIdentifierArgAtIndexList(Records, OS);
}

void EmitClangAttrSubjectMatchRulesParserStringSwitches(RecordKeeper &Records,
                                                        raw_ostream &OS) {
  getPragmaAttributeSupport(Records).generateParsingHelpers(OS);
}

void EmitClangAttrDocTable(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("Clang attribute documentation", OS, Records);

  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  for (const auto *A : Attrs) {
    if (!A->getValueAsBit("ASTNode"))
      continue;
    std::vector<Record *> Docs = A->getValueAsListOfDefs("Documentation");
    assert(!Docs.empty());
    // Only look at the first documentation if there are several.
    // (Currently there's only one such attr, revisit if this becomes common).
    StringRef Text =
        Docs.front()->getValueAsOptionalString("Content").value_or("");
    OS << "\nstatic const char AttrDoc_" << A->getName() << "[] = "
       << "R\"reST(" << Text.trim() << ")reST\";\n";
  }
}

enum class SpellingKind : size_t {
  GNU,
  CXX11,
  C23,
  Declspec,
  Microsoft,
  Keyword,
  Pragma,
  HLSLAnnotation,
  NumSpellingKinds
};
static const size_t NumSpellingKinds = (size_t)SpellingKind::NumSpellingKinds;

class SpellingList {
  std::vector<std::string> Spellings[NumSpellingKinds];

public:
  ArrayRef<std::string> operator[](SpellingKind K) const {
    return Spellings[(size_t)K];
  }

  void add(const Record &Attr, FlattenedSpelling Spelling) {
    SpellingKind Kind =
        StringSwitch<SpellingKind>(Spelling.variety())
            .Case("GNU", SpellingKind::GNU)
            .Case("CXX11", SpellingKind::CXX11)
            .Case("C23", SpellingKind::C23)
            .Case("Declspec", SpellingKind::Declspec)
            .Case("Microsoft", SpellingKind::Microsoft)
            .Case("Keyword", SpellingKind::Keyword)
            .Case("Pragma", SpellingKind::Pragma)
            .Case("HLSLAnnotation", SpellingKind::HLSLAnnotation);
    std::string Name;
    if (!Spelling.nameSpace().empty()) {
      switch (Kind) {
      case SpellingKind::CXX11:
      case SpellingKind::C23:
        Name = Spelling.nameSpace() + "::";
        break;
      case SpellingKind::Pragma:
        Name = Spelling.nameSpace() + " ";
        break;
      default:
        PrintFatalError(Attr.getLoc(), "Unexpected namespace in spelling");
      }
    }
    Name += Spelling.name();

    Spellings[(size_t)Kind].push_back(Name);
  }
};

class DocumentationData {
public:
  const Record *Documentation;
  const Record *Attribute;
  std::string Heading;
  SpellingList SupportedSpellings;

  DocumentationData(const Record &Documentation, const Record &Attribute,
                    std::pair<std::string, SpellingList> HeadingAndSpellings)
      : Documentation(&Documentation), Attribute(&Attribute),
        Heading(std::move(HeadingAndSpellings.first)),
        SupportedSpellings(std::move(HeadingAndSpellings.second)) {}
};

static void WriteCategoryHeader(const Record *DocCategory,
                                raw_ostream &OS) {
  const StringRef Name = DocCategory->getValueAsString("Name");
  OS << Name << "\n" << std::string(Name.size(), '=') << "\n";

  // If there is content, print that as well.
  const StringRef ContentStr = DocCategory->getValueAsString("Content");
  // Trim leading and trailing newlines and spaces.
  OS << ContentStr.trim();

  OS << "\n\n";
}

static std::pair<std::string, SpellingList>
GetAttributeHeadingAndSpellings(const Record &Documentation,
                                const Record &Attribute,
                                StringRef Cat) {
  // FIXME: there is no way to have a per-spelling category for the attribute
  // documentation. This may not be a limiting factor since the spellings
  // should generally be consistently applied across the category.

  std::vector<FlattenedSpelling> Spellings = GetFlattenedSpellings(Attribute);
  if (Spellings.empty())
    PrintFatalError(Attribute.getLoc(),
                    "Attribute has no supported spellings; cannot be "
                    "documented");

  // Determine the heading to be used for this attribute.
  std::string Heading = std::string(Documentation.getValueAsString("Heading"));
  if (Heading.empty()) {
    // If there's only one spelling, we can simply use that.
    if (Spellings.size() == 1)
      Heading = Spellings.begin()->name();
    else {
      std::set<std::string> Uniques;
      for (auto I = Spellings.begin(), E = Spellings.end();
           I != E; ++I) {
        std::string Spelling =
            std::string(NormalizeNameForSpellingComparison(I->name()));
        Uniques.insert(Spelling);
      }
      // If the semantic map has only one spelling, that is sufficient for our
      // needs.
      if (Uniques.size() == 1)
        Heading = *Uniques.begin();
      // If it's in the undocumented category, just construct a header by
      // concatenating all the spellings. Might not be great, but better than
      // nothing.
      else if (Cat == "Undocumented")
        Heading = llvm::join(Uniques.begin(), Uniques.end(), ", ");
    }
  }

  // If the heading is still empty, it is an error.
  if (Heading.empty())
    PrintFatalError(Attribute.getLoc(),
                    "This attribute requires a heading to be specified");

  SpellingList SupportedSpellings;
  for (const auto &I : Spellings)
    SupportedSpellings.add(Attribute, I);

  return std::make_pair(std::move(Heading), std::move(SupportedSpellings));
}

static void WriteDocumentation(RecordKeeper &Records,
                               const DocumentationData &Doc, raw_ostream &OS) {
  OS << Doc.Heading << "\n" << std::string(Doc.Heading.length(), '-') << "\n";

  // List what spelling syntaxes the attribute supports.
  // Note: "#pragma clang attribute" is handled outside the spelling kinds loop
  // so it must be last.
  OS << ".. csv-table:: Supported Syntaxes\n";
  OS << "   :header: \"GNU\", \"C++11\", \"C23\", \"``__declspec``\",";
  OS << " \"Keyword\", \"``#pragma``\", \"HLSL Annotation\", \"``#pragma "
        "clang ";
  OS << "attribute``\"\n\n   \"";
  for (size_t Kind = 0; Kind != NumSpellingKinds; ++Kind) {
    SpellingKind K = (SpellingKind)Kind;
    // TODO: List Microsoft (IDL-style attribute) spellings once we fully
    // support them.
    if (K == SpellingKind::Microsoft)
      continue;

    bool PrintedAny = false;
    for (StringRef Spelling : Doc.SupportedSpellings[K]) {
      if (PrintedAny)
        OS << " |br| ";
      OS << "``" << Spelling << "``";
      PrintedAny = true;
    }

    OS << "\",\"";
  }

  if (getPragmaAttributeSupport(Records).isAttributedSupported(
          *Doc.Attribute))
    OS << "Yes";
  OS << "\"\n\n";

  // If the attribute is deprecated, print a message about it, and possibly
  // provide a replacement attribute.
  if (!Doc.Documentation->isValueUnset("Deprecated")) {
    OS << "This attribute has been deprecated, and may be removed in a future "
       << "version of Clang.";
    const Record &Deprecated = *Doc.Documentation->getValueAsDef("Deprecated");
    const StringRef Replacement = Deprecated.getValueAsString("Replacement");
    if (!Replacement.empty())
      OS << "  This attribute has been superseded by ``" << Replacement
         << "``.";
    OS << "\n\n";
  }

  const StringRef ContentStr = Doc.Documentation->getValueAsString("Content");
  // Trim leading and trailing newlines and spaces.
  OS << ContentStr.trim();

  OS << "\n\n\n";
}

void EmitClangAttrDocs(RecordKeeper &Records, raw_ostream &OS) {
  // Get the documentation introduction paragraph.
  const Record *Documentation = Records.getDef("GlobalDocumentation");
  if (!Documentation) {
    PrintFatalError("The Documentation top-level definition is missing, "
                    "no documentation will be generated.");
    return;
  }

  OS << Documentation->getValueAsString("Intro") << "\n";

  // Gather the Documentation lists from each of the attributes, based on the
  // category provided.
  std::vector<Record *> Attrs = Records.getAllDerivedDefinitions("Attr");
  struct CategoryLess {
    bool operator()(const Record *L, const Record *R) const {
      return L->getValueAsString("Name") < R->getValueAsString("Name");
    }
  };
  std::map<const Record *, std::vector<DocumentationData>, CategoryLess>
      SplitDocs;
  for (const auto *A : Attrs) {
    const Record &Attr = *A;
    std::vector<Record *> Docs = Attr.getValueAsListOfDefs("Documentation");
    for (const auto *D : Docs) {
      const Record &Doc = *D;
      const Record *Category = Doc.getValueAsDef("Category");
      // If the category is "InternalOnly", then there cannot be any other
      // documentation categories (otherwise, the attribute would be
      // emitted into the docs).
      const StringRef Cat = Category->getValueAsString("Name");
      bool InternalOnly = Cat == "InternalOnly";
      if (InternalOnly && Docs.size() > 1)
        PrintFatalError(Doc.getLoc(),
                        "Attribute is \"InternalOnly\", but has multiple "
                        "documentation categories");

      if (!InternalOnly)
        SplitDocs[Category].push_back(DocumentationData(
            Doc, Attr, GetAttributeHeadingAndSpellings(Doc, Attr, Cat)));
    }
  }

  // Having split the attributes out based on what documentation goes where,
  // we can begin to generate sections of documentation.
  for (auto &I : SplitDocs) {
    WriteCategoryHeader(I.first, OS);

    llvm::sort(I.second,
               [](const DocumentationData &D1, const DocumentationData &D2) {
                 return D1.Heading < D2.Heading;
               });

    // Walk over each of the attributes in the category and write out their
    // documentation.
    for (const auto &Doc : I.second)
      WriteDocumentation(Records, Doc, OS);
  }
}

void EmitTestPragmaAttributeSupportedAttributes(RecordKeeper &Records,
                                                raw_ostream &OS) {
  PragmaClangAttributeSupport Support = getPragmaAttributeSupport(Records);
  ParsedAttrMap Attrs = getParsedAttrList(Records);
  OS << "#pragma clang attribute supports the following attributes:\n";
  for (const auto &I : Attrs) {
    if (!Support.isAttributedSupported(*I.second))
      continue;
    OS << I.first;
    if (I.second->isValueUnset("Subjects")) {
      OS << " ()\n";
      continue;
    }
    const Record *SubjectObj = I.second->getValueAsDef("Subjects");
    std::vector<Record *> Subjects =
        SubjectObj->getValueAsListOfDefs("Subjects");
    OS << " (";
    bool PrintComma = false;
    for (const auto &Subject : llvm::enumerate(Subjects)) {
      if (!isSupportedPragmaClangAttributeSubject(*Subject.value()))
        continue;
      if (PrintComma)
        OS << ", ";
      PrintComma = true;
      PragmaClangAttributeSupport::RuleOrAggregateRuleSet &RuleSet =
          Support.SubjectsToRules.find(Subject.value())->getSecond();
      if (RuleSet.isRule()) {
        OS << RuleSet.getRule().getEnumValueName();
        continue;
      }
      OS << "(";
      for (const auto &Rule : llvm::enumerate(RuleSet.getAggregateRuleSet())) {
        if (Rule.index())
          OS << ", ";
        OS << Rule.value().getEnumValueName();
      }
      OS << ")";
    }
    OS << ")\n";
  }
  OS << "End of supported attributes.\n";
}

} // end namespace clang
