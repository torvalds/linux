;; This code used to be expanded through interesting expansions in
;; the machine description, compiled from this code:
;;
;; #ifdef L_mulsi3
;; long __Mul (unsigned long a, unsigned long b) __attribute__ ((__const__));
;; 
;; /* This must be compiled with the -mexpand-mul flag, to synthesize the
;;    multiplication from the mstep instructions.  The check for
;;    smaller-size multiplication pays off in the order of .5-10%;
;;    estimated median 1%, depending on application.
;;     FIXME: It can be further optimized if we go to assembler code, as
;;    gcc 2.7.2 adds a few unnecessary instructions and does not put the
;;    basic blocks in optimal order.  */
;; long
;; __Mul (unsigned long a, unsigned long b)
;; {
;; #if defined (__CRIS_arch_version) && __CRIS_arch_version >= 10
;;   /* In case other code is compiled without -march=v10, they will
;; 	contain calls to __Mul, regardless of flags at link-time.  The
;; 	"else"-code below will work, but is unnecessarily slow.  This
;; 	sometimes cuts a few minutes off from simulation time by just
;; 	returning a "mulu.d".  */
;;   return a * b;
;; #else
;;   unsigned long min;
;; 
;;   /* Get minimum via the bound insn.  */
;;   min = a < b ? a : b;
;; 
;;   /* Can we omit computation of the high part?	*/
;;   if (min > 65535)
;;     /* No.  Perform full multiplication.  */
;;     return a * b;
;;   else
;;     {
;; 	 /* Check if both operands are within 16 bits.  */
;; 	 unsigned long max;
;; 
;; 	 /* Get maximum, by knowing the minimum.
;; 	    This will partition a and b into max and min.
;; 	    This is not currently something GCC understands,
;; 	    so do this trick by asm.  */
;; 	 __asm__ ("xor %1,%0\n\txor %2,%0"
;; 		  : "=r" (max)
;; 		  :  "r" (b), "r" (a), "0" (min));
;; 
;;     if (max > 65535)
;; 	 /* Make GCC understand that only the low part of "min" will be
;; 	    used.  */
;; 	 return max * (unsigned short) min;
;;     else
;; 	 /* Only the low parts of both operands are necessary.  */
;; 	 return ((unsigned short) max) * (unsigned short) min;
;;     }
;; #endif /* not __CRIS_arch_version >= 10 */
;; }
;; #endif /* L_mulsi3 */
;;
;; That approach was abandoned since the caveats outweighted the
;; benefits.  The expand-multiplication machinery is also removed, so you
;; can't do this anymore.
;;
;; For doubters of there being any benefits, some where: insensitivity to:
;; - ABI changes (mostly for experimentation)
;; - assembler syntax differences (mostly debug format).
;; - insn scheduling issues.
;; Most ABI experiments will presumably happen with arches with mul insns,
;; so that argument doesn't really hold anymore, and it's unlikely there
;; being new arch variants needing insn scheduling and not having mul
;; insns.

;; ELF and a.out have different syntax for local labels: the "wrong"
;; one may not be omitted from the object.
#undef L
#ifdef __AOUT__
# define L(x) x
#else
# define L(x) .x
#endif

	.global ___Mul
	.type	___Mul,@function
___Mul:
#if defined (__CRIS_arch_version) && __CRIS_arch_version >= 10
;; Can't have the mulu.d last on a cache-line (in the delay-slot of the
;; "ret"), due to hardware bug.  See documentation for -mmul-bug-workaround.
;; Not worthwhile to conditionalize here.
	.p2alignw 2,0x050f
	mulu.d $r11,$r10
	ret
	nop
#else
	move.d $r10,$r12
	move.d $r11,$r9
	bound.d $r12,$r9
	cmpu.w 65535,$r9
	bls L(L3)
	move.d $r12,$r13

	movu.w $r11,$r9
	lslq 16,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	mstep $r9,$r13
	clear.w $r10
	test.d $r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	movu.w $r12,$r12
	move.d $r11,$r9
	clear.w $r9
	test.d $r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	mstep $r12,$r9
	add.w $r9,$r10
	lslq 16,$r10
	ret
	add.d $r13,$r10

L(L3):
	move.d $r9,$r10
	xor $r11,$r10
	xor $r12,$r10
	cmpu.w 65535,$r10
	bls L(L5)
	movu.w $r9,$r13

	movu.w $r13,$r13
	move.d $r10,$r9
	lslq 16,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	mstep $r13,$r9
	clear.w $r10
	test.d $r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	mstep $r13,$r10
	lslq 16,$r10
	ret
	add.d $r9,$r10

L(L5):
	movu.w $r9,$r9
	lslq 16,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	mstep $r9,$r10
	ret
	mstep $r9,$r10
#endif
L(Lfe1):
	.size	___Mul,L(Lfe1)-___Mul
