#!/usr/bin/env python3
"""RKNano (Rockchip RKNanoD) firmware image tool — FiiO Echo Nano.

Image layout (all little-endian):
  0x000  header: u16 year, u16 monthday (BCD build date, e.g. 2026 0824),
         "Rockchip", "RKnano SDK 1.0", section table. The loader keeps two
         firmware copies in flash and boots the valid one with the newest date.
  0x1F4  u32 firmware length, then "RKnanoFW"
  0x200  module table: u32 load_base, u32 count, count * 8-word entries
         (code_load, code_exec, code_len, data_load, data_exec, data_len,
          bss_exec, bss_len). "load" addresses are load_base-relative
         file offsets (file = addr - load_base + 0x200).
  ...    resource sections (sysdefault, fonts, codepage, lang, images)
  fw_len a copy of the 0x200-byte header (checked by the loader)
  end-4  CRC32 over everything before it: Rockchip CRC (poly 0x04C10DB7,
         MSB-first, init 0), stored LE.

Commands:
  info   IMG                       print header, sections, modules
  unpack IMG OUTDIR                write blobs + manifest.json
  crc    IMG                       verify trailer CRC
  fix    IMG                       recompute and write trailer CRC in place
  patch  IMG OUT PATCH...          apply patches, fix CRC, write OUT
         PATCH = MOD:EXECADDR:VALUE   (MOD = module index, 'code'/'data'
                 region picked by address) or @file with one patch per line.
                 VALUE = hex bytes, u"text" (UTF-16LE + NUL) or a"text" (ASCII + NUL)
         PATCH = lang:ID:LANG:u"text"  replace UI string ID (hex) in language
                 LANG (index, code from LANGS, or * for all)
         --date YYYYMMDD sets the build date (default: keep)
         IMG must be a known stock image (STOCK), else --any-input
  check  IMG                       is IMG a known stock image?

lang section: u16 count, u32[count] block offsets; each block is LANG_SLOTS
slots of LANG_SLOT bytes: 58-byte header (u16 id, ..., prev, next) followed
by the zero-padded UTF-16LE text.
  a2f    IMG MOD EXECADDR          translate exec address -> file offset
"""
import hashlib
import json
import os
import struct
import sys

# Stock images the patches were written for (SHA-256 of the official file).
# Patch addresses are specific to one build: patching anything else can brick.
STOCK = {
    "54a45d233a23c3ce142d1da9c093cc40c46eba417aa91fe915ee867726f4a116":
        "FiiO Echo Nano V1.7.0 (NANOV170.IMG, build date 2026-08-24)",
}


def stock_name(path):
    """Return the STOCK description of the file at `path`, or None."""
    return STOCK.get(hashlib.sha256(open(path, "rb").read()).hexdigest())

TABLE_OFF = 0x200
ENTRY_WORDS = 8
ENTRY_FIELDS = ("code_load", "code_exec", "code_len", "data_load",
                "data_exec", "data_len", "bss_exec", "bss_len")
# header offsets of (offset, length) pairs for resource sections
SECTION_NAMES = ["sysdefault", "font_a", "font_b", "codepage", "lang", "images"]
# lang blocks in section order (identified from the "Albums" string)
LANGS = ["zh-cn", "zh-tw", "en", "ja", "ko", "fr", "de", "it", "es", "pt", "ru",
         "sv", "th", "pl", "da", "nl", "el", "cs", "tr", "he", "ar"]
LANG_SLOT, LANG_HDR, LANG_SLOTS = 258, 58, 431


def _crc_table():
    t = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C10DB7) if c & 0x80000000 else (c << 1)
            c &= 0xFFFFFFFF
        t.append(c)
    return t


_T = _crc_table()


def rkcrc(buf, crc=0):
    for b in buf:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _T[((crc >> 24) ^ b) & 0xFF]
    return crc


class Image:
    def __init__(self, data):
        self.d = bytearray(data)
        if self.d[0x10:0x18] != b"Rockchip" or self.d[0x1F8:0x200] != b"RKnanoFW":
            raise ValueError("not an RKNano firmware image")
        self.load_base = self.u32(TABLE_OFF)
        n = self.u32(TABLE_OFF + 4)
        self.modules = []
        for i in range(n):
            e = struct.unpack_from("<8I", self.d, TABLE_OFF + 8 + i * 32)
            self.modules.append(dict(zip(ENTRY_FIELDS, e), index=i))
        self.sections = self._sections()

    def u32(self, o):
        return struct.unpack_from("<I", self.d, o)[0]

    def l2f(self, a):
        return a - self.load_base + TABLE_OFF

    def _sections(self):
        # 0x70: sysdefault(off,len) font_a(off,len) font_b(off,len)
        # 0xCC/0xF8/0x14C: (off,len) preceded by a 1 flag
        pairs = [(0x70, 0x74), (0x78, 0x7C), (0x80, 0x84),
                 (0xCC, 0xD0), (0xF8, 0xFC), (0x14C, 0x150)]
        out = []
        for name, (po, pl) in zip(SECTION_NAMES, pairs):
            off, ln = self.u32(po), self.u32(pl)
            if name == "sysdefault":  # stored as (off, sectors)
                ln = self.u32(0x78) - off
            out.append(dict(name=name, offset=off, length=ln))
        return out

    @property
    def fw_len(self):
        return self.u32(0x1F4)

    def header_copy_ok(self):
        return self.d[:0x200] == self.d[self.fw_len:self.fw_len + 0x200]

    def sync_header_copy(self):
        self.d[self.fw_len:self.fw_len + 0x200] = self.d[:0x200]

    @property
    def date(self):
        y, md = struct.unpack_from("<HH", self.d, 0)
        return "%04x%04x" % (y, md)

    def set_date(self, yyyymmdd):
        struct.pack_into("<HH", self.d, 0, int(yyyymmdd[:4], 16), int(yyyymmdd[4:], 16))

    def finalize(self):
        self.sync_header_copy()
        self.fix_crc()

    def crc_stored(self):
        return self.u32(len(self.d) - 4)

    def crc_calc(self):
        return rkcrc(self.d[:-4])

    def fix_crc(self):
        struct.pack_into("<I", self.d, len(self.d) - 4, self.crc_calc())

    def region(self, mod, addr):
        """Return (file_offset, max_len) for an exec address inside a module."""
        m = self.modules[mod]
        for kind in ("code", "data"):
            ex, ln = m[kind + "_exec"], m[kind + "_len"]
            if ex <= addr < ex + ln:
                return self.l2f(m[kind + "_load"]) + (addr - ex), ex + ln - addr
        raise ValueError("0x%08x not in module %d code/data" % (addr, mod))

    def patch(self, mod, addr, data):
        off, room = self.region(mod, addr)
        if len(data) > room:
            raise ValueError("patch at 0x%08x overruns module %d" % (addr, mod))
        self.d[off:off + len(data)] = data
        return off

    def lang_text(self, lang, sid):
        """Return the file offset of string `sid`'s text field in language block `lang`."""
        sec = next(s for s in self.sections if s["name"] == "lang")
        n = struct.unpack_from("<H", self.d, sec["offset"])[0]
        if not (0 <= lang < n and 0 <= sid < LANG_SLOTS):
            raise ValueError("lang %d / string 0x%x out of range" % (lang, sid))
        blk = sec["offset"] + struct.unpack_from("<I", self.d, sec["offset"] + 2 + lang * 4)[0]
        slot = blk + sid * LANG_SLOT
        if struct.unpack_from("<H", self.d, slot)[0] != sid:
            raise ValueError("lang %d slot 0x%x has wrong id" % (lang, sid))
        return slot + LANG_HDR

    def patch_lang(self, lang, sid, data):
        room = LANG_SLOT - LANG_HDR
        if len(data) > room:
            raise ValueError("string 0x%x longer than %d bytes" % (sid, room))
        off = self.lang_text(lang, sid)
        self.d[off:off + room] = data.ljust(room, b"\0")
        return off


def load(path):
    with open(path, "rb") as f:
        return Image(f.read())


def cmd_info(img):
    im = load(img)
    print("build date %s  fw length 0x%x  file 0x%x  header copy %s" % (
        im.date, im.fw_len, len(im.d), "OK" if im.header_copy_ok() else "BAD"))
    ok = im.crc_stored() == im.crc_calc()
    print("crc stored 0x%08x %s" % (im.crc_stored(), "OK" if ok else "BAD"))
    print("load_base 0x%08x  modules %d" % (im.load_base, len(im.modules)))
    for s in im.sections:
        print("  section %-10s off 0x%07x len 0x%07x" % (s["name"], s["offset"], s["length"]))
    for m in im.modules:
        if not (m["code_len"] or m["data_len"] or m["bss_len"]):
            continue
        print("  mod %2d code %08x+%-6x (file %07x)  data %08x+%-6x  bss %08x+%x" % (
            m["index"], m["code_exec"], m["code_len"], im.l2f(m["code_load"]),
            m["data_exec"], m["data_len"], m["bss_exec"], m["bss_len"]))


def cmd_unpack(img, out):
    im = load(img)
    os.makedirs(os.path.join(out, "modules"), exist_ok=True)
    os.makedirs(os.path.join(out, "sections"), exist_ok=True)
    man = dict(image=os.path.basename(img), load_base=im.load_base,
               crc=im.crc_stored(), modules=[], sections=[])
    for m in im.modules:
        e = dict(m)
        for kind in ("code", "data"):
            ln = m[kind + "_len"]
            if ln:
                off = im.l2f(m[kind + "_load"])
                fn = "modules/mod%02d_%s_%08x.bin" % (m["index"], kind, m[kind + "_exec"])
                with open(os.path.join(out, fn), "wb") as f:
                    f.write(im.d[off:off + ln])
                e[kind + "_file"] = fn
                e[kind + "_file_offset"] = off
        man["modules"].append(e)
    for s in im.sections:
        fn = "sections/%s.bin" % s["name"]
        with open(os.path.join(out, fn), "wb") as f:
            f.write(im.d[s["offset"]:s["offset"] + s["length"]])
        man["sections"].append(dict(s, file=fn))
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(man, f, indent=1)
    print("unpacked to", out)


def parse_patches(args):
    for a in args:
        if a.startswith("@"):
            with open(a[1:]) as f:
                lines = [l.split("#")[0].strip() for l in f]
            yield from parse_patches([l for l in lines if l])
            continue
        if a.startswith("lang:"):
            _, sid, lang, val = a.split(":", 3)
            langs = range(len(LANGS)) if lang == "*" else [
                LANGS.index(lang) if lang in LANGS else int(lang, 0)]
            for l in langs:
                yield "lang", (l, int(sid, 16)), _value(val)
            continue
        mod, addr, val = a.split(":", 2)
        yield int(mod, 0), int(addr, 16), _value(val)


def _value(val):
    if val[:2] in ('u"', 'a"') and val.endswith('"'):
        enc = "utf-16le" if val[0] == "u" else "ascii"
        return (val[2:-1] + "\0").encode(enc)
    return bytes.fromhex(val.replace(" ", ""))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    c = argv[1]
    if c == "info":
        cmd_info(argv[2])
    elif c == "unpack":
        cmd_unpack(argv[2], argv[3])
    elif c == "crc":
        im = load(argv[2])
        s, k = im.crc_stored(), im.crc_calc()
        print("stored 0x%08x calc 0x%08x %s" % (s, k, "OK" if s == k else "MISMATCH"))
        print("header copy", "OK" if im.header_copy_ok() else "MISMATCH")
        sys.exit(0 if s == k and im.header_copy_ok() else 1)
    elif c == "fix":
        im = load(argv[2])
        im.finalize()
        open(argv[2], "wb").write(im.d)
        print("crc 0x%08x" % im.crc_stored())
    elif c == "check":
        name = stock_name(argv[2])
        print("%s: %s" % (argv[2], name or "NOT a known stock image"))
        sys.exit(0 if name else 1)
    elif c == "patch":
        args = argv[4:]
        if args[:1] == ["--any-input"]:
            args = args[1:]
        elif not stock_name(argv[2]):
            sys.exit("%s is not a known stock image (see STOCK in %s); the patches "
                     "only fit that exact build. Use --any-input to override."
                     % (argv[2], os.path.basename(__file__)))
        im = load(argv[2])
        if args[:1] == ["--date"]:
            im.set_date(args[1])
            args = args[2:]
        for mod, addr, data in parse_patches(args):
            if mod == "lang":
                off = im.patch_lang(*addr, data)
                print("lang %s string 0x%x (file 0x%x): %r" % (
                    LANGS[addr[0]], addr[1], off, data.decode("utf-16le").rstrip("\0")))
                continue
            off = im.patch(mod, addr, data)
            print("mod %d 0x%08x (file 0x%x): %s" % (mod, addr, off, data.hex()))
        im.finalize()
        open(argv[3], "wb").write(im.d)
        print("wrote %s date %s crc 0x%08x" % (argv[3], im.date, im.crc_stored()))
    elif c == "a2f":
        im = load(argv[2])
        off, room = im.region(int(argv[3], 0), int(argv[4], 16))
        print("file 0x%x (%d bytes left in region)" % (off, room))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
