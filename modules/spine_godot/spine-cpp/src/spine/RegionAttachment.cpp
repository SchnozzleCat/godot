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

#include <spine/RegionAttachment.h>

#include <spine/Atlas.h>
#include <spine/Bone.h>
#include <spine/Slot.h>

#include <assert.h>

using namespace spine;

RTTI_IMPL(RegionAttachment, Attachment)

const int RegionAttachment::BLX = 0;
const int RegionAttachment::BLY = 1;
const int RegionAttachment::ULX = 2;
const int RegionAttachment::ULY = 3;
const int RegionAttachment::URX = 4;
const int RegionAttachment::URY = 5;
const int RegionAttachment::BRX = 6;
const int RegionAttachment::BRY = 7;

RegionAttachment::RegionAttachment(const String &name)
	: Attachment(name), _region(NULL), _path(), _x(0), _y(0), _scaleX(1), _scaleY(1), _rotation(0), _width(0), _height(0), _color(1, 1, 1, 1),
	  _sequence(NULL) {
	_offset.setSize(8, 0);
	_uvs.setSize(8, 0);
}

RegionAttachment::~RegionAttachment() {
	if (_sequence) delete _sequence;
}

void RegionAttachment::updateRegion() {
	float width = getWidth(), height = getHeight();
	float localX2 = width / 2;
	float localY2 = height / 2;
	float localX = -localX2;
	float localY = -localY2;
	bool rotated = false;
	AtlasRegion *atlasRegion = NULL;
	if (_region != NULL) {
		atlasRegion = _region->getRTTI().isExactly(AtlasRegion::rtti) ? static_cast<AtlasRegion *>(_region) : NULL;
	}
	if (atlasRegion) {
		localX += atlasRegion->_offsetX / atlasRegion->_originalWidth * width;
		localY += atlasRegion->_offsetY / atlasRegion->_originalHeight * height;
		if (atlasRegion->_degrees == 90) {
			rotated = true;
			localX2 -= (atlasRegion->_originalWidth - atlasRegion->_offsetX - atlasRegion->_packedHeight) / atlasRegion->_originalWidth * width;
			localY2 -= (atlasRegion->_originalHeight - atlasRegion->_offsetY - atlasRegion->_packedWidth) / atlasRegion->_originalHeight * height;
		} else {
			localX2 -= (atlasRegion->_originalWidth - atlasRegion->_offsetX - atlasRegion->_packedWidth) / atlasRegion->_originalWidth * width;
			localY2 -= (atlasRegion->_originalHeight - atlasRegion->_offsetY - atlasRegion->_packedHeight) / atlasRegion->_originalHeight * height;
		}
	}
	float scaleX = getScaleX(), scaleY = getScaleY();
	localX *= scaleX;
	localY *= scaleY;
	localX2 *= scaleX;
	localY2 *= scaleY;
	float cos = MathUtil::cosDeg(_rotation);
	float sin = MathUtil::sinDeg(_rotation);
	float localXCos = localX * cos + _x;
	float localXSin = localX * sin;
	float localYCos = localY * cos + _y;
	float localYSin = localY * sin;
	float localX2Cos = localX2 * cos + _x;
	float localX2Sin = localX2 * sin;
	float localY2Cos = localY2 * cos + _y;
	float localY2Sin = localY2 * sin;

	_offset[BLX] = localXCos - localYSin;
	_offset[BLY] = localYCos + localXSin;
	_offset[ULX] = localXCos - localY2Sin;
	_offset[ULY] = localY2Cos + localXSin;
	_offset[URX] = localX2Cos - localY2Sin;
	_offset[URY] = localY2Cos + localX2Sin;
	_offset[BRX] = localX2Cos - localYSin;
	_offset[BRY] = localYCos + localX2Sin;

	if (_region == NULL) {
		_uvs[BLX] = 0;
		_uvs[BLY] = 0;
		_uvs[ULX] = 0;
		_uvs[ULY] = 1;
		_uvs[URX] = 1;
		_uvs[URY] = 1;
		_uvs[BRX] = 1;
		_uvs[BRY] = 0;
	} else if (rotated) {
		_uvs[BLX] = _region->_u2;
		_uvs[BLY] = _region->_v;
		_uvs[ULX] = _region->_u2;
		_uvs[ULY] = _region->_v2;
		_uvs[URX] = _region->_u;
		_uvs[URY] = _region->_v2;
		_uvs[BRX] = _region->_u;
		_uvs[BRY] = _region->_v;
	} else {
		_uvs[BLX] = _region->_u2;
		_uvs[BLY] = _region->_v2;
		_uvs[ULX] = _region->_u;
		_uvs[ULY] = _region->_v2;
		_uvs[URX] = _region->_u;
		_uvs[URY] = _region->_v;
		_uvs[BRX] = _region->_u2;
		_uvs[BRY] = _region->_v;
	}
}

void RegionAttachment::computeWorldVertices(Slot &slot, Array<float> &worldVertices, size_t offset, size_t stride) {
	assert(worldVertices.size() >= (offset + 8));
	computeWorldVertices(slot, worldVertices.buffer(), offset, stride);
}

void RegionAttachment::computeWorldVertices(Slot &slot, float *worldVertices, size_t offset, size_t stride) {
	if (_sequence) _sequence->apply(&slot.getAppliedPose(), this);

	BonePose &bone = slot.getBone().getAppliedPose();
	float x = bone.getWorldX(), y = bone.getWorldY();
	float a = bone.getA(), b = bone.getB(), c = bone.getC(), d = bone.getD();
	float offsetX, offsetY;

	offsetX = _offset[BRX];
	offsetY = _offset[BRY];
	worldVertices[offset] = offsetX * a + offsetY * b + x;// br
	worldVertices[offset + 1] = offsetX * c + offsetY * d + y;
	offset += stride;

	offsetX = _offset[BLX];
	offsetY = _offset[BLY];
	worldVertices[offset] = offsetX * a + offsetY * b + x;// bl
	worldVertices[offset + 1] = offsetX * c + offsetY * d + y;
	offset += stride;

	offsetX = _offset[ULX];
	offsetY = _offset[ULY];
	worldVertices[offset] = offsetX * a + offsetY * b + x;// ul
	worldVertices[offset + 1] = offsetX * c + offsetY * d + y;
	offset += stride;

	offsetX = _offset[URX];
	offsetY = _offset[URY];
	worldVertices[offset] = offsetX * a + offsetY * b + x;// ur
	worldVertices[offset + 1] = offsetX * c + offsetY * d + y;
}

float RegionAttachment::getX() {
	return _x;
}

void RegionAttachment::setX(float inValue) {
	_x = inValue;
}

float RegionAttachment::getY() {
	return _y;
}

void RegionAttachment::setY(float inValue) {
	_y = inValue;
}

float RegionAttachment::getRotation() {
	return _rotation;
}

void RegionAttachment::setRotation(float inValue) {
	_rotation = inValue;
}

float RegionAttachment::getScaleX() {
	return _scaleX;
}

void RegionAttachment::setScaleX(float inValue) {
	_scaleX = inValue;
}

float RegionAttachment::getScaleY() {
	return _scaleY;
}

void RegionAttachment::setScaleY(float inValue) {
	_scaleY = inValue;
}

float RegionAttachment::getWidth() {
	return _width;
}

void RegionAttachment::setWidth(float inValue) {
	_width = inValue;
}

float RegionAttachment::getHeight() {
	return _height;
}

void RegionAttachment::setHeight(float inValue) {
	_height = inValue;
}

const String &RegionAttachment::getPath() {
	return _path;
}

void RegionAttachment::setPath(const String &inValue) {
	_path = inValue;
}

TextureRegion *RegionAttachment::getRegion() {
	return _region;
}

void RegionAttachment::setRegion(TextureRegion *region) {
	_region = region;
}

Sequence *RegionAttachment::getSequence() {
	return _sequence;
}

void RegionAttachment::setSequence(Sequence *sequence) {
	_sequence = sequence;
}

Array<float> &RegionAttachment::getOffset() {
	return _offset;
}

Array<float> &RegionAttachment::getUVs() {
	return _uvs;
}

Color &RegionAttachment::getColor() {
	return _color;
}

Attachment &RegionAttachment::copy() {
	RegionAttachment *copy = new (__FILE__, __LINE__) RegionAttachment(getName());
	copy->_region = _region;
	copy->_path = _path;
	copy->_x = _x;
	copy->_y = _y;
	copy->_scaleX = _scaleX;
	copy->_scaleY = _scaleY;
	copy->_rotation = _rotation;
	copy->_width = _width;
	copy->_height = _height;
	copy->_uvs.clearAndAddAll(_uvs);
	copy->_offset.clearAndAddAll(_offset);
	copy->_color.set(_color);
	copy->_sequence = _sequence != NULL ? &_sequence->copy() : NULL;
	return *copy;
}
