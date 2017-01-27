#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG // for loading "easy_font_raw.png"
#include "stb_image.h"

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

/*
    Uniform random numbers between 0.0 (inclusive) and 1.0 (exclusive)
    Lehmer RNG, "minimal standard"
*/
double rng()
{
    static unsigned int seed = 123;
    seed *= 16807;
    return seed / (double)0x100000000ULL;
}

GLFWwindow *window;
double resx = 900;
double resy = 600;

double prevx, prevy;    // for mouse position
int clickedButtons = 0; // bit field for mouse clicks

enum buttonMaps { FIRST_BUTTON=1, SECOND_BUTTON=2, THIRD_BUTTON=4, FOURTH_BUTTON=8, FIFTH_BUTTON=16, NO_BUTTON=0 };
enum modifierMaps { CTRL=2, SHIFT=1, ALT=4, META=8, NO_MODIFIER=0 };

// all glfw and opengl init here
void init_GL();

// utility functions to load shaders
char *readFile(const char *filename);
GLuint LoadShaders(const char * vertex_file_path,const char * fragment_file_path);

// callback functions to send to glfw
void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods);
void mousebutton_callback(GLFWwindow* win, int button, int action, int mods);
void mousepos_callback(GLFWwindow* win, double xpos, double ypos);
void mousewheel_callback(GLFWwindow* win, double xoffset, double yoffset);
void windowsize_callback(GLFWwindow *win, int width, int height);

#define MAX_STRING_LEN 40000

/*
    The font stores all the glyphs sequentially, 
    so the texture is "size of font" high, and
    sum(glyph_widths) wide

    one string can hold up to MAX_STRING_LEN chars, which is hard coded to 40k...
    there's really no need to have multiple vbos for multiple strings, because
    it's so damn fast anyway. 40k is the max used storage, buy usually you'll just
    use the first 100 or so chars

    uses instancing, so that the only thing that need to be updated is the
    position of each glyph in the string (relative to the lower left corner)
    and its index for lookup in the metadata texture

    the metadata texture values are then used to look up in the bitmap texture

*/
typedef struct Font {
    // font info and data
    int height;                 // font height (with padding), 10 px for easy_font_raw.png
    int width;                  // width of texture
    int width_padded;           // opengl wants textures that are multiple of 4 wide
    unsigned char *font_bitmap; // RGB
    
    int num_glyphs;     // 96 glyphs for easy_font_raw.png
    int *glyph_widths;  // variable width of each glyph
    int *glyph_offsets; // starting point of each glyph

    // opengl stuff
    GLuint vao; 
    
    // vbo used for glyph instancing, it's just [0,1]x[0,1]
    GLuint vbo_glyph_pos_instance;
    
    // 2D texture for bitmap data
    GLuint texture_fontdata;

    // 1D texture for glyph metadata, RGBA: (glyph_offset_x, glyph_offset_y, glyph_width, glyph_height)
    // normalized (since it's a texture)
    GLuint texture_metadata;

    GLuint vbo_code_instances;  // vec3: (char_pos_x, char_pos_y, char_index)
    char str[MAX_STRING_LEN];   // persistent storage of string
    int ctr;                    // the number of glyphs to draw (i.e. the length of the string minus newlines)
} Font;


typedef struct vec2 {
    float x, y;
} vec2;

typedef struct Color {
    float R, G, B;
} Color;

// console printing, for debugging purposes
void font_print_string(char *str, Font *font);

void font_read(char *filename, Font *font);
void font_clean(Font *font);
void font_setup_texture(Font *font);
void font_setup_text(Font *font);
void font_update_text(Font *font);
void font_draw(Font *font, vec2 offset, Color bgColor, Color fgColor, float size);

GLuint program_text;

int main() 
{
    init_GL();

    program_text = LoadShaders( "vertex_shader.vs", "fragment_shader.fs" );

    Font font = {0};
    font_read("easy_font_raw.png", &font);
    font_print_string("wi asd abd i w", &font); // for testing if texture is loaded correctly

    font_setup_text(&font);

    double t1 = glfwGetTime();
    double avg_dt = 1.0/60;
    double alpha = 0.01;

    Color fgColor = {248/255.0, 248/255.0, 242/225.0};
    Color bgColor = {68/255.0, 71/255.0, 90/225.0};

    while ( !glfwWindowShouldClose(window)) { 
        // calculate fps
        double t2 = glfwGetTime();
        double dt = t2 - t1;
        avg_dt = alpha*dt + (1.0 - alpha)*avg_dt;
        t1 = t2;

        glfwPollEvents();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        {
            vec2 offset = {-1.0 + 4.0*1.0/resx, 1.0 - 4.0*12.0/resy};
            sprintf(font.str, "fps = %.3f\n\0", 1.0/avg_dt);
            font_update_text(&font);
            //glFinish();
            font_draw(&font, offset, bgColor, fgColor, 2.0);
            glFinish();
        }
        {
            vec2 offset = {-1.0 + 4.0*1.0/resx, 1.0 - 4.0*32.0/resy};
            sprintf(font.str, "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    vec2 uv = fragCoord.xy / iResolution.xy;\n    fragColor = vec4(uv,0.5+0.5*sin(iGlobalTime), 1.0);\n}\n");
            sprintf(font.str, "asdas dasd fps = %.3f\n\0", 1.0/avg_dt);

            font_update_text(&font);
            //glFinish();
            font_draw(&font, offset, bgColor, fgColor, 2.0);
            glFinish();
        }
        // glFinish();

        glfwSwapBuffers(window);

        char str[128];
        sprintf(str, "fps = %f\n", 1.0/avg_dt);
        glfwSetWindowTitle(window, str);
    }

    glfwTerminate();

    font_clean(&font);

    return 0;
}

/*
    Prints a string in ASCII bitmap form to stdout 

    str:     C-string containing the string to be rendered
    offsets: the offsets to the start of each glyph along the x-axis in the texture
    widths:  the width of each glyph
    length:  the horisontal size of the texture
    height:  the vertical size of the texture
    data:    the individual pixels of the texture

    easy_font_raw.png contains 96 glyphs with variable width and contains 453x11 24 bit pixels
*/
void font_print_string(char *str, Font *font)
{
    // prints the first line of each glyph sequentially, then the second line, etc.
    for (int j = 0; j < font->height; j++) {
        printf("%d: ", j);
        char *code = str;
        while(*code) {
            int code_base = (*code) - 32;
            int offset = font->glyph_offsets[code_base];
            int width = font->glyph_widths[code_base];
            
            for (int i = 0; i < width; i++) {
                int k = (font->height - j-1)*font->width_padded + (offset+i);
                                
                if (font->font_bitmap[k+0] == 0) {
                    putchar('O');
                } else {
                    putchar(' ');
                }
                //putchar(' ');
            }
            code++;
        }
        putchar('\n');
    }
}

/*
    Reads easy_font_raw.png and extracts the font info
    i.e. offset and width of each glyph, by parsing the first row 
    containing black dots
*/
void font_read(char *filename, Font *font)
{
    int x, y, n;
    unsigned char *data = stbi_load("easy_font_raw.png", &x, &y, &n, 0);

    // scan the first row of pixels once to find the number of glyphs by counting the black dots
    font->num_glyphs = 0;
    for (int i = 0; i < x; i++) {
        if (data[3*i+0] == 0 && data[3*i+1] == 0 && data[3*i+2] == 0) {
            font->num_glyphs++;
        }
    }

    font->glyph_widths  = malloc(sizeof(int)*font->num_glyphs);
    font->glyph_offsets = malloc(sizeof(int)*font->num_glyphs);

    // scan once more, but this time also count the spacing between the black dots to get the width of each glyph
    // and the offset to reach that glyph
    // NOTE: Ugly, but works. Can this be cleaned up?
    int num = 0;
    int width = 0;
    for (int i = 0; i < x; i++) {
        if (data[3*i+0] == 0 && data[3*i+1] == 0 && data[3*i+2] == 0) {
            if (i != 0) {
                font->glyph_widths[num-1] = width;
                width = 0;
            } 
            font->glyph_offsets[num] = i;

            num++;
        }
        width++;
    }
    font->glyph_widths[num-1] = width;


    // convert the RGB texture into a 1-byte texture, strip the first line (containing width info)
    // add padding, so that texture width is a multiple of 4 (for opengl packing compliance)
    // y-axis is flipped (since input image is in image space, i.e. +Y is downwards)
    font->height = y-1;
    font->width = x;
    font->width_padded = (font->width + 3) & ~0x03;

    font->font_bitmap = malloc(font->width_padded*font->height);

    for (int j = 0; j < font->height; j++) {
        for (int i = 0; i < font->width; i++) {
            int k1 = (j+1)*font->width + i;
            int k2 = (font->height - j - 1)*font->width_padded + i; // flip y-axis of texture

            int R = data[3*k1+0];
            int G = data[3*k1+1];
            int B = data[3*k1+2];
            
            // red = vertical segment, blue = horisontal segment
            if ((R == 255 || B == 255) && G == 0) {
                font->font_bitmap[k2] = 0;
            } else {
                font->font_bitmap[k2] = 255;
            }
        }
    }

    free(data); 

    font_setup_texture(font);
}

void font_clean(Font *font)
{
    if (font == NULL)
        return;

    if (font->font_bitmap)
        free(font->font_bitmap);

    if (font->glyph_widths)
        free(font->glyph_widths);

    if (font->glyph_offsets)
        free(font->glyph_offsets);
}


/*
    Initialize opengl stuff in the font struct, based on the data read from the file

    Creates one grayscale 2D texture containing the font bitmap data (as in the .png file)
    and one RGBA32F 1D texture containing font metadata, i.e. for some ascii code, where
    does it have to go to find that glyph in the 2D texture, and how much should it get
*/
void font_setup_texture(Font *font)
{
    //-------------------------------------------------------------------------
    glGenVertexArrays(1, &font->vao);
    glBindVertexArray(font->vao);
    
    //-------------------------------------------------------------------------
    // glyph vertex positions, just uv coordinates that will be stretched accordingly 
    // by the glyphs width and height
    float v[] = {0.0, 0.0, 
                 1.0, 0.0, 
                 0.0, 1.0,
                 0.0, 1.0,
                 1.0, 0.0,
                 1.0, 1.0};
    
    glCreateBuffers(1, &font->vbo_glyph_pos_instance);
    glNamedBufferData(font->vbo_glyph_pos_instance, sizeof(v), v, GL_STATIC_DRAW);
    
    glEnableVertexArrayAttrib(font->vao, 0);
    glVertexArrayVertexBuffer(font->vao, 0, font->vbo_glyph_pos_instance, 0, 2*sizeof(float));
    glVertexArrayAttribFormat(font->vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayBindingDivisor(font->vao, 0, 0);

    //-------------------------------------------------------------------------
    // create 2D texture and upload font bitmap data
    glCreateTextures(GL_TEXTURE_2D, 1, &font->texture_fontdata);

    glTextureParameteri(font->texture_fontdata, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(font->texture_fontdata, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(font->texture_fontdata, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(font->texture_fontdata, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTextureStorage2D(font->texture_fontdata, 1, GL_R8, font->width_padded, font->height);
    glTextureSubImage2D(font->texture_fontdata, 0, 0, 0, font->width_padded, font->height, GL_RED, GL_UNSIGNED_BYTE, font->font_bitmap);

    //-------------------------------------------------------------------------
    // create 1D texture and upload font metadata
    // used for lookup in the bitmap texture
    float *texture_metadata = malloc(sizeof(float)*4*font->num_glyphs);
    
    for (int i = 0; i < font->num_glyphs; i++) {
        // all the glyphs are in a single line, but in principle we can support multiple lines
        texture_metadata[4*i+0] = font->glyph_offsets[i]/(double)font->width_padded;
        texture_metadata[4*i+1] = 0.0;
        texture_metadata[4*i+2] = font->glyph_widths[i]/(double)font->width_padded;
        texture_metadata[4*i+3] = 1.0;
    }
    
    glCreateTextures(GL_TEXTURE_1D, 1, &font->texture_metadata);

    glTextureParameteri(font->texture_metadata, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(font->texture_metadata, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(font->texture_metadata, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    glTextureStorage1D(font->texture_metadata, 1, GL_RGBA32F, font->width_padded);
    glTextureSubImage1D(font->texture_metadata, 0, 0, font->width_padded, GL_RGBA, GL_FLOAT, texture_metadata);

    free(texture_metadata);
}


float *text_glyph_data;

/*
    Creates the vbo used for updating and drawing text. 
    Initially has a max length of MAX_STRING_LEN, but you're not obliged to use all of it
*/
void font_setup_text(Font *font)
{
    glCreateBuffers(1, &font->vbo_code_instances);
    glNamedBufferStorage(font->vbo_code_instances, 4*4*MAX_STRING_LEN, NULL, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT);
    text_glyph_data = glMapNamedBufferRange(font->vbo_code_instances, 0, 4*4*MAX_STRING_LEN, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

    //glNamedBufferData(font->vbo_code_instances, 4*4*MAX_STRING_LEN, NULL, GL_DYNAMIC_DRAW);
    
    glEnableVertexArrayAttrib(font->vao, 1);
    glVertexArrayVertexBuffer(font->vao, 1, font->vbo_code_instances, 0, 4*sizeof(float));
    glVertexArrayAttribFormat(font->vao, 1, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayBindingDivisor(font->vao, 1, 1);

    glUseProgram(program_text);
    glUniform2f(glGetUniformLocation(program_text, "resolution"), resx, resy);
}



typedef enum Token_Type {TOKEN_OTHER=0, TOKEN_OPERATOR, TOKEN_NUMERIC, TOKEN_FUNCTION, TOKEN_KEYWORD, TOKEN_COMMENT, TOKEN_TYPE, TOKEN_UNSET} Token_Type;

const char *TOKEN_NAMES[] = {"other", "operator", "numeric", "function", "keyword", "comment", "type", "unset"};

const char *TYPES[] = {"void", "int", "float", "vec2", "vec3", "vec4", "sampler1D", "sampler2D"};
const char *KEYWORDS[] = {"#version", "#define", "in", "out", "uniform", "layout", "return", "if", "else", "for", "while"};

typedef struct Token 
{
    char *start;
    char *stop;
    Token_Type type;
} Token;

/*
    Parses a string (in font_string.str) and creates position and index data sent to 
    the vertex shader (location 1)

    Supports newlines
*/
void font_update_text(Font *font)
{
    if (font->ctr > MAX_STRING_LEN) {
        printf("Error: string too long. Returning\n");
        return;
    } 
    

    float str_colors[MAX_STRING_LEN] = {0};

    // ignored characters
    char delims[] = " ,(){};\t\n";
    int num_delims = strlen(delims);

    char operators[] = "/+-*<>=&|";
    int num_operators = strlen(operators);

    Token tokens[9999]; // hurr
    int num_tokens = 0; // running counter


    char *ptr = font->str;
    while (*ptr) {
        // skip delimiters
        int is_delim = 0;
        for (int i = 0; i < num_delims; i++) {
            if (*ptr == delims[i]) {
                is_delim = 1;
                break;
            }
        }

        if (is_delim == 1) {
            ptr++;
            continue;
        }


        // found a token!
        char *start = ptr;

        if (*ptr == '/' && *(ptr+1) == '/') {
            // found a line comment, go to end of line or end of file
            while (*ptr != '\n' && *ptr != '\0') {
                ptr++;
            }

            tokens[num_tokens].start = start;
            tokens[num_tokens].stop = ptr;
            tokens[num_tokens].type = TOKEN_COMMENT;
            num_tokens++;

            ptr++;
            continue;
        }

        if (*ptr == '/' && *(ptr+1) == '*') {
            // found a block comment, go to end of line or end of file
            while (!(*ptr == '*' && *(ptr+1) == '/') && *ptr != '\0') {
                ptr++;
            }
            ptr++;

            tokens[num_tokens].start = start;
            tokens[num_tokens].stop = ptr+1;
            tokens[num_tokens].type = TOKEN_COMMENT;
            num_tokens++;

            ptr++;
            continue;
        } 

        // check if it's an operator
        int is_operator = 0;
        for (int i = 0; i < num_operators; i++) {
            if (*ptr == operators[i]) {
                is_operator = 1;
                break;
            }
        }

        if (is_operator == 1) {
            tokens[num_tokens].start = start;
            tokens[num_tokens].stop = ptr+1;
            tokens[num_tokens].type = TOKEN_OPERATOR;
            num_tokens++;
            ptr++;
            continue;
        } 

        // it's either a name, type, a keyword, a function, or an names separated by an operator without spaces
        while (*ptr) {
            // check whether it's an operator stuck between two names
            int is_operator2 = 0;
            for (int i = 0; i < num_operators; i++) {
                if (*ptr == operators[i]) {
                    is_operator2 = 1;
                    break;
                }
            }

            if (is_operator2 == 1) {
                tokens[num_tokens].start = start;
                tokens[num_tokens].stop = ptr;
                tokens[num_tokens].type = TOKEN_UNSET;
                num_tokens++;
                break;
            }

            // otherwise go until we find the next delimiter
            int is_delim2 = 0;
            for (int i = 0; i < num_delims; i++) {
                if (*ptr == delims[i]) {
                    is_delim2 = 1;
                    break;
                }
            }

            if (is_delim2 == 1) {
                tokens[num_tokens].start = start;
                tokens[num_tokens].stop = ptr;
                tokens[num_tokens].type = TOKEN_UNSET;
                num_tokens++;
                ptr++;
                break;
            } 

            // did not find delimiter, check next char
            ptr++; 
        }
    }

    // determine the types of the unset tokens, i.e. either
    // a name, a type, a keyword, or a function
    int num_keywords = sizeof(KEYWORDS)/sizeof(char*);
    int num_types = sizeof(TYPES)/sizeof(char*);
    // printf("num_keywords = %d\n", num_keywords);
    // printf("num_types = %d\n", num_types);

    for (int i = 0; i < num_tokens; i++) {
        // TOKEN_OPERATOR and TOKEN_COMMENT should already be set, so skip those
        if (tokens[i].type != TOKEN_UNSET) {
            continue;
        }

        char end_char = *tokens[i].stop;

        // temporarily null terminate at end of token, restored after parsing
        *tokens[i].stop = '\0';

        // parse
        
        // Check if it's a function
        float f;
        if (end_char == '(') {
            tokens[i].type = TOKEN_FUNCTION;
            *tokens[i].stop = end_char;
            continue;
        } 

        // or if it's a numeric value. catches both integers and floats
        if (sscanf(tokens[i].start, "%f", &f) == 1) {
            tokens[i].type = TOKEN_NUMERIC;
            *tokens[i].stop = end_char;
            continue;
        } 

        // if it's a keyword
        int is_keyword = 0;
        for (int j = 0; j < num_keywords; j++) {
            if (strcmp(tokens[i].start, KEYWORDS[j]) == 0) {
                is_keyword = 1;
                break;
            }
        }
        if (is_keyword == 1) {
            tokens[i].type = TOKEN_KEYWORD;
            *tokens[i].stop = end_char;
            continue;
        } 

        // if it's a variable type
        int is_type = 0;
        for (int j = 0; j < num_types; j++) {
            if (strcmp(tokens[i].start, TYPES[j]) == 0) {
                is_type = 1;
                break;
            }
        }
        if (is_type == 1) {
            tokens[i].type = TOKEN_TYPE;
            *tokens[i].stop = end_char;
            continue;
        } 

        // otherwise it's a regular variable name 
        tokens[i].type = TOKEN_OTHER;
        *tokens[i].stop = end_char;
    }
    
    // print all tokens and their types
    for (int i = 0; i < num_tokens; i++) {
        /*
        printf("token[%3d]: type: %-9s string: \"", i, TOKEN_NAMES[tokens[i].type]);
        for (char *p = tokens[i].start; p != tokens[i].stop; p++)
            printf("%c", *p);
        printf("\"\n");
        */
        for (char *p = tokens[i].start; p != tokens[i].stop; p++) {
            str_colors[(p - font->str)] = tokens[i].type;
            
        }
    }



    float X = 0.0;
    float Y = 0.0;

    int ctr = 0;
    float height = font->height;
    float width_padded = font->width_padded;

    int *glyph_offsets = font->glyph_offsets;
    int *glyph_widths = font->glyph_widths;

    int len = strlen(font->str);
    for (int i = 0; i < len; i++) {

        if (font->str[i] == '\n') {
            X = 0.0;
            Y -= height;
            continue;
        }

        // printf("%c\n", font->str[i]);

        int code_base = font->str[i]-32; // first glyph is ' ', i.e. ascii code 32
        float offset = glyph_offsets[code_base];
        float width = glyph_widths[code_base];

        float x1 = X;
        float y1 = Y;

        int ctr1 = 4*ctr;
        text_glyph_data[ctr1++] = x1;
        text_glyph_data[ctr1++] = y1;
        text_glyph_data[ctr1++] = code_base;
        // text_glyph_data[ctr1++] = i % 6;
        //text_glyph_data[ctr1++] = 0.0;
        text_glyph_data[ctr1++] = str_colors[i];

        X += width;
        ctr++;
    }

    // actual uploading
    // glNamedBufferSubData(font->vbo_code_instances, 0, 4*4*ctr, text_glyph_data);
    // printf("%d: %s\n", ctr, font->str);
    font->ctr = ctr;
}

void font_draw(Font *font, vec2 offset, Color bgColor, Color fgColor, float size) 
{
    // printf("%d: %s\n", font->ctr, font->str);
    glUseProgram(program_text);

    glUniform1f(glGetUniformLocation(program_text, "time"), glfwGetTime());
    glUniform3fv(glGetUniformLocation(program_text, "bgColor"), 1, &bgColor.R);
    glUniform3fv(glGetUniformLocation(program_text, "fgColor"), 1, &fgColor.R);
    glUniform2fv(glGetUniformLocation(program_text, "string_offset"), 1, &offset.x);
    glUniform2f(glGetUniformLocation(program_text, "string_size"), size, size);

    glBindVertexArray(font->vao);

    glBindTextureUnit(0, font->texture_fontdata);
    glBindTextureUnit(1, font->texture_metadata);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, font->ctr);
}


/*****************************************************************************/
// OpenGL and GLFW boilerplate below

void init_GL()
{

    // openGL stuff
    printf("Initializing OpenGL/GLFW\n"); 
    if (!glfwInit()) {
        printf("Could not initialize\n");
        exit(-1);
    }
    glfwWindowHint(GLFW_SAMPLES, 4);    // samples, for antialiasing
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); // shader version should match these
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // do not use deprecated functionality

    window = glfwCreateWindow(resx, resy, "GLSL template", 0, 0);
    if (!window) {
        printf("Could not open glfw window\n");
        glfwTerminate();
        exit(-2);
    }
    glfwMakeContextCurrent(window); 


    glewExperimental = 1; // Needed for core profile
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW\n");
        exit(-3);
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mousebutton_callback);
    glfwSetScrollCallback(window, mousewheel_callback);
    glfwSetCursorPosCallback(window, mousepos_callback);
    glfwSetWindowSizeCallback(window, windowsize_callback);

    glfwSwapInterval(0);
    glClearColor(40/255.0, 42/255.0, 54/225.0, 1.0f);
}


// Callback function called every time the window size change
// Adjusts the viewport
// Resets shader uniform containing resolution
void windowsize_callback(GLFWwindow* win, int width, int height) {
    (void)win;

    resx = width;
    resy = height;

    glViewport(0, 0, resx, resy);

    glUseProgram(program_text);
    glUniform2f(glGetUniformLocation(program_text, "resolution"), resx, resy);
}

// Callback function called every time a keyboard key is pressed, released or held down
void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    printf("key = %d, scancode = %d, action = %d, mods = %d\n", key, scancode, action, mods); fflush(stdout);

    // Close window if escape is released
    if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) {
        glfwSetWindowShouldClose(win, GL_TRUE);
    }
}

// Callback function called every time a mouse button pressed or released
void mousebutton_callback(GLFWwindow* win, int button, int action, int mods) {
    // get current cursor position, convert to world coordinates
    glfwGetCursorPos(win, &prevx, &prevy);
    double xend = prevx;
    double yend = prevy;

    printf("button = %d, action = %d, mods = %d at (%f %f)\n", button, action, mods, xend, yend); fflush(stdout);

    // To track the state of buttons outside this function
    if (action == 1)
        clickedButtons |= (1 << button);
    else
        clickedButtons &= ~(1 << button);


    // Test each button
    if (clickedButtons&FIRST_BUTTON) {
        
    } else if (clickedButtons&SECOND_BUTTON) {

    } else if (clickedButtons&THIRD_BUTTON) {

    } else if (clickedButtons&FOURTH_BUTTON) {

    } else if (clickedButtons&FIFTH_BUTTON) {

    }
}

// Callback function called every time a the mouse is moved
void mousepos_callback(GLFWwindow* win, double xpos, double ypos) {
    (void)win;

    if (clickedButtons&FIRST_BUTTON) {
        prevx = xpos;
        prevy = ypos;
    } else if (clickedButtons&SECOND_BUTTON) {

    } else if (clickedButtons&THIRD_BUTTON) {

    } else if (clickedButtons&FOURTH_BUTTON) {

    } else if (clickedButtons&FIFTH_BUTTON) {

    }
}

void mousewheel_callback(GLFWwindow* win, double xoffset, double yoffset) {
    (void)xoffset;

    double zoomFactor = pow(0.95,yoffset);

    glfwGetCursorPos(win, &prevx, &prevy);
}


// Shader loading routines
char *readFile(const char *filename) {
    // Read content of "filename" and return it as a c-string.
    printf("Reading %s\n", filename);
    FILE *f = fopen(filename, "rb");

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("Filesize = %d\n", (int)fsize);

    char *string = (char*)malloc(fsize + 1);
    int ret = fread(string, fsize, 1, f);
    string[fsize] = '\0';
    fclose(f);

    (void)ret;

    return string;
}

GLuint LoadShaders(const char * vertex_file_path,const char * fragment_file_path){
    GLint Result = GL_FALSE;
    int InfoLogLength;

    // Create the Vertex shader
    GLuint VertexShaderID;
    VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
    char *VertexShaderCode   = readFile(vertex_file_path);

    // Compile Vertex Shader
    printf("Compiling shader : %s\n", vertex_file_path); fflush(stdout);
    glShaderSource(VertexShaderID, 1, (const char**)&VertexShaderCode , NULL);
    glCompileShader(VertexShaderID);

    // Check Vertex Shader
    glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
    glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

    if ( InfoLogLength > 0 ){
        char VertexShaderErrorMessage[9999];
        glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, VertexShaderErrorMessage);
        printf("%s\n", VertexShaderErrorMessage); fflush(stdout);
    }


    // Create the Fragment shader
    GLuint FragmentShaderID;
    FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
    char *FragmentShaderCode = readFile(fragment_file_path);

    // Compile Fragment Shader
    printf("Compiling shader : %s\n", fragment_file_path); fflush(stdout);
    glShaderSource(FragmentShaderID, 1, (const char**)&FragmentShaderCode , NULL);
    glCompileShader(FragmentShaderID);

    // Check Fragment Shader
    glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
    glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
    if ( InfoLogLength > 0 ){
        char FragmentShaderErrorMessage[9999];
        glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, FragmentShaderErrorMessage);
        printf("%s\n", FragmentShaderErrorMessage); fflush(stdout);
    }


    // Create and Link the program
    printf("Linking program\n"); fflush(stdout);
    GLuint ProgramID;
    ProgramID= glCreateProgram();
    glAttachShader(ProgramID, VertexShaderID);
    glAttachShader(ProgramID, FragmentShaderID);
    glLinkProgram(ProgramID);

    // Check the program
    glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
    glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);

    if ( InfoLogLength > 0 ){
        GLchar ProgramErrorMessage[9999];
        glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
        printf("%s\n", &ProgramErrorMessage[0]); fflush(stdout);
    }

    glDeleteShader(VertexShaderID);
    glDeleteShader(FragmentShaderID);
    free(FragmentShaderCode);
    free(VertexShaderCode);

    return ProgramID;
}
