# dsl-nix

NixOS definitions for Dim Sum Lab machines.

Every host imports `hosts/common/baseline.nix`. Put packages
that belong on all machines there.

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
- Suspend is blocked while a PipeWire stream is active.

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

## greeter

Raspberry Pi 5, hostname `greeter` (`greeter.local`). Same Pi
platform as `~/Code/pi5-nix`: nixos-raspberrypi's nixpkgs,
firmware bootloader, no 16k page size. User is `dimsum`, with
the same zsh, powerlevel10k, and Byobu as jukebox. No Spotify.

HDMI-A-1 is a sideways 2560x1440 touch panel (WDT AC270).
An unrotated console has its top on the physical right.
greetd starts `dsl-bao` on tty1. That program is the DRM
client of `/dev/dri/card1` and draws with OpenGL ES 2.
`screenTransform` in `hosts/greeter/display.nix` is `1`: the
portrait scene is turned onto the landscape mode, and touch
uses the inverse. Use `3` if the picture is still sideways.
The screen is the Dim Sum Labs bao
(`hosts/greeter/dsl-logo-bao.svg`, from
dimsumlabs/dimsumlabs-graphics). It drifts, and a tap makes
it jump. Top right is a QR code for `https://t.me/dimsumlabs`,
about a fifth of the screen height. Left of it, in Asap
Regular: "Joins us on Telegram! Scan the code on the right."
A tap on the bao plays boing, ouch, "don't touch me!", or
"Stop it!" at random. Bottom right, half the screen width:
"Don't touch the bao!" greetd autologins `dimsum` on tty1.
SSH stays up.
If the session misbehaves: `sudo systemctl stop greetd`.

Motion check, no display:

```sh
cc -std=c11 -Wall -Wextra -Werror -DDSL_BAO_HEADLESS \
  -o /tmp/bao-check hosts/greeter/bao.c -lm \
  && /tmp/bao-check --check
rm /tmp/bao-check
```

Wav and QR writers:

```sh
python3 hosts/greeter/bao.py --check
```

The live image still logs in as `nigel`. A switch adds `dimsum`
and leaves the `nigel` account in place.

On the Pi, from a checkout. The cache flag is required. This
flake does not set it, same as `pi5-nix`:

```sh
sudo nixos-rebuild switch --flake .#greeter \
  --option extra-substituters \
    https://nixos-raspberrypi.cachix.org \
  --option extra-trusted-public-keys \
    nixos-raspberrypi.cachix.org-1:4iMO9LXa8BqhU+Rpg6LQKiGa2lsNh/j2oiYLNOQ5sPI=
```
