ifndef target_rules_mk
target_rules_mk=1

include $(YSR.libdir)/functions/display.mk
include $(YSR.libdir)/templates/program.mk
include $(YSR.libdir)/templates/library.mk

help-prologue:
	@$(ysr-display-banner) "\nWelcome to project: $(YSR.project.name)"
	@$(ysr-display-banner) "\nFor a complete list of rules:\n\n\t$(MAKE) help-rules\n"
	@$(ysr-display-banner) "\nDocumentation may be found at $(YSR.project.dest.docs)/docs/api\n"

help:  help-prologue help-progs help-libs

help-rules: help-rules-progs help-rules-libs

.PHONY: help help-prologue help-rules
.NOTPARALLEL: help help-rules

endif
