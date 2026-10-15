/**************************************************************************/
/*  asset_vault.cpp                                                       */
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

#include "asset_vault.h"

#include "core/config/engine.h"
#include "core/crypto/crypto_core.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#include <string.h>

// Fixed, versioned, domain-separated label for the HKDF salt.
// Salt and info are untrusted inputs of HKDF; the fixed salt only pins the
// scheme's version so a future format change cannot reuse old derived keys.
static const char *const VAULT_HKDF_SALT = "godot-asset-vault-v1";

AssetVault *AssetVault::singleton = nullptr;

AssetVault *AssetVault::get_singleton() {
	return singleton;
}

String AssetVault::_normalize_uid_text(const String &p_uid) {
	String uid = p_uid.strip_edges();
	if (uid.begins_with("uid://")) {
		return uid;
	}
	// Numeric uid (int64) -> canonical text form.
	if (!uid.is_valid_int()) {
		return String();
	}
	ResourceUID::ID id = (ResourceUID::ID)uid.to_int();
	if (id < 0) {
		return String();
	}
	String txt = ResourceUID::get_singleton()->id_to_text(id);
	if (txt == "uid://<invalid>") {
		return String();
	}
	return txt;
}

ResourceUID::ID AssetVault::_resolve_uid(const String &p_uid) {
	String txt = _normalize_uid_text(p_uid);
	if (txt.is_empty()) {
		return ResourceUID::INVALID_ID;
	}
	return ResourceUID::get_singleton()->text_to_id(txt);
}

Error AssetVault::grant_key(const String &p_uid, const PackedByteArray &p_key) {
	ResourceUID::ID id = _resolve_uid(p_uid);
	ERR_FAIL_COND_V_MSG(id == ResourceUID::INVALID_ID, ERR_INVALID_PARAMETER,
			"Vault: invalid uid '" + p_uid + "'.");
	ERR_FAIL_COND_V_MSG(p_key.size() != (int)VaultContainer::KEY_SIZE, ERR_INVALID_PARAMETER,
			"Vault: key for " + ResourceUID::get_singleton()->id_to_text(id) +
					" must be exactly " + itos(VaultContainer::KEY_SIZE) + " bytes (AES-128).");

	MutexLock lock(mutex);
	Vector<uint8_t> key;
	key.resize(p_key.size());
	memcpy(key.ptrw(), p_key.ptr(), p_key.size());
	keys.insert(id, key);
	return OK;
}

Error AssetVault::revoke_key(const String &p_uid) {
	ResourceUID::ID id = _resolve_uid(p_uid);
	ERR_FAIL_COND_V_MSG(id == ResourceUID::INVALID_ID, ERR_INVALID_PARAMETER,
			"Vault: invalid uid '" + p_uid + "'.");

	MutexLock lock(mutex);
	if (!keys.has(id)) {
		return ERR_DOES_NOT_EXIST;
	}
	HashMap<ResourceUID::ID, Vector<uint8_t>>::Iterator it = keys.find(id);
	// Best-effort zeroization.
	if (it->value.ptrw() != nullptr) {
		memset(it->value.ptrw(), 0, it->value.size());
	}
	keys.erase(id);
	return OK;
}

bool AssetVault::has_key(const String &p_uid) const {
	ResourceUID::ID id = _resolve_uid(p_uid);
	if (id == ResourceUID::INVALID_ID) {
		return false;
	}
	MutexLock lock(mutex);
	return keys.has(id);
}

void AssetVault::clear() {
	MutexLock lock(mutex);
	// Best-effort zeroization of all key buffers.
	for (HashMap<ResourceUID::ID, Vector<uint8_t>>::Iterator it = keys.begin(); it; ++it) {
		if (it->value.ptrw() != nullptr) {
			memset(it->value.ptrw(), 0, it->value.size());
		}
	}
	keys.clear();
}

Error AssetVault::get_key_bytes(ResourceUID::ID p_uid, uint8_t r_key[VaultContainer::KEY_SIZE]) const {
	ERR_FAIL_COND_V(p_uid < 0, ERR_INVALID_PARAMETER);
	MutexLock lock(mutex);
	HashMap<ResourceUID::ID, Vector<uint8_t>>::ConstIterator it = keys.find(p_uid);
	if (!it) {
		return ERR_UNAUTHORIZED;
	}
	memcpy(r_key, it->value.ptr(), VaultContainer::KEY_SIZE);
	return OK;
}

PackedByteArray AssetVault::derive_key(const PackedByteArray &p_master, const String &p_uid, uint32_t p_tag) {
	PackedByteArray out;
	String uid_text = _normalize_uid_text(p_uid);
	ERR_FAIL_COND_V_MSG(uid_text.is_empty(), PackedByteArray(),
			"Vault: invalid uid '" + p_uid + "'.");
	ERR_FAIL_COND_V_MSG(p_master.size() < 16, PackedByteArray(),
			"Vault: master key too short (16 bytes minimum, 32 recommended).");

	CharString info = (uid_text + ":" + itos(p_tag)).utf8();

	out.resize(VaultContainer::KEY_SIZE);
	Error err = CryptoCore::hkdf_sha256(
			p_master.ptr(), p_master.size(),
			(const uint8_t *)VAULT_HKDF_SALT, strlen(VAULT_HKDF_SALT),
			(const uint8_t *)info.get_data(), info.length(),
			out.ptrw(), out.size());
	ERR_FAIL_COND_V_MSG(err != OK, PackedByteArray(), "Vault: HKDF key derivation failed.");
	return out;
}

PackedByteArray AssetVault::masterkey_from_env() {
	String hex = OS::get_singleton()->get_environment("GODOT_VAULT_MASTERKEY");
	return hex_to_bytes(hex);
}

String AssetVault::get_vault_path(ResourceUID::ID p_uid) {
	ERR_FAIL_COND_V(p_uid < 0, String());
	String txt = ResourceUID::get_singleton()->id_to_text(p_uid);
	ERR_FAIL_COND_V_MSG(txt == "uid://<invalid>", String(), "Vault: invalid uid " + itos(p_uid) + ".");
	return String("res://.vault/") + txt.substr(6) + ".enc";
}

PackedByteArray AssetVault::hex_to_bytes(const String &p_hex) {
	PackedByteArray out;
	String hex = p_hex.strip_edges();
	if (hex.begins_with("0x") || hex.begins_with("0X")) {
		hex = hex.substr(2);
	}
	if (hex.length() == 0 || (hex.length() % 2) != 0) {
		return out;
	}
	if (hex.length() / 2 > 0xFFFF) {
		return out;
	}
	out.resize(hex.length() / 2);
	for (int i = 0; i < hex.length(); i += 2) {
		char32_t h = hex[i];
		char32_t l = hex[i + 1];
		int hb;
		if (h >= '0' && h <= '9') {
			hb = h - '0';
		} else if (h >= 'a' && h <= 'f') {
			hb = h - 'a' + 10;
		} else if (h >= 'A' && h <= 'F') {
			hb = h - 'A' + 10;
		} else {
			out.clear();
			return out;
		}
		int lb;
		if (l >= '0' && l <= '9') {
			lb = l - '0';
		} else if (l >= 'a' && l <= 'f') {
			lb = l - 'a' + 10;
		} else if (l >= 'A' && l <= 'F') {
			lb = l - 'A' + 10;
		} else {
			out.clear();
			return out;
		}
		out.ptrw()[i / 2] = (uint8_t)(hb * 16 + lb);
	}
	return out;
}

AssetVault::AssetVault() {
	singleton = this;
}

AssetVault::~AssetVault() {
	clear();
	if (singleton == this) {
		singleton = nullptr;
	}
}

void AssetVault::_bind_methods() {
	ClassDB::bind_method(D_METHOD("grant_key", "uid", "key"), &AssetVault::grant_key);
	ClassDB::bind_method(D_METHOD("revoke_key", "uid"), &AssetVault::revoke_key);
	ClassDB::bind_method(D_METHOD("has_key", "uid"), &AssetVault::has_key);
	ClassDB::bind_method(D_METHOD("clear"), &AssetVault::clear);

	ClassDB::bind_static_method("AssetVault", D_METHOD("derive_key", "master", "uid", "tag"), &AssetVault::derive_key, DEFVAL(1));
	ClassDB::bind_static_method("AssetVault", D_METHOD("masterkey_from_env"), &AssetVault::masterkey_from_env);
	ClassDB::bind_static_method("AssetVault", D_METHOD("get_vault_path", "uid"), &AssetVault::get_vault_path);
	ClassDB::bind_static_method("AssetVault", D_METHOD("hex_to_bytes", "hex"), &AssetVault::hex_to_bytes);
}
