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

import pathlib
import shutil
import multiprocessing

from collections.abc import Sequence
from plumbum import local
from plumbum.commands.processes import ProcessExecutionError
from plumbum.cmd import bash

from logger import *
from config import CFG_TOOLS_MODULES

#########################################################################################
# Image Builder
#########################################################################################

# The image builder assembles the test image from a set of modules. It supports building
# natively and in a docker image on the local machine.


# Abstract Image Builder with receipts for building the image
class AbstractImageBuilder :
    # Constructor
    def __init__(self, sourcepath : pathlib.Path, menulst : pathlib.Path, buildpath : pathlib.Path = pathlib.Path('build')):

        if not menulst.exists():
            raise ValueError(f"Menulst file '{menulst}' does not exist. Please provide a valid menulst file.")

        if not sourcepath.exists():
            raise ValueError(f"Source path '{sourcepath}' does not exist.")

        loginfo(f" - Menulst: {menulst}")
        loginfo(f" - Source directory: {sourcepath}")
        loginfo(f" - Build directory: {buildpath}")

        # the path to the menulst file for the base image
        self._menulst = menulst
        # the path to the source tree
        self._sourcepath = sourcepath
        # the path to the build directory
        self._buildpath = buildpath
        # the contents of the menulst
        self._menulst_content = []
        # the bootdriver module
        self._bootdriver = None
        # the cpudriver module
        self._cpudriver = None
        # list of modules in the menulst
        self._modules = []
        # list of modules required for the test
        self._test_modules = []


    # obtains the buildpath of the image builder
    def buildpath(self) :
        return self._buildpath

    # obtains the source path of the image builder
    def sourcepath(self) :
        return self._sourcepath

    # adds a new module to the list of test modules
    def add_test_module(self, module : Sequence[str]):
        self._test_modules.append(module)

    # clears the list of test modules
    def clear_test_modules(self):
        self._test_modules.clear()

    # obtains the list of modules from the base image (without the cmdline arguments)
    def get_modules_list(self):
        return list(map(lambda x: x[0], self._modules))

    # obtains the list of modules from the test (without the cmdline arguments)
    def get_test_modules_list(self):
        return list(map(lambda x: x[0], self._test_modules))

    # prepares the build environemnt for buildig the image, parses the menulst file
    def prepare(self):
        loginfo(f"Setting up build path")
        if not self._buildpath.exists():
            loginfo(f" - Creating build path '{self._buildpath}'")
            self._buildpath.mkdir(parents=True, exist_ok=True)
        else :
            loginfo(f" - Reusing existing build path '{self._buildpath}'")

        self._parse_menu_lst()

    # runs hake to generate the Makefile
    def hake(self, force : bool = False):
        loginfo("Hake: generating Makefile")
        makefile = self._buildpath / "Makefile"
        if makefile.exists() and not force:
            loginfo(" - Hake completed (skipped -- use -f to force running hake)")
            return

        loginfo(" - Running hake...")
        self._run_bash(self._hake_cmd())
        loginfo(" - Hake completed. Makefile generated")

    # builds the tools required to build the image
    def build_tools(self):
        loginfo(f"Building build tools...")
        self._run_make(CFG_TOOLS_MODULES)

    # builds the base image (all modules from the menu.lst file)
    def build_base_image(self):
        loginfo(f"Build: building base image...")
        mods = [self._bootdriver[0], self._cpudriver[0]] + self.get_modules_list()
        self._run_make(mods)

    # builds the modules required for the test
    def build_modules(self, include_base_modules : bool):
        loginfo(f"Building modules...")
        mods = [self._bootdriver[0], self._cpudriver[0]]
        if include_base_modules :
            mods +=  self.get_modules_list()
        mods += self.get_test_modules_list()
        self._run_make(mods)

    # builds the qemu image, returns a path to the built image
    def build_qemu_image(self, imgname : str, include_base_modules : bool):
        loginfo(f"Building qemu image...")

        # write the menu.lst file for the test
        menulst = self._write_menu_lst(f"menu.lst.armv8_aos_autograder", include_base_modules)

        # arguments relative to the build directory
        args = [ self._harness_efiimg_script(), str(menulst), ".", str(imgname) ]
        self._run_python(args)
        logok(f"Qemu image generated in {self._buildpath / imgname}")
        return self._buildpath / imgname

    # builds the imx8x image, returns a path to the image
    def build_imx8x_image(self, imgname, include_base_modules):
        loginfo(f"Building imx8x image...")
        menulst = self._write_menu_lst(f"menu.lst.armv8_aos_autograder", include_base_modules)

        imgobj = f"{imgname}.o"
        imgblob = f"{imgname}.blob"

        # ./tools/bin/armv8_bootimage platforms/arm/menu.lst.armv8_imx8x armv8_imx8x_blob .
        cmd = "./tools/bin/armv8_bootimage"
        args = [ str(menulst), imgblob, "."]
        self._run_cmd(cmd, args)

        # run object copy
        args = [ "-I", "binary", "-O", "elf64-littleaarch64", "-B", "aarch64",
           "--redefine-sym", f"_binary_{imgname}_blob_start=barrelfish_blob_start",
           "--redefine-sym", f"_binary_{imgname}_blob_end=barrelfish_blob_end",
           "--redefine-sym", f"_binary_{imgname}_blob_size=barrelfish_blob_size",
            imgblob, f"{imgblob}.o"
        ]
        self._run_objcopy(args)

        # the ependencies for building the image, copy to build directory
        deps = [
            "tools/armv8_bootimage/crt0-efi-aarch64.o",
            "tools/armv8_bootimage/elf_aarch64_efi.lds",
            "tools/armv8_bootimage/libgnuefi.a",
            "tools/armv8_bootimage/libefi.a"
        ]

        # copy the dst files into the build directory, so we don't need to worry about paths
        for dep in deps :
            dst = self._buildpath / dep
            if dst.is_file():
                continue
            src = self._sourcepath / dep
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, dst)

        # creating the efiloader
        self._run_make(["./armv8/tools/armv8_bootimage/efi_loader.o"])

        # run linker
        args = [
            "tools/armv8_bootimage/crt0-efi-aarch64.o",
            "-znocombreloc", "-Bsymbolic", "-T", "tools/armv8_bootimage/elf_aarch64_efi.lds",
            "-shared", "--no-undefined", "--defsym=EFI_SUBSYSTEM=10", f"{imgblob}.o",
            "./armv8/tools/armv8_bootimage/efi_loader.o", "-o", imgobj,
            "tools/armv8_bootimage/libgnuefi.a", "tools/armv8_bootimage/libefi.a"
        ]
        self._run_ld(args)

        # run object copy again to create the binary image
        self._run_objcopy(["-O", "binary", imgobj, imgname])

        logok(f"imx8x image generated in {self._buildpath / imgname}")
        return self._buildpath / imgname

    # parses the menulst file
    def _parse_menu_lst(self):
        loginfo(f"Parsing menulst file: '{self._menulst}'")

        try:
            f = open(self._menulst, 'r')
        except Exception as e:
            logerr(f"File '{self._menulst}' could not be opened for reading.")
            raise e

        # parse the lines!
        for l in f.readlines():
            # store the original
            self._menulst_content.append(l)

            # strip the white space
            l = l.strip()

            # skip empty lines, discard comments
            if l == "" or l.startswith("#"):
                continue

            logverbose(f"  - '{l}'")

            # split the line into words
            words = l.split(" ")

            # extract the cmdline
            cmdline = []
            for w in words[1:]:
                if w.startswith("#"):
                    break
                wstripped = w.strip()
                if wstripped != "":
                    cmdline.append(wstripped)

            # remove leading /
            if cmdline[0].startswith('/'):
                cmdline[0] = cmdline[0][1:]

            if words[0] == "module":
                self._modules.append(cmdline)
            elif words[0] == "bootdriver":
                if self._bootdriver != None:
                    logwarn(" - bootdriver already set! (menulst boguous?)")
                else:
                    self._bootdriver = cmdline
            elif words[0] == "cpudriver":
                if self._cpudriver != None:
                    logwarn(" - cpudriver already set! (menulst boguous?)")
                else:
                    self._cpudriver = cmdline
            else:
                logwarn(f" - Unrecognized line '{l}' in menu.lst")

        f.close()
        loginfo(f" - Parsed menu.lst file, found total {2 + len(self._modules)} modules.")

    # writes the menulst file in the build directory
    def _write_menu_lst(self, name : str, include_base_modules : bool):
        menulstpath = self._buildpath / "platforms" / "arm"
        menulstpath.mkdir(parents=True, exist_ok=True)
        path = menulstpath / name
        if path == self._menulst:
            raise ValueError("Cannot write menu.lst to the same file as the original.")

        logverbose(f" - Writing menu.lst to '{path}'")
        try:
            f = open(path, 'w')
        except Exception as e:
            logerr(f"File '{path}' could not be opened.")
            raise(e)

        f.write("# this file is autogenerated by the autograder\n")
        f.write(f"bootdriver /{' '.join(self._bootdriver)}\n")
        f.write(f"cpudriver /{' '.join(self._cpudriver)}\n")

        # when a module is specified in the test, then don't include the base module,
        # as we may need to pass the right arguments
        test_modules_map = {}
        for m in self._test_modules:
            test_modules_map[m[0]] = True

        if include_base_modules:
            for m in self._modules:
                if m[0] in test_modules_map:
                    continue
                modname = ' '.join(m)
                logverbose(f"    + {modname}")
                f.write(f"module /{modname}\n")

        for m in self._test_modules:
            modname = ' '.join(m)
            logverbose(f"    + {modname}")
            f.write(f"module /{modname}\n")

        f.flush()
        f.close()

        # return the path relative to the build directory
        return path.relative_to(self._buildpath)

    # runs the make command
    def _run_make(self, modules : Sequence[str]):
        nproc = multiprocessing.cpu_count()
        self._run_cmd("make", ["-j", str(nproc)] + modules)

    # runs a bash script
    def _run_bash(self, args: Sequence[str]):
        self._run_cmd("bash", args)

    def _triple(self):
        from sys import platform
        if platform == 'darwin':
            return 'aarch64-none-elf'
        else:
            return 'aarch64-linux-gnu'

    # runs objcopy
    def _run_objcopy(self, args: Sequence[str]):
        self._run_cmd(f"{self._triple()}-objcopy", args)

    # runs ld
    def _run_ld(self, args: Sequence[str]):
        self._run_cmd(f"{self._triple()}-ld", args)

    # runs a python script
    def _run_python(self, args: Sequence[str]):
        self._run_cmd("python3", args)

    # prints the command
    def _print_cmd(self, cmd : str, args : Sequence[str]):
        logverbose(f"  - {cmd} {' '.join(args)}")

    # obtainst he efi script path
    def _harness_efiimg_script(self):
        pass

    # returns the hake command
    def _hake_cmd(self) :
        pass

    # executes a command
    def _run_cmd(self, cmd : str, args: Sequence[str]):
        pass

# Image builder for creating test image int a Docker container on the local machine
class ImageBuilderDocker(AbstractImageBuilder):
    def __init__(self, sourcepath : pathlib.Path, menulst : pathlib.Path):
        loginfo("Setting up image builder (docker container)")
        super().__init__(sourcepath, menulst, pathlib.Path("build"))

    # obtainst he efi script path
    def _harness_efiimg_script(self):
        # in the docker container, where the parent's build dir is always on top
        return "../tools/harness/efiimage.py"

    # returns the hake command
    def _hake_cmd(self) :
        return ["../hake/hake.sh", "-s", "../"]

     # executes a command
    def _run_cmd(self, cmd : str, args: Sequence[str]):
        self._print_cmd(str(cmd), args)
        dockerscript = self._sourcepath / "tools" / "bfdocker.sh"
        args = [str(dockerscript)] + ["--no-interactive"] + [cmd] + args
        try :
            logverbose("Executing: bash " + " ".join(args))
            res = bash(*args)
        except ProcessExecutionError as e:
            logerr("Command execution failes")
            logoutput(e.stderr)
            raise e
        except Exception as e:
            logerr("Unknown exception while running command.")
            raise e
        return res


# Image builder for creating test image natively on the local machine
class ImageBuilderNative(AbstractImageBuilder):
    def __init__(self, sourcepath : pathlib.Path, menulst : pathlib.Path, buildpath : pathlib.Path):
        loginfo("Setting up image builder (native)")
        super().__init__(sourcepath, menulst, buildpath)

    # obtainst he efi script path
    def _harness_efiimg_script(self):
        return str(self._sourcepath / "tools" / "harness" / "efiimage.py")

    # returns the hake command
    def _hake_cmd(self) :
        return [str(self._sourcepath / "hake/hake.sh"), "-s", str(self._sourcepath)]

    # returns the hake command
    def _run_cmd(self, cmd : str, args: Sequence[str]):
        self._print_cmd(str(cmd), args)
        try :
            with local.cwd(self._buildpath) :
                l_cmd = local[cmd]
                logverbose(f"Executing: {str(cmd)} " + " ".join(args))
                res = l_cmd(*args)
        except ProcessExecutionError as e:
            logerr("Command execution failes")
            logoutput(e.stderr)
            raise e
        except Exception as e:
            logerr("Unknown exception while running command.")
            raise e
        return res
