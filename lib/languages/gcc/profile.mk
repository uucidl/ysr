ifndef thirdparty_gcc_profile_mk
thirdparty_gcc_profile_mk=1

# This stuff is here for now but shall be moved later (it's specific to gcc)

CSTD=-std=c99
CXXSTD=-std=c++11
GCCFLAGS+=-Wall -Wextra -Winit-self -Wno-undef -Wno-unused -Wno-unused-parameter
CC_VERSION:=$(strip $(shell $(YSR.libdir)/scripts/cc-version.sh "$(CC)"))

ifeq ($(CC_VERSION),)
$(error "Could not find out exact gcc version (Using '$(CC)').")
endif

GCC_VARIANT:=$(word 1,$(CC_VERSION))
GCC_VERSION_MAJOR:=$(word 2,$(CC_VERSION))
GCC_VERSION_MINOR:=$(word 3,$(CC_VERSION))

ifneq ($(filter-out clang gcc,$(GCC_VARIANT)),)
$(error "Unknown gcc variant '$(GCC_VARIANT)'")
endif

# the math library
GLOBAL_LIBS:=m

ifneq ($(or $(findstring DEBUG,$(OTYPE)),$(findstring TEST,$(OTYPE))),)
GLOBAL_OFLAGS+=-g
ifeq ($(ARCH),MACOSX)
GLOBAL_OFLAGS+=-glldb -g3
else
GLOBAL_OFLAGS+=-ggdb -g3
endif

# helps with debugging
GLOBAL_OFLAGS+=-fno-inline
endif

GLOBAL_OFLAGS+=-fstrict-aliasing -Wstrict-aliasing=2

ifneq ($(or $(findstring TEST,$(OTYPE)),$(findstring RELEASE,$(OTYPE))),)
GLOBAL_OFLAGS+=-Os -ffast-math
ifeq ($(GCC_VARIANT),gcc)
GLOBAL_OFLAGS+=-fexpensive-optimizations
endif

endif

ifeq ($(ARCH),WIN32)

ifneq ($(findstring CYGWIN,$(HOST_SYSTEM)),)
GCCFLAGS+=-mno-cygwin
GLOBAL_INCLUDES:=/usr/include/mingw $(INCLUDES)
GLOBAL_LIBSPATH:=/lib/mingw $(LIBSPATH)
else
ifeq ($(GCC_VERSION_MAJOR),4)
GLOBAL_LDFLAGS+=-static-libgcc
GLOBAL_LDXXFLAGS+=-static-libstdc++
endif

endif

endif

ifeq ($(ARCH),MACOSX)
GLOBAL_CXXFLAGS+=-fvisibility=hidden
GCCFLAGS+=-no-cpp-precomp

ifneq ($(CPU),x86_64)
## compatibility towards tiger
GLOBAL_LDFLAGS+=-mmacosx-version-min=10.4
MACOSX_DEPLOYMENT_TARGET=10.4
export MACOSX_DEPLOYMENT_TARGET
GCCFLAGS+=-m32
GLOBAL_LDFLAGS+=-m32
endif

endif

# Executables look for their dll in their installation dir.
ifeq ($(ARCH),LINUX)
GLOBAL_LDFLAGS+=-Wl,-z,origin -Wl,-rpath,'$$ORIGIN/.'
endif


GLOBAL_CFLAGS+=$(GCCFLAGS)
GLOBAL_CXXFLAGS+=$(GCCFLAGS)
GLOBAL_LDFLAGS+=$(GCCFLAGS) $(CXXFLAGS)
GLOBAL_CPPFLAGS+=$(GCCFLAGS)

ifdef STRICT
GLOBAL_CFLAGS+=-Werror
GLOBAL_CXXFLAGS+=-Werror
endif

endif
