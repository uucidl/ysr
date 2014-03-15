ifndef third_party_gcc_flags
third_party_gcc_flags=1

include $(YSR.libdir)/languages/gcc/profile.mk

# Used for dynamic shared objects / dlls to make the entry point
# visible.

ifneq ($(filter $(GCC_VERSION_MAJOR),1 2 3),)
CXXFLAGS_dso=
else
CXXFLAGS_dso=-fvisibility=default
endif
