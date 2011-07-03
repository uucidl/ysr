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

YSR.libdir=$(realpath $(YSR.libdir))
MAKE:=$(YSR.bin)
-include $(YSR.project.file)

$(info Using project file: $(YSR.project.file))
YSR.project.name?="Unnamed"

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

# Now we are trying to find out on which architecture (OS/CPU
# combination) we are building this project.
#
# ARCH represents the software/hardware architecture (LINUX, WIN32, MACOSX)
# CPU  represents the cpu (i386, PENTIUM, PENTIUMPRO, K8, POWERPC, EE)
#
HOST_SYSTEM:=$(shell sh $(YSR.libdir)/scripts/guess-build-system.sh)
HOST_ARCH:=$(firstword $(HOST_SYSTEM))
HOST_CPU:=$(shell sh $(YSR.libdir)/scripts/guess-build-cpu.sh)
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
YSR.destdir?=$(TOP)/OUTPUT
DEST_NAME=$(firstword $(OTYPE))_$(ARCH)_$(CPU)
DEST=$(YSR.destdir)/$(DEST_NAME)

# C/C++:
#
# Set up top of the tree for inclusion of headers.

GLOBAL_INCLUDES=$(TOP)

#
# Makefile specific variables
#

VPATH=$(TOP)

all:

DOXYGEN?=doxygen

docs:
	@echo "Rebuilding documentation"
	@(cd $(TOP)/ide ; $(DOXYGEN) ln2.doxy)

$(TOP)/ide/TAGS: require-etags
	$(RM) "$@"
	$(call perform-shell,find $${DIR} \( -name "*.mk" -o -name "Makefile" -o -name "*.c" -o -name "*.h" -o -name "*.hh" -o -name "*.cc" -o -name "*.cpp" \) -print0) | xargs -0 $(ETAGS) -a -o "$@"

TAGS: $(TOP)/TAGS

.PHONY: TAGS $(TOP)/TAGS

HOST_CONFIG_MK=$(TOP)/ysr/$(HOST_NAME)-config.mk
-include $(HOST_CONFIG_MK)

include $(YSR.libdir)/functions/functions.mk
include $(YSR.libdir)/languages/gcc/rules.mk
include $(YSR.libdir)/languages/gcc/flags.mk

##
# helps you define rules for making programs.
#
include $(YSR.libdir)/templates/target-rules.mk

##
# fetch the current revision of the tree

REPO_IS_SVN=$(if $(realpath $(TOP)/.svn),T,)
REPO_IS_GIT=$(if $(realpath $(TOP)/.git),T,)
REPO_TYPE=$(if $(REPO_IS_SVN),svn,$(if $(REPO_IS_GIT),git,none))

REPO_REV=$(shell sh $(YSR.libdir)/scripts/get-revision-$(REPO_TYPE).sh)

BUILD_DESC:=$(strip $(OTYPE)-$(REPO_REV))

GLOBAL_DEFINES+=BUILD_DESC="\"$(BUILD_DESC)\""
GLOBAL_DEFINES+=ARCH_IS_$(ARCH)
GLOBAL_DEFINES+=OTYPE_IS_$(OTYPE)
GLOBAL_DEFINES+=TOP=$(call nativepath,$(TOP))
