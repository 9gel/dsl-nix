{
  description = "Dim Sum Lab NixOS machines";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
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
        ];
      };

      devShells = each (system: {
        default = nixpkgs.legacyPackages.${system}.mkShellNoCC { };
      });
    };
}
