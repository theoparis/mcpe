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
                stdenv = llvmPackages.stdenv;
              }
              rec {
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

                LD_LIBRARY_PATH = lib.makeLibraryPath buildInputs;
                DYLD_LIBRARY_PATH = lib.makeLibraryPath buildInputs;
              };
        }
      );
    };
}
