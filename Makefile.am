# Upcalls
# Copyright (C) 2025 Boston University,
# Eric B. Munson
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


EXTRA_DIST = \
	.gitignore \
	LICENSE \
	README.md

CLEANFILES = \
	*~ *.o *.a \
	stamp-h1 \
	upcall-initrd.cpio.xz \
	linux/.config \
	vmlinuz

clean-local:
	-cd linux && $(MAKE) clean
	-cd $(PROGRAM) && $(MAKE) clean
	-cd initrd && $(MAKE) clean
	-find -name config.cache -delete

min-clean: app-clean kern-clean

app-clean:
	-cd libupcall && $(MAKE) clean
	-rm vmlinuz linux/vmlinux linux/arch/x86/boot/bzImage
	-cd $(PROGRAM) && $(MAKE) clean

kern-clean:
	-rm vmlinuz
	-cd initrd && $(MAKE) clean
	-cd linux && $(MAKE) clean && rm .config

init-clean:
	-cd initrd && $(MAKE) clean

DIST_SUBDIRS = event-tester

#----------------------------------------------------------------------
# Build Linux kernel

noinst_DATA = vmlinuz

stamp-linux-dir:
	rm -f $@
	if ! test -d linux; then \
		git clone -b upcalls https://github.com/unikernelLinux/linux.git; \
	fi
	touch $@

initrd/perf: linux/tools/perf/perf stamp-linux-dir
	cp $< $@

linux/tools/perf/perf: stamp-linux-dir linux/arch/x86/boot/bzImage
	$(MAKE) -C $(srcdir)/linux/tools perf

vmlinuz: stamp-linux-dir linux/arch/x86/boot/bzImage
	cp $< $@

linux/arch/x86/boot/bzImage: stamp-linux-dir linux/.config
	$(MAKE) -C $(srcdir)/linux olddefconfig O=$(abs_builddir)/linux
	$(MAKE) -C $(srcdir)/linux O=$(abs_builddir)/linux
	$(MAKE) -C $(srcdir)/linux scripts_gdb

linux/.config: stamp-linux-dir savedconfig
	rm -f $@
	cp $(srcdir)/savedconfig $@

#----------------------------------------------------------------------
# Compile the program.

if ENABLE_LIBUPCALL
UPCALL_LIB = $(abs_top_builddir)/libupcall/libupcall.a
else
UPCALL_LIB =
endif

$(abs_top_builddir)/libupcall/libupcall.a:
	cd $(abs_top_builddir)/libupcall && $(MAKE) 

$(PROGRAM)/stamp-initrd:
	$(MAKE) -C $(PROGRAM) stamp-initrd

#----------------------------------------------------------------------
# Per-program configuration and data files (if required).
$(PROGRAM)/data.tar.gz: $(UPCALL_LIB)
	rm -f $@
	cd $(PROGRAM) && $(MAKE) all
	if ! test -f $@; then tar zcf $@ --no-recursion . ; fi

#----------------------------------------------------------------------
# Build the initramfs

noinst_DATA += upcall-initrd.cpio.xz
# TODO add our application to the initrd
upcall-initrd.cpio.xz: $(PROGRAM) $(PROGRAM)/stamp-initrd $(PROGRAM)/data.tar.gz vmlinuz initrd/perf
	cp $(PROGRAM)/data.tar.gz initrd/
	$(MAKE) -C $(srcdir)/initrd $@
	cp initrd/$@ $@

#----------------------------------------------------------------------
# Boot the under qemu.

# Non-privileged:
boot: vmlinuz upcall-initrd.cpio.xz
	./run_qemu.sh $^

# Privileged (probably required if the kernel is expecting incoming
# network connections).
boot-priv: vmlinuz upcall-initrd.cpio.xz
	sudo run_net_qemu.sh $^

#----------------------------------------------------------------------
# Maintainers only!

# Check no files are missing from EXTRA_DIST rules, and that all
# generated files have been included in the tarball.  (Note you must
# have done 'make dist')
maintainer-check-extra-dist:
	@zcat $(PACKAGE_NAME)-$(VERSION).tar.gz | tar tf - | sort | \
	  sed 's,^$(PACKAGE_NAME)-$(VERSION)/,,' > tarfiles
	@git ls-files | \
	    sort > gitfiles
	@comm -13 tarfiles gitfiles > comm-out
	@echo Checking for differences between EXTRA_DIST and git ...
	@cat comm-out
	@[ ! -s comm-out ]
	@rm tarfiles gitfiles comm-out
	@echo PASS: EXTRA_DIST tests

# Commit everything in the current directory and set the commit
# message to the current version number.
maintainer-commit:
	git commit -a -m "Version $(VERSION)."

# Tag HEAD with the current version.
maintainer-tag:
	git tag -a v$(VERSION) -m "Version $(VERSION)." -f
