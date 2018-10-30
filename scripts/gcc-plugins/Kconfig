preferred-plugin-hostcc := $(if-success,[ $(gcc-version) -ge 40800 ],$(HOSTCXX),$(HOSTCC))

config PLUGIN_HOSTCC
	string
	default "$(shell,$(srctree)/scripts/gcc-plugin.sh "$(preferred-plugin-hostcc)" "$(HOSTCXX)" "$(CC)")" if CC_IS_GCC
	help
	  Host compiler used to build GCC plugins.  This can be $(HOSTCXX),
	  $(HOSTCC), or a null string if GCC plugin is unsupported.

config HAVE_GCC_PLUGINS
	bool
	help
	  An arch should select this symbol if it supports building with
	  GCC plugins.

menuconfig GCC_PLUGINS
	bool "GCC plugins"
	depends on HAVE_GCC_PLUGINS
	depends on PLUGIN_HOSTCC != ""
	help
	  GCC plugins are loadable modules that provide extra features to the
	  compiler. They are useful for runtime instrumentation and static analysis.

	  See Documentation/gcc-plugins.txt for details.

if GCC_PLUGINS

config GCC_PLUGIN_CYC_COMPLEXITY
	bool "Compute the cyclomatic complexity of a function" if EXPERT
	depends on !COMPILE_TEST	# too noisy
	help
	  The complexity M of a function's control flow graph is defined as:
	   M = E - N + 2P
	  where

	  E = the number of edges
	  N = the number of nodes
	  P = the number of connected components (exit nodes).

	  Enabling this plugin reports the complexity to stderr during the
	  build. It mainly serves as a simple example of how to create a
	  gcc plugin for the kernel.

config GCC_PLUGIN_SANCOV
	bool
	help
	  This plugin inserts a __sanitizer_cov_trace_pc() call at the start of
	  basic blocks. It supports all gcc versions with plugin support (from
	  gcc-4.5 on). It is based on the commit "Add fuzzing coverage support"
	  by Dmitry Vyukov <dvyukov@google.com>.

config GCC_PLUGIN_LATENT_ENTROPY
	bool "Generate some entropy during boot and runtime"
	help
	  By saying Y here the kernel will instrument some kernel code to
	  extract some entropy from both original and artificially created
	  program state.  This will help especially embedded systems where
	  there is little 'natural' source of entropy normally.  The cost
	  is some slowdown of the boot process (about 0.5%) and fork and
	  irq processing.

	  Note that entropy extracted this way is not cryptographically
	  secure!

	  This plugin was ported from grsecurity/PaX. More information at:
	   * https://grsecurity.net/
	   * https://pax.grsecurity.net/

config GCC_PLUGIN_STRUCTLEAK
	bool "Force initialization of variables containing userspace addresses"
	# Currently STRUCTLEAK inserts initialization out of live scope of
	# variables from KASAN point of view. This leads to KASAN false
	# positive reports. Prohibit this combination for now.
	depends on !KASAN_EXTRA
	help
	  This plugin zero-initializes any structures containing a
	  __user attribute. This can prevent some classes of information
	  exposures.

	  This plugin was ported from grsecurity/PaX. More information at:
	   * https://grsecurity.net/
	   * https://pax.grsecurity.net/

config GCC_PLUGIN_STRUCTLEAK_BYREF_ALL
	bool "Force initialize all struct type variables passed by reference"
	depends on GCC_PLUGIN_STRUCTLEAK
	depends on !COMPILE_TEST
	help
	  Zero initialize any struct type local variable that may be passed by
	  reference without having been initialized.

config GCC_PLUGIN_STRUCTLEAK_VERBOSE
	bool "Report forcefully initialized variables"
	depends on GCC_PLUGIN_STRUCTLEAK
	depends on !COMPILE_TEST	# too noisy
	help
	  This option will cause a warning to be printed each time the
	  structleak plugin finds a variable it thinks needs to be
	  initialized. Since not all existing initializers are detected
	  by the plugin, this can produce false positive warnings.

config GCC_PLUGIN_RANDSTRUCT
	bool "Randomize layout of sensitive kernel structures"
	select MODVERSIONS if MODULES
	help
	  If you say Y here, the layouts of structures that are entirely
	  function pointers (and have not been manually annotated with
	  __no_randomize_layout), or structures that have been explicitly
	  marked with __randomize_layout, will be randomized at compile-time.
	  This can introduce the requirement of an additional information
	  exposure vulnerability for exploits targeting these structure
	  types.

	  Enabling this feature will introduce some performance impact,
	  slightly increase memory usage, and prevent the use of forensic
	  tools like Volatility against the system (unless the kernel
	  source tree isn't cleaned after kernel installation).

	  The seed used for compilation is located at
	  scripts/gcc-plgins/randomize_layout_seed.h.  It remains after
	  a make clean to allow for external modules to be compiled with
	  the existing seed and will be removed by a make mrproper or
	  make distclean.

	  Note that the implementation requires gcc 4.7 or newer.

	  This plugin was ported from grsecurity/PaX. More information at:
	   * https://grsecurity.net/
	   * https://pax.grsecurity.net/

config GCC_PLUGIN_RANDSTRUCT_PERFORMANCE
	bool "Use cacheline-aware structure randomization"
	depends on GCC_PLUGIN_RANDSTRUCT
	depends on !COMPILE_TEST	# do not reduce test coverage
	help
	  If you say Y here, the RANDSTRUCT randomization will make a
	  best effort at restricting randomization to cacheline-sized
	  groups of elements.  It will further not randomize bitfields
	  in structures.  This reduces the performance hit of RANDSTRUCT
	  at the cost of weakened randomization.

endif
