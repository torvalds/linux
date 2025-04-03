# SPDX-License-Identifier: GPL-2.0

# Where to place rustdoc generated documentation
rustdoc_output := $(objtree)/Documentation/output/rust/rustdoc

obj-$(CONFIG_RUST) += core.o compiler_builtins.o ffi.o
always-$(CONFIG_RUST) += exports_core_generated.h

# Missing prototypes are expected in the helpers since these are exported
# for Rust only, thus there is no header nor prototypes.
obj-$(CONFIG_RUST) += helpers/helpers.o
CFLAGS_REMOVE_helpers/helpers.o = -Wmissing-prototypes -Wmissing-declarations

always-$(CONFIG_RUST) += bindings/bindings_generated.rs bindings/bindings_helpers_generated.rs
obj-$(CONFIG_RUST) += bindings.o pin_init.o kernel.o
always-$(CONFIG_RUST) += exports_helpers_generated.h \
    exports_bindings_generated.h exports_kernel_generated.h

always-$(CONFIG_RUST) += uapi/uapi_generated.rs
obj-$(CONFIG_RUST) += uapi.o

ifdef CONFIG_RUST_BUILD_ASSERT_ALLOW
obj-$(CONFIG_RUST) += build_error.o
else
always-$(CONFIG_RUST) += build_error.o
endif

obj-$(CONFIG_RUST) += exports.o

always-$(CONFIG_RUST_KERNEL_DOCTESTS) += doctests_kernel_generated.rs
always-$(CONFIG_RUST_KERNEL_DOCTESTS) += doctests_kernel_generated_kunit.c

obj-$(CONFIG_RUST_KERNEL_DOCTESTS) += doctests_kernel_generated.o
obj-$(CONFIG_RUST_KERNEL_DOCTESTS) += doctests_kernel_generated_kunit.o

always-$(subst y,$(CONFIG_RUST),$(CONFIG_JUMP_LABEL)) += kernel/generated_arch_static_branch_asm.rs

# Avoids running `$(RUSTC)` when it may not be available.
ifdef CONFIG_RUST

libmacros_name := $(shell MAKEFLAGS= $(RUSTC) --print file-names --crate-name macros --crate-type proc-macro - </dev/null)
libmacros_extension := $(patsubst libmacros.%,%,$(libmacros_name))

libpin_init_internal_name := $(shell MAKEFLAGS= $(RUSTC) --print file-names --crate-name pin_init_internal --crate-type proc-macro - </dev/null)
libpin_init_internal_extension := $(patsubst libpin_init_internal.%,%,$(libpin_init_internal_name))

always-$(CONFIG_RUST) += $(libmacros_name) $(libpin_init_internal_name)

# `$(rust_flags)` is passed in case the user added `--sysroot`.
rustc_sysroot := $(shell MAKEFLAGS= $(RUSTC) $(rust_flags) --print sysroot)
rustc_host_target := $(shell $(RUSTC) --version --verbose | grep -F 'host: ' | cut -d' ' -f2)
RUST_LIB_SRC ?= $(rustc_sysroot)/lib/rustlib/src/rust/library

ifneq ($(quiet),)
rust_test_quiet=-q
rustdoc_test_quiet=--test-args -q
rustdoc_test_kernel_quiet=>/dev/null
endif

core-cfgs = \
    --cfg no_fp_fmt_parse

quiet_cmd_rustdoc = RUSTDOC $(if $(rustdoc_host),H, ) $<
      cmd_rustdoc = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) $(filter-out $(skip_flags),$(if $(rustdoc_host),$(rust_common_flags),$(rust_flags))) \
		$(rustc_target_flags) -L$(objtree)/$(obj) \
		-Zunstable-options --generate-link-to-definition \
		--output $(rustdoc_output) \
		--crate-name $(subst rustdoc-,,$@) \
		$(if $(rustdoc_host),,--sysroot=/dev/null) \
		@$(objtree)/include/generated/rustc_cfg $<

# The `html_logo_url` and `html_favicon_url` forms of the `doc` attribute
# can be used to specify a custom logo. However:
#   - The given value is used as-is, thus it cannot be relative or a local file
#     (unlike the non-custom case) since the generated docs have subfolders.
#   - It requires adding it to every crate.
#   - It requires changing `core` which comes from the sysroot.
#
# Using `-Zcrate-attr` would solve the last two points, but not the first.
# The https://github.com/rust-lang/rfcs/pull/3226 RFC suggests two new
# command-like flags to solve the issue. Meanwhile, we use the non-custom case
# and then retouch the generated files.
rustdoc: rustdoc-core rustdoc-macros rustdoc-compiler_builtins \
    rustdoc-kernel rustdoc-pin_init
	$(Q)cp $(srctree)/Documentation/images/logo.svg $(rustdoc_output)/static.files/
	$(Q)cp $(srctree)/Documentation/images/COPYING-logo $(rustdoc_output)/static.files/
	$(Q)find $(rustdoc_output) -name '*.html' -type f -print0 | xargs -0 sed -Ei \
		-e 's:rust-logo-[0-9a-f]+\.svg:logo.svg:g' \
		-e 's:favicon-[0-9a-f]+\.svg:logo.svg:g' \
		-e 's:<link rel="alternate icon" type="image/png" href="[/.]+/static\.files/favicon-(16x16|32x32)-[0-9a-f]+\.png">::g' \
		-e 's:<a href="srctree/([^"]+)">:<a href="$(realpath $(srctree))/\1">:g'
	$(Q)for f in $(rustdoc_output)/static.files/rustdoc-*.css; do \
		echo ".logo-container > img { object-fit: contain; }" >> $$f; done

rustdoc-macros: private rustdoc_host = yes
rustdoc-macros: private rustc_target_flags = --crate-type proc-macro \
    --extern proc_macro
rustdoc-macros: $(src)/macros/lib.rs FORCE
	+$(call if_changed,rustdoc)

# Starting with Rust 1.82.0, skipping `-Wrustdoc::unescaped_backticks` should
# not be needed -- see https://github.com/rust-lang/rust/pull/128307.
rustdoc-core: private skip_flags = -Wrustdoc::unescaped_backticks
rustdoc-core: private rustc_target_flags = $(core-cfgs)
rustdoc-core: $(RUST_LIB_SRC)/core/src/lib.rs FORCE
	+$(call if_changed,rustdoc)

rustdoc-compiler_builtins: $(src)/compiler_builtins.rs rustdoc-core FORCE
	+$(call if_changed,rustdoc)

rustdoc-ffi: $(src)/ffi.rs rustdoc-core FORCE
	+$(call if_changed,rustdoc)

rustdoc-pin_init_internal: private rustdoc_host = yes
rustdoc-pin_init_internal: private rustc_target_flags = --cfg kernel \
    --extern proc_macro --crate-type proc-macro
rustdoc-pin_init_internal: $(src)/pin-init/internal/src/lib.rs FORCE
	+$(call if_changed,rustdoc)

rustdoc-pin_init: private rustdoc_host = yes
rustdoc-pin_init: private rustc_target_flags = --extern pin_init_internal \
    --extern macros --extern alloc --cfg kernel --cfg feature=\"alloc\"
rustdoc-pin_init: $(src)/pin-init/src/lib.rs rustdoc-pin_init_internal \
    rustdoc-macros FORCE
	+$(call if_changed,rustdoc)

rustdoc-kernel: private rustc_target_flags = --extern ffi --extern pin_init \
    --extern build_error --extern macros \
    --extern bindings --extern uapi
rustdoc-kernel: $(src)/kernel/lib.rs rustdoc-core rustdoc-ffi rustdoc-macros \
    rustdoc-pin_init rustdoc-compiler_builtins $(obj)/$(libmacros_name) \
    $(obj)/bindings.o FORCE
	+$(call if_changed,rustdoc)

quiet_cmd_rustc_test_library = $(RUSTC_OR_CLIPPY_QUIET) TL $<
      cmd_rustc_test_library = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTC_OR_CLIPPY) $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg $(rustc_target_flags) \
		--crate-type $(if $(rustc_test_library_proc),proc-macro,rlib) \
		--out-dir $(objtree)/$(obj)/test --cfg testlib \
		-L$(objtree)/$(obj)/test \
		--crate-name $(subst rusttest-,,$(subst rusttestlib-,,$@)) $<

rusttestlib-build_error: $(src)/build_error.rs FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-ffi: $(src)/ffi.rs FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-macros: private rustc_target_flags = --extern proc_macro
rusttestlib-macros: private rustc_test_library_proc = yes
rusttestlib-macros: $(src)/macros/lib.rs FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-pin_init_internal: private rustc_target_flags = --cfg kernel \
    --extern proc_macro
rusttestlib-pin_init_internal: private rustc_test_library_proc = yes
rusttestlib-pin_init_internal: $(src)/pin-init/internal/src/lib.rs FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-pin_init: private rustc_target_flags = --extern pin_init_internal \
    --extern macros --cfg kernel
rusttestlib-pin_init: $(src)/pin-init/src/lib.rs rusttestlib-macros \
    rusttestlib-pin_init_internal $(obj)/$(libpin_init_internal_name) FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-kernel: private rustc_target_flags = --extern ffi \
    --extern build_error --extern macros --extern pin_init \
    --extern bindings --extern uapi
rusttestlib-kernel: $(src)/kernel/lib.rs rusttestlib-bindings rusttestlib-uapi \
    rusttestlib-build_error rusttestlib-pin_init $(obj)/$(libmacros_name) \
    $(obj)/bindings.o FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-bindings: private rustc_target_flags = --extern ffi
rusttestlib-bindings: $(src)/bindings/lib.rs rusttestlib-ffi FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-uapi: private rustc_target_flags = --extern ffi
rusttestlib-uapi: $(src)/uapi/lib.rs rusttestlib-ffi FORCE
	+$(call if_changed,rustc_test_library)

quiet_cmd_rustdoc_test = RUSTDOC T $<
      cmd_rustdoc_test = \
	RUST_MODFILE=test.rs \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) --test $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg \
		$(rustc_target_flags) $(rustdoc_test_target_flags) \
		$(rustdoc_test_quiet) \
		-L$(objtree)/$(obj)/test --output $(rustdoc_output) \
		--crate-name $(subst rusttest-,,$@) $<

quiet_cmd_rustdoc_test_kernel = RUSTDOC TK $<
      cmd_rustdoc_test_kernel = \
	rm -rf $(objtree)/$(obj)/test/doctests/kernel; \
	mkdir -p $(objtree)/$(obj)/test/doctests/kernel; \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) --test $(rust_flags) \
		-L$(objtree)/$(obj) --extern ffi --extern pin_init \
		--extern kernel --extern build_error --extern macros \
		--extern bindings --extern uapi \
		--no-run --crate-name kernel -Zunstable-options \
		--sysroot=/dev/null \
		--test-builder $(objtree)/scripts/rustdoc_test_builder \
		$< $(rustdoc_test_kernel_quiet); \
	$(objtree)/scripts/rustdoc_test_gen

%/doctests_kernel_generated.rs %/doctests_kernel_generated_kunit.c: \
    $(src)/kernel/lib.rs $(obj)/kernel.o \
    $(objtree)/scripts/rustdoc_test_builder \
    $(objtree)/scripts/rustdoc_test_gen FORCE
	+$(call if_changed,rustdoc_test_kernel)

# We cannot use `-Zpanic-abort-tests` because some tests are dynamic,
# so for the moment we skip `-Cpanic=abort`.
quiet_cmd_rustc_test = $(RUSTC_OR_CLIPPY_QUIET) T  $<
      cmd_rustc_test = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTC_OR_CLIPPY) --test $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg \
		$(rustc_target_flags) --out-dir $(objtree)/$(obj)/test \
		-L$(objtree)/$(obj)/test \
		--crate-name $(subst rusttest-,,$@) $<; \
	$(objtree)/$(obj)/test/$(subst rusttest-,,$@) $(rust_test_quiet) \
		$(rustc_test_run_flags)

rusttest: rusttest-macros rusttest-kernel

rusttest-macros: private rustc_target_flags = --extern proc_macro \
	--extern macros --extern kernel --extern pin_init
rusttest-macros: private rustdoc_test_target_flags = --crate-type proc-macro
rusttest-macros: $(src)/macros/lib.rs \
    rusttestlib-macros rusttestlib-kernel rusttestlib-pin_init FORCE
	+$(call if_changed,rustc_test)
	+$(call if_changed,rustdoc_test)

rusttest-kernel: private rustc_target_flags = --extern ffi --extern pin_init \
    --extern build_error --extern macros --extern bindings --extern uapi
rusttest-kernel: $(src)/kernel/lib.rs rusttestlib-ffi rusttestlib-kernel \
    rusttestlib-build_error rusttestlib-macros rusttestlib-bindings \
    rusttestlib-uapi rusttestlib-pin_init FORCE
	+$(call if_changed,rustc_test)

ifdef CONFIG_CC_IS_CLANG
bindgen_c_flags = $(c_flags)
else
# bindgen relies on libclang to parse C. Ideally, bindgen would support a GCC
# plugin backend and/or the Clang driver would be perfectly compatible with GCC.
#
# For the moment, here we are tweaking the flags on the fly. This is a hack,
# and some kernel configurations may not work (e.g. `GCC_PLUGIN_RANDSTRUCT`
# if we end up using one of those structs).
bindgen_skip_c_flags := -mno-fp-ret-in-387 -mpreferred-stack-boundary=% \
	-mskip-rax-setup -mgeneral-regs-only -msign-return-address=% \
	-mindirect-branch=thunk-extern -mindirect-branch-register \
	-mfunction-return=thunk-extern -mrecord-mcount -mabi=lp64 \
	-mindirect-branch-cs-prefix -mstack-protector-guard% -mtraceback=no \
	-mno-pointers-to-nested-functions -mno-string \
	-mno-strict-align -mstrict-align -mdirect-extern-access \
	-mexplicit-relocs -mno-check-zero-division \
	-fconserve-stack -falign-jumps=% -falign-loops=% \
	-femit-struct-debug-baseonly -fno-ipa-cp-clone -fno-ipa-sra \
	-fno-partial-inlining -fplugin-arg-arm_ssp_per_task_plugin-% \
	-fno-reorder-blocks -fno-allow-store-data-races -fasan-shadow-offset=% \
	-fzero-call-used-regs=% -fno-stack-clash-protection \
	-fno-inline-functions-called-once -fsanitize=bounds-strict \
	-fstrict-flex-arrays=% -fmin-function-alignment=% \
	-fzero-init-padding-bits=% \
	--param=% --param asan-%

# Derived from `scripts/Makefile.clang`.
BINDGEN_TARGET_x86	:= x86_64-linux-gnu
BINDGEN_TARGET_arm64	:= aarch64-linux-gnu
BINDGEN_TARGET_arm	:= arm-linux-gnueabi
BINDGEN_TARGET_loongarch	:= loongarch64-linux-gnusf
BINDGEN_TARGET_um	:= $(BINDGEN_TARGET_$(SUBARCH))
BINDGEN_TARGET		:= $(BINDGEN_TARGET_$(SRCARCH))

# All warnings are inhibited since GCC builds are very experimental,
# many GCC warnings are not supported by Clang, they may only appear in
# some configurations, with new GCC versions, etc.
bindgen_extra_c_flags = -w --target=$(BINDGEN_TARGET)

# Auto variable zero-initialization requires an additional special option with
# clang that is going to be removed sometime in the future (likely in
# clang-18), so make sure to pass this option only if clang supports it
# (libclang major version < 16).
#
# https://github.com/llvm/llvm-project/issues/44842
# https://github.com/llvm/llvm-project/blob/llvmorg-16.0.0-rc2/clang/docs/ReleaseNotes.rst#deprecated-compiler-flags
ifdef CONFIG_INIT_STACK_ALL_ZERO
libclang_maj_ver=$(shell $(BINDGEN) $(srctree)/scripts/rust_is_available_bindgen_libclang.h 2>&1 | sed -ne 's/.*clang version \([0-9]*\).*/\1/p')
ifeq ($(shell expr $(libclang_maj_ver) \< 16), 1)
bindgen_extra_c_flags += -enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang
endif
endif

bindgen_c_flags = $(filter-out $(bindgen_skip_c_flags), $(c_flags)) \
	$(bindgen_extra_c_flags)
endif

ifdef CONFIG_LTO
bindgen_c_flags_lto = $(filter-out $(CC_FLAGS_LTO), $(bindgen_c_flags))
else
bindgen_c_flags_lto = $(bindgen_c_flags)
endif

# `-fno-builtin` is passed to avoid `bindgen` from using `clang` builtin
# prototypes for functions like `memcpy` -- if this flag is not passed,
# `bindgen`-generated prototypes use `c_ulong` or `c_uint` depending on
# architecture instead of generating `usize`.
bindgen_c_flags_final = $(bindgen_c_flags_lto) -fno-builtin -D__BINDGEN__

# Each `bindgen` release may upgrade the list of Rust target versions. By
# default, the highest stable release in their list is used. Thus we need to set
# a `--rust-target` to avoid future `bindgen` releases emitting code that
# `rustc` may not understand. On top of that, `bindgen` does not support passing
# an unknown Rust target version.
#
# Therefore, the Rust target for `bindgen` can be only as high as the minimum
# Rust version the kernel supports and only as high as the greatest stable Rust
# target supported by the minimum `bindgen` version the kernel supports (that
# is, if we do not test the actual `rustc`/`bindgen` versions running).
#
# Starting with `bindgen` 0.71.0, we will be able to set any future Rust version
# instead, i.e. we will be able to set here our minimum supported Rust version.
quiet_cmd_bindgen = BINDGEN $@
      cmd_bindgen = \
	$(BINDGEN) $< $(bindgen_target_flags) --rust-target 1.68 \
		--use-core --with-derive-default --ctypes-prefix ffi --no-layout-tests \
		--no-debug '.*' --enable-function-attribute-detection \
		-o $@ -- $(bindgen_c_flags_final) -DMODULE \
		$(bindgen_target_cflags) $(bindgen_target_extra)

$(obj)/bindings/bindings_generated.rs: private bindgen_target_flags = \
    $(shell grep -Ev '^#|^$$' $(src)/bindgen_parameters)
$(obj)/bindings/bindings_generated.rs: private bindgen_target_extra = ; \
    sed -Ei 's/pub const RUST_CONST_HELPER_([a-zA-Z0-9_]*)/pub const \1/g' $@
$(obj)/bindings/bindings_generated.rs: $(src)/bindings/bindings_helper.h \
    $(src)/bindgen_parameters FORCE
	$(call if_changed_dep,bindgen)

$(obj)/uapi/uapi_generated.rs: private bindgen_target_flags = \
    $(shell grep -Ev '^#|^$$' $(src)/bindgen_parameters)
$(obj)/uapi/uapi_generated.rs: $(src)/uapi/uapi_helper.h \
    $(src)/bindgen_parameters FORCE
	$(call if_changed_dep,bindgen)

# See `CFLAGS_REMOVE_helpers.o` above. In addition, Clang on C does not warn
# with `-Wmissing-declarations` (unlike GCC), so it is not strictly needed here
# given it is `libclang`; but for consistency, future Clang changes and/or
# a potential future GCC backend for `bindgen`, we disable it too.
$(obj)/bindings/bindings_helpers_generated.rs: private bindgen_target_flags = \
    --blocklist-type '.*' --allowlist-var '' \
    --allowlist-function 'rust_helper_.*'
$(obj)/bindings/bindings_helpers_generated.rs: private bindgen_target_cflags = \
    -I$(objtree)/$(obj) -Wno-missing-prototypes -Wno-missing-declarations
$(obj)/bindings/bindings_helpers_generated.rs: private bindgen_target_extra = ; \
    sed -Ei 's/pub fn rust_helper_([a-zA-Z0-9_]*)/#[link_name="rust_helper_\1"]\n    pub fn \1/g' $@
$(obj)/bindings/bindings_helpers_generated.rs: $(src)/helpers/helpers.c FORCE
	$(call if_changed_dep,bindgen)

rust_exports = $(NM) -p --defined-only $(1) | awk '$$2~/(T|R|D|B)/ && $$3!~/__cfi/ && $$3!~/__odr_asan/ { printf $(2),$$3 }'

quiet_cmd_exports = EXPORTS $@
      cmd_exports = \
	$(call rust_exports,$<,"EXPORT_SYMBOL_RUST_GPL(%s);\n") > $@

$(obj)/exports_core_generated.h: $(obj)/core.o FORCE
	$(call if_changed,exports)

# Even though Rust kernel modules should never use the bindings directly,
# symbols from the `bindings` crate and the C helpers need to be exported
# because Rust generics and inlined functions may not get their code generated
# in the crate where they are defined. Other helpers, called from non-inline
# functions, may not be exported, in principle. However, in general, the Rust
# compiler does not guarantee codegen will be performed for a non-inline
# function either. Therefore, we export all symbols from helpers and bindings.
# In the future, this may be revisited to reduce the number of exports after
# the compiler is informed about the places codegen is required.
$(obj)/exports_helpers_generated.h: $(obj)/helpers/helpers.o FORCE
	$(call if_changed,exports)

$(obj)/exports_bindings_generated.h: $(obj)/bindings.o FORCE
	$(call if_changed,exports)

$(obj)/exports_kernel_generated.h: $(obj)/kernel.o FORCE
	$(call if_changed,exports)

quiet_cmd_rustc_procmacro = $(RUSTC_OR_CLIPPY_QUIET) P $@
      cmd_rustc_procmacro = \
	$(RUSTC_OR_CLIPPY) $(rust_common_flags) $(rustc_target_flags) \
		-Clinker-flavor=gcc -Clinker=$(HOSTCC) \
		-Clink-args='$(call escsq,$(KBUILD_PROCMACROLDFLAGS))' \
		--emit=dep-info=$(depfile) --emit=link=$@ --extern proc_macro \
		--crate-type proc-macro \
		--crate-name $(patsubst lib%.$(libmacros_extension),%,$(notdir $@)) $<

# Procedural macros can only be used with the `rustc` that compiled it.
$(obj)/$(libmacros_name): $(src)/macros/lib.rs FORCE
	+$(call if_changed_dep,rustc_procmacro)

$(obj)/$(libpin_init_internal_name): private rustc_target_flags = --cfg kernel
$(obj)/$(libpin_init_internal_name): $(src)/pin-init/internal/src/lib.rs FORCE
	+$(call if_changed_dep,rustc_procmacro)

quiet_cmd_rustc_library = $(if $(skip_clippy),RUSTC,$(RUSTC_OR_CLIPPY_QUIET)) L $@
      cmd_rustc_library = \
	OBJTREE=$(abspath $(objtree)) \
	$(if $(skip_clippy),$(RUSTC),$(RUSTC_OR_CLIPPY)) \
		$(filter-out $(skip_flags),$(rust_flags) $(rustc_target_flags)) \
		--emit=dep-info=$(depfile) --emit=obj=$@ \
		--emit=metadata=$(dir $@)$(patsubst %.o,lib%.rmeta,$(notdir $@)) \
		--crate-type rlib -L$(objtree)/$(obj) \
		--crate-name $(patsubst %.o,%,$(notdir $@)) $< \
		--sysroot=/dev/null \
	$(if $(rustc_objcopy),;$(OBJCOPY) $(rustc_objcopy) $@) \
	$(cmd_objtool)

rust-analyzer:
	$(Q)MAKEFLAGS= $(srctree)/scripts/generate_rust_analyzer.py \
		--cfgs='core=$(core-cfgs)' \
		$(realpath $(srctree)) $(realpath $(objtree)) \
		$(rustc_sysroot) $(RUST_LIB_SRC) $(if $(KBUILD_EXTMOD),$(srcroot)) \
		> rust-project.json

redirect-intrinsics = \
	__addsf3 __eqsf2 __extendsfdf2 __gesf2 __lesf2 __ltsf2 __mulsf3 __nesf2 __truncdfsf2 __unordsf2 \
	__adddf3 __eqdf2 __ledf2 __ltdf2 __muldf3 __unorddf2 \
	__muloti4 __multi3 \
	__udivmodti4 __udivti3 __umodti3

ifdef CONFIG_ARM
	# Add eabi initrinsics for ARM 32-bit
	redirect-intrinsics += \
		__aeabi_fadd __aeabi_fmul __aeabi_fcmpeq __aeabi_fcmple __aeabi_fcmplt __aeabi_fcmpun \
		__aeabi_dadd __aeabi_dmul __aeabi_dcmple __aeabi_dcmplt __aeabi_dcmpun \
		__aeabi_uldivmod
endif
ifneq ($(or $(CONFIG_ARM64),$(and $(CONFIG_RISCV),$(CONFIG_64BIT))),)
	# These intrinsics are defined for ARM64 and RISCV64
	redirect-intrinsics += \
		__ashrti3 \
		__ashlti3 __lshrti3
endif

ifdef CONFIG_MODVERSIONS
cmd_gendwarfksyms = $(if $(skip_gendwarfksyms),, \
	$(call rust_exports,$@,"%s\n") | \
	scripts/gendwarfksyms/gendwarfksyms \
		$(if $(KBUILD_GENDWARFKSYMS_STABLE), --stable) \
		$(if $(KBUILD_SYMTYPES), --symtypes $(@:.o=.symtypes),) \
		$@ >> $(dot-target).cmd)
endif

define rule_rustc_library
	$(call cmd_and_fixdep,rustc_library)
	$(call cmd,gen_objtooldep)
	$(call cmd,gendwarfksyms)
endef

define rule_rust_cc_library
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)
	$(call cmd,gendwarfksyms)
endef

# helpers.o uses the same export mechanism as Rust libraries, so ensure symbol
# versions are calculated for the helpers too.
$(obj)/helpers/helpers.o: $(src)/helpers/helpers.c $(recordmcount_source) FORCE
	+$(call if_changed_rule,rust_cc_library)

# Disable symbol versioning for exports.o to avoid conflicts with the actual
# symbol versions generated from Rust objects.
$(obj)/exports.o: private skip_gendwarfksyms = 1

$(obj)/core.o: private skip_clippy = 1
$(obj)/core.o: private skip_flags = -Wunreachable_pub
$(obj)/core.o: private rustc_objcopy = $(foreach sym,$(redirect-intrinsics),--redefine-sym $(sym)=__rust$(sym))
$(obj)/core.o: private rustc_target_flags = $(core-cfgs)
$(obj)/core.o: $(RUST_LIB_SRC)/core/src/lib.rs \
    $(wildcard $(objtree)/include/config/RUSTC_VERSION_TEXT) FORCE
	+$(call if_changed_rule,rustc_library)
ifneq ($(or $(CONFIG_X86_64),$(CONFIG_X86_32)),)
$(obj)/core.o: scripts/target.json
endif

$(obj)/compiler_builtins.o: private skip_gendwarfksyms = 1
$(obj)/compiler_builtins.o: private rustc_objcopy = -w -W '__*'
$(obj)/compiler_builtins.o: $(src)/compiler_builtins.rs $(obj)/core.o FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/pin_init.o: private skip_gendwarfksyms = 1
$(obj)/pin_init.o: private rustc_target_flags = --extern pin_init_internal \
    --extern macros --cfg kernel
$(obj)/pin_init.o: $(src)/pin-init/src/lib.rs $(obj)/compiler_builtins.o \
    $(obj)/$(libpin_init_internal_name) $(obj)/$(libmacros_name) FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/build_error.o: private skip_gendwarfksyms = 1
$(obj)/build_error.o: $(src)/build_error.rs $(obj)/compiler_builtins.o FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/ffi.o: private skip_gendwarfksyms = 1
$(obj)/ffi.o: $(src)/ffi.rs $(obj)/compiler_builtins.o FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/bindings.o: private rustc_target_flags = --extern ffi
$(obj)/bindings.o: $(src)/bindings/lib.rs \
    $(obj)/ffi.o \
    $(obj)/bindings/bindings_generated.rs \
    $(obj)/bindings/bindings_helpers_generated.rs FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/uapi.o: private rustc_target_flags = --extern ffi
$(obj)/uapi.o: private skip_gendwarfksyms = 1
$(obj)/uapi.o: $(src)/uapi/lib.rs \
    $(obj)/ffi.o \
    $(obj)/uapi/uapi_generated.rs FORCE
	+$(call if_changed_rule,rustc_library)

$(obj)/kernel.o: private rustc_target_flags = --extern ffi --extern pin_init \
    --extern build_error --extern macros --extern bindings --extern uapi
$(obj)/kernel.o: $(src)/kernel/lib.rs $(obj)/build_error.o $(obj)/pin_init.o \
    $(obj)/$(libmacros_name) $(obj)/bindings.o $(obj)/uapi.o FORCE
	+$(call if_changed_rule,rustc_library)

ifdef CONFIG_JUMP_LABEL
$(obj)/kernel.o: $(obj)/kernel/generated_arch_static_branch_asm.rs
endif

endif # CONFIG_RUST
