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

import os
import time
import pathlib
import pexpect

from collections.abc import Sequence
from plumbum import SshMachine, local
from plumbum.commands.processes import ProcessExecutionError


from logger import *



#########################################################################################
# Board Control Classes
#########################################################################################

# The board control classes are responsible for obtaining a console to the board, perform
# resets of the hardware, and boot the board to either Linux or Barrelfish. The board
# controllers are site specific, and support locally connected board and remote board
# control.

# an array of possible site configurations
CFG_SITE_CONFIGS = []


# Abstract board controller base class defining the interface to the board controller.
class AbstractBoardController:
    # constructor, sets the board name
    def __init__(self, board : str):
        # the name of the board this board controller manages
        self._board = board

    # obtains the board name
    def board(self) -> str:
        return self._board

    # obtains a console to the board
    # returns a pexpect object, or None on failure
    def console(self, timeout : int = 60):
        pass

    # turns the board on
    def board_on(self):
        pass

    # turns the board off
    def board_off(self):
        pass

    # resets the board, or performs a power cycle
    def board_reset(self):
        pass

    # boots the supplied barrelfish image on the board
    def boot_barrelfish(self, image : pathlib.Path):
        pass

    # boots into the locally installed Linux
    def boot_linux(self):
        pass


# Abstract remote board controller base class
class AbstractRemoteBoardController(AbstractBoardController):
    # constructor for the remote configuration
    def __init__(self, board : str, host : str, port : int = 22, user : str = None, remotepath : pathlib.Path = None):
        super().__init__(board)
        loginfo(f"setting up remote board control for board {board} on host {host}:{port}")
        # the autograder user
        self._user = user
        # the host name of the remote machine
        self._host = host
        # the port to be used for ssh connections, stored as a string
        self._port = str(port)
        # the remote path to be used
        self._remotepath = remotepath
        # path to the image on the local machine
        self._localimage = None
        # path to the image on the remote machine
        self._remoteimg = None
        # initialize the remote machine connection
        try :
            self._remote = None # make sure the member is there
            self._remote = SshMachine(self._host, port=self._port, user=self._user)
        except Exception as e:
            logerr("could not connect to remote host.")
            raise e


    # destructor, closes the connection to the remote machine
    def __del__(self):
        if not self._remote == None:
            self._remote.close()
            self._remote = None

    # returns the command for obtaining the console to the board as a list of strings
    # needs to be implemented by the subclass
    def _console_cmd(self) :
        pass

    # returns the command for booting barrelfish on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_barrelfish_cmd(self, img : pathlib.Path) :
        pass

    # returns the command for booting Linux on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_linux_cmd(self) :
        pass

    # returns the command for resetting the board as a list of strings
    # needs to be implemented by the subclass,
    # or _board_on_cmd() and _board_off_cmd() must be implemented
    def _board_reset_cmd(self) :
        pass

    # returns the command for turning on the board as a list of strings
    def _board_on_cmd(self) :
        pass

    # returns the command for turning off the board as a list of strings
    def _board_off_cmd(self) :
        pass

    # executes a command on the remote machine
    # returns the output of the command
    def _exec_remote_command(self, cmdline : Sequence[str]):
        # don't try to execute if the command is None
        if cmdline == None or len(cmdline) == 0:
            return None

        cmd = cmdline[0]
        args = cmdline[1:]

        cmdstr = ' '.join(cmdline)
        logverbose(f"Executing remote command: {cmdstr} on host {self._host}")

        # construct the command to be executed
        r_cmd = self._remote[cmd]
        try :
            return r_cmd(*args)
        except ProcessExecutionError as e:
            logerr(f"failure while executing the remote command: {e}")
            raise e
        except Exception as e:
            logerr(f"unknown failure while executing the remote command")
            raise e

    # installs the image so it can be booted
    def _install_image(self, image : pathlib.Path):
        dst = f"{self._host}:{str(self._remotepath)}" if self._remotepath != None else self._host
        loginfo(f"Installing image {str(image)} to {dst}...")
        self._localimage = image

        # prepend the remote path if it is set for installing it, store teh remote img
        if self._remotepath == None:
            self._remoteimg = image.name
        else :
            self._remoteimg = self._remotepath / image.name

        try :
            # create the remote directory if it doesn't exist
            if self._remotepath != None:
                self._exec_remote_command(["mkdir", "-p", str(self._remotepath)])
            self._remote.upload(self._localimage, self._remoteimg)
        except Exception as e:
            logerr(f"unknown failure when copying the file to the remote machine")
            raise e

    # obtains a console to the board
    # returns a pexpect object, or None on failure
    def console(self, timeout : int = 60):
        loginfo(f"Obtaining console for board {self._host}:{self._board}...")
        cmd = self._console_cmd()
        if cmd == None:
            raise Exception("No console command for this configuration.")

        sshargs = [ "-p", self._port ]
        if self._user == None:
            sshargs.append(self._host)
        else :
            sshargs.append(f"{self._user}@{self._host}")
        sshargs = sshargs + cmd
        logverbose(f"Executing remote command: ssh {' '.join(sshargs)}")
        try:
            con = pexpect.spawn("ssh", sshargs, timeout=timeout + 1)
        except pexpect.EOF as e:
            raise Exception("Console has exited, so we don't have a console.")
        except pexpect.TIMEOUT as e:
            raise Exception("Timeout happened already, so we don't have a working console.")
        except Exception as e:
            logerr(f"Could not spawn the console. Unknown exception: {str(e)}")
            raise e

        # wait two seconds for the console to start up
        time.sleep(2)
        if not con.isalive():
            logerr(f"Could not spawn the console. Is the serial available?")
            msg = con.read()
            logoutput(msg.decode())
            con.terminate(True)
            raise Exception("Could not spawn the console. Is the resource busy?")

        return con

    # boots barrelfish on the board
    def boot_barrelfish(self, image : pathlib.Path):
        self._install_image(image)
        loginfo(f"Booting Barrelfish on board {self._host}:{self._board} with image {str(image)}...")
        cmd = self._bootcmd_barrelfish_cmd(self._remoteimg)
        if cmd == None:
            raise Exception("No Barrelfish boot command for this configuration.")
        self._exec_remote_command(cmd)

    # boots into the locally installed Linux
    def boot_linux(self):
        loginfo(f"Booting Linux on board {self._host}:{self._board}...")
        cmd = self._bootcmd_linux_cmd()
        if cmd == None:
            raise Exception("No Linux boot command for this configuration.")
        self._exec_remote_command(cmd)

    # resets the board (power cycle)
    def board_reset(self):
        loginfo(f"Resetting board {self._host}:{self._board}...")
        cmd = self._board_reset_cmd()
        if cmd == None:
            off_cmd = self._board_off_cmd()
            on_cmd = self._board_on_cmd()
            if off_cmd == None or on_cmd == None:
                raise Exception("No reset command for this configuration.")
            if not self._exec_remote_command(off_cmd):
                raise Exception("Resetting the board has failed")
            self._exec_remote_command(on_cmd)
            return
        self._exec_remote_command(self._board_reset_cmd())

    # turns the board on
    def board_on(self):
        loginfo(f"Turning on board {self._host}:{self._board}...")
        cmd = self._board_on_cmd()
        if cmd == None:
            logverbose("No command for turning the board on for this configuration. Continuing.")
            return True
        return self._exec_remote_command(cmd)

    # turns the board off
    def board_off(self):
        loginfo(f"Turning off board {self._host}:{self._board}...")
        cmd = self._board_off_cmd()
        if cmd == None:
            logverbose("No command for turning the board off for this configuration. Continuing.")
            return True
        return self._exec_remote_command(cmd)


# Abstractlocal board controller base class
class AbstractLocalBoardController(AbstractBoardController):
    def __init__(self, board = None, serial = '/dev/ttyUSB0'):
        super().__init__(board)
        self._serial = serial
        loginfo(f"setting up local board control for board {board}")

    def _exec_local_command(self, cmdline : Sequence[str]):
        if cmdline == None or len(cmdline) == 0:
            return True

        cmd = cmdline[0]
        args = cmdline[1:]

        try :
            l_cmd = local[cmd]
            logverbose(f"Executing: {str(cmd)} " + " ".join(args))
            res = l_cmd(*args)
        except ProcessExecutionError as e:
            logerr(f"failure while executing the local command: {e}")
            raise e
        except Exception as e:
            logerr(f"unknown failure while executing the local command")
            raise e
        return res

    # returns the command for obtaining the console to the board as a list of strings
    # needs to be implemented by the subclass
    def _console_cmd(self) :
        pass

    # returns the command for booting barrelfish on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_barrelfish_cmd(self, img : pathlib.Path) :
        pass

    # returns the command for booting Linux on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_linux_cmd(self) :
        pass

    # obtains a console to the board
    def console(self, timeout : int = 60):
        loginfo(f"Obtaining console for local board {self._board}...")
        cmdline = self._console_cmd()
        if cmdline == None:
            logerr("No console command for this configuration.")
            return None
        cmd = cmdline[0]
        args = cmdline[1:]
        logverbose(f"Executing local command: {' '.join(cmdline)}")
        try:
            con = pexpect.spawn(cmd, args, timeout=timeout)
            if not con.isalive():
                logerr("Could not spawn the console. (not alive)")
                return None
        except pexpect.EOF as e:
            raise Exception("Console has exited, so we don't have a console.")
        except pexpect.TIMEOUT as e:
            raise Exception("Timeout happened already, so we don't have a working console.")
        except Exception as e:
            logerr(f"Could not spawn the console. Unknown exception: {str(e)}")
            raise e
        return con

    # Tries to resets the board (power cycle) through manually pressing the button
    def board_reset(self):
        _ = input("-----> (reset the board using the physical button, then press enter)")

    # boots barrelfish on the board
    def boot_barrelfish(self, image : pathlib.Path):
        loginfo(f"Booting Barrelfish on local board {self._board} with image {str(image)}...")
        cmd = self._bootcmd_barrelfish_cmd(image)
        if cmd == None:
            raise Exception("No Barrelfish boot command for this configuration.")
        self._exec_local_command(cmd)

    # boots into the locally installed Linux
    def boot_linux(self):
        loginfo(f"Booting Linux on local board {self._board}...")
        cmd = self._bootcmd_linux_cmd()
        if cmd == None:
            raise Exception("No Linux boot command for this configuration.")
        self._exec_remote_command(cmd)


#########################################################################################
# Local Board Control (Single Board Connected)
#########################################################################################



CFG_SITE_CONFIGS += ["local", "local-auto"]

# Board Control for local board access
class BoardCtrlLocalDefault(AbstractLocalBoardController):
    def __init__(self, bfroot, board=None, serial='/dev/ttyUSB0'):
        super().__init__(board, serial)
        self._bfroot = pathlib.Path(bfroot)

    # returns the command for obtaining the console to the board as a list of strings
    # needs to be implemented by the subclass
    def _console_cmd(self) :
        return ["picocom", "-b", "115200",  "-f", "n", self._serial]

    # returns the command for booting barrelfish on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_barrelfish_cmd(self, img : pathlib.Path) :
        scriptpath = self._bfroot / "tools" / "imx8x" / "bf-boot.sh"
        cmd = [str(scriptpath), "--no-reset", "--bf", str(img)]
        if self._board != None :
            logwarn(f"Board {self._board} specified. Multiple local boards are not supported, expect badness.")
        return cmd

    # returns the command for booting Linux on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_linux_cmd(self) :
        scriptpath = self._bfroot / "tools" / "imx8x" /" local-boot.sh"
        cmd = [scriptpath, "--no-reset"]
        if self._board != None :
            logwarn(f"Board {self._board} specified. Multiple local boards are not supported, expect badness.")
        return cmd


# Board Control for local board access with USB reset
class BoardCtrlLocalAutoReset(BoardCtrlLocalDefault):
    def board_reset(self) :
        scriptpath = self._bfroot / "tools" / "imx8x" / "board_ctrl.py"
        self._exec_local_command([str(scriptpath), "reset"])


#########################################################################################
# ETH Zurich Specific Configurations
#########################################################################################



CFG_SITE_CONFIGS += ["ethz-remote", "ethz-local"]

# Board controller for remotely accessing boards attached to emmentaler
class BoardCtrlRemoteETHZ(AbstractRemoteBoardController):
    def __init__(self, board):
        # the possible boards are colibri{3..5}
        boards = [f"colibri{i}" for i in range(3, 6)]
        self._remote = None
        if board == None :
            raise ValueError(f"No board specified! Please specify a board from {boards}")
        if not board in boards :
            raise ValueError(f"Unknown board identifier: '{board}', available boards: {boards}")

        super().__init__(board, "emmentaler.ethz.ch", port = 8006,
            remotepath = pathlib.PurePosixPath("/mnt") / "netos" / "tftpboot" / os.getlogin()
        )

    # returns the command for obtaining the console to the board as a list of strings
    # needs to be implemented by the subclass
    def _console_cmd(self) :
        return ["console", "-f", self._board]

    # returns the command for booting barrelfish on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_barrelfish_cmd(self, img : pathlib.Path) :
        return ["bash", "/mnt/netos/tools/bin/rackboot.sh", "-b", self._board]

    # returns the command for booting Linux on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_linux_cmd(self) :
        return ["bash", "/mnt/netos/tools/bin/rackboot.sh", "-l", self._board]

    # returns the command for resetting the board as a list of strings
    # needs to be implemented by the subclass,
    # or _board_on_cmd() and _board_off_cmd() must be implemented
    def _board_reset_cmd(self) :
        return ["rackpower", "-r", self._board]

    # returns the command for turning on the board as a list of strings
    def _board_on_cmd(self) :
        return None

    # returns the command for turning off the board as a list of strings
    def _board_off_cmd(self) :
        return None


# Board Control for local board access at ETHZ (on emmentaler)
class BoardCtrlLocalETHZ(AbstractLocalBoardController):
    def __init__(self, board):
        boards = [f"colibri{i}" for i in range(3, 6)]
        if not board in boards :
            logerr(f"Invalid board {board}. Valid boards are {boards}")
            raise ValueError(f"Invalid board {board}")
        super().__init__(self, board, "console")

    # returns the command for obtaining the console to the board as a list of strings
    # needs to be implemented by the subclass
    def _console_cmd(self) :
        return ["console", "-f", self._board]

    # returns the command for booting barrelfish on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_barrelfish_cmd(self, img : pathlib.Path) :
        return ["bash", "/mnt/netos/tools/bin/rackboot.sh", "-b", self._board, str(img)]

    # returns the command for booting Linux on the  board as a list of strings
    # needs to be implemented by the subclass
    def _bootcmd_linux_cmd(self) :
        return ["bash", "/mnt/netos/tools/bin/rackboot.sh", "-h", self._board]

    # resets the board (power cycle)
    def board_reset(self) :
        self._exec_local_command(["rackpower", "-r", self._board])



#########################################################################################
# UBC Specific Board Control Classes
#########################################################################################



CFG_SITE_CONFIGS += ["ubc"]

# Board Control for remote board access for UBC
class BoardCtrlRemoteUBC(AbstractRemoteBoardController):
    def __init__(self, board):
        boards = [f"colibri{i}" for i in range(1, 21)]
        self._remote = None
        if board == None :
            raise ValueError(f"No board specified! Please specify a board from {boards}")
        if not board in boards :
            raise ValueError(f"Unknown board identifier: '{board}', available boards: {boards}")
        try :
            super().__init__(board, "toradex.students.cs.ubc.ca", port = 22)
        except Exception as e:
            logerr("remote init failed. are you in the CS network?")
            raise e

    def _console_cmd(self) :
        return ["bash", "/projects/cs-436a/console.sh", self._board]

    def _bootcmd_barrelfish_cmd(self, img) :
        return ["bash", "/projects/cs-436a/boot-barrelfish.sh", self._board, str(img)]

    def _bootcmd_linux_cmd(self) :
        return ["bash", "/projects/cs-436a/boot-linux.sh", self._board]

    def _board_reset_cmd(self) :
        return ["bash", "/projects/cs-436a/rackpower.sh", "reset", self._board]

    def _board_on_cmd(self) :
        return ["bash", "/projects/cs-436a/rackpower.sh", "on", self._board]

    def _board_off_cmd(self) :
        return ["bash", "/projects/cs-436a/rackpower.sh", "off", self._board]
