ifndef target_rules_mk
target_rules_mk=1

include $(YSR.libdir)/templates/program.mk
include $(YSR.libdir)/templates/library.mk

help: help-progs help-libs
	@$(echo-e) "For a complete list of rules:\\n\\t$(MAKE) help-rules"
	@echo
	@$(echo-e) "Documentation may be found at $(YSR.project.dest.docs)/docs/api"
	@echo

help-rules: help-rules-progs help-rules-libs

.PHONY: help help-rules

endif