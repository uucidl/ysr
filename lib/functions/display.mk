ifndef scripts_display_mk
scripts_display_mk=1

ifneq (,$(findstring xterm,$(TERM)))
# the terminal is a xterm like: use colors and positionning
DISPLAY_COLS:=73
DISPLAY_ANSI_COLORSET:=\x1B[1m
DISPLAY_ANSI_COLORRESET:=\x1B[0m
DISPLAY_ANSI_MOVETOEND:=\x1B[A\x1B[$(DISPLAY_COLS)G
endif

DISPLAY_DISABLED:=false

##
# display functions
#

# does echo support the -e flag?
ifeq ($(shell echo -e 2> /dev/null),-e)
echo-e:=echo
else
echo-e:=echo -e
endif

# left side $(1) is the text to display
display-left=($(DISPLAY_DISABLED) || $(echo-e) "$(DISPLAY_ANSI_COLORSET)"$(1)" ... $(DISPLAY_ANSI_COLORRESET)")

# right side
ifneq ($(DISPLAY_ANSI_MOVETOEND),)
display-right=($(DISPLAY_DISABLED) || $(echo-e) "$(DISPLAY_ANSI_COLORSET)$(DISPLAY_ANSI_MOVETOEND)"$(1)"$(DISPLAY_ANSI_COLORRESET)")
else
display-right=true
endif

display-interactive-warning=(echo && echo "WARNING: $(1)" && echo && echo "Waiting 3 seconds." && sleep 3)

endif
