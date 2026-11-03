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

AirPlay receivers coexist: Shairport Sync advertises as
`dimsum-audio` for audio only; UxPlay advertises as `dimsum-av`
for screen mirroring with audio. UxPlay starts with the GNOME
session and uses TCP/UDP 7000–7002, separate from Shairport
Sync. Both send audio through the default PipeWire output.
Spotify Connect still advertises as `dimsum`.

After switching, log out and back in to start the new graphical
session service. From an Apple device on the same LAN, select
`dimsum-av` under Screen Mirroring, or `dimsum-audio` for audio.
The pinned **UxPlay — dimsum-av** launcher starts the receiver
and shows a notification. It reuses the service, so repeated
clicks do not launch competing receivers. The video window
appears only when a device sends screen mirroring.
Check receiver status in a terminal on the laptop:

```sh
systemctl --user status shairport-sync uxplay
```

UxPlay uses native Wayland video output and requests 1024×768
for the projector. The sender controls the final stream size
and aspect ratio; reconnect after changing this setting.
Connection diagnostics and GStreamer warnings are available with:

```sh
journalctl --user -u uxplay -f
```

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
greetd starts `dsl-bao-3d` on tty1. That program is the DRM
client of `/dev/dri/card1` and draws with OpenGL ES 2.
`screenTransform` in `hosts/greeter/bao/bao-3d.nix` is `1`:
the portrait scene is turned onto the landscape mode, and
touch uses the inverse. Use `3` if the picture is still
sideways. The bao chip from dimsumlabs-graphics
`3D/model_for_dslan.blend` (no cable, no socket) paces the
corner of a red room and trots on its pins. Now and then it
stops, turns side-on and tips its top back and down to look
up. The webcam and palm vision stay off: greetd does not
start `dsl-bao-wave`. Less often it walks near the corner, faces the
viewer, raises its rear, leans away and lifts its three near
hind pins out sideways like a dog (rearmost highest), and
pees from its rear onto the left wall. The puddle dries in
about 30 s. A tap interrupts whatever it is doing: it jumps,
spins, bolts and plays boing, ouch, "don't touch me!", or
"Stop it!" at random. Top right is a QR code for
`https://t.me/dimsumlabs`, about a fifth of the screen height.
Left of it, in Asap Regular: "Joins us on Telegram! Scan the
code on the right." Bottom right, half the screen width:
"Don't touch the bao!" greetd autologins `dimsum` on tty1.
SSH stays up.
If the session misbehaves: `sudo systemctl stop greetd`.

Unit tests, from a checkout. No display and no camera:

```sh
make -C hosts/greeter/bao test
```

The Nix build runs `make all` from this Makefile.

The `.glb` files are committed. To remake them from the blend:

```sh
Blender -b ~/Code/dimsumlabs-graphics/3D/model_for_dslan.blend \
  --python hosts/greeter/bao/bao-3d-export.py -- hosts/greeter/bao
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
