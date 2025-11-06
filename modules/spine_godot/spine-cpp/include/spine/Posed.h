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

#ifndef Spine_Posed_h
#define Spine_Posed_h

#include <spine/SpineObject.h>

namespace spine {
	class SP_API Posed {
	public:
		Posed() {
		}
		virtual ~Posed() {
		}

		virtual void constrained() = 0;

		virtual void resetConstrained() = 0;

		virtual bool isPoseEqualToApplied() = 0;

	protected:
		virtual void setupPose() = 0;

		virtual void pose() = 0;
	};

	template<class D, class P, class A>
	class PosedGeneric : public Posed, public SpineObject {
		friend class AnimationState;
		friend class BoneTimeline1;
		friend class BoneTimeline2;
		friend class RotateTimeline;
		friend class IkConstraint;
		friend class TransformConstraint;
		friend class VertexAttachment;
		friend class PathConstraint;
		friend class PhysicsConstraint;
		friend class Skeleton;
		friend class RegionAttachment;
		friend class PointAttachment;
		friend class AttachmentTimeline;
		friend class RGBATimeline;
		friend class RGBTimeline;
		friend class AlphaTimeline;
		friend class RGBA2Timeline;
		friend class RGB2Timeline;
		friend class ScaleTimeline;
		friend class ScaleXTimeline;
		friend class ScaleYTimeline;
		friend class ShearTimeline;
		friend class ShearXTimeline;
		friend class ShearYTimeline;
		friend class TranslateTimeline;
		friend class TranslateXTimeline;
		friend class TranslateYTimeline;
		friend class InheritTimeline;
		friend class Skeleton;

	public:
		PosedGeneric(D &data) : _data(data), _pose(), _constrained(), _applied(&_pose) {
			setupPose();
		}

		virtual ~PosedGeneric() {
		}

		/// The constraint's setup pose data.
		D &getData() {
			return _data;
		}

		P &getPose() {
			// Upcast A to P (safe due to static_assert that A extends P)
			// For most classes P==A so this is a no-op, but for Bone it casts BonePose->BoneLocal
			return _pose;
		}

		A &getAppliedPose() {
			return *_applied;
		}

		virtual void resetConstrained() override {
			_constrained.set(_pose);
		}

		virtual void constrained() override {
			_applied = &_constrained;
		}

		virtual bool isPoseEqualToApplied() override {
			return _applied == &_pose;
		}

	protected:
		virtual void pose() override {
			_applied = &_pose;
		}
		virtual void setupPose() override {
			_pose.set(_data.getSetupPose());
		}

	protected:
		D &_data;
		A _pose;       ///< Stored as A type (concrete pose type) to match Java behavior
		A _constrained;///< Stored as A type (concrete pose type) to match Java behavior
		A *_applied;   ///< Points to either _pose or _constrained, reassignable like Java
	};
}// namespace spine

#endif /* Spine_Posed_h */