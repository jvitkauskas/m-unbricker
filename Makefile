# SPDX-License-Identifier: MIT
# M-Unbricker: a RAM app, one build per board it runs on (fm1, smk37) and one for both (any: board 0 in its trailer,
# found at run time from the unit's flash, src/board.rs).
#
# It's Rust (src/main.rs), on m-bootloader's Rust crate (the m-bootloader submodule) for the chip's common drivers.
# Needs the pi32v2 LLVM toolchain and the pi32v2 Rust compiler (see README), unpacked in toolchain/ and pi32v2-rust/
# or at TOOLCHAIN=<directory> and PI32V2_RUST=<directory>, cargo and Python 3. `make test` and `make preview` need any
# recent rustc for this computer (HOST_RUSTC), and the preview's contact sheets need Pillow.
SHELL := /bin/bash
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
TOOLCHAIN ?= toolchain
CLANG := $(TOOLCHAIN)/bin/clang
LINK := $(TOOLCHAIN)/bin/ld.lld
OBJCOPY := $(TOOLCHAIN)/bin/llvm-objcopy
OBJDUMP := $(TOOLCHAIN)/bin/llvm-objdump
NM := $(TOOLCHAIN)/bin/llvm-nm
AR := $(TOOLCHAIN)/bin/llvm-ar
# The compiler has to be built on the toolchain's LLVM: the linker generates the code from its bitcode.
PI32V2_RUST ?= pi32v2-rust
RUSTC := $(PI32V2_RUST)/bin/rustc
HOST_RUSTC ?= rustc
CARGO ?= cargo
MB := m-bootloader

BOARDS := fm1 smk37 any
all: $(foreach b,$(BOARDS),build/m-unbricker-$(b).bin)

# Rust's optimisation level: z, the smallest, with the inlining option of pi32v2-llvm 23.1.6-2, as m-bootloader and
# m-apps have it. rustc writes bitcode, and the linker optimises the app as one program with the drivers and `core`.
# A panic (a failed bounds check, say) is the compiler's trap, an illegal instruction.
RUST_OPT ?= z
ifeq ($(RUST_OPT),z)
LINK += -mllvm -inline-minsize-hint-threshold=50
RUST_HINT := -C llvm-args=-inline-minsize-hint-threshold=50
endif
RUST_LIB := lib/rustlib/pi32v2-unknown-none/lib
RUST_FLAGS := --target pi32v2-unknown-none --edition 2024 -C opt-level=$(RUST_OPT) -C codegen-units=1 -C debuginfo=0 \
	-C symbol-mangling-version=v0 -Zunstable-options -C panic=immediate-abort $(RUST_HINT) \
	--remap-path-prefix=$(CURDIR)=. --sysroot build/rust/sysroot -Clinker-plugin-lto -Clto=fat
APP_SRC := $(wildcard src/*.rs) src/mflr-loader-a.bin build/rescue_img.rs
MB_SRC := $(wildcard $(MB)/rust/src/*.rs $(MB)/rust/src/*/*.rs)

# What changes the Rust without changing a file: another compiler or linker builds it all again.
build/rust/options: FORCE
	@mkdir -p $(@D)
	@{ echo 'RUST_OPT=$(RUST_OPT)'; $(RUSTC) -vV; $(LINK) --version; } > $@.new 2>&1; \
		cmp -s $@.new $@ && rm $@.new || mv $@.new $@
# Rust's core library for pi32v2, built once from the compiler's sources as bitcode (m-bootloader's rust/sysroot, an
# empty crate that makes cargo build it).
build/rust/sysroot.stamp: $(MB)/rust/sysroot/Cargo.toml Makefile build/rust/options
	@mkdir -p build/rust/sysroot/$(RUST_LIB)
	$(CARGO) clean --quiet --manifest-path $(MB)/rust/sysroot/Cargo.toml --target-dir build/rust/target
	CARGO_TARGET_DIR=build/rust/target CARGO_PROFILE_RELEASE_OPT_LEVEL=$(RUST_OPT) RUSTC_BOOTSTRAP=1 RUSTC=$(RUSTC) RUSTFLAGS="-Csymbol-mangling-version=v0 -Zunstable-options -Cpanic=immediate-abort $(RUST_HINT) --remap-path-prefix=$(CURDIR)=. -Clto=fat" \
		$(CARGO) build --quiet --release --locked --manifest-path $(MB)/rust/sysroot/Cargo.toml --target pi32v2-unknown-none -Zbuild-std=core
	@# (wherever this cargo keeps them: under deps/ up to 1.99, under build/<crate>/ from 1.100)
	for f in core compiler_builtins; do \
		set -- $$(find build/rust/target/pi32v2-unknown-none/release -name "lib$$f-*.rlib"); \
		[ $$# = 1 ] && cp "$$1" build/rust/sysroot/$(RUST_LIB)/lib$$f.rlib || exit 1; done
	touch $@
# A library's one object, for the linker.
rlib_object = $(AR) p $(1) $$($(AR) t $(1) | grep '\.o$$') > $(2)
build/rust/core.o: build/rust/sysroot.stamp
	$(call rlib_object,build/rust/sysroot/$(RUST_LIB)/libcore.rlib,$@)
build/rust/compiler_builtins.o: build/rust/sysroot.stamp
	$(call rlib_object,build/rust/sysroot/$(RUST_LIB)/libcompiler_builtins.rlib,$@)
# m-bootloader's crate, as a library: the same for every board, as M-Unbricker uses none of its board's own parts.
build/rust/libmboot.rlib: $(MB_SRC) build/rust/sysroot.stamp Makefile
	$(RUSTC) $(RUST_FLAGS) --crate-name mboot --crate-type rlib $(MB)/rust/src/lib.rs -o $@
build/rust/mboot.o: build/rust/libmboot.rlib
	$(call rlib_object,$<,$@)
RUNTIME := build/rust/mboot.o build/rust/core.o build/rust/compiler_builtins.o

# The rescue images, packed (tools/make_images.py): generated, as they are large.
build/rescue_img.rs: tools/make_images.py rescue/fm1/boot.bin rescue/smk37/boot.bin $(wildcard rescue/*/app.bin)
	@mkdir -p build
	python3 tools/make_images.py rescue $@

define BOARD_RULES
build/$(1)/m-unbricker.o: $$(APP_SRC) build/rust/libmboot.rlib Makefile
	@mkdir -p $$(@D)
	$$(RUSTC) $$(RUST_FLAGS) --crate-name m_unbricker --crate-type lib --emit=obj --cfg 'board="$(1)"' \
		--extern mboot=build/rust/libmboot.rlib -L build/rust src/main.rs -o $$@
build/$(1)/start.o: src/start.S
	@mkdir -p $$(@D)
	$$(CLANG) -target pi32v2 -mcpu=r3 -c $$< -o $$@
build/$(1)/m-unbricker.elf: build/$(1)/start.o build/$(1)/m-unbricker.o $$(RUNTIME) src/ram.ld
	$$(LINK) --plugin-opt=O3 --relax --gc-sections -T src/ram.ld -Map build/$(1)/m-unbricker.map -o $$@ $$(filter %.o,$$^)
build/m-unbricker-$(1).bin: build/$(1)/m-unbricker.elf tools/app_trailer.py
	@$$(OBJCOPY) -O binary -j .image $$< build/$(1)/flat.bin
	@$$(OBJDUMP) -d $$< > build/$(1)/m-unbricker.dis
	@$$(NM) $$< | grep -E 'rk_(image_end|bss_end)' | tr '\n' ' '
	@python3 tools/app_trailer.py build/$(1)/flat.bin $$@ $(1)
endef
$(foreach b,$(BOARDS),$(eval $(call BOARD_RULES,$(b))))

# The programs for this computer: the preview and the tests, with m-bootloader's crate built for it.
HOST_FLAGS := --edition 2024 -C opt-level=1 -C debuginfo=1 -C overflow-checks=on -C debug-assertions=on
build/host/libmboot.rlib: $(MB_SRC) Makefile
	@mkdir -p $(@D)
	$(HOST_RUSTC) $(HOST_FLAGS) --crate-name mboot --crate-type rlib $(MB)/rust/src/lib.rs -o $@
# build/host/<board>/<program>: tools/preview.rs, or tests/<program>.rs, for one board's build.
host_program = $(HOST_RUSTC) $(HOST_FLAGS) --cfg 'board="$(1)"' --extern mboot=build/host/libmboot.rlib -L build/host
build/host/%/preview: tools/preview.rs $(APP_SRC) build/host/libmboot.rlib Makefile
	@mkdir -p $(@D)
	$(call host_program,$*) $< -o $@

# make preview: every page with the app's own renderer, into build/preview/<board>/<page>.png, and a contact sheet of
# each board's (build/preview/pages-<board>.png).
preview: build/host/fm1/preview build/host/smk37/preview
	for b in fm1 smk37; do mkdir -p build/preview/$$b && build/host/$$b/preview build/preview/$$b || exit 1; done
	python3 tools/preview.py

# Generated sources that are kept in the repository (they need DejaVu fonts and Pillow).
fonts:
	python3 tools/make_fonts.py
art:
	python3 tools/make_art.py

clean:
	rm -rf build
FORCE:
.PHONY: all clean fonts art preview test FORCE
