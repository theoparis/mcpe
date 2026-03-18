{
  lib,
  stdenv,
  pkg-config,
  cmakeMinimal,
  ninja,
  nixfmt,
  nixd,
  llvmPackages,
  libglvnd,
  shaderc,
  vulkan-headers,
  vulkan-loader,
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
    shaderc
  ];
  buildInputs = [
    libglvnd
    libglvnd.dev
    vulkan-loader
    vulkan-headers
    libpng
    openal
    sdl3
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    wayland
  ];

  cmakeFlags = [
    "-GNinja"
  ];

  postInstall = ''
    mkdir -p $out/share/
    cp -r ${./data} $out/share/mcpe
  '';
}
