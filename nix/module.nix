{ self }:
{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.programs.dwl;
in
{
  options.programs.dwl = {
    enable = lib.mkEnableOption "dwl - Wayland compositor";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.dwl;
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
}