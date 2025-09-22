//===-- LVScope.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVScope class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVRange.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Scope"

namespace {
const char *const KindArray = "Array";
const char *const KindBlock = "Block";
const char *const KindCallSite = "CallSite";
const char *const KindClass = "Class";
const char *const KindCompileUnit = "CompileUnit";
const char *const KindEnumeration = "Enumeration";
const char *const KindFile = "File";
const char *const KindFunction = "Function";
const char *const KindInlinedFunction = "InlinedFunction";
const char *const KindNamespace = "Namespace";
const char *const KindStruct = "Struct";
const char *const KindTemplateAlias = "TemplateAlias";
const char *const KindTemplatePack = "TemplatePack";
const char *const KindUndefined = "Undefined";
const char *const KindUnion = "Union";
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// DWARF lexical block, such as: namespace, function, compile unit, module, etc.
//===----------------------------------------------------------------------===//
// Return a string representation for the scope kind.
const char *LVScope::kind() const {
  const char *Kind = KindUndefined;
  if (getIsArray())
    Kind = KindArray;
  else if (getIsBlock())
    Kind = KindBlock;
  else if (getIsCallSite())
    Kind = KindCallSite;
  else if (getIsCompileUnit())
    Kind = KindCompileUnit;
  else if (getIsEnumeration())
    Kind = KindEnumeration;
  else if (getIsInlinedFunction())
    Kind = KindInlinedFunction;
  else if (getIsNamespace())
    Kind = KindNamespace;
  else if (getIsTemplatePack())
    Kind = KindTemplatePack;
  else if (getIsRoot())
    Kind = KindFile;
  else if (getIsTemplateAlias())
    Kind = KindTemplateAlias;
  else if (getIsClass())
    Kind = KindClass;
  else if (getIsFunction())
    Kind = KindFunction;
  else if (getIsStructure())
    Kind = KindStruct;
  else if (getIsUnion())
    Kind = KindUnion;
  return Kind;
}

LVScopeDispatch LVScope::Dispatch = {
    {LVScopeKind::IsAggregate, &LVScope::getIsAggregate},
    {LVScopeKind::IsArray, &LVScope::getIsArray},
    {LVScopeKind::IsBlock, &LVScope::getIsBlock},
    {LVScopeKind::IsCallSite, &LVScope::getIsCallSite},
    {LVScopeKind::IsCatchBlock, &LVScope::getIsCatchBlock},
    {LVScopeKind::IsClass, &LVScope::getIsClass},
    {LVScopeKind::IsCompileUnit, &LVScope::getIsCompileUnit},
    {LVScopeKind::IsEntryPoint, &LVScope::getIsEntryPoint},
    {LVScopeKind::IsEnumeration, &LVScope::getIsEnumeration},
    {LVScopeKind::IsFunction, &LVScope::getIsFunction},
    {LVScopeKind::IsFunctionType, &LVScope::getIsFunctionType},
    {LVScopeKind::IsInlinedFunction, &LVScope::getIsInlinedFunction},
    {LVScopeKind::IsLabel, &LVScope::getIsLabel},
    {LVScopeKind::IsLexicalBlock, &LVScope::getIsLexicalBlock},
    {LVScopeKind::IsNamespace, &LVScope::getIsNamespace},
    {LVScopeKind::IsRoot, &LVScope::getIsRoot},
    {LVScopeKind::IsStructure, &LVScope::getIsStructure},
    {LVScopeKind::IsTemplate, &LVScope::getIsTemplate},
    {LVScopeKind::IsTemplateAlias, &LVScope::getIsTemplateAlias},
    {LVScopeKind::IsTemplatePack, &LVScope::getIsTemplatePack},
    {LVScopeKind::IsTryBlock, &LVScope::getIsTryBlock},
    {LVScopeKind::IsUnion, &LVScope::getIsUnion}};

void LVScope::addToChildren(LVElement *Element) {
  if (!Children)
    Children = std::make_unique<LVElements>();
  Children->push_back(Element);
}

void LVScope::addElement(LVElement *Element) {
  assert(Element && "Invalid element.");
  if (Element->getIsType())
    addElement(static_cast<LVType *>(Element));
  else if (Element->getIsScope())
    addElement(static_cast<LVScope *>(Element));
  else if (Element->getIsSymbol())
    addElement(static_cast<LVSymbol *>(Element));
  else if (Element->getIsLine())
    addElement(static_cast<LVLine *>(Element));
  else
    llvm_unreachable("Invalid Element.");
}

// Adds the line info item to the ones stored in the scope.
void LVScope::addElement(LVLine *Line) {
  assert(Line && "Invalid line.");
  assert(!Line->getParent() && "Line already inserted");
  if (!Lines)
    Lines = std::make_unique<LVLines>();

  // Add it to parent.
  Lines->push_back(Line);
  Line->setParent(this);

  // Notify the reader about the new element being added.
  getReaderCompileUnit()->addedElement(Line);

  // All logical elements added to the children, are sorted by any of the
  // following criterias: offset, name, line number, kind.
  // Do not add the line records to the children, as they represent the
  // logical view for the text section and any sorting will not preserve
  // the original sequence.

  // Indicate that this tree branch has lines.
  traverseParents(&LVScope::getHasLines, &LVScope::setHasLines);
}

// Add a location.
void LVScope::addObject(LVLocation *Location) {
  assert(Location && "Invalid location.");
  assert(!Location->getParent() && "Location already inserted");
  if (!Ranges)
    Ranges = std::make_unique<LVLocations>();

  // Add it to parent.
  Location->setParent(this);
  Location->setOffset(getOffset());

  Ranges->push_back(Location);
  setHasRanges();
}

// Adds the scope to the child scopes and sets the parent in the child.
void LVScope::addElement(LVScope *Scope) {
  assert(Scope && "Invalid scope.");
  assert(!Scope->getParent() && "Scope already inserted");
  if (!Scopes)
    Scopes = std::make_unique<LVScopes>();

  // Add it to parent.
  Scopes->push_back(Scope);
  addToChildren(Scope);
  Scope->setParent(this);

  // Notify the reader about the new element being added.
  getReaderCompileUnit()->addedElement(Scope);

  // If the element is a global reference, mark its parent as having global
  // references; that information is used, to print only those branches
  // with global references.
  if (Scope->getIsGlobalReference())
    traverseParents(&LVScope::getHasGlobals, &LVScope::setHasGlobals);
  else
    traverseParents(&LVScope::getHasLocals, &LVScope::setHasLocals);

  // Indicate that this tree branch has scopes.
  traverseParents(&LVScope::getHasScopes, &LVScope::setHasScopes);
}

// Adds a symbol to the ones stored in the scope.
void LVScope::addElement(LVSymbol *Symbol) {
  assert(Symbol && "Invalid symbol.");
  assert(!Symbol->getParent() && "Symbol already inserted");
  if (!Symbols)
    Symbols = std::make_unique<LVSymbols>();

  // Add it to parent.
  Symbols->push_back(Symbol);
  addToChildren(Symbol);
  Symbol->setParent(this);

  // Notify the reader about the new element being added.
  getReaderCompileUnit()->addedElement(Symbol);

  // If the element is a global reference, mark its parent as having global
  // references; that information is used, to print only those branches
  // with global references.
  if (Symbol->getIsGlobalReference())
    traverseParents(&LVScope::getHasGlobals, &LVScope::setHasGlobals);
  else
    traverseParents(&LVScope::getHasLocals, &LVScope::setHasLocals);

  // Indicate that this tree branch has symbols.
  traverseParents(&LVScope::getHasSymbols, &LVScope::setHasSymbols);
}

// Adds a type to the ones stored in the scope.
void LVScope::addElement(LVType *Type) {
  assert(Type && "Invalid type.");
  assert(!Type->getParent() && "Type already inserted");
  if (!Types)
    Types = std::make_unique<LVTypes>();

  // Add it to parent.
  Types->push_back(Type);
  addToChildren(Type);
  Type->setParent(this);

  // Notify the reader about the new element being added.
  getReaderCompileUnit()->addedElement(Type);

  // If the element is a global reference, mark its parent as having global
  // references; that information is used, to print only those branches
  // with global references.
  if (Type->getIsGlobalReference())
    traverseParents(&LVScope::getHasGlobals, &LVScope::setHasGlobals);
  else
    traverseParents(&LVScope::getHasLocals, &LVScope::setHasLocals);

  // Indicate that this tree branch has types.
  traverseParents(&LVScope::getHasTypes, &LVScope::setHasTypes);
}

// Add a pair of ranges.
void LVScope::addObject(LVAddress LowerAddress, LVAddress UpperAddress) {
  // Pack the ranges into a Location object.
  LVLocation *Location = getReader().createLocation();
  Location->setLowerAddress(LowerAddress);
  Location->setUpperAddress(UpperAddress);
  Location->setIsAddressRange();

  addObject(Location);
}

bool LVScope::removeElement(LVElement *Element) {
  auto Predicate = [Element](LVElement *Item) -> bool {
    return Item == Element;
  };
  auto RemoveElement = [Element, Predicate](auto &Container) -> bool {
    auto Iter = std::remove_if(Container->begin(), Container->end(), Predicate);
    if (Iter != Container->end()) {
      Container->erase(Iter, Container->end());
      Element->resetParent();
      return true;
    }
    return false;
  };

  // As 'children' contains only (scopes, symbols and types), check if the
  // element we are deleting is a line.
  if (Element->getIsLine())
    return RemoveElement(Lines);

  if (RemoveElement(Children)) {
    if (Element->getIsSymbol())
      return RemoveElement(Symbols);
    if (Element->getIsType())
      return RemoveElement(Types);
    if (Element->getIsScope())
      return RemoveElement(Scopes);
    llvm_unreachable("Invalid element.");
  }

  return false;
}

void LVScope::addMissingElements(LVScope *Reference) {
  setAddedMissing();
  if (!Reference)
    return;

  // Get abstract symbols for the given scope reference.
  const LVSymbols *ReferenceSymbols = Reference->getSymbols();
  if (!ReferenceSymbols)
    return;

  LVSymbols References;
  References.append(ReferenceSymbols->begin(), ReferenceSymbols->end());

  // Erase abstract symbols already in this scope from the collection of
  // symbols in the referenced scope.
  if (getSymbols())
    for (const LVSymbol *Symbol : *getSymbols())
      if (Symbol->getHasReferenceAbstract())
        llvm::erase(References, Symbol->getReference());

  // If we have elements left in 'References', those are the elements that
  // need to be inserted in the current scope.
  if (References.size()) {
    LLVM_DEBUG({
      dbgs() << "Insert Missing Inlined Elements\n"
             << "Offset = " << hexSquareString(getOffset()) << " "
             << "Abstract = " << hexSquareString(Reference->getOffset())
             << "\n";
    });
    for (LVSymbol *Reference : References) {
      LLVM_DEBUG({
        dbgs() << "Missing Offset = " << hexSquareString(Reference->getOffset())
               << "\n";
      });
      // We can't clone the abstract origin reference, as it contain extra
      // information that is incorrect for the element to be inserted.
      // As the symbol being added does not exist in the debug section,
      // use its parent scope offset, to indicate its DIE location.
      LVSymbol *Symbol = getReader().createSymbol();
      addElement(Symbol);
      Symbol->setOffset(getOffset());
      Symbol->setIsOptimized();
      Symbol->setReference(Reference);

      // The symbol can be a constant, parameter or variable.
      if (Reference->getIsConstant())
        Symbol->setIsConstant();
      else if (Reference->getIsParameter())
        Symbol->setIsParameter();
      else if (Reference->getIsVariable())
        Symbol->setIsVariable();
      else
        llvm_unreachable("Invalid symbol kind.");
    }
  }
}

void LVScope::updateLevel(LVScope *Parent, bool Moved) {
  // Update the level for the element itself and all its children, using the
  // given scope parent as reference.
  setLevel(Parent->getLevel() + 1);

  // Update the children.
  if (Children)
    for (LVElement *Element : *Children)
      Element->updateLevel(this, Moved);

  // Update any lines.
  if (Lines)
    for (LVLine *Line : *Lines)
      Line->updateLevel(this, Moved);
}

void LVScope::resolve() {
  if (getIsResolved())
    return;

  // Resolve the element itself.
  LVElement::resolve();

  // Resolve the children.
  if (Children)
    for (LVElement *Element : *Children) {
      if (getIsGlobalReference())
        // If the scope is a global reference, mark all its children as well.
        Element->setIsGlobalReference();
      Element->resolve();
    }
}

void LVScope::resolveName() {
  if (getIsResolvedName())
    return;
  setIsResolvedName();

  // If the scope is a template, resolve the template parameters and get
  // the name for the template with the encoded arguments.
  if (getIsTemplate())
    resolveTemplate();
  else {
    if (LVElement *BaseType = getType()) {
      BaseType->resolveName();
      resolveFullname(BaseType);
    }
  }

  // In the case of unnamed scopes, try to generate a name for it, using
  // the parents name and the line information. In the case of compiler
  // generated functions, use its linkage name if is available.
  if (!isNamed()) {
    if (getIsArtificial())
      setName(getLinkageName());
    else
      generateName();
  }

  LVElement::resolveName();

  // Resolve any given pattern.
  patterns().resolvePatternMatch(this);
}

void LVScope::resolveReferences() {
  // The scopes can have the following references to other elements:
  //   A type:
  //     DW_AT_type             ->  Type or Scope
  //     DW_AT_import           ->  Type
  //   A Reference:
  //     DW_AT_specification    ->  Scope
  //     DW_AT_abstract_origin  ->  Scope
  //     DW_AT_extension        ->  Scope

  // Resolve any referenced scope.
  LVScope *Reference = getReference();
  if (Reference) {
    Reference->resolve();
    // Recursively resolve the scope names.
    resolveReferencesChain();
  }

  // Set the file/line information using the Debug Information entry.
  setFile(Reference);

  // Resolve any referenced type or scope.
  if (LVElement *Element = getType())
    Element->resolve();
}

void LVScope::resolveElements() {
  // The current element represents the Root. Traverse each Compile Unit.
  if (!Scopes)
    return;

  for (LVScope *Scope : *Scopes) {
    LVScopeCompileUnit *CompileUnit = static_cast<LVScopeCompileUnit *>(Scope);
    getReader().setCompileUnit(CompileUnit);
    CompileUnit->resolve();
    // Propagate any matching information into the scopes tree.
    CompileUnit->propagatePatternMatch();
  }
}

StringRef LVScope::resolveReferencesChain() {
  // If the scope has a DW_AT_specification or DW_AT_abstract_origin,
  // follow the chain to resolve the name from those references.
  if (getHasReference() && !isNamed())
    setName(getReference()->resolveReferencesChain());

  return getName();
}

// Get template parameter types.
bool LVScope::getTemplateParameterTypes(LVTypes &Params) {
  // Traverse the scope types and populate the given container with those
  // types that are template parameters; that container will be used by
  // 'encodeTemplateArguments' to resolve them.
  if (const LVTypes *Types = getTypes())
    for (LVType *Type : *Types)
      if (Type->getIsTemplateParam()) {
        Type->resolve();
        Params.push_back(Type);
      }

  return !Params.empty();
}

// Resolve the template parameters/arguments relationship.
void LVScope::resolveTemplate() {
  if (getIsTemplateResolved())
    return;
  setIsTemplateResolved();

  // Check if we need to encode the template arguments.
  if (options().getAttributeEncoded()) {
    LVTypes Params;
    if (getTemplateParameterTypes(Params)) {
      std::string EncodedArgs;
      // Encode the arguments as part of the template name and update the
      // template name, to reflect the encoded parameters.
      encodeTemplateArguments(EncodedArgs, &Params);
      setEncodedArgs(EncodedArgs);
    }
  }
}

// Get the qualified name for the template.
void LVScope::getQualifiedName(std::string &QualifiedName) const {
  if (getIsRoot() || getIsCompileUnit())
    return;

  if (LVScope *Parent = getParentScope())
    Parent->getQualifiedName(QualifiedName);
  if (!QualifiedName.empty())
    QualifiedName.append("::");
  QualifiedName.append(std::string(getName()));
}

// Encode the template arguments as part of the template name.
void LVScope::encodeTemplateArguments(std::string &Name) const {
  // Qualify only when we are expanding parameters that are template
  // instances; the debugger will assume the current scope symbol as
  // the qualifying tag for the symbol being generated, which gives:
  //   namespace std {
  //     ...
  //     set<float,std::less<float>,std::allocator<float>>
  //     ...
  //   }
  // The 'set' symbol is assumed to have the qualified tag 'std'.

  // We are resolving a template parameter which is another template. If
  // it is already resolved, just get the qualified name and return.
  std::string BaseName;
  getQualifiedName(BaseName);
  if (getIsTemplateResolved())
    Name.append(BaseName);
}

void LVScope::encodeTemplateArguments(std::string &Name,
                                      const LVTypes *Types) const {
  // The encoded string will start with the scope name.
  Name.append("<");

  // The list of types are the template parameters.
  if (Types) {
    bool AddComma = false;
    for (const LVType *Type : *Types) {
      if (AddComma)
        Name.append(", ");
      Type->encodeTemplateArgument(Name);
      AddComma = true;
    }
  }

  Name.append(">");
}

bool LVScope::resolvePrinting() const {
  // The warnings collected during the scope creation as per compile unit.
  // If there is a request for printing warnings, always print its associate
  // Compile Unit.
  if (options().getPrintWarnings() && (getIsRoot() || getIsCompileUnit()))
    return true;

  // In selection mode, always print the root scope regardless of the
  // number of matched elements. If no matches, the root by itself will
  // indicate no matches.
  if (options().getSelectExecute()) {
    return getIsRoot() || getIsCompileUnit() || getHasPattern();
  }

  bool Globals = options().getAttributeGlobal();
  bool Locals = options().getAttributeLocal();
  if ((Globals && Locals) || (!Globals && !Locals)) {
    // Print both Global and Local.
  } else {
    // Check for Global or Local Objects.
    if ((Globals && !(getHasGlobals() || getIsGlobalReference())) ||
        (Locals && !(getHasLocals() || !getIsGlobalReference())))
      return false;
  }

  // For the case of functions, skip it if is compiler generated.
  if (getIsFunction() && getIsArtificial() &&
      !options().getAttributeGenerated())
    return false;

  return true;
}

Error LVScope::doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                       bool Full) const {
  // During a view output splitting, use the output stream created by the
  // split context, then switch to the reader output stream.
  raw_ostream *StreamSplit = &OS;

  // Ignore the CU generated by the VS toolchain, when compiling to PDB.
  if (getIsSystem() && !options().getAttributeSystem())
    return Error::success();

  // If 'Split', we use the scope name (CU name) as the ouput file; the
  // delimiters in the pathname, must be replaced by a normal character.
  if (getIsCompileUnit()) {
    getReader().setCompileUnit(const_cast<LVScope *>(this));
    if (Split) {
      std::string ScopeName(getName());
      if (std::error_code EC =
              getReaderSplitContext().open(ScopeName, ".txt", OS))
        return createStringError(EC, "Unable to create split output file %s",
                                 ScopeName.c_str());
      StreamSplit = static_cast<raw_ostream *>(&getReaderSplitContext().os());
    }
  }

  // Ignore discarded or stripped scopes (functions).
  bool DoPrint = (options().getAttributeDiscarded()) ? true : !getIsDiscarded();

  // If we are in compare mode, the only conditions are related to the
  // element being missing. In the case of elements comparison, we print the
  // augmented view, that includes added elements.
  // In print mode, we check other conditions, such as local, global, etc.
  if (DoPrint) {
    DoPrint =
        getIsInCompare() ? options().getReportExecute() : resolvePrinting();
  }

  // At this point we have checked for very specific options, to decide if the
  // element will be printed. Include the caller's test for element general
  // print.
  DoPrint = DoPrint && (Print || options().getOutputSplit());

  if (DoPrint) {
    // Print the element itself.
    print(*StreamSplit, Full);

    // Check if we have reached the requested lexical level specified in the
    // command line options. Input file is level zero and the CU is level 1.
    if ((getIsRoot() || options().getPrintAnyElement()) &&
        options().getPrintFormatting() &&
        getLevel() < options().getOutputLevel()) {
      // Print the children.
      if (Children)
        for (const LVElement *Element : *Children) {
          if (Match && !Element->getHasPattern())
            continue;
          if (Error Err =
                  Element->doPrint(Split, Match, Print, *StreamSplit, Full))
            return Err;
        }

      // Print the line records.
      if (Lines)
        for (const LVLine *Line : *Lines) {
          if (Match && !Line->getHasPattern())
            continue;
          if (Error Err =
                  Line->doPrint(Split, Match, Print, *StreamSplit, Full))
            return Err;
        }

      // Print the warnings.
      if (options().getPrintWarnings())
        printWarnings(*StreamSplit, Full);
    }
  }

  // Done printing the compile unit. Print any requested summary and
  // restore the original output context.
  if (getIsCompileUnit()) {
    if (options().getPrintSummary())
      printSummary(*StreamSplit);
    if (options().getPrintSizes())
      printSizes(*StreamSplit);
    if (Split) {
      getReaderSplitContext().close();
      StreamSplit = &getReader().outputStream();
    }
  }

  if (getIsRoot() && options().getPrintWarnings()) {
    getReader().printRecords(*StreamSplit);
  }

  return Error::success();
}

void LVScope::sort() {
  // Preserve the lines order as they are associated with user code.
  LVSortFunction SortFunction = getSortFunction();
  if (SortFunction) {
    std::function<void(LVScope * Parent, LVSortFunction SortFunction)> Sort =
        [&](LVScope *Parent, LVSortFunction SortFunction) {
          auto Traverse = [&](auto &Set, LVSortFunction SortFunction) {
            if (Set)
              std::stable_sort(Set->begin(), Set->end(), SortFunction);
          };
          Traverse(Parent->Types, SortFunction);
          Traverse(Parent->Symbols, SortFunction);
          Traverse(Parent->Scopes, SortFunction);
          Traverse(Parent->Ranges, compareRange);
          Traverse(Parent->Children, SortFunction);

          if (Parent->Scopes)
            for (LVScope *Scope : *Parent->Scopes)
              Sort(Scope, SortFunction);
        };

    // Start traversing the scopes root and transform the element name.
    Sort(this, SortFunction);
  }
}

void LVScope::traverseParents(LVScopeGetFunction GetFunction,
                              LVScopeSetFunction SetFunction) {
  // Traverse the parent tree.
  LVScope *Parent = this;
  while (Parent) {
    // Terminates if the 'SetFunction' has been already executed.
    if ((Parent->*GetFunction)())
      break;
    (Parent->*SetFunction)();
    Parent = Parent->getParentScope();
  }
}

void LVScope::traverseParentsAndChildren(LVObjectGetFunction GetFunction,
                                         LVObjectSetFunction SetFunction) {
  if (options().getReportParents()) {
    // First traverse the parent tree.
    LVScope *Parent = this;
    while (Parent) {
      // Terminates if the 'SetFunction' has been already executed.
      if ((Parent->*GetFunction)())
        break;
      (Parent->*SetFunction)();
      Parent = Parent->getParentScope();
    }
  }

  std::function<void(LVScope * Scope)> TraverseChildren = [&](LVScope *Scope) {
    auto Traverse = [&](const auto *Set) {
      if (Set)
        for (const auto &Entry : *Set)
          (Entry->*SetFunction)();
    };

    (Scope->*SetFunction)();

    Traverse(Scope->getTypes());
    Traverse(Scope->getSymbols());
    Traverse(Scope->getLines());

    if (const LVScopes *Scopes = Scope->getScopes())
      for (LVScope *Scope : *Scopes)
        TraverseChildren(Scope);
  };

  if (options().getReportChildren())
    TraverseChildren(this);
}

// Traverse the symbol location ranges and for each range:
// - Apply the 'ValidLocation' validation criteria.
// - Add any failed range to the 'LocationList'.
// - Calculate location coverage.
void LVScope::getLocations(LVLocations &LocationList,
                           LVValidLocation ValidLocation, bool RecordInvalid) {
  // Traverse scopes and symbols.
  if (Symbols)
    for (LVSymbol *Symbol : *Symbols)
      Symbol->getLocations(LocationList, ValidLocation, RecordInvalid);
  if (Scopes)
    for (LVScope *Scope : *Scopes)
      Scope->getLocations(LocationList, ValidLocation, RecordInvalid);
}

// Traverse the scope ranges and for each range:
// - Apply the 'ValidLocation' validation criteria.
// - Add any failed range to the 'LocationList'.
// - Calculate location coverage.
void LVScope::getRanges(LVLocations &LocationList,
                        LVValidLocation ValidLocation, bool RecordInvalid) {
  // Ignore discarded or stripped scopes (functions).
  if (getIsDiscarded())
    return;

  // Process the ranges for current scope.
  if (Ranges) {
    for (LVLocation *Location : *Ranges) {
      // Add the invalid location object.
      if (!(Location->*ValidLocation)() && RecordInvalid)
        LocationList.push_back(Location);
    }

    // Calculate coverage factor.
    calculateCoverage();
  }

  // Traverse the scopes.
  if (Scopes)
    for (LVScope *Scope : *Scopes)
      Scope->getRanges(LocationList, ValidLocation, RecordInvalid);
}

// Get all the ranges associated with scopes.
void LVScope::getRanges(LVRange &RangeList) {
  // Ignore discarded or stripped scopes (functions).
  if (getIsDiscarded())
    return;

  if (Ranges)
    RangeList.addEntry(this);
  if (Scopes)
    for (LVScope *Scope : *Scopes)
      Scope->getRanges(RangeList);
}

LVScope *LVScope::outermostParent(LVAddress Address) {
  LVScope *Parent = this;
  while (Parent) {
    const LVLocations *ParentRanges = Parent->getRanges();
    if (ParentRanges)
      for (const LVLocation *Location : *ParentRanges)
        if (Location->getLowerAddress() <= Address)
          return Parent;
    Parent = Parent->getParentScope();
  }
  return Parent;
}

LVScope *LVScope::findIn(const LVScopes *Targets) const {
  if (!Targets)
    return nullptr;

  // In the case of overloaded functions, sometimes the DWARF used to
  // describe them, does not give suficient information. Try to find a
  // perfect match or mark them as possible conflicts.
  LVScopes Candidates;
  for (LVScope *Target : *Targets)
    if (LVScope::equals(Target))
      Candidates.push_back(Target);

  LLVM_DEBUG({
    if (!Candidates.empty()) {
      dbgs() << "\n[LVScope::findIn]\n"
             << "Reference: "
             << "Offset = " << hexSquareString(getOffset()) << ", "
             << "Level = " << getLevel() << ", "
             << "Kind = " << formattedKind(kind()) << ", "
             << "Name = " << formattedName(getName()) << "\n";
      for (const LVScope *Candidate : Candidates)
        dbgs() << "Candidate: "
               << "Offset = " << hexSquareString(Candidate->getOffset()) << ", "
               << "Level = " << Candidate->getLevel() << ", "
               << "Kind = " << formattedKind(Candidate->kind()) << ", "
               << "Name = " << formattedName(Candidate->getName()) << "\n";
    }
  });

  if (!Candidates.empty())
    return (Candidates.size() == 1)
               ? (equals(Candidates[0]) ? Candidates[0] : nullptr)
               : findEqualScope(&Candidates);

  return nullptr;
}

bool LVScope::equalNumberOfChildren(const LVScope *Scope) const {
  // Same number of children. Take into account which elements are requested
  // to be included in the comparison.
  return !(
      (options().getCompareScopes() && scopeCount() != Scope->scopeCount()) ||
      (options().getCompareSymbols() &&
       symbolCount() != Scope->symbolCount()) ||
      (options().getCompareTypes() && typeCount() != Scope->typeCount()) ||
      (options().getCompareLines() && lineCount() != Scope->lineCount()));
}

void LVScope::markMissingParents(const LVScope *Target, bool TraverseChildren) {
  auto SetCompareState = [&](auto &Container) {
    if (Container)
      for (auto *Entry : *Container)
        Entry->setIsInCompare();
  };
  SetCompareState(Types);
  SetCompareState(Symbols);
  SetCompareState(Lines);
  SetCompareState(Scopes);

  // At this point, we are ready to start comparing the current scope, once
  // the compare bits have been set.
  if (options().getCompareTypes() && getTypes() && Target->getTypes())
    LVType::markMissingParents(getTypes(), Target->getTypes());
  if (options().getCompareSymbols() && getSymbols() && Target->getSymbols())
    LVSymbol::markMissingParents(getSymbols(), Target->getSymbols());
  if (options().getCompareLines() && getLines() && Target->getLines())
    LVLine::markMissingParents(getLines(), Target->getLines());
  if (getScopes() && Target->getScopes())
    LVScope::markMissingParents(getScopes(), Target->getScopes(),
                                TraverseChildren);
}

void LVScope::markMissingParents(const LVScopes *References,
                                 const LVScopes *Targets,
                                 bool TraverseChildren) {
  if (!(References && Targets))
    return;

  LLVM_DEBUG({
    dbgs() << "\n[LVScope::markMissingParents]\n";
    for (const LVScope *Reference : *References)
      dbgs() << "References: "
             << "Offset = " << hexSquareString(Reference->getOffset()) << ", "
             << "Level = " << Reference->getLevel() << ", "
             << "Kind = " << formattedKind(Reference->kind()) << ", "
             << "Name = " << formattedName(Reference->getName()) << "\n";
    for (const LVScope *Target : *Targets)
      dbgs() << "Targets   : "
             << "Offset = " << hexSquareString(Target->getOffset()) << ", "
             << "Level = " << Target->getLevel() << ", "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Name = " << formattedName(Target->getName()) << "\n";
  });

  for (LVScope *Reference : *References) {
    // Don't process 'Block' scopes, as we can't identify them.
    if (Reference->getIsBlock() || Reference->getIsGeneratedName())
      continue;

    LLVM_DEBUG({
      dbgs() << "\nSearch Reference: "
             << "Offset = " << hexSquareString(Reference->getOffset()) << " "
             << "Name = " << formattedName(Reference->getName()) << "\n";
    });
    LVScope *Target = Reference->findIn(Targets);
    if (Target) {
      LLVM_DEBUG({
        dbgs() << "\nFound Target: "
               << "Offset = " << hexSquareString(Target->getOffset()) << " "
               << "Name = " << formattedName(Target->getName()) << "\n";
      });
      if (TraverseChildren)
        Reference->markMissingParents(Target, TraverseChildren);
    } else {
      LLVM_DEBUG({
        dbgs() << "Missing Reference: "
               << "Offset = " << hexSquareString(Reference->getOffset()) << " "
               << "Name = " << formattedName(Reference->getName()) << "\n";
      });
      Reference->markBranchAsMissing();
    }
  }
}

bool LVScope::equals(const LVScope *Scope) const {
  if (!LVElement::equals(Scope))
    return false;
  // For lexical scopes, check if their parents are the same.
  if (getIsLexicalBlock() && Scope->getIsLexicalBlock())
    return getParentScope()->equals(Scope->getParentScope());
  return true;
}

LVScope *LVScope::findEqualScope(const LVScopes *Scopes) const {
  assert(Scopes && "Scopes must not be nullptr");
  for (LVScope *Scope : *Scopes)
    if (equals(Scope))
      return Scope;
  return nullptr;
}

bool LVScope::equals(const LVScopes *References, const LVScopes *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets && References->size() == Targets->size()) {
    for (const LVScope *Reference : *References)
      if (!Reference->findIn(Targets))
        return false;
    return true;
  }
  return false;
}

void LVScope::report(LVComparePass Pass) {
  getComparator().printItem(this, Pass);
  getComparator().push(this);
  if (Children)
    for (LVElement *Element : *Children)
      Element->report(Pass);

  if (Lines)
    for (LVLine *Line : *Lines)
      Line->report(Pass);
  getComparator().pop();
}

void LVScope::printActiveRanges(raw_ostream &OS, bool Full) const {
  if (options().getPrintFormatting() && options().getAttributeRange() &&
      Ranges) {
    for (const LVLocation *Location : *Ranges)
      Location->print(OS, Full);
  }
}

void LVScope::printEncodedArgs(raw_ostream &OS, bool Full) const {
  if (options().getPrintFormatting() && options().getAttributeEncoded())
    printAttributes(OS, Full, "{Encoded} ", const_cast<LVScope *>(this),
                    getEncodedArgs(), /*UseQuotes=*/false, /*PrintRef=*/false);
}

void LVScope::print(raw_ostream &OS, bool Full) const {
  if (getIncludeInPrint() && getReader().doPrintScope(this)) {
    // For a summary (printed elements), do not count the scope root.
    // For a summary (selected elements) do not count a compile unit.
    if (!(getIsRoot() || (getIsCompileUnit() && options().getSelectExecute())))
      getReaderCompileUnit()->incrementPrintedScopes();
    LVElement::print(OS, Full);
    printExtra(OS, Full);
  }
}

void LVScope::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind());
  // Do not print any type or name for a lexical block.
  if (!getIsBlock()) {
    OS << " " << formattedName(getName());
    if (!getIsAggregate())
      OS << " -> " << typeOffsetAsString()
         << formattedNames(getTypeQualifiedName(), typeAsString());
  }
  OS << "\n";

  // Print any active ranges.
  if (Full && getIsBlock())
    printActiveRanges(OS, Full);
}

//===----------------------------------------------------------------------===//
// DWARF Union/Structure/Class.
//===----------------------------------------------------------------------===//
bool LVScopeAggregate::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;

  if (!equalNumberOfChildren(Scope))
    return false;

  // Check if the parameters match in the case of templates.
  if (!LVType::parametersMatch(getTypes(), Scope->getTypes()))
    return false;

  if (!isNamed() && !Scope->isNamed())
    // In the case of unnamed union/structure/class compare the file name.
    if (getFilenameIndex() != Scope->getFilenameIndex())
      return false;

  return true;
}

LVScope *LVScopeAggregate::findEqualScope(const LVScopes *Scopes) const {
  assert(Scopes && "Scopes must not be nullptr");
  for (LVScope *Scope : *Scopes)
    if (equals(Scope))
      return Scope;
  return nullptr;
}

void LVScopeAggregate::printExtra(raw_ostream &OS, bool Full) const {
  LVScope::printExtra(OS, Full);
  if (Full) {
    if (getIsTemplateResolved())
      printEncodedArgs(OS, Full);
    LVScope *Reference = getReference();
    if (Reference)
      Reference->printReference(OS, Full, const_cast<LVScopeAggregate *>(this));
  }
}

//===----------------------------------------------------------------------===//
// DWARF Template alias.
//===----------------------------------------------------------------------===//
bool LVScopeAlias::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;
  return equalNumberOfChildren(Scope);
}

void LVScopeAlias::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << " -> "
     << typeOffsetAsString()
     << formattedNames(getTypeQualifiedName(), typeAsString()) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF array (DW_TAG_array_type).
//===----------------------------------------------------------------------===//
void LVScopeArray::resolveExtra() {
  // If the scope is an array, resolve the subrange entries and get those
  // values encoded and assigned to the scope type.
  // Encode the array subrange entries as part of the name.
  if (getIsArrayResolved())
    return;
  setIsArrayResolved();

  // There are 2 cases to represent the bounds information for an array:
  // 1) DW_TAG_array_type
  //      DW_AT_type --> ref_type
  //      DW_TAG_subrange_type
  //        DW_AT_type --> ref_type (type of object)
  //        DW_AT_count --> value (number of elements in subrange)

  // 2) DW_TAG_array_type
  //      DW_AT_type --> ref_type
  //        DW_TAG_subrange_type
  //          DW_AT_lower_bound --> value
  //          DW_AT_upper_bound --> value

  // The idea is to represent the bounds as a string, depending on the format:
  // 1) [count]
  // 2) [lower][upper]

  // Traverse scope types, looking for those types that are subranges.
  LVTypes Subranges;
  if (const LVTypes *Types = getTypes())
    for (LVType *Type : *Types)
      if (Type->getIsSubrange()) {
        Type->resolve();
        Subranges.push_back(Type);
      }

  // Use the subrange types to generate the high level name for the array.
  // Check the type has been fully resolved.
  if (LVElement *BaseType = getType()) {
    BaseType->resolveName();
    resolveFullname(BaseType);
  }

  // In 'resolveFullname' a check is done for double spaces in the type name.
  std::stringstream ArrayInfo;
  if (ElementType)
    ArrayInfo << getTypeName().str() << " ";

  for (const LVType *Type : Subranges) {
    if (Type->getIsSubrangeCount())
      // Check if we have DW_AT_count subrange style.
      ArrayInfo << "[" << Type->getCount() << "]";
    else {
      // Get lower and upper subrange values.
      unsigned LowerBound;
      unsigned UpperBound;
      std::tie(LowerBound, UpperBound) = Type->getBounds();

      // The representation depends on the bound values. If the lower value
      // is zero, treat the pair as the elements count. Otherwise, just use
      // the pair, as they are representing arrays in languages other than
      // C/C++ and the lower limit is not zero.
      if (LowerBound)
        ArrayInfo << "[" << LowerBound << ".." << UpperBound << "]";
      else
        ArrayInfo << "[" << UpperBound + 1 << "]";
    }
  }

  // Update the scope name, to reflect the encoded subranges.
  setName(ArrayInfo.str());
}

bool LVScopeArray::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;

  if (!equalNumberOfChildren(Scope))
    return false;

  // Despite the arrays are encoded, to reflect the dimensions, we have to
  // check the subranges, in order to determine if they are the same.
  if (!LVType::equals(getTypes(), Scope->getTypes()))
    return false;

  return true;
}

void LVScopeArray::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << typeOffsetAsString()
     << formattedName(getName()) << "\n";
}

//===----------------------------------------------------------------------===//
// An object file (single or multiple CUs).
//===----------------------------------------------------------------------===//
void LVScopeCompileUnit::addSize(LVScope *Scope, LVOffset Lower,
                                 LVOffset Upper) {
  LLVM_DEBUG({
    dbgs() << format(
        "CU [0x%08" PRIx64 "], Scope [0x%08" PRIx64 "], Range [0x%08" PRIx64
        ":0x%08" PRIx64 "], Size = %" PRId64 "\n",
        getOffset(), Scope->getOffset(), Lower, Upper, Upper - Lower);
  });

  // There is no need to check for a previous entry, as we are traversing the
  // debug information in sequential order.
  LVOffset Size = Upper - Lower;
  Sizes[Scope] = Size;
  if (this == Scope)
    // Record contribution size for the compilation unit.
    CUContributionSize = Size;
}

// Update parents and children with pattern information.
void LVScopeCompileUnit::propagatePatternMatch() {
  // At this stage, we have finished creating the Scopes tree and we have
  // a list of elements that match the pattern specified in the command line.
  // The pattern corresponds to a scope or element; mark parents and/or
  // children as having that pattern, before any printing is done.
  if (!options().getSelectExecute())
    return;

  if (MatchedScopes.size()) {
    for (LVScope *Scope : MatchedScopes)
      Scope->traverseParentsAndChildren(&LVScope::getHasPattern,
                                        &LVScope::setHasPattern);
  } else {
    // Mark the compile unit as having a pattern to enable any requests to
    // print sizes and summary as that information is recorded at that level.
    setHasPattern();
  }
}

void LVScopeCompileUnit::processRangeLocationCoverage(
    LVValidLocation ValidLocation) {
  if (options().getAttributeRange()) {
    // Traverse the scopes to get scopes that have invalid ranges.
    LVLocations Locations;
    bool RecordInvalid = options().getWarningRanges();
    getRanges(Locations, ValidLocation, RecordInvalid);

    // Validate ranges associated with scopes.
    if (RecordInvalid)
      for (LVLocation *Location : Locations)
        addInvalidRange(Location);
  }

  if (options().getAttributeLocation()) {
    // Traverse the scopes to get locations that have invalid ranges.
    LVLocations Locations;
    bool RecordInvalid = options().getWarningLocations();
    getLocations(Locations, ValidLocation, RecordInvalid);

    // Validate ranges associated with locations.
    if (RecordInvalid)
      for (LVLocation *Location : Locations)
        addInvalidLocation(Location);
  }
}

void LVScopeCompileUnit::addMapping(LVLine *Line, LVSectionIndex SectionIndex) {
  LVAddress Address = Line->getOffset();
  SectionMappings.add(SectionIndex, Address, Line);
}

LVLine *LVScopeCompileUnit::lineLowerBound(LVAddress Address,
                                           LVScope *Scope) const {
  LVSectionIndex SectionIndex = getReader().getSectionIndex(Scope);
  LVAddressToLine *Map = SectionMappings.findMap(SectionIndex);
  if (!Map || Map->empty())
    return nullptr;
  LVAddressToLine::const_iterator Iter = Map->lower_bound(Address);
  return (Iter != Map->end()) ? Iter->second : nullptr;
}

LVLine *LVScopeCompileUnit::lineUpperBound(LVAddress Address,
                                           LVScope *Scope) const {
  LVSectionIndex SectionIndex = getReader().getSectionIndex(Scope);
  LVAddressToLine *Map = SectionMappings.findMap(SectionIndex);
  if (!Map || Map->empty())
    return nullptr;
  LVAddressToLine::const_iterator Iter = Map->upper_bound(Address);
  if (Iter != Map->begin())
    Iter = std::prev(Iter);
  return Iter->second;
}

LVLineRange LVScopeCompileUnit::lineRange(LVLocation *Location) const {
  // The parent of a location can be a symbol or a scope.
  LVElement *Element = Location->getParent();
  LVScope *Parent = Element->getIsScope() ? static_cast<LVScope *>(Element)
                                          : Element->getParentScope();
  LVLine *LowLine = lineLowerBound(Location->getLowerAddress(), Parent);
  LVLine *HighLine = lineUpperBound(Location->getUpperAddress(), Parent);
  return LVLineRange(LowLine, HighLine);
}

StringRef LVScopeCompileUnit::getFilename(size_t Index) const {
  if (Index <= 0 || Index > Filenames.size())
    return StringRef();
  return getStringPool().getString(Filenames[Index - 1]);
}

bool LVScopeCompileUnit::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;

  return getNameIndex() == Scope->getNameIndex();
}

void LVScopeCompileUnit::incrementPrintedLines() {
  options().getSelectExecute() ? ++Found.Lines : ++Printed.Lines;
}
void LVScopeCompileUnit::incrementPrintedScopes() {
  options().getSelectExecute() ? ++Found.Scopes : ++Printed.Scopes;
}
void LVScopeCompileUnit::incrementPrintedSymbols() {
  options().getSelectExecute() ? ++Found.Symbols : ++Printed.Symbols;
}
void LVScopeCompileUnit::incrementPrintedTypes() {
  options().getSelectExecute() ? ++Found.Types : ++Printed.Types;
}

// Values are used by '--summary' option (allocated).
void LVScopeCompileUnit::increment(LVLine *Line) {
  if (Line->getIncludeInPrint())
    ++Allocated.Lines;
}
void LVScopeCompileUnit::increment(LVScope *Scope) {
  if (Scope->getIncludeInPrint())
    ++Allocated.Scopes;
}
void LVScopeCompileUnit::increment(LVSymbol *Symbol) {
  if (Symbol->getIncludeInPrint())
    ++Allocated.Symbols;
}
void LVScopeCompileUnit::increment(LVType *Type) {
  if (Type->getIncludeInPrint())
    ++Allocated.Types;
}

// A new element has been added to the scopes tree. Take the following steps:
// Increase the added element counters, for printing summary.
// During comparison notify the Reader of the new element.
void LVScopeCompileUnit::addedElement(LVLine *Line) {
  increment(Line);
  getReader().notifyAddedElement(Line);
}
void LVScopeCompileUnit::addedElement(LVScope *Scope) {
  increment(Scope);
  getReader().notifyAddedElement(Scope);
}
void LVScopeCompileUnit::addedElement(LVSymbol *Symbol) {
  increment(Symbol);
  getReader().notifyAddedElement(Symbol);
}
void LVScopeCompileUnit::addedElement(LVType *Type) {
  increment(Type);
  getReader().notifyAddedElement(Type);
}

// Record unsuported DWARF tags.
void LVScopeCompileUnit::addDebugTag(dwarf::Tag Target, LVOffset Offset) {
  addItem<LVTagOffsetsMap, dwarf::Tag, LVOffset>(&DebugTags, Target, Offset);
}

// Record elements with invalid offsets.
void LVScopeCompileUnit::addInvalidOffset(LVOffset Offset, LVElement *Element) {
  if (WarningOffsets.find(Offset) == WarningOffsets.end())
    WarningOffsets.emplace(Offset, Element);
}

// Record symbols with invalid coverage values.
void LVScopeCompileUnit::addInvalidCoverage(LVSymbol *Symbol) {
  LVOffset Offset = Symbol->getOffset();
  if (InvalidCoverages.find(Offset) == InvalidCoverages.end())
    InvalidCoverages.emplace(Offset, Symbol);
}

// Record symbols with invalid locations.
void LVScopeCompileUnit::addInvalidLocation(LVLocation *Location) {
  addInvalidLocationOrRange(Location, Location->getParentSymbol(),
                            &InvalidLocations);
}

// Record scopes with invalid ranges.
void LVScopeCompileUnit::addInvalidRange(LVLocation *Location) {
  addInvalidLocationOrRange(Location, Location->getParentScope(),
                            &InvalidRanges);
}

// Record line zero.
void LVScopeCompileUnit::addLineZero(LVLine *Line) {
  LVScope *Scope = Line->getParentScope();
  LVOffset Offset = Scope->getOffset();
  addInvalidOffset(Offset, Scope);
  addItem<LVOffsetLinesMap, LVOffset, LVLine *>(&LinesZero, Offset, Line);
}

void LVScopeCompileUnit::printLocalNames(raw_ostream &OS, bool Full) const {
  if (!options().getPrintFormatting())
    return;

  // Calculate an indentation value, to preserve a nice layout.
  size_t Indentation = options().indentationSize() +
                       lineNumberAsString().length() +
                       indentAsString(getLevel() + 1).length() + 3;

  enum class Option { Directory, File };
  auto PrintNames = [&](Option Action) {
    StringRef Kind = Action == Option::Directory ? "Directory" : "File";
    std::set<std::string> UniqueNames;
    for (size_t Index : Filenames) {
      // In the case of missing directory name in the .debug_line table,
      // the returned string has a leading '/'.
      StringRef Name = getStringPool().getString(Index);
      size_t Pos = Name.rfind('/');
      if (Pos != std::string::npos)
        Name = (Action == Option::File) ? Name.substr(Pos + 1)
                                        : Name.substr(0, Pos);
      // Collect only unique names.
      UniqueNames.insert(std::string(Name));
    }
    for (const std::string &Name : UniqueNames)
      OS << std::string(Indentation, ' ') << formattedKind(Kind) << " "
         << formattedName(Name) << "\n";
  };

  if (options().getAttributeDirectories())
    PrintNames(Option::Directory);
  if (options().getAttributeFiles())
    PrintNames(Option::File);
  if (options().getAttributePublics()) {
    StringRef Kind = "Public";
    // The public names are indexed by 'LVScope *'. We want to print
    // them by logical element address, to show the scopes layout.
    using OffsetSorted = std::map<LVAddress, LVPublicNames::const_iterator>;
    OffsetSorted SortedNames;
    for (LVPublicNames::const_iterator Iter = PublicNames.begin();
         Iter != PublicNames.end(); ++Iter)
      SortedNames.emplace(Iter->first->getOffset(), Iter);

    LVPublicNames::const_iterator Iter;
    for (OffsetSorted::reference Entry : SortedNames) {
      Iter = Entry.second;
      OS << std::string(Indentation, ' ') << formattedKind(Kind) << " "
         << formattedName((*Iter).first->getName());
      if (options().getAttributeOffset()) {
        LVAddress Address = (*Iter).second.first;
        size_t Size = (*Iter).second.second;
        OS << " [" << hexString(Address) << ":" << hexString(Address + Size)
           << "]";
      }
      OS << "\n";
    }
  }
}

void LVScopeCompileUnit::printWarnings(raw_ostream &OS, bool Full) const {
  auto PrintHeader = [&](const char *Header) { OS << "\n" << Header << ":\n"; };
  auto PrintFooter = [&](auto &Set) {
    if (Set.empty())
      OS << "None\n";
  };
  auto PrintOffset = [&](unsigned &Count, LVOffset Offset) {
    if (Count == 5) {
      Count = 0;
      OS << "\n";
    }
    ++Count;
    OS << hexSquareString(Offset) << " ";
  };
  auto PrintElement = [&](const LVOffsetElementMap &Map, LVOffset Offset) {
    LVOffsetElementMap::const_iterator Iter = Map.find(Offset);
    LVElement *Element = Iter != Map.end() ? Iter->second : nullptr;
    OS << "[" << hexString(Offset) << "]";
    if (Element)
      OS << " " << formattedKind(Element->kind()) << " "
         << formattedName(Element->getName());
    OS << "\n";
  };
  auto PrintInvalidLocations = [&](const LVOffsetLocationsMap &Map,
                                   const char *Header) {
    PrintHeader(Header);
    for (LVOffsetLocationsMap::const_reference Entry : Map) {
      PrintElement(WarningOffsets, Entry.first);
      for (const LVLocation *Location : Entry.second)
        OS << hexSquareString(Location->getOffset()) << " "
           << Location->getIntervalInfo() << "\n";
    }
    PrintFooter(Map);
  };

  if (options().getInternalTag() && getReader().isBinaryTypeELF()) {
    PrintHeader("Unsupported DWARF Tags");
    for (LVTagOffsetsMap::const_reference Entry : DebugTags) {
      OS << format("\n0x%02x", (unsigned)Entry.first) << ", "
         << dwarf::TagString(Entry.first) << "\n";
      unsigned Count = 0;
      for (const LVOffset &Offset : Entry.second)
        PrintOffset(Count, Offset);
      OS << "\n";
    }
    PrintFooter(DebugTags);
  }

  if (options().getWarningCoverages()) {
    PrintHeader("Symbols Invalid Coverages");
    for (LVOffsetSymbolMap::const_reference Entry : InvalidCoverages) {
      // Symbol basic information.
      LVSymbol *Symbol = Entry.second;
      OS << hexSquareString(Entry.first) << " {Coverage} "
         << format("%.2f%%", Symbol->getCoveragePercentage()) << " "
         << formattedKind(Symbol->kind()) << " "
         << formattedName(Symbol->getName()) << "\n";
    }
    PrintFooter(InvalidCoverages);
  }

  if (options().getWarningLines()) {
    PrintHeader("Lines Zero References");
    for (LVOffsetLinesMap::const_reference Entry : LinesZero) {
      PrintElement(WarningOffsets, Entry.first);
      unsigned Count = 0;
      for (const LVLine *Line : Entry.second)
        PrintOffset(Count, Line->getOffset());
      OS << "\n";
    }
    PrintFooter(LinesZero);
  }

  if (options().getWarningLocations())
    PrintInvalidLocations(InvalidLocations, "Invalid Location Ranges");

  if (options().getWarningRanges())
    PrintInvalidLocations(InvalidRanges, "Invalid Code Ranges");
}

void LVScopeCompileUnit::printTotals(raw_ostream &OS) const {
  OS << "\nTotals by lexical level:\n";
  for (size_t Index = 1; Index <= MaxSeenLevel; ++Index)
    OS << format("[%03d]: %10d (%6.2f%%)\n", Index, Totals[Index].first,
                 Totals[Index].second);
}

void LVScopeCompileUnit::printScopeSize(const LVScope *Scope, raw_ostream &OS) {
  LVSizesMap::const_iterator Iter = Sizes.find(Scope);
  if (Iter != Sizes.end()) {
    LVOffset Size = Iter->second;
    assert(CUContributionSize && "Invalid CU contribution size.");
    // Get a percentage rounded to two decimal digits. This avoids
    // implementation-defined rounding inside printing functions.
    float Percentage =
        rint((float(Size) / CUContributionSize) * 100.0 * 100.0) / 100.0;
    OS << format("%10" PRId64 " (%6.2f%%) : ", Size, Percentage);
    Scope->print(OS);

    // Keep record of the total sizes at each lexical level.
    LVLevel Level = Scope->getLevel();
    if (Level > MaxSeenLevel)
      MaxSeenLevel = Level;
    if (Level >= Totals.size())
      Totals.resize(2 * Level);
    Totals[Level].first += Size;
    Totals[Level].second += Percentage;
  }
}

void LVScopeCompileUnit::printSizes(raw_ostream &OS) const {
  // Recursively print the contributions for each scope.
  std::function<void(const LVScope *Scope)> PrintScope =
      [&](const LVScope *Scope) {
        // If we have selection criteria, then use only the selected scopes.
        if (options().getSelectExecute() && options().getReportAnyView()) {
          for (const LVScope *Scope : MatchedScopes)
            if (Scope->getLevel() < options().getOutputLevel())
              printScopeSize(Scope, OS);
          return;
        }
        if (Scope->getLevel() < options().getOutputLevel()) {
          if (const LVScopes *Scopes = Scope->getScopes())
            for (const LVScope *Scope : *Scopes) {
              printScopeSize(Scope, OS);
              PrintScope(Scope);
            }
        }
      };

  bool PrintScopes = options().getPrintScopes();
  if (!PrintScopes)
    options().setPrintScopes();
  getReader().setCompileUnit(const_cast<LVScopeCompileUnit *>(this));

  OS << "\nScope Sizes:\n";
  options().resetPrintFormatting();
  options().setPrintOffset();

  // Print the scopes regardless if the user has requested any scopes
  // printing. Set the option just to allow printing the contributions.
  printScopeSize(this, OS);
  PrintScope(this);

  // Print total scope sizes by level.
  printTotals(OS);

  options().resetPrintOffset();
  options().setPrintFormatting();

  if (!PrintScopes)
    options().resetPrintScopes();
}

void LVScopeCompileUnit::printSummary(raw_ostream &OS) const {
  printSummary(OS, options().getSelectExecute() ? Found : Printed, "Printed");
}

// Print summary details for the scopes tree.
void LVScopeCompileUnit::printSummary(raw_ostream &OS, const LVCounter &Counter,
                                      const char *Header) const {
  std::string Separator = std::string(29, '-');
  auto PrintSeparator = [&]() { OS << Separator << "\n"; };
  auto PrintHeadingRow = [&](const char *T, const char *U, const char *V) {
    OS << format("%-9s%9s  %9s\n", T, U, V);
  };
  auto PrintDataRow = [&](const char *T, unsigned U, unsigned V) {
    OS << format("%-9s%9d  %9d\n", T, U, V);
  };

  OS << "\n";
  PrintSeparator();
  PrintHeadingRow("Element", "Total", Header);
  PrintSeparator();
  PrintDataRow("Scopes", Allocated.Scopes, Counter.Scopes);
  PrintDataRow("Symbols", Allocated.Symbols, Counter.Symbols);
  PrintDataRow("Types", Allocated.Types, Counter.Types);
  PrintDataRow("Lines", Allocated.Lines, Counter.Lines);
  PrintSeparator();
  PrintDataRow(
      "Total",
      Allocated.Scopes + Allocated.Symbols + Allocated.Lines + Allocated.Types,
      Counter.Scopes + Counter.Symbols + Counter.Lines + Counter.Types);
}

void LVScopeCompileUnit::printMatchedElements(raw_ostream &OS,
                                              bool UseMatchedElements) {
  LVSortFunction SortFunction = getSortFunction();
  if (SortFunction)
    std::stable_sort(MatchedElements.begin(), MatchedElements.end(),
                     SortFunction);

  // Check the type of elements required to be printed. 'MatchedElements'
  // contains generic elements (lines, scopes, symbols, types). If we have a
  // request to print any generic element, then allow the normal printing.
  if (options().getPrintAnyElement()) {
    if (UseMatchedElements)
      OS << "\n";
    print(OS);

    if (UseMatchedElements) {
      // Print the details for the matched elements.
      for (const LVElement *Element : MatchedElements)
        Element->print(OS);
    } else {
      // Print the view for the matched scopes.
      for (const LVScope *Scope : MatchedScopes) {
        Scope->print(OS);
        if (const LVElements *Elements = Scope->getChildren())
          for (LVElement *Element : *Elements)
            Element->print(OS);
      }
    }

    // Print any requested summary.
    if (options().getPrintSummary()) {
      // In the case of '--report=details' the matched elements are
      // already counted; just proceed to print any requested summary.
      // Otherwise, count them and print the summary.
      if (!options().getReportList()) {
        for (LVElement *Element : MatchedElements) {
          if (!Element->getIncludeInPrint())
            continue;
          if (Element->getIsType())
            ++Found.Types;
          else if (Element->getIsSymbol())
            ++Found.Symbols;
          else if (Element->getIsScope())
            ++Found.Scopes;
          else if (Element->getIsLine())
            ++Found.Lines;
          else
            assert(Element && "Invalid element.");
        }
      }
      printSummary(OS, Found, "Printed");
    }
  }

  // Check if we have a request to print sizes for the matched elements
  // that are scopes.
  if (options().getPrintSizes()) {
    OS << "\n";
    print(OS);

    OS << "\nScope Sizes:\n";
    printScopeSize(this, OS);
    for (LVElement *Element : MatchedElements)
      if (Element->getIsScope())
        // Print sizes only for scopes.
        printScopeSize(static_cast<LVScope *>(Element), OS);

    printTotals(OS);
  }
}

void LVScopeCompileUnit::print(raw_ostream &OS, bool Full) const {
  // Reset counters for printed and found elements.
  const_cast<LVScopeCompileUnit *>(this)->Found.reset();
  const_cast<LVScopeCompileUnit *>(this)->Printed.reset();

  if (getReader().doPrintScope(this) && options().getPrintFormatting())
    OS << "\n";

  LVScope::print(OS, Full);
}

void LVScopeCompileUnit::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " '" << getName() << "'\n";
  if (options().getPrintFormatting() && options().getAttributeProducer())
    printAttributes(OS, Full, "{Producer} ",
                    const_cast<LVScopeCompileUnit *>(this), getProducer(),
                    /*UseQuotes=*/true,
                    /*PrintRef=*/false);

  // Reset file index, to allow its children to print the correct filename.
  options().resetFilenameIndex();

  // Print any files, directories, public names and active ranges.
  if (Full) {
    printLocalNames(OS, Full);
    printActiveRanges(OS, Full);
  }
}

//===----------------------------------------------------------------------===//
// DWARF enumeration (DW_TAG_enumeration_type).
//===----------------------------------------------------------------------===//
bool LVScopeEnumeration::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;
  return equalNumberOfChildren(Scope);
}

void LVScopeEnumeration::printExtra(raw_ostream &OS, bool Full) const {
  // Print the full type name.
  OS << formattedKind(kind()) << " " << (getIsEnumClass() ? "class " : "")
     << formattedName(getName());
  if (getHasType())
    OS << " -> " << typeOffsetAsString()
       << formattedNames(getTypeQualifiedName(), typeAsString());
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF formal parameter pack (DW_TAG_GNU_formal_parameter_pack).
//===----------------------------------------------------------------------===//
bool LVScopeFormalPack::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;
  return equalNumberOfChildren(Scope);
}

void LVScopeFormalPack::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF function.
//===----------------------------------------------------------------------===//
void LVScopeFunction::resolveReferences() {
  // Before we resolve any references to other elements, check if we have
  // to insert missing elements, that have been stripped, which will help
  // the logical view comparison.
  if (options().getAttributeInserted() && getHasReferenceAbstract() &&
      !getAddedMissing()) {
    // Add missing elements at the function scope.
    addMissingElements(getReference());
    if (Scopes)
      for (LVScope *Scope : *Scopes)
        if (Scope->getHasReferenceAbstract() && !Scope->getAddedMissing())
          Scope->addMissingElements(Scope->getReference());
  }

  LVScope::resolveReferences();

  // The DWARF 'extern' attribute is generated at the class level.
  // 0000003f DW_TAG_class_type "CLASS"
  //   00000048 DW_TAG_subprogram "bar"
  //	            DW_AT_external DW_FORM_flag_present
  // 00000070 DW_TAG_subprogram "bar"
  //   DW_AT_specification DW_FORM_ref4 0x00000048
  // CodeView does not include any information at the class level to
  // mark the member function as external.
  // If there is a reference linking the declaration and definition, mark
  // the definition as extern, to facilitate the logical view comparison.
  if (getHasReferenceSpecification()) {
    LVScope *Reference = getReference();
    if (Reference && Reference->getIsExternal()) {
      Reference->resetIsExternal();
      setIsExternal();
    }
  }

  // Resolve the function associated type.
  if (!getType())
    if (LVScope *Reference = getReference())
      setType(Reference->getType());
}

void LVScopeFunction::setName(StringRef ObjectName) {
  LVScope::setName(ObjectName);
  // Check for system generated functions.
  getReader().isSystemEntry(this, ObjectName);
}

void LVScopeFunction::resolveExtra() {
  // Check if we need to encode the template arguments.
  if (getIsTemplate())
    resolveTemplate();
}

bool LVScopeFunction::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;

  // When comparing logical elements, ignore any difference in the children.
  if (options().getCompareContext() && !equalNumberOfChildren(Scope))
    return false;

  // Check if the linkage name matches.
  if (getLinkageNameIndex() != Scope->getLinkageNameIndex())
    return false;

  // Check if the parameters match in the case of templates.
  if (!LVType::parametersMatch(getTypes(), Scope->getTypes()))
    return false;

  // Check if the arguments match.
  if (!LVSymbol::parametersMatch(getSymbols(), Scope->getSymbols()))
    return false;

  // Check if the lines match.
  if (options().getCompareLines() &&
      !LVLine::equals(getLines(), Scope->getLines()))
    return false;

  // Check if any reference is the same.
  if (!referenceMatch(Scope))
    return false;

  if (getReference() && !getReference()->equals(Scope->getReference()))
    return false;

  return true;
}

LVScope *LVScopeFunction::findEqualScope(const LVScopes *Scopes) const {
  assert(Scopes && "Scopes must not be nullptr");
  // Go through candidates and try to find a best match.
  for (LVScope *Scope : *Scopes)
    // Match arguments, children, lines, references.
    if (equals(Scope))
      return Scope;
  return nullptr;
}

void LVScopeFunction::printExtra(raw_ostream &OS, bool Full) const {
  LVScope *Reference = getReference();

  // Inline attributes based on the reference element.
  uint32_t InlineCode =
      Reference ? Reference->getInlineCode() : getInlineCode();

  // Accessibility depends on the parent (class, structure).
  uint32_t AccessCode = 0;
  if (getIsMember())
    AccessCode = getParentScope()->getIsClass() ? dwarf::DW_ACCESS_private
                                                : dwarf::DW_ACCESS_public;

  std::string Attributes =
      getIsCallSite()
          ? ""
          : formatAttributes(externalString(), accessibilityString(AccessCode),
                             inlineCodeString(InlineCode), virtualityString());

  OS << formattedKind(kind()) << " " << Attributes << formattedName(getName())
     << discriminatorAsString() << " -> " << typeOffsetAsString()
     << formattedNames(getTypeQualifiedName(), typeAsString()) << "\n";

  // Print any active ranges.
  if (Full) {
    if (getIsTemplateResolved())
      printEncodedArgs(OS, Full);
    printActiveRanges(OS, Full);
    if (getLinkageNameIndex())
      printLinkageName(OS, Full, const_cast<LVScopeFunction *>(this),
                       const_cast<LVScopeFunction *>(this));
    if (Reference)
      Reference->printReference(OS, Full, const_cast<LVScopeFunction *>(this));
  }
}

//===----------------------------------------------------------------------===//
// DWARF inlined function (DW_TAG_inlined_function).
//===----------------------------------------------------------------------===//
void LVScopeFunctionInlined::resolveExtra() {
  // Check if we need to encode the template arguments.
  if (getIsTemplate())
    resolveTemplate();
}

bool LVScopeFunctionInlined::equals(const LVScope *Scope) const {
  if (!LVScopeFunction::equals(Scope))
    return false;

  // Check if any reference is the same.
  if (getHasDiscriminator() && Scope->getHasDiscriminator())
    if (getDiscriminator() != Scope->getDiscriminator())
      return false;

  // Check the call site information.
  if (getCallFilenameIndex() != Scope->getCallFilenameIndex() ||
      getCallLineNumber() != Scope->getCallLineNumber())
    return false;

  return true;
}

LVScope *LVScopeFunctionInlined::findEqualScope(const LVScopes *Scopes) const {
  return LVScopeFunction::findEqualScope(Scopes);
}

void LVScopeFunctionInlined::printExtra(raw_ostream &OS, bool Full) const {
  LVScopeFunction::printExtra(OS, Full);
}

//===----------------------------------------------------------------------===//
// DWARF subroutine type.
//===----------------------------------------------------------------------===//
// Resolve a Subroutine Type (Callback).
void LVScopeFunctionType::resolveExtra() {
  if (getIsMemberPointerResolved())
    return;
  setIsMemberPointerResolved();

  // The encoded string has the return type and the formal parameters type.
  std::string Name(typeAsString());
  Name.append(" (*)");
  Name.append("(");

  // Traverse the scope symbols, looking for those which are parameters.
  if (const LVSymbols *Symbols = getSymbols()) {
    bool AddComma = false;
    for (LVSymbol *Symbol : *Symbols)
      if (Symbol->getIsParameter()) {
        Symbol->resolve();
        if (LVElement *Type = Symbol->getType())
          Type->resolveName();
        if (AddComma)
          Name.append(", ");
        Name.append(std::string(Symbol->getTypeName()));
        AddComma = true;
      }
  }

  Name.append(")");

  // Update the scope name, to reflect the encoded parameters.
  setName(Name);
}

//===----------------------------------------------------------------------===//
// DWARF namespace (DW_TAG_namespace).
//===----------------------------------------------------------------------===//
bool LVScopeNamespace::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;

  if (!equalNumberOfChildren(Scope))
    return false;

  // Check if any reference is the same.
  if (!referenceMatch(Scope))
    return false;

  if (getReference() && !getReference()->equals(Scope->getReference()))
    return false;

  return true;
}

LVScope *LVScopeNamespace::findEqualScope(const LVScopes *Scopes) const {
  assert(Scopes && "Scopes must not be nullptr");
  // Go through candidates and try to find a best match.
  for (LVScope *Scope : *Scopes)
    if (equals(Scope))
      return Scope;
  return nullptr;
}

void LVScopeNamespace::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << "\n";

  // Print any active ranges.
  if (Full) {
    printActiveRanges(OS, Full);

    if (LVScope *Reference = getReference())
      Reference->printReference(OS, Full, const_cast<LVScopeNamespace *>(this));
  }
}

//===----------------------------------------------------------------------===//
// An object file (single or multiple CUs).
//===----------------------------------------------------------------------===//
void LVScopeRoot::processRangeInformation() {
  if (!options().getAttributeAnyLocation())
    return;

  if (Scopes)
    for (LVScope *Scope : *Scopes) {
      LVScopeCompileUnit *CompileUnit =
          static_cast<LVScopeCompileUnit *>(Scope);
      getReader().setCompileUnit(CompileUnit);
      CompileUnit->processRangeLocationCoverage();
    }
}

void LVScopeRoot::transformScopedName() {
  // Recursively transform all names.
  std::function<void(LVScope * Parent)> TraverseScope = [&](LVScope *Parent) {
    auto Traverse = [&](const auto *Set) {
      if (Set)
        for (const auto &Entry : *Set)
          Entry->setInnerComponent();
    };
    if (const LVScopes *Scopes = Parent->getScopes())
      for (LVScope *Scope : *Scopes) {
        Scope->setInnerComponent();
        TraverseScope(Scope);
      }
    Traverse(Parent->getSymbols());
    Traverse(Parent->getTypes());
    Traverse(Parent->getLines());
  };

  // Start traversing the scopes root and transform the element name.
  TraverseScope(this);
}

bool LVScopeRoot::equals(const LVScope *Scope) const {
  return LVScope::equals(Scope);
}

void LVScopeRoot::print(raw_ostream &OS, bool Full) const {
  OS << "\nLogical View:\n";
  LVScope::print(OS, Full);
}

void LVScopeRoot::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << "";
  if (options().getAttributeFormat())
    OS << " -> " << getFileFormatName();
  OS << "\n";
}

Error LVScopeRoot::doPrintMatches(bool Split, raw_ostream &OS,
                                  bool UseMatchedElements) const {
  // During a view output splitting, use the output stream created by the
  // split context, then switch to the reader output stream.
  static raw_ostream *StreamSplit = &OS;

  if (Scopes) {
    if (UseMatchedElements)
      options().resetPrintFormatting();
    print(OS);

    for (LVScope *Scope : *Scopes) {
      getReader().setCompileUnit(const_cast<LVScope *>(Scope));

      // If 'Split', we use the scope name (CU name) as the ouput file; the
      // delimiters in the pathname, must be replaced by a normal character.
      if (Split) {
        std::string ScopeName(Scope->getName());
        if (std::error_code EC =
                getReaderSplitContext().open(ScopeName, ".txt", OS))
          return createStringError(EC, "Unable to create split output file %s",
                                   ScopeName.c_str());
        StreamSplit = static_cast<raw_ostream *>(&getReaderSplitContext().os());
      }

      Scope->printMatchedElements(*StreamSplit, UseMatchedElements);

      // Done printing the compile unit. Restore the original output context.
      if (Split) {
        getReaderSplitContext().close();
        StreamSplit = &getReader().outputStream();
      }
    }
    if (UseMatchedElements)
      options().setPrintFormatting();
  }

  return Error::success();
}

//===----------------------------------------------------------------------===//
// DWARF template parameter pack (DW_TAG_GNU_template_parameter_pack).
//===----------------------------------------------------------------------===//
bool LVScopeTemplatePack::equals(const LVScope *Scope) const {
  if (!LVScope::equals(Scope))
    return false;
  return equalNumberOfChildren(Scope);
}

void LVScopeTemplatePack::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << "\n";
}
