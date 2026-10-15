/**************************************************************************/
/*  compressed_texture_resource_format.cpp                                */
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

#include "compressed_texture_resource_format.h"

#include "core/io/file_access.h"

#include "scene/resources/compressed_texture.h"

// Asset Vault: vault-encrypted assets are exported at uniform
// "res://.vault/<uid-text>.enc" paths regardless of payload format, so the
// format can't be told from the path's extension. Decryption happens
// transparently at the FileAccess layer (see FileAccessPack), so vault files
// are claimed by decrypted payload content instead: "GST2" is a
// CompressedTexture2D; "GSTL" is a layered/3D compressed texture whose layer
// mode (the third u32 in the GSTL header, matching
// ResourceImporterLayeredTexture::Mode, which writes it) tells the exact class
// apart: 0 = 2D array, 1 = cubemap, 2 = cubemap array, 3 = 3D.

static bool _is_vault_path(const String &p_path) {
	String simplified = p_path.simplify_path();
	if (simplified.begins_with("res://")) {
		simplified = simplified.substr(6);
	}
	return simplified.begins_with(".vault/") && simplified.ends_with(".enc");
}

// Returns true if the vault payload is a "GST2" CompressedTexture2D. Returns
// false if it is not (or can't be decrypted/opened), so the loader declines.
static bool _vault_payload_is_texture2d(const String &p_path) {
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (f.is_null() || err != OK) {
		return false; // Not a vault texture payload (or no key granted); not ours to claim.
	}
	uint8_t magic[4] = {};
	f->get_buffer(magic, 4);
	return magic[0] == 'G' && magic[1] == 'S' && magic[2] == 'T' && magic[3] == '2';
}

// Returns the GSTL layer mode (0-3, see above) of a vault payload, or -1 if it
// is not a GSTL texture (or can't be decrypted/opened).
static int _vault_payload_get_layered_mode(const String &p_path) {
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (f.is_null() || err != OK) {
		return -1; // Not a vault texture payload (or no key granted); not ours to claim.
	}
	uint8_t magic[4] = {};
	f->get_buffer(magic, 4);
	if (magic[0] != 'G' || magic[1] != 'S' || magic[2] != 'T' || magic[3] != 'L') {
		return -1;
	}
	f->get_32(); // Format version.
	f->get_32(); // Layer count / depth.
	return (int)f->get_32(); // Layer mode.
}

Ref<Resource> ResourceFormatLoaderCompressedTexture2D::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Ref<CompressedTexture2D> st;
	st.instantiate();
	Error err = st->load(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}

	return st;
}

void ResourceFormatLoaderCompressedTexture2D::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ctex");
}

bool ResourceFormatLoaderCompressedTexture2D::handles_type(const String &p_type) const {
	return p_type == "CompressedTexture2D";
}

String ResourceFormatLoaderCompressedTexture2D::get_resource_type(const String &p_path) const {
	if (p_path.has_extension("ctex")) {
		return "CompressedTexture2D";
	}
	return "";
}

bool ResourceFormatLoaderCompressedTexture2D::recognize_path(const String &p_path, const String &p_for_type) const {
	if (_is_vault_path(p_path)) {
		// Vault paths carry no usable extension; claim by payload content.
		return _vault_payload_is_texture2d(p_path);
	}
	return ResourceFormatLoader::recognize_path(p_path, p_for_type);
}

Ref<Resource> ResourceFormatLoaderCompressedTextureLayered::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Ref<CompressedTextureLayered> ct;
	if (_is_vault_path(p_path)) {
		// Vault paths carry no usable extension; pick the class by the layer
		// mode recorded in the (decrypted) GSTL payload header.
		switch (_vault_payload_get_layered_mode(p_path)) {
			case 0: {
				Ref<CompressedTexture2DArray> c;
				c.instantiate();
				ct = c;
			} break;
			case 1: {
				Ref<CompressedCubemap> c;
				c.instantiate();
				ct = c;
			} break;
			case 2: {
				Ref<CompressedCubemapArray> c;
				c.instantiate();
				ct = c;
			} break;
			default: {
				if (r_error) {
					*r_error = ERR_FILE_UNRECOGNIZED;
				}
				return Ref<Resource>();
			} break;
		}
	} else if (p_path.has_extension("ctexarray")) {
		Ref<CompressedTexture2DArray> c;
		c.instantiate();
		ct = c;
	} else if (p_path.has_extension("ccube")) {
		Ref<CompressedCubemap> c;
		c.instantiate();
		ct = c;
	} else if (p_path.has_extension("ccubearray")) {
		Ref<CompressedCubemapArray> c;
		c.instantiate();
		ct = c;
	} else {
		if (r_error) {
			*r_error = ERR_FILE_UNRECOGNIZED;
		}
		return Ref<Resource>();
	}
	Error err = ct->load(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}

	return ct;
}

void ResourceFormatLoaderCompressedTextureLayered::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ctexarray");
	p_extensions->push_back("ccube");
	p_extensions->push_back("ccubearray");
}

bool ResourceFormatLoaderCompressedTextureLayered::handles_type(const String &p_type) const {
	return p_type == "CompressedTexture2DArray" || p_type == "CompressedCubemap" || p_type == "CompressedCubemapArray";
}

String ResourceFormatLoaderCompressedTextureLayered::get_resource_type(const String &p_path) const {
	if (p_path.has_extension("ctexarray")) {
		return "CompressedTexture2DArray";
	}
	if (p_path.has_extension("ccube")) {
		return "CompressedCubemap";
	}
	if (p_path.has_extension("ccubearray")) {
		return "CompressedCubemapArray";
	}
	return "";
}

bool ResourceFormatLoaderCompressedTextureLayered::recognize_path(const String &p_path, const String &p_for_type) const {
	if (_is_vault_path(p_path)) {
		// Vault paths carry no usable extension; claim by payload content. The
		// layer mode tells the exact class apart (3 = 3D, claimed by
		// ResourceFormatLoaderCompressedTexture3D instead).
		int mode = _vault_payload_get_layered_mode(p_path);
		return mode >= 0 && mode <= 2;
	}
	return ResourceFormatLoader::recognize_path(p_path, p_for_type);
}

Ref<Resource> ResourceFormatLoaderCompressedTexture3D::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Ref<CompressedTexture3D> st;
	st.instantiate();
	Error err = st->load(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}

	return st;
}

void ResourceFormatLoaderCompressedTexture3D::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ctex3d");
}

bool ResourceFormatLoaderCompressedTexture3D::handles_type(const String &p_type) const {
	return p_type == "CompressedTexture3D";
}

String ResourceFormatLoaderCompressedTexture3D::get_resource_type(const String &p_path) const {
	if (p_path.has_extension("ctex3d")) {
		return "CompressedTexture3D";
	}
	return "";
}

bool ResourceFormatLoaderCompressedTexture3D::recognize_path(const String &p_path, const String &p_for_type) const {
	if (_is_vault_path(p_path)) {
		// Vault paths carry no usable extension; claim by payload content.
		return _vault_payload_get_layered_mode(p_path) == 3; // MODE_3D.
	}
	return ResourceFormatLoader::recognize_path(p_path, p_for_type);
}
