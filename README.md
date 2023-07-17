# AOS Code Repository

Welcome to AOS. This is the code handout repository.

The code in this repository is a simplified version of the [Barrelfish OS](barrelfish.org).

## License

**By cloning this repository, you agree to not publish or distribute the code in any form.**
**Only use the official course-provided git repositories for course work and submission.**

see the LICENSE file.

## Pulling Code in your Repository

Make sure you are on the right branch and add the code handout repository as a new git remote.
We assume the git url is stored in the `$CODEHANDOUT` variable.
Then we can pull the changes from the handout remote.

```
$ git checkout main
$ git remote add handout $CODEHANDOUT
$ git pull handout main
```

You can use `git remote -v` to list the remotes.

## Dependencies

Before you can start, make sure you have installed the following dependencies:

```
# apt-get install build-essential pkg-config bison flex ghc libghc-src-exts-dev \
                libghc-ghc-paths-dev libghc-parsec3-dev libghc-random-dev \
                libghc-ghc-mtl-dev libghc-async-dev  cabal-install libelf-dev \
                git gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# cabal v1-update
# cabal v1-install bytestring-trie
```

**Booting in Qemu**

```
# apt-get install python3 parted mtools qemu-efi-aarch64 qemu-system-arm qemu-utils python3
```

**Debugging in Qemu**
```
# apt-get install gdb gdb-multiarch
```

**Booting on the Toradex imx8x**

```
# apt-get install picocom wget python3

# wget -P $HOME/bin https://github.com/NXPmicro/mfgtools/releases/download/uuu_1.4.165/uuu
# chmod 755 $HOME/bin/uuu
```

**Autograder**

```
# apt-get install python3 python3-pexpect python3-plumbum
```

## Docker

Use the following command to obtain and start a Docker container with all dependencies.

```
./tools/bfdocker.sh
```

## Building

To build Barrelfish, create a build directory, and execute Hake to generate the Makefile.
Note, have your build directory outside of the source tree, or name it `build`.

```
mkdir build
cd build
../hake/hake.sh -s ../ -a armv8
```

Then you can use `make` to build Barrelfish. To obtain an overview of all targets execute
```
make help
```

Likewise, for all platforms that can be build

```
make help-platforms
```

and finally the boot targets with

```
make help-boot
```

## Installing

If you want to install your image to a different machine in case you have remote accessible
boards, you can use the `make install_imx8x` target. The install destination is controller  by
the `INSTALL_PREFIX` environment variable.

```
INSTALL_PREFIX=host.tld:/path make install_imx8x
```
