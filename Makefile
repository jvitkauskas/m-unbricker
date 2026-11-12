# SPDX-License-Identifier: MIT
# M-Unbricker: a RAM app that turns a working FM-1 or SMK-37 into the unbricker of another.
# `make` builds one per board it runs on: build/m-unbricker-fm1.bin, build/m-unbricker-smk37.bin.
#
# Needs the pi32v2 LLVM toolchain (see README), unpacked in toolchain/ or at TOOLCHAIN=<directory>,
# and Python 3.
TOOLCHAIN ?= toolchain
CLANG := $(TOOLCHAIN)/bin/clang
LD := $(TOOLCHAIN)/bin/ld.lld
OBJCOPY := $(TOOLCHAIN)/bin/llvm-objcopy
OBJDUMP := $(TOOLCHAIN)/bin/llvm-objdump
NM := $(TOOLCHAIN)/bin/llvm-nm

CFLAGS := -target pi32v2 -mcpu=r3 -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fno-stack-protector \
	-Wall -Wextra -Werror -Wno-unused-parameter -Isrc -Isrc/mbl -Ibuild

SRC := src/main.c src/usbio.c src/husb.c src/bot.c src/jl.c src/writer.c src/ui.c src/lcd.c \
	src/mbl/keys.c src/mbl/hw.c src/mbl/rt.c
vpath %.c src src/mbl

BOARD_fm1 := 1
BOARD_smk37 := 2

all: build/m-unbricker-fm1.bin build/m-unbricker-smk37.bin

# The rescue images, packed (tools/make_images.py): generated, as they are large.
build/rescue_img.h: tools/make_images.py rescue/fm1/boot.bin rescue/fm1/app.bin rescue/smk37/boot.bin rescue/smk37/app.bin
	@mkdir -p build
	@python3 tools/make_images.py rescue $@

define BOARD_RULES
OBJ_$(1) := $$(patsubst %.c,build/$(1)/%.o,$$(notdir $$(SRC))) build/$(1)/start.o
build/$(1)/%.o: %.c src/*.h src/mbl/*.h build/rescue_img.h
	@mkdir -p build/$(1)
	@echo "  CC    $$< ($(1))"
	@$$(CLANG) $$(CFLAGS) -DDISP_BOARD=$$(BOARD_$(1)) -DBOARD_ID=$$(BOARD_$(1)) -c $$< -o $$@
build/$(1)/start.o: src/start.S
	@mkdir -p build/$(1)
	@$$(CLANG) $$(CFLAGS) -c $$< -o $$@
build/$(1)/m-unbricker.elf: $$(OBJ_$(1)) src/ram.ld
	@$$(LD) --gc-sections -T src/ram.ld -Map build/$(1)/m-unbricker.map -o $$@ $$(OBJ_$(1))
build/m-unbricker-$(1).bin: build/$(1)/m-unbricker.elf tools/app_trailer.py
	@$$(OBJCOPY) -O binary -j .image $$< build/$(1)/flat.bin
	@$$(OBJDUMP) -d $$< > build/$(1)/m-unbricker.dis
	@$$(NM) $$< | grep -E 'rk_(image_end|bss_end)' | tr '\n' ' '
	@python3 tools/app_trailer.py build/$(1)/flat.bin $$@ $(1)
endef
$(eval $(call BOARD_RULES,fm1))
$(eval $(call BOARD_RULES,smk37))

# Generated sources that are kept in the repository (they need DejaVu fonts and Pillow).
fonts:
	python3 tools/make_fonts.py
art:
	python3 tools/make_art.py
preview: build/rescue_img.h
	python3 tools/preview.py

clean:
	rm -rf build
.PHONY: all clean fonts art preview
