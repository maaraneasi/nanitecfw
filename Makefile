# FiiO Echo Nano custom firmware: patches the official V1.7.0 image you supply.
#
#   make                 build out/NANOV170.IMG from NANOV170.IMG (stock) + patches/ + src/
#   make FW=path/to/NANOV170.IMG   stock image somewhere else
#   make DATE=20261101   build date (must be newer than the firmware on the device)
#   make recovery        unmodified stock code with a new date -> out/recovery/NANOV170.IMG
#   make check           is the input the expected stock image?
#   make info / verify / diff / clean

FW      ?= NANOV170.IMG
OUT     ?= out/NANOV170.IMG
DATE    ?= $(shell date +%Y%m%d)
PY      := python3
FWTOOL  := $(PY) tools/rknano_fw.py

PATCHES := $(sort $(wildcard patches/*.patch))
ASM     := $(sort $(wildcard patches/*.S))
GEN     := $(ASM:patches/%.S=out/gen/%.patch)
SRC     := $(wildcard src/*.c src/*.S src/*.h src/*.ld)
BLOB    := out/gen/cfw_blob.patch out/gen/cfw_hooks.patch

ifeq ($(filter grouped-target,$(.FEATURES)),)
$(error GNU make 4.3 or newer needed (on macOS: brew install make, then run gmake))
endif
ifeq ($(shell command -v arm-none-eabi-gcc),)
$(warning arm-none-eabi-gcc not found - see README "Requirements")
endif

.PHONY: all build recovery check verify info diff help clean

all: build

help:
	@sed -n 's/^#   //p' Makefile

check:
	$(FWTOOL) check $(FW)

build: check $(GEN) $(BLOB)
	@mkdir -p $(dir $(OUT))
	$(FWTOOL) patch $(FW) $(OUT) --date $(DATE) $(addprefix @,$(PATCHES) $(GEN) $(BLOB))
	$(FWTOOL) crc $(OUT)

# C code run from RAM during the library refresh; syms.inc exports its symbols to patches/*.S
$(BLOB) out/gen/syms.inc &: $(SRC) src/hooks.txt tools/build_blob.sh tools/hooks.py
	tools/build_blob.sh out/gen $(FW)

# stock code, new date: the loader boots the newest valid copy, so the original
# image would not replace a newer custom build
recovery: check
	@mkdir -p out/recovery
	$(FWTOOL) patch $(FW) out/recovery/NANOV170.IMG --date $(DATE)
	$(FWTOOL) crc out/recovery/NANOV170.IMG

out/gen/%.patch: patches/%.S patches/fw.inc out/gen/syms.inc tools/asm2patch.sh
	tools/asm2patch.sh $< $@

verify:
	$(FWTOOL) crc $(OUT)

info:
	$(FWTOOL) info $(FW)

# byte ranges that differ from stock
diff:
	@cmp -l $(FW) $(OUT) | awk 'NR==1||$$1!=p+1{if(NR>1)printf "-0x%x\n",p-1;printf "0x%x",$$1-1}{p=$$1}END{if(NR)printf "-0x%x\n",p-1}'

clean:
	rm -rf out
