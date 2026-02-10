{
  lib,
  stdenv,
  pkg-config,
  wayland-scanner,
  wlroots_0_19,
  wayland,
  wayland-protocols,
  libxkbcommon,
  libinput,
  pixman,
  libdrm,
  xorg,
  scenefx,
  libGL,
  enableXWayland ? false,
}:

stdenv.mkDerivation {
  pname = "dwl";
  version = "0.8-dev";
  src = ./..;

  nativeBuildInputs = [
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    wlroots_0_19
    wayland
    wayland-protocols
    libxkbcommon
    libinput
    pixman
    libdrm
    xorg.libxcb
    xorg.xcbutilwm
    scenefx
    libGL
  ];

  makeFlags = [
    "PREFIX=$(out)"
    (lib.optional enableXWayland "-DXWAYLAND")
  ];
  passthru.providedSessions = [ "dwl" ];

  meta = {
    description = "dwl - Wayland compositor with scenefx effects";
    homepage = "https://codeberg.org/dwl/dwl"; # upstream attribution
    license = lib.licenses.gpl3Only;
    platforms = lib.platforms.linux;
    mainProgram = "dwl";
  };
}
