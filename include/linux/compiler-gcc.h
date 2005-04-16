/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/*
 * Common definitions for all gcc versions go here.
 */


/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")

/* This macro obfuscates arithmetic on a variable address so that gcc
   shouldn't recognize the original var, and make assumptions about it */
#define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
    __asm__ ("" : "=g"(__ptr) : "0"(ptr));		\
    (typeof(ptr)) (__ptr + (off)); })
