#
# Student makefile for Cache Lab
#
CC = gcc
CFLAGS = -g -Wall -Werror -std=c99
LLVM_PATH = /usr/local/depot/llvm-4.0/bin/

all: csim test-trans tracegen-ct
	-tar -cvf handin.tar  csim.c trans.c key.txt

csim: csim.c cachelab.c cachelab.h
	$(CC) $(CFLAGS) -o csim csim.c cachelab.c -lm

test-trans: test-trans.c trans.o cachelab.c cachelab.h
	$(CC) $(CFLAGS) -o test-trans test-trans.c cachelab.c trans.o

tracegen-ct: tracegen-ct.c trans.c cachelab.c
	$(LLVM_PATH)clang -emit-llvm -S -O0 trans.c -o trans.bc
	$(LLVM_PATH)opt trans.bc -load=ct/Check.so -Check -o trans.bc
	$(LLVM_PATH)opt trans.bc -O3 -o trans.bc
	$(LLVM_PATH)opt trans.bc -load=ct/CLabInst.so -CLabInst -o trans_ct.bc
	$(LLVM_PATH)llvm-link trans_ct.bc ct/ct.bc -o trans_fin.bc
	$(LLVM_PATH)clang -o tracegen-ct -O3 trans_fin.bc cachelab.c tracegen-ct.c -pthread -lrt

trans.o: trans.c
	$(CC) $(CFLAGS) -O0 -c trans.c

#
# Clean the src dirctory
#
clean:
	rm -rf *.o
	rm -f *.bc
	rm -f csim
	rm -f test-trans tracegen tracegen-ct
	rm -f trace.all trace.f*
	rm -f .csim_results .marker
