//===--- Checkers.td - Static Analyzer Checkers -===-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "CheckerBase.td"

//===----------------------------------------------------------------------===//
// Packages.
//===----------------------------------------------------------------------===//

// The Alpha package is for checkers that have too many false positives to be
// turned on by default. The hierarchy under Alpha should be organized in the
// hierarchy checkers would have had if they were truly at the top level.
// (For example, a Cocoa-specific checker that is alpha should be in
// alpha.osx.cocoa).
def Alpha : Package<"alpha">;

def Core : Package<"core">;
def CoreBuiltin : Package<"builtin">, ParentPackage<Core>, Hidden;
def CoreUninitialized  : Package<"uninitialized">, ParentPackage<Core>;
def CoreAlpha : Package<"core">, ParentPackage<Alpha>;

// The OptIn package is for checkers that are not alpha and that would normally
// be on by default but where the driver does not have enough information to
// determine when they are applicable. For example, localizability checkers fit
// this criterion because the driver cannot determine whether a project is
// localized or not -- this is best determined at the IDE or build-system level.
//
// The checker hierarchy under OptIn should mirror that in Alpha: checkers
// should be organized as if they were at the top level.
//
// Note: OptIn is *not* intended for checkers that are too noisy to be on by
// default. Such checkers belong in the alpha package.
def OptIn : Package<"optin">;

def CoreOptIn : Package<"core">, ParentPackage<OptIn>;

// In the Portability package reside checkers for finding code that relies on
// implementation-defined behavior. Such checks are wanted for cross-platform
// development, but unwanted for developers who target only a single platform.
def PortabilityOptIn : Package<"portability">, ParentPackage<OptIn>;

// Optional checkers related to taint security analysis.
def TaintOptIn : Package<"taint">, ParentPackage<OptIn>;

def Nullability : Package<"nullability">,
  PackageOptions<[
    CmdLineOption<Boolean,
                  "NoDiagnoseCallsToSystemHeaders",
                  "Suppresses warnings for violating nullability annotations "
                  "of system header functions. This is useful if you are "
                  "concerned with your custom nullability annotations more "
                  "than with following nullability specifications of system "
                  "header functions.",
                  "false",
                  Released>
  ]>;

def Cplusplus : Package<"cplusplus">;
def CplusplusAlpha : Package<"cplusplus">, ParentPackage<Alpha>;
def CplusplusOptIn : Package<"cplusplus">, ParentPackage<OptIn>;

def Valist : Package<"valist">;

def DeadCode : Package<"deadcode">;
def DeadCodeAlpha : Package<"deadcode">, ParentPackage<Alpha>;

def Performance : Package<"performance">, ParentPackage<OptIn>;

def Security : Package <"security">;
def InsecureAPI : Package<"insecureAPI">, ParentPackage<Security>;
def SecurityAlpha : Package<"security">, ParentPackage<Alpha>;
def Taint : Package<"taint">, ParentPackage<SecurityAlpha>;

def CERT : Package<"cert">, ParentPackage<Security>;
def ENV : Package<"env">, ParentPackage<CERT>;

def CERTAlpha : Package<"cert">, ParentPackage<SecurityAlpha>;
def POSAlpha : Package<"pos">, ParentPackage<CERTAlpha>;

def Unix : Package<"unix">;
def UnixAlpha : Package<"unix">, ParentPackage<Alpha>;
def CString : Package<"cstring">, ParentPackage<Unix>;
def CStringAlpha : Package<"cstring">, ParentPackage<UnixAlpha>;

def OSX : Package<"osx">;
def OSXAlpha : Package<"osx">, ParentPackage<Alpha>;
def OSXOptIn : Package<"osx">, ParentPackage<OptIn>;

def Cocoa : Package<"cocoa">, ParentPackage<OSX>;
def CocoaAlpha : Package<"cocoa">, ParentPackage<OSXAlpha>;
def CocoaOptIn : Package<"cocoa">, ParentPackage<OSXOptIn>;

def CoreFoundation : Package<"coreFoundation">, ParentPackage<OSX>;
def Containers : Package<"containers">, ParentPackage<CoreFoundation>;

def LocalizabilityAlpha : Package<"localizability">, ParentPackage<CocoaAlpha>;
def LocalizabilityOptIn : Package<"localizability">, ParentPackage<CocoaOptIn>;

def MPI : Package<"mpi">, ParentPackage<OptIn>;

def LLVM : Package<"llvm">;
def LLVMAlpha : Package<"llvm">, ParentPackage<Alpha>;

// The APIModeling package is for checkers that model APIs and don't perform
// any diagnostics. These checkers are always turned on; this package is
// intended for API modeling that is not controlled by the target triple.
def APIModeling : Package<"apiModeling">, Hidden;
def APIModelingAlpha : Package<"apiModeling">, ParentPackage<Alpha>, Hidden;

def GoogleAPIModeling : Package<"google">, ParentPackage<APIModeling>, Hidden;
def LLVMAPIModeling : Package<"llvm">, ParentPackage<APIModeling>, Hidden;

def Debug : Package<"debug">, Hidden;

def CloneDetectionAlpha : Package<"clone">, ParentPackage<Alpha>;

def NonDeterminismAlpha : Package<"nondeterminism">, ParentPackage<Alpha>;

def Fuchsia : Package<"fuchsia">;
def FuchsiaAlpha : Package<"fuchsia">, ParentPackage<Alpha>;

def WebKit : Package<"webkit">;
def WebKitAlpha : Package<"webkit">, ParentPackage<Alpha>;

//===----------------------------------------------------------------------===//
// Core Checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Core in {

def BitwiseShiftChecker : Checker<"BitwiseShift">,
  HelpText<"Finds cases where bitwise shift operation causes undefined behaviour.">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "Pedantic",
                  "If set to true, the checker reports undefined behavior even "
                  "if it is supported by most compilers. (This flag has no "
                  "effect in C++20 where these constructs are legal.)",
                  "false",
                  Released>,
  ]>,
  Documentation<HasDocumentation>;

def CallAndMessageModeling : Checker<"CallAndMessageModeling">,
  HelpText<"Responsible for essential modeling and assumptions after a "
           "function/method call. For instance, if we can't reason about the "
           "nullability of the implicit this parameter after a method call, "
           "this checker conservatively assumes it to be non-null">,
  Documentation<HasDocumentation>,
  Hidden;

def CallAndMessageChecker : Checker<"CallAndMessage">,
  HelpText<"Check for logical errors for function calls and Objective-C "
           "message expressions (e.g., uninitialized arguments, null function "
           "pointers)">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "FunctionPointer",
                  "Check whether a called function pointer is null or "
                  "undefined",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "ParameterCount",
                  "Check whether a function was called with the appropriate "
                  "number of arguments",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "CXXThisMethodCall",
                  "Check whether the implicit this parameter is null or "
                  "undefined upon a method call",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "CXXDeallocationArg",
                  "Check whether the argument of operator delete is undefined",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "ArgInitializedness",
                  "Check whether any of the pass-by-value parameters is "
                  "undefined",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "ArgPointeeInitializedness",
                  "Check whether the pointee of a pass-by-reference or "
                  "pass-by-pointer is undefined",
                  "false",
                  InAlpha>,
    CmdLineOption<Boolean,
                  "NilReceiver",
                  "Check whether the reciever in the message expression is nil",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "UndefReceiver",
                  "Check whether the reciever in the message expression is "
                  "undefined",
                  "true",
                  Released>,
  ]>,
  Documentation<HasDocumentation>,
  Dependencies<[CallAndMessageModeling]>;

def DereferenceChecker : Checker<"NullDereference">,
  HelpText<"Check for dereferences of null pointers">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "SuppressAddressSpaces",
                  "Suppresses warning when pointer dereferences an address space",
                  "true",
                  Released>
  ]>,
  Documentation<HasDocumentation>;

def NonNullParamChecker : Checker<"NonNullParamChecker">,
  HelpText<"Check for null pointers passed as arguments to a function whose "
           "arguments are references or marked with the 'nonnull' attribute">,
  Documentation<HasDocumentation>;

def VLASizeChecker : Checker<"VLASize">,
  HelpText<"Check for declarations of VLA of undefined or zero size">,
  Documentation<HasDocumentation>;

def DivZeroChecker : Checker<"DivideZero">,
  HelpText<"Check for division by zero">,
  Documentation<HasDocumentation>;

def UndefResultChecker : Checker<"UndefinedBinaryOperatorResult">,
  HelpText<"Check for undefined results of binary operators">,
  Documentation<HasDocumentation>;

def StackAddrEscapeBase : Checker<"StackAddrEscapeBase">,
  HelpText<"Generate information about stack address escapes.">,
  Documentation<NotDocumented>,
  Hidden;

def StackAddrEscapeChecker : Checker<"StackAddressEscape">,
  HelpText<"Check that addresses to stack memory do not escape the function">,
  Dependencies<[StackAddrEscapeBase]>,
  Documentation<HasDocumentation>;

def DynamicTypePropagation : Checker<"DynamicTypePropagation">,
  HelpText<"Generate dynamic type information">,
  Documentation<NotDocumented>,
  Hidden;

def NonnullGlobalConstantsChecker: Checker<"NonnilStringConstants">,
  HelpText<"Assume that const string-like globals are non-null">,
  Documentation<NotDocumented>,
  Hidden;

} // end "core"

let ParentPackage = CoreAlpha in {

def BoolAssignmentChecker : Checker<"BoolAssignment">,
  HelpText<"Warn about assigning non-{0,1} values to Boolean variables">,
  Documentation<HasDocumentation>;

def CastSizeChecker : Checker<"CastSize">,
  HelpText<"Check when casting a malloc'ed type T, whether the size is a "
           "multiple of the size of T">,
  Documentation<HasDocumentation>;

def CastToStructChecker : Checker<"CastToStruct">,
  HelpText<"Check for cast from non-struct pointer to struct pointer">,
  Documentation<HasDocumentation>;

def ConversionChecker : Checker<"Conversion">,
  HelpText<"Loss of sign/precision in implicit conversions">,
  Documentation<HasDocumentation>;

def IdenticalExprChecker : Checker<"IdenticalExpr">,
  HelpText<"Warn about unintended use of identical expressions in operators">,
  Documentation<HasDocumentation>;

def FixedAddressChecker : Checker<"FixedAddr">,
  HelpText<"Check for assignment of a fixed address to a pointer">,
  Documentation<HasDocumentation>;

def PointerArithChecker : Checker<"PointerArithm">,
  HelpText<"Check for pointer arithmetic on locations other than array "
           "elements">,
  Documentation<HasDocumentation>;

def PointerSubChecker : Checker<"PointerSub">,
  HelpText<"Check for pointer subtractions on two pointers pointing to "
           "different memory chunks">,
  Documentation<HasDocumentation>;

def TestAfterDivZeroChecker : Checker<"TestAfterDivZero">,
  HelpText<"Check for division by variable that is later compared against 0. "
           "Either the comparison is useless or there is division by zero.">,
  Documentation<HasDocumentation>;

def DynamicTypeChecker : Checker<"DynamicTypeChecker">,
  HelpText<"Check for cases where the dynamic and the static type of an object "
           "are unrelated.">,
  Documentation<HasDocumentation>;

def StackAddrAsyncEscapeChecker : Checker<"StackAddressAsyncEscape">,
  HelpText<"Check that addresses to stack memory do not escape the function">,
  Dependencies<[StackAddrEscapeBase]>,
  Documentation<HasDocumentation>;

def PthreadLockBase : Checker<"PthreadLockBase">,
  HelpText<"Helper registering multiple checks.">,
  Documentation<NotDocumented>,
  Hidden;

def C11LockChecker : Checker<"C11Lock">,
  HelpText<"Simple lock -> unlock checker">,
  Dependencies<[PthreadLockBase]>,
  Documentation<HasDocumentation>;

def StdVariantChecker : Checker<"StdVariant">,
  HelpText<"Check for bad type access for std::variant.">,
  Documentation<HasDocumentation>;

} // end "alpha.core"

//===----------------------------------------------------------------------===//
// Nullability checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Nullability in {

def NullabilityBase : Checker<"NullabilityBase">,
  HelpText<"Stores information during the analysis about nullability.">,
  Documentation<NotDocumented>,
  Hidden;

def NullPassedToNonnullChecker : Checker<"NullPassedToNonnull">,
  HelpText<"Warns when a null pointer is passed to a pointer which has a "
           "_Nonnull type.">,
  Dependencies<[NullabilityBase]>,
  Documentation<HasDocumentation>;

def NullReturnedFromNonnullChecker : Checker<"NullReturnedFromNonnull">,
  HelpText<"Warns when a null pointer is returned from a function that has "
           "_Nonnull return type.">,
  Dependencies<[NullabilityBase]>,
  Documentation<HasDocumentation>;

def NullableDereferencedChecker : Checker<"NullableDereferenced">,
  HelpText<"Warns when a nullable pointer is dereferenced.">,
  Dependencies<[NullabilityBase]>,
  Documentation<HasDocumentation>;

def NullablePassedToNonnullChecker : Checker<"NullablePassedToNonnull">,
  HelpText<"Warns when a nullable pointer is passed to a pointer which has a "
           "_Nonnull type.">,
  Dependencies<[NullabilityBase]>,
  Documentation<HasDocumentation>;

def NullableReturnedFromNonnullChecker : Checker<"NullableReturnedFromNonnull">,
  HelpText<"Warns when a nullable pointer is returned from a function that has "
           "_Nonnull return type.">,
  Dependencies<[NullabilityBase]>,
  Documentation<NotDocumented>;

} // end "nullability"

//===----------------------------------------------------------------------===//
// APIModeling.
//===----------------------------------------------------------------------===//

let ParentPackage = APIModeling in {

def ErrnoModeling : Checker<"Errno">,
  HelpText<"Make the special value 'errno' available to other checkers.">,
  Documentation<NotDocumented>;

def TrustNonnullChecker : Checker<"TrustNonnull">,
  HelpText<"Trust that returns from framework methods annotated with _Nonnull "
           "are not null">,
  Documentation<NotDocumented>;

def TrustReturnsNonnullChecker : Checker<"TrustReturnsNonnull">,
  HelpText<"Trust that returns from methods annotated with returns_nonnull "
           "are not null">,
  Documentation<NotDocumented>;

} // end "apiModeling"

//===----------------------------------------------------------------------===//
// Evaluate "builtin" functions.
//===----------------------------------------------------------------------===//

let ParentPackage = CoreBuiltin in {

def NoReturnFunctionChecker : Checker<"NoReturnFunctions">,
  HelpText<"Evaluate \"panic\" functions that are known to not return to the "
           "caller">,
  Documentation<NotDocumented>;

def BuiltinFunctionChecker : Checker<"BuiltinFunctions">,
  HelpText<"Evaluate compiler builtin functions (e.g., alloca())">,
  Documentation<NotDocumented>;

} // end "core.builtin"

//===----------------------------------------------------------------------===//
// Uninitialized values checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = CoreUninitialized in {

def UndefinedArraySubscriptChecker : Checker<"ArraySubscript">,
  HelpText<"Check for uninitialized values used as array subscripts">,
  Documentation<HasDocumentation>;

def UndefinedAssignmentChecker : Checker<"Assign">,
  HelpText<"Check for assigning uninitialized values">,
  Documentation<HasDocumentation>;

def UndefBranchChecker : Checker<"Branch">,
  HelpText<"Check for uninitialized values used as branch conditions">,
  Documentation<HasDocumentation>;

def UndefCapturedBlockVarChecker : Checker<"CapturedBlockVariable">,
  HelpText<"Check for blocks that capture uninitialized values">,
  Documentation<NotDocumented>;

def ReturnUndefChecker : Checker<"UndefReturn">,
  HelpText<"Check for uninitialized values being returned to the caller">,
  Documentation<HasDocumentation>;

def UndefinedNewArraySizeChecker : Checker<"NewArraySize">,
  HelpText<"Check if the size of the array in a new[] expression is undefined">,
  Documentation<HasDocumentation>;

} // end "core.uninitialized"

//===----------------------------------------------------------------------===//
// Optin checkers for core language features
//===----------------------------------------------------------------------===//

let ParentPackage = CoreOptIn in {

def EnumCastOutOfRangeChecker : Checker<"EnumCastOutOfRange">,
  HelpText<"Check integer to enumeration casts for out of range values">,
  Documentation<HasDocumentation>;

} // end "optin.core"

//===----------------------------------------------------------------------===//
// Unix API checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = CString in {

def CStringModeling : Checker<"CStringModeling">,
  HelpText<"The base of several CString related checkers. On it's own it emits "
           "no reports, but adds valuable information to the analysis when "
           "enabled.">,
  Documentation<NotDocumented>,
  Hidden;

def CStringNullArg : Checker<"NullArg">,
  HelpText<"Check for null pointers being passed as arguments to C string "
           "functions">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

def CStringSyntaxChecker : Checker<"BadSizeArg">,
  HelpText<"Check the size argument passed into C string functions for common "
           "erroneous patterns">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

} // end "unix.cstring"

let ParentPackage = CStringAlpha in {

def CStringOutOfBounds : Checker<"OutOfBounds">,
  HelpText<"Check for out-of-bounds access in string functions">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

def CStringBufferOverlap : Checker<"BufferOverlap">,
  HelpText<"Checks for overlap in two buffer arguments">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

def CStringNotNullTerm : Checker<"NotNullTerminated">,
  HelpText<"Check for arguments which are not null-terminating strings">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

def CStringUninitializedRead : Checker<"UninitializedRead">,
  HelpText<"Checks if the string manipulation function would read uninitialized bytes">,
  Dependencies<[CStringModeling]>,
  Documentation<HasDocumentation>;

} // end "alpha.unix.cstring"

let ParentPackage = Unix in {

def UnixAPIMisuseChecker : Checker<"API">,
  HelpText<"Check calls to various UNIX/Posix functions">,
  Documentation<HasDocumentation>;

def BlockInCriticalSectionChecker : Checker<"BlockInCriticalSection">,
  HelpText<"Check for calls to blocking functions inside a critical section">,
  Documentation<HasDocumentation>;

def DynamicMemoryModeling: Checker<"DynamicMemoryModeling">,
  HelpText<"The base of several malloc() related checkers. On it's own it "
           "emits no reports, but adds valuable information to the analysis "
           "when enabled.">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "Optimistic",
                  "If set to true, the checker assumes that all the "
                  "allocating and deallocating functions are annotated with "
                  "ownership_holds, ownership_takes and ownership_returns.",
                  "false",
                  InAlpha>,
    CmdLineOption<Boolean,
                  "AddNoOwnershipChangeNotes",
                  "Add an additional note to the bug report for leak-like "
                  "bugs. Dynamically allocated objects passed to functions "
                  "that neither deallocated it, or have taken responsibility "
                  "of the ownership are noted, similarly to "
                  "NoStoreFuncVisitor.",
                  "true",
                  Released,
                  Hide>
  ]>,
  Dependencies<[CStringModeling]>,
  Documentation<NotDocumented>,
  Hidden;

def ErrnoChecker : Checker<"Errno">,
  HelpText<"Check for improper use of 'errno'">,
  Dependencies<[ErrnoModeling]>,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "AllowErrnoReadOutsideConditionExpressions",
                  "Allow read of undefined value from errno outside of conditions",
                  "true",
                  InAlpha>,
  ]>,
  Documentation<HasDocumentation>;

def MallocChecker: Checker<"Malloc">,
  HelpText<"Check for memory leaks, double free, and use-after-free problems. "
           "Traces memory managed by malloc()/free().">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<HasDocumentation>;

def MallocSizeofChecker : Checker<"MallocSizeof">,
  HelpText<"Check for dubious malloc arguments involving sizeof">,
  Documentation<HasDocumentation>;

def MismatchedDeallocatorChecker : Checker<"MismatchedDeallocator">,
  HelpText<"Check for mismatched deallocators.">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<HasDocumentation>;

// This must appear before StdCLibraryFunctionsChecker because a dependency.
def StreamChecker : Checker<"Stream">,
  HelpText<"Check stream handling functions">,
  WeakDependencies<[NonNullParamChecker]>,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "Pedantic",
                  "If false, assume that stream operations which are often not "
                  "checked for error do not fail.",
                  "false",
                  InAlpha>
  ]>,
  Documentation<HasDocumentation>;

def StdCLibraryFunctionsChecker : Checker<"StdCLibraryFunctions">,
  HelpText<"Check for invalid arguments of C standard library functions, "
           "and apply relations between arguments and return value">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "DisplayLoadedSummaries",
                  "If set to true, the checker displays the found summaries "
                  "for the given translation unit.",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "ModelPOSIX",
                  "If set to true, the checker models additional functions "
                  "from the POSIX standard.",
                  "true",
                  InAlpha>
  ]>,
  WeakDependencies<[CallAndMessageChecker, NonNullParamChecker, StreamChecker]>,
  Documentation<HasDocumentation>;

def VforkChecker : Checker<"Vfork">,
  HelpText<"Check for proper usage of vfork">,
  Documentation<HasDocumentation>;

} // end "unix"

let ParentPackage = UnixAlpha in {

def ChrootChecker : Checker<"Chroot">,
  HelpText<"Check improper use of chroot">,
  Documentation<HasDocumentation>;

def PthreadLockChecker : Checker<"PthreadLock">,
  HelpText<"Simple lock -> unlock checker">,
  Dependencies<[PthreadLockBase]>,
  Documentation<HasDocumentation>;

def SimpleStreamChecker : Checker<"SimpleStream">,
  HelpText<"Check for misuses of stream APIs">,
  Documentation<HasDocumentation>;

} // end "alpha.unix"

//===----------------------------------------------------------------------===//
// C++ checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Cplusplus in {

def ArrayDeleteChecker : Checker<"ArrayDelete">,
  HelpText<"Reports destructions of arrays of polymorphic objects that are "
           "destructed as their base class.">,
  Documentation<HasDocumentation>;

def InnerPointerChecker : Checker<"InnerPointer">,
  HelpText<"Check for inner pointers of C++ containers used after "
           "re/deallocation">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<NotDocumented>;

def NewDeleteChecker : Checker<"NewDelete">,
  HelpText<"Check for double-free and use-after-free problems. Traces memory "
           "managed by new/delete.">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<HasDocumentation>;

def NewDeleteLeaksChecker : Checker<"NewDeleteLeaks">,
  HelpText<"Check for memory leaks. Traces memory managed by new/delete.">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<HasDocumentation>;

def PlacementNewChecker : Checker<"PlacementNew">,
  HelpText<"Check if default placement new is provided with pointers to "
           "sufficient storage capacity">,
  Dependencies<[DynamicMemoryModeling]>,
  Documentation<HasDocumentation>;

def CXXSelfAssignmentChecker : Checker<"SelfAssignment">,
  HelpText<"Checks C++ copy and move assignment operators for self assignment">,
  Documentation<NotDocumented>,
  Hidden;

def SmartPtrModeling: Checker<"SmartPtrModeling">,
  HelpText<"Model behavior of C++ smart pointers">,
  Documentation<NotDocumented>,
    CheckerOptions<[
    CmdLineOption<Boolean,
                  "ModelSmartPtrDereference",
                  "Enable modeling for SmartPtr null dereferences",
                  "false",
                  InAlpha,
                  Hide>,
  ]>,
  Hidden;

def StringChecker: Checker<"StringChecker">,
  HelpText<"Checks C++ std::string bugs">,
  Documentation<HasDocumentation>;

def MoveChecker: Checker<"Move">,
  HelpText<"Find use-after-move bugs in C++">,
  CheckerOptions<[
    CmdLineOption<String,
                  "WarnOn",
                  "With setting \"KnownsOnly\" warn only on objects with known "
                  "move semantics like smart pointers and other STL objects. "
                  "With setting \"KnownsAndLocals\" warn additionally on local "
                  "variables (or rvalue references). With setting \"All\" warn "
                  "on all variables (excluding global variables).",
                  "KnownsAndLocals",
                  Released>
  ]>,
  Documentation<HasDocumentation>;

def VirtualCallModeling : Checker<"VirtualCallModeling">,
  HelpText<"Auxiliary modeling for the virtual method call checkers">,
  Documentation<NotDocumented>,
  Hidden;

def PureVirtualCallChecker : Checker<"PureVirtualCall">,
  HelpText<"Check pure virtual function calls during construction/destruction">,
  Dependencies<[VirtualCallModeling]>,
  Documentation<HasDocumentation>;
} // end: "cplusplus"

let ParentPackage = CplusplusOptIn in {

def UninitializedObjectChecker: Checker<"UninitializedObject">,
  HelpText<"Reports uninitialized fields after object construction">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "Pedantic",
                  "If set to false, the checker won't emit warnings "
                  "for objects that don't have at least one initialized "
                  "field.",
                  "false",
                  Released>,
    CmdLineOption<Boolean,
                  "NotesAsWarnings",
                  "If set to true, the checker will emit a warning "
                  "for each uninitalized field, as opposed to emitting one "
                  "warning per constructor call, and listing the uninitialized "
                  "fields that belongs to it in notes.",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "CheckPointeeInitialization",
                  "If set to false, the checker will not analyze "
                  "the pointee of pointer/reference fields, and will only "
                  "check whether the object itself is initialized.",
                  "false",
                  InAlpha>,
    CmdLineOption<String,
                  "IgnoreRecordsWithField",
                  "If supplied, the checker will not analyze "
                  "structures that have a field with a name or type name that "
                  "matches the given pattern.",
                  "\"\"",
                  Released>,
    CmdLineOption<Boolean,
                  "IgnoreGuardedFields",
                  "If set to true, the checker will analyze _syntactically_ "
                  "whether the found uninitialized object is used without a "
                  "preceding assert call. Defaults to false.",
                  "false",
                  InAlpha>
  ]>,
  Documentation<HasDocumentation>;

def VirtualCallChecker : Checker<"VirtualCall">,
  HelpText<"Check virtual function calls during construction/destruction">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "ShowFixIts",
                  "Enable fix-it hints for this checker",
                  "false",
                  InAlpha>,
    CmdLineOption<Boolean,
                  "PureOnly",
                  "Disables the checker. Keeps cplusplus.PureVirtualCall "
                  "enabled. This option is only provided for backwards "
                  "compatibility.",
                  "false",
                  InAlpha>
  ]>,
  Dependencies<[VirtualCallModeling]>,
  Documentation<HasDocumentation>;

} // end: "optin.cplusplus"

let ParentPackage = CplusplusAlpha in {

def ContainerModeling : Checker<"ContainerModeling">,
  HelpText<"Models C++ containers">,
  Documentation<NotDocumented>,
  Hidden;

def DeleteWithNonVirtualDtorChecker : Checker<"DeleteWithNonVirtualDtor">,
  HelpText<"Reports destructions of polymorphic objects with a non-virtual "
           "destructor in their base class">,
  Documentation<HasDocumentation>;

def IteratorModeling : Checker<"IteratorModeling">,
  HelpText<"Models iterators of C++ containers">,
  Dependencies<[ContainerModeling]>,
  Documentation<NotDocumented>,
  Hidden;

def STLAlgorithmModeling : Checker<"STLAlgorithmModeling">,
  HelpText<"Models the algorithm library of the C++ STL.">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "AggressiveStdFindModeling",
                  "Enables exploration of the failure branch in std::find-like "
                  "functions.",
                  "false",
                  Released>
  ]>,
  Dependencies<[ContainerModeling]>,
  Documentation<NotDocumented>;

def InvalidatedIteratorChecker : Checker<"InvalidatedIterator">,
  HelpText<"Check for use of invalidated iterators">,
  Dependencies<[IteratorModeling]>,
  Documentation<HasDocumentation>;

def IteratorRangeChecker : Checker<"IteratorRange">,
  HelpText<"Check for iterators used outside their valid ranges">,
  Dependencies<[IteratorModeling]>,
  Documentation<HasDocumentation>;

def MismatchedIteratorChecker : Checker<"MismatchedIterator">,
  HelpText<"Check for use of iterators of different containers where iterators "
           "of the same container are expected">,
  Dependencies<[IteratorModeling]>,
  Documentation<HasDocumentation>;

def SmartPtrChecker: Checker<"SmartPtr">,
  HelpText<"Find the dereference of null SmrtPtr">,
  Dependencies<[SmartPtrModeling]>,
  Documentation<HasDocumentation>;

} // end: "alpha.cplusplus"


//===----------------------------------------------------------------------===//
// Valist checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Valist in {

def ValistBase : Checker<"ValistBase">,
  HelpText<"Gathers information about va_lists.">,
  Documentation<NotDocumented>,
  Hidden;

def UninitializedChecker : Checker<"Uninitialized">,
  HelpText<"Check for usages of uninitialized (or already released) va_lists.">,
  Dependencies<[ValistBase]>,
  Documentation<NotDocumented>;

def UnterminatedChecker : Checker<"Unterminated">,
  HelpText<"Check for va_lists which are not released by a va_end call.">,
  Dependencies<[ValistBase]>,
  Documentation<NotDocumented>;

def CopyToSelfChecker : Checker<"CopyToSelf">,
  HelpText<"Check for va_lists which are copied onto itself.">,
  Dependencies<[ValistBase]>,
  Documentation<NotDocumented>;

} // end : "valist"

//===----------------------------------------------------------------------===//
// Deadcode checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = DeadCode in {

def DeadStoresChecker : Checker<"DeadStores">,
  HelpText<"Check for values stored to variables that are never read "
           "afterwards">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "WarnForDeadNestedAssignments",
                  "Warns for deadstores in nested assignments."
                  "E.g.: if ((P = f())) where P is unused.",
                  "true",
                  Released>,
    CmdLineOption<Boolean,
                  "ShowFixIts",
                  "Enable fix-it hints for this checker",
                  "false",
                  InAlpha>
  ]>,
  Documentation<HasDocumentation>;

} // end DeadCode

let ParentPackage = DeadCodeAlpha in {

def UnreachableCodeChecker : Checker<"UnreachableCode">,
  HelpText<"Check unreachable code">,
  Documentation<HasDocumentation>;

} // end "alpha.deadcode"

//===----------------------------------------------------------------------===//
// Performance checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Performance in {

def PaddingChecker : Checker<"Padding">,
  HelpText<"Check for excessively padded structs.">,
  CheckerOptions<[
    CmdLineOption<Integer,
                  "AllowedPad",
                  "Reports are only generated if the excessive padding exceeds "
                  "'AllowedPad' in bytes.",
                  "24",
                  Released>
  ]>,
  Documentation<HasDocumentation>;

} // end: "padding"

//===----------------------------------------------------------------------===//
// Security checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = InsecureAPI in {

def SecuritySyntaxChecker : Checker<"SecuritySyntaxChecker">,
  HelpText<"Base of various security function related checkers">,
  Documentation<NotDocumented>,
  Hidden;

def bcmp : Checker<"bcmp">,
  HelpText<"Warn on uses of the 'bcmp' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def bcopy : Checker<"bcopy">,
  HelpText<"Warn on uses of the 'bcopy' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def bzero : Checker<"bzero">,
  HelpText<"Warn on uses of the 'bzero' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def gets : Checker<"gets">,
  HelpText<"Warn on uses of the 'gets' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def getpw : Checker<"getpw">,
  HelpText<"Warn on uses of the 'getpw' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def mktemp : Checker<"mktemp">,
  HelpText<"Warn on uses of the 'mktemp' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def mkstemp : Checker<"mkstemp">,
  HelpText<"Warn when 'mkstemp' is passed fewer than 6 X's in the format "
           "string">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def rand : Checker<"rand">,
  HelpText<"Warn on uses of the 'rand', 'random', and related functions">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def strcpy : Checker<"strcpy">,
  HelpText<"Warn on uses of the 'strcpy' and 'strcat' functions">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def vfork : Checker<"vfork">,
  HelpText<"Warn on uses of the 'vfork' function">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def UncheckedReturn : Checker<"UncheckedReturn">,
  HelpText<"Warn on uses of functions whose return values must be always "
           "checked">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def DeprecatedOrUnsafeBufferHandling :
  Checker<"DeprecatedOrUnsafeBufferHandling">,
  HelpText<"Warn on uses of unsecure or deprecated buffer manipulating "
           "functions">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def decodeValueOfObjCType : Checker<"decodeValueOfObjCType">,
  HelpText<"Warn on uses of the '-decodeValueOfObjCType:at:' method">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

} // end "security.insecureAPI"

let ParentPackage = Security in {

def FloatLoopCounter : Checker<"FloatLoopCounter">,
  HelpText<"Warn on using a floating point value as a loop counter (CERT: "
           "FLP30-C, FLP30-CPP)">,
  Dependencies<[SecuritySyntaxChecker]>,
  Documentation<HasDocumentation>;

def PutenvStackArray : Checker<"PutenvStackArray">,
  HelpText<"Finds calls to the function 'putenv' which pass a pointer to "
           "an automatic (stack-allocated) array as the argument.">,
  Documentation<HasDocumentation>;

def SetgidSetuidOrderChecker : Checker<"SetgidSetuidOrder">,
  HelpText<"Warn on possible reversed order of 'setgid(getgid()))' and "
           "'setuid(getuid())' (CERT: POS36-C)">,
  Documentation<HasDocumentation>;

} // end "security"

let ParentPackage = ENV in {

  def InvalidPtrChecker : Checker<"InvalidPtr">,
  HelpText<"Finds usages of possibly invalidated pointers">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "InvalidatingGetEnv",
                  "Regard getenv as an invalidating call (as per POSIX "
                  "standard), which can lead to false positives depending on "
                  "implementation.",
                  "false",
                  Released>,
  ]>,
  Documentation<HasDocumentation>;

} // end "security.cert.env"

let ParentPackage = SecurityAlpha in {

def ArrayBoundChecker : Checker<"ArrayBound">,
  HelpText<"Warn about buffer overflows (older checker)">,
  Documentation<HasDocumentation>;

def ArrayBoundCheckerV2 : Checker<"ArrayBoundV2">,
  HelpText<"Warn about buffer overflows (newer checker)">,
  Documentation<HasDocumentation>;

def MallocOverflowSecurityChecker : Checker<"MallocOverflow">,
  HelpText<"Check for overflows in the arguments to malloc()">,
  Documentation<HasDocumentation>;

def MmapWriteExecChecker : Checker<"MmapWriteExec">,
  HelpText<"Warn on mmap() calls that are both writable and executable">,
  CheckerOptions<[
    CmdLineOption<Integer,
                  "MmapProtExec",
                  "Specifies the value of PROT_EXEC",
                  "0x04",
                  Released>,
    CmdLineOption<Integer,
                  "MmapProtRead",
                  "Specifies the value of PROT_READ",
                  "0x01",
                  Released>
  ]>,
  Documentation<HasDocumentation>;

def ReturnPointerRangeChecker : Checker<"ReturnPtrRange">,
  HelpText<"Check for an out-of-bound pointer being returned to callers">,
  Documentation<HasDocumentation>;

} // end "alpha.security"

//===----------------------------------------------------------------------===//
// Taint checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Taint in {

def TaintPropagationChecker : Checker<"TaintPropagation">, // Modelling checker
  HelpText<"Generate taint information used by other checkers">,
  CheckerOptions<[
    CmdLineOption<String,
                  "Config",
                  "Specifies the name of the configuration file.",
                  "",
                  InAlpha>,
  ]>,
  Documentation<NotDocumented>,
  Hidden;

def GenericTaintChecker : Checker<"GenericTaint">,
  HelpText<"Reports potential injection vulnerabilities">,
  Dependencies<[TaintPropagationChecker]>,
  Documentation<HasDocumentation>;

} // end "alpha.security.taint"

//===----------------------------------------------------------------------===//
// Mac OS X, Cocoa, and Core Foundation checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Cocoa in {

def RetainCountBase : Checker<"RetainCountBase">,
  HelpText<"Common base of various retain count related checkers">,
  Documentation<NotDocumented>,
  Hidden;

} // end "osx.cocoa"

let ParentPackage = OSX in {

def NSOrCFErrorDerefChecker : Checker<"NSOrCFErrorDerefChecker">,
  HelpText<"Implementation checker for NSErrorChecker and CFErrorChecker">,
  Documentation<NotDocumented>,
  Hidden;

def NumberObjectConversionChecker : Checker<"NumberObjectConversion">,
  HelpText<"Check for erroneous conversions of objects representing numbers "
           "into numbers">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "Pedantic",
                  "Enables detection of more conversion patterns (which are "
                  "most likely more harmless, and therefore are more likely to "
                  "produce false positives).",
                  "false",
                  Released>
  ]>,
  Documentation<NotDocumented>;

def MacOSXAPIChecker : Checker<"API">,
  HelpText<"Check for proper uses of various Apple APIs">,
  Documentation<HasDocumentation>;

def MacOSKeychainAPIChecker : Checker<"SecKeychainAPI">,
  HelpText<"Check for proper uses of Secure Keychain APIs">,
  Documentation<HasDocumentation>;

def MIGChecker : Checker<"MIG">,
  HelpText<"Find violations of the Mach Interface Generator "
           "calling convention">,
  Documentation<NotDocumented>;

def ObjCPropertyChecker : Checker<"ObjCProperty">,
  HelpText<"Check for proper uses of Objective-C properties">,
  Documentation<NotDocumented>;

def OSObjectRetainCountChecker : Checker<"OSObjectRetainCount">,
  HelpText<"Check for leaks and improper reference count management for "
           "OSObject">,
  Dependencies<[RetainCountBase]>,
  Documentation<NotDocumented>;

} // end "osx"

let ParentPackage = Cocoa in {

def RunLoopAutoreleaseLeakChecker : Checker<"RunLoopAutoreleaseLeak">,
  HelpText<"Check for leaked memory in autorelease pools that will never be "
           "drained">,
  Documentation<NotDocumented>;

def ObjCAtSyncChecker : Checker<"AtSync">,
  HelpText<"Check for nil pointers used as mutexes for @synchronized">,
  Documentation<HasDocumentation>;

def NilArgChecker : Checker<"NilArg">,
  HelpText<"Check for prohibited nil arguments to ObjC method calls">,
  Documentation<HasDocumentation>;

def ClassReleaseChecker : Checker<"ClassRelease">,
  HelpText<"Check for sending 'retain', 'release', or 'autorelease' directly "
           "to a Class">,
  Documentation<HasDocumentation>;

def VariadicMethodTypeChecker : Checker<"VariadicMethodTypes">,
  HelpText<"Check for passing non-Objective-C types to variadic collection "
           "initialization methods that expect only Objective-C types">,
  Documentation<HasDocumentation>;

def NSAutoreleasePoolChecker : Checker<"NSAutoreleasePool">,
  HelpText<"Warn for suboptimal uses of NSAutoreleasePool in Objective-C GC "
           "mode">,
  Documentation<HasDocumentation>;

def ObjCMethSigsChecker : Checker<"IncompatibleMethodTypes">,
  HelpText<"Warn about Objective-C method signatures with type "
           "incompatibilities">,
  Documentation<HasDocumentation>;

def ObjCUnusedIvarsChecker : Checker<"UnusedIvars">,
  HelpText<"Warn about private ivars that are never used">,
  Documentation<HasDocumentation>;

def ObjCSelfInitChecker : Checker<"SelfInit">,
  HelpText<"Check that 'self' is properly initialized inside an initializer "
           "method">,
  Documentation<HasDocumentation>;

def ObjCLoopChecker : Checker<"Loops">,
  HelpText<"Improved modeling of loops using Cocoa collection types">,
  Documentation<NotDocumented>;

def ObjCNonNilReturnValueChecker : Checker<"NonNilReturnValue">,
  HelpText<"Model the APIs that are guaranteed to return a non-nil value">,
  Documentation<NotDocumented>;

def ObjCSuperCallChecker : Checker<"MissingSuperCall">,
  HelpText<"Warn about Objective-C methods that lack a necessary call to "
           "super">,
  Documentation<NotDocumented>;

def NSErrorChecker : Checker<"NSError">,
  HelpText<"Check usage of NSError** parameters">,
  Dependencies<[NSOrCFErrorDerefChecker]>,
  Documentation<HasDocumentation>;

def RetainCountChecker : Checker<"RetainCount">,
  HelpText<"Check for leaks and improper reference count management">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "TrackNSCFStartParam",
                  "Check not only that the code follows retain-release rules "
                  "with respect to objects it allocates or borrows from "
                  "elsewhere, but also that it fulfills its own retain count "
                  "specification with respect to objects that it receives as "
                  "arguments.",
                  "false",
                  Released>
  ]>,
  Dependencies<[RetainCountBase]>,
  Documentation<HasDocumentation>;

def ObjCGenericsChecker : Checker<"ObjCGenerics">,
  HelpText<"Check for type errors when using Objective-C generics">,
  Dependencies<[DynamicTypePropagation]>,
  Documentation<HasDocumentation>;

def ObjCDeallocChecker : Checker<"Dealloc">,
  HelpText<"Warn about Objective-C classes that lack a correct implementation "
           "of -dealloc">,
  Documentation<HasDocumentation>;

def ObjCSuperDeallocChecker : Checker<"SuperDealloc">,
  HelpText<"Warn about improper use of '[super dealloc]' in Objective-C">,
  Documentation<HasDocumentation>;

def AutoreleaseWriteChecker : Checker<"AutoreleaseWrite">,
  HelpText<"Warn about potentially crashing writes to autoreleasing objects "
           "from different autoreleasing pools in Objective-C">,
  Documentation<NotDocumented>;

} // end "osx.cocoa"

let ParentPackage = Performance in {

def GCDAntipattern : Checker<"GCDAntipattern">,
  HelpText<"Check for performance anti-patterns when using Grand Central "
           "Dispatch">,
  Documentation<NotDocumented>;
} // end "optin.performance"

let ParentPackage = OSXOptIn in {

def OSObjectCStyleCast : Checker<"OSObjectCStyleCast">,
  HelpText<"Checker for C-style casts of OSObjects">,
  Documentation<NotDocumented>;

} // end "optin.osx"

let ParentPackage = CocoaAlpha in {

def IvarInvalidationModeling : Checker<"IvarInvalidationModeling">,
  HelpText<"Gathers information for annotation driven invalidation checking "
           "for classes that contains a method annotated with "
           "'objc_instance_variable_invalidator'">,
  Documentation<NotDocumented>,
  Hidden;

def InstanceVariableInvalidation : Checker<"InstanceVariableInvalidation">,
  HelpText<"Check that the invalidatable instance variables are invalidated in "
           "the methods annotated with objc_instance_variable_invalidator">,
  Dependencies<[IvarInvalidationModeling]>,
  Documentation<HasDocumentation>;

def MissingInvalidationMethod : Checker<"MissingInvalidationMethod">,
  HelpText<"Check that the invalidation methods are present in classes that "
           "contain invalidatable instance variables">,
  Dependencies<[IvarInvalidationModeling]>,
  Documentation<HasDocumentation>;

def DirectIvarAssignment : Checker<"DirectIvarAssignment">,
  HelpText<"Check for direct assignments to instance variables">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "AnnotatedFunctions",
                  "Check for direct assignments to instance variables in the "
                  "methods annotated with "
                  "objc_no_direct_instance_variable_assignment",
                  "false",
                  InAlpha>
  ]>,
  Documentation<HasDocumentation>;

} // end "alpha.osx.cocoa"

let ParentPackage = CoreFoundation in {

def CFNumberChecker : Checker<"CFNumber">,
  HelpText<"Check for proper uses of CFNumber APIs">,
  Documentation<HasDocumentation>;

def CFRetainReleaseChecker : Checker<"CFRetainRelease">,
  HelpText<"Check for null arguments to CFRetain/CFRelease/CFMakeCollectable">,
  Documentation<HasDocumentation>;

def CFErrorChecker : Checker<"CFError">,
  HelpText<"Check usage of CFErrorRef* parameters">,
  Dependencies<[NSOrCFErrorDerefChecker]>,
  Documentation<HasDocumentation>;

} // end "osx.coreFoundation"

let ParentPackage = Containers in {

def ObjCContainersASTChecker : Checker<"PointerSizedValues">,
  HelpText<"Warns if 'CFArray', 'CFDictionary', 'CFSet' are created with "
           "non-pointer-size values">,
  Documentation<HasDocumentation>;

def ObjCContainersChecker : Checker<"OutOfBounds">,
  HelpText<"Checks for index out-of-bounds when using 'CFArray' API">,
  Documentation<HasDocumentation>;

} // end "osx.coreFoundation.containers"

let ParentPackage = LocalizabilityOptIn in {

def NonLocalizedStringChecker : Checker<"NonLocalizedStringChecker">,
  HelpText<"Warns about uses of non-localized NSStrings passed to UI methods "
           "expecting localized NSStrings">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "AggressiveReport",
                  "Marks a string being returned by any call as localized if "
                  "it is in LocStringFunctions (LSF) or the function is "
                  "annotated. Otherwise, we mark it as NonLocalized "
                  "(Aggressive) or NonLocalized only if it is not backed by a "
                  "SymRegion (Non-Aggressive), basically leaving only string "
                  "literals as NonLocalized.",
                  "false",
                  InAlpha,
                  Hide>
  ]>,
  Documentation<HasDocumentation>;

def EmptyLocalizationContextChecker :
  Checker<"EmptyLocalizationContextChecker">,
  HelpText<"Check that NSLocalizedString macros include a comment for context">,
  Documentation<HasDocumentation>;

} // end "optin.osx.cocoa.localizability"

let ParentPackage = LocalizabilityAlpha in {

def PluralMisuseChecker : Checker<"PluralMisuseChecker">,
  HelpText<"Warns against using one vs. many plural pattern in code when "
           "generating localized strings.">,
  Documentation<HasDocumentation>;

} // end "alpha.osx.cocoa.localizability"

let ParentPackage = MPI in {

def MPIChecker : Checker<"MPI-Checker">,
  HelpText<"Checks MPI code">,
  Documentation<HasDocumentation>;

} // end "optin.mpi"

//===----------------------------------------------------------------------===//
// Checkers for LLVM development.
//===----------------------------------------------------------------------===//

let ParentPackage = LLVMAlpha in {

def LLVMConventionsChecker : Checker<"Conventions">,
  HelpText<"Check code for LLVM codebase conventions">,
  Documentation<HasDocumentation>;

} // end "llvm"

let ParentPackage = LLVMAPIModeling in {

def CastValueChecker : Checker<"CastValue">,
  HelpText<"Model implementation of custom RTTIs">,
  Documentation<NotDocumented>;

def ReturnValueChecker : Checker<"ReturnValue">,
  HelpText<"Model certain Error() methods that always return true by convention">,
  Documentation<NotDocumented>;

} // end "apiModeling.llvm"

//===----------------------------------------------------------------------===//
// Checkers modeling Google APIs.
//===----------------------------------------------------------------------===//

let ParentPackage = GoogleAPIModeling in {

def GTestChecker : Checker<"GTest">,
  HelpText<"Model gtest assertion APIs">,
  Documentation<NotDocumented>;

} // end "apiModeling.google"

//===----------------------------------------------------------------------===//
// Debugging checkers (for analyzer development).
//===----------------------------------------------------------------------===//

let ParentPackage = Debug in {

def AnalysisOrderChecker : Checker<"AnalysisOrder">,
  HelpText<"Print callbacks that are called during analysis in order">,
  CheckerOptions<[
    CmdLineOption<Boolean,
                  "PreStmtCastExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtCastExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreStmtArraySubscriptExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtArraySubscriptExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreStmtCXXNewExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtCXXNewExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreStmtCXXDeleteExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtCXXDeleteExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreStmtCXXConstructExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtCXXConstructExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreStmtOffsetOfExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostStmtOffsetOfExpr",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "EvalCall",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PreCall",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PostCall",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "EndFunction",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "EndAnalysis",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "NewAllocator",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "Bind",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "LiveSymbols",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "RegionChanges",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "PointerEscape",
                  "",
                  "false",
                  Released,
                  Hide>,
    CmdLineOption<Boolean,
                  "*",
                  "Enables all callbacks.",
                  "false",
                  Released,
                  Hide>
  ]>,
  Documentation<NotDocumented>;

def DominatorsTreeDumper : Checker<"DumpDominators">,
  HelpText<"Print the dominance tree for a given CFG">,
  Documentation<NotDocumented>;

def PostDominatorsTreeDumper : Checker<"DumpPostDominators">,
  HelpText<"Print the post dominance tree for a given CFG">,
  Documentation<NotDocumented>;

def ControlDependencyTreeDumper : Checker<"DumpControlDependencies">,
  HelpText<"Print the post control dependency tree for a given CFG">,
  Documentation<NotDocumented>;

def LiveVariablesDumper : Checker<"DumpLiveVars">,
  HelpText<"Print results of live variable analysis">,
  Documentation<NotDocumented>;

def LiveExpressionsDumper : Checker<"DumpLiveExprs">,
  HelpText<"Print results of live expression analysis">,
  Documentation<NotDocumented>;

def CFGViewer : Checker<"ViewCFG">,
  HelpText<"View Control-Flow Graphs using GraphViz">,
  Documentation<NotDocumented>;

def CFGDumper : Checker<"DumpCFG">,
  HelpText<"Display Control-Flow Graphs">,
  Documentation<NotDocumented>;

def CallGraphViewer : Checker<"ViewCallGraph">,
  HelpText<"View Call Graph using GraphViz">,
  Documentation<NotDocumented>;

def CallGraphDumper : Checker<"DumpCallGraph">,
  HelpText<"Display Call Graph">,
  Documentation<NotDocumented>;

def ConfigDumper : Checker<"ConfigDumper">,
  HelpText<"Dump config table">,
  Documentation<NotDocumented>;

def TraversalDumper : Checker<"DumpTraversal">,
  HelpText<"Print branch conditions as they are traversed by the engine">,
  Documentation<NotDocumented>;

def CallDumper : Checker<"DumpCalls">,
  HelpText<"Print calls as they are traversed by the engine">,
  Documentation<NotDocumented>;

def AnalyzerStatsChecker : Checker<"Stats">,
  HelpText<"Emit warnings with analyzer statistics">,
  Documentation<NotDocumented>;

def TaintTesterChecker : Checker<"TaintTest">,
  HelpText<"Mark tainted symbols as such.">,
  Documentation<NotDocumented>;

// This checker *technically* depends on SteamChecker, but we don't allow
// dependency checkers to emit diagnostics, and a debug checker isn't worth
// the chore needed to create a modeling portion on its own. Since this checker
// is for development purposes only anyways, make sure that StreamChecker is
// also enabled, at least for the time being.
def StreamTesterChecker : Checker<"StreamTester">,
  HelpText<"Add test functions to StreamChecker for test and debugging "
           "purposes.">,
  WeakDependencies<[StreamChecker]>,
  Documentation<NotDocumented>;

def ErrnoTesterChecker : Checker<"ErrnoTest">,
  HelpText<"Check modeling aspects of 'errno'.">,
  Dependencies<[ErrnoModeling]>,
  Documentation<NotDocumented>;

def ExprInspectionChecker : Checker<"ExprInspection">,
  HelpText<"Check the analyzer's understanding of expressions">,
  Documentation<NotDocumented>;

def ExplodedGraphViewer : Checker<"ViewExplodedGraph">,
  HelpText<"View Exploded Graphs using GraphViz">,
  Documentation<NotDocumented>;

def ReportStmts : Checker<"ReportStmts">,
  HelpText<"Emits a warning for every statement.">,
  Documentation<NotDocumented>;

def DebugContainerModeling : Checker<"DebugContainerModeling">,
  HelpText<"Check the analyzer's understanding of C++ containers">,
  Dependencies<[ContainerModeling]>,
  Documentation<NotDocumented>;

def DebugIteratorModeling : Checker<"DebugIteratorModeling">,
  HelpText<"Check the analyzer's understanding of C++ iterators">,
  Dependencies<[DebugContainerModeling, IteratorModeling]>,
  Documentation<NotDocumented>;

def StdCLibraryFunctionsTesterChecker : Checker<"StdCLibraryFunctionsTester">,
  HelpText<"Add test functions to the summary map, so testing of individual "
           "summary constituents becomes possible.">,
  WeakDependencies<[StdCLibraryFunctionsChecker]>,
  Documentation<NotDocumented>;

def CheckerDocumentationChecker : Checker<"CheckerDocumentation">,
  HelpText<"Defines an empty checker callback for all possible handlers.">,
  Documentation<NotDocumented>;

} // end "debug"


//===----------------------------------------------------------------------===//
// Clone Detection
//===----------------------------------------------------------------------===//

let ParentPackage = CloneDetectionAlpha in {

def CloneChecker : Checker<"CloneChecker">,
  HelpText<"Reports similar pieces of code.">,
  CheckerOptions<[
    CmdLineOption<Integer,
                  "MinimumCloneComplexity",
                  "Ensures that every clone has at least the given complexity. "
                  "Complexity is here defined as the total amount of children "
                  "of a statement. This constraint assumes the first statement "
                  "in the group is representative for all other statements in "
                  "the group in terms of complexity.",
                  "50",
                  Released>,
    CmdLineOption<Boolean,
                  "ReportNormalClones",
                  "Report all clones, even less suspicious ones.",
                  "true",
                  Released>,
    CmdLineOption<String,
                  "IgnoredFilesPattern",
                  "If supplied, the checker wont analyze files with a filename "
                  "that matches the given pattern.",
                  "\"\"",
                  Released>
  ]>,
  Documentation<HasDocumentation>;

} // end "clone"

//===----------------------------------------------------------------------===//
// Portability checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = PortabilityOptIn in {

def UnixAPIPortabilityChecker : Checker<"UnixAPI">,
  HelpText<"Finds implementation-defined behavior in UNIX/Posix functions">,
  Documentation<NotDocumented>;

} // end optin.portability


//===----------------------------------------------------------------------===//
// Taint checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = TaintOptIn in {

def TaintedAllocChecker: Checker<"TaintedAlloc">,
  HelpText<"Check for memory allocations, where the size parameter "
           "might be a tainted (attacker controlled) value.">,
  Dependencies<[DynamicMemoryModeling, TaintPropagationChecker]>,
  Documentation<HasDocumentation>;

} // end "optin.taint"

//===----------------------------------------------------------------------===//
// NonDeterminism checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = NonDeterminismAlpha in {

def PointerIterationChecker : Checker<"PointerIteration">,
  HelpText<"Checks for non-determinism caused by iteration of unordered containers of pointers">,
  Documentation<HasDocumentation>;

def PointerSortingChecker : Checker<"PointerSorting">,
  HelpText<"Check for non-determinism caused by sorting of pointers">,
  Documentation<HasDocumentation>;

} // end alpha.nondeterminism

//===----------------------------------------------------------------------===//
// Fuchsia checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Fuchsia in {

def FuchsiaHandleChecker : Checker<"HandleChecker">,
  HelpText<"A Checker that detect leaks related to Fuchsia handles">,
  Documentation<HasDocumentation>;

}

let ParentPackage = FuchsiaAlpha in {

def FuchsiaLockChecker : Checker<"Lock">,
  HelpText<"Check for the correct usage of locking APIs.">,
  Dependencies<[PthreadLockBase]>,
  Documentation<HasDocumentation>;

} // end fuchsia

//===----------------------------------------------------------------------===//
// WebKit checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = WebKit in {

def RefCntblBaseVirtualDtorChecker : Checker<"RefCntblBaseVirtualDtor">,
  HelpText<"Check for any ref-countable base class having virtual destructor.">,
  Documentation<HasDocumentation>;

def NoUncountedMemberChecker : Checker<"NoUncountedMemberChecker">,
  HelpText<"Check for no uncounted member variables.">,
  Documentation<HasDocumentation>;

def UncountedLambdaCapturesChecker : Checker<"UncountedLambdaCapturesChecker">,
  HelpText<"Check uncounted lambda captures.">,
  Documentation<HasDocumentation>;

} // end webkit

let ParentPackage = WebKitAlpha in {

def UncountedCallArgsChecker : Checker<"UncountedCallArgsChecker">,
  HelpText<"Check uncounted call arguments.">,
  Documentation<HasDocumentation>;

def UncountedLocalVarsChecker : Checker<"UncountedLocalVarsChecker">,
  HelpText<"Check uncounted local variables.">,
  Documentation<HasDocumentation>;

} // end alpha.webkit
