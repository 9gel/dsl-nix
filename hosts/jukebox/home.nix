{ pkgs, ... }:

{
  xdg.desktopEntries.uxplay = {
    name = "UxPlay — dimsum-av";
    comment = "Start AirPlay screen mirroring";
    icon = "video-display";
    categories = [ "AudioVideo" ];
    terminal = false;
    exec = toString (pkgs.writeShellScript "uxplay-launcher" ''
      if ${pkgs.systemd}/bin/systemctl --user start uxplay.service; then
        ${pkgs.libnotify}/bin/notify-send "UxPlay is ready" \
          "Select dimsum-av in Screen Mirroring on your Apple device."
      else
        ${pkgs.libnotify}/bin/notify-send -u critical \
          "UxPlay failed to start" \
          "Check systemctl --user status uxplay in a terminal."
        exit 1
      fi
    '');
  };

  dconf.settings."org/gnome/desktop/interface" = {
    monospace-font-name = "MesloLGS NF 11";
  };

  # Dash (left bar) pins. This replaces the whole list.
  dconf.settings."org/gnome/shell" = {
    favorite-apps = [
      "firefox.desktop"
      "org.gnome.Nautilus.desktop"
      "spotify.desktop"
      "com.bitwig.BitwigStudio.desktop"
      "com.github.wwmm.easyeffects.desktop"
      "uxplay.desktop"
    ];
  };
}
