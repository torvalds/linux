#ifndef XEN_HVC_CONSOLE_H
#define XEN_HVC_CONSOLE_H

extern struct console xenboot_console;

void xen_raw_console_write(const char *str);
void xen_raw_printk(const char *fmt, ...);

#endif	/* XEN_HVC_CONSOLE_H */
