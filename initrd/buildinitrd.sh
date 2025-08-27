#!/bin/bash
# This file based in part on the mkinitramfs script for the LFS LiveCD
# written by Alexander E. Patrakov and Jeremy Huntwork.

copy()
{
  local file

  if [ "$2" = "lib" ]; then
    file=$(PATH=/usr/lib:/usr/lib64:/usr/lib/x86_64-linux-gnu type -p $1)
  else
    file=$(type -p $1)
  fi

  if [ -n "$file" ] ; then
    cp $file ${WDIR}$file
  else
    echo "Missing required file: $1 for directory $2"
    rm -rf ${WDIR}
    exit 1
  fi
}

printf "Creating initramfs structure ... "

cp ./perf /usr/sbin/

binfiles="cat ls mkdir lspci mknod mount bash top touch awk less"
binfiles="$binfiles umount sed sleep ln rm uname grep nproc"
binfiles="$binfiles readlink basename chmod ps pidof pgrep pkill"
binfiles="$binfiles cut netstat"

sbinfiles="halt dropbear ip rdmsr wrmsr perf ethtool"

unsorted=$(mktemp /tmp/unsorted.XXXXXXXXXX)

INITIN=./init

# Create a temporary working directory
WDIR=$1

mkdir -p ${WDIR}
chmod 775 ${WDIR}

# Create base directory structure
for dir in  "${WDIR}/dev" "${WDIR}/etc/dropbear" "${WDIR}/run" "${WDIR}/sys" "${WDIR}/proc" "${WDIR}/usr/bin" "${WDIR}/usr/lib/x86_64-linux-gnu" "${WDIR}/usr/lib64" "${WDIR}/usr/sbin" "${WDIR}/var/run" 
do
	mkdir -p $dir
done
ln -s usr/bin  ${WDIR}/bin
ln -s usr/lib  ${WDIR}/lib
ln -s usr/sbin ${WDIR}/sbin
ln -s lib      ${WDIR}/lib64

# Create necessary device nodes
mknod -m 640 ${WDIR}/dev/tty0    c 4 0
mknod -m 640 ${WDIR}/dev/tty1    c 4 1
mknod -m 640 ${WDIR}/dev/tty     c 5 0
mknod -m 640 ${WDIR}/dev/console c 5 1
mknod -m 644 ${WDIR}/dev/ptmx    c 5 2
mknod -m 664 ${WDIR}/dev/null    c 1 3
mknod -m 664 ${WDIR}/dev/zero    c 1 5
mknod -m 664 ${WDIR}/dev/random  c 1 8
mknod -m 664 ${WDIR}/dev/urandom c 1 9
mknod -m 664 ${WDIR}/dev/loop0   b 7 0
mknod -m 664 ${WDIR}/dev/loop1   b 7 1
mkdir -m 755 ${WDIR}/dev/pts
mknod -m 600 ${WDIR}/dev/pts/0   c 136 0
mknod -m 000 ${WDIR}/dev/pts/ptmx c 5 2


# Install the init file
install -m0755 $INITIN ${WDIR}/init

# At somepoint dropbear stopped supporting scp directly and now requires access
# to an sftp server so we have to pull in enough of openssh to make that work
mkdir -p ${WDIR}/usr/lib/openssh/
ldd /usr/lib/openssh/sftp-server | sed "s/\t//" | cut -d " " -f1 >> $unsorted
copy /usr/lib/openssh/sftp-server usr/lib/openssh/

# Install basic binaries
for f in $binfiles ; do
  ldd /usr/bin/$f | sed "s/\t//" | cut -d " " -f1 >> $unsorted
  copy /usr/bin/$f bin
done

ln -s bash ${WDIR}/usr/bin/sh

for f in $sbinfiles ; do
  ldd /usr/sbin/$f | sed "s/\t//" | cut -d " " -f1 >> $unsorted
  copy $f sbin
done

for f in `ls app/` ; do
  ldd $f | sed "s/\t//" | cut -d " " -f1 >> $unsorted
  cp f ${WDIR}/usr/bin/
done

# Install libraries
sort $unsorted | uniq | while read library ; do
# linux-vdso and linux-gate are pseudo libraries and do not correspond to a file
# libsystemd-shared is in /lib/systemd, so it is not found by copy, and
# it is copied below anyway
  if [[ "$library" == linux-vdso.so.1 ]] ||
     [[ "$library" == linux-gate.so.1 ]] ||
     [[ "$library" == libsystemd-shared* ]]; then
    continue
  fi

  copy $library lib
done

mkdir -p ${WDIR}/usr/share

cp -r /usr/share/terminfo ${WDIR}/usr/share

conf="bash.bashrc bash_completion.d group hosts passwd profile"
conf="$conf shells shadow"
for f in $conf ; do
  cp -r /etc/$f ${WDIR}/etc/$f
done

if [ -z "$2" -a -s "$2" ] ; then
	cp $2 ${WDIR}/usr/bin/
fi

if [ -s data.tar.gz ] ; then
	tar xf data.tar.gz -C ${WDIR}
fi

cp -r exp ${WDIR}

mv lib/modules ${WDIR}/lib

rm -f $unsorted

printf "done.\n"
