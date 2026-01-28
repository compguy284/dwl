{
  description = "macwc - Wayland compositor";

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
        nixosModules.macwc = import ./nix/module.nix { inherit self; };
      };

      perSystem =
        { pkgs, config, ... }:
        let
          macwc = pkgs.callPackage ./nix/default.nix {
            scenefx = inputs.scenefx.packages.${system.stdenv.hostPlatform.system}.default;
          };
          shellOverride = old: {
            nativeBuildInputs = old.nativeBuildInputs ++ [ ];
            buildInputs = old.buildInputs ++ [ ];
          };
        in
        {
          packages.default = macwc;
          overlayAttrs = {
            inherit (config.packages) macwc;
          };
          packages = {
            inherit macwc;
          };
          devShells.default = macwc.overrideAttrs shellOverride;
          formatter = pkgs.nixfmt;
        };
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
    };
}
