# Graphics Binary Debugger

The Graphics Binary Debugger, `gbd`, will inspect an N64 graphics task for errors and try to point out crashing GBI commands. Currently, it only targets F3DZEX and S2DEX2 for Zelda Ocarina of Time MQ Debug, its scope is intended to be expanded upon in the future. This does not attempt to accurately simulate the microcode task or the workings of the RDP, it only seeks to validate arguments to GBI commands.

It is still rather primitive, but good enough to debug several kinds of common crashes.

## Usage

`gbd <path to RAM dump> <start address>`

or

`gbd <path to RAM dump> *<pointer to start address>`

For example `gbd ram.bin 0x801B4100` (disassemble from `0x801B4100`) or `gbd ram.bin *0x8012D260` (disassemble from the address found at `0x8012D260`)

Currently, the only way to use `gbd` is by dumping the contents of RDRAM to a file. `AUTO` can be entered in place of a start address to use the default start address.

## Building

Run `make` to build the program. Building requires [libiconv](https://www.gnu.org/software/libiconv/) to be installed with `--enable-static`. Either install it yourself or run `make libiconv` to install it to `libiconv/linux` in the repository directory. If you would like to install libiconv elsewhere you can override `ICONV_PREFIX` in the Makefile.

Building for windows is also supported. The steps are the same, merely add `TARGET=windows` in the make commands. If running `make libiconv` for windows, it will be installed locally to `libiconv/windows`.
