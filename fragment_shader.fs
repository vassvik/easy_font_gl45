#version 450 core

in vec2 uv;
in float color_index;

layout(binding = 0) uniform sampler2D sampler_font;

uniform vec3 bgColor;
uniform vec3 fgColor;

out vec3 color;

vec3 pal( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d )
{
    return a + b*cos( 6.28318*(c*t+d) );
}

void main()
{
    vec2 res_font = textureSize(sampler_font, 0);
    vec2 uv2 = uv - vec2(0.5, 0.5)/res_font; // sample center of texel

    float s =/* test */smoothstep(0.4, 0.6, texture(sampler_font, uv2).r);
    
    if (color_index < 0.5) {
    	color = bgColor*s + fgColor*(1.0 - s);
    } else {
	    // https://www.shadertoy.com/view/ll2GD3
	    vec3 col = pal( color_index/5.0, vec3(0.5,0.5,0.5),vec3(0.5,0.5,0.5),vec3(1.0,1.0,1.0),vec3(0.0,0.33,0.67) );
	    color = bgColor*s + col*(1.0 - s);
    }
}
/* test */