# Board controller for the imx8x Colibri boards
#
# Copyright (c) 2022, The University of British Columbia
# All rights reserved.
#
# This file is distributed under the terms in the attached LICENSE file.
# If you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.

# for type annotations
from __future__ import annotations

import json
import pexpect
import time
import re

from pathlib import Path
from collections.abc import Sequence
from plumbum import local
from plumbum.commands.processes import CommandNotFound

from imagebuilder import AbstractImageBuilder
from boardctrl import AbstractBoardController
from logger import *
from config import *


#########################################################################################
# Test Class
#########################################################################################


# Test class capturing a subtest and its result
class Test:
    # constructor
    def __init__(self, testdir: Path, milestone : str, ident : str, config : dict):
        # directory to the tests
        self._testdir = testdir
        # the test configuration
        self._config = config
        # the milestone this test belongs to
        self._milestone = milestone
        # the test identifier
        self._ident = ident

        # the time when the test was started
        self._tstart = None

        # the time when the test was ended
        self._tend = None

        # console file handle
        self._con = None
        # the expected test string
        self._expected = ""
        # the image to be used to run the test
        self._test_image = None
        # the next test step to be extecuted [0, len(teststeps)]
        self._current_test_step = 0
        # the next prepare step to be executed [0, len(prepare)]
        self._current_prepare_step = 0
        # whether the test has failed
        self._failed = False
        # whether there was an error with the test
        self._error = False
        # the number of points awarded for this test
        self._points = 0.0
        # the maximum number of possibel points
        self._max_points = 0.0
        for s in config['teststeps']:
            if s["action"] == "expect":
                rep = s.get("repeat", 1)
                self._max_points += (s['points'] * rep if 'points' in s else 0)

    # obtains the milestone string
    def milestone(self):
        return self._milestone

    # obtains the test identifier
    def id(self) :
        return self._ident

    # obtains the log file name to be used
    def logfilename(self) :
        return self._config['logfile']

    # obtains the test name
    def name(self):
        return self._config["title"]

    # obtains the test class
    def tclass(self):
        cl = self._config.get("class", "Default")
        if cl.lower() == "base" :
            return "Base"
        if cl.lower() == "target" :
            return "Target"
        if cl.lower() == "extra" :
            return "Extra"
        return cl

    # obtains the duraction of the test
    def duration(self) :
        if self._tstart == None or self._tend == None :
            return 0
        return self._tend - self._tstart

    # returns the number of points awarded
    def points(self):
        return self._points

    # gets the number of points for this test
    def max_points(self):
        return self._max_points

    # sets the console filehandle to be used
    def set_con(self, con):
        self._con = con

    # sets the test image to be used
    def set_test_image(self, image):
        self._test_image = image

    # obtains the test image that is being used
    def test_image(self):
        return self._test_image

    # gets the modules this test requires
    def get_modules(self) :
        return self._config["modules"]

    # whether or not the base image module should be included
    def include_base_modules(self):
        return self._config["base_modules"]

    # the test timeout
    def timeout(self):
        return self._config["timeout"]

    # the last expected strings
    def expected(self):
        return self._expected

    # whether or not the thest has failed
    def failed(self):
        return self._failed

    # whether the test had an error
    def error(self):
        return self._error

    def has_prepare(self):
        return "prepare" in self._config.keys()

    def prepare_user(self):
        if 'prepare_user' in self._config.keys():
            return self._config['prepare_user']
        else :
            return "aos"

    # whether the execution of the test is done
    def test_done(self):
        return self._current_test_step == len(self._config["teststeps"]) or self._failed or self._error

    # whether the test preparation is done.
    def prepare_done(self):
        return not self.has_prepare() or self._current_prepare_step == len(self._config["prepare"]) or self._failed or self._error

    # handles a wait setp
    def _handle_wait(self, step) :
        logverbose(f"waiting for { step['seconds'] } seconds")
        time.sleep(float(step['seconds']))

    # handles a reboot step
    def _handle_reboot(self, _step) :
        logverbose("rebooting the platform")
        logfail("don't know how to reboot yet...")
        self._con.expect(CFG_EXPECT_BOOT)

    # handles an input step
    def _handle_input(self, step) :
        logverbose(f"input '{step['value']}' to stdio")
        delay = step['delay'] if 'delay' in step else CFG_CONSOLE_TYPE_DELAY
        # write single characters at a time, with some delay
        if delay == 0:
            self._con.sendline(step["value"])
            return

        for c in step["value"]:
            self._con.send(c)
            self._con.send('\x04') # end of transmission to trigger the key to be sent.
                                   # this seems not to appear on the console.
            if delay != 0:
                time.sleep(delay)
            self._con.flush()
        self._con.flush()

    # handles an expect step
    def _handle_expect(self, step):

        rep         = step.get('repeat', 1)
        should_fail = step.get("should_fail", False)
        match_all   = step.get("match_all", False)

        logverbose(f"expecting '{step['pass']}' ({rep}x, {'all' if match_all else ''})")

        points = self._get_step_points(step)

        # construct the fail and passing regexes
        exp_pass = step.get("pass", [])

        if match_all and should_fail:
            exp_fail = step.get("fail", [])
        else :
            exp_fail = CFG_DEFAULT_FAIL + step.get("fail", [])

        # if the test should fail, then swap the regexes
        if should_fail:
            exp_pass, exp_fail = exp_fail, exp_pass

        matches = len(exp_pass) if match_all else 1

        for _ in range(rep):
            for _ in range(matches):
                self._expected = exp_pass
                idx = self._con.expect(exp_fail + exp_pass)
                if (idx >= len(exp_fail)):
                    idx = idx - len(exp_fail)
                    if points > 0 :
                        logpass(f"Test step pass: '{exp_pass[idx]}' recognized. ({points} points)")
                    elif 'points' not in step:
                        logpass(f"Test step pass: '{exp_pass[idx]}' recognized.")
                    else :
                        logverbose(f"Test step pass: '{exp_pass[idx]}' recognized.")
                    if match_all:
                        exp_pass = exp_pass[0:idx] + exp_pass[idx+1:]
                    else :
                        self._points += points
                else:
                    logfail(f"Test failed (expect fail): '{exp_fail[idx]}' encountered.")
                    self._consume_output(1)
                    self._failed = True
                    return
        if match_all:
            self._points += points

    # consomes some output from the console for a number of time
    def _consume_output(self, timeout):
        try :
            self._con.expect(pexpect.EOF, timeout=timeout)
        except:
            pass

#ifdef AOS_SOLUTION_M7
    # consumes all output until the end, or until timeout happens
    def _consume_output_no_log(self, timeout=0.4):
        fout = self._con.logfile
        self._con.logfile = None
        self._consume_output(timeout)
        self._con.logfile = fout

    # handles the sending of the file
    def _handle_sendfile(self, step) :
        src = self._testdir / step['src']
        if not src.is_file():
            logfail(f"sending file '{src}' to '{step['dst']}'")


        dst = step['dst']
        loginfo(f"sending file '{src}' to '{dst}'")

        # truncate the file
        self._con.sendline(f"echo \"\" > {dst}")
        # open vi
        self._con.sendline(f"vi {dst}\ni")

        with open(src, 'r') as f:
            for line in f.readlines():
                logverbose(f"sending line: {line}")
                self._con.sendline(re.sub(r'[^\x20-\x7e]+', '', line))
                self._consume_output_no_log()

        time.sleep(1)
        self._consume_output_no_log(1.5)

        self._con.send("\r")
        self._con.send(chr(27))
        self._con.send("\r")
        self._con.send(chr(27))
        self._con.send("\r")
        self._con.send(chr(27))
        self._con.send("\r")
        self._con.send(chr(27))
        self._con.send("\r")
        self._con.sendline(":wq")

        time.sleep(1)
        self._consume_output_no_log(1)
        self._consume_output_no_log()

        # print(f"verifying: {lastline}")
        # self._con.sendline(f"\rcat {dst}")
        # time.sleep(1)

    # handles the sending of the file
    def _handle_sendbinary(self, step) :
        pass

#endif

    # obtains the number of points for a test step
    def _get_step_points(self, step):
        return step['points'] if 'points' in step else 0.0

    # sets the start time of the test
    def start(self) :
        self._tstart = time.time()


    # triggers the next test step to be checked
    def next_test_step(self):
        if self._current_test_step >= len(self._config["teststeps"]) :
            return

        try :
            step = self._config["teststeps"][self._current_test_step]
            action = step['action']
            if action == "wait":
                self._handle_wait(step)
            elif action == "reboot":
                self._handle_reboot(step)
            elif action == "input":
                self._handle_input(step)
            elif action == "expect":
                self._handle_expect(step)
            else :
                logerr("unknown action '{}'".format(action))
                raise ValueError(f"Unknown test action {action} in step {self._current_test_step} of test {self._config['title']} ")

            self._current_test_step += 1

            if self._current_test_step >= len(self._config["teststeps"]):
                self._tend = time.time()

        except pexpect.TIMEOUT as e:
            logfail(f"Test timed out. Check the logfile for more details.")
            logfail(f"Test failed (timeout): {self.expected()} - points: {self.points()} / {self.max_points()} pts")
            self._failed = True
        except Exception as e:
            logerr(f"Test failed due to an exception: {e}")
            self._error = True
            self._failed = True

    # executes the next prepare step
    def next_prepare_step(self):

        if not self.has_prepare() or self._current_prepare_step >= len(self._config["prepare"]) :
            return

        try :
            step = self._config["prepare"][self._current_prepare_step]
            action = step['action']
            if action == "wait":
                self._handle_wait(step)
            elif action == "reboot":
                self._handle_reboot(step)
            elif action == "input":
                self._handle_input(step)
            elif action == "expect":
                self._handle_expect(step)
#ifdef AOS_SOLUTION_M7
            elif action == "sendfile":
                self._handle_sendfile(step)
            elif action == "sendbinary":
                self._handle_sendbinary(step)
#endif
            else :
                logerr("unknown action '{}'".format(action))
                raise ValueError(f"Unknown prepare action {action} in step {self._current_prepare_step} of test {self._config['title']} ")

            self._current_prepare_step += 1

        except pexpect.TIMEOUT as e:
            logfail(f"Test failed (timeout): {self.expected()} - points: {self.points()} / {self.max_points()} pts")
            self._error = True
            self._failed = True
        except Exception as e:
            logerr(f"Test failed due to an exception: {e}")
            self._error = True
            self._failed = True


#########################################################################################
# Test Runner
#########################################################################################


# Abstract Testrunner class
class AbstractTestRunner:
    def __init__(self, imgbuilder : AbstractImageBuilder, testdir: Path, tests : Sequence[str]):
        # the image builder to be used
        self._imgbuilder = imgbuilder
        # name of the image that will be built.
        self._imgname = "armv8_aos_autograder_img"
        # directory to be used for logging
        self._logdir = None
        # the directory where the tests are stored
        self._testdir = testdir
        # the test configurations
        self._configs = {}
        # console to the DUT
        self._con = None
        # the tests to be run, as dictionary of testname -> subtests
        self._tests = {}
        for t in tests :
            ts = t.split(":")
            maintest = ts[0]
            subtests = ts[1:]
            if maintest in self._tests:
                self._tests[maintest].extend(subtests)
            else :
                self._tests[maintest] = subtests

    # sets the loging directory
    def set_logdir(self, logdir : Path):
        self._logdir = logdir

    # parses a test description file and creates the Test objects
    def _parse_test_description(self, maintest: str, subtests: str):
            loginfo(f"parsing test decription `{maintest}`")
            testcfg = self._testdir / f"{maintest}.json"

            if not testcfg.exists():
                logwarn(f"Test configuration '{testcfg}' not found (continue)")
                return 0

            try :
                with open(testcfg) as json_data:
                    testdata = json.load(json_data)
            except Exception as e:
                logwarn(f"Error while loading test description for tests '{testcfg}' (continue)")
                logwarn(f"Error: {e}")
                return 0
            if subtests == [] or "all" in subtests:
                activatedtests = list(testdata["tests"])
            elif subtests == ['base'] :
                loginfo("selecting 'base' tests")
                activatedtests = [t for t in testdata["tests"] if testdata["tests"][t]["class"] == "base"]
            elif subtests == ['target'] :
                loginfo("selecting 'target' tests")
                activatedtests = [t for t in testdata["tests"] if testdata["tests"][t]["class"] == "target"]
            elif subtests == ['extra'] :
                loginfo("selecting 'extra' tests")
                activatedtests = [t for t in testdata["tests"] if testdata["tests"][t]["class"] == "extra"]
            else :
                activatedtests = []
                for t in subtests:
                    if t not in testdata["tests"]:
                        logwarn(f"Test '{t}' not found in test configuration '{testcfg}' (continue)")
                        continue
                    if t not in activatedtests:
                        activatedtests.append(t)

            logverbose(f"Selected tests:  {activatedtests}")

            # store it in the test configs
            self._configs[maintest] = {
                "title": testdata["title"],
                "tests" : [Test(self._testdir, maintest, c, testdata["tests"][c]) for c in activatedtests]
            }

            return len(activatedtests)


    # prepares the tests, creates directories, parses the test description files
    def prepare(self):
        logtitle("Preparing tests")
        if self._logdir != None:
            self._logdir.mkdir(parents=True, exist_ok=True)

        if len(self._tests) == 0:
            logwarn("No tests found")
            return

        for t in self._tests:
            logdir = self._logdir / t
            logdir.mkdir(parents=True, exist_ok=True)

        numtests = 0
        for (maintest, subtests) in self._tests.items():
            numtests += self._parse_test_description(maintest, subtests)

        if numtests > 0:
            logok(f"total of {numtests} tests selected.")
        else :
            logwarn("No tests selected")

    # run all tests loaded by the test runner
    def run_tests(self):
        for (_, testdesc) in self._configs.items():
            logtitle(f"Running Tests: '{testdesc['title']}'")
            if len(testdesc['tests']) == 0:
                logwarn("No tests selected")
                return
            results = {}
            for t in testdesc['tests'] :
                res = self._run_test(t)
                results[t.id()] = res

    # runs a single test
    def _run_test(self, test: Test):
        logsubtitle(f"Subtest {test.id()}: {test.name()}")

        logfile = self._logdir / test.milestone() / test.logfilename()
        try :
            fout = open(logfile, 'wb')
        except Exception as e:
            logwarn(f"Could not create log file '{logfile}'")
            fout = None

        self._imgbuilder.clear_test_modules()
        for m in test.get_modules():
            self._imgbuilder.add_test_module(m)

        try :
            self._imgbuilder.build_modules(test.include_base_modules())
            imgpath = self._build_test_image(test.include_base_modules())
            test.set_test_image(imgpath)
        except Exception as e:
            logfail("failed to build the OS image for the test.")
            raise e

        try :
            self._prepare_test(test, fout)
        except Exception as e:
            logfail("failed to perform relevant perparation steps for the test")
            test._error = True
            test.set_con(None)
            self._terminate_test()
            raise e

        if test.test_done():
            logpass(f"Test passed (nothing to be run): {test.points()} / {test.max_points()} pts")
            return

        try :
            self._start_test(test, fout)

            # set the console filehandle for the test
            test.set_con(self._con)

            # wait for the OS to boot
            self.wait_for_os_boot(CFG_EXPECT_BOOT)
            logok("Barrelfish has booted successfully.")

        except Exception as e:
            logerr("failure while starting the test.")
            test._error = True
            test.set_con(None)
            self._terminate_test()
            raise e

        # indicate that we are now starting the test
        test.start()

        # waiting a few seconds to get userspace up
        time.sleep(CFG_BOOT_TIMEOUT)

        while not test.test_done():
            test.next_test_step()


        test.set_con(None)
        self._terminate_test()
        if test.error() :
            logerr(f"Error while executing the test")
        elif not test.failed():
            logpass(f"Test passed: {test.points()} / {test.max_points()} pts")
        else :
            logfail(f"Test failed: {test.points()} / {test.max_points()} pts")

    # save and print the results
    def save_and_print_results(self) :

        for (maintest, testdesc) in self._configs.items():
            testresults = {
                "test": testdesc['title'],
                "total_points": 0.0,
                "total_success": 0,
                "total_failed": 0,
                "max_points": 0.0,
                "total_errors": 0,
                "subtests": {}
            }

            for t in testdesc['tests'] :
                if t.error() :
                    testresults["total_errors"] += 1
                else :
                    if t.failed():
                        testresults["total_failed"] += 1
                    else :
                        testresults["total_success"] += 1

                testresults["total_points"] += t.points()
                testresults["max_points"] += t.max_points()

                testresults["subtests"][t.id()] = {
                    "name": t.name(),
                    "class" : t.tclass(),
                    "points": t.points(),
                    "max_points": t.max_points(),
                    "duration" : t.duration(),
                }

            # where there any errors?
            if testresults['total_errors'] > 0:
                logwarn("There were {} tests with errors".format(testresults['total_errors']))

                # print the results
            subtests = testresults["total_success"] + \
                testresults["total_failed"] + testresults["total_errors"]
            logresult(f"{testdesc['title']}:  {testresults['total_points']} / {testresults['max_points']} points.    {subtests} subtests, {testresults['total_success']} pass, {testresults['total_failed']} fail,  {testresults['total_errors']} error")

            for r in testresults["subtests"]:
                res = testresults["subtests"][r]
                loginfo(f"{r:<{3}}  {res['class']:<{7}}  {res['name']:<{40}}  {res['points']:3.2f} / {res['max_points']:3.2f} points  {res['duration']: 7.2f} seconds")

            resfile = self._logdir / f"{maintest}.json"

            fout = open(resfile, 'w')
            json.dump(testresults, fout, indent=4)

    # wait for the OS to boot into the OS kernel
    def wait_for_os_boot(self, msg : str) :
        try:
            loginfo(f"Waiting for the OS to boot... ('{msg}')")
            self._con.expect(msg)
            self._con.setecho(False)
        except pexpect.TIMEOUT as e:
            logerr(f"Timeout: Did not see expected boot string '{msg}'")
            self._con.terminate(True)
            raise e
        except pexpect.EOF as e:
            logerr(f"Unexpected EOF: we did not have console, resource in use?")
            logoutput(self._con.before)
            self._con.terminate(True)
            raise Exception("Console has exited before we could boot.")
        except Exception as e:
            logerr("Exception happened while waiting for OS to boot")
            self._con.terminate(True)
            raise e

    # terminates the test, releases resources
    def _terminate_test(self):
        if self._con != None:
            self._con.flush()
            self._con.terminate(True)
            self._con = None

    # builds the test image
    def _build_test_image(self):
        pass

    # prepares the test (format SDCard, network setup, acquire resources)
    def _prepare_test(self, test: Test, fout):
        pass

    # starts the execution of the test
    def _start_test(self, test: Test, fout):
        pass



#########################################################################################
# TestRunner Implementations
#########################################################################################


# TestRunner class for running the tests on the Toradex imx8x colibri
class TestRunnerColibri(AbstractTestRunner):
    def __init__(self, boardctl : AbstractBoardController, imgbuilder : AbstractImageBuilder, testdir: Path,tests):
        super().__init__(imgbuilder, testdir, tests)
        self._boardctrl = boardctl
        self._imgname = f"armv8_imx8x_aos_autograder_{boardctl.board()}_img"

    def _prepare_login(self, test: Test) :
        usr = test.prepare_user().split(':')

        s = {
            "action" : "expect",
            "pass" : [ f"{self._boardctrl.board()} login:"],
        }
        test._handle_expect(s)

        self._con.sendline(f"{usr[0]}\n")
        self._con.flush()
        if len(usr) > 1:
            s = {
                "action" : "expect",
                "pass" : [ "Password:"],
            }
            test._handle_expect(s)
            self._con.sendline(f"{usr[1]}\n")

        s = {
            "action" : "expect",
            "pass" : [ f"{usr[0]}@colibri"],
            "fail" : [ ],
            "points" : 0
        }
        test._handle_expect(s)

        loginfo("Logged in, setting environment variables")
        self._con.sendline(f"export AOS_BOARD_ID=\"{self._boardctrl.board()}\"")
        self._con.flush()
        self._con.sendline(f"echo \"Board: $AOS_BOARD_ID\"\n")
        self._con.flush()
        s = {
            "action" : "expect",
            "pass" : [ f"Board: {self._boardctrl.board()}"],
            "fail" : [ ],
            "points" : 0
        }
        test._handle_expect(s)

    # prepares the test (format SDCard, network setup, acquire resources)
    def _prepare_test(self, test: Test, fout):
        if not test.has_prepare():
            loginfo(f"No test preparation required.")
            return

        loginfo(f"Preparing board for test...")
        self._boardctrl.board_reset()
        self._con = self._boardctrl.console(CFG_PREPARE_TIMEOUT)
        self._con.logfile = fout
        self._boardctrl.boot_linux()
        self.wait_for_os_boot(CFG_EXPECT_BOOT_LINUX)

        test.set_con(self._con)

        loginfo("Linux booted, logging in...")
        self._prepare_login(test)

        while not test.prepare_done():
            test.next_prepare_step()

        test.set_con(None)
        self._terminate_test()
        logok("board for test prepared.")

    # builds the test image
    def _build_test_image(self, base_modules : bool):
        return self._imgbuilder.build_imx8x_image(self._imgname , base_modules)

    # starts the execution of the test
    def _start_test(self, test: Test, fout):
        self._boardctrl.board_reset()
        self._con = self._boardctrl.console(test.timeout())
        self._con.logfile = fout
        self._boardctrl.boot_barrelfish(test.test_image())


# TestRunner class for running the tests inside qemu inside the docker container
class TestRunnerQemuDocker(AbstractTestRunner):
    def __init__(self, imgbuilder : AbstractImageBuilder, testdir: Path, tests):
        super().__init__(imgbuilder, testdir, tests)
        self._imgname = "armv8_qemu_aos_autograder_img"

    # builds the test image
    def _build_test_image(self, base_modules : bool):
        return self._imgbuilder.build_qemu_image(self._imgname, base_modules)

    # starts the execution of the test
    def _start_test(self, test: Test, fout):
        loginfo("Starting test in QEMU (docker)...")

        dockerpath = self._imgbuilder.sourcepath() / "tools" / "bfdocker.sh"
        scriptpath = "../tools/qemu-wrapper.sh"
        biospath = "../tools/hagfish/QEMU_EFI.fd"

        # ..//tools/qemu-wrapper.sh --image armv8_a57_qemu_image --arch armv8 --bios ..//tools/hagfish/QEMU_EFI.fd
        qemuargs = [
            str(dockerpath), "bash",
            str(scriptpath),
            "--arch", "armv8",
            "--bios",  str(biospath),
            "--image", str(test.test_image().relative_to(self._imgbuilder.buildpath())),
        ]
        try :
            with local.cwd(self._imgbuilder.buildpath()):
                logverbose("calling qemu with args: {}".format(qemuargs))
                con = pexpect.spawn("bash", qemuargs, timeout=test.timeout())
                con.logfile = fout
                time.sleep(5);
                if not con.isalive():
                    con.read()
                    con.close()
                    raise Exception("Qemu could not be started (already exited). Check log file for details.")
        except Exception as e:
            logerr(f"qemu execution failed: {str(e)}")
            raise e
        self._con = con


# TestRunner class for running the tests inside qemu
class TestRunnerQemu(AbstractTestRunner):
    def __init__(self, imgbuilder : AbstractImageBuilder, testdir: Path, tests):
        super().__init__(imgbuilder, testdir, tests)
        self._imgname = "armv8_qemu_aos_autograder_img"

    # builds the test image
    def _build_test_image(self, base_modules : bool):
        return self._imgbuilder.build_qemu_image(self._imgname, base_modules)

    # starts the execution of the test
    def _start_test(self, test: Test, fout):
        loginfo("Starting test in QEMU...")

        try :
            local.get('qemu-system-aarch64')
        except CommandNotFound:
            raise Exception("QEMU command (qemu-system-aarch64) not found. Please make sure QEMU is installed.")
        except Exception as e:
            raise e

        scriptpath = self._imgbuilder.sourcepath() / "tools" / "qemu-wrapper.sh"
        biospath = self._imgbuilder.sourcepath() / "tools" / "hagfish" / "QEMU_EFI.fd"

        # ..//tools/qemu-wrapper.sh --image armv8_a57_qemu_image --arch armv8 --bios ..//tools/hagfish/QEMU_EFI.fd
        qemuargs = [
            f"{scriptpath}",
            "--arch", "armv8",
            "--bios",  str(biospath),
            "--image", str(test.test_image()),
        ]
        try :
            with local.cwd(self._imgbuilder.buildpath()):
                logverbose("calling qemu with args: {}".format(qemuargs))
                con = pexpect.spawn("bash", qemuargs, timeout=test.timeout())
                con.logfile = fout
                time.sleep(5);
                if not con.isalive():
                    con.read()
                    con.close()
                    raise Exception("Qemu could not be started (already exited). Check log file for details.")

        except Exception as e:
            logerr(f"qemu execution failed: {str(e)}")
            raise e
        self._con = con
