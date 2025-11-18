// Silence deprecation warnings on Apple's distribution of OpenGL
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif

// immintrin.h does not compile on ARM machines
#ifdef __aarch64__
#define CY_NO_IMMINTRIN_H
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <cmath>
#include <cyVector.h>
#include <cyTriMesh.h>
#include <cyGL.h>
#include <cyMatrix.h>


// Definitions
#define OPENGL_MAJOR 3
#define OPENGL_MINOR 3
#define SENSITIVITY 10
#define PROJECT_NAME "Transformations"


// Global app state
static cy::GLSLProgram program;
static double prev_x;
static double prev_y;
static float camera_rotation_z;
static float camera_rotation_x;
static float camera_distance;
static bool perspective = true;


// Shaders
static const char* vert_src = R"VS(
in vec3 pos;
uniform mat4 transform;
void main() {
    gl_Position = transform * vec4(pos, 1);
}
)VS";

static const char* frag_src = R"FS(
out vec4 color;
void main() {
    color = vec4(1.0, 1.0, 1.0, 1.0);
}
)FS";

static void compile_shaders() {
    // Prepend the GLSL version string
    char version[20];
    snprintf(version, 20, "#version %d%d0 core\n", OPENGL_MAJOR, OPENGL_MINOR);
    
    // Build
    program.BuildSources(vert_src, frag_src, NULL, NULL, NULL, version);
}


// GLFW Callbacks
static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error %d: %s\n", error, description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
            case GLFW_KEY_F6:
                printf("Recompiling shaders...\n");
                compile_shaders();
                break;
            case GLFW_KEY_P:
                perspective = !perspective;
                break;
        }
    }
}

static void mouse_callback(GLFWwindow* window, double x, double y) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
        // Change camera rotation in proportion to the the cursors' change in location
        camera_rotation_z += (x - prev_x) * (SENSITIVITY / 100.0);
        camera_rotation_x += (y - prev_y) * (SENSITIVITY / 100.0);
        
        // Cap the rotation on the X axis (verticality of the camera)
        if (camera_rotation_x > 0) {
            camera_rotation_x = 0;
        } else if (camera_rotation_x < -M_PI) {
            camera_rotation_x = -M_PI;
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
        // Change camera distance in proportion to the cursors' change in vertical location
        camera_distance += (y - prev_y) * (SENSITIVITY / 100.0);
        
        // Cap the smallest camera distance
        if (camera_distance < 0.05) {
            camera_distance = 0.05;
        }
    }
    
    // Keep track of cursor location for future callbacks
    prev_x = x;
    prev_y = y;
}


// Window creation and render
int main(int argc, char** argv) {
    // Check arg count
    if (argc != 2) {
        fprintf(stderr, "Expected exactly one argument, which should be a path to a .obj file. Terminating.\n");
        exit(EXIT_FAILURE);
    }
    
    // Use arg to check mesh validity
    cy::TriMesh mesh;
    if (!mesh.LoadFromFileObj(argv[1])) {
        fprintf(stderr, "Error while loading .obj file. Terminating.\n");
        exit(EXIT_FAILURE);
    }
    
    // Init GLFW
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "Error while initializing GLFW. Terminating.\n");
        exit(EXIT_FAILURE);
    }

    // MacOS requires core profile for OpenGL
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Init window
    GLFWwindow* window = glfwCreateWindow(1280, 720, PROJECT_NAME, NULL, NULL);
    if(!window) {
        fprintf(stderr, "Error in window or context creation. Terminating.\n");
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    
    // Use crosshair cursor
    GLFWcursor* cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    glfwSetCursor(window, cursor);
    
    // Mouse and keyboard input callbacks
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
 
    // Init OpenGL
    glfwMakeContextCurrent(window);
    glewInit();
    glfwSwapInterval(1);
    
    // New VBO to store vertex positions
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NV(), &mesh.V(0), GL_STATIC_DRAW);
    
    // Link VBO with new VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
 
    // Build shader program
    compile_shaders();
    program.SetAttribBuffer("pos", vbo, 3);
    
    // Center mesh and place camera at appropriate position
    mesh.ComputeBoundingBox();
    cy::Vec3f min = mesh.GetBoundMin(),
        max = mesh.GetBoundMax(),
        mesh_center = (min + max) / 2;
    float max_box_dimension = std::max(
        abs(min[0]) + abs(max[0]),
        std::max(abs(min[1]) + abs(min[1]), abs(min[2]) + abs(max[2]))
    );
    camera_distance = max_box_dimension;
    
    // Render loop
    while (!glfwWindowShouldClose(window)) {
        // Find frame size
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        // Clear frame
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Rotation and centering translation matrix
        cy::Matrix4f centering_translation = cy::Matrix4f::Translation(-mesh_center),
            x_rotation = cy::Matrix4f::RotationX(camera_rotation_x),
            z_rotation = cy::Matrix4f::RotationZ(camera_rotation_z),
            transform = x_rotation * z_rotation * centering_translation;
        
        // If perspective, translate the camera and perform perspective transformation
        if (perspective) {
            transform =
                cy::Matrix4f::Perspective((2.0/5.0) * M_PI, width / (float) height, -30, 0) *
                cy::Matrix4f::Translation(cy::Vec3f {0.0, 0.0, -camera_distance}) *
                transform;
                
        // If orthogonal, simply scale by the reciprocal of the camera distance
        } else {
            transform = cy::Matrix4f(1.0 / camera_distance) * transform;
        }
        
        // Linearize matrix
        float linearized_transform[16];
        transform.Get(linearized_transform);
        
        // Send transform to shader program
        program.Bind();
        program.SetUniformMatrix4("transform", linearized_transform);
        
        // Draw points
        glPointSize(12.0f);
        glDrawArrays(GL_POINTS, 0, mesh.NV());
 
        // Wait for next GLFW event to draw a new frame (no idle animation)
        glfwSwapBuffers(window);
        glfwWaitEvents();
    }
    
    glfwTerminate();
    exit(EXIT_SUCCESS);
}