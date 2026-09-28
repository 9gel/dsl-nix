{ config, pkgs, ... }:
{
  home.username = "dimsum";
  home.homeDirectory = "/home/dimsum";
  home.stateVersion = "26.05";

  home.packages = with pkgs; [
    direnv
    byobu
  ];

  programs.zsh = {
    enable = true;
    dotDir = config.home.homeDirectory;
    autocd = false;
    shellAliases = {
      grep = "grep --color=always";
      ls = "ls --color=always";
    };
    initContent = ''
      source ${config.home.homeDirectory}/.p10k.zsh
      # Byobu on an interactive login. NO_BYOBU=1 skips it.
      # Probe first. A wedged tmux ignores TERM, so use KILL.
      # Exit 0 or 1 means the server answered.
      if [ -z "$BYOBU_WINDOW" ] && [ -z "$TMUX" ] \
          && [ -z "$NO_BYOBU" ] && [ -t 0 ]; then
        ${pkgs.coreutils}/bin/timeout -s KILL 3 \
          ${pkgs.byobu}/bin/byobu-tmux list-sessions \
          >/dev/null 2>&1
        rc=$?
        if [ "$rc" = 0 ] || [ "$rc" = 1 ]; then
          exec ${pkgs.byobu}/bin/byobu
        else
          echo "byobu: tmux server wedged. plain shell." >&2
          echo "fix: kill -9 \$(pgrep -f 'tmux.*byobu')" >&2
          echo "or set NO_BYOBU=1" >&2
        fi
      fi
    '';
    plugins = [
      {
        name = "zsh-powerlevel10k";
        src = "${pkgs.zsh-powerlevel10k}/share/zsh-powerlevel10k/";
        file = "powerlevel10k.zsh-theme";
      }
    ];
    oh-my-zsh = {
      enable = true;
      plugins = [ "direnv" ];
    };
  };

  home.file.".p10k.zsh".source = ./p10k.zsh;

  dconf.settings."org/gnome/desktop/interface" = {
    monospace-font-name = "MesloLGS NF 11";
  };
}
