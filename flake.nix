{
  description = "Dim Sum Lab NixOS machines";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    home-manager.url =
      "github:nix-community/home-manager/release-26.05";
    home-manager.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { self, nixpkgs, home-manager }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      each = nixpkgs.lib.genAttrs systems;
    in
    {
      nixosConfigurations.jukebox = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [
          ./hosts/jukebox/configuration.nix
          home-manager.nixosModules.home-manager
          {
            home-manager.useGlobalPkgs = true;
            home-manager.useUserPackages = true;
            home-manager.users.dimsum =
              import ./hosts/jukebox/home.nix;
          }
        ];
      };

      devShells = each (system: {
        default = nixpkgs.legacyPackages.${system}.mkShellNoCC { };
      });
    };
}
