source $BYOBU_PREFIX/share/byobu/keybindings/f-keys.tmux

# F1 sits next to ESC.
unbind-key -n F1

bind-key -n F5 display-message "Reloading tmuxrc" \; \
  source $BYOBU_PREFIX/share/byobu/profiles/tmuxrc
unbind-key -n M-F5
# C-S-F5 runs byobu-select-profile and resets these colors.
unbind-key -n C-S-F5
bind-key -n M-S-F8 \
  run-shell "byobu-layout restore three-vertical"

# C-a is beginning-of-line in the shell, not a tmux prefix.
set -g prefix F12
set -gu prefix2
unbind-key C-a
unbind-key -n C-a
