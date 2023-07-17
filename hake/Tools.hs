--------------------------------------------------------------------------
-- Copyright (c) 2015 ETH Zurich.
-- All rights reserved.
--
-- This file is distributed under the terms in the attached LICENSE file.
-- If you do not find this file, copies can be found by writing to:
-- ETH Zurich D-INFK, CAB F.78, Universitaetstrasse 6, CH-8092 Zurich,
-- Attn: Systems Group.
--
-- Toolchain definitions for Hake
--
--------------------------------------------------------------------------

module Tools where

import System.FilePath
import Data.Maybe(fromMaybe)

findTool path prefix tool = path </> (prefix ++ tool)

data ToolDetails =
    ToolDetails {
        toolPath :: FilePath,
        toolPrefix :: String,
        extraCFlags :: [String]
    }

-- This is the default root under which toolchains are installed at ETH.
-- It can be overridden when running Hake.
mkRoot root = fromMaybe "/home/netos/tools" root

--
-- ARM Cortex-A little-endian toolchains (armv7)
--

-- System (Ubuntu) ARM toolchain
arm_system _
    = ToolDetails {
        toolPath = "",
        toolPrefix = "arm-linux-gnueabi-",
        extraCFlags = []
      }

-- Linaro 2015.08 (GCC 5.1)
arm_netos_linaro_2015_08 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-5.1-2015.08-x86_64_arm-eabi" </>
                   "bin",
        toolPrefix = "arm-eabi-",
        extraCFlags = []
      }

-- Linaro 2015.06 (GCC 4.8)
arm_netos_linaro_2015_06 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.8-2015.06-x86_64_arm-eabi" </>
                   "bin",
        toolPrefix = "arm-eabi-",
        extraCFlags = []
      }

-- Linaro 2015.05 (GCC 4.9)
arm_netos_linaro_2015_05 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2015.05-x86_64_arm-eabi" </>
                   "bin",
        toolPrefix = "arm-eabi-",
        extraCFlags = []
      }

-- Linaro 2015.02 (GCC 4.9)
arm_netos_linaro_2015_02 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2015.02-3-x86_64_arm-eabi" </>
                   "bin",
        toolPrefix = "arm-eabi-",
        extraCFlags = []
      }

-- Linaro 2014.11 (GCC 4.9)
arm_netos_linaro_2014_11 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2014.11-x86_64_arm-eabi" </>
                   "bin",
        toolPrefix = "arm-eabi-",
        extraCFlags = []
      }

--
-- ARM AArch64 toolchains
--

-- System (Ubuntu) ARM toolchain
arm_system_aarch64 _
    = ToolDetails {
        toolPath = "",
        toolPrefix = "aarch64-linux-gnu-",
        extraCFlags = []
      }

-- System (macOS Homebrew) ARM toolchain from Keil
arm_system_aarch64_macos _
    = ToolDetails {
        toolPath = "",
        toolPrefix = "aarch64-none-elf-",
        extraCFlags = [
            -- GCC 10.1 enables outline atomics by default, disabling.
            -- https://patchwork.kernel.org/project/kvm/patch/20200728121751.15083-1-drjones@redhat.com/
            "-mno-outline-atomics",
              -- The Keil aarch64-none-elf- toolchain used on macOS complains about
              -- RWE segments.  Disabling this warning as of now, but should check
              -- why we have RWE segments in the first place.
              -- https://github.com/OP-TEE/optee_os/issues/5471
              "-Wl,--no-warn-rwx-segments"
            ]
      }

-- Linaro 2015.08 (GCC 5.1)
arm_netos_linaro_aarch64_2015_08 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-5.1-2015.08-x86_64_aarch64-elf" </>
                   "bin",
        toolPrefix = "aarch64-elf-",
        extraCFlags = []
      }

-- Linaro 2015.08 (GCC 5.1)
arm_netos_linaro_aarch64_2016_02 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-5.3-2016.02-x86_64_aarch64-elf" </>
                   "bin",
        toolPrefix = "aarch64-elf-",
        extraCFlags = []
      }

-- Linaro 2015.02 (GCC 4.9)
arm_netos_linaro_aarch64_2015_02 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2015.02-3-x86_64_aarch64-elf" </>
                   "bin",
        toolPrefix = "aarch64-elf-",
        extraCFlags = []
      }

-- Linaro 2014.11 (GCC 4.9)
arm_netos_linaro_aarch64_2014_11 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2014.11-x86_64_aarch64-elf" </>
                   "bin",
        toolPrefix = "aarch64-none-elf-",
        extraCFlags = []
      }

--
-- ARM Cortex-M little-endian toolchains (armv7m)
--

-- ARM-GCC 2014q4 (GCC 4.9)
arm_netos_arm_2014q4 root
    = ToolDetails {
        toolPath = mkRoot root </> "gcc-arm-embedded" </>
                   "gcc-arm-none-eabi-4_9-2014q4" </>
                   "bin",
        toolPrefix = "arm-none-eabi-",
        extraCFlags = []
      }

-- ARM-GCC 2015q1 (GCC 4.9)
arm_netos_arm_2015q1 root
    = ToolDetails {
        toolPath = mkRoot root </> "gcc-arm-embedded" </>
                   "gcc-arm-none-eabi-4_9-2015q1" </>
                   "bin",
        toolPrefix = "arm-none-eabi-",
        extraCFlags = []
      }

-- ARM-GCC 2015q2 (GCC 4.9)
arm_netos_arm_2015q2 root
    = ToolDetails {
        toolPath = mkRoot root </> "gcc-arm-embedded" </>
                   "gcc-arm-none-eabi-4_9-2015q2" </>
                   "bin",
        toolPrefix = "arm-none-eabi-",
        extraCFlags = []
      }

--
-- ARM big-endian toolchains (xscale)
--

-- Linaro 2015.02 (GCC 4.9)
arm_netos_linaro_be_2015_02 root
    = ToolDetails {
        toolPath = mkRoot root </> "linaro" </>
                   "gcc-linaro-4.9-2015.02-3-x86_64_armeb-eabi" </>
                   "bin",
        toolPrefix = "armeb-eabi-",
        extraCFlags = []
      }

-- System (Ubuntu 16.04) aarch64 toolchain
aarch64_system _
    = ToolDetails {
        toolPath = "",
        toolPrefix = "aarch64-linux-gnu-",
        extraCFlags = []
      }

--
--
-- X86_64 toolchains (x86_64)
--

-- System (Ubuntu) ARM toolchain
x86_system _
    = ToolDetails {
        toolPath = "",
        toolPrefix = "x86_64-linux-gnu-",
        extraCFlags = []
      }

--
-- Xeon Phi toolchains (k1om)
--

-- Intel MPSS 3.4 (GCC 4.7)
k1om_netos_mpss_3_4 root
    = ToolDetails {
        toolPath = mkRoot root </>
                   "mpss-3.4/x86_64-mpsssdk-linux" </>
                   "usr/bin/k1om-mpss-linux",
        toolPrefix = "k1om-mpss-linux-",
        extraCFlags = []
      }

k1om_netos_mpss_3_7_1 root
    = ToolDetails {
        toolPath = mkRoot root </>
                   "mpss-3.7.1/x86_64-mpsssdk-linux" </>
                   "usr/bin/k1om-mpss-linux",
        toolPrefix = "k1om-mpss-linux-",
        extraCFlags = []
      }

k1om_system root
    = ToolDetails {
        toolPath = "",
        toolPrefix = "k1om-mpss-linux-",
        extraCFlags = []
      }
