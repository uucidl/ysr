ifndef target_rules_mk
target_rules_mk=1

include $(YSR.libdir)/templates/program.mk
include $(YSR.libdir)/templates/library.mk

help-prologue:
	@$(ysr-display-banner) "Welcome to project: $(YSR.project.name)"
	@echo
	@$(ysr-display-banner) "For a complete list of rules:\\n\\t$(MAKE) help-rules"
	@echo
	@$(ysr-display-banner) "Documentation may be found at $(YSR.project.dest.docs)/docs/api"
	@echo

help:  help-prologue help-progs help-libs

help-rules: help-rules-progs help-rules-libs

.PHONY: help help-prologue help-rules
.NOTPARALLEL: help help-rules

endif
