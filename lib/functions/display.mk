ifndef ysr_lib_display_mk
ysr_lib_display_mk=1

ifneq (,$(findstring xterm,$(TERM))$(findstring cygwin,$(TERM)))
# the terminal is a xterm like: use colors and positionning
YSR_DISPLAY_COLS:=73
YSR_DISPLAY_ANSI_COLORSET:=\x1B[1m
YSR_DISPLAY_ANSI_COLORRESET:=\x1B[0m
YSR_DISPLAY_ANSI_MOVETOEND:=\x1B[A\x1B[$(YSR_DISPLAY_COLS)G
endif

YSR_DISPLAY_DISABLED:=false

##
# display functions
#

ysr-printf=printf

# left side $(1) is the text to display
ysr-display-left=($(YSR_DISPLAY_DISABLED) || $(ysr-printf) "$(YSR_DISPLAY_ANSI_COLORSET)"$(1)" ... $(YSR_DISPLAY_ANSI_COLORRESET)\n")

# right side
ifneq ($(YSR_DISPLAY_ANSI_MOVETOEND),)
ysr-display-right=($(YSR_DISPLAY_DISABLED) || $(ysr-printf) "$(YSR_DISPLAY_ANSI_COLORSET)$(YSR_DISPLAY_ANSI_MOVETOEND)"$(1)"$(YSR_DISPLAY_ANSI_COLORRESET)\n")
else
ysr-display-right=true
endif

ysr-display-interactive-warning=(printf "\nWARNING: $(1)\n\nWaiting 3 seconds.\n" && sleep 3)
ysr-display-banner=$(ysr-printf)

endif
