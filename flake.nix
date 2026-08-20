{
  description = "Development shell for vecdecor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/07e1d92cdc0ed416cfa11ff3ca40d17e61cfba7a";

  outputs =
    { nixpkgs, ... }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [
        "aarch64-linux"
        "x86_64-linux"
      ];
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          wayfirePkgConfig = pkgs.runCommand "wayfire-pkg-config" { } ''
            mkdir -p $out/lib/pkgconfig
            substitute ${pkgs.wayfire}/lib/pkgconfig/wayfire.pc $out/lib/pkgconfig/wayfire.pc \
              --replace-fail 'metadatadir=''${prefix}/share/wayfire/metadata' 'metadatadir=/usr/share/wayfire/metadata'
          '';
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ pkgs.wayfire ];

            packages = with pkgs; [
              just
              meson
              ninja
              pkg-config
              uncrustify
              wayfire
            ];

            WAYFIRE_UNCRUSTIFY_CONFIG = "${pkgs.wayfire.src}/uncrustify.ini";

            shellHook = ''
              export PKG_CONFIG_PATH="${wayfirePkgConfig}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            '';
          };
        }
      );
    };
}
