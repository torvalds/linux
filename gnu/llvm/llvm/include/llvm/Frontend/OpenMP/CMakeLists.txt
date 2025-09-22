set(LLVM_TARGET_DEFINITIONS OMP.td)
tablegen(LLVM OMP.h.inc --gen-directive-decl)
tablegen(LLVM OMP.inc --gen-directive-impl)
add_public_tablegen_target(omp_gen)
