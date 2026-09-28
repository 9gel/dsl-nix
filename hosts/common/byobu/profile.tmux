source $BYOBU_PREFIX/share/byobu/profiles/tmux

set-option -g set-titles on
set-option -g set-titles-string '#(whoami)@#H - byobu (#S)'

set -g pane-border-status top
# Active border fg matches bg, so the title needs a light fg.
set -g pane-border-format "#[fg=$BYOBU_LIGHT,bold] #P: #T #[default]"

set-window-option -g window-status-current-style \
  bg=$BYOBU_DARK,fg=$BYOBU_LIGHT
set -g window-status-separator "#[fg=$COLOR_INVERSE_BG]│"
set -g window-status-format "#W#F"
set -g window-status-current-format \
  "#[dotted-underscore,bold]#[fg=$BYOBU_DARK,bg=$BYOBU_LIGHT]#W#F#[none]"
set -g window-status-activity-style none
set -g window-status-bell-style none
set -g message-style bg=$BYOBU_HIGHLIGHT,fg=$BYOBU_LIGHT
