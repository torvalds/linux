//=== ClangASTPropsEmitter.cpp - Generate Clang AST properties --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits code for working with Clang AST properties.
//
//===----------------------------------------------------------------------===//

#include "ASTTableGen.h"
#include "TableGenBackends.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
using namespace llvm;
using namespace clang;
using namespace clang::tblgen;

static StringRef getReaderResultType(TypeNode _) { return "QualType"; }

namespace {

struct ReaderWriterInfo {
  bool IsReader;

  /// The name of the node hierarchy.  Not actually sensitive to IsReader,
  /// but useful to cache here anyway.
  StringRef HierarchyName;

  /// The suffix on classes: Reader/Writer
  StringRef ClassSuffix;

  /// The base name of methods: read/write
  StringRef MethodPrefix;

  /// The name of the property helper member: R/W
  StringRef HelperVariable;

  /// The result type of methods on the class.
  StringRef ResultType;

  template <class NodeClass>
  static ReaderWriterInfo forReader() {
    return ReaderWriterInfo{
      true,
      NodeClass::getASTHierarchyName(),
      "Reader",
      "read",
      "R",
      getReaderResultType(NodeClass())
    };
  }

  template <class NodeClass>
  static ReaderWriterInfo forWriter() {
    return ReaderWriterInfo{
      false,
      NodeClass::getASTHierarchyName(),
      "Writer",
      "write",
      "W",
      "void"
    };
  }
};

struct NodeInfo {
  std::vector<Property> Properties;
  CreationRule Creator = nullptr;
  OverrideRule Override = nullptr;
  ReadHelperRule ReadHelper = nullptr;
};

struct CasedTypeInfo {
  TypeKindRule KindRule;
  std::vector<TypeCase> Cases;
};

class ASTPropsEmitter {
	raw_ostream &Out;
	RecordKeeper &Records;
	std::map<HasProperties, NodeInfo> NodeInfos;
  std::vector<PropertyType> AllPropertyTypes;
  std::map<PropertyType, CasedTypeInfo> CasedTypeInfos;

public:
	ASTPropsEmitter(RecordKeeper &records, raw_ostream &out)
		: Out(out), Records(records) {

		// Find all the properties.
		for (Property property :
           records.getAllDerivedDefinitions(PropertyClassName)) {
			HasProperties node = property.getClass();
			NodeInfos[node].Properties.push_back(property);
		}

    // Find all the creation rules.
    for (CreationRule creationRule :
           records.getAllDerivedDefinitions(CreationRuleClassName)) {
      HasProperties node = creationRule.getClass();

      auto &info = NodeInfos[node];
      if (info.Creator) {
        PrintFatalError(creationRule.getLoc(),
                        "multiple creator rules for \"" + node.getName()
                          + "\"");
      }
      info.Creator = creationRule;
    }

    // Find all the override rules.
    for (OverrideRule overrideRule :
           records.getAllDerivedDefinitions(OverrideRuleClassName)) {
      HasProperties node = overrideRule.getClass();

      auto &info = NodeInfos[node];
      if (info.Override) {
        PrintFatalError(overrideRule.getLoc(),
                        "multiple override rules for \"" + node.getName()
                          + "\"");
      }
      info.Override = overrideRule;
    }

    // Find all the write helper rules.
    for (ReadHelperRule helperRule :
           records.getAllDerivedDefinitions(ReadHelperRuleClassName)) {
      HasProperties node = helperRule.getClass();

      auto &info = NodeInfos[node];
      if (info.ReadHelper) {
        PrintFatalError(helperRule.getLoc(),
                        "multiple write helper rules for \"" + node.getName()
                          + "\"");
      }
      info.ReadHelper = helperRule;
    }

    // Find all the concrete property types.
    for (PropertyType type :
           records.getAllDerivedDefinitions(PropertyTypeClassName)) {
      // Ignore generic specializations; they're generally not useful when
      // emitting basic emitters etc.
      if (type.isGenericSpecialization()) continue;

      AllPropertyTypes.push_back(type);
    }

    // Find all the type kind rules.
    for (TypeKindRule kindRule :
           records.getAllDerivedDefinitions(TypeKindClassName)) {
      PropertyType type = kindRule.getParentType();
      auto &info = CasedTypeInfos[type];
      if (info.KindRule) {
        PrintFatalError(kindRule.getLoc(),
                        "multiple kind rules for \""
                           + type.getCXXTypeName() + "\"");
      }
      info.KindRule = kindRule;
    }

    // Find all the type cases.
    for (TypeCase typeCase :
           records.getAllDerivedDefinitions(TypeCaseClassName)) {
      CasedTypeInfos[typeCase.getParentType()].Cases.push_back(typeCase);
    }

    Validator(*this).validate();
	}

  void visitAllProperties(HasProperties derived, const NodeInfo &derivedInfo,
                          function_ref<void (Property)> visit) {
    std::set<StringRef> ignoredProperties;

    auto overrideRule = derivedInfo.Override;
    if (overrideRule) {
      auto list = overrideRule.getIgnoredProperties();
      ignoredProperties.insert(list.begin(), list.end());
    }

    // TODO: we should sort the properties in various ways
    //   - put arrays at the end to enable abbreviations
    //   - put conditional properties after properties used in the condition

    visitAllNodesWithInfo(derived, derivedInfo,
                          [&](HasProperties node, const NodeInfo &info) {
      for (Property prop : info.Properties) {
        if (ignoredProperties.count(prop.getName()))
          continue;

        visit(prop);
      }
    });
  }

  void visitAllNodesWithInfo(HasProperties derivedNode,
                             const NodeInfo &derivedNodeInfo,
                             llvm::function_ref<void (HasProperties node,
                                                      const NodeInfo &info)>
                               visit) {
    visit(derivedNode, derivedNodeInfo);

    // Also walk the bases if appropriate.
    if (ASTNode base = derivedNode.getAs<ASTNode>()) {
      for (base = base.getBase(); base; base = base.getBase()) {
        auto it = NodeInfos.find(base);

        // Ignore intermediate nodes that don't add interesting properties.
        if (it == NodeInfos.end()) continue;
        auto &baseInfo = it->second;

        visit(base, baseInfo);
      }
    }
  }

  template <class NodeClass>
  void emitNodeReaderClass() {
    auto info = ReaderWriterInfo::forReader<NodeClass>();
    emitNodeReaderWriterClass<NodeClass>(info);
  }

  template <class NodeClass>
  void emitNodeWriterClass() {
    auto info = ReaderWriterInfo::forWriter<NodeClass>();
    emitNodeReaderWriterClass<NodeClass>(info);
  }

  template <class NodeClass>
  void emitNodeReaderWriterClass(const ReaderWriterInfo &info);

  template <class NodeClass>
  void emitNodeReaderWriterMethod(NodeClass node,
                                  const ReaderWriterInfo &info);

  void emitPropertiedReaderWriterBody(HasProperties node,
                                      const ReaderWriterInfo &info);

  void emitReadOfProperty(StringRef readerName, Property property);
  void emitReadOfProperty(StringRef readerName, StringRef name,
                          PropertyType type, StringRef condition = "");

  void emitWriteOfProperty(StringRef writerName, Property property);
  void emitWriteOfProperty(StringRef writerName, StringRef name,
                           PropertyType type, StringRef readCode,
                           StringRef condition = "");

  void emitBasicReaderWriterFile(const ReaderWriterInfo &info);
  void emitDispatcherTemplate(const ReaderWriterInfo &info);
  void emitPackUnpackOptionalTemplate(const ReaderWriterInfo &info);
  void emitBasicReaderWriterTemplate(const ReaderWriterInfo &info);

  void emitCasedReaderWriterMethodBody(PropertyType type,
                                       const CasedTypeInfo &typeCases,
                                       const ReaderWriterInfo &info);

private:
  class Validator {
    ASTPropsEmitter &Emitter;
    std::set<HasProperties> ValidatedNodes;

  public:
    Validator(ASTPropsEmitter &emitter) : Emitter(emitter) {}
    void validate();

  private:
    void validateNode(HasProperties node, const NodeInfo &nodeInfo);
    void validateType(PropertyType type, WrappedRecord context);
  };
};

} // end anonymous namespace

void ASTPropsEmitter::Validator::validate() {
  for (auto &entry : Emitter.NodeInfos) {
    validateNode(entry.first, entry.second);
  }

  if (ErrorsPrinted > 0) {
    PrintFatalError("property validation failed");
  }
}

void ASTPropsEmitter::Validator::validateNode(HasProperties derivedNode,
                                              const NodeInfo &derivedNodeInfo) {
  if (!ValidatedNodes.insert(derivedNode).second) return;

  // A map from property name to property.
  std::map<StringRef, Property> allProperties;

  Emitter.visitAllNodesWithInfo(derivedNode, derivedNodeInfo,
                                [&](HasProperties node,
                                    const NodeInfo &nodeInfo) {
    for (Property property : nodeInfo.Properties) {
      validateType(property.getType(), property);

      auto result = allProperties.insert(
                      std::make_pair(property.getName(), property));

      // Diagnose non-unique properties.
      if (!result.second) {
        // The existing property is more likely to be associated with a
        // derived node, so use it as the error.
        Property existingProperty = result.first->second;
        PrintError(existingProperty.getLoc(),
                   "multiple properties named \"" + property.getName()
                      + "\" in hierarchy of " + derivedNode.getName());
        PrintNote(property.getLoc(), "existing property");
      }
    }
  });
}

void ASTPropsEmitter::Validator::validateType(PropertyType type,
                                              WrappedRecord context) {
  if (!type.isGenericSpecialization()) {
    if (type.getCXXTypeName() == "") {
      PrintError(type.getLoc(),
                 "type is not generic but has no C++ type name");
      if (context) PrintNote(context.getLoc(), "type used here");
    }
  } else if (auto eltType = type.getArrayElementType()) {
    validateType(eltType, context);
  } else if (auto valueType = type.getOptionalElementType()) {
    validateType(valueType, context);

    if (valueType.getPackOptionalCode().empty()) {
      PrintError(valueType.getLoc(),
                 "type doesn't provide optional-packing code");
      if (context) PrintNote(context.getLoc(), "type used here");
    } else if (valueType.getUnpackOptionalCode().empty()) {
      PrintError(valueType.getLoc(),
                 "type doesn't provide optional-unpacking code");
      if (context) PrintNote(context.getLoc(), "type used here");
    }
  } else {
    PrintError(type.getLoc(), "unknown generic property type");
    if (context) PrintNote(context.getLoc(), "type used here");
  }
}

/****************************************************************************/
/**************************** AST READER/WRITERS ****************************/
/****************************************************************************/

template <class NodeClass>
void ASTPropsEmitter::emitNodeReaderWriterClass(const ReaderWriterInfo &info) {
  StringRef suffix = info.ClassSuffix;
  StringRef var = info.HelperVariable;

  // Enter the class declaration.
  Out << "template <class Property" << suffix << ">\n"
         "class Abstract" << info.HierarchyName << suffix << " {\n"
         "public:\n"
         "  Property" << suffix << " &" << var << ";\n\n";

  // Emit the constructor.
  Out << "  Abstract" << info.HierarchyName << suffix
                      << "(Property" << suffix << " &" << var << ") : "
                      << var << "(" << var << ") {}\n\n";

  // Emit a method that dispatches on a kind to the appropriate node-specific
  // method.
  Out << "  " << info.ResultType << " " << info.MethodPrefix << "(";
  if (info.IsReader)
    Out       << NodeClass::getASTIdTypeName() << " kind";
  else
    Out       << "const " << info.HierarchyName << " *node";
  Out         << ") {\n"
         "    switch (";
  if (info.IsReader)
    Out         << "kind";
  else
    Out         << "node->" << NodeClass::getASTIdAccessorName() << "()";
  Out           << ") {\n";
  visitASTNodeHierarchy<NodeClass>(Records, [&](NodeClass node, NodeClass _) {
    if (node.isAbstract()) return;
    Out << "    case " << info.HierarchyName << "::" << node.getId() << ":\n"
           "      return " << info.MethodPrefix << node.getClassName() << "(";
    if (!info.IsReader)
      Out                  << "static_cast<const " << node.getClassName()
                           << " *>(node)";
    Out                    << ");\n";
  });
  Out << "    }\n"
         "    llvm_unreachable(\"bad kind\");\n"
         "  }\n\n";

  // Emit node-specific methods for all the concrete nodes.
  visitASTNodeHierarchy<NodeClass>(Records,
                                   [&](NodeClass node, NodeClass base) {
    if (node.isAbstract()) return;
    emitNodeReaderWriterMethod(node, info);
  });

  // Finish the class.
  Out << "};\n\n";
}

/// Emit a reader method for the given concrete AST node class.
template <class NodeClass>
void ASTPropsEmitter::emitNodeReaderWriterMethod(NodeClass node,
                                           const ReaderWriterInfo &info) {
  // Declare and start the method.
  Out << "  " << info.ResultType << " "
              << info.MethodPrefix << node.getClassName() << "(";
  if (!info.IsReader)
    Out <<       "const " << node.getClassName() << " *node";
  Out <<         ") {\n";
  if (info.IsReader)
    Out << "    auto &ctx = " << info.HelperVariable << ".getASTContext();\n";

  emitPropertiedReaderWriterBody(node, info);

  // Finish the method declaration.
  Out << "  }\n\n";
}

void ASTPropsEmitter::emitPropertiedReaderWriterBody(HasProperties node,
                                               const ReaderWriterInfo &info) {
  // Find the information for this node.
  auto it = NodeInfos.find(node);
  if (it == NodeInfos.end())
    PrintFatalError(node.getLoc(),
                    "no information about how to deserialize \""
                      + node.getName() + "\"");
  auto &nodeInfo = it->second;

  StringRef creationCode;
  if (info.IsReader) {
    // We should have a creation rule.
    if (!nodeInfo.Creator)
      PrintFatalError(node.getLoc(),
                      "no " CreationRuleClassName " for \""
                        + node.getName() + "\"");

    creationCode = nodeInfo.Creator.getCreationCode();
  }

  // Emit the ReadHelper code, if present.
  if (!info.IsReader && nodeInfo.ReadHelper) {
    Out << "    " << nodeInfo.ReadHelper.getHelperCode() << "\n";
  }

  // Emit code to read all the properties.
  visitAllProperties(node, nodeInfo, [&](Property prop) {
    // Verify that the creation code refers to this property.
    if (info.IsReader && !creationCode.contains(prop.getName()))
      PrintFatalError(nodeInfo.Creator.getLoc(),
                      "creation code for " + node.getName()
                        + " doesn't refer to property \""
                        + prop.getName() + "\"");

    // Emit code to read or write this property.
    if (info.IsReader)
      emitReadOfProperty(info.HelperVariable, prop);
    else
      emitWriteOfProperty(info.HelperVariable, prop);
  });

  // Emit the final creation code.
  if (info.IsReader)
    Out << "    " << creationCode << "\n";
}

static void emitBasicReaderWriterMethodSuffix(raw_ostream &out,
                                              PropertyType type,
                                              bool isForRead) {
  if (!type.isGenericSpecialization()) {
    out << type.getAbstractTypeName();
  } else if (auto eltType = type.getArrayElementType()) {
    out << "Array";
    // We only include an explicit template argument for reads so that
    // we don't cause spurious const mismatches.
    if (isForRead) {
      out << "<";
      eltType.emitCXXValueTypeName(isForRead, out);
      out << ">";
    }
  } else if (auto valueType = type.getOptionalElementType()) {
    out << "Optional";
    // We only include an explicit template argument for reads so that
    // we don't cause spurious const mismatches.
    if (isForRead) {
      out << "<";
      valueType.emitCXXValueTypeName(isForRead, out);
      out << ">";
    }
  } else {
    PrintFatalError(type.getLoc(), "unexpected generic property type");
  }
}

/// Emit code to read the given property in a node-reader method.
void ASTPropsEmitter::emitReadOfProperty(StringRef readerName,
                                         Property property) {
  emitReadOfProperty(readerName, property.getName(), property.getType(),
                     property.getCondition());
}

void ASTPropsEmitter::emitReadOfProperty(StringRef readerName,
                                         StringRef name,
                                         PropertyType type,
                                         StringRef condition) {
  // Declare all the necessary buffers.
  auto bufferTypes = type.getBufferElementTypes();
  for (size_t i = 0, e = bufferTypes.size(); i != e; ++i) {
    Out << "    llvm::SmallVector<";
    PropertyType(bufferTypes[i]).emitCXXValueTypeName(/*for read*/ true, Out);
    Out << ", 8> " << name << "_buffer_" << i << ";\n";
  }

  //   T prop = R.find("prop").read##ValueType(buffers...);
  // We intentionally ignore shouldPassByReference here: we're going to
  // get a pr-value back from read(), and we should be able to forward
  // that in the creation rule.
  Out << "    ";
  if (!condition.empty())
    Out << "std::optional<";
  type.emitCXXValueTypeName(true, Out);
  if (!condition.empty()) Out << ">";
  Out << " " << name;

  if (condition.empty()) {
    Out << " = ";
  } else {
    Out << ";\n"
           "    if (" << condition << ") {\n"
           "      " << name << ".emplace(";
  }

  Out << readerName << ".find(\"" << name << "\")."
      << (type.isGenericSpecialization() ? "template " : "") << "read";
  emitBasicReaderWriterMethodSuffix(Out, type, /*for read*/ true);
  Out << "(";
  for (size_t i = 0, e = bufferTypes.size(); i != e; ++i) {
    Out << (i > 0 ? ", " : "") << name << "_buffer_" << i;
  }
  Out << ")";

  if (condition.empty()) {
    Out << ";\n";
  } else {
    Out << ");\n"
           "    }\n";
  }
}

/// Emit code to write the given property in a node-writer method.
void ASTPropsEmitter::emitWriteOfProperty(StringRef writerName,
                                          Property property) {
  emitWriteOfProperty(writerName, property.getName(), property.getType(),
                      property.getReadCode(), property.getCondition());
}

void ASTPropsEmitter::emitWriteOfProperty(StringRef writerName,
                                          StringRef name,
                                          PropertyType type,
                                          StringRef readCode,
                                          StringRef condition) {
  if (!condition.empty()) {
    Out << "    if (" << condition << ") {\n";
  }

  // Focus down to the property:
  //   T prop = <READ>;
  //   W.find("prop").write##ValueType(prop);
  Out << "    ";
  type.emitCXXValueTypeName(false, Out);
  Out << " " << name << " = (" << readCode << ");\n"
         "    " << writerName << ".find(\"" << name << "\").write";
  emitBasicReaderWriterMethodSuffix(Out, type, /*for read*/ false);
  Out << "(" << name << ");\n";

  if (!condition.empty()) {
    Out << "    }\n";
  }
}

/// Emit an .inc file that defines the AbstractFooReader class
/// for the given AST class hierarchy.
template <class NodeClass>
static void emitASTReader(RecordKeeper &records, raw_ostream &out,
                          StringRef description) {
  emitSourceFileHeader(description, out, records);

  ASTPropsEmitter(records, out).emitNodeReaderClass<NodeClass>();
}

void clang::EmitClangTypeReader(RecordKeeper &records, raw_ostream &out) {
  emitASTReader<TypeNode>(records, out, "A CRTP reader for Clang Type nodes");
}

/// Emit an .inc file that defines the AbstractFooWriter class
/// for the given AST class hierarchy.
template <class NodeClass>
static void emitASTWriter(RecordKeeper &records, raw_ostream &out,
                          StringRef description) {
  emitSourceFileHeader(description, out, records);

  ASTPropsEmitter(records, out).emitNodeWriterClass<NodeClass>();
}

void clang::EmitClangTypeWriter(RecordKeeper &records, raw_ostream &out) {
  emitASTWriter<TypeNode>(records, out, "A CRTP writer for Clang Type nodes");
}

/****************************************************************************/
/*************************** BASIC READER/WRITERS ***************************/
/****************************************************************************/

void
ASTPropsEmitter::emitDispatcherTemplate(const ReaderWriterInfo &info) {
  // Declare the {Read,Write}Dispatcher template.
  StringRef dispatcherPrefix = (info.IsReader ? "Read" : "Write");
  Out << "template <class ValueType>\n"
         "struct " << dispatcherPrefix << "Dispatcher;\n";

  // Declare a specific specialization of the dispatcher template.
  auto declareSpecialization =
    [&](StringRef specializationParameters,
        const Twine &cxxTypeName,
        StringRef methodSuffix) {
    StringRef var = info.HelperVariable;
    Out << "template " << specializationParameters << "\n"
           "struct " << dispatcherPrefix << "Dispatcher<"
                     << cxxTypeName << "> {\n";
    Out << "  template <class Basic" << info.ClassSuffix << ", class... Args>\n"
           "  static " << (info.IsReader ? cxxTypeName : "void") << " "
                       << info.MethodPrefix
                       << "(Basic" << info.ClassSuffix << " &" << var
                       << ", Args &&... args) {\n"
           "    return " << var << "."
                         << info.MethodPrefix << methodSuffix
                         << "(std::forward<Args>(args)...);\n"
           "  }\n"
           "};\n";
  };

  // Declare explicit specializations for each of the concrete types.
  for (PropertyType type : AllPropertyTypes) {
    declareSpecialization("<>",
                          type.getCXXTypeName(),
                          type.getAbstractTypeName());
    // Also declare a specialization for the const type when appropriate.
    if (!info.IsReader && type.isConstWhenWriting()) {
      declareSpecialization("<>",
                            "const " + type.getCXXTypeName(),
                            type.getAbstractTypeName());
    }
  }
  // Declare partial specializations for ArrayRef and Optional.
  declareSpecialization("<class T>",
                        "llvm::ArrayRef<T>",
                        "Array");
  declareSpecialization("<class T>", "std::optional<T>", "Optional");
  Out << "\n";
}

void
ASTPropsEmitter::emitPackUnpackOptionalTemplate(const ReaderWriterInfo &info) {
  StringRef classPrefix = (info.IsReader ? "Unpack" : "Pack");
  StringRef methodName = (info.IsReader ? "unpack" : "pack");

  // Declare the {Pack,Unpack}OptionalValue template.
  Out << "template <class ValueType>\n"
         "struct " << classPrefix << "OptionalValue;\n";

  auto declareSpecialization = [&](const Twine &typeName, StringRef code) {
    Out << "template <>\n"
           "struct "
        << classPrefix << "OptionalValue<" << typeName
        << "> {\n"
           "  static "
        << (info.IsReader ? "std::optional<" : "") << typeName
        << (info.IsReader ? "> " : " ") << methodName << "("
        << (info.IsReader ? "" : "std::optional<") << typeName
        << (info.IsReader ? "" : ">")
        << " value) {\n"
           "    return "
        << code
        << ";\n"
           "  }\n"
           "};\n";
  };

  for (PropertyType type : AllPropertyTypes) {
    StringRef code = (info.IsReader ? type.getUnpackOptionalCode()
                                    : type.getPackOptionalCode());
    if (code.empty()) continue;

    StringRef typeName = type.getCXXTypeName();
    declareSpecialization(typeName, code);
    if (type.isConstWhenWriting() && !info.IsReader)
      declareSpecialization("const " + typeName, code);
  }
  Out << "\n";
}

void
ASTPropsEmitter::emitBasicReaderWriterTemplate(const ReaderWriterInfo &info) {
  // Emit the Basic{Reader,Writer}Base template.
  Out << "template <class Impl>\n"
         "class Basic" << info.ClassSuffix << "Base {\n";
  Out << "  ASTContext &C;\n";
  Out << "protected:\n"
         "  Basic"
      << info.ClassSuffix << "Base" << ("(ASTContext &ctx) : C(ctx)")
      << " {}\n"
         "public:\n";
  Out << "  ASTContext &getASTContext() { return C; }\n";
  Out << "  Impl &asImpl() { return static_cast<Impl&>(*this); }\n";

  auto enterReaderWriterMethod = [&](StringRef cxxTypeName,
                                     StringRef abstractTypeName,
                                     bool shouldPassByReference,
                                     bool constWhenWriting,
                                     StringRef paramName) {
    Out << "  " << (info.IsReader ? cxxTypeName : "void")
                << " " << info.MethodPrefix << abstractTypeName << "(";
    if (!info.IsReader)
      Out       << (shouldPassByReference || constWhenWriting ? "const " : "")
                << cxxTypeName
                << (shouldPassByReference ? " &" : "") << " " << paramName;
    Out         << ") {\n";
  };

  // Emit {read,write}ValueType methods for all the enum and subclass types
  // that default to using the integer/base-class implementations.
  for (PropertyType type : AllPropertyTypes) {
    auto enterMethod = [&](StringRef paramName) {
      enterReaderWriterMethod(type.getCXXTypeName(),
                              type.getAbstractTypeName(),
                              type.shouldPassByReference(),
                              type.isConstWhenWriting(),
                              paramName);
    };
    auto exitMethod = [&] {
      Out << "  }\n";
    };

    // Handled cased types.
    auto casedIter = CasedTypeInfos.find(type);
    if (casedIter != CasedTypeInfos.end()) {
      enterMethod("node");
      emitCasedReaderWriterMethodBody(type, casedIter->second, info);
      exitMethod();

    } else if (type.isEnum()) {
      enterMethod("value");
      if (info.IsReader)
        Out << "    return asImpl().template readEnum<"
            <<         type.getCXXTypeName() << ">();\n";
      else
        Out << "    asImpl().writeEnum(value);\n";
      exitMethod();

    } else if (PropertyType superclass = type.getSuperclassType()) {
      enterMethod("value");
      if (info.IsReader)
        Out << "    return cast_or_null<" << type.getSubclassClassName()
                                          << ">(asImpl().read"
                                          << superclass.getAbstractTypeName()
                                          << "());\n";
      else
        Out << "    asImpl().write" << superclass.getAbstractTypeName()
                                    << "(value);\n";
      exitMethod();

    } else {
      // The other types can't be handled as trivially.
    }
  }
  Out << "};\n\n";
}

void ASTPropsEmitter::emitCasedReaderWriterMethodBody(PropertyType type,
                                             const CasedTypeInfo &typeCases,
                                             const ReaderWriterInfo &info) {
  if (typeCases.Cases.empty()) {
    assert(typeCases.KindRule);
    PrintFatalError(typeCases.KindRule.getLoc(),
                    "no cases found for \"" + type.getCXXTypeName() + "\"");
  }
  if (!typeCases.KindRule) {
    assert(!typeCases.Cases.empty());
    PrintFatalError(typeCases.Cases.front().getLoc(),
                    "no kind rule for \"" + type.getCXXTypeName() + "\"");
  }

  auto var = info.HelperVariable;
  std::string subvar = ("sub" + var).str();

  // Bind `ctx` for readers.
  if (info.IsReader)
    Out << "    auto &ctx = asImpl().getASTContext();\n";

  // Start an object.
  Out << "    auto &&" << subvar << " = asImpl()."
                       << info.MethodPrefix << "Object();\n";

  // Read/write the kind property;
  TypeKindRule kindRule = typeCases.KindRule;
  StringRef kindProperty = kindRule.getKindPropertyName();
  PropertyType kindType = kindRule.getKindType();
  if (info.IsReader) {
    emitReadOfProperty(subvar, kindProperty, kindType);
  } else {
    // Write the property.  Note that this will implicitly read the
    // kind into a local variable with the right name.
    emitWriteOfProperty(subvar, kindProperty, kindType,
                        kindRule.getReadCode());
  }

  // Prepare a ReaderWriterInfo with a helper variable that will use
  // the sub-reader/writer.
  ReaderWriterInfo subInfo = info;
  subInfo.HelperVariable = subvar;

  // Switch on the kind.
  Out << "    switch (" << kindProperty << ") {\n";
  for (TypeCase typeCase : typeCases.Cases) {
    Out << "    case " << type.getCXXTypeName() << "::"
                       << typeCase.getCaseName() << ": {\n";
    emitPropertiedReaderWriterBody(typeCase, subInfo);
    if (!info.IsReader)
      Out << "    return;\n";
    Out << "    }\n\n";
  }
  Out << "    }\n"
         "    llvm_unreachable(\"bad " << kindType.getCXXTypeName()
                                       << "\");\n";
}

void ASTPropsEmitter::emitBasicReaderWriterFile(const ReaderWriterInfo &info) {
  emitDispatcherTemplate(info);
  emitPackUnpackOptionalTemplate(info);
  emitBasicReaderWriterTemplate(info);
}

/// Emit an .inc file that defines some helper classes for reading
/// basic values.
void clang::EmitClangBasicReader(RecordKeeper &records, raw_ostream &out) {
  emitSourceFileHeader("Helper classes for BasicReaders", out, records);

  // Use any property, we won't be using those properties.
  auto info = ReaderWriterInfo::forReader<TypeNode>();
  ASTPropsEmitter(records, out).emitBasicReaderWriterFile(info);
}

/// Emit an .inc file that defines some helper classes for writing
/// basic values.
void clang::EmitClangBasicWriter(RecordKeeper &records, raw_ostream &out) {
  emitSourceFileHeader("Helper classes for BasicWriters", out, records);

  // Use any property, we won't be using those properties.
  auto info = ReaderWriterInfo::forWriter<TypeNode>();
  ASTPropsEmitter(records, out).emitBasicReaderWriterFile(info);
}
