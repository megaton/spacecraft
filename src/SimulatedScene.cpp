#include "SimulatedScene.hpp"

#include <QGLShader>
#include <QKeyEvent>
#include <QApplication>
#include <QMatrix>
#include <QImage>
#include <QFile>

#include "Utils.hpp"
#include "GLRenderWidget.hpp"
#include "MeshData/Stars.hpp"

#include <memory>
#include <math.h>
#include <iostream>
#include <fstream>

#include <QtNetwork>

#include "Messages/ClientMessageHandler.h"

using namespace Eigen;
using namespace std;
//using namespace messages;


SimulatedScene::SimulatedScene(float initial_WTR) :
  Scene( NULL ),
  m_text( "LMMonoCaps10", 10, Qt::green ),
  m_lastTime( 0 ),
  m_velocity( Vector3f::Zero() ),
  m_playerShip( 0 )
{
  WORLD_TIME_RATIO = initial_WTR;

  m_camera = CameraPtr( new Camera() );

  m_tcpSocket = new QTcpSocket(this);

  connect( m_tcpSocket, SIGNAL( readyRead() ), this, SLOT( readMessage() ) );

  //! хак(?) для проверки не пришло ли ещё что-то в сокет
  QTimer* timer = new QTimer( this );
  timer->start( 200 );
  connect( timer, SIGNAL( timeout() ), this, SLOT( readMessage() ) );

  m_handler = new mes::ClientHandler<mes::MessageTypes>( *this );

  Eigen::AngleAxisd asd( 1.5, Eigen::Vector3d( 1, 1, 1 ) );
  std::cout << asd.angle();

}

void SimulatedScene::connectToServer( QString addres, int port )
{
  m_tcpSocket->abort();
  m_tcpSocket->connectToHost( addres, port );
}

bool SimulatedScene::addModelFromFile( QString modelFileName, int modelId )
{
  bool res = false;
  if( QFile::exists( modelFileName ) )
  {
    ShipModel* newShip = new ShipModel( modelFileName.toStdString() );
    if( !m_playerShip )
    {
      m_playerShip = newShip;
      m_playerShipName = modelFileName;
    }
    if( modelId >= 0 ) newShip->setId( modelId );
    m_sceneObjects.push_back( BaseSceneObjectPtr( newShip  ) );
    m_sceneObjectNames.push_back( modelFileName.toStdString() );

    res = true;
  }
  return res;
}

bool SimulatedScene::loadSceneFromFile( QString sceneFileName )
{
  bool res = false;
  m_sceneObjects.clear();

  if( QFile::exists( sceneFileName ) )
  {
    res = true;

    ifstream infile;
    infile.open( sceneFileName.toStdString().c_str() );

    size_t objCount;
    infile >> objCount;

    for( size_t i = 0; i < objCount; i++ )
    {
      string modelFileName;
      infile >> modelFileName;
      float x, y, z;
      infile >> x; infile >> y; infile >> z;

      if( addModelFromFile( QString( modelFileName.c_str() ) ) )
        m_sceneObjects.back()->position() = Vector3d( x, y, z );

    }

    infile.close();
  }

  return res;
}

void SimulatedScene::readMessage()
{
  QDataStream in( m_tcpSocket );
  in.setVersion( QDataStream::Qt_4_0 );

  static quint16 blockSize = 0;
  if ( blockSize == 0 ) {
      if ( m_tcpSocket->bytesAvailable() < (int)sizeof( quint16 ) )
          return;

      in >> blockSize;
  }

  if ( m_tcpSocket->bytesAvailable() < blockSize )
      return;

  m_dispatcher.dispatch( in, *m_handler );

  blockSize = 0;
}

void SimulatedScene::handleDataUpdate( mes::MessageSnapshot* msg )
{
  for( int i = 0; i < msg->objIDs.size(); i++ )
  {
    int id = msg->objIDs[ i ];
    BaseSceneObjectPtr currObj;
    for( BaseSceneObjectPtr obj: m_sceneObjects )
    {
      if( obj->m_id == id )
      {
        currObj = obj;
        break;
      }
    }
    if( currObj )
    {
      currObj->m_position = qtVectorToEigend( msg->positions[ i ] );
      currObj->m_velocity = qtVectorToEigend( msg->velocities[ i ] );
      currObj->m_rotation = Eigen::AngleAxisd( msg->rotAngles[ i ], qtVectorToEigend( msg->rotAxes[ i ] ) );
      currObj->m_angularVelocity = qtVectorToEigend( msg->angularVelocities[ i ] );
      currObj->m_massCenter = qtVectorToEigend( msg->massCenteres[ i ] );
    }
  }
}

SimulatedScene::~SimulatedScene()
{
  delete m_handler;
}

void SimulatedScene::initialize()
{
    assert( m_widget );

    if ( !prepareShaderProgram( m_cubeShader,
                                QString( SPACECRAFT_PATH ) + "/shaders/blockMaterial.vert",
                                QString( SPACECRAFT_PATH ) + "/shaders/blockMaterial.frag" ) )
      return;

    if ( !prepareShaderProgram( m_starShader,
                                QString( SPACECRAFT_PATH ) + "/shaders/stars.vert",
                                QString( SPACECRAFT_PATH ) + "/shaders/stars.frag" ) )
      return;

    QImage starsImage( QString( SPACECRAFT_PATH ) + "/images/stars.jpg" );
    starsImage.setAlphaChannel( QImage( QString( SPACECRAFT_PATH ) + "/images/starsAlpha.jpg" ) );
    m_textures.insert( "stars", m_widget->bindTexture( starsImage ) );

    QImage blocksImage( QString( SPACECRAFT_PATH ) + "/images/block_faces.png" );
    m_textures.insert( "block_faces", m_widget->bindTexture( blocksImage ) );


    if ( !m_cubeShader.bind() )
    {
        qWarning() << "Could not bind shader program to context";
        return;
    }

    for( BaseSceneObjectPtr obj: m_sceneObjects )
    {
      obj->refreshModel();
      obj->attachShader( m_cubeShader );
    }

    StarBuilder stars;
    stars.buildStarMesh();
    m_starMesh.writeSimpleData( stars.getVertices(), stars.getTexcoords(), stars.getVerticeCount() );
    m_starMesh.attachShader( m_starShader );

    m_cubeShader.release();

    m_camera->setPosition( Eigen::Vector3f( 0.0, 0.0, 0.0 ) );

}

bool engineRunning = false;

void SimulatedScene::draw()
{

//  m_shipModel.octreeRaycastIntersect( m_camera->position(), m_camera->view(), m_minIntersection );

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  QMatrix4x4 modelMatrix;
  //QMatrix4x4 projectionMatrix( m_camera->projectionMatrix() );
  QMatrix4x4 viewMatrix( m_camera->viewMatrix() );
  QMatrix4x4 viewStarMatrix( viewMatrix );

  // зануляем translation часть в видовой матрице для звёзд
  viewStarMatrix.setColumn( 3, QVector4D( 0.0, 0.0, 0.0, 1.0 ) );

  viewStarMatrix.scale( 10.0 );

  m_starShader.bind();

  glBindTexture( GL_TEXTURE_2D, m_textures.find( "stars" ).value() );

  glDisable( GL_DEPTH_TEST );

  m_starShader.setUniformValue( "projectionMatrix", projectionMatrix );
  m_starShader.setUniformValue( "viewMatrix", viewStarMatrix );
  m_starShader.setUniformValue( "colorTexture", 0 );

  m_starMesh.drawSimple();
  m_starShader.release();

  glEnable( GL_DEPTH_TEST );

  m_cubeShader.bind();

  glBindTexture( GL_TEXTURE_2D, m_textures.find( "block_faces" ).value() );

  for( BaseSceneObjectPtr obj: m_sceneObjects )
  {
    modelMatrix.setToIdentity();
    modelMatrix.translate( eigenVectorToQt( obj->position() ) );

    modelMatrix.rotate( obj->rotation().angle() / M_PI * 180.,
                        eigenVectorToQt( obj->rotation().axis() ) );

    modelMatrix.translate( eigenVectorToQt( (Vector3d)(obj->getMassCenter() * (-1)) ) );//по-моему если от объекта чёт отвалится и центр масс сместится тут будет косяк (объект "внезапно" переместится с учётам нового цнтра масс)

    m_cubeShader.setUniformValue( "projectionMatrix", projectionMatrix );
    m_cubeShader.setUniformValue( "viewMatrix", viewMatrix );
    m_cubeShader.setUniformValue( "modelMatrix", modelMatrix );
    m_cubeShader.setUniformValue( "colorTexture", 0 );


    obj->draw();
  }

  glBindTexture( GL_TEXTURE_2D, 0 );

  m_cubeShader.release();

  m_text.add( "fps\t", dynamic_cast< GLRenderWidget* >( m_widget )->getFPS() );
  m_text.add( "camera\t", m_camera->position() );

  m_text.add( "engine run\t", engineRunning );

  m_text.draw( m_widget, 10, 15 );
  m_text.clear();

  m_text.add("+");
  m_text.draw( m_widget, m_widget->width()/2 - 2, m_widget->height()/2 + 3 );
  m_text.clear();

}


void SimulatedScene::process( int newTime )
{
  int deltaTime = newTime - m_lastTime;
  m_lastTime = newTime;

  float delta = deltaTime / 100.f;

  m_velocity *= 0.7;

  applyInput();

  m_camera->translate( m_velocity * delta );

//  for( BaseSceneObjectPtr obj: m_sceneObjects )
//    obj->process( delta );
}

void SimulatedScene::applyInput()
{
  Vector3f delta( 0.0F, 0.0F, 0.0F );
  Vector3f deltaVelocity;
  float scale = 1.0;

  InputMap::const_iterator i;

  i = m_inputMap.find( Qt::Key_W );
  if( i != m_inputMap.end() && i.value() == true )
    delta[ 2 ] += 1.0;

  i = m_inputMap.find( Qt::Key_S );
  if( i != m_inputMap.end() && i.value() == true )
    delta[ 2 ] -= 1.0;

  i = m_inputMap.find( Qt::Key_A );
  if( i != m_inputMap.end() && i.value() == true )
    delta[ 0 ] += 1.0;

  i = m_inputMap.find( Qt::Key_D );
  if( i != m_inputMap.end() && i.value() == true )
    delta[ 0 ] -= 1.0;

  deltaVelocity = m_camera->rotation() * delta;

  m_velocity += deltaVelocity * scale;

}

void SimulatedScene::viewportResize( int w, int h )
{
  m_camera->viewportResize( w, qMax( h, 1 ) );
  projectionMatrix = m_camera->projectionMatrix();
}

void SimulatedScene::keyPressEvent( QKeyEvent *e )
{
  static bool wireframeMode = false;
  switch ( e->key() )
  {
  case Qt::Key_Escape:
    QCoreApplication::instance()->quit();
    break;
  case Qt::Key_F:
    if( wireframeMode )
      glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    else
      glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    wireframeMode = !wireframeMode;
  break;
/*
  case Qt::Key_M:
    m_camera->setPosition( m_shipModel.getMassCenter() );
  break;*/

  case Qt::Key_E:
    engineRunning = !engineRunning;

    mes::MessageWrapper<mes::MessageEngines, mes::MessageTypes> msg;
    msg.clientId = m_ID;
    msg.enginesEnabled = engineRunning;
    sendMessage( msg, m_tcpSocket );
  break;

  }
}

void SimulatedScene::wheelEvent( QWheelEvent* event )
{
  m_camera->translate( m_camera->rotation() *
                       Vector3f( 0.0, 0.0, static_cast< GLfloat >( event->delta() ) * 1.0 / 100 ) );
}

void SimulatedScene::mousePressEvent( QMouseEvent* event )
{
  m_lastMousePos = event->pos();
}

void SimulatedScene::mouseMoveEvent( QMouseEvent* event )
{
  GLfloat dx = static_cast< GLfloat >( event->pos().x() - m_lastMousePos.x() ) / m_widget->width () / 2 * M_PI;
  GLfloat dy = static_cast< GLfloat >( event->pos().y() - m_lastMousePos.y() ) / m_widget->height() / 2 * M_PI;

  if( ( event->buttons() & Qt::LeftButton ) | ( event->buttons() & Qt::RightButton ) )
  {
    m_camera->eyeTurn( dx, dy );
  }

  m_lastMousePos = event->pos();
}

