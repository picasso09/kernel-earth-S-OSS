#!/bin/bash

# set environment variables
git clone --depth=1 https://github.com/picasso09/proton-clang clang
export KBUILD_BUILD_HOST="DV-WORK"
export KBUILD_BUILD_USER="picasso09"
export PATH="$(pwd)/clang/bin:$PATH"
export TZ="Asia/Jakarta"

# build kernel
make -j$(nproc --all) O=out ARCH=arm64 earth_defconfig
make -j68 ARCH=arm64 O=out \
                      HOSTCC="ccache clang" \
                      HOSTCXX="ccache clang++" \
                      CC="ccache clang" \
                      LD=ld.lld \
                      AR=llvm-ar \
                      NM=llvm-nm \
                      OBJCOPY=llvm-objcopy \
                      OBJDUMP=llvm-objdump \
                      READELF=llvm-readelf \
                      OBJSIZE=llvm-size \
                      STRIP=llvm-strip \
                      CROSS_COMPILE=aarch64-linux-gnu- \
                      CROSS_COMPILE_ARM32=arm-linux-gnueabi-

