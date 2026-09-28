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
- Byobu autostarts in interactive zsh. `NO_BYOBU=1` skips it.

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

## pi5

Raspberry Pi 5 at `pi5.local`. Stock NVMe image. Hostname stays
`pi5`. User in this flake is `dimsum` (same zsh, powerlevel10k,
and Byobu as jukebox). No desktop and no Spotify.

The live image still logs in as `nigel`. A switch adds `dimsum`
and leaves the `nigel` account in place.

On the Pi, from a checkout:

```sh
sudo nixos-rebuild switch --flake .#pi5
```
