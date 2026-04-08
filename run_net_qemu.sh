#!/bin/bash

set -x

SMP=${3:-1}
DEBUG=
if [ -n "${5}" ] ; then
	DEBUG="-s -S"
fi
TAPDEV=${6:-tapupcall}

MQ=
QUEUES=
TMQ=
if [[ $SMP > 1 ]]; then
	MQ="mq=on,vectors=$((2*$SMP+2)),"
	QUEUES=",queues=${SMP}"
	TMQ="multi_queue"
fi

ip tuntap add dev $TAPDEV mode tap ${TMQ}
ip link set dev $TAPDEV master qbr0
ip link set dev qbr0 up
ip link set dev $TAPDEV up

qemu-system-x86_64 \
    -cpu host,-smap,-smep -accel kvm -m 64G \
    -kernel $1 \
    -initrd $2 \
    -nodefaults -nographic -serial stdio \
    -no-shutdown -no-reboot \
    -append "console=ttyS0 net.ifnames=0 biosdevname=0 nordand nopti nokaslr -- IP=192.168.222.128 MAC=52:54:00:12:34:56 AFFINITY=${4}" \
    -netdev tap,ifname=${TAPDEV},id=eth0,script=no,downscript=no${QUEUES} \
    -device virtio-net-pci,netdev=eth0,${MQ}mac=52:54:00:12:34:56 \
    -smp $SMP $DEBUG

ip tuntap del dev $TAPDEV mode tap ${TMQ}
