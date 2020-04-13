# SPDX-License-Identifier: GPL-2.0
# Makefile for firmware tools

CFLAGS = -Wall -Wextra -g

all: ihex2fw
%: %.c
	@$(CC) $(CFLAGS) -o $@ $^

clean:
	@$(RM) -f *.o ihex2fw

install:
	@$(foreach file, $(wildcard ihex2fw), cp -rf $(file) /usr/bin;)

.PHONY: all clean install
