ifndef scripts_program_mk
scripts_program_mk=1

include $(YSR.libdir)/functions/template-functions.mk

##
# creates rules to build a program named after the given parameter.
#
# - objects are derived from <program_name>_OBJS
#
# - additional dependencies can be provided via <program_name>_DEPS
# - modules can be imported via <program_name>_REQUIRES
# - shared libraries to be linked with the program: <program_name>_SHLIBS
# - other files used as data for the program: <program_name>_DATAFILES
#
# some other extra variables (low level, you should avoid using them)
#  LDFLAGS, CPPFLAGS, CFLAGS, CXXFLAGS
#
# compilation variables will be altered to take those dependencies
# into account.
#
# - program parameters (when ran) can be provided via
# <program_name>_ARGS
#
# the function will export rules:
#   <program_name>-run
#	Params: <program_name>_ARGS (command line arguments)
#		<program_name>_RUNTITLE (a name for this run)
#       Results:
#         produces a timestamped file in the program's directory. Suffixed with the RUNTITLE.
#
#   <program_name>-debug
#   <program_name>-clean
#
# and modify:
#   ALL_PROG_NAMES (adding <program_name>)
#   ALL_PROGS (adding the corresponding executable)
#

##
# These functions take the name of a program as parameter, and will
# build the appropriate rules using <program_name>_<suffix>.
#
# Using:
#
# $(call ysr-add-c-prog,myprogram)
#
# will create rules myprogram, myprogram-run, myprogram-debug using
# myprogram_OBJS and myprogram_REQUIRES to find the objects to build
# and their dependencies, respectively.
#

##
# Create a program based on the C runtime
#
# @param $(1) name of the program
ysr-add-c-prog=$(eval $(call ysr-prv-prog-rule-template,$(1),c,))

##
# Create a program based on the C++ runtime
#
# @param $(1) name of the program
ysr-add-c++-prog=$(eval $(call ysr-prv-prog-rule-template,$(1),c++,))

##
# Create a console program based on the C runtime
#
# @param $(1) name of the program
ysr-add-c-console-prog=$(eval $(call ysr-prv-prog-rule-template,$(1),c,console))

##
# Create a console program based on the C++ runtime
#
# @param $(1) name of the program
ysr-add-c++-console-prog=$(eval $(call ysr-prv-prog-rule-template,$(1),c++,console))

### PRIVATE IMPLEMENTATION #

##
# ysr-prv-prog-rule-template : when 'evaled' will add rules to build the
# provided "program"
#
# $(1) : name of the program
# $(2) : language/runtime environment (c++, c)
# $(3) : 'console' if the program must be run in a console.
#
# requires <program_name>_OBJS <program_name>_DEPS <program_name>_REQUIRES

define ysr-prv-prog-rule-template

## The program

$(1)_PROG:=$$(call to-executable-$$(ARCH)-with-ext,$$(DEST)/$(1)/$(1))
ifeq ($$($(1)_PROG),)
$$(error "Couldn't produce an executable name for $(1)")
endif

$(1)_ARCH_P:=$$(ARCH)$$(filter console,$(3))
ifeq ($$($(1)_ARCH_P),MACOSX)
$(1)_use_osx_bundle=T
endif

ifeq ($$($(1)_ARCH_P),IOS)
$(1)_use_osx_bundle=T
endif

ifeq ($$($(1)_use_osx_bundle),T)
# MACOSX/IOS + not a console program => run the bundle rather than the executable.
$(1)_BUNDLE:=$(DEST)/$(1)/$(1).app
$(1)_LOCAL_EXEC:=./$$(notdir $$($(1)_BUNDLE))/Contents/MacOS/$$(notdir $$($(1)_PROG))
$(1)_EXEC_PATH:=$$($(1)_BUNDLE)/Contents/Resources
$(1)_EXEC:=$$($(1)_BUNDLE)/Contents/MacOS/$$(notdir $$($(1)_PROG))
else
$(1)_BUNDLE:=
$(1)_LOCAL_EXEC:=./$$(notdir $$($(1)_PROG))
$(1)_EXEC:=$$($(1)_PROG)
$(1)_EXEC_PATH:=$$(dir $$($(1)_PROG))
endif

###
## find out everything about the required modules and validate
## that they are ready to be included.
$(1)_private_REQUIRES:=$$(call ysr-prv-walk-requires,$(1))
$$(call ysr-prv-requires-all,$$($(1)_private_REQUIRES),$(1),$(2))

## Uncomment to debug dependencies:
## $$(info $(1) = $$($(1)_private_REQUIRES))

##
# function importing variables from dependencies
# $(1) is the variable suffix to import.
#
# The core version of the variable are also appended
#
$(1)_import=$$(call ysr-prefixing-references,$$(1),GLOBAL $(1) $$($(1)_private_REQUIRES))

$(1)_all_DEPS:=$$(call $(1)_import,DEPS)
$(1)_all_OBJS:=$$(call $(1)_import,OBJS)
$(1)_all_CPPFLAGS:=$$(call $(1)_import,CPPFLAGS)
$(1)_all_CFLAGS:=$$(call $(1)_import,CFLAGS)
$(1)_all_CXXFLAGS:=$$(call $(1)_import,CXXFLAGS)
$(1)_all_OFLAGS:=$$(call $(1)_import,OFLAGS)
$(1)_all_LDFLAGS:=$$(call $(1)_import,LDFLAGS)
$(1)_all_LDXXFLAGS:=$$(call $(1)_import,LDXXFLAGS)
$(1)_all_FRAMEWORKSPATH:=$$(call $(1)_import,FRAMEWORKSPATH)
$(1)_all_FRAMEWORKS:=$$(call $(1)_import,FRAMEWORKS)
$(1)_all_INCLUDES:=$$(call $(1)_import,INCLUDES)
$(1)_all_DEFINES:=$$(call $(1)_import,DEFINES)
$(1)_all_LIBSPATH:=$$(call $(1)_import,LIBSPATH)
$(1)_all_LIBS:=$$(call $(1)_import,LIBS)
$(1)_all_SHLIBS:=$$(call $(1)_import,SHLIBS)
$(1)_all_DATAFILES:=$$(strip $$(call $(1)_import,DATAFILES))

ALL_PROG_NAMES+=$(1)
ALL_PROGS+=$$($(1)_PROG)

# autogenerated dependencies are found in .dep files
$(1)_MKDEP:=$$(patsubst %.o,%.o.dep,$$(filter %.o,$$($(1)_all_OBJS)))
$(1)_MKDEPALL:=$$($(1)_MKDEP) $$(patsubst %.o,%.D,$$(filter %.o,$$($(1)_all_OBJS)))

# how to build the program
$$($(1)_PROG): CPPFLAGS=$$($(1)_all_CPPFLAGS)
$$($(1)_PROG): INCLUDES=$$($(1)_all_INCLUDES)
$$($(1)_PROG): CFLAGS=$$($(1)_all_CFLAGS)
$$($(1)_PROG): OFLAGS=$$($(1)_all_OFLAGS)
$$($(1)_PROG): CXXFLAGS=$$($(1)_all_CXXFLAGS)
$$($(1)_PROG): DEFINES=$$($(1)_all_DEFINES)
$$($(1)_PROG): LIBS=$$($(1)_all_LIBS)
$$($(1)_PROG): LIBSPATH=$$($(1)_all_LIBSPATH)
$$($(1)_PROG): LDFLAGS=$$($(1)_all_LDFLAGS)
$$($(1)_PROG): LDXXFLAGS=$$($(1)_all_LDXXFLAGS)
$$($(1)_PROG): FRAMEWORKSPATH=$$($(1)_all_FRAMEWORKSPATH)
$$($(1)_PROG): FRAMEWORKS=$$($(1)_all_FRAMEWORKS)

ifneq ($$($(1)_COMPILER_FAMILY),)
$$($(1)_PROG): COMPILER_FAMILY=$$($(1)_COMPILER_FAMILY)
endif

$$($(1)_MKDEP): INCLUDES=$$($(1)_all_INCLUDES)
$$($(1)_MKDEP): DEFINES=$$($(1)_all_DEFINES)

$$($(1)_PROG): $$($(1)_MKDEP)
$$($(1)_PROG): $$($(1)_all_DEPS)
$$($(1)_PROG): $$($(1)_all_OBJS) $$$$(@D)/.dir
	$$(call link-$(2)-$$(COMPILER_FAMILY),$$^,$$@,)

$(1)-bundle: $$($(1)_PROG)
	@test -n "$$($(1)_BUNDLENAME)" || ($(ysr-display-banner) 'You must configure a bundle name for $(1) with $(1)_BUNDLENAME!\n' && false)
	$(YSR.libdir)/scripts/$(ARCH)/make-application-bundle.rb $$($(1)_BUNDLENAME) $$($(1)_PROG) $$($(1)_all_SHLIBS) $$($(1)_all_DATAFILES)

ifneq ($$($(1)_BUNDLE),)
$(1): $(1)-bundle
else
$(1): $$($(1)_PROG)
	[ -z "$$($(1)_all_DATAFILES)" ] || cp -r -f $$($(1)_all_DATAFILES) $$(dir $$($(1)_PROG))
endif

ifneq ($$($(1)_PERFMON),)
ifneq ($$(PYTHON),)
$(1)_perfmon_exec=\
		([ -f $$$${PP}/perf.log ] && rm $$$${PP}/perf.log ; \
		 touch $$$${PP}/perf.log ; \
		 python $$(TOP)/scripts/perfmon.py --realtime $$$${PP}/perf.log > /dev/null &)
else
$(1)_perfmon_exec=true
endif

else
$(1)_perfmon_exec=true
endif

$(1)_RUNENV:=
ifeq ($(ARCH),WIN32)
$(1)_RUNENV:=export PATH=$$$$PATH:$$(dir $$($(1)_PROG))
else
$(1)_RUNENV:=export LD_LIBRARY_PATH=$$(dir $$($(1)_PROG))
endif

ifeq ($(ARCH),MACOSX)
$(1)_RUNENV+=; export MACOSX_DEPLOYMENT_TARGET=
endif

$(1)_RUNTITLE ?= default
$(1)_REPORTS_DIR=$(DEST)/reports
$(1)_REPORTFILE=$($(1)_REPORTS_DIR)/$(1)-report-$$(call isotimestamp)-$$($(1)_RUNTITLE).out

##
# running the test (the complex command line is to enable capturing
# the return code while also using tee to redirect the result of the
# command to a log file
#
# Additional parameters can be provided in the <program_name_ARGS>
# variable
$(1)-run: $(1) $$$$(dir $$($(1)_REPORTS_DIR))/.dir
	@$(ysr-display-banner) "* Testing $(1)\n"
# copy win32 dlls into the destination
	@$(ysr-display-banner) "Copying shared libraries:\n"
	true $$(foreach D,$$($(1)_all_SHLIBS),; cp -f $$(D) $$(dir $$($(1)_PROG)))
	@$(ysr-display-banner) "Running:\n"
	(exec 3>&1 ; RC=$$$$(exec 4>&1; { \
		export PP=$$($(1)_EXEC_PATH) ; \
		export HH=$(DEST)/$(1) ; \
		$$($(1)_RUNENV) ; \
		$$($(1)_perfmon_exec) ; \
		cd $$$${HH} ; \
		PATH=$$$${PATH}:. \
		$$($(1)_LOCAL_EXEC) $$($(1)_ARGS) ; \
		echo $$$$? >&4; \
		true; \
	} | tee $$($(1)_REPORTFILE) >&3) ; exec 3>&- ; [ $$$${RC} -eq 0 ] || false)

$(1)-COPY_SHLIBS=true $$(foreach D,$$($(1)_all_SHLIBS),; cp -f $$(D) $$(dir $$($(1)_PROG)))

##
# simple version of the above (just running the program)
$(1)-run-simple: $(1)
	@$(ysr-display-banner) "* Testing $(1)\n"
	$$($(1)-COPY_SHLIBS)
	$$($(1)_RUNENV)
	$$($(1)_EXEC) $$($(1)_ARGS)

$(1)-debug: $(1)
	@[ "$(OTYPE)" == "DEBUG" ] || $(call ysr-display-interactive-warning,"'You are debugging yet OTYPE=$(OTYPE) (use $(MAKE) OTYPE=DEBUG instead).. symbols might not be available to the debugger.'")
	$$($(1)-COPY_SHLIBS)
	($$($(1)_RUNENV) ; gdb --args $$($(1)_EXEC) $$($(1)_ARGS))

$(1)-valgrind: $(1)
	$$($(1)-COPY_SHLIBS)
	($$($(1)_RUNENV) ; valgrind --smc-check=all --leak-check=full --show-reachable=yes $$(VALGRIND_ARGS) $$($(1)_PROG) $$($(1)_ARGS))

$(1)-depclean:
	rm -f $$($(1)_MKDEPALL)

$(1)-clean: $(1)-depclean
	rm -f $$($(1)_all_OBJS)

$(1)-showdep:
	@$(ysr-display-banner) $$($(1)_MKDEP)

clean: $(1)-clean
depclean: $(1)-depclean

.PHONY: $(1) $(1)-run $(1)-bundle $(1)-valgrind $(1)-debug $(1)-clean $(1)-depclean

-include $$($(1)_MKDEP)

all-progs: $$($(1)_PROG)

endef

PROGRAM_MK_WELCOME:=* Programs defined for this project:
PROGRAM_MK_WELCOME_RULES:=Use the following rules: <program-name>-<rule>

PROGRAM_MK_RULES:=run debug valgrind clean depclean showdep
PROGRAM_MK_DESCRIBE-build:=build the program
PROGRAM_MK_DESCRIBE-run:=run
PROGRAM_MK_DESCRIBE-debug:=debug
PROGRAM_MK_DESCRIBE-valgrind:=use the valgrind tool to find memory leaks
PROGRAM_MK_DESCRIBE-clean:=clean all objects and dependencies\n\t\t(may be useful in order to rebuild the program)
PROGRAM_MK_DESCRIBE-depclean:=clean the dependencies\n\t\t(may be useful in order to rebuild the program)

help-progs:
	@$(ysr-display-banner) "\n$(PROGRAM_MK_WELCOME)\n"
	@true $(foreach P,$(ALL_PROG_NAMES),&& $(ysr-display-banner) "\n\t$(P)")
	@$(ysr-display-banner) "\n\n$(PROGRAM_MK_WELCOME_RULES)"
	@$(ysr-display-banner) "\nWith rule amongst: $(PROGRAM_MK_RULES)\n"

help-rules-progs:
	@$(ysr-display-banner) "\n$(PROGRAM_MK_WELCOME_RULES)\n"
	@true $(foreach P,$(ALL_PROG_NAMES),&& $(ysr-display-banner) "\n\t$(P) -- $(PROGRAM_MK_DESCRIBE-build)" $(foreach R,$(PROGRAM_MK_RULES),&& $(ysr-display-banner) "\n\t$(P)-$(R) -- $(PROGRAM_MK_DESCRIBE-$(R))") && $(ysr-display-banner) "\n")

.PHONY: help-progs help-rules-progs clean depclean all-progs

endif
