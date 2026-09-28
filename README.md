# dsl-nix

NixOS definitions for Dim Sum Lab machines.

## jukebox

Laptop at `172.31.3.100`, user `dimsum`. Config copied from the
live `/etc/nixos` (channel install, hostname `nixos`,
`26.05pre-git`), then:

- `networking.hostName` set to `jukebox`
- `spotify` added to `environment.systemPackages`
- flakes enabled for later rebuilds
- touchpad option is `services.libinput.enable`
- Home Manager for `dimsum`: zsh, oh-my-zsh, powerlevel10k,
  and `~/.p10k.zsh` copied from this host's home-manager

This repo does not switch the live machine.

On the laptop, as root, from a checkout:

```sh
sudo nixos-rebuild switch --flake .#jukebox \
  --option experimental-features 'nix-command flakes'
```

After that switch, flakes stay on:

```sh
sudo nixos-rebuild switch --flake .#jukebox
```

AirPlay and Spotify Connect still advertise as `dimsum`.

Check without building:

```sh
nix eval --raw \
  .#nixosConfigurations.jukebox.config.networking.hostName
```
