//===- ExtractAPI/API.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the APIRecord-based structs and the APISet class.
///
/// Clang ExtractAPI is a tool to collect API information from a given set of
/// header files. The structures in this file describe data representations of
/// the API information collected for various kinds of symbols.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_API_H
#define LLVM_CLANG_EXTRACTAPI_API_H

#include "clang/AST/Availability.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RawCommentList.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/ExtractAPI/DeclarationFragments.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>

namespace clang {
namespace extractapi {

class Template {
  struct TemplateParameter {
    // "class", "typename", or concept name
    std::string Type;
    std::string Name;
    unsigned int Index;
    unsigned int Depth;
    bool IsParameterPack;

    TemplateParameter(std::string Type, std::string Name, unsigned int Index,
                      unsigned int Depth, bool IsParameterPack)
        : Type(Type), Name(Name), Index(Index), Depth(Depth),
          IsParameterPack(IsParameterPack) {}
  };

  struct TemplateConstraint {
    // type name of the constraint, if it has one
    std::string Type;
    std::string Kind;
    std::string LHS, RHS;
  };
  llvm::SmallVector<TemplateParameter> Parameters;
  llvm::SmallVector<TemplateConstraint> Constraints;

public:
  Template() = default;

  Template(const TemplateDecl *Decl) {
    for (auto *const Parameter : *Decl->getTemplateParameters()) {
      const auto *Param = dyn_cast<TemplateTypeParmDecl>(Parameter);
      if (!Param) // some params are null
        continue;
      std::string Type;
      if (Param->hasTypeConstraint())
        Type = Param->getTypeConstraint()->getNamedConcept()->getName().str();
      else if (Param->wasDeclaredWithTypename())
        Type = "typename";
      else
        Type = "class";

      addTemplateParameter(Type, Param->getName().str(), Param->getIndex(),
                           Param->getDepth(), Param->isParameterPack());
    }
  }

  Template(const ClassTemplatePartialSpecializationDecl *Decl) {
    for (auto *const Parameter : *Decl->getTemplateParameters()) {
      const auto *Param = dyn_cast<TemplateTypeParmDecl>(Parameter);
      if (!Param) // some params are null
        continue;
      std::string Type;
      if (Param->hasTypeConstraint())
        Type = Param->getTypeConstraint()->getNamedConcept()->getName().str();
      else if (Param->wasDeclaredWithTypename())
        Type = "typename";
      else
        Type = "class";

      addTemplateParameter(Type, Param->getName().str(), Param->getIndex(),
                           Param->getDepth(), Param->isParameterPack());
    }
  }

  Template(const VarTemplatePartialSpecializationDecl *Decl) {
    for (auto *const Parameter : *Decl->getTemplateParameters()) {
      const auto *Param = dyn_cast<TemplateTypeParmDecl>(Parameter);
      if (!Param) // some params are null
        continue;
      std::string Type;
      if (Param->hasTypeConstraint())
        Type = Param->getTypeConstraint()->getNamedConcept()->getName().str();
      else if (Param->wasDeclaredWithTypename())
        Type = "typename";
      else
        Type = "class";

      addTemplateParameter(Type, Param->getName().str(), Param->getIndex(),
                           Param->getDepth(), Param->isParameterPack());
    }
  }

  const llvm::SmallVector<TemplateParameter> &getParameters() const {
    return Parameters;
  }

  const llvm::SmallVector<TemplateConstraint> &getConstraints() const {
    return Constraints;
  }

  void addTemplateParameter(std::string Type, std::string Name,
                            unsigned int Index, unsigned int Depth,
                            bool IsParameterPack) {
    Parameters.emplace_back(Type, Name, Index, Depth, IsParameterPack);
  }

  bool empty() const { return Parameters.empty() && Constraints.empty(); }
};

/// DocComment is a vector of RawComment::CommentLine.
///
/// Each line represents one line of striped documentation comment,
/// with source range information. This simplifies calculating the source
/// location of a character in the doc comment for pointing back to the source
/// file.
/// e.g.
/// \code
///   /// This is a documentation comment
///       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~'  First line.
///   ///     with multiple lines.
///       ^~~~~~~~~~~~~~~~~~~~~~~'         Second line.
/// \endcode
using DocComment = std::vector<RawComment::CommentLine>;

struct APIRecord;

// This represents a reference to another symbol that might come from external
/// sources.
struct SymbolReference {
  StringRef Name;
  StringRef USR;

  /// The source project/module/product of the referred symbol.
  StringRef Source;

  // A Pointer to the APIRecord for this reference if known
  const APIRecord *Record = nullptr;

  SymbolReference() = default;
  SymbolReference(StringRef Name, StringRef USR, StringRef Source = "")
      : Name(Name), USR(USR), Source(Source) {}
  SymbolReference(const APIRecord *R);

  /// Determine if this SymbolReference is empty.
  ///
  /// \returns true if and only if all \c Name, \c USR, and \c Source is empty.
  bool empty() const { return Name.empty() && USR.empty() && Source.empty(); }
};

class RecordContext;

// Concrete classes deriving from APIRecord need to have a construct with first
// arguments USR, and Name, in that order. This is so that they
// are compatible with `APISet::createRecord`.
// When adding a new kind of record don't forget to update APIRecords.inc!
/// The base representation of an API record. Holds common symbol information.
struct APIRecord {
  /// Discriminator for LLVM-style RTTI (dyn_cast<> et al.)
  enum RecordKind {
    RK_Unknown,
    // If adding a record context record kind here make sure to update
    // RecordContext::classof if needed and add a RECORD_CONTEXT entry to
    // APIRecords.inc
    RK_FirstRecordContext,
    RK_Namespace,
    RK_Enum,
    RK_Struct,
    RK_Union,
    RK_ObjCInterface,
    RK_ObjCCategory,
    RK_ObjCProtocol,
    RK_CXXClass,
    RK_ClassTemplate,
    RK_ClassTemplateSpecialization,
    RK_ClassTemplatePartialSpecialization,
    RK_StructField,
    RK_UnionField,
    RK_CXXField,
    RK_StaticField,
    RK_CXXFieldTemplate,
    RK_GlobalVariable,
    RK_GlobalVariableTemplate,
    RK_GlobalVariableTemplateSpecialization,
    RK_GlobalVariableTemplatePartialSpecialization,
    RK_LastRecordContext,
    RK_GlobalFunction,
    RK_GlobalFunctionTemplate,
    RK_GlobalFunctionTemplateSpecialization,
    RK_EnumConstant,
    RK_Concept,
    RK_CXXStaticMethod,
    RK_CXXInstanceMethod,
    RK_CXXConstructorMethod,
    RK_CXXDestructorMethod,
    RK_CXXMethodTemplate,
    RK_CXXMethodTemplateSpecialization,
    RK_ObjCInstanceProperty,
    RK_ObjCClassProperty,
    RK_ObjCIvar,
    RK_ObjCClassMethod,
    RK_ObjCInstanceMethod,
    RK_MacroDefinition,
    RK_Typedef,
  };

  StringRef USR;
  StringRef Name;

  SymbolReference Parent;

  PresumedLoc Location;
  AvailabilityInfo Availability;
  LinkageInfo Linkage;

  /// Documentation comment lines attached to this symbol declaration.
  DocComment Comment;

  /// Declaration fragments of this symbol declaration.
  DeclarationFragments Declaration;

  /// SubHeading provides a more detailed representation than the plain
  /// declaration name.
  ///
  /// SubHeading is an array of declaration fragments of tagged declaration
  /// name, with potentially more tokens (for example the \c +/- symbol for
  /// Objective-C class/instance methods).
  DeclarationFragments SubHeading;

  /// Whether the symbol was defined in a system header.
  bool IsFromSystemHeader;

  AccessControl Access;

  RecordKind KindForDisplay;

private:
  const RecordKind Kind;
  friend class RecordContext;
  // Used to store the next child record in RecordContext. This works because
  // APIRecords semantically only have one parent.
  mutable APIRecord *NextInContext = nullptr;

public:
  APIRecord *getNextInContext() const { return NextInContext; }

  RecordKind getKind() const { return Kind; }
  RecordKind getKindForDisplay() const { return KindForDisplay; }

  static APIRecord *castFromRecordContext(const RecordContext *Ctx);
  static RecordContext *castToRecordContext(const APIRecord *Record);

  APIRecord() = delete;

  APIRecord(RecordKind Kind, StringRef USR, StringRef Name,
            SymbolReference Parent, PresumedLoc Location,
            AvailabilityInfo Availability, LinkageInfo Linkage,
            const DocComment &Comment, DeclarationFragments Declaration,
            DeclarationFragments SubHeading, bool IsFromSystemHeader,
            AccessControl Access = AccessControl())
      : USR(USR), Name(Name), Parent(std::move(Parent)), Location(Location),
        Availability(std::move(Availability)), Linkage(Linkage),
        Comment(Comment), Declaration(Declaration), SubHeading(SubHeading),
        IsFromSystemHeader(IsFromSystemHeader), Access(std::move(Access)),
        KindForDisplay(Kind), Kind(Kind) {}

  APIRecord(RecordKind Kind, StringRef USR, StringRef Name)
      : USR(USR), Name(Name), KindForDisplay(Kind), Kind(Kind) {}

  // Pure virtual destructor to make APIRecord abstract
  virtual ~APIRecord() = 0;
  static bool classof(const APIRecord *Record) { return true; }
  static bool classofKind(RecordKind K) { return true; }
  static bool classof(const RecordContext *Ctx) { return true; }
};

/// Base class used for specific record types that have children records this is
/// analogous to the DeclContext for the AST
class RecordContext {
public:
  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(APIRecord::RecordKind K) {
    return K > APIRecord::RK_FirstRecordContext &&
           K < APIRecord::RK_LastRecordContext;
  }

  static bool classof(const RecordContext *Context) { return true; }

  RecordContext(APIRecord::RecordKind Kind) : Kind(Kind) {}

  /// Append \p Other children chain into ours and empty out Other's record
  /// chain.
  void stealRecordChain(RecordContext &Other);

  APIRecord::RecordKind getKind() const { return Kind; }

  struct record_iterator {
  private:
    APIRecord *Current = nullptr;

  public:
    using value_type = APIRecord *;
    using reference = const value_type &;
    using pointer = const value_type *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    record_iterator() = default;
    explicit record_iterator(value_type R) : Current(R) {}
    reference operator*() const { return Current; }
    // This doesn't strictly meet the iterator requirements, but it's the
    // behavior we want here.
    value_type operator->() const { return Current; }
    record_iterator &operator++() {
      Current = Current->getNextInContext();
      return *this;
    }
    record_iterator operator++(int) {
      record_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(record_iterator x, record_iterator y) {
      return x.Current == y.Current;
    }
    friend bool operator!=(record_iterator x, record_iterator y) {
      return x.Current != y.Current;
    }
  };

  using record_range = llvm::iterator_range<record_iterator>;
  record_range records() const {
    return record_range(records_begin(), records_end());
  }
  record_iterator records_begin() const { return record_iterator(First); };
  record_iterator records_end() const { return record_iterator(); }
  bool records_empty() const { return First == nullptr; };

private:
  APIRecord::RecordKind Kind;
  mutable APIRecord *First = nullptr;
  mutable APIRecord *Last = nullptr;
  bool IsWellFormed() const;

protected:
  friend class APISet;
  void addToRecordChain(APIRecord *) const;
};

struct NamespaceRecord : APIRecord, RecordContext {
  NamespaceRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                  PresumedLoc Loc, AvailabilityInfo Availability,
                  LinkageInfo Linkage, const DocComment &Comment,
                  DeclarationFragments Declaration,
                  DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(RK_Namespace, USR, Name, Parent, Loc, std::move(Availability),
                  Linkage, Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        RecordContext(RK_Namespace) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_Namespace; }
};

/// This holds information associated with global functions.
struct GlobalFunctionRecord : APIRecord {
  FunctionSignature Signature;

  GlobalFunctionRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                       PresumedLoc Loc, AvailabilityInfo Availability,
                       LinkageInfo Linkage, const DocComment &Comment,
                       DeclarationFragments Declaration,
                       DeclarationFragments SubHeading,
                       FunctionSignature Signature, bool IsFromSystemHeader)
      : APIRecord(RK_GlobalFunction, USR, Name, Parent, Loc,
                  std::move(Availability), Linkage, Comment, Declaration,
                  SubHeading, IsFromSystemHeader),
        Signature(Signature) {}

  GlobalFunctionRecord(RecordKind Kind, StringRef USR, StringRef Name,
                       SymbolReference Parent, PresumedLoc Loc,
                       AvailabilityInfo Availability, LinkageInfo Linkage,
                       const DocComment &Comment,
                       DeclarationFragments Declaration,
                       DeclarationFragments SubHeading,
                       FunctionSignature Signature, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  Linkage, Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        Signature(Signature) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_GlobalFunction; }

private:
  virtual void anchor();
};

struct GlobalFunctionTemplateRecord : GlobalFunctionRecord {
  Template Templ;

  GlobalFunctionTemplateRecord(StringRef USR, StringRef Name,
                               SymbolReference Parent, PresumedLoc Loc,
                               AvailabilityInfo Availability,
                               LinkageInfo Linkage, const DocComment &Comment,
                               DeclarationFragments Declaration,
                               DeclarationFragments SubHeading,
                               FunctionSignature Signature, Template Template,
                               bool IsFromSystemHeader)
      : GlobalFunctionRecord(RK_GlobalFunctionTemplate, USR, Name, Parent, Loc,
                             std::move(Availability), Linkage, Comment,
                             Declaration, SubHeading, Signature,
                             IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalFunctionTemplate;
  }
};

struct GlobalFunctionTemplateSpecializationRecord : GlobalFunctionRecord {
  GlobalFunctionTemplateSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, LinkageInfo Linkage,
      const DocComment &Comment, DeclarationFragments Declaration,
      DeclarationFragments SubHeading, FunctionSignature Signature,
      bool IsFromSystemHeader)
      : GlobalFunctionRecord(RK_GlobalFunctionTemplateSpecialization, USR, Name,
                             Parent, Loc, std::move(Availability), Linkage,
                             Comment, Declaration, SubHeading, Signature,
                             IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalFunctionTemplateSpecialization;
  }
};

/// This holds information associated with global functions.
struct GlobalVariableRecord : APIRecord, RecordContext {
  GlobalVariableRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                       PresumedLoc Loc, AvailabilityInfo Availability,
                       LinkageInfo Linkage, const DocComment &Comment,
                       DeclarationFragments Declaration,
                       DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(RK_GlobalVariable, USR, Name, Parent, Loc,
                  std::move(Availability), Linkage, Comment, Declaration,
                  SubHeading, IsFromSystemHeader),
        RecordContext(RK_GlobalVariable) {}

  GlobalVariableRecord(RecordKind Kind, StringRef USR, StringRef Name,
                       SymbolReference Parent, PresumedLoc Loc,
                       AvailabilityInfo Availability, LinkageInfo Linkage,
                       const DocComment &Comment,
                       DeclarationFragments Declaration,
                       DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  Linkage, Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        RecordContext(Kind) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalVariable || K == RK_GlobalVariableTemplate ||
           K == RK_GlobalVariableTemplateSpecialization ||
           K == RK_GlobalVariableTemplatePartialSpecialization;
  }

private:
  virtual void anchor();
};

struct GlobalVariableTemplateRecord : GlobalVariableRecord {
  Template Templ;

  GlobalVariableTemplateRecord(StringRef USR, StringRef Name,
                               SymbolReference Parent, PresumedLoc Loc,
                               AvailabilityInfo Availability,
                               LinkageInfo Linkage, const DocComment &Comment,
                               DeclarationFragments Declaration,
                               DeclarationFragments SubHeading,
                               class Template Template, bool IsFromSystemHeader)
      : GlobalVariableRecord(RK_GlobalVariableTemplate, USR, Name, Parent, Loc,
                             std::move(Availability), Linkage, Comment,
                             Declaration, SubHeading, IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalVariableTemplate;
  }
};

struct GlobalVariableTemplateSpecializationRecord : GlobalVariableRecord {
  GlobalVariableTemplateSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, LinkageInfo Linkage,
      const DocComment &Comment, DeclarationFragments Declaration,
      DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : GlobalVariableRecord(RK_GlobalVariableTemplateSpecialization, USR, Name,
                             Parent, Loc, std::move(Availability), Linkage,
                             Comment, Declaration, SubHeading,
                             IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalVariableTemplateSpecialization;
  }
};

struct GlobalVariableTemplatePartialSpecializationRecord
    : GlobalVariableRecord {
  Template Templ;

  GlobalVariableTemplatePartialSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, LinkageInfo Linkage,
      const DocComment &Comment, DeclarationFragments Declaration,
      DeclarationFragments SubHeading, class Template Template,
      bool IsFromSystemHeader)
      : GlobalVariableRecord(RK_GlobalVariableTemplatePartialSpecialization,
                             USR, Name, Parent, Loc, std::move(Availability),
                             Linkage, Comment, Declaration, SubHeading,
                             IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_GlobalVariableTemplatePartialSpecialization;
  }
};

/// This holds information associated with enum constants.
struct EnumConstantRecord : APIRecord {
  EnumConstantRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                     PresumedLoc Loc, AvailabilityInfo Availability,
                     const DocComment &Comment,
                     DeclarationFragments Declaration,
                     DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(RK_EnumConstant, USR, Name, Parent, Loc,
                  std::move(Availability), LinkageInfo::none(), Comment,
                  Declaration, SubHeading, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_EnumConstant; }

private:
  virtual void anchor();
};

struct TagRecord : APIRecord, RecordContext {
  TagRecord(RecordKind Kind, StringRef USR, StringRef Name,
            SymbolReference Parent, PresumedLoc Loc,
            AvailabilityInfo Availability, const DocComment &Comment,
            DeclarationFragments Declaration, DeclarationFragments SubHeading,
            bool IsFromSystemHeader, bool IsEmbeddedInVarDeclarator,
            AccessControl Access = AccessControl())
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader, std::move(Access)),
        RecordContext(Kind),
        IsEmbeddedInVarDeclarator(IsEmbeddedInVarDeclarator){};

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_Struct || K == RK_Union || K == RK_Enum;
  }

  bool IsEmbeddedInVarDeclarator;

  virtual ~TagRecord() = 0;
};

/// This holds information associated with enums.
struct EnumRecord : TagRecord {
  EnumRecord(StringRef USR, StringRef Name, SymbolReference Parent,
             PresumedLoc Loc, AvailabilityInfo Availability,
             const DocComment &Comment, DeclarationFragments Declaration,
             DeclarationFragments SubHeading, bool IsFromSystemHeader,
             bool IsEmbeddedInVarDeclarator,
             AccessControl Access = AccessControl())
      : TagRecord(RK_Enum, USR, Name, Parent, Loc, std::move(Availability),
                  Comment, Declaration, SubHeading, IsFromSystemHeader,
                  IsEmbeddedInVarDeclarator, std::move(Access)) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }

  static bool classofKind(RecordKind K) { return K == RK_Enum; }

private:
  virtual void anchor();
};

/// This holds information associated with struct or union fields fields.
struct RecordFieldRecord : APIRecord, RecordContext {
  RecordFieldRecord(RecordKind Kind, StringRef USR, StringRef Name,
                    SymbolReference Parent, PresumedLoc Loc,
                    AvailabilityInfo Availability, const DocComment &Comment,
                    DeclarationFragments Declaration,
                    DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        RecordContext(Kind) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_StructField || K == RK_UnionField;
  }

  virtual ~RecordFieldRecord() = 0;
};

/// This holds information associated with structs and unions.
struct RecordRecord : TagRecord {
  RecordRecord(RecordKind Kind, StringRef USR, StringRef Name,
               SymbolReference Parent, PresumedLoc Loc,
               AvailabilityInfo Availability, const DocComment &Comment,
               DeclarationFragments Declaration,
               DeclarationFragments SubHeading, bool IsFromSystemHeader,
               bool IsEmbeddedInVarDeclarator,
               AccessControl Access = AccessControl())
      : TagRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  Comment, Declaration, SubHeading, IsFromSystemHeader,
                  IsEmbeddedInVarDeclarator, std::move(Access)) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_Struct || K == RK_Union;
  }

  bool isAnonymousWithNoTypedef() { return Name.empty(); }

  virtual ~RecordRecord() = 0;
};

struct StructFieldRecord : RecordFieldRecord {
  StructFieldRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                    PresumedLoc Loc, AvailabilityInfo Availability,
                    const DocComment &Comment, DeclarationFragments Declaration,
                    DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : RecordFieldRecord(RK_StructField, USR, Name, Parent, Loc,
                          std::move(Availability), Comment, Declaration,
                          SubHeading, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_StructField; }

private:
  virtual void anchor();
};

struct StructRecord : RecordRecord {
  StructRecord(StringRef USR, StringRef Name, SymbolReference Parent,
               PresumedLoc Loc, AvailabilityInfo Availability,
               const DocComment &Comment, DeclarationFragments Declaration,
               DeclarationFragments SubHeading, bool IsFromSystemHeader,
               bool IsEmbeddedInVarDeclarator)
      : RecordRecord(RK_Struct, USR, Name, Parent, Loc, std::move(Availability),
                     Comment, Declaration, SubHeading, IsFromSystemHeader,
                     IsEmbeddedInVarDeclarator) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_Struct; }

private:
  virtual void anchor();
};

struct UnionFieldRecord : RecordFieldRecord {
  UnionFieldRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                   PresumedLoc Loc, AvailabilityInfo Availability,
                   const DocComment &Comment, DeclarationFragments Declaration,
                   DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : RecordFieldRecord(RK_UnionField, USR, Name, Parent, Loc,
                          std::move(Availability), Comment, Declaration,
                          SubHeading, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_UnionField; }

private:
  virtual void anchor();
};

struct UnionRecord : RecordRecord {
  UnionRecord(StringRef USR, StringRef Name, SymbolReference Parent,
              PresumedLoc Loc, AvailabilityInfo Availability,
              const DocComment &Comment, DeclarationFragments Declaration,
              DeclarationFragments SubHeading, bool IsFromSystemHeader,
              bool IsEmbeddedInVarDeclarator)
      : RecordRecord(RK_Union, USR, Name, Parent, Loc, std::move(Availability),
                     Comment, Declaration, SubHeading, IsFromSystemHeader,
                     IsEmbeddedInVarDeclarator) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_Union; }

private:
  virtual void anchor();
};

struct CXXFieldRecord : APIRecord, RecordContext {
  CXXFieldRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                 PresumedLoc Loc, AvailabilityInfo Availability,
                 const DocComment &Comment, DeclarationFragments Declaration,
                 DeclarationFragments SubHeading, AccessControl Access,
                 bool IsFromSystemHeader)
      : APIRecord(RK_CXXField, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader, std::move(Access)),
        RecordContext(RK_CXXField) {}

  CXXFieldRecord(RecordKind Kind, StringRef USR, StringRef Name,
                 SymbolReference Parent, PresumedLoc Loc,
                 AvailabilityInfo Availability, const DocComment &Comment,
                 DeclarationFragments Declaration,
                 DeclarationFragments SubHeading, AccessControl Access,
                 bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader, std::move(Access)),
        RecordContext(Kind) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_CXXField || K == RK_CXXFieldTemplate || K == RK_StaticField;
  }

private:
  virtual void anchor();
};

struct CXXFieldTemplateRecord : CXXFieldRecord {
  Template Templ;

  CXXFieldTemplateRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                         PresumedLoc Loc, AvailabilityInfo Availability,
                         const DocComment &Comment,
                         DeclarationFragments Declaration,
                         DeclarationFragments SubHeading, AccessControl Access,
                         Template Template, bool IsFromSystemHeader)
      : CXXFieldRecord(RK_CXXFieldTemplate, USR, Name, Parent, Loc,
                       std::move(Availability), Comment, Declaration,
                       SubHeading, std::move(Access), IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXFieldTemplate; }
};

struct CXXMethodRecord : APIRecord {
  FunctionSignature Signature;

  CXXMethodRecord() = delete;

  CXXMethodRecord(RecordKind Kind, StringRef USR, StringRef Name,
                  SymbolReference Parent, PresumedLoc Loc,
                  AvailabilityInfo Availability, const DocComment &Comment,
                  DeclarationFragments Declaration,
                  DeclarationFragments SubHeading, FunctionSignature Signature,
                  AccessControl Access, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader, std::move(Access)),
        Signature(Signature) {}

  virtual ~CXXMethodRecord() = 0;
};

struct CXXConstructorRecord : CXXMethodRecord {
  CXXConstructorRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                       PresumedLoc Loc, AvailabilityInfo Availability,
                       const DocComment &Comment,
                       DeclarationFragments Declaration,
                       DeclarationFragments SubHeading,
                       FunctionSignature Signature, AccessControl Access,
                       bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXConstructorMethod, USR, Name, Parent, Loc,
                        std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader) {}
  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXConstructorMethod; }

private:
  virtual void anchor();
};

struct CXXDestructorRecord : CXXMethodRecord {
  CXXDestructorRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                      PresumedLoc Loc, AvailabilityInfo Availability,
                      const DocComment &Comment,
                      DeclarationFragments Declaration,
                      DeclarationFragments SubHeading,
                      FunctionSignature Signature, AccessControl Access,
                      bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXDestructorMethod, USR, Name, Parent, Loc,
                        std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader) {}
  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXDestructorMethod; }

private:
  virtual void anchor();
};

struct CXXStaticMethodRecord : CXXMethodRecord {
  CXXStaticMethodRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                        PresumedLoc Loc, AvailabilityInfo Availability,
                        const DocComment &Comment,
                        DeclarationFragments Declaration,
                        DeclarationFragments SubHeading,
                        FunctionSignature Signature, AccessControl Access,
                        bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXStaticMethod, USR, Name, Parent, Loc,
                        std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader) {}
  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXStaticMethod; }

private:
  virtual void anchor();
};

struct CXXInstanceMethodRecord : CXXMethodRecord {
  CXXInstanceMethodRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                          PresumedLoc Loc, AvailabilityInfo Availability,
                          const DocComment &Comment,
                          DeclarationFragments Declaration,
                          DeclarationFragments SubHeading,
                          FunctionSignature Signature, AccessControl Access,
                          bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXInstanceMethod, USR, Name, Parent, Loc,
                        std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXInstanceMethod; }

private:
  virtual void anchor();
};

struct CXXMethodTemplateRecord : CXXMethodRecord {
  Template Templ;

  CXXMethodTemplateRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                          PresumedLoc Loc, AvailabilityInfo Availability,
                          const DocComment &Comment,
                          DeclarationFragments Declaration,
                          DeclarationFragments SubHeading,
                          FunctionSignature Signature, AccessControl Access,
                          Template Template, bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXMethodTemplate, USR, Name, Parent, Loc,
                        std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_CXXMethodTemplate; }
};

struct CXXMethodTemplateSpecializationRecord : CXXMethodRecord {
  CXXMethodTemplateSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, const DocComment &Comment,
      DeclarationFragments Declaration, DeclarationFragments SubHeading,
      FunctionSignature Signature, AccessControl Access,
      bool IsFromSystemHeader)
      : CXXMethodRecord(RK_CXXMethodTemplateSpecialization, USR, Name, Parent,
                        Loc, std::move(Availability), Comment, Declaration,
                        SubHeading, Signature, std::move(Access),
                        IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_CXXMethodTemplateSpecialization;
  }
};

/// This holds information associated with Objective-C properties.
struct ObjCPropertyRecord : APIRecord {
  /// The attributes associated with an Objective-C property.
  enum AttributeKind : unsigned {
    NoAttr = 0,
    ReadOnly = 1,
    Dynamic = 1 << 2,
  };

  AttributeKind Attributes;
  StringRef GetterName;
  StringRef SetterName;
  bool IsOptional;

  ObjCPropertyRecord(RecordKind Kind, StringRef USR, StringRef Name,
                     SymbolReference Parent, PresumedLoc Loc,
                     AvailabilityInfo Availability, const DocComment &Comment,
                     DeclarationFragments Declaration,
                     DeclarationFragments SubHeading, AttributeKind Attributes,
                     StringRef GetterName, StringRef SetterName,
                     bool IsOptional, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        Attributes(Attributes), GetterName(GetterName), SetterName(SetterName),
        IsOptional(IsOptional) {}

  bool isReadOnly() const { return Attributes & ReadOnly; }
  bool isDynamic() const { return Attributes & Dynamic; }

  virtual ~ObjCPropertyRecord() = 0;
};

struct ObjCInstancePropertyRecord : ObjCPropertyRecord {
  ObjCInstancePropertyRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, const DocComment &Comment,
      DeclarationFragments Declaration, DeclarationFragments SubHeading,
      AttributeKind Attributes, StringRef GetterName, StringRef SetterName,
      bool IsOptional, bool IsFromSystemHeader)
      : ObjCPropertyRecord(RK_ObjCInstanceProperty, USR, Name, Parent, Loc,
                           std::move(Availability), Comment, Declaration,
                           SubHeading, Attributes, GetterName, SetterName,
                           IsOptional, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCInstanceProperty; }

private:
  virtual void anchor();
};

struct ObjCClassPropertyRecord : ObjCPropertyRecord {
  ObjCClassPropertyRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                          PresumedLoc Loc, AvailabilityInfo Availability,
                          const DocComment &Comment,
                          DeclarationFragments Declaration,
                          DeclarationFragments SubHeading,
                          AttributeKind Attributes, StringRef GetterName,
                          StringRef SetterName, bool IsOptional,
                          bool IsFromSystemHeader)
      : ObjCPropertyRecord(RK_ObjCClassProperty, USR, Name, Parent, Loc,
                           std::move(Availability), Comment, Declaration,
                           SubHeading, Attributes, GetterName, SetterName,
                           IsOptional, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCClassProperty; }

private:
  virtual void anchor();
};

/// This holds information associated with Objective-C instance variables.
struct ObjCInstanceVariableRecord : APIRecord {
  ObjCInstanceVariableRecord(StringRef USR, StringRef Name,
                             SymbolReference Parent, PresumedLoc Loc,
                             AvailabilityInfo Availability,
                             const DocComment &Comment,
                             DeclarationFragments Declaration,
                             DeclarationFragments SubHeading,
                             bool IsFromSystemHeader)
      : APIRecord(RK_ObjCIvar, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCIvar; }

private:
  virtual void anchor();
};

/// This holds information associated with Objective-C methods.
struct ObjCMethodRecord : APIRecord {
  FunctionSignature Signature;

  ObjCMethodRecord() = delete;

  ObjCMethodRecord(RecordKind Kind, StringRef USR, StringRef Name,
                   SymbolReference Parent, PresumedLoc Loc,
                   AvailabilityInfo Availability, const DocComment &Comment,
                   DeclarationFragments Declaration,
                   DeclarationFragments SubHeading, FunctionSignature Signature,
                   bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        Signature(Signature) {}

  virtual ~ObjCMethodRecord() = 0;
};

struct ObjCInstanceMethodRecord : ObjCMethodRecord {
  ObjCInstanceMethodRecord(StringRef USR, StringRef Name,
                           SymbolReference Parent, PresumedLoc Loc,
                           AvailabilityInfo Availability,
                           const DocComment &Comment,
                           DeclarationFragments Declaration,
                           DeclarationFragments SubHeading,
                           FunctionSignature Signature, bool IsFromSystemHeader)
      : ObjCMethodRecord(RK_ObjCInstanceMethod, USR, Name, Parent, Loc,
                         std::move(Availability), Comment, Declaration,
                         SubHeading, Signature, IsFromSystemHeader) {}
  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCInstanceMethod; }

private:
  virtual void anchor();
};

struct ObjCClassMethodRecord : ObjCMethodRecord {
  ObjCClassMethodRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                        PresumedLoc Loc, AvailabilityInfo Availability,
                        const DocComment &Comment,
                        DeclarationFragments Declaration,
                        DeclarationFragments SubHeading,
                        FunctionSignature Signature, bool IsFromSystemHeader)
      : ObjCMethodRecord(RK_ObjCClassMethod, USR, Name, Parent, Loc,
                         std::move(Availability), Comment, Declaration,
                         SubHeading, Signature, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCClassMethod; }

private:
  virtual void anchor();
};

struct StaticFieldRecord : CXXFieldRecord {
  StaticFieldRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                    PresumedLoc Loc, AvailabilityInfo Availability,
                    LinkageInfo Linkage, const DocComment &Comment,
                    DeclarationFragments Declaration,
                    DeclarationFragments SubHeading, AccessControl Access,
                    bool IsFromSystemHeader)
      : CXXFieldRecord(RK_StaticField, USR, Name, Parent, Loc,
                       std::move(Availability), Comment, Declaration,
                       SubHeading, std::move(Access), IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_StaticField; }
};

/// The base representation of an Objective-C container record. Holds common
/// information associated with Objective-C containers.
struct ObjCContainerRecord : APIRecord, RecordContext {
  SmallVector<SymbolReference> Protocols;

  ObjCContainerRecord() = delete;

  ObjCContainerRecord(RecordKind Kind, StringRef USR, StringRef Name,
                      SymbolReference Parent, PresumedLoc Loc,
                      AvailabilityInfo Availability, LinkageInfo Linkage,
                      const DocComment &Comment,
                      DeclarationFragments Declaration,
                      DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : APIRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                  Linkage, Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        RecordContext(Kind) {}

  virtual ~ObjCContainerRecord() = 0;
};

struct CXXClassRecord : RecordRecord {
  SmallVector<SymbolReference> Bases;

  CXXClassRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                 PresumedLoc Loc, AvailabilityInfo Availability,
                 const DocComment &Comment, DeclarationFragments Declaration,
                 DeclarationFragments SubHeading, RecordKind Kind,
                 AccessControl Access, bool IsFromSystemHeader,
                 bool IsEmbeddedInVarDeclarator = false)
      : RecordRecord(Kind, USR, Name, Parent, Loc, std::move(Availability),
                     Comment, Declaration, SubHeading, IsFromSystemHeader,
                     IsEmbeddedInVarDeclarator, std::move(Access)) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_CXXClass || K == RK_ClassTemplate ||
           K == RK_ClassTemplateSpecialization ||
           K == RK_ClassTemplatePartialSpecialization;
  }

private:
  virtual void anchor();
};

struct ClassTemplateRecord : CXXClassRecord {
  Template Templ;

  ClassTemplateRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                      PresumedLoc Loc, AvailabilityInfo Availability,
                      const DocComment &Comment,
                      DeclarationFragments Declaration,
                      DeclarationFragments SubHeading, Template Template,
                      AccessControl Access, bool IsFromSystemHeader)
      : CXXClassRecord(USR, Name, Parent, Loc, std::move(Availability), Comment,
                       Declaration, SubHeading, RK_ClassTemplate,
                       std::move(Access), IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ClassTemplate; }
};

struct ClassTemplateSpecializationRecord : CXXClassRecord {
  ClassTemplateSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, const DocComment &Comment,
      DeclarationFragments Declaration, DeclarationFragments SubHeading,
      AccessControl Access, bool IsFromSystemHeader)
      : CXXClassRecord(USR, Name, Parent, Loc, std::move(Availability), Comment,
                       Declaration, SubHeading, RK_ClassTemplateSpecialization,
                       Access, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_ClassTemplateSpecialization;
  }
};

struct ClassTemplatePartialSpecializationRecord : CXXClassRecord {
  Template Templ;
  ClassTemplatePartialSpecializationRecord(
      StringRef USR, StringRef Name, SymbolReference Parent, PresumedLoc Loc,
      AvailabilityInfo Availability, const DocComment &Comment,
      DeclarationFragments Declaration, DeclarationFragments SubHeading,
      Template Template, AccessControl Access, bool IsFromSystemHeader)
      : CXXClassRecord(USR, Name, Parent, Loc, std::move(Availability), Comment,
                       Declaration, SubHeading,
                       RK_ClassTemplatePartialSpecialization, Access,
                       IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) {
    return K == RK_ClassTemplatePartialSpecialization;
  }
};

struct ConceptRecord : APIRecord {
  Template Templ;

  ConceptRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                PresumedLoc Loc, AvailabilityInfo Availability,
                const DocComment &Comment, DeclarationFragments Declaration,
                DeclarationFragments SubHeading, Template Template,
                bool IsFromSystemHeader)
      : APIRecord(RK_Concept, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo::none(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        Templ(Template) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_Concept; }
};

/// This holds information associated with Objective-C categories.
struct ObjCCategoryRecord : ObjCContainerRecord {
  SymbolReference Interface;

  ObjCCategoryRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                     PresumedLoc Loc, AvailabilityInfo Availability,
                     const DocComment &Comment,
                     DeclarationFragments Declaration,
                     DeclarationFragments SubHeading, SymbolReference Interface,
                     bool IsFromSystemHeader)
      : ObjCContainerRecord(RK_ObjCCategory, USR, Name, Parent, Loc,
                            std::move(Availability), LinkageInfo::none(),
                            Comment, Declaration, SubHeading,
                            IsFromSystemHeader),
        Interface(Interface) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCCategory; }

  bool isExtendingExternalModule() const { return !Interface.Source.empty(); }

  std::optional<StringRef> getExtendedExternalModule() const {
    if (!isExtendingExternalModule())
      return {};
    return Interface.Source;
  }

private:
  virtual void anchor();
};

/// This holds information associated with Objective-C interfaces/classes.
struct ObjCInterfaceRecord : ObjCContainerRecord {
  SymbolReference SuperClass;

  ObjCInterfaceRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                      PresumedLoc Loc, AvailabilityInfo Availability,
                      LinkageInfo Linkage, const DocComment &Comment,
                      DeclarationFragments Declaration,
                      DeclarationFragments SubHeading,
                      SymbolReference SuperClass, bool IsFromSystemHeader)
      : ObjCContainerRecord(RK_ObjCInterface, USR, Name, Parent, Loc,
                            std::move(Availability), Linkage, Comment,
                            Declaration, SubHeading, IsFromSystemHeader),
        SuperClass(SuperClass) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCInterface; }

private:
  virtual void anchor();
};

/// This holds information associated with Objective-C protocols.
struct ObjCProtocolRecord : ObjCContainerRecord {
  ObjCProtocolRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                     PresumedLoc Loc, AvailabilityInfo Availability,
                     const DocComment &Comment,
                     DeclarationFragments Declaration,
                     DeclarationFragments SubHeading, bool IsFromSystemHeader)
      : ObjCContainerRecord(RK_ObjCProtocol, USR, Name, Parent, Loc,
                            std::move(Availability), LinkageInfo::none(),
                            Comment, Declaration, SubHeading,
                            IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_ObjCProtocol; }

private:
  virtual void anchor();
};

/// This holds information associated with macro definitions.
struct MacroDefinitionRecord : APIRecord {
  MacroDefinitionRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                        PresumedLoc Loc, DeclarationFragments Declaration,
                        DeclarationFragments SubHeading,
                        bool IsFromSystemHeader)
      : APIRecord(RK_MacroDefinition, USR, Name, Parent, Loc,
                  AvailabilityInfo(), LinkageInfo(), {}, Declaration,
                  SubHeading, IsFromSystemHeader) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_MacroDefinition; }

private:
  virtual void anchor();
};

/// This holds information associated with typedefs.
///
/// Note: Typedefs for anonymous enums and structs typically don't get emitted
/// by the serializers but still get a TypedefRecord. Instead we use the
/// typedef name as a name for the underlying anonymous struct or enum.
struct TypedefRecord : APIRecord {
  SymbolReference UnderlyingType;

  TypedefRecord(StringRef USR, StringRef Name, SymbolReference Parent,
                PresumedLoc Loc, AvailabilityInfo Availability,
                const DocComment &Comment, DeclarationFragments Declaration,
                DeclarationFragments SubHeading, SymbolReference UnderlyingType,
                bool IsFromSystemHeader)
      : APIRecord(RK_Typedef, USR, Name, Parent, Loc, std::move(Availability),
                  LinkageInfo(), Comment, Declaration, SubHeading,
                  IsFromSystemHeader),
        UnderlyingType(UnderlyingType) {}

  static bool classof(const APIRecord *Record) {
    return classofKind(Record->getKind());
  }
  static bool classofKind(RecordKind K) { return K == RK_Typedef; }

private:
  virtual void anchor();
};

/// APISet holds the set of API records collected from given inputs.
class APISet {
public:
  /// Get the target triple for the ExtractAPI invocation.
  const llvm::Triple &getTarget() const { return Target; }

  /// Get the language used by the APIs.
  Language getLanguage() const { return Lang; }

  /// Finds the APIRecord for a given USR.
  ///
  /// \returns a pointer to the APIRecord associated with that USR or nullptr.
  APIRecord *findRecordForUSR(StringRef USR) const;

  /// Copy \p String into the Allocator in this APISet.
  ///
  /// \returns a StringRef of the copied string in APISet::Allocator.
  StringRef copyString(StringRef String);

  SymbolReference createSymbolReference(StringRef Name, StringRef USR,
                                        StringRef Source = "");

  /// Create a subclass of \p APIRecord and store it in the APISet.
  ///
  /// \returns A pointer to the created record or the already existing record
  /// matching this USR.
  template <typename RecordTy, typename... CtorArgsContTy>
  typename std::enable_if_t<std::is_base_of_v<APIRecord, RecordTy>, RecordTy> *
  createRecord(StringRef USR, StringRef Name, CtorArgsContTy &&...CtorArgs);

  ArrayRef<const APIRecord *> getTopLevelRecords() const {
    return TopLevelRecords;
  }

  APISet(const llvm::Triple &Target, Language Lang,
         const std::string &ProductName)
      : Target(Target), Lang(Lang), ProductName(ProductName) {}

  // Prevent moves and copies
  APISet(const APISet &Other) = delete;
  APISet &operator=(const APISet &Other) = delete;
  APISet(APISet &&Other) = delete;
  APISet &operator=(APISet &&Other) = delete;

private:
  /// BumpPtrAllocator that serves as the memory arena for the allocated objects
  llvm::BumpPtrAllocator Allocator;

  const llvm::Triple Target;
  const Language Lang;

  struct APIRecordDeleter {
    void operator()(APIRecord *Record) { Record->~APIRecord(); }
  };

  // Ensure that the destructor of each record is called when the LookupTable is
  // destroyed without calling delete operator as the memory for the record
  // lives in the BumpPtrAllocator.
  using APIRecordStoredPtr = std::unique_ptr<APIRecord, APIRecordDeleter>;
  llvm::DenseMap<StringRef, APIRecordStoredPtr> USRBasedLookupTable;
  std::vector<const APIRecord *> TopLevelRecords;

public:
  const std::string ProductName;
};

template <typename RecordTy, typename... CtorArgsContTy>
typename std::enable_if_t<std::is_base_of_v<APIRecord, RecordTy>, RecordTy> *
APISet::createRecord(StringRef USR, StringRef Name,
                     CtorArgsContTy &&...CtorArgs) {
  // Ensure USR refers to a String stored in the allocator.
  auto USRString = copyString(USR);
  auto Result = USRBasedLookupTable.insert({USRString, nullptr});
  RecordTy *Record;

  // Create the record if it does not already exist
  if (Result.second) {
    Record = new (Allocator) RecordTy(
        USRString, copyString(Name), std::forward<CtorArgsContTy>(CtorArgs)...);
    // Store the record in the record lookup map
    Result.first->second = APIRecordStoredPtr(Record);

    if (auto *ParentContext =
            dyn_cast_if_present<RecordContext>(Record->Parent.Record))
      ParentContext->addToRecordChain(Record);
    else
      TopLevelRecords.push_back(Record);
  } else {
    Record = dyn_cast<RecordTy>(Result.first->second.get());
  }

  return Record;
}

// Helper type for implementing casting to RecordContext pointers.
// Selected when FromTy not a known subclass of RecordContext.
template <typename FromTy,
          bool IsKnownSubType = std::is_base_of_v<RecordContext, FromTy>>
struct ToRecordContextCastInfoWrapper {
  static_assert(std::is_base_of_v<APIRecord, FromTy>,
                "Can only cast APIRecord and derived classes to RecordContext");

  static bool isPossible(FromTy *From) { return RecordContext::classof(From); }

  static RecordContext *doCast(FromTy *From) {
    return APIRecord::castToRecordContext(From);
  }
};

// Selected when FromTy is a known subclass of RecordContext.
template <typename FromTy> struct ToRecordContextCastInfoWrapper<FromTy, true> {
  static_assert(std::is_base_of_v<APIRecord, FromTy>,
                "Can only cast APIRecord and derived classes to RecordContext");
  static bool isPossible(const FromTy *From) { return true; }
  static RecordContext *doCast(FromTy *From) {
    return static_cast<RecordContext *>(From);
  }
};

// Helper type for implementing casting to RecordContext pointers.
// Selected when ToTy isn't a known subclass of RecordContext
template <typename ToTy,
          bool IsKnownSubType = std::is_base_of_v<RecordContext, ToTy>>
struct FromRecordContextCastInfoWrapper {
  static_assert(
      std::is_base_of_v<APIRecord, ToTy>,
      "Can only class RecordContext to APIRecord and derived classes");

  static bool isPossible(RecordContext *Ctx) {
    return ToTy::classofKind(Ctx->getKind());
  }

  static ToTy *doCast(RecordContext *Ctx) {
    return APIRecord::castFromRecordContext(Ctx);
  }
};

// Selected when ToTy is a known subclass of RecordContext.
template <typename ToTy> struct FromRecordContextCastInfoWrapper<ToTy, true> {
  static_assert(
      std::is_base_of_v<APIRecord, ToTy>,
      "Can only class RecordContext to APIRecord and derived classes");
  static bool isPossible(RecordContext *Ctx) {
    return ToTy::classof(Ctx->getKind());
  }
  static RecordContext *doCast(RecordContext *Ctx) {
    return static_cast<ToTy *>(Ctx);
  }
};

} // namespace extractapi
} // namespace clang

// Implement APIRecord (and derived classes) to and from RecordContext
// conversions
namespace llvm {

template <typename FromTy>
struct CastInfo<::clang::extractapi::RecordContext, FromTy *>
    : public NullableValueCastFailed<::clang::extractapi::RecordContext *>,
      public DefaultDoCastIfPossible<
          ::clang::extractapi::RecordContext *, FromTy *,
          CastInfo<::clang::extractapi::RecordContext, FromTy *>> {
  static inline bool isPossible(FromTy *From) {
    return ::clang::extractapi::ToRecordContextCastInfoWrapper<
        FromTy>::isPossible(From);
  }

  static inline ::clang::extractapi::RecordContext *doCast(FromTy *From) {
    return ::clang::extractapi::ToRecordContextCastInfoWrapper<FromTy>::doCast(
        From);
  }
};

template <typename FromTy>
struct CastInfo<::clang::extractapi::RecordContext, const FromTy *>
    : public ConstStrippingForwardingCast<
          ::clang::extractapi::RecordContext, const FromTy *,
          CastInfo<::clang::extractapi::RecordContext, FromTy *>> {};

template <typename ToTy>
struct CastInfo<ToTy, ::clang::extractapi::RecordContext *>
    : public NullableValueCastFailed<ToTy *>,
      public DefaultDoCastIfPossible<
          ToTy *, ::clang::extractapi::RecordContext *,
          CastInfo<ToTy, ::clang::extractapi::RecordContext *>> {
  static inline bool isPossible(::clang::extractapi::RecordContext *Ctx) {
    return ::clang::extractapi::FromRecordContextCastInfoWrapper<
        ToTy>::isPossible(Ctx);
  }

  static inline ToTy *doCast(::clang::extractapi::RecordContext *Ctx) {
    return ::clang::extractapi::FromRecordContextCastInfoWrapper<ToTy>::doCast(
        Ctx);
  }
};

template <typename ToTy>
struct CastInfo<ToTy, const ::clang::extractapi::RecordContext *>
    : public ConstStrippingForwardingCast<
          ToTy, const ::clang::extractapi::RecordContext *,
          CastInfo<ToTy, ::clang::extractapi::RecordContext *>> {};

} // namespace llvm

#endif // LLVM_CLANG_EXTRACTAPI_API_H
