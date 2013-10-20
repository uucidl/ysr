ifndef ysr_lib_functions_mk
ysr_lib_functions_mk=1

##
# Find the first file found under the given list of paths
#
# param[0] = name/path of file
# param[1] = paths to find it under, separated with spaces
#
findfile = $(firstword $(wildcard $(1) $(addsuffix /$(1), $(2))))

##
# Find an executable in the path.
#
# param [0] = name of executable
#
pathsearch = $(call findfile,$(1),$(subst :, ,$(PATH)))

##
# function validating the filename of an executable for the build platform
#
# $(1): name or list of executables (canonical name as you would call
#       it on the command line
#
to-executable-LINUX=$(1)
to-executable-MACOSX=$(1)
to-executable-IOS=$(1)
to-executable-WIN32=$(addsuffix .exe,$(basename $(1)))

to-executable-LINUX-with-ext=$(addsuffix .elf,$(1))
to-executable-MACOSX-with-ext=$(addsuffix .elf,$(1))
to-executable-IOS-with-ext=$(addsuffix .elf,$(1))
to-executable-WIN32-with-ext=$(addsuffix .exe,$(basename $(1)))

to-lib-prefix=$(dir $(1))/lib$(basename $(notdir $(1)))

to-lib-LINUX-shared-ext=$(addsuffix .so,$(1))
to-lib-MACOSX-shared-ext=$(addsuffix .dylib,$(1))
to-lib-WIN32-shared-ext=$(addsuffix .dll,$(1))

to-lib-LINUX-shared=$(call to-lib-LINUX-shared-ext,$(call to-lib-prefix,$(1)))
to-lib-MACOSX-shared=$(call to-lib-MACOSX-shared-ext,$(call to-lib-prefix,$(1)))
to-lib-WIN32-shared=$(call to-lib-WIN32-shared-ext,$(1))

to-lib-LINUX-static=$(addsuffix .a,$(call to-lib-prefix,$(1)))
to-lib-MACOSX-static=$(addsuffix .a,$(call to-lib-prefix,$(1)))
to-lib-WIN32-static=$(addsuffix .a,$(call to-lib-prefix,$(1)))

to-lib-LINUX-bundle=$(error bundles are only supported on OSX)
to-lib-WIN32-bundle=$(error bundles are only supported on OSX)
to-lib-MACOSX-bundle=$(1)

##
#
# Find an executable in the path. The executable name is provided but
# can be overriden by the provided variable.
#
# param[1] = input variable name where the executable name can be found
# param[2] = canonical executable name
#
findexec=$(or $(or $(and $($(1)),$(call pathsearch,$($(1)))), $(call pathsearch,$(2))),)

##
# make sure the given directory exists
#
require-directory=[ -d "$(1)" ] || mkdir -p $(1)

##
# convert a filename or file path into paths suitable for native programs
#
# used when hosted on cygwin where a special conversion is useful in order to work with non-cygwin programs.
#
# $(1) is the pathname to change
nativepath=$(1)

ifneq ($(findstring CYGWIN,$(HOST_SYSTEM)),)
nativepath=$(shell cygpath -m $(1))
endif

## iso timestamp
isotimestamp=$(shell date +%Y%m%d@%H%M%S)

endif
