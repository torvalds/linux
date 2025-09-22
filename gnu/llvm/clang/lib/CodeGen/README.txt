IRgen optimization opportunities.

//===---------------------------------------------------------------------===//

The common pattern of
--
short x; // or char, etc
(x == 10)
--
generates an zext/sext of x which can easily be avoided.

//===---------------------------------------------------------------------===//

Bitfields accesses can be shifted to simplify masking and sign
extension. For example, if the bitfield width is 8 and it is
appropriately aligned then is is a lot shorter to just load the char
directly.

//===---------------------------------------------------------------------===//

It may be worth avoiding creation of alloca's for formal arguments
for the common situation where the argument is never written to or has
its address taken. The idea would be to begin generating code by using
the argument directly and if its address is taken or it is stored to
then generate the alloca and patch up the existing code.

In theory, the same optimization could be a win for block local
variables as long as the declaration dominates all statements in the
block.

NOTE: The main case we care about this for is for -O0 -g compile time
performance, and in that scenario we will need to emit the alloca
anyway currently to emit proper debug info. So this is blocked by
being able to emit debug information which refers to an LLVM
temporary, not an alloca.

//===---------------------------------------------------------------------===//

We should try and avoid generating basic blocks which only contain
jumps. At -O0, this penalizes us all the way from IRgen (malloc &
instruction overhead), all the way down through code generation and
assembly time.

On 176.gcc:expr.ll, it looks like over 12% of basic blocks are just
direct branches!

//===---------------------------------------------------------------------===//
