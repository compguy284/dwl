{
  lib,
  stdenv,
  pkg-config,
  meson,
  ninja,
  wayland-scanner,
  wlroots_0_19,
  wayland,
  wayland-protocols,
  libxkbcommon,
  libinput,
  pixman,
  libdrm,
  libxcb,
  libxcb-wm ? null,
  xcbutilwm ? null,
  scenefx,
  libGL,
  enableXWayland ? false,
  debug ? false,
}:

stdenv.mkDerivation {
  pname = "dwl";
  version = "0.8-dev";
  src = ./..;

  nativeBuildInputs = [
    pkg-config
    meson
    ninja
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
    libxcb
    (if libxcb-wm != null then libxcb-wm else xcbutilwm)
    scenefx
    libGL
  ];

  mesonBuildType = if debug then "debug" else "release";

  mesonFlags = [
    "-Dxwayland=${lib.boolToString enableXWayland}"
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
