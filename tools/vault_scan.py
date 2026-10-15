#!/usr/bin/env python3
# Asset Vault scanner / server-catalog generator.
#
# Parses a Godot .pck (GDPC v2/v3/v4, unencrypted directory) and lists the
# vault-encrypted assets (PACK_FILE_VAULT entries at "res://.vault/<uid-text>.enc",
# GVAU containers). Emits the per-asset catalog {uid, key_tag} the key-granting
# server derives keys from:
#
#   key = HKDF-SHA256(IKM = master, salt = "godot-asset-vault-v1",
#                    info = utf8("uid://<uid-text>:<tag>"), L = 16)
#
# With --decrypt it derives the keys from the master key, decrypts every
# vault payload and verifies the AES-128-GCM tag and the payload magic —
# the same checks the runtime performs. Uses the `cryptography` package when
# available and falls back to a self-contained pure-Python AES-128-GCM
# implementation otherwise (no third-party dependencies required).
#
# With --audit --project-dir <dir> it mechanically verifies that the pack
# leaks nothing about the vault-encrypted assets beyond what is visible by
# design (each GVAU header is plaintext: uid, key tag, scheme, payload
# length; plus per-asset sizes and the asset count):
#   1. every vault entry carries PACK_FILE_VAULT and its uniform path matches
#      its embedded uid; no plaintext entry lives under ".vault/",
#   2. no entry is stored under a vault asset's original path,
#   3. no entry's path or bytes contain a vault asset's original path/name —
#      including the decompressed contents of compressed entries (".gdc" script
#      tokenizer buffers and "RSCC" binary resources; basenames are reported as
#      warnings: same-named unrelated files are not a leak),
#   4. ".godot/uid_cache.bin" maps vault uids to their vault paths only, to
#      paths that exist in the pack, and no uid at all to an original path,
#   5. every entry's md5 matches the directory,
#   6. no entry duplicates a vault asset's contents in plaintext (source bytes
#      from the project; decrypted payloads too when the master key is given).
# Exits non-zero on findings, so it can run in CI.
#
# Usage:
#   python3 tools/vault_scan.py game.pck                        # list + catalog
#   python3 tools/vault_scan.py game.pck --json catalog.json    # write catalog
#   GODOT_VAULT_MASTERKEY=hex... python3 tools/vault_scan.py game.pck --decrypt
#   python3 tools/vault_scan.py game.pck --master-key hex... --decrypt --out decrypted/
#   python3 tools/vault_scan.py game.pck --audit --project-dir /path/to/project
import argparse
import hashlib
import hmac
import json
import os
import struct
import sys

try:
    import compression.zstd as zstd  # Python 3.14+.
except ImportError:
    zstd = None

PACK_HEADER_MAGIC = 0x43504447  # "GDPC" (stored little-endian).

PACK_DIR_ENCRYPTED = 1 << 0
PACK_REL_FILEBASE = 1 << 1
PACK_SPARSE_BUNDLE = 1 << 2

PACK_FILE_ENCRYPTED = 1 << 0
PACK_FILE_REMOVAL = 1 << 1
PACK_FILE_DELTA = 1 << 2
PACK_FILE_VAULT = 1 << 3

# GVAU container header (see core/io/vault_container.h).
GVAU_HEADER_SIZE = 64
GVAU_AAD_SIZE = 44
GVAU_TAG_SIZE = 16
GVAU_NONCE_SIZE = 12

# Supported payload formats (first 4 bytes of the decrypted payload; see
# VaultContainer::is_*_payload in core/io/vault_container.h): (compressed)
# generic binary resources (".res"/".scn"/".sample"/".translation"/".fontdata"/...)
# and compressed textures ("GST2" = CompressedTexture2D, "GSTL" = layered/3D).
SUPPORTED_PAYLOAD_MAGICS = [b"RSRC", b"RSCC", b"GST2", b"GSTL"]

# ResourceUID text charset (core/io/resource_uid.cpp): base 34, 'a'-'y' then '0'-'8'.
UID_CHARACTERS = "abcdefghijklmnopqrstuvwxy" + "012345678"
UID_BASE = len(UID_CHARACTERS)

VAULT_HKDF_SALT = b"godot-asset-vault-v1"


def uid_to_text(uid: int) -> str:
    if uid < 0:
        return "uid://<invalid>"  # Mirrors ResourceUID::id_to_text().
    chars = []
    while uid:
        chars.append(UID_CHARACTERS[uid % UID_BASE])
        uid //= UID_BASE
    return "uid://" + ("".join(reversed(chars)) if chars else "a")


def hkdf_sha256(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    # RFC 5869 extract-and-expand.
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    okm = b""
    t = b""
    counter = 1
    while len(okm) < length:
        t = hmac.new(prk, t + info + bytes([counter]), hashlib.sha256).digest()
        okm += t
        counter += 1
    return okm[:length]


def derive_vault_key(master: bytes, uid_text: str, key_tag: int) -> bytes:
    return hkdf_sha256(master, VAULT_HKDF_SALT, f"{uid_text}:{key_tag}".encode("utf-8"), 16)


def read_u32(f):
    return struct.unpack("<I", f.read(4))[0]


def read_u64(f):
    return struct.unpack("<Q", f.read(8))[0]


class PckDirectoryEntry:
    def __init__(self, path, offset, size, md5, flags, file_base):
        self.path = path
        self.offset = file_base + offset
        self.size = size
        self.md5 = md5
        self.flags = flags


def parse_pck(pck_path: str):
    f = open(pck_path, "rb")
    try:
        if read_u32(f) != PACK_HEADER_MAGIC:
            raise SystemExit(f"error: '{pck_path}' is not a Godot .pck (bad magic).")
        format_version = read_u32(f)
        if format_version not in (2, 3, 4):
            raise SystemExit(f"error: unsupported pck format version {format_version}.")
        f.read(12)  # Engine version (u32 x3).
        pack_flags = read_u32(f)
        if pack_flags & PACK_DIR_ENCRYPTED:
            raise SystemExit(
                "error: the pack directory is script-encrypted (PACK_DIR_ENCRYPTED). "
                "vault_scan.py does not decrypt packed directories; export without "
                "'Encrypt Index' to scan it."
            )
        if pack_flags & PACK_SPARSE_BUNDLE:
            raise SystemExit("error: sparse bundles are not supported by vault_scan.py.")
        file_base = read_u64(f)  # PACK_REL_FILEBASE: relative to pack start (we read from 0).
        if format_version == 2:
            # V2: no directory offset; 16 reserved u32s, then the directory inline.
            for _ in range(16):
                read_u32(f)  # Reserved.
        else:
            dir_offset = read_u64(f)
            f.seek(dir_offset)
        file_count = read_u32(f)
        entries = []
        for _ in range(file_count):
            sl = read_u32(f)
            # The stored length is NUL-padded to a 4-byte multiple (the padding is
            # part of it); the C++ reader truncates the path at the first NUL.
            path = f.read(sl).split(b"\x00")[0].decode("utf-8")
            ofs = read_u64(f)
            size = read_u64(f)
            md5 = f.read(16)
            flags = read_u32(f)
            if flags & PACK_FILE_REMOVAL:
                continue
            entries.append(PckDirectoryEntry(path, ofs, size, md5, flags, file_base))
    except Exception:
        f.close()
        raise
    return entries, f


def parse_gvau_header(blob: bytes):
    if len(blob) < GVAU_HEADER_SIZE or blob[0:4] != b"GVAU":
        raise ValueError("not a GVAU container")
    version, flags = struct.unpack_from("<HH", blob, 4)
    uid, = struct.unpack_from("<q", blob, 8)
    scheme = blob[16]
    key_tag, = struct.unpack_from("<I", blob, 18)
    nonce = blob[24:36]
    plain_length, = struct.unpack_from("<Q", blob, 36)
    gcm_tag = blob[44:60]
    return {
        "version": version,
        "flags": flags,
        "uid": uid,
        "scheme": scheme,
        "key_tag": key_tag,
        "nonce": nonce,
        "plain_length": plain_length,
        "gcm_tag": gcm_tag,
    }


def decrypt_gvau(container: bytes, key: bytes):
    hdr = parse_gvau_header(container)
    aad = container[:GVAU_AAD_SIZE]
    # NOTE: the pck aligns entries by writing zero padding *between* entries;
    # the directory size covers only the entry itself, so `container` never has
    # trailing padding. Slice the ciphertext to plain_length anyway (GCM is a
    # stream mode), so this stays correct for pack generators whose stored
    # sizes do include alignment padding.
    ciphertext = container[GVAU_HEADER_SIZE:GVAU_HEADER_SIZE + hdr["plain_length"]]
    if len(ciphertext) != hdr["plain_length"]:
        raise ValueError("ciphertext length mismatch")

    plain = aes128_gcm_decrypt(key, hdr["nonce"], ciphertext, aad, hdr["gcm_tag"])
    return hdr, plain


# --- Pure-Python AES-128 + GCM (NIST SP 800-38D; used when `cryptography` is
# unavailable). Only the AES *forward* cipher is needed for GCM.

_AES_SBOX = bytes.fromhex(
    "637c777bf26b6fc53001672bfed7ab76"
    "ca82c97dfa5947f0add4a2af9ca472c0"
    "b7fd9326363ff7cc34a5e5f171d83115"
    "04c723c31896059a071280e2eb27b275"
    "09832c1a1b6e5aa0523bd6b329e32f84"
    "53d100ed20fcb15b6acbbe394a4c58cf"
    "d0efaafb434d338545f9027f503c9fa8"
    "51a3408f929d38f5bcb6da2110fff3d2"
    "cd0c13ec5f974417c4a77e3d645d1973"
    "60814fdc222a908846eeb814de5e0bdb"
    "e0323a0a4906245cc2d3ac629195e479"
    "e7c8376d8dd54ea96c56f4ea657aae08"
    "ba78252e1ca6b4c6e8dd741f4bbd8b8a"
    "703eb5664803f60e613557b986c11d9e"
    "e1f8981169d98e949b1e87e9ce5528df"
    "8ca1890dbfe6426841992d0fb054bb16"
)
_AES_RCON = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36]


def _xtime(a: int) -> int:
    a <<= 1
    return (a ^ 0x1B) & 0xFF if a & 0x100 else a


def _aes128_expand_key(key: bytes) -> list:
    w = [list(key[4 * i: 4 * i + 4]) for i in range(4)]
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [_AES_SBOX[b] for b in t]
            t[0] ^= _AES_RCON[i // 4 - 1]
        w.append([a ^ b for a, b in zip(w[i - 4], t)])
    return w


def _aes128_encrypt_block(round_keys: list, block: bytes) -> bytes:
    def add_round_key(s: list, rk: list) -> None:
        for i in range(16):
            s[i] ^= rk[i]

    def sub_bytes(s: list) -> None:
        for i in range(16):
            s[i] = _AES_SBOX[s[i]]

    def shift_rows(s: list) -> None:
        for r in range(1, 4):
            row = [s[r + 4 * c] for c in range(4)]
            for c in range(4):
                s[r + 4 * c] = row[(c + r) % 4]

    def mix_columns(s: list) -> None:
        for c in range(4):
            a = [s[4 * c + i] for i in range(4)]
            t = a[0] ^ a[1] ^ a[2] ^ a[3]
            s[4 * c + 0] = a[0] ^ t ^ _xtime(a[0] ^ a[1])
            s[4 * c + 1] = a[1] ^ t ^ _xtime(a[1] ^ a[2])
            s[4 * c + 2] = a[2] ^ t ^ _xtime(a[2] ^ a[3])
            s[4 * c + 3] = a[3] ^ t ^ _xtime(a[3] ^ a[0])

    state = list(block)
    add_round_key(state, sum(round_keys[0:4], []))
    for r in range(1, 10):
        sub_bytes(state)
        shift_rows(state)
        mix_columns(state)
        add_round_key(state, sum(round_keys[4 * r: 4 * r + 4], []))
    sub_bytes(state)
    shift_rows(state)
    add_round_key(state, sum(round_keys[40:44], []))
    return bytes(state)


# GF(2^128) multiplication for GHASH (NIST SP 800-38D): bit strings are
# interpreted with the MSB as the x^0 coefficient; R = x^128 + x^7 + x^2 + x + 1.

def _gf_mult(x: int, y: int) -> int:
    R = 0xE1 << 120
    z = 0
    v = y
    for i in range(127, -1, -1):
        if (x >> i) & 1:
            z ^= v
        if v & 1:
            v = (v >> 1) ^ R
        else:
            v >>= 1
    return z


def _ghash(h: int, aad: bytes, ciphertext: bytes) -> int:
    data = aad + b"\x00" * ((16 - len(aad) % 16) % 16)
    data += ciphertext + b"\x00" * ((16 - len(ciphertext) % 16) % 16)
    data += (len(aad) * 8).to_bytes(8, "big") + (len(ciphertext) * 8).to_bytes(8, "big")
    y = 0
    for i in range(0, len(data), 16):
        y = _gf_mult(y ^ int.from_bytes(data[i:i + 16], "big"), h)
    return y


def _inc32(counter: bytes) -> bytes:
    n = int.from_bytes(counter, "big") + 1
    return n.to_bytes(16, "big")


def aes128_gcm_decrypt(key: bytes, nonce: bytes, ciphertext: bytes, aad: bytes, tag: bytes) -> bytes:
    """AES-128-GCM decrypt-and-verify. Raises ValueError on tag mismatch."""
    try:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM

        return AESGCM(key).decrypt(nonce, ciphertext + tag, aad)
    except ImportError:
        pass  # Fall back to the self-contained implementation below.

    round_keys = _aes128_expand_key(key)

    def aes_block(block: bytes) -> bytes:
        return _aes128_encrypt_block(round_keys, block)

    h = int.from_bytes(aes_block(b"\x00" * 16), "big")
    if len(nonce) == 12:
        j0 = nonce + b"\x00\x00\x00\x01"
    else:
        j0 = _ghash(h, b"", nonce).to_bytes(16, "big")

    # CTR keystream (GCM's encryption operation is the same for both directions).
    plain = bytearray()
    counter = j0
    for i in range(0, len(ciphertext), 16):
        counter = _inc32(counter)
        keystream = aes_block(counter)
        block = ciphertext[i:i + 16]
        plain += bytes(a ^ b for a, b in zip(block, keystream))

    expected = (_ghash(h, aad, ciphertext) ^ int.from_bytes(aes_block(j0), "big")).to_bytes(16, "big")
    if expected != tag:
        raise ValueError("GCM tag mismatch")
    return bytes(plain)


# Godot Compression::Mode (core/io/compression.h): 0 = FASTLZ, 1 = DEFLATE,
# 2 = ZSTD (the default used by both formats below), 3 = GZIP, 4 = BROTLI.
COMPRESSION_MODE_ZSTD = 2


def entry_contents(body):
    """Returns (contents, decompressed) for a pack entry's bytes.

    Decompresses the compressed entry formats so deep scans (string search,
    content comparison) see what the runtime sees:
    - "GDSC": GDScript tokenizer buffers (.gdc) — magic + u32 version +
      u32 decompressed size (0 = uncompressed) + a single zstd frame.
    - "RSCC": FileAccessCompressed binary resources — magic + u32 mode +
      u32 block size + u32 total size + per-block compressed sizes + zstd
      blocks (core/io/file_access_compressed.cpp).
    Other entries are returned as stored. Compressed entries with unsupported
    compression modes (or missing zstd support) are returned as stored with
    decompressed = False.
    """
    try:
        if body[:4] == b"GDSC":
            decompressed_size, = struct.unpack_from("<I", body, 8)
            if decompressed_size == 0:
                return body[12:], True
            if zstd is None:
                return body, False
            contents = zstd.decompress(body[12:])
            return contents, len(contents) == decompressed_size
        if body[:4] == b"RSCC":
            cmode, block_size, read_total = struct.unpack_from("<III", body, 4)
            if cmode != COMPRESSION_MODE_ZSTD or zstd is None:
                return body, False
            block_count = read_total // block_size + 1
            p = 16
            sizes = struct.unpack_from(f"<{block_count}I", body, p)
            p += block_count * 4
            contents = bytearray()
            for compressed_size in sizes:
                contents += zstd.decompress(body[p:p + compressed_size])
                p += compressed_size
            return bytes(contents), len(contents) == read_total
    except Exception:  # noqa: BLE001 — corrupt/unsupported compressed entry.
        return body, False
    return body, True


def audit_pack(args, entries, pack_file, master):
    """Mechanical leak audit for vault-encrypted assets.

    Verifies that the pack leaks nothing about them beyond the by-design
    metadata (plaintext GVAU headers carry uid, key tag, scheme and payload
    length; per-asset container sizes and the asset count are visible too).
    Returns the number of hard findings (errors); exit non-zero on any.
    """
    errors = 0
    warnings = 0

    def finding(severity, msg):
        nonlocal errors, warnings
        if severity == "ERROR":
            errors += 1
        elif severity == "WARN":
            warnings += 1
        print(f"  {severity}: {msg}", file=sys.stderr)

    vault_entries = [e for e in entries if e.flags & PACK_FILE_VAULT]
    other = [e for e in entries if not (e.flags & PACK_FILE_VAULT)]

    def entry_bytes(e):
        pack_file.seek(e.offset)
        return pack_file.read(e.size)

    print(f"auditing {args.pck} ({len(entries)} files, {len(vault_entries)} vault-encrypted)")

    # --- 1. Vault entry sanity; no plaintext entries under ".vault/". ---
    vault_by_uid_text = {}
    for e in vault_entries:
        try:
            hdr = parse_gvau_header(entry_bytes(e))
        except ValueError as err:
            finding("ERROR", f"{e.path}: not a GVAU container ({err})")
            continue
        uid_text = uid_to_text(hdr["uid"])
        expected = f".vault/{uid_text[len('uid://'):]}.enc"
        if e.path != expected:
            finding("ERROR", f"vault entry path {e.path} does not match its uid (expected {expected})")
        if hdr["scheme"] != 1:
            finding("ERROR", f"{e.path}: unknown scheme {hdr['scheme']}")
        vault_by_uid_text[uid_text] = e
    for e in other:
        if e.path.startswith(".vault/"):
            finding("ERROR", f"plaintext entry stored under a vault path: {e.path}")
    entry_paths = {e.path for e in entries}

    # --- Secrets: original paths of the encrypted assets, from the ".vault" sidecars. ---
    secrets = {}  # original posix rel path -> {"uid", "tag", "basename", "source_md5"}
    if args.project_dir:
        project_dir = os.path.abspath(args.project_dir)
        for root, dirs, files in os.walk(project_dir):
            dirs[:] = [d for d in dirs if d not in (".git", ".godot")]
            for fn in files:
                if not fn.endswith(".vault"):
                    continue
                sidecar = os.path.join(root, fn)
                orig = sidecar[: -len(".vault")]
                rel = os.path.relpath(orig, project_dir).replace(os.sep, "/")
                sec = {"uid": None, "tag": None}
                # ".vault" sidecar = ConfigFile written by EditorVault ([vault] uid/tag).
                try:
                    with open(sidecar, encoding="utf-8") as sf:
                        for line in sf:
                            line = line.strip()
                            if line.startswith("uid="):
                                sec["uid"] = line.split("=", 1)[1].strip('"')
                            elif line.startswith("tag="):
                                try:
                                    sec["tag"] = int(line.split("=", 1)[1])
                                except ValueError:
                                    pass
                except OSError as err:
                    finding("WARN", f"cannot read sidecar {sidecar}: {err}")
                if os.path.exists(orig):
                    with open(orig, "rb") as of:
                        sec["source_md5"] = hashlib.md5(of.read()).hexdigest()
                sec["basename"] = os.path.basename(rel)
                secrets[rel] = sec
        if not secrets:
            finding("WARN", f"no .vault sidecars found under {args.project_dir}")
    else:
        finding("WARN", "no --project-dir given: original-path, name and source-content checks are skipped")

    # --- 2. No entry stored under a vault asset's original path. ---
    for rel in secrets:
        for e in entries:
            if e.path == rel:
                finding("ERROR", f"vault asset {rel} is also stored plaintext at '{e.path}'")

    # --- 3. No original paths/names inside any plaintext entry. ---
    # Full relative paths (and their res:// form) are hard errors; bare basenames
    # are warnings (an unrelated file with the same name is not a leak).
    probes = []
    for rel, sec in secrets.items():
        probes.append((rel, "ERROR"))
        probes.append(("res://" + rel, "ERROR"))
        if sec["basename"] not in (rel, "res://" + rel):
            probes.append((sec["basename"], "WARN"))
    undecompressed = []
    for e in other:
        raw = entry_bytes(e)
        contents, ok = entry_contents(raw)
        if not ok:
            undecompressed.append(e.path)
        for probe, severity in probes:
            hit = None
            i = raw.find(probe.encode("utf-8"))
            if i >= 0:
                hit = raw
            elif contents is not raw:
                i = contents.find(probe.encode("utf-8"))
                if i >= 0:
                    hit = contents
            if hit is not None:
                ctx = hit[max(0, i - 40):i + len(probe) + 40]
                extra = "" if hit is raw else " (in decompressed content)"
                finding(severity, f"'{e.path}' contains '{probe}'{extra} (context: {ctx!r})")
            elif probe in e.path:
                finding(severity, f"entry path '{e.path}' reveals a vault asset's original name ('{probe}')")
    if undecompressed:
        finding("WARN", f"{len(undecompressed)} compressed entries could not be decompressed for the deep scan (unsupported mode or corrupt): {', '.join(undecompressed[:8])}")

    # --- 4. ".godot/uid_cache.bin" must map vault uids to vault paths only. ---
    uc_entry = next((e for e in entries if e.path == ".godot/uid_cache.bin"), None)
    if uc_entry is None:
        finding("WARN", "no .godot/uid_cache.bin in the pack")
    else:
        uc = entry_bytes(uc_entry)
        count = struct.unpack_from("<I", uc, 0)[0]  # StreamPeerBuffer stores little-endian.
        p = 4
        for i in range(count):
            uid, = struct.unpack_from("<Q", uc, p)
            p += 8
            ln, = struct.unpack_from("<I", uc, p)
            p += 4
            mapped = uc[p:p + ln].decode("utf-8", "replace")
            p += ln
            uid_text = uid_to_text(uid)
            if uid_text in vault_by_uid_text:
                expected = "res://" + vault_by_uid_text[uid_text].path
                if mapped != expected:
                    finding("ERROR", f"uid_cache maps vault uid {uid_text} to '{mapped}' (expected '{expected}')")
            if mapped.startswith("res://.vault/") and mapped[len("res://"):] not in entry_paths:
                finding("ERROR", f"uid_cache maps uid {uid_text} to '{mapped}' but no such entry exists in the pack (the vault emission was skipped)")
            for rel in secrets:
                if mapped == "res://" + rel:
                    finding("ERROR", f"uid_cache maps uid {uid_text} to the original path of vault asset {rel}")

    # --- 5. Every non-delta entry's md5 must match the directory (integrity). ---
    delta_entries = [e for e in entries if e.flags & PACK_FILE_DELTA]
    if delta_entries:
        finding("WARN", f"{len(delta_entries)} delta-patched entries skipped by the md5 integrity check (patch packs store delta streams, better audited against a full export)")
    for e in entries:
        if e.flags & PACK_FILE_DELTA:
            continue
        actual = hashlib.md5(entry_bytes(e)).hexdigest()
        if actual != e.md5.hex():
            finding("ERROR", f"'{e.path}': md5 mismatch (directory {e.md5.hex()}, actual {actual}) — corrupted or tampered")

    # --- 6. No entry may duplicate a vault asset's contents in plaintext ---
    # ---    (compared on decompressed contents, so compressed copies count). ---
    other_md5 = {e: hashlib.md5(entry_contents(entry_bytes(e))[0]).hexdigest() for e in other}
    for rel, sec in secrets.items():
        src_md5 = sec.get("source_md5")
        if src_md5:
            for e, m in other_md5.items():
                if m == src_md5:
                    finding("ERROR", f"'{e.path}' duplicates the plaintext source of vault asset {rel}")
    if master:
        for uid_text, e in vault_by_uid_text.items():
            hdr = parse_gvau_header(entry_bytes(e))
            key = derive_vault_key(master, uid_text, hdr["key_tag"])
            try:
                _, plain = decrypt_gvau(entry_bytes(e), key)
            except Exception:  # noqa: BLE001 — wrong master or stale key tag.
                finding("WARN", f"{e.path}: cannot decrypt (wrong master or stale key tag); decrypted-content check skipped")
                continue
            plain_md5 = hashlib.md5(plain).hexdigest()
            for oe, m in other_md5.items():
                if m == plain_md5:
                    finding("ERROR", f"'{oe.path}' duplicates the decrypted content of vault asset {uid_text}")

    print("  note (by design): vault asset uids, key tags, schemes and payload lengths are visible in the")
    print("  note (by design): plaintext GVAU headers, as are per-asset container sizes and the asset count.")
    if errors or warnings:
        print(f"audit: {errors} error(s), {warnings} warning(s)")
    else:
        print("audit: no leaks found (beyond the by-design metadata above)")
    return errors


def main():
    parser = argparse.ArgumentParser(description="Asset Vault scanner / catalog generator")
    parser.add_argument("pck", help="path to the exported .pck")
    parser.add_argument("--json", dest="json_out", help="write the {uid, tag} catalog to this file")
    parser.add_argument("--master-key", help="vault master key (hex); env GODOT_VAULT_MASTERKEY is used as fallback")
    parser.add_argument("--decrypt", action="store_true", help="decrypt all vault payloads (uses the `cryptography` package when available)")
    parser.add_argument("--out", help="directory to write decrypted payloads to (implies --decrypt)")
    parser.add_argument("--audit", action="store_true", help="audit the pack for leaks of vault asset metadata (original paths, names, contents)")
    parser.add_argument("--project-dir", help="project directory with the \".vault\" sidecars (used by --audit to know the original paths)")
    args = parser.parse_args()

    if args.out and not args.decrypt:
        args.decrypt = True

    master_hex = args.master_key or os.environ.get("GODOT_VAULT_MASTERKEY", "")
    master = bytes.fromhex(master_hex.removeprefix("0x").strip()) if master_hex else b""
    if master and len(master) < 32:
        print(f"warning: master key is {len(master)} bytes; 32+ recommended", file=sys.stderr)
    if args.decrypt and not master:
        raise SystemExit("error: --decrypt requires --master-key or GODOT_VAULT_MASTERKEY")

    entries, pack_file = parse_pck(args.pck)
    try:
        vault_entries = [e for e in entries if e.flags & PACK_FILE_VAULT]
        other = [e for e in entries if not e.flags & PACK_FILE_VAULT]

        print(f"{args.pck}: {len(entries)} files, {len(vault_entries)} vault-encrypted")
        catalog = []
        for e in vault_entries:
            if not (e.path.startswith(".vault/") and e.path.endswith(".enc")):
                print(f"  ERROR: vault entry with non-uniform path: {e.path}", file=sys.stderr)
                continue
            pack_file.seek(e.offset)
            container = pack_file.read(e.size)
            hdr = parse_gvau_header(container)
            uid_text = uid_to_text(hdr["uid"])
            expected_path = f".vault/{uid_text[len('uid://'):]}.enc"
            if e.path != expected_path:
                print(f"  ERROR: vault path {e.path} does not match its uid ({expected_path})", file=sys.stderr)
            catalog.append({"uid": uid_text, "key_tag": hdr["key_tag"]})
            line = f"  {e.path}  uid={uid_text} key_tag={hdr['key_tag']} scheme={hdr['scheme']} plain={hdr['plain_length']}B"
            if hdr["scheme"] != 1:
                line += "  (UNKNOWN SCHEME)"
            print(line)

        if args.json_out:
            with open(args.json_out, "w", encoding="utf-8") as jf:
                json.dump({"vault": catalog}, jf, indent=2)
            print(f"catalog written to {args.json_out}")

        if args.decrypt:
            if not vault_entries:
                print("nothing to decrypt")
                return
            if args.out:
                os.makedirs(args.out, exist_ok=True)
            ok = 0
            for e in vault_entries:
                pack_file.seek(e.offset)
                container = pack_file.read(e.size)
                hdr = parse_gvau_header(container)
                uid_text = uid_to_text(hdr["uid"])
                key = derive_vault_key(master, uid_text, hdr["key_tag"])
                try:
                    _, plain = decrypt_gvau(container, key)
                except Exception:  # noqa: BLE001 — InvalidTag or format errors.
                    print(f"  DECRYPT FAILED: {e.path} (uid={uid_text} key_tag={hdr['key_tag']}) — wrong master or stale tag", file=sys.stderr)
                    sys.exit(1)
                if plain[0:4] not in SUPPORTED_PAYLOAD_MAGICS:
                    print(f"  ERROR: {e.path}: payload has unsupported magic {plain[0:4]!r} (expected one of {SUPPORTED_PAYLOAD_MAGICS})", file=sys.stderr)
                    sys.exit(1)
                if args.out:
                    name = e.path.replace("/", "_")
                    with open(os.path.join(args.out, name), "wb") as out:
                        out.write(plain)
                ok += 1
            print(f"decrypted and verified {ok}/{len(vault_entries)} vault payloads (GCM + payload magic checks passed)")

        # Leak audit (supersedes the old .import/.remap heuristic scan).
        if args.audit:
            if audit_pack(args, entries, pack_file, master) > 0:
                sys.exit(1)
    finally:
        pack_file.close()


if __name__ == "__main__":
    main()
