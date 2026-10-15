/**************************************************************************/
/*  test_vault.cpp                                                        */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_vault)

#include "core/crypto/crypto_core.h"
#include "core/io/asset_vault.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/vault_container.h"
#include "core/os/os.h"
#include "tests/test_utils.h"

namespace TestVault {

Vector<uint8_t> hex_bytes(const String &p_hex) {
	Vector<uint8_t> out;
	for (int i = 0; i + 1 < p_hex.length(); i += 2) {
		// Manual nibble parse (String::hex_to_int would treat a "0x" chunk as a prefix).
		int hi = -1;
		int lo = -1;
		char32_t c = p_hex[i];
		hi = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
		c = p_hex[i + 1];
		lo = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
		if (hi < 0) {
			CHECK_MESSAGE(false, "Invalid hex digit in test vector.");
			return Vector<uint8_t>();
		}
		if (lo < 0) {
			CHECK_MESSAGE(false, "Invalid hex digit in test vector.");
			return Vector<uint8_t>();
		}
		out.push_back((uint8_t)(hi * 16 + lo));
	}
	return out;
}

// RFC 5869 Appendix A, Test Case 1 (SHA-256).
TEST_CASE("[Crypto] HKDF-SHA256 matches RFC 5869 test case 1") {
	Vector<uint8_t> ikm;
	ikm.resize(22);
	memset(ikm.ptrw(), 0x0b, 22);
	Vector<uint8_t> salt = hex_bytes("000102030405060708090a0b0c");
	Vector<uint8_t> info = hex_bytes("f0f1f2f3f4f5f6f7f8f9");

	uint8_t okm[42];
	Error err = CryptoCore::hkdf_sha256(ikm.ptr(), ikm.size(), salt.ptr(), salt.size(), info.ptr(), info.size(), okm, 42);
	CHECK(err == OK);

	Vector<uint8_t> expected = hex_bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
	CHECK(memcmp(okm, expected.ptr(), 42) == 0);
}

// RFC 5869 Appendix A, Test Case 2 (SHA-256, longer inputs/outputs).
TEST_CASE("[Crypto] HKDF-SHA256 matches RFC 5869 test case 2") {
	Vector<uint8_t> ikm = hex_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f");
	Vector<uint8_t> salt = hex_bytes("606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
	Vector<uint8_t> info = hex_bytes("b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");

	uint8_t okm[82];
	Error err = CryptoCore::hkdf_sha256(ikm.ptr(), ikm.size(), salt.ptr(), salt.size(), info.ptr(), info.size(), okm, 82);
	CHECK(err == OK);

	Vector<uint8_t> expected = hex_bytes("b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87");
	CHECK(memcmp(okm, expected.ptr(), 82) == 0);
}

// RFC 5869 Appendix A, Test Case 3 (SHA-256, zero-length salt/info).
TEST_CASE("[Crypto] HKDF-SHA256 matches RFC 5869 test case 3") {
	Vector<uint8_t> ikm;
	ikm.resize(22);
	memset(ikm.ptrw(), 0x0b, 22);

	uint8_t okm[42];
	Error err = CryptoCore::hkdf_sha256(ikm.ptr(), ikm.size(), nullptr, 0, nullptr, 0, okm, 42);
	CHECK(err == OK);

	Vector<uint8_t> expected = hex_bytes("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
	CHECK(memcmp(okm, expected.ptr(), 42) == 0);
}

TEST_CASE("[VaultContainer] GVAU round-trip with key tag") {
	Vector<uint8_t> plain;
	plain.resize(64);
	for (int i = 0; i < plain.size(); i++) {
		plain.write[i] = (uint8_t)i;
	}

	const ResourceUID::ID uid = 12345;
	const uint32_t key_tag = 7;
	const uint16_t flags = VaultContainer::FLAG_IMPORTED_ARTIFACT;
	const uint8_t key[VaultContainer::KEY_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

	Vector<uint8_t> container;
	Error err = VaultContainer::encrypt(container, plain, uid, key_tag, flags, key);
	CHECK(err == OK);

	// Container is header (64 bytes) + ciphertext (== plaintext size).
	CHECK(container.size() == VaultContainer::HEADER_SIZE + plain.size());

	// Plaintext header is parseable without a key and self-describing.
	VaultContainer::Header hdr;
	err = VaultContainer::parse_header(container.ptr(), container.size(), hdr);
	CHECK(err == OK);
	CHECK(hdr.version == VaultContainer::FORMAT_VERSION);
	CHECK(hdr.uid == uid);
	CHECK(hdr.key_tag == key_tag);
	CHECK(hdr.flags == flags);
	CHECK(hdr.scheme == VaultContainer::SCHEME_AES128_GCM);
	CHECK(hdr.plain_length == (uint64_t)plain.size());

	// Decrypt with the right key.
	Vector<uint8_t> decrypted;
	err = VaultContainer::decrypt(decrypted, container, key);
	CHECK(err == OK);
	CHECK(decrypted == plain);

	// Wrong key -> GCM tag failure.
	const uint8_t wrong_key[VaultContainer::KEY_SIZE] = {};
	err = VaultContainer::decrypt(decrypted, container, wrong_key);
	CHECK(err == ERR_FILE_CORRUPT);

	// A container encrypted with a different key tag but the same key is
	// internally self-consistent (the tag is authenticated metadata; it does
	// not alter the key at the container level — per-tag key derivation is
	// AssetVault's job).
	Vector<uint8_t> container2;
	err = VaultContainer::encrypt(container2, plain, uid, key_tag + 1, flags, key);
	CHECK(err == OK);
	err = VaultContainer::decrypt(decrypted, container2, key);
	CHECK(err == OK);

	// Stale grant semantics: encrypting with a key derived for one tag and
	// decrypting with a key derived for another tag must fail (the derived
	// keys differ).
	const ResourceUID::ID derive_uid = 54321;
	const String uid_text = ResourceUID::get_singleton()->id_to_text(derive_uid);
	Vector<uint8_t> master;
	master.resize(32);
	for (int i = 0; i < master.size(); i++) {
		master.write[i] = (uint8_t)(i * 5 + 1);
	}
	const PackedByteArray key_tag7 = AssetVault::derive_key(master, uid_text, 7);
	const PackedByteArray key_tag8 = AssetVault::derive_key(master, uid_text, 8);
	Vector<uint8_t> tagged_container;
	uint8_t derived7[VaultContainer::KEY_SIZE];
	uint8_t derived8[VaultContainer::KEY_SIZE];
	memcpy(derived7, key_tag7.ptr(), VaultContainer::KEY_SIZE);
	memcpy(derived8, key_tag8.ptr(), VaultContainer::KEY_SIZE);
	err = VaultContainer::encrypt(tagged_container, plain, derive_uid, 7, 0, derived7);
	CHECK(err == OK);
	err = VaultContainer::decrypt(decrypted, tagged_container, derived7);
	CHECK(err == OK);
	err = VaultContainer::decrypt(decrypted, tagged_container, derived8);
	CHECK(err == ERR_FILE_CORRUPT);

	// Tampering with the ciphertext -> ERR_FILE_CORRUPT.
	Vector<uint8_t> tampered = container;
	tampered.write[VaultContainer::HEADER_SIZE + 3] ^= 0xFF;
	err = VaultContainer::decrypt(decrypted, tampered, key);
	CHECK(err == ERR_FILE_CORRUPT);

	// Tampering with the plaintext header (uid) -> ERR_FILE_CORRUPT
	// (the GCM tag authenticates the header).
	Vector<uint8_t> tampered_header = container;
	tampered_header.write[8] ^= 0xFF;
	err = VaultContainer::decrypt(decrypted, tampered_header, key);
	CHECK(err == ERR_FILE_CORRUPT);

	// Empty payload round-trips.
	Vector<uint8_t> empty;
	Vector<uint8_t> empty_container;
	err = VaultContainer::encrypt(empty_container, empty, uid, 1, 0, key);
	CHECK(err == OK);
	err = VaultContainer::decrypt(decrypted, empty_container, key);
	CHECK(err == OK);
	CHECK(decrypted.size() == 0);

	// Random nonces: two encryptions of the same data differ.
	Vector<uint8_t> other;
	err = VaultContainer::encrypt(other, plain, uid, key_tag, flags, key);
	CHECK(err == OK);
	CHECK(other != container);
}

TEST_CASE("[VaultContainer] Non-container data is rejected") {
	Vector<uint8_t> garbage;
	garbage.resize(VaultContainer::HEADER_SIZE + 16);
	for (int i = 0; i < garbage.size(); i++) {
		garbage.write[i] = (uint8_t)(i * 7);
	}

	VaultContainer::Header hdr;
	CHECK(VaultContainer::parse_header(garbage.ptr(), garbage.size(), hdr) == ERR_FILE_UNRECOGNIZED);

	Vector<uint8_t> decrypted;
	const uint8_t key[VaultContainer::KEY_SIZE] = {};
	CHECK(VaultContainer::decrypt(decrypted, garbage, key) == ERR_FILE_UNRECOGNIZED);

	// Too short for even the header.
	CHECK(VaultContainer::parse_header(garbage.ptr(), 32, hdr) == ERR_FILE_UNRECOGNIZED);
}

TEST_CASE("[VaultContainer] Supported payload magic helpers") {
	const uint8_t rsrc[4] = { 'R', 'S', 'R', 'C' }; // Binary resource.
	const uint8_t rscc[4] = { 'R', 'S', 'C', 'C' }; // Compressed binary resource.
	const uint8_t gst2[4] = { 'G', 'S', 'T', '2' }; // CompressedTexture2D.
	const uint8_t gstl[4] = { 'G', 'S', 'T', 'L' }; // Layered/3D compressed texture.
	const uint8_t garbage[4] = { 'R', 'S', 'T', 'C' }; // Unsupported.

	CHECK(VaultContainer::is_binary_resource_payload(rsrc));
	CHECK(VaultContainer::is_binary_resource_payload(rscc));
	CHECK(VaultContainer::is_compressed_texture_payload(gst2));
	CHECK(VaultContainer::is_compressed_texture_payload(gstl));
	CHECK_FALSE(VaultContainer::is_binary_resource_payload(gst2));
	CHECK_FALSE(VaultContainer::is_binary_resource_payload(gstl));
	CHECK_FALSE(VaultContainer::is_compressed_texture_payload(rsrc));
	CHECK_FALSE(VaultContainer::is_binary_resource_payload(garbage));
	CHECK_FALSE(VaultContainer::is_compressed_texture_payload(garbage));
}

TEST_CASE("[AssetVault] Key derivation is deterministic and tag/uid-sensitive") {
	Vector<uint8_t> master;
	master.resize(32);
	for (int i = 0; i < 32; i++) {
		master.write[i] = (uint8_t)(i * 3 + 1);
	}

	const String uid_text = ResourceUID::get_singleton()->id_to_text(98765);
	PackedByteArray k1 = AssetVault::derive_key(master, uid_text, 1);
	PackedByteArray k1_again = AssetVault::derive_key(master, uid_text, 1);
	CHECK(k1.size() == 16);
	CHECK(k1 == k1_again); // Deterministic (server derives the same key).

	// Default tag is 1.
	CHECK(AssetVault::derive_key(master, uid_text) == k1);

	// Rotation tag changes the key.
	PackedByteArray k2 = AssetVault::derive_key(master, uid_text, 2);
	CHECK(k2.size() == 16);
	CHECK(k2 != k1);

	// Different uid -> different key.
	String other_uid_text = ResourceUID::get_singleton()->id_to_text(4242);
	PackedByteArray k_other = AssetVault::derive_key(master, other_uid_text, 1);
	CHECK(k_other != k1);

	// Different master -> different key.
	Vector<uint8_t> other_master = master;
	other_master.write[0] ^= 0xFF;
	CHECK(AssetVault::derive_key(other_master, uid_text, 1) != k1);

	// Numeric uid input normalizes to the same key as uid:// text input.
	CHECK(AssetVault::derive_key(master, itos(98765), 1) == k1);
}

TEST_CASE("[AssetVault] grant/revoke/has and 16-byte key validation") {
	AssetVault *vault = AssetVault::get_singleton();
	CHECK(vault != nullptr);

	const String uid_text = ResourceUID::get_singleton()->id_to_text(777);
	const String uid_numeric = itos(777);

	CHECK_FALSE(vault->has_key(uid_text));

	PackedByteArray key;
	key.resize(16);
	for (int i = 0; i < 16; i++) {
		key.write[i] = (uint8_t)(0xA0 + i);
	}

	CHECK(vault->grant_key(uid_text, key) == OK);
	CHECK(vault->has_key(uid_text));

	// Granted via text; readable via numeric form.
	uint8_t out_key[VaultContainer::KEY_SIZE] = {};
	CHECK(vault->get_key_bytes(777, out_key) == OK);
	CHECK(memcmp(out_key, key.ptr(), 16) == 0);

	// Invalid key sizes are rejected.
	PackedByteArray short_key;
	short_key.resize(15);
	CHECK(vault->grant_key(uid_text + "x", short_key) == ERR_INVALID_PARAMETER);
	CHECK(vault->grant_key("not-a-uid", key) == ERR_INVALID_PARAMETER);
	// Revoke -> future lookups are unauthorized.
	CHECK(vault->revoke_key(uid_numeric) == OK);
	CHECK_FALSE(vault->has_key(uid_text));
	CHECK(vault->get_key_bytes(777, out_key) == ERR_UNAUTHORIZED);

	// Clear removes everything.
	CHECK(vault->grant_key(uid_text, key) == OK);
	vault->clear();
	CHECK_FALSE(vault->has_key(uid_text));
	CHECK(vault->get_key_bytes(777, out_key) == ERR_UNAUTHORIZED);
}

TEST_CASE("[AssetVault] hex_to_bytes and vault export path") {
	CHECK(AssetVault::hex_to_bytes("00112233445566778899aabbccddeeff").size() == 16);
	CHECK(AssetVault::hex_to_bytes("0x00ff")[0] == 0x00);
	CHECK(AssetVault::hex_to_bytes("0x00ff")[1] == 0xff);
	CHECK(AssetVault::hex_to_bytes("").size() == 0);
	CHECK(AssetVault::hex_to_bytes("abc").size() == 0); // Odd length.
	CHECK(AssetVault::hex_to_bytes("zz").size() == 0); // Invalid digits.

	const ResourceUID::ID uid = 54321;
	String path = AssetVault::get_vault_path(uid);
	CHECK(path == "res://.vault/" + ResourceUID::get_singleton()->id_to_text(uid).substr(6) + ".enc");
	CHECK(path.ends_with(".enc"));
	CHECK(path.get_file().get_extension() == "enc");
}

	// ---------------------------------------------------------------------
	// Integration tests: FileAccessPack + ResourceFormatVaultLoader +
	// AssetVault, exercised through a real pack registered in PackedData.

	// Serves pack-referenced files like PackedSourcePCK::get_file does, minus
	// PCK parsing: the test registers entries directly via
	// PackedData::add_path (the pack file itself only needs to exist on disk,
	// since FileAccessPack must be able to open it).
	class VaultTestPackSource : public PackSource {
	public:
		virtual bool try_open_pack(const String &, bool, uint64_t, const Vector<uint8_t> &) override { return false; }
		virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> & = Vector<uint8_t>()) override {
			Ref<FileAccess> file(memnew(FileAccessPack(p_path, *p_file)));
			if (p_file->vault && !file->is_open()) {
				// Vault open failed (no granted key / bad key); the specific error
				// is reachable via PackedData::try_open_path's error parameter.
				return Ref<FileAccess>();
			}
			return file;
		}
	};

	// Installs a PackedData singleton for the test's duration (tests run
	// sequentially; none of the other tests rely on PackedData state).
	class VaultTestPackedData {
		PackedData *pd = nullptr;
		VaultTestPackSource *src = nullptr;

	public:
		VaultTestPackedData() {
			pd = memnew(PackedData); // Sets the singleton.
			src = memnew(VaultTestPackSource);
			pd->add_pack_source(src);
		}
		~VaultTestPackedData() {
			memdelete(pd); // Deletes sources and resets the singleton.
		}

		void add_vault_entry(const String &p_pack_path, const String &p_path, const Vector<uint8_t> &p_container) const {
			unsigned char md5[16];
			CHECK_MESSAGE(CryptoCore::md5(p_container.ptr(), p_container.size(), md5) == OK, "Computing the container md5 should succeed.");
			pd->add_path(p_pack_path, p_path, 0, p_container.size(), md5, src, true, false, false, false, String(), true);
		}

		ResourceUID::ID vault_uid() const { return 111222333; }
		String vault_path() const { return AssetVault::get_vault_path(vault_uid()); }
	};

	static Vector<uint8_t> read_whole(const String &p_path, Error *r_err) {
		*r_err = OK;
		Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, r_err);
		if (f.is_null()) {
			return Vector<uint8_t>();
		}
		return f->get_buffer(f->get_length());
	}

	static Vector<uint8_t> make_valid_resource_payload() {
		// A real binary resource ("RSRC" payload, as produced by the exporter
		// for a non-imported vault asset): save an actual Resource.
		const String payload_path = TestUtils::get_temp_path("vault_test_payload.res");
		Ref<Resource> res;
		res.instantiate();
		res->set_meta("vault_test", "payload");
		CHECK_MESSAGE(ResourceSaver::save(res, payload_path) == OK, "Saving the plain .res payload should succeed.");
		Vector<uint8_t> payload = FileAccess::get_file_as_bytes(payload_path);
		DirAccess::remove_absolute(payload_path);
		return payload;
	}

TEST_CASE("[Vault][Pack integration] FileAccessPack: no key, granted key, stale key and revoke") {
	const Vector<uint8_t> payload = make_valid_resource_payload();
	CHECK(payload.size() > VaultContainer::HEADER_SIZE);

	VaultTestPackedData guard;
	AssetVault *vault = AssetVault::get_singleton();
	REQUIRE(vault != nullptr);

	const ResourceUID::ID uid = guard.vault_uid();
	const String uid_text = ResourceUID::get_singleton()->id_to_text(uid);
	const String vault_path = guard.vault_path();

	// Master and derived keys (tag 4 = the container's tag, tag 5 = stale).
	Vector<uint8_t> master;
	master.resize(32);
	for (int i = 0; i < master.size(); i++) {
		master.write[i] = (uint8_t)(i * 11 + 2);
	}
	PackedByteArray right_key = AssetVault::derive_key(master, uid_text, 4);
	PackedByteArray wrong_key = AssetVault::derive_key(master, uid_text, 5); // Stale tag.
	REQUIRE(right_key.size() == VaultContainer::KEY_SIZE);
	REQUIRE(wrong_key.size() == VaultContainer::KEY_SIZE);
	CHECK(right_key != wrong_key);

	Vector<uint8_t> container;
	REQUIRE(VaultContainer::encrypt(container, payload, uid, 4, 0, (const uint8_t *)right_key.ptr()) == OK);

	const String pack_path = TestUtils::get_temp_path("vault_test_pack.bin");
	{
		Ref<FileAccess> f = FileAccess::open(pack_path, FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_buffer(container.ptr(), container.size());
	}
	guard.add_vault_entry(pack_path, vault_path, container);

	// --- Without a granted key: the load fails with ERR_UNAUTHORIZED. ---
	{
		ERR_PRINT_OFF;
		Error err = OK;
		CHECK_MESSAGE(ResourceLoader::load(vault_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err).is_null(),
				"Loading without a granted key must fail.");
		CHECK_MESSAGE(err == ERR_UNAUTHORIZED, vformat("Loading without a key should report ERR_UNAUTHORIZED (got %s).", error_names[err]));

		err = OK;
		CHECK_MESSAGE(read_whole(vault_path, &err).is_empty(), "Direct open without a key should fail.");
		CHECK_MESSAGE(err == ERR_UNAUTHORIZED, vformat("Direct open without a key should report ERR_UNAUTHORIZED (got %s).", error_names[err]));
		ERR_PRINT_ON;
	}

	// --- With the right key: the vault path loads the decrypted resource. ---
	{
		REQUIRE(vault->grant_key(uid_text, right_key) == OK);

		Error err = OK;
		Ref<Resource> loaded = ResourceLoader::load(vault_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		CHECK(err == OK);
		CHECK_MESSAGE(loaded.is_valid(), "Loading with a granted key must succeed.");
		CHECK(loaded->get_meta("vault_test") == Variant("payload"));

		// Two direct opens (2nd one served by the decrypt cache) both give the plaintext.
		Error err1 = OK, err2 = OK;
		Vector<uint8_t> plain1 = read_whole(vault_path, &err1);
		Vector<uint8_t> plain2 = read_whole(vault_path, &err2);
		CHECK(err1 == OK);
		CHECK(err2 == OK);
		CHECK(plain1 == payload);
		CHECK(plain2 == payload); // Cache path (or a fresh decrypt) must still verify.
	}

	// --- With a stale/wrong key: the load fails with ERR_FILE_CORRUPT. ---
	{
		REQUIRE(vault->revoke_key(uid_text) == OK);
		REQUIRE(vault->grant_key(uid_text, wrong_key) == OK);
		ERR_PRINT_OFF;
		Error err = OK;
		CHECK_MESSAGE(ResourceLoader::load(vault_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err).is_null(),
				"Loading with a stale key must fail.");
		CHECK_MESSAGE(err == ERR_FILE_CORRUPT, vformat("Loading with a stale key should report ERR_FILE_CORRUPT (got %s).", error_names[err]));
		ERR_PRINT_ON;

		vault->revoke_key(uid_text);
	}

	// --- Revoked again: unauthorized. ---
	{
		ERR_PRINT_OFF;
		Error err = OK;
		CHECK_MESSAGE(ResourceLoader::load(vault_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err).is_null(),
				"Loading after revoking must fail.");
		CHECK_MESSAGE(err == ERR_UNAUTHORIZED, vformat("Loading after revoking should report ERR_UNAUTHORIZED (got %s).", error_names[err]));
		ERR_PRINT_ON;
	}

	DirAccess::remove_absolute(pack_path);
}

TEST_CASE("[Vault][Pack integration] uid:// loading and decrypt-cache reuse across opens") {
	const Vector<uint8_t> payload = make_valid_resource_payload();
	CHECK(payload.size() > VaultContainer::HEADER_SIZE);

	VaultTestPackedData guard;
	AssetVault *vault = AssetVault::get_singleton();
	REQUIRE(vault != nullptr);

	const ResourceUID::ID uid = guard.vault_uid();
	const String uid_text = ResourceUID::get_singleton()->id_to_text(uid);
	const String vault_path = guard.vault_path();

	Vector<uint8_t> master;
	master.resize(32);
	for (int i = 0; i < master.size(); i++) {
		master.write[i] = (uint8_t)(i * 7 + 3);
	}
	PackedByteArray key = AssetVault::derive_key(master, uid_text, 1);
	REQUIRE(key.size() == VaultContainer::KEY_SIZE);

	Vector<uint8_t> container;
	REQUIRE(VaultContainer::encrypt(container, payload, uid, 1, 0, (const uint8_t *)key.ptr()) == OK);

	const String pack_path = TestUtils::get_temp_path("vault_test_pack_2.bin");
	{
		Ref<FileAccess> f = FileAccess::open(pack_path, FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_buffer(container.ptr(), container.size());
	}
	guard.add_vault_entry(pack_path, vault_path, container);

	// The uid maps to the uniform vault path (as exported in uid_cache.bin).
	if (!ResourceUID::get_singleton()->has_id(uid)) {
		ResourceUID::get_singleton()->add_id(uid, vault_path);
	} else {
		ResourceUID::get_singleton()->set_id(uid, vault_path);
	}

	REQUIRE(vault->grant_key(uid_text, key) == OK);

	// Loading through the uid:// form (the runtime's normal path) works.
	{
		Error err = OK;
		Ref<Resource> loaded = ResourceLoader::load(uid_text, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		CHECK_MESSAGE(loaded.is_valid(), "Loading through uid:// must succeed.");
		CHECK(loaded->get_meta("vault_test") == Variant("payload"));
	}

	// Repeated direct opens of the same container reuse the cached plaintext.
	{
		Error err = OK;
		Vector<uint8_t> first = read_whole(vault_path, &err);
		CHECK(err == OK);
		for (int i = 0; i < 3; i++) {
			Vector<uint8_t> again = read_whole(vault_path, &err);
			CHECK(err == OK);
			CHECK(again == first); // Decrypted stream is stable across opens.
		}
	}

	vault->revoke_key(uid_text);
	ResourceUID::get_singleton()->remove_id(uid);
	DirAccess::remove_absolute(pack_path);
}

} // namespace TestVault
