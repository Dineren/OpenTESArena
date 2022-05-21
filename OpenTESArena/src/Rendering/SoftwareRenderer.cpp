#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

#include "ArenaRenderUtils.h"
#include "LegacyRendererUtils.h"
#include "RenderCamera.h"
#include "RendererUtils.h"
#include "RenderFrameSettings.h"
#include "RenderInitSettings.h"
#include "SoftwareRenderer.h"
#include "../Math/Constants.h"
#include "../Math/MathUtils.h"
#include "../Math/Random.h"
#include "../Media/Color.h"
#include "../Media/Palette.h"
#include "../Media/TextureBuilder.h"
#include "../World/ChunkUtils.h"

#include "components/debug/Debug.h"

namespace swConstants
{
	constexpr double NEAR_PLANE = 0.001;
	constexpr double FAR_PLANE = 1000.0;
	constexpr double PLAYER_LIGHT_DISTANCE = 3.0;
}

namespace swCamera
{
	Double3 GetCameraEye(const RenderCamera &camera)
	{
		// @todo: eventually I think the chunk should be zeroed out and everything should always treat
		// the player's chunk as the origin chunk.
		return VoxelUtils::chunkPointToNewPoint(camera.chunk, camera.point);
	}
}

// Internal geometry types/functions.
namespace swGeometry
{
	struct TriangleClipResult
	{
		int triangleCount = 0;
		RenderTriangle triangles[2];

		static TriangleClipResult zero()
		{
			TriangleClipResult result;
			result.triangleCount = 0;
			return result;
		}

		static TriangleClipResult one(const RenderTriangle &triangle)
		{
			TriangleClipResult result;
			result.triangleCount = 1;
			result.triangles[0] = triangle;
			return result;
		}

		static TriangleClipResult two(const RenderTriangle &triangleA, const RenderTriangle &triangleB)
		{
			TriangleClipResult result;
			result.triangleCount = 2;
			result.triangles[0] = triangleA;
			result.triangles[1] = triangleB;
			return result;
		}
	};

	TriangleClipResult ClipTriangle(const RenderTriangle &triangle, const Double3 &eye, const Double3 &planePoint, const Double3 &planeNormal)
	{
		std::array<const Double3*, 3> insidePoints, outsidePoints;
		std::array<const Double2*, 3> insideUVs, outsideUVs;
		int insidePointCount = 0;
		int outsidePointCount = 0;

		const std::array<const Double3*, 3> vertexPtrs = { &triangle.v0, &triangle.v1, &triangle.v2 };
		const std::array<const Double2*, 3> uvPtrs = { &triangle.uv0, &triangle.uv1, &triangle.uv2 };

		// Determine which vertices are in the positive half-space of the clipping plane.
		for (int i = 0; i < static_cast<int>(vertexPtrs.size()); i++)
		{
			const Double3 *vertexPtr = vertexPtrs[i];
			const double dist = MathUtils::distanceToPlane(*vertexPtr, planePoint, planeNormal);
			if (dist >= 0.0)
			{
				insidePoints[insidePointCount] = vertexPtr;
				insideUVs[insidePointCount] = uvPtrs[i];
				insidePointCount++;
			}
			else
			{
				outsidePoints[outsidePointCount] = vertexPtr;
				outsideUVs[outsidePointCount] = uvPtrs[i];
				outsidePointCount++;
			}
		}

		// Clip triangle depending on the inside/outside vertex case.
		const bool isCompletelyOutside = insidePointCount == 0;
		const bool isCompletelyInside = insidePointCount == 3;
		const bool becomesSmallerTriangle = insidePointCount == 1;
		const bool becomesQuad = insidePointCount == 2;
		if (isCompletelyOutside)
		{
			return TriangleClipResult::zero();
		}
		else if (isCompletelyInside)
		{
			return TriangleClipResult::one(triangle);
		}
		else if (becomesSmallerTriangle)
		{
			const Double3 &insidePoint = *insidePoints[0];
			const Double2 &insideUV = *insideUVs[0];
			const Double3 &outsidePoint0 = *outsidePoints[0];
			const Double3 &outsidePoint1 = *outsidePoints[1];

			// @todo: replace ray-plane intersection with one that get T value internally
			Double3 newInsidePoint1, newInsidePoint2;
			MathUtils::rayPlaneIntersection(insidePoint, (outsidePoint0 - insidePoint).normalized(),
				planePoint, planeNormal, &newInsidePoint1);
			MathUtils::rayPlaneIntersection(insidePoint, (outsidePoint1 - insidePoint).normalized(),
				planePoint, planeNormal, &newInsidePoint2);

			const double t0 = (outsidePoint0 - insidePoint).length();
			const double t1 = (outsidePoint1 - insidePoint).length();
			const double newT0 = (newInsidePoint1 - insidePoint).length();
			const double newT1 = (newInsidePoint2 - insidePoint).length();

			const Double2 outsideUV0 = *outsideUVs[0];
			const Double2 outsideUV1 = *outsideUVs[1];
			const Double2 newInsideUV0 = insideUV.lerp(outsideUV0, newT0 / t0);
			const Double2 newInsideUV1 = insideUV.lerp(outsideUV1, newT1 / t1);

			// Swap vertex winding if needed so we don't generate a back-facing triangle from a front-facing one.
			const Double3 unormal = (insidePoint - newInsidePoint2).cross(newInsidePoint1 - insidePoint);
			RenderTriangle newTriangle;
			if ((eye - insidePoint).dot(unormal) >= Constants::Epsilon)
			{
				newTriangle.init(insidePoint, newInsidePoint1, newInsidePoint2, insideUV, newInsideUV0,
					newInsideUV1, triangle.materialID, triangle.param0);
			}
			else
			{
				newTriangle.init(newInsidePoint2, newInsidePoint1, insidePoint, newInsideUV1, newInsideUV0,
					insideUV, triangle.materialID, triangle.param0);
			}

			return TriangleClipResult::one(newTriangle);
		}
		else if (becomesQuad)
		{
			const Double3 &insidePoint0 = *insidePoints[0];
			const Double3 &insidePoint1 = *insidePoints[1];
			const Double3 &outsidePoint0 = *outsidePoints[0];
			const Double2 &insideUV0 = *insideUVs[0];
			const Double2 &insideUV1 = *insideUVs[1];
			const Double2 &outsideUV0 = *outsideUVs[0];

			const Double3 &newTriangle0V0 = insidePoint0;
			const Double3 &newTriangle0V1 = insidePoint1;
			const Double2 &newTriangle0UV0 = insideUV0;
			const Double2 &newTriangle0UV1 = insideUV1;

			const double t0 = (outsidePoint0 - newTriangle0V0).length();

			// @todo: replace ray-plane intersection with one that get T value internally
			Double3 newTriangle0V2;
			MathUtils::rayPlaneIntersection(newTriangle0V0, (outsidePoint0 - newTriangle0V0).normalized(),
				planePoint, planeNormal, &newTriangle0V2);
			const double newTriangle0T = (newTriangle0V2 - newTriangle0V0).length();
			const Double2 newTriangle0UV2 = newTriangle0UV0.lerp(outsideUV0, newTriangle0T / t0);

			const Double3 &newTriangle1V0 = insidePoint1;
			const Double3 &newTriangle1V1 = newTriangle0V2;
			const Double2 &newTriangle1UV0 = insideUV1;
			const Double2 &newTriangle1UV1 = newTriangle0UV2;

			const double t1 = (outsidePoint0 - newTriangle1V0).length();

			// @todo: replace ray-plane intersection with one that get T value internally
			Double3 newTriangle1V2;
			MathUtils::rayPlaneIntersection(newTriangle1V0, (outsidePoint0 - newTriangle1V0).normalized(),
				planePoint, planeNormal, &newTriangle1V2);
			const double newTriangle1T = (newTriangle1V2 - newTriangle1V0).length();
			const Double2 newTriangle1UV2 = newTriangle1UV0.lerp(outsideUV0, newTriangle1T / t1);

			// Swap vertex winding if needed so we don't generate a back-facing triangle from a front-facing one.
			const Double3 unormal0 = (newTriangle0V0 - newTriangle0V2).cross(newTriangle0V1 - newTriangle0V0);
			RenderTriangle newTriangle0;
			if ((eye - newTriangle0V0).dot(unormal0) >= Constants::Epsilon)
			{
				newTriangle0.init(newTriangle0V0, newTriangle0V1, newTriangle0V2, newTriangle0UV0, newTriangle0UV1,
					newTriangle0UV2, triangle.materialID, triangle.param0);
			}
			else
			{
				newTriangle0.init(newTriangle0V2, newTriangle0V1, newTriangle0V0, newTriangle0UV2, newTriangle0UV1,
					newTriangle0UV0, triangle.materialID, triangle.param0);
			}

			const Double3 unormal1 = (newTriangle1V0 - newTriangle1V2).cross(newTriangle1V1 - newTriangle1V0);
			RenderTriangle newTriangle1;
			if ((eye - newTriangle1V0).dot(unormal1) >= Constants::Epsilon)
			{
				newTriangle1.init(newTriangle1V0, newTriangle1V1, newTriangle1V2, newTriangle1UV0, newTriangle1UV1,
					newTriangle1UV2, triangle.materialID, triangle.param0);
			}
			else
			{
				newTriangle1.init(newTriangle1V2, newTriangle1V1, newTriangle1V0, newTriangle1UV2, newTriangle1UV1,
					newTriangle1UV0, triangle.materialID, triangle.param0);
			}

			return TriangleClipResult::two(newTriangle0, newTriangle1);
		}
		else
		{
			DebugUnhandledReturnMsg(TriangleClipResult, "Unhandled triangle clip case (inside: " +
				std::to_string(insidePointCount) + ", outside: " + std::to_string(outsidePointCount) + ").");
		}
	}

	// Caches for visible triangle processing/clipping.
	std::vector<RenderTriangle> g_visibleOpaqueVoxelTriangles, g_visibleOpaqueVoxelClipList;
	std::vector<RenderTriangle> g_visibleAlphaTestedVoxelTriangles, g_visibleAlphaTestedVoxelClipList;
	std::vector<RenderTriangle> g_visibleEntityTriangles, g_visibleEntityClipList;
	int g_totalOpaqueVoxelTriangleCount = 0;
	int g_totalAlphaTestedVoxelTriangleCount = 0;
	int g_totalEntityTriangleCount = 0;

	// Processes the given world space triangles in the following ways, and returns a view to a geometry cache
	// that is invalidated the next time this function is called.
	// 1) Back-face culling
	// 2) Frustum culling
	// 3) Clipping
	BufferView<const RenderTriangle> ProcessTrianglesForRasterization(const BufferView<const RenderTriangle> &triangles,
		const RenderCamera &camera, std::vector<RenderTriangle> &outVisibleTriangles, std::vector<RenderTriangle> &outClipList,
		int *outTotalTriangleCount)
	{
		const Double3 eye = swCamera::GetCameraEye(camera);

		// Frustum directions pointing away from the camera eye.
		const Double3 leftFrustumDir = (camera.forwardScaled - camera.rightScaled).normalized();
		const Double3 rightFrustumDir = (camera.forwardScaled + camera.rightScaled).normalized();
		const Double3 bottomFrustumDir = (camera.forwardScaled - camera.up).normalized();
		const Double3 topFrustumDir = (camera.forwardScaled + camera.up).normalized();

		// Frustum plane normals pointing towards the inside of the frustum volume.
		const Double3 leftFrustumNormal = leftFrustumDir.cross(camera.up).normalized();
		const Double3 rightFrustumNormal = camera.up.cross(rightFrustumDir).normalized();
		const Double3 bottomFrustumNormal = camera.right.cross(bottomFrustumDir).normalized();
		const Double3 topFrustumNormal = topFrustumDir.cross(camera.right).normalized();

		struct ClippingPlane
		{
			Double3 point;
			Double3 normal;
		};

		// Plane point and normal pairs in world space.
		const std::array<ClippingPlane, 5> clippingPlanes =
		{
			{
				// Near plane (far plane is not necessary due to how chunks are managed - it only matters if a view distance slider exists)
				{ eye + (camera.forward * swConstants::NEAR_PLANE), camera.forward },
				// Left
				{ eye, leftFrustumNormal },
				// Right
				{ eye, rightFrustumNormal },
				// Bottom
				{ eye, bottomFrustumNormal },
				// Top
				{ eye, topFrustumNormal }
			}
		};

		outVisibleTriangles.clear();
		for (int i = 0; i < triangles.getCount(); i++)
		{
			const RenderTriangle &triangle = triangles.get(i);
			const Double3 &v0 = triangle.v0;
			const Double3 &v1 = triangle.v1;
			const Double3 &v2 = triangle.v2;

			// Discard back-facing and almost-back-facing.
			const Double3 v0ToEye = eye - v0;
			if (v0ToEye.dot(triangle.normal) < Constants::Epsilon)
			{
				continue;
			}

			outClipList.clear();
			outClipList.emplace_back(triangle);
			for (const ClippingPlane &plane : clippingPlanes)
			{
				for (int j = static_cast<int>(outClipList.size()); j > 0; j--)
				{
					const RenderTriangle &clipListTriangle = outClipList.front();
					const TriangleClipResult clipResult = ClipTriangle(clipListTriangle, eye, plane.point, plane.normal);
					for (int k = 0; k < clipResult.triangleCount; k++)
					{
						outClipList.emplace_back(clipResult.triangles[k]);
					}

					outClipList.erase(outClipList.begin());
				}
			}

			outVisibleTriangles.insert(outVisibleTriangles.end(), outClipList.begin(), outClipList.end());
		}

		*outTotalTriangleCount = triangles.getCount();
		return BufferView<const RenderTriangle>(outVisibleTriangles.data(), static_cast<int>(outVisibleTriangles.size()));
	}
}

// Rendering functions, per-pixel work.
namespace swRender
{
	void DrawDebugRGB(const RenderCamera &camera, BufferView2D<uint32_t> &colorBuffer)
	{
		const int frameBufferWidth = colorBuffer.getWidth();
		const int frameBufferHeight = colorBuffer.getHeight();
		uint32_t *colorBufferPtr = colorBuffer.get();

		for (int y = 0; y < frameBufferHeight; y++)
		{
			const double yPercent = (static_cast<double>(y) + 0.50) / static_cast<double>(frameBufferHeight);

			for (int x = 0; x < frameBufferWidth; x++)
			{
				const double xPercent = (static_cast<double>(x) + 0.50) / static_cast<double>(frameBufferWidth);

				const Double3 pixelDir = ((camera.forwardScaled - camera.rightScaled + camera.up) +
					(camera.rightScaled * (xPercent * 2.0)) - (camera.up * (yPercent * 2.0))).normalized();

				const Double3 pixelDirClamped(
					std::max(pixelDir.x, 0.0),
					std::max(pixelDir.y, 0.0),
					std::max(pixelDir.z, 0.0));

				const Color color(
					static_cast<uint8_t>(pixelDirClamped.x * 255.0),
					static_cast<uint8_t>(pixelDirClamped.y * 255.0),
					static_cast<uint8_t>(pixelDirClamped.z * 255.0));

				const uint32_t outputColor = color.toARGB();
				const int outputIndex = x + (y * frameBufferWidth);
				colorBufferPtr[outputIndex] = outputColor;
			}
		}
	}

	void ClearFrameBuffers(uint32_t clearColor, BufferView2D<uint32_t> &colorBuffer, BufferView2D<double> &depthBuffer)
	{
		colorBuffer.fill(clearColor);
		depthBuffer.fill(std::numeric_limits<double>::infinity());
	}

	// The provided triangles are assumed to be back-face culled and clipped.
	void RasterizeTriangles(const BufferView<const RenderTriangle> &triangles, bool debug_alphaTest, // @temp
		const SoftwareRenderer::ObjectMaterialPool &materials, const SoftwareRenderer::ObjectTexturePool &textures,
		const SoftwareRenderer::ObjectTexture &paletteTexture, const SoftwareRenderer::ObjectTexture &lightTableTexture,
		const RenderCamera &camera, BufferView2D<uint32_t> &colorBuffer, BufferView2D<double> &depthBuffer)
	{
		const int frameBufferWidth = colorBuffer.getWidth();
		const int frameBufferHeight = colorBuffer.getHeight();
		const double frameBufferWidthReal = static_cast<double>(frameBufferWidth);
		const double frameBufferHeightReal = static_cast<double>(frameBufferHeight);

		const Double3 eye = swCamera::GetCameraEye(camera);
		const Double2 eye2D(eye.x, eye.z); // For 2D lighting.
		const Matrix4d viewMatrix = Matrix4d::view(eye, camera.forward, camera.right, camera.up);
		const Matrix4d perspectiveMatrix = Matrix4d::perspective(camera.fovY, camera.aspectRatio,
			swConstants::NEAR_PLANE, swConstants::FAR_PLANE);

		constexpr double yShear = 0.0;

		const uint32_t *paletteTexels = paletteTexture.paletteTexels.get();

		const int lightLevelTexelCount = lightTableTexture.texels.getWidth(); // Per light level, not the whole table.
		const int lightLevelCount = lightTableTexture.texels.getHeight();
		const double lightLevelCountReal = static_cast<double>(lightLevelCount);
		const uint8_t *lightLevelTexels = lightTableTexture.texels.get();

		uint32_t *colorBufferPtr = colorBuffer.get();
		double *depthBufferPtr = depthBuffer.get();

		const int triangleCount = triangles.getCount();
		for (int i = 0; i < triangleCount; i++)
		{
			const RenderTriangle &triangle = triangles.get(i);
			const Double3 &v0 = triangle.v0;
			const Double3 &v1 = triangle.v1;
			const Double3 &v2 = triangle.v2;
			const Double4 view0 = RendererUtils::worldSpaceToCameraSpace(Double4(v0, 1.0), viewMatrix);
			const Double4 view1 = RendererUtils::worldSpaceToCameraSpace(Double4(v1, 1.0), viewMatrix);
			const Double4 view2 = RendererUtils::worldSpaceToCameraSpace(Double4(v2, 1.0), viewMatrix);
			const Double4 clip0 = RendererUtils::cameraSpaceToClipSpace(view0, perspectiveMatrix);
			const Double4 clip1 = RendererUtils::cameraSpaceToClipSpace(view1, perspectiveMatrix);
			const Double4 clip2 = RendererUtils::cameraSpaceToClipSpace(view2, perspectiveMatrix);
			const Double3 ndc0 = RendererUtils::clipSpaceToNDC(clip0);
			const Double3 ndc1 = RendererUtils::clipSpaceToNDC(clip1);
			const Double3 ndc2 = RendererUtils::clipSpaceToNDC(clip2);
			const Double3 screenSpace0 = RendererUtils::ndcToScreenSpace(ndc0, yShear, frameBufferWidthReal, frameBufferHeightReal);
			const Double3 screenSpace1 = RendererUtils::ndcToScreenSpace(ndc1, yShear, frameBufferWidthReal, frameBufferHeightReal);
			const Double3 screenSpace2 = RendererUtils::ndcToScreenSpace(ndc2, yShear, frameBufferWidthReal, frameBufferHeightReal);
			const Double2 screenSpace0_2D(screenSpace0.x, screenSpace0.y);
			const Double2 screenSpace1_2D(screenSpace1.x, screenSpace1.y);
			const Double2 screenSpace2_2D(screenSpace2.x, screenSpace2.y);
			const Double2 screenSpace01 = screenSpace1_2D - screenSpace0_2D;
			const Double2 screenSpace12 = screenSpace2_2D - screenSpace1_2D;
			const Double2 screenSpace20 = screenSpace0_2D - screenSpace2_2D;
			const Double2 screenSpace01Perp = screenSpace01.rightPerp();
			const Double2 screenSpace12Perp = screenSpace12.rightPerp();
			const Double2 screenSpace20Perp = screenSpace20.rightPerp();

			// Naive screen-space bounding box around triangle.
			const double xMin = std::min(screenSpace0.x, std::min(screenSpace1.x, screenSpace2.x));
			const double xMax = std::max(screenSpace0.x, std::max(screenSpace1.x, screenSpace2.x));
			const double yMin = std::min(screenSpace0.y, std::min(screenSpace1.y, screenSpace2.y));
			const double yMax = std::max(screenSpace0.y, std::max(screenSpace1.y, screenSpace2.y));
			const int xStart = RendererUtils::getLowerBoundedPixel(xMin, frameBufferWidth);
			const int xEnd = RendererUtils::getUpperBoundedPixel(xMax, frameBufferWidth);
			const int yStart = RendererUtils::getLowerBoundedPixel(yMin, frameBufferHeight);
			const int yEnd = RendererUtils::getUpperBoundedPixel(yMax, frameBufferHeight);

			const double z0 = view0.z;
			const double z1 = view1.z;
			const double z2 = view2.z;
			const double z0Recip = 1.0 / z0;
			const double z1Recip = 1.0 / z1;
			const double z2Recip = 1.0 / z2;

			const Double2 &uv0 = triangle.uv0;
			const Double2 &uv1 = triangle.uv1;
			const Double2 &uv2 = triangle.uv2;
			const Double2 uv0Perspective = uv0 * z0Recip;
			const Double2 uv1Perspective = uv1 * z1Recip;
			const Double2 uv2Perspective = uv2 * z2Recip;

			const ObjectMaterial &material = materials.get(triangle.materialID);
			const bool isMultiTextured = material.id1 >= 0;
			const SoftwareRenderer::ObjectTexture &texture0 = textures.get(material.id0);
			const SoftwareRenderer::ObjectTexture &texture1 = isMultiTextured ? textures.get(material.id1) : texture0;
			
			const int texture0Width = texture0.texels.getWidth();
			const int texture0Height = texture0.texels.getHeight();
			const uint8_t *texture0Texels = texture0.texels.get();

			const int texture1Width = texture1.texels.getWidth();
			const int texture1Height = texture1.texels.getHeight();
			const uint8_t *texture1Texels = texture1.texels.get();

			const double fadePercent = triangle.param0;
			const bool isFading = fadePercent > 0.0;

			for (int y = yStart; y < yEnd; y++)
			{
				const double yScreenPercent = (static_cast<double>(y) + 0.50) / frameBufferHeightReal;

				for (int x = xStart; x < xEnd; x++)
				{
					const double xScreenPercent = (static_cast<double>(x) + 0.50) / frameBufferWidthReal;
					const Double2 pixelCenter(
						xScreenPercent * frameBufferWidthReal,
						yScreenPercent * frameBufferHeightReal);

					// See if pixel center is inside triangle.
					const bool inHalfSpace0 = MathUtils::isPointInHalfSpace(pixelCenter, screenSpace0_2D, screenSpace01Perp);
					const bool inHalfSpace1 = MathUtils::isPointInHalfSpace(pixelCenter, screenSpace1_2D, screenSpace12Perp);
					const bool inHalfSpace2 = MathUtils::isPointInHalfSpace(pixelCenter, screenSpace2_2D, screenSpace20Perp);
					if (inHalfSpace0 && inHalfSpace1 && inHalfSpace2)
					{
						const Double2 &ss0 = screenSpace01;
						const Double2 ss1 = screenSpace2_2D - screenSpace0_2D;
						const Double2 ss2 = pixelCenter - screenSpace0_2D;

						const double dot00 = ss0.dot(ss0);
						const double dot01 = ss0.dot(ss1);
						const double dot11 = ss1.dot(ss1);
						const double dot20 = ss2.dot(ss0);
						const double dot21 = ss2.dot(ss1);
						const double denominator = (dot00 * dot11) - (dot01 * dot01);

						const double v = ((dot11 * dot20) - (dot01 * dot21)) / denominator;
						const double w = ((dot00 * dot21) - (dot01 * dot20)) / denominator;
						const double u = 1.0 - v - w;

						const double depth = 1.0 / ((u * z0Recip) + (v * z1Recip) + (w * z2Recip));

						const int outputIndex = x + (y * frameBufferWidth);
						if (depth < depthBufferPtr[outputIndex])
						{
							const double texelPercentX = ((u * uv0Perspective.x) + (v * uv1Perspective.x) + (w * uv2Perspective.x)) /
								((u * z0Recip) + (v * z1Recip) + (w * z2Recip));
							const double texelPercentY = ((u * uv0Perspective.y) + (v * uv1Perspective.y) + (w * uv2Perspective.y)) /
								((u * z0Recip) + (v * z1Recip) + (w * z2Recip));

							// @todo: move this into two separate pixel shaders
							uint8_t texel;
							if (isMultiTextured)
							{
								const int layerTexelX = std::clamp(static_cast<int>(texelPercentX * texture1Width), 0, texture1Width - 1);
								const int layerTexelY = std::clamp(static_cast<int>(texelPercentY * texture1Height), 0, texture1Height - 1);
								const int layerTexelIndex = layerTexelX + (layerTexelY * texture1Width);
								texel = texture1Texels[layerTexelIndex];

								const bool isTransparent = texel == 0;
								if (isTransparent)
								{
									const int texelX = std::clamp(static_cast<int>(texelPercentX * texture0Width), 0, texture0Width - 1);
									const int texelY = std::clamp(static_cast<int>(texelPercentY * texture0Height), 0, texture0Height - 1);
									const int texelIndex = texelX + (texelY * texture0Width);
									texel = texture0Texels[texelIndex];
								}
							}
							else
							{
								const int texelX = std::clamp(static_cast<int>(texelPercentX * texture0Width), 0, texture0Width - 1);
								const int texelY = std::clamp(static_cast<int>(texelPercentY * texture0Height), 0, texture0Height - 1);
								const int texelIndex = texelX + (texelY * texture0Width);
								texel = texture0Texels[texelIndex];
							}
							
							if (debug_alphaTest)
							{
								const bool isTransparent = texel == 0;
								if (isTransparent)
								{
									continue;
								}
							}

							double shadingPercent;
							if (isFading)
							{
								shadingPercent = fadePercent;
							}
							else
							{
								// @todo: fix interpolated world space point calculation
								// XZ position of pixel center in world space.
								const Double2 v2D(
									(u * v0.x) + (v * v1.x) + (w * v2.x),
									(u * v0.z) + (v * v1.z) + (w * v2.z));
								const double distanceToLight = (v2D - eye2D).length();
								shadingPercent = std::clamp(distanceToLight / swConstants::PLAYER_LIGHT_DISTANCE, 0.0, 1.0);
							}
							
							const double lightLevelValue = shadingPercent * lightLevelCountReal;

							// Index into light table palettes.
							const int lightLevelIndex = std::clamp(static_cast<int>(lightLevelValue), 0, lightLevelCount - 1);

							// Percent through the current light level.
							const double lightLevelPercent = std::clamp(lightLevelValue - std::floor(lightLevelValue), 0.0, Constants::JustBelowOne);

							const int shadedTexelIndex = texel + (lightLevelIndex * lightLevelTexelCount);
							const uint8_t shadedTexel = lightLevelTexels[shadedTexelIndex];
							const uint32_t shadedTexelColor = paletteTexels[shadedTexel];

							colorBufferPtr[outputIndex] = shadedTexelColor;
							depthBufferPtr[outputIndex] = depth;
						}
					}
				}
			}
		}
	}
}

void SoftwareRenderer::ObjectTexture::init8Bit(int width, int height)
{
	this->texels.init(width, height);
}

void SoftwareRenderer::ObjectTexture::initPalette(int count)
{
	this->paletteTexels.init(count);
}

void SoftwareRenderer::ObjectTexture::clear()
{
	this->texels.clear();
	this->paletteTexels.clear();
}

void SoftwareRenderer::VertexBuffer::init(int vertexCount, int componentsPerVertex)
{
	const int valueCount = vertexCount * componentsPerVertex;
	this->vertices.init(valueCount);
}

void SoftwareRenderer::AttributeBuffer::init(int vertexCount, int componentsPerVertex)
{
	const int valueCount = vertexCount * componentsPerVertex;
	this->attributes.init(valueCount);
}

void SoftwareRenderer::IndexBuffer::init(int indexCount)
{
	this->indices.init(indexCount);
}

SoftwareRenderer::SoftwareRenderer()
{

}

SoftwareRenderer::~SoftwareRenderer()
{

}

void SoftwareRenderer::init(const RenderInitSettings &settings)
{
	this->depthBuffer.init(settings.width, settings.height);
}

void SoftwareRenderer::shutdown()
{
	this->depthBuffer.clear();
	this->vertexBuffers.clear();
	this->attributeBuffers.clear();
	this->indexBuffers.clear();
	this->objectTextures.clear();
}

bool SoftwareRenderer::isInited() const
{
	return this->depthBuffer.isValid();
}

void SoftwareRenderer::resize(int width, int height)
{
	this->depthBuffer.init(width, height);
	this->depthBuffer.fill(std::numeric_limits<double>::infinity());
}

bool SoftwareRenderer::tryCreateVertexBuffer(int vertexCount, int componentsPerVertex, VertexBufferID *outID)
{
	DebugAssert(vertexCount > 0);
	DebugAssert(componentsPerVertex >= 2);

	if (!this->vertexBuffers.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate vertex buffer ID.");
		return false;
	}

	VertexBuffer &buffer = this->vertexBuffers.get(*outID);
	buffer.init(vertexCount, componentsPerVertex);
	return true;
}

bool SoftwareRenderer::tryCreateAttributeBuffer(int vertexCount, int componentsPerVertex, AttributeBufferID *outID)
{
	DebugAssert(vertexCount > 0);
	DebugAssert(componentsPerVertex >= 2);

	if (!this->attributeBuffers.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate attribute buffer ID.");
		return false;
	}

	AttributeBuffer &buffer = this->attributeBuffers.get(*outID);
	buffer.init(vertexCount, componentsPerVertex);
	return true;
}

bool SoftwareRenderer::tryCreateIndexBuffer(int indexCount, IndexBufferID *outID)
{
	DebugAssert(indexCount > 0);
	DebugAssert((indexCount % 3) == 0);

	if (!this->indexBuffers.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate index buffer ID.");
		return false;
	}

	IndexBuffer &buffer = this->indexBuffers.get(*outID);
	buffer.init(indexCount);
	return true;
}

void SoftwareRenderer::populateVertexBuffer(VertexBufferID id, const BufferView<const double> &vertices)
{
	VertexBuffer &buffer = this->vertexBuffers.get(id);
	const int srcCount = vertices.getCount();
	const int dstCount = buffer.vertices.getCount();
	if (srcCount != dstCount)
	{
		DebugLogError("Mismatched vertex buffer sizes for ID " + std::to_string(id) + ": " +
			std::to_string(srcCount) + " != " + std::to_string(dstCount));
		return;
	}

	const auto srcBegin = vertices.get();
	const auto srcEnd = srcBegin + srcCount;
	std::copy(srcBegin, srcEnd, buffer.vertices.get());
}

void SoftwareRenderer::populateAttributeBuffer(AttributeBufferID id, const BufferView<const double> &attributes)
{
	AttributeBuffer &buffer = this->attributeBuffers.get(id);
	const int srcCount = attributes.getCount();
	const int dstCount = buffer.attributes.getCount();
	if (srcCount != dstCount)
	{
		DebugLogError("Mismatched attribute buffer sizes for ID " + std::to_string(id) + ": " +
			std::to_string(srcCount) + " != " + std::to_string(dstCount));
		return;
	}

	const auto srcBegin = attributes.get();
	const auto srcEnd = srcBegin + srcCount;
	std::copy(srcBegin, srcEnd, buffer.attributes.get());
}

void SoftwareRenderer::populateIndexBuffer(IndexBufferID id, const BufferView<const int32_t> &indices)
{
	IndexBuffer &buffer = this->indexBuffers.get(id);
	const int srcCount = indices.getCount();
	const int dstCount = buffer.indices.getCount();
	if (srcCount != dstCount)
	{
		DebugLogError("Mismatched index buffer sizes for ID " + std::to_string(id) + ": " +
			std::to_string(srcCount) + " != " + std::to_string(dstCount));
		return;
	}

	const auto srcBegin = indices.get();
	const auto srcEnd = srcBegin + srcCount;
	std::copy(srcBegin, srcEnd, buffer.indices.get());
}

void SoftwareRenderer::freeVertexBuffer(VertexBufferID id)
{
	this->vertexBuffers.free(id);
}

void SoftwareRenderer::freeAttributeBuffer(AttributeBufferID id)
{
	this->attributeBuffers.free(id);
}

void SoftwareRenderer::freeIndexBuffer(IndexBufferID id)
{
	this->indexBuffers.free(id);
}

bool SoftwareRenderer::tryCreateObjectTexture(int width, int height, bool isPalette, ObjectTextureID *outID)
{
	if (!this->objectTextures.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate object texture ID.");
		return false;
	}

	ObjectTexture &texture = this->objectTextures.get(*outID);
	if (!isPalette)
	{
		texture.init8Bit(width, height);
		texture.texels.fill(0);
	}
	else
	{
		texture.initPalette(width * height);
		texture.paletteTexels.fill(0);
	}

	return true;
}

bool SoftwareRenderer::tryCreateObjectTexture(const TextureBuilder &textureBuilder, ObjectTextureID *outID)
{
	const int width = textureBuilder.getWidth();
	const int height = textureBuilder.getHeight();
	if (!this->tryCreateObjectTexture(width, height, false, outID))
	{
		DebugLogWarning("Couldn't create " + std::to_string(width) + "x" + std::to_string(height) + " object texture.");
		return false;
	}

	ObjectTexture &texture = this->objectTextures.get(*outID);
	uint8_t *dstTexels = texture.texels.get();

	const TextureBuilder::Type textureBuilderType = textureBuilder.getType();
	if (textureBuilderType == TextureBuilder::Type::Paletted)
	{
		const TextureBuilder::PalettedTexture &palettedTexture = textureBuilder.getPaletted();
		const Buffer2D<uint8_t> &srcTexels = palettedTexture.texels;
		std::copy(srcTexels.get(), srcTexels.end(), dstTexels);
	}
	else if (textureBuilderType == TextureBuilder::Type::TrueColor)
	{
		DebugLogWarning("True color texture (dimensions " + std::to_string(width) + "x" + std::to_string(height) + ") not supported.");
		texture.texels.fill(0);
		const TextureBuilder::TrueColorTexture &trueColorTexture = textureBuilder.getTrueColor();
		const Buffer2D<uint32_t> &srcTexels = trueColorTexture.texels;
		//std::transform(srcTexels.get(), srcTexels.end(), dstTexels, ...)
	}
	else
	{
		DebugUnhandledReturnMsg(bool, std::to_string(static_cast<int>(textureBuilderType)));
	}

	return true;
}

bool SoftwareRenderer::tryCreateObjectMaterial(ObjectTextureID id0, ObjectTextureID id1, ObjectMaterialID *outID)
{
	if (id0 < 0)
	{
		DebugLogError("Invalid main object texture ID \"" + std::to_string(id0) + "\" to initialize material with.");
		return false;
	}

	if (id1 < 0)
	{
		DebugLogError("Invalid optional object texture ID \"" + std::to_string(id1) + "\" to initialize material with.");
		return false;
	}

	if (!this->objectMaterials.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate object material ID.");
		return false;
	}

	ObjectMaterial &material = this->objectMaterials.get(*outID);
	material.init(id0, id1);

	return true;
}

bool SoftwareRenderer::tryCreateObjectMaterial(ObjectTextureID id, ObjectMaterialID *outID)
{
	if (id < 0)
	{
		DebugLogError("Invalid main object texture ID \"" + std::to_string(id) + "\" to initialize material with.");
		return false;
	}

	if (!this->objectMaterials.tryAlloc(outID))
	{
		DebugLogError("Couldn't allocate object material ID.");
		return false;
	}

	ObjectMaterial &material = this->objectMaterials.get(*outID);
	material.init(id);

	return true;
}

LockedTexture SoftwareRenderer::lockObjectTexture(ObjectTextureID id)
{
	ObjectTexture &texture = this->objectTextures.get(id);
	if (texture.texels.isValid())
	{
		return LockedTexture(texture.texels.get(), false);
	}
	else if (texture.paletteTexels.isValid())
	{
		return LockedTexture(texture.paletteTexels.get(), true);
	}
	else
	{
		DebugNotImplemented();
		return LockedTexture(nullptr, false);
	}
}

void SoftwareRenderer::unlockObjectTexture(ObjectTextureID id)
{
	// Do nothing; any writes are already in RAM.
	static_cast<void>(id);
}

void SoftwareRenderer::freeObjectTexture(ObjectTextureID id)
{
	this->objectTextures.free(id);
}

void SoftwareRenderer::freeObjectMaterial(ObjectMaterialID id)
{
	this->objectMaterials.free(id);
}

std::optional<Int2> SoftwareRenderer::tryGetObjectTextureDims(ObjectTextureID id) const
{
	const ObjectTexture &texture = this->objectTextures.get(id);
	return Int2(texture.texels.getWidth(), texture.texels.getHeight());
}

bool SoftwareRenderer::tryGetObjectMaterialTextures(ObjectMaterialID id, ObjectTextureID *outID0, ObjectTextureID *outID1) const
{
	const ObjectMaterial &material = this->objectMaterials.get(id);
	*outID0 = material.id0;
	*outID1 = material.id1;
	return true;
}

bool SoftwareRenderer::tryGetEntitySelectionData(const Double2 &uv, ObjectTextureID textureID, bool pixelPerfect, bool *outIsSelected) const
{
	if (pixelPerfect)
	{
		// Get the texture list from the texture group at the given animation state and angle.
		const ObjectTexture &texture = this->objectTextures.get(textureID);
		const int textureWidth = texture.texels.getWidth();
		const int textureHeight = texture.texels.getHeight();

		const int textureX = static_cast<int>(uv.x * static_cast<double>(textureWidth));
		const int textureY = static_cast<int>(uv.y * static_cast<double>(textureHeight));

		if ((textureX < 0) || (textureX >= textureWidth) ||
			(textureY < 0) || (textureY >= textureHeight))
		{
			// Outside the texture; out of bounds.
			return false;
		}

		// Check if the texel is non-transparent.
		const uint8_t texel = texture.texels.get(textureX, textureY);
		*outIsSelected = texel != 0;
		return true;
	}
	else
	{
		// The entity's projected rectangle is hit if the texture coordinates are valid.
		const bool withinEntity = (uv.x >= 0.0) && (uv.x <= 1.0) && (uv.y >= 0.0) && (uv.y <= 1.0);
		*outIsSelected = withinEntity;
		return true;
	}
}

Double3 SoftwareRenderer::screenPointToRay(double xPercent, double yPercent, const Double3 &cameraDirection,
	Degrees fovY, double aspect) const
{
	return LegacyRendererUtils::screenPointToRay(xPercent, yPercent, cameraDirection, fovY, aspect);
}

RendererSystem3D::ProfilerData SoftwareRenderer::getProfilerData() const
{
	const int renderWidth = this->depthBuffer.getWidth();
	const int renderHeight = this->depthBuffer.getHeight();

	const int threadCount = 1;
	const int potentiallyVisTriangleCount = swGeometry::g_totalOpaqueVoxelTriangleCount +
		swGeometry::g_totalAlphaTestedVoxelTriangleCount + swGeometry::g_totalEntityTriangleCount;
	const int visTriangleCount = static_cast<int>(
		swGeometry::g_visibleOpaqueVoxelTriangles.size() +
		swGeometry::g_visibleAlphaTestedVoxelTriangles.size() +
		swGeometry::g_visibleEntityTriangles.size());
	const int visLightCount = 0;

	return ProfilerData(renderWidth, renderHeight, threadCount, potentiallyVisTriangleCount, visTriangleCount, visLightCount);
}

void SoftwareRenderer::submitFrame(const RenderCamera &camera, const BufferView<const RenderTriangle> &opaqueVoxelTriangles,
	const BufferView<const RenderTriangle> &alphaTestedVoxelTriangles, const BufferView<const RenderTriangle> &entityTriangles,
	const RenderFrameSettings &settings, uint32_t *outputBuffer)
{
	const int frameBufferWidth = this->depthBuffer.getWidth();
	const int frameBufferHeight = this->depthBuffer.getHeight();
	BufferView2D<uint32_t> colorBufferView(outputBuffer, frameBufferWidth, frameBufferHeight);
	BufferView2D<double> depthBufferView(this->depthBuffer.get(), frameBufferWidth, frameBufferHeight);

	// Palette for 8-bit -> 32-bit color conversion.
	const ObjectTexture &paletteTexture = this->objectTextures.get(settings.paletteTextureID);

	// Light table for shading/transparency look-ups.
	const ObjectTexture &lightTableTexture = this->objectTextures.get(settings.lightTableTextureID);

	const uint32_t clearColor = Color::Black.toARGB();
	swRender::ClearFrameBuffers(clearColor, colorBufferView, depthBufferView);

	// @optimization: these processing functions could be on separate threads
	const BufferView<const RenderTriangle> clippedOpaqueVoxelTriangles = swGeometry::ProcessTrianglesForRasterization(
		opaqueVoxelTriangles, camera, swGeometry::g_visibleOpaqueVoxelTriangles, swGeometry::g_visibleOpaqueVoxelClipList,
		&swGeometry::g_totalOpaqueVoxelTriangleCount);
	const BufferView<const RenderTriangle> clippedAlphaTestedVoxelTriangles = swGeometry::ProcessTrianglesForRasterization(
		alphaTestedVoxelTriangles, camera, swGeometry::g_visibleAlphaTestedVoxelTriangles, swGeometry::g_visibleAlphaTestedVoxelClipList,
		&swGeometry::g_totalAlphaTestedVoxelTriangleCount);
	const BufferView<const RenderTriangle> clippedEntityTriangles = swGeometry::ProcessTrianglesForRasterization(
		entityTriangles, camera, swGeometry::g_visibleEntityTriangles, swGeometry::g_visibleEntityClipList,
		&swGeometry::g_totalEntityTriangleCount);

	swRender::RasterizeTriangles(clippedOpaqueVoxelTriangles, false, this->objectMaterials, this->objectTextures,
		paletteTexture, lightTableTexture, camera, colorBufferView, depthBufferView);
	swRender::RasterizeTriangles(clippedAlphaTestedVoxelTriangles, true, this->objectMaterials, this->objectTextures,
		paletteTexture, lightTableTexture, camera, colorBufferView, depthBufferView);
	swRender::RasterizeTriangles(clippedEntityTriangles, true, this->objectMaterials, this->objectTextures,
		paletteTexture, lightTableTexture, camera, colorBufferView, depthBufferView);
}

void SoftwareRenderer::present()
{
	// Do nothing for now, might change later.
}
