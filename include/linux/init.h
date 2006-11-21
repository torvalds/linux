#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata between the variable name and equal
 * sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * Don't forget to initialize data not at file scope, i.e. within a function,
 * as gcc otherwise puts the data into the bss section and not into the init
 * section.
 * 
 * Also note, that this data cannot be "const".
 */

/* These are for everybody (although not all archs will actually
   discard it in modules) */
#define __init		__attribute__ ((__section__ (".init.text")))
#define __initdata	__attribute__ ((__section__ (".init.data")))
#define __exitdata	__attribute__ ((__section__(".exit.data")))
#define __exit_call	__attribute_used__ __attribute__ ((__section__ (".exitcall.exit")))

#ifdef MODULE
#define __exit		__attribute__ ((__section__(".exit.text")))
#else
#define __exit		__attribute_used__ __attribute__ ((__section__(".exit.text")))
#endif

/* For assembly routines */
#define __INIT		.section	".init.text","ax"
#define __FINIT		.previous
#define __INITDATA	.section	".init.data","aw"

#ifndef __ASSEMBLY__
/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);

extern initcall_t __con_initcall_start[], __con_initcall_end[];
extern initcall_t __security_initcall_start[], __security_initcall_end[];

/* Defined in init/main.c */
extern char saved_command_line[];
extern unsigned int reset_devices;

/* used by init/main.c */
extern void setup_arch(char **);

#endif
  
#ifndef MODULE

#ifndef __ASSEMBLY__

/* initcalls are now grouped by functionality into separate 
 * subsections. Ordering inside the subsections is determined
 * by link order. 
 * For backwards compatibility, initcall() puts the call in 
 * the device init subsection.
 *
 * The `id' arg to __define_initcall() is needed so that multiple initcalls
 * can point at the same handler without causing duplicate-symbol build errors.
 */

#define __define_initcall(level,fn,id) \
	static initcall_t __initcall_##fn##id __attribute_used__ \
	__attribute__((__section__(".initcall" level ".init"))) = fn

/*
 * A "pure" initcall has no dependencies on anything else, and purely
 * initializes variables that couldn't be statically initialized.
 *
 * This only exists for built-in code, not for modules.
 */
#define pure_initcall(fn)		__define_initcall("0",fn,1)

#define core_initcall(fn)		__define_initcall("1",fn,1)
#define core_initcall_sync(fn)		__define_initcall("1s",fn,1s)
#define postcore_initcall(fn)		__define_initcall("2",fn,2)
#define postcore_initcall_sync(fn)	__define_initcall("2s",fn,2s)
#define arch_initcall(fn)		__define_initcall("3",fn,3)
#define arch_initcall_sync(fn)		__define_initcall("3s",fn,3s)
#define subsys_initcall(fn)		__define_initcall("4",fn,4)
#define subsys_initcall_sync(fn)	__define_initcall("4s",fn,4s)
#define fs_initcall(fn)			__define_initcall("5",fn,5)
#define fs_initcall_sync(fn)		__define_initcall("5s",fn,5s)
#define device_initcall(fn)		__define_initcall("6",fn,6)
#define device_initcall_sync(fn)	__define_initcall("6s",fn,6s)
#define late_initcall(fn)		__define_initcall("7",fn,7)
#define late_initcall_sync(fn)		__define_initcall("7s",fn,7s)

#define __initcall(fn) device_initcall(fn)

#define __exitcall(fn) \
	static exitcall_t __exitcall_##fn __exit_call = fn

#define console_initcall(fn) \
	static initcall_t __initcall_##fn \
	__attribute_used__ __attribute__((__section__(".con_initcall.init")))=fn

#define security_initcall(fn) \
	static initcall_t __initcall_##fn \
	__attribute_used__ __attribute__((__section__(".security_initcall.init"))) = fn

struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};

/*
 * Only for really core code.  See moduleparam.h for the normal way.
 *
 * Force the alignment so the compiler doesn't space elements of the
 * obs_kernel_param "array" too far apart in .init.setup.
 */
#define __setup_param(str, unique_id, fn, early)			\
	static char __setup_str_##unique_id[] __initdata = str;	\
	static struct obs_kernel_param __setup_##unique_id	\
		__attribute_used__				\
		__attribute__((__section__(".init.setup")))	\
		__attribute__((aligned((sizeof(long)))))	\
		= { __setup_str_##unique_id, fn, early }

#define __setup_null_param(str, unique_id)			\
	__setup_param(str, unique_id, NULL, 0)

#define __setup(str, fn)					\
	__setup_param(str, fn, fn, 0)

#define __obsolete_setup(str)					\
	__setup_null_param(str, __LINE__)

/* NOTE: fn is as per module_param, not __setup!  Emits warning if fn
 * returns non-zero. */
#define early_param(str, fn)					\
	__setup_param(str, fn, fn, 1)

/* Relies on saved_command_line being set */
void __init parse_early_param(void);
#endif /* __ASSEMBLY__ */

/**
 * module_init() - driver initialization entry point
 * @x: function to be run at kernel boot time or module insertion
 * 
 * module_init() will either be called during do_initcalls (if
 * builtin) or at module insertion time (if a module).  There can only
 * be one per module.
 */
#define module_init(x)	__initcall(x);

/**
 * module_exit() - driver exit entry point
 * @x: function to be run when driver is removed
 * 
 * module_exit() will wrap the driver clean-up code
 * with cleanup_module() when used with rmmod when
 * the driver is a module.  If the driver is statically
 * compiled into the kernel, module_exit() has no effect.
 * There can only be one per module.
 */
#define module_exit(x)	__exitcall(x);

#else /* MODULE */

/* Don't use these in modules, but some people do... */
#define core_initcall(fn)		module_init(fn)
#define postcore_initcall(fn)		module_init(fn)
#define arch_initcall(fn)		module_init(fn)
#define subsys_initcall(fn)		module_init(fn)
#define fs_initcall(fn)			module_init(fn)
#define device_initcall(fn)		module_init(fn)
#define late_initcall(fn)		module_init(fn)

#define security_initcall(fn)		module_init(fn)

/* These macros create a dummy inline: gcc 2.9x does not count alias
 as usage, hence the `unused function' warning when __init functions
 are declared static. We use the dummy __*_module_inline functions
 both to kill the warning and check the type of the init/cleanup
 function. */

/* Each module must use one module_init(), or one no_module_init */
#define module_init(initfn)					\
	static inline initcall_t __inittest(void)		\
	{ return initfn; }					\
	int init_module(void) __attribute__((alias(#initfn)));

/* This is only required if you want to be unloadable. */
#define module_exit(exitfn)					\
	static inline exitcall_t __exittest(void)		\
	{ return exitfn; }					\
	void cleanup_module(void) __attribute__((alias(#exitfn)));

#define __setup_param(str, unique_id, fn)	/* nothing */
#define __setup_null_param(str, unique_id) 	/* nothing */
#define __setup(str, func) 			/* nothing */
#define __obsolete_setup(str) 			/* nothing */
#endif

/* Data marked not to be saved by software_suspend() */
#define __nosavedata __attribute__ ((__section__ (".data.nosave")))

/* This means "can be init if no module support, otherwise module load
   may call it." */
#ifdef CONFIG_MODULES
#define __init_or_module
#define __initdata_or_module
#else
#define __init_or_module __init
#define __initdata_or_module __initdata
#endif /*CONFIG_MODULES*/

#ifdef CONFIG_HOTPLUG
#define __devinit
#define __devinitdata
#define __devexit
#define __devexitdata
#else
#define __devinit __init
#define __devinitdata __initdata
#define __devexit __exit
#define __devexitdata __exitdata
#endif

#ifdef CONFIG_HOTPLUG_CPU
#define __cpuinit
#define __cpuinitdata
#define __cpuexit
#define __cpuexitdata
#else
#define __cpuinit	__init
#define __cpuinitdata __initdata
#define __cpuexit __exit
#define __cpuexitdata	__exitdata
#endif

#if defined(CONFIG_MEMORY_HOTPLUG) || defined(CONFIG_ACPI_HOTPLUG_MEMORY) \
	|| defined(CONFIG_ACPI_HOTPLUG_MEMORY_MODULE)
#define __meminit
#define __meminitdata
#define __memexit
#define __memexitdata
#else
#define __meminit	__init
#define __meminitdata __initdata
#define __memexit __exit
#define __memexitdata	__exitdata
#endif

/* Functions marked as __devexit may be discarded at kernel link time, depending
   on config options.  Newer versions of binutils detect references from
   retained sections to discarded sections and flag an error.  Pointers to
   __devexit functions must use __devexit_p(function_name), the wrapper will
   insert either the function_name or NULL, depending on the config options.
 */
#if defined(MODULE) || defined(CONFIG_HOTPLUG)
#define __devexit_p(x) x
#else
#define __devexit_p(x) NULL
#endif

#ifdef MODULE
#define __exit_p(x) x
#else
#define __exit_p(x) NULL
#endif

#endif /* _LINUX_INIT_H */
