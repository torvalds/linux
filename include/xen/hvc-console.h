#ifndef XEN_HVC_CONSOLE_H
#define XEN_HVC_CONSOLE_H

extern struct console xenboot_console;

#ifdef CONFIG_HVC_XEN
void xen_console_resume(void);
#else
static inline void xen_console_resume(void) { }
#endif

void xen_raw_console_write(const char *str);
void xen_raw_printk(const char *fmt, ...);

#endif	/* XEN_HVC_CONSOLE_H */
