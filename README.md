# Graphics Binary Debugger

The Graphics Binary Debugger, `gbd`, will inspect an N64 graphics task for errors and try to point out crashing GBI commands. Currently, it only targets F3DZEX and S2DEX2 for Zelda Ocarina of Time MQ Debug, it's scope is intended to be expanded upon in the future. This does not attempt to accurately simulate the microcode task or the workings of the RDP, it only seeks to validate arguments to GBI commands.

It is still rather primitive, but good enough to debug several kinds of common crashes.

## Usage

`gbd <path to RAM dump> <start address>`

Currently, the only way to use `gbd` is by dumping the contents of RDRAM to a file. `AUTO` can be entered in place of a start address to use the default start address.

## Building

Run `make` to build the program. Building requires [libiconv](https://www.gnu.org/software/libiconv/) be installed with `--enable-static`. Either install it yourself or run `make libiconv` to install it to `/usr/local/lib`.
