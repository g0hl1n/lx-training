#!/bin/sh
#
ARCH=arm
CROSS_COMPILE=/usr/bin/arm-linux-gnueabi-
export ARCH CROSS_COMPILE

# make the omap defconfig
make omap2plus_defconfig

# build the kernel binary
LOADADDR=0x80008000 LOCALVERSION=-defconfig make -j 4 uImage

# build the device tree binaries
make dtbs

# copy to tftp server dir
sudo cp arch/arm/boot/uImage /var/lib/tftpboot/
sudo cp arch/arm/boot/dts/am335x-boneblack.dtb /var/lib/tftpboot/
