# Board controller for the imx8x Colibri boards
#
# Copyright (c) 2022, The University of British Columbia
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.
# If you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.

from logger import *


#########################################################################################
# Static Autograder Configuration Options
#########################################################################################


# this is the expected string to be seen when booting
CFG_EXPECT_BOOT = "Barrelfish CPU driver starting on ARMv8 \\(BSP\\)"

# the boot phrase of the linux boot
CFG_EXPECT_BOOT_LINUX = "Colibri-iMX8X-NetOS_Reference-Minimal-Image"

# this is the timeout to wait after boot
CFG_BOOT_TIMEOUT = 5

# timeout for preparing the baord
CFG_PREPARE_TIMEOUT = 300

# this is the delay between two characters sent to the serial
CFG_CONSOLE_TYPE_DELAY = 0.13

# the newline-return characters
CFG_CONSOLE_RETURN = "\r\n"

# this defines default strings that cause the test to fail.
CFG_DEFAULT_FAIL = [ ]

# the menu.lst list for the imx8x platform
CFG_MENU_LST_IMX8X = 'menu.lst.armv8_imx8x'

# the menu.lst list for the QEMU platform
CFG_MENU_LST_QEMU = 'menu.lst.armv8_a57_qemu'

# this is the USB serial to be used
CFG_LOCAL_CONSOLE_USB = "/dev/ttyUSB0"

# the additional tools to be built
CFG_TOOLS_MODULES = ["./tools/bin/armv8_bootimage"]

# the default linux user
CFG_LINUX_USER = "root"

# the linux user password (psst!)
CFG_LINUX_PASS = ""
