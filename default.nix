{
  lib,
  stdenv,
  useWildLinker,
  makeWrapper,
  pkg-config,
  llvmPackages,
  vulkan-loader,
  rustPlatform,
  wayland,
  libxkbcommon,
}:

rustPlatform.buildRustPackage.override
  { stdenv = if stdenv.hostPlatform.isLinux then useWildLinker stdenv else stdenv; }
  {
    pname = "mcpe";
    version = "0.6.1+dev.1";
    src = ./.;
    cargoLock = {
      lockFile = ./Cargo.lock;
      outputHashes = {
        "lz4_flex-0.11.5" = "sha256-zcgqA7uIRnSwDyEKaS1XZftOYaRnhIIYiHDcEANizPs=";
      };
    };

    nativeBuildInputs = [
      pkg-config
      makeWrapper
    ];
    buildInputs = [
      vulkan-loader
    ]
    ++ lib.optionals stdenv.hostPlatform.isLinux [
      wayland
      libxkbcommon
    ];

    postInstall = ''
      mkdir -p $out/share/
      cp -r ${./data} $out/share/mcpe

      wrapProgram $out/bin/mcpe --set MCPE_DATA_DIR "$out/share/mcpe"
    '';
  }
