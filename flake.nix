{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs";
  };

  outputs =
    { self, nixpkgs, ... }:
    let
      eachSystem = nixpkgs.lib.genAttrs [
        "x86_64-linux"
        "aarch64-linux"
        "riscv64-linux"
        "aarch64-darwin"
      ];
    in
    {
      packages = eachSystem (
        system: with nixpkgs.legacyPackages.${system}; {
          default = callPackage ./default.nix {
            stdenv = llvmPackages.stdenv;
          };
        }
      );

      devShells = eachSystem (
        system: with nixpkgs.legacyPackages.${system}; {
          default =
            mkShell.override
              {
                stdenv =
                  if stdenv.hostPlatform.isLinux then useWildLinker llvmPackages.stdenv else llvmPackages.stdenv;
              }
              rec {
                nativeBuildInputs = [
                  pkg-config
                  rustfmt
                  clippy
                  cargo
                  rustc
                  rust-analyzer
                  nixfmt
                  nil
                ];

                buildInputs = [
                  vulkan-loader
                  stdenv.cc.cc.lib
                ]
                ++ lib.optionals stdenv.hostPlatform.isLinux [
                  wayland
                  libxkbcommon
                ];

                RUST_SRC_PATH = rustPlatform.rustLibSrc;
                LD_LIBRARY_PATH = lib.makeLibraryPath buildInputs;
                DYLD_LIBRARY_PATH = lib.makeLibraryPath buildInputs;
              };
        }
      );
    };
}
