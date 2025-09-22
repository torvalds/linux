//===- CXXInheritance.cpp - C++ Inheritance -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides routines that help analyzing C++ inheritance hierarchies.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CXXInheritance.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <utility>
#include <cassert>
#include <vector>

using namespace clang;

/// isAmbiguous - Determines whether the set of paths provided is
/// ambiguous, i.e., there are two or more paths that refer to
/// different base class subobjects of the same type. BaseType must be
/// an unqualified, canonical class type.
bool CXXBasePaths::isAmbiguous(CanQualType BaseType) {
  BaseType = BaseType.getUnqualifiedType();
  IsVirtBaseAndNumberNonVirtBases Subobjects = ClassSubobjects[BaseType];
  return Subobjects.NumberOfNonVirtBases + (Subobjects.IsVirtBase ? 1 : 0) > 1;
}

/// clear - Clear out all prior path information.
void CXXBasePaths::clear() {
  Paths.clear();
  ClassSubobjects.clear();
  VisitedDependentRecords.clear();
  ScratchPath.clear();
  DetectedVirtual = nullptr;
}

/// Swaps the contents of this CXXBasePaths structure with the
/// contents of Other.
void CXXBasePaths::swap(CXXBasePaths &Other) {
  std::swap(Origin, Other.Origin);
  Paths.swap(Other.Paths);
  ClassSubobjects.swap(Other.ClassSubobjects);
  VisitedDependentRecords.swap(Other.VisitedDependentRecords);
  std::swap(FindAmbiguities, Other.FindAmbiguities);
  std::swap(RecordPaths, Other.RecordPaths);
  std::swap(DetectVirtual, Other.DetectVirtual);
  std::swap(DetectedVirtual, Other.DetectedVirtual);
}

bool CXXRecordDecl::isDerivedFrom(const CXXRecordDecl *Base) const {
  CXXBasePaths Paths(/*FindAmbiguities=*/false, /*RecordPaths=*/false,
                     /*DetectVirtual=*/false);
  return isDerivedFrom(Base, Paths);
}

bool CXXRecordDecl::isDerivedFrom(const CXXRecordDecl *Base,
                                  CXXBasePaths &Paths) const {
  if (getCanonicalDecl() == Base->getCanonicalDecl())
    return false;

  Paths.setOrigin(const_cast<CXXRecordDecl*>(this));

  const CXXRecordDecl *BaseDecl = Base->getCanonicalDecl();
  return lookupInBases(
      [BaseDecl](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
        return Specifier->getType()->getAsRecordDecl() &&
               FindBaseClass(Specifier, Path, BaseDecl);
      },
      Paths);
}

bool CXXRecordDecl::isVirtuallyDerivedFrom(const CXXRecordDecl *Base) const {
  if (!getNumVBases())
    return false;

  CXXBasePaths Paths(/*FindAmbiguities=*/false, /*RecordPaths=*/false,
                     /*DetectVirtual=*/false);

  if (getCanonicalDecl() == Base->getCanonicalDecl())
    return false;

  Paths.setOrigin(const_cast<CXXRecordDecl*>(this));

  const CXXRecordDecl *BaseDecl = Base->getCanonicalDecl();
  return lookupInBases(
      [BaseDecl](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
        return FindVirtualBaseClass(Specifier, Path, BaseDecl);
      },
      Paths);
}

bool CXXRecordDecl::isProvablyNotDerivedFrom(const CXXRecordDecl *Base) const {
  const CXXRecordDecl *TargetDecl = Base->getCanonicalDecl();
  return forallBases([TargetDecl](const CXXRecordDecl *Base) {
    return Base->getCanonicalDecl() != TargetDecl;
  });
}

bool
CXXRecordDecl::isCurrentInstantiation(const DeclContext *CurContext) const {
  assert(isDependentContext());

  for (; !CurContext->isFileContext(); CurContext = CurContext->getParent())
    if (CurContext->Equals(this))
      return true;

  return false;
}

bool CXXRecordDecl::forallBases(ForallBasesCallback BaseMatches) const {
  SmallVector<const CXXRecordDecl*, 8> Queue;

  const CXXRecordDecl *Record = this;
  while (true) {
    for (const auto &I : Record->bases()) {
      const RecordType *Ty = I.getType()->getAs<RecordType>();
      if (!Ty)
        return false;

      CXXRecordDecl *Base =
            cast_or_null<CXXRecordDecl>(Ty->getDecl()->getDefinition());
      if (!Base ||
          (Base->isDependentContext() &&
           !Base->isCurrentInstantiation(Record))) {
        return false;
      }

      Queue.push_back(Base);
      if (!BaseMatches(Base))
        return false;
    }

    if (Queue.empty())
      break;
    Record = Queue.pop_back_val(); // not actually a queue.
  }

  return true;
}

bool CXXBasePaths::lookupInBases(ASTContext &Context,
                                 const CXXRecordDecl *Record,
                                 CXXRecordDecl::BaseMatchesCallback BaseMatches,
                                 bool LookupInDependent) {
  bool FoundPath = false;

  // The access of the path down to this record.
  AccessSpecifier AccessToHere = ScratchPath.Access;
  bool IsFirstStep = ScratchPath.empty();

  for (const auto &BaseSpec : Record->bases()) {
    // Find the record of the base class subobjects for this type.
    QualType BaseType =
        Context.getCanonicalType(BaseSpec.getType()).getUnqualifiedType();

    // C++ [temp.dep]p3:
    //   In the definition of a class template or a member of a class template,
    //   if a base class of the class template depends on a template-parameter,
    //   the base class scope is not examined during unqualified name lookup
    //   either at the point of definition of the class template or member or
    //   during an instantiation of the class tem- plate or member.
    if (!LookupInDependent && BaseType->isDependentType())
      continue;

    // Determine whether we need to visit this base class at all,
    // updating the count of subobjects appropriately.
    IsVirtBaseAndNumberNonVirtBases &Subobjects = ClassSubobjects[BaseType];
    bool VisitBase = true;
    bool SetVirtual = false;
    if (BaseSpec.isVirtual()) {
      VisitBase = !Subobjects.IsVirtBase;
      Subobjects.IsVirtBase = true;
      if (isDetectingVirtual() && DetectedVirtual == nullptr) {
        // If this is the first virtual we find, remember it. If it turns out
        // there is no base path here, we'll reset it later.
        DetectedVirtual = BaseType->getAs<RecordType>();
        SetVirtual = true;
      }
    } else {
      ++Subobjects.NumberOfNonVirtBases;
    }
    if (isRecordingPaths()) {
      // Add this base specifier to the current path.
      CXXBasePathElement Element;
      Element.Base = &BaseSpec;
      Element.Class = Record;
      if (BaseSpec.isVirtual())
        Element.SubobjectNumber = 0;
      else
        Element.SubobjectNumber = Subobjects.NumberOfNonVirtBases;
      ScratchPath.push_back(Element);

      // Calculate the "top-down" access to this base class.
      // The spec actually describes this bottom-up, but top-down is
      // equivalent because the definition works out as follows:
      // 1. Write down the access along each step in the inheritance
      //    chain, followed by the access of the decl itself.
      //    For example, in
      //      class A { public: int foo; };
      //      class B : protected A {};
      //      class C : public B {};
      //      class D : private C {};
      //    we would write:
      //      private public protected public
      // 2. If 'private' appears anywhere except far-left, access is denied.
      // 3. Otherwise, overall access is determined by the most restrictive
      //    access in the sequence.
      if (IsFirstStep)
        ScratchPath.Access = BaseSpec.getAccessSpecifier();
      else
        ScratchPath.Access = CXXRecordDecl::MergeAccess(AccessToHere,
                                                 BaseSpec.getAccessSpecifier());
    }

    // Track whether there's a path involving this specific base.
    bool FoundPathThroughBase = false;

    if (BaseMatches(&BaseSpec, ScratchPath)) {
      // We've found a path that terminates at this base.
      FoundPath = FoundPathThroughBase = true;
      if (isRecordingPaths()) {
        // We have a path. Make a copy of it before moving on.
        Paths.push_back(ScratchPath);
      } else if (!isFindingAmbiguities()) {
        // We found a path and we don't care about ambiguities;
        // return immediately.
        return FoundPath;
      }
    } else if (VisitBase) {
      CXXRecordDecl *BaseRecord;
      if (LookupInDependent) {
        BaseRecord = nullptr;
        const TemplateSpecializationType *TST =
            BaseSpec.getType()->getAs<TemplateSpecializationType>();
        if (!TST) {
          if (auto *RT = BaseSpec.getType()->getAs<RecordType>())
            BaseRecord = cast<CXXRecordDecl>(RT->getDecl());
        } else {
          TemplateName TN = TST->getTemplateName();
          if (auto *TD =
                  dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl()))
            BaseRecord = TD->getTemplatedDecl();
        }
        if (BaseRecord) {
          if (!BaseRecord->hasDefinition() ||
              VisitedDependentRecords.count(BaseRecord)) {
            BaseRecord = nullptr;
          } else {
            VisitedDependentRecords.insert(BaseRecord);
          }
        }
      } else {
        BaseRecord = cast<CXXRecordDecl>(
            BaseSpec.getType()->castAs<RecordType>()->getDecl());
      }
      if (BaseRecord &&
          lookupInBases(Context, BaseRecord, BaseMatches, LookupInDependent)) {
        // C++ [class.member.lookup]p2:
        //   A member name f in one sub-object B hides a member name f in
        //   a sub-object A if A is a base class sub-object of B. Any
        //   declarations that are so hidden are eliminated from
        //   consideration.

        // There is a path to a base class that meets the criteria. If we're
        // not collecting paths or finding ambiguities, we're done.
        FoundPath = FoundPathThroughBase = true;
        if (!isFindingAmbiguities())
          return FoundPath;
      }
    }

    // Pop this base specifier off the current path (if we're
    // collecting paths).
    if (isRecordingPaths()) {
      ScratchPath.pop_back();
    }

    // If we set a virtual earlier, and this isn't a path, forget it again.
    if (SetVirtual && !FoundPathThroughBase) {
      DetectedVirtual = nullptr;
    }
  }

  // Reset the scratch path access.
  ScratchPath.Access = AccessToHere;

  return FoundPath;
}

bool CXXRecordDecl::lookupInBases(BaseMatchesCallback BaseMatches,
                                  CXXBasePaths &Paths,
                                  bool LookupInDependent) const {
  // If we didn't find anything, report that.
  if (!Paths.lookupInBases(getASTContext(), this, BaseMatches,
                           LookupInDependent))
    return false;

  // If we're not recording paths or we won't ever find ambiguities,
  // we're done.
  if (!Paths.isRecordingPaths() || !Paths.isFindingAmbiguities())
    return true;

  // C++ [class.member.lookup]p6:
  //   When virtual base classes are used, a hidden declaration can be
  //   reached along a path through the sub-object lattice that does
  //   not pass through the hiding declaration. This is not an
  //   ambiguity. The identical use with nonvirtual base classes is an
  //   ambiguity; in that case there is no unique instance of the name
  //   that hides all the others.
  //
  // FIXME: This is an O(N^2) algorithm, but DPG doesn't see an easy
  // way to make it any faster.
  Paths.Paths.remove_if([&Paths](const CXXBasePath &Path) {
    for (const CXXBasePathElement &PE : Path) {
      if (!PE.Base->isVirtual())
        continue;

      CXXRecordDecl *VBase = nullptr;
      if (const RecordType *Record = PE.Base->getType()->getAs<RecordType>())
        VBase = cast<CXXRecordDecl>(Record->getDecl());
      if (!VBase)
        break;

      // The declaration(s) we found along this path were found in a
      // subobject of a virtual base. Check whether this virtual
      // base is a subobject of any other path; if so, then the
      // declaration in this path are hidden by that patch.
      for (const CXXBasePath &HidingP : Paths) {
        CXXRecordDecl *HidingClass = nullptr;
        if (const RecordType *Record =
                HidingP.back().Base->getType()->getAs<RecordType>())
          HidingClass = cast<CXXRecordDecl>(Record->getDecl());
        if (!HidingClass)
          break;

        if (HidingClass->isVirtuallyDerivedFrom(VBase))
          return true;
      }
    }
    return false;
  });

  return true;
}

bool CXXRecordDecl::FindBaseClass(const CXXBaseSpecifier *Specifier,
                                  CXXBasePath &Path,
                                  const CXXRecordDecl *BaseRecord) {
  assert(BaseRecord->getCanonicalDecl() == BaseRecord &&
         "User data for FindBaseClass is not canonical!");
  return Specifier->getType()->castAs<RecordType>()->getDecl()
            ->getCanonicalDecl() == BaseRecord;
}

bool CXXRecordDecl::FindVirtualBaseClass(const CXXBaseSpecifier *Specifier,
                                         CXXBasePath &Path,
                                         const CXXRecordDecl *BaseRecord) {
  assert(BaseRecord->getCanonicalDecl() == BaseRecord &&
         "User data for FindBaseClass is not canonical!");
  return Specifier->isVirtual() &&
         Specifier->getType()->castAs<RecordType>()->getDecl()
            ->getCanonicalDecl() == BaseRecord;
}

static bool isOrdinaryMember(const NamedDecl *ND) {
  return ND->isInIdentifierNamespace(Decl::IDNS_Ordinary | Decl::IDNS_Tag |
                                     Decl::IDNS_Member);
}

static bool findOrdinaryMember(const CXXRecordDecl *RD, CXXBasePath &Path,
                               DeclarationName Name) {
  Path.Decls = RD->lookup(Name).begin();
  for (DeclContext::lookup_iterator I = Path.Decls, E = I.end(); I != E; ++I)
    if (isOrdinaryMember(*I))
      return true;

  return false;
}

bool CXXRecordDecl::hasMemberName(DeclarationName Name) const {
  CXXBasePath P;
  if (findOrdinaryMember(this, P, Name))
    return true;

  CXXBasePaths Paths(false, false, false);
  return lookupInBases(
      [Name](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
        return findOrdinaryMember(Specifier->getType()->getAsCXXRecordDecl(),
                                  Path, Name);
      },
      Paths);
}

static bool
findOrdinaryMemberInDependentClasses(const CXXBaseSpecifier *Specifier,
                                     CXXBasePath &Path, DeclarationName Name) {
  const TemplateSpecializationType *TST =
      Specifier->getType()->getAs<TemplateSpecializationType>();
  if (!TST) {
    auto *RT = Specifier->getType()->getAs<RecordType>();
    if (!RT)
      return false;
    return findOrdinaryMember(cast<CXXRecordDecl>(RT->getDecl()), Path, Name);
  }
  TemplateName TN = TST->getTemplateName();
  const auto *TD = dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl());
  if (!TD)
    return false;
  CXXRecordDecl *RD = TD->getTemplatedDecl();
  if (!RD)
    return false;
  return findOrdinaryMember(RD, Path, Name);
}

std::vector<const NamedDecl *> CXXRecordDecl::lookupDependentName(
    DeclarationName Name,
    llvm::function_ref<bool(const NamedDecl *ND)> Filter) {
  std::vector<const NamedDecl *> Results;
  // Lookup in the class.
  bool AnyOrdinaryMembers = false;
  for (const NamedDecl *ND : lookup(Name)) {
    if (isOrdinaryMember(ND))
      AnyOrdinaryMembers = true;
    if (Filter(ND))
      Results.push_back(ND);
  }
  if (AnyOrdinaryMembers)
    return Results;

  // Perform lookup into our base classes.
  CXXBasePaths Paths;
  Paths.setOrigin(this);
  if (!lookupInBases(
          [&](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
            return findOrdinaryMemberInDependentClasses(Specifier, Path, Name);
          },
          Paths, /*LookupInDependent=*/true))
    return Results;
  for (DeclContext::lookup_iterator I = Paths.front().Decls, E = I.end();
       I != E; ++I) {
    if (isOrdinaryMember(*I) && Filter(*I))
      Results.push_back(*I);
  }
  return Results;
}

void OverridingMethods::add(unsigned OverriddenSubobject,
                            UniqueVirtualMethod Overriding) {
  SmallVectorImpl<UniqueVirtualMethod> &SubobjectOverrides
    = Overrides[OverriddenSubobject];
  if (!llvm::is_contained(SubobjectOverrides, Overriding))
    SubobjectOverrides.push_back(Overriding);
}

void OverridingMethods::add(const OverridingMethods &Other) {
  for (const_iterator I = Other.begin(), IE = Other.end(); I != IE; ++I) {
    for (overriding_const_iterator M = I->second.begin(),
                                MEnd = I->second.end();
         M != MEnd;
         ++M)
      add(I->first, *M);
  }
}

void OverridingMethods::replaceAll(UniqueVirtualMethod Overriding) {
  for (iterator I = begin(), IEnd = end(); I != IEnd; ++I) {
    I->second.clear();
    I->second.push_back(Overriding);
  }
}

namespace {

class FinalOverriderCollector {
  /// The number of subobjects of a given class type that
  /// occur within the class hierarchy.
  llvm::DenseMap<const CXXRecordDecl *, unsigned> SubobjectCount;

  /// Overriders for each virtual base subobject.
  llvm::DenseMap<const CXXRecordDecl *, CXXFinalOverriderMap *> VirtualOverriders;

  CXXFinalOverriderMap FinalOverriders;

public:
  ~FinalOverriderCollector();

  void Collect(const CXXRecordDecl *RD, bool VirtualBase,
               const CXXRecordDecl *InVirtualSubobject,
               CXXFinalOverriderMap &Overriders);
};

} // namespace

void FinalOverriderCollector::Collect(const CXXRecordDecl *RD,
                                      bool VirtualBase,
                                      const CXXRecordDecl *InVirtualSubobject,
                                      CXXFinalOverriderMap &Overriders) {
  unsigned SubobjectNumber = 0;
  if (!VirtualBase)
    SubobjectNumber
      = ++SubobjectCount[cast<CXXRecordDecl>(RD->getCanonicalDecl())];

  for (const auto &Base : RD->bases()) {
    if (const RecordType *RT = Base.getType()->getAs<RecordType>()) {
      const CXXRecordDecl *BaseDecl = cast<CXXRecordDecl>(RT->getDecl());
      if (!BaseDecl->isPolymorphic())
        continue;

      if (Overriders.empty() && !Base.isVirtual()) {
        // There are no other overriders of virtual member functions,
        // so let the base class fill in our overriders for us.
        Collect(BaseDecl, false, InVirtualSubobject, Overriders);
        continue;
      }

      // Collect all of the overridders from the base class subobject
      // and merge them into the set of overridders for this class.
      // For virtual base classes, populate or use the cached virtual
      // overrides so that we do not walk the virtual base class (and
      // its base classes) more than once.
      CXXFinalOverriderMap ComputedBaseOverriders;
      CXXFinalOverriderMap *BaseOverriders = &ComputedBaseOverriders;
      if (Base.isVirtual()) {
        CXXFinalOverriderMap *&MyVirtualOverriders = VirtualOverriders[BaseDecl];
        BaseOverriders = MyVirtualOverriders;
        if (!MyVirtualOverriders) {
          MyVirtualOverriders = new CXXFinalOverriderMap;

          // Collect may cause VirtualOverriders to reallocate, invalidating the
          // MyVirtualOverriders reference. Set BaseOverriders to the right
          // value now.
          BaseOverriders = MyVirtualOverriders;

          Collect(BaseDecl, true, BaseDecl, *MyVirtualOverriders);
        }
      } else
        Collect(BaseDecl, false, InVirtualSubobject, ComputedBaseOverriders);

      // Merge the overriders from this base class into our own set of
      // overriders.
      for (CXXFinalOverriderMap::iterator OM = BaseOverriders->begin(),
                               OMEnd = BaseOverriders->end();
           OM != OMEnd;
           ++OM) {
        const CXXMethodDecl *CanonOM = OM->first->getCanonicalDecl();
        Overriders[CanonOM].add(OM->second);
      }
    }
  }

  for (auto *M : RD->methods()) {
    // We only care about virtual methods.
    if (!M->isVirtual())
      continue;

    CXXMethodDecl *CanonM = M->getCanonicalDecl();
    using OverriddenMethodsRange =
        llvm::iterator_range<CXXMethodDecl::method_iterator>;
    OverriddenMethodsRange OverriddenMethods = CanonM->overridden_methods();

    if (OverriddenMethods.begin() == OverriddenMethods.end()) {
      // This is a new virtual function that does not override any
      // other virtual function. Add it to the map of virtual
      // functions for which we are tracking overridders.

      // C++ [class.virtual]p2:
      //   For convenience we say that any virtual function overrides itself.
      Overriders[CanonM].add(SubobjectNumber,
                             UniqueVirtualMethod(CanonM, SubobjectNumber,
                                                 InVirtualSubobject));
      continue;
    }

    // This virtual method overrides other virtual methods, so it does
    // not add any new slots into the set of overriders. Instead, we
    // replace entries in the set of overriders with the new
    // overrider. To do so, we dig down to the original virtual
    // functions using data recursion and update all of the methods it
    // overrides.
    SmallVector<OverriddenMethodsRange, 4> Stack(1, OverriddenMethods);
    while (!Stack.empty()) {
      for (const CXXMethodDecl *OM : Stack.pop_back_val()) {
        const CXXMethodDecl *CanonOM = OM->getCanonicalDecl();

        // C++ [class.virtual]p2:
        //   A virtual member function C::vf of a class object S is
        //   a final overrider unless the most derived class (1.8)
        //   of which S is a base class subobject (if any) declares
        //   or inherits another member function that overrides vf.
        //
        // Treating this object like the most derived class, we
        // replace any overrides from base classes with this
        // overriding virtual function.
        Overriders[CanonOM].replaceAll(
                               UniqueVirtualMethod(CanonM, SubobjectNumber,
                                                   InVirtualSubobject));

        auto OverriddenMethods = CanonOM->overridden_methods();
        if (OverriddenMethods.begin() == OverriddenMethods.end())
          continue;

        // Continue recursion to the methods that this virtual method
        // overrides.
        Stack.push_back(OverriddenMethods);
      }
    }

    // C++ [class.virtual]p2:
    //   For convenience we say that any virtual function overrides itself.
    Overriders[CanonM].add(SubobjectNumber,
                           UniqueVirtualMethod(CanonM, SubobjectNumber,
                                               InVirtualSubobject));
  }
}

FinalOverriderCollector::~FinalOverriderCollector() {
  for (llvm::DenseMap<const CXXRecordDecl *, CXXFinalOverriderMap *>::iterator
         VO = VirtualOverriders.begin(), VOEnd = VirtualOverriders.end();
       VO != VOEnd;
       ++VO)
    delete VO->second;
}

void
CXXRecordDecl::getFinalOverriders(CXXFinalOverriderMap &FinalOverriders) const {
  FinalOverriderCollector Collector;
  Collector.Collect(this, false, nullptr, FinalOverriders);

  // Weed out any final overriders that come from virtual base class
  // subobjects that were hidden by other subobjects along any path.
  // This is the final-overrider variant of C++ [class.member.lookup]p10.
  for (auto &OM : FinalOverriders) {
    for (auto &SO : OM.second) {
      SmallVectorImpl<UniqueVirtualMethod> &Overriding = SO.second;
      if (Overriding.size() < 2)
        continue;

      auto IsHidden = [&Overriding](const UniqueVirtualMethod &M) {
        if (!M.InVirtualSubobject)
          return false;

        // We have an overriding method in a virtual base class
        // subobject (or non-virtual base class subobject thereof);
        // determine whether there exists an other overriding method
        // in a base class subobject that hides the virtual base class
        // subobject.
        for (const UniqueVirtualMethod &OP : Overriding)
          if (&M != &OP &&
              OP.Method->getParent()->isVirtuallyDerivedFrom(
                  M.InVirtualSubobject))
            return true;
        return false;
      };

      // FIXME: IsHidden reads from Overriding from the middle of a remove_if
      // over the same sequence! Is this guaranteed to work?
      llvm::erase_if(Overriding, IsHidden);
    }
  }
}

static void
AddIndirectPrimaryBases(const CXXRecordDecl *RD, ASTContext &Context,
                        CXXIndirectPrimaryBaseSet& Bases) {
  // If the record has a virtual primary base class, add it to our set.
  const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
  if (Layout.isPrimaryBaseVirtual())
    Bases.insert(Layout.getPrimaryBase());

  for (const auto &I : RD->bases()) {
    assert(!I.getType()->isDependentType() &&
           "Cannot get indirect primary bases for class with dependent bases.");

    const CXXRecordDecl *BaseDecl =
      cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

    // Only bases with virtual bases participate in computing the
    // indirect primary virtual base classes.
    if (BaseDecl->getNumVBases())
      AddIndirectPrimaryBases(BaseDecl, Context, Bases);
  }

}

void
CXXRecordDecl::getIndirectPrimaryBases(CXXIndirectPrimaryBaseSet& Bases) const {
  ASTContext &Context = getASTContext();

  if (!getNumVBases())
    return;

  for (const auto &I : bases()) {
    assert(!I.getType()->isDependentType() &&
           "Cannot get indirect primary bases for class with dependent bases.");

    const CXXRecordDecl *BaseDecl =
      cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

    // Only bases with virtual bases participate in computing the
    // indirect primary virtual base classes.
    if (BaseDecl->getNumVBases())
      AddIndirectPrimaryBases(BaseDecl, Context, Bases);
  }
}
