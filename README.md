# pacclean

Tired of running `pacman -R $(pacman -Qdtq)` ten times to remove all your unused packages installed just as dependencies?
Want a `pacman -Qdt` that can detect dependency cycles that are not needed?

Then `pacclean` is for you!

## Dependencies

Requires [libalpm](https://www.archlinux.org/pacman/libalpm.3.html), [Boost.Program_options](https://www.boost.org/doc/libs/release/libs/program_options/) and a C++ compiler supporting C++17.

Probably only works on Arch Linux, so just install the dependencies with:
```sh
pacman -Syu boost g++ make
```

## Building

Just run `make` and the `pacclean` binary should appear.

## TODO

- Figure out what to do, when a package depends one another package, that more than one installed package provide.
  Currently we just treat all of those packages as dependencies.
