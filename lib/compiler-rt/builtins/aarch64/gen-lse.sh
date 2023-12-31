#!/bin/bash

##########################################################################
# Copyright (c) 2022 University of British Columbia.
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.
# If you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
# Attn: Systems Group.
##########################################################################

for p in cas swp ldadd ldclr ldeor ldset; do
    for s in 1 2 4 8; do
        for m in 1 2 3 4; do
            f="lse_${p}_${s}_${m}.S"
            echo "// this file is autogenerated by gen-lse.sh" > $f
            echo "#define L_${p}" >> ${f}
            echo "#define SIZE ${s}" >> ${f}
            echo "#define MODEL ${m}" >> ${f}
            echo "#include \"lse.S\"" >> ${f}
        done;
    done;
done;