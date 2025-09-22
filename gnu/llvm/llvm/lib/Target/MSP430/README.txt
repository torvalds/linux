//===---------------------------------------------------------------------===//
// MSP430 backend.
//===---------------------------------------------------------------------===//

DISCLAIMER: This backend should be considered as highly experimental. I never
seen nor worked with this MCU, all information was gathered from datasheet
only. The original intention of making this backend was to write documentation
of form "How to write backend for dummies" :) Thes notes hopefully will be
available pretty soon.

Some things are incomplete / not implemented yet (this list surely is not
complete as well):

1. Verify, how stuff is handling implicit zext with 8 bit operands (this might
be modelled currently in improper way - should we need to mark the superreg as
def for every 8 bit instruction?).

2. Libcalls: multiplication, division, remainder. Note, that calling convention
for libcalls is incomptible with calling convention of libcalls of msp430-gcc
(these cannot be used though due to license restriction).

3. Implement multiplication / division by constant (dag combiner hook?).

4. Implement non-constant shifts.

5. Implement varargs stuff.

6. Verify and fix (if needed) how's stuff playing with i32 / i64.

7. Implement floating point stuff (softfp?)

8. Implement instruction encoding for (possible) direct code emission in the
future.

9. Since almost all instructions set flags - implement brcond / select in better
way (currently they emit explicit comparison).

10. Handle imm in comparisons in better way (see comment in MSP430InstrInfo.td)

11. Implement hooks for better memory op folding, etc.

