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
          wayfireUncrustifyConfig = pkgs.runCommand "wayfire-uncrustify.ini" { } ''
            substitute ${pkgs.wayfire.src}/uncrustify.ini $out \
              --replace-fail sp_before_tr_emb_cmt sp_before_tr_cmt \
              --replace-fail sp_num_before_tr_emb_cmt sp_num_before_tr_cmt
          '';
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ pkgs.wayfire ];

            packages = with pkgs; [
              gettext
              just
              meson
              ninja
              pkg-config
              uncrustify
              wayfire
            ];

            WAYFIRE_UNCRUSTIFY_CONFIG = wayfireUncrustifyConfig;

            shellHook = ''
              export PKG_CONFIG_PATH="${wayfirePkgConfig}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            '';
          };
        }
      );
    };
}
