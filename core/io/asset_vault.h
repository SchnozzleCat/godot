/**************************************************************************/
/*  asset_vault.h                                                         */
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

#ifndef ASSET_VAULT_H
#define ASSET_VAULT_H

#include "core/io/resource_uid.h"
#include "core/io/vault_container.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

class String;

// Runtime key store for vault-encrypted assets.
//
// The client receives per-asset AES-128 keys from the key-granting server
// (which derives them with AssetVault::derive_key from the master key) and
// grants them here. FileAccessPack transparently decrypts vault files using
// the keys granted to this singleton; resources are then loaded through
// their uid:// paths as usual.
//
// The client never receives the master key. Keep derive_key()/masterkey_from_env()
// off client machines — they are intended for export/headless-server tooling.
class AssetVault : public Object {
	GDCLASS(AssetVault, Object);

private:
	HashMap<ResourceUID::ID, Vector<uint8_t>> keys;
	mutable Mutex mutex;

	static AssetVault *singleton;

	static String _normalize_uid_text(const String &p_uid);
	static ResourceUID::ID _resolve_uid(const String &p_uid);

public:
	AssetVault();
protected:
	static void _bind_methods();

public:
	static AssetVault *get_singleton();

	// Grants the per-asset AES-128 key (16 bytes) for the given uid.
	// p_uid accepts "uid://…" text or a numeric uid. Idempotent.
	Error grant_key(const String &p_uid, const PackedByteArray &p_key);
	// Forgets the granted key for the given uid (future loads of the asset
	// fail with ERR_UNAUTHORIZED until a new key is granted).
	Error revoke_key(const String &p_uid);
	bool has_key(const String &p_uid) const;
	// Forgets all granted keys (best-effort zeroization of key buffers).
	void clear();

	// C++ fast path used by FileAccessPack: copies the granted key for p_uid
	// into r_key. ERR_UNAUTHORIZED if no key is granted.
	Error get_key_bytes(ResourceUID::ID p_uid, uint8_t r_key[VaultContainer::KEY_SIZE]) const;

	// Derives the per-asset AES-128 key for p_uid (rotation tag p_tag, 1-based)
	// from the master key:
	//   HKDF-SHA256(IKM = master, salt = "godot-asset-vault-v1",
	//              info = utf8("uid://<uid-text>:<tag>"), L = 16)
	// Intended for the export machine / key-granting server. Shipping a client
	// build with the master key present defeats the vault entirely.
	static PackedByteArray derive_key(const PackedByteArray &p_master, const String &p_uid, uint32_t p_tag = 1);
	// Reads and hex-decodes GODOT_VAULT_MASTERKEY; empty array if unset/invalid.
	static PackedByteArray masterkey_from_env();

	// The in-pack path a vault asset is exported to:
	// "res://.vault/<uid-text>.enc" (uniform extension, no type info).
	static String get_vault_path(ResourceUID::ID p_uid);

	// Hex string -> bytes; returns an empty array on invalid input.
	static PackedByteArray hex_to_bytes(const String &p_hex);

	~AssetVault();
};

#endif // ASSET_VAULT_H
