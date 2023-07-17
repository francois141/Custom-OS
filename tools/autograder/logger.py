# Board controller for the imx8x Colibri boards
#
# Copyright (c) 2022, The University of British Columbia
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.
# If you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.


from plumbum import colors

#########################################################################################
# Logging Facility
#########################################################################################

verboselogging = False

def set_verbose_logging(verbose):
    global verboselogging
    verboselogging = verbose

def logwarn(msg):
    print(colors.bold & colors.yellow | " + [WARN]  ", end=" "),
    print(colors.bold.reset & colors.yellow.reset | msg)


def logerr(msg):
    print(colors.bold & colors.red | " + [ERR ]  ", end=" "),
    print(colors.bold & colors.red.reset | msg)


def loginfo(msg):
    print(colors.bold & colors.info | " + [INFO]  ", end=" "),
    print(colors.bold.reset & colors.info.reset | msg)


def logok(msg):
    print(colors.bold & colors.success | " + [ OK ]  ", end=" "),
    print(colors.bold & colors.info.reset | msg)


def logstart(msg):
    print(colors.bold & colors.info | "\n + [TEST]  ", end=" "),
    print(colors.bold & colors.info.reset | msg)


def logpass(msg):
    print(colors.bold & colors.success | " # [PASS]  ", end=" "),
    print(colors.bold & colors.success.reset | msg)


def logfail(msg):
    print(colors.bold & colors.red | " # [FAIL]  ", end=" "),
    print(colors.bold & colors.red.reset | msg)


def logtitle(msg):
    print(colors.bold & colors.title | "\n\n{}".format(msg)),
    print(colors.bold.reset & colors.title.reset)

def logsubtitle(msg):
    print(colors.bold | "\n", end=""),
    print(colors.bold & colors.info.reset | msg)

def logresult(msg):
    print(colors.bold & colors.title | "\n# [RESULT] {}".format(msg)),
    print(colors.bold.reset & colors.title.reset)


def logmsg(msg):
    print(colors.bold.reset | "            > {}".format(msg))


def logverbose(msg):
    if verboselogging:
        print(colors.bold.reset | "            > {}".format(msg))


def logoutput(msg):
    print(colors.bold.reset | "            <------------- start of error output ------------->")
    lines = msg.split("\n")
    nlines = len(lines)
    if nlines > 10:
        print(colors.bold.reset | "             | {} more lines".format(nlines - 10))
        print(colors.bold.reset | "             | ")
        lines = lines[-9:]

    for l in lines:
        print(colors.bold.reset | "             | {}".format(l))

    print(colors.bold.reset | "            <-------------- end of error output -------------->")