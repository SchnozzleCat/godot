/**************************************************************************/
/*  resource_format_vault.cpp                                             */
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

#include "resource_format_vault.h"

#include "core/io/asset_vault.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/resource_format_binary.h"
#include "core/io/vault_container.h"
#include "core/string/print_string.h"

// Vault assets are exported at uniform "res://.vault/<uid-text>.enc" paths.
// Decryption happens transparently at the FileAccess layer, so any loader can
// read a vault path once its key is granted; the only thing the uniform ".enc"
// path hides is which loader should parse the payload. Binary resource payloads
// ("RSRC"/"RSCC") are delegated to the binary loader here; compressed texture
// payloads ("GST2"/"GSTL", most imported textures) are declined so
// ResourceLoader::_load falls through to the compressed texture loaders, which
// claim vault paths by payload content (see compressed_texture_resource_format.cpp).

static constexpr const char *VAULT_DIR = ".vault";
static constexpr const char *VAULT_EXT = "enc";

ResourceUID::ID ResourceFormatVaultLoader::_get_uid_from_path(const String &p_path) {
	String simplified = p_path.simplify_path();
	if (simplified.begins_with("res://")) {
		simplified = simplified.substr(6);
	}
	if (!simplified.begins_with(String(VAULT_DIR) + "/")) {
		return ResourceUID::INVALID_ID;
	}
	String file = simplified.get_file();
	if (file.get_extension() != VAULT_EXT) {
		return ResourceUID::INVALID_ID;
	}
	String uid_text = file.get_basename();
	ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id("uid://" + uid_text);
	if (uid == ResourceUID::INVALID_ID) {
		return ResourceUID::INVALID_ID;
	}
	return uid;
}

Ref<Resource> ResourceFormatVaultLoader::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	ResourceUID::ID uid = _get_uid_from_path(p_path);
	if (uid != ResourceUID::INVALID_ID && !AssetVault::get_singleton()->has_key(ResourceUID::get_singleton()->id_to_text(uid))) {
		String msg = vformat(R"(Vault: no key granted for %s while loading "%s".)", ResourceUID::get_singleton()->id_to_text(uid), p_path);
		if (r_error) {
			*r_error = ERR_UNAUTHORIZED;
		}
		ERR_FAIL_V_MSG(Ref<Resource>(), msg);
	}

	if (uid != ResourceUID::INVALID_ID) {
		// The uniform ".enc" vault path hides the file type; dispatch on the
		// payload magic (decryption happens transparently at the FileAccess
		// layer, so peeking it is enough).
		Error ferr = OK;
		Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &ferr);
		if (r_error) {
			// Surface the actual cause (ERR_UNAUTHORIZED without a key,
			// ERR_FILE_CORRUPT with a wrong/stale key): ResourceLoader keeps the
			// error last set by a loader if no loader succeeds, so make sure one
			// was set for this path.
			*r_error = ferr;
		}
		ERR_FAIL_COND_V_MSG(f.is_null() || ferr != OK, Ref<Resource>(), vformat(R"(Vault: can't open "%s" (error %s).)", p_path, error_names[ferr]));
		if (r_error) {
			*r_error = OK;
		}
		uint8_t magic[4] = {};
		f->get_buffer(magic, 4);
		if (VaultContainer::is_compressed_texture_payload(magic)) {
			// Compressed texture payload: not a binary resource. Decline it so
			// ResourceLoader::_load falls through to the compressed texture
			// loaders, which claim vault paths by payload content.
			if (r_error) {
				*r_error = ERR_FILE_UNRECOGNIZED;
			}
			print_verbose(vformat(R"(Vault: texture payload for "%s"; deferring to the compressed texture loaders.)", p_path));
			return Ref<Resource>();
		}
		if (!VaultContainer::is_binary_resource_payload(magic)) {
			if (r_error) {
				*r_error = ERR_FILE_UNRECOGNIZED;
			}
			ERR_FAIL_V_MSG(Ref<Resource>(), vformat(R"(Vault: payload of "%s" is not a supported vault format (binary resource (RSRC/RSCC) or compressed texture (GST2/GSTL) expected).)", p_path));
		}
	}

	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	return binary->load(p_path, p_original_path, r_error, p_use_sub_threads, r_progress, p_cache_mode);
}

bool ResourceFormatVaultLoader::recognize_path(const String &p_path, const String &p_for_type) const {
	// Only claim the uniform in-pack vault paths - NOT any ".enc" file a
	// project may contain (those are user data, not vault assets).
	String simplified = p_path.simplify_path();
	if (simplified.begins_with("res://")) {
		simplified = simplified.substr(6);
	}
	return simplified.begins_with(".vault/") && simplified.ends_with(".enc");
}

bool ResourceFormatVaultLoader::exists(const String &p_path) const {
	// The file can exist in the pack without a granted key; check the pack
	// directory directly (no key, no decrypt).
	if (PackedData::get_singleton() && !PackedData::get_singleton()->is_disabled() && PackedData::get_singleton()->has_path(p_path)) {
		return true;
	}
	return FileAccess::exists(p_path);
}

bool ResourceFormatVaultLoader::handles_type(const String &p_type) const {
	// Same as the binary loader: it is the actual parser.
	return true;
}

String ResourceFormatVaultLoader::get_resource_type(const String &p_path) const {
	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	return binary->get_resource_type(p_path);
}

String ResourceFormatVaultLoader::get_resource_script_class(const String &p_path) const {
	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	return binary->get_resource_script_class(p_path);
}

void ResourceFormatVaultLoader::get_classes_used(const String &p_path, HashSet<StringName> *r_classes) {
	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	binary->get_classes_used(p_path, r_classes);
}

ResourceUID::ID ResourceFormatVaultLoader::get_resource_uid(const String &p_path) const {
	// The uid is carried in the vault path itself; readable without a key.
	ResourceUID::ID uid = _get_uid_from_path(p_path);
	if (uid != ResourceUID::INVALID_ID) {
		return uid;
	}
	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	return binary->get_resource_uid(p_path);
}

bool ResourceFormatVaultLoader::has_custom_uid_support() const {
	return true;
}

void ResourceFormatVaultLoader::get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types) {
	Ref<ResourceFormatLoaderBinary> binary;
	binary.instantiate();
	binary->get_dependencies(p_path, p_dependencies, p_add_types);
}
