/**************************************************************************/
/*  resource_format_vault.h                                               */
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

#ifndef RESOURCE_FORMAT_VAULT_H
#define RESOURCE_FORMAT_VAULT_H

#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"

// Loader for vault-encrypted assets exported at uniform "res://.vault/<uid-text>.enc"
// paths (GVAU containers; see vault_container.h). Decryption happens below the
// loader layer in FileAccessPack, so this loader is a thin extension dispatcher
// ("enc") for the uniform vault paths. The payload format is only visible
// after decryption: binary resource payloads ("RSRC"/"RSCC") are delegated to
// the binary resource loader, which parses by payload magic without checking
// extensions; compressed texture payloads ("GST2"/"GSTL") are declined here
// and claimed by the compressed texture loaders instead (which peek the
// decrypted payload, since the ".enc" extension carries no type info). The
// original file type and path are therefore only visible after decryption
// with a granted key (AssetVault).
//
// get_resource_uid()/exists() work WITHOUT a granted key (the uid is carried in
// the vault path); type introspection (get_resource_type(), get_dependencies(),
// …) delegates to the binary loader and requires a granted key.
class ResourceFormatVaultLoader : public ResourceFormatLoader {
	GDSOFTCLASS(ResourceFormatVaultLoader, ResourceFormatLoader);

	// "res://.vault/<uid-text>.enc" -> uid (INVALID_ID if not a vault path).
	static ResourceUID::ID _get_uid_from_path(const String &p_path);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual bool recognize_path(const String &p_path, const String &p_for_type = String()) const override;
	virtual bool exists(const String &p_path) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
	virtual String get_resource_script_class(const String &p_path) const override;
	virtual void get_classes_used(const String &p_path, HashSet<StringName> *r_classes) override;
	virtual ResourceUID::ID get_resource_uid(const String &p_path) const override;
	virtual bool has_custom_uid_support() const override;
	virtual void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false) override;
};

#endif // RESOURCE_FORMAT_VAULT_H
