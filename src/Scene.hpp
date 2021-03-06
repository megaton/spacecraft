#ifndef SCENE_HPP
#define SCENE_HPP

#include <QObject>
#include <QMap>
#include <QKeyEvent>
#include <QGLWidget>
#include <QGLShaderProgram>

#include <memory>

typedef QMap< int, bool > InputMap;

class Scene : public QObject
{
    Q_OBJECT
public:
    explicit Scene( QObject *parent = 0 );
    virtual ~Scene(){}

    virtual void initialize() = 0;
    virtual void draw() = 0;

    virtual void process( int newTime ) = 0;

    virtual void viewportResize( int width, int height ) = 0;

    InputMap& getInputMap() { return m_inputMap; }
    void setWidget( QGLWidget* widget ){ m_widget = widget; }

    virtual void keyPressEvent( QKeyEvent* e ) = 0;
    virtual void mouseMoveEvent( QMouseEvent* e ) = 0;
    virtual void mousePressEvent( QMouseEvent* e ) = 0;
    virtual void wheelEvent( QWheelEvent* e ) = 0;

protected:

    QMatrix4x4 projectionMatrix;

    InputMap m_inputMap;
    QGLWidget* m_widget;

};

typedef std::shared_ptr< Scene > ScenePtr;

#endif // SCENE_HPP
