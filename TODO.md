# Project TODO List #

## TODO: ##

+ Add to `configure.ac` a check for **GNU** `make` on build machine. `unixcw`'s `Makefile`s may not work with
  _non-**GNU**_ `make` on non-Linux machines.
+ Add to `configure.ac` a check for `pkg-config`. It is necessary for configuring **QT4** application _(`xcwcp`)_.
+ Make `qa_test_configure_flags.sh` portable. Some shells _(on **FreeBSD**)_ don't like the `options[]` table.
+ After finalizing split of `libcw` into modules, add `configure` flags for disabling modules
  _(e.g. `--disable-libcw-receiver`, `--disable-libcw-key`)_.
+ Check if it's possible to use `pkg-config` to get `ncurses` compilation flags.


## DONE: ##

+ _(nothing, yet...)_
