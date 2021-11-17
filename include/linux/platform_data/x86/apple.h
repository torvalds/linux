#ifndef PLATFORM_DATA_X86_APPLE_H
#define PLATFORM_DATA_X86_APPLE_H

#ifdef CONFIG_X86
/**
 * x86_apple_machine - whether the machine is an x86 Apple Macintosh
 */
extern bool x86_apple_machine;
#else
#define x86_apple_machine false
#endif

#endif
