#ifndef MESH_BUILDER_H
#define MESH_BUILDER_H

#include "Mesh.h"

namespace GameEngine
{
    class MeshBuilder
    {
        struct Vertex
        {
            VectorMath::Vec3 pos;
            VectorMath::Vec2 uv;
            VectorMath::Vec3 normal;
            VectorMath::Vec3 tangent;
        };

    private:
        CoreLib::List<MeshBuilder::Vertex> vertices;
        CoreLib::List<int> indices;
		CoreLib::List<MeshElementRange> elementRanges;
		CoreLib::List<VectorMath::Matrix4> transformStack;
		void UpdateElementRange(bool newElement);
		void AddVertex(MeshBuilder::Vertex vtx);
    public:
		MeshBuilder()
		{
			VectorMath::Matrix4 identity;
			VectorMath::Matrix4::CreateIdentityMatrix(identity);
			transformStack.Add(identity);
		}
		void PushRotation(VectorMath::Vec3 axis, float angle);
		void PushTranslation(VectorMath::Vec3 offset);
		void PushScale(VectorMath::Vec3 val);
		void PopTransform();
		VectorMath::Matrix4 GetCurrentTransform()
		{
			return transformStack.Last();
		}
        void AddTriangle(VectorMath::Vec3 v1, VectorMath::Vec3 v2, VectorMath::Vec3 v3, bool asNewElement = false);
        void AddTriangle(VectorMath::Vec3 v1, VectorMath::Vec2 uv1, VectorMath::Vec3 v2, VectorMath::Vec2 uv2, VectorMath::Vec3 v3, VectorMath::Vec2 uv3, bool asNewElement = false);
        void AddQuad(VectorMath::Vec3 v1, VectorMath::Vec3 v2, VectorMath::Vec3 v3, VectorMath::Vec3 v4, bool asNewElement = false);
        void AddLine(VectorMath::Vec3 v1, VectorMath::Vec3 v2, VectorMath::Vec3 normal, float width, bool asNewElement = false);
        void AddDisk(VectorMath::Vec3 center, VectorMath::Vec3 normal, float radiusOuter, float radiusInner = 0.0f, int edges = 16, bool asNewElement = false);
		void AddCone(float radius, float height, int edges, bool asNewElement = false);
		void AddCylinder(float radius, float height, int edges, bool asNewElement = false);
        void AddBox(VectorMath::Vec3 vmin, VectorMath::Vec3 vmax, bool asNewElement = false);
        void AddFrustum(VectorMath::Vec3 v1, VectorMath::Vec3 v2, float radius1, float radius2, int edges = 16, bool asNewElement = false);
		void AddPyramid(float width, float depth, float height, bool asNewElement = false);
        Mesh ToMesh();
		static Mesh TransformMesh(Mesh & input, VectorMath::Matrix4 & transform);
	};
}

#endif