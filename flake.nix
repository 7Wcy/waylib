{
  description = "A basic flake to help develop waylib";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nix-filter.url = "github:numtide/nix-filter";
    qwlroots = {
      url = "github:vioken/qwlroots";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
      inputs.nix-filter.follows = "nix-filter";
    };
  };

  outputs = { self, nixpkgs, flake-utils, nix-filter, qwlroots }@input:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" "riscv64-linux" ]
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          waylib = pkgs.qt6.callPackage ./nix {
            nix-filter = nix-filter.lib;
            qwlroots = qwlroots.packages.${system}.qwlroots-qt6-wlroots-git;
          };
        in
        rec {
          packages.default = waylib;

          devShells.default = pkgs.mkShell { 
            packages = with pkgs; [
              wayland-utils
            ];

            inputsFrom = [
              packages.default
              qwlroots.packages.${system}.qwlroots-qt6-wlroots-git
            ];

            shellHook = let
              makeQtpluginPath = pkgs.lib.makeSearchPathOutput "out" pkgs.qt6.qtbase.qtPluginPrefix;
              makeQmlpluginPath = pkgs.lib.makeSearchPathOutput "out" pkgs.qt6.qtbase.qtQmlPrefix;
            in ''
              echo "welcome to waylib"
              echo "wlroots: $(pkg-config --modversion wlroots)"
              echo "wayland-server: $(pkg-config --modversion wayland-server)"
              # unexpected QT_NO_DEBUG form qt-base-hook
              # https://github.com/NixOS/nixpkgs/issues/251918
              export NIX_CFLAGS_COMPILE=$(echo $NIX_CFLAGS_COMPILE | sed 's/-DQT_NO_DEBUG//')
              export QT_LOGGING_RULES="*.debug=true;qt.*.debug=false"
              #export MESA_DEBUG=1
              #export EGL_LOG_LEVEL=debug
              #export LIBGL_DEBUG=verbose
              #export WAYLAND_DEBUG=1
              export QT_PLUGIN_PATH=${makeQtpluginPath (with pkgs.qt6; [ qtbase qtdeclarative qtquick3d qtwayland ])}
              export QML2_IMPORT_PATH=${makeQmlpluginPath (with pkgs.qt6; [ qtdeclarative qtquick3d ])}
              export QML_IMPORT_PATH=$QML2_IMPORT_PATH
            '';
          };
        }
      );
}
