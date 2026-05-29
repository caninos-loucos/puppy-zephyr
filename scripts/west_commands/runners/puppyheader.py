# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import sys
import ctypes

from dataclasses import dataclass
from pathlib import Path



UINT32_MASK = 0xFFFFFFFF

def div_round_up(n: int, d: int) -> int:
    return (n + d - 1) // d

def round_up(x: int, align: int) -> int:
    return div_round_up(x, align) * align

@dataclass
class FlashHeader:
    next_desc: int
    nb_areas: int
    entry: int
    bootaddr: int


@dataclass
class FlashMemArea:
    start: int
    ptr: int
    size: int
    blocks: int



def save_binary_file(filename: str, src: bytes | bytearray) -> bool:
    try:
        Path(filename).write_bytes(src)
        return True
    except OSError as ex:
        print(f"Error: SaveBinaryFile() {ex}", file=sys.stderr)
        return False

def read_word(off: int, src: bytes | bytearray) -> tuple[int, int] | None:
    if off + 4 > len(src):
        return None
    value = src[off]
    value |= src[off + 1] << 8
    value |= src[off + 2] << 16
    value |= src[off + 3] << 24
    return 4, value & UINT32_MASK


def read_header(off: int, src: bytes | bytearray) -> tuple[int, FlashHeader] | None:
    if off + 16 > len(src):
        return None

    values: list[int] = []
    pos = off
    for _ in range(4):
        result = read_word(pos, src)
        if result is None:
            return None
        consumed, value = result
        pos += consumed
        values.append(value)

    return 16, FlashHeader(
        next_desc=values[0],
        nb_areas=values[1],
        entry=values[2],
        bootaddr=values[3],
    )

def read_mem_area(off: int, src: bytes | bytearray) -> tuple[int, FlashMemArea] | None:
    if off + 16 > len(src):
        return None

    values: list[int] = []
    pos = off
    for _ in range(4):
        result = read_word(pos, src)
        if result is None:
            return None
        consumed, value = result
        pos += consumed
        values.append(value)

    return 16, FlashMemArea(
        start=values[0],
        ptr=values[1],
        size=values[2],
        blocks=values[3],
    )

def crc32(src: bytes | bytearray) -> int:
    dst = 0xFFFFFFFF

    for byte in src:
        dst = (dst ^ byte) & UINT32_MASK
        for _ in range(8):
            if (dst & 0x1) == 0x1:
                dst = ((dst >> 1) ^ 0xEDB88320) & UINT32_MASK
            else:
                dst = (dst >> 1) & UINT32_MASK

    return (~dst) & UINT32_MASK


def append_word(src: int, dst: bytearray) -> None:
    dst.extend((src & UINT32_MASK).to_bytes(4, byteorder="little"))


def append_crc32(dst: bytearray) -> bool:
    try:
        append_word(crc32(dst), dst)
        return True
    except Exception as ex:
        print(f"Error: AppendCRC32() {ex}", file=sys.stderr)
        return False


def append_binary(src: bytes | bytearray, dst: bytearray) -> bool:
    try:
        dst.extend(src)
        return True
    except Exception as ex:
        print(f"Error: AppendBinary() {ex}", file=sys.stderr)
        return False


def append_padding(total: int, dst: bytearray) -> bool:
    try:
        if total > len(dst):
            dst.extend(b"\x00" * (total - len(dst)))
        return True
    except Exception as ex:
        print(f"Error: AppendPadding() {ex}", file=sys.stderr)
        return False


def append_header(src: FlashHeader, dst: bytearray) -> bool:
    try:
        tmp = bytearray()
        append_word(src.next_desc, tmp)
        append_word(src.nb_areas, tmp)
        append_word(src.entry, tmp)
        append_word(src.bootaddr, tmp)
        dst.extend(tmp)
        return True
    except Exception as ex:
        print(f"Error: AppendHeader() {ex}", file=sys.stderr)
        return False


def append_mem_area(src: FlashMemArea, dst: bytearray) -> bool:
    try:
        tmp = bytearray()
        append_word(src.start, tmp)
        append_word(src.ptr, tmp)
        append_word(src.size, tmp)
        append_word(src.blocks, tmp)
        dst.extend(tmp)
        return True
    except Exception as ex:
        print(f"Error: AppendMemArea() {ex}", file=sys.stderr)
        return False


def dump_info(data: bytes | bytearray, silent: bool) -> bool:
    total_size = len(data) & UINT32_MASK
    idx = 0

    if not silent:
        print(f"Output Binary Size = 0x{total_size:x}")

    header_result = read_header(idx, data)
    if header_result is None:
        if not silent:
            print("Error: Invalid Header.", file=sys.stderr)
        return False

    consumed, header = header_result
    idx += consumed

    if not silent:
        print(f"Header.nextDesc = 0x{header.next_desc:x}")
        print(f"Header.nbAreas = {header.nb_areas}")
        print(f"Header.entry = 0x{header.entry:x}")
        print(f"Header.bootaddr = 0x{header.bootaddr:x}")

    for i in range(header.nb_areas):
        area_result = read_mem_area(idx, data)
        if area_result is None:
            if not silent:
                print(f"Error: Invalid Memory Area ({i}).", file=sys.stderr)
            return False

        consumed, area = area_result
        idx += consumed

        if not silent:
            print(f"Area({i}).start = 0x{area.start:x}")
            print(f"Area({i}).ptr = 0x{area.ptr:x}")
            print(f"Area({i}).size = {area.size}")
            print(f"Area({i}).blocks = {area.blocks}")
            print(f"Area({i}) end = 0x{(area.start + area.size) & UINT32_MASK:x}")

    crc_offset = 16 + 64 * header.nb_areas
    if crc_offset > len(data):
        if not silent:
            print("Error: Invalid CRC32 Offset.", file=sys.stderr)
        return False

    crc_result = read_word(crc_offset, data)
    if crc_result is None:
        if not silent:
            print("Error: Invalid CRC32 Value.", file=sys.stderr)
        return False

    _, stored_crc = crc_result
    calculated_crc = crc32(data[:crc_offset])

    if not silent:
        print(f"Header CRC32 = 0x{stored_crc:x}")
        print(f"Calculated CRC32 = 0x{calculated_crc:x}")

    if calculated_crc != stored_crc:
        if not silent:
            print("Error: CRC32 Mismatch.", file=sys.stderr)
        return False

    return True



class PuppyHeaderMaker:

    def __init__(self, filename: Path | None,
                 base_addr: int | None,
                 entry_addr: int | None):
        if filename is None:
            raise RuntimeError("no binary file was specified")
        self.src = bytearray(filename.read_bytes())

        if dump_info(self.src, True):
            raise RuntimeError("binary already has a header")

        RAM_START_ADDR = 0x1C000000
        RAM_LIMIT_ADDR = 0x1C050000

        if base_addr is None:
            base_addr = RAM_START_ADDR
        if entry_addr is None:
            entry_addr = RAM_START_ADDR

        if base_addr + len(self.src) > RAM_LIMIT_ADDR:
            raise RuntimeError("binary is too big")
        if entry_addr < base_addr or entry_addr >= RAM_LIMIT_ADDR:
            raise RuntimeError("invalid entry point address")

        self.header = FlashHeader(
            next_desc = 0, nb_areas = 1,
            entry = entry_addr, bootaddr = base_addr,
        )

    def make_bootable(self, dst_file: str) -> bool:
        FLASH_BLOCK_SIZE = 0x1000

        flash_offset = 16 + 64 * self.header.nb_areas
        crc_offset = flash_offset
        flash_offset += 4
        flash_offset = round_up(flash_offset, FLASH_BLOCK_SIZE)

        area = FlashMemArea(
            start=flash_offset,
            ptr=self.header.bootaddr,
            size=len(self.src) & UINT32_MASK,
            blocks=div_round_up(len(self.src), FLASH_BLOCK_SIZE),
        )

        print(f"Input Binary Size = 0x{area.size:x}")

        flash_offset += area.blocks * FLASH_BLOCK_SIZE
        self.header.next_desc = flash_offset & UINT32_MASK

        dst = bytearray()
        if not append_header(self.header, dst):
            return False
        if not append_mem_area(area, dst):
            return False
        if not append_padding(crc_offset, dst):
            return False
        if not append_crc32(dst):
            return False
        if not append_padding(area.start, dst):
            return False
        if not append_binary(self.src, dst):
            return False
        if not append_padding(flash_offset, dst):
            return False
        if not save_binary_file(dst_file, dst):
            return False

        dump_info(dst, False)
        return True

