# Build s5fstool

CC     ?= cc
RM     ?= rm -f
STRIP  ?= strip
CFLAGS ?= -Os -Wall -Wextra -pedantic

.PHONY: all
s5fstool: s5fstool.c

.PHONY: clean
clean:
	-$(RM) -f ./s5fstool

.PHONY: strip
strip: s5fstool
	-$(STRIP) ./s5fstool

