//===- ClauseT.h -- clause template definitions ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains template classes that represent OpenMP clauses, as
// described in the OpenMP API specification.
//
// The general structure of any specific clause class is that it is either
// empty, or it consists of a single data member, which can take one of these
// three forms:
// - a value member, named `v`, or
// - a tuple of values, named `t`, or
// - a variant (i.e. union) of values, named `u`.
// To assist with generic visit algorithms, classes define one of the following
// traits:
// - EmptyTrait: the class has no data members.
// - WrapperTrait: the class has a single member `v`
// - TupleTrait: the class has a tuple member `t`
// - UnionTrait the class has a variant member `u`
// - IncompleteTrait: the class is a placeholder class that is currently empty,
//   but will be completed at a later time.
// Note: This structure follows the one used in flang parser.
//
// The types used in the class definitions follow the names used in the spec
// (there are a few exceptions to this). For example, given
//   Clause `foo`
//   - foo-modifier : description...
//   - list         : list of variables
// the corresponding class would be
//   template <...>
//   struct FooT {
//     using FooModifier = type that can represent the modifier
//     using List = ListT<ObjectT<...>>;
//     using TupleTrait = std::true_type;
//     std::tuple<std::optional<FooModifier>, List> t;
//   };
//===----------------------------------------------------------------------===//
#ifndef LLVM_FRONTEND_OPENMP_CLAUSET_H
#define LLVM_FRONTEND_OPENMP_CLAUSET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/OpenMP/OMP.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#define ENUM(Name, ...) enum class Name { __VA_ARGS__ }
#define OPT(x) std::optional<x>

// A number of OpenMP clauses contain values that come from a given set of
// possibilities. In the IR these are usually represented by enums. Both
// clang and flang use different types for the enums, and the enum elements
// representing the same thing may have different values between clang and
// flang.
// Since the representation below tries to adhere to the spec, and be source
// language agnostic, it defines its own enums, independent from any language
// frontend. As a consequence, when instantiating the templates below,
// frontend-specific enums need to be translated into the representation
// used here. The macros below are intended to assist with the conversion.

// Helper macro for enum-class conversion.
#define CLAUSET_SCOPED_ENUM_MEMBER_CONVERT(Ov, Tv)                             \
  if (v == OtherEnum::Ov) {                                                    \
    return ThisEnum::Tv;                                                       \
  }

// Helper macro for enum (non-class) conversion.
#define CLAUSET_UNSCOPED_ENUM_MEMBER_CONVERT(Ov, Tv)                           \
  if (v == Ov) {                                                               \
    return ThisEnum::Tv;                                                       \
  }

#define CLAUSET_ENUM_CONVERT(func, OtherE, ThisE, Maps)                        \
  auto func = [](OtherE v) -> ThisE {                                          \
    using ThisEnum = ThisE;                                                    \
    using OtherEnum = OtherE;                                                  \
    (void)sizeof(OtherEnum); /*Avoid "unused local typedef" warning*/          \
    Maps;                                                                      \
    llvm_unreachable("Unexpected value in " #OtherE);                          \
  }

// Usage:
//
// Given two enums,
//   enum class Other { o1, o2 };
//   enum class This { t1, t2 };
// generate conversion function "Func : Other -> This" with
//   CLAUSET_ENUM_CONVERT(
//       Func, Other, This,
//       CLAUSET_ENUM_MEMBER_CONVERT(o1, t1)      // <- No comma
//       CLAUSET_ENUM_MEMBER_CONVERT(o2, t2)
//       ...
//   )
//
// Note that the sequence of M(other-value, this-value) is separated
// with _spaces_, not commas.

namespace detail {
// Type trait to determine whether T is a specialization of std::variant.
template <typename T> struct is_variant {
  static constexpr bool value = false;
};

template <typename... Ts> struct is_variant<std::variant<Ts...>> {
  static constexpr bool value = true;
};

template <typename T> constexpr bool is_variant_v = is_variant<T>::value;

// Helper utility to create a type which is a union of two given variants.
template <typename...> struct UnionOfTwo;

template <typename... Types1, typename... Types2>
struct UnionOfTwo<std::variant<Types1...>, std::variant<Types2...>> {
  using type = std::variant<Types1..., Types2...>;
};
} // namespace detail

namespace tomp {
namespace type {

// Helper utility to create a type which is a union of an arbitrary number
// of variants.
template <typename...> struct Union;

template <> struct Union<> {
  // Legal to define, illegal to instantiate.
  using type = std::variant<>;
};

template <typename T, typename... Ts> struct Union<T, Ts...> {
  static_assert(detail::is_variant_v<T>);
  using type =
      typename detail::UnionOfTwo<T, typename Union<Ts...>::type>::type;
};

template <typename T> using ListT = llvm::SmallVector<T, 0>;

// The ObjectT class represents a variable or a locator (as defined in
// the OpenMP spec).
// Note: the ObjectT template is not defined. Any user of it is expected to
// provide their own specialization that conforms to the requirements listed
// below.
//
// Let ObjectS be any specialization of ObjectT:
//
// ObjectS must provide the following definitions:
// {
//    using IdTy = Id;
//    using ExprTy = Expr;
//
//    auto id() const -> IdTy {
//      // Return a value such that a.id() == b.id() if and only if:
//      // (1) both `a` and `b` represent the same variable or location, or
//      // (2) bool(a.id()) == false and bool(b.id()) == false
//    }
// }
//
// The type IdTy should be hashable (usable as key in unordered containers).
//
// Values of type IdTy should be contextually convertible to `bool`.
//
// If S is an object of type ObjectS, then `bool(S.id())` is `false` if
// and only if S does not represent any variable or location.
//
// ObjectS should be copyable, movable, and default-constructible.
template <typename IdType, typename ExprType> struct ObjectT;

// By default, object equality is only determined by its identity.
template <typename I, typename E>
bool operator==(const ObjectT<I, E> &o1, const ObjectT<I, E> &o2) {
  return o1.id() == o2.id();
}

template <typename I, typename E> using ObjectListT = ListT<ObjectT<I, E>>;

using DirectiveName = llvm::omp::Directive;

template <typename I, typename E> //
struct DefinedOperatorT {
  struct DefinedOpName {
    using WrapperTrait = std::true_type;
    ObjectT<I, E> v;
  };
  ENUM(IntrinsicOperator, Power, Multiply, Divide, Add, Subtract, Concat, LT,
       LE, EQ, NE, GE, GT, NOT, AND, OR, EQV, NEQV, Min, Max);
  using UnionTrait = std::true_type;
  std::variant<DefinedOpName, IntrinsicOperator> u;
};

// V5.2: [3.2.6] `iterator` modifier
template <typename E> //
struct RangeT {
  // range-specification: begin : end[: step]
  using TupleTrait = std::true_type;
  std::tuple<E, E, OPT(E)> t;
};

// V5.2: [3.2.6] `iterator` modifier
template <typename TypeType, typename IdType, typename ExprType> //
struct IteratorSpecifierT {
  // iterators-specifier: [ iterator-type ] identifier = range-specification
  using TupleTrait = std::true_type;
  std::tuple<OPT(TypeType), ObjectT<IdType, ExprType>, RangeT<ExprType>> t;
};

// Note:
// For motion or map clauses the OpenMP spec allows a unique mapper modifier.
// In practice, since these clauses apply to multiple objects, there can be
// multiple effective mappers applicable to these objects (due to overloads,
// etc.). Because of that store a list of mappers every time a mapper modifier
// is allowed. If the mapper list contains a single element, it applies to
// all objects in the clause, otherwise there should be as many mappers as
// there are objects.
// V5.2: [5.8.2] Mapper identifiers and `mapper` modifiers
template <typename I, typename E> //
struct MapperT {
  using MapperIdentifier = ObjectT<I, E>;
  using WrapperTrait = std::true_type;
  MapperIdentifier v;
};

// V5.2: [15.8.1] `memory-order` clauses
// When used as arguments for other clauses, e.g. `fail`.
ENUM(MemoryOrder, AcqRel, Acquire, Relaxed, Release, SeqCst);
ENUM(MotionExpectation, Present);
// V5.2: [15.9.1] `task-dependence-type` modifier
ENUM(TaskDependenceType, In, Out, Inout, Mutexinoutset, Inoutset, Depobj);

template <typename I, typename E> //
struct LoopIterationT {
  struct Distance {
    using TupleTrait = std::true_type;
    std::tuple<DefinedOperatorT<I, E>, E> t;
  };
  using TupleTrait = std::true_type;
  std::tuple<ObjectT<I, E>, OPT(Distance)> t;
};

template <typename I, typename E> //
struct ProcedureDesignatorT {
  using WrapperTrait = std::true_type;
  ObjectT<I, E> v;
};

// Note:
// For reduction clauses the OpenMP spec allows a unique reduction identifier.
// For reasons analogous to those listed for the MapperT type, clauses that
// according to the spec contain a reduction identifier will contain a list of
// reduction identifiers. The same constraints apply: there is either a single
// identifier that applies to all objects, or there are as many identifiers
// as there are objects.
template <typename I, typename E> //
struct ReductionIdentifierT {
  using UnionTrait = std::true_type;
  std::variant<DefinedOperatorT<I, E>, ProcedureDesignatorT<I, E>> u;
};

template <typename T, typename I, typename E> //
using IteratorT = ListT<IteratorSpecifierT<T, I, E>>;

template <typename T>
std::enable_if_t<T::EmptyTrait::value, bool> operator==(const T &a,
                                                        const T &b) {
  return true;
}
template <typename T>
std::enable_if_t<T::IncompleteTrait::value, bool> operator==(const T &a,
                                                             const T &b) {
  return true;
}
template <typename T>
std::enable_if_t<T::WrapperTrait::value, bool> operator==(const T &a,
                                                          const T &b) {
  return a.v == b.v;
}
template <typename T>
std::enable_if_t<T::TupleTrait::value, bool> operator==(const T &a,
                                                        const T &b) {
  return a.t == b.t;
}
template <typename T>
std::enable_if_t<T::UnionTrait::value, bool> operator==(const T &a,
                                                        const T &b) {
  return a.u == b.u;
}
} // namespace type

template <typename T> using ListT = type::ListT<T>;

template <typename I, typename E> using ObjectT = type::ObjectT<I, E>;
template <typename I, typename E> using ObjectListT = type::ObjectListT<I, E>;

template <typename T, typename I, typename E>
using IteratorT = type::IteratorT<T, I, E>;

template <
    typename ContainerTy, typename FunctionTy,
    typename ElemTy = typename llvm::remove_cvref_t<ContainerTy>::value_type,
    typename ResultTy = std::invoke_result_t<FunctionTy, ElemTy>>
ListT<ResultTy> makeList(ContainerTy &&container, FunctionTy &&func) {
  ListT<ResultTy> v;
  llvm::transform(container, std::back_inserter(v), func);
  return v;
}

namespace clause {
using type::operator==;

// V5.2: [8.3.1] `assumption` clauses
template <typename T, typename I, typename E> //
struct AbsentT {
  using List = ListT<type::DirectiveName>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [15.8.1] `memory-order` clauses
template <typename T, typename I, typename E> //
struct AcqRelT {
  using EmptyTrait = std::true_type;
};

// V5.2: [15.8.1] `memory-order` clauses
template <typename T, typename I, typename E> //
struct AcquireT {
  using EmptyTrait = std::true_type;
};

// V5.2: [7.5.2] `adjust_args` clause
template <typename T, typename I, typename E> //
struct AdjustArgsT {
  using IncompleteTrait = std::true_type;
};

// V5.2: [12.5.1] `affinity` clause
template <typename T, typename I, typename E> //
struct AffinityT {
  using Iterator = type::IteratorT<T, I, E>;
  using LocatorList = ObjectListT<I, E>;

  using TupleTrait = std::true_type;
  std::tuple<OPT(Iterator), LocatorList> t;
};

// V5.2: [6.3] `align` clause
template <typename T, typename I, typename E> //
struct AlignT {
  using Alignment = E;

  using WrapperTrait = std::true_type;
  Alignment v;
};

// V5.2: [5.11] `aligned` clause
template <typename T, typename I, typename E> //
struct AlignedT {
  using Alignment = E;
  using List = ObjectListT<I, E>;

  using TupleTrait = std::true_type;
  std::tuple<OPT(Alignment), List> t;
};

template <typename T, typename I, typename E> //
struct AllocatorT;

// V5.2: [6.6] `allocate` clause
template <typename T, typename I, typename E> //
struct AllocateT {
  using AllocatorSimpleModifier = E;
  using AllocatorComplexModifier = AllocatorT<T, I, E>;
  using AlignModifier = AlignT<T, I, E>;
  using List = ObjectListT<I, E>;

  using TupleTrait = std::true_type;
  std::tuple<OPT(AllocatorSimpleModifier), OPT(AllocatorComplexModifier),
             OPT(AlignModifier), List>
      t;
};

// V5.2: [6.4] `allocator` clause
template <typename T, typename I, typename E> //
struct AllocatorT {
  using Allocator = E;
  using WrapperTrait = std::true_type;
  Allocator v;
};

// V5.2: [7.5.3] `append_args` clause
template <typename T, typename I, typename E> //
struct AppendArgsT {
  using IncompleteTrait = std::true_type;
};

// V5.2: [8.1] `at` clause
template <typename T, typename I, typename E> //
struct AtT {
  ENUM(ActionTime, Compilation, Execution);
  using WrapperTrait = std::true_type;
  ActionTime v;
};

// V5.2: [8.2.1] `requirement` clauses
template <typename T, typename I, typename E> //
struct AtomicDefaultMemOrderT {
  using MemoryOrder = type::MemoryOrder;
  using WrapperTrait = std::true_type;
  MemoryOrder v; // Name not provided in spec
};

// V5.2: [11.7.1] `bind` clause
template <typename T, typename I, typename E> //
struct BindT {
  ENUM(Binding, Teams, Parallel, Thread);
  using WrapperTrait = std::true_type;
  Binding v;
};

// V5.2: [15.8.3] `extended-atomic` clauses
template <typename T, typename I, typename E> //
struct CaptureT {
  using EmptyTrait = std::true_type;
};

// V5.2: [4.4.3] `collapse` clause
template <typename T, typename I, typename E> //
struct CollapseT {
  using N = E;
  using WrapperTrait = std::true_type;
  N v;
};

// V5.2: [15.8.3] `extended-atomic` clauses
template <typename T, typename I, typename E> //
struct CompareT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.3.1] `assumption` clauses
template <typename T, typename I, typename E> //
struct ContainsT {
  using List = ListT<type::DirectiveName>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.7.1] `copyin` clause
template <typename T, typename I, typename E> //
struct CopyinT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.7.2] `copyprivate` clause
template <typename T, typename I, typename E> //
struct CopyprivateT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.4.1] `default` clause
template <typename T, typename I, typename E> //
struct DefaultT {
  ENUM(DataSharingAttribute, Firstprivate, None, Private, Shared);
  using WrapperTrait = std::true_type;
  DataSharingAttribute v;
};

// V5.2: [5.8.7] `defaultmap` clause
template <typename T, typename I, typename E> //
struct DefaultmapT {
  ENUM(ImplicitBehavior, Alloc, To, From, Tofrom, Firstprivate, None, Default,
       Present);
  ENUM(VariableCategory, Scalar, Aggregate, Pointer, Allocatable);
  using TupleTrait = std::true_type;
  std::tuple<ImplicitBehavior, OPT(VariableCategory)> t;
};

template <typename T, typename I, typename E> //
struct DoacrossT;

// V5.2: [15.9.5] `depend` clause
template <typename T, typename I, typename E> //
struct DependT {
  using Iterator = type::IteratorT<T, I, E>;
  using LocatorList = ObjectListT<I, E>;
  using TaskDependenceType = tomp::type::TaskDependenceType;

  struct WithLocators { // Modern form
    using TupleTrait = std::true_type;
    // Empty LocatorList means "omp_all_memory".
    std::tuple<TaskDependenceType, OPT(Iterator), LocatorList> t;
  };

  using Doacross = DoacrossT<T, I, E>;
  using UnionTrait = std::true_type;
  std::variant<Doacross, WithLocators> u; // Doacross form is legacy
};

// V5.2: [3.5] `destroy` clause
template <typename T, typename I, typename E> //
struct DestroyT {
  using DestroyVar = ObjectT<I, E>;
  using WrapperTrait = std::true_type;
  // DestroyVar can be ommitted in "depobj destroy".
  OPT(DestroyVar) v;
};

// V5.2: [12.5.2] `detach` clause
template <typename T, typename I, typename E> //
struct DetachT {
  using EventHandle = ObjectT<I, E>;
  using WrapperTrait = std::true_type;
  EventHandle v;
};

// V5.2: [13.2] `device` clause
template <typename T, typename I, typename E> //
struct DeviceT {
  using DeviceDescription = E;
  ENUM(DeviceModifier, Ancestor, DeviceNum);
  using TupleTrait = std::true_type;
  std::tuple<OPT(DeviceModifier), DeviceDescription> t;
};

// V5.2: [13.1] `device_type` clause
template <typename T, typename I, typename E> //
struct DeviceTypeT {
  ENUM(DeviceTypeDescription, Any, Host, Nohost);
  using WrapperTrait = std::true_type;
  DeviceTypeDescription v;
};

// V5.2: [11.6.1] `dist_schedule` clause
template <typename T, typename I, typename E> //
struct DistScheduleT {
  ENUM(Kind, Static);
  using ChunkSize = E;
  using TupleTrait = std::true_type;
  std::tuple<Kind, OPT(ChunkSize)> t;
};

// V5.2: [15.9.6] `doacross` clause
template <typename T, typename I, typename E> //
struct DoacrossT {
  using Vector = ListT<type::LoopIterationT<I, E>>;
  ENUM(DependenceType, Source, Sink);
  using TupleTrait = std::true_type;
  // Empty Vector means "omp_cur_iteration"
  std::tuple<DependenceType, Vector> t;
};

// V5.2: [8.2.1] `requirement` clauses
template <typename T, typename I, typename E> //
struct DynamicAllocatorsT {
  using EmptyTrait = std::true_type;
};

// V5.2: [5.8.4] `enter` clause
template <typename T, typename I, typename E> //
struct EnterT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.6.2] `exclusive` clause
template <typename T, typename I, typename E> //
struct ExclusiveT {
  using WrapperTrait = std::true_type;
  using List = ObjectListT<I, E>;
  List v;
};

// V5.2: [15.8.3] `extended-atomic` clauses
template <typename T, typename I, typename E> //
struct FailT {
  using MemoryOrder = type::MemoryOrder;
  using WrapperTrait = std::true_type;
  MemoryOrder v;
};

// V5.2: [10.5.1] `filter` clause
template <typename T, typename I, typename E> //
struct FilterT {
  using ThreadNum = E;
  using WrapperTrait = std::true_type;
  ThreadNum v;
};

// V5.2: [12.3] `final` clause
template <typename T, typename I, typename E> //
struct FinalT {
  using Finalize = E;
  using WrapperTrait = std::true_type;
  Finalize v;
};

// V5.2: [5.4.4] `firstprivate` clause
template <typename T, typename I, typename E> //
struct FirstprivateT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.9.2] `from` clause
template <typename T, typename I, typename E> //
struct FromT {
  using LocatorList = ObjectListT<I, E>;
  using Expectation = type::MotionExpectation;
  using Iterator = type::IteratorT<T, I, E>;
  // See note at the definition of the MapperT type.
  using Mappers = ListT<type::MapperT<I, E>>; // Not a spec name

  using TupleTrait = std::true_type;
  std::tuple<OPT(Expectation), OPT(Mappers), OPT(Iterator), LocatorList> t;
};

// V5.2: [9.2.1] `full` clause
template <typename T, typename I, typename E> //
struct FullT {
  using EmptyTrait = std::true_type;
};

// V5.2: [12.6.1] `grainsize` clause
template <typename T, typename I, typename E> //
struct GrainsizeT {
  ENUM(Prescriptiveness, Strict);
  using GrainSize = E;
  using TupleTrait = std::true_type;
  std::tuple<OPT(Prescriptiveness), GrainSize> t;
};

// V5.2: [5.4.9] `has_device_addr` clause
template <typename T, typename I, typename E> //
struct HasDeviceAddrT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [15.1.2] `hint` clause
template <typename T, typename I, typename E> //
struct HintT {
  using HintExpr = E;
  using WrapperTrait = std::true_type;
  HintExpr v;
};

// V5.2: [8.3.1] Assumption clauses
template <typename T, typename I, typename E> //
struct HoldsT {
  using WrapperTrait = std::true_type;
  E v; // No argument name in spec 5.2
};

// V5.2: [3.4] `if` clause
template <typename T, typename I, typename E> //
struct IfT {
  using DirectiveNameModifier = type::DirectiveName;
  using IfExpression = E;
  using TupleTrait = std::true_type;
  std::tuple<OPT(DirectiveNameModifier), IfExpression> t;
};

// V5.2: [7.7.1] `branch` clauses
template <typename T, typename I, typename E> //
struct InbranchT {
  using EmptyTrait = std::true_type;
};

// V5.2: [5.6.1] `exclusive` clause
template <typename T, typename I, typename E> //
struct InclusiveT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [7.8.3] `indirect` clause
template <typename T, typename I, typename E> //
struct IndirectT {
  using InvokedByFptr = E;
  using WrapperTrait = std::true_type;
  InvokedByFptr v;
};

// V5.2: [14.1.2] `init` clause
template <typename T, typename I, typename E> //
struct InitT {
  using ForeignRuntimeId = E;
  using InteropVar = ObjectT<I, E>;
  using InteropPreference = ListT<ForeignRuntimeId>;
  ENUM(InteropType, Target, Targetsync);   // Repeatable
  using InteropTypes = ListT<InteropType>; // Not a spec name

  using TupleTrait = std::true_type;
  std::tuple<OPT(InteropPreference), InteropTypes, InteropVar> t;
};

// V5.2: [5.5.4] `initializer` clause
template <typename T, typename I, typename E> //
struct InitializerT {
  using InitializerExpr = E;
  using WrapperTrait = std::true_type;
  InitializerExpr v;
};

// V5.2: [5.5.10] `in_reduction` clause
template <typename T, typename I, typename E> //
struct InReductionT {
  using List = ObjectListT<I, E>;
  // See note at the definition of the ReductionIdentifierT type.
  // The name ReductionIdentifiers is not a spec name.
  using ReductionIdentifiers = ListT<type::ReductionIdentifierT<I, E>>;
  using TupleTrait = std::true_type;
  std::tuple<ReductionIdentifiers, List> t;
};

// V5.2: [5.4.7] `is_device_ptr` clause
template <typename T, typename I, typename E> //
struct IsDevicePtrT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.4.5] `lastprivate` clause
template <typename T, typename I, typename E> //
struct LastprivateT {
  using List = ObjectListT<I, E>;
  ENUM(LastprivateModifier, Conditional);
  using TupleTrait = std::true_type;
  std::tuple<OPT(LastprivateModifier), List> t;
};

// V5.2: [5.4.6] `linear` clause
template <typename T, typename I, typename E> //
struct LinearT {
  // std::get<type> won't work here due to duplicate types in the tuple.
  using List = ObjectListT<I, E>;
  using StepSimpleModifier = E;
  using StepComplexModifier = E;
  ENUM(LinearModifier, Ref, Val, Uval);

  using TupleTrait = std::true_type;
  // Step == nullopt means 1.
  std::tuple<OPT(StepSimpleModifier), OPT(StepComplexModifier),
             OPT(LinearModifier), List>
      t;
};

// V5.2: [5.8.5] `link` clause
template <typename T, typename I, typename E> //
struct LinkT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.8.3] `map` clause
template <typename T, typename I, typename E> //
struct MapT {
  using LocatorList = ObjectListT<I, E>;
  ENUM(MapType, To, From, Tofrom, Alloc, Release, Delete);
  ENUM(MapTypeModifier, Always, Close, Present, OmpxHold);
  // See note at the definition of the MapperT type.
  using Mappers = ListT<type::MapperT<I, E>>; // Not a spec name
  using Iterator = type::IteratorT<T, I, E>;
  using MapTypeModifiers = ListT<MapTypeModifier>; // Not a spec name

  using TupleTrait = std::true_type;
  std::tuple<OPT(MapType), OPT(MapTypeModifiers), OPT(Mappers), OPT(Iterator),
             LocatorList>
      t;
};

// V5.2: [7.5.1] `match` clause
template <typename T, typename I, typename E> //
struct MatchT {
  using IncompleteTrait = std::true_type;
};

// V5.2: [12.2] `mergeable` clause
template <typename T, typename I, typename E> //
struct MergeableT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.5.2] `message` clause
template <typename T, typename I, typename E> //
struct MessageT {
  using MsgString = E;
  using WrapperTrait = std::true_type;
  MsgString v;
};

// V5.2: [7.6.2] `nocontext` clause
template <typename T, typename I, typename E> //
struct NocontextT {
  using DoNotUpdateContext = E;
  using WrapperTrait = std::true_type;
  DoNotUpdateContext v;
};

// V5.2: [15.7] `nowait` clause
template <typename T, typename I, typename E> //
struct NogroupT {
  using EmptyTrait = std::true_type;
};

// V5.2: [10.4.1] `nontemporal` clause
template <typename T, typename I, typename E> //
struct NontemporalT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [8.3.1] `assumption` clauses
template <typename T, typename I, typename E> //
struct NoOpenmpT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.3.1] `assumption` clauses
template <typename T, typename I, typename E> //
struct NoOpenmpRoutinesT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.3.1] `assumption` clauses
template <typename T, typename I, typename E> //
struct NoParallelismT {
  using EmptyTrait = std::true_type;
};

// V5.2: [7.7.1] `branch` clauses
template <typename T, typename I, typename E> //
struct NotinbranchT {
  using EmptyTrait = std::true_type;
};

// V5.2: [7.6.1] `novariants` clause
template <typename T, typename I, typename E> //
struct NovariantsT {
  using DoNotUseVariant = E;
  using WrapperTrait = std::true_type;
  DoNotUseVariant v;
};

// V5.2: [15.6] `nowait` clause
template <typename T, typename I, typename E> //
struct NowaitT {
  using EmptyTrait = std::true_type;
};

// V5.2: [12.6.2] `num_tasks` clause
template <typename T, typename I, typename E> //
struct NumTasksT {
  using NumTasks = E;
  ENUM(Prescriptiveness, Strict);
  using TupleTrait = std::true_type;
  std::tuple<OPT(Prescriptiveness), NumTasks> t;
};

// V5.2: [10.2.1] `num_teams` clause
template <typename T, typename I, typename E> //
struct NumTeamsT {
  using TupleTrait = std::true_type;
  using LowerBound = E;
  using UpperBound = E;
  std::tuple<OPT(LowerBound), UpperBound> t;
};

// V5.2: [10.1.2] `num_threads` clause
template <typename T, typename I, typename E> //
struct NumThreadsT {
  using Nthreads = E;
  using WrapperTrait = std::true_type;
  Nthreads v;
};

template <typename T, typename I, typename E> //
struct OmpxAttributeT {
  using EmptyTrait = std::true_type;
};

template <typename T, typename I, typename E> //
struct OmpxBareT {
  using EmptyTrait = std::true_type;
};

template <typename T, typename I, typename E> //
struct OmpxDynCgroupMemT {
  using WrapperTrait = std::true_type;
  E v;
};

// V5.2: [10.3] `order` clause
template <typename T, typename I, typename E> //
struct OrderT {
  ENUM(OrderModifier, Reproducible, Unconstrained);
  ENUM(Ordering, Concurrent);
  using TupleTrait = std::true_type;
  std::tuple<OPT(OrderModifier), Ordering> t;
};

// V5.2: [4.4.4] `ordered` clause
template <typename T, typename I, typename E> //
struct OrderedT {
  using N = E;
  using WrapperTrait = std::true_type;
  OPT(N) v;
};

// V5.2: [7.4.2] `otherwise` clause
template <typename T, typename I, typename E> //
struct OtherwiseT {
  using IncompleteTrait = std::true_type;
};

// V5.2: [9.2.2] `partial` clause
template <typename T, typename I, typename E> //
struct PartialT {
  using UnrollFactor = E;
  using WrapperTrait = std::true_type;
  OPT(UnrollFactor) v;
};

// V5.2: [12.4] `priority` clause
template <typename T, typename I, typename E> //
struct PriorityT {
  using PriorityValue = E;
  using WrapperTrait = std::true_type;
  PriorityValue v;
};

// V5.2: [5.4.3] `private` clause
template <typename T, typename I, typename E> //
struct PrivateT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [10.1.4] `proc_bind` clause
template <typename T, typename I, typename E> //
struct ProcBindT {
  ENUM(AffinityPolicy, Close, Master, Spread, Primary);
  using WrapperTrait = std::true_type;
  AffinityPolicy v;
};

// V5.2: [15.8.2] Atomic clauses
template <typename T, typename I, typename E> //
struct ReadT {
  using EmptyTrait = std::true_type;
};

// V5.2: [5.5.8] `reduction` clause
template <typename T, typename I, typename E> //
struct ReductionT {
  using List = ObjectListT<I, E>;
  // See note at the definition of the ReductionIdentifierT type.
  // The name ReductionIdentifiers is not a spec name.
  using ReductionIdentifiers = ListT<type::ReductionIdentifierT<I, E>>;
  ENUM(ReductionModifier, Default, Inscan, Task);
  using TupleTrait = std::true_type;
  std::tuple<OPT(ReductionModifier), ReductionIdentifiers, List> t;
};

// V5.2: [15.8.1] `memory-order` clauses
template <typename T, typename I, typename E> //
struct RelaxedT {
  using EmptyTrait = std::true_type;
};

// V5.2: [15.8.1] `memory-order` clauses
template <typename T, typename I, typename E> //
struct ReleaseT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.2.1] `requirement` clauses
template <typename T, typename I, typename E> //
struct ReverseOffloadT {
  using EmptyTrait = std::true_type;
};

// V5.2: [10.4.2] `safelen` clause
template <typename T, typename I, typename E> //
struct SafelenT {
  using Length = E;
  using WrapperTrait = std::true_type;
  Length v;
};

// V5.2: [11.5.3] `schedule` clause
template <typename T, typename I, typename E> //
struct ScheduleT {
  ENUM(Kind, Static, Dynamic, Guided, Auto, Runtime);
  using ChunkSize = E;
  ENUM(OrderingModifier, Monotonic, Nonmonotonic);
  ENUM(ChunkModifier, Simd);
  using TupleTrait = std::true_type;
  std::tuple<Kind, OPT(OrderingModifier), OPT(ChunkModifier), OPT(ChunkSize)> t;
};

// V5.2: [15.8.1] Memory-order clauses
template <typename T, typename I, typename E> //
struct SeqCstT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.5.1] `severity` clause
template <typename T, typename I, typename E> //
struct SeverityT {
  ENUM(SevLevel, Fatal, Warning);
  using WrapperTrait = std::true_type;
  SevLevel v;
};

// V5.2: [5.4.2] `shared` clause
template <typename T, typename I, typename E> //
struct SharedT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [15.10.3] `parallelization-level` clauses
template <typename T, typename I, typename E> //
struct SimdT {
  using EmptyTrait = std::true_type;
};

// V5.2: [10.4.3] `simdlen` clause
template <typename T, typename I, typename E> //
struct SimdlenT {
  using Length = E;
  using WrapperTrait = std::true_type;
  Length v;
};

// V5.2: [9.1.1] `sizes` clause
template <typename T, typename I, typename E> //
struct SizesT {
  using SizeList = ListT<E>;
  using WrapperTrait = std::true_type;
  SizeList v;
};

// V5.2: [5.5.9] `task_reduction` clause
template <typename T, typename I, typename E> //
struct TaskReductionT {
  using List = ObjectListT<I, E>;
  // See note at the definition of the ReductionIdentifierT type.
  // The name ReductionIdentifiers is not a spec name.
  using ReductionIdentifiers = ListT<type::ReductionIdentifierT<I, E>>;
  using TupleTrait = std::true_type;
  std::tuple<ReductionIdentifiers, List> t;
};

// V5.2: [13.3] `thread_limit` clause
template <typename T, typename I, typename E> //
struct ThreadLimitT {
  using Threadlim = E;
  using WrapperTrait = std::true_type;
  Threadlim v;
};

// V5.2: [15.10.3] `parallelization-level` clauses
template <typename T, typename I, typename E> //
struct ThreadsT {
  using EmptyTrait = std::true_type;
};

// V5.2: [5.9.1] `to` clause
template <typename T, typename I, typename E> //
struct ToT {
  using LocatorList = ObjectListT<I, E>;
  using Expectation = type::MotionExpectation;
  // See note at the definition of the MapperT type.
  using Mappers = ListT<type::MapperT<I, E>>; // Not a spec name
  using Iterator = type::IteratorT<T, I, E>;

  using TupleTrait = std::true_type;
  std::tuple<OPT(Expectation), OPT(Mappers), OPT(Iterator), LocatorList> t;
};

// V5.2: [8.2.1] `requirement` clauses
template <typename T, typename I, typename E> //
struct UnifiedAddressT {
  using EmptyTrait = std::true_type;
};

// V5.2: [8.2.1] `requirement` clauses
template <typename T, typename I, typename E> //
struct UnifiedSharedMemoryT {
  using EmptyTrait = std::true_type;
};

// V5.2: [5.10] `uniform` clause
template <typename T, typename I, typename E> //
struct UniformT {
  using ParameterList = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  ParameterList v;
};

template <typename T, typename I, typename E> //
struct UnknownT {
  using EmptyTrait = std::true_type;
};

// V5.2: [12.1] `untied` clause
template <typename T, typename I, typename E> //
struct UntiedT {
  using EmptyTrait = std::true_type;
};

// Both of the following
// V5.2: [15.8.2] `atomic` clauses
// V5.2: [15.9.3] `update` clause
template <typename T, typename I, typename E> //
struct UpdateT {
  using TaskDependenceType = tomp::type::TaskDependenceType;
  using WrapperTrait = std::true_type;
  OPT(TaskDependenceType) v;
};

// V5.2: [14.1.3] `use` clause
template <typename T, typename I, typename E> //
struct UseT {
  using InteropVar = ObjectT<I, E>;
  using WrapperTrait = std::true_type;
  InteropVar v;
};

// V5.2: [5.4.10] `use_device_addr` clause
template <typename T, typename I, typename E> //
struct UseDeviceAddrT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [5.4.8] `use_device_ptr` clause
template <typename T, typename I, typename E> //
struct UseDevicePtrT {
  using List = ObjectListT<I, E>;
  using WrapperTrait = std::true_type;
  List v;
};

// V5.2: [6.8] `uses_allocators` clause
template <typename T, typename I, typename E> //
struct UsesAllocatorsT {
  using MemSpace = E;
  using TraitsArray = ObjectT<I, E>;
  using Allocator = E;
  struct AllocatorSpec { // Not a spec name
    using TupleTrait = std::true_type;
    std::tuple<OPT(MemSpace), OPT(TraitsArray), Allocator> t;
  };
  using Allocators = ListT<AllocatorSpec>; // Not a spec name
  using WrapperTrait = std::true_type;
  Allocators v;
};

// V5.2: [15.8.3] `extended-atomic` clauses
template <typename T, typename I, typename E> //
struct WeakT {
  using EmptyTrait = std::true_type;
};

// V5.2: [7.4.1] `when` clause
template <typename T, typename I, typename E> //
struct WhenT {
  using IncompleteTrait = std::true_type;
};

// V5.2: [15.8.2] Atomic clauses
template <typename T, typename I, typename E> //
struct WriteT {
  using EmptyTrait = std::true_type;
};

// ---

template <typename T, typename I, typename E>
using ExtensionClausesT =
    std::variant<OmpxAttributeT<T, I, E>, OmpxBareT<T, I, E>,
                 OmpxDynCgroupMemT<T, I, E>>;

template <typename T, typename I, typename E>
using EmptyClausesT = std::variant<
    AcqRelT<T, I, E>, AcquireT<T, I, E>, CaptureT<T, I, E>, CompareT<T, I, E>,
    DynamicAllocatorsT<T, I, E>, FullT<T, I, E>, InbranchT<T, I, E>,
    MergeableT<T, I, E>, NogroupT<T, I, E>, NoOpenmpRoutinesT<T, I, E>,
    NoOpenmpT<T, I, E>, NoParallelismT<T, I, E>, NotinbranchT<T, I, E>,
    NowaitT<T, I, E>, ReadT<T, I, E>, RelaxedT<T, I, E>, ReleaseT<T, I, E>,
    ReverseOffloadT<T, I, E>, SeqCstT<T, I, E>, SimdT<T, I, E>,
    ThreadsT<T, I, E>, UnifiedAddressT<T, I, E>, UnifiedSharedMemoryT<T, I, E>,
    UnknownT<T, I, E>, UntiedT<T, I, E>, UseT<T, I, E>, WeakT<T, I, E>,
    WriteT<T, I, E>>;

template <typename T, typename I, typename E>
using IncompleteClausesT =
    std::variant<AdjustArgsT<T, I, E>, AppendArgsT<T, I, E>, MatchT<T, I, E>,
                 OtherwiseT<T, I, E>, WhenT<T, I, E>>;

template <typename T, typename I, typename E>
using TupleClausesT =
    std::variant<AffinityT<T, I, E>, AlignedT<T, I, E>, AllocateT<T, I, E>,
                 DefaultmapT<T, I, E>, DeviceT<T, I, E>, DistScheduleT<T, I, E>,
                 DoacrossT<T, I, E>, FromT<T, I, E>, GrainsizeT<T, I, E>,
                 IfT<T, I, E>, InitT<T, I, E>, InReductionT<T, I, E>,
                 LastprivateT<T, I, E>, LinearT<T, I, E>, MapT<T, I, E>,
                 NumTasksT<T, I, E>, OrderT<T, I, E>, ReductionT<T, I, E>,
                 ScheduleT<T, I, E>, TaskReductionT<T, I, E>, ToT<T, I, E>>;

template <typename T, typename I, typename E>
using UnionClausesT = std::variant<DependT<T, I, E>>;

template <typename T, typename I, typename E>
using WrapperClausesT = std::variant<
    AbsentT<T, I, E>, AlignT<T, I, E>, AllocatorT<T, I, E>,
    AtomicDefaultMemOrderT<T, I, E>, AtT<T, I, E>, BindT<T, I, E>,
    CollapseT<T, I, E>, ContainsT<T, I, E>, CopyinT<T, I, E>,
    CopyprivateT<T, I, E>, DefaultT<T, I, E>, DestroyT<T, I, E>,
    DetachT<T, I, E>, DeviceTypeT<T, I, E>, EnterT<T, I, E>,
    ExclusiveT<T, I, E>, FailT<T, I, E>, FilterT<T, I, E>, FinalT<T, I, E>,
    FirstprivateT<T, I, E>, HasDeviceAddrT<T, I, E>, HintT<T, I, E>,
    HoldsT<T, I, E>, InclusiveT<T, I, E>, IndirectT<T, I, E>,
    InitializerT<T, I, E>, IsDevicePtrT<T, I, E>, LinkT<T, I, E>,
    MessageT<T, I, E>, NocontextT<T, I, E>, NontemporalT<T, I, E>,
    NovariantsT<T, I, E>, NumTeamsT<T, I, E>, NumThreadsT<T, I, E>,
    OrderedT<T, I, E>, PartialT<T, I, E>, PriorityT<T, I, E>, PrivateT<T, I, E>,
    ProcBindT<T, I, E>, SafelenT<T, I, E>, SeverityT<T, I, E>, SharedT<T, I, E>,
    SimdlenT<T, I, E>, SizesT<T, I, E>, ThreadLimitT<T, I, E>,
    UniformT<T, I, E>, UpdateT<T, I, E>, UseDeviceAddrT<T, I, E>,
    UseDevicePtrT<T, I, E>, UsesAllocatorsT<T, I, E>>;

template <typename T, typename I, typename E>
using UnionOfAllClausesT = typename type::Union< //
    EmptyClausesT<T, I, E>,                      //
    ExtensionClausesT<T, I, E>,                  //
    IncompleteClausesT<T, I, E>,                 //
    TupleClausesT<T, I, E>,                      //
    UnionClausesT<T, I, E>,                      //
    WrapperClausesT<T, I, E>                     //
    >::type;
} // namespace clause

using type::operator==;

// The variant wrapper that encapsulates all possible specific clauses.
// The `Extras` arguments are additional types representing local extensions
// to the clause set, e.g.
//
// using Clause = ClauseT<Type, Id, Expr,
//                        MyClause1, MyClause2>;
//
// The member Clause::u will be a variant containing all specific clauses
// defined above, plus MyClause1 and MyClause2.
//
// Note: Any derived class must be constructible from the base class
// ClauseT<...>.
template <typename TypeType, typename IdType, typename ExprType,
          typename... Extras>
struct ClauseT {
  using TypeTy = TypeType;
  using IdTy = IdType;
  using ExprTy = ExprType;

  // Type of "self" to specify this type given a derived class type.
  using BaseT = ClauseT<TypeType, IdType, ExprType, Extras...>;

  using VariantTy = typename type::Union<
      clause::UnionOfAllClausesT<TypeType, IdType, ExprType>,
      std::variant<Extras...>>::type;

  llvm::omp::Clause id; // The numeric id of the clause
  using UnionTrait = std::true_type;
  VariantTy u;
};

template <typename ClauseType> struct DirectiveWithClauses {
  llvm::omp::Directive id = llvm::omp::Directive::OMPD_unknown;
  tomp::type::ListT<ClauseType> clauses;
};

} // namespace tomp

#undef OPT
#undef ENUM

#endif // LLVM_FRONTEND_OPENMP_CLAUSET_H
