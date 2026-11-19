# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Makefile for camera driver on Rockchip (device tree overlay + kernel module)

SRC_DIR   := $(shell pwd)
BUILD_DIR := build

DRV_SRC   := $(wildcard *.c)
DRV_NAME  := $(shell grep '^BUILT_MODULE_NAME=' dkms.conf | cut -d'"' -f2)
DRV_VER   := $(shell grep '^PACKAGE_VERSION=' dkms.conf | cut -d'"' -f2)
DTS       := $(wildcard *.dts)
DTSI      := $(wildcard *.dtsi)
DTBO_MONO := $(patsubst $(DRV_NAME)-%.dts,$(BUILD_DIR)/$(DRV_NAME)-mono-%.dtbo,$(DTS))
DTBO      := $(addprefix $(BUILD_DIR)/,$(DTS:.dts=.dtbo)) $(DTBO_MONO)
DTC       := dtc
DTC_FLAGS := -Wno-graph_child_address -@ -I dts -O dtb

KDIR      ?= /lib/modules/$(shell uname -r)/build
CCFLAGS   := -Werror

# Overlays include dt-bindings headers from kernel tree
CPP       := cpp
CPP_FLAGS := -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I$(KDIR)/include

# Status line formatter (8-char tag column, arg at col 11)
PRINT = printf '  %-7s %s\n'

define DTBO_RECIPE
@$(PRINT) CPP "$<$(if $(DTS_DEFS), $(DTS_DEFS))"
@$(CPP) $(CPP_FLAGS) $(DTS_DEFS) -o $(basename $@).dts.preprocessed $<
@$(PRINT) DTC $@
@$(DTC) $(DTC_FLAGS) -o $@ $(basename $@).dts.preprocessed
@rm -f $(basename $@).dts.preprocessed
@$(PRINT) BUILT $@
endef

ifeq ($(DRV_SRC),)
  $(error No .c source file found in project root)
endif

ifeq ($(DRV_NAME),)
  $(error No BUILT_MODULE_NAME found in dkms.conf)
endif

ifeq ($(DRV_VER),)
  $(error No PACKAGE_VERSION found in dkms.conf)
endif

ifeq ($(DTS),)
  $(error No *.dts file found in project root)
endif

.PHONY: all obj dtbo module clean

all: $(DTBO) $(BUILD_DIR)/$(DRV_NAME).ko

obj: $(BUILD_DIR)/$(DRV_NAME).o

dtbo: $(DTBO)

module: $(BUILD_DIR)/$(DRV_NAME).ko

$(DTBO_MONO): DTS_DEFS := -DIMX462_MONO

$(BUILD_DIR)/$(DRV_NAME)-mono-%.dtbo: $(DRV_NAME)-%.dts $(DTSI) | $(BUILD_DIR)
	$(DTBO_RECIPE)

$(BUILD_DIR)/%.dtbo: %.dts $(DTSI) | $(BUILD_DIR)
	$(DTBO_RECIPE)

$(BUILD_DIR)/Kbuild: | $(BUILD_DIR)
	@echo "ccflags-y += $(CCFLAGS)" > $@
	@printf 'ccflags-y += -include %s/modver.h\n' '$(abspath $(BUILD_DIR))' >> $@
	@echo "obj-m += $(DRV_NAME).o" >> $@
	@ln -sf $(SRC_DIR)/$(DRV_SRC) $(BUILD_DIR)/$(DRV_SRC)

$(BUILD_DIR)/modver.h: dkms.conf | $(BUILD_DIR)
	@printf '#include <linux/module.h>\nMODULE_VERSION("%s");\n' '$(DRV_VER)' > $@

$(BUILD_DIR)/$(DRV_NAME).o: $(DRV_SRC) $(BUILD_DIR)/Kbuild $(BUILD_DIR)/modver.h
	@$(PRINT) KBUILD $(DRV_NAME).o
	@$(MAKE) -C $(KDIR) M=$(abspath $(BUILD_DIR)) $(DRV_NAME).o
	@$(PRINT) BUILT $@

$(BUILD_DIR)/$(DRV_NAME).ko: $(DRV_SRC) $(BUILD_DIR)/Kbuild $(BUILD_DIR)/modver.h
	@$(PRINT) KBUILD $(DRV_NAME).ko
	@$(MAKE) -C $(KDIR) M=$(abspath $(BUILD_DIR)) modules
	@$(PRINT) BUILT $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@$(PRINT) CLEAN $(BUILD_DIR)
	@rm -rf $(BUILD_DIR)
