# # SPDX-License-Identifier: MIT
#
# Copyright (C) 2021 Xilinx, Inc.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
# Authors:
#     Sonal Santan <sonal.santan@amd.com>
#

CXXFLAGS = -Wall -I /usr/include/libdrm -I ../uapi
LDLIBS = -ldrm
CC = g++

debug ?= 0
ifeq ($(debug), 1)
    CXXFLAGS +=-DDEBUG -g
else
    CXXFLAGS +=-DNDEBUG -O2
endif

all: test0 test1 test2 test3

test0: test0.o

test1: test1.o

test2: test2.o

test3: test3.o

clean:
	$(RM) -f test0.o test1.o test2.o test3.o test0 test1 test2 test3

run: all
ifeq ($(verbose), 1)
	sudo bash -c "echo 0x2 > /sys/module/drm/parameters/debug"
endif
	./test0
	./test1 -c 1000
	./test2 -c 1000
	./test1 -c 1000 -j 2
	./test3 -c 100

compile_commands.json: test0.cpp test1.cpp test2.cpp test3.cpp common.h
	bear -- make debug=1 all

compdb: compile_commands.json
