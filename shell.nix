# Build tools for the Echo Nano custom firmware. Enter with `nix-shell`, then `make`.
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    gnumake
    python3
    gcc-arm-embedded # arm-none-eabi-{gcc,as,ld,objcopy,objdump,nm}
  ];
}
