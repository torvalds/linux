/*===---- cpuid.h - X86 cpu model detection --------------------------------===
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */

#if !(__x86_64__ || __i386__)
#error this header is for x86 only
#endif

/* Features in %ecx for level 1 */
#define bit_SSE3        0x00000001
#define bit_PCLMULQDQ   0x00000002
#define bit_PCLMUL      bit_PCLMULQDQ /* for gcc compat */
#define bit_DTES64      0x00000004
#define bit_MONITOR     0x00000008
#define bit_DSCPL       0x00000010
#define bit_VMX         0x00000020
#define bit_SMX         0x00000040
#define bit_EIST        0x00000080
#define bit_TM2         0x00000100
#define bit_SSSE3       0x00000200
#define bit_CNXTID      0x00000400
#define bit_FMA         0x00001000
#define bit_CMPXCHG16B  0x00002000
#define bit_xTPR        0x00004000
#define bit_PDCM        0x00008000
#define bit_PCID        0x00020000
#define bit_DCA         0x00040000
#define bit_SSE41       0x00080000
#define bit_SSE4_1      bit_SSE41   /* for gcc compat */
#define bit_SSE42       0x00100000
#define bit_SSE4_2      bit_SSE42   /* for gcc compat */
#define bit_x2APIC      0x00200000
#define bit_MOVBE       0x00400000
#define bit_POPCNT      0x00800000
#define bit_TSCDeadline 0x01000000
#define bit_AESNI       0x02000000
#define bit_AES         bit_AESNI   /* for gcc compat */
#define bit_XSAVE       0x04000000
#define bit_OSXSAVE     0x08000000
#define bit_AVX         0x10000000
#define bit_F16C        0x20000000
#define bit_RDRAND      0x40000000

/* Features in %edx for level 1 */
#define bit_FPU         0x00000001
#define bit_VME         0x00000002
#define bit_DE          0x00000004
#define bit_PSE         0x00000008
#define bit_TSC         0x00000010
#define bit_MSR         0x00000020
#define bit_PAE         0x00000040
#define bit_MCE         0x00000080
#define bit_CX8         0x00000100
#define bit_CMPXCHG8B   bit_CX8     /* for gcc compat */
#define bit_APIC        0x00000200
#define bit_SEP         0x00000800
#define bit_MTRR        0x00001000
#define bit_PGE         0x00002000
#define bit_MCA         0x00004000
#define bit_CMOV        0x00008000
#define bit_PAT         0x00010000
#define bit_PSE36       0x00020000
#define bit_PSN         0x00040000
#define bit_CLFSH       0x00080000
#define bit_DS          0x00200000
#define bit_ACPI        0x00400000
#define bit_MMX         0x00800000
#define bit_FXSR        0x01000000
#define bit_FXSAVE      bit_FXSR    /* for gcc compat */
#define bit_SSE         0x02000000
#define bit_SSE2        0x04000000
#define bit_SS          0x08000000
#define bit_HTT         0x10000000
#define bit_TM          0x20000000
#define bit_PBE         0x80000000

/* Features in %ebx for level 7 sub-leaf 0 */
#define bit_FSGSBASE    0x00000001
#define bit_BMI         0x00000008
#define bit_HLE         0x00000010
#define bit_AVX2        0x00000020
#define bit_SMEP        0x00000080
#define bit_BMI2        0x00000100
#define bit_ENH_MOVSB   0x00000200
#define bit_INVPCID     0x00000400
#define bit_RTM         0x00000800
#define bit_RDSEED      0x00040000
#define bit_ADX         0x00080000
#define bit_SMAP        0x00100000

/* PIC on i386 uses %ebx, so preserve it. */
#if __i386__
#define __cpuid(__level, __eax, __ebx, __ecx, __edx) \
    __asm("  pushl  %%ebx\n" \
          "  cpuid\n" \
          "  mov    %%ebx,%1\n" \
          "  popl   %%ebx" \
        : "=a"(__eax), "=r" (__ebx), "=c"(__ecx), "=d"(__edx) \
        : "0"(__level))

#define __cpuid_count(__level, __count, __eax, __ebx, __ecx, __edx) \
    __asm("  pushl  %%ebx\n" \
          "  cpuid\n" \
          "  mov    %%ebx,%1\n" \
          "  popl   %%ebx" \
        : "=a"(__eax), "=r" (__ebx), "=c"(__ecx), "=d"(__edx) \
        : "0"(__level), "2"(__count))
#else
#define __cpuid(__level, __eax, __ebx, __ecx, __edx) \
    __asm("cpuid" : "=a"(__eax), "=b" (__ebx), "=c"(__ecx), "=d"(__edx) \
                  : "0"(__level))

#define __cpuid_count(__level, __count, __eax, __ebx, __ecx, __edx) \
    __asm("cpuid" : "=a"(__eax), "=b" (__ebx), "=c"(__ecx), "=d"(__edx) \
                  : "0"(__level), "2"(__count))
#endif

static __inline int __get_cpuid (unsigned int __level, unsigned int *__eax,
                                 unsigned int *__ebx, unsigned int *__ecx,
                                 unsigned int *__edx) {
    __cpuid(__level, *__eax, *__ebx, *__ecx, *__edx);
    return 1;
}

static __inline int __get_cpuid_max (unsigned int __level, unsigned int *__sig)
{
    unsigned int __eax, __ebx, __ecx, __edx;
#if __i386__
    int __cpuid_supported;

    __asm("  pushfl\n"
          "  popl   %%eax\n"
          "  movl   %%eax,%%ecx\n"
          "  xorl   $0x00200000,%%eax\n"
          "  pushl  %%eax\n"
          "  popfl\n"
          "  pushfl\n"
          "  popl   %%eax\n"
          "  movl   $0,%0\n"
          "  cmpl   %%eax,%%ecx\n"
          "  je     1f\n"
          "  movl   $1,%0\n"
          "1:"
        : "=r" (__cpuid_supported) : : "eax", "ecx");
    if (!__cpuid_supported)
        return 0;
#endif

    __cpuid(__level, __eax, __ebx, __ecx, __edx);
    if (__sig)
        *__sig = __ebx;
    return __eax;
}
