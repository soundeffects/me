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
#include <lodepng.h>


// Definitions
#define OPENGL_MAJOR 3
#define OPENGL_MINOR 3
#define SENSITIVITY 1
#define PROJECT_NAME "Render Buffers"


// Global app state
static cy::GLSLProgram object_program, plane_program;
static double prev_mouse_x, prev_mouse_y;
static float object_camera_rotation_x,
    object_camera_rotation_z,
    object_camera_distance,
    plane_camera_rotation_x,
    plane_camera_rotation_z,
    plane_camera_distance,
    light_rotation_x,
    light_rotation_z,
    aspect_ratio;


// Shaders
static const char* vert_src = R"VS(
in vec3 position;
in vec3 normal;
in vec2 texture_coordinate;
uniform mat4 position_transform;
uniform mat3 normal_transform;
out vec3 fragment_position;
out vec3 fragment_normal;
out vec2 fragment_texture_coordinate;

void main() {
    // Apply transform
    vec4 transformed_position = position_transform * vec4(position, 1);
    
    // Render fragments
    gl_Position = transformed_position;
    
    // Send data to fragments
    fragment_position = vec3(transformed_position);
    fragment_normal = normal_transform * normal;
    fragment_texture_coordinate = texture_coordinate;
}
)VS";

static const char* frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
in vec2 fragment_texture_coordinate;
uniform mat3 light_transform;
uniform sampler2D diffuse_texture;
uniform sampler2D specular_texture;
out vec4 color;

void main() {
    // Find context vectors
    vec3 light_direction = normalize(light_transform * vec3(1.0, 0.0, 0.0));
    vec3 view_direction = normalize(-fragment_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    vec3 normal = normalize(fragment_normal);
    
    // Ambient component
    // Note that I have set the ambient color to black, because of the black background.
    // This means that the ambient color doesn't actually do anything.
    vec4 ambient_color = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 ambient_component = ambient_color * 0.1;
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = texture(diffuse_texture, fragment_texture_coordinate);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Specular component
    float blinn_term = max(0.0, dot(normal, half_vector));
    vec4 specular_color = texture(specular_texture, fragment_texture_coordinate);
    vec4 specular_component = specular_color * pow(blinn_term, 30.0);
    
    // Composite final color
    color = ambient_component + diffuse_component + specular_component;
}
)FS";

static const char* plane_vert_src = R"VS(
in vec3 position;
uniform mat4 position_transform;
out vec2 fragment_texture_coordinate;

void main() {
    // Apply transform
    vec4 transformed_position = position_transform * vec4(position, 1);
    
    // Render fragments
    gl_Position = transformed_position;
    
    // Send data to fragments
    fragment_texture_coordinate = vec2(position);
}
)VS";

static const char* plane_frag_src = R"FS(
in vec2 fragment_texture_coordinate;
uniform sampler2D render_texture;
out vec4 color;

void main() {
    color = texture(render_texture, fragment_texture_coordinate) + vec4(0.1, 0.1, 0.1, 1.0);
}
)FS";

static void compile_shaders() {
    // Prepend the GLSL version string
    char version[20];
    snprintf(version, 20, "#version %d%d0 core\n", OPENGL_MAJOR, OPENGL_MINOR);
    
    // Build
    object_program.BuildSources(vert_src, frag_src, NULL, NULL, NULL, version);
    plane_program.BuildSources(plane_vert_src, plane_frag_src, NULL, NULL, NULL, version);
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
        }
    }
}

static void move_rotation(float* x_rotation, float* z_rotation, float dx, float dy) {
    *x_rotation += dy * (SENSITIVITY / 100.0);
    *z_rotation += dx * (SENSITIVITY / 100.0);
    
    if (*x_rotation > 0) {
        *x_rotation = 0;
    } else if (*x_rotation < -M_PI) {
        *x_rotation = -M_PI;
    }
}

static void move_distance(float* distance, float dy) {
    *distance += dy * (SENSITIVITY / 10.0);
    
    if (*distance < 0.5) {
        *distance = 0.5;
    }
}

static void mouse_callback(GLFWwindow* window, double x, double y) {
    // Calculate change in mouse position
    float dx = x - prev_mouse_x,
        dy = y - prev_mouse_y;
        
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
        if (glfwGetKey(window, GLFW_KEY_LEFT_ALT)) {
            move_rotation(&plane_camera_rotation_x, &plane_camera_rotation_z, dx, dy);
        } else if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
            move_rotation(&light_rotation_x, &light_rotation_z, dx, dy);
        } else {
            move_rotation(&object_camera_rotation_x, &object_camera_rotation_z, dx, dy);
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
        if (glfwGetKey(window, GLFW_KEY_LEFT_ALT)) {
            move_distance(&plane_camera_distance, dy);
        } else {
            move_distance(&object_camera_distance, dy);
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
    
    // Using the material file associated with the mesh, load diffuse and specular textures
    const char* diffuse_texture_path = mesh.M(0).map_Kd.data,
        * specular_texture_path = mesh.M(0).map_Ks.data;
    std::vector<unsigned char> diffuse_texture_image, specular_texture_image;
    unsigned diffuse_texture_width, diffuse_texture_height, specular_texture_width, specular_texture_height;
    if (lodepng::decode(diffuse_texture_image, diffuse_texture_width, diffuse_texture_height, diffuse_texture_path) ||
        lodepng::decode(specular_texture_image, specular_texture_width, specular_texture_height, specular_texture_path)) {
        fprintf(stderr, "Error while loading material or texture files associated with .obj file. Terminating.\n");
        exit(EXIT_FAILURE);
    }
    
    // Mesh normals averaged, so that one position maps to one normal
    int vertex_count = mesh.NV();
    mesh.ComputeNormals();
    GLuint indices[3 * mesh.NF()];
    cy::Vec2f texture_coordinates[vertex_count];
    for (int i = 0; i < mesh.NF(); i++) {
        cy::TriMesh::TriFace face = mesh.F(i), texture_face = mesh.FT(i);
        
        for (int face_index = 0; face_index < 3; face_index++) {
            indices[3 * i + face_index] = face.v[face_index];
            cy::Vec3f texture_coordinate_3d = mesh.VT(texture_face.v[face_index]);
            cy::Vec2f texture_coordinate_2d = cy::Vec2f { texture_coordinate_3d.x, texture_coordinate_3d.y };
            texture_coordinates[face.v[face_index]] = texture_coordinate_2d;
        }
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
    compile_shaders();
    
    // Generate vao for object view
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // New VBO to store vertex positions, normals, and texture coordinates
    GLuint vbo[3];
    glGenBuffers(3, vbo);
    
    // Position buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glEnableVertexAttribArray(0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NV(), &mesh.V(0), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    
    // Normal buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glEnableVertexAttribArray(1);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NVN(), &mesh.VN(0), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    
    // texture coordinate buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo[2]);
    glEnableVertexAttribArray(2);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texture_coordinates), texture_coordinates, GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    
    // Link buffers in object shader
    object_program.Bind();
    object_program.SetAttribBuffer("position", vbo[0], 3);
    object_program.SetAttribBuffer("normal", vbo[1], 3);
    object_program.SetAttribBuffer("texture_coordinate", vbo[2], 2);
    
    // Separate VAO for plane
    GLuint plane_vao;
    glGenVertexArrays(1, &plane_vao);
    glBindVertexArray(plane_vao);

    // Set up plane vertices in a VBO
    GLuint plane_vbo[1];
    float plane_vertices[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 0.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 1.0, 0.0
    };
    glGenBuffers(1, plane_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, plane_vbo[0]);
    glEnableVertexAttribArray(0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    
    // Link buffers in plane shader
    plane_program.Bind();
    plane_program.SetAttribBuffer("position", plane_vbo[0], 3);
    
    // Transfer indices to buffer object
    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), &indices[0], GL_STATIC_DRAW);
    
    // Set up diffuse texture sampler
    cy::GLTexture2D diffuse_texture, specular_texture;
    diffuse_texture.Bind(0);
    diffuse_texture.Initialize();
    diffuse_texture.SetImage(&diffuse_texture_image[0], 4, diffuse_texture_width, diffuse_texture_height);
    diffuse_texture.BuildMipmaps();
    object_program.SetUniform("diffuse_texture", 0);
    
    // Set up specular texture sampler
    specular_texture.Bind(1);
    specular_texture.Initialize();
    specular_texture.SetImage(&specular_texture_image[0], 4, specular_texture_width, specular_texture_height);
    specular_texture.BuildMipmaps();
    object_program.SetUniform("specular_texture", 1);
    
    // Center mesh and place camera at appropriate position
    mesh.ComputeBoundingBox();
    cy::Vec3f min = mesh.GetBoundMin(),
        max = mesh.GetBoundMax(),
        mesh_center = (min + max) / 2;
    float bounds_diagonal = min.Length() + max.Length();
    object_camera_distance = bounds_diagonal / 2.0;
    plane_camera_distance = 3.0;
    
    // Setup render buffer
    cy::GLRenderTexture2D render_buffer;
    render_buffer.BindTexture(2);
    plane_program.SetUniform("render_texture", 2);
    
    // Render loop
    while (!glfwWindowShouldClose(window)) {
        // Clear frame
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Transform matrix is a series of transformations into camera space
        cy::Matrix4f object_position_transform =
            cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, 1.0, 0.05, 100.0) *
            cy::Matrix4f::Translation(cy::Vec3f {0.0, 0.0, -object_camera_distance}) *
            cy::Matrix4f::RotationX(object_camera_rotation_x) *
            cy::Matrix4f::RotationZ(object_camera_rotation_z) *
            cy::Matrix4f::Translation(-mesh_center);
        
        // Same for plane, with it's own separate camera
        cy::Matrix4f plane_position_transform =
            cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.05, 100.0) *
            cy::Matrix4f::Translation(cy::Vec3f {0.0, 0.0, -plane_camera_distance}) *
            cy::Matrix4f::RotationX(plane_camera_rotation_x) *
            cy::Matrix4f::RotationZ(plane_camera_rotation_z) *
            cy::Matrix4f::Translation(cy::Vec3f { -0.5, -0.5, 0.0 });
        
        // Use the transpose of inverse trick to get the normal transformation
        cy::Matrix3f normal_transform =
            object_position_transform.GetSubMatrix3().GetInverse().GetTranspose();
        
        // Rotate for light transform (direction)
        cy::Matrix3f light_transform =
            cy::Matrix3f::RotationX(light_rotation_x) *
            cy::Matrix3f::RotationZ(light_rotation_z);
        
        // Linearize uniform values
        float linear_object_position_transform[16],
            linear_plane_position_transform[16],
            linear_normal_transform[9],
            linear_light_transform[9];
        object_position_transform.Get(linear_object_position_transform);
        plane_position_transform.Get(linear_plane_position_transform);
        normal_transform.Get(linear_normal_transform);
        light_transform.Get(linear_light_transform);
        
        // Send uniform values
        object_program.Bind();
        object_program.SetUniformMatrix4("position_transform", linear_object_position_transform);
        object_program.SetUniformMatrix3("normal_transform", linear_normal_transform);
        object_program.SetUniformMatrix3("light_transform", linear_light_transform);
        
        // Refresh render buffer
        render_buffer.Delete();
        render_buffer.Initialize(
            true,
            3,
            1024,
            1024
        );
        
        // Draw Object
        render_buffer.Bind();
        glBindVertexArray(vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);
        render_buffer.Unbind();
        render_buffer.BuildTextureMipmaps();
        render_buffer.SetTextureFilteringMode(GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR);
        render_buffer.SetTextureAnisotropy(2.0);
        
        // Draw Plane
        plane_program.Bind();
        plane_program.SetUniformMatrix4("position_transform", linear_plane_position_transform);
        glBindVertexArray(plane_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Wait for next GLFW event to draw a new frame (no idle animation)
        glfwSwapBuffers(window);
        glfwWaitEvents();
    }
    
    glfwTerminate();
    exit(EXIT_SUCCESS);
}