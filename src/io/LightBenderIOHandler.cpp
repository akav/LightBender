#include "LightBenderIOHandler.hpp"

// system
#include <vector>
#include <algorithm>

// shared
#include "glad/glad.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "graphics/glfw/GlfwWrapper.hpp"
#include "graphics/opengl/OpenGLWrapper.hpp"
#include "graphics/Camera.hpp"
#include "io/ImguiCallback.hpp"
#include "imgui.h"

// project
#include "LightBenderCallback.hpp"
#include "LightBenderConfig.hpp"
#include "OptixBasicScene.hpp"
#include "OptixAdvancedScene.hpp"
#include "OptixModelScene.hpp"


namespace light
{


namespace
{

constexpr int defaultWidth  = 1280;
constexpr int defaultHeight = 720;

int displayType = 2;

bool pathTrace = false;

int cameraType  = 0;
int sqrtSamples = 1;

int maxBounces  = 5;
int firstBounce = 0;

std::string outputFilename = "lightBenderFrame.ppm";

}


/////////////////////////////////////////////
/// \brief LightBender::LightBender
///
/// \author Logan Barnes
/////////////////////////////////////////////
LightBenderIOHandler::LightBenderIOHandler( shared::World &world )
  : ImguiOpenGLIOHandler( world, true, defaultWidth, defaultHeight, false )
  , currentScene_       ( 0 )
{

  std::unique_ptr< graphics::Callback > upCallback( new LightBenderCallback( *this ) );

  imguiCallback_->setCallback( std::move( upCallback ) );


  //
  // OpenGL
  //
  upGLWrapper_->init( );

  upGLWrapper_->setViewportSize( defaultWidth, defaultHeight );

  std::string vertShader = SHADER_PATH + "screenSpace/shader.vert";
  std::string fragShader = SHADER_PATH + "screenSpace/shader.frag";

  upGLWrapper_->addProgram(
                           "fullscreenProgram",
                           vertShader.c_str( ),
                           fragShader.c_str( )
                           );

  // fullscreen buffer
  std::vector< float > vboData =
  {
    -1.0, -1.0, -1.0,
    -1.0,  1.0, -1.0,
    1.0, -1.0, -1.0,
    1.0,  1.0, -1.0
  };

  upGLWrapper_->addUVBuffer(
                            "screenBuffer",
                            "fullscreenProgram",
                            vboData.data( ),
                            static_cast< GLuint >( vboData.size( ) )
                            );

  upGLWrapper_->addTextureArray( "rayTex", defaultWidth, defaultHeight, 0 );

  // First allocate the memory for the GL buffer, then attach it to OptiX.
  upGLWrapper_->addBufferNoVAO< float >(
                                        "renderBuffer",
                                        static_cast< GLsizeiptr >( sizeof( optix::float4 ) )
                                        * defaultWidth * defaultHeight,
                                        0,
                                        GL_STREAM_DRAW
                                        );

    _setScene( );


  //
  // camera
  //
  upCamera_->setAspectRatio( defaultWidth * 1.0f / defaultHeight );
  upCamera_->updateOrbit( 20.0f, 45.0f, -30.0f );

}



/////////////////////////////////////////////
/// \brief LightBender::~LightBender
///
/// \author Logan Barnes
/////////////////////////////////////////////
LightBenderIOHandler::~LightBenderIOHandler( )
{}



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::rotateCamera
/// \param deltaX
/// \param deltaY
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::rotateCamera(
                                   double deltaX,
                                   double deltaY
                                   )
{

  upCamera_->updateOrbit( 0.f, static_cast< float >( deltaX ), static_cast< float >( deltaY ) );

  upScene_->resetFrameCount( );

}



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::zoomCamera
/// \param deltaZ
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::zoomCamera( double deltaZ )
{

  upCamera_->updateOrbit( static_cast< float >( deltaZ * 0.25 ), 0.f, 0.f );

  upScene_->resetFrameCount( );

}



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::resize
/// \param w
/// \param h
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::resize(
                             int w,
                             int h
                             )
{

  upGLWrapper_->setViewportSize( w, h );
  upCamera_->setAspectRatio( w * 1.0f / h );

  upScene_->resetFrameCount( );

}



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::saveFrame
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::saveFrame( )
{

  upScene_->saveFrame( light::OUTPUT_PATH + outputFilename );

}



/////////////////////////////////////////////
/// \brief LightBender::onRender
/// \param alpha
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::_onRender( const double )
{

  upGLWrapper_->clearWindow( );

  upGLWrapper_->useProgram  ( "fullscreenProgram" );

  if ( upScene_ )
  {

    upScene_->renderWorld( *upCamera_ );


    optix::Buffer buffer = upScene_->getBuffer( );

    RTsize elementSize = buffer->getElementSize( );

    int alignmentSize = 1;

    if ( ( elementSize % 8 ) == 0 )
    {
      alignmentSize = 8;
    }
    else
    if ( ( elementSize % 4 ) == 0 )
    {
      alignmentSize = 4;
    }
    else
    if ( ( elementSize % 2 ) == 0 )
    {
      alignmentSize = 2;
    }

    RTsize bufWidth, bufHeight;
    buffer->getSize( bufWidth, bufHeight );


    upGLWrapper_->bindBufferToTexture(
                                      "rayTex",
                                      buffer->getGLBOId( ),
                                      alignmentSize,
                                      static_cast< int >( bufWidth ),
                                      static_cast< int >( bufHeight )
                                      );

  }

  glm::vec2 screenSize(
                       upGLWrapper_->getViewportWidth( ),
                       upGLWrapper_->getViewportHeight( )
                       );

  upGLWrapper_->setFloatUniform(
                                "fullscreenProgram",
                                "screenSize",
                                glm::value_ptr( screenSize ),
                                2
                                );

  upGLWrapper_->setTextureUniform(
                                  "fullscreenProgram",
                                  "tex",
                                  "rayTex",
                                  0
                                  );

  upGLWrapper_->renderBuffer( "screenBuffer", 4, GL_TRIANGLE_STRIP );

} // LightBenderIOHandler::onRender



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::_onGuiRender
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::_onGuiRender( )
{

  bool alwaysTrue = true;

  ImGui::SetNextWindowSize( ImVec2( 345, defaultHeight * 1.0f ), ImGuiSetCond_FirstUseEver );
  ImGui::SetNextWindowPos ( ImVec2( 0, 0 ), ImGuiSetCond_FirstUseEver );

  ImGui::Begin( "Light Bender Settings", &alwaysTrue );

  //
  // FPS
  //
  ImGui::Text(
              "Application average %.3f ms/frame (%.1f FPS)",
              1000.0f / ImGui::GetIO( ).Framerate,
              ImGui::GetIO( ).Framerate
              );


  //
  // Scene selction
  //
  if ( ImGui::CollapsingHeader( "Scene", "scene", false, true ) )
  {

    bool oldPathTrace = pathTrace;
    ImGui::Checkbox( "Pathtrace", &pathTrace );

    if ( oldPathTrace != pathTrace )
    {

      upScene_->setPathTracing( pathTrace );
      upScene_->setCameraType ( cameraType );

    }

    if ( pathTrace )
    {

      int oldBounces = maxBounces;
      ImGui::SliderInt( "Max Bounces ", &maxBounces, 0, 10 );
      maxBounces = std::max( firstBounce, maxBounces );

      if ( oldBounces != maxBounces )
      {
        upScene_->setMaxBounces( static_cast< unsigned >( maxBounces ) );
      }


      int oldBounce = firstBounce;
      ImGui::SliderInt( "First Bounces ", &firstBounce, 0, 10 );
      firstBounce = std::min( firstBounce, maxBounces );

      if ( oldBounce != firstBounce )
      {
        upScene_->setFirstBounce( static_cast< unsigned >( firstBounce ) );
      }

    }


    //
    // camera stuff
    //
    ImGui::Separator( );
    ImGui::Text( "Camera" );

    int oldCamera = cameraType;
    ImGui::Combo( " ", &cameraType, " Perspective \0 Orthographic \0\0" );

    if ( oldCamera != cameraType )
    {
      upScene_->setCameraType( cameraType );
    }


    int oldSamples = sqrtSamples;
    ImGui::SliderInt( "Samples (sqrt)", &sqrtSamples, 1, 10 );

    if ( oldSamples != sqrtSamples  )
    {
      upScene_->setSqrtSamples( static_cast< unsigned >( sqrtSamples ) );
    }

    ImGui::Separator( );

    glm::vec3 eye ( upCamera_->getEye( ) );
    glm::vec3 look( upCamera_->getLook( ) );
    ImGui::Text( "EYE:  %2.2f, %2.2f, %2.2f",    eye.x,  eye.y,   eye.z  );
    ImGui::Text( "LOOK:  %1.2f,  %1.2f,  %1.2f", look.x, look.y, look.z );

  }

  //
  // Display type
  //
  ImGui::Separator( );

  int oldDisplay = displayType;
  ImGui::Combo( "Display", &displayType, " Normals \0 Simple Shading \0 BSDF \0\0" );

  if ( oldDisplay != displayType )
  {

    upScene_->setDisplayType( displayType );

  }

  ImGui::Separator( );
  ImGui::Text( "Type" );

  int oldScene = currentScene_;
  ImGui::Combo( "", &currentScene_, " Basic \0 Advanced \0 Model \0\0" );

  if ( oldScene != currentScene_ )
  {

    _setScene( );

  }

  upScene_->renderSceneGui( );

  //
  // output filename
  //
  if ( ImGui::CollapsingHeader( "Output", "output", false, false ) )
  {

    constexpr unsigned bufSize = 512;

    std::vector< char > cstr(
                             outputFilename.c_str( ),
                             outputFilename.c_str( ) + outputFilename.size( ) + 1
                             );

    cstr.resize( bufSize );

    ImGui::InputText( "Output file", cstr.data( ), bufSize );

    outputFilename = std::string( cstr.data( ) );

  }

  //
  // Control listing
  //
  if ( ImGui::CollapsingHeader( "Controls", "controls", false, true ) )
  {

    ImGui::Text(
                "Camera Movement:\n\n"
                "    Left Mouse      -    Rotate\n" \
                "    Right Mouse     -    Zoom\n"   \
                "    Middle Scroll   -    Zoom\n"
                );

  }


  ImGui::End( );

} // LightBenderIOHandler::onGuiRender



/////////////////////////////////////////////
/// \brief LightBenderIOHandler::_setScene
///
/// \author Logan Barnes
/////////////////////////////////////////////
void
LightBenderIOHandler::_setScene( )
{

  if ( upScene_ )
  {

    upScene_.reset( );

  }


  std::string modelFile = light::MODEL_PATH + "tie_interceptor/obj_format/tie_interceptor.obj";
//  std::string modelFile = light::MODEL_PATH + "x_wing/x-wing.obj";


  switch ( currentScene_ )
  {

  case 0:

    upScene_
      = std::unique_ptr< OptixScene >( new OptixBasicScene(
                                                           defaultWidth,
                                                           defaultHeight,
                                                           upGLWrapper_->getBuffer( "renderBuffer" )
                                                           ) );

    break;


  case 1:

    upScene_
      = std::unique_ptr< OptixScene >( new OptixAdvancedScene(
                                                              defaultWidth,
                                                              defaultHeight,
                                                              upGLWrapper_->getBuffer(
                                                                                      "renderBuffer"
                                                                                      )
                                                              ) );
    break;


  case 2:

    upScene_
      = std::unique_ptr< OptixScene >( new OptixModelScene(
                                                           defaultWidth,
                                                           defaultHeight,
                                                           upGLWrapper_->getBuffer( "renderBuffer" ),
                                                           modelFile
                                                           ) );
    break;


  default:

    throw std::runtime_error( "Unknown scene" );

    break;

  } // switch

  upScene_->setDisplayType( displayType );
  upScene_->setPathTracing( pathTrace );
  upScene_->setCameraType ( cameraType );
  upScene_->setMaxBounces ( static_cast< unsigned >( maxBounces ) );
  upScene_->setFirstBounce( static_cast< unsigned >( firstBounce ) );

} // LightBenderIOHandler::_setScene



} // namespace light
