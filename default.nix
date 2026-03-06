{
  lib,
  stdenv,
  pkg-config,
  cmakeMinimal,
  ninja,
  nixfmt,
  nixd,
  llvmPackages,
  libGL,
  angle,
  libpng,
  openal,
  sdl3,
  wayland,
}:

stdenv.mkDerivation {
  name = "mcpe";
  version = "0.6.1+dev.1";
  src = ./.;

  nativeBuildInputs = [
    pkg-config
    cmakeMinimal
    ninja
    nixfmt
    nixd
    llvmPackages.clang-tools
  ];
  buildInputs = [
    libGL
    angle
    libpng
    openal
    sdl3
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    wayland
  ];

  cmakeFlags = [
    "-GNinja"
    "-DBUILD_WITH_NIX=ON"
  ];

  postInstall = ''
    mkdir -p $out/share/
    cp -r ${./data} $out/share/mcpe
  '';
}
