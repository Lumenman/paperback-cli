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
SOURCES=$(SDIR)/main.c $(SDIR)/paperbak.c $(SDIR)/Printer.c $(SDIR)/Scanner.c $(SDIR)/Fileproc.c $(SDIR)/Decoder.c $(SDIR)/Crc16.c $(SDIR)/Ecc.c $(PDIR)/src/FileAttributes.c $(PDIR)/src/Borland.c
HEADERS=$(wildcard include/*.h) $(wildcard $(PDIR)/include/*.h)

.PHONY: all main test clean
all: $(EX)
main: $(EX)

$(EX): $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(CORE): test_core.c $(filter-out $(SDIR)/main.c,$(SOURCES)) $(HEADERS) $(SDIR)/main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_core.c $(filter-out $(SDIR)/main.c,$(SOURCES)) $(LDFLAGS) $(LDLIBS) -o $@

test: $(EX) $(CORE)
	$(PYTHON) test_cli.py ./$(EX)
	./$(CORE)

clean:
	$(RM) paperback-cli paperback-cli.exe test_core test_core.exe *.o
