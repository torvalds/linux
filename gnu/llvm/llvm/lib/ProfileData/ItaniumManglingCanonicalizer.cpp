//===----------------- ItaniumManglingCanonicalizer.cpp -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/ItaniumManglingCanonicalizer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/ItaniumDemangle.h"
#include "llvm/Support/Allocator.h"

using namespace llvm;
using llvm::itanium_demangle::ForwardTemplateReference;
using llvm::itanium_demangle::Node;
using llvm::itanium_demangle::NodeKind;

namespace {
struct FoldingSetNodeIDBuilder {
  llvm::FoldingSetNodeID &ID;
  void operator()(const Node *P) { ID.AddPointer(P); }
  void operator()(std::string_view Str) {
    if (Str.empty())
      ID.AddString({});
    else
      ID.AddString(llvm::StringRef(&*Str.begin(), Str.size()));
  }
  template <typename T>
  std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>> operator()(T V) {
    ID.AddInteger((unsigned long long)V);
  }
  void operator()(itanium_demangle::NodeArray A) {
    ID.AddInteger(A.size());
    for (const Node *N : A)
      (*this)(N);
  }
};

template<typename ...T>
void profileCtor(llvm::FoldingSetNodeID &ID, Node::Kind K, T ...V) {
  FoldingSetNodeIDBuilder Builder = {ID};
  Builder(K);
  int VisitInOrder[] = {
    (Builder(V), 0) ...,
    0 // Avoid empty array if there are no arguments.
  };
  (void)VisitInOrder;
}

// FIXME: Convert this to a generic lambda when possible.
template<typename NodeT> struct ProfileSpecificNode {
  FoldingSetNodeID &ID;
  template<typename ...T> void operator()(T ...V) {
    profileCtor(ID, NodeKind<NodeT>::Kind, V...);
  }
};

struct ProfileNode {
  FoldingSetNodeID &ID;
  template<typename NodeT> void operator()(const NodeT *N) {
    N->match(ProfileSpecificNode<NodeT>{ID});
  }
};

template<> void ProfileNode::operator()(const ForwardTemplateReference *N) {
  llvm_unreachable("should never canonicalize a ForwardTemplateReference");
}

void profileNode(llvm::FoldingSetNodeID &ID, const Node *N) {
  N->visit(ProfileNode{ID});
}

class FoldingNodeAllocator {
  class alignas(alignof(Node *)) NodeHeader : public llvm::FoldingSetNode {
  public:
    // 'Node' in this context names the injected-class-name of the base class.
    itanium_demangle::Node *getNode() {
      return reinterpret_cast<itanium_demangle::Node *>(this + 1);
    }
    void Profile(llvm::FoldingSetNodeID &ID) { profileNode(ID, getNode()); }
  };

  BumpPtrAllocator RawAlloc;
  llvm::FoldingSet<NodeHeader> Nodes;

public:
  void reset() {}

  template <typename T, typename... Args>
  std::pair<Node *, bool> getOrCreateNode(bool CreateNewNodes, Args &&... As) {
    // FIXME: Don't canonicalize forward template references for now, because
    // they contain state (the resolved template node) that's not known at their
    // point of creation.
    if (std::is_same<T, ForwardTemplateReference>::value) {
      // Note that we don't use if-constexpr here and so we must still write
      // this code in a generic form.
      return {new (RawAlloc.Allocate(sizeof(T), alignof(T)))
                  T(std::forward<Args>(As)...),
              true};
    }

    llvm::FoldingSetNodeID ID;
    profileCtor(ID, NodeKind<T>::Kind, As...);

    void *InsertPos;
    if (NodeHeader *Existing = Nodes.FindNodeOrInsertPos(ID, InsertPos))
      return {static_cast<T*>(Existing->getNode()), false};

    if (!CreateNewNodes)
      return {nullptr, true};

    static_assert(alignof(T) <= alignof(NodeHeader),
                  "underaligned node header for specific node kind");
    void *Storage =
        RawAlloc.Allocate(sizeof(NodeHeader) + sizeof(T), alignof(NodeHeader));
    NodeHeader *New = new (Storage) NodeHeader;
    T *Result = new (New->getNode()) T(std::forward<Args>(As)...);
    Nodes.InsertNode(New, InsertPos);
    return {Result, true};
  }

  template<typename T, typename... Args>
  Node *makeNode(Args &&...As) {
    return getOrCreateNode<T>(true, std::forward<Args>(As)...).first;
  }

  void *allocateNodeArray(size_t sz) {
    return RawAlloc.Allocate(sizeof(Node *) * sz, alignof(Node *));
  }
};

class CanonicalizerAllocator : public FoldingNodeAllocator {
  Node *MostRecentlyCreated = nullptr;
  Node *TrackedNode = nullptr;
  bool TrackedNodeIsUsed = false;
  bool CreateNewNodes = true;
  llvm::SmallDenseMap<Node*, Node*, 32> Remappings;

  template<typename T, typename ...Args> Node *makeNodeSimple(Args &&...As) {
    std::pair<Node *, bool> Result =
        getOrCreateNode<T>(CreateNewNodes, std::forward<Args>(As)...);
    if (Result.second) {
      // Node is new. Make a note of that.
      MostRecentlyCreated = Result.first;
    } else if (Result.first) {
      // Node is pre-existing; check if it's in our remapping table.
      if (auto *N = Remappings.lookup(Result.first)) {
        Result.first = N;
        assert(!Remappings.contains(Result.first) &&
               "should never need multiple remap steps");
      }
      if (Result.first == TrackedNode)
        TrackedNodeIsUsed = true;
    }
    return Result.first;
  }

  /// Helper to allow makeNode to be partially-specialized on T.
  template<typename T> struct MakeNodeImpl {
    CanonicalizerAllocator &Self;
    template<typename ...Args> Node *make(Args &&...As) {
      return Self.makeNodeSimple<T>(std::forward<Args>(As)...);
    }
  };

public:
  template<typename T, typename ...Args> Node *makeNode(Args &&...As) {
    return MakeNodeImpl<T>{*this}.make(std::forward<Args>(As)...);
  }

  void reset() { MostRecentlyCreated = nullptr; }

  void setCreateNewNodes(bool CNN) { CreateNewNodes = CNN; }

  void addRemapping(Node *A, Node *B) {
    // Note, we don't need to check whether B is also remapped, because if it
    // was we would have already remapped it when building it.
    Remappings.insert(std::make_pair(A, B));
  }

  bool isMostRecentlyCreated(Node *N) const { return MostRecentlyCreated == N; }

  void trackUsesOf(Node *N) {
    TrackedNode = N;
    TrackedNodeIsUsed = false;
  }
  bool trackedNodeIsUsed() const { return TrackedNodeIsUsed; }
};

// FIXME: Also expand built-in substitutions?

using CanonicalizingDemangler =
    itanium_demangle::ManglingParser<CanonicalizerAllocator>;
} // namespace

struct ItaniumManglingCanonicalizer::Impl {
  CanonicalizingDemangler Demangler = {nullptr, nullptr};
};

ItaniumManglingCanonicalizer::ItaniumManglingCanonicalizer() : P(new Impl) {}
ItaniumManglingCanonicalizer::~ItaniumManglingCanonicalizer() { delete P; }

ItaniumManglingCanonicalizer::EquivalenceError
ItaniumManglingCanonicalizer::addEquivalence(FragmentKind Kind, StringRef First,
                                             StringRef Second) {
  auto &Alloc = P->Demangler.ASTAllocator;
  Alloc.setCreateNewNodes(true);

  auto Parse = [&](StringRef Str) {
    P->Demangler.reset(Str.begin(), Str.end());
    Node *N = nullptr;
    switch (Kind) {
      // A <name>, with minor extensions to allow arbitrary namespace and
      // template names that can't easily be written as <name>s.
    case FragmentKind::Name:
      // Very special case: allow "St" as a shorthand for "3std". It's not
      // valid as a <name> mangling, but is nonetheless the most natural
      // way to name the 'std' namespace.
      if (Str.size() == 2 && P->Demangler.consumeIf("St"))
        N = P->Demangler.make<itanium_demangle::NameType>("std");
      // We permit substitutions to name templates without their template
      // arguments. This mostly just falls out, as almost all template names
      // are valid as <name>s, but we also want to parse <substitution>s as
      // <name>s, even though they're not.
      else if (Str.starts_with("S"))
        // Parse the substitution and optional following template arguments.
        N = P->Demangler.parseType();
      else
        N = P->Demangler.parseName();
      break;

      // A <type>.
    case FragmentKind::Type:
      N = P->Demangler.parseType();
      break;

      // An <encoding>.
    case FragmentKind::Encoding:
      N = P->Demangler.parseEncoding();
      break;
    }

    // If we have trailing junk, the mangling is invalid.
    if (P->Demangler.numLeft() != 0)
      N = nullptr;

    // If any node was created after N, then we cannot safely remap it because
    // it might already be in use by another node.
    return std::make_pair(N, Alloc.isMostRecentlyCreated(N));
  };

  Node *FirstNode, *SecondNode;
  bool FirstIsNew, SecondIsNew;

  std::tie(FirstNode, FirstIsNew) = Parse(First);
  if (!FirstNode)
    return EquivalenceError::InvalidFirstMangling;

  Alloc.trackUsesOf(FirstNode);
  std::tie(SecondNode, SecondIsNew) = Parse(Second);
  if (!SecondNode)
    return EquivalenceError::InvalidSecondMangling;

  // If they're already equivalent, there's nothing to do.
  if (FirstNode == SecondNode)
    return EquivalenceError::Success;

  if (FirstIsNew && !Alloc.trackedNodeIsUsed())
    Alloc.addRemapping(FirstNode, SecondNode);
  else if (SecondIsNew)
    Alloc.addRemapping(SecondNode, FirstNode);
  else
    return EquivalenceError::ManglingAlreadyUsed;

  return EquivalenceError::Success;
}

static ItaniumManglingCanonicalizer::Key
parseMaybeMangledName(CanonicalizingDemangler &Demangler, StringRef Mangling,
                      bool CreateNewNodes) {
  Demangler.ASTAllocator.setCreateNewNodes(CreateNewNodes);
  Demangler.reset(Mangling.begin(), Mangling.end());
  // Attempt demangling only for names that look like C++ mangled names.
  // Otherwise, treat them as extern "C" names. We permit the latter to
  // be remapped by (eg)
  //   encoding 6memcpy 7memmove
  // consistent with how they are encoded as local-names inside a C++ mangling.
  Node *N;
  if (Mangling.starts_with("_Z") || Mangling.starts_with("__Z") ||
      Mangling.starts_with("___Z") || Mangling.starts_with("____Z"))
    N = Demangler.parse();
  else
    N = Demangler.make<itanium_demangle::NameType>(
        std::string_view(Mangling.data(), Mangling.size()));
  return reinterpret_cast<ItaniumManglingCanonicalizer::Key>(N);
}

ItaniumManglingCanonicalizer::Key
ItaniumManglingCanonicalizer::canonicalize(StringRef Mangling) {
  return parseMaybeMangledName(P->Demangler, Mangling, true);
}

ItaniumManglingCanonicalizer::Key
ItaniumManglingCanonicalizer::lookup(StringRef Mangling) {
  return parseMaybeMangledName(P->Demangler, Mangling, false);
}
