
with import <nixpkgs> {};
stdenv.mkDerivation {
  name = "devel";
  buildInputs = let python3-select-packages = python-packages: with python-packages; [
    matplotlib numpy pandas psutil seaborn tkinter
  ];
  python3-with-packages = python3.withPackages python3-select-packages;

  r-packages = with rPackages; [
    R data_table ggplot2 dplyr here tidyverse
  ];
  in [
    binutils clang cmake gnumake ninja pkgconfig
    python3-with-packages
    iperf3
    #linuxHeaders linux.dev
    linuxHeaders linux_latest.dev
  ] ++ r-packages;
}


