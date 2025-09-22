#ifndef LLVM_TABLEGEN_DIRECTIVEEMITTER_H
#define LLVM_TABLEGEN_DIRECTIVEEMITTER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <string>
#include <vector>

namespace llvm {

// Wrapper class that contains DirectiveLanguage's information defined in
// DirectiveBase.td and provides helper methods for accessing it.
class DirectiveLanguage {
public:
  explicit DirectiveLanguage(const llvm::RecordKeeper &Records)
      : Records(Records) {
    const auto &DirectiveLanguages = getDirectiveLanguages();
    Def = DirectiveLanguages[0];
  }

  StringRef getName() const { return Def->getValueAsString("name"); }

  StringRef getCppNamespace() const {
    return Def->getValueAsString("cppNamespace");
  }

  StringRef getDirectivePrefix() const {
    return Def->getValueAsString("directivePrefix");
  }

  StringRef getClausePrefix() const {
    return Def->getValueAsString("clausePrefix");
  }

  StringRef getClauseEnumSetClass() const {
    return Def->getValueAsString("clauseEnumSetClass");
  }

  StringRef getFlangClauseBaseClass() const {
    return Def->getValueAsString("flangClauseBaseClass");
  }

  bool hasMakeEnumAvailableInNamespace() const {
    return Def->getValueAsBit("makeEnumAvailableInNamespace");
  }

  bool hasEnableBitmaskEnumInNamespace() const {
    return Def->getValueAsBit("enableBitmaskEnumInNamespace");
  }

  std::vector<Record *> getAssociations() const {
    return Records.getAllDerivedDefinitions("Association");
  }

  std::vector<Record *> getCategories() const {
    return Records.getAllDerivedDefinitions("Category");
  }

  std::vector<Record *> getDirectives() const {
    return Records.getAllDerivedDefinitions("Directive");
  }

  std::vector<Record *> getClauses() const {
    return Records.getAllDerivedDefinitions("Clause");
  }

  bool HasValidityErrors() const;

private:
  const llvm::Record *Def;
  const llvm::RecordKeeper &Records;

  std::vector<Record *> getDirectiveLanguages() const {
    return Records.getAllDerivedDefinitions("DirectiveLanguage");
  }
};

// Base record class used for Directive and Clause class defined in
// DirectiveBase.td.
class BaseRecord {
public:
  explicit BaseRecord(const llvm::Record *Def) : Def(Def) {}

  StringRef getName() const { return Def->getValueAsString("name"); }

  StringRef getAlternativeName() const {
    return Def->getValueAsString("alternativeName");
  }

  // Returns the name of the directive formatted for output. Whitespace are
  // replaced with underscores.
  std::string getFormattedName() {
    StringRef Name = Def->getValueAsString("name");
    std::string N = Name.str();
    std::replace(N.begin(), N.end(), ' ', '_');
    return N;
  }

  bool isDefault() const { return Def->getValueAsBit("isDefault"); }

  // Returns the record name.
  StringRef getRecordName() const { return Def->getName(); }

protected:
  const llvm::Record *Def;
};

// Wrapper class that contains a Directive's information defined in
// DirectiveBase.td and provides helper methods for accessing it.
class Directive : public BaseRecord {
public:
  explicit Directive(const llvm::Record *Def) : BaseRecord(Def) {}

  std::vector<Record *> getAllowedClauses() const {
    return Def->getValueAsListOfDefs("allowedClauses");
  }

  std::vector<Record *> getAllowedOnceClauses() const {
    return Def->getValueAsListOfDefs("allowedOnceClauses");
  }

  std::vector<Record *> getAllowedExclusiveClauses() const {
    return Def->getValueAsListOfDefs("allowedExclusiveClauses");
  }

  std::vector<Record *> getRequiredClauses() const {
    return Def->getValueAsListOfDefs("requiredClauses");
  }

  std::vector<Record *> getLeafConstructs() const {
    return Def->getValueAsListOfDefs("leafConstructs");
  }

  Record *getAssociation() const { return Def->getValueAsDef("association"); }

  Record *getCategory() const { return Def->getValueAsDef("category"); }
};

// Wrapper class that contains Clause's information defined in DirectiveBase.td
// and provides helper methods for accessing it.
class Clause : public BaseRecord {
public:
  explicit Clause(const llvm::Record *Def) : BaseRecord(Def) {}

  // Optional field.
  StringRef getClangClass() const {
    return Def->getValueAsString("clangClass");
  }

  // Optional field.
  StringRef getFlangClass() const {
    return Def->getValueAsString("flangClass");
  }

  // Get the formatted name for Flang parser class. The generic formatted class
  // name is constructed from the name were the first letter of each word is
  // captitalized and the underscores are removed.
  // ex: async -> Async
  //     num_threads -> NumThreads
  std::string getFormattedParserClassName() {
    StringRef Name = Def->getValueAsString("name");
    std::string N = Name.str();
    bool Cap = true;
    std::transform(N.begin(), N.end(), N.begin(), [&Cap](unsigned char C) {
      if (Cap == true) {
        C = llvm::toUpper(C);
        Cap = false;
      } else if (C == '_') {
        Cap = true;
      }
      return C;
    });
    llvm::erase(N, '_');
    return N;
  }

  // Optional field.
  StringRef getEnumName() const {
    return Def->getValueAsString("enumClauseValue");
  }

  std::vector<Record *> getClauseVals() const {
    return Def->getValueAsListOfDefs("allowedClauseValues");
  }

  bool isValueOptional() const { return Def->getValueAsBit("isValueOptional"); }

  bool isValueList() const { return Def->getValueAsBit("isValueList"); }

  StringRef getDefaultValue() const {
    return Def->getValueAsString("defaultValue");
  }

  bool isImplicit() const { return Def->getValueAsBit("isImplicit"); }

  std::vector<StringRef> getAliases() const {
    return Def->getValueAsListOfStrings("aliases");
  }

  StringRef getPrefix() const { return Def->getValueAsString("prefix"); }

  bool isPrefixOptional() const {
    return Def->getValueAsBit("isPrefixOptional");
  }
};

// Wrapper class that contains VersionedClause's information defined in
// DirectiveBase.td and provides helper methods for accessing it.
class VersionedClause {
public:
  explicit VersionedClause(const llvm::Record *Def) : Def(Def) {}

  // Return the specific clause record wrapped in the Clause class.
  Clause getClause() const { return Clause{Def->getValueAsDef("clause")}; }

  int64_t getMinVersion() const { return Def->getValueAsInt("minVersion"); }

  int64_t getMaxVersion() const { return Def->getValueAsInt("maxVersion"); }

private:
  const llvm::Record *Def;
};

class ClauseVal : public BaseRecord {
public:
  explicit ClauseVal(const llvm::Record *Def) : BaseRecord(Def) {}

  int getValue() const { return Def->getValueAsInt("value"); }

  bool isUserVisible() const { return Def->getValueAsBit("isUserValue"); }
};

} // namespace llvm

#endif
