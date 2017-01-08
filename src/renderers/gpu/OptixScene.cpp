#include "OptixScene.hpp"
#include "optixMod/optix_math_stream_namespace_mod.h"
#include "LightBenderConfig.hpp"
#include "graphics/Camera.hpp"
#include "commonStructs.h"


namespace light
{



///////////////////////////////////////////////////////////////
/// \brief OptixScene::OptixScene
///////////////////////////////////////////////////////////////
OptixScene::OptixScene(
                       int      width,
                       int      height,
                       unsigned vbo
                       )
  : OptixRenderer ( width, height, vbo )
  , sceneMaterial_( context_->createMaterial( ) )
{

  //
  // Various ways to display objects
  //
  std::string brdfPtxFile(
                          light::RES_PATH
                          + "ptx/cudaLightBender_generated_Brdf.cu.ptx"
                          );

  materialPrograms_.push_back( context_->createProgramFromPTXFile(
                                                                  brdfPtxFile,
                                                                  "closest_hit_normals"
                                                                  ) );

  materialPrograms_.push_back( context_->createProgramFromPTXFile(
                                                                  brdfPtxFile,
                                                                  "closest_hit_simple_shading"
                                                                  ) );

  materialPrograms_.push_back( context_->createProgramFromPTXFile(
                                                                  brdfPtxFile,
                                                                  "closest_hit_bsdf"
                                                                  ) );

  sceneMaterial_->setClosestHitProgram( 0, materialPrograms_.back( ) );


  //
  // for lights
  //
  materialPrograms_.push_back( context_->createProgramFromPTXFile(
                                                                  brdfPtxFile,
                                                                  "closest_hit_emission"
                                                                  ) );


  //
  // for shadowing
  //
  materialPrograms_.push_back( context_->createProgramFromPTXFile(
                                                                  brdfPtxFile,
                                                                  "any_hit_occlusion"
                                                                  ) );

  sceneMaterial_->setAnyHitProgram( 1, materialPrograms_.back( ) );

  sceneMaterial_[ "albedo"    ]->setFloat( 0.71f, 0.62f, 0.53f );
  sceneMaterial_[ "roughness" ]->setFloat( 0.3f );


  // defaults
  setDisplayType( 2 ); // oren-nayar diffuse brdf
  setCameraType ( 0 ); // basic non-pathtracer
  setSqrtSamples( 1 ); // single sample per pixel on each pass
  setMaxBounces ( 5 ); // only allow 5 bounces
  setFirstBounce( 0 ); // start rendering on first bounce

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::~OptixScene
///////////////////////////////////////////////////////////////
OptixScene::~OptixScene( )
{}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::setDisplayType
/// \param type
///////////////////////////////////////////////////////////////
void
OptixScene::setDisplayType( int type )
{

  optix::Program currentProgram = materialPrograms_[ static_cast< size_t >( type ) ];

  sceneMaterial_->setClosestHitProgram( 0, currentProgram );

  for ( unsigned i = 0; i + 1 < shapes_.size( ); ++i )
  {

    ShapeGroup &s = shapes_[ i ];

    for ( optix::Material &mat : s.materials )
    {

      mat->setClosestHitProgram( 0, currentProgram );

    }

  }

  resetFrameCount( );

} // OptixScene::setDisplayType



///////////////////////////////////////////////////////////////
/// \brief OptixScene::setMaxBounces
/// \param bounces
///////////////////////////////////////////////////////////////
void
OptixScene::setMaxBounces( unsigned bounces )
{

  context_[ "max_bounces" ]->setUint( bounces );

  resetFrameCount( );

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::setFirstBounce
/// \param bounce
///////////////////////////////////////////////////////////////
void
OptixScene::setFirstBounce( unsigned bounce )
{

  context_[ "first_bounce" ]->setUint( bounce );

  resetFrameCount( );

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createBoxPrimitive
/// \param min
/// \param max
/// \return
///////////////////////////////////////////////////////////////
optix::Geometry
OptixScene::createBoxPrimitive(
                               optix::float3 min,
                               optix::float3 max
                               )
{

  // Create box
  std::string box_ptx( light::RES_PATH + "ptx/cudaLightBender_generated_Box.cu.ptx" );
  optix::Program box_bounds    = context_->createProgramFromPTXFile( box_ptx, "box_bounds" );
  optix::Program box_intersect = context_->createProgramFromPTXFile( box_ptx, "box_intersect" );

  optix::Geometry box = context_->createGeometry( );
  box->setPrimitiveCount( 1u );
  box->setBoundingBoxProgram( box_bounds );
  box->setIntersectionProgram( box_intersect );
  box[ "boxmin" ]->setFloat( min );
  box[ "boxmax" ]->setFloat( max );

  return box;

} // OptixScene::createBoxPrimitive



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createSpherePrimitive
/// \param center
/// \param radius
/// \return
///////////////////////////////////////////////////////////////
optix::Geometry
OptixScene::createSpherePrimitive(
                                  optix::float3 center,
                                  float         radius
                                  )
{

  // Create sphere
  std::string sphere_ptx( light::RES_PATH + "ptx/cudaLightBender_generated_Sphere.cu.ptx" );
  optix::Program sphere_bounds    = context_->createProgramFromPTXFile( sphere_ptx, "bounds" );
  optix::Program sphere_intersect = context_->createProgramFromPTXFile( sphere_ptx, "intersect" );

  optix::Geometry sphere = context_->createGeometry( );
  sphere->setPrimitiveCount( 1u );
  sphere->setBoundingBoxProgram ( sphere_bounds );
  sphere->setIntersectionProgram( sphere_intersect );
  sphere[ "sphere" ]->setFloat( center.x, center.y, center.z, radius );

  return sphere;

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createQuadPrimitive
/// \param anchor
/// \param v1
/// \param v2
/// \return
///////////////////////////////////////////////////////////////
optix::Geometry
OptixScene::createQuadPrimitive(
                                optix::float3 anchor,
                                optix::float3 v1,
                                optix::float3 v2
                                )
{

  // Create quad
  std::string quad_ptx( light::RES_PATH + "ptx/cudaLightBender_generated_Parallelogram.cu.ptx" );
  optix::Program quad_bounds    = context_->createProgramFromPTXFile( quad_ptx, "bounds" );
  optix::Program quad_intersect = context_->createProgramFromPTXFile( quad_ptx, "intersect" );

  optix::Geometry quad = context_->createGeometry( );
  quad->setPrimitiveCount( 1u );
  quad->setBoundingBoxProgram( quad_bounds );
  quad->setIntersectionProgram( quad_intersect );

  optix::float3 normal = optix::cross( v1, v2 );

  normal = normalize( normal );

  float d = dot( normal, anchor );
  v1 *= 1.0f / dot( v1, v1 );
  v2 *= 1.0f / dot( v2, v2 );
  optix::float4 plane = optix::make_float4( normal, d );

  quad[ "plane"  ]->setFloat( plane.x, plane.y, plane.z, plane.w );
  quad[ "v1"     ]->setFloat( v1.x, v1.y, v1.z );
  quad[ "v2"     ]->setFloat( v2.x, v2.y, v2.z );
  quad[ "anchor" ]->setFloat( anchor.x, anchor.y, anchor.z );

  return quad;

} // OptixScene::createQuadPrimitive



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createMaterial
/// \param closestHitProgram
/// \param anyHitProgram
/// \return
///////////////////////////////////////////////////////////////
optix::Material
OptixScene::createMaterial(
                           optix::Program closestHitProgram,
                           optix::Program anyHitProgram
                           )
{

  optix::Material material = context_->createMaterial( );

  material->setClosestHitProgram( 0, closestHitProgram );

  if ( anyHitProgram )
  {

    material->setAnyHitProgram ( 1, anyHitProgram );

  }

  return material;

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createGeomGroup
/// \param geometries
/// \param materials
/// \param builderAccel
/// \param traverserAccel
/// \return
///////////////////////////////////////////////////////////////
optix::GeometryGroup
OptixScene::createGeomGroup(
                            const std::vector< optix::Geometry > &geometries,
                            const std::vector< optix::Material > &materials,
                            const std::string                    &builderAccel,
                            const std::string                    &traverserAccel
                            )
{

  // check for one to one mapping of geometries and materials
  if ( materials.size( ) != geometries.size( ) )
  {

    throw std::runtime_error( "Geometries and Materials must contain the same number of elements" );

  }

  unsigned numInstances = static_cast< unsigned >( geometries.size( ) );

  // fill out geometry group for all geometries and materials
  optix::GeometryGroup geometryGroup = context_->createGeometryGroup( );
  geometryGroup->setChildCount( numInstances );

  for ( unsigned i = 0; i < numInstances; ++i )
  {

    optix::GeometryInstance gi = context_->createGeometryInstance( );

    gi->setGeometry( geometries[ i ] );
    gi->setMaterialCount( 1 );
    gi->setMaterial( 0, materials[ i ] );

    geometryGroup->setChild( i, gi );

  }

  geometryGroup->setAcceleration( context_->createAcceleration(
                                                               builderAccel.c_str( ),
                                                               traverserAccel.c_str( )
                                                               ) );

  return geometryGroup;

} // OptixScene::createGeomGroup



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createShapeGeomGroup
/// \param pShape
///////////////////////////////////////////////////////////////
void
OptixScene::createShapeGeomGroup( ShapeGroup *pShape )
{

  pShape->group = createGeomGroup(
                                  pShape->geometries,
                                  pShape->materials,
                                  pShape->builderAccel,
                                  pShape->traverserAccel
                                  );

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::attachToGroup
/// \param group
/// \param geomGroup
/// \param translation
/// \param scale
/// \param rotationAngle
/// \param rotationAxis
///////////////////////////////////////////////////////////////
void
OptixScene::attachToGroup(
                          optix::Group         group,
                          optix::GeometryGroup geomGroup,
                          unsigned             childNum,
                          optix::float3        translation,
                          optix::float3        scale,
                          float                rotationAngle,
                          optix::float3        rotationAxis
                          )
{

  optix::Transform trans = context_->createTransform( );
  trans->setChild( geomGroup );
  group->setChild( childNum, trans );

  optix::Matrix4x4 T = optix::Matrix4x4::translate( translation );
  optix::Matrix4x4 S = optix::Matrix4x4::scale( scale );
  optix::Matrix4x4 R = optix::Matrix4x4::rotate( rotationAngle, rotationAxis );

  optix::Matrix4x4 M = T * R * S;

  trans->setMatrix( false, M.getData( ), 0 );

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::attachToGroup
/// \param group
/// \param geomGroup
/// \param childNum
/// \param M
///////////////////////////////////////////////////////////////
void
OptixScene::attachToGroup(
                          optix::Group         group,
                          optix::GeometryGroup geomGroup,
                          unsigned             childNum,
                          optix::Matrix4x4     M
                          )
{

  optix::Transform trans = context_->createTransform( );
  trans->setChild( geomGroup );
  group->setChild( childNum, trans );

  trans->setMatrix( false, M.getData( ), 0 );

}



///////////////////////////////////////////////////////////////
/// \brief OptixScene::createShapeGroup
/// \param geometries
/// \param materials
/// \param builderAccel
/// \param traverserAccel
/// \param translation
/// \param scale
/// \param rotationAngle
/// \param rotationAxis
/// \return
///////////////////////////////////////////////////////////////
ShapeGroup
OptixScene::createShapeGroup(
                             const std::vector< optix::Geometry > &geometries,
                             const std::vector< optix::Material > &materials,
                             const std::string                    &builderAccel,
                             const std::string                    &traverserAccel,
                             optix::float3                         translation,
                             optix::float3                         scale,
                             float                                 rotationAngle,
                             optix::float3                         rotationAxis
                             )
{

  ShapeGroup shape;

  shape.geometries     = geometries;
  shape.materials      = materials;
  shape.builderAccel   = builderAccel;
  shape.traverserAccel = traverserAccel;

  optix::Matrix4x4 T = optix::Matrix4x4::translate( translation );
  optix::Matrix4x4 S = optix::Matrix4x4::scale( scale );
  optix::Matrix4x4 R = optix::Matrix4x4::rotate( rotationAngle, rotationAxis );

  shape.transform = T * R * S;

  createShapeGeomGroup( &shape );

  return shape;

} // OptixScene::createShapeGroup



} // namespace light
