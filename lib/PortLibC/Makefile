EX=
TESTEX=runtests
CC=g++
INC=include
TESTDIR=test
CFLAGS=-std=c++11 -I"$(INC)"

tester: $(TESTDIR)/test.cpp
	$(CC) $^ src/FileAttributes.c src/Borland.c $(CFLAGS) -o $(TESTEX) -lboost_unit_test_framework
	cp $(TESTEX) .git/hooks/pre-commit


clean:
	rm $(TESTEX) $(EX)

