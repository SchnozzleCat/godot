/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated July 28, 2023. Replaces all prior versions.
 *
 * Copyright (c) 2013-2023, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software or
 * otherwise create derivative works of the Spine Runtimes (collectively,
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THE
 * SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/BonePose.h>
#include "spine/RTTI.h"
#include <spine/Bone.h>
#include <spine/BoneData.h>
#include <spine/Skeleton.h>
#include <spine/Physics.h>
#include <spine/MathUtil.h>

using namespace spine;

RTTI_IMPL(BonePose, Update);

BonePose::BonePose() : BoneLocal(), _bone(nullptr), _a(0), _b(0), _worldX(0), _c(0), _d(0), _worldY(0), _world(0), _local(0) {
}

BonePose::~BonePose() {
}

void BonePose::update(Skeleton &skeleton, Physics physics) {
	if (_world != skeleton._update) updateWorldTransform(skeleton);
}

void BonePose::updateWorldTransform(Skeleton &skeleton) {
	if (_local == skeleton._update)
		updateLocalTransform(skeleton);
	else
		_world = skeleton._update;

	if (_bone->getParent() == nullptr) {// Root bone.
		float sx = skeleton.getScaleX(), sy = skeleton.getScaleY();
		float rx = (_rotation + _shearX) * MathUtil::Deg_Rad;
		float ry = (_rotation + 90 + _shearY) * MathUtil::Deg_Rad;
		_a = MathUtil::cos(rx) * _scaleX * sx;
		_b = MathUtil::cos(ry) * _scaleY * sx;
		_c = MathUtil::sin(rx) * _scaleX * sy;
		_d = MathUtil::sin(ry) * _scaleY * sy;
		_worldX = _x * sx + skeleton.getX();
		_worldY = _y * sy + skeleton.getY();
		return;
	}

	BonePose &parent = _bone->getParent()->getAppliedPose();
	float pa = parent._a, pb = parent._b, pc = parent._c, pd = parent._d;
	_worldX = pa * _x + pb * _y + parent._worldX;
	_worldY = pc * _x + pd * _y + parent._worldY;

	switch (_inherit) {
		case Inherit_Normal: {
			float rx = (_rotation + _shearX) * MathUtil::Deg_Rad;
			float ry = (_rotation + 90 + _shearY) * MathUtil::Deg_Rad;
			float la = MathUtil::cos(rx) * _scaleX;
			float lb = MathUtil::cos(ry) * _scaleY;
			float lc = MathUtil::sin(rx) * _scaleX;
			float ld = MathUtil::sin(ry) * _scaleY;
			_a = pa * la + pb * lc;
			_b = pa * lb + pb * ld;
			_c = pc * la + pd * lc;
			_d = pc * lb + pd * ld;
			return;
		}
		case Inherit_OnlyTranslation: {
			float rx = (_rotation + _shearX) * MathUtil::Deg_Rad;
			float ry = (_rotation + 90 + _shearY) * MathUtil::Deg_Rad;
			_a = MathUtil::cos(rx) * _scaleX;
			_b = MathUtil::cos(ry) * _scaleY;
			_c = MathUtil::sin(rx) * _scaleX;
			_d = MathUtil::sin(ry) * _scaleY;
			break;
		}
		case Inherit_NoRotationOrReflection: {
			float sx = 1 / skeleton.getScaleX(), sy = 1 / skeleton.getScaleY();
			pa *= sx;
			pc *= sy;
			float s = pa * pa + pc * pc, prx;
			if (s > 0.0001f) {
				s = MathUtil::abs(pa * pd * sy - pb * sx * pc) / s;
				pb = pc * s;
				pd = pa * s;
				prx = MathUtil::atan2(pc, pa) * MathUtil::Rad_Deg;
			} else {
				pa = 0;
				pc = 0;
				prx = 90 - MathUtil::atan2(pd, pb) * MathUtil::Rad_Deg;
			}
			float rx = (_rotation + _shearX - prx) * MathUtil::Deg_Rad;
			float ry = (_rotation + _shearY - prx + 90) * MathUtil::Deg_Rad;
			float la = MathUtil::cos(rx) * _scaleX;
			float lb = MathUtil::cos(ry) * _scaleY;
			float lc = MathUtil::sin(rx) * _scaleX;
			float ld = MathUtil::sin(ry) * _scaleY;
			_a = pa * la - pb * lc;
			_b = pa * lb - pb * ld;
			_c = pc * la + pd * lc;
			_d = pc * lb + pd * ld;
			break;
		}
		case Inherit_NoScale:
		case Inherit_NoScaleOrReflection: {
			float r = _rotation * MathUtil::Deg_Rad, cosR = MathUtil::cos(r), sinR = MathUtil::sin(r);
			float za = (pa * cosR + pb * sinR) / skeleton.getScaleX();
			float zc = (pc * cosR + pd * sinR) / skeleton.getScaleY();
			float s = MathUtil::sqrt(za * za + zc * zc);
			if (s > 0.00001f) s = 1 / s;
			za *= s;
			zc *= s;
			s = MathUtil::sqrt(za * za + zc * zc);
			if (_inherit == Inherit_NoScale && (pa * pd - pb * pc < 0) != ((skeleton.getScaleX() < 0) != (skeleton.getScaleY() < 0))) s = -s;
			r = MathUtil::Pi / 2 + MathUtil::atan2(zc, za);
			float zb = MathUtil::cos(r) * s;
			float zd = MathUtil::sin(r) * s;
			float rx = _shearX * MathUtil::Deg_Rad;
			float ry = (90 + _shearY) * MathUtil::Deg_Rad;
			float la = MathUtil::cos(rx) * _scaleX;
			float lb = MathUtil::cos(ry) * _scaleY;
			float lc = MathUtil::sin(rx) * _scaleX;
			float ld = MathUtil::sin(ry) * _scaleY;
			_a = za * la + zb * lc;
			_b = za * lb + zb * ld;
			_c = zc * la + zd * lc;
			_d = zc * lb + zd * ld;
			break;
		}
	}
	_a *= skeleton.getScaleX();
	_b *= skeleton.getScaleX();
	_c *= skeleton.getScaleY();
	_d *= skeleton.getScaleY();
}

void BonePose::updateLocalTransform(Skeleton &skeleton) {
	_local = 0;
	_world = skeleton._update;

	if (_bone->getParent() == nullptr) {
		_x = _worldX - skeleton.getX();
		_y = _worldY - skeleton.getY();
		float a = this->_a, b = this->_b, c = this->_c, d = this->_d;
		_rotation = MathUtil::atan2(c, a) * MathUtil::Rad_Deg;
		_scaleX = MathUtil::sqrt(a * a + c * c);
		_scaleY = MathUtil::sqrt(b * b + d * d);
		_shearX = 0;
		_shearY = MathUtil::atan2(a * b + c * d, a * d - b * c) * MathUtil::Rad_Deg;
		return;
	}

	BonePose &parent = _bone->getParent()->getAppliedPose();
	float pa = parent._a, pb = parent._b, pc = parent._c, pd = parent._d;
	float pid = 1 / (pa * pd - pb * pc);
	float ia = pd * pid, ib = pb * pid, ic = pc * pid, id = pa * pid;
	float dx = _worldX - parent._worldX, dy = _worldY - parent._worldY;
	_x = (dx * ia - dy * ib);
	_y = (dy * id - dx * ic);

	float ra, rb, rc, rd;
	if (_inherit == Inherit_OnlyTranslation) {
		ra = _a;
		rb = _b;
		rc = _c;
		rd = _d;
	} else {
		switch (_inherit) {
			case Inherit_NoRotationOrReflection: {
				float s = MathUtil::abs(pa * pd - pb * pc) / (pa * pa + pc * pc);
				pb = -pc * skeleton.getScaleX() * s / skeleton.getScaleY();
				pd = pa * skeleton.getScaleY() * s / skeleton.getScaleX();
				pid = 1 / (pa * pd - pb * pc);
				ia = pd * pid;
				ib = pb * pid;
				break;
			}
			case Inherit_NoScale:
			case Inherit_NoScaleOrReflection: {
				float r = _rotation * MathUtil::Deg_Rad, cosR = MathUtil::cos(r), sinR = MathUtil::sin(r);
				pa = (pa * cosR + pb * sinR) / skeleton.getScaleX();
				pc = (pc * cosR + pd * sinR) / skeleton.getScaleY();
				float s = MathUtil::sqrt(pa * pa + pc * pc);
				if (s > 0.00001f) s = 1 / s;
				pa *= s;
				pc *= s;
				s = MathUtil::sqrt(pa * pa + pc * pc);
				if (_inherit == Inherit_NoScale && (pid < 0) != ((skeleton.getScaleX() < 0) != (skeleton.getScaleY() < 0))) s = -s;
				r = MathUtil::Pi / 2 + MathUtil::atan2(pc, pa);
				pb = MathUtil::cos(r) * s;
				pd = MathUtil::sin(r) * s;
				pid = 1 / (pa * pd - pb * pc);
				ia = pd * pid;
				ib = pb * pid;
				ic = pc * pid;
				id = pa * pid;
				break;
			}
			default:
				break;
		}
		ra = ia * _a - ib * _c;
		rb = ia * _b - ib * _d;
		rc = id * _c - ic * _a;
		rd = id * _d - ic * _b;
	}

	_shearX = 0;
	_scaleX = MathUtil::sqrt(ra * ra + rc * rc);
	if (_scaleX > 0.0001f) {
		float det = ra * rd - rb * rc;
		_scaleY = det / _scaleX;
		_shearY = -MathUtil::atan2(ra * rb + rc * rd, det) * MathUtil::Rad_Deg;
		_rotation = MathUtil::atan2(rc, ra) * MathUtil::Rad_Deg;
	} else {
		_scaleX = 0;
		_scaleY = MathUtil::sqrt(rb * rb + rd * rd);
		_shearY = 0;
		_rotation = 90 - MathUtil::atan2(rd, rb) * MathUtil::Rad_Deg;
	}
}

void BonePose::validateLocalTransform(Skeleton &skeleton) {
	if (_local == skeleton._update) updateLocalTransform(skeleton);
}

void BonePose::modifyLocal(Skeleton &skeleton) {
	if (_local == skeleton._update) updateLocalTransform(skeleton);
	_world = 0;
	resetWorld(skeleton._update);
}

void BonePose::modifyWorld(int update) {
	_local = update;
	_world = update;
	resetWorld(update);
}

void BonePose::resetWorld(int update) {
	Array<Bone *> &children = _bone->getChildren();
	for (size_t i = 0, n = children.size(); i < n; i++) {
		BonePose &child = children[i]->getAppliedPose();
		if (child._world == update) {
			child._world = 0;
			child._local = 0;
			child.resetWorld(update);
		}
	}
}

float BonePose::getA() {
	return _a;
}

void BonePose::setA(float a) {
	this->_a = a;
}

float BonePose::getB() {
	return _b;
}

void BonePose::setB(float b) {
	this->_b = b;
}

float BonePose::getC() {
	return _c;
}

void BonePose::setC(float c) {
	this->_c = c;
}

float BonePose::getD() {
	return _d;
}

void BonePose::setD(float d) {
	this->_d = d;
}

float BonePose::getWorldX() {
	return _worldX;
}

void BonePose::setWorldX(float worldX) {
	this->_worldX = worldX;
}

float BonePose::getWorldY() {
	return _worldY;
}

void BonePose::setWorldY(float worldY) {
	this->_worldY = worldY;
}

float BonePose::getWorldRotationX() {
	return MathUtil::atan2(_c, _a) * MathUtil::Rad_Deg;
}

float BonePose::getWorldRotationY() {
	return MathUtil::atan2(_d, _b) * MathUtil::Rad_Deg;
}

float BonePose::getWorldScaleX() {
	return MathUtil::sqrt(_a * _a + _c * _c);
}

float BonePose::getWorldScaleY() {
	return MathUtil::sqrt(_b * _b + _d * _d);
}


void BonePose::worldToLocal(float worldX, float worldY, float &outLocalX, float &outLocalY) {
	float det = _a * _d - _b * _c;
	float x = worldX - _worldX, y = worldY - _worldY;
	outLocalX = (x * _d - y * _b) / det;
	outLocalY = (y * _a - x * _c) / det;
}

void BonePose::localToWorld(float localX, float localY, float &outWorldX, float &outWorldY) {
	outWorldX = localX * _a + localY * _b + _worldX;
	outWorldY = localX * _c + localY * _d + _worldY;
}

void BonePose::worldToParent(float worldX, float worldY, float &outParentX, float &outParentY) {
	if (_bone->getParent() == nullptr) {
		outParentX = worldX;
		outParentY = worldY;
	} else {
		_bone->getParent()->getAppliedPose().worldToLocal(worldX, worldY, outParentX, outParentY);
	}
}

void BonePose::parentToWorld(float parentX, float parentY, float &outWorldX, float &outWorldY) {
	if (_bone->getParent() == nullptr) {
		outWorldX = parentX;
		outWorldY = parentY;
	} else {
		_bone->getParent()->getAppliedPose().localToWorld(parentX, parentY, outWorldX, outWorldY);
	}
}

float BonePose::worldToLocalRotation(float worldRotation) {
	worldRotation *= MathUtil::Deg_Rad;
	float sinRot = MathUtil::sin(worldRotation), cosRot = MathUtil::cos(worldRotation);
	return MathUtil::atan2(_a * sinRot - _c * cosRot, _d * cosRot - _b * sinRot) * MathUtil::Rad_Deg + _rotation - _shearX;
}

float BonePose::localToWorldRotation(float localRotation) {
	localRotation = (localRotation - _rotation - _shearX) * MathUtil::Deg_Rad;
	float sinRot = MathUtil::sin(localRotation), cosRot = MathUtil::cos(localRotation);
	return MathUtil::atan2(cosRot * _c + sinRot * _d, cosRot * _a + sinRot * _b) * MathUtil::Rad_Deg;
}

void BonePose::rotateWorld(float degrees) {
	degrees *= MathUtil::Deg_Rad;
	float sinRot = MathUtil::sin(degrees), cosRot = MathUtil::cos(degrees);
	float ra = _a, rb = _b;
	_a = cosRot * ra - sinRot * _c;
	_b = cosRot * rb - sinRot * _d;
	_c = sinRot * ra + cosRot * _c;
	_d = sinRot * rb + cosRot * _d;
}