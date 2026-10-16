{
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
    ];
  };
}
