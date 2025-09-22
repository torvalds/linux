# Allow Location Descriptions on the DWARF Expression Stack <!-- omit in toc -->

```{contents}
---
local:
---
```

# 1. Extension

In DWARF 5, expressions are evaluated using a typed value stack, a separate
location area, and an independent loclist mechanism. This extension unifies all
three mechanisms into a single generalized DWARF expression evaluation model
that allows both typed values and location descriptions to be manipulated on the
evaluation stack. Both single and multiple location descriptions are supported
on the stack. In addition, the call frame information (CFI) is extended to
support the full generality of location descriptions. This is done in a manner
that is backwards compatible with DWARF 5. The extension involves changes to the
DWARF 5 sections 2.5 (pp 26-38), 2.6 (pp 38-45), and 6.4 (pp 171-182).

The extension permits operations to act on location descriptions in an
incremental, consistent, and composable manner. It allows a small number of
operations to be defined to address the requirements of heterogeneous devices as
well as providing benefits to non-heterogeneous devices. It acts as a foundation
to provide support for other issues that have been raised that would benefit all
devices.

Other approaches were explored that involved adding specialized operations and
rules. However, these resulted in the need for more operations that did not
compose. It also resulted in operations with context sensitive semantics and
corner cases that had to be defined. The observation was that numerous
specialized context sensitive operations are harder for both producers and
consumers than a smaller number of general composable operations that have
consistent semantics regardless of context.

First, section [2. Heterogeneous Computing Devices](#heterogeneous-computing-devices)
describes heterogeneous devices and the features they have that are not addressed by DWARF 5.
Then section [3. DWARF5](#dwarf-5) presents a brief simplified overview of the DWARF 5 expression
evaluation model that highlights the difficulties for supporting the
heterogeneous features. Next, section [4. Extension
Solution](#extension-solution) provides an overview of the proposal, using
simplified examples to illustrate how it can address the issues of heterogeneous
devices and also benefit non-heterogeneous devices. Then overall conclusions are
covered in section [5. Conclusion](#conclusion).
Appendix [A. Changes to DWARF Debugging Information Format Version 5](#changes-to-dwarf-debugging-information-format-version-5) gives changes
relative to the DWARF Version 5 standard. Finally, appendix
[B. Further Information](#further-information) has references to further information.

# 2. Heterogeneous Computing Devices

GPUs and other heterogeneous computing devices have features not common to CPU
computing devices.

These devices often have many more registers than a CPU. This helps reduce
memory accesses which tend to be more expensive than on a CPU due to the much
larger number of threads concurrently executing. In addition to traditional
scalar registers of a CPU, these devices often have many wide vector registers.

![Example GPU Hardware](images/example-gpu-hardware.png)

They may support masked vector instructions that are used by the compiler to map
high level language threads onto the lanes of the vector registers. As a
consequence, multiple language threads execute in lockstep as the vector
instructions are executed. This is termed single instruction multiple thread
(SIMT) execution.

![SIMT/SIMD Execution Model](images/simt-execution-model.png)

GPUs can have multiple memory address spaces in addition to the single global
memory address space of a CPU. These additional address spaces are accessed
using distinct instructions and are often local to a particular thread or group
of threads.

For example, a GPU may have a per thread block address space that is implemented
as scratch pad memory with explicit hardware support to isolate portions to
specific groups of threads created as a single thread block.

A GPU may also use global memory in a non linear manner. For example, to support
providing a SIMT per lane address space efficiently, there may be instructions
that support interleaved access.

Through optimization, the source variables may be located across these different
storage kinds. SIMT execution requires locations to be able to express selection
of runtime defined pieces of vector registers. With the more complex locations,
there is a benefit to be able to factorize their calculation which requires all
location kinds to be supported uniformly, otherwise duplication is necessary.

# 3. DWARF 5

Before presenting the proposed solution to supporting heterogeneous devices, a
brief overview of the DWARF 5 expression evaluation model will be given to
highlight the aspects being addressed by the extension.

## 3.1 How DWARF Maps Source Language To Hardware

DWARF is a standardized way to specify debug information. It describes source
language entities such as compilation units, functions, types, variables, etc.
It is either embedded directly in sections of the code object executables, or
split into separate files that they reference.

DWARF maps between source program language entities and their hardware
representations. For example:

- It maps a hardware instruction program counter to a source language program
  line, and vice versa.
- It maps a source language function to the hardware instruction program counter
  for its entry point.
- It maps a source language variable to its hardware location when at a
  particular program counter.
- It provides information to allow virtual unwinding of hardware registers for a
  source language function call stack.
- In addition, it provides numerous other information about the source language
  program.

In particular, there is great diversity in the way a source language entity
could be mapped to a hardware location. The location may involve runtime values.
For example, a source language variable location could be:

- In register.
- At a memory address.
- At an offset from the current stack pointer.
- Optimized away, but with a known compiler time value.
- Optimized away, but with an unknown value, such as happens for unused
  variables.
- Spread across combination of the above kinds of locations.
- At a memory address, but also transiently loaded into registers.

To support this DWARF 5 defines a rich expression language comprised of loclist
expressions and operation expressions. Loclist expressions allow the result to
vary depending on the PC. Operation expressions are made up of a list of
operations that are evaluated on a simple stack machine.

A DWARF expression can be used as the value of different attributes of different
debug information entries (DIE). A DWARF expression can also be used as an
argument to call frame information information (CFI) entry operations. An
expression is evaluated in a context dictated by where it is used. The context
may include:

- Whether the expression needs to produce a value or the location of an entity.
- The current execution point including process, thread, PC, and stack frame.
- Some expressions are evaluated with the stack initialized with a specific
  value or with the location of a base object that is available using the
  DW_OP_push_object_address operation.

## 3.2 Examples

The following examples illustrate how DWARF expressions involving operations are
evaluated in DWARF 5. DWARF also has expressions involving location lists that
are not covered in these examples.

### 3.2.1 Dynamic Array Size

The first example is for an operation expression associated with a DIE attribute
that provides the number of elements in a dynamic array type. Such an attribute
dictates that the expression must be evaluated in the context of providing a
value result kind.

![Dynamic Array Size Example](images/01-value.example.png)

In this hypothetical example, the compiler has allocated an array descriptor in
memory and placed the descriptor's address in architecture register SGPR0. The
first location of the array descriptor is the runtime size of the array.

A possible expression to retrieve the dynamic size of the array is:

    DW_OP_regval_type SGPR0 Generic
    DW_OP_deref

The expression is evaluated one operation at a time. Operations have operands
and can pop and push entries on a stack.

![Dynamic Array Size Example: Step 1](images/01-value.example.frame.1.png)

The expression evaluation starts with the first DW_OP_regval_type operation.
This operation reads the current value of an architecture register specified by
its first operand: SGPR0. The second operand specifies the size of the data to
read. The read value is pushed on the stack. Each stack element is a value and
its associated type.

![Dynamic Array Size Example: Step 2](images/01-value.example.frame.2.png)

The type must be a DWARF base type. It specifies the encoding, byte ordering,
and size of values of the type. DWARF defines that each architecture has a
default generic type: it is an architecture specific integral encoding and byte
ordering, that is the size of the architecture's global memory address.

The DW_OP_deref operation pops a value off the stack, treats it as a global
memory address, and reads the contents of that location using the generic type.
It pushes the read value on the stack as the value and its associated generic
type.

![Dynamic Array Size Example: Step 3](images/01-value.example.frame.3.png)

The evaluation stops when it reaches the end of the expression. The result of an
expression that is evaluated with a value result kind context is the top element
of the stack, which provides the value and its type.

### 3.2.2 Variable Location in Register

This example is for an operation expression associated with a DIE attribute that
provides the location of a source language variable. Such an attribute dictates
that the expression must be evaluated in the context of providing a location
result kind.

DWARF defines the locations of objects in terms of location descriptions.

In this example, the compiler has allocated a source language variable in
architecture register SGPR0.

![Variable Location in Register Example](images/02-reg.example.png)

A possible expression to specify the location of the variable is:

    DW_OP_regx SGPR0

![Variable Location in Register Example: Step 1](images/02-reg.example.frame.1.png)

The DW_OP_regx operation creates a location description that specifies the
location of the architecture register specified by the operand: SGPR0. Unlike
values, location descriptions are not pushed on the stack. Instead they are
conceptually placed in a location area. Unlike values, location descriptions do
not have an associated type, they only denote the location of the base of the
object.

![Variable Location in Register Example: Step 2](images/02-reg.example.frame.2.png)

Again, evaluation stops when it reaches the end of the expression. The result of
an expression that is evaluated with a location result kind context is the
location description in the location area.

### 3.2.3 Variable Location in Memory

The next example is for an operation expression associated with a DIE attribute
that provides the location of a source language variable that is allocated in a
stack frame. The compiler has placed the stack frame pointer in architecture
register SGPR0, and allocated the variable at offset 0x10 from the stack frame
base. The stack frames are allocated in global memory, so SGPR0 contains a
global memory address.

![Variable Location in Memory Example](images/03-memory.example.png)

A possible expression to specify the location of the variable is:

    DW_OP_regval_type SGPR0 Generic
    DW_OP_plus_uconst 0x10

![Variable Location in Memory Example: Step 1](images/03-memory.example.frame.1.png)

As in the previous example, the DW_OP_regval_type operation pushes the stack
frame pointer global memory address onto the stack. The generic type is the size
of a global memory address.

![Variable Location in Memory Example: Step 2](images/03-memory.example.frame.2.png)

The DW_OP_plus_uconst operation pops a value from the stack, which must have a
type with an integral encoding, adds the value of its operand, and pushes the
result back on the stack with the same associated type. In this example, that
computes the global memory address of the source language variable.

![Variable Location in Memory Example: Step 3](images/03-memory.example.frame.3.png)

Evaluation stops when it reaches the end of the expression. If the expression
that is evaluated has a location result kind context, and the location area is
empty, then the top stack element must be a value with the generic type. The
value is implicitly popped from the stack, and treated as a global memory
address to create a global memory location description, which is placed in the
location area. The result of the expression is the location description in the
location area.

![Variable Location in Memory Example: Step 4](images/03-memory.example.frame.4.png)

### 3.2.4 Variable Spread Across Different Locations

This example is for a source variable that is partly in a register, partly undefined, and partly in memory.

![Variable Spread Across Different Locations Example](images/04-composite.example.png)

DWARF defines composite location descriptions that can have one or more parts.
Each part specifies a location description and the number of bytes used from it.
The following operation expression creates a composite location description.

    DW_OP_regx SGPR3
    DW_OP_piece 4
    DW_OP_piece 2
    DW_OP_bregx SGPR0 0x10
    DW_OP_piece 2

![Variable Spread Across Different Locations Example: Step 1](images/04-composite.example.frame.1.png)

The DW_OP_regx operation creates a register location description in the location
area.

![Variable Spread Across Different Locations Example: Step 2](images/04-composite.example.frame.2.png)

The first DW_OP_piece operation creates an incomplete composite location
description in the location area with a single part. The location description in
the location area is used to define the beginning of the part for the size
specified by the operand, namely 4 bytes.

![Variable Spread Across Different Locations Example: Step 3](images/04-composite.example.frame.3.png)

A subsequent DW_OP_piece adds a new part to an incomplete composite location
description already in the location area. The parts form a contiguous set of
bytes. If there are no other location descriptions in the location area, and no
value on the stack, then the part implicitly uses the undefined location
description. Again, the operand specifies the size of the part in bytes. The
undefined location description can be used to indicate a part that has been
optimized away. In this case, 2 bytes of undefined value.

![Variable Spread Across Different Locations Example: Step 4](images/04-composite.example.frame.4.png)

The DW_OP_bregx operation reads the architecture register specified by the first
operand (SGPR0) as the generic type, adds the value of the second operand
(0x10), and pushes the value on the stack.

![Variable Spread Across Different Locations Example: Step 5](images/04-composite.example.frame.5.png)

The next DW_OP_piece operation adds another part to the already created
incomplete composite location.

If there is no other location in the location area, but there is a value on
stack, the new part is a memory location description. The memory address used is
popped from the stack. In this case, the operand of 2 indicates there are 2
bytes from memory.

![Variable Spread Across Different Locations Example: Step 6](images/04-composite.example.frame.6.png)

Evaluation stops when it reaches the end of the expression. If the expression
that is evaluated has a location result kind context, and the location area has
an incomplete composite location description, the incomplete composite location
is implicitly converted to a complete composite location description. The result
of the expression is the location description in the location area.

![Variable Spread Across Different Locations Example: Step 7](images/04-composite.example.frame.7.png)

### 3.2.5 Offsetting a Composite Location

This example attempts to extend the previous example to offset the composite
location description it created. The [3.2.3 Variable Location in
Memory](#variable-location-in-memory) example conveniently used the DW_OP_plus
operation to offset a memory address.

    DW_OP_regx SGPR3
    DW_OP_piece 4
    DW_OP_piece 2
    DW_OP_bregx SGPR0 0x10
    DW_OP_piece 2
    DW_OP_plus_uconst 5

![Offsetting a Composite Location Example: Step 6](images/05-composite-plus.example.frame.1.png)

However, DW_OP_plus cannot be used to offset a composite location. It only
operates on the stack.

![Offsetting a Composite Location Example: Step 7](images/05-composite-plus.example.frame.2.png)

To offset a composite location description, the compiler would need to make a
different composite location description, starting at the part corresponding to
the offset. For example:

    DW_OP_piece 1
    DW_OP_bregx SGPR0 0x10
    DW_OP_piece 2

This illustrates that operations on stack values are not composable with
operations on location descriptions.

### 3.2.6 Pointer to Member

> NOTE: Without loss of generality, DWARF 4 is used in this example as full
> support for DWARF 5 is not present in the versions of the tools used. No
> feature of DWARF 5 provides a remedy for this issue.

This example highlights the inability of DWARF 5 to describe C++
pointer-to-member use semantics.

The mechanism DWARF 5 provides for describing pointer-to-member use is
`DW_AT_use_location`, which is defined as encoding a location description which
computes the address of the member pointed to by a pointer-to-member, given the
pointer-to-member object and the address of the containing object.

That is, when a debug agent wishes to evaluate a pointer-to-member access
operation, it first pushes two values onto the DWARF expression stack:

* The pointer-to-member object
* The address of the containing object

It then evaluates the location description associated with the
`DW_AT_use_location` of the pointer-to-member type, and interprets the result
as the address of the member pointed to by the pointer-to-member.

Consider the following C++ source file `s.cc`:

```cpp
struct s {
  int m;
  int n;
};
int s::* p;
```

When compiled with GCC and inspected with dwarfdump:

```
$ g++ -gdwarf-5 -O3 -c s.cc
$ dwarfdump s.o
< 1><0x0000001e>    DW_TAG_structure_type
                      DW_AT_name                  s
                      DW_AT_byte_size             0x00000008
                      DW_AT_sibling               <0x0000003c>
< 2><0x00000029>      DW_TAG_member
                        DW_AT_name                  m
                        DW_AT_type                  <0x0000003c>
                        DW_AT_data_member_location  0
< 2><0x00000032>      DW_TAG_member
                        DW_AT_name                  n
                        DW_AT_type                  <0x0000003c>
                        DW_AT_data_member_location  4
< 1><0x0000003c>    DW_TAG_base_type
                      DW_AT_byte_size             0x00000004
                      DW_AT_encoding              DW_ATE_signed
                      DW_AT_name                  int
< 1><0x00000043>    DW_TAG_ptr_to_member_type
                      DW_AT_containing_type       <0x0000001e>
                      DW_AT_type                  <0x0000003c>
                      DW_AT_use_location          len 0x0001: 22: DW_OP_plus
< 1><0x0000004e>    DW_TAG_variable
                      DW_AT_name                  p
                      DW_AT_type                  <0x00000043>
                      DW_AT_external              yes(1)
                      DW_AT_location              len 0x0009: 030000000000000000: DW_OP_addr 0x00000000
```

Note the location description for `DW_AT_use_location` is `DW_OP_plus`, which
reflects the GCC implementation of the pointer-to-member as an integral byte
offset within the containing object. For example, the value of `&s::m` in this
implementation is `offsetof(s, m)` and the value of `&s::n` is `offsetof(s, n)`:

```cpp
struct s {
    int m;   // offsetof(s, m) == 0
    int n;   // offsetof(s, n) == 4
} o;         // &o == 0xff00
int s::* p;
int *i;

p = &s::m;   // p == 0
i = &(o.*p); // i == 0xff00 + 0
p = &s::n;   // p == 4
i = &(o.*p); // i == 0xff00 + 4
```

The expression `DW_OP_plus` accurately describes this implementation so long as
the entire containing object resides in memory in the default address space.

However, what if the containing object or the member pointed to are not at any
default address space address?

The compiler may store the containing object in memory in any address space,
in a register, recompute its value at each use, or compose any of these in
arbitrary ways.

The richness of the existing DWARF 5 expression language is a reflection of the
diversity of possible implementation strategies and optimization choices
affecting the location of an object in a program, and (modulo address spaces)
it can describe all of these locations for variables. However, the moment we
look at a pointer-to-member use we are restricted to only objects residing in a
contiguous piece of memory in the default address space.

To demonstrate the problem, consider a program which GCC chooses to optimize in
such a way that the containing object is not in memory at all:

ptm.h:
```cpp
struct s {
    int m;
    int n;
};
void i(int);
extern int t;
void f(s x, int s::* p);
```

ptm.cc:
```cpp
#include "ptm.h"
void f(s x, int s::* p) {
    for (int a = 0; a < t; ++a) {
        x.m += a + x.n;
        i(x.*p);
    }
}
```

main.cc:
```cpp
#include "ptm.h"
int t = 100;
void i(int) {}
int main(int argc, char *argv[]) {
    s x = { 0, 1 };
    f(x, &s::m);
}
```

When compiled and run under GDB:

```
$ g++-9 -gdwarf-4 -O3 -c main.cc -o main.o
$ g++-9 -gdwarf-4 -O3 -c ptm.cc -o ptm.o
$ g++-9 main.o ptm.o -o use_location.out
$ gdb ./use_location.out
(gdb) maint set dwarf always-disassemble
(gdb) b ptm.cc:5
Breakpoint 1 at 0x119e: file ptm.cc, line 5.
(gdb) r

Breakpoint 1, f (x=..., p=<optimized out>) at ptm.cc:5
5               i(x.*p);
```

Note that the compiler has promoted the entire object `x` into register `rdi`
for the body of the loop:

```
(gdb) info addr x
Symbol "x" is multi-location:
  Range 0x555555555160-0x5555555551af: a complex DWARF expression:
     0: DW_OP_reg5 [$rdi]

  Range 0x5555555551af-0x5555555551ba: a complex DWARF expression:
     0: DW_OP_fbreg -56

.
(gdb) p $pc
$1 = (void (*)(void)) 0x55555555519e <f(s, int s::*)+62>
```

And so it is impossible to interpret `DW_OP_use_location` in this case:

```
(gdb) p x.*p
Address requested for identifier "x" which is in register $rdi
```

With location descriptions on the stack, the definition of `DW_OP_use_location`
can be modified by replacing every instance of "address" with "location
description", as is described in [Type Entries](#type-entries).

To implement the fully generalized version of this attribute, GCC would only
need to change the expression from `DW_OP_plus` to `DW_OP_swap,
DW_OP_LLVM_offset`.

### 3.2.7 Virtual Base Class

> NOTE: Without loss of generality, DWARF 4 is used in this example as full
> support for DWARF 5 is not present in the versions of the tools used. No
> feature of DWARF 5 provides a remedy for this issue.

This example highlights the inability of DWARF 5 to describe C++
virtual inheritance semantics.

The mechanism DWARF 5 provides for describing the location of an inherited
subobject is `DW_AT_data_member_location`. This attribute is overloaded to
describe both data member locations and inherited subobject locations, and
in each case has multiple possible forms:

* If an integral constant form, it encodes the byte offset from the derived
  object to the data member or subobject.
* Otherwise, it encodes a location description to compute the address of the
  data member or subobject given the address of the derived object.

Only the attribute describing a subobject, and only the location description
form are considered here.

In this case, when a debug agent wishes to locate the subobject, it first
pushes the address of the derived object onto the DWARF expression stack. It
then evaluates the location description associated with the
`DW_AT_data_member_location` of the `DW_TAG_inheritence` DIE corresponding to
the inherited subobject.

Consider the following C++ source file `ab.cc`:

```cpp
class A {
public:
    char x;
};
class B
: public virtual A {} o;
```

When compiled with GCC and inspected with dwarfdump:

```
$ g++ -gdwarf-5 -O3 -c ab.cc
$ dwarfdump ab.o
< 1><0x0000002a>    DW_TAG_class_type
                      DW_AT_name                  A
                      DW_AT_byte_size             0x00000001
                      DW_AT_sibling               <0x00000042>
< 1><0x00000049>    DW_TAG_class_type
                      DW_AT_name                  B
                      DW_AT_byte_size             0x00000010
                      DW_AT_containing_type       <0x00000049>
                      DW_AT_sibling               <0x000000f9>
< 2><0x00000058>      DW_TAG_inheritance
                        DW_AT_type                  <0x0000002a>
                        DW_AT_data_member_location  len 0x0006: 1206481c0622:
                          DW_OP_dup DW_OP_deref DW_OP_lit24 DW_OP_minus DW_OP_deref DW_OP_plus
                        DW_AT_virtuality            DW_VIRTUALITY_virtual
                        DW_AT_accessibility         DW_ACCESS_public
```

This `DW_AT_data_member_location` expression describes the dynamic process of
locating the `A`-in-`B` subobject according to the [Itanium
ABI](https://refspecs.linuxfoundation.org/cxxabi-1.86.html#layout). A diagram
of the logical layout of class `B` is:

```
0: class B
0:   vptr B
8:   class A
8:     A::x
```

That is, the address of an object of class `B` is equivalent to the address for
the `vtable` pointer for `B`. As there are no other direct data members of `B`
the primary base class subobject of class `A` comes next, and there is no
intervening padding as the subobject alignment requirements are already
satisfied.

The `vtable` pointer for `B` contains an entry `vbase_offset` for each virtual
base class. In this case, that table layout is:

```
-24: vbase_offset[A]=8
-16: offset_to_top=0
 -8: B RTTI
  0: <vtable for B>
```

That is, to find the `vbase_offset` for the `A`-in-`B` subobject the address
`vptr B` is offset by the statically determined value `-24`.

Thus, in order to implement `DW_AT_data_member_location` for `A`-in-`B`, the
expression needs to index to a statically known byte offset of `-24` through
`vptr B` to lookup `vbase_offset` for `A`-in-`B`. It must then offset the
location of `B` by the dynamic value of `vbase_offset` (in this case `8`) to
arrive at the location of the inherited subobject.

This definition shares the same problem as example [3.2.6](#pointer-to-member)
in that it relies on the address of the derived object and inherited subobject,
when there is no guarantee either or both have any address at all.

To demonstrate the problem, consider a program which GCC chooses to optimize in
such a way that the derived object is not in memory at all:

f.h:
```cpp
class A {
public:
    char x;
};
class B
: public virtual A {};
void f(B b);
```

f.cc:
```cpp
#include "f.h"
void f(B b) {}
```

main.cc:
```cpp
#include "f.h"
int main(int argc, char *argv[]) {
    B b;
    b.x = 42;
    f(b);
    return b.x;
}
```

When compiled and run under GDB:

```
$ g++-9 -gdwarf-4 -O3 -c main.cc -o main.o
$ g++-9 -gdwarf-4 -O3 -c f.cc -o f.o
$ g++-9 main.o f.o -o cpp-vbase.out
(gdb) maint set dwarf always-disassemble
(gdb) b main.cc:6
Breakpoint 1 at 0x1090: file main.cc, line 6.
(gdb) r

Breakpoint 1, main (argc=<optimized out>, argv=<optimized out>) at main.cc:6
6           return b.x;
```

Note that the compiler has elided storage for the entire object `x` in the
body of `main()`:

```
(gdb) info addr b
Symbol "b" is multi-location:
  Range 0x555555555078-0x5555555550af: a complex DWARF expression:
     0: DW_OP_piece 8 (bytes)
     2: DW_OP_const1u 42
     4: DW_OP_stack_value
     5: DW_OP_piece 1 (bytes)
     7: DW_OP_piece 7 (bytes)

.
(gdb) p $pc
$1 = (void (*)(void)) 0x555555555090 <main(int, char**)+48>
```

And so it is impossible to interpret `DW_OP_data_member_location` in this case:

```
(gdb) p b
$2 = {<A> = <invalid address>, _vptr.B = <optimized out>}
```

> NOTE: The `vptr B` which should occupy the first 8 bytes of the object `b`
> are undefined in the DWARF, but could be described as an implicit value by
> the compiler. This change would be trivial and would directly expose the
> issue in DWARF 5 described here.

With location descriptions on the stack, the definition of
`DW_OP_data_member_location` can be modified by replacing every instance of
"address" with "location description", as is described in [A.5 Type Entries](#type-entries).

To implement the fully generalized version of this attribute, GCC would only
need to change the last operation in the expression from `DW_OP_plus` to
`DW_OP_LLVM_offset`.

## 3.3 Limitations

DWARF 5 is unable to describe variables in runtime indexed parts of registers.
This is required to describe a source variable that is located in a lane of a
SIMT vector register.

Some features only work when located in global memory. The type attribute
expressions require a base object which could be in any kind of location.

DWARF procedures can only accept global memory address arguments. This limits
the ability to factorize the creation of locations that involve other location
kinds.

There are no vector base types. This is required to describe vector registers.

There is no operation to create a memory location in a non-global address space.
Only the dereference operation supports providing an address space.

CFI location expressions do not allow composite locations or non-global address
space memory locations. Both these are needed in optimized code for devices with
vector registers and address spaces.

Bit field offsets are only supported in a limited way for register locations.
Supporting them in a uniform manner for all location kinds is required to
support languages with bit sized entities.

# 4. Extension Solution

This section outlines the extension to generalize the DWARF expression evaluation
model to allow location descriptions to be manipulated on the stack. It presents
a number of simplified examples to demonstrate the benefits and how the extension
solves the issues of heterogeneous devices. It presents how this is done in
a manner that is backwards compatible with DWARF 5.

## 4.1 Location Description

In order to have consistent, composable operations that act on location
descriptions, the extension defines a uniform way to handle all location kinds.
That includes memory, register, implicit, implicit pointer, undefined, and
composite location descriptions.

Each kind of location description is conceptually a zero-based offset within a
piece of storage. The storage is a contiguous linear organization of a certain
number of bytes (see below for how this is extended to support bit sized
storage).

- For global memory, the storage is the linear stream of bytes of the
  architecture's address size.
- For each separate architecture register, it is the linear stream of bytes of
  the size of that specific register.
- For an implicit, it is the linear stream of bytes of the value when
  represented using the value's base type which specifies the encoding, size,
  and byte ordering.
- For undefined, it is an infinitely sized linear stream where every byte is
  undefined.
- For composite, it is a linear stream of bytes defined by the composite's parts.

## 4.2 Stack Location Description Operations

The DWARF expression stack is extended to allow each stack entry to either be a
value or a location description.

Evaluation rules are defined to implicitly convert a stack element that is a
value to a location description, or vice versa, so that all DWARF 5 expressions
continue to have the same semantics. This reflects that a memory address is
effectively used as a proxy for a memory location description.

For each place that allows a DWARF expression to be specified, it is defined if
the expression is to be evaluated as a value or a location description.

Existing DWARF expression operations that are used to act on memory addresses
are generalized to act on any location description kind. For example, the
DW_OP_deref operation pops a location description rather than a memory address
value from the stack and reads the storage associated with the location kind
starting at the location description's offset.

Existing DWARF expression operations that create location descriptions are
changed to pop and push location descriptions on the stack. For example, the
DW_OP_value, DW_OP_regx, DW_OP_implicit_value, DW_OP_implicit_pointer,
DW_OP_stack_value, and DW_OP_piece.

New operations that act on location descriptions can be added. For example, a
DW_OP_offset operation that modifies the offset of the location description on
top of the stack. Unlike the DW_OP_plus operation that only works with memory
address, a DW_OP_offset operation can work with any location kind.

To allow incremental and nested creation of composite location descriptions, a
DW_OP_piece_end can be defined to explicitly indicate the last part of a
composite. Currently, creating a composite must always be the last operation of
an expression.

A DW_OP_undefined operation can be defined that explicitly creates the undefined
location description. Currently this is only possible as a piece of a composite
when the stack is empty.

## 4.3 Examples

This section provides some motivating examples to illustrate the benefits that
result from allowing location descriptions on the stack.

### 4.3.1 Source Language Variable Spilled to Part of a Vector Register

A compiler generating code for a GPU may allocate a source language variable
that it proves has the same value for every lane of a SIMT thread in a scalar
register. It may then need to spill that scalar register. To avoid the high cost
of spilling to memory, it may spill to a fixed lane of one of the numerous
vector registers.

![Source Language Variable Spilled to Part of a Vector Register Example](images/06-extension-spill-sgpr-to-static-vpgr-lane.example.png)

The following expression defines the location of a source language variable that
the compiler allocated in a scalar register, but had to spill to lane 5 of a
vector register at this point of the code.

    DW_OP_regx VGPR0
    DW_OP_offset_uconst 20

![Source Language Variable Spilled to Part of a Vector Register Example: Step 1](images/06-extension-spill-sgpr-to-static-vpgr-lane.example.frame.1.png)

The DW_OP_regx pushes a register location description on the stack. The storage
for the register is the size of the vector register. The register location
description conceptually references that storage with an initial offset of 0.
The architecture defines the byte ordering of the register.

![Source Language Variable Spilled to Part of a Vector Register Example: Step 2](images/06-extension-spill-sgpr-to-static-vpgr-lane.example.frame.2.png)

The DW_OP_offset_uconst pops a location description off the stack, adds its
operand value to the offset, and pushes the updated location description back on
the stack. In this case the source language variable is being spilled to lane 5
and each lane's component which is 32-bits (4 bytes), so the offset is 5*4=20.

![Source Language Variable Spilled to Part of a Vector Register Example: Step 3](images/06-extension-spill-sgpr-to-static-vpgr-lane.example.frame.3.png)

The result of the expression evaluation is the location description on the top
of the stack.

An alternative approach could be for the target to define distinct register
names for each part of each vector register. However, this is not practical for
GPUs due to the sheer number of registers that would have to be defined. It
would also not permit a runtime index into part of the whole register to be used
as shown in the next example.

### 4.3.2 Source Language Variable Spread Across Multiple Vector Registers

A compiler may generate SIMT code for a GPU. Each source language thread of
execution is mapped to a single lane of the GPU thread. Source language
variables that are mapped to a register, are mapped to the lane component of the
vector registers corresponding to the source language's thread of execution.

The location expression for such variables must therefore be executed in the
context of the focused source language thread of execution. A DW_OP_push_lane
operation can be defined to push the value of the lane for the currently focused
source language thread of execution. The value to use would be provided by the
consumer of DWARF when it evaluates the location expression.

If the source language variable is larger than the size of the vector register
lane component, then multiple vector registers are used. Each source language
thread of execution will only use the vector register components for its
associated lane.

![Source Language Variable Spread Across Multiple Vector Registers Example](images/07-extension-multi-lane-vgpr.example.png)

The following expression defines the location of a source language variable that
has to occupy two vector registers. A composite location description is created
that combines the two parts. It will give the correct result regardless of which
lane corresponds to the source language thread of execution that the user is
focused on.

    DW_OP_regx VGPR0
    DW_OP_push_lane
    DW_OP_uconst 4
    DW_OP_mul
    DW_OP_offset
    DW_OP_piece 4
    DW_OP_regx VGPR1
    DW_OP_push_lane
    DW_OP_uconst 4
    DW_OP_mul
    DW_OP_offset
    DW_OP_piece 4

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 1](images/07-extension-multi-lane-vgpr.example.frame.1.png)

The DW_OP_regx VGPR0 pushes a location description for the first register.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 2](images/07-extension-multi-lane-vgpr.example.frame.2.png)

The DW_OP_push_lane; DW_OP_uconst 4; DW_OP_mul calculates the offset for the
focused lanes vector register component as 4 times the lane number.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 3](images/07-extension-multi-lane-vgpr.example.frame.3.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 4](images/07-extension-multi-lane-vgpr.example.frame.4.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 5](images/07-extension-multi-lane-vgpr.example.frame.5.png)

The DW_OP_offset adjusts the register location description's offset to the
runtime computed value.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 6](images/07-extension-multi-lane-vgpr.example.frame.6.png)

The DW_OP_piece either creates a new composite location description, or adds a
new part to an existing incomplete one. It pops the location description to use
for the new part. It then pops the next stack element if it is an incomplete
composite location description, otherwise it creates a new incomplete composite
location description with no parts. Finally it pushes the incomplete composite
after adding the new part.

In this case a register location description is added to a new incomplete
composite location description. The 4 of the DW_OP_piece specifies the size of
the register storage that comprises the part. Note that the 4 bytes start at the
computed register offset.

For backwards compatibility, if the stack is empty or the top stack element is
an incomplete composite, an undefined location description is used for the part.
If the top stack element is a generic base type value, then it is implicitly
converted to a global memory location description with an offset equal to the
value.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 7](images/07-extension-multi-lane-vgpr.example.frame.7.png)

The rest of the expression does the same for VGPR1. However, when the
DW_OP_piece is evaluated there is an incomplete composite on the stack. So the
VGPR1 register location description is added as a second part.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 8](images/07-extension-multi-lane-vgpr.example.frame.8.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 9](images/07-extension-multi-lane-vgpr.example.frame.9.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 10](images/07-extension-multi-lane-vgpr.example.frame.10.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 11](images/07-extension-multi-lane-vgpr.example.frame.11.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 12](images/07-extension-multi-lane-vgpr.example.frame.12.png)

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 13](images/07-extension-multi-lane-vgpr.example.frame.13.png)

At the end of the expression, if the top stack element is an incomplete
composite location description, it is converted to a complete location
description and returned as the result.

![Source Language Variable Spread Across Multiple Vector Registers Example: Step 14](images/07-extension-multi-lane-vgpr.example.frame.14.png)

### 4.3.3 Source Language Variable Spread Across Multiple Kinds of Locations

This example is the same as the previous one, except the first 2 bytes of the
second vector register have been spilled to memory, and the last 2 bytes have
been proven to be a constant and optimized away.

![Source Language Variable Spread Across Multiple Kinds of Locations Example](images/08-extension-mixed-composite.example.png)

    DW_OP_regx VGPR0
    DW_OP_push_lane
    DW_OP_uconst 4
    DW_OP_mul
    DW_OP_offset
    DW_OP_piece 4
    DW_OP_addr 0xbeef
    DW_OP_piece 2
    DW_OP_uconst 0xf00d
    DW_OP_stack_value
    DW_OP_piece 2
    DW_OP_piece_end

The first 6 operations are the same.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 7](images/08-extension-mixed-composite.example.frame.1.png)

The DW_OP_addr operation pushes a global memory location description on the
stack with an offset equal to the address.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 8](images/08-extension-mixed-composite.example.frame.2.png)

The next DW_OP_piece adds the global memory location description as the next 2
byte part of the composite.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 9](images/08-extension-mixed-composite.example.frame.3.png)

The DW_OP_uconst 0xf00d; DW_OP_stack_value pushes an implicit location
description on the stack. The storage of the implicit location description is
the representation of the value 0xf00d using the generic base type's encoding,
size, and byte ordering.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 10](images/08-extension-mixed-composite.example.frame.4.png)

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 11](images/08-extension-mixed-composite.example.frame.5.png)

The final DW_OP_piece adds 2 bytes of the implicit location description as the
third part of the composite location description.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 12](images/08-extension-mixed-composite.example.frame.6.png)

The DW_OP_piece_end operation explicitly makes the incomplete composite location
description into a complete location description. This allows a complete
composite location description to be created on the stack that can be used as
the location description of another following operation. For example, the
DW_OP_offset can be applied to it. More practically, it permits creation of
multiple composite location descriptions on the stack which can be used to pass
arguments to a DWARF procedure using a DW_OP_call* operation. This can be
beneficial to factor the incrementally creation of location descriptions.

![Source Language Variable Spread Across Multiple Kinds of Locations Example: Step 12](images/08-extension-mixed-composite.example.frame.7.png)

### 4.3.4 Address Spaces

Heterogeneous devices can have multiple hardware supported address spaces which
use specific hardware instructions to access them.

For example, GPUs that use SIMT execution may provide hardware support to access
memory such that each lane can see a linear memory view, while the backing
memory is actually being accessed in an interleaved manner so that the locations
for each lanes Nth dword are contiguous. This minimizes cache lines read by the
SIMT execution.

![Address Spaces Example](images/09-extension-form-aspace.example.png)

The following expression defines the location of a source language variable that
is allocated at offset 0x10 in the current subprograms stack frame. The
subprogram stack frames are per lane and reside in an interleaved address space.

    DW_OP_regval_type SGPR0 Generic
    DW_OP_uconst 1
    DW_OP_form_aspace_address
    DW_OP_offset 0x10

![Address Spaces Example: Step 1](images/09-extension-form-aspace.example.frame.1.png)

The DW_OP_regval_type operation pushes the contents of SGPR0 as a generic value.
This is the register that holds the address of the current stack frame.

![Address Spaces Example: Step 2](images/09-extension-form-aspace.example.frame.2.png)

The DW_OP_uconst operation pushes the address space number. Each architecture
defines the numbers it uses in DWARF. In this case, address space 1 is being
used as the per lane memory.

![Address Spaces Example: Step 3](images/09-extension-form-aspace.example.frame.3.png)

The DW_OP_form_aspace_address operation pops a value and an address space
number. Each address space is associated with a separate storage. A memory
location description is pushed which refers to the address space's storage, with
an offset of the popped value.

![Address Spaces Example: Step 4](images/09-extension-form-aspace.example.frame.4.png)

All operations that act on location descriptions work with memory locations
regardless of their address space.

Every architecture defines address space 0 as the default global memory address
space.

Generalizing memory location descriptions to include an address space component
avoids having to create specialized operations to work with address spaces.

The source variable is at offset 0x10 in the stack frame. The DW_OP_offset
operation works on memory location descriptions that have an address space just
like for any other kind of location description.

![Address Spaces Example: Step 5](images/09-extension-form-aspace.example.frame.5.png)

The only operations in DWARF 5 that take an address space are DW_OP_xderef*.
They treat a value as the address in a specified address space, and read its
contents. There is no operation to actually create a location description that
references an address space. There is no way to include address space memory
locations in parts of composite locations.

Since DW_OP_piece now takes any kind of location description for its pieces, it
is now possible for parts of a composite to involve locations in different
address spaces. For example, this can happen when parts of a source variable
allocated in a register are spilled to a stack frame that resides in the
non-global address space.

### 4.3.5 Bit Offsets

With the generalization of location descriptions on the stack, it is possible to
define a DW_OP_bit_offset operation that adjusts the offset of any kind of
location in terms of bits rather than bytes. The offset can be a runtime
computed value. This is generally useful for any source language that support
bit sized entities, and for registers that are not a whole number of bytes.

DWARF 5 only supports bit fields in composites using DW_OP_bit_piece. It does
not support runtime computed offsets which can happen for bit field packed
arrays. It is also not generally composable as it must be the last part of an
expression.

The following example defines a location description for a source variable that
is allocated starting at bit 20 of a register. A similar expression could be
used if the source variable was at a bit offset within memory or a particular
address space, or if the offset is a runtime value.

![Bit Offsets Example](images/10-extension-bit-offset.example.png)

    DW_OP_regx SGPR3
    DW_OP_uconst 20
    DW_OP_bit_offset

![Bit Offsets Example: Step 1](images/10-extension-bit-offset.example.frame.1.png)

![Bit Offsets Example: Step 2](images/10-extension-bit-offset.example.frame.2.png)

![Bit Offsets Example: Step 3](images/10-extension-bit-offset.example.frame.3.png)

The DW_OP_bit_offset operation pops a value and location description from the
stack. It pushes the location description after updating its offset using the
value as a bit count.

![Bit Offsets Example: Step 4](images/10-extension-bit-offset.example.frame.4.png)

The ordering of bits within a byte, like byte ordering, is defined by the target
architecture. A base type could be extended to specify bit ordering in addition
to byte ordering.

## 4.4 Call Frame Information (CFI)

DWARF defines call frame information (CFI) that can be used to virtually unwind
the subprogram call stack. This involves determining the location where register
values have been spilled. DWARF 5 limits these locations to either be registers
or global memory. As shown in the earlier examples, heterogeneous devices may
spill registers to parts of other registers, to non-global memory address
spaces, or even a composite of different location kinds.

Therefore, the extension extends the CFI rules to support any kind of location
description, and operations to create locations in address spaces.

## 4.5 Objects Not In Byte Aligned Global Memory

DWARF 5 only effectively supports byte aligned memory locations on the stack by
using a global memory address as a proxy for a memory location description. This
is a problem for attributes that define DWARF expressions that require the
location of some source language entity that is not allocated in byte aligned
global memory.

For example, the DWARF expression of the DW_AT_data_member_location attribute is
evaluated with an initial stack containing the location of a type instance
object. That object could be located in a register, in a non-global memory
address space, be described by a composite location description, or could even
be an implicit location description.

A similar problem exists for DWARF expressions that use the
DW_OP_push_object_address operation. This operation pushes the location of a
program object associated with the attribute that defines the expression.

Allowing any kind of location description on the stack permits the DW_OP_call*
operations to be used to factor the creation of location descriptions. The
inputs and outputs of the call are passed on the stack. For example, on GPUs an
expression can be defined to describe the effective PC of inactive lanes of SIMT
execution. This is naturally done by composing the result of expressions for
each nested control flow region. This can be done by making each control flow
region have its own DWARF procedure, and then calling it from the expressions of
the nested control flow regions. The alternative is to make each control flow
region have the complete expression which results in much larger DWARF and is
less convenient to generate.

GPU compilers work hard to allocate objects in the larger number of registers to
reduce memory accesses, they have to use different memory address spaces, and
they perform optimizations that result in composites of these. Allowing
operations to work with any kind of location description enables creating
expressions that support all of these.

Full general support for bit fields and implicit locations benefits
optimizations on any target.

## 4.6 Higher Order Operations

The generalization allows an elegant way to add higher order operations that
create location descriptions out of other location descriptions in a general
composable manner.

For example, a DW_OP_extend operation could create a composite location
description out of a location description, an element size, and an element
count. The resulting composite would effectively be a vector of element count
elements with each element being the same location description of the specified
bit size.

A DW_OP_select_bit_piece operation could create a composite location description
out of two location descriptions, a bit mask value, and an element size. The
resulting composite would effectively be a vector of elements, selecting from
one of the two input locations according to the bit mask.

These could be used in the expression of an attribute that computes the
effective PC of lanes of SIMT execution. The vector result efficiently computes
the PC for each SIMT lane at once. The mask could be the hardware execution mask
register that controls which SIMT lanes are executing. For active divergent
lanes the vector element would be the current PC, and for inactive divergent
lanes the PC would correspond to the source language line at which the lane is
logically positioned.

Similarly, a DW_OP_overlay_piece operation could be defined that creates a
composite location description out of two location descriptions, an offset
value, and a size. The resulting composite would consist of parts that are
equivalent to one of the location descriptions, but with the other location
description replacing a slice defined by the offset and size. This could be used
to efficiently express a source language array that has had a set of elements
promoted into a vector register when executing a set of iterations of a loop in
a SIMD manner.

## 4.7 Objects In Multiple Places

A compiler may allocate a source variable in stack frame memory, but for some
range of code may promote it to a register. If the generated code does not
change the register value, then there is no need to save it back to memory.
Effectively, during that range, the source variable is in both memory and a
register. If a consumer, such as a debugger, allows the user to change the value
of the source variable in that PC range, then it would need to change both
places.

DWARF 5 supports loclists which are able to specify the location of a source
language entity is in different places at different PC locations. It can also
express that a source language entity is in multiple places at the same time.

DWARF 5 defines operation expressions and loclists separately. In general, this
is adequate as non-memory location descriptions can only be computed as the last
step of an expression evaluation.

However, allowing location descriptions on the stack permits non-memory location
descriptions to be used in the middle of expression evaluation. For example, the
DW_OP_call* and DW_OP_implicit_pointer operations can result in evaluating the
expression of a DW_AT_location attribute of a DIE. The DW_AT_location attribute
allows the loclist form. So the result could include multiple location
descriptions.

Similarly, the DWARF expression associated with attributes such as
DW_AT_data_member_location that are evaluated with an initial stack containing a
location description, or a DWARF operation expression that uses the
DW_OP_push_object_address operation, may want to act on the result of another
expression that returned a location description involving multiple places.

Therefore, the extension needs to define how expression operations that use those
results will behave. The extension does this by generalizing the expression stack
to allow an entry to be one or more single location descriptions. In doing this,
it unifies the definitions of DWARF operation expressions and loclist
expressions in a natural way.

All operations that act on location descriptions are extended to act on multiple
single location descriptions. For example, the DW_OP_offset operation adds the
offset to each single location description. The DW_OP_deref* operations simply
read the storage of one of the single location descriptions, since multiple
single location descriptions must all hold the same value. Similarly, if the
evaluation of a DWARF expression results in multiple single location
descriptions, the consumer can ensure any updates are done to all of them, and
any reads can use any one of them.

# 5. Conclusion

A strength of DWARF is that it has generally sought to provide generalized
composable solutions that address many problems, rather than solutions that only
address one-off issues. This extension attempts to follow that tradition by
defining a backwards compatible composable generalization that can address a
significant family of issues. It addresses the specific issues present for
heterogeneous computing devices, provides benefits for non-heterogeneous
devices, and can help address a number of other previously reported issues.

# A. Changes to DWARF Debugging Information Format Version 5

> NOTE: This appendix provides changes relative to DWARF Version 5. It has been
> defined such that it is backwards compatible with DWARF Version 5.
> Non-normative text is shown in <i>italics</i>. The section numbers generally
> correspond to those in the DWARF Version 5 standard unless specified
> otherwise. Definitions are given to clarify how existing expression
> operations, CFI operations, and attributes behave with respect to generalized
> location descriptions that support multiple places.
>
> > NOTE: Notes are included to describe how the changes are to be applied to
> > the DWARF Version 5 standard. They also describe rational and issues that
> > may need further consideration.

## A.2 General Description

### A.2.5 DWARF Expressions

> NOTE: This section, and its nested sections, replaces DWARF Version 5 section
> 2.5 and section 2.6. It is based on the text of the existing DWARF Version 5
> standard.

DWARF expressions describe how to compute a value or specify a location.

<i>The evaluation of a DWARF expression can provide the location of an object,
the value of an array bound, the length of a dynamic string, the desired value
itself, and so on.</i>

If the evaluation of a DWARF expression does not encounter an error, then it can
either result in a value (see [2.5.2 DWARF Expression
Value](#dwarf-expression-value)) or a location description (see [2.5.3 DWARF
Location Description](#dwarf-location-description)). When a DWARF expression
is evaluated, it may be specified whether a value or location description is
required as the result kind.

If a result kind is specified, and the result of the evaluation does not match
the specified result kind, then the implicit conversions described in
[2.5.4.4.3 Memory Location Description Operations](#memory-location-description-operations)
are performed if valid. Otherwise, the DWARF expression is ill-formed.

If the evaluation of a DWARF expression encounters an evaluation error, then the
result is an evaluation error.

> NOTE: Decided to define the concept of an evaluation error. An alternative is
> to introduce an undefined value base type in a similar way to location
> descriptions having an undefined location description. Then operations that
> encounter an evaluation error can return the undefined location description or
> value with an undefined base type.
>
> All operations that act on values would return an undefined entity if given an
> undefined value. The expression would then always evaluate to completion, and
> can be tested to determine if it is an undefined entity.
>
> However, this would add considerable additional complexity and does not match
> that GDB throws an exception when these evaluation errors occur.

If a DWARF expression is ill-formed, then the result is undefined.

The following sections detail the rules for when a DWARF expression is
ill-formed or results in an evaluation error.

A DWARF expression can either be encoded as an operation expression (see [2.5.4
DWARF Operation Expressions](#dwarf-operation-expressions)), or as a
location list expression (see [2.5.5 DWARF Location List
Expressions](#dwarf-location-list-expressions)).

#### A.2.5.1 DWARF Expression Evaluation Context

A DWARF expression is evaluated in a context that can include a number of
context elements. If multiple context elements are specified then they must be
self consistent or the result of the evaluation is undefined. The context
elements that can be specified are:

1.  <i>A current result kind</i>

    The kind of result required by the DWARF expression evaluation. If specified
    it can be a location description or a value.

2.  <i>A current thread</i>

    The target architecture thread identifier of the source program thread of
    execution for which a user presented expression is currently being
    evaluated.

    It is required for operations that are related to target architecture
    threads.

    <i>For example, the `DW_OP_regval_type` operation.</i>

3.  <i>A current call frame</i>

    The target architecture call frame identifier. It identifies a call frame
    that corresponds to an active invocation of a subprogram in the current
    thread. It is identified by its address on the call stack. The address is
    referred to as the Canonical Frame Address (CFA). The call frame information
    is used to determine the CFA for the call frames of the current thread's
    call stack (see [6.4 Call Frame Information](#call-frame-information)).

    It is required for operations that specify target architecture registers to
    support virtual unwinding of the call stack.

    <i>For example, the `DW_OP_*reg*` operations.</i>

    If specified, it must be an active call frame in the current thread.
    Otherwise the result is undefined.

    If it is the currently executing call frame, then it is termed the top call
    frame.

4.  <i>A current program location</i>

    The target architecture program location corresponding to the current call
    frame of the current thread.

    The program location of the top call frame is the target architecture
    program counter for the current thread. The call frame information is used
    to obtain the value of the return address register to determine the program
    location of the other call frames (see [6.4 Call Frame
    Information](#call-frame-information)).

    It is required for the evaluation of location list expressions to select
    amongst multiple program location ranges. It is required for operations that
    specify target architecture registers to support virtual unwinding of the
    call stack (see [6.4 Call Frame Information](#call-frame-information)).

    If specified:

    - If the current call frame is the top call frame, it must be the current
      target architecture program location.
    - If the current call frame F is not the top call frame, it must be the
      program location associated with the call site in the current caller frame
      F that invoked the callee frame.
    - Otherwise the result is undefined.

5.  <i>A current compilation unit</i>

    The compilation unit debug information entry that contains the DWARF
    expression being evaluated.

    It is required for operations that reference debug information associated
    with the same compilation unit, including indicating if such references use
    the 32-bit or 64-bit DWARF format. It can also provide the default address
    space address size if no current target architecture is specified.

    <i>For example, the `DW_OP_constx` and `DW_OP_addrx` operations.</i>

    <i>Note that this compilation unit may not be the same as the compilation
    unit determined from the loaded code object corresponding to the current
    program location. For example, the evaluation of the expression E associated
    with a `DW_AT_location` attribute of the debug information entry operand of
    the `DW_OP_call*` operations is evaluated with the compilation unit that
    contains E and not the one that contains the `DW_OP_call*` operation
    expression.</i>

6.  <i>A current target architecture</i>

    The target architecture.

    It is required for operations that specify target architecture specific
    entities.

    <i>For example, target architecture specific entities include DWARF register
    identifiers, DWARF address space identifiers, the default address space, and
    the address space address sizes.</i>

    If specified:

    - If the current frame is specified, then the current target architecture
      must be the same as the target architecture of the current frame.

    - If the current frame is specified and is the top frame, and if the current
      thread is specified, then the current target architecture must be the same
      as the target architecture of the current thread.

    - If the current compilation unit is specified, then the current target
      architecture default address space address size must be the same as the
      `address_size` field in the header of the current compilation unit and any
      associated entry in the `.debug_aranges` section.
    - If the current program location is specified, then the current target
      architecture must be the same as the target architecture of any line
      number information entry (see [6.2 Line Number
      Information](#line-number-information)) corresponding to the current
      program location.
    - If the current program location is specified, then the current target
      architecture default address space address size must be the same as the
      `address_size` field in the header of any entry corresponding to the
      current program location in the `.debug_addr`, `.debug_line`,
      `.debug_rnglists`, `.debug_rnglists.dwo`, `.debug_loclists`, and
      `.debug_loclists.dwo` sections.
    - Otherwise the result is undefined.

7.  <i>A current object</i>

    The location description of a program object.

    It is required for the `DW_OP_push_object_address` operation.

    <i>For example, the `DW_AT_data_location` attribute on type debug
    information entries specifies the program object corresponding to a runtime
    descriptor as the current object when it evaluates its associated
    expression.</i>

    The result is undefined if the location description is invalid (see [2.5.3
    DWARF Location Description](#dwarf-location-description)).

8.  <i>An initial stack</i>

    This is a list of values or location descriptions that will be pushed on the
    operation expression evaluation stack in the order provided before
    evaluation of an operation expression starts.

    Some debugger information entries have attributes that evaluate their DWARF
    expression value with initial stack entries. In all other cases the initial
    stack is empty.

    The result is undefined if any location descriptions are invalid (see [2.5.3
    DWARF Location Description](#dwarf-location-description)).

If the evaluation requires a context element that is not specified, then the
result of the evaluation is an error.

<i>A DWARF expression for a location description may be able to be evaluated
without a thread, call frame, program location, or architecture context. For
example, the location of a global variable may be able to be evaluated without
such context. If the expression evaluates with an error then it may indicate the
variable has been optimized and so requires more context.</i>

<i>The DWARF expression for call frame information (see [6.4 Call Frame
Information](#call-frame-information)) operations are restricted to those
that do not require the compilation unit context to be specified.</i>

The DWARF is ill-formed if all the `address_size` fields in the headers of all
the entries in the `.debug_info`, `.debug_addr`, `.debug_line`,
`.debug_rnglists`, `.debug_rnglists.dwo`, `.debug_loclists`, and
`.debug_loclists.dwo` sections corresponding to any given program location do
not match.

#### A.2.5.2 DWARF Expression Value

A value has a type and a literal value. It can represent a literal value of any
supported base type of the target architecture. The base type specifies the
size, encoding, and endianity of the literal value.

> NOTE: It may be desirable to add an implicit pointer base type encoding. It
> would be used for the type of the value that is produced when the
> `DW_OP_deref*` operation retrieves the full contents of an implicit pointer
> location storage created by the `DW_OP_implicit_pointer` operation. The
> literal value would record the debugging information entry and byte
> displacement specified by the associated `DW_OP_implicit_pointer` operation.

There is a distinguished base type termed the generic type, which is an integral
type that has the size of an address in the target architecture default address
space, a target architecture defined endianity, and unspecified signedness.

<i>The generic type is the same as the unspecified type used for stack
operations defined in DWARF Version 4 and before.</i>

An integral type is a base type that has an encoding of `DW_ATE_signed`,
`DW_ATE_signed_char`, `DW_ATE_unsigned`, `DW_ATE_unsigned_char`,
`DW_ATE_boolean`, or any target architecture defined integral encoding in the
inclusive range `DW_ATE_lo_user` to `DW_ATE_hi_user`.

> NOTE: It is unclear if `DW_ATE_address` is an integral type. GDB does not seem
> to consider it as integral.

#### A.2.5.3 DWARF Location Description

<i>Debugging information must provide consumers a way to find the location of
program variables, determine the bounds of dynamic arrays and strings, and
possibly to find the base address of a subprogram's call frame or the return
address of a subprogram. Furthermore, to meet the needs of recent computer
architectures and optimization techniques, debugging information must be able to
describe the location of an object whose location changes over the object's
lifetime, and may reside at multiple locations simultaneously during parts of an
object's lifetime.</i>

Information about the location of program objects is provided by location
descriptions.

Location descriptions can consist of one or more single location descriptions.

A single location description specifies the location storage that holds a
program object and a position within the location storage where the program
object starts. The position within the location storage is expressed as a bit
offset relative to the start of the location storage.

A location storage is a linear stream of bits that can hold values. Each
location storage has a size in bits and can be accessed using a zero-based bit
offset. The ordering of bits within a location storage uses the bit numbering
and direction conventions that are appropriate to the current language on the
target architecture.

There are five kinds of location storage:

1.  <i>memory location storage</i>

    Corresponds to the target architecture memory address spaces.

2.  <i>register location storage</i>

    Corresponds to the target architecture registers.

3.  <i>implicit location storage</i>

    Corresponds to fixed values that can only be read.

4.  <i>undefined location storage</i>

    Indicates no value is available and therefore cannot be read or written.

5.  <i>composite location storage</i>

    Allows a mixture of these where some bits come from one location storage and
    some from another location storage, or from disjoint parts of the same
    location storage.

> NOTE: It may be better to add an implicit pointer location storage kind used
> by the `DW_OP_implicit_pointer` operation. It would specify the debugger
> information entry and byte offset provided by the operations.

<i>Location descriptions are a language independent representation of addressing
rules.</i>

- <i>They can be the result of evaluating a debugger information entry attribute
  that specifies an operation expression of arbitrary complexity. In this usage
  they can describe the location of an object as long as its lifetime is either
  static or the same as the lexical block (see [3.5 Lexical Block
  Entries](#lexical-block-entries)) that owns it, and it does not move during
  its lifetime.</i>

- <i>They can be the result of evaluating a debugger information entry attribute
  that specifies a location list expression. In this usage they can describe the
  location of an object that has a limited lifetime, changes its location during
  its lifetime, or has multiple locations over part or all of its lifetime.</i>

If a location description has more than one single location description, the
DWARF expression is ill-formed if the object value held in each single location
description's position within the associated location storage is not the same
value, except for the parts of the value that are uninitialized.

<i>A location description that has more than one single location description can
only be created by a location list expression that has overlapping program
location ranges, or certain expression operations that act on a location
description that has more than one single location description. There are no
operation expression operations that can directly create a location description
with more than one single location description.</i>

<i>A location description with more than one single location description can be
used to describe objects that reside in more than one piece of storage at the
same time. An object may have more than one location as a result of
optimization. For example, a value that is only read may be promoted from memory
to a register for some region of code, but later code may revert to reading the
value from memory as the register may be used for other purposes. For the code
region where the value is in a register, any change to the object value must be
made in both the register and the memory so both regions of code will read the
updated value.</i>

<i>A consumer of a location description with more than one single location
description can read the object's value from any of the single location
descriptions (since they all refer to location storage that has the same value),
but must write any changed value to all the single location descriptions.</i>

Updating a location description L by a bit offset B is defined as adding the
value of B to the bit offset of each single location description SL of L. It is
an evaluation error if the updated bit offset of any SL is less than 0 or
greater than or equal to the size of the location storage specified by SL.

The evaluation of an expression may require context elements to create a
location description. If such a location description is accessed, the storage it
denotes is that associated with the context element values specified when the
location description was created, which may differ from the context at the time
it is accessed.

<i>For example, creating a register location description requires the thread
context: the location storage is for the specified register of that thread.
Creating a memory location description for an address space may required a
thread context: the location storage is the memory associated with that
thread.</i>

If any of the context elements required to create a location description change,
the location description becomes invalid and accessing it is undefined.

<i>Examples of context that can invalidate a location description are:</i>

- <i>The thread context is required and execution causes the thread to
  terminate.</i>
- <i>The call frame context is required and further execution causes the call
  frame to return to the calling frame.</i>
- <i>The program location is required and further execution of the thread
  occurs. That could change the location list entry or call frame information
  entry that applies.</i>
- <i>An operation uses call frame information:</i>
  - <i>Any of the frames used in the virtual call frame unwinding return.</i>
  - <i>The top call frame is used, the program location is used to select the
    call frame information entry, and further execution of the thread
    occurs.</i>

<i>A DWARF expression can be used to compute a location description for an
object. A subsequent DWARF expression evaluation can be given the object
location description as the object context or initial stack context to compute a
component of the object. The final result is undefined if the object location
description becomes invalid between the two expression evaluations.</i>

A change of a thread's program location may not make a location description
invalid, yet may still render it as no longer meaningful. Accessing such a
location description, or using it as the object context or initial stack context
of an expression evaluation, may produce an undefined result.

<i>For example, a location description may specify a register that no longer
holds the intended program object after a program location change. One way to
avoid such problems is to recompute location descriptions associated with
threads when their program locations change.</i>

#### A.2.5.4 DWARF Operation Expressions

An operation expression is comprised of a stream of operations, each consisting
of an opcode followed by zero or more operands. The number of operands is
implied by the opcode.

Operations represent a postfix operation on a simple stack machine. Each stack
entry can hold either a value or a location description. Operations can act on
entries on the stack, including adding entries and removing entries. If the kind
of a stack entry does not match the kind required by the operation and is not
implicitly convertible to the required kind
(see [2.5.4.4.3 Memory Location Description Operations](#memory-location-description-operations)),
then the DWARF operation expression is ill-formed.

Evaluation of an operation expression starts with an empty stack on which the
entries from the initial stack provided by the context are pushed in the order
provided. Then the operations are evaluated, starting with the first operation
of the stream. Evaluation continues until either an operation has an evaluation
error, or until one past the last operation of the stream is reached.

The result of the evaluation is:

- If an operation has an evaluation error, or an operation evaluates an
  expression that has an evaluation error, then the result is an evaluation
  error.
- If the current result kind specifies a location description, then:
  - If the stack is empty, the result is a location description with one
    undefined location description.

    <i>This rule is for backwards compatibility with DWARF Version 5 which uses
    an empty operation expression for this purpose.</i>

  - If the top stack entry is a location description, or can be converted to one
    (see [2.5.4.4.3 Memory Location Description Operations](#memory-location-description-operations)),
    then the result is that, possibly converted, location description. Any other entries on the
    stack are discarded.
  - Otherwise the DWARF expression is ill-formed.

    > NOTE: Could define this case as returning an implicit location description
    > as if the `DW_OP_implicit` operation is performed.

- If the current result kind specifies a value, then:
  - If the top stack entry is a value, or can be converted to one (see
    [2.5.4.4.3 Memory Location Description Operations](#memory-location-description-operations)),
    then the result is that, possibly converted, value. Any other entries on the stack are
    discarded.
  - Otherwise the DWARF expression is ill-formed.
- If the current result kind is not specified, then:
  - If the stack is empty, the result is a location description with one
    undefined location description.

    <i>This rule is for backwards compatibility with DWARF Version 5 which uses
    an empty operation expression for this purpose.</i>

    > NOTE: This rule is consistent with the rule above for when a location
    > description is requested. However, GDB appears to report this as an error
    > and no GDB tests appear to cause an empty stack for this case.

  - Otherwise, the top stack entry is returned. Any other entries on the stack
    are discarded.

An operation expression is encoded as a byte block with some form of prefix that
specifies the byte count. It can be used:

- as the value of a debugging information entry attribute that is encoded using
  class `exprloc` (see [7.5.5 Classes and Forms](#classes-and-forms)),
- as the operand to certain operation expression operations,
- as the operand to certain call frame information operations (see [6.4 Call
  Frame Information](#call-frame-information)),
- and in location list entries (see [2.5.5 DWARF Location List
  Expressions](#dwarf-location-list-expressions)).

##### A.2.5.4.1 Stack Operations

> NOTE: This section replaces DWARF Version 5 section 2.5.1.3.

The following operations manipulate the DWARF stack. Operations that index the
stack assume that the top of the stack (most recently added entry) has index 0.
They allow the stack entries to be either a value or location description.

If any stack entry accessed by a stack operation is an incomplete composite
location description (see [2.5.4.4.6 Composite Location Description Operations]
(#composite-location-description-operations)), then the DWARF expression is ill-formed.

> NOTE: These operations now support stack entries that are values and location
> descriptions.

> NOTE: If it is desired to also make them work with incomplete composite
> location descriptions, then would need to define that the composite location
> storage specified by the incomplete composite location description is also
> replicated when a copy is pushed. This ensures that each copy of the
> incomplete composite location description can update the composite location
> storage they specify independently.

1.  `DW_OP_dup`

    `DW_OP_dup` duplicates the stack entry at the top of the stack.

2.  `DW_OP_drop`

    `DW_OP_drop` pops the stack entry at the top of the stack and discards it.

3.  `DW_OP_pick`

    `DW_OP_pick` has a single unsigned 1-byte operand that represents an index
    I.  A copy of the stack entry with index I is pushed onto the stack.

4.  `DW_OP_over`

    `DW_OP_over` pushes a copy of the entry with index 1.

    <i>This is equivalent to a `DW_OP_pick 1` operation.</i>

5.  `DW_OP_swap`

    `DW_OP_swap` swaps the top two stack entries. The entry at the top of the
    stack becomes the second stack entry, and the second stack entry becomes the
    top of the stack.

6.  `DW_OP_rot`

    `DW_OP_rot` rotates the first three stack entries. The entry at the top of
    the stack becomes the third stack entry, the second entry becomes the top of
    the stack, and the third entry becomes the second entry.

<i>Examples illustrating many of these stack operations are found in Appendix
D.1.2 on page 289.</i>

##### A.2.5.4.2 Control Flow Operations

> NOTE: This section replaces DWARF Version 5 section 2.5.1.5.

The following operations provide simple control of the flow of a DWARF operation
expression.

1.  `DW_OP_nop`

    `DW_OP_nop` is a place holder. It has no effect on the DWARF stack entries.

2.  `DW_OP_le`, `DW_OP_ge`, `DW_OP_eq`, `DW_OP_lt`, `DW_OP_gt`,
    `DW_OP_ne`

    > NOTE: The same as in DWARF Version 5 section 2.5.1.5.

3.  `DW_OP_skip`

    `DW_OP_skip` is an unconditional branch. Its single operand is a 2-byte
    signed integer constant. The 2-byte constant is the number of bytes of the
    DWARF expression to skip forward or backward from the current operation,
    beginning after the 2-byte constant.

    If the updated position is at one past the end of the last operation, then
    the operation expression evaluation is complete.

    Otherwise, the DWARF expression is ill-formed if the updated operation
    position is not in the range of the first to last operation inclusive, or
    not at the start of an operation.

4.  `DW_OP_bra`

    `DW_OP_bra` is a conditional branch. Its single operand is a 2-byte signed
    integer constant. This operation pops the top of stack. If the value popped
    is not the constant 0, the 2-byte constant operand is the number of bytes of
    the DWARF operation expression to skip forward or backward from the current
    operation, beginning after the 2-byte constant.

    If the updated position is at one past the end of the last operation, then
    the operation expression evaluation is complete.

    Otherwise, the DWARF expression is ill-formed if the updated operation
    position is not in the range of the first to last operation inclusive, or
    not at the start of an operation.

5.  `DW_OP_call2, DW_OP_call4, DW_OP_call_ref`

    `DW_OP_call2`, `DW_OP_call4`, and `DW_OP_call_ref` perform DWARF procedure
    calls during evaluation of a DWARF operation expression.

    `DW_OP_call2` and `DW_OP_call4`, have one operand that is, respectively, a
    2-byte or 4-byte unsigned offset DR that represents the byte offset of a
    debugging information entry D relative to the beginning of the current
    compilation unit.

    `DW_OP_call_ref` has one operand that is a 4-byte unsigned value in the
    32-bit DWARF format, or an 8-byte unsigned value in the 64-bit DWARF format,
    that represents the byte offset DR of a debugging information entry D
    relative to the beginning of the `.debug_info` section that contains the
    current compilation unit. D may not be in the current compilation unit.

    > NOTE: DWARF Version 5 states that DR can be an offset in a `.debug_info`
    > section other than the one that contains the current compilation unit. It
    > states that relocation of references from one executable or shared object
    > file to another must be performed by the consumer. But given that DR is
    > defined as an offset in a `.debug_info` section this seems impossible. If
    > DR was defined as an implementation defined value, then the consumer could
    > choose to interpret the value in an implementation defined manner to
    > reference a debug information in another executable or shared object.
    >
    > In ELF the `.debug_info` section is in a non-`PT_LOAD` segment so standard
    > dynamic relocations cannot be used. But even if they were loaded segments
    > and dynamic relocations were used, DR would need to be the address of D,
    > not an offset in a `.debug_info` section. That would also need DR to be
    > the size of a global address. So it would not be possible to use the
    > 32-bit DWARF format in a 64-bit global address space. In addition, the
    > consumer would need to determine what executable or shared object the
    > relocated address was in so it could determine the containing compilation
    > unit.
    >
    > GDB only interprets DR as an offset in the `.debug_info` section that
    > contains the current compilation unit.
    >
    > This comment also applies to `DW_OP_implicit_pointer`.

    <i>Operand interpretation of `DW_OP_call2`, `DW_OP_call4`, and
    `DW_OP_call_ref` is exactly like that for `DW_FORM_ref2`, `DW_FORM_ref4`,
    and `DW_FORM_ref_addr`, respectively.</i>

    The call operation is evaluated by:

    - If D has a `DW_AT_location` attribute that is encoded as a `exprloc` that
      specifies an operation expression E, then execution of the current
      operation expression continues from the first operation of E. Execution
      continues until one past the last operation of E is reached, at which
      point execution continues with the operation following the call operation.
      The operations of E are evaluated with the same current context, except
      current compilation unit is the one that contains D and the stack is the
      same as that being used by the call operation. After the call operation
      has been evaluated, the stack is therefore as it is left by the evaluation
      of the operations of E. Since E is evaluated on the same stack as the call
      operation, E can use, and/or remove entries already on the stack, and can
      add new entries to the stack.

      <i>Values on the stack at the time of the call may be used as parameters
      by the called expression and values left on the stack by the called
      expression may be used as return values by prior agreement between the
      calling and called expressions.</i>

    - If D has a `DW_AT_location` attribute that is encoded as a `loclist` or
      `loclistsptr`, then the specified location list expression E is evaluated.
      The evaluation of E uses the current context, except the result kind is a
      location description, the compilation unit is the one that contains D, and
      the initial stack is empty. The location description result is pushed on
      the stack.

      > NOTE: This rule avoids having to define how to execute a matched
      > location list entry operation expression on the same stack as the call
      > when there are multiple matches. But it allows the call to obtain the
      > location description for a variable or formal parameter which may use a
      > location list expression.
      >
      > An alternative is to treat the case when D has a `DW_AT_location`
      > attribute that is encoded as a `loclist` or `loclistsptr`, and the
      > specified location list expression E' matches a single location list
      > entry with operation expression E, the same as the `exprloc` case and
      > evaluate on the same stack.
      >
      > But this is not attractive as if the attribute is for a variable that
      > happens to end with a non-singleton stack, it will not simply put a
      > location description on the stack. Presumably the intent of using
      > `DW_OP_call*` on a variable or formal parameter debugger information
      > entry is to push just one location description on the stack. That
      > location description may have more than one single location description.
      >
      > The previous rule for `exprloc` also has the same problem, as normally a
      > variable or formal parameter location expression may leave multiple
      > entries on the stack and only return the top entry.
      >
      > GDB implements `DW_OP_call*` by always executing E on the same stack. If
      > the location list has multiple matching entries, it simply picks the
      > first one and ignores the rest. This seems fundamentally at odds with
      > the desire to support multiple places for variables.
      >
      > So, it feels like `DW_OP_call*` should both support pushing a location
      > description on the stack for a variable or formal parameter, and also
      > support being able to execute an operation expression on the same stack.
      > Being able to specify a different operation expression for different
      > program locations seems a desirable feature to retain.
      >
      > A solution to that is to have a distinct `DW_AT_proc` attribute for the
      > `DW_TAG_dwarf_procedure` debugging information entry. Then the
      > `DW_AT_location` attribute expression is always executed separately and
      > pushes a location description (that may have multiple single location
      > descriptions), and the `DW_AT_proc` attribute expression is always
      > executed on the same stack and can leave anything on the stack.
      >
      > The `DW_AT_proc` attribute could have the new classes `exprproc`,
      > `loclistproc`, and `loclistsptrproc` to indicate that the expression is
      > executed on the same stack. `exprproc` is the same encoding as
      > `exprloc`. `loclistproc` and `loclistsptrproc` are the same encoding as
      > their non-`proc` counterparts, except the DWARF is ill-formed if the
      > location list does not match exactly one location list entry and a
      > default entry is required. These forms indicate explicitly that the
      > matched single operation expression must be executed on the same stack.
      > This is better than ad hoc special rules for `loclistproc` and
      > `loclistsptrproc` which are currently clearly defined to always return a
      > location description. The producer then explicitly indicates the intent
      > through the attribute classes.
      >
      > Such a change would be a breaking change for how GDB implements
      > `DW_OP_call*`. However, are the breaking cases actually occurring in
      > practice? GDB could implement the current approach for DWARF Version 5,
      > and the new semantics for DWARF Version 6 which has been done for some
      > other features.
      >
      > Another option is to limit the execution to be on the same stack only to
      > the evaluation of an expression E that is the value of a
      > `DW_AT_location` attribute of a `DW_TAG_dwarf_procedure` debugging
      > information entry. The DWARF would be ill-formed if E is a location list
      > expression that does not match exactly one location list entry. In all
      > other cases the evaluation of an expression E that is the value of a
      > `DW_AT_location` attribute would evaluate E with the current context,
      > except the result kind is a location description, the compilation unit
      > is the one that contains D, and the initial stack is empty. The location
      > description result is pushed on the stack.

    - If D has a `DW_AT_const_value` attribute with a value V, then it is as if
      a `DW_OP_implicit_value V` operation was executed.

      <i>This allows a call operation to be used to compute the location
      description for any variable or formal parameter regardless of whether the
      producer has optimized it to a constant. This is consistent with the
      `DW_OP_implicit_pointer` operation.</i>

      > NOTE: Alternatively, could deprecate using `DW_AT_const_value` for
      > `DW_TAG_variable` and `DW_TAG_formal_parameter` debugger information
      > entries that are constants and instead use `DW_AT_location` with an
      > operation expression that results in a location description with one
      > implicit location description. Then this rule would not be required.

    - Otherwise, there is no effect and no changes are made to the stack.

      > NOTE: In DWARF Version 5, if D does not have a `DW_AT_location` then
      > `DW_OP_call*` is defined to have no effect. It is unclear that this is
      > the right definition as a producer should be able to rely on using
      > `DW_OP_call*` to get a location description for any
      > non-`DW_TAG_dwarf_procedure` debugging information entries. Also, the
      > producer should not be creating DWARF with `DW_OP_call*` to a
      > `DW_TAG_dwarf_procedure` that does not have a `DW_AT_location`
      > attribute. So, should this case be defined as an ill-formed DWARF
      > expression?

    <i>The `DW_TAG_dwarf_procedure` debugging information entry can be used to
    define DWARF procedures that can be called.</i>

##### A.2.5.4.3 Value Operations

This section describes the operations that push values on the stack.

Each value stack entry has a type and a literal value. It can represent a
literal value of any supported base type of the target architecture. The base
type specifies the size, encoding, and endianity of the literal value.

The base type of value stack entries can be the distinguished generic type.

###### A.2.5.4.3.1 Literal Operations

> NOTE: This section replaces DWARF Version 5 section 2.5.1.1.

The following operations all push a literal value onto the DWARF stack.

Operations other than `DW_OP_const_type` push a value V with the generic type.
If V is larger than the generic type, then V is truncated to the generic type
size and the low-order bits used.

1.  `DW_OP_lit0`, `DW_OP_lit1`, ..., `DW_OP_lit31`

    `DW_OP_lit<N>` operations encode an unsigned literal value N from 0 through
    31, inclusive. They push the value N with the generic type.

2.  `DW_OP_const1u`, `DW_OP_const2u`, `DW_OP_const4u`, `DW_OP_const8u`

    `DW_OP_const<N>u` operations have a single operand that is a 1, 2, 4, or
    8-byte unsigned integer constant U, respectively. They push the value U with
    the generic type.

3.  `DW_OP_const1s`, `DW_OP_const2s`, `DW_OP_const4s`, `DW_OP_const8s`

    `DW_OP_const<N>s` operations have a single operand that is a 1, 2, 4, or
    8-byte signed integer constant S, respectively. They push the value S with
    the generic type.

4.  `DW_OP_constu`

    `DW_OP_constu` has a single unsigned LEB128 integer operand N. It pushes the
    value N with the generic type.

5.  `DW_OP_consts`

    `DW_OP_consts` has a single signed LEB128 integer operand N. It pushes the
    value N with the generic type.

6.  `DW_OP_constx`

    `DW_OP_constx` has a single unsigned LEB128 integer operand that represents
    a zero-based index into the `.debug_addr` section relative to the value of
    the `DW_AT_addr_base` attribute of the associated compilation unit. The
    value N in the `.debug_addr` section has the size of the generic type. It
    pushes the value N with the generic type.

    <i>The `DW_OP_constx` operation is provided for constants that require
    link-time relocation but should not be interpreted by the consumer as a
    relocatable address (for example, offsets to thread-local storage).</i>

7.  `DW_OP_const_type`

    `DW_OP_const_type` has three operands. The first is an unsigned LEB128
    integer DR that represents the byte offset of a debugging information entry
    D relative to the beginning of the current compilation unit, that provides
    the type T of the constant value. The second is a 1-byte unsigned integral
    constant S. The third is a block of bytes B, with a length equal to S.

    TS is the bit size of the type T. The least significant TS bits of B are
    interpreted as a value V of the type D. It pushes the value V with the type
    D.

    The DWARF is ill-formed if D is not a `DW_TAG_base_type` debugging
    information entry in the current compilation unit, or if TS divided by 8
    (the byte size) and rounded up to a whole number is not equal to S.

    <i>While the size of the byte block B can be inferred from the type D
    definition, it is encoded explicitly into the operation so that the
    operation can be parsed easily without reference to the `.debug_info`
    section.</i>

###### A.2.5.4.3.2 Arithmetic and Logical Operations

> NOTE: This section is the same as DWARF Version 5 section 2.5.1.4.

###### A.2.5.4.3.3 Type Conversion Operations

> NOTE: This section is the same as DWARF Version 5 section 2.5.1.6.

###### A.2.5.4.3.4 Special Value Operations

> NOTE: This section replaces parts of DWARF Version 5 sections 2.5.1.2,
  2.5.1.3, and 2.5.1.7.

There are these special value operations currently defined:

1.  `DW_OP_regval_type`

    `DW_OP_regval_type` has two operands. The first is an unsigned LEB128
    integer that represents a register number R. The second is an unsigned
    LEB128 integer DR that represents the byte offset of a debugging information
    entry D relative to the beginning of the current compilation unit, that
    provides the type T of the register value.

    The operation is equivalent to performing `DW_OP_regx R; DW_OP_deref_type
    DR`.

    > NOTE: Should DWARF allow the type T to be a larger size than the size of
    > the register R? Restricting a larger bit size avoids any issue of
    > conversion as the, possibly truncated, bit contents of the register is
    > simply interpreted as a value of T. If a conversion is wanted it can be
    > done explicitly using a `DW_OP_convert` operation.
    >
    > GDB has a per register hook that allows a target specific conversion on a
    > register by register basis. It defaults to truncation of bigger registers.
    > Removing use of the target hook does not cause any test failures in common
    > architectures. If the compiler for a target architecture did want some
    > form of conversion, including a larger result type, it could always
    > explicitly use the `DW_OP_convert` operation.
    >
    > If T is a larger type than the register size, then the default GDB
    > register hook reads bytes from the next register (or reads out of bounds
    > for the last register!). Removing use of the target hook does not cause
    > any test failures in common architectures (except an illegal hand written
    > assembly test). If a target architecture requires this behavior, these
    > extensions allow a composite location description to be used to combine
    > multiple registers.

2.  `DW_OP_deref`

    S is the bit size of the generic type divided by 8 (the byte size) and
    rounded up to a whole number. DR is the offset of a hypothetical debug
    information entry D in the current compilation unit for a base type of the
    generic type.

    The operation is equivalent to performing `DW_OP_deref_type S, DR`.

3.  `DW_OP_deref_size`

    `DW_OP_deref_size` has a single 1-byte unsigned integral constant that
    represents a byte result size S.

    TS is the smaller of the generic type bit size and S scaled by 8 (the byte
    size). If TS is smaller than the generic type bit size then T is an unsigned
    integral type of bit size TS, otherwise T is the generic type. DR is the
    offset of a hypothetical debug information entry D in the current
    compilation unit for a base type T.

    > NOTE: Truncating the value when S is larger than the generic type matches
    > what GDB does. This allows the generic type size to not be an integral
    > byte size. It does allow S to be arbitrarily large. Should S be restricted
    > to the size of the generic type rounded up to a multiple of 8?

    The operation is equivalent to performing `DW_OP_deref_type S, DR`, except
    if T is not the generic type, the value V pushed is zero-extended to the
    generic type bit size and its type changed to the generic type.

4.  `DW_OP_deref_type`

    `DW_OP_deref_type` has two operands. The first is a 1-byte unsigned integral
    constant S. The second is an unsigned LEB128 integer DR that represents the
    byte offset of a debugging information entry D relative to the beginning of
    the current compilation unit, that provides the type T of the result value.

    TS is the bit size of the type T.

    <i>While the size of the pushed value V can be inferred from the type T, it
    is encoded explicitly as the operand S so that the operation can be parsed
    easily without reference to the `.debug_info` section.</i>

    > NOTE: It is unclear why the operand S is needed. Unlike
    > `DW_OP_const_type`, the size is not needed for parsing. Any evaluation
    > needs to get the base type T to push with the value to know its encoding
    > and bit size.

    It pops one stack entry that must be a location description L.

    A value V of TS bits is retrieved from the location storage LS specified by
    one of the single location descriptions SL of L.

    <i>If L, or the location description of any composite location description
    part that is a subcomponent of L, has more than one single location
    description, then any one of them can be selected as they are required to
    all have the same value. For any single location description SL, bits are
    retrieved from the associated storage location starting at the bit offset
    specified by SL. For a composite location description, the retrieved bits
    are the concatenation of the N bits from each composite location part PL,
    where N is limited to the size of PL.</i>

    V is pushed on the stack with the type T.

    > NOTE: This definition makes it an evaluation error if L is a register
    > location description that has less than TS bits remaining in the register
    > storage. Particularly since these extensions extend location descriptions
    > to have a bit offset, it would be odd to define this as performing sign
    > extension based on the type, or be target architecture dependent, as the
    > number of remaining bits could be any number. This matches the GDB
    > implementation for `DW_OP_deref_type`.
    >
    > These extensions define `DW_OP_*breg*` in terms of `DW_OP_regval_type`.
    > `DW_OP_regval_type` is defined in terms of `DW_OP_regx`, which uses a 0
    > bit offset, and `DW_OP_deref_type`. Therefore, it requires the register
    > size to be greater or equal to the address size of the address space. This
    > matches the GDB implementation for `DW_OP_*breg*`.

    The DWARF is ill-formed if D is not in the current compilation unit, D is
    not a `DW_TAG_base_type` debugging information entry, or if TS divided by 8
    (the byte size) and rounded up to a whole number is not equal to S.

    > NOTE: This definition allows the base type to be a bit size since there
    > seems no reason to restrict it.

    It is an evaluation error if any bit of the value is retrieved from the
    undefined location storage or the offset of any bit exceeds the size of the
    location storage LS specified by any single location description SL of L.

    See [2.5.4.4.5 Implicit Location Description Operations](#implicit-location-description-operations)
    for special rules concerning implicit location descriptions created by the
    `DW_OP_implicit_pointer` operation.

5.  `DW_OP_xderef`

    `DW_OP_xderef` pops two stack entries. The first must be an integral type
    value that represents an address A. The second must be an integral type
    value that represents a target architecture specific address space
    identifier AS.

    The address size S is defined as the address bit size of the target
    architecture specific address space that corresponds to AS.

    A is adjusted to S bits by zero extending if necessary, and then treating
    the least significant S bits as an unsigned value A'.

    It creates a location description L with one memory location description SL.
    SL specifies the memory location storage LS that corresponds to AS with a
    bit offset equal to A' scaled by 8 (the byte size).

    If AS is an address space that is specific to context elements, then LS
    corresponds to the location storage associated with the current context.

    <i>For example, if AS is for per thread storage then LS is the location
    storage for the current thread. Therefore, if L is accessed by an operation,
    the location storage selected when the location description was created is
    accessed, and not the location storage associated with the current context
    of the access operation.</i>

    The DWARF expression is ill-formed if AS is not one of the values defined by
    the target architecture specific `DW_ASPACE_*` values.

    The operation is equivalent to popping A and AS, pushing L, and then
    performing `DW_OP_deref`. The value V retrieved is left on the stack with
    the generic type.

6.  `DW_OP_xderef_size`

    `DW_OP_xderef_size` has a single 1-byte unsigned integral constant that
    represents a byte result size S.

    It pops two stack entries. The first must be an integral type value
    that represents an address A. The second must be an integral type
    value that represents a target architecture specific address space
    identifier AS.

    It creates a location description L as described for `DW_OP_xderef`.

    The operation is equivalent to popping A and AS, pushing L, and then
    performing `DW_OP_deref_size S` . The zero-extended value V retrieved is
    left on the stack with the generic type.

7.  `DW_OP_xderef_type`

    `DW_OP_xderef_type` has two operands. The first is a 1-byte unsigned
    integral constant S. The second operand is an unsigned LEB128 integer DR
    that represents the byte offset of a debugging information entry D relative
    to the beginning of the current compilation unit, that provides the type T
    of the result value.

    It pops two stack entries. The first must be an integral type value that
    represents an address A. The second must be an integral type value that
    represents a target architecture specific address space identifier AS.

    It creates a location description L as described for `DW_OP_xderef`.

    The operation is equivalent to popping A and AS, pushing L, and then
    performing `DW_OP_deref_type DR` . The value V retrieved is left on the
    stack with the type T.

8.  `DW_OP_entry_value` <i>Deprecated</i>

    `DW_OP_entry_value` pushes the value of an expression that is evaluated in
    the context of the calling frame.

    <i>It may be used to determine the value of arguments on entry to the
    current call frame provided they are not clobbered.</i>

    It has two operands. The first is an unsigned LEB128 integer S. The second
    is a block of bytes, with a length equal S, interpreted as a DWARF operation
    expression E.

    E is evaluated with the current context, except the result kind is
    unspecified, the call frame is the one that called the current frame, the
    program location is the call site in the calling frame, the object is
    unspecified, and the initial stack is empty. The calling frame information
    is obtained by virtually unwinding the current call frame using the call
    frame information (see [6.4 Call Frame
    Information](#call-frame-information)).

    If the result of E is a location description L (see [2.5.4.4.4 Register
    Location Description
    Operations](#register-location-description-operations)), and the last
    operation executed by E is a `DW_OP_reg*` for register R with a target
    architecture specific base type of T, then the contents of the register are
    retrieved as if a `DW_OP_deref_type DR` operation was performed where DR is
    the offset of a hypothetical debug information entry in the current
    compilation unit for T. The resulting value V is pushed on the stack.

    <i>Using `DW_OP_reg*` provides a more compact form for the case where the
    value was in a register on entry to the subprogram.</i>

    > NOTE: It is unclear how this provides a more compact expression, as
    > `DW_OP_regval_type` could be used which is marginally larger.

    If the result of E is a value V, then V is pushed on the stack.

    Otherwise, the DWARF expression is ill-formed.

    <i>The `DW_OP_entry_value` operation is deprecated as its main usage is
    provided by other means. DWARF Version 5 added the
    `DW_TAG_call_site_parameter` debugger information entry for call sites that
    has `DW_AT_call_value`, `DW_AT_call_data_location`, and
    `DW_AT_call_data_value` attributes that provide DWARF expressions to compute
    actual parameter values at the time of the call, and requires the producer
    to ensure the expressions are valid to evaluate even when virtually
    unwound.</i>

    > NOTE: GDB only implements `DW_OP_entry_value` when E is exactly
    > `DW_OP_reg*` or `DW_OP_breg*; DW_OP_deref*`.

##### A.2.5.4.4 Location Description Operations

This section describes the operations that push location descriptions on the
stack.

###### A.2.5.4.4.1 General Location Description Operations

> NOTE: This section replaces part of DWARF Version 5 section 2.5.1.3.

1.  `DW_OP_push_object_address`

    `DW_OP_push_object_address` pushes the location description L of the current
    object.

    <i>This object may correspond to an independent variable that is part of a
    user presented expression that is being evaluated. The object location
    description may be determined from the variable's own debugging information
    entry or it may be a component of an array, structure, or class whose
    address has been dynamically determined by an earlier step during user
    expression evaluation.</i>

    <i>This operation provides explicit functionality (especially for arrays
    involving descriptors) that is analogous to the implicit push of the base
    location description of a structure prior to evaluation of a
    `DW_AT_data_member_location` to access a data member of a structure.</i>

    > NOTE: This operation could be removed and the object location description
    > specified as the initial stack as for `DW_AT_data_member_location`.
    >
    > Or this operation could be used instead of needing to specify an initial
    > stack. The latter approach is more composable as access to the object may
    > be needed at any point of the expression, and passing it as the initial
    > stack requires the entire expression to be aware where on the stack it is.
    > If this were done, ``DW_AT_use_location`` would require a
    > ``DW_OP_push_object2_address`` operation for the second object.
    >
    > Or a more general way to pass an arbitrary number of arguments in and an
    > operation to get the Nth one such as ``DW_OP_arg N``. A vector of
    > arguments would then be passed in the expression context rather than an
    > initial stack. This could also resolve the issues with ``DW_OP_call*`` by
    > allowing a specific number of arguments passed in and returned to be
    > specified. The ``DW_OP_call*`` operation could then always execute on a
    > separate stack: the number of arguments would be specified in a new call
    > operation and taken from the callers stack, and similarly the number of
    > return results specified and copied from the called stack back to the
    > callee stack when the called expression was complete.
    >
    > The only attribute that specifies a current object is
    > `DW_AT_data_location` so the non-normative text seems to overstate how
    > this is being used. Or are there other attributes that need to state they
    > pass an object?

###### A.2.5.4.4.2 Undefined Location Description Operations

> NOTE: This section replaces DWARF Version 5 section 2.6.1.1.1.

<i>The undefined location storage represents a piece or all of an object that is
present in the source but not in the object code (perhaps due to optimization).
Neither reading nor writing to the undefined location storage is meaningful.</i>

An undefined location description specifies the undefined location storage.
There is no concept of the size of the undefined location storage, nor of a bit
offset for an undefined location description. The `DW_OP_*piece` operations can
implicitly specify an undefined location description, allowing any size and
offset to be specified, and results in a part with all undefined bits.

###### A.2.5.4.4.3 Memory Location Description Operations

> NOTE: This section replaces parts of DWARF Version 5 section 2.5.1.1, 2.5.1.2,
> 2.5.1.3, and 2.6.1.1.2.

Each of the target architecture specific address spaces has a corresponding
memory location storage that denotes the linear addressable memory of that
address space. The size of each memory location storage corresponds to the range
of the addresses in the corresponding address space.

<i>It is target architecture defined how address space location storage maps to
target architecture physical memory. For example, they may be independent
memory, or more than one location storage may alias the same physical memory
possibly at different offsets and with different interleaving. The mapping may
also be dictated by the source language address classes.</i>

A memory location description specifies a memory location storage. The bit
offset corresponds to a bit position within a byte of the memory. Bits accessed
using a memory location description, access the corresponding target
architecture memory starting at the bit position within the byte specified by
the bit offset.

A memory location description that has a bit offset that is a multiple of 8 (the
byte size) is defined to be a byte address memory location description. It has a
memory byte address A that is equal to the bit offset divided by 8.

A memory location description that does not have a bit offset that is a multiple
of 8 (the byte size) is defined to be a bit field memory location description.
It has a bit position B equal to the bit offset modulo 8, and a memory byte
address A equal to the bit offset minus B that is then divided by 8.

The address space AS of a memory location description is defined to be the
address space that corresponds to the memory location storage associated with
the memory location description.

A location description that is comprised of one byte address memory location
description SL is defined to be a memory byte address location description. It
has a byte address equal to A and an address space equal to AS of the
corresponding SL.

`DW_ASPACE_none` is defined as the target architecture default address space.

If a stack entry is required to be a location description, but it is a value V
with the generic type, then it is implicitly converted to a location description
L with one memory location description SL. SL specifies the memory location
storage that corresponds to the target architecture default address space with a
bit offset equal to V scaled by 8 (the byte size).

> NOTE: If it is wanted to allow any integral type value to be implicitly
> converted to a memory location description in the target architecture default
> address space:
>
> > If a stack entry is required to be a location description, but is a value V
> > with an integral type, then it is implicitly converted to a location
> > description L with a one memory location description SL. If the type size of
> > V is less than the generic type size, then the value V is zero extended to
> > the size of the generic type. The least significant generic type size bits
> > are treated as an unsigned value to be used as an address A. SL specifies
> > memory location storage corresponding to the target architecture default
> > address space with a bit offset equal to A scaled by 8 (the byte size).
>
> The implicit conversion could also be defined as target architecture specific.
> For example, GDB checks if V is an integral type. If it is not it gives an
> error. Otherwise, GDB zero-extends V to 64 bits. If the GDB target defines a
> hook function, then it is called. The target specific hook function can modify
> the 64-bit value, possibly sign extending based on the original value type.
> Finally, GDB treats the 64-bit value V as a memory location address.

If a stack entry is required to be a location description, but it is an implicit
pointer value IPV with the target architecture default address space, then it is
implicitly converted to a location description with one single location
description specified by IPV. See
[2.5.4.4.5 Implicit Location Description Operations](#implicit-location-description-operations).

If a stack entry is required to be a value, but it is a location description L
with one memory location description SL in the target architecture default
address space with a bit offset B that is a multiple of 8, then it is implicitly
converted to a value equal to B divided by 8 (the byte size) with the generic
type.

1.  `DW_OP_addr`

    `DW_OP_addr` has a single byte constant value operand, which has the size of
    the generic type, that represents an address A.

    It pushes a location description L with one memory location description SL
    on the stack. SL specifies the memory location storage corresponding to the
    target architecture default address space with a bit offset equal to A
    scaled by 8 (the byte size).

    <i>If the DWARF is part of a code object, then A may need to be relocated.
    For example, in the ELF code object format, A must be adjusted by the
    difference between the ELF segment virtual address and the virtual address
    at which the segment is loaded.</i>

2.  `DW_OP_addrx`

    `DW_OP_addrx` has a single unsigned LEB128 integer operand that represents a
    zero-based index into the `.debug_addr` section relative to the value of the
    `DW_AT_addr_base` attribute of the associated compilation unit. The address
    value A in the `.debug_addr` section has the size of the generic type.

    It pushes a location description L with one memory location description SL
    on the stack. SL specifies the memory location storage corresponding to the
    target architecture default address space with a bit offset equal to A
    scaled by 8 (the byte size).

    <i>If the DWARF is part of a code object, then A may need to be relocated.
    For example, in the ELF code object format, A must be adjusted by the
    difference between the ELF segment virtual address and the virtual address
    at which the segment is loaded.</i>

3.  `DW_OP_form_tls_address`

    `DW_OP_form_tls_address` pops one stack entry that must be an integral type
    value and treats it as a thread-local storage address TA.

    It pushes a location description L with one memory location description SL
    on the stack. SL is the target architecture specific memory location
    description that corresponds to the thread-local storage address TA.

    The meaning of the thread-local storage address TA is defined by the
    run-time environment. If the run-time environment supports multiple
    thread-local storage blocks for a single thread, then the block
    corresponding to the executable or shared library containing this DWARF
    expression is used.

    <i>Some implementations of C, C++, Fortran, and other languages, support a
    thread-local storage class. Variables with this storage class have distinct
    values and addresses in distinct threads, much as automatic variables have
    distinct values and addresses in each subprogram invocation. Typically,
    there is a single block of storage containing all thread-local variables
    declared in the main executable, and a separate block for the variables
    declared in each shared library. Each thread-local variable can then be
    accessed in its block using an identifier. This identifier is typically a
    byte offset into the block and pushed onto the DWARF stack by one of the
    `DW_OP_const*` operations prior to the `DW_OP_form_tls_address` operation.
    Computing the address of the appropriate block can be complex (in some
    cases, the compiler emits a function call to do it), and difficult to
    describe using ordinary DWARF location descriptions. Instead of forcing
    complex thread-local storage calculations into the DWARF expressions, the
    `DW_OP_form_tls_address` allows the consumer to perform the computation
    based on the target architecture specific run-time environment.</i>

4.  `DW_OP_call_frame_cfa`

    `DW_OP_call_frame_cfa` pushes the location description L of the Canonical
    Frame Address (CFA) of the current subprogram, obtained from the call frame
    information on the stack. See [6.4 Call Frame
    Information](#call-frame-information).

    <i>Although the value of the `DW_AT_frame_base` attribute of the debugger
    information entry corresponding to the current subprogram can be computed
    using a location list expression, in some cases this would require an
    extensive location list because the values of the registers used in
    computing the CFA change during a subprogram execution. If the call frame
    information is present, then it already encodes such changes, and it is
    space efficient to reference that using the `DW_OP_call_frame_cfa`
    operation.</i>

5.  `DW_OP_fbreg`

    `DW_OP_fbreg` has a single signed LEB128 integer operand that represents a
    byte displacement B.

    The location description L for the <i>frame base</i> of the current
    subprogram is obtained from the `DW_AT_frame_base` attribute of the debugger
    information entry corresponding to the current subprogram as described in
    [3.3.5 Low-Level Information](#low-level-information).

    The location description L is updated by bit offset B scaled by 8 (the byte
    size) and pushed on the stack.

6.  `DW_OP_breg0`, `DW_OP_breg1`, ..., `DW_OP_breg31`

    The `DW_OP_breg<N>` operations encode the numbers of up to 32 registers,
    numbered from 0 through 31, inclusive. The register number R corresponds to
    the N in the operation name.

    They have a single signed LEB128 integer operand that represents a byte
    displacement B.

    The address space identifier AS is defined as the one corresponding to the
    target architecture specific default address space.

    The address size S is defined as the address bit size of the target
    architecture specific address space corresponding to AS.

    The contents of the register specified by R are retrieved as if a
    `DW_OP_regval_type R, DR` operation was performed where DR is the offset of
    a hypothetical debug information entry in the current compilation unit for
    an unsigned integral base type of size S bits. B is added and the least
    significant S bits are treated as an unsigned value to be used as an address
    A.

    They push a location description L comprising one memory location
    description LS on the stack. LS specifies the memory location storage that
    corresponds to AS with a bit offset equal to A scaled by 8 (the byte size).

7.  `DW_OP_bregx`

    `DW_OP_bregx` has two operands. The first is an unsigned LEB128 integer that
    represents a register number R. The second is a signed LEB128 integer that
    represents a byte displacement B.

    The action is the same as for `DW_OP_breg<N>`, except that R is used as the
    register number and B is used as the byte displacement.

###### A.2.5.4.4.4 Register Location Description Operations

> NOTE: This section replaces DWARF Version 5 section 2.6.1.1.3.

There is a register location storage that corresponds to each of the target
architecture registers. The size of each register location storage corresponds
to the size of the corresponding target architecture register.

A register location description specifies a register location storage. The bit
offset corresponds to a bit position within the register. Bits accessed using a
register location description access the corresponding target architecture
register starting at the specified bit offset.

1.  `DW_OP_reg0`, `DW_OP_reg1`, ..., `DW_OP_reg31`

    `DW_OP_reg<N>` operations encode the numbers of up to 32 registers, numbered
    from 0 through 31, inclusive. The target architecture register number R
    corresponds to the N in the operation name.

    The operation is equivalent to performing `DW_OP_regx R`.

2.  `DW_OP_regx`

    `DW_OP_regx` has a single unsigned LEB128 integer operand that represents a
    target architecture register number R.

    If the current call frame is the top call frame, it pushes a location
    description L that specifies one register location description SL on the
    stack. SL specifies the register location storage that corresponds to R with
    a bit offset of 0 for the current thread.

    If the current call frame is not the top call frame, call frame information
    (see [6.4 Call Frame Information](#call-frame-information)) is used to
    determine the location description that holds the register for the current
    call frame and current program location of the current thread. The resulting
    location description L is pushed.

    <i>Note that if call frame information is used, the resulting location
    description may be register, memory, or undefined.</i>

    <i>An implementation may evaluate the call frame information immediately, or
    may defer evaluation until L is accessed by an operation. If evaluation is
    deferred, R and the current context can be recorded in L. When accessed, the
    recorded context is used to evaluate the call frame information, not the
    current context of the access operation.</i>

<i>These operations obtain a register location. To fetch the contents of a
register, it is necessary to use `DW_OP_regval_type`, use one of the
`DW_OP_breg*` register-based addressing operations, or use `DW_OP_deref*` on a
register location description.</i>

###### A.2.5.4.4.5 Implicit Location Description Operations

> NOTE: This section replaces DWARF Version 5 section 2.6.1.1.4.

Implicit location storage represents a piece or all of an object which has no
actual location in the program but whose contents are nonetheless known, either
as a constant or can be computed from other locations and values in the program.

An implicit location description specifies an implicit location storage. The bit
offset corresponds to a bit position within the implicit location storage. Bits
accessed using an implicit location description, access the corresponding
implicit storage value starting at the bit offset.

1.  `DW_OP_implicit_value`

    `DW_OP_implicit_value` has two operands. The first is an unsigned LEB128
    integer that represents a byte size S. The second is a block of bytes with a
    length equal to S treated as a literal value V.

    An implicit location storage LS is created with the literal value V and a
    size of S.

    It pushes location description L with one implicit location description SL
    on the stack. SL specifies LS with a bit offset of 0.

2.  `DW_OP_stack_value`

    `DW_OP_stack_value` pops one stack entry that must be a value V.

    An implicit location storage LS is created with the literal value V using
    the size, encoding, and endianity specified by V's base type.

    It pushes a location description L with one implicit location description SL
    on the stack. SL specifies LS with a bit offset of 0.

    <i>The `DW_OP_stack_value` operation specifies that the object does not
    exist in memory, but its value is nonetheless known. In this form, the
    location description specifies the actual value of the object, rather than
    specifying the memory or register storage that holds the value.</i>

    See `DW_OP_implicit_pointer` (following) for special rules concerning
    implicit pointer values produced by dereferencing implicit location
    descriptions created by the `DW_OP_implicit_pointer` operation.

    Note: Since location descriptions are allowed on the stack, the
    `DW_OP_stack_value` operation no longer terminates the DWARF operation
    expression execution as in DWARF Version 5.

3.  `DW_OP_implicit_pointer`

    <i>An optimizing compiler may eliminate a pointer, while still retaining the
    value that the pointer addressed. `DW_OP_implicit_pointer` allows a producer
    to describe this value.</i>

    <i>`DW_OP_implicit_pointer` specifies an object is a pointer to the target
    architecture default address space that cannot be represented as a real
    pointer, even though the value it would point to can be described. In this
    form, the location description specifies a debugging information entry that
    represents the actual location description of the object to which the
    pointer would point. Thus, a consumer of the debug information would be able
    to access the dereferenced pointer, even when it cannot access the pointer
    itself.</i>

    `DW_OP_implicit_pointer` has two operands. The first operand is a 4-byte
    unsigned value in the 32-bit DWARF format, or an 8-byte unsigned value in
    the 64-bit DWARF format, that represents the byte offset DR of a debugging
    information entry D relative to the beginning of the `.debug_info` section
    that contains the current compilation unit. The second operand is a signed
    LEB128 integer that represents a byte displacement B.

    <i>Note that D might not be in the current compilation unit.</i>

    <i>The first operand interpretation is exactly like that for
    `DW_FORM_ref_addr`.</i>

    The address space identifier AS is defined as the one corresponding to the
    target architecture specific default address space.

    The address size S is defined as the address bit size of the target
    architecture specific address space corresponding to AS.

    An implicit location storage LS is created with the debugging information
    entry D, address space AS, and size of S.

    It pushes a location description L that comprises one implicit location
    description SL on the stack. SL specifies LS with a bit offset of 0.

    It is an evaluation error if a `DW_OP_deref*` operation pops a location
    description L', and retrieves S bits, such that any retrieved bits come from
    an implicit location storage that is the same as LS, unless both the
    following conditions are met:

    1.  All retrieved bits come from an implicit location description that
        refers to an implicit location storage that is the same as LS.

        <i>Note that all bits do not have to come from the same implicit
        location description, as L' may involve composite location
        descriptions.</i>

    2.  The bits come from consecutive ascending offsets within their respective
        implicit location storage.

    <i>These rules are equivalent to retrieving the complete contents of LS.</i>

    If both the above conditions are met, then the value V pushed by the
    `DW_OP_deref*` operation is an implicit pointer value IPV with a target
    architecture specific address space of AS, a debugging information entry of
    D, and a base type of T. If AS is the target architecture default address
    space, then T is the generic type. Otherwise, T is a target architecture
    specific integral type with a bit size equal to S.

    If IPV is either implicitly converted to a location description (only done
    if AS is the target architecture default address space), then the resulting
    location description RL is:

    - If D has a `DW_AT_location` attribute, the DWARF expression E from the
      `DW_AT_location` attribute is evaluated with the current context, except
      that the result kind is a location description, the compilation unit is
      the one that contains D, the object is unspecified, and the initial stack
      is empty. RL is the expression result.

      <i>Note that E is evaluated with the context of the expression accessing
      IPV, and not the context of the expression that contained the
      `DW_OP_implicit_pointer` operation that created L.</i>

    - If D has a `DW_AT_const_value` attribute, then an implicit location
      storage RLS is created from the `DW_AT_const_value` attribute's value with
      a size matching the size of the `DW_AT_const_value` attribute's value. RL
      comprises one implicit location description SRL. SRL specifies RLS with a
      bit offset of 0.

      > NOTE: If using `DW_AT_const_value` for variables and formal parameters
      > is deprecated and instead `DW_AT_location` is used with an implicit
      > location description, then this rule would not be required.

    - Otherwise, it is an evaluation error.

    The location description RL is updated by bit offset B scaled by 8 (the byte
    size).

    If a `DW_OP_stack_value` operation pops a value that is the same as IPV,
    then it pushes a location description that is the same as L.

    It is an evaluation error if LS or IPV is accessed in any other manner.

    <i>The restrictions on how an implicit pointer location description created
    by `DW_OP_implicit_pointer` can be used are to simplify the DWARF consumer.
    Similarly, for an implicit pointer value created by `DW_OP_deref*` and
    `DW_OP_stack_value`.</i>

<i>Typically a `DW_OP_implicit_pointer` operation is used in a DWARF expression
E<sub>1</sub> of a `DW_TAG_variable` or `DW_TAG_formal_parameter` debugging
information entry D<sub>1</sub>'s `DW_AT_location` attribute. The debugging
information entry referenced by the `DW_OP_implicit_pointer` operation is
typically itself a `DW_TAG_variable` or `DW_TAG_formal_parameter` debugging
information entry D<sub>2</sub> whose `DW_AT_location` attribute gives a second
DWARF expression E<sub>2</sub>.</i>

<i>D<sub>1</sub> and E<sub>1</sub> are describing the location of a pointer type
object. D<sub>2</sub> and E<sub>2</sub> are describing the location of the
object pointed to by that pointer object.</i>

<i>However, D<sub>2</sub> may be any debugging information entry that contains a
`DW_AT_location` or `DW_AT_const_value` attribute (for example,
`DW_TAG_dwarf_procedure`). By using E<sub>2</sub>, a consumer can reconstruct
the value of the object when asked to dereference the pointer described by
E<sub>1</sub> which contains the `DW_OP_implicit_pointer` operation.</i>

###### A.2.5.4.4.6 Composite Location Description Operations

> NOTE: This section replaces DWARF Version 5 section 2.6.1.2.

A composite location storage represents an object or value which may be
contained in part of another location storage or contained in parts of more than
one location storage.

Each part has a part location description L and a part bit size S. L can have
one or more single location descriptions SL. If there are more than one SL then
that indicates that part is located in more than one place. The bits of each
place of the part comprise S contiguous bits from the location storage LS
specified by SL starting at the bit offset specified by SL. All the bits must be
within the size of LS or the DWARF expression is ill-formed.

A composite location storage can have zero or more parts. The parts are
contiguous such that the zero-based location storage bit index will range over
each part with no gaps between them. Therefore, the size of a composite location
storage is the sum of the size of its parts. The DWARF expression is ill-formed
if the size of the contiguous location storage is larger than the size of the
memory location storage corresponding to the largest target architecture
specific address space.

A composite location description specifies a composite location storage. The bit
offset corresponds to a bit position within the composite location storage.

There are operations that create a composite location storage.

There are other operations that allow a composite location storage to be
incrementally created. Each part is created by a separate operation. There may
be one or more operations to create the final composite location storage. A
series of such operations describes the parts of the composite location storage
that are in the order that the associated part operations are executed.

To support incremental creation, a composite location storage can be in an
incomplete state. When an incremental operation operates on an incomplete
composite location storage, it adds a new part.

A composite location description that specifies a composite location storage
that is incomplete is termed an incomplete composite location description. A
composite location description that specifies a composite location storage that
is complete is termed a complete composite location description.

If the top stack entry is a location description that has one incomplete
composite location description SL after the execution of an operation expression
has completed, SL is converted to a complete composite location description.

<i>Note that this conversion does not happen after the completion of an
operation expression that is evaluated on the same stack by the `DW_OP_call*`
operations. Such executions are not a separate evaluation of an operation
expression, but rather the continued evaluation of the same operation expression
that contains the `DW_OP_call*` operation.</i>

If a stack entry is required to be a location description L, but L has an
incomplete composite location description, then the DWARF expression is
ill-formed. The exception is for the operations involved in incrementally
creating a composite location description as described below.

<i>Note that a DWARF operation expression may arbitrarily compose composite
location descriptions from any other location description, including those that
have multiple single location descriptions, and those that have composite
location descriptions.</i>

<i>The incremental composite location description operations are defined to be
compatible with the definitions in DWARF Version 5.</i>

1.  `DW_OP_piece`

    `DW_OP_piece` has a single unsigned LEB128 integer that represents a byte
    size S.

    The action is based on the context:

    - If the stack is empty, then a location description L comprised of one
      incomplete composite location description SL is pushed on the stack.

      An incomplete composite location storage LS is created with a single part
      P. P specifies a location description PL and has a bit size of S scaled by
      8 (the byte size). PL is comprised of one undefined location description
      PSL.

      SL specifies LS with a bit offset of 0.

    - Otherwise, if the top stack entry is a location description L comprised of
      one incomplete composite location description SL, then the incomplete
      composite location storage LS that SL specifies is updated to append a new
      part P. P specifies a location description PL and has a bit size of S
      scaled by 8 (the byte size). PL is comprised of one undefined location
      description PSL. L is left on the stack.
    - Otherwise, if the top stack entry is a location description or can be
      converted to one, then it is popped and treated as a part location
      description PL. Then:

      - If the top stack entry (after popping PL) is a location description L
        comprised of one incomplete composite location description SL, then the
        incomplete composite location storage LS that SL specifies is updated to
        append a new part P. P specifies the location description PL and has a
        bit size of S scaled by 8 (the byte size). L is left on the stack.
      - Otherwise, a location description L comprised of one
        incomplete composite location description SL is pushed on
        the stack.

        An incomplete composite location storage LS is created with a single
        part P. P specifies the location description PL and has a bit size of S
        scaled by 8 (the byte size).

        SL specifies LS with a bit offset of 0.

    - Otherwise, the DWARF expression is ill-formed

    <i>Many compilers store a single variable in sets of registers or store a
    variable partially in memory and partially in registers. `DW_OP_piece`
    provides a way of describing where a part of a variable is located.</i>

    <i>The evaluation rules for the `DW_OP_piece` operation allow it to be
    compatible with the DWARF Version 5 definition.</i>

    > NOTE: Since these extensions allow location descriptions to be entries on
    > the stack, a simpler operation to create composite location descriptions
    > could be defined. For example, just one operation that specifies how many
    > parts, and pops pairs of stack entries for the part size and location
    > description. Not only would this be a simpler operation and avoid the
    > complexities of incomplete composite location descriptions, but it may
    > also have a smaller encoding in practice. However, the desire for
    > compatibility with DWARF Version 5 is likely a stronger consideration.

2.  `DW_OP_bit_piece`

    `DW_OP_bit_piece` has two operands. The first is an unsigned LEB128 integer
    that represents the part bit size S. The second is an unsigned LEB128
    integer that represents a bit displacement B.

    The action is the same as for `DW_OP_piece`, except that any part created
    has the bit size S, and the location description PL of any created part is
    updated by a bit offset B.

    <i>`DW_OP_bit_piece` is used instead of `DW_OP_piece` when the piece to be
    assembled is not byte-sized or is not at the start of the part location
    description.</i>

#### A.2.5.5 DWARF Location List Expressions

> NOTE: This section replaces DWARF Version 5 section 2.6.2.

<i>To meet the needs of recent computer architectures and optimization
techniques, debugging information must be able to describe the location of an
object whose location changes over the object's lifetime, and may reside at
multiple locations during parts of an object's lifetime. Location list
expressions are used in place of operation expressions whenever the object whose
location is being described has these requirements.</i>

A location list expression consists of a series of location list entries. Each
location list entry is one of the following kinds:

1.  <i>Bounded location description</i>

    This kind of location list entry provides an operation expression that
    evaluates to the location description of an object that is valid over a
    lifetime bounded by a starting and ending address. The starting address is
    the lowest address of the address range over which the location is valid.
    The ending address is the address of the first location past the highest
    address of the address range.

    The location list entry matches when the current program location is within
    the given range.

    There are several kinds of bounded location description entries which differ
    in the way that they specify the starting and ending addresses.

2.  <i>Default location description</i>

    This kind of location list entry provides an operation expression that
    evaluates to the location description of an object that is valid when no
    bounded location description entry applies.

    The location list entry matches when the current program location is not
    within the range of any bounded location description entry.

3.  <i>Base address</i>

    This kind of location list entry provides an address to be used as the base
    address for beginning and ending address offsets given in certain kinds of
    bounded location description entries. The applicable base address of a
    bounded location description entry is the address specified by the closest
    preceding base address entry in the same location list. If there is no
    preceding base address entry, then the applicable base address defaults to
    the base address of the compilation unit (see DWARF Version 5 section
    3.1.1).

    In the case of a compilation unit where all of the machine code is contained
    in a single contiguous section, no base address entry is needed.

4.  <i>End-of-list</i>

    This kind of location list entry marks the end of the location list
    expression.

The address ranges defined by the bounded location description entries of a
location list expression may overlap. When they do, they describe a situation in
which an object exists simultaneously in more than one place.

If all of the address ranges in a given location list expression do not
collectively cover the entire range over which the object in question is
defined, and there is no following default location description entry, it is
assumed that the object is not available for the portion of the range that is
not covered.

The result of the evaluation of a DWARF location list expression is:

- If the current program location is not specified, then it is an evaluation
  error.

  > NOTE: If the location list only has a single default entry, should that be
  > considered a match if there is no program location? If there are non-default
  > entries then it seems it has to be an evaluation error when there is no
  > program location as that indicates the location depends on the program
  > location which is not known.

- If there are no matching location list entries, then the result is a location
  description that comprises one undefined location description.
- Otherwise, the operation expression E of each matching location list entry is
  evaluated with the current context, except that the result kind is a location
  description, the object is unspecified, and the initial stack is empty. The
  location list entry result is the location description returned by the
  evaluation of E.

  The result is a location description that is comprised of the union of the
  single location descriptions of the location description result of each
  matching location list entry.

A location list expression can only be used as the value of a debugger
information entry attribute that is encoded using class `loclist` or
`loclistsptr` (see [7.5.5 Classes and Forms](#classes-and-forms)). The value of
the attribute provides an index into a separate object file section called
`.debug_loclists` or `.debug_loclists.dwo` (for split DWARF object files) that
contains the location list entries.

A `DW_OP_call*` and `DW_OP_implicit_pointer` operation can be used to specify a
debugger information entry attribute that has a location list expression.
Several debugger information entry attributes allow DWARF expressions that are
evaluated with an initial stack that includes a location description that may
originate from the evaluation of a location list expression.

<i>This location list representation, the `loclist` and `loclistsptr` class, and
the related `DW_AT_loclists_base` attribute are new in DWARF Version 5. Together
they eliminate most, or all of the code object relocations previously needed for
location list expressions.</i>

> NOTE: The rest of this section is the same as DWARF Version 5 section 2.6.2.

## A.3 Program Scope Entries

> NOTE: This section provides changes to existing debugger information entry
> attributes. These would be incorporated into the corresponding DWARF Version 5
> chapter 3 sections.

### A.3.3 Subroutine and Entry Point Entries

#### A.3.3.5 Low-Level Information

1.  A `DW_TAG_subprogram`, `DW_TAG_inlined_subroutine`, or `DW_TAG_entry_point`
    debugger information entry may have a `DW_AT_return_addr` attribute, whose
    value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description L of the place where the return address for the current call
    frame's subprogram or entry point is stored.

    The DWARF is ill-formed if L is not comprised of one memory location
    description for one of the target architecture specific address spaces.

    > NOTE: It is unclear why `DW_TAG_inlined_subroutine` has a
    > `DW_AT_return_addr` attribute but not a `DW_AT_frame_base` or
    > `DW_AT_static_link` attribute. Seems it would either have all of them or
    > none. Since inlined subprograms do not have a call frame it seems they
    > would have none of these attributes.

2.  A `DW_TAG_subprogram` or `DW_TAG_entry_point` debugger information entry may
    have a `DW_AT_frame_base` attribute, whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any.

    The DWARF is ill-formed if E contains a `DW_OP_fbreg` operation, or the
    resulting location description L is not comprised of one single location
    description SL.

    If SL is a register location description for register R, then L is replaced
    with the result of evaluating a `DW_OP_bregx R, 0` operation. This computes
    the frame base memory location description in the target architecture
    default address space.

    <i>This allows the more compact `DW_OP_reg*` to be used instead of
    `DW_OP_breg* 0`.</i>

    > NOTE: This rule could be removed and require the producer to create the
    > required location description directly using `DW_OP_call_frame_cfa` or
    > `DW_OP_breg*`. This would also then allow a target to implement the call
    > frames within a large register.

    Otherwise, the DWARF is ill-formed if SL is not a memory location
    description in any of the target architecture specific address spaces.

    The resulting L is the <i>frame base</i> for the subprogram or entry point.

    <i>Typically, E will use the `DW_OP_call_frame_cfa` operation or be a stack
    pointer register plus or minus some offset.</i>

3.  If a `DW_TAG_subprogram` or `DW_TAG_entry_point` debugger information entry
    is lexically nested, it may have a `DW_AT_static_link` attribute, whose
    value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description L of the <i>canonical frame address</i> (see [6.4 Call Frame
    Information](#call-frame-information)) of the relevant call frame of the
    subprogram instance that immediately lexically encloses the current call
    frame's subprogram or entry point.

    The DWARF is ill-formed if L is is not comprised of one memory location
    description for one of the target architecture specific address spaces.

### A.3.4 Call Site Entries and Parameters

#### A.3.4.2 Call Site Parameters

1.  A `DW_TAG_call_site_parameter` debugger information entry may have a
    `DW_AT_call_value` attribute, whose value is a DWARF operation expression
    E<sub>1</sub>.

    The result of the `DW_AT_call_value` attribute is obtained by evaluating
    E<sub>1</sub> with a context that has a result kind of a value, an unspecified
    object, the compilation unit that contains E, an empty initial stack, and other
    context elements corresponding to the source language thread of execution upon
    which the user is focused, if any. The resulting value V<sub>1</sub> is the
    value of the parameter at the time of the call made by the call site.

    For parameters passed by reference, where the code passes a pointer to a
    location which contains the parameter, or for reference type parameters, the
    `DW_TAG_call_site_parameter` debugger information entry may also have a
    `DW_AT_call_data_location` attribute whose value is a DWARF operation expression
    E<sub>2</sub>, and a `DW_AT_call_data_value` attribute whose value is a DWARF
    operation expression E<sub>3</sub>.

    The value of the `DW_AT_call_data_location` attribute is obtained by evaluating
    E<sub>2</sub> with a context that has a result kind of a location description,
    an unspecified object, the compilation unit that contains E, an empty initial
    stack, and other context elements corresponding to the source language thread of
    execution upon which the user is focused, if any. The resulting location
    description L<sub>2</sub> is the location where the referenced parameter lives
    during the call made by the call site. If E<sub>2</sub> would just be a
    `DW_OP_push_object_address`, then the `DW_AT_call_data_location` attribute may
    be omitted.

    > NOTE: The DWARF Version 5 implies that `DW_OP_push_object_address` may be
    > used but does not state what object must be specified in the context.
    > Either `DW_OP_push_object_address` cannot be used, or the object to be
    > passed in the context must be defined.

    The value of the `DW_AT_call_data_value` attribute is obtained by evaluating
    E<sub>3</sub> with a context that has a result kind of a value, an unspecified
    object, the compilation unit that contains E, an empty initial stack, and other
    context elements corresponding to the source language thread of execution upon
    which the user is focused, if any. The resulting value V<sub>3</sub> is the
    value in L<sub>2</sub> at the time of the call made by the call site.

    The result of these attributes is undefined if the current call frame is not for
    the subprogram containing the `DW_TAG_call_site_parameter` debugger information
    entry or the current program location is not for the call site containing the
    `DW_TAG_call_site_parameter` debugger information entry in the current call
    frame.

    <i>The consumer may have to virtually unwind to the call site (see [6.4 Call
    Frame Information](#call-frame-information)) in order to evaluate these
    attributes. This will ensure the source language thread of execution upon which
    the user is focused corresponds to the call site needed to evaluate the
    expression.</i>

    If it is not possible to avoid the expressions of these attributes from
    accessing registers or memory locations that might be clobbered by the
    subprogram being called by the call site, then the associated attribute should
    not be provided.

    <i>The reason for the restriction is that the parameter may need to be accessed
    during the execution of the callee. The consumer may virtually unwind from the
    called subprogram back to the caller and then evaluate the attribute
    expressions. The call frame information (see [6.4 Call Frame
    Information](#call-frame-information)) will not be able to restore registers
    that have been clobbered, and clobbered memory will no longer have the value at
    the time of the call.</i>

### A.3.5 Lexical Block Entries

> NOTE: This section is the same as DWARF Version 5 section 3.5.

## A.4 Data Object and Object List Entries

> NOTE: This section provides changes to existing debugger information entry
> attributes. These would be incorporated into the corresponding DWARF Version 5
> chapter 4 sections.

### A.4.1 Data Object Entries

Program variables, formal parameters and constants are represented by debugging
information entries with the tags `DW_TAG_variable`, `DW_TAG_formal_parameter`
and `DW_TAG_constant`, respectively.

*The tag `DW_TAG_constant` is used for languages that have true named constants.*

The debugging information entry for a program variable, formal parameter or
constant may have the following attributes:

1.  A `DW_AT_location` attribute, whose value is a DWARF expression E that
    describes the location of a variable or parameter at run-time.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an empty initial stack, and other context
    elements corresponding to the source language thread of execution upon which
    the user is focused, if any. The result of the evaluation is the location
    description of the base of the data object.

    See [2.5.4.2 Control Flow Operations](#control-flow-operations) for special
    evaluation rules used by the `DW_OP_call*` operations.

    > NOTE: Delete the description of how the `DW_OP_call*` operations evaluate
    > a `DW_AT_location` attribute as that is now described in the operations.

    > NOTE: See the discussion about the `DW_AT_location` attribute in the
    > `DW_OP_call*` operation. Having each attribute only have a single purpose
    > and single execution semantics seems desirable. It makes it easier for the
    > consumer that no longer have to track the context. It makes it easier for
    > the producer as it can rely on a single semantics for each attribute.
    >
    > For that reason, limiting the `DW_AT_location` attribute to only
    > supporting evaluating the location description of an object, and using a
    > different attribute and encoding class for the evaluation of DWARF
    > expression <i>procedures</i> on the same operation expression stack seems
    > desirable.

2.  `DW_AT_const_value`

    > NOTE: Could deprecate using the `DW_AT_const_value` attribute for
    > `DW_TAG_variable` or `DW_TAG_formal_parameter` debugger information
    > entries that have been optimized to a constant. Instead, `DW_AT_location`
    > could be used with a DWARF expression that produces an implicit location
    > description now that any location description can be used within a DWARF
    > expression. This allows the `DW_OP_call*` operations to be used to push
    > the location description of any variable regardless of how it is
    > optimized.

### A.4.2 Common Block Entries

A common block entry also has a DW_AT_location attribute whose value is a DWARF
expression E that describes the location of the common block at run-time. The
result of the attribute is obtained by evaluating E with a context that has a
result kind of a location description, an unspecified object, the compilation
unit that contains E, an empty initial stack, and other context elements
corresponding to the source language thread of execution upon which the user is
focused, if any. The result of the evaluation is the location description of the
base of the common block. See 2.5.4.2 Control Flow Operations for special
evaluation rules used by the DW_OP_call* operations.

## A.5 Type Entries

> NOTE: This section provides changes to existing debugger information entry
> attributes. These would be incorporated into the corresponding DWARF Version 5
> chapter 5 sections.

### A.5.7 Structure, Union, Class and Interface Type Entries

#### A.5.7.3 Derived or Extended Structures, Classes and Interfaces

1.  For a `DW_AT_data_member_location` attribute there are two cases:

    1.  If the attribute is an integer constant B, it provides the offset in
        bytes from the beginning of the containing entity.

        The result of the attribute is obtained by updating the bit offset of
        the location description of the beginning of the containing entity by B
        scaled by 8 (the byte size). The result is the location description of
        the base of the member entry.

        <i>If the beginning of the containing entity is not byte aligned, then
        the beginning of the member entry has the same bit displacement within a
        byte.</i>

    2.  Otherwise, the attribute must be a DWARF expression E which is evaluated
        with a context that has a result kind of a location description, an
        unspecified object, the compilation unit that contains E, an initial
        stack comprising the location description of the beginning of the
        containing entity, and other context elements corresponding to the
        source language thread of execution upon which the user is focused, if
        any. The result of the evaluation is the location description of the
        base of the member entry.

    > NOTE: The beginning of the containing entity can now be any location
    > description, including those with more than one single location
    > description, and those with single location descriptions that are of any
    > kind and have any bit offset.

#### A.5.7.8 Member Function Entries

1.  An entry for a virtual function also has a `DW_AT_vtable_elem_location`
    attribute whose value is a DWARF expression E.

    The result of the attribute is obtained by evaluating E with a context that
    has a result kind of a location description, an unspecified object, the
    compilation unit that contains E, an initial stack comprising the location
    description of the object of the enclosing type, and other context elements
    corresponding to the source language thread of execution upon which the user
    is focused, if any. The result of the evaluation is the location description
    of the slot for the function within the virtual function table for the
    enclosing class.

### A.5.14 Pointer to Member Type Entries

1.  The `DW_TAG_ptr_to_member_type` debugging information entry has a
    `DW_AT_use_location` attribute whose value is a DWARF expression E. It is used
    to compute the location description of the member of the class to which the
    pointer to member entry points.

    <i>The method used to find the location description of a given member of a
    class, structure, or union is common to any instance of that class, structure,
    or union and to any instance of the pointer to member type. The method is thus
    associated with the pointer to member type, rather than with each object that
    has a pointer to member type.</i>

    The `DW_AT_use_location` DWARF expression is used in conjunction with the
    location description for a particular object of the given pointer to member type
    and for a particular structure or class instance.

    The result of the attribute is obtained by evaluating E with a context that has
    a result kind of a location description, an unspecified object, the compilation
    unit that contains E, an initial stack comprising two entries, and other context
    elements corresponding to the source language thread of execution upon which the
    user is focused, if any. The first stack entry is the value of the pointer to
    member object itself. The second stack entry is the location description of the
    base of the entire class, structure, or union instance containing the member
    whose location is being calculated. The result of the evaluation is the location
    description of the member of the class to which the pointer to member entry
    points.

### A.5.18 Dynamic Properties of Types

#### A.5.18.1 Data Location

1.  The `DW_AT_data_location` attribute may be used with any type that provides one
    or more levels of hidden indirection and/or run-time parameters in its
    representation. Its value is a DWARF operation expression E which computes the
    location description of the data for an object. When this attribute is omitted,
    the location description of the data is the same as the location description of
    the object.

    The result of the attribute is obtained by evaluating E with a context that has
    a result kind of a location description, an object that is the location
    description of the data descriptor, the compilation unit that contains E, an
    empty initial stack, and other context elements corresponding to the source
    language thread of execution upon which the user is focused, if any. The result
    of the evaluation is the location description of the base of the member entry.

    <i>E will typically involve an operation expression that begins with a
    `DW_OP_push_object_address` operation which loads the location description
    of the object which can then serve as a descriptor in subsequent
    calculation.</i>

    > NOTE: Since `DW_AT_data_member_location`, `DW_AT_use_location`, and
    > `DW_AT_vtable_elem_location` allow both operation expressions and location
    > list expressions, why does `DW_AT_data_location` not allow both? In all cases
    > they apply to data objects so less likely that optimization would cause
    > different operation expressions for different program location ranges. But if
    > supporting for some then should be for all.
    >
    > It seems odd this attribute is not the same as `DW_AT_data_member_location` in
    > having an initial stack with the location description of the object since the
    > expression has to need it.

## A.6 Other Debugging Information

> NOTE: This section provides changes to existing debugger information entry
> attributes. These would be incorporated into the corresponding DWARF Version 5
> chapter 6 sections.

### A.6.2 Line Number Information

> NOTE: This section is the same as DWARF Version 5 section 6.2.

### A.6.4 Call Frame Information

> NOTE: This section provides changes to DWARF Version 5 section 6.4. Register
> unwind DWARF expressions are generalized to allow any location description,
> including those with composite and implicit location descriptions.

#### A.6.4.1 Structure of Call Frame Information

The register rules are:

1.  <i>undefined</i>

    A register that has this rule has no recoverable value in the previous
    frame. The previous value of this register is the undefined location
    description (see [2.5.4.4.2 Undefined Location Description
    Operations](#undefined-location-description-operations)).

    <i>By convention, the register is not preserved by a callee.</i>

2.  <i>same value</i>

    This register has not been modified from the previous caller frame.

    If the current frame is the top frame, then the previous value of this
    register is the location description L that specifies one register location
    description SL. SL specifies the register location storage that corresponds
    to the register with a bit offset of 0 for the current thread.

    If the current frame is not the top frame, then the previous value of this
    register is the location description obtained using the call frame
    information for the callee frame and callee program location invoked by the
    current caller frame for the same register.

    <i>By convention, the register is preserved by the callee, but the callee
    has not modified it.</i>

3.  <i>offset(N)</i>

    N is a signed byte offset. The previous value of this register is saved at
    the location description L. Where L is the location description of the
    current CFA (see [2.5.4 DWARF Operation
    Expressions](#dwarf-operation-expressions)) updated with the bit offset N
    scaled by 8 (the byte size).

4.  <i>val_offset(N)</i>

    N is a signed byte offset. The previous value of this register is the memory
    byte address of the location description L. Where L is the location
    description of the current CFA (see [2.5.4 DWARF Operation
    Expressions](#dwarf-operation-expressions)) updated with the bit offset N
    scaled by 8 (the byte size).

    The DWARF is ill-formed if the CFA location description is not a memory byte
    address location description, or if the register size does not match the
    size of an address in the target architecture default address space.

    <i>Since the CFA location description is required to be a memory byte
    address location description, the value of val_offset(N) will also be a
    memory byte address location description since it is offsetting the CFA
    location description by N bytes. Furthermore, the value of val_offset(N)
    will be a memory byte address in the target architecture default address
    space.</i>

    > NOTE: Should DWARF allow the address size to be a different size to the
    > size of the register? Requiring them to be the same bit size avoids any
    > issue of conversion as the bit contents of the register is simply
    > interpreted as a value of the address.
    >
    > GDB has a per register hook that allows a target specific conversion on a
    > register by register basis. It defaults to truncation of bigger registers,
    > and to actually reading bytes from the next register (or reads out of
    > bounds for the last register) for smaller registers. There are no GDB
    > tests that read a register out of bounds (except an illegal hand written
    > assembly test).

5.  <i>register(R)</i>

    This register has been stored in another register numbered R.

    The previous value of this register is the location description obtained
    using the call frame information for the current frame and current program
    location for register R.

    The DWARF is ill-formed if the size of this register does not match the size
    of register R or if there is a cyclic dependency in the call frame
    information.

    > NOTE: Should this also allow R to be larger than this register? If so is
    > the value stored in the low order bits and it is undefined what is stored
    > in the extra upper bits?

6.  <i>expression(E)</i>

    The previous value of this register is located at the location description
    produced by evaluating the DWARF operation expression E (see [2.5.4 DWARF
    Operation Expressions](#dwarf-operation-expressions)).

    E is evaluated with the current context, except the result kind is a
    location description, the compilation unit is unspecified, the object is
    unspecified, and an initial stack comprising the location description of the
    current CFA (see [2.5.4 DWARF Operation
    Expressions](#dwarf-operation-expressions)).

7.  <i>val_expression(E)</i>

    The previous value of this register is located at the implicit location
    description created from the value produced by evaluating the DWARF
    operation expression E (see [2.5.4 DWARF Operation
    Expressions](#dwarf-operation-expressions)).

    E is evaluated with the current context, except the result kind is a value,
    the compilation unit is unspecified, the object is unspecified, and an
    initial stack comprising the location description of the current CFA (see
    [2.5.4 DWARF Operation Expressions](#dwarf-operation-expressions)).

    The DWARF is ill-formed if the resulting value type size does not match the
    register size.

    > NOTE: This has limited usefulness as the DWARF expression E can only
    > produce values up to the size of the generic type. This is due to not
    > allowing any operations that specify a type in a CFI operation expression.
    > This makes it unusable for registers that are larger than the generic
    > type. However, <i>expression(E)</i> can be used to create an implicit
    > location description of any size.

8.  <i>architectural</i>

    The rule is defined externally to this specification by the augmenter.

A Common Information Entry (CIE) holds information that is shared among many
Frame Description Entries (FDE). There is at least one CIE in every non-empty
`.debug_frame` section. A CIE contains the following fields, in order:

1.  `length` (initial length)

    A constant that gives the number of bytes of the CIE structure, not
    including the length field itself. The size of the length field plus the
    value of length must be an integral multiple of the address size specified
    in the `address_size` field.

2.  `CIE_id` (4 or 8 bytes, see [7.4 32-Bit and 64-Bit DWARF Formats](#bit-and-64-bit-dwarf-formats))

    A constant that is used to distinguish CIEs from FDEs.

    In the 32-bit DWARF format, the value of the CIE id in the CIE header is
    0xffffffff; in the 64-bit DWARF format, the value is 0xffffffffffffffff.

3.  `version` (ubyte)

    A version number. This number is specific to the call frame information and
    is independent of the DWARF version number.

    The value of the CIE version number is 4.

    > NOTE: Would this be increased to 5 to reflect the changes in these
    > extensions?

4.  `augmentation` (sequence of UTF-8 characters)

    A null-terminated UTF-8 string that identifies the augmentation to this CIE
    or to the FDEs that use it. If a reader encounters an augmentation string
    that is unexpected, then only the following fields can be read:

    - CIE: length, CIE_id, version, augmentation
    - FDE: length, CIE_pointer, initial_location, address_range

    If there is no augmentation, this value is a zero byte.

    <i>The augmentation string allows users to indicate that there is additional
    vendor and target architecture specific information in the CIE or FDE which
    is needed to virtually unwind a stack frame. For example, this might be
    information about dynamically allocated data which needs to be freed on exit
    from the routine.</i>

    <i>Because the `.debug_frame` section is useful independently of any
    `.debug_info` section, the augmentation string always uses UTF-8
    encoding.</i>

5.  `address_size` (ubyte)

    The size of a target address in this CIE and any FDEs that use it, in bytes.
    If a compilation unit exists for this frame, its address size must match the
    address size here.

6.  `segment_selector_size` (ubyte)

    The size of a segment selector in this CIE and any FDEs that use it, in
    bytes.

7.  `code_alignment_factor` (unsigned LEB128)

    A constant that is factored out of all advance location instructions (see
    [6.4.2.1 Row Creation Instructions](#row-creation-instructions)). The
    resulting value is `(operand * code_alignment_factor)`.

8.  `data_alignment_factor` (signed LEB128)

    A constant that is factored out of certain offset instructions (see [6.4.2.2
    CFA Definition Instructions](#cfa-definition-instructions) and [6.4.2.3
    Register Rule Instructions](#register-rule-instructions)). The
    resulting value is `(operand * data_alignment_factor)`.

9.  `return_address_register` (unsigned LEB128)

    An unsigned LEB128 constant that indicates which column in the rule table
    represents the return address of the subprogram. Note that this column might
    not correspond to an actual machine register.

    The value of the return address register is used to determine the program
    location of the caller frame. The program location of the top frame is the
    target architecture program counter value of the current thread.

10. `initial_instructions` (array of ubyte)

    A sequence of rules that are interpreted to create the initial setting of
    each column in the table.

    The default rule for all columns before interpretation of the initial
    instructions is the undefined rule. However, an ABI authoring body or a
    compilation system authoring body may specify an alternate default value for
    any or all columns.

11. `padding` (array of ubyte)

    Enough `DW_CFA_nop` instructions to make the size of this entry match the
    length value above.

An FDE contains the following fields, in order:

1.  `length` (initial length)

    A constant that gives the number of bytes of the header and instruction
    stream for this subprogram, not including the length field itself. The size
    of the length field plus the value of length must be an integral multiple of
    the address size.

2.  `CIE_pointer` (4 or 8 bytes, see [7.4 32-Bit and 64-Bit DWARF Formats](#bit-and-64-bit-dwarf-formats))

    A constant offset into the `.debug_frame` section that denotes the CIE that
    is associated with this FDE.

3.  `initial_location` (segment selector and target address)

    The address of the first location associated with this table entry. If the
    segment_selector_size field of this FDE's CIE is non-zero, the initial
    location is preceded by a segment selector of the given length.

4.  `address_range` (target address)

    The number of bytes of program instructions described by this entry.

5.  `instructions` (array of ubyte)

    A sequence of table defining instructions that are described in [6.4.2 Call
    Frame Instructions](#call-frame-instructions).

6.  `padding` (array of ubyte)

    Enough `DW_CFA_nop` instructions to make the size of this entry match the
    length value above.

#### A.6.4.2 Call Frame Instructions

Some call frame instructions have operands that are encoded as DWARF operation
expressions E (see [2.5.4 DWARF Operation
Expressions](#dwarf-operation-expressions)). The DWARF operations that can be
used in E have the following restrictions:

- `DW_OP_addrx`, `DW_OP_call2`, `DW_OP_call4`, `DW_OP_call_ref`,
  `DW_OP_const_type`, `DW_OP_constx`, `DW_OP_convert`, `DW_OP_deref_type`,
  `DW_OP_fbreg`, `DW_OP_implicit_pointer`, `DW_OP_regval_type`,
  `DW_OP_reinterpret`, and `DW_OP_xderef_type` operations are not allowed
  because the call frame information must not depend on other debug sections.
- `DW_OP_push_object_address` is not allowed because there is no object context
  to provide a value to push.
- `DW_OP_call_frame_cfa` and `DW_OP_entry_value` are not allowed because their
  use would be circular.

<i>Call frame instructions to which these restrictions apply include
`DW_CFA_def_cfa_expression`, `DW_CFA_expression`, and
`DW_CFA_val_expression`.</i>

##### A.6.4.2.1 Row Creation Instructions

> NOTE: These instructions are the same as in DWARF Version 5 section 6.4.2.1.

##### A.6.4.2.2 CFA Definition Instructions

1.  `DW_CFA_def_cfa`

    The `DW_CFA_def_cfa` instruction takes two unsigned LEB128 operands
    representing a register number R and a (non-factored) byte displacement B.
    The required action is to define the current CFA rule to be equivalent to
    the result of evaluating the DWARF operation expression `DW_OP_bregx R, B`
    as a location description.

2.  `DW_CFA_def_cfa_sf`

    The `DW_CFA_def_cfa_sf` instruction takes two operands: an unsigned LEB128
    value representing a register number R and a signed LEB128 factored byte
    displacement B. The required action is to define the current CFA rule to be
    equivalent to the result of evaluating the DWARF operation expression
    `DW_OP_bregx R, B * data_alignment_factor` as a location description.

    <i>The action is the same as `DW_CFA_def_cfa`, except that the second
    operand is signed and factored.</i>

3.  `DW_CFA_def_cfa_register`

    The `DW_CFA_def_cfa_register` instruction takes a single unsigned LEB128
    operand representing a register number R. The required action is to define
    the current CFA rule to be equivalent to the result of evaluating the DWARF
    operation expression `DW_OP_bregx R, B` as a location description. B is the
    old CFA byte displacement.

    If the subprogram has no current CFA rule, or the rule was defined by a
    `DW_CFA_def_cfa_expression` instruction, then the DWARF is ill-formed.

4.  `DW_CFA_def_cfa_offset`

    The `DW_CFA_def_cfa_offset` instruction takes a single unsigned LEB128
    operand representing a (non-factored) byte displacement B. The required
    action is to define the current CFA rule to be equivalent to the result of
    evaluating the DWARF operation expression `DW_OP_bregx R, B` as a location
    description. R is the old CFA register number.

    If the subprogram has no current CFA rule, or the rule was defined by a
    `DW_CFA_def_cfa_expression` instruction, then the DWARF is ill-formed.

5.  `DW_CFA_def_cfa_offset_sf`

    The `DW_CFA_def_cfa_offset_sf` instruction takes a signed LEB128 operand
    representing a factored byte displacement B. The required action is to
    define the current CFA rule to be equivalent to the result of evaluating the
    DWARF operation expression `DW_OP_bregx R, B * data_alignment_factor` as a
    location description. R is the old CFA register number.

    If the subprogram has no current CFA rule, or the rule was defined by a
    `DW_CFA_def_cfa_expression` instruction, then the DWARF is ill-formed.

    <i>The action is the same as `DW_CFA_def_cfa_offset`, except that the
    operand is signed and factored.</i>

6.  `DW_CFA_def_cfa_expression`

    The `DW_CFA_def_cfa_expression` instruction takes a single operand encoded
    as a `DW_FORM_exprloc` value representing a DWARF operation expression E.
    The required action is to define the current CFA rule to be equivalent to
    the result of evaluating E with the current context, except the result kind
    is a location description, the compilation unit is unspecified, the object
    is unspecified, and an empty initial stack.

    <i>See [6.4.2 Call Frame Instructions](#call-frame-instructions) regarding
    restrictions on the DWARF expression operations that can be used in E.</i>

    The DWARF is ill-formed if the result of evaluating E is not a memory byte
    address location description.

##### A.6.4.2.3 Register Rule Instructions

1.  `DW_CFA_undefined`

    The `DW_CFA_undefined` instruction takes a single unsigned LEB128 operand
    that represents a register number R. The required action is to set the rule
    for the register specified by R to `undefined`.

2.  `DW_CFA_same_value`

    The `DW_CFA_same_value` instruction takes a single unsigned LEB128 operand
    that represents a register number R. The required action is to set the rule
    for the register specified by R to `same value`.

3.  `DW_CFA_offset`

    The `DW_CFA_offset` instruction takes two operands: a register number R
    (encoded with the opcode) and an unsigned LEB128 constant representing a
    factored displacement B. The required action is to change the rule for the
    register specified by R to be an <i>offset(B * data_alignment_factor)</i>
    rule.

    > NOTE: Seems this should be named `DW_CFA_offset_uf` since the offset is
    > unsigned factored.

4.  `DW_CFA_offset_extended`

    The `DW_CFA_offset_extended` instruction takes two unsigned LEB128 operands
    representing a register number R and a factored displacement B. This
    instruction is identical to `DW_CFA_offset`, except for the encoding and
    size of the register operand.

    > NOTE: Seems this should be named `DW_CFA_offset_extended_uf` since the
    > displacement is unsigned factored.

5.  `DW_CFA_offset_extended_sf`

    The `DW_CFA_offset_extended_sf` instruction takes two operands: an unsigned
    LEB128 value representing a register number R and a signed LEB128 factored
    displacement B. This instruction is identical to `DW_CFA_offset_extended`,
    except that B is signed.

6.  `DW_CFA_val_offset`

    The `DW_CFA_val_offset` instruction takes two unsigned LEB128 operands
    representing a register number R and a factored displacement B. The required
    action is to change the rule for the register indicated by R to be a
    <i>val_offset(B * data_alignment_factor)</i> rule.

    > NOTE: Seems this should be named `DW_CFA_val_offset_uf` since the
    displacement is unsigned factored.

7.  `DW_CFA_val_offset_sf`

    The `DW_CFA_val_offset_sf` instruction takes two operands: an unsigned
    LEB128 value representing a register number R and a signed LEB128 factored
    displacement B. This instruction is identical to `DW_CFA_val_offset`, except
    that B is signed.

8.  `DW_CFA_register`

    The `DW_CFA_register` instruction takes two unsigned LEB128 operands
    representing register numbers R1 and R2 respectively. The required action is
    to set the rule for the register specified by R1 to be a <i>register(R2)</i>
    rule.

9.  `DW_CFA_expression`

    The `DW_CFA_expression` instruction takes two operands: an unsigned LEB128
    value representing a register number R, and a `DW_FORM_block` value
    representing a DWARF operation expression E. The required action is to
    change the rule for the register specified by R to be an
    <i>expression(E)</i> rule.

    <i>That is, E computes the location description where the register value can
    be retrieved.</i>

    <i>See [6.4.2 Call Frame Instructions](#call-frame-instructions) regarding
    restrictions on the DWARF expression operations that can be used in E.</i>

10. `DW_CFA_val_expression`

    The `DW_CFA_val_expression` instruction takes two operands: an unsigned
    LEB128 value representing a register number R, and a `DW_FORM_block` value
    representing a DWARF operation expression E. The required action is to
    change the rule for the register specified by R to be a
    <i>val_expression(E)</i> rule.

    <i>That is, E computes the value of register R.</i>

    <i>See [6.4.2 Call Frame Instructions](#call-frame-instructions) regarding
    restrictions on the DWARF expression operations that can be used in E.</i>

    If the result of evaluating E is not a value with a base type size that
    matches the register size, then the DWARF is ill-formed.

11. `DW_CFA_restore`

    The `DW_CFA_restore` instruction takes a single operand (encoded with the
    opcode) that represents a register number R. The required action is to
    change the rule for the register specified by R to the rule assigned it by
    the `initial_instructions` in the CIE.

12. `DW_CFA_restore_extended`

    The `DW_CFA_restore_extended` instruction takes a single unsigned LEB128
    operand that represents a register number R. This instruction is identical
    to `DW_CFA_restore`, except for the encoding and size of the register
    operand.

##### A.6.4.2.4 Row State Instructions

> NOTE: These instructions are the same as in DWARF Version 5 section 6.4.2.4.

##### A.6.4.2.5 Padding Instruction

> NOTE: These instructions are the same as in DWARF Version 5 section 6.4.2.5.

#### A.6.4.3 Call Frame Instruction Usage

> NOTE: The same as in DWARF Version 5 section 6.4.3.

#### A.6.4.4 Call Frame Calling Address

> NOTE: The same as in DWARF Version 5 section 6.4.4.

## A.7 Data Representation

> NOTE: This section provides changes to existing debugger information entry
> attributes. These would be incorporated into the corresponding DWARF Version 5
> chapter 7 sections.

### A.7.4 32-Bit and 64-Bit DWARF Formats

> NOTE: This augments DWARF Version 5 section 7.4 list item 3's table.

    Form                     Role
    ------------------------ --------------------------------------
    DW_OP_implicit_pointer   offset in `.debug_info`

### A.7.5 Format of Debugging Information

#### A.7.5.5 Classes and Forms

> NOTE: The same as in DWARF Version 5 section 7.5.5.

### A.7.7 DWARF Expressions

> NOTE: Rename DWARF Version 5 section 7.7 to reflect the unification of
> location descriptions into DWARF expressions.

#### A.7.7.1 Operation Expressions

> NOTE: Rename DWARF Version 5 section 7.7.1 and delete section 7.7.2 to reflect
> the unification of location descriptions into DWARF expressions.

#### A.7.7.3 Location List Expressions

> NOTE: Rename DWARF Version 5 section 7.7.3 to reflect that location lists are
> a kind of DWARF expression.

# B. Further Information

The following references provide additional information on the extension.

A reference to the DWARF standard is provided.

A formatted version of this extension is available on the LLVM site. It includes
many figures that help illustrate the textual description, especially of the
example DWARF expression evaluations.

Slides and a video of a presentation at the Linux Plumbers Conference 2021
related to this extension are available.

The LLVM compiler extension includes the operations mentioned in the motivating
examples. It also covers other extensions needed for heterogeneous devices.

- [DWARF Debugging Information Format](https://dwarfstd.org/)
  - [DWARF Debugging Information Format Version 5](https://dwarfstd.org/Dwarf5Std.php)
- [Allow Location Descriptions on the DWARF Expression Stack](https://llvm.org/docs/AMDGPUDwarfExtensionAllowLocationDescriptionOnTheDwarfExpressionStack/AMDGPUDwarfExtensionAllowLocationDescriptionOnTheDwarfExpressionStack.html)
- DWARF extensions for optimized SIMT/SIMD (GPU) debugging - Linux Plumbers Conference 2021
  - [Video](https://www.youtube.com/watch?v=QiR0ra0ymEY&t=10015s)
  - [Slides](https://linuxplumbersconf.org/event/11/contributions/1012/attachments/798/1505/DWARF_Extensions_for_Optimized_SIMT-SIMD_GPU_Debugging-LPC2021.pdf)
- [DWARF Extensions For Heterogeneous Debugging](https://llvm.org/docs/AMDGPUDwarfExtensionsForHeterogeneousDebugging.html)
