import abc
import ctypes
from pathlib import Path
import platform
import traceback
from typing import Sequence


SYSTEM = platform.system()
if SYSTEM not in {"Linux", "Windows"}:
    raise NotImplementedError(SYSTEM)


gfxbd = ctypes.CDLL(
    Path(__file__).parent.parent.parent
    / "build"
    / {
        "Linux": "linux64/libgbd.so",
        "Windows": "windows/libgbd.dll",
    }[SYSTEM]
)


class rdram_interface_t(ctypes.Structure):
    fn_close = ctypes.CFUNCTYPE(ctypes.c_int)
    fn_open = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
    fn_pos = ctypes.CFUNCTYPE(ctypes.c_long)
    fn_addr_valid = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint32)
    fn_read = ctypes.CFUNCTYPE(
        ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t
    )
    fn_seek = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint32)
    fn_read_at = ctypes.CFUNCTYPE(
        ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t
    )

    _fields_ = [
        ("close", fn_close),
        ("open", fn_open),
        ("pos", fn_pos),
        ("addr_valid", fn_addr_valid),
        ("read", fn_read),
        ("seek", fn_seek),
        ("read_at", fn_read_at),
    ]


def copy_bytes_to_buf(data: bytes, buf: ctypes.c_void_p):
    # TODO this looks pretty bad
    ctypes.memmove(buf, ctypes.create_string_buffer(data, len(data)), len(data))


class PyRDRAMInterface(abc.ABC):
    def make_rdram_interface_t(self):
        return rdram_interface_t(
            rdram_interface_t.fn_close(self._close),
            rdram_interface_t.fn_open(self._open),
            rdram_interface_t.fn_pos(self._pos),
            rdram_interface_t.fn_addr_valid(self._addr_valid),
            rdram_interface_t.fn_read(self._read),
            rdram_interface_t.fn_seek(self._seek),
            rdram_interface_t.fn_read_at(self._read_at),
        )

    def _close(self):
        try:
            return self.close()
        except:
            traceback.print_exc()
            return -1  # EOF (cf man fclose)

    def _open(self, arg: ctypes.c_void_p):
        try:
            return self.open()
        except:
            traceback.print_exc()
            return -1

    def _pos(self):
        try:
            return self.pos()
        except:
            traceback.print_exc()
            return -1

    def _addr_valid(self, addr: int):
        try:
            assert isinstance(addr, int)
            return self.addr_valid(addr)
        except:
            traceback.print_exc()
            return False

    def _read(
        self,
        buf: ctypes.c_void_p,
        elem_size: ctypes.c_size_t,
        elem_count: ctypes.c_size_t,
    ):
        try:
            nbytes = int(elem_size) * int(elem_count)
            data = self.read(nbytes)
            assert isinstance(data, bytes), type(data)

            copy_bytes_to_buf(data, buf)

            return len(data)
        except:
            traceback.print_exc()
            return 0

    def _seek(self, addr):
        try:
            assert isinstance(addr, int)
            return self.seek(addr)
        except:
            traceback.print_exc()
            return False

    def _read_at(
        self,
        buf: ctypes.c_void_p,
        addr: ctypes.c_uint32,
        size: ctypes.c_size_t,
    ):
        try:
            data = self.read_at(int(addr), int(size))
            assert isinstance(data, bytes)

            if len(data) != size:
                return False

            copy_bytes_to_buf(data, buf)

            return True
        except:
            traceback.print_exc()
            return False

    @abc.abstractmethod
    def close(self) -> int: ...

    @abc.abstractmethod
    def open(self) -> int: ...

    @abc.abstractmethod
    def pos(self) -> int: ...

    @abc.abstractmethod
    def addr_valid(self, addr: int) -> bool: ...

    @abc.abstractmethod
    def read(self, nbytes: int) -> bytes: ...

    @abc.abstractmethod
    def seek(self, addr: int) -> bool: ...

    @abc.abstractmethod
    def read_at(self, addr: int, size: int) -> bytes: ...


class PyRDRAMInterface_ReadAtBased(PyRDRAMInterface):
    def __init__(self):
        super().__init__()
        self._curpos = 0
        self.is_open = False

    def close(self) -> int:
        self.is_open = False
        return 0

    def open(self) -> int:
        self.is_open = True
        return 0

    def pos(self) -> int:
        return self._curpos

    @abc.abstractmethod
    def addr_valid(self, addr: int) -> bool: ...

    def read(self, nbytes: int) -> bytes:
        data = self.read_at_impl(self._curpos, nbytes)
        assert isinstance(data, bytes)
        self._curpos += len(data)
        return data

    def seek(self, addr: int) -> bool:
        self._curpos = addr
        return True

    def read_at(self, addr: int, size: int) -> bytes:
        data = self.read_at_impl(self._curpos, size)
        assert isinstance(data, bytes)
        self._curpos = addr + len(data)
        return data

    @abc.abstractmethod
    def read_at_impl(self, addr: int, size: int) -> bytes: ...


class PyRDRAMInterface_BytesBased(PyRDRAMInterface_ReadAtBased):
    def __init__(self, data: bytes):
        super().__init__()
        self.data = data

    def addr_valid(self, addr: int) -> bool:
        return addr >= 0 and addr < len(self.data)

    def read_at_impl(self, addr: int, size: int) -> bytes:
        return self.data[addr : addr + size]


gfxd_ucode_t = ctypes.c_void_p  # gfxd_ucode_t == struct gfxd_ucode *

gfxd_f3db = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3db")
gfxd_f3d = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3d")
gfxd_f3dexb = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3dexb")
gfxd_f3dex = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3dex")
gfxd_f3dex2 = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3dex2")
gfxd_f3dex3 = gfxd_ucode_t.in_dll(gfxbd, "gfxd_f3dex3")
gfxd_s2dex2 = gfxd_ucode_t.in_dll(gfxbd, "gfxd_s2dex2")


class gfx_ucode_registry_t(ctypes.Structure):
    _fields_ = [
        ("text_start", ctypes.c_uint32),
        ("ucode", gfxd_ucode_t),
    ]

    @staticmethod
    def new(text_start: int, ucode: gfxd_ucode_t):
        return gfx_ucode_registry_t(text_start, ucode)


class gbd_options_t(ctypes.Structure):
    _fields_ = [
        ("quiet", ctypes.c_bool),
        ("print_vertices", ctypes.c_bool),
        ("print_textures", ctypes.c_bool),
        ("print_matrices", ctypes.c_bool),
        ("print_lights", ctypes.c_bool),
        ("print_multi_packet", ctypes.c_bool),
        ("hex_color", ctypes.c_bool),
        ("q_macros", ctypes.c_bool),
        ("to_num", ctypes.c_int),
        ("no_volume_cull", ctypes.c_bool),
        ("no_depth_cull", ctypes.c_bool),
        ("all_depth_cull", ctypes.c_bool),
        ("string_encoding", ctypes.c_char_p),
    ]

    @staticmethod
    def new(
        quiet=False,
        print_vertices=False,
        print_textures=False,
        print_matrices=False,
        print_lights=False,
        print_multi_packet=False,
        hex_color=False,
        q_macros=True,
        to_num: int | None = None,
        no_volume_cull=False,
        no_depth_cull=False,
        all_depth_cull=False,
        string_encoding="EUC-JP",
    ):
        return gbd_options_t(
            quiet,
            print_vertices,
            print_textures,
            print_matrices,
            print_lights,
            print_multi_packet,
            hex_color,
            q_macros,
            0 if to_num is None else to_num,
            no_volume_cull,
            no_depth_cull,
            all_depth_cull,
            string_encoding.encode(),
        )


USE_GIVEN_START_ADDR = 0
USE_START_ADDR_AT_POINTER = 1


class struct_start_location_info(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("start_location", ctypes.c_uint32),
        ("start_location_ptr", ctypes.c_uint32),
    ]

    @staticmethod
    def new_from_start_location(start_location: int):
        return struct_start_location_info(USE_GIVEN_START_ADDR, start_location, 0)

    @staticmethod
    def new_from_start_location_ptr(start_location_ptr: int):
        return struct_start_location_info(
            USE_START_ADDR_AT_POINTER, 0, start_location_ptr
        )


gfxbd.analyze_gbi.argtypes = [
    ctypes.c_void_p,  # FILE *print_out
    ctypes.POINTER(gfx_ucode_registry_t),
    ctypes.POINTER(gbd_options_t),
    ctypes.POINTER(rdram_interface_t),
    ctypes.c_void_p,  # rdram_arg
    ctypes.POINTER(struct_start_location_info),  # start_location
]
gfxbd.analyze_gbi.restype = ctypes.c_int


def fdopen(fd: int):
    if SYSTEM == "Linux":
        libc = ctypes.CDLL("libc.so.6")
        libc.fdopen.argtypes = [ctypes.c_int, ctypes.c_char_p]
        libc.fdopen.restype = ctypes.c_void_p  # FILE *
        _fdopen = libc.fdopen
        return _fdopen(fd, b"w")
    elif SYSTEM == "Windows":
        raise NotImplementedError(SYSTEM)


def analyze_gbi(
    print_out_fileno: int,
    ucodes: Sequence[gfx_ucode_registry_t],
    opts: gbd_options_t,
    rdram: rdram_interface_t,
    start_location: struct_start_location_info,
):
    r = gfxbd.analyze_gbi(
        fdopen(print_out_fileno),
        (gfx_ucode_registry_t * (len(ucodes) + 1))(
            *ucodes,
            gfx_ucode_registry_t(),
        ),
        ctypes.byref(opts),
        ctypes.byref(rdram),
        ctypes.c_void_p(0),
        ctypes.byref(start_location),
    )
    assert isinstance(r, int)
    return r


def main():
    import argparse
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument("ram_dump", type=Path)
    parser.add_argument("start_addr", type=int)
    args = parser.parse_args()

    ram_dump_p: Path = args.ram_dump
    start_location: int = args.start_addr

    ram_dump_bin = ram_dump_p.read_bytes()

    analyze_gbi(
        sys.stdout.fileno(),
        [
            gfx_ucode_registry_t.new(0x80155F50, gfxd_f3dex2),
            gfx_ucode_registry_t.new(0x80113070, gfxd_s2dex2),
        ],
        gbd_options_t.new(),
        PyRDRAMInterface_BytesBased(ram_dump_bin).make_rdram_interface_t(),
        struct_start_location_info.new_from_start_location(start_location),
    )


if __name__ == "__main__":
    main()
