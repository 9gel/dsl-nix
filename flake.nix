{
  description = "Dim Sum Lab NixOS machines";

  nixConfig = {
    extra-substituters = [
      "https://nixos-raspberrypi.cachix.org"
    ];
    extra-trusted-public-keys = [
      "nixos-raspberrypi.cachix.org-1:4iMO9LXa8BqhU+Rpg6LQKiGa2lsNh/j2oiYLNOQ5sPI="
    ];
  };

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    home-manager.url =
      "github:nix-community/home-manager/release-26.05";
    home-manager.inputs.nixpkgs.follows = "nixpkgs";
    nixos-raspberrypi.url =
      "github:nvmd/nixos-raspberrypi/main";
    nixos-raspberrypi.inputs.nixpkgs.follows = "nixpkgs";
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

      nixosConfigurations.pi5 = nixos-raspberrypi.lib.nixosSystem {
        modules = [
          ./hosts/pi5/configuration.nix
          home-manager.nixosModules.home-manager
          dimsumHm
        ];
      };

      devShells = each (system: {
        default = nixpkgs.legacyPackages.${system}.mkShellNoCC { };
      });
    };
}
