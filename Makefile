# jornada-gpib build: C (sh-elf GCC) -> SH-3 assembly -> filter -> sh-pe gas -> PE/COFF for
# Windows CE 2.11 on the HP Jornada 680e. See docs/toolchain.md for the why.
#
#   make            build every target into build/
#   make hello      build/hello.exe (toolchain validation)
#   make test       host-side unit tests (pytest + C)
#   make toolchain  build the cross toolchain into ~/.cache/jornada-gpib/xtools

XTOOLS   ?= $(HOME)/.cache/jornada-gpib/xtools/bin
CC        = $(XTOOLS)/sh-elf-gcc
AS        = $(XTOOLS)/sh-pe-as
LD        = $(XTOOLS)/sh-pe-ld
OBJDUMP   = $(XTOOLS)/sh-pe-objdump
PYTHON   ?= python3

BUILD     = build
TOOLS     = tools
INCLUDES  = -Iinclude
CFLAGS    = -m3 -ml -O2 -std=gnu11 -Wall -Wextra -Werror -fshort-wchar -ffreestanding \
            -fno-common -fno-zero-initialized-in-bss -fno-reorder-blocks-and-partition \
            -fno-function-sections -fno-data-sections -fno-asynchronous-unwind-tables \
            -fno-dwarf2-cfi-asm -fno-stack-protector -fno-pic -fno-jump-tables \
            -fno-builtin-printf -mno-usermode $(INCLUDES)
# Windows CE 2.11 image parameters. EXEs link at 0x10000, which is where the current
# process is always mapped (slot 0), so they need no relocations; DLLs carry a .reloc section.
LDFLAGS_COMMON = --subsystem wince:2.11 --major-os-version 2 --minor-os-version 11 \
                 --major-subsystem-version 2 --minor-subsystem-version 11 \
                 --file-alignment 0x200 --section-alignment 0x1000
LDFLAGS_EXE = --image-base 0x10000 -e _WinMainCRTStartup $(LDFLAGS_COMMON)
LDFLAGS_DLL = --dll --enable-reloc-section -e _DllMainCRTStartup $(LDFLAGS_COMMON)

RT_SRC   = src/rt/crt.c src/rt/fmt.c
RT_OBJ   = $(patsubst src/%.c,$(BUILD)/obj/%.o,$(RT_SRC))
CE_OBJ   = $(BUILD)/obj/ce/ce_cs.o
GPIB_CORE_OBJ = $(BUILD)/obj/gpib/tnt4882.o $(BUILD)/obj/gpib/gpib488.o
GPIB_DRV_OBJ  = $(BUILD)/obj/gpib/drv_ce.o $(BUILD)/obj/gpib/drv_log.o $(BUILD)/obj/gpib/install.o \
                $(BUILD)/obj/gpib/thunks_exports.o
THUNKS   = $(BUILD)/obj/rt/thunks_imports.o $(BUILD)/obj/rt/thunks_calls.o $(BUILD)/obj/rt/imports_coredll.o
EXE_ENTRY = $(BUILD)/obj/rt/entry_exe.o
DLL_ENTRY = $(BUILD)/obj/rt/entry_dll.o

.PHONY: all hello driver cisdump gpibtest test toolchain clean
all: hello driver cisdump gpibtest

hello: $(BUILD)/hello.exe
driver: $(BUILD)/gpib.dll
cisdump: $(BUILD)/cisdump.exe
gpibtest: $(BUILD)/gpibtest.exe

toolchain:
	$(TOOLS)/build-toolchain.sh

# --- compile pipeline -------------------------------------------------------------------
$(BUILD)/obj/%.s.elf: src/%.c | dirs
	$(CC) $(CFLAGS) -S -o $@ $<

$(BUILD)/obj/%.s: $(BUILD)/obj/%.s.elf
	$(PYTHON) $(TOOLS)/asmfilter.py $< $@

$(BUILD)/obj/%.o: $(BUILD)/obj/%.s
	$(AS) -little -o $@ $<

.PRECIOUS: $(BUILD)/obj/%.s.elf $(BUILD)/obj/%.s

# --- generated assembly: calling-convention thunks and the coredll import table ----------
$(BUILD)/obj/rt/thunks_imports.s: $(TOOLS)/coredll_imports.txt $(TOOLS)/gen_thunks.py | dirs
	$(PYTHON) $(TOOLS)/gen_thunks.py imports $< > $@

$(BUILD)/obj/rt/thunks_calls.s: $(TOOLS)/gen_thunks.py | dirs
	$(PYTHON) $(TOOLS)/gen_thunks.py calls > $@

$(BUILD)/obj/rt/imports_coredll.s: $(TOOLS)/coredll_imports.txt $(TOOLS)/mkimplib.py | dirs
	$(PYTHON) $(TOOLS)/mkimplib.py coredll.dll $< > $@

$(BUILD)/obj/gpib/thunks_exports.s: $(TOOLS)/gpib_exports.txt $(TOOLS)/gen_thunks.py | dirs
	$(PYTHON) $(TOOLS)/gen_thunks.py exports $< > $@

# --- targets -----------------------------------------------------------------------------
$(BUILD)/hello.exe: $(EXE_ENTRY) $(BUILD)/obj/hello/hello.o $(RT_OBJ) $(THUNKS)
	$(LD) -o $@ $(EXE_ENTRY) $(BUILD)/obj/hello/hello.o $(RT_OBJ) $(THUNKS) $(LDFLAGS_EXE) -Map $(BUILD)/hello.map
	$(PYTHON) $(TOOLS)/pefix.py $@

CISDUMP_OBJ = $(BUILD)/obj/cisdump/cisdump.o $(BUILD)/obj/gpib/pnpid.o $(BUILD)/obj/gpib/drv_log.o

$(BUILD)/cisdump.exe: $(EXE_ENTRY) $(CISDUMP_OBJ) $(CE_OBJ) $(RT_OBJ) $(THUNKS)
	$(LD) -o $@ $(EXE_ENTRY) $(CISDUMP_OBJ) $(CE_OBJ) $(RT_OBJ) $(THUNKS) $(LDFLAGS_EXE) -Map $(BUILD)/cisdump.map
	$(PYTHON) $(TOOLS)/pefix.py $@

GPIBTEST_OBJ = $(BUILD)/obj/gpibtest/gpibtest.o $(BUILD)/obj/gpibapi/ib.o $(BUILD)/obj/gpib/drv_log.o

$(BUILD)/gpibtest.exe: $(EXE_ENTRY) $(GPIBTEST_OBJ) $(RT_OBJ) $(THUNKS)
	$(LD) -o $@ $(EXE_ENTRY) $(GPIBTEST_OBJ) $(RT_OBJ) $(THUNKS) $(LDFLAGS_EXE) -Map $(BUILD)/gpibtest.map
	$(PYTHON) $(TOOLS)/pefix.py $@

$(BUILD)/gpib.dll: $(DLL_ENTRY) $(GPIB_DRV_OBJ) $(GPIB_CORE_OBJ) $(CE_OBJ) $(RT_OBJ) $(THUNKS) src/gpib/gpib.def
	$(LD) -o $@ $(DLL_ENTRY) $(GPIB_DRV_OBJ) $(GPIB_CORE_OBJ) $(CE_OBJ) $(RT_OBJ) $(THUNKS) src/gpib/gpib.def $(LDFLAGS_DLL) -Map $(BUILD)/gpib.map
	$(PYTHON) $(TOOLS)/pefix.py $@

dirs:
	@mkdir -p $(BUILD)/obj/rt $(BUILD)/obj/ce $(BUILD)/obj/hello $(BUILD)/obj/cisdump \
	          $(BUILD)/obj/gpib $(BUILD)/obj/gpibtest $(BUILD)/obj/gpibapi $(BUILD)/lib

# --- tests -------------------------------------------------------------------------------
test:
	$(PYTHON) -m pytest -q tests
	$(MAKE) -C tests/host

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tests/host clean
