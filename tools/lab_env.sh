#!/bin/bash

# This script tells GHC to look for additional packages on the provided network share.
# You only need it if you are setting up on a lab machine.
export GHC_PACKAGE_PATH="/net5/sg.fs.inf.ethz.ch/export/sg/nfs3/netos/teaching/aos/ghc/x86_64-linux-$(ghc --numeric-version)/package.conf.d:"