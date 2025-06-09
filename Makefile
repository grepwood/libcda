# SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
CC=gcc
CFLAGS=-O2 -fPIC
ifeq ($(DEBUG),1)
	CFLAGS += -ggdb
endif
WARNING_FLAGS=-Wall -Wextra -pedantic -Werror

JSON_C_LDFLAGS=-ljson-c
LIBCURL_LDFLAGS=-lcurl
LIBXML2_LDFLAGS=$(shell xml2-config --libs)
LINKER_FLAGS=$(JSON_C_LDFLAGS) $(LIBCURL_LDFLAGS) $(LIBXML2_LDFLAGS)

LIBXML2_IFLAGS=$(shell xml2-config --cflags)

.PHONY: all clean

all: cda2url cda2url-nolib

clean:
	rm -f cda2url.o libcda.so main.o cda2url cda2url-nolib

cda2url.o:
	$(CC) $(LIBXML2_IFLAGS) $(CFLAGS) $(WARNING_FLAGS) -c cda2url.c -o cda2url.o

libcda.so: cda2url.o
	$(CC) $(LINKER_FLAGS) $(CFLAGS) $(WARNING_FLAGS) -shared cda2url.o -o libcda.so

main.o:
	$(CC) $(CFLAGS) $(WARNING_FLAGS) -c main.c -o main.o

cda2url: libcda.so main.o
	$(CC) $(LIBCURL_LDFLAGS) $(CFLAGS) $(WARNING_FLAGS) libcda.so main.o -o cda2url

cda2url-nolib: cda2url.o main.o
	$(CC) $(LINKER_FLAGS) $(CFLAGS) $(WARNING_FLAGS) cda2url.o main.o -o cda2url-nolib
