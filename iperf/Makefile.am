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

CLEANFILES = *~ data.tar.gz stamp-initrd

noinst_DATA = 

data.tar.gz:
	touch data.tar.gz

# Get a local copy of iperf
stamp-iperf-dir:
	rm -f $@
	if ! test -d iperf-3.20; then \
		wget https://github.com/esnet/iperf/releases/download/3.20/iperf-3.20.tar.gz; \
		tar xvf iperf-3.20.tar.gz; \
	fi
	touch $@

iperf: stamp-iperf-dir
	cd iperf-3.20 && ./configure
	$(MAKE) -C iperf-3.20
	cp iperf-3.20/src/iperf3 ./iperf

all: stamp-initrd

stamp-initrd: iperf
	rm -f $@
	mkdir -p ../initrd/app
	cp iperf ../initrd/app/iperf
	touch $@

.PHONY:
clean:
	rm -f $(CLEANFILES)
	$(MAKE) -C iperf-3.20 clean
