{
  description = "dwl - Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    scenefx = {
      url = "github:wlrfx/scenefx/3a6cfb12e4ba97b43326357d14f7b3e40897adfc";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    # wlroots = {
    #   # url = "https://gitlab.freedesktop.org/wlroots/wlroots.git";
    #   # url = "gitlab:wlroots/wlroots?host=gitlab.freedesktop.org";
    #   flake = false;
    # };
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
          date = {
            year = builtins.substring 0 4;
            month = builtins.substring 4 2;
            day = builtins.substring 6 2;
            hour = builtins.substring 8 2;
            minute = builtins.substring 10 2;
            second = builtins.substring 12 2;
          };
          fmt-date = raw: "${date.year raw}-${date.month raw}-${date.day raw}";
          package-version = src: "unstable-${fmt-date src.lastModifiedDate}-${src.shortRev}";
          my_wlroots = pkgs.wlroots_0_19.overrideAttrs (
            new: old: {
              src = inputs.wlroots;
              version = package-version inputs.wlroots;
            }
          );
          scenefxPkg = inputs.scenefx.packages.${pkgs.stdenv.hostPlatform.system}.default;
          dwl = pkgs.callPackage ./nix/default.nix {
            scenefx = scenefxPkg;
            # wlroots_0_19 = my_wlroots;
          };
          dwl-debug = dwl.override { debug = true; };
        in
        {
          packages.default = dwl;
          overlayAttrs = {
            inherit (config.packages) dwl;
          };
          packages = {
            inherit dwl dwl-debug;
          };
          devShells.default = pkgs.mkShell {
            inputsFrom = [ dwl ];
            packages = with pkgs; [
              meson
              ninja
              cmocka
            ];
          };
          formatter = pkgs.nixfmt;
        };
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
    };
}
