ifndef thirdparty_gcc_profile_mk
thirdparty_gcc_profile_mk=1

# This stuff is here for now but shall be moved later (it's specific to gcc)

CSTD=-std=c99
CXXSTD=-std=c++11
GCCFLAGS+=-Wall -Wextra -Winit-self -Wno-undef -Wno-unused -Wno-unused-parameter

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
endif

ifeq ($(ARCH),WIN32)
GLOBAL_LDFLAGS+=-static-libgcc
GLOBAL_LDXXFLAGS+=-static-libstdc++
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
