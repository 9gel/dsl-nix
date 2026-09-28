# Packages every host gets. Add new baseline tools here.
{ pkgs, ... }:
{
  environment.systemPackages = with pkgs; [
    git
  ];
}
