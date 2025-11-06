/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated April 5, 2025. Replaces all prior versions.
 *
 * Copyright (c) 2013-2025, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/MeshAttachment.h>
#include <spine/Atlas.h>
#include <spine/Slot.h>

using namespace spine;

RTTI_IMPL(MeshAttachment, VertexAttachment)

MeshAttachment::MeshAttachment(const String &name)
	: VertexAttachment(name), _region(NULL), _path(), _color(1, 1, 1, 1), _hullLength(0), _parentMesh(NULL), _sequence(NULL), _width(0), _height(0) {
}

MeshAttachment::~MeshAttachment() {
	if (_sequence) delete _sequence;
}

void MeshAttachment::updateRegion() {
	if (_uvs.size() != _regionUVs.size()) _uvs.setSize(_regionUVs.size(), 0);
	int n = (int) _regionUVs.size();
	float u, v, width, height;
	if (_region != nullptr && _region->getRTTI().instanceOf(AtlasRegion::rtti)) {
		AtlasRegion *atlasRegion = static_cast<AtlasRegion *>(_region);
		u = _region->_u;
		v = _region->_v;

		float textureWidth = atlasRegion->_packedWidth / (_region->_u2 - _region->_u);
		float textureHeight = atlasRegion->_packedHeight / (_region->_v2 - _region->_v);

		switch (atlasRegion->_degrees) {
			case 90: {
				// Note: packed dimensions are swapped in Atlas.cpp for 90-degree regions
				// So we need to un-swap them here to get the original atlas dimensions
				textureWidth = atlasRegion->_packedWidth / (_region->_u2 - _region->_u);
				textureHeight = atlasRegion->_packedHeight / (_region->_v2 - _region->_v);
				u -= (atlasRegion->_originalHeight - atlasRegion->_offsetY - atlasRegion->_packedWidth) / textureWidth;
				v -= (atlasRegion->_originalWidth - atlasRegion->_offsetX - atlasRegion->_packedHeight) / textureHeight;
				width = atlasRegion->_originalHeight / textureWidth;
				height = atlasRegion->_originalWidth / textureHeight;
				for (int i = 0; i < n; i += 2) {
					_uvs[i] = u + _regionUVs[i + 1] * width;
					_uvs[i + 1] = v + (1 - _regionUVs[i]) * height;
				}
				return;
			}
			case 180: {
				u -= (atlasRegion->_originalWidth - atlasRegion->_offsetX - atlasRegion->_packedWidth) / textureWidth;
				v -= atlasRegion->_offsetY / textureHeight;
				width = atlasRegion->_originalWidth / textureWidth;
				height = atlasRegion->_originalHeight / textureHeight;
				for (int i = 0; i < n; i += 2) {
					_uvs[i] = u + (1 - _regionUVs[i]) * width;
					_uvs[i + 1] = v + (1 - _regionUVs[i + 1]) * height;
				}
				return;
			}
			case 270: {
				textureHeight = atlasRegion->_packedHeight / (_region->_v2 - _region->_v);
				textureWidth = atlasRegion->_packedWidth / (_region->_u2 - _region->_u);
				u -= atlasRegion->_offsetY / textureWidth;
				v -= atlasRegion->_offsetX / textureHeight;
				width = atlasRegion->_originalHeight / textureWidth;
				height = atlasRegion->_originalWidth / textureHeight;
				for (int i = 0; i < n; i += 2) {
					_uvs[i] = u + (1 - _regionUVs[i + 1]) * width;
					_uvs[i + 1] = v + _regionUVs[i] * height;
				}
				return;
			}
			default: {
				u -= atlasRegion->_offsetX / textureWidth;
				v -= (atlasRegion->_originalHeight - atlasRegion->_offsetY - atlasRegion->_packedHeight) / textureHeight;
				width = atlasRegion->_originalWidth / textureWidth;
				height = atlasRegion->_originalHeight / textureHeight;
			}
		}
	} else if (_region == nullptr) {
		u = v = 0;
		width = height = 1;
	} else {
		u = _region->_u;
		v = _region->_v;
		width = _region->_u2 - u;
		height = _region->_v2 - v;
	}
	for (int i = 0; i < n; i += 2) {
		_uvs[i] = u + _regionUVs[i] * width;
		_uvs[i + 1] = v + _regionUVs[i + 1] * height;
	}
}

int MeshAttachment::getHullLength() {
	return _hullLength;
}

void MeshAttachment::setHullLength(int inValue) {
	_hullLength = inValue;
}

Array<float> &MeshAttachment::getRegionUVs() {
	return _regionUVs;
}

void MeshAttachment::setRegionUVs(Array<float> &inValue) {
	_regionUVs.clearAndAddAll(inValue);
}

Array<float> &MeshAttachment::getUVs() {
	return _uvs;
}

Array<unsigned short> &MeshAttachment::getTriangles() {
	return _triangles;
}

void MeshAttachment::setTriangles(Array<unsigned short> &inValue) {
	_triangles.clearAndAddAll(inValue);
}

const String &MeshAttachment::getPath() {
	return _path;
}

void MeshAttachment::setPath(const String &inValue) {
	_path = inValue;
}

TextureRegion *MeshAttachment::getRegion() {
	return _region;
}

void MeshAttachment::setRegion(TextureRegion *region) {
	_region = region;
}

Sequence *MeshAttachment::getSequence() {
	return _sequence;
}

void MeshAttachment::setSequence(Sequence *sequence) {
	_sequence = sequence;
}

MeshAttachment *MeshAttachment::getParentMesh() {
	return _parentMesh;
}

void MeshAttachment::setParentMesh(MeshAttachment *inValue) {
	_parentMesh = inValue;
	if (inValue != NULL) {
		_bones.clearAndAddAll(inValue->_bones);
		_vertices.clearAndAddAll(inValue->_vertices);
		_worldVerticesLength = inValue->_worldVerticesLength;
		_regionUVs.clearAndAddAll(inValue->_regionUVs);
		_triangles.clearAndAddAll(inValue->_triangles);
		_hullLength = inValue->_hullLength;
		_edges.clearAndAddAll(inValue->_edges);
		_width = inValue->_width;
		_height = inValue->_height;
	}
}

Array<unsigned short> &MeshAttachment::getEdges() {
	return _edges;
}

void MeshAttachment::setEdges(Array<unsigned short> &inValue) {
	_edges.clearAndAddAll(inValue);
}

float MeshAttachment::getWidth() {
	return _width;
}

void MeshAttachment::setWidth(float inValue) {
	_width = inValue;
}

float MeshAttachment::getHeight() {
	return _height;
}

void MeshAttachment::setHeight(float inValue) {
	_height = inValue;
}

Color &MeshAttachment::getColor() {
	return _color;
}

Attachment &MeshAttachment::copy() {
	if (_parentMesh) return newLinkedMesh();

	MeshAttachment *copy = new (__FILE__, __LINE__) MeshAttachment(getName());
	copy->setRegion(_region);
	copy->setSequence(_sequence != NULL ? &_sequence->copy() : NULL);
	copy->_path = _path;
	copy->_color.set(_color);

	copyTo(*copy);
	copy->_regionUVs.clearAndAddAll(_regionUVs);
	copy->_uvs.clearAndAddAll(_uvs);
	copy->_triangles.clearAndAddAll(_triangles);
	copy->_hullLength = _hullLength;

	// Nonessential.
	copy->_edges.clearAndAddAll(_edges);
	copy->_width = _width;
	copy->_height = _height;
	return *copy;
}

MeshAttachment &MeshAttachment::newLinkedMesh() {
	MeshAttachment *copy = new (__FILE__, __LINE__) MeshAttachment(getName());
	copy->setRegion(_region);
	copy->_path = _path;
	copy->_color.set(_color);
	copy->_timelineAttachment = this->_timelineAttachment;
	copy->setParentMesh(_parentMesh ? _parentMesh : this);
	if (copy->_region) copy->updateRegion();
	return *copy;
}

void MeshAttachment::computeWorldVertices(Skeleton &skeleton, Slot &slot, size_t start, size_t count, float *worldVertices, size_t offset,
										  size_t stride) {
	if (_sequence) _sequence->apply(&slot.getAppliedPose(), this);
	VertexAttachment::computeWorldVertices(skeleton, slot, start, count, worldVertices, offset, stride);
}
