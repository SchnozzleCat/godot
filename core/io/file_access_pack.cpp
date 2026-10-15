/**************************************************************************/
/*  file_access_pack.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "file_access_pack.h"

#include "core/io/asset_vault.h"
#include "core/io/file_access_encrypted.h"
#include "core/io/file_access_memory.h"
#include "core/io/file_access_patched.h"
#include "core/io/vault_container.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/version.h"

#include <string.h>

// Asset Vault: single-slot per-thread memo of the last decrypted container.
//
// Loading one vault asset opens the same container several times in a row
// (uid and type hints, payload magic peeks by recognize_path, then the actual
// load); without a memo, each open repeats the AES-128-GCM pass (~3.3 ms per
// MB — over 100 ms per pass for a large texture). The redundant opens always
// target the same container consecutively on one thread, so a single slot per
// thread captures them with no lock and no policy (no LRU, no byte cap, no
// invalidation). Slot identity (container md5, granted key) makes a hit
// equivalent to "the exact key that verified these container bytes was
// granted": with no key granted, or a different one, there is no hit, so
// authorization semantics are identical with and without the memo, and
// identical content always decrypts to the same plaintext, so returning a
// memoized payload is safe even if packs were replaced underneath.
struct VaultOpenMemo {
	bool valid = false;
	uint8_t md5[16] = {};
	uint8_t key[VaultContainer::KEY_SIZE] = {};
	Vector<uint8_t> plain; // Verified plaintext (kept alive via COW sharing).
};
static thread_local VaultOpenMemo vault_open_memo;

static bool _vault_memo_match(const PackedData::PackedFile &p_pf, const uint8_t p_key[VaultContainer::KEY_SIZE]) {
	VaultOpenMemo &memo = vault_open_memo;
	return memo.valid && memcmp(memo.md5, p_pf.md5, sizeof(memo.md5)) == 0 && memcmp(memo.key, p_key, sizeof(memo.key)) == 0;
}

Error PackedData::add_pack(const String &p_path, bool p_replace_files, uint64_t p_offset, const Vector<uint8_t> &p_decryption_key) {
	for (int i = 0; i < sources.size(); i++) {
		if (sources[i]->try_open_pack(p_path, p_replace_files, p_offset, p_decryption_key)) {
			return OK;
		}
	}

	return ERR_FILE_UNRECOGNIZED;
}

void PackedData::add_path(const String &p_pkg_path, const String &p_path, uint64_t p_ofs, uint64_t p_size, const uint8_t *p_md5, PackSource *p_src, bool p_replace_files, bool p_encrypted, bool p_bundle, bool p_delta, const String &p_salt, bool p_vault) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());

	bool exists = files.has(pmd5);

	PackedFile pf;
	pf.encrypted = p_encrypted;
	pf.bundle = p_bundle;
	pf.delta = p_delta;
	pf.vault = p_vault;
	pf.pack = p_pkg_path;
	pf.salt = p_salt;
	pf.offset = p_ofs;
	pf.size = p_size;
	for (int i = 0; i < 16; i++) {
		pf.md5[i] = p_md5[i];
	}
	pf.src = p_src;

	if (p_delta) {
		delta_patches[pmd5].push_back(pf);
	} else if (!exists || p_replace_files) {
		files[pmd5] = pf;
		delta_patches[pmd5].clear();
	}

	if (!exists) {
		// Search for directory.
		PackedDir *cd = root;

		if (simplified_path.contains_char('/')) { // In a subdirectory.
			Vector<String> ds = simplified_path.get_base_dir().split("/");

			for (int j = 0; j < ds.size(); j++) {
				if (!cd->subdirs.has(ds[j])) {
					PackedDir *pd = memnew(PackedDir);
					pd->name = ds[j];
					pd->parent = cd;
					cd->subdirs[pd->name] = pd;
					cd = pd;
				} else {
					cd = cd->subdirs[ds[j]];
				}
			}
		}
		String filename = simplified_path.get_file();
		// Don't add as a file if the path points to a directory.
		if (!filename.is_empty()) {
			cd->files.insert(filename);
		}
	}
}

void PackedData::remove_path(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	if (!files.has(pmd5)) {
		return;
	}

	// Search for directory.
	PackedDir *cd = root;

	if (simplified_path.contains_char('/')) { // In a subdirectory.
		Vector<String> ds = simplified_path.get_base_dir().split("/");

		for (int j = 0; j < ds.size(); j++) {
			if (!cd->subdirs.has(ds[j])) {
				return; // Subdirectory does not exist, do not bother creating.
			} else {
				cd = cd->subdirs[ds[j]];
			}
		}
	}

	cd->files.erase(simplified_path.get_file());

	files.erase(pmd5);
}

void PackedData::add_pack_source(PackSource *p_source) {
	if (p_source != nullptr) {
		sources.push_back(p_source);
	}
}

uint8_t *PackedData::get_file_hash(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return nullptr;
	}

	return E->value.md5;
}

Vector<PackedData::PackedFile> PackedData::get_delta_patches(const String &p_path) const {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, Vector<PackedFile>, PathMD5>::ConstIterator E = delta_patches.find(pmd5);
	if (!E) {
		return Vector<PackedFile>();
	}

	return E->value;
}

bool PackedData::has_delta_patches(const String &p_path) const {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, Vector<PackedFile>, PathMD5>::ConstIterator E = delta_patches.find(pmd5);
	if (!E) {
		return false;
	}

	return !E->value.is_empty();
}

HashSet<String> PackedData::get_file_paths() const {
	HashSet<String> file_paths;
	_get_file_paths(root, root->name, file_paths);
	return file_paths;
}

void PackedData::_get_file_paths(PackedDir *p_dir, const String &p_parent_dir, HashSet<String> &r_paths) const {
	for (const String &E : p_dir->files) {
		r_paths.insert(p_parent_dir.path_join(E));
	}

	for (const KeyValue<String, PackedDir *> &E : p_dir->subdirs) {
		_get_file_paths(E.value, p_parent_dir.path_join(E.key), r_paths);
	}
}

void PackedData::clear() {
	files.clear();
	delta_patches.clear();
	_free_packed_dirs(root);
	root = memnew(PackedDir);
}

PackedData::PackedData() {
	singleton = this;
	root = memnew(PackedDir);

	add_pack_source(memnew(PackedSourcePCK));
}

void PackedData::_free_packed_dirs(PackedDir *p_dir) {
	for (const KeyValue<String, PackedDir *> &E : p_dir->subdirs) {
		_free_packed_dirs(E.value);
	}
	memdelete(p_dir);
}

PackedData::~PackedData() {
	if (singleton == this) {
		singleton = nullptr;
	}

	for (int i = 0; i < sources.size(); i++) {
		memdelete(sources[i]);
	}
	_free_packed_dirs(root);
}

//////////////////////////////////////////////////////////////////

bool PackedSourcePCK::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset, const Vector<uint8_t> &p_decryption_key) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}

	bool pck_header_found = false;

	// Search for the header at the start offset - standalone PCK file.
	f->seek(p_offset);
	uint32_t magic = f->get_32();
	if (magic == PACK_HEADER_MAGIC) {
		pck_header_found = true;
	}

	// Search for the header in the executable "pck" section - self contained executable.
	if (!pck_header_found) {
		// Loading with offset feature not supported for self contained exe files.
		if (p_offset != 0) {
			ERR_FAIL_V_MSG(false, "Loading self-contained executable with offset not supported.");
		}

		int64_t pck_off = OS::get_singleton()->get_embedded_pck_offset();
		if (pck_off != 0) {
			// Search for the header, in case PCK start and section have different alignment.
			for (int i = 0; i < 8; i++) {
				f->seek(pck_off);
				magic = f->get_32();
				if (magic == PACK_HEADER_MAGIC) {
#ifdef DEBUG_ENABLED
					print_verbose("PCK header found in executable pck section, loading from offset 0x" + String::num_int64(pck_off - 4, 16));
#endif
					pck_header_found = true;
					break;
				}
				pck_off++;
			}
		}
	}

	// Search for the header at the end of file - self contained executable.
	if (!pck_header_found) {
		// Loading with offset feature not supported for self contained exe files.
		if (p_offset != 0) {
			ERR_FAIL_V_MSG(false, "Loading self-contained executable with offset not supported.");
		}

		f->seek_end();
		f->seek(f->get_position() - 4);
		magic = f->get_32();

		if (magic == PACK_HEADER_MAGIC) {
			f->seek(f->get_position() - 12);
			uint64_t ds = f->get_64();
			f->seek(f->get_position() - ds - 8);
			magic = f->get_32();
			if (magic == PACK_HEADER_MAGIC) {
#ifdef DEBUG_ENABLED
				print_verbose("PCK header found at the end of executable, loading from offset 0x" + String::num_int64(f->get_position() - 4, 16));
#endif
				pck_header_found = true;
			}
		}
	}

	if (!pck_header_found) {
		return false;
	}

	int64_t pck_start_pos = f->get_position() - 4;

	// Read header.
	uint32_t version = f->get_32();
	uint32_t ver_major = f->get_32();
	uint32_t ver_minor = f->get_32();
	uint32_t ver_patch = f->get_32(); // Not used for validation.

	ERR_FAIL_COND_V_MSG(version != PACK_FORMAT_VERSION_V4 && version != PACK_FORMAT_VERSION_V3 && version != PACK_FORMAT_VERSION_V2, false, vformat("Pack version unsupported: %d.", version));
	ERR_FAIL_COND_V_MSG(ver_major > GODOT_VERSION_MAJOR || (ver_major == GODOT_VERSION_MAJOR && ver_minor > GODOT_VERSION_MINOR), false, vformat("Pack created with a newer version of the engine: %d.%d.%d.", ver_major, ver_minor, ver_patch));

	uint32_t pack_flags = f->get_32();
	bool enc_directory = (pack_flags & PACK_DIR_ENCRYPTED);
	bool rel_filebase = (pack_flags & PACK_REL_FILEBASE); // Note: Always enabled for V3.
	bool sparse_bundle = (pack_flags & PACK_SPARSE_BUNDLE);
	String salt;

	uint64_t file_base = f->get_64();
	if ((version == PACK_FORMAT_VERSION_V4) || (version == PACK_FORMAT_VERSION_V3) || (version == PACK_FORMAT_VERSION_V2 && rel_filebase)) {
		file_base += pck_start_pos;
	}

	if (version == PACK_FORMAT_VERSION_V3 || version == PACK_FORMAT_VERSION_V4) {
		// V3/V4: Read directory offset and skip reserved part of the header.
		uint64_t dir_offset = f->get_64() + pck_start_pos;
		if (sparse_bundle && enc_directory && version == PACK_FORMAT_VERSION_V4) {
			// V4: Read encrypted directory salt.
			Vector<uint8_t> salt_data = f->get_buffer(32);
			salt.append_latin1(Span((const char *)salt_data.ptr(), salt_data.size()));
		}
		f->seek(dir_offset);
	} else if (version == PACK_FORMAT_VERSION_V2) {
		// V2: Directory directly after the header.
		for (int i = 0; i < 16; i++) {
			f->get_32(); // Reserved.
		}
	}

	// Read directory.
	int file_count = f->get_32();
	if (enc_directory) {
		Ref<FileAccessEncrypted> fae;
		fae.instantiate();
		ERR_FAIL_COND_V_MSG(fae.is_null(), false, "Can't open encrypted pack directory.");

		Vector<uint8_t> key;
#ifdef TOOLS_ENABLED
		if (!p_decryption_key.is_empty()) {
			ERR_FAIL_COND_V_MSG(p_decryption_key.size() != 32, false, "Decryption key must be 256-bit.");
			constexpr uint8_t empty_key[32] = {};
			if (memcmp(script_encryption_key, empty_key, sizeof(empty_key)) == 0) {
				key = p_decryption_key;
			}
		} else
#endif
		{
			key.resize(32);
			memcpy(key.ptrw(), script_encryption_key, 32);
		}

		Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_READ, false);
		ERR_FAIL_COND_V_MSG(err, false, "Can't open encrypted pack directory.");
		f = fae;
	}

	for (int i = 0; i < file_count; i++) {
		uint32_t sl = f->get_32();
		CharString cs;
		cs.resize_uninitialized(sl + 1);
		f->get_buffer((uint8_t *)cs.ptr(), sl);
		cs[sl] = 0;

		String path = String::utf8(cs.ptr(), sl);
		uint64_t ofs = f->get_64();
		uint64_t size = f->get_64();
		uint8_t md5[16];
		f->get_buffer(md5, 16);
		uint32_t flags = f->get_32();

		if (flags & PACK_FILE_REMOVAL) { // The file was removed.
			PackedData::get_singleton()->remove_path(path);
		} else {
			PackedData::get_singleton()->add_path(p_path, path, file_base + ofs, size, md5, this, p_replace_files, (flags & PACK_FILE_ENCRYPTED), sparse_bundle, (flags & PACK_FILE_DELTA), salt, (flags & PACK_FILE_VAULT));
		}
	}

	return true;
}

Ref<FileAccess> PackedSourcePCK::get_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key) {
	Ref<FileAccess> file(memnew(FileAccessPack(p_path, *p_file, p_decryption_key)));

	if (p_file->vault && !file->is_open()) {
		// Vault open failed (no granted key / bad key); the specific error was
		// already reported by FileAccessPack. Fail the open cleanly.
		return Ref<FileAccess>();
	}

	if (PackedData::get_singleton()->has_delta_patches(p_path)) {
		Ref<FileAccessPatched> file_patched;
		file_patched.instantiate();
		Error err = file_patched->open_custom(file);
		ERR_FAIL_COND_V(err != OK, Ref<FileAccess>());
		file = file_patched;
	}

	return file;
}

//////////////////////////////////////////////////////////////////

bool PackedSourceDirectory::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset, const Vector<uint8_t> &p_decryption_key) {
	// Load with offset feature only supported for PCK files.
	ERR_FAIL_COND_V_MSG(p_offset != 0, false, "Invalid PCK data. Note that loading files with a non-zero offset isn't supported with directories.");

	if (p_path != "res://") {
		return false;
	}
	add_directory(p_path, p_replace_files);
	return true;
}

Ref<FileAccess> PackedSourceDirectory::get_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key) {
	Ref<FileAccess> ret = FileAccess::create_for_path(p_path);
	ret->reopen(p_path, FileAccess::READ);
	return ret;
}

void PackedSourceDirectory::add_directory(const String &p_path, bool p_replace_files) {
	Ref<DirAccess> da = DirAccess::open(p_path);
	if (da.is_null()) {
		return;
	}
	da->set_include_hidden(true);

	for (const String &file_name : da->get_files()) {
		String file_path = p_path.path_join(file_name);
		uint8_t md5[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		PackedData::get_singleton()->add_path(p_path, file_path, 0, 0, md5, this, p_replace_files, false, false, false);
	}

	for (const String &sub_dir_name : da->get_directories()) {
		String sub_dir_path = p_path.path_join(sub_dir_name);
		add_directory(sub_dir_path, p_replace_files);
	}
}

//////////////////////////////////////////////////////////////////

Error FileAccessPack::open_internal(const String &p_path, int p_mode_flags) {
	ERR_PRINT("Can't open pack-referenced file.");
	return ERR_UNAVAILABLE;
}

bool FileAccessPack::is_open() const {
	if (vault_failed) {
		return false;
	}
	if (f.is_valid()) {
		return f->is_open();
	} else {
		return false;
	}
}

void FileAccessPack::seek(uint64_t p_position) {
	ERR_FAIL_COND_MSG(f.is_null(), "File must be opened before use.");

	if (p_position > pf.size) {
		eof = true;
	} else {
		eof = false;
	}

	f->seek(off + p_position);
	pos = p_position;
}

void FileAccessPack::seek_end(int64_t p_position) {
	seek(pf.size + p_position);
}

uint64_t FileAccessPack::get_position() const {
	return pos;
}

uint64_t FileAccessPack::get_length() const {
	return pf.size;
}

bool FileAccessPack::eof_reached() const {
	return eof;
}

uint64_t FileAccessPack::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V_MSG(f.is_null(), -1, "File must be opened before use.");
	ERR_FAIL_COND_V(!p_dst && p_length > 0, -1);

	if (eof) {
		return 0;
	}

	int64_t to_read = p_length;
	if (to_read + pos > pf.size) {
		eof = true;
		to_read = (int64_t)pf.size - (int64_t)pos;
	}

	pos += to_read;

	if (to_read <= 0) {
		return 0;
	}
	f->get_buffer(p_dst, to_read);

	return to_read;
}

void FileAccessPack::set_big_endian(bool p_big_endian) {
	ERR_FAIL_COND_MSG(f.is_null(), "File must be opened before use.");

	FileAccess::set_big_endian(p_big_endian);
	f->set_big_endian(p_big_endian);
}

Error FileAccessPack::get_error() const {
	if (eof) {
		return ERR_FILE_EOF;
	}
	return OK;
}

void FileAccessPack::flush() {
	ERR_FAIL();
}

bool FileAccessPack::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	ERR_FAIL_V(false);
}

bool FileAccessPack::file_exists(const String &p_name) {
	return false;
}

void FileAccessPack::close() {
	f = Ref<FileAccess>();
}

FileAccessPack::FileAccessPack(const String &p_path, const PackedData::PackedFile &p_file, const Vector<uint8_t> &p_decryption_key) {
	path = p_path;
	pf = p_file;
	if (pf.bundle) {
		String simplified_path = p_path.simplify_path();
		String path_to_load = simplified_path;
#ifdef TOOLS_ENABLED
		if (!pf.salt.is_empty()) {
			path_to_load = pf.pack.get_base_dir().path_join((simplified_path + pf.salt).sha256_text());
		} else {
			path_to_load = pf.pack.get_base_dir().path_join(simplified_path.replace("res://", ""));
		}
#else
		if (!pf.salt.is_empty()) {
			path_to_load = "res://" + (simplified_path + pf.salt).sha256_text();
		}
#endif
		Error err = OK;
		f = FileAccess::open(path_to_load, FileAccess::READ | FileAccess::SKIP_PACK, &err);
		ERR_FAIL_COND_MSG(err != OK, vformat(R"(Can't open pack-referenced file "%s" from sparse pack "%s" due to error "%s".)", simplified_path, pf.pack, error_names[err]));
		off = 0; // For the sparse pack offset is always zero.
	} else {
		Error err = OK;
		f = FileAccess::open(pf.pack, FileAccess::READ, &err);
		ERR_FAIL_COND_MSG(err != OK, vformat(R"(Can't open pack-referenced file "%s" from pack "%s" due to error "%s".)", p_path, pf.pack, error_names[err]));
		f->seek(pf.offset);
		off = pf.offset;
	}

	if (pf.encrypted) {
		Ref<FileAccessEncrypted> fae;
		fae.instantiate();
		ERR_FAIL_COND_MSG(fae.is_null(), vformat(R"(Can't open encrypted pack-referenced file "%s" from pack "%s".)", p_path, pf.pack));

		Vector<uint8_t> key;
#ifdef TOOLS_ENABLED
		if (!p_decryption_key.is_empty()) {
			ERR_FAIL_COND_MSG(p_decryption_key.size() != 32, "Decryption key must be 256-bit.");
			constexpr uint8_t empty_key[32] = {};
			if (memcmp(script_encryption_key, empty_key, sizeof(empty_key)) == 0) {
				key = p_decryption_key;
			}
		} else
#endif
		{
			key.resize(32);
			memcpy(key.ptrw(), script_encryption_key, 32);
		}

		Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_READ, false);
		ERR_FAIL_COND_MSG(err != OK, vformat(R"(Can't open encrypted pack-referenced file "%s" from pack "%s" due to error "%s".)", p_path, pf.pack, error_names[err]));
		f = fae;
		off = 0;
	}

	if (pf.vault) {
		if (_open_vault() != OK) {
			// The specific error was already reported by _open_vault(); make the
			// file unusable so any subsequent operation fails cleanly.
			f.unref();
			pf.size = 0;
		}
		pos = 0;
		eof = false;
		return;
	}

	pos = 0;
	eof = false;
}

Error FileAccessPack::_open_vault() {
	// Plaintext header first (cheap: 64 bytes, no key needed): it carries the
	// uid, so the granted key can be looked up before touching the payload.
	// Needed both for the decrypt-cache lookup and the key check, which must
	// happen no matter what (cached or not) so authorization semantics stay
	// identical with or without the cache.
	const uint64_t container_start = f->get_position();
	uint8_t hdr_buf[VaultContainer::HEADER_SIZE] = {};
	{
		uint64_t got = f->get_buffer(hdr_buf, VaultContainer::HEADER_SIZE);
		if (got < VaultContainer::HEADER_SIZE) {
			vault_failed = true;
			ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, vformat(R"(Vault: short read of container "%s" from pack "%s".)", path, pf.pack));
		}
	}
	VaultContainer::Header hdr;
	Error err = VaultContainer::parse_header(hdr_buf, sizeof(hdr_buf), hdr);
	if (err != OK) {
		vault_failed = true;
		ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, vformat(R"(Vault: invalid container for "%s" from pack "%s" — not a GVAU v1 AES-128-GCM file.)", path, pf.pack));
	}

	String uid_text = ResourceUID::get_singleton()->id_to_text(hdr.uid);

	if (AssetVault::get_singleton() == nullptr) {
		// Should not happen (the singleton is registered before any pack is
		// opened); guarded so a broken core-setup order fails cleanly.
		vault_failed = true;
		PackedData::get_singleton()->vault_error = ERR_UNCONFIGURED;
		ERR_FAIL_V_MSG(ERR_UNCONFIGURED, vformat(R"(Vault: the AssetVault singleton is not registered while loading "%s" from pack "%s".)", path, pf.pack));
	}

	uint8_t key[VaultContainer::KEY_SIZE] = {};
	err = AssetVault::get_singleton()->get_key_bytes(hdr.uid, key);
	if (err == ERR_UNAUTHORIZED) {
		vault_failed = true;
		PackedData::get_singleton()->vault_error = ERR_UNAUTHORIZED;
		ERR_FAIL_V_MSG(ERR_UNAUTHORIZED, vformat(R"(Vault: no key granted for %s while loading "%s" from pack "%s".)", uid_text, path, pf.pack));
	}
	if (err != OK) {
		vault_failed = true;
		PackedData::get_singleton()->vault_error = err;
		ERR_FAIL_V_MSG(err, vformat(R"(Vault: key lookup failed for %s while loading "%s" from pack "%s".)", uid_text, path, pf.pack));
	}

	// Decrypt-memo fast path (see vault_open_memo): the redundant opens that
	// loader introspection performs all target this container consecutively
	// with the same key; a hit replaces an identical AES-GCM pass.
	if (_vault_memo_match(pf, key)) {
		Vector<uint8_t> cached = vault_open_memo.plain; // COW: refcount bump.
		Ref<FileAccessMemory> fam;
		fam.instantiate();
		fam->open_custom(cached.ptr(), cached.size());
		f = fam;
		vault_data = cached; // COW: shares the memo buffer; nobody writes it.
		pf.size = vault_data.size();
		off = 0; // The decrypted stream is memory-backed; no pack offset applies.
		return OK;
	}

	// Memo miss: read the whole GVAU container (GCM needs the full stream to
	// verify the tag), starting over from the container's beginning.
	f->seek(container_start);
	Vector<uint8_t> container;
	container.resize(pf.size);
	{
		uint8_t *w = container.ptrw();
		uint64_t read_total = 0;
		while (read_total < pf.size) {
			uint64_t got = f->get_buffer(w + read_total, pf.size - read_total);
			if (got == 0) {
				vault_failed = true;
				ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, vformat(R"(Vault: short read of container "%s" from pack "%s".)", path, pf.pack));
			}
			read_total += got;
		}
	}

	Vector<uint8_t> plain;
	err = VaultContainer::decrypt(plain, container, key);
	if (err == ERR_FILE_CORRUPT) {
		vault_failed = true;
		PackedData::get_singleton()->vault_error = err;
		ERR_FAIL_V_MSG(err, vformat(R"(Vault: decryption failed for %s (key tag %d) while loading "%s" from pack "%s" — invalid key or tampered payload. Check that the granted key was derived for key tag %d.)", uid_text, hdr.key_tag, path, pf.pack, hdr.key_tag));
	}
	if (err != OK) {
		vault_failed = true;
		PackedData::get_singleton()->vault_error = err;
		ERR_FAIL_V_MSG(err, vformat(R"(Vault: decryption failed for %s while loading "%s" from pack "%s".)", uid_text, path, pf.pack));
	}

	// Memoize the verified plaintext (COW, no copy) for the consecutive opens
	// that follow, then hand its buffer to the memory-backed accessor.
	{
		VaultOpenMemo &memo = vault_open_memo;
		memcpy(memo.md5, pf.md5, sizeof(memo.md5));
		memcpy(memo.key, key, sizeof(memo.key));
		memo.plain = plain; // COW: shares p_plain's buffer, no copy.
		memo.valid = true;
	}
	Ref<FileAccessMemory> fam;
	fam.instantiate();
	fam->open_custom(plain.ptr(), plain.size());
	f = fam;
	vault_data = plain; // Must outlive fam (it reads the buffer in place).
	pf.size = plain.size();
	off = 0; // The decrypted stream is memory-backed; no pack offset applies.
	return OK;
}

//////////////////////////////////////////////////////////////////////////////////
// DIR ACCESS
//////////////////////////////////////////////////////////////////////////////////

Error DirAccessPack::list_dir_begin() {
	list_dirs.clear();
	list_files.clear();

	for (const KeyValue<String, PackedData::PackedDir *> &E : current->subdirs) {
		list_dirs.push_back(E.key);
	}

	for (const String &E : current->files) {
		list_files.push_back(E);
	}

	return OK;
}

String DirAccessPack::get_next() {
	if (list_dirs.size()) {
		cdir = true;
		String d = list_dirs.front()->get();
		list_dirs.pop_front();
		return d;
	} else if (list_files.size()) {
		cdir = false;
		String f = list_files.front()->get();
		list_files.pop_front();
		return f;
	} else {
		return String();
	}
}

bool DirAccessPack::current_is_dir() const {
	return cdir;
}

bool DirAccessPack::current_is_hidden() const {
	return false;
}

void DirAccessPack::list_dir_end() {
	list_dirs.clear();
	list_files.clear();
}

int DirAccessPack::get_drive_count() {
	return 0;
}

String DirAccessPack::get_drive(int p_drive) {
	return "";
}

PackedData::PackedDir *DirAccessPack::_find_dir(const String &p_dir) {
	String nd = p_dir.replace_char('\\', '/');

	// Special handling since simplify_path() will forbid it
	if (p_dir == "..") {
		return current->parent;
	}

	bool absolute = false;
	if (nd.begins_with("res://")) {
		nd = nd.replace_first("res://", "");
		absolute = true;
	}

	nd = nd.simplify_path();

	if (nd.is_empty()) {
		nd = ".";
	}

	if (nd.begins_with("/")) {
		nd = nd.replace_first("/", "");
		absolute = true;
	}

	Vector<String> paths = nd.split("/");

	PackedData::PackedDir *pd;

	if (absolute) {
		pd = PackedData::get_singleton()->root;
	} else {
		pd = current;
	}

	for (int i = 0; i < paths.size(); i++) {
		const String &p = paths[i];
		if (p == ".") {
			continue;
		} else if (p == "..") {
			if (pd->parent) {
				pd = pd->parent;
			}
		} else if (pd->subdirs.has(p)) {
			pd = pd->subdirs[p];

		} else {
			return nullptr;
		}
	}

	return pd;
}

Error DirAccessPack::change_dir(String p_dir) {
	PackedData::PackedDir *pd = _find_dir(p_dir);
	if (pd) {
		current = pd;
		return OK;
	} else {
		return ERR_INVALID_PARAMETER;
	}
}

String DirAccessPack::get_current_dir(bool p_include_drive) const {
	PackedData::PackedDir *pd = current;
	String p = current->name;

	while (pd->parent) {
		pd = pd->parent;
		p = pd->name.path_join(p);
	}

	return "res://" + p;
}

bool DirAccessPack::file_exists(String p_file) {
	PackedData::PackedDir *pd = _find_dir(p_file.get_base_dir());
	if (!pd) {
		return false;
	}
	return pd->files.has(p_file.get_file());
}

bool DirAccessPack::dir_exists(String p_dir) {
	return _find_dir(p_dir) != nullptr;
}

Error DirAccessPack::make_dir(String p_dir) {
	return ERR_UNAVAILABLE;
}

Error DirAccessPack::rename(String p_from, String p_to) {
	return ERR_UNAVAILABLE;
}

Error DirAccessPack::remove(String p_name) {
	return ERR_UNAVAILABLE;
}

uint64_t DirAccessPack::get_space_left() {
	return 0;
}

String DirAccessPack::get_filesystem_type() const {
	return "PCK";
}

DirAccessPack::DirAccessPack() {
	current = PackedData::get_singleton()->root;
}
