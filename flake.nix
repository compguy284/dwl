{
  description = "dwl - dwm for Wayland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    scenefx = {
      url = "github:wlrfx/scenefx";
      inputs.nixpkgs.follows = "nixpkgs";
    };
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

      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.dwl;
        in
        {
          options.programs.dwl = {
            enable = lib.mkEnableOption "dwl - dwm for Wayland";

            package = lib.mkOption {
              type = lib.types.package;
              default = self.packages.${pkgs.system}.default;
              defaultText = lib.literalExpression "inputs.dwl.packages.\${system}.default";
              description = "The dwl package to use.";
            };
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ];

            # Register as a wayland session for display managers
            services.displayManager.sessionPackages = [ cfg.package ];

            # Ensure wayland/graphics stack is available
            hardware.graphics.enable = lib.mkDefault true;
          };
        };

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
              meson
              ninja
            ];

            shellHook = ''
              echo "dwl development shell (with scenefx)"
              echo "Run 'meson setup build && meson compile -C build' to build"
            '';
          };
        }
      );
    };
}
