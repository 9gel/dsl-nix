# Touch kiosk. HDMI-A-1 is a 2560x1440 panel mounted sideways:
# an unrotated console has its top on the physical right.
# screenTransform 1 is 90 degrees clockwise. Use 3 if the
# picture is still sideways. The digitizer is the USB WDT AC270.
{ pkgs, ... }:
let
  screenTransform = 1;
  logoPng = pkgs.runCommand "dsl-logo-bao.png" {
    nativeBuildInputs = [ pkgs.librsvg ];
  } ''
    rsvg-convert -w 768 \
      ${./dsl-logo-bao.svg} -o "$out"
  '';
  asapVar = pkgs.fetchurl {
    url = "https://raw.githubusercontent.com/google/fonts/"
      + "main/ofl/asap/Asap%5Bwdth%2Cwght%5D.ttf";
    name = "Asap-Variable.ttf";
    hash = "sha256-e/KbyrcvfQDeYA6WSj/CBmIAUNhEjd2XaCI3jQ/hgCg=";
  };
  # Asap ships as a variable font. Weight 400 is Regular.
  asapRegular = pkgs.runCommand "Asap-Regular.ttf" {
    nativeBuildInputs = [ pkgs.python3Packages.fonttools ];
  } ''
    fonttools varLib.instancer \
      ${asapVar} wght=400 wdth=100 \
      -o "$out"
  '';
  pythonEnv = pkgs.python3.withPackages (ps: [
    ps.pygame
    ps.segno
  ]);
  dslBao = pkgs.writeShellScriptBin "dsl-bao" ''
    export DSL_BAO_LOGO=${logoPng}
    export DSL_BAO_FONT=${asapRegular}
    export SDL_VIDEODRIVER=wayland
    exec ${pythonEnv}/bin/python3 ${./bao.py}
  '';
  # Hyprland 0.55 reads Lua. screenTransform rotates the
  # picture and the touchscreen together.
  hyprCfg = pkgs.writeText "hyprland.lua" ''
    local screenTransform = ${toString screenTransform}

    hl.monitor({
        output = "HDMI-A-1",
        mode = "preferred",
        position = "auto",
        scale = 1,
        transform = screenTransform,
    })
    hl.monitor({
        output = "HDMI-A-2",
        disabled = true,
    })

    hl.env("AQ_DRM_DEVICES", "/dev/dri/card1")
    hl.env("AQ_NO_HARDWARE_CURSORS", "1")
    hl.env("SDL_VIDEODRIVER", "wayland")

    hl.config({
        general = {
            gaps_in = 0,
            gaps_out = 0,
            border_size = 0,
        },
        decoration = {
            rounding = 0,
            shadow = { enabled = false },
            blur = { enabled = false },
        },
        animations = { enabled = false },
        misc = {
            force_default_wallpaper = 0,
            disable_hyprland_logo = true,
        },
        cursor = {
            invisible = true,
            no_hardware_cursors = 1,
        },
        input = {
            touchdevice = {
                transform = screenTransform,
                output = "HDMI-A-1",
            },
        },
    })

    hl.on("hyprland.start", function ()
        hl.exec_cmd(
            "${pkgs.bash}/bin/bash -c 'while true; do "
            .. "${dslBao}/bin/dsl-bao; sleep 1; done'"
        )
    end)
  '';
in
{
  hardware.graphics.enable = true;
  boot.kernelParams = [ "consoleblank=0" ];

  programs.hyprland = {
    enable = true;
    withUWSM = false;
  };

  users.users.dimsum.extraGroups = [
    "video"
    "render"
    "input"
  ];

  environment.systemPackages = [ dslBao ];

  # default_session.user in the greetd module is mkDefault
  # "greeter". dimsum overrides that. No initial_session, so
  # greetd respawns Hyprland when the process exits.
  services.greetd = {
    enable = true;
    settings.default_session = {
      user = "dimsum";
      command = "/run/wrappers/bin/Hyprland --config ${hyprCfg}";
    };
  };
}
