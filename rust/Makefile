# SPDX-License-Identifier: GPL-2.0

# Where to place rustdoc generated documentation
rustdoc_output := $(objtree)/Documentation/output/rust/rustdoc

obj-$(CONFIG_RUST) += core.o compiler_builtins.o
always-$(CONFIG_RUST) += exports_core_generated.h

# Missing prototypes are expected in the helpers since these are exported
# for Rust only, thus there is no header nor prototypes.
obj-$(CONFIG_RUST) += helpers.o
CFLAGS_REMOVE_helpers.o = -Wmissing-prototypes -Wmissing-declarations

always-$(CONFIG_RUST) += libmacros.so
no-clean-files += libmacros.so

always-$(CONFIG_RUST) += bindings/bindings_generated.rs bindings/bindings_helpers_generated.rs
obj-$(CONFIG_RUST) += alloc.o bindings.o kernel.o
always-$(CONFIG_RUST) += exports_alloc_generated.h exports_bindings_generated.h \
    exports_kernel_generated.h

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

# Avoids running `$(RUSTC)` for the sysroot when it may not be available.
ifdef CONFIG_RUST

# `$(rust_flags)` is passed in case the user added `--sysroot`.
rustc_sysroot := $(shell MAKEFLAGS= $(RUSTC) $(rust_flags) --print sysroot)
rustc_host_target := $(shell $(RUSTC) --version --verbose | grep -F 'host: ' | cut -d' ' -f2)
RUST_LIB_SRC ?= $(rustc_sysroot)/lib/rustlib/src/rust/library

ifeq ($(quiet),silent_)
cargo_quiet=-q
rust_test_quiet=-q
rustdoc_test_quiet=--test-args -q
rustdoc_test_kernel_quiet=>/dev/null
else ifeq ($(quiet),quiet_)
rust_test_quiet=-q
rustdoc_test_quiet=--test-args -q
rustdoc_test_kernel_quiet=>/dev/null
else
cargo_quiet=--verbose
endif

core-cfgs = \
    --cfg no_fp_fmt_parse

alloc-cfgs = \
    --cfg no_borrow \
    --cfg no_fmt \
    --cfg no_global_oom_handling \
    --cfg no_macros \
    --cfg no_rc \
    --cfg no_str \
    --cfg no_string \
    --cfg no_sync \
    --cfg no_thin

quiet_cmd_rustdoc = RUSTDOC $(if $(rustdoc_host),H, ) $<
      cmd_rustdoc = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) $(if $(rustdoc_host),$(rust_common_flags),$(rust_flags)) \
		$(rustc_target_flags) -L$(objtree)/$(obj) \
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
    rustdoc-alloc rustdoc-kernel
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

rustdoc-core: private rustc_target_flags = $(core-cfgs)
rustdoc-core: $(RUST_LIB_SRC)/core/src/lib.rs FORCE
	+$(call if_changed,rustdoc)

rustdoc-compiler_builtins: $(src)/compiler_builtins.rs rustdoc-core FORCE
	+$(call if_changed,rustdoc)

# We need to allow `rustdoc::broken_intra_doc_links` because some
# `no_global_oom_handling` functions refer to non-`no_global_oom_handling`
# functions. Ideally `rustdoc` would have a way to distinguish broken links
# due to things that are "configured out" vs. entirely non-existing ones.
rustdoc-alloc: private rustc_target_flags = $(alloc-cfgs) \
    -Arustdoc::broken_intra_doc_links
rustdoc-alloc: $(src)/alloc/lib.rs rustdoc-core rustdoc-compiler_builtins FORCE
	+$(call if_changed,rustdoc)

rustdoc-kernel: private rustc_target_flags = --extern alloc \
    --extern build_error --extern macros=$(objtree)/$(obj)/libmacros.so \
    --extern bindings --extern uapi
rustdoc-kernel: $(src)/kernel/lib.rs rustdoc-core rustdoc-macros \
    rustdoc-compiler_builtins rustdoc-alloc $(obj)/libmacros.so \
    $(obj)/bindings.o FORCE
	+$(call if_changed,rustdoc)

quiet_cmd_rustc_test_library = RUSTC TL $<
      cmd_rustc_test_library = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTC) $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg $(rustc_target_flags) \
		--crate-type $(if $(rustc_test_library_proc),proc-macro,rlib) \
		--out-dir $(objtree)/$(obj)/test --cfg testlib \
		--sysroot $(objtree)/$(obj)/test/sysroot \
		-L$(objtree)/$(obj)/test \
		--crate-name $(subst rusttest-,,$(subst rusttestlib-,,$@)) $<

rusttestlib-build_error: $(src)/build_error.rs rusttest-prepare FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-macros: private rustc_target_flags = --extern proc_macro
rusttestlib-macros: private rustc_test_library_proc = yes
rusttestlib-macros: $(src)/macros/lib.rs rusttest-prepare FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-bindings: $(src)/bindings/lib.rs rusttest-prepare FORCE
	+$(call if_changed,rustc_test_library)

rusttestlib-uapi: $(src)/uapi/lib.rs rusttest-prepare FORCE
	+$(call if_changed,rustc_test_library)

quiet_cmd_rustdoc_test = RUSTDOC T $<
      cmd_rustdoc_test = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) --test $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg \
		$(rustc_target_flags) $(rustdoc_test_target_flags) \
		--sysroot $(objtree)/$(obj)/test/sysroot $(rustdoc_test_quiet) \
		-L$(objtree)/$(obj)/test --output $(rustdoc_output) \
		--crate-name $(subst rusttest-,,$@) $<

quiet_cmd_rustdoc_test_kernel = RUSTDOC TK $<
      cmd_rustdoc_test_kernel = \
	rm -rf $(objtree)/$(obj)/test/doctests/kernel; \
	mkdir -p $(objtree)/$(obj)/test/doctests/kernel; \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTDOC) --test $(rust_flags) \
		-L$(objtree)/$(obj) --extern alloc --extern kernel \
		--extern build_error --extern macros \
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
quiet_cmd_rustc_test = RUSTC T  $<
      cmd_rustc_test = \
	OBJTREE=$(abspath $(objtree)) \
	$(RUSTC) --test $(rust_common_flags) \
		@$(objtree)/include/generated/rustc_cfg \
		$(rustc_target_flags) --out-dir $(objtree)/$(obj)/test \
		--sysroot $(objtree)/$(obj)/test/sysroot \
		-L$(objtree)/$(obj)/test \
		--crate-name $(subst rusttest-,,$@) $<; \
	$(objtree)/$(obj)/test/$(subst rusttest-,,$@) $(rust_test_quiet) \
		$(rustc_test_run_flags)

rusttest: rusttest-macros rusttest-kernel

# This prepares a custom sysroot with our custom `alloc` instead of
# the standard one.
#
# This requires several hacks:
#   - Unlike `core` and `alloc`, `std` depends on more than a dozen crates,
#     including third-party crates that need to be downloaded, plus custom
#     `build.rs` steps. Thus hardcoding things here is not maintainable.
#   - `cargo` knows how to build the standard library, but it is an unstable
#     feature so far (`-Zbuild-std`).
#   - `cargo` only considers the use case of building the standard library
#     to use it in a given package. Thus we need to create a dummy package
#     and pick the generated libraries from there.
#   - Since we only keep a subset of upstream `alloc` in-tree, we need
#     to recreate it on the fly by putting our sources on top.
#   - The usual ways of modifying the dependency graph in `cargo` do not seem
#     to apply for the `-Zbuild-std` steps, thus we have to mislead it
#     by modifying the sources in the sysroot.
#   - To avoid messing with the user's Rust installation, we create a clone
#     of the sysroot. However, `cargo` ignores `RUSTFLAGS` in the `-Zbuild-std`
#     steps, thus we use a wrapper binary passed via `RUSTC` to pass the flag.
#
# In the future, we hope to avoid the whole ordeal by either:
#   - Making the `test` crate not depend on `std` (either improving upstream
#     or having our own custom crate).
#   - Making the tests run in kernel space (requires the previous point).
#   - Making `std` and friends be more like a "normal" crate, so that
#     `-Zbuild-std` and related hacks are not needed.
quiet_cmd_rustsysroot = RUSTSYSROOT
      cmd_rustsysroot = \
	rm -rf $(objtree)/$(obj)/test; \
	mkdir -p $(objtree)/$(obj)/test; \
	cp -a $(rustc_sysroot) $(objtree)/$(obj)/test/sysroot; \
	cp -r $(srctree)/$(src)/alloc/* \
		$(objtree)/$(obj)/test/sysroot/lib/rustlib/src/rust/library/alloc/src; \
	echo '\#!/bin/sh' > $(objtree)/$(obj)/test/rustc_sysroot; \
	echo "$(RUSTC) --sysroot=$(abspath $(objtree)/$(obj)/test/sysroot) \"\$$@\"" \
		>> $(objtree)/$(obj)/test/rustc_sysroot; \
	chmod u+x $(objtree)/$(obj)/test/rustc_sysroot; \
	$(CARGO) -q new $(objtree)/$(obj)/test/dummy; \
	RUSTC=$(objtree)/$(obj)/test/rustc_sysroot $(CARGO) $(cargo_quiet) \
		test -Zbuild-std --target $(rustc_host_target) \
		--manifest-path $(objtree)/$(obj)/test/dummy/Cargo.toml; \
	rm $(objtree)/$(obj)/test/sysroot/lib/rustlib/$(rustc_host_target)/lib/*; \
	cp $(objtree)/$(obj)/test/dummy/target/$(rustc_host_target)/debug/deps/* \
		$(objtree)/$(obj)/test/sysroot/lib/rustlib/$(rustc_host_target)/lib

rusttest-prepare: FORCE
	+$(call if_changed,rustsysroot)

rusttest-macros: private rustc_target_flags = --extern proc_macro
rusttest-macros: private rustdoc_test_target_flags = --crate-type proc-macro
rusttest-macros: $(src)/macros/lib.rs rusttest-prepare FORCE
	+$(call if_changed,rustc_test)
	+$(call if_changed,rustdoc_test)

rusttest-kernel: private rustc_target_flags = --extern alloc \
    --extern build_error --extern macros --extern bindings --extern uapi
rusttest-kernel: $(src)/kernel/lib.rs rusttest-prepare \
    rusttestlib-build_error rusttestlib-macros rusttestlib-bindings \
    rusttestlib-uapi FORCE
	+$(call if_changed,rustc_test)
	+$(call if_changed,rustc_test_library)

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
	-mno-strict-align -mstrict-align \
	-fconserve-stack -falign-jumps=% -falign-loops=% \
	-femit-struct-debug-baseonly -fno-ipa-cp-clone -fno-ipa-sra \
	-fno-partial-inlining -fplugin-arg-arm_ssp_per_task_plugin-% \
	-fno-reorder-blocks -fno-allow-store-data-races -fasan-shadow-offset=% \
	-fzero-call-used-regs=% -fno-stack-clash-protection \
	-fno-inline-functions-called-once -fsanitize=bounds-strict \
	-fstrict-flex-arrays=% \
	--param=% --param asan-%

# Derived from `scripts/Makefile.clang`.
BINDGEN_TARGET_x86	:= x86_64-linux-gnu
BINDGEN_TARGET_arm64	:= aarch64-linux-gnu
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

bindgen_c_flags_final = $(bindgen_c_flags_lto) -D__BINDGEN__

quiet_cmd_bindgen = BINDGEN $@
      cmd_bindgen = \
	$(BINDGEN) $< $(bindgen_target_flags) \
		--use-core --with-derive-default --ctypes-prefix core::ffi --no-layout-tests \
		--no-debug '.*' \
		-o $@ -- $(bindgen_c_flags_final) -DMODULE \
		$(bindgen_target_cflags) $(bindgen_target_extra)

$(obj)/bindings/bindings_generated.rs: private bindgen_target_flags = \
    $(shell grep -Ev '^#|^$$' $(srctree)/$(src)/bindgen_parameters)
$(obj)/bindings/bindings_generated.rs: private bindgen_target_extra = ; \
    sed -Ei 's/pub const RUST_CONST_HELPER_([a-zA-Z0-9_]*)/pub const \1/g' $@
$(obj)/bindings/bindings_generated.rs: $(src)/bindings/bindings_helper.h \
    $(src)/bindgen_parameters FORCE
	$(call if_changed_dep,bindgen)

$(obj)/uapi/uapi_generated.rs: private bindgen_target_flags = \
    $(shell grep -Ev '^#|^$$' $(srctree)/$(src)/bindgen_parameters)
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
$(obj)/bindings/bindings_helpers_generated.rs: $(src)/helpers.c FORCE
	$(call if_changed_dep,bindgen)

quiet_cmd_exports = EXPORTS $@
      cmd_exports = \
	$(NM) -p --defined-only $< \
		| awk '/ (T|R|D) / {printf "EXPORT_SYMBOL_RUST_GPL(%s);\n",$$3}' > $@

$(obj)/exports_core_generated.h: $(obj)/core.o FORCE
	$(call if_changed,exports)

$(obj)/exports_alloc_generated.h: $(obj)/alloc.o FORCE
	$(call if_changed,exports)

$(obj)/exports_bindings_generated.h: $(obj)/bindings.o FORCE
	$(call if_changed,exports)

$(obj)/exports_kernel_generated.h: $(obj)/kernel.o FORCE
	$(call if_changed,exports)

quiet_cmd_rustc_procmacro = $(RUSTC_OR_CLIPPY_QUIET) P $@
      cmd_rustc_procmacro = \
	$(RUSTC_OR_CLIPPY) $(rust_common_flags) \
		-Clinker-flavor=gcc -Clinker=$(HOSTCC) \
		-Clink-args='$(call escsq,$(KBUILD_HOSTLDFLAGS))' \
		--emit=dep-info=$(depfile) --emit=link=$@ --extern proc_macro \
		--crate-type proc-macro \
		--crate-name $(patsubst lib%.so,%,$(notdir $@)) $<

# Procedural macros can only be used with the `rustc` that compiled it.
# Therefore, to get `libmacros.so` automatically recompiled when the compiler
# version changes, we add `core.o` as a dependency (even if it is not needed).
$(obj)/libmacros.so: $(src)/macros/lib.rs $(obj)/core.o FORCE
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
	$(if $(rustc_objcopy),;$(OBJCOPY) $(rustc_objcopy) $@)

rust-analyzer:
	$(Q)$(srctree)/scripts/generate_rust_analyzer.py \
		--cfgs='core=$(core-cfgs)' --cfgs='alloc=$(alloc-cfgs)' \
		$(realpath $(srctree)) $(realpath $(objtree)) \
		$(RUST_LIB_SRC) $(KBUILD_EXTMOD) > \
		$(if $(KBUILD_EXTMOD),$(extmod_prefix),$(objtree))/rust-project.json

redirect-intrinsics = \
	__addsf3 __eqsf2 __gesf2 __lesf2 __ltsf2 __mulsf3 __nesf2 __unordsf2 \
	__adddf3 __ledf2 __ltdf2 __muldf3 __unorddf2 \
	__muloti4 __multi3 \
	__udivmodti4 __udivti3 __umodti3

ifneq ($(or $(CONFIG_ARM64),$(and $(CONFIG_RISCV),$(CONFIG_64BIT))),)
	# These intrinsics are defined for ARM64 and RISCV64
	redirect-intrinsics += \
		__ashrti3 \
		__ashlti3 __lshrti3
endif

$(obj)/core.o: private skip_clippy = 1
$(obj)/core.o: private skip_flags = -Dunreachable_pub
$(obj)/core.o: private rustc_objcopy = $(foreach sym,$(redirect-intrinsics),--redefine-sym $(sym)=__rust$(sym))
$(obj)/core.o: private rustc_target_flags = $(core-cfgs)
$(obj)/core.o: $(RUST_LIB_SRC)/core/src/lib.rs FORCE
	+$(call if_changed_dep,rustc_library)
ifneq ($(or $(CONFIG_X86_64),$(CONFIG_LOONGARCH)),)
$(obj)/core.o: scripts/target.json
endif

$(obj)/compiler_builtins.o: private rustc_objcopy = -w -W '__*'
$(obj)/compiler_builtins.o: $(src)/compiler_builtins.rs $(obj)/core.o FORCE
	+$(call if_changed_dep,rustc_library)

$(obj)/alloc.o: private skip_clippy = 1
$(obj)/alloc.o: private skip_flags = -Dunreachable_pub
$(obj)/alloc.o: private rustc_target_flags = $(alloc-cfgs)
$(obj)/alloc.o: $(src)/alloc/lib.rs $(obj)/compiler_builtins.o FORCE
	+$(call if_changed_dep,rustc_library)

$(obj)/build_error.o: $(src)/build_error.rs $(obj)/compiler_builtins.o FORCE
	+$(call if_changed_dep,rustc_library)

$(obj)/bindings.o: $(src)/bindings/lib.rs \
    $(obj)/compiler_builtins.o \
    $(obj)/bindings/bindings_generated.rs \
    $(obj)/bindings/bindings_helpers_generated.rs FORCE
	+$(call if_changed_dep,rustc_library)

$(obj)/uapi.o: $(src)/uapi/lib.rs \
    $(obj)/compiler_builtins.o \
    $(obj)/uapi/uapi_generated.rs FORCE
	+$(call if_changed_dep,rustc_library)

$(obj)/kernel.o: private rustc_target_flags = --extern alloc \
    --extern build_error --extern macros --extern bindings --extern uapi
$(obj)/kernel.o: $(src)/kernel/lib.rs $(obj)/alloc.o $(obj)/build_error.o \
    $(obj)/libmacros.so $(obj)/bindings.o $(obj)/uapi.o FORCE
	+$(call if_changed_dep,rustc_library)

endif # CONFIG_RUST
