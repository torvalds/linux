//===--- arm_neon_incl.td - ARM NEON compiler interface -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines data structures shared by arm_neon.td and arm_fp16.td.
//  It constains base operation classes, operations, instructions, instruction
//  modifiers, etc.
//
//===----------------------------------------------------------------------===//
//
// Each intrinsic is a subclass of the Inst class. An intrinsic can either
// generate a __builtin_* call or it can expand to a set of generic operations.
//
// The operations are subclasses of Operation providing a list of DAGs, the
// last of which is the return value. The available DAG nodes are documented
// below.
//
//===----------------------------------------------------------------------===//

// The base Operation class. All operations must subclass this.
class Operation<list<dag> ops=[]> {
  list<dag> Ops = ops;
  bit Unavailable = 0;
}
// An operation that only contains a single DAG.
class Op<dag op> : Operation<[op]>;
// A shorter version of Operation - takes a list of DAGs. The last of these will
// be the return value.
class LOp<list<dag> ops> : Operation<ops>;

// These defs and classes are used internally to implement the SetTheory
// expansion and should be ignored.
foreach Index = 0-63 in
  def sv#Index;
class MaskExpand;

//===----------------------------------------------------------------------===//
// Available operations
//===----------------------------------------------------------------------===//

// DAG arguments can either be operations (documented below) or variables.
// Variables are prefixed with '$'. There are variables for each input argument,
// with the name $pN, where N starts at zero. So the zero'th argument will be
// $p0, the first $p1 etc.

// op - Binary or unary operator, depending on the number of arguments. The
//      operator itself is just treated as a raw string and is not checked.
// example: (op "+", $p0, $p1) -> "__p0 + __p1".
//          (op "-", $p0)      -> "-__p0"
def op;
// call - Invoke another intrinsic. The input types are type checked and
//        disambiguated. If there is no intrinsic defined that takes
//        the given types (or if there is a type ambiguity) an error is
//        generated at tblgen time. The name of the intrinsic is the raw
//        name as given to the Inst class (not mangled).
// example: (call "vget_high", $p0) -> "vgetq_high_s16(__p0)"
//            (assuming $p0 has type int16x8_t).
def call;
// call_mangled - Invoke another intrinsic matching the mangled name variation
//                of the caller's base type. If there is no intrinsic defined
//                that has the variation and takes the given types, an error
//                is generated at tblgen time.
// example: (call_mangled "vfma_lane", $p0, $p1) -> "vfma_lane(__p0, __p1)"
//            (assuming non-LaneQ caller)
//          (call_mangled "vfma_lane", $p0, $p1) -> "vfma_laneq(__p0, __p1)"
//            (assuming LaneQ caller)
def call_mangled;
// cast - Perform a cast to a different type. This gets emitted as a static
//        C-style cast. For a pure reinterpret cast (T x = *(T*)&y), use
//        "bitcast".
//
//        The syntax is (cast MOD* VAL). The last argument is the value to
//        cast, preceded by a sequence of type modifiers. The target type
//        starts off as the type of VAL, and is modified by MOD in sequence.
//        The available modifiers are:
//          - $X  - Take the type of parameter/variable X. For example:
//                  (cast $p0, $p1) would cast $p1 to the type of $p0.
//          - "R" - The type of the return type.
//          - A typedef string - A NEON or stdint.h type that is then parsed.
//                               for example: (cast "uint32x4_t", $p0).
//          - "U" - Make the type unsigned.
//          - "S" - Make the type signed.
//          - "H" - Halve the number of lanes in the type.
//          - "D" - Double the number of lanes in the type.
//          - "8" - Convert type to an equivalent vector of 8-bit signed
//                  integers.
//          - "32" - Convert type to an equivalent vector of 32-bit integers.
// example: (cast "R", "U", $p0) -> "(uint32x4_t)__p0" (assuming the return
//           value is of type "int32x4_t".
//          (cast $p0, "D", "8", $p1) -> "(int8x16_t)__p1" (assuming __p0
//           has type float64x1_t or any other vector type of 64 bits).
//          (cast "int32_t", $p2) -> "(int32_t)__p2"
def cast;
// bitcast - Same as "cast", except a reinterpret-cast is produced:
//             (bitcast "T", $p0) -> "*(T*)&__p0".
//           The VAL argument is saved to a temporary so it can be used
//           as an l-value.
def bitcast;
// dup - Take a scalar argument and create a vector by duplicating it into
//       all lanes. The type of the vector is the base type of the intrinsic.
// example: (dup $p1) -> "(uint32x2_t) {__p1, __p1}" (assuming the base type
//          is uint32x2_t).
def dup;
// dup_typed - Take a vector and a scalar argument, and create a new vector of
//             the same type by duplicating the scalar value into all lanes.
// example: (dup_typed $p1, $p2) -> "(float16x4_t) {__p2, __p2, __p2, __p2}"
//          (assuming __p1 is float16x4_t, and __p2 is a compatible scalar).
def dup_typed;
// save_temp - Create a temporary (local) variable. The variable takes a name
//             based on the zero'th parameter and can be referenced using
//             using that name in subsequent DAGs in the same
//             operation. The scope of a temp is the operation. If a variable
//             with the given name already exists, an error will be given at
//             tblgen time.
// example: [(save_temp $var, (call "foo", $p0)),
//           (op "+", $var, $p1)] ->
//              "int32x2_t __var = foo(__p0); return __var + __p1;"
def save_temp;
// name_replace - Return the name of the current intrinsic with the first
//                argument replaced by the second argument. Raises an error if
//                the first argument does not exist in the intrinsic name.
// example: (call (name_replace "_high_", "_"), $p0) (to call the non-high
//            version of this intrinsic).
def name_replace;
// literal - Create a literal piece of code. The code is treated as a raw
//           string, and must be given a type. The type is a stdint.h or
//           NEON intrinsic type as given to (cast).
// example: (literal "int32_t", "0")
def literal;
// shuffle - Create a vector shuffle. The syntax is (shuffle ARG0, ARG1, MASK).
//           The MASK argument is a set of elements. The elements are generated
//           from the two special defs "mask0" and "mask1". "mask0" expands to
//           the lane indices in sequence for ARG0, and "mask1" expands to
//           the lane indices in sequence for ARG1. They can be used as-is, e.g.
//
//             (shuffle $p0, $p1, mask0) -> $p0
//             (shuffle $p0, $p1, mask1) -> $p1
//
//           or, more usefully, they can be manipulated using the SetTheory
//           operators plus some extra operators defined in the NEON emitter.
//           The operators are described below.
// example: (shuffle $p0, $p1, (add (highhalf mask0), (highhalf mask1))) ->
//            A concatenation of the high halves of the input vectors.
def shuffle;

// add, interleave, decimate: These set operators are vanilla SetTheory
// operators and take their normal definition.
def add;
def interleave;
def decimate;
// rotl - Rotate set left by a number of elements.
// example: (rotl mask0, 3) -> [3, 4, 5, 6, 0, 1, 2]
def rotl;
// rotl - Rotate set right by a number of elements.
// example: (rotr mask0, 3) -> [4, 5, 6, 0, 1, 2, 3]
def rotr;
// highhalf - Take only the high half of the input.
// example: (highhalf mask0) -> [4, 5, 6, 7] (assuming mask0 had 8 elements)
def highhalf;
// highhalf - Take only the low half of the input.
// example: (lowhalf mask0) -> [0, 1, 2, 3] (assuming mask0 had 8 elements)
def lowhalf;
// rev - Perform a variable-width reversal of the elements. The zero'th argument
//       is a width in bits to reverse. The lanes this maps to is determined
//       based on the element width of the underlying type.
// example: (rev 32, mask0) -> [3, 2, 1, 0, 7, 6, 5, 4] (if 8-bit elements)
// example: (rev 32, mask0) -> [1, 0, 3, 2]             (if 16-bit elements)
def rev;
// mask0 - The initial sequence of lanes for shuffle ARG0
def mask0 : MaskExpand;
// mask0 - The initial sequence of lanes for shuffle ARG1
def mask1 : MaskExpand;

def OP_NONE  : Operation;
def OP_UNAVAILABLE : Operation {
  let Unavailable = 1;
}

//===----------------------------------------------------------------------===//
// Instruction definitions
//===----------------------------------------------------------------------===//

// Every intrinsic subclasses "Inst". An intrinsic has a name, a prototype and
// a sequence of typespecs.
//
// The name is the base name of the intrinsic, for example "vget_lane". This is
// then mangled by the tblgen backend to add type information ("vget_lane_s16").
//
// A typespec is a sequence of uppercase characters (modifiers) followed by one
// lowercase character. A typespec encodes a particular "base type" of the
// intrinsic.
//
// An example typespec is "Qs" - quad-size short - uint16x8_t. The available
// typespec codes are given below.
//
// The string given to an Inst class is a sequence of typespecs. The intrinsic
// is instantiated for every typespec in the sequence. For example "sdQsQd".
//
// The prototype is a string that defines the return type of the intrinsic
// and the type of each argument. The return type and every argument gets a
// set of "modifiers" that can change in some way the "base type" of the
// intrinsic.
//
// Typespecs
// ---------
// c: char
// s: short
// i: int
// l: long
// k: 128-bit long
// f: float
// h: half-float
// d: double
// b: bfloat16
//
// Typespec modifiers
// ------------------
// S: scalar, only used for function mangling.
// U: unsigned
// Q: 128b
// H: 128b without mangling 'q'
// P: polynomial
//
// Prototype modifiers
// -------------------
// prototype: return (arg, arg, ...)
//
// Each type modifier is either a single character, or a group surrounded by
// parentheses.
//
// .: default
// v: change to void category.
// S: change to signed integer category.
// U: change to unsigned integer category.
// F: change to floating category.
// B: change to BFloat16
// P: change to polynomial category.
// p: change polynomial to equivalent integer category. Otherwise nop.
//
// >: double element width (vector size unchanged).
// <: half element width (vector size unchanged).
//
// 1: change to scalar.
// 2: change to struct of two vectors.
// 3: change to struct of three vectors.
// 4: change to struct of four vectors.
//
// *: make a pointer argument.
// c: make a constant argument (for pointers).
//
// Q: force 128-bit width.
// q: force 64-bit width.
//
// I: make 32-bit signed scalar immediate
// !: make this the key type passed to CGBuiltin.cpp in a polymorphic call.


// Every intrinsic subclasses Inst.
class Inst <string n, string p, string t, Operation o> {
  string Name = n;
  string Prototype = p;
  string Types = t;
  string ArchGuard = "";
  string TargetGuard = "neon";

  Operation Operation = o;
  bit BigEndianSafe = 0;
  bit isShift = 0;
  bit isScalarShift = 0;
  bit isScalarNarrowShift = 0;
  bit isVCVT_N = 0;
  bit isVXAR = 0;
  // For immediate checks: the immediate will be assumed to specify the lane of
  // a Q register. Only used for intrinsics which end up calling polymorphic
  // builtins.
  bit isLaneQ = 0;

  // Certain intrinsics have different names than their representative
  // instructions. This field allows us to handle this correctly when we
  // are generating tests.
  string InstName = "";

  // Certain intrinsics even though they are not a WOpInst or LOpInst,
  // generate a WOpInst/LOpInst instruction (see below for definition
  // of a WOpInst/LOpInst). For testing purposes we need to know
  // this. Ex: vset_lane which outputs vmov instructions.
  bit isHiddenWInst = 0;
  bit isHiddenLInst = 0;

  string CartesianProductWith = "";
}

// The following instruction classes are implemented via builtins.
// These declarations are used to generate Builtins.def:
//
// SInst: Instruction with signed/unsigned suffix (e.g., "s8", "u8", "p8")
// IInst: Instruction with generic integer suffix (e.g., "i8")
// WInst: Instruction with only bit size suffix (e.g., "8")
class SInst<string n, string p, string t> : Inst<n, p, t, OP_NONE> {}
class IInst<string n, string p, string t> : Inst<n, p, t, OP_NONE> {}
class WInst<string n, string p, string t> : Inst<n, p, t, OP_NONE> {}

// The following instruction classes are implemented via operators
// instead of builtins. As such these declarations are only used for
// the purpose of generating tests.
//
// SOpInst:       Instruction with signed/unsigned suffix (e.g., "s8",
//                "u8", "p8").
// IOpInst:       Instruction with generic integer suffix (e.g., "i8").
// WOpInst:       Instruction with bit size only suffix (e.g., "8").
// LOpInst:       Logical instruction with no bit size suffix.
// NoTestOpInst:  Intrinsic that has no corresponding instruction.
class SOpInst<string n, string p, string t, Operation o> : Inst<n, p, t, o> {}
class IOpInst<string n, string p, string t, Operation o> : Inst<n, p, t, o> {}
class WOpInst<string n, string p, string t, Operation o> : Inst<n, p, t, o> {}
class LOpInst<string n, string p, string t, Operation o> : Inst<n, p, t, o> {}
class NoTestOpInst<string n, string p, string t, Operation o> : Inst<n, p, t, o> {}
