	.abicalls
	.set	noreorder
	.set	nomacro

/* The GNU and SGI linkers differ in their implementation of -init and -fini.
   With the GNU linker, there can only be a single -init option, and the
   linker simply sets DT_INIT to that value.  gcc's initialization and
   finalization code can go directly in .init, with the prologue and
   epilogue of the main initialization routine being provided by external
   object files (*crti.o and *crtn.o in this case).

   The SGI linker instead accepts several -init options.  It will set DT_INIT
   to a linker-created function (placed in .init) that calls each of the -init
   functions in turn.  If there is any user code in .init, this linker-created
   function will be placed after it.  Note that such user code is not treated
   specially; it will only be called if the -init options arrange for it to
   be called.

   In theory, the SGI model should allow the crti, crtn and intermediate code
   to go in .init, just like it can with the GNU linker.  However, doing this
   seems to confuse the linker and triggers an internal error:

      ld32: FATAL   2  : Internal: at ../../ld/mips_code.c mips_code_fixup()
	 text section overflow!

   (seen with MIPSpro 7.30).  We therefore put everything in a special
   .gcc_init section instead.  */

	.section .gcc_init,"ax",@progbits
	.globl	__gcc_init
__gcc_init:
#if _MIPS_SIM == _ABIO32
	addiu	$sp,$sp,-16
	sw	$31,0($sp)
#else
	daddiu	$sp,$sp,-16
	sd	$31,0($sp)
	sd	$28,8($sp)
#endif

	.section .gcc_fini,"ax",@progbits
	.globl	__gcc_fini
__gcc_fini:
#if _MIPS_SIM == _ABIO32
	addiu	$sp,$sp,-16
	sw	$31,0($sp)
#else
	daddiu	$sp,$sp,-16
	sd	$31,0($sp)
	sd	$28,8($sp)
#endif
