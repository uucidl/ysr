ifndef third_party_gcc_rules_mk
third_party_gcc_rules_mk=1

#
# set up default variables from the environment
#
include $(YSR.libdir)/languages/gcc/profile.mk

# Uses the following variables:
#
#   CC       : c compiler
#   CXX      : c++ compiler
#   CFLAGS   : flags for c compilation
#   CXXFLAGS : flags for c++ compilation
#   ASFLAGS  : flags for assembly compilation
#   LDFLAGS  : flags used when linking objects
#
#   INCLUDES : directories to include on top of the path
#   DEFINES  : preprocessor tokens which must be defined
#   LIBSPATH : where to find libraries
#   LIBS     : libraries to link against (static and dynamic)
#   FRAMEWORKSPATH : where to find frameworks
#   FRAMEWORKS : frameworks to link against (macosx)
#
#   DEST     : directory where the output objects will be built
#
# In addition, each source file can have special flags, named after
# the source's target, with _FLAGS appended.
#
# For example, to customize main.c's flags, define a variable named:
#   main.c_FLAGS
#
# In most case you may want to use GNU Make's target-specific variable
# overriding feature, though
#

##
# Functions

include $(YSR.libdir)/functions/display.mk
include $(YSR.libdir)/functions/functions.mk

## GCC FAMILY of functions

DFLAGS=$(addprefix -D,$(DEFINES))
IFLAGS=$(addprefix -I,$(INCLUDES)) $(addprefix -F,$(FRAMEWORKSPATH))
LFLAGS=$(LDFLAGS) $(addprefix -L,$(LIBSPATH)) $(addprefix -l,$(filter-out %.a %.lib,$(LIBS))) $(filter %.a %.lib,$(LIBS)) $(addprefix -F,$(FRAMEWORKSPATH)) $(addprefix -framework ,$(FRAMEWORKS))

# preprocessing flags
all_CPPFLAGS=$(CPPFLAGS) $(DFLAGS) $(IFLAGS)

##
# compile C files
#
# $(1): paths to the C files
# $(2): path of the object
# $(3): extra-flags
#
compile-c-gcc= \
	$(call ysr-display-left,"[$(CC) C] $(notdir $(1))") && \
	$(CC) $(CSTD) $(CFLAGS) $(OFLAGS) $(all_CPPFLAGS) $(3) -c $(1) -o $(2) && \
	$(call ysr-display-right,"done")

##
# compile C++ files
#
# $(1): paths to the C++ files
# $(2): path of the object
#
compile-c++-gcc= \
	$(call ysr-display-left,"[$(CXX) C++] $(notdir $(1))") && \
	$(CXX) $(CXXFLAGS) $(OFLAGS) $(all_CPPFLAGS) $(3) -c $(1) -o $(2) && \
	$(call ysr-display-right,"done")

##
# compile objective C files
#
# $(1): paths to the objective c files
# $(2): path of the object
#
compile-objc-gcc= \
	$(call ysr-display-left,"[$(CXX) ObjC] $(notdir $(1))") && \
	$(CC) -x objective-c $(CSTD) $(CFLAGS) $(OFLAGS) $(all_CPPFLAGS) $(3) -c $(1) -o $(2) && \
	$(call ysr-display-right,"done")

##
# compile objective C files
#
# $(1): paths to the objective c files
# $(2): path of the object
#
compile-objcc-gcc= \
	$(call ysr-display-left,"[$(CXX) ObjC] $(notdir $(1))") && \
	$(CXX) $(CFLAGS) $(OFLAGS) $(all_CPPFLAGS) $(3) -c $(1) -o $(2) && \
	$(call ysr-display-right,"done")

##
# compile as files (gcc assembly)
#
# $(1): paths to the as files
# $(2): path of the object
# $(3): extra flags
#
compile-as-gcc= \
	$(call ysr-display-left,"[$(CC) AS] $(notdir $(1))") && \
	$(CC) $(ASFLAGS) $(all_CPPFLAGS) $(3) -c $(1) -o $(2) && \
	$(call ysr-display-right,"done")

##
# link a c executable
#
# $(1): path to the objects
# $(2): path to the executable
# $(3): extra flags
#
link-c-gcc= \
	$(call ysr-display-left,"[$(CC)] => $(notdir $(2))") && \
	$(CC) $(LDFLAGS) -o $(2) $(filter %.o,$(1)) $(filter %.a,$(1)) $(3) $(LFLAGS) && \
	$(call ysr-display-right,"done")

##
# link a c++ executable
# $(1): path to the objects
# $(2): path to the executable
# $(3): extra flags
#
link-c++-gcc= \
	$(call ysr-display-left,"[$(CXX)] => $(notdir $(2))") && \
	$(CXX) $(LDFLAGS) $(LDXXFLAGS) -o $(2) $(filter %.o,$(1)) $(filter %.a,$(1)) $(3) $(LFLAGS) && \
	$(call ysr-display-right,"done")

##
# link a c bundle
# $(1): path to the objects
# $(2): path to the executable
# $(3): extra flags
#
link-c-bundle-gcc= \
	$(call ysr-display-left,"[$(CC)] => $(notdir $(2))") && \
	$(CC) $(LDFLAGS) -o $(2) $(filter %.o,$(1)) $(filter %.a,$(1)) $(3) $(LFLAGS) && \
	$(call ysr-display-right,"done")

##
# link a c bundle
# $(1): path to the objects
# $(2): path to the executable
# $(3): extra flags
#
link-c++-bundle-gcc= \
	$(call ysr-display-left,"[$(CXX)] => $(notdir $(2))") && \
	$(CXX) $(LDFLAGS) $(LDXXFLAGS) -o $(2) $(filter %.o,$(1)) $(filter %.a,$(1)) $(3) $(LFLAGS) && \
	$(call ysr-display-right,"done")

##
# archive object files into a lib
# $(1): path to objects
# $(2): path to archive
# $(3): extra flags
#
ar-lib= \
	$(call ysr-display-left,"[$(AR)] => $(notdir $(2))") && \
	$(AR) crus $(3) $(2) $(1) && \
	$(call ysr-display-right,"done")

# libraries use different linking methods:
link-c-shared-gcc=$(call link-c-gcc,$(1),$(2),$(3))
link-c++-shared-gcc=$(call link-c++-gcc,$(1),$(2),$(3))
link-c-static-gcc=$(call ar-lib,$(1),$(2),$(3))
link-c++-static-gcc=$(call ar-lib,$(1),$(2),$(3))

##
# common rules

COMPILER_FAMILY=gcc

%.o: %.c
	@$(call compile-c-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

%.o: %.cpp
	@$(call compile-c++-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

%.o: %.cc
	@$(call compile-c++-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

%.o: %.s
	@$(call compile-as-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

%.o: %.m
	@$(call compile-objc-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

%.o: %.mm
	@$(call compile-objcc-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

$(DEST)/%.o: $(TOP)/%.c $(DEST)/%.o.dep
	@$(call require-directory,$(dir $@))
	@$(call compile-c-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

$(DEST)/%.o: $(TOP)/%.cpp $(DEST)/%.o.dep
	@$(call require-directory,$(dir $@))
	@$(call compile-c++-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

$(DEST)/%.o: $(TOP)/%.cc $(DEST)/%.o.dep
	@$(call require-directory,$(dir $@))
	@$(call compile-c++-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

$(DEST)/%.o: $(TOP)/%.m $(DEST)/%.o.dep
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"$*.o.dep")
	@$(call compile-objc-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))
	@$(call ysr-display-right,"done")

$(DEST)/%.o: $(TOP)/%.mm $(DEST)/%.o.dep
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"$*.o.dep")
	@$(call compile-objcc-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))
	@$(call ysr-display-right,"done")

$(DEST)/%.o: $(TOP)/%.s $(DEST)/%.o.dep
	@$(call compile-as-$(COMPILER_FAMILY),$<,$@,$($<_FLAGS))

##
# Dependancy computation
#

##
# convert objects to the filenames of their dependency files
#
# $(1) objects
#
objs-to-deps=$(patsubst %.o,%.o.dep,$(filter %.o,$(1)))

##
# flags used when building dependencies

depflag-filter=$(filter-out -O% -g% -W%,$(1))

ifeq ($(ARCH),MACOSX)
SED=sed -E
SED_EXPR=-e
SED_PLUS=+
SED_LPAREN=(
SED_RPAREN=)
else
SED=sed
SED_EXPR=-e
SED_PLUS=\+
SED_LPAREN=\(
SED_RPAREN=\)
endif

REMOVE_COMMENTS=$(SED_EXPR) 's/\#.*//'
REMOVE_TARGET=$(SED_EXPR) 's/^[^:]*: *//'
REMOVE_EXTRA_BACKSLASH=$(SED_EXPR) 's/ *\\$$//'
REMOVE_EMPTY_LINES=$(SED_EXPR) '/^$$/ d'
POSTFIX_LINES_WITH_COLON=$(SED_EXPR) 's/$$/ :/'

$(DEST)/%.o.dep: $(DEST)/%.D
	@$(SED) $(SED_EXPR) 's,^$(SED_LPAREN).$(SED_PLUS)$(SED_RPAREN)\.o[ :]$(SED_PLUS),$(DEST)/$*\.o $@ : $(YSR.libdir)/languages/gcc/profile.mk ,g' $< > $@
	@$(SED) $(REMOVE_COMMENTS) $(REMOVE_TARGET) $(REMOVE_EXTRA_BACKSLASH) $(REMOVE_EMPTY_LINES) $(POSTFIX_LINES_WITH_COLON) $< >> $@
	@$(ysr-display-banner) "$@: \n" >> $@

$(DEST)/%.D: YSR_DISPLAY_DISABLED=true
$(DEST)/%.D: CPPFLAGS+=-w -MM

$(DEST)/%.D: %.c
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CC) C dep] $*.D")
	@$(CC) $(CSTD) $(call depflag-filter,$(CFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: $(DEST)/%.c
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CC) C dep] $*.D")
	@$(CC) $(CSTD) $(call depflag-filter,$(CFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: %.cpp
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CXX) C++ dep] $*.D")
	@$(CXX) $(call depflag-filter,$(CXXFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: %.cc
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CXX) C++ dep] $*.D")
	@$(CXX) $(call depflag-filter,$(CXXFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: %.s
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CC) AS dep] $*.D")
	@$(CC) $(call depflag-filter,$(ASFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: %.m
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CXX) Obj-C dep] $*.D")
	@$(CC) $(call depflag-filter,$(CFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

$(DEST)/%.D: %.mm
	@$(call require-directory,$(dir $@))
	@$(call ysr-display-left,"[$(CXX) Obj-C dep] $*.D")
	@$(CXX) $(call depflag-filter,$(CFLAGS)) $(all_CPPFLAGS) $($<_FLAGS) $< > $@
	@$(call ysr-display-right,"done")

ifeq ($(ARCH),WIN32)
$(DEST)/%.o.dep: %.rc
	@$(ysr-display-banner) "\n" > $@

$(DEST)/%.o: %.rc
	@$(call require-directory,$(dir $@))
	@windres -O coff $(IFLAGS) $< $@
endif

endif
