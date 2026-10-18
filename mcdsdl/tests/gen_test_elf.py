# -*- coding: utf-8 -*-
"""Generate a synthetic ELF32 LE (TriCore) with a few symbols for the
mcdsdl smoke test (the symbol table cross-check)."""
import struct

# the symbols: (name, value, size, type)  type: 1=OBJECT, 2=FUNC
SYMS = [
    ("test_func_a", 0x80000100, 0x40, 2),
    ("test_func_b", 0x80000200, 0x20, 2),
    ("test_data_x", 0x70000000, 0x10, 1),
    ("test_data_y", 0x70000010, 0x04, 1),
]

# ---- the string tables
strtab = b"\x00"
name_off = {}
for name, _, _, _ in SYMS:
    name_off[name] = len(strtab)
    strtab += name.encode() + b"\x00"

shstrtab = b"\x00"
sh_name = {}
for s in (".text", ".symtab", ".strtab", ".shstrtab"):
    sh_name[s] = len(shstrtab)
    shstrtab += s.encode() + b"\x00"

# ---- the .text (a small dummy body)
text = b"\x91\x00\x00\x00" * 16   # some bytes

# ---- the .symtab
symtab = b"\x00" * 16              # the null symbol
for name, value, size, stype in SYMS:
    info = (1 << 4) | stype        # GLOBAL | type
    symtab += struct.pack("<IIIBBH", name_off[name], value, size, info, 0, 1)

# ---- the layout
ehsize = 52
shentsize = 40
off_text = ehsize
off_symtab = off_text + len(text)
off_strtab = off_symtab + len(symtab)
off_shstrtab = off_strtab + len(strtab)
off_sh = (off_shstrtab + len(shstrtab) + 3) & ~3
shnum = 5

# ---- the ELF header
e_ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\x00" * 8
ehdr = e_ident + struct.pack("<HHIIIIIHHHHHH",
    2,          # e_type = ET_EXEC
    189,        # e_machine = EM_TRICORE
    1,          # e_version
    0x80000100, # e_entry
    0,          # e_phoff
    off_sh,     # e_shoff
    0,          # e_flags
    ehsize, 0, 0, shentsize, shnum, 4)  # e_shstrndx = 4

# ---- the section headers
def shdr(name, stype, flags, addr, off, size, link, info, align, entsize):
    return struct.pack("<IIIIIIIIII", name, stype, flags, addr, off, size,
                       link, info, align, entsize)

shdrs = b""
shdrs += shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)                      # 0 NULL
shdrs += shdr(sh_name[".text"], 1, 6, 0x80000100, off_text,
              len(text), 0, 0, 4, 0)                             # 1 PROGBITS
shdrs += shdr(sh_name[".symtab"], 2, 0, 0, off_symtab, len(symtab),
              3, 1, 4, 16)                                       # 2 SYMTAB (link=3!)
shdrs += shdr(sh_name[".strtab"], 3, 0, 0, off_strtab, len(strtab),
              0, 0, 1, 0)                                        # 3 STRTAB
shdrs += shdr(sh_name[".shstrtab"], 3, 0, 0, off_shstrtab,
              len(shstrtab), 0, 0, 1, 0)                         # 4 SHSTRTAB

blob = ehdr + text + symtab + strtab + shstrtab
blob += b"\x00" * (off_sh - len(blob))
blob += shdrs

out = r"C:\work\mcdsdl\tests\test_elf.elf"
open(out, "wb").write(blob)
print("wrote", out, len(blob), "bytes")
print("symbols:", [(n, hex(v)) for n, v, _, _ in SYMS])
