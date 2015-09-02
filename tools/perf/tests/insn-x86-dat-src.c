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

#endif /* #ifndef __x86_64__ */

	/* Following line is a marker for the awk script - do not change */
	asm volatile("rdtsc"); /* Stop here */

	return 0;
}
