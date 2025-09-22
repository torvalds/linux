//===- TargetCallingConv.td - Target Calling Conventions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces with which targets
// describe their calling conventions.
//
//===----------------------------------------------------------------------===//

class CCAction;
class CallingConv;

/// CCCustom - Calls a custom arg handling function.
class CCCustom<string fn> : CCAction {
  string FuncName = fn;
}

/// CCPredicateAction - Instances of this class check some predicate, then
/// delegate to another action if the predicate is true.
class CCPredicateAction<CCAction A> : CCAction {
  CCAction SubAction = A;
}

/// CCIfType - If the current argument is one of the specified types, apply
/// Action A.
class CCIfType<list<ValueType> vts, CCAction A> : CCPredicateAction<A> {
  list<ValueType> VTs = vts;
}

/// CCIf - If the predicate matches, apply A.
class CCIf<string predicate, CCAction A> : CCPredicateAction<A> {
  string Predicate = predicate;
}

/// CCIfByVal - If the current argument has ByVal parameter attribute, apply
/// Action A.
class CCIfByVal<CCAction A> : CCIf<"ArgFlags.isByVal()", A> {
}

/// CCIfPreallocated - If the current argument has Preallocated parameter attribute,
/// apply Action A.
class CCIfPreallocated<CCAction A> : CCIf<"ArgFlags.isPreallocated()", A> {
}

/// CCIfSwiftSelf - If the current argument has swiftself parameter attribute,
/// apply Action A.
class CCIfSwiftSelf<CCAction A> : CCIf<"ArgFlags.isSwiftSelf()", A> {
}

/// CCIfSwiftAsync - If the current argument has swiftasync parameter attribute,
/// apply Action A.
class CCIfSwiftAsync<CCAction A> : CCIf<"ArgFlags.isSwiftAsync()", A> {
}

/// CCIfSwiftError - If the current argument has swifterror parameter attribute,
/// apply Action A.
class CCIfSwiftError<CCAction A> : CCIf<"ArgFlags.isSwiftError()", A> {
}

/// CCIfCFGuardTarget - If the current argument has cfguardtarget parameter
/// attribute, apply Action A.
class CCIfCFGuardTarget<CCAction A> : CCIf<"ArgFlags.isCFGuardTarget()", A> {
}

/// CCIfConsecutiveRegs - If the current argument has InConsecutiveRegs
/// parameter attribute, apply Action A.
class CCIfConsecutiveRegs<CCAction A> : CCIf<"ArgFlags.isInConsecutiveRegs()", A> {
}

/// CCIfCC - Match if the current calling convention is 'CC'.
class CCIfCC<string CC, CCAction A>
  : CCIf<!strconcat("State.getCallingConv() == ", CC), A> {}

/// CCIfInReg - If this argument is marked with the 'inreg' attribute, apply
/// the specified action.
class CCIfInReg<CCAction A> : CCIf<"ArgFlags.isInReg()", A> {}

/// CCIfNest - If this argument is marked with the 'nest' attribute, apply
/// the specified action.
class CCIfNest<CCAction A> : CCIf<"ArgFlags.isNest()", A> {}

/// CCIfSplit - If this argument is marked with the 'split' attribute, apply
/// the specified action.
class CCIfSplit<CCAction A> : CCIf<"ArgFlags.isSplit()", A> {}

/// CCIfSRet - If this argument is marked with the 'sret' attribute, apply
/// the specified action.
class CCIfSRet<CCAction A> : CCIf<"ArgFlags.isSRet()", A> {}

/// CCIfVarArg - If the current function is vararg - apply the action
class CCIfVarArg<CCAction A> : CCIf<"State.isVarArg()", A> {}

/// CCIfNotVarArg - If the current function is not vararg - apply the action
class CCIfNotVarArg<CCAction A> : CCIf<"!State.isVarArg()", A> {}

/// CCIfPtrAddrSpace - If the top-level parent of the current argument has
/// pointer type in the specified address-space.
class CCIfPtrAddrSpace<int AS, CCAction A>
    : CCIf<"(ArgFlags.isPointer() && ArgFlags.getPointerAddrSpace() == " # AS # ")", A> {}

/// CCIfPtr - If the top-level parent of the current argument had
/// pointer type in some address-space.
class CCIfPtr<CCAction A> : CCIf<"ArgFlags.isPointer()", A> {}

/// CCAssignToReg - This action matches if there is a register in the specified
/// list that is still available.  If so, it assigns the value to the first
/// available register and succeeds.
class CCAssignToReg<list<Register> regList> : CCAction {
  list<Register> RegList = regList;
}

/// CCAssignToRegWithShadow - Same as CCAssignToReg, but with list of registers
/// which became shadowed, when some register is used.
class CCAssignToRegWithShadow<list<Register> regList,
                              list<Register> shadowList> : CCAction {
  list<Register> RegList = regList;
  list<Register> ShadowRegList = shadowList;
}

/// CCAssignToStack - This action always matches: it assigns the value to a
/// stack slot of the specified size and alignment on the stack.  If size is
/// zero then the ABI size is used; if align is zero then the ABI alignment
/// is used - these may depend on the target or subtarget.
class CCAssignToStack<int size, int align> : CCAction {
  int Size = size;
  int Align = align;
}

/// CCAssignToStackWithShadow - Same as CCAssignToStack, but with a list of
/// registers to be shadowed. Note that, unlike CCAssignToRegWithShadow, this
/// shadows ALL of the registers in shadowList.
class CCAssignToStackWithShadow<int size,
                                int align,
                                list<Register> shadowList> : CCAction {
  int Size = size;
  int Align = align;
  list<Register> ShadowRegList = shadowList;
}

/// CCAssignToRegAndStack - Same as CCAssignToReg, but also allocates a stack
/// slot, when some register is used. Basically, it works like:
/// CCIf<CCAssignToReg<regList>, CCAssignToStack<size, align>>.
class CCAssignToRegAndStack<list<Register> regList, int size, int align>
    : CCAssignToReg<regList> {
  int Size = size;
  int Align = align;
}

/// CCPassByVal - This action always matches: it assigns the value to a stack
/// slot to implement ByVal aggregate parameter passing. Size and alignment
/// specify the minimum size and alignment for the stack slot.
class CCPassByVal<int size, int align> : CCAction {
  int Size = size;
  int Align = align;
}

/// CCPromoteToType - If applied, this promotes the specified current value to
/// the specified type.
class CCPromoteToType<ValueType destTy> : CCAction {
  ValueType DestTy = destTy;
}

/// CCPromoteToUpperBitsInType - If applied, this promotes the specified current
/// value to the specified type and shifts the value into the upper bits.
class CCPromoteToUpperBitsInType<ValueType destTy> : CCAction {
  ValueType DestTy = destTy;
}

/// CCBitConvertToType - If applied, this bitconverts the specified current
/// value to the specified type.
class CCBitConvertToType<ValueType destTy> : CCAction {
  ValueType DestTy = destTy;
}

/// CCTruncToType - If applied, this truncates the specified current value to
/// the specified type.
class CCTruncToType<ValueType destTy> : CCAction {
  ValueType DestTy = destTy;
}

/// CCPassIndirect - If applied, this stores the value to stack and passes the pointer
/// as normal argument.
class CCPassIndirect<ValueType destTy> : CCAction {
  ValueType DestTy = destTy;
}

/// CCDelegateTo - This action invokes the specified sub-calling-convention.  It
/// is successful if the specified CC matches.
class CCDelegateTo<CallingConv cc> : CCAction {
  CallingConv CC = cc;
}

/// CallingConv - An instance of this is used to define each calling convention
/// that the target supports.
class CallingConv<list<CCAction> actions> {
  list<CCAction> Actions = actions;

  /// If true, this calling convention will be emitted as externally visible in
  /// the llvm namespaces instead of as a static function.
  bit Entry = false;

  bit Custom = false;
}

/// CustomCallingConv - An instance of this is used to declare calling
/// conventions that are implemented using a custom function of the same name.
class CustomCallingConv : CallingConv<[]> {
  let Custom = true;
}

/// CalleeSavedRegs - A list of callee saved registers for a given calling
/// convention.  The order of registers is used by PrologEpilogInsertion when
/// allocation stack slots for saved registers.
///
/// For each CalleeSavedRegs def, TableGen will emit a FOO_SaveList array for
/// returning from getCalleeSavedRegs(), and a FOO_RegMask bit mask suitable for
/// returning from getCallPreservedMask().
class CalleeSavedRegs<dag saves> {
  dag SaveList = saves;

  // Registers that are also preserved across function calls, but should not be
  // included in the generated FOO_SaveList array. These registers will be
  // included in the FOO_RegMask bit mask. This can be used for registers that
  // are saved automatically, like the SPARC register windows.
  dag OtherPreserved;
}
