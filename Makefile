ifeq ($(OS),Windows_NT)
EXEEXT=.exe
PYTHON=python
else
PYTHON=python3
endif
EX=paperback-cli$(EXEEXT)
CORE=test_core$(EXEEXT)
SDIR=src
PDIR=lib/PortLibC
CC=gcc
CFLAGS=-O2 -std=gnu11
CPPFLAGS=-Iinclude -I$(PDIR)/include
LDLIBS=-lm
SOURCES=$(SDIR)/main.c $(SDIR)/paperbak.c $(SDIR)/Printer.c $(SDIR)/Scanner.c $(SDIR)/Fileproc.c $(SDIR)/Decoder.c $(SDIR)/Crc16.c $(SDIR)/Sha256.c $(SDIR)/Text.c $(SDIR)/Ecc.c $(PDIR)/src/FileAttributes.c $(PDIR)/src/Borland.c
HEADERS=$(wildcard include/*.h) $(wildcard $(PDIR)/include/*.h)

.PHONY: all main test check sanitize clean
all: $(EX)
main: $(EX)

$(EX): $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

# test_core includes main.c and Decoder.c directly, to reach their static
# functions; both are therefore filtered out of the link.
$(CORE): test_core.c $(filter-out $(SDIR)/main.c $(SDIR)/Decoder.c,$(SOURCES)) $(HEADERS) $(SDIR)/main.c $(SDIR)/Decoder.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_core.c $(filter-out $(SDIR)/main.c $(SDIR)/Decoder.c,$(SOURCES)) $(LDFLAGS) $(LDLIBS) -o $@

test: $(EX) $(CORE)
	$(PYTHON) test_cli.py ./$(EX)
	./$(CORE)

# Warnings are part of the build, not an optional extra: the four that were
# left here for years hid a branch that reported "printing is disabled" and
# then went on with an unset width, and a -o long enough to run off the end of
# the buffer it was copied into.
check: CFLAGS+=-Wall -Wextra -Werror
check: clean $(EX) $(CORE)
	$(PYTHON) test_cli.py ./$(EX)
	./$(CORE)

# Same tests under AddressSanitizer and UndefinedBehaviorSanitizer, which is
# most of what a bounds-checked language would give this code: every array
# walk, every packed struct read and every path built from a scanned page is
# checked as it runs. Slower, so it is a target and not the default.
#
# Both need runtime libraries that mingw-w64 GCC does not ship, so on Windows
# either build under MSYS2 clang (CC=clang) or ask for the variant that needs
# no runtime and aborts instead of reporting:
#
#   make sanitize SANFLAGS="-fsanitize=undefined -fsanitize-undefined-trap-on-error"
#
# That one does link with mingw GCC and both suites pass under it.
SANFLAGS=-fsanitize=address,undefined
sanitize: CFLAGS=-O1 -g -std=gnu11 $(SANFLAGS) -fno-omit-frame-pointer
sanitize: LDFLAGS+=$(SANFLAGS)
sanitize: clean $(EX) $(CORE)
	$(PYTHON) test_cli.py ./$(EX)
	./$(CORE)

clean:
	$(RM) paperback-cli paperback-cli.exe test_core test_core.exe *.o
