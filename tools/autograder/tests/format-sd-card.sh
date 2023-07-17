#!/bin/bash

############################################################################
# Copyright (c) 2022 The University of British Columbia.
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.
# If you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
# Attn: Systems Group.
############################################################################

# abort on error
set -e

# set the mount point for the SD card
MOUNT_POINT=/mnt/sdcard

# we want to format the sd card
dev=/dev/mmcblk1
part=${dev}p1

# This print is being checkd by the grading script. Leave it as is.
echo "Preparing SD card ${dev}"

# unmount and create the partition
umount -q $MOUNT_POINT || true

# create the file system on the partition
/usr/sbin/mkfs.vfat -I -F 32 -S 512 -s 8 $part

# mount it again
mount ${MOUNT_POINT}

# creating a few files
echo "Hello World" > ${MOUNT_POINT}/hello.txt
echo "file contents" > ${MOUNT_POINT}/note.txt

# creating a few directories
mkdir ${MOUNT_POINT}/mydir

echo "Hello Subdir" > ${MOUNT_POINT}/mydir/hello.txt
echo "subdir file contents" > ${MOUNT_POINT}/mydir/note.txt

umount ${MOUNT_POINT}

# This print is being checkd by the grading script. Leave it as is.
echo "SD Card prepared."
