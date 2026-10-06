# 3D touch kiosk: the bao chip from model_for_dslan.blend paces
# a red room corner. bao-3d-export.py makes the .glb files.
# Same panel, touch, QR and sounds as bao.nix. Import one of
# the two, not both.
#
# HDMI-A-1 is a 2560x1440 panel mounted sideways:
# an unrotated console has its top on the physical right.
# screenTransform 1 turns the portrait scene onto that mode.
# Use 3 if the picture is still sideways. Touch uses the
# inverse of the same number. The digitizer is the USB WDT
# AC270. dsl-bao-3d is the DRM client of /dev/dri/card1.
{ pkgs, lib, ... }:
let
  screenTransform = 1;
  models = pkgs.runCommand "dsl-bao-3d-models" { } ''
    mkdir -p "$out"
    cp ${./bao-3d-body.glb} "$out/bao-3d-body.glb"
    cp ${./bao-3d-legs-a.glb} "$out/bao-3d-legs-a.glb"
    cp ${./bao-3d-legs-b.glb} "$out/bao-3d-legs-b.glb"
    cp ${./bao-3d-hind-1.glb} "$out/bao-3d-hind-1.glb"
    cp ${./bao-3d-hind-2.glb} "$out/bao-3d-hind-2.glb"
    cp ${./bao-3d-hind-3.glb} "$out/bao-3d-hind-3.glb"
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
  speak = pkgs.espeak-ng.override {
    mbrolaSupport = false;
  };
  sounds = pkgs.runCommand "dsl-bao-sounds" {
    nativeBuildInputs = [ speak pkgs.python3 ];
  } ''
    mkdir -p "$out"
    espeak-ng -w "$out/ouch.wav" -s 150 -p 62 "ouch"
    espeak-ng -w "$out/dont-touch-me.wav" \
      -s 138 -p 48 "don't touch me!"
    espeak-ng -w "$out/stop-it.wav" \
      -s 160 -p 75 "Stop it!"
    espeak-ng -w "$out/huh.wav" -v en+f3 -s 155 -p 90 "huh?"
    python3 ${./bao.py} --write-boing "$out/boing.wav"
  '';
  qrPng = pkgs.runCommand "dsl-bao-qr.png" {
    nativeBuildInputs = [ pythonEnv ];
  } ''
    export SDL_VIDEODRIVER=dummy
    python3 ${./bao.py} --write-qr "$out"
  '';
  # Desktop raylib is GLFW. This one is the Pi's display:
  # kernel DRM, EGL, OpenGL ES 2. DSL_DRM_CARD picks card1.
  raylibDrm = pkgs.stdenv.mkDerivation {
    pname = "raylib-drm";
    version = "6.0";
    src = pkgs.fetchFromGitHub {
      owner = "raysan5";
      repo = "raylib";
      rev = "dbc56a87da87d973a9c5baa4e7438a9d20121d28";
      hash = "sha256-8+6MDTMc7Spix4ndAUzp51Q5iWcl7pQmyXuV2RutnOk=";
    };
    patches = [ ./raylib-drm-card.patch ];
    nativeBuildInputs = [
      pkgs.cmake
      pkgs.pkg-config
    ];
    # libGL is the libglvnd dispatch library and its headers.
    # The Pi's Mesa drivers come from /run/opengl-driver.
    # xf86drm.h includes <drm.h>, which lives in include/libdrm.
    buildInputs = [
      pkgs.libdrm
      pkgs.libgbm
      pkgs.libGL
      pkgs.alsa-lib
    ];
    env.NIX_CFLAGS_COMPILE = "-I${pkgs.libdrm.dev}/include/libdrm";
    # DRM's default graphics API is OpenGL ES 2. A value with a
    # space is split by the Nix cmake hook, so leave the default.
    cmakeFlags = [
      "-DPLATFORM=DRM"
      "-DBUILD_EXAMPLES=OFF"
      "-DBUILD_SHARED_LIBS=ON"
    ];
  };
  baoBin = pkgs.stdenv.mkDerivation {
    pname = "dsl-bao-3d";
    version = "0";
    dontUnpack = true;
    dontConfigure = true;
    buildInputs = [ raylibDrm pkgs.alsa-lib ];
    nativeBuildInputs = [ pkgs.pkg-config ];
    buildPhase = ''
      $CC -O2 -std=c11 -Wall -Wextra -o dsl-bao-3d ${./bao-3d.c} \
        -DDSL_SCREEN_TURN=${toString screenTransform} \
        -DDSL_BAO_MODELS=\"${models}\" \
        -DDSL_BAO_FONT=\"${asapRegular}\" \
        -DDSL_BAO_QR=\"${qrPng}\" \
        -DDSL_BAO_SOUNDS=\"${sounds}\" \
        $(pkg-config --cflags --libs raylib) \
        -lasound -lm
    '';
    doCheck = true;
    checkPhase = "./dsl-bao-3d --check";
    installPhase = ''
      mkdir -p $out/bin
      install -m 755 dsl-bao-3d $out/bin/dsl-bao-3d-bin
    '';
  };
  # MediaPipe palm detector, Apache-2.0, CPU only.
  palmOnnx = pkgs.fetchurl {
    url = "https://huggingface.co/opencv/"
      + "palm_detection_mediapipe/resolve/main/"
      + "palm_detection_mediapipe_2023feb.onnx";
    hash = "sha256-eP9Rw4SWt/yLjr22zIwauwL6bDhCfGhIJUzaulf8znw=";
  };
  # CPU watcher. The bao keeps the display GPU.
  waveBin = pkgs.stdenv.mkDerivation {
    pname = "dsl-bao-wave";
    version = "0";
    dontUnpack = true;
    dontConfigure = true;
    buildInputs = [ pkgs.libjpeg pkgs.onnxruntime ];
    nativeBuildInputs = [ pkgs.pkg-config ];
    buildPhase = ''
      $CC -O2 -std=c11 -Wall -Wextra -o dsl-bao-wave ${./bao-wave.c} \
        -DDSL_BAO_PALM=\"${palmOnnx}\" \
        -I${lib.getDev pkgs.onnxruntime}/include \
        $(pkg-config --cflags --libs libjpeg) -lonnxruntime -lm
    '';
    doCheck = true;
    checkPhase = "./dsl-bao-wave --check";
    installPhase = ''
      mkdir -p $out/bin
      install -m 755 dsl-bao-wave $out/bin/dsl-bao-wave
    '';
  };
  dslBao = pkgs.writeShellScriptBin "dsl-bao-3d" ''
    export DSL_DRM_CARD=/dev/dri/card1
    export LD_LIBRARY_PATH=${lib.makeLibraryPath [ pkgs.alsa-lib ]}
    if [ -d /run/opengl-driver/lib ]; then
      export LD_LIBRARY_PATH=/run/opengl-driver/lib:$LD_LIBRARY_PATH
      export LIBGL_DRIVERS_PATH=/run/opengl-driver/lib/dri
    fi
    vendor=/run/opengl-driver/share/glvnd/egl_vendor.d
    if [ -d "$vendor" ]; then
      export __EGL_VENDOR_LIBRARY_DIRS=$vendor
    fi
    exec ${baoBin}/bin/dsl-bao-3d-bin
  '';
  # greetd runs this. The picture loop stays up if the bao
  # exits. The watcher retries the camera on its own.
  baoSession = pkgs.writeShellScript "dsl-bao-3d-session" ''
    (
      while true; do
        ${waveBin}/bin/dsl-bao-wave || true
        ${pkgs.coreutils}/bin/sleep 2
      done
    ) &
    while true; do
      ${dslBao}/bin/dsl-bao-3d || true
      ${pkgs.coreutils}/bin/sleep 1
    done
  '';
in
{
  hardware.graphics.enable = true;
  boot.kernelParams = [ "consoleblank=0" ];

  services.pipewire = {
    enable = true;
    alsa.enable = true;
    pulse.enable = true;
  };
  users.users.dimsum.extraGroups = [
    "audio"
    "video"
    "render"
    "input"
  ];

  environment.systemPackages = [ dslBao waveBin ];
  system.build.dslBao = dslBao;

  # default_session.user in the greetd module is mkDefault
  # "greeter". dimsum overrides that. The loop restarts the
  # picture if it exits. SSH stays up if greetd stops.
  services.greetd = {
    enable = true;
    settings.default_session = {
      user = "dimsum";
      command = "${baoSession}";
    };
  };
}
