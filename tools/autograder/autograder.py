#!/usr/bin/python3

import argparse
import os
import sys
import pathlib
import traceback

from logger import *
from config import *
from boardctrl import *
from imagebuilder import *
from testrunner import *

#########################################################################################
# Get the script paths and tools directory
#########################################################################################

# XXX: this assumes that the autograder is in $SRC/tools/autograder

# this is the directory of the current script
SCRIPT_DIRECTORY = pathlib.Path(os.path.dirname(os.path.realpath(__file__)))

# this is the directory which contains the tests
TESTS_DIRECTORY = SCRIPT_DIRECTORY / "tests"

# this is the tools directory
TOOLS_DIRECTORY = SCRIPT_DIRECTORY.parent

# this is the root of the aos source tree
ROOT_DIRECTORY = TOOLS_DIRECTORY.parent

# this is the build path, defaults to $ROOT/build
BUILD_PATH = ROOT_DIRECTORY / 'build'

# where the script stores the test logs
LOG_DIRECTORY = ROOT_DIRECTORY / "test-logs"


#########################################################################################
# Arguments Parser
#########################################################################################

parser = argparse.ArgumentParser()

parser.add_argument("-v", "--verbose", action="store_true",
                    help="increase output verbosity")

parser.add_argument("-d", "--docker", default=False, action="store_true",
                    help="run compilation in a docker container, and run Qemu in docker. (default: False)")

parser.add_argument("-q", "--qemu", default=False, action="store_true",
                    help="run all tests in Qemu instead of the board. (default: False)")

parser.add_argument("-t", "--tests", nargs='+', default=[],
                    help='the tests to run <test:subtest:subtest>')

parser.add_argument("-b", "--board", default=None,
                    help="identifier of the board to be selected to run the test.")

parser.add_argument("-s", "--serial", default=CFG_LOCAL_CONSOLE_USB,
                    help=f"path to the USB TTY of the board's console to be used (default {CFG_LOCAL_CONSOLE_USB})")

parser.add_argument("-o", "--buildpath", default=BUILD_PATH,
                    help=f"sets the path of the build directory (default {BUILD_PATH}")

parser.add_argument("-c", "--config", default=CFG_SITE_CONFIGS[0], choices=CFG_SITE_CONFIGS,
                    help=f"selects the run configuration for the board. (default: {CFG_SITE_CONFIGS[0]}, availabe: {CFG_SITE_CONFIGS}")

parser.add_argument("-l", "--log", default=LOG_DIRECTORY,
                    help=f"sets the path of the log directory (default {LOG_DIRECTORY}")


parser.add_argument("-r", "--remote", default=None,
                    help='run on remote server')

parser.add_argument("-p", "--port", default=22,
                    help='ssh port of the remote server')

parser.add_argument("-f", "--forcehake", action="store_true",
                    help="force rehake during the build")

parser.add_argument("-m", "--menulst", default=None,
                    help='the menu lst to be used')


#########################################################################################
# Main
#########################################################################################

if __name__ == '__main__':
    args = parser.parse_args()

    # change into the root directory
    os.chdir(ROOT_DIRECTORY)

    # set the logging
    set_verbose_logging(args.verbose)

    # create the log directory
    LOG_DIRECTORY.mkdir(parents=True, exist_ok=True)

    # figure out the menulst file to be used
    if args.menulst == None or len(args.menulst) == 0:
        if args.qemu:
            menulst = ROOT_DIRECTORY / 'hake' / CFG_MENU_LST_QEMU
        else:
            menulst = ROOT_DIRECTORY / 'hake' / CFG_MENU_LST_IMX8X
    else :
        menulst = pathlib.Path(args.menulst)

    # create the image builder object
    logtitle("Prepare Step: setting up the build environment.")
    try :
        buildpath = pathlib.Path(args.buildpath)
        if args.docker :
            imgbuilder = ImageBuilderDocker(ROOT_DIRECTORY, menulst)
        else :
            imgbuilder = ImageBuilderNative(ROOT_DIRECTORY, menulst, buildpath)
        imgbuilder.prepare()
        imgbuilder.hake(force=args.forcehake)
        imgbuilder.build_tools()
        imgbuilder.build_base_image()
        logok("Preparation completed. Makefile ready.")
    except Exception as e:
        logerr("Preparation failed.")
        logerr(str(e))
        if args.verbose:
            print(traceback.format_exc())
        sys.exit(1)

    # create the test runner for the configuration
    if args.qemu :
        if args.docker :
            testrunner = TestRunnerQemuDocker(imgbuilder, TESTS_DIRECTORY, args.tests)
        else :
            testrunner = TestRunnerQemu(imgbuilder, TESTS_DIRECTORY, args.tests)
    else:
        print(args.config)
        try :
            if args.config == "ethz-remote":
                boardctrl = BoardCtrlRemoteETHZ(args.board)
            elif args.config == "ethz-local":
                boardctrl = BoardCtrlLocalETHZ(args.board)
            elif args.config == "ubc":
                boardctrl = BoardCtrlRemoteUBC(args.board)
            elif args.config == "local-auto":
                boardctrl = BoardCtrlLocalAutoReset(ROOT_DIRECTORY, args.board, args.serial)
            else :
                boardctrl = BoardCtrlLocalDefault(ROOT_DIRECTORY, args.board, args.serial)
        except Exception as e:
            logerr("Configuration failed. see -h for help")
            logerr(str(e))
            sys.exit(1)

        testrunner = TestRunnerColibri(boardctrl, imgbuilder, TESTS_DIRECTORY, args.tests)

    testrunner.set_logdir(pathlib.Path(args.log))

    # prepare, run the tests and report the results
    try:
        testrunner.prepare()
        testrunner.run_tests()
        testrunner.save_and_print_results()
    except Exception as e:
        logtitle("Exception occurred")
        logerr("Execution Failed")
        logerr(str(e))
        if args.verbose:
            print(traceback.format_exc())