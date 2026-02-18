{ self }:
{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.programs.swl;
in
{
  options.programs.swl = {
    enable = lib.mkEnableOption "swl - Wayland compositor";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.swl;
      description = "The swl package to use.";
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