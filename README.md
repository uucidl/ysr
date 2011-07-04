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

* WIN32 (mingw32)
* MACOSX 
* LINUX

# Requirements
	GNU Bash
	GNU Make >3.80
	Ruby (on OSX)	

# Installation

Just put bin/ into your path.

# Usage

Binaries, dependency files and other products of the build are produced in their corresponding subdirectory inside build/. The build system is forbidden to create files outside of this directories.

Thus, to clean up a project, deleting build/ is sufficient.

Using the two function mk-c-prog-rule and mk-c++-prog-rule functions in one Makefile will dynamically create rules in order to build a program. They will usually be named
      <program_name>-<rule_name>

The following commands

	ysr help
	ysr help-rules

Will list the defined programs and rules in a Makefile (as long as mk-?-prog-rule was used)

Thus to define a small program, say helloworld, you would create a Makefile with:

helloworld/Makefile:

	include ysr.mk

	helloworld_OBJS:=$(DEST)/helloworld/hello.o

	$(call mk-c-prog-rule,helloworld)

The system finds out which source file to compile by taking the name of the objects (here $(DEST)/apps/helloworld/hello.o) then
translating the path from $(DEST) (output tree) back to $(TOP) (source tree) and finding out candidates:

Here it would look up for a file named
    $(TOP)/apps/helloworld/hello.c

and if that one isn't found, for a
    $(TOP)/apps/helloworld/hello.cc (c++) file.

