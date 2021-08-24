# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2021 Xilinx, Inc. All rights reserved.
#
# Authors:
#

obj-m	+= sched_test.o
ccflags-y := -Iinclude/drm

sched_test-y := \
	sched_test_drv.o \
	sched_test_core.o


CONFIG_MODULE_SIG=n
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

PWD	:= $(shell pwd)
ROOT	:= $(dir $(M))

ifeq ($(DEBUG),1)
ccflags-y += -DDEBUG
endif

ifeq ($(SYMBOL),1)
ccflags-y += -g
endif

all:
	@echo $(PWD)
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

install: all
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install
	depmod -a
	-rmmod sched_test
	-modprobe sched_test

clean:
	rm -rf *.o *.o.d *~ core .depend .*.cmd *.ko *.ko.unsigned *.mod.c \
	.tmp_versions *.symvers modules.order
