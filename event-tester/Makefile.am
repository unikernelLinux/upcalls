# Upcalls
# Copyright (C) 2025 Boston University,
# Eric B. Munson
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

CLEANFILES = *~ *.o *.a tcp_client tcp_echo data.tar.gz stamp-initrd

noinst_DATA = 

if ENABLE_LIBUPCALL
UPCALL_LIB = $(abs_top_builddir)/libupcall/libupcall.a
else
UPCALL_LIB =
endif

AM_CFLAGS = -O2 -ggdb -Wall -Werror

EV_SYS = @EVENT_SYSTEM@

if EV_IS_URING
SYS_LIB = liburing.a
AM_CFLAGS += -Iliburing/src/include
else
SYS_LIB =
endif

data.tar.gz:
	touch data.tar.gz

# Get a local copy of liburing
stamp-liburing-dir:
	rm -f $@
	if ! test -d liburing; then \
		git clone https://github.com/axboe/liburing.git; \
	fi
	touch $@

liburing.a: stamp-liburing-dir
	rm -f $@
	cd liburing && \
	./configure && \
	CFLAGS='-Wall -Wextra -fno-stack-protector' ENABLE_SHARED=0 make -j`nproc`
	mv liburing/src/liburing.a $@

tcp_client: tcp_client.c echo_defs.h tsc_logger.h
	gcc -o $@ $< -ggdb -Wall -Werror -lpthread

tcp_echo: tcp_echo.c $(EV_SYS).c tsc_logger.h echo_defs.h $(UPCALL_LIB) $(SYS_LIB)
	gcc $(AM_CFLAGS) -o $@ tcp_echo.c $(EV_SYS).c $(UPCALL_LIB) $(SYS_LIB) -ggdb -Wall -Werror -lpthread

all: tcp_client tcp_echo

stamp-initrd: tcp_echo
	rm -f $@
	mkdir -p ../initrd/app
	cp tcp_echo ../initrd/app
	touch $@

# automake doesn't add this rule unless we were to specify a C program
# to compile, which we don't want to do because of the very special
# linking requirements.
.c.o:
	$(CC) $(CFLAGS) $(AM_CFLAGS) -c $< -o $@
