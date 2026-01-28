{
  lib,
  stdenv,
  meson,
  ninja,
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
}:

stdenv.mkDerivation {
  pname = "macwc";
  version = "0.8-dev";
  src = ./..;

  nativeBuildInputs = [
    meson
    ninja
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

  mesonFlags = [
    "-Dxwayland=disabled"
  ];

  meta = with lib; {
    description = "macwc - Wayland compositor with scenefx effects";
    homepage = "https://codeberg.org/dwl/dwl"; # upstream attribution
    license = licenses.gpl3Only;
    platforms = platforms.linux;
  };
}
