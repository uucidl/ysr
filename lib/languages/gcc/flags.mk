ifndef third_party_gcc_flags
third_party_gcc_flags=1

include $(YSR.libdir)/languages/gcc/profile.mk

# Used for dynamic shared objects / dlls to make the entry point visible.
ifeq ($(GCC_VERSION_MAJOR),4)
CXXFLAGS_dso=-fvisibility=default
else ifeq ($(GCC_VERSION_MAJOR),3)
CXXFLAGS_dso=
else
$(error "Undefined CXXFLAGS_dso" $(GCC_VERSION))
endif
endif
