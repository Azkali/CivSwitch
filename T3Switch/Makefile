CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
SOURCES = src/hidkbd_ffs.c src/typer.c
HEADERS = $(wildcard src/*.h)

.PHONY: all clean check lint parity
all: hidkbd_ffs
hidkbd_ffs: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
check: hidkbd_ffs
	python3 tests/test_typer.py ./hidkbd_ffs
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_control.c src/typer.c $(LDFLAGS) $(LDLIBS) -o tests/test_control
	./tests/test_control
	@for file in scripts/*.sh tools/*.sh; do sh -n "$$file" || exit; done
lint:
	shellcheck -x -P scripts scripts/*.sh tools/*.sh
parity: hidkbd_ffs
	python3 tools/check-parity.py .. ./hidkbd_ffs
clean:
	rm -f hidkbd_ffs tests/test_control
