/*
 * This file contains instructions for testing by the test titled:
 *
 *         "Test x86 instruction decoder - new instructions"
 *
 * Note that the 'Expecting' comment lines are consumed by the
 * gen-insn-x86-dat.awk script and have the format:
 *
 *         Expecting: <op> <branch> <rel>
 *
 * If this file is changed, remember to run the gen-insn-x86-dat.sh
 * script and commit the result.
 *
 * Refer to insn-x86.c for more details.
 */

int main(void)
{
	/* Following line is a marker for the awk script - do not change */
	asm volatile("rdtsc"); /* Start here */

	/* Test fix for vcvtph2ps in x86-opcode-map.txt */

	asm volatile("vcvtph2ps %xmm3,%ymm5");

#ifdef __x86_64__

	/* bndmk m64, bnd */

	asm volatile("bndmk (%rax), %bnd0");
	asm volatile("bndmk (%r8), %bnd0");
	asm volatile("bndmk (0x12345678), %bnd0");
	asm volatile("bndmk (%rax), %bnd3");
	asm volatile("bndmk (%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndmk (%rax,%rcx,1), %bnd0");
	asm volatile("bndmk (%rax,%rcx,8), %bnd0");
	asm volatile("bndmk 0x12(%rax), %bnd0");
	asm volatile("bndmk 0x12(%rbp), %bnd0");
	asm volatile("bndmk 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndmk 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndmk 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndmk 0x12345678(%rax), %bnd0");
	asm volatile("bndmk 0x12345678(%rbp), %bnd0");
	asm volatile("bndmk 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rax,%rcx,8), %bnd0");

	/* bndcl r/m64, bnd */

	asm volatile("bndcl (%rax), %bnd0");
	asm volatile("bndcl (%r8), %bnd0");
	asm volatile("bndcl (0x12345678), %bnd0");
	asm volatile("bndcl (%rax), %bnd3");
	asm volatile("bndcl (%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcl (%rax,%rcx,1), %bnd0");
	asm volatile("bndcl (%rax,%rcx,8), %bnd0");
	asm volatile("bndcl 0x12(%rax), %bnd0");
	asm volatile("bndcl 0x12(%rbp), %bnd0");
	asm volatile("bndcl 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcl 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcl 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcl 0x12345678(%rax), %bnd0");
	asm volatile("bndcl 0x12345678(%rbp), %bnd0");
	asm volatile("bndcl 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcl %rax, %bnd0");

	/* bndcu r/m64, bnd */

	asm volatile("bndcu (%rax), %bnd0");
	asm volatile("bndcu (%r8), %bnd0");
	asm volatile("bndcu (0x12345678), %bnd0");
	asm volatile("bndcu (%rax), %bnd3");
	asm volatile("bndcu (%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcu (%rax,%rcx,1), %bnd0");
	asm volatile("bndcu (%rax,%rcx,8), %bnd0");
	asm volatile("bndcu 0x12(%rax), %bnd0");
	asm volatile("bndcu 0x12(%rbp), %bnd0");
	asm volatile("bndcu 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcu 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcu 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcu 0x12345678(%rax), %bnd0");
	asm volatile("bndcu 0x12345678(%rbp), %bnd0");
	asm volatile("bndcu 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcu %rax, %bnd0");

	/* bndcn r/m64, bnd */

	asm volatile("bndcn (%rax), %bnd0");
	asm volatile("bndcn (%r8), %bnd0");
	asm volatile("bndcn (0x12345678), %bnd0");
	asm volatile("bndcn (%rax), %bnd3");
	asm volatile("bndcn (%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcn (%rax,%rcx,1), %bnd0");
	asm volatile("bndcn (%rax,%rcx,8), %bnd0");
	asm volatile("bndcn 0x12(%rax), %bnd0");
	asm volatile("bndcn 0x12(%rbp), %bnd0");
	asm volatile("bndcn 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcn 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcn 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcn 0x12345678(%rax), %bnd0");
	asm volatile("bndcn 0x12345678(%rbp), %bnd0");
	asm volatile("bndcn 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcn %rax, %bnd0");

	/* bndmov m128, bnd */

	asm volatile("bndmov (%rax), %bnd0");
	asm volatile("bndmov (%r8), %bnd0");
	asm volatile("bndmov (0x12345678), %bnd0");
	asm volatile("bndmov (%rax), %bnd3");
	asm volatile("bndmov (%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndmov (%rax,%rcx,1), %bnd0");
	asm volatile("bndmov (%rax,%rcx,8), %bnd0");
	asm volatile("bndmov 0x12(%rax), %bnd0");
	asm volatile("bndmov 0x12(%rbp), %bnd0");
	asm volatile("bndmov 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndmov 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndmov 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndmov 0x12345678(%rax), %bnd0");
	asm volatile("bndmov 0x12345678(%rbp), %bnd0");
	asm volatile("bndmov 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rax,%rcx,8), %bnd0");

	/* bndmov bnd, m128 */

	asm volatile("bndmov %bnd0, (%rax)");
	asm volatile("bndmov %bnd0, (%r8)");
	asm volatile("bndmov %bnd0, (0x12345678)");
	asm volatile("bndmov %bnd3, (%rax)");
	asm volatile("bndmov %bnd0, (%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(,%rax,1)");
	asm volatile("bndmov %bnd0, (%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, (%rax,%rcx,8)");
	asm volatile("bndmov %bnd0, 0x12(%rax)");
	asm volatile("bndmov %bnd0, 0x12(%rbp)");
	asm volatile("bndmov %bnd0, 0x12(%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12(%rbp,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12(%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, 0x12(%rax,%rcx,8)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax)");
	asm volatile("bndmov %bnd0, 0x12345678(%rbp)");
	asm volatile("bndmov %bnd0, 0x12345678(%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rbp,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax,%rcx,8)");

	/* bndmov bnd2, bnd1 */

	asm volatile("bndmov %bnd0, %bnd1");
	asm volatile("bndmov %bnd1, %bnd0");

	/* bndldx mib, bnd */

	asm volatile("bndldx (%rax), %bnd0");
	asm volatile("bndldx (%r8), %bnd0");
	asm volatile("bndldx (0x12345678), %bnd0");
	asm volatile("bndldx (%rax), %bnd3");
	asm volatile("bndldx (%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndldx (%rax,%rcx,1), %bnd0");
	asm volatile("bndldx 0x12(%rax), %bnd0");
	asm volatile("bndldx 0x12(%rbp), %bnd0");
	asm volatile("bndldx 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndldx 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rax), %bnd0");
	asm volatile("bndldx 0x12345678(%rbp), %bnd0");
	asm volatile("bndldx 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rax,%rcx,1), %bnd0");

	/* bndstx bnd, mib */

	asm volatile("bndstx %bnd0, (%rax)");
	asm volatile("bndstx %bnd0, (%r8)");
	asm volatile("bndstx %bnd0, (0x12345678)");
	asm volatile("bndstx %bnd3, (%rax)");
	asm volatile("bndstx %bnd0, (%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(,%rax,1)");
	asm volatile("bndstx %bnd0, (%rax,%rcx,1)");
	asm volatile("bndstx %bnd0, 0x12(%rax)");
	asm volatile("bndstx %bnd0, 0x12(%rbp)");
	asm volatile("bndstx %bnd0, 0x12(%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12(%rbp,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12(%rax,%rcx,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rax)");
	asm volatile("bndstx %bnd0, 0x12345678(%rbp)");
	asm volatile("bndstx %bnd0, 0x12345678(%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rbp,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rax,%rcx,1)");

	/* bnd prefix on call, ret, jmp and all jcc */

	asm volatile("bnd call label1");  /* Expecting: call unconditional 0 */
	asm volatile("bnd call *(%eax)"); /* Expecting: call indirect      0 */
	asm volatile("bnd ret");          /* Expecting: ret  indirect      0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0 */
	asm volatile("bnd jmp *(%ecx)");  /* Expecting: jmp  indirect      0 */
	asm volatile("bnd jne label1");   /* Expecting: jcc  conditional   0 */

	/* sha1rnds4 imm8, xmm2/m128, xmm1 */

	asm volatile("sha1rnds4 $0x0, %xmm1, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm2");
	asm volatile("sha1rnds4 $0x91, %xmm8, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm8");
	asm volatile("sha1rnds4 $0x91, %xmm15, %xmm8");
	asm volatile("sha1rnds4 $0x91, (%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%r8), %xmm0");
	asm volatile("sha1rnds4 $0x91, (0x12345678), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax), %xmm3");
	asm volatile("sha1rnds4 $0x91, (%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rbp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rbp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1nexte xmm2/m128, xmm1 */

	asm volatile("sha1nexte %xmm1, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm2");
	asm volatile("sha1nexte %xmm8, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm8");
	asm volatile("sha1nexte %xmm15, %xmm8");
	asm volatile("sha1nexte (%rax), %xmm0");
	asm volatile("sha1nexte (%r8), %xmm0");
	asm volatile("sha1nexte (0x12345678), %xmm0");
	asm volatile("sha1nexte (%rax), %xmm3");
	asm volatile("sha1nexte (%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1nexte (%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte (%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12(%rax), %xmm0");
	asm volatile("sha1nexte 0x12(%rbp), %xmm0");
	asm volatile("sha1nexte 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rbp), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1msg1 xmm2/m128, xmm1 */

	asm volatile("sha1msg1 %xmm1, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm2");
	asm volatile("sha1msg1 %xmm8, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm8");
	asm volatile("sha1msg1 %xmm15, %xmm8");
	asm volatile("sha1msg1 (%rax), %xmm0");
	asm volatile("sha1msg1 (%r8), %xmm0");
	asm volatile("sha1msg1 (0x12345678), %xmm0");
	asm volatile("sha1msg1 (%rax), %xmm3");
	asm volatile("sha1msg1 (%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1msg1 (%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 (%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12(%rax), %xmm0");
	asm volatile("sha1msg1 0x12(%rbp), %xmm0");
	asm volatile("sha1msg1 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rbp), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1msg2 xmm2/m128, xmm1 */

	asm volatile("sha1msg2 %xmm1, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm2");
	asm volatile("sha1msg2 %xmm8, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm8");
	asm volatile("sha1msg2 %xmm15, %xmm8");
	asm volatile("sha1msg2 (%rax), %xmm0");
	asm volatile("sha1msg2 (%r8), %xmm0");
	asm volatile("sha1msg2 (0x12345678), %xmm0");
	asm volatile("sha1msg2 (%rax), %xmm3");
	asm volatile("sha1msg2 (%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1msg2 (%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 (%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12(%rax), %xmm0");
	asm volatile("sha1msg2 0x12(%rbp), %xmm0");
	asm volatile("sha1msg2 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rbp), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256rnds2 <XMM0>, xmm2/m128, xmm1 */
	/* Note sha256rnds2 has an implicit operand 'xmm0' */

	asm volatile("sha256rnds2 %xmm4, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm2");
	asm volatile("sha256rnds2 %xmm8, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm8");
	asm volatile("sha256rnds2 %xmm15, %xmm8");
	asm volatile("sha256rnds2 (%rax), %xmm1");
	asm volatile("sha256rnds2 (%r8), %xmm1");
	asm volatile("sha256rnds2 (0x12345678), %xmm1");
	asm volatile("sha256rnds2 (%rax), %xmm3");
	asm volatile("sha256rnds2 (%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(,%rax,1), %xmm1");
	asm volatile("sha256rnds2 (%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 (%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax), %xmm1");
	asm volatile("sha256rnds2 0x12(%rbp), %xmm1");
	asm volatile("sha256rnds2 0x12(%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rbp,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rbp), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rbp,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256msg1 xmm2/m128, xmm1 */

	asm volatile("sha256msg1 %xmm1, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm2");
	asm volatile("sha256msg1 %xmm8, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm8");
	asm volatile("sha256msg1 %xmm15, %xmm8");
	asm volatile("sha256msg1 (%rax), %xmm0");
	asm volatile("sha256msg1 (%r8), %xmm0");
	asm volatile("sha256msg1 (0x12345678), %xmm0");
	asm volatile("sha256msg1 (%rax), %xmm3");
	asm volatile("sha256msg1 (%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha256msg1 (%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 (%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12(%rax), %xmm0");
	asm volatile("sha256msg1 0x12(%rbp), %xmm0");
	asm volatile("sha256msg1 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rbp), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256msg2 xmm2/m128, xmm1 */

	asm volatile("sha256msg2 %xmm1, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm2");
	asm volatile("sha256msg2 %xmm8, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm8");
	asm volatile("sha256msg2 %xmm15, %xmm8");
	asm volatile("sha256msg2 (%rax), %xmm0");
	asm volatile("sha256msg2 (%r8), %xmm0");
	asm volatile("sha256msg2 (0x12345678), %xmm0");
	asm volatile("sha256msg2 (%rax), %xmm3");
	asm volatile("sha256msg2 (%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha256msg2 (%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 (%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12(%rax), %xmm0");
	asm volatile("sha256msg2 0x12(%rbp), %xmm0");
	asm volatile("sha256msg2 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rbp), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,8), %xmm15");

	/* clflushopt m8 */

	asm volatile("clflushopt (%rax)");
	asm volatile("clflushopt (%r8)");
	asm volatile("clflushopt (0x12345678)");
	asm volatile("clflushopt 0x12345678(%rax,%rcx,8)");
	asm volatile("clflushopt 0x12345678(%r8,%rcx,8)");
	/* Also check instructions in the same group encoding as clflushopt */
	asm volatile("clflush (%rax)");
	asm volatile("clflush (%r8)");
	asm volatile("sfence");

	/* clwb m8 */

	asm volatile("clwb (%rax)");
	asm volatile("clwb (%r8)");
	asm volatile("clwb (0x12345678)");
	asm volatile("clwb 0x12345678(%rax,%rcx,8)");
	asm volatile("clwb 0x12345678(%r8,%rcx,8)");
	/* Also check instructions in the same group encoding as clwb */
	asm volatile("xsaveopt (%rax)");
	asm volatile("xsaveopt (%r8)");
	asm volatile("mfence");

	/* xsavec mem */

	asm volatile("xsavec (%rax)");
	asm volatile("xsavec (%r8)");
	asm volatile("xsavec (0x12345678)");
	asm volatile("xsavec 0x12345678(%rax,%rcx,8)");
	asm volatile("xsavec 0x12345678(%r8,%rcx,8)");

	/* xsaves mem */

	asm volatile("xsaves (%rax)");
	asm volatile("xsaves (%r8)");
	asm volatile("xsaves (0x12345678)");
	asm volatile("xsaves 0x12345678(%rax,%rcx,8)");
	asm volatile("xsaves 0x12345678(%r8,%rcx,8)");

	/* xrstors mem */

	asm volatile("xrstors (%rax)");
	asm volatile("xrstors (%r8)");
	asm volatile("xrstors (0x12345678)");
	asm volatile("xrstors 0x12345678(%rax,%rcx,8)");
	asm volatile("xrstors 0x12345678(%r8,%rcx,8)");

#else  /* #ifdef __x86_64__ */

	/* bndmk m32, bnd */

	asm volatile("bndmk (%eax), %bnd0");
	asm volatile("bndmk (0x12345678), %bnd0");
	asm volatile("bndmk (%eax), %bnd3");
	asm volatile("bndmk (%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndmk (%eax,%ecx,1), %bnd0");
	asm volatile("bndmk (%eax,%ecx,8), %bnd0");
	asm volatile("bndmk 0x12(%eax), %bnd0");
	asm volatile("bndmk 0x12(%ebp), %bnd0");
	asm volatile("bndmk 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndmk 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndmk 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndmk 0x12345678(%eax), %bnd0");
	asm volatile("bndmk 0x12345678(%ebp), %bnd0");
	asm volatile("bndmk 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndmk 0x12345678(%eax,%ecx,8), %bnd0");

	/* bndcl r/m32, bnd */

	asm volatile("bndcl (%eax), %bnd0");
	asm volatile("bndcl (0x12345678), %bnd0");
	asm volatile("bndcl (%eax), %bnd3");
	asm volatile("bndcl (%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcl (%eax,%ecx,1), %bnd0");
	asm volatile("bndcl (%eax,%ecx,8), %bnd0");
	asm volatile("bndcl 0x12(%eax), %bnd0");
	asm volatile("bndcl 0x12(%ebp), %bnd0");
	asm volatile("bndcl 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcl 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcl 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcl 0x12345678(%eax), %bnd0");
	asm volatile("bndcl 0x12345678(%ebp), %bnd0");
	asm volatile("bndcl 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcl 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcl %eax, %bnd0");

	/* bndcu r/m32, bnd */

	asm volatile("bndcu (%eax), %bnd0");
	asm volatile("bndcu (0x12345678), %bnd0");
	asm volatile("bndcu (%eax), %bnd3");
	asm volatile("bndcu (%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcu (%eax,%ecx,1), %bnd0");
	asm volatile("bndcu (%eax,%ecx,8), %bnd0");
	asm volatile("bndcu 0x12(%eax), %bnd0");
	asm volatile("bndcu 0x12(%ebp), %bnd0");
	asm volatile("bndcu 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcu 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcu 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcu 0x12345678(%eax), %bnd0");
	asm volatile("bndcu 0x12345678(%ebp), %bnd0");
	asm volatile("bndcu 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcu 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcu %eax, %bnd0");

	/* bndcn r/m32, bnd */

	asm volatile("bndcn (%eax), %bnd0");
	asm volatile("bndcn (0x12345678), %bnd0");
	asm volatile("bndcn (%eax), %bnd3");
	asm volatile("bndcn (%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcn (%eax,%ecx,1), %bnd0");
	asm volatile("bndcn (%eax,%ecx,8), %bnd0");
	asm volatile("bndcn 0x12(%eax), %bnd0");
	asm volatile("bndcn 0x12(%ebp), %bnd0");
	asm volatile("bndcn 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcn 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcn 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcn 0x12345678(%eax), %bnd0");
	asm volatile("bndcn 0x12345678(%ebp), %bnd0");
	asm volatile("bndcn 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcn 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcn %eax, %bnd0");

	/* bndmov m64, bnd */

	asm volatile("bndmov (%eax), %bnd0");
	asm volatile("bndmov (0x12345678), %bnd0");
	asm volatile("bndmov (%eax), %bnd3");
	asm volatile("bndmov (%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndmov (%eax,%ecx,1), %bnd0");
	asm volatile("bndmov (%eax,%ecx,8), %bnd0");
	asm volatile("bndmov 0x12(%eax), %bnd0");
	asm volatile("bndmov 0x12(%ebp), %bnd0");
	asm volatile("bndmov 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndmov 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndmov 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndmov 0x12345678(%eax), %bnd0");
	asm volatile("bndmov 0x12345678(%ebp), %bnd0");
	asm volatile("bndmov 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndmov 0x12345678(%eax,%ecx,8), %bnd0");

	/* bndmov bnd, m64 */

	asm volatile("bndmov %bnd0, (%eax)");
	asm volatile("bndmov %bnd0, (0x12345678)");
	asm volatile("bndmov %bnd3, (%eax)");
	asm volatile("bndmov %bnd0, (%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(,%eax,1)");
	asm volatile("bndmov %bnd0, (%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, (%eax,%ecx,8)");
	asm volatile("bndmov %bnd0, 0x12(%eax)");
	asm volatile("bndmov %bnd0, 0x12(%ebp)");
	asm volatile("bndmov %bnd0, 0x12(%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12(%ebp,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12(%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, 0x12(%eax,%ecx,8)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax)");
	asm volatile("bndmov %bnd0, 0x12345678(%ebp)");
	asm volatile("bndmov %bnd0, 0x12345678(%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%ebp,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax,%ecx,8)");

	/* bndmov bnd2, bnd1 */

	asm volatile("bndmov %bnd0, %bnd1");
	asm volatile("bndmov %bnd1, %bnd0");

	/* bndldx mib, bnd */

	asm volatile("bndldx (%eax), %bnd0");
	asm volatile("bndldx (0x12345678), %bnd0");
	asm volatile("bndldx (%eax), %bnd3");
	asm volatile("bndldx (%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndldx (%eax,%ecx,1), %bnd0");
	asm volatile("bndldx 0x12(%eax), %bnd0");
	asm volatile("bndldx 0x12(%ebp), %bnd0");
	asm volatile("bndldx 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndldx 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndldx 0x12345678(%eax), %bnd0");
	asm volatile("bndldx 0x12345678(%ebp), %bnd0");
	asm volatile("bndldx 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%eax,%ecx,1), %bnd0");

	/* bndstx bnd, mib */

	asm volatile("bndstx %bnd0, (%eax)");
	asm volatile("bndstx %bnd0, (0x12345678)");
	asm volatile("bndstx %bnd3, (%eax)");
	asm volatile("bndstx %bnd0, (%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(,%eax,1)");
	asm volatile("bndstx %bnd0, (%eax,%ecx,1)");
	asm volatile("bndstx %bnd0, 0x12(%eax)");
	asm volatile("bndstx %bnd0, 0x12(%ebp)");
	asm volatile("bndstx %bnd0, 0x12(%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12(%ebp,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12(%eax,%ecx,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%eax)");
	asm volatile("bndstx %bnd0, 0x12345678(%ebp)");
	asm volatile("bndstx %bnd0, 0x12345678(%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%ebp,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%eax,%ecx,1)");

	/* bnd prefix on call, ret, jmp and all jcc */

	asm volatile("bnd call label1");  /* Expecting: call unconditional 0xfffffffc */
	asm volatile("bnd call *(%eax)"); /* Expecting: call indirect      0 */
	asm volatile("bnd ret");          /* Expecting: ret  indirect      0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0xfffffffc */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0xfffffffc */
	asm volatile("bnd jmp *(%ecx)");  /* Expecting: jmp  indirect      0 */
	asm volatile("bnd jne label1");   /* Expecting: jcc  conditional   0xfffffffc */

	/* sha1rnds4 imm8, xmm2/m128, xmm1 */

	asm volatile("sha1rnds4 $0x0, %xmm1, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm2");
	asm volatile("sha1rnds4 $0x91, (%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, (0x12345678), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax), %xmm3");
	asm volatile("sha1rnds4 $0x91, (%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax,%ecx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ebp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ebp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1nexte xmm2/m128, xmm1 */

	asm volatile("sha1nexte %xmm1, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm2");
	asm volatile("sha1nexte (%eax), %xmm0");
	asm volatile("sha1nexte (0x12345678), %xmm0");
	asm volatile("sha1nexte (%eax), %xmm3");
	asm volatile("sha1nexte (%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1nexte (%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte (%eax,%ecx,8), %xmm0");
	asm volatile("sha1nexte 0x12(%eax), %xmm0");
	asm volatile("sha1nexte 0x12(%ebp), %xmm0");
	asm volatile("sha1nexte 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ebp), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1msg1 xmm2/m128, xmm1 */

	asm volatile("sha1msg1 %xmm1, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm2");
	asm volatile("sha1msg1 (%eax), %xmm0");
	asm volatile("sha1msg1 (0x12345678), %xmm0");
	asm volatile("sha1msg1 (%eax), %xmm3");
	asm volatile("sha1msg1 (%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1msg1 (%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 (%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg1 0x12(%eax), %xmm0");
	asm volatile("sha1msg1 0x12(%ebp), %xmm0");
	asm volatile("sha1msg1 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ebp), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1msg2 xmm2/m128, xmm1 */

	asm volatile("sha1msg2 %xmm1, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm2");
	asm volatile("sha1msg2 (%eax), %xmm0");
	asm volatile("sha1msg2 (0x12345678), %xmm0");
	asm volatile("sha1msg2 (%eax), %xmm3");
	asm volatile("sha1msg2 (%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1msg2 (%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 (%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg2 0x12(%eax), %xmm0");
	asm volatile("sha1msg2 0x12(%ebp), %xmm0");
	asm volatile("sha1msg2 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ebp), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha256rnds2 <XMM0>, xmm2/m128, xmm1 */
	/* Note sha256rnds2 has an implicit operand 'xmm0' */

	asm volatile("sha256rnds2 %xmm4, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm2");
	asm volatile("sha256rnds2 (%eax), %xmm1");
	asm volatile("sha256rnds2 (0x12345678), %xmm1");
	asm volatile("sha256rnds2 (%eax), %xmm3");
	asm volatile("sha256rnds2 (%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(,%eax,1), %xmm1");
	asm volatile("sha256rnds2 (%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 (%eax,%ecx,8), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax), %xmm1");
	asm volatile("sha256rnds2 0x12(%ebp), %xmm1");
	asm volatile("sha256rnds2 0x12(%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%ebp,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax,%ecx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ebp), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ebp,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax,%ecx,8), %xmm1");

	/* sha256msg1 xmm2/m128, xmm1 */

	asm volatile("sha256msg1 %xmm1, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm2");
	asm volatile("sha256msg1 (%eax), %xmm0");
	asm volatile("sha256msg1 (0x12345678), %xmm0");
	asm volatile("sha256msg1 (%eax), %xmm3");
	asm volatile("sha256msg1 (%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha256msg1 (%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 (%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg1 0x12(%eax), %xmm0");
	asm volatile("sha256msg1 0x12(%ebp), %xmm0");
	asm volatile("sha256msg1 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ebp), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha256msg2 xmm2/m128, xmm1 */

	asm volatile("sha256msg2 %xmm1, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm2");
	asm volatile("sha256msg2 (%eax), %xmm0");
	asm volatile("sha256msg2 (0x12345678), %xmm0");
	asm volatile("sha256msg2 (%eax), %xmm3");
	asm volatile("sha256msg2 (%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha256msg2 (%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 (%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg2 0x12(%eax), %xmm0");
	asm volatile("sha256msg2 0x12(%ebp), %xmm0");
	asm volatile("sha256msg2 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ebp), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax,%ecx,8), %xmm0");

	/* clflushopt m8 */

	asm volatile("clflushopt (%eax)");
	asm volatile("clflushopt (0x12345678)");
	asm volatile("clflushopt 0x12345678(%eax,%ecx,8)");
	/* Also check instructions in the same group encoding as clflushopt */
	asm volatile("clflush (%eax)");
	asm volatile("sfence");

	/* clwb m8 */

	asm volatile("clwb (%eax)");
	asm volatile("clwb (0x12345678)");
	asm volatile("clwb 0x12345678(%eax,%ecx,8)");
	/* Also check instructions in the same group encoding as clwb */
	asm volatile("xsaveopt (%eax)");
	asm volatile("mfence");

	/* xsavec mem */

	asm volatile("xsavec (%eax)");
	asm volatile("xsavec (0x12345678)");
	asm volatile("xsavec 0x12345678(%eax,%ecx,8)");

	/* xsaves mem */

	asm volatile("xsaves (%eax)");
	asm volatile("xsaves (0x12345678)");
	asm volatile("xsaves 0x12345678(%eax,%ecx,8)");

	/* xrstors mem */

	asm volatile("xrstors (%eax)");
	asm volatile("xrstors (0x12345678)");
	asm volatile("xrstors 0x12345678(%eax,%ecx,8)");

#endif /* #ifndef __x86_64__ */

	/* pcommit */

	asm volatile("pcommit");

	/* Following line is a marker for the awk script - do not change */
	asm volatile("rdtsc"); /* Stop here */

	return 0;
}
