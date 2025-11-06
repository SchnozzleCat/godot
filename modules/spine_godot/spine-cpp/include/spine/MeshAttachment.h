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

#ifndef Spine_MeshAttachment_h
#define Spine_MeshAttachment_h

#include <spine/VertexAttachment.h>
#include <spine/TextureRegion.h>
#include <spine/Sequence.h>
#include <spine/Array.h>
#include <spine/Color.h>
#include <spine/HasRendererObject.h>

namespace spine {
	/// Attachment that displays a texture region using a mesh.
	class SP_API MeshAttachment : public VertexAttachment {
		friend class SkeletonBinary;

		friend class SkeletonJson;

		friend class AtlasAttachmentLoader;

		RTTI_DECL

	public:
		explicit MeshAttachment(const String &name);

		virtual ~MeshAttachment();

		using VertexAttachment::computeWorldVertices;

		virtual void computeWorldVertices(Skeleton &skeleton, Slot &slot, size_t start, size_t count, float *worldVertices, size_t offset,
										  size_t stride = 2) override;

		void updateRegion();

		int getHullLength();

		void setHullLength(int inValue);

		Array<float> &getRegionUVs();

		void setRegionUVs(Array<float> &inValue);

		/// The UV pair for each vertex, normalized within the entire texture. See also MeshAttachment::updateRegion
		Array<float> &getUVs();

		Array<unsigned short> &getTriangles();

		void setTriangles(Array<unsigned short> &inValue);

		Color &getColor();

		const String &getPath();

		void setPath(const String &inValue);

		TextureRegion *getRegion();

		void setRegion(TextureRegion *region);

		Sequence *getSequence();

		void setSequence(Sequence *sequence);

		MeshAttachment *getParentMesh();

		void setParentMesh(MeshAttachment *inValue);

		// Nonessential.
		Array<unsigned short> &getEdges();

		void setEdges(Array<unsigned short> &inValue);

		float getWidth();

		void setWidth(float inValue);

		float getHeight();

		void setHeight(float inValue);

		virtual Attachment &copy() override;

		MeshAttachment &newLinkedMesh();

	private:
		TextureRegion *_region;
		String _path;
		Array<float> _regionUVs;
		Array<float> _uvs;
		Array<unsigned short> _triangles;
		Color _color;
		int _hullLength;
		MeshAttachment *_parentMesh;
		Sequence *_sequence;

		// Nonessential.
		Array<unsigned short> _edges;
		int _width, _height;
	};
}

#endif /* Spine_MeshAttachment_h */
