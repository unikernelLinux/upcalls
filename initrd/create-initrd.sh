#!/bin/bash

set -x

ABSINIT=`readlink -f .`
pushd ../linux
INSTALL_MOD_PATH=${ABSINIT} make -j`nproc` modules_install
popd
docker build upcall-base -t upcall-base:latest
CONTAINER=$(docker run --rm --privileged -v ${ABSINIT}:/src -dit upcall-base:latest /bin/bash)
docker exec -w /src/ -it $CONTAINER ./set-passwd.sh
docker exec -w /src/ -it $CONTAINER ./buildinitrd.sh upcall-initrd $1
#docker exec -w /src/ -it $CONTAINER rm -rf upcall-initrd
docker stop $CONTAINER >/dev/null 2>&1 &
rmdir lib
