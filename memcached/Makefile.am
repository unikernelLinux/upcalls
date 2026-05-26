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

CLEANFILES = *~ *.o stamp-initrd data.tar.gz

data.tar.gz:
	touch data.tar.gz

clean:
	rm -f ${CLEANFILES}
	make -C memcached clean

stamp-initrd: memcached/memcached
	rm -f $@
	mkdir -p ../initrd/app
	cp memcached/memcached ../initrd/app/memcached
	touch $@

stamp-memcached-dir:
	rm -Rf $@
	if ! test -d memcached; then \
		git clone https://github.com/unikernelLinux/memcached.git --branch upcalls; \
	fi
	touch $@

memcached/memcached: stamp-memcached-dir
	if [ -z "${ENABLE_LIBUPCALL}" ]; then \
	cd memcached && git checkout vanilla; \
	else \
	cd memcached && git checkout upcalls; \
	fi
	cd memcached && ./autogen.sh
	cd memcached && ./configure
	make -j$(nproc) -C memcached

all: memcached/memcached

