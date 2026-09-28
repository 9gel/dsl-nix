{ config, pkgs, ... }:
{
  home.username = "dimsum";
  home.homeDirectory = "/home/dimsum";
  home.stateVersion = "26.05";

  home.packages = [ pkgs.direnv ];

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
