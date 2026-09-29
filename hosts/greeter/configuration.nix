# Pi 5, root on the NVMe this image is flashed to.
# Stock NixOS U-Boot cannot continue boot from NVMe. The firmware
# "kernel" bootloader can. First boot grows the root partition.
{
  lib,
  pkgs,
  nixos-raspberrypi,
  ...
}:
let
  userSshKey = lib.removeSuffix "\n" (
    builtins.readFile ./dimsum.pub
  );
in
{
  imports = with nixos-raspberrypi.nixosModules; [
    raspberry-pi-5.base
    # No page-size-16k. That build has no cache.
    sd-image
  ] ++ [
    ./bao.nix
  ];

  boot.kernelPackages = pkgs.linuxPackages_rpi5;
  boot.loader.raspberry-pi.firmwarePackage = pkgs.raspberrypifw;
  boot.loader.raspberry-pi.bootloader = "kernel";

  # mdadm in the initrd breaks Pi firmware boot.
  boot.swraid.enable = lib.mkForce false;

  sdImage.compressImage = false;
  boot.zfs.forceImportRoot = false;

  networking.hostName = "greeter";
  networking.useDHCP = lib.mkDefault true;
  time.timeZone = "Asia/Hong_Kong";

  # PCIe is off unless a HAT+ EEPROM enables it. Turn the root
  # port on so an NVMe adapter enumerates. Official docs:
  # dtparam=pciex1.
  hardware.raspberry-pi.config.all.base-dt-params.pciex1.enable =
    true;

  services.openssh = {
    enable = true;
    openFirewall = true;
    settings.PermitRootLogin = "prohibit-password";
    settings.PasswordAuthentication = false;
  };

  services.avahi = {
    enable = true;
    nssmdns4 = true;
    publish.enable = true;
    publish.addresses = true;
    openFirewall = true;
  };

  programs.zsh.enable = true;

  users.users.dimsum = {
    isNormalUser = true;
    extraGroups = [ "wheel" ];
    openssh.authorizedKeys.keys = [ userSshKey ];
    shell = pkgs.zsh;
  };
  users.users.root.openssh.authorizedKeys.keys = [ userSshKey ];

  security.sudo.wheelNeedsPassword = false;
  nix.settings.trusted-users = [
    "root"
    "dimsum"
  ];
  nix.settings.experimental-features = [
    "nix-command"
    "flakes"
  ];
  # One compiler. A parallel kernel build OOM'd this board.
  nix.settings.max-jobs = 1;
  nix.settings.cores = 1;
  systemd.services.nix-daemon.serviceConfig = {
    MemoryHigh = "50%";
    MemoryMax = "70%";
  };

  environment.systemPackages = with pkgs; [
    raspberrypi-eeprom
  ];

  system.stateVersion = "26.05";
}
