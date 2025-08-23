FROM debian:13

RUN apt update
RUN apt -y install sed libelf-dev bc hostname perl dropbear dropbear-bin dropbear-initramfs \
        msr-tools wget bzip2 curl xz-utils cpio passwd procps iproute2 \
	pciutils net-tools openssh-server openssh-sftp-server less ethtool
