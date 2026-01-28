{ self }:
{ config, lib, pkgs, ... }:
let
  cfg = config.programs.macwc;
in
{
  options.programs.macwc = {
    enable = lib.mkEnableOption "macwc - Wayland compositor";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.system}.default;
      defaultText = lib.literalExpression "inputs.macwc.packages.\${system}.default";
      description = "The macwc package to use.";
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
