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
#define SENSITIVITY 1
#define PROJECT_NAME "Shading"


// Global app state
static cy::GLSLProgram program;
static double prev_mouse_x;
static double prev_mouse_y;
static float camera_rotation_x;
static float camera_rotation_z;
static float camera_distance;
static bool perspective = true;
static float light_rotation_x;
static float light_rotation_z;
static float aspect_ratio;


// Shaders
static const char* vert_src = R"VS(
in vec3 position;
in vec3 normal;
uniform mat4 position_transform;
uniform mat3 normal_transform;
out vec3 fragment_position;
out vec3 fragment_normal;

void main() {
    // Apply transform
    vec4 transformed_position = position_transform * vec4(position, 1);
    
    // Render fragments
    gl_Position = transformed_position;
    
    // Send data to fragments
    fragment_position = vec3(transformed_position);
    fragment_normal = normal_transform * normal;
}
)VS";

static const char* frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
uniform mat3 light_transform;
out vec4 color;

void main() {
    // Find context vectors
    vec3 light_direction = normalize(light_transform * vec3(1.0, 0.0, 0.0));
    vec3 view_direction = normalize(-fragment_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    vec3 normal = normalize(fragment_normal);
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = vec4(1.0, 0.2, 0.3, 1.0);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Specular component
    float blinn_term = max(0.0, dot(normal, half_vector));
    vec4 specular_color = vec4(1.0);
    vec4 specular_component = specular_color * pow(blinn_term, 30.0);
    
    // Composite final color
    color = diffuse_component + specular_component;
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
    // Calculate change in mouse position
    float delta_mouse_x = x - prev_mouse_x,
        delta_mouse_y = y - prev_mouse_y;
    
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
        if(glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
            // Change light rotation in proportion to the cursors' change in location, when CTRL is held
            light_rotation_z += delta_mouse_x * (SENSITIVITY / 100.0);
            light_rotation_x += delta_mouse_y * (SENSITIVITY / 100.0);
            
            // Cap the rotation on the X axis (verticality of the light)
            if (light_rotation_x > 0) {
                light_rotation_x = 0;
            } else if (light_rotation_x < -M_PI) {
                light_rotation_x = -M_PI;
            }
        } else {
            // Change camera rotation in proportion to the the cursors' change in location
            camera_rotation_z += delta_mouse_x * (SENSITIVITY / 100.0);
            camera_rotation_x += delta_mouse_y * (SENSITIVITY / 100.0);
            
            // Cap the rotation on the X axis (verticality of the camera)
            if (camera_rotation_x > 0) {
                camera_rotation_x = 0;
            } else if (camera_rotation_x < -M_PI) {
                camera_rotation_x = -M_PI;
            }
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
        // Change camera distance in proportion to the cursors' change in vertical location
        camera_distance += delta_mouse_y * (SENSITIVITY / 10.0);
        
        // Cap the smallest camera distance
        if (camera_distance < 0.05) {
            camera_distance = 0.05;
        }
    }
    
    // Keep track of cursor location for future callbacks
    prev_mouse_x = x;
    prev_mouse_y = y;
}

static void resize_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    aspect_ratio = width / (float) height;
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
    
    // Mesh normals averaged, so that one position maps to one normal
    mesh.ComputeNormals();
    
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
    
    // Input Callback registration
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
    
    // Resize callback, window size init
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    aspect_ratio = width / (float) height;
    glfwSetFramebufferSizeCallback(window, resize_callback);
    
    // Init OpenGL
    glfwMakeContextCurrent(window);
    glewInit();
    glfwSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    
    // New VBO to store vertex positions and normals
    GLuint vbo[2];
    glGenBuffers(2, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NV(), &mesh.V(0), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NVN(), &mesh.VN(0), GL_STATIC_DRAW);
    
    // Link VBO with new VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
 
    // Build shader program
    compile_shaders();
    program.SetAttribBuffer("position", vbo[0], 3);
    program.SetAttribBuffer("normal", vbo[1], 3);
    
    // Grab indices from TriMesh class
    GLuint indices[3 * mesh.NF()];
    for (int i = 0; i < mesh.NF(); i++) {
        cy::TriMesh::TriFace face = mesh.F(i);
        indices[3 * i] = face.v[0];
        indices[3 * i + 1] = face.v[1];
        indices[3 * i + 2] = face.v[2];
    }
    
    // Transfer indices to buffer object
    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), &indices[0], GL_STATIC_DRAW);
    
    // Center mesh and place camera at appropriate position
    mesh.ComputeBoundingBox();
    cy::Vec3f min = mesh.GetBoundMin(),
        max = mesh.GetBoundMax(),
        mesh_center = (min + max) / 2;
    float bounds_diagonal = min.Length() + max.Length();
    camera_distance = bounds_diagonal / 2.0;
    
    // Render loop
    while (!glfwWindowShouldClose(window)) {
        // Clear frame
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Define matrices
        cy::Matrix4f position_transform, projection;
        cy::Matrix3f normal_transform, light_transform;
        
        // Rotation and centering translation matrix
        position_transform =
            cy::Matrix4f::RotationX(camera_rotation_x) *
            cy::Matrix4f::RotationZ(camera_rotation_z) *
            cy::Matrix4f::Translation(-mesh_center);
        
        // If perspective, translate the camera and perform perspective transformation
        if (perspective) {
            projection = cy::Matrix4f::Perspective((2.0/5.0) * M_PI, aspect_ratio, 0.5, 100.0) *
                cy::Matrix4f::Translation(cy::Vec3f {0.0, 0.0, -camera_distance});
                
        // If orthogonal, simply scale by the reciprocal of the camera distance, correcting for aspect ratio
        } else {
            projection = cy::Matrix4f::Scale(1.0/aspect_ratio, 1.0, 1.0, 1.0) *
                cy::Matrix4f(1.0 / camera_distance);
        }
        
        // Apply projection, and use the inverse + transpose trick to get the normal transformation
        position_transform = projection * position_transform;
        normal_transform = position_transform.GetSubMatrix3().GetInverse().GetTranspose();
        
        // Rotate for light transform (direction)
        light_transform =
            cy::Matrix3f::RotationX(light_rotation_x) *
            cy::Matrix3f::RotationZ(light_rotation_z);
        
        // Linearize uniform values
        float linear_position_transform[16], linear_normal_transform[9], linear_light_transform[9];
        position_transform.Get(linear_position_transform);
        normal_transform.Get(linear_normal_transform);
        light_transform.Get(linear_light_transform);
        
        // Send uniform values
        program.Bind();
        program.SetUniformMatrix4("position_transform", linear_position_transform);
        program.SetUniformMatrix3("normal_transform", linear_normal_transform);
        program.SetUniformMatrix3("light_transform", linear_light_transform);
        
        // Draw
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);
 
        // Wait for next GLFW event to draw a new frame (no idle animation)
        glfwSwapBuffers(window);
        glfwWaitEvents();
    }
    
    glfwTerminate();
    exit(EXIT_SUCCESS);
}