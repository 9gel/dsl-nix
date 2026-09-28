# Edit this configuration file to define what should be installed on
# your system.  Help is available in the configuration.nix(5) man page
# and in the NixOS manual (accessible by running ‘nixos-help’).

{ config, pkgs, ... }:

{
  imports =
    [ # Include the results of the hardware scan.
      ./hardware-configuration.nix
    ];

  # Bootloader.
  boot.loader.systemd-boot.enable = true;
  boot.loader.efi.canTouchEfiVariables = true;

  networking.hostName = "jukebox";
  # networking.wireless.enable = true;
  # Enables wireless support via wpa_supplicant.

  # Configure network proxy if necessary
  # networking.proxy.default = "http://user:password@proxy:port/";
  # networking.proxy.noProxy = "127.0.0.1,localhost,internal.domain";

  # Enable networking
  networking.networkmanager.enable = true;

  # Set your time zone.
  time.timeZone = "Asia/Hong_Kong";

  # Select internationalisation properties.
  i18n.defaultLocale = "en_HK.UTF-8";

  # Enable the X11 windowing system.
  services.xserver.enable = true;

  # Enable the GNOME Desktop Environment.
  services.displayManager.gdm.enable = true;
  services.desktopManager.gnome.enable = true;
  services.displayManager.autoLogin.enable = true;
  services.displayManager.autoLogin.user = "dimsum";

  # Configure keymap in X11
  services.xserver.xkb = {
    layout = "us";
    variant = "";
  };

  # Enable CUPS to print documents.
  services.printing.enable = true;

  # Enable sound with pipewire.
  services.pulseaudio.enable = false;
  security.rtkit.enable = true;
  services.pipewire = {
    enable = true;
    alsa.enable = true;
    alsa.support32Bit = true;
    pulse.enable = true;
    # If you want to use JACK applications, uncomment this
    #jack.enable = true;

    # use the example session manager (no others are packaged yet
    # so this is enabled by default, no need to redefine it in
    # your config for now)
    #media-session.enable = true;
  };

  # Enable touchpad support (enabled default in most desktopManager).
  services.libinput.enable = true;

  fonts.packages = [ pkgs.meslo-lgs-nf ];

  programs.dconf.enable = true;
  programs.zsh.enable = true;

  # Define a user account. Don't forget to set a password with ‘passwd’.
  users.users.dimsum = {
    isNormalUser = true;
    description = "Dim Sum Labs";
    extraGroups = [ "networkmanager" "wheel" ];
    packages = with pkgs; [
    ];
    shell = pkgs.zsh;
  };

  # Install firefox.
  programs.firefox.enable = true;

  # Allow unfree packages
  nixpkgs.config.allowUnfree = true;

  nix.settings.experimental-features = [
    "nix-command"
    "flakes"
  ];

  # List packages installed in system profile. To search, run:
  # $ nix search wget
  environment.systemPackages = with pkgs; [
    # Nano is also installed by default.
    vim
    wget
    git
    xscreensaver
    # AirPlay receiver (also run as a user service below).
    shairport-sync
    # Spotify Connect receiver (also a user service below).
    librespot
    # Graphical EQ / effects on the default sink (autostarted).
    easyeffects
    spotify
  ];

  ##
  # AirPlay + Spotify Connect network receivers.
  #
  # Both run as *user* services in dimsum's PipeWire session (GNOME autologin),
  # so they route to whatever the default sink is — the Mackie
  # BIG KNOB STUDIO+ — exactly like the browser does. They are
  # discovered on the LAN over mDNS via Avahi (AirPlay) and
  # librespot's own libmdns (Spotify).
  ##

  # mDNS/zeroconf so AirPlay and Spotify endpoints are
  # discoverable on the LAN.
  services.avahi = {
    enable = true;
    nssmdns4 = true;
    publish = {
      enable = true;
      userServices = true;
    };
    openFirewall = true; # UDP 5353
  };

  # AirPlay: RTSP on TCP 5000, audio/control/timing on UDP 6001-6011.
  # Spotify Connect: librespot advertises its control server on TCP 5354.
  networking.firewall.allowedTCPPorts = [ 5000 5354 ];
  networking.firewall.allowedUDPPortRanges = [
    { from = 6001; to = 6011; }
  ];

  # AirPlay receiver -> user PipeWire, shown in the AirPlay picker as "dimsum".
  systemd.user.services.shairport-sync = {
    description = "shairport-sync AirPlay receiver";
    wantedBy = [ "default.target" ];
    after = [ "pipewire.service" "wireplumber.service" ];
    serviceConfig = {
      # The stable-channel shairport-sync is built without the pulse/pipewire
      # backends, so use ALSA aimed at the "default" PCM, which is PipeWire's
      # ALSA bridge (services.pipewire.alsa.enable) -> routes to the Big Knob.
      ExecStart =
        "${pkgs.shairport-sync}/bin/shairport-sync"
        + " -a dimsum -o alsa -- -d default";
      Restart = "on-failure";
      RestartSec = 3;
    };
  };

  # Spotify Connect receiver -> user PipeWire (via pipewire-pulse), shown in the
  # Spotify app's device list as "dimsum".
  systemd.user.services.librespot = {
    description = "librespot Spotify Connect receiver";
    wantedBy = [ "default.target" ];
    after = [ "pipewire.service" "wireplumber.service" ];
    serviceConfig = {
      ExecStart =
        "${pkgs.librespot}/bin/librespot"
        + " --name dimsum --backend pulseaudio"
        + " --bitrate 320 --zeroconf-port 5354"
        + " --initial-volume 50";
      Restart = "on-failure";
      RestartSec = 3;
    };
  };

  # EasyEffects: graphical EQ + effects processor. Runs as a background user
  # service (no window) that inserts a filter chain on the default sink, so it
  # processes everything going to the Big Knob — local apps, AirPlay and
  # Spotify alike. Open the "EasyEffects" GUI to build/tweak the EQ preset; the
  # service reapplies it automatically. Autostarts on login (GNOME autologin).
  systemd.user.services.easyeffects = {
    description = "EasyEffects audio effects (background service)";
    wantedBy = [ "graphical-session.target" ];
    after = [
      "pipewire.service"
      "wireplumber.service"
      "graphical-session.target"
    ];
    # No window. offscreen skips the X11 plugin, which aborts
    # the service when it starts before the GNOME display is up
    # and then leaves Spotify streams stuck.
    environment.QT_QPA_PLATFORM = "offscreen";
    serviceConfig = {
      ExecStart =
        "${pkgs.easyeffects}/bin/easyeffects"
        + " --gapplication-service";
      Restart = "on-failure";
      RestartSec = 3;
    };
  };

  # Some programs need SUID wrappers, can be configured further or are
  # started in user sessions.
  # programs.mtr.enable = true;
  # programs.gnupg.agent = {
  #   enable = true;
  #   enableSSHSupport = true;
  # };

  # List services that you want to enable:

  # Enable the OpenSSH daemon.
  services.openssh.enable = true;

  # Open ports in the firewall.
  # networking.firewall.allowedTCPPorts = [ ... ];
  # networking.firewall.allowedUDPPorts = [ ... ];
  # Or disable the firewall altogether.
  # networking.firewall.enable = false;

  # This value determines the NixOS release from which the default
  # settings for stateful data, like file locations and database versions
  # on your system were taken. It‘s perfectly fine and recommended to leave
  # this value at the release version of the first install of this system.
  # Before changing this value read the documentation for this option
  # (e.g. man configuration.nix or on https://nixos.org/nixos/options.html).
  system.stateVersion = "26.05"; # Did you read the comment?

}
