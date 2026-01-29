{
  description = "dwl - Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    scenefx = {
      url = "github:wlrfx/scenefx";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { self, flake-parts, ... }@inputs:
    flake-parts.lib.mkFlake { inherit inputs; } {
      imports = [
        inputs.flake-parts.flakeModules.easyOverlay
      ];

      flake = {
        nixosModules.dwl = import ./nix/module.nix { inherit self; };
      };

      perSystem =
        { pkgs, config, ... }:
        let
          dwl = pkgs.callPackage ./nix/default.nix {
            scenefx = inputs.scenefx.packages.${pkgs.stdenv.hostPlatform.system}.default;
          };
          shellOverride = old: {
            nativeBuildInputs = old.nativeBuildInputs ++ [ ];
            buildInputs = old.buildInputs ++ [ ];
          };
        in
        {
          packages.default = dwl;
          overlayAttrs = {
            inherit (config.packages) dwl;
          };
          packages = {
            inherit dwl;
          };
          devShells.default = dwl.overrideAttrs shellOverride;
          formatter = pkgs.nixfmt;
        };
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
    };
}
