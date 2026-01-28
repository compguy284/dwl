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
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      perSystem =
        { pkgs, system, ... }:
        {
          packages = {
            macwc = pkgs.callPackage ./nix/default.nix {
              scenefx = inputs.scenefx.packages.${system}.default;
            };

            default = self.packages.${system}.macwc;
          };

          devShells.default = pkgs.mkShell {
            buildInputs = with pkgs; [
              # Core dependencies
              wlroots_0_19
              wayland
              wayland-protocols
              wayland-scanner
              libxkbcommon
              libinput
              pixman
              libdrm

              # XWayland support
              xorg.libxcb
              xorg.xcbutilwm
              xwayland

              # SceneFX
              inputs.scenefx.packages.${system}.default
              libGL

              # Build tools
              pkg-config
              meson
              ninja
            ];

            shellHook = ''
              echo "macwc development shell (with scenefx)"
              echo "Run 'meson setup build && meson compile -C build' to build"
            '';
          };
        };

      flake = {
        nixosModules.default = import ./nix/module.nix { inherit self; };
      };
    };
}
