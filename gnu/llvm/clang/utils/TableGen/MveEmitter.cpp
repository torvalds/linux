//===- MveEmitter.cpp - Generate arm_mve.h for use with clang -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This set of linked tablegen backends is responsible for emitting the bits
// and pieces that implement <arm_mve.h>, which is defined by the ACLE standard
// and provides a set of types and functions for (more or less) direct access
// to the MVE instruction set, including the scalar shifts as well as the
// vector instructions.
//
// MVE's standard intrinsic functions are unusual in that they have a system of
// polymorphism. For example, the function vaddq() can behave like vaddq_u16(),
// vaddq_f32(), vaddq_s8(), etc., depending on the types of the vector
// arguments you give it.
//
// This constrains the implementation strategies. The usual approach to making
// the user-facing functions polymorphic would be to either use
// __attribute__((overloadable)) to make a set of vaddq() functions that are
// all inline wrappers on the underlying clang builtins, or to define a single
// vaddq() macro which expands to an instance of _Generic.
//
// The inline-wrappers approach would work fine for most intrinsics, except for
// the ones that take an argument required to be a compile-time constant,
// because if you wrap an inline function around a call to a builtin, the
// constant nature of the argument is not passed through.
//
// The _Generic approach can be made to work with enough effort, but it takes a
// lot of machinery, because of the design feature of _Generic that even the
// untaken branches are required to pass all front-end validity checks such as
// type-correctness. You can work around that by nesting further _Generics all
// over the place to coerce things to the right type in untaken branches, but
// what you get out is complicated, hard to guarantee its correctness, and
// worst of all, gives _completely unreadable_ error messages if the user gets
// the types wrong for an intrinsic call.
//
// Therefore, my strategy is to introduce a new __attribute__ that allows a
// function to be mapped to a clang builtin even though it doesn't have the
// same name, and then declare all the user-facing MVE function names with that
// attribute, mapping each one directly to the clang builtin. And the
// polymorphic ones have __attribute__((overloadable)) as well. So once the
// compiler has resolved the overload, it knows the internal builtin ID of the
// selected function, and can check the immediate arguments against that; and
// if the user gets the types wrong in a call to a polymorphic intrinsic, they
// get a completely clear error message showing all the declarations of that
// function in the header file and explaining why each one doesn't fit their
// call.
//
// The downside of this is that if every clang builtin has to correspond
// exactly to a user-facing ACLE intrinsic, then you can't save work in the
// frontend by doing it in the header file: CGBuiltin.cpp has to do the entire
// job of converting an ACLE intrinsic call into LLVM IR. So the Tablegen
// description for an MVE intrinsic has to contain a full description of the
// sequence of IRBuilder calls that clang will need to make.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace {

class EmitterBase;
class Result;

// -----------------------------------------------------------------------------
// A system of classes to represent all the types we'll need to deal with in
// the prototypes of intrinsics.
//
// Query methods include finding out the C name of a type; the "LLVM name" in
// the sense of a C++ code snippet that can be used in the codegen function;
// the suffix that represents the type in the ACLE intrinsic naming scheme
// (e.g. 's32' represents int32_t in intrinsics such as vaddq_s32); whether the
// type is floating-point related (hence should be under #ifdef in the MVE
// header so that it isn't included in integer-only MVE mode); and the type's
// size in bits. Not all subtypes support all these queries.

class Type {
public:
  enum class TypeKind {
    // Void appears as a return type (for store intrinsics, which are pure
    // side-effect). It's also used as the parameter type in the Tablegen
    // when an intrinsic doesn't need to come in various suffixed forms like
    // vfooq_s8,vfooq_u16,vfooq_f32.
    Void,

    // Scalar is used for ordinary int and float types of all sizes.
    Scalar,

    // Vector is used for anything that occupies exactly one MVE vector
    // register, i.e. {uint,int,float}NxM_t.
    Vector,

    // MultiVector is used for the {uint,int,float}NxMxK_t types used by the
    // interleaving load/store intrinsics v{ld,st}{2,4}q.
    MultiVector,

    // Predicate is used by all the predicated intrinsics. Its C
    // representation is mve_pred16_t (which is just an alias for uint16_t).
    // But we give more detail here, by indicating that a given predicate
    // instruction is logically regarded as a vector of i1 containing the
    // same number of lanes as the input vector type. So our Predicate type
    // comes with a lane count, which we use to decide which kind of <n x i1>
    // we'll invoke the pred_i2v IR intrinsic to translate it into.
    Predicate,

    // Pointer is used for pointer types (obviously), and comes with a flag
    // indicating whether it's a pointer to a const or mutable instance of
    // the pointee type.
    Pointer,
  };

private:
  const TypeKind TKind;

protected:
  Type(TypeKind K) : TKind(K) {}

public:
  TypeKind typeKind() const { return TKind; }
  virtual ~Type() = default;
  virtual bool requiresFloat() const = 0;
  virtual bool requiresMVE() const = 0;
  virtual unsigned sizeInBits() const = 0;
  virtual std::string cName() const = 0;
  virtual std::string llvmName() const {
    PrintFatalError("no LLVM type name available for type " + cName());
  }
  virtual std::string acleSuffix(std::string) const {
    PrintFatalError("no ACLE suffix available for this type");
  }
};

enum class ScalarTypeKind { SignedInt, UnsignedInt, Float };
inline std::string toLetter(ScalarTypeKind kind) {
  switch (kind) {
  case ScalarTypeKind::SignedInt:
    return "s";
  case ScalarTypeKind::UnsignedInt:
    return "u";
  case ScalarTypeKind::Float:
    return "f";
  }
  llvm_unreachable("Unhandled ScalarTypeKind enum");
}
inline std::string toCPrefix(ScalarTypeKind kind) {
  switch (kind) {
  case ScalarTypeKind::SignedInt:
    return "int";
  case ScalarTypeKind::UnsignedInt:
    return "uint";
  case ScalarTypeKind::Float:
    return "float";
  }
  llvm_unreachable("Unhandled ScalarTypeKind enum");
}

class VoidType : public Type {
public:
  VoidType() : Type(TypeKind::Void) {}
  unsigned sizeInBits() const override { return 0; }
  bool requiresFloat() const override { return false; }
  bool requiresMVE() const override { return false; }
  std::string cName() const override { return "void"; }

  static bool classof(const Type *T) { return T->typeKind() == TypeKind::Void; }
  std::string acleSuffix(std::string) const override { return ""; }
};

class PointerType : public Type {
  const Type *Pointee;
  bool Const;

public:
  PointerType(const Type *Pointee, bool Const)
      : Type(TypeKind::Pointer), Pointee(Pointee), Const(Const) {}
  unsigned sizeInBits() const override { return 32; }
  bool requiresFloat() const override { return Pointee->requiresFloat(); }
  bool requiresMVE() const override { return Pointee->requiresMVE(); }
  std::string cName() const override {
    std::string Name = Pointee->cName();

    // The syntax for a pointer in C is different when the pointee is
    // itself a pointer. The MVE intrinsics don't contain any double
    // pointers, so we don't need to worry about that wrinkle.
    assert(!isa<PointerType>(Pointee) && "Pointer to pointer not supported");

    if (Const)
      Name = "const " + Name;
    return Name + " *";
  }
  std::string llvmName() const override {
    return "llvm::PointerType::getUnqual(" + Pointee->llvmName() + ")";
  }
  const Type *getPointeeType() const { return Pointee; }

  static bool classof(const Type *T) {
    return T->typeKind() == TypeKind::Pointer;
  }
};

// Base class for all the types that have a name of the form
// [prefix][numbers]_t, like int32_t, uint16x8_t, float32x4x2_t.
//
// For this sub-hierarchy we invent a cNameBase() method which returns the
// whole name except for the trailing "_t", so that Vector and MultiVector can
// append an extra "x2" or whatever to their element type's cNameBase(). Then
// the main cName() query method puts "_t" on the end for the final type name.

class CRegularNamedType : public Type {
  using Type::Type;
  virtual std::string cNameBase() const = 0;

public:
  std::string cName() const override { return cNameBase() + "_t"; }
};

class ScalarType : public CRegularNamedType {
  ScalarTypeKind Kind;
  unsigned Bits;
  std::string NameOverride;

public:
  ScalarType(const Record *Record) : CRegularNamedType(TypeKind::Scalar) {
    Kind = StringSwitch<ScalarTypeKind>(Record->getValueAsString("kind"))
               .Case("s", ScalarTypeKind::SignedInt)
               .Case("u", ScalarTypeKind::UnsignedInt)
               .Case("f", ScalarTypeKind::Float);
    Bits = Record->getValueAsInt("size");
    NameOverride = std::string(Record->getValueAsString("nameOverride"));
  }
  unsigned sizeInBits() const override { return Bits; }
  ScalarTypeKind kind() const { return Kind; }
  std::string suffix() const { return toLetter(Kind) + utostr(Bits); }
  std::string cNameBase() const override {
    return toCPrefix(Kind) + utostr(Bits);
  }
  std::string cName() const override {
    if (NameOverride.empty())
      return CRegularNamedType::cName();
    return NameOverride;
  }
  std::string llvmName() const override {
    if (Kind == ScalarTypeKind::Float) {
      if (Bits == 16)
        return "HalfTy";
      if (Bits == 32)
        return "FloatTy";
      if (Bits == 64)
        return "DoubleTy";
      PrintFatalError("bad size for floating type");
    }
    return "Int" + utostr(Bits) + "Ty";
  }
  std::string acleSuffix(std::string overrideLetter) const override {
    return "_" + (overrideLetter.size() ? overrideLetter : toLetter(Kind))
               + utostr(Bits);
  }
  bool isInteger() const { return Kind != ScalarTypeKind::Float; }
  bool requiresFloat() const override { return !isInteger(); }
  bool requiresMVE() const override { return false; }
  bool hasNonstandardName() const { return !NameOverride.empty(); }

  static bool classof(const Type *T) {
    return T->typeKind() == TypeKind::Scalar;
  }
};

class VectorType : public CRegularNamedType {
  const ScalarType *Element;
  unsigned Lanes;

public:
  VectorType(const ScalarType *Element, unsigned Lanes)
      : CRegularNamedType(TypeKind::Vector), Element(Element), Lanes(Lanes) {}
  unsigned sizeInBits() const override { return Lanes * Element->sizeInBits(); }
  unsigned lanes() const { return Lanes; }
  bool requiresFloat() const override { return Element->requiresFloat(); }
  bool requiresMVE() const override { return true; }
  std::string cNameBase() const override {
    return Element->cNameBase() + "x" + utostr(Lanes);
  }
  std::string llvmName() const override {
    return "llvm::FixedVectorType::get(" + Element->llvmName() + ", " +
           utostr(Lanes) + ")";
  }

  static bool classof(const Type *T) {
    return T->typeKind() == TypeKind::Vector;
  }
};

class MultiVectorType : public CRegularNamedType {
  const VectorType *Element;
  unsigned Registers;

public:
  MultiVectorType(unsigned Registers, const VectorType *Element)
      : CRegularNamedType(TypeKind::MultiVector), Element(Element),
        Registers(Registers) {}
  unsigned sizeInBits() const override {
    return Registers * Element->sizeInBits();
  }
  unsigned registers() const { return Registers; }
  bool requiresFloat() const override { return Element->requiresFloat(); }
  bool requiresMVE() const override { return true; }
  std::string cNameBase() const override {
    return Element->cNameBase() + "x" + utostr(Registers);
  }

  // MultiVectorType doesn't override llvmName, because we don't expect to do
  // automatic code generation for the MVE intrinsics that use it: the {vld2,
  // vld4, vst2, vst4} family are the only ones that use these types, so it was
  // easier to hand-write the codegen for dealing with these structs than to
  // build in lots of extra automatic machinery that would only be used once.

  static bool classof(const Type *T) {
    return T->typeKind() == TypeKind::MultiVector;
  }
};

class PredicateType : public CRegularNamedType {
  unsigned Lanes;

public:
  PredicateType(unsigned Lanes)
      : CRegularNamedType(TypeKind::Predicate), Lanes(Lanes) {}
  unsigned sizeInBits() const override { return 16; }
  std::string cNameBase() const override { return "mve_pred16"; }
  bool requiresFloat() const override { return false; };
  bool requiresMVE() const override { return true; }
  std::string llvmName() const override {
    return "llvm::FixedVectorType::get(Builder.getInt1Ty(), " + utostr(Lanes) +
           ")";
  }

  static bool classof(const Type *T) {
    return T->typeKind() == TypeKind::Predicate;
  }
};

// -----------------------------------------------------------------------------
// Class to facilitate merging together the code generation for many intrinsics
// by means of varying a few constant or type parameters.
//
// Most obviously, the intrinsics in a single parametrised family will have
// code generation sequences that only differ in a type or two, e.g. vaddq_s8
// and vaddq_u16 will look the same apart from putting a different vector type
// in the call to CGM.getIntrinsic(). But also, completely different intrinsics
// will often code-generate in the same way, with only a different choice of
// _which_ IR intrinsic they lower to (e.g. vaddq_m_s8 and vmulq_m_s8), but
// marshalling the arguments and return values of the IR intrinsic in exactly
// the same way. And others might differ only in some other kind of constant,
// such as a lane index.
//
// So, when we generate the IR-building code for all these intrinsics, we keep
// track of every value that could possibly be pulled out of the code and
// stored ahead of time in a local variable. Then we group together intrinsics
// by textual equivalence of the code that would result if _all_ those
// parameters were stored in local variables. That gives us maximal sets that
// can be implemented by a single piece of IR-building code by changing
// parameter values ahead of time.
//
// After we've done that, we do a second pass in which we only allocate _some_
// of the parameters into local variables, by tracking which ones have the same
// values as each other (so that a single variable can be reused) and which
// ones are the same across the whole set (so that no variable is needed at
// all).
//
// Hence the class below. Its allocParam method is invoked during code
// generation by every method of a Result subclass (see below) that wants to
// give it the opportunity to pull something out into a switchable parameter.
// It returns a variable name for the parameter, or (if it's being used in the
// second pass once we've decided that some parameters don't need to be stored
// in variables after all) it might just return the input expression unchanged.

struct CodeGenParamAllocator {
  // Accumulated during code generation
  std::vector<std::string> *ParamTypes = nullptr;
  std::vector<std::string> *ParamValues = nullptr;

  // Provided ahead of time in pass 2, to indicate which parameters are being
  // assigned to what. This vector contains an entry for each call to
  // allocParam expected during code gen (which we counted up in pass 1), and
  // indicates the number of the parameter variable that should be returned, or
  // -1 if this call shouldn't allocate a parameter variable at all.
  //
  // We rely on the recursive code generation working identically in passes 1
  // and 2, so that the same list of calls to allocParam happen in the same
  // order. That guarantees that the parameter numbers recorded in pass 1 will
  // match the entries in this vector that store what EmitterBase::EmitBuiltinCG
  // decided to do about each one in pass 2.
  std::vector<int> *ParamNumberMap = nullptr;

  // Internally track how many things we've allocated
  unsigned nparams = 0;

  std::string allocParam(StringRef Type, StringRef Value) {
    unsigned ParamNumber;

    if (!ParamNumberMap) {
      // In pass 1, unconditionally assign a new parameter variable to every
      // value we're asked to process.
      ParamNumber = nparams++;
    } else {
      // In pass 2, consult the map provided by the caller to find out which
      // variable we should be keeping things in.
      int MapValue = (*ParamNumberMap)[nparams++];
      if (MapValue < 0)
        return std::string(Value);
      ParamNumber = MapValue;
    }

    // If we've allocated a new parameter variable for the first time, store
    // its type and value to be retrieved after codegen.
    if (ParamTypes && ParamTypes->size() == ParamNumber)
      ParamTypes->push_back(std::string(Type));
    if (ParamValues && ParamValues->size() == ParamNumber)
      ParamValues->push_back(std::string(Value));

    // Unimaginative naming scheme for parameter variables.
    return "Param" + utostr(ParamNumber);
  }
};

// -----------------------------------------------------------------------------
// System of classes that represent all the intermediate values used during
// code-generation for an intrinsic.
//
// The base class 'Result' can represent a value of the LLVM type 'Value', or
// sometimes 'Address' (for loads/stores, including an alignment requirement).
//
// In the case where the Tablegen provides a value in the codegen dag as a
// plain integer literal, the Result object we construct here will be one that
// returns true from hasIntegerConstantValue(). This allows the generated C++
// code to use the constant directly in contexts which can take a literal
// integer, such as Builder.CreateExtractValue(thing, 1), without going to the
// effort of calling llvm::ConstantInt::get() and then pulling the constant
// back out of the resulting llvm:Value later.

class Result {
public:
  // Convenient shorthand for the pointer type we'll be using everywhere.
  using Ptr = std::shared_ptr<Result>;

private:
  Ptr Predecessor;
  std::string VarName;
  bool VarNameUsed = false;
  unsigned Visited = 0;

public:
  virtual ~Result() = default;
  using Scope = std::map<std::string, Ptr>;
  virtual void genCode(raw_ostream &OS, CodeGenParamAllocator &) const = 0;
  virtual bool hasIntegerConstantValue() const { return false; }
  virtual uint32_t integerConstantValue() const { return 0; }
  virtual bool hasIntegerValue() const { return false; }
  virtual std::string getIntegerValue(const std::string &) {
    llvm_unreachable("non-working Result::getIntegerValue called");
  }
  virtual std::string typeName() const { return "Value *"; }

  // Mostly, when a code-generation operation has a dependency on prior
  // operations, it's because it uses the output values of those operations as
  // inputs. But there's one exception, which is the use of 'seq' in Tablegen
  // to indicate that operations have to be performed in sequence regardless of
  // whether they use each others' output values.
  //
  // So, the actual generation of code is done by depth-first search, using the
  // prerequisites() method to get a list of all the other Results that have to
  // be computed before this one. That method divides into the 'predecessor',
  // set by setPredecessor() while processing a 'seq' dag node, and the list
  // returned by 'morePrerequisites', which each subclass implements to return
  // a list of the Results it uses as input to whatever its own computation is
  // doing.

  virtual void morePrerequisites(std::vector<Ptr> &output) const {}
  std::vector<Ptr> prerequisites() const {
    std::vector<Ptr> ToRet;
    if (Predecessor)
      ToRet.push_back(Predecessor);
    morePrerequisites(ToRet);
    return ToRet;
  }

  void setPredecessor(Ptr p) {
    // If the user has nested one 'seq' node inside another, and this
    // method is called on the return value of the inner 'seq' (i.e.
    // the final item inside it), then we can't link _this_ node to p,
    // because it already has a predecessor. Instead, walk the chain
    // until we find the first item in the inner seq, and link that to
    // p, so that nesting seqs has the obvious effect of linking
    // everything together into one long sequential chain.
    Result *r = this;
    while (r->Predecessor)
      r = r->Predecessor.get();
    r->Predecessor = p;
  }

  // Each Result will be assigned a variable name in the output code, but not
  // all those variable names will actually be used (e.g. the return value of
  // Builder.CreateStore has void type, so nobody will want to refer to it). To
  // prevent annoying compiler warnings, we track whether each Result's
  // variable name was ever actually mentioned in subsequent statements, so
  // that it can be left out of the final generated code.
  std::string varname() {
    VarNameUsed = true;
    return VarName;
  }
  void setVarname(const StringRef s) { VarName = std::string(s); }
  bool varnameUsed() const { return VarNameUsed; }

  // Emit code to generate this result as a Value *.
  virtual std::string asValue() {
    return varname();
  }

  // Code generation happens in multiple passes. This method tracks whether a
  // Result has yet been visited in a given pass, without the need for a
  // tedious loop in between passes that goes through and resets a 'visited'
  // flag back to false: you just set Pass=1 the first time round, and Pass=2
  // the second time.
  bool needsVisiting(unsigned Pass) {
    bool ToRet = Visited < Pass;
    Visited = Pass;
    return ToRet;
  }
};

// Result subclass that retrieves one of the arguments to the clang builtin
// function. In cases where the argument has pointer type, we call
// EmitPointerWithAlignment and store the result in a variable of type Address,
// so that load and store IR nodes can know the right alignment. Otherwise, we
// call EmitScalarExpr.
//
// There are aggregate parameters in the MVE intrinsics API, but we don't deal
// with them in this Tablegen back end: they only arise in the vld2q/vld4q and
// vst2q/vst4q family, which is few enough that we just write the code by hand
// for those in CGBuiltin.cpp.
class BuiltinArgResult : public Result {
public:
  unsigned ArgNum;
  bool AddressType;
  bool Immediate;
  BuiltinArgResult(unsigned ArgNum, bool AddressType, bool Immediate)
      : ArgNum(ArgNum), AddressType(AddressType), Immediate(Immediate) {}
  void genCode(raw_ostream &OS, CodeGenParamAllocator &) const override {
    OS << (AddressType ? "EmitPointerWithAlignment" : "EmitScalarExpr")
       << "(E->getArg(" << ArgNum << "))";
  }
  std::string typeName() const override {
    return AddressType ? "Address" : Result::typeName();
  }
  // Emit code to generate this result as a Value *.
  std::string asValue() override {
    if (AddressType)
      return "(" + varname() + ".emitRawPointer(*this))";
    return Result::asValue();
  }
  bool hasIntegerValue() const override { return Immediate; }
  std::string getIntegerValue(const std::string &IntType) override {
    return "GetIntegerConstantValue<" + IntType + ">(E->getArg(" +
           utostr(ArgNum) + "), getContext())";
  }
};

// Result subclass for an integer literal appearing in Tablegen. This may need
// to be turned into an llvm::Result by means of llvm::ConstantInt::get(), or
// it may be used directly as an integer, depending on which IRBuilder method
// it's being passed to.
class IntLiteralResult : public Result {
public:
  const ScalarType *IntegerType;
  uint32_t IntegerValue;
  IntLiteralResult(const ScalarType *IntegerType, uint32_t IntegerValue)
      : IntegerType(IntegerType), IntegerValue(IntegerValue) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    OS << "llvm::ConstantInt::get("
       << ParamAlloc.allocParam("llvm::Type *", IntegerType->llvmName())
       << ", ";
    OS << ParamAlloc.allocParam(IntegerType->cName(), utostr(IntegerValue))
       << ")";
  }
  bool hasIntegerConstantValue() const override { return true; }
  uint32_t integerConstantValue() const override { return IntegerValue; }
};

// Result subclass representing a cast between different integer types. We use
// our own ScalarType abstraction as the representation of the target type,
// which gives both size and signedness.
class IntCastResult : public Result {
public:
  const ScalarType *IntegerType;
  Ptr V;
  IntCastResult(const ScalarType *IntegerType, Ptr V)
      : IntegerType(IntegerType), V(V) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    OS << "Builder.CreateIntCast(" << V->varname() << ", "
       << ParamAlloc.allocParam("llvm::Type *", IntegerType->llvmName()) << ", "
       << ParamAlloc.allocParam("bool",
                                IntegerType->kind() == ScalarTypeKind::SignedInt
                                    ? "true"
                                    : "false")
       << ")";
  }
  void morePrerequisites(std::vector<Ptr> &output) const override {
    output.push_back(V);
  }
};

// Result subclass representing a cast between different pointer types.
class PointerCastResult : public Result {
public:
  const PointerType *PtrType;
  Ptr V;
  PointerCastResult(const PointerType *PtrType, Ptr V)
      : PtrType(PtrType), V(V) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    OS << "Builder.CreatePointerCast(" << V->asValue() << ", "
       << ParamAlloc.allocParam("llvm::Type *", PtrType->llvmName()) << ")";
  }
  void morePrerequisites(std::vector<Ptr> &output) const override {
    output.push_back(V);
  }
};

// Result subclass representing a call to an IRBuilder method. Each IRBuilder
// method we want to use will have a Tablegen record giving the method name and
// describing any important details of how to call it, such as whether a
// particular argument should be an integer constant instead of an llvm::Value.
class IRBuilderResult : public Result {
public:
  StringRef CallPrefix;
  std::vector<Ptr> Args;
  std::set<unsigned> AddressArgs;
  std::map<unsigned, std::string> IntegerArgs;
  IRBuilderResult(StringRef CallPrefix, const std::vector<Ptr> &Args,
                  const std::set<unsigned> &AddressArgs,
                  const std::map<unsigned, std::string> &IntegerArgs)
      : CallPrefix(CallPrefix), Args(Args), AddressArgs(AddressArgs),
        IntegerArgs(IntegerArgs) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    OS << CallPrefix;
    const char *Sep = "";
    for (unsigned i = 0, e = Args.size(); i < e; ++i) {
      Ptr Arg = Args[i];
      auto it = IntegerArgs.find(i);

      OS << Sep;
      Sep = ", ";

      if (it != IntegerArgs.end()) {
        if (Arg->hasIntegerConstantValue())
          OS << "static_cast<" << it->second << ">("
             << ParamAlloc.allocParam(it->second,
                                      utostr(Arg->integerConstantValue()))
             << ")";
        else if (Arg->hasIntegerValue())
          OS << ParamAlloc.allocParam(it->second,
                                      Arg->getIntegerValue(it->second));
      } else {
        OS << Arg->varname();
      }
    }
    OS << ")";
  }
  void morePrerequisites(std::vector<Ptr> &output) const override {
    for (unsigned i = 0, e = Args.size(); i < e; ++i) {
      Ptr Arg = Args[i];
      if (IntegerArgs.find(i) != IntegerArgs.end())
        continue;
      output.push_back(Arg);
    }
  }
};

// Result subclass representing making an Address out of a Value.
class AddressResult : public Result {
public:
  Ptr Arg;
  const Type *Ty;
  unsigned Align;
  AddressResult(Ptr Arg, const Type *Ty, unsigned Align)
      : Arg(Arg), Ty(Ty), Align(Align) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    OS << "Address(" << Arg->varname() << ", " << Ty->llvmName()
       << ", CharUnits::fromQuantity(" << Align << "))";
  }
  std::string typeName() const override {
    return "Address";
  }
  void morePrerequisites(std::vector<Ptr> &output) const override {
    output.push_back(Arg);
  }
};

// Result subclass representing a call to an IR intrinsic, which we first have
// to look up using an Intrinsic::ID constant and an array of types.
class IRIntrinsicResult : public Result {
public:
  std::string IntrinsicID;
  std::vector<const Type *> ParamTypes;
  std::vector<Ptr> Args;
  IRIntrinsicResult(StringRef IntrinsicID,
                    const std::vector<const Type *> &ParamTypes,
                    const std::vector<Ptr> &Args)
      : IntrinsicID(std::string(IntrinsicID)), ParamTypes(ParamTypes),
        Args(Args) {}
  void genCode(raw_ostream &OS,
               CodeGenParamAllocator &ParamAlloc) const override {
    std::string IntNo = ParamAlloc.allocParam(
        "Intrinsic::ID", "Intrinsic::" + IntrinsicID);
    OS << "Builder.CreateCall(CGM.getIntrinsic(" << IntNo;
    if (!ParamTypes.empty()) {
      OS << ", {";
      const char *Sep = "";
      for (auto T : ParamTypes) {
        OS << Sep << ParamAlloc.allocParam("llvm::Type *", T->llvmName());
        Sep = ", ";
      }
      OS << "}";
    }
    OS << "), {";
    const char *Sep = "";
    for (auto Arg : Args) {
      OS << Sep << Arg->asValue();
      Sep = ", ";
    }
    OS << "})";
  }
  void morePrerequisites(std::vector<Ptr> &output) const override {
    output.insert(output.end(), Args.begin(), Args.end());
  }
};

// Result subclass that specifies a type, for use in IRBuilder operations such
// as CreateBitCast that take a type argument.
class TypeResult : public Result {
public:
  const Type *T;
  TypeResult(const Type *T) : T(T) {}
  void genCode(raw_ostream &OS, CodeGenParamAllocator &) const override {
    OS << T->llvmName();
  }
  std::string typeName() const override {
    return "llvm::Type *";
  }
};

// -----------------------------------------------------------------------------
// Class that describes a single ACLE intrinsic.
//
// A Tablegen record will typically describe more than one ACLE intrinsic, by
// means of setting the 'list<Type> Params' field to a list of multiple
// parameter types, so as to define vaddq_{s8,u8,...,f16,f32} all in one go.
// We'll end up with one instance of ACLEIntrinsic for *each* parameter type,
// rather than a single one for all of them. Hence, the constructor takes both
// a Tablegen record and the current value of the parameter type.

class ACLEIntrinsic {
  // Structure documenting that one of the intrinsic's arguments is required to
  // be a compile-time constant integer, and what constraints there are on its
  // value. Used when generating Sema checking code.
  struct ImmediateArg {
    enum class BoundsType { ExplicitRange, UInt };
    BoundsType boundsType;
    int64_t i1, i2;
    StringRef ExtraCheckType, ExtraCheckArgs;
    const Type *ArgType;
  };

  // For polymorphic intrinsics, FullName is the explicit name that uniquely
  // identifies this variant of the intrinsic, and ShortName is the name it
  // shares with at least one other intrinsic.
  std::string ShortName, FullName;

  // Name of the architecture extension, used in the Clang builtin name
  StringRef BuiltinExtension;

  // A very small number of intrinsics _only_ have a polymorphic
  // variant (vuninitializedq taking an unevaluated argument).
  bool PolymorphicOnly;

  // Another rarely-used flag indicating that the builtin doesn't
  // evaluate its argument(s) at all.
  bool NonEvaluating;

  // True if the intrinsic needs only the C header part (no codegen, semantic
  // checks, etc). Used for redeclaring MVE intrinsics in the arm_cde.h header.
  bool HeaderOnly;

  const Type *ReturnType;
  std::vector<const Type *> ArgTypes;
  std::map<unsigned, ImmediateArg> ImmediateArgs;
  Result::Ptr Code;

  std::map<std::string, std::string> CustomCodeGenArgs;

  // Recursive function that does the internals of code generation.
  void genCodeDfs(Result::Ptr V, std::list<Result::Ptr> &Used,
                  unsigned Pass) const {
    if (!V->needsVisiting(Pass))
      return;

    for (Result::Ptr W : V->prerequisites())
      genCodeDfs(W, Used, Pass);

    Used.push_back(V);
  }

public:
  const std::string &shortName() const { return ShortName; }
  const std::string &fullName() const { return FullName; }
  StringRef builtinExtension() const { return BuiltinExtension; }
  const Type *returnType() const { return ReturnType; }
  const std::vector<const Type *> &argTypes() const { return ArgTypes; }
  bool requiresFloat() const {
    if (ReturnType->requiresFloat())
      return true;
    for (const Type *T : ArgTypes)
      if (T->requiresFloat())
        return true;
    return false;
  }
  bool requiresMVE() const {
    return ReturnType->requiresMVE() ||
           any_of(ArgTypes, [](const Type *T) { return T->requiresMVE(); });
  }
  bool polymorphic() const { return ShortName != FullName; }
  bool polymorphicOnly() const { return PolymorphicOnly; }
  bool nonEvaluating() const { return NonEvaluating; }
  bool headerOnly() const { return HeaderOnly; }

  // External entry point for code generation, called from EmitterBase.
  void genCode(raw_ostream &OS, CodeGenParamAllocator &ParamAlloc,
               unsigned Pass) const {
    assert(!headerOnly() && "Called genCode for header-only intrinsic");
    if (!hasCode()) {
      for (auto kv : CustomCodeGenArgs)
        OS << "  " << kv.first << " = " << kv.second << ";\n";
      OS << "  break; // custom code gen\n";
      return;
    }
    std::list<Result::Ptr> Used;
    genCodeDfs(Code, Used, Pass);

    unsigned varindex = 0;
    for (Result::Ptr V : Used)
      if (V->varnameUsed())
        V->setVarname("Val" + utostr(varindex++));

    for (Result::Ptr V : Used) {
      OS << "  ";
      if (V == Used.back()) {
        assert(!V->varnameUsed());
        OS << "return "; // FIXME: what if the top-level thing is void?
      } else if (V->varnameUsed()) {
        std::string Type = V->typeName();
        OS << V->typeName();
        if (!StringRef(Type).ends_with("*"))
          OS << " ";
        OS << V->varname() << " = ";
      }
      V->genCode(OS, ParamAlloc);
      OS << ";\n";
    }
  }
  bool hasCode() const { return Code != nullptr; }

  static std::string signedHexLiteral(const llvm::APInt &iOrig) {
    llvm::APInt i = iOrig.trunc(64);
    SmallString<40> s;
    i.toString(s, 16, true, true);
    return std::string(s);
  }

  std::string genSema() const {
    assert(!headerOnly() && "Called genSema for header-only intrinsic");
    std::vector<std::string> SemaChecks;

    for (const auto &kv : ImmediateArgs) {
      const ImmediateArg &IA = kv.second;

      llvm::APInt lo(128, 0), hi(128, 0);
      switch (IA.boundsType) {
      case ImmediateArg::BoundsType::ExplicitRange:
        lo = IA.i1;
        hi = IA.i2;
        break;
      case ImmediateArg::BoundsType::UInt:
        lo = 0;
        hi = llvm::APInt::getMaxValue(IA.i1).zext(128);
        break;
      }

      std::string Index = utostr(kv.first);

      // Emit a range check if the legal range of values for the
      // immediate is smaller than the _possible_ range of values for
      // its type.
      unsigned ArgTypeBits = IA.ArgType->sizeInBits();
      llvm::APInt ArgTypeRange = llvm::APInt::getMaxValue(ArgTypeBits).zext(128);
      llvm::APInt ActualRange = (hi-lo).trunc(64).sext(128);
      if (ActualRange.ult(ArgTypeRange))
        SemaChecks.push_back("SemaRef.BuiltinConstantArgRange(TheCall, " +
                             Index + ", " + signedHexLiteral(lo) + ", " +
                             signedHexLiteral(hi) + ")");

      if (!IA.ExtraCheckType.empty()) {
        std::string Suffix;
        if (!IA.ExtraCheckArgs.empty()) {
          std::string tmp;
          StringRef Arg = IA.ExtraCheckArgs;
          if (Arg == "!lanesize") {
            tmp = utostr(IA.ArgType->sizeInBits());
            Arg = tmp;
          }
          Suffix = (Twine(", ") + Arg).str();
        }
        SemaChecks.push_back((Twine("SemaRef.BuiltinConstantArg") +
                              IA.ExtraCheckType + "(TheCall, " + Index +
                              Suffix + ")")
                                 .str());
      }

      assert(!SemaChecks.empty());
    }
    if (SemaChecks.empty())
      return "";
    return join(std::begin(SemaChecks), std::end(SemaChecks),
                " ||\n         ") +
           ";\n";
  }

  ACLEIntrinsic(EmitterBase &ME, Record *R, const Type *Param);
};

// -----------------------------------------------------------------------------
// The top-level class that holds all the state from analyzing the entire
// Tablegen input.

class EmitterBase {
protected:
  // EmitterBase holds a collection of all the types we've instantiated.
  VoidType Void;
  std::map<std::string, std::unique_ptr<ScalarType>> ScalarTypes;
  std::map<std::tuple<ScalarTypeKind, unsigned, unsigned>,
           std::unique_ptr<VectorType>>
      VectorTypes;
  std::map<std::pair<std::string, unsigned>, std::unique_ptr<MultiVectorType>>
      MultiVectorTypes;
  std::map<unsigned, std::unique_ptr<PredicateType>> PredicateTypes;
  std::map<std::string, std::unique_ptr<PointerType>> PointerTypes;

  // And all the ACLEIntrinsic instances we've created.
  std::map<std::string, std::unique_ptr<ACLEIntrinsic>> ACLEIntrinsics;

public:
  // Methods to create a Type object, or return the right existing one from the
  // maps stored in this object.
  const VoidType *getVoidType() { return &Void; }
  const ScalarType *getScalarType(StringRef Name) {
    return ScalarTypes[std::string(Name)].get();
  }
  const ScalarType *getScalarType(Record *R) {
    return getScalarType(R->getName());
  }
  const VectorType *getVectorType(const ScalarType *ST, unsigned Lanes) {
    std::tuple<ScalarTypeKind, unsigned, unsigned> key(ST->kind(),
                                                       ST->sizeInBits(), Lanes);
    if (VectorTypes.find(key) == VectorTypes.end())
      VectorTypes[key] = std::make_unique<VectorType>(ST, Lanes);
    return VectorTypes[key].get();
  }
  const VectorType *getVectorType(const ScalarType *ST) {
    return getVectorType(ST, 128 / ST->sizeInBits());
  }
  const MultiVectorType *getMultiVectorType(unsigned Registers,
                                            const VectorType *VT) {
    std::pair<std::string, unsigned> key(VT->cNameBase(), Registers);
    if (MultiVectorTypes.find(key) == MultiVectorTypes.end())
      MultiVectorTypes[key] = std::make_unique<MultiVectorType>(Registers, VT);
    return MultiVectorTypes[key].get();
  }
  const PredicateType *getPredicateType(unsigned Lanes) {
    unsigned key = Lanes;
    if (PredicateTypes.find(key) == PredicateTypes.end())
      PredicateTypes[key] = std::make_unique<PredicateType>(Lanes);
    return PredicateTypes[key].get();
  }
  const PointerType *getPointerType(const Type *T, bool Const) {
    PointerType PT(T, Const);
    std::string key = PT.cName();
    if (PointerTypes.find(key) == PointerTypes.end())
      PointerTypes[key] = std::make_unique<PointerType>(PT);
    return PointerTypes[key].get();
  }

  // Methods to construct a type from various pieces of Tablegen. These are
  // always called in the context of setting up a particular ACLEIntrinsic, so
  // there's always an ambient parameter type (because we're iterating through
  // the Params list in the Tablegen record for the intrinsic), which is used
  // to expand Tablegen classes like 'Vector' which mean something different in
  // each member of a parametric family.
  const Type *getType(Record *R, const Type *Param);
  const Type *getType(DagInit *D, const Type *Param);
  const Type *getType(Init *I, const Type *Param);

  // Functions that translate the Tablegen representation of an intrinsic's
  // code generation into a collection of Value objects (which will then be
  // reprocessed to read out the actual C++ code included by CGBuiltin.cpp).
  Result::Ptr getCodeForDag(DagInit *D, const Result::Scope &Scope,
                            const Type *Param);
  Result::Ptr getCodeForDagArg(DagInit *D, unsigned ArgNum,
                               const Result::Scope &Scope, const Type *Param);
  Result::Ptr getCodeForArg(unsigned ArgNum, const Type *ArgType, bool Promote,
                            bool Immediate);

  void GroupSemaChecks(std::map<std::string, std::set<std::string>> &Checks);

  // Constructor and top-level functions.

  EmitterBase(RecordKeeper &Records);
  virtual ~EmitterBase() = default;

  virtual void EmitHeader(raw_ostream &OS) = 0;
  virtual void EmitBuiltinDef(raw_ostream &OS) = 0;
  virtual void EmitBuiltinSema(raw_ostream &OS) = 0;
  void EmitBuiltinCG(raw_ostream &OS);
  void EmitBuiltinAliases(raw_ostream &OS);
};

const Type *EmitterBase::getType(Init *I, const Type *Param) {
  if (auto Dag = dyn_cast<DagInit>(I))
    return getType(Dag, Param);
  if (auto Def = dyn_cast<DefInit>(I))
    return getType(Def->getDef(), Param);

  PrintFatalError("Could not convert this value into a type");
}

const Type *EmitterBase::getType(Record *R, const Type *Param) {
  // Pass to a subfield of any wrapper records. We don't expect more than one
  // of these: immediate operands are used as plain numbers rather than as
  // llvm::Value, so it's meaningless to promote their type anyway.
  if (R->isSubClassOf("Immediate"))
    R = R->getValueAsDef("type");
  else if (R->isSubClassOf("unpromoted"))
    R = R->getValueAsDef("underlying_type");

  if (R->getName() == "Void")
    return getVoidType();
  if (R->isSubClassOf("PrimitiveType"))
    return getScalarType(R);
  if (R->isSubClassOf("ComplexType"))
    return getType(R->getValueAsDag("spec"), Param);

  PrintFatalError(R->getLoc(), "Could not convert this record into a type");
}

const Type *EmitterBase::getType(DagInit *D, const Type *Param) {
  // The meat of the getType system: types in the Tablegen are represented by a
  // dag whose operators select sub-cases of this function.

  Record *Op = cast<DefInit>(D->getOperator())->getDef();
  if (!Op->isSubClassOf("ComplexTypeOp"))
    PrintFatalError(
        "Expected ComplexTypeOp as dag operator in type expression");

  if (Op->getName() == "CTO_Parameter") {
    if (isa<VoidType>(Param))
      PrintFatalError("Parametric type in unparametrised context");
    return Param;
  }

  if (Op->getName() == "CTO_Vec") {
    const Type *Element = getType(D->getArg(0), Param);
    if (D->getNumArgs() == 1) {
      return getVectorType(cast<ScalarType>(Element));
    } else {
      const Type *ExistingVector = getType(D->getArg(1), Param);
      return getVectorType(cast<ScalarType>(Element),
                           cast<VectorType>(ExistingVector)->lanes());
    }
  }

  if (Op->getName() == "CTO_Pred") {
    const Type *Element = getType(D->getArg(0), Param);
    return getPredicateType(128 / Element->sizeInBits());
  }

  if (Op->isSubClassOf("CTO_Tuple")) {
    unsigned Registers = Op->getValueAsInt("n");
    const Type *Element = getType(D->getArg(0), Param);
    return getMultiVectorType(Registers, cast<VectorType>(Element));
  }

  if (Op->isSubClassOf("CTO_Pointer")) {
    const Type *Pointee = getType(D->getArg(0), Param);
    return getPointerType(Pointee, Op->getValueAsBit("const"));
  }

  if (Op->getName() == "CTO_CopyKind") {
    const ScalarType *STSize = cast<ScalarType>(getType(D->getArg(0), Param));
    const ScalarType *STKind = cast<ScalarType>(getType(D->getArg(1), Param));
    for (const auto &kv : ScalarTypes) {
      const ScalarType *RT = kv.second.get();
      if (RT->kind() == STKind->kind() && RT->sizeInBits() == STSize->sizeInBits())
        return RT;
    }
    PrintFatalError("Cannot find a type to satisfy CopyKind");
  }

  if (Op->isSubClassOf("CTO_ScaleSize")) {
    const ScalarType *STKind = cast<ScalarType>(getType(D->getArg(0), Param));
    int Num = Op->getValueAsInt("num"), Denom = Op->getValueAsInt("denom");
    unsigned DesiredSize = STKind->sizeInBits() * Num / Denom;
    for (const auto &kv : ScalarTypes) {
      const ScalarType *RT = kv.second.get();
      if (RT->kind() == STKind->kind() && RT->sizeInBits() == DesiredSize)
        return RT;
    }
    PrintFatalError("Cannot find a type to satisfy ScaleSize");
  }

  PrintFatalError("Bad operator in type dag expression");
}

Result::Ptr EmitterBase::getCodeForDag(DagInit *D, const Result::Scope &Scope,
                                       const Type *Param) {
  Record *Op = cast<DefInit>(D->getOperator())->getDef();

  if (Op->getName() == "seq") {
    Result::Scope SubScope = Scope;
    Result::Ptr PrevV = nullptr;
    for (unsigned i = 0, e = D->getNumArgs(); i < e; ++i) {
      // We don't use getCodeForDagArg here, because the argument name
      // has different semantics in a seq
      Result::Ptr V =
          getCodeForDag(cast<DagInit>(D->getArg(i)), SubScope, Param);
      StringRef ArgName = D->getArgNameStr(i);
      if (!ArgName.empty())
        SubScope[std::string(ArgName)] = V;
      if (PrevV)
        V->setPredecessor(PrevV);
      PrevV = V;
    }
    return PrevV;
  } else if (Op->isSubClassOf("Type")) {
    if (D->getNumArgs() != 1)
      PrintFatalError("Type casts should have exactly one argument");
    const Type *CastType = getType(Op, Param);
    Result::Ptr Arg = getCodeForDagArg(D, 0, Scope, Param);
    if (const auto *ST = dyn_cast<ScalarType>(CastType)) {
      if (!ST->requiresFloat()) {
        if (Arg->hasIntegerConstantValue())
          return std::make_shared<IntLiteralResult>(
              ST, Arg->integerConstantValue());
        else
          return std::make_shared<IntCastResult>(ST, Arg);
      }
    } else if (const auto *PT = dyn_cast<PointerType>(CastType)) {
      return std::make_shared<PointerCastResult>(PT, Arg);
    }
    PrintFatalError("Unsupported type cast");
  } else if (Op->getName() == "address") {
    if (D->getNumArgs() != 2)
      PrintFatalError("'address' should have two arguments");
    Result::Ptr Arg = getCodeForDagArg(D, 0, Scope, Param);

    const Type *Ty = nullptr;
    if (auto *DI = dyn_cast<DagInit>(D->getArg(0)))
      if (auto *PTy = dyn_cast<PointerType>(getType(DI->getOperator(), Param)))
        Ty = PTy->getPointeeType();
    if (!Ty)
      PrintFatalError("'address' pointer argument should be a pointer");

    unsigned Alignment;
    if (auto *II = dyn_cast<IntInit>(D->getArg(1))) {
      Alignment = II->getValue();
    } else {
      PrintFatalError("'address' alignment argument should be an integer");
    }
    return std::make_shared<AddressResult>(Arg, Ty, Alignment);
  } else if (Op->getName() == "unsignedflag") {
    if (D->getNumArgs() != 1)
      PrintFatalError("unsignedflag should have exactly one argument");
    Record *TypeRec = cast<DefInit>(D->getArg(0))->getDef();
    if (!TypeRec->isSubClassOf("Type"))
      PrintFatalError("unsignedflag's argument should be a type");
    if (const auto *ST = dyn_cast<ScalarType>(getType(TypeRec, Param))) {
      return std::make_shared<IntLiteralResult>(
        getScalarType("u32"), ST->kind() == ScalarTypeKind::UnsignedInt);
    } else {
      PrintFatalError("unsignedflag's argument should be a scalar type");
    }
  } else if (Op->getName() == "bitsize") {
    if (D->getNumArgs() != 1)
      PrintFatalError("bitsize should have exactly one argument");
    Record *TypeRec = cast<DefInit>(D->getArg(0))->getDef();
    if (!TypeRec->isSubClassOf("Type"))
      PrintFatalError("bitsize's argument should be a type");
    if (const auto *ST = dyn_cast<ScalarType>(getType(TypeRec, Param))) {
      return std::make_shared<IntLiteralResult>(getScalarType("u32"),
                                                ST->sizeInBits());
    } else {
      PrintFatalError("bitsize's argument should be a scalar type");
    }
  } else {
    std::vector<Result::Ptr> Args;
    for (unsigned i = 0, e = D->getNumArgs(); i < e; ++i)
      Args.push_back(getCodeForDagArg(D, i, Scope, Param));
    if (Op->isSubClassOf("IRBuilderBase")) {
      std::set<unsigned> AddressArgs;
      std::map<unsigned, std::string> IntegerArgs;
      for (Record *sp : Op->getValueAsListOfDefs("special_params")) {
        unsigned Index = sp->getValueAsInt("index");
        if (sp->isSubClassOf("IRBuilderAddrParam")) {
          AddressArgs.insert(Index);
        } else if (sp->isSubClassOf("IRBuilderIntParam")) {
          IntegerArgs[Index] = std::string(sp->getValueAsString("type"));
        }
      }
      return std::make_shared<IRBuilderResult>(Op->getValueAsString("prefix"),
                                               Args, AddressArgs, IntegerArgs);
    } else if (Op->isSubClassOf("IRIntBase")) {
      std::vector<const Type *> ParamTypes;
      for (Record *RParam : Op->getValueAsListOfDefs("params"))
        ParamTypes.push_back(getType(RParam, Param));
      std::string IntName = std::string(Op->getValueAsString("intname"));
      if (Op->getValueAsBit("appendKind"))
        IntName += "_" + toLetter(cast<ScalarType>(Param)->kind());
      return std::make_shared<IRIntrinsicResult>(IntName, ParamTypes, Args);
    } else {
      PrintFatalError("Unsupported dag node " + Op->getName());
    }
  }
}

Result::Ptr EmitterBase::getCodeForDagArg(DagInit *D, unsigned ArgNum,
                                          const Result::Scope &Scope,
                                          const Type *Param) {
  Init *Arg = D->getArg(ArgNum);
  StringRef Name = D->getArgNameStr(ArgNum);

  if (!Name.empty()) {
    if (!isa<UnsetInit>(Arg))
      PrintFatalError(
          "dag operator argument should not have both a value and a name");
    auto it = Scope.find(std::string(Name));
    if (it == Scope.end())
      PrintFatalError("unrecognized variable name '" + Name + "'");
    return it->second;
  }

  // Sometimes the Arg is a bit. Prior to multiclass template argument
  // checking, integers would sneak through the bit declaration,
  // but now they really are bits.
  if (auto *BI = dyn_cast<BitInit>(Arg))
    return std::make_shared<IntLiteralResult>(getScalarType("u32"),
                                              BI->getValue());

  if (auto *II = dyn_cast<IntInit>(Arg))
    return std::make_shared<IntLiteralResult>(getScalarType("u32"),
                                              II->getValue());

  if (auto *DI = dyn_cast<DagInit>(Arg))
    return getCodeForDag(DI, Scope, Param);

  if (auto *DI = dyn_cast<DefInit>(Arg)) {
    Record *Rec = DI->getDef();
    if (Rec->isSubClassOf("Type")) {
      const Type *T = getType(Rec, Param);
      return std::make_shared<TypeResult>(T);
    }
  }

  PrintError("bad DAG argument type for code generation");
  PrintNote("DAG: " + D->getAsString());
  if (TypedInit *Typed = dyn_cast<TypedInit>(Arg))
    PrintNote("argument type: " + Typed->getType()->getAsString());
  PrintFatalNote("argument number " + Twine(ArgNum) + ": " + Arg->getAsString());
}

Result::Ptr EmitterBase::getCodeForArg(unsigned ArgNum, const Type *ArgType,
                                       bool Promote, bool Immediate) {
  Result::Ptr V = std::make_shared<BuiltinArgResult>(
      ArgNum, isa<PointerType>(ArgType), Immediate);

  if (Promote) {
    if (const auto *ST = dyn_cast<ScalarType>(ArgType)) {
      if (ST->isInteger() && ST->sizeInBits() < 32)
        V = std::make_shared<IntCastResult>(getScalarType("u32"), V);
    } else if (const auto *PT = dyn_cast<PredicateType>(ArgType)) {
      V = std::make_shared<IntCastResult>(getScalarType("u32"), V);
      V = std::make_shared<IRIntrinsicResult>("arm_mve_pred_i2v",
                                              std::vector<const Type *>{PT},
                                              std::vector<Result::Ptr>{V});
    }
  }

  return V;
}

ACLEIntrinsic::ACLEIntrinsic(EmitterBase &ME, Record *R, const Type *Param)
    : ReturnType(ME.getType(R->getValueAsDef("ret"), Param)) {
  // Derive the intrinsic's full name, by taking the name of the
  // Tablegen record (or override) and appending the suffix from its
  // parameter type. (If the intrinsic is unparametrised, its
  // parameter type will be given as Void, which returns the empty
  // string for acleSuffix.)
  StringRef BaseName =
      (R->isSubClassOf("NameOverride") ? R->getValueAsString("basename")
                                       : R->getName());
  StringRef overrideLetter = R->getValueAsString("overrideKindLetter");
  FullName =
      (Twine(BaseName) + Param->acleSuffix(std::string(overrideLetter))).str();

  // Derive the intrinsic's polymorphic name, by removing components from the
  // full name as specified by its 'pnt' member ('polymorphic name type'),
  // which indicates how many type suffixes to remove, and any other piece of
  // the name that should be removed.
  Record *PolymorphicNameType = R->getValueAsDef("pnt");
  SmallVector<StringRef, 8> NameParts;
  StringRef(FullName).split(NameParts, '_');
  for (unsigned i = 0, e = PolymorphicNameType->getValueAsInt(
                           "NumTypeSuffixesToDiscard");
       i < e; ++i)
    NameParts.pop_back();
  if (!PolymorphicNameType->isValueUnset("ExtraSuffixToDiscard")) {
    StringRef ExtraSuffix =
        PolymorphicNameType->getValueAsString("ExtraSuffixToDiscard");
    auto it = NameParts.end();
    while (it != NameParts.begin()) {
      --it;
      if (*it == ExtraSuffix) {
        NameParts.erase(it);
        break;
      }
    }
  }
  ShortName = join(std::begin(NameParts), std::end(NameParts), "_");

  BuiltinExtension = R->getValueAsString("builtinExtension");

  PolymorphicOnly = R->getValueAsBit("polymorphicOnly");
  NonEvaluating = R->getValueAsBit("nonEvaluating");
  HeaderOnly = R->getValueAsBit("headerOnly");

  // Process the intrinsic's argument list.
  DagInit *ArgsDag = R->getValueAsDag("args");
  Result::Scope Scope;
  for (unsigned i = 0, e = ArgsDag->getNumArgs(); i < e; ++i) {
    Init *TypeInit = ArgsDag->getArg(i);

    bool Promote = true;
    if (auto TypeDI = dyn_cast<DefInit>(TypeInit))
      if (TypeDI->getDef()->isSubClassOf("unpromoted"))
        Promote = false;

    // Work out the type of the argument, for use in the function prototype in
    // the header file.
    const Type *ArgType = ME.getType(TypeInit, Param);
    ArgTypes.push_back(ArgType);

    // If the argument is a subclass of Immediate, record the details about
    // what values it can take, for Sema checking.
    bool Immediate = false;
    if (auto TypeDI = dyn_cast<DefInit>(TypeInit)) {
      Record *TypeRec = TypeDI->getDef();
      if (TypeRec->isSubClassOf("Immediate")) {
        Immediate = true;

        Record *Bounds = TypeRec->getValueAsDef("bounds");
        ImmediateArg &IA = ImmediateArgs[i];
        if (Bounds->isSubClassOf("IB_ConstRange")) {
          IA.boundsType = ImmediateArg::BoundsType::ExplicitRange;
          IA.i1 = Bounds->getValueAsInt("lo");
          IA.i2 = Bounds->getValueAsInt("hi");
        } else if (Bounds->getName() == "IB_UEltValue") {
          IA.boundsType = ImmediateArg::BoundsType::UInt;
          IA.i1 = Param->sizeInBits();
        } else if (Bounds->getName() == "IB_LaneIndex") {
          IA.boundsType = ImmediateArg::BoundsType::ExplicitRange;
          IA.i1 = 0;
          IA.i2 = 128 / Param->sizeInBits() - 1;
        } else if (Bounds->isSubClassOf("IB_EltBit")) {
          IA.boundsType = ImmediateArg::BoundsType::ExplicitRange;
          IA.i1 = Bounds->getValueAsInt("base");
          const Type *T = ME.getType(Bounds->getValueAsDef("type"), Param);
          IA.i2 = IA.i1 + T->sizeInBits() - 1;
        } else {
          PrintFatalError("unrecognised ImmediateBounds subclass");
        }

        IA.ArgType = ArgType;

        if (!TypeRec->isValueUnset("extra")) {
          IA.ExtraCheckType = TypeRec->getValueAsString("extra");
          if (!TypeRec->isValueUnset("extraarg"))
            IA.ExtraCheckArgs = TypeRec->getValueAsString("extraarg");
        }
      }
    }

    // The argument will usually have a name in the arguments dag, which goes
    // into the variable-name scope that the code gen will refer to.
    StringRef ArgName = ArgsDag->getArgNameStr(i);
    if (!ArgName.empty())
      Scope[std::string(ArgName)] =
          ME.getCodeForArg(i, ArgType, Promote, Immediate);
  }

  // Finally, go through the codegen dag and translate it into a Result object
  // (with an arbitrary DAG of depended-on Results hanging off it).
  DagInit *CodeDag = R->getValueAsDag("codegen");
  Record *MainOp = cast<DefInit>(CodeDag->getOperator())->getDef();
  if (MainOp->isSubClassOf("CustomCodegen")) {
    // Or, if it's the special case of CustomCodegen, just accumulate
    // a list of parameters we're going to assign to variables before
    // breaking from the loop.
    CustomCodeGenArgs["CustomCodeGenType"] =
        (Twine("CustomCodeGen::") + MainOp->getValueAsString("type")).str();
    for (unsigned i = 0, e = CodeDag->getNumArgs(); i < e; ++i) {
      StringRef Name = CodeDag->getArgNameStr(i);
      if (Name.empty()) {
        PrintFatalError("Operands to CustomCodegen should have names");
      } else if (auto *II = dyn_cast<IntInit>(CodeDag->getArg(i))) {
        CustomCodeGenArgs[std::string(Name)] = itostr(II->getValue());
      } else if (auto *SI = dyn_cast<StringInit>(CodeDag->getArg(i))) {
        CustomCodeGenArgs[std::string(Name)] = std::string(SI->getValue());
      } else {
        PrintFatalError("Operands to CustomCodegen should be integers");
      }
    }
  } else {
    Code = ME.getCodeForDag(CodeDag, Scope, Param);
  }
}

EmitterBase::EmitterBase(RecordKeeper &Records) {
  // Construct the whole EmitterBase.

  // First, look up all the instances of PrimitiveType. This gives us the list
  // of vector typedefs we have to put in arm_mve.h, and also allows us to
  // collect all the useful ScalarType instances into a big list so that we can
  // use it for operations such as 'find the unsigned version of this signed
  // integer type'.
  for (Record *R : Records.getAllDerivedDefinitions("PrimitiveType"))
    ScalarTypes[std::string(R->getName())] = std::make_unique<ScalarType>(R);

  // Now go through the instances of Intrinsic, and for each one, iterate
  // through its list of type parameters making an ACLEIntrinsic for each one.
  for (Record *R : Records.getAllDerivedDefinitions("Intrinsic")) {
    for (Record *RParam : R->getValueAsListOfDefs("params")) {
      const Type *Param = getType(RParam, getVoidType());
      auto Intrinsic = std::make_unique<ACLEIntrinsic>(*this, R, Param);
      ACLEIntrinsics[Intrinsic->fullName()] = std::move(Intrinsic);
    }
  }
}

/// A wrapper on raw_string_ostream that contains its own buffer rather than
/// having to point it at one elsewhere. (In other words, it works just like
/// std::ostringstream; also, this makes it convenient to declare a whole array
/// of them at once.)
///
/// We have to set this up using multiple inheritance, to ensure that the
/// string member has been constructed before raw_string_ostream's constructor
/// is given a pointer to it.
class string_holder {
protected:
  std::string S;
};
class raw_self_contained_string_ostream : private string_holder,
                                          public raw_string_ostream {
public:
  raw_self_contained_string_ostream() : raw_string_ostream(S) {}
};

const char LLVMLicenseHeader[] =
    " *\n"
    " *\n"
    " * Part of the LLVM Project, under the Apache License v2.0 with LLVM"
    " Exceptions.\n"
    " * See https://llvm.org/LICENSE.txt for license information.\n"
    " * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
    " *\n"
    " *===-----------------------------------------------------------------"
    "------===\n"
    " */\n"
    "\n";

// Machinery for the grouping of intrinsics by similar codegen.
//
// The general setup is that 'MergeableGroup' stores the things that a set of
// similarly shaped intrinsics have in common: the text of their code
// generation, and the number and type of their parameter variables.
// MergeableGroup is the key in a std::map whose value is a set of
// OutputIntrinsic, which stores the ways in which a particular intrinsic
// specializes the MergeableGroup's generic description: the function name and
// the _values_ of the parameter variables.

struct ComparableStringVector : std::vector<std::string> {
  // Infrastructure: a derived class of vector<string> which comes with an
  // ordering, so that it can be used as a key in maps and an element in sets.
  // There's no requirement on the ordering beyond being deterministic.
  bool operator<(const ComparableStringVector &rhs) const {
    if (size() != rhs.size())
      return size() < rhs.size();
    for (size_t i = 0, e = size(); i < e; ++i)
      if ((*this)[i] != rhs[i])
        return (*this)[i] < rhs[i];
    return false;
  }
};

struct OutputIntrinsic {
  const ACLEIntrinsic *Int;
  std::string Name;
  ComparableStringVector ParamValues;
  bool operator<(const OutputIntrinsic &rhs) const {
    if (Name != rhs.Name)
      return Name < rhs.Name;
    return ParamValues < rhs.ParamValues;
  }
};
struct MergeableGroup {
  std::string Code;
  ComparableStringVector ParamTypes;
  bool operator<(const MergeableGroup &rhs) const {
    if (Code != rhs.Code)
      return Code < rhs.Code;
    return ParamTypes < rhs.ParamTypes;
  }
};

void EmitterBase::EmitBuiltinCG(raw_ostream &OS) {
  // Pass 1: generate code for all the intrinsics as if every type or constant
  // that can possibly be abstracted out into a parameter variable will be.
  // This identifies the sets of intrinsics we'll group together into a single
  // piece of code generation.

  std::map<MergeableGroup, std::set<OutputIntrinsic>> MergeableGroupsPrelim;

  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;
    if (Int.headerOnly())
      continue;

    MergeableGroup MG;
    OutputIntrinsic OI;

    OI.Int = &Int;
    OI.Name = Int.fullName();
    CodeGenParamAllocator ParamAllocPrelim{&MG.ParamTypes, &OI.ParamValues};
    raw_string_ostream OS(MG.Code);
    Int.genCode(OS, ParamAllocPrelim, 1);
    OS.flush();

    MergeableGroupsPrelim[MG].insert(OI);
  }

  // Pass 2: for each of those groups, optimize the parameter variable set by
  // eliminating 'parameters' that are the same for all intrinsics in the
  // group, and merging together pairs of parameter variables that take the
  // same values as each other for all intrinsics in the group.

  std::map<MergeableGroup, std::set<OutputIntrinsic>> MergeableGroups;

  for (const auto &kv : MergeableGroupsPrelim) {
    const MergeableGroup &MG = kv.first;
    std::vector<int> ParamNumbers;
    std::map<ComparableStringVector, int> ParamNumberMap;

    // Loop over the parameters for this group.
    for (size_t i = 0, e = MG.ParamTypes.size(); i < e; ++i) {
      // Is this parameter the same for all intrinsics in the group?
      const OutputIntrinsic &OI_first = *kv.second.begin();
      bool Constant = all_of(kv.second, [&](const OutputIntrinsic &OI) {
        return OI.ParamValues[i] == OI_first.ParamValues[i];
      });

      // If so, record it as -1, meaning 'no parameter variable needed'. Then
      // the corresponding call to allocParam in pass 2 will not generate a
      // variable at all, and just use the value inline.
      if (Constant) {
        ParamNumbers.push_back(-1);
        continue;
      }

      // Otherwise, make a list of the values this parameter takes for each
      // intrinsic, and see if that value vector matches anything we already
      // have. We also record the parameter type, so that we don't accidentally
      // match up two parameter variables with different types. (Not that
      // there's much chance of them having textually equivalent values, but in
      // _principle_ it could happen.)
      ComparableStringVector key;
      key.push_back(MG.ParamTypes[i]);
      for (const auto &OI : kv.second)
        key.push_back(OI.ParamValues[i]);

      auto Found = ParamNumberMap.find(key);
      if (Found != ParamNumberMap.end()) {
        // Yes, an existing parameter variable can be reused for this.
        ParamNumbers.push_back(Found->second);
        continue;
      }

      // No, we need a new parameter variable.
      int ExistingIndex = ParamNumberMap.size();
      ParamNumberMap[key] = ExistingIndex;
      ParamNumbers.push_back(ExistingIndex);
    }

    // Now we're ready to do the pass 2 code generation, which will emit the
    // reduced set of parameter variables we've just worked out.

    for (const auto &OI_prelim : kv.second) {
      const ACLEIntrinsic *Int = OI_prelim.Int;

      MergeableGroup MG;
      OutputIntrinsic OI;

      OI.Int = OI_prelim.Int;
      OI.Name = OI_prelim.Name;
      CodeGenParamAllocator ParamAlloc{&MG.ParamTypes, &OI.ParamValues,
                                       &ParamNumbers};
      raw_string_ostream OS(MG.Code);
      Int->genCode(OS, ParamAlloc, 2);
      OS.flush();

      MergeableGroups[MG].insert(OI);
    }
  }

  // Output the actual C++ code.

  for (const auto &kv : MergeableGroups) {
    const MergeableGroup &MG = kv.first;

    // List of case statements in the main switch on BuiltinID, and an open
    // brace.
    const char *prefix = "";
    for (const auto &OI : kv.second) {
      OS << prefix << "case ARM::BI__builtin_arm_" << OI.Int->builtinExtension()
         << "_" << OI.Name << ":";

      prefix = "\n";
    }
    OS << " {\n";

    if (!MG.ParamTypes.empty()) {
      // If we've got some parameter variables, then emit their declarations...
      for (size_t i = 0, e = MG.ParamTypes.size(); i < e; ++i) {
        StringRef Type = MG.ParamTypes[i];
        OS << "  " << Type;
        if (!Type.ends_with("*"))
          OS << " ";
        OS << " Param" << utostr(i) << ";\n";
      }

      // ... and an inner switch on BuiltinID that will fill them in with each
      // individual intrinsic's values.
      OS << "  switch (BuiltinID) {\n";
      for (const auto &OI : kv.second) {
        OS << "  case ARM::BI__builtin_arm_" << OI.Int->builtinExtension()
           << "_" << OI.Name << ":\n";
        for (size_t i = 0, e = MG.ParamTypes.size(); i < e; ++i)
          OS << "    Param" << utostr(i) << " = " << OI.ParamValues[i] << ";\n";
        OS << "    break;\n";
      }
      OS << "  }\n";
    }

    // And finally, output the code, and close the outer pair of braces. (The
    // code will always end with a 'return' statement, so we need not insert a
    // 'break' here.)
    OS << MG.Code << "}\n";
  }
}

void EmitterBase::EmitBuiltinAliases(raw_ostream &OS) {
  // Build a sorted table of:
  // - intrinsic id number
  // - full name
  // - polymorphic name or -1
  StringToOffsetTable StringTable;
  OS << "static const IntrinToName MapData[] = {\n";
  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;
    if (Int.headerOnly())
      continue;
    int32_t ShortNameOffset =
        Int.polymorphic() ? StringTable.GetOrAddStringOffset(Int.shortName())
                          : -1;
    OS << "  { ARM::BI__builtin_arm_" << Int.builtinExtension() << "_"
       << Int.fullName() << ", "
       << StringTable.GetOrAddStringOffset(Int.fullName()) << ", "
       << ShortNameOffset << "},\n";
  }
  OS << "};\n\n";

  OS << "ArrayRef<IntrinToName> Map(MapData);\n\n";

  OS << "static const char IntrinNames[] = {\n";
  StringTable.EmitString(OS);
  OS << "};\n\n";
}

void EmitterBase::GroupSemaChecks(
    std::map<std::string, std::set<std::string>> &Checks) {
  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;
    if (Int.headerOnly())
      continue;
    std::string Check = Int.genSema();
    if (!Check.empty())
      Checks[Check].insert(Int.fullName());
  }
}

// -----------------------------------------------------------------------------
// The class used for generating arm_mve.h and related Clang bits
//

class MveEmitter : public EmitterBase {
public:
  MveEmitter(RecordKeeper &Records) : EmitterBase(Records){};
  void EmitHeader(raw_ostream &OS) override;
  void EmitBuiltinDef(raw_ostream &OS) override;
  void EmitBuiltinSema(raw_ostream &OS) override;
};

void MveEmitter::EmitHeader(raw_ostream &OS) {
  // Accumulate pieces of the header file that will be enabled under various
  // different combinations of #ifdef. The index into parts[] is made up of
  // the following bit flags.
  constexpr unsigned Float = 1;
  constexpr unsigned UseUserNamespace = 2;

  constexpr unsigned NumParts = 4;
  raw_self_contained_string_ostream parts[NumParts];

  // Write typedefs for all the required vector types, and a few scalar
  // types that don't already have the name we want them to have.

  parts[0] << "typedef uint16_t mve_pred16_t;\n";
  parts[Float] << "typedef __fp16 float16_t;\n"
                  "typedef float float32_t;\n";
  for (const auto &kv : ScalarTypes) {
    const ScalarType *ST = kv.second.get();
    if (ST->hasNonstandardName())
      continue;
    raw_ostream &OS = parts[ST->requiresFloat() ? Float : 0];
    const VectorType *VT = getVectorType(ST);

    OS << "typedef __attribute__((__neon_vector_type__(" << VT->lanes()
       << "), __clang_arm_mve_strict_polymorphism)) " << ST->cName() << " "
       << VT->cName() << ";\n";

    // Every vector type also comes with a pair of multi-vector types for
    // the VLD2 and VLD4 instructions.
    for (unsigned n = 2; n <= 4; n += 2) {
      const MultiVectorType *MT = getMultiVectorType(n, VT);
      OS << "typedef struct { " << VT->cName() << " val[" << n << "]; } "
         << MT->cName() << ";\n";
    }
  }
  parts[0] << "\n";
  parts[Float] << "\n";

  // Write declarations for all the intrinsics.

  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;

    // We generate each intrinsic twice, under its full unambiguous
    // name and its shorter polymorphic name (if the latter exists).
    for (bool Polymorphic : {false, true}) {
      if (Polymorphic && !Int.polymorphic())
        continue;
      if (!Polymorphic && Int.polymorphicOnly())
        continue;

      // We also generate each intrinsic under a name like __arm_vfooq
      // (which is in C language implementation namespace, so it's
      // safe to define in any conforming user program) and a shorter
      // one like vfooq (which is in user namespace, so a user might
      // reasonably have used it for something already). If so, they
      // can #define __ARM_MVE_PRESERVE_USER_NAMESPACE before
      // including the header, which will suppress the shorter names
      // and leave only the implementation-namespace ones. Then they
      // have to write __arm_vfooq everywhere, of course.

      for (bool UserNamespace : {false, true}) {
        raw_ostream &OS = parts[(Int.requiresFloat() ? Float : 0) |
                                (UserNamespace ? UseUserNamespace : 0)];

        // Make the name of the function in this declaration.

        std::string FunctionName =
            Polymorphic ? Int.shortName() : Int.fullName();
        if (!UserNamespace)
          FunctionName = "__arm_" + FunctionName;

        // Make strings for the types involved in the function's
        // prototype.

        std::string RetTypeName = Int.returnType()->cName();
        if (!StringRef(RetTypeName).ends_with("*"))
          RetTypeName += " ";

        std::vector<std::string> ArgTypeNames;
        for (const Type *ArgTypePtr : Int.argTypes())
          ArgTypeNames.push_back(ArgTypePtr->cName());
        std::string ArgTypesString =
            join(std::begin(ArgTypeNames), std::end(ArgTypeNames), ", ");

        // Emit the actual declaration. All these functions are
        // declared 'static inline' without a body, which is fine
        // provided clang recognizes them as builtins, and has the
        // effect that this type signature is used in place of the one
        // that Builtins.td didn't provide. That's how we can get
        // structure types that weren't defined until this header was
        // included to be part of the type signature of a builtin that
        // was known to clang already.
        //
        // The declarations use __attribute__(__clang_arm_builtin_alias),
        // so that each function declared will be recognized as the
        // appropriate MVE builtin in spite of its user-facing name.
        //
        // (That's better than making them all wrapper functions,
        // partly because it avoids any compiler error message citing
        // the wrapper function definition instead of the user's code,
        // and mostly because some MVE intrinsics have arguments
        // required to be compile-time constants, and that property
        // can't be propagated through a wrapper function. It can be
        // propagated through a macro, but macros can't be overloaded
        // on argument types very easily - you have to use _Generic,
        // which makes error messages very confusing when the user
        // gets it wrong.)
        //
        // Finally, the polymorphic versions of the intrinsics are
        // also defined with __attribute__(overloadable), so that when
        // the same name is defined with several type signatures, the
        // right thing happens. Each one of the overloaded
        // declarations is given a different builtin id, which
        // has exactly the effect we want: first clang resolves the
        // overload to the right function, then it knows which builtin
        // it's referring to, and then the Sema checking for that
        // builtin can check further things like the constant
        // arguments.
        //
        // One more subtlety is the newline just before the return
        // type name. That's a cosmetic tweak to make the error
        // messages legible if the user gets the types wrong in a call
        // to a polymorphic function: this way, clang will print just
        // the _final_ line of each declaration in the header, to show
        // the type signatures that would have been legal. So all the
        // confusing machinery with __attribute__ is left out of the
        // error message, and the user sees something that's more or
        // less self-documenting: "here's a list of actually readable
        // type signatures for vfooq(), and here's why each one didn't
        // match your call".

        OS << "static __inline__ __attribute__(("
           << (Polymorphic ? "__overloadable__, " : "")
           << "__clang_arm_builtin_alias(__builtin_arm_mve_" << Int.fullName()
           << ")))\n"
           << RetTypeName << FunctionName << "(" << ArgTypesString << ");\n";
      }
    }
  }
  for (auto &part : parts)
    part << "\n";

  // Now we've finished accumulating bits and pieces into the parts[] array.
  // Put it all together to write the final output file.

  OS << "/*===---- arm_mve.h - ARM MVE intrinsics "
        "-----------------------------------===\n"
     << LLVMLicenseHeader
     << "#ifndef __ARM_MVE_H\n"
        "#define __ARM_MVE_H\n"
        "\n"
        "#if !__ARM_FEATURE_MVE\n"
        "#error \"MVE support not enabled\"\n"
        "#endif\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n";

  for (size_t i = 0; i < NumParts; ++i) {
    std::vector<std::string> conditions;
    if (i & Float)
      conditions.push_back("(__ARM_FEATURE_MVE & 2)");
    if (i & UseUserNamespace)
      conditions.push_back("(!defined __ARM_MVE_PRESERVE_USER_NAMESPACE)");

    std::string condition =
        join(std::begin(conditions), std::end(conditions), " && ");
    if (!condition.empty())
      OS << "#if " << condition << "\n\n";
    OS << parts[i].str();
    if (!condition.empty())
      OS << "#endif /* " << condition << " */\n\n";
  }

  OS << "#ifdef __cplusplus\n"
        "} /* extern \"C\" */\n"
        "#endif\n"
        "\n"
        "#endif /* __ARM_MVE_H */\n";
}

void MveEmitter::EmitBuiltinDef(raw_ostream &OS) {
  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;
    OS << "BUILTIN(__builtin_arm_mve_" << Int.fullName()
       << ", \"\", \"n\")\n";
  }

  std::set<std::string> ShortNamesSeen;

  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;
    if (Int.polymorphic()) {
      StringRef Name = Int.shortName();
      if (ShortNamesSeen.find(std::string(Name)) == ShortNamesSeen.end()) {
        OS << "BUILTIN(__builtin_arm_mve_" << Name << ", \"vi.\", \"nt";
        if (Int.nonEvaluating())
          OS << "u"; // indicate that this builtin doesn't evaluate its args
        OS << "\")\n";
        ShortNamesSeen.insert(std::string(Name));
      }
    }
  }
}

void MveEmitter::EmitBuiltinSema(raw_ostream &OS) {
  std::map<std::string, std::set<std::string>> Checks;
  GroupSemaChecks(Checks);

  for (const auto &kv : Checks) {
    for (StringRef Name : kv.second)
      OS << "case ARM::BI__builtin_arm_mve_" << Name << ":\n";
    OS << "  return " << kv.first;
  }
}

// -----------------------------------------------------------------------------
// Class that describes an ACLE intrinsic implemented as a macro.
//
// This class is used when the intrinsic is polymorphic in 2 or 3 types, but we
// want to avoid a combinatorial explosion by reinterpreting the arguments to
// fixed types.

class FunctionMacro {
  std::vector<StringRef> Params;
  StringRef Definition;

public:
  FunctionMacro(const Record &R);

  const std::vector<StringRef> &getParams() const { return Params; }
  StringRef getDefinition() const { return Definition; }
};

FunctionMacro::FunctionMacro(const Record &R) {
  Params = R.getValueAsListOfStrings("params");
  Definition = R.getValueAsString("definition");
}

// -----------------------------------------------------------------------------
// The class used for generating arm_cde.h and related Clang bits
//

class CdeEmitter : public EmitterBase {
  std::map<StringRef, FunctionMacro> FunctionMacros;

public:
  CdeEmitter(RecordKeeper &Records);
  void EmitHeader(raw_ostream &OS) override;
  void EmitBuiltinDef(raw_ostream &OS) override;
  void EmitBuiltinSema(raw_ostream &OS) override;
};

CdeEmitter::CdeEmitter(RecordKeeper &Records) : EmitterBase(Records) {
  for (Record *R : Records.getAllDerivedDefinitions("FunctionMacro"))
    FunctionMacros.emplace(R->getName(), FunctionMacro(*R));
}

void CdeEmitter::EmitHeader(raw_ostream &OS) {
  // Accumulate pieces of the header file that will be enabled under various
  // different combinations of #ifdef. The index into parts[] is one of the
  // following:
  constexpr unsigned None = 0;
  constexpr unsigned MVE = 1;
  constexpr unsigned MVEFloat = 2;

  constexpr unsigned NumParts = 3;
  raw_self_contained_string_ostream parts[NumParts];

  // Write typedefs for all the required vector types, and a few scalar
  // types that don't already have the name we want them to have.

  parts[MVE] << "typedef uint16_t mve_pred16_t;\n";
  parts[MVEFloat] << "typedef __fp16 float16_t;\n"
                     "typedef float float32_t;\n";
  for (const auto &kv : ScalarTypes) {
    const ScalarType *ST = kv.second.get();
    if (ST->hasNonstandardName())
      continue;
    // We don't have float64x2_t
    if (ST->kind() == ScalarTypeKind::Float && ST->sizeInBits() == 64)
      continue;
    raw_ostream &OS = parts[ST->requiresFloat() ? MVEFloat : MVE];
    const VectorType *VT = getVectorType(ST);

    OS << "typedef __attribute__((__neon_vector_type__(" << VT->lanes()
       << "), __clang_arm_mve_strict_polymorphism)) " << ST->cName() << " "
       << VT->cName() << ";\n";
  }
  parts[MVE] << "\n";
  parts[MVEFloat] << "\n";

  // Write declarations for all the intrinsics.

  for (const auto &kv : ACLEIntrinsics) {
    const ACLEIntrinsic &Int = *kv.second;

    // We generate each intrinsic twice, under its full unambiguous
    // name and its shorter polymorphic name (if the latter exists).
    for (bool Polymorphic : {false, true}) {
      if (Polymorphic && !Int.polymorphic())
        continue;
      if (!Polymorphic && Int.polymorphicOnly())
        continue;

      raw_ostream &OS =
          parts[Int.requiresFloat() ? MVEFloat
                                    : Int.requiresMVE() ? MVE : None];

      // Make the name of the function in this declaration.
      std::string FunctionName =
          "__arm_" + (Polymorphic ? Int.shortName() : Int.fullName());

      // Make strings for the types involved in the function's
      // prototype.
      std::string RetTypeName = Int.returnType()->cName();
      if (!StringRef(RetTypeName).ends_with("*"))
        RetTypeName += " ";

      std::vector<std::string> ArgTypeNames;
      for (const Type *ArgTypePtr : Int.argTypes())
        ArgTypeNames.push_back(ArgTypePtr->cName());
      std::string ArgTypesString =
          join(std::begin(ArgTypeNames), std::end(ArgTypeNames), ", ");

      // Emit the actual declaration. See MveEmitter::EmitHeader for detailed
      // comments
      OS << "static __inline__ __attribute__(("
         << (Polymorphic ? "__overloadable__, " : "")
         << "__clang_arm_builtin_alias(__builtin_arm_" << Int.builtinExtension()
         << "_" << Int.fullName() << ")))\n"
         << RetTypeName << FunctionName << "(" << ArgTypesString << ");\n";
    }
  }

  for (const auto &kv : FunctionMacros) {
    StringRef Name = kv.first;
    const FunctionMacro &FM = kv.second;

    raw_ostream &OS = parts[MVE];
    OS << "#define "
       << "__arm_" << Name << "(" << join(FM.getParams(), ", ") << ") "
       << FM.getDefinition() << "\n";
  }

  for (auto &part : parts)
    part << "\n";

  // Now we've finished accumulating bits and pieces into the parts[] array.
  // Put it all together to write the final output file.

  OS << "/*===---- arm_cde.h - ARM CDE intrinsics "
        "-----------------------------------===\n"
     << LLVMLicenseHeader
     << "#ifndef __ARM_CDE_H\n"
        "#define __ARM_CDE_H\n"
        "\n"
        "#if !__ARM_FEATURE_CDE\n"
        "#error \"CDE support not enabled\"\n"
        "#endif\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n";

  for (size_t i = 0; i < NumParts; ++i) {
    std::string condition;
    if (i == MVEFloat)
      condition = "__ARM_FEATURE_MVE & 2";
    else if (i == MVE)
      condition = "__ARM_FEATURE_MVE";

    if (!condition.empty())
      OS << "#if " << condition << "\n\n";
    OS << parts[i].str();
    if (!condition.empty())
      OS << "#endif /* " << condition << " */\n\n";
  }

  OS << "#ifdef __cplusplus\n"
        "} /* extern \"C\" */\n"
        "#endif\n"
        "\n"
        "#endif /* __ARM_CDE_H */\n";
}

void CdeEmitter::EmitBuiltinDef(raw_ostream &OS) {
  for (const auto &kv : ACLEIntrinsics) {
    if (kv.second->headerOnly())
      continue;
    const ACLEIntrinsic &Int = *kv.second;
    OS << "BUILTIN(__builtin_arm_cde_" << Int.fullName()
       << ", \"\", \"ncU\")\n";
  }
}

void CdeEmitter::EmitBuiltinSema(raw_ostream &OS) {
  std::map<std::string, std::set<std::string>> Checks;
  GroupSemaChecks(Checks);

  for (const auto &kv : Checks) {
    for (StringRef Name : kv.second)
      OS << "case ARM::BI__builtin_arm_cde_" << Name << ":\n";
    OS << "  Err = " << kv.first << "  break;\n";
  }
}

} // namespace

namespace clang {

// MVE

void EmitMveHeader(RecordKeeper &Records, raw_ostream &OS) {
  MveEmitter(Records).EmitHeader(OS);
}

void EmitMveBuiltinDef(RecordKeeper &Records, raw_ostream &OS) {
  MveEmitter(Records).EmitBuiltinDef(OS);
}

void EmitMveBuiltinSema(RecordKeeper &Records, raw_ostream &OS) {
  MveEmitter(Records).EmitBuiltinSema(OS);
}

void EmitMveBuiltinCG(RecordKeeper &Records, raw_ostream &OS) {
  MveEmitter(Records).EmitBuiltinCG(OS);
}

void EmitMveBuiltinAliases(RecordKeeper &Records, raw_ostream &OS) {
  MveEmitter(Records).EmitBuiltinAliases(OS);
}

// CDE

void EmitCdeHeader(RecordKeeper &Records, raw_ostream &OS) {
  CdeEmitter(Records).EmitHeader(OS);
}

void EmitCdeBuiltinDef(RecordKeeper &Records, raw_ostream &OS) {
  CdeEmitter(Records).EmitBuiltinDef(OS);
}

void EmitCdeBuiltinSema(RecordKeeper &Records, raw_ostream &OS) {
  CdeEmitter(Records).EmitBuiltinSema(OS);
}

void EmitCdeBuiltinCG(RecordKeeper &Records, raw_ostream &OS) {
  CdeEmitter(Records).EmitBuiltinCG(OS);
}

void EmitCdeBuiltinAliases(RecordKeeper &Records, raw_ostream &OS) {
  CdeEmitter(Records).EmitBuiltinAliases(OS);
}

} // end namespace clang
