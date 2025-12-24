# SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
CC=gcc
CFLAGS=-O2 -fPIC
ifeq ($(DEBUG),1)
	CFLAGS += -ggdb
endif
WARNING_FLAGS=-Wall -Wextra -pedantic -Werror
IFLAGS=-Iinclude

JSON_C_LDFLAGS=-ljson-c
LIBCURL_LDFLAGS=-lcurl
LIBXML2_LDFLAGS=$(shell xml2-config --libs)
LINKER_FLAGS=$(JSON_C_LDFLAGS) $(LIBCURL_LDFLAGS) $(LIBXML2_LDFLAGS)

LIBXML2_IFLAGS=$(shell xml2-config --cflags)

.PHONY: all clean

all: cdatool cdatool-nolib

clean:
	rm -f src/get_url.o libcda.so src/main.o cdatool cdatool-nolib

src/get_url.o:
	$(CC) $(IFLAGS) $(LIBXML2_IFLAGS) $(CFLAGS) $(WARNING_FLAGS) -c src/get_url.c -o src/get_url.o

libcda.so: src/get_url.o
	$(CC) $(LINKER_FLAGS) $(CFLAGS) $(WARNING_FLAGS) -shared src/get_url.o -o libcda.so

src/main.o:
	$(CC) $(IFLAGS) $(CFLAGS) $(WARNING_FLAGS) -c src/main.c -o src/main.o

cdatool: libcda.so src/main.o
	$(CC) $(LIBCURL_LDFLAGS) $(CFLAGS) $(WARNING_FLAGS) libcda.so src/main.o -o cdatool

cdatool-nolib: src/get_url.o src/main.o
	$(CC) $(LINKER_FLAGS) $(CFLAGS) $(WARNING_FLAGS) src/get_url.o src/main.o -o cdatool-nolib
