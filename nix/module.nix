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

    xwayland = {
      enable = lib.mkEnableOption "XWayland support";
    };

    extraPackages = lib.mkOption {
      type = with lib.types; listOf package;
      default = with pkgs; [
        foot
        grim
        slurp
        wmenu
        wl-clipboard
        swayidle
        swaylock
      ];
      defaultText = lib.literalExpression ''
        with pkgs; [ foot grim slurp wmenu wl-clipboard swayidle swaylock ]
      '';
      description = "Extra packages to install system-wide alongside swl.";
    };

    portalPackage = lib.mkOption {
      type = lib.types.package;
      default = pkgs.xdg-desktop-portal-wlr;
      defaultText = lib.literalExpression "pkgs.xdg-desktop-portal-wlr";
      description = "The xdg-desktop-portal-wlr package to use for screen sharing.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages =
      [
        (if cfg.xwayland.enable then
          cfg.package.override { enableXWayland = true; }
        else
          cfg.package)
      ]
      ++ cfg.extraPackages;

    # Register as a wayland session for display managers
    services.displayManager.sessionPackages = [ cfg.package ];

    # Ensure wayland/graphics stack is available
    hardware.graphics.enable = lib.mkDefault true;

    # XDG portal configuration for screen sharing (OBS, Firefox, etc.)
    xdg.portal = {
      enable = true;
      extraPortals = [
        cfg.portalPackage
        pkgs.xdg-desktop-portal-gtk
      ];
      config.swl = {
        default = [ "gtk" ];
        "org.freedesktop.impl.portal.ScreenCast" = "wlr";
        "org.freedesktop.impl.portal.Screenshot" = "wlr";
        "org.freedesktop.impl.portal.Inhibit" = "none";
      };
    };

    # Wayland environment variables
    environment.sessionVariables = {
      XDG_CURRENT_DESKTOP = "swl";
      XDG_SESSION_TYPE = "wayland";
      NIXOS_OZONE_WL = "1";
    };

    # Privilege escalation for GUI apps
    security.polkit.enable = lib.mkDefault true;

    # PAM service so swaylock can authenticate
    security.pam.services.swaylock = { };

    # GTK settings via dconf
    programs.dconf.enable = lib.mkDefault true;

    # XWayland
    programs.xwayland.enable = lib.mkIf cfg.xwayland.enable (lib.mkDefault true);

    # Run XDG autostart files (WMs don't handle this unlike DEs)
    services.xserver.desktopManager.runXdgAutostartIfNone = lib.mkDefault true;
  };
}
