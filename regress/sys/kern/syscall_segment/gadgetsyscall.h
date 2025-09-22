/*	$OpenBSD: gadgetsyscall.h,v 1.2 2020/10/20 14:40:00 kettenis Exp $	*/

pid_t gadget_getpid() {
	pid_t ans = 0;
#if defined(__aarch64__)
	asm("ldr x8, #0x14; svc 0; dsb nsh; isb; mov %w0, w0" : "=r"(ans) :: "x0", "x8");
#elif defined(__amd64__)
	asm("mov $0x14, %%eax; syscall; mov %%eax, %0;" :"=r"(ans)::"%eax", "%ecx", "%r11");
#endif
	return ans;
}
