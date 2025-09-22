To-do
-----

* Keep the address of the constant pool in a register instead of forming its
  address all of the time.
* We can fold small constant offsets into the %hi/%lo references to constant
  pool addresses as well.
* When in V9 mode, register allocate %icc[0-3].
* Add support for isel'ing UMUL_LOHI instead of marking it as Expand.
* Emit the 'Branch on Integer Register with Prediction' instructions.  It's
  not clear how to write a pattern for this though:

float %t1(int %a, int* %p) {
        %C = seteq int %a, 0
        br bool %C, label %T, label %F
T:
        store int 123, int* %p
        br label %F
F:
        ret float undef
}

codegens to this:

t1:
        save -96, %o6, %o6
1)      subcc %i0, 0, %l0
1)      bne .LBBt1_2    ! F
        nop
.LBBt1_1:       ! T
        or %g0, 123, %l0
        st %l0, [%i1]
.LBBt1_2:       ! F
        restore %g0, %g0, %g0
        retl
        nop

1) should be replaced with a brz in V9 mode.

* Same as above, but emit conditional move on register zero (p192) in V9
  mode.  Testcase:

int %t1(int %a, int %b) {
        %C = seteq int %a, 0
        %D = select bool %C, int %a, int %b
        ret int %D
}

* Emit MULX/[SU]DIVX instructions in V9 mode instead of fiddling
  with the Y register, if they are faster.

* Codegen bswap(load)/store(bswap) -> load/store ASI

* Implement frame pointer elimination, e.g. eliminate save/restore for
  leaf fns.
* Fill delay slots

* Use %g0 directly to materialize 0. No instruction is required.
