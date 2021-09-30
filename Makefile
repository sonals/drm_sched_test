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
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC := /lib/modules/$(KERNEL_VERSION)/build
#KERMEL_SRC = 5.11.0-27-generic

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
	sudo $(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install
	sudo depmod -a
	-sudo rmmod sched_test
	-sudo modprobe sched_test

clean:
	rm -rf *.o *.o.d *~ core .depend .*.cmd *.ko *.ko.unsigned *.mod.c \
	.tmp_versions *.symvers modules.order
