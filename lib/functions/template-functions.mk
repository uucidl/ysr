ifndef ysr_lib_template_functions
ysr_lib_template_functions=1

## PRIVATE Implementation functions, used by program.mk and library.mk

##
# recursive search for dependencies, and flatten them into one single
# list (this is to find all the modules required by modules the
# program itself required.

ysr-prv-walk-requires=$(sort $(foreach s,$($(1)_REQUIRES),$(s) $(call ysr-prv-walk-requires,$(s))))

##
# verify that the dependency has been loaded
#   $(1) token representing the depencency (see _REQUIRES variable)
#   $(2) who asked for it
#   $(3) what runtime environment we are building for (c, c++)
ysr-prv-requires=$(if $(subst 0,,$($(1))),,$(error missing dependency: $(1) needed by $(2)))\
	 $(if $($(1)_RUNTIME),\
		$(if $(filter $(3),$($(1)_RUNTIME)),,$(error invalid runtime: $(2) needs $(1) which needs a $($(1)_RUNTIME) program -- got $(3) -- )),)

##
# Returns a series of references to variables of the form <prefix>_<suffix> where
# $(1) is suffix
# $(2) are the prefixes
ysr-prefixing-references=$(foreach PREFIX,$(2),$($(PREFIX)_$(1)))

##
# test each required dependency in the given list
# $(1) list
# $(2) who asked
# $(3) which runtime
ysr-prv-requires-all=$(foreach F,$(1),$(call ysr-prv-requires,$(F),$(2),$(3)))

endif
