# Instruction referencing for debug info

This document explains how LLVM uses value tracking, or instruction
referencing, to determine variable locations for debug info in the code
generation stage of compilation. This content is aimed at those working on code
generation targets and optimisation passes. It may also be of interest to anyone
curious about low-level debug info handling.

# Problem statement

At the end of compilation, LLVM must produce a DWARF location list (or similar)
describing what register or stack location a variable can be found in, for each
instruction in that variable's lexical scope. We could track the virtual
register that the variable resides in through compilation, however this is
vulnerable to register optimisations during regalloc, and instruction
movements.

# Solution: instruction referencing

Rather than identify the virtual register that a variable value resides in,
instead in instruction referencing mode, LLVM refers to the machine instruction
and operand position that the value is defined in. Consider the LLVM IR way of
referring to instruction values:

```llvm
%2 = add i32 %0, %1
  #dbg_value(metadata i32 %2,
```

In LLVM IR, the IR Value is synonymous with the instruction that computes the
value, to the extent that in memory a Value is a pointer to the computing
instruction. Instruction referencing implements this relationship in the
codegen backend of LLVM, after instruction selection. Consider the X86 assembly
below and instruction referencing debug info, corresponding to the earlier
LLVM IR:

```text
%2:gr32 = ADD32rr %0, %1, implicit-def $eflags, debug-instr-number 1
DBG_INSTR_REF 1, 0, !123, !456, debug-location !789
```

While the function remains in SSA form, virtual register `%2` is sufficient to
identify the value computed by the instruction -- however the function
eventually leaves SSA form, and register optimisations will obscure which
register the desired value is in. Instead, a more consistent way of identifying
the instruction's value is to refer to the `MachineOperand` where the value is
defined: independently of which register is defined by that `MachineOperand`. In
the code above, the `DBG_INSTR_REF` instruction refers to instruction number
one, operand zero, while the `ADD32rr` has a `debug-instr-number` attribute
attached indicating that it is instruction number one.

De-coupling variable locations from registers avoids difficulties involving
register allocation and optimisation, but requires additional instrumentation
when the instructions are optimised instead. Optimisations that replace
instructions with optimised versions that compute the same value must either
preserve the instruction number, or record a substitution from the old
instruction / operand number pair to the new instruction / operand pair -- see
`MachineFunction::substituteDebugValuesForInst`. If debug info maintenance is
not performed, or an instruction is eliminated as dead code, the variable
location is safely dropped and marked "optimised out". The exception is
instructions that are mutated rather than replaced, which always need debug info
maintenance.

# Register allocator considerations

When the register allocator runs, debugging instructions do not directly refer
to any virtual registers, and thus there is no need for expensive location
maintenance during regalloc (i.e. `LiveDebugVariables`). Debug instructions are
unlinked from the function, then linked back in after register allocation
completes.

The exception is `PHI` instructions: these become implicit definitions at
control flow merges once regalloc finishes, and any debug numbers attached to
`PHI` instructions are lost. To circumvent this, debug numbers of `PHI`s are
recorded at the start of register allocation (`phi-node-elimination`), then
`DBG_PHI` instructions are inserted after regalloc finishes. This requires some
maintenance of which register a variable is located in during regalloc, but at
single positions (block entry points) rather than ranges of instructions.

An example, before regalloc:

```text
bb.2:
  %2 = PHI %1, %bb.0, %2, %bb.1, debug-instr-number 1
```

After:

```text
bb.2:
  DBG_PHI $rax, 1
```

# `LiveDebugValues`

After optimisations and code layout complete, information about variable
values must be translated into variable locations, i.e. registers and stack
slots. This is performed in the [`LiveDebugValues` pass][LiveDebugValues], where
the debug instructions and machine code are separated out into two independent
functions:
 * One that assigns values to variable names,
 * One that assigns values to machine registers and stack slots.

LLVM's existing SSA tools are used to place `PHI`s for each function, between
variable values and the values contained in machine locations, with value
propagation eliminating any unnecessary `PHI`s. The two can then be joined up
to map variables to values, then values to locations, for each instruction in
the function.

Key to this process is being able to identify the movement of values between
registers and stack locations, so that the location of values can be preserved
for the full time that they are resident in the machine.

# Required target support and transition guide

Instruction referencing will work on any target, but likely with poor coverage.
Supporting instruction referencing well requires:
 * Target hooks to be implemented to allow `LiveDebugValues` to follow values
   through the machine,
 * Target-specific optimisations to be instrumented, to preserve instruction
   numbers.

## Target hooks

`TargetInstrInfo::isCopyInstrImpl` must be implemented to recognise any
instructions that are copy-like -- `LiveDebugValues` uses this to identify when
values move between registers.

`TargetInstrInfo::isLoadFromStackSlotPostFE` and
`TargetInstrInfo::isStoreToStackSlotPostFE` are needed to identify spill and
restore instructions. Each should return the destination or source register
respectively. `LiveDebugValues` will track the movement of a value from / to
the stack slot. In addition, any instruction that writes to a stack spill
should have a `MachineMemoryOperand` attached, so that `LiveDebugValues` can
recognise that a slot has been clobbered.

## Target-specific optimisation instrumentation

Optimisations come in two flavours: those that mutate a `MachineInstr` to make
it do something different, and those that create a new instruction to replace
the operation of the old.

The former _must_ be instrumented -- the relevant question is whether any
register def in any operand will produce a different value, as a result of the
mutation. If the answer is yes, then there is a risk that a `DBG_INSTR_REF`
instruction referring to that operand will end up assigning the different
value to a variable, presenting the debugging developer with an unexpected
variable value. In such scenarios, call `MachineInstr::dropDebugNumber()` on the
mutated instruction to erase its instruction number. Any `DBG_INSTR_REF`
referring to it will produce an empty variable location instead, that appears
as "optimised out" in the debugger.

For the latter flavour of optimisation, to increase coverage you should record
an instruction number substitution: a mapping from the old instruction number /
operand pair to new instruction number / operand pair. Consider if we replace
a three-address add instruction with a two-address add:

```text
%2:gr32 = ADD32rr %0, %1, debug-instr-number 1
```

becomes

```text
%2:gr32 = ADD32rr %0(tied-def 0), %1, debug-instr-number 2
```

With a substitution from "instruction number 1 operand 0" to "instruction number
2 operand 0" recorded in the `MachineFunction`. In `LiveDebugValues`,
`DBG_INSTR_REF`s will be mapped through the substitution table to find the most
recent instruction number / operand number of the value it refers to.

Use `MachineFunction::substituteDebugValuesForInst` to automatically produce
substitutions between an old and new instruction. It assumes that any operand
that is a def in the old instruction is a def in the new instruction at the
same operand position. This works most of the time, for example in the example
above.

If operand numbers do not line up between the old and new instruction, use
`MachineInstr::getDebugInstrNum` to acquire the instruction number for the new
instruction, and `MachineFunction::makeDebugValueSubstitution` to record the
mapping between register definitions in the old and new instructions. If some
values computed by the old instruction are no longer computed by the new
instruction, record no substitution -- `LiveDebugValues` will safely drop the
now unavailable variable value.

Should your target clone instructions, much the same as the `TailDuplicator`
optimisation pass, do not attempt to preserve the instruction numbers or
record any substitutions. `MachineFunction::CloneMachineInstr` should drop the
instruction number of any cloned instruction, to avoid duplicate numbers
appearing to `LiveDebugValues`. Dealing with duplicated instructions is a
natural extension to instruction referencing that's currently unimplemented.

[LiveDebugValues]: project:SourceLevelDebugging.rst#LiveDebugValues expansion of variable locations
