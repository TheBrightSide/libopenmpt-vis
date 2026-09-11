{
  description = "Empty Template";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        buildInputs = with pkgs; [
          clang
          clang-tools
          pkg-config
          raylib
          libopenmpt
          gf
          sdl3
          gitui
          perf
          odin
          ols
        ];
      in {
        devShells.default =
          pkgs.mkShell { inherit buildInputs; };
      });
}
