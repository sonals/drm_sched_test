# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2021-2022 Xilinx, Inc.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
# Authors:
#     Sonal Santan <sonal.santan@amd.com>
#

obj-m	+= sched_test.o
ccflags-y := -Iinclude/drm

sched_test-y := \
	sched_test_drv.o \
	sched_test_core.o


CONFIG_MODULE_SIG=n
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC := /lib/modules/$(KERNEL_VERSION)/build

PWD	:= $(shell pwd)
ROOT	:= $(dir $(M))
SECURE  := $(test -f /var/lib/dkms/mok.pub)

ifeq ($(DEBUG),1)
ccflags-y += -DDEBUG
endif

ifeq ($(SYMBOL),1)
ccflags-y += -g
endif

all:
	@echo $(PWD)
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

sign: all
	sudo $(KERNEL_SRC)/scripts/sign-file sha256 /var/lib/dkms/mok.key /var/lib/dkms/mok.pub sched_test.ko

install: all
	sudo $(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install
	sudo depmod -a
	-sudo rmmod sched_test
	-sudo modprobe sched_test

clean:
	rm -rf *.o *.o.d *~ core .depend .*.cmd *.ko *.ko.unsigned *.mod.c \
	.tmp_versions *.symvers modules.order

compiledb:
	bear -- make all
