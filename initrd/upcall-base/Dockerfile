FROM debian:13

RUN printf "Types: deb\nURIs: http://deb.debian.org/debian\nSuites: trixie\nComponents: non-free-firmware\n" > /etc/apt/sources.list.d/debiannonfree.sources && \
    apt-get update
RUN apt-cache search firmware-linux-nonfree
RUN apt-get upgrade -y
RUN apt-get -y install bash sed libelf-dev bc hostname perl dropbear dropbear-bin dropbear-initramfs \
        pciutils msr-tools wget bzip2 curl xz-utils cpio passwd procps iproute2 \
	net-tools openssh-server openssh-sftp-server less ethtool libacl1 kmod libbabeltrace1 \
	libdebuginfod1t64 libnuma1 libpfm4 libpython3.13 libslang2 libtraceevent1 kmod strace \
	firmware-linux-nonfree firmware-realtek firmware-bnx2 firmware-bnx2x firmware-qlogic
