# # SPDX-License-Identifier: MIT
#
# Copyright (C) 2021 Xilinx, Inc.
# Authors:
#     Sonal Santan <sonal.santan@xilinx.com>
#

CXXFLAGS = -g -Wall -I ../uapi
CC = g++

all: test1 test2

test1 : test1.o

test2 : test2.o

clean:
	$(RM) -f test1.o test2.o test1 test2

run: all
	./test1 -c 10000
	./test2 -c 10000
