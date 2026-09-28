{
  description = "Dim Sum Lab NixOS machines";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    home-manager.url =
      "github:nix-community/home-manager/release-26.05";
    home-manager.inputs.nixpkgs.follows = "nixpkgs";
    # Own nixpkgs, same as ~/Code/pi5-nix. Do not follow this
    # flake's nixpkgs: that misses the Raspberry Pi cache.
    nixos-raspberrypi.url =
      "github:nvmd/nixos-raspberrypi/main";
  };

  outputs = { self, nixpkgs, home-manager, nixos-raspberrypi }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      each = nixpkgs.lib.genAttrs systems;
      dimsumHm = {
        home-manager.useGlobalPkgs = true;
        home-manager.useUserPackages = true;
        home-manager.users.dimsum = import ./hosts/common/home.nix;
      };
    in
    {
      nixosConfigurations.jukebox = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [
          ./hosts/jukebox/configuration.nix
          home-manager.nixosModules.home-manager
          dimsumHm
          {
            home-manager.users.dimsum.imports = [
              ./hosts/jukebox/home.nix
            ];
          }
        ];
      };

      nixosConfigurations.greeter = nixos-raspberrypi.lib.nixosSystem {
        modules = [
          ./hosts/greeter/configuration.nix
          home-manager.nixosModules.home-manager
          dimsumHm
        ];
      };

      devShells = each (system: {
        default = nixpkgs.legacyPackages.${system}.mkShellNoCC { };
      });
    };
}
