##
# You must include this file in your makefile to import YSR functionality.

##
#
# This file setups GNU Make and imports our templates & makefiles
#
# Per machine configuration flags are stored inside
#		configs/<hostname>-config.mk
#   See them as examples!
#
# It exports:
#
# TOP, DEST, HOST_SYSTEM, HOST_ARCH, HOST_CPU, ARCH, CPU, OTYPE
#
# You can export
#
# STRICT=1 if you wish to force the compiler to fail on warnings
# VERBOSE=T if you wish to see MORE warnings from us.
#

##
# These flags are automatically defined for use in C/C++ programs:
#
# BUILD_DESC - a string representing the particular build. Includes
# the revision and object type.
#

# require GNU Make Version 3.80 at least
eval_is_available:=
$(eval eval_is_available:=T)

ifneq ($(eval_is_available),T)
$(error GNU Make 3.80 at least is required)
endif

# we make use of the secondary expansion feature to create destination directories automatically.
ifneq ($(filter second-expansion,$(.FEATURES)),second-expansion)
$(error GNU Make 3.81 at least is required)
endif

.SECONDEXPANSION:

YSR.libdir=$(realpath $(YSR.libdir))
MAKE:=$(YSR.bin)

# clears up the suffixes to speed up make starting up somewhat
#
.SUFFIXES:

%.mk :: ;

# remove some match-anything rules

% :: %,v
% :: RCS/%,v
% :: RCS/%
% :: s.%
% :: SCCS/s.%

# TOP is the top of the source tree
TOP:=$(realpath ./$(TOP))
ifeq ($(TOP),)
TOP:=.
endif

ifneq ($(YSR.project.file),)
TOP:=$(dir $(abspath $(YSR.project.file)))
endif

-include $(YSR.project.file)

YSR.project.name?="Unnamed"

# Now we are trying to find out on which architecture (OS/CPU
# combination) we are building this project.
#
# ARCH represents the software/hardware architecture (LINUX, WIN32, MACOSX)
# CPU  represents the cpu (i386, PENTIUM, PENTIUMPRO, x86_64, POWERPC, EE)
#
# Yes, I know ARCH is a bad name for what is effectively the software
# platform / operating system.

ifeq ($(OS),Windows_NT)

  ## the OS variable is present by default on Windows:
  HOST_SYSTEM:=WIN32
  HOST_ARCH:=WIN32

  ifeq ($(firstword $(subst /, ,$(PWD))),cygdrive)
  HOST_SYSTEM:=WIN32 CYGWIN
  endif

  ## and comes with another variable PROCESSOR_ARCHITECTURE which we can
  ## use to find out the processor type:
  ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
  HOST_CPU:=x86_64
  endif
  ifeq ($(PROCESSOR_ARCHITECTURE),x86)
  HOST_CPU:=i386
  endif

else
## otherwise we presume you have some sort of posix system where we can run the
## shell scripts:
HOST_SYSTEM:=$(shell sh $(YSR.libdir)/scripts/guess-build-system.sh)
HOST_ARCH:=$(firstword $(HOST_SYSTEM))
HOST_CPU:=$(shell sh $(YSR.libdir)/scripts/guess-build-cpu.sh)
endif
HOST_NAME:=$(shell hostname)

ifeq ($(HOST_NAME),,)
HOST_NAME:=local
endif

# By default we are building for the host architecture.
ARCH?=$(HOST_ARCH)
CPU?=$(HOST_CPU)

# Type of binaries: DEBUG, TEST or RELEASE
OTYPE ?= TEST
ifeq ($(filter DEBUG TEST RELEASE,$(OTYPE)),)
$(error $(OTYPE) is not an accepted object type)
endif

and_ARCH_CPU_OTYPE:=
ifneq ($(ARCH),)
ifneq ($(CPU),)
ifneq ($(OTYPE)),)
and_ARCH_CPU_OTYPE:=T
endif
endif
endif

ifeq ($(and_ARCH_CPU_OTYPE),)
$(error ARCH [value: $(ARCH)], CPU [value: $(CPU)] and OTYPE [value: $(OTYPE)] must be defined)
endif

# DEST is the top of the products tree (where objects and final
# programs are built)
YSR.destdir?=$(TOP)/build
DEST_NAME=$(firstword $(OTYPE))-$(ARCH)-$(CPU)
DEST=$(abspath $(YSR.destdir)/$(DEST_NAME))


# C/C++:
#
# Set up top of the tree for inclusion of headers.

GLOBAL_INCLUDES=$(TOP)

#
# Makefile specific variables
#

VPATH=$(TOP)

.PHONY: all
all:

##
# automatic directory creation

# by setting a file "<dir>/.dir" as the prerequisite of a rule,
# you can ensure the corresponding directory to be created using this rule:

ifeq ($(ARCH),WIN32)
make-directories=$(shell $(YSR.libdir)/scripts/makedirs.bat $(call nativepath,$(1)))
else
# assume posix
make-directories=$(shell mkdir -p $(1))
endif

%/.dir:
	$(call make-directories,$(dir $@))
	touch $@
.PRECIOUS:%/.dir


HOST_CONFIG_MK=$(TOP)/ysr/$(HOST_NAME)-config.mk
$(info Loading host-specific config from $(HOST_CONFIG_MK))
-include $(HOST_CONFIG_MK)

WINDRES?=windres

include $(YSR.libdir)/functions/functions.mk
include $(YSR.libdir)/languages/gcc/rules.mk
include $(YSR.libdir)/languages/gcc/flags.mk

##
# helps you define rules for making programs.
#
include $(YSR.libdir)/templates/target-rules.mk

GLOBAL_DEFINES+=ARCH_IS_$(ARCH)
GLOBAL_DEFINES+=OTYPE_IS_$(OTYPE)
GLOBAL_DEFINES+=TOP=$(call nativepath,$(TOP))

$(info Using project file: $(YSR.project.file), TOP is `$(TOP)', DEST is `$(DEST)')
