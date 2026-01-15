{
  description = "dwl - dwm for Wayland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    scenefx.url = "github:wlrfx/scenefx";
  };

  outputs =
    {
      self,
      nixpkgs,
      scenefx,
    }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          dwl = pkgs.callPackage ./nix/default.nix {
            scenefx = scenefx.packages.${system}.default;
          };

          default = self.packages.${system}.dwl;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          scenefxPkg = scenefx.packages.${system}.default;
        in
        {
          default = pkgs.mkShell {
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
              scenefxPkg
              libGL

              # Build tools
              pkg-config
              gnumake
            ];

            shellHook = ''
              echo "dwl development shell (with scenefx)"
              echo "Run 'make' to build"
            '';
          };
        }
      );
    };
}
