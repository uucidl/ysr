ifndef scripts_template_functions
scripts_template_functions=1

## PRIVATE Implementation functions, used by program.mk and library.mk

##
# recursive search for dependencies, and flatten them into one single
# list (this is to find all the modules required by modules the
# program itself required.

walk-requires=$(foreach s,$($(1)_REQUIRES),$(s) $(call walk-requires,$(s)))

##
# verify that the dependency has been loaded
#   $(1) token representing the depencency (see _REQUIRES variable)
#   $(2) who asked for it
#   $(3) what runtime environment we are building for (c, c++)
requires=$(if $(subst 0,,$($(1))),,$(error missing dependency: $(1) needed by $(2)))\
	 $(if $($(1)_RUNTIME),\
		$(if $(filter $(3),$($(1)_RUNTIME)),,$(error invalid runtime: $(2) needs $(1) which needs a $($(1)_RUNTIME) program -- got $(3) -- )),)

##
# Returns a series of references to variables of the form <prefix>_<suffix> where
# $(1) is suffix
# $(2) are the prefixes
prefixing-references=$(foreach PREFIX,$(2),$($(PREFIX)_$(1)))

endif