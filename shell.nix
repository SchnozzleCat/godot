{pkgs ? import <nixpkgs> {}}: let
  pname = "godot4-mono";
  fetchNuGet = pkgs.fetchNuGet; # or however it's defined in your nixpkgs
  nugetDeps = import ./deps.nix {inherit fetchNuGet;};
  nugetSource = pkgs.mkNugetSource {
    name = "${pname}-nuget-source";
    description = "A Nuget source with dependencies for ${pname}";
    deps = [nugetDeps];
  };

  nugetConfig = pkgs.writeText "NuGet.Config" ''
    <?xml version="1.0" encoding="utf-8"?>
    <configuration>
      <packageSources>
        <add key="${pname}-deps" value="${nugetSource}/lib" />
      </packageSources>
    </configuration>
  '';
in
  pkgs.mkShell {
    nativeBuildInputs = with pkgs; [
      pkg-config
      autoPatchelfHook
      installShellFiles
      python3
      mono
      dotnet-sdk
      dotnet-runtime
      scons
    ];

    buildInputs = with pkgs; [
      vulkan-loader
      libGL
      autoPatchelfHook
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
      libpulseaudio
      dbus
      speechd
      fontconfig
      udev
    ];

    shellHook = ''
      echo "Setting up development environment for Godot with Mono"
      # Here you can set up environment variables or other necessary setup steps.
    '';
  }
