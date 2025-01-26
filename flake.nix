{
  description = "A Nix-flake-based C/C++ development environment";

  inputs.nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0.1.*.tar.gz";

  outputs = {
    self,
    nixpkgs,
  }: let
    supportedSystems = ["x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin"];
    forEachSupportedSystem = f:
      nixpkgs.lib.genAttrs supportedSystems (system:
        f rec {
          pkgs = import nixpkgs {inherit system;};
          deps = with pkgs; [
            pkg-config
            autoPatchelfHook
            installShellFiles
            python3
            speechd
            wayland-scanner
            makeWrapper
            mono
            vulkan-loader
            libGL
            xorg.libX11
            xorg.libXcursor
            xorg.libXinerama
            xorg.libXext
            xorg.libXrandr
            xorg.libXrender
            xorg.libXi
            xorg.libXfixes
            libxkbcommon
            alsa-lib
            mono
            wayland-scanner
            wayland
            libdecor
            libpulseaudio
            dbus
            dbus.lib
            speechd
            fontconfig
            fontconfig.lib
            udev
            dotnetCorePackages.sdk_8_0
            scons
          ];
        });
  in {
    scripts = pkgs: [
      (pkgs.writeShellScriptBin "fullrebuild" ''
        echo "Building Godot with version status $GODOT_VERSION_STATUS..."
        scons p=linuxbsd target=editor precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=no
        bin/godot.linuxbsd.editor.x86_64.mono --headless --generate-mono-glue modules/mono/glue
        scons p=linuxbsd target=editor precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=yes
        python modules/mono/build_scripts/build_assemblies.py --godot-output-dir bin --precision=single --push-nupkgs-local $GODOT_VERSION_STATUS
      '')
      (pkgs.writeShellScriptBin "run" ''
        ./bin/godot.linuxbsd.editor.x86_64.mono
      '')
    ];
    devShells = forEachSupportedSystem ({
      pkgs,
      deps,
    }: {
      default =
        pkgs.mkShell.override
        {
          # Override stdenv in order to change compiler:
          # stdenv = pkgs.clangStdenv;
        }
        {
          GODOT_VERSION_STATUS = "SchnozzleCat-custom";
          packages =
            deps
            ++ self.scripts pkgs;
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath deps;
        };
    });
  };
}
