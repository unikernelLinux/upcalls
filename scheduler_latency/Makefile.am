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

CLEANFILES = *~ *.o *.a sched_lat

noinst_DATA = 

AM_CFLAGS = -O2 -ggdb -Wall -Werror

data.tar.gz:
	touch data.tar.gz

sched_lat: tester.o
	gcc -o $@ $< -ggdb -Wall -Werror -lpthread

all: sched_lat

stamp-initrd: sched_lat
	rm -f $@
	mkdir -p ../initrd/app
	cp $< ../initrd/app
	touch $@

.c.o:
	$(CC) $(CFLAGS) $(AM_CFLAGS) -c $< -o $@
