# Debug Info Assignment Tracking

Assignment Tracking is an alternative technique for tracking variable location
debug info through optimisations in LLVM. It provides accurate variable
locations for assignments where a local variable (or a field of one) is the
LHS. In rare and complicated circumstances indirect assignments might be
optimized away without being tracked, but otherwise we make our best effort to
track all variable locations.

The core idea is to track more information about source assignments in order
and preserve enough information to be able to defer decisions about whether to
use non-memory locations (register, constant) or memory locations until after
middle end optimisations have run. This is in opposition to using
`#dbg_declare` and `#dbg_value`, which is to make the decision for most
variables early on, which can result in suboptimal variable locations that may
be either incorrect or incomplete.

A secondary goal of assignment tracking is to cause minimal additional work for
LLVM pass writers, and minimal disruption to LLVM in general.

## Status and usage

**Status**: Experimental work in progress. Enabling is strongly advised against
except for development and testing.

**Enable in Clang**: `-Xclang -fexperimental-assignment-tracking`

That causes Clang to get LLVM to run the pass `declare-to-assign`. The pass
converts conventional debug records to assignment tracking metadata and sets
the module flag `debug-info-assignment-tracking` to the value `i1 true`. To
check whether assignment tracking is enabled for a module call
`isAssignmentTrackingEnabled(const Module &M)` (from `llvm/IR/DebugInfo.h`).

## Design and implementation

### Assignment markers: `#dbg_assign`

`#dbg_value`, a conventional debug record, marks out a position in the
IR where a variable takes a particular value. Similarly, Assignment Tracking
marks out the position of assignments with a record called `#dbg_assign`.

In order to know where in IR it is appropriate to use a memory location for a
variable, each assignment marker must in some way refer to the store, if any
(or multiple!), that performs the assignment. That way, the position of the
store and marker can be considered together when making that choice. Another
important benefit of referring to the store is that we can then build a two-way
mapping of stores<->markers that can be used to find markers that need to be
updated when stores are modified.

An `#dbg_assign` marker that is not linked to any instruction signals that
the store that performed the assignment has been optimised out, and therefore
the memory location will not be valid for at least some part of the program.

Here's the `#dbg_assign` signature. `Value *` type parameters are first wrapped
in `ValueAsMetadata`:

```
  #dbg_assign(Value *Value,
              DIExpression *ValueExpression,
              DILocalVariable *Variable,
              DIAssignID *ID,
              Value *Address,
              DIExpression *AddressExpression)
```

The first three parameters look and behave like an `#dbg_value`. `ID` is a
reference to a store (see next section). `Address` is the destination address
of the store and it is modified by `AddressExpression`. An empty/undef/poison
address means the address component has been killed (the memory address is no
longer a valid location). LLVM currently encodes variable fragment information
in `DIExpression`s, so as an implementation quirk the `FragmentInfo` for
`Variable` is contained within `ValueExpression` only.

### Instruction link: `DIAssignID`

`DIAssignID` metadata is the mechanism that is currently used to encode the
store<->marker link. The metadata node has no operands and all instances are
`distinct`; equality is checked for by comparing addresses.

`#dbg_assign` records use a `DIAssignID` metadata node instance as an
operand. This way it refers to any store-like instruction that has the same
`DIAssignID` attachment. E.g. For this test.cpp,

```
int fun(int a) {
  return a;
}
```
compiled without optimisations:
```
$ clang++ test.cpp -o test.ll -emit-llvm -S -g -O0 -Xclang -fexperimental-assignment-tracking
```
we get:
```
define dso_local noundef i32 @_Z3funi(i32 noundef %a) #0 !dbg !8 {
entry:
  %a.addr = alloca i32, align 4, !DIAssignID !13
    #dbg_assign(i1 undef, !14, !DIExpression(), !13, i32* %a.addr, !DIExpression(), !15)
  store i32 %a, i32* %a.addr, align 4, !DIAssignID !16
    #dbg_assign(i32 %a, !14, !DIExpression(), !16, i32* %a.addr, !DIExpression(), !15)
  %0 = load i32, i32* %a.addr, align 4, !dbg !17
  ret i32 %0, !dbg !18
}

...
!13 = distinct !DIAssignID()
!14 = !DILocalVariable(name: "a", ...)
...
!16 = distinct !DIAssignID()
```

The first `#dbg_assign` refers to the `alloca` through `!DIAssignID !13`,
and the second refers to the `store` through `!DIAssignID !16`.

### Store-like instructions

In the absence of a linked `#dbg_assign`, a store to an address that is
known to be the backing storage for a variable is considered to represent an
assignment to that variable.

This gives us a safe fall-back in cases where `#dbg_assign` records have
been deleted, the `DIAssignID` attachment on the store has been dropped, or the
optimiser has made a once-indirect store (not tracked with Assignment Tracking)
direct.

### Middle-end: Considerations for pass-writers

#### Non-debug instruction updates

**Cloning** an instruction: nothing new to do. Cloning automatically clones a
`DIAssignID` attachment. Multiple instructions may have the same `DIAssignID`
instruction. In this case, the assignment is considered to take place in
multiple positions in the program.

**Moving** a non-debug instruction: nothing new to do. Instructions linked to a
`#dbg_assign` have their initial IR position marked by the position of the
`#dbg_assign`.

**Deleting** a non-debug instruction: nothing new to do. Simple DSE does not
require any change; it’s safe to delete an instruction with a `DIAssignID`
attachment. A `#dbg_assign` that uses a `DIAssignID` that is not attached
to any instruction indicates that the memory location isn’t valid.

**Merging** stores: In many cases no change is required as `DIAssignID`
attachments are automatically merged if `combineMetadata` is called. One way or
another, the `DIAssignID` attachments must be merged such that new store
becomes linked to all the `#dbg_assign` records that the merged stores
were linked to. This can be achieved simply by calling a helper function
`Instruction::mergeDIAssignID`.

**Inlining** stores: As stores are inlined we generate `#dbg_assign`
records and `DIAssignID` attachments as if the stores represent source
assignments, just like the in frontend. This isn’t perfect, as stores may have
been moved, modified or deleted before inlining, but it does at least keep the
information about the variable correct within the non-inlined scope.

**Splitting** stores: SROA and passes that split stores treat `#dbg_assign`
records similarly to `#dbg_declare` records. Clone the
`#dbg_assign` records linked to the store, update the FragmentInfo in
the `ValueExpression`, and give the split stores (and cloned records) new
`DIAssignID` attachments each. In other words, treat the split stores as
separate assignments. For partial DSE (e.g. shortening a memset), we do the
same except that `#dbg_assign` for the dead fragment gets an `Undef`
`Address`.

**Promoting** allocas and store/loads: `#dbg_assign` records implicitly
describe joined values in memory locations at CFG joins, but this is not
necessarily the case after promoting (or partially promoting) the
variable. Passes that promote variables are responsible for inserting
`#dbg_assign` records after the resultant PHIs generated during
promotion. `mem2reg` already has to do this (with `#dbg_value`) for
`#dbg_declare`s. Where a store has no linked record, the store is
assumed to represent an assignment for variables stored at the destination
address.

#### Debug record updates

**Moving** a debug record: avoid moving `#dbg_assign` records where
possible, as they represent a source-level assignment, whose position in the
program should not be affected by optimization passes.

**Deleting** a debug record: Nothing new to do. Just like for conventional
debug records, unless it is unreachable, it’s almost always incorrect to
delete a `#dbg_assign` record.

### Lowering `#dbg_assign` to MIR

To begin with only SelectionDAG ISel will be supported. `#dbg_assign`
records are lowered to MIR `DBG_INSTR_REF` instructions. Before this happens
we need to decide where it is appropriate to use memory locations and where we
must use a non-memory location (or no location) for each variable. In order to
make those decisions we run a standard fixed-point dataflow analysis that makes
the choice at each instruction, iteratively joining the results for each block.

### TODO list

As this is an experimental work in progress so there are some items we still need
to tackle:

* As mentioned in test llvm/test/DebugInfo/assignment-tracking/X86/diamond-3.ll,
  the analysis should treat escaping calls like untagged stores.

* The system expects locals to be backed by a local alloca. This isn't always
  the case - sometimes a pointer to storage is passed into a function
  (e.g. sret, byval). We need to be able to handle those cases. See
  llvm/test/DebugInfo/Generic/assignment-tracking/track-assignments.ll and
  clang/test/CodeGen/assignment-tracking/assignment-tracking.cpp for examples.

* `trackAssignments` doesn't yet work for variables that have their
  `#dbg_declare` location modified by a `DIExpression`, e.g. when the
  address of the variable is itself stored in an `alloca` with the
  `#dbg_declare` using `DIExpression(DW_OP_deref)`. See `indirectReturn` in
  llvm/test/DebugInfo/Generic/assignment-tracking/track-assignments.ll and in
  clang/test/CodeGen/assignment-tracking/assignment-tracking.cpp for an
  example.

* In order to solve the first bullet-point we need to be able to specify that a
  memory location is available without using a `DIAssignID`. This is because
  the storage address is not computed by an instruction (it's an argument
  value) and therefore we have nowhere to put the metadata attachment. To solve
  this we probably need another marker record to denote "the variable's
  stack home is X address" - similar to `#dbg_declare` except that it needs
  to compose with `#dbg_assign` records such that the stack home address
  is only selected as a location for the variable when the `#dbg_assign`
  records agree it should be.

* Given the above (a special "the stack home is X" record), and the fact
  that we can only track assignments with fixed offsets and sizes, I think we
  can probably get rid of the address and address-expression part, since it
  will always be computable with the info we have.
