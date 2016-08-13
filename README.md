YSR, a GNU Make front end / template

# About

YSR is a front end for GNU Make. It is meant for use for development in C/C++ without an IDE.

It imposes a structure to all programs using it, and offers the following services:

* an homogeneous handling of build compilation flags
* platform and cpu based constants
* creation of OSX application bundles
* higher level dependencies through modules
* low-level dependency
* no-recursive Make

Target platforms are:

* WIN32 (mingw32/gcc)
* MACOSX (clang/gcc)
* LINUX (gcc)

# Requirements
	GNU Bash
	GNU Make >3.80
	Ruby (on OSX)
	GNU Sed
	GNU Awk
	dirname, uname, sh, hostname, date, which, cp

# Installation

Just put bin/ into your path.

# Usage

When launched, the ysr command will attempt to run make on a file named Makefile.ysr, or if none exists, the default filenames used by GNU Make.

Binaries, dependency files and other products of the build are produced in their corresponding subdirectory inside build/. The build system is forbidden to create files outside of this directories.

Thus, to clean up a project, deleting build/ is sufficient.

Using the two functions `ysr-add-c-prog' and `ysr-add-c++-prog' in one Makefile will dynamically create rules in order to build a program. They will usually be named
      <program_name>-<rule_name>

The following commands

	ysr help
	ysr help-rules

Will list the defined programs and rules in a Makefile (as long as ysr-add-*-prog was used)

# Example project definitions

(You may find examples in the examples subdirectory)

Thus to define a small program, say helloworld, you would create a Makefile with:

helloworld/Makefile:

	include ysr.mk

	helloworld_OBJS:=$(DEST)/src/hello.o

	$(call ysr-add-c-prog,helloworld)

The system finds out which source file to compile by starting from the name of the objects (here $(DEST)/helloworld/hello.o) then
translating the path from $(DEST) (output tree) back to $(TOP) (source tree) in order to find out candidates:

* $(TOP)/src/hello.c
* $(TOP)/src/hello.cc — (c++)
* $(TOP)/src/hello.m  — (objective c)
* ... — etc

The program's executable will be compiled to

* $(DEST)/helloworld/helloworld.elf
* or $(DEST)/helloworld/helloworld.app on OSX

# Per host configuration

It is often useful to define host-specific configuration and for this purpose, ysr loads a per project configuration file before reading your makefile.

The file path obey the following pattern:

  $(TOP)/ysr/`hostname`-config.mk

That is, if your machine is named "edge" then the configuration file can be created as:

  ysr/edge-config.mk

The file must be consistent with the name returned by the hostname command.

# Some information about its operation

ysr recurses until the top of the source tree is found. This top of
the tree can be set by adding a project.ysr file.

ysr then:
- finds out the architecture (OS, type of CPU) of the host system.
- imports the rule set for the GCC compiler
- imports some utilitary functions
- imports templates for program and libraries
- defines variable TOP, the top of the source tree
- defines variable DEST, the top of the output tree
- defines and uses variable OTYPE (shorthand for object type) which can be DEBUG, TEST or RELEASE.

Any built program will end up under the $(DEST) directory. This
directory is different for each object type, in order not to mix
release binaries and debugging binaries
