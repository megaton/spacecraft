#version 330 core

// текстура
uniform sampler2D colorTexture;

// параметры, полученные из вершинного шейдера
in vec2 fragTexcoord;
in vec4 pos;

// результирующий цвет пикселя на экране
out vec4 color;

void main(void)
{
    color = texture(colorTexture, fragTexcoord);
//    color.a = 0.5;
}
