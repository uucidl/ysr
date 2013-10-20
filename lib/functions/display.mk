ifndef ysr_lib_display_mk
ysr_lib_display_mk=1

ifneq (,$(findstring xterm,$(TERM)))
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

# does echo support the -e flag?
ifeq ($(shell echo -e 2> /dev/null),-e)
ysr-echo-e:=echo
else
ysr-echo-e:=echo -e
endif

# left side $(1) is the text to display
ysr-display-left=($(YSR_DISPLAY_DISABLED) || $(ysr-echo-e) "$(YSR_DISPLAY_ANSI_COLORSET)"$(1)" ... $(YSR_DISPLAY_ANSI_COLORRESET)")

# right side
ifneq ($(YSR_DISPLAY_ANSI_MOVETOEND),)
ysr-display-right=($(YSR_DISPLAY_DISABLED) || $(ysr-echo-e) "$(YSR_DISPLAY_ANSI_COLORSET)$(YSR_DISPLAY_ANSI_MOVETOEND)"$(1)"$(YSR_DISPLAY_ANSI_COLORRESET)")
else
ysr-display-right=true
endif

ysr-display-interactive-warning=(echo && echo "WARNING: $(1)" && echo && echo "Waiting 3 seconds." && sleep 3)
ysr-display-banner=$(ysr-echo-e) $(1)

endif
