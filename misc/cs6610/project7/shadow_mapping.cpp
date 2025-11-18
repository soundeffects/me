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
#include <cmath>
#include <cyGL.h>
#include <cyMatrix.h>
#include <cyTriMesh.h>
#include <cyVector.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

// Definitions
#define OPENGL_MAJOR 3
#define OPENGL_MINOR 3
#define SENSITIVITY 1
#define PROJECT_NAME "Shadow Mapping"

// Global app state
static cy::GLSLProgram object_program, plane_program, light_object_program;
static double prev_mouse_x;
static double prev_mouse_y;
static float camera_rotation_x;
static float camera_rotation_y;
static float camera_distance;
static float light_rotation_x;
static float light_rotation_y;
static float light_distance;
static float aspect_ratio;

// Shaders
static const char *object_vert_src = R"VS(
in vec3 position;
in vec3 normal;
uniform mat4 position_transform;
uniform mat3 normal_transform;
uniform mat4 shadow_transform;
uniform mat4 light_transform;
out vec3 fragment_position;
out vec3 fragment_normal;
out vec3 light_position;
out vec4 light_view_position;

void main() {
    // Apply transform
    vec4 transformed_position = position_transform * vec4(position, 1);
    
    // Render fragments
    gl_Position = transformed_position;
    
    // Send data to fragments
    fragment_position = vec3(transformed_position);
    fragment_normal = normal_transform * normal;
    light_position = vec3(position_transform * light_transform * vec4(0, 0, 0, 1)); 
    light_view_position = shadow_transform * vec4(position, 1);
}
)VS";

static const char *object_frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
in vec3 light_position;
in vec4 light_view_position;
uniform mat4 light_transform;
uniform mat3 world_transform;
uniform sampler2DShadow light_depth_texture;
out vec4 color;

void main() {
    // Find context vectors
    vec3 light_direction = normalize(light_position - fragment_position);
    vec3 view_direction = normalize(-fragment_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    vec3 normal = normalize(fragment_normal);
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = vec4(1.0, 0.2, 0.3, 1.0);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Specular component
    float blinn_term = max(0.0, dot(normal, half_vector));
    vec4 specular_color = vec4(1);
    vec4 specular_component = specular_color * pow(blinn_term, 30.0);
    
    // Composite final color
    color = diffuse_component + specular_component;

    // Check shadow depth
    color *= textureProj(light_depth_texture, light_view_position);
}
)FS";

static const char *plane_vert_src = R"VS(
in vec3 position;
uniform mat4 position_transform;
uniform mat3 normal_transform;
uniform mat4 shadow_transform;
uniform mat4 light_transform;
out vec3 fragment_position;
out vec3 fragment_normal;
out vec3 light_position;
out vec4 light_view_position;

void main() {
    // Apply transform
    vec4 transformed_position = position_transform * vec4(position, 1);

    // Render fragments
    gl_Position = transformed_position;
    
    // Send data to fragments
    fragment_position = vec3(transformed_position);
    fragment_normal = normal_transform * vec3(0.0, 1.0, 0.0);
    light_position = vec3(position_transform * light_transform * vec4(0, 0, 0, 1)); 
    light_view_position = shadow_transform * vec4(position, 1);
}
)VS";

static const char *plane_frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
in vec3 light_position;
in vec4 light_view_position;
uniform mat4 position_transform;
uniform mat4 light_transform;
uniform sampler2DShadow light_depth_texture;
out vec4 color;

void main() {
    // Find context vectors
    vec3 light_direction = normalize(light_position - fragment_position);
    vec3 normal = normalize(fragment_normal);
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = vec4(0.5, 0.5, 0.5, 1.0);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Composite final color
    color = diffuse_component;

    // Check shadow depth
    color *= textureProj(light_depth_texture, light_view_position);
}
)FS";

static const char *light_vert_src = R"VS(
in vec3 position;
uniform mat4 position_transform;
uniform mat4 light_transform;

void main() {
    gl_Position = position_transform * light_transform * vec4(position, 1);
}
)VS";

static const char *light_frag_src = R"FS(
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
  object_program.BuildSources(object_vert_src, object_frag_src, NULL, NULL,
                              NULL, version);
  plane_program.BuildSources(plane_vert_src, plane_frag_src, NULL, NULL, NULL,
                             version);
  light_object_program.BuildSources(light_vert_src, light_frag_src, NULL, NULL,
                                    NULL, version);
}

// GLFW Callbacks
static void error_callback(int error, const char *description) {
  fprintf(stderr, "Error %d: %s\n", error, description);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods) {
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

static void mouse_callback(GLFWwindow *window, double x, double y) {
  // Calculate change in mouse position
  float dx = x - prev_mouse_x, dy = y - prev_mouse_y;

  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
      // Change light rotation in proportion to the cursors' change in location,
      // when CTRL is held
      light_rotation_x += dy * (SENSITIVITY / 100.0);
      light_rotation_y += dx * (SENSITIVITY / 100.0);

      // Cap the rotation on the X axis (verticality of the light)
      if (light_rotation_x > M_PI / 2) {
        light_rotation_x = M_PI / 2;
      } else if (light_rotation_x < -M_PI / 2) {
        light_rotation_x = -M_PI / 2;
      }
    } else {
      // Change camera rotation in proportion to the the cursors' change in
      // location
      camera_rotation_x += dy * (SENSITIVITY / 100.0);
      camera_rotation_y += dx * (SENSITIVITY / 100.0);

      // Cap the rotation on the X axis (verticality of the camera)
      if (camera_rotation_x > M_PI / 2) {
        camera_rotation_x = M_PI / 2;
      } else if (camera_rotation_x < -M_PI / 2) {
        camera_rotation_x = -M_PI / 2;
      }
    }
  } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
      // Change light distance in proportion to the cursors' change in vertical
      // location
      light_distance += dy * (SENSITIVITY / 10.0);

      // Cap the smallest camera distance
      if (light_distance < 0.05) {
        light_distance = 0.05;
      }
    } else {
      // Change camera distance in proportion to the cursors' change in vertical
      // location
      camera_distance += dy * (SENSITIVITY / 10.0);

      // Cap the smallest camera distance
      if (camera_distance < 0.05) {
        camera_distance = 0.05;
      }
    }
  }

  // Keep track of cursor location for future callbacks
  prev_mouse_x = x;
  prev_mouse_y = y;
}

static void resize_callback(GLFWwindow *window, int width, int height) {
  glViewport(0, 0, width, height);
  aspect_ratio = width / (float)height;
}

// Window creation and render
int main(int argc, char **argv) {
  // Check arg count
  if (argc != 2) {
    fprintf(stderr, "Expected exactly one argument, which should be a path to "
                    "a .obj file. Terminating.\n");
    exit(EXIT_FAILURE);
  }

  // Use arg to check mesh validity
  cy::TriMesh mesh, light_object;
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
  GLFWwindow *window = glfwCreateWindow(1280, 720, PROJECT_NAME, NULL, NULL);
  if (!window) {
    fprintf(stderr, "Error in window or context creation. Terminating.\n");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  // Use crosshair cursor
  GLFWcursor *cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
  glfwSetCursor(window, cursor);

  // Input Callback registration
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetKeyCallback(window, key_callback);

  // Resize callback, window size init
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);
  glViewport(0, 0, width, height);
  aspect_ratio = width / (float)height;
  glfwSetFramebufferSizeCallback(window, resize_callback);

  // Init OpenGL
  glfwMakeContextCurrent(window);
  glewInit();
  glfwSwapInterval(1);
  glEnable(GL_DEPTH_TEST);
  compile_shaders();
  glPointSize(12.0f);

  // Initialize VAO/VBO's
  GLuint vao[3];
  glGenVertexArrays(3, vao);
  GLuint vbo[4];
  glGenBuffers(4, vbo);

  // Setup object VAO
  glBindVertexArray(vao[0]);

  // Position buffer
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NV(), &mesh.V(0),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Normal buffer
  mesh.ComputeNormals();
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glEnableVertexAttribArray(1);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NVN(), &mesh.VN(0),
               GL_STATIC_DRAW);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in object shader
  object_program.Bind();
  object_program.SetAttribBuffer("position", vbo[0], 3);
  object_program.SetAttribBuffer("normal", vbo[1], 3);
  object_program.SetUniform("light_depth_texture", 0);

  // Setup plane VAO
  glBindVertexArray(vao[1]);

  // Position buffer
  const float plane_vertices[] = {-60.0, 0.0, 60.0,  60.0,  0.0, 60.0,
                                  60.0,  0.0, -60.0, -60.0, 0.0, 60.0,
                                  60.0,  0.0, -60.0, -60.0, 0.0, -60.0};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[2]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in plane shader
  plane_program.Bind();
  plane_program.SetAttribBuffer("position", vbo[2], 3);
  plane_program.SetUniform("light_depth_texture", 0);

  // Setup light object VAO
  glBindVertexArray(vao[2]);

  // Position buffer
  const float light_vertex[] = {0.0, 0.0, 0.0};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[3]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(light_vertex), light_vertex,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in object shader
  light_object_program.Bind();
  light_object_program.SetAttribBuffer("position", vbo[3], 3);

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
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  // Center mesh and place camera at appropriate position
  mesh.ComputeBoundingBox();
  cy::Vec3f min = mesh.GetBoundMin(), max = mesh.GetBoundMax(),
            mesh_center = (min + max) / 2;
  float bounds_diagonal = min.Length() + max.Length();
  camera_distance = light_distance = bounds_diagonal / 2.0;
  light_rotation_x = M_PI / 2;

  // Create depth texture
  cy::GLRenderDepth2D light_depth_texture;
  light_depth_texture.Initialize(true, 1024, 1024);
  light_depth_texture.BindTexture(0);
  light_depth_texture.SetTextureFilteringMode(GL_LINEAR, GL_LINEAR);

  // Render loop
  while (!glfwWindowShouldClose(window)) {
    // Clear frame
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Define matrices
    cy::Matrix4f position_transform, light_transform, light_view_transform,
        shadow_transform;
    cy::Matrix3f world_transform, normal_transform;

    // Rotation and centering translation matrix
    position_transform =
        cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.5,
                                  200.0) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -camera_distance}) *
        cy::Matrix4f::RotationX(camera_rotation_x) *
        cy::Matrix4f::RotationY(camera_rotation_y) *
        cy::Matrix4f::Translation(-mesh_center);

    world_transform = position_transform.GetSubMatrix3().GetInverse();
    normal_transform = world_transform.GetTranspose();

    // Rotate for light transform (direction)
    light_transform =
        cy::Matrix4f::RotationY(light_rotation_y) *
        cy::Matrix4f::RotationX(light_rotation_x) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -light_distance});

    light_view_transform =
        cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.5,
                                  200.0) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -light_distance}) *
        cy::Matrix4f::RotationX(light_rotation_x) *
        cy::Matrix4f::RotationY(M_PI - light_rotation_y) *
        cy::Matrix4f::Translation(-mesh_center);

    shadow_transform = cy::Matrix4f::Translation(cy::Vec3f{0.5, 0.5, 0.499}) *
                       cy::Matrix4f::Scale(0.5) * light_view_transform;

    // Linearize uniform values
    float linear_position_transform[16], linear_light_transform[16],
        linear_light_view_transform[16], linear_shadow_transform[16],
        linear_world_transform[9], linear_normal_transform[9];
    position_transform.Get(linear_position_transform);
    world_transform.Get(linear_world_transform);
    normal_transform.Get(linear_normal_transform);
    light_transform.Get(linear_light_transform);
    light_view_transform.Get(linear_light_view_transform);
    shadow_transform.Get(linear_shadow_transform);

    // Light depth pass
    light_depth_texture.Bind();
    glClear(GL_DEPTH_BUFFER_BIT);

    // Setup object shader
    object_program.Bind();
    object_program.SetUniformMatrix4("position_transform",
                                     linear_light_view_transform);
    object_program.SetUniformMatrix4("light_transform", linear_light_transform);
    object_program.SetUniformMatrix4("shadow_transform",
                                     linear_shadow_transform);
    object_program.SetUniformMatrix3("world_transform", linear_world_transform);
    object_program.SetUniformMatrix3("normal_transform",
                                     linear_normal_transform);

    // Draw object
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);

    // Setup plane shader
    plane_program.Bind();
    plane_program.SetUniformMatrix4("position_transform",
                                    linear_light_view_transform);
    plane_program.SetUniformMatrix4("light_transform", linear_light_transform);
    plane_program.SetUniformMatrix4("shadow_transform",
                                    linear_shadow_transform);
    plane_program.SetUniformMatrix3("normal_transform",
                                    linear_normal_transform);

    // Draw plane
    glBindVertexArray(vao[1]);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Normal render pass
    light_depth_texture.Unbind();

    // Draw object
    object_program.Bind();
    object_program.SetUniformMatrix4("position_transform",
                                     linear_position_transform);
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);

    // Draw plane
    plane_program.Bind();
    plane_program.SetUniformMatrix4("position_transform",
                                    linear_position_transform);
    glBindVertexArray(vao[1]);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Setup light object shader
    light_object_program.Bind();
    light_object_program.SetUniformMatrix4("position_transform",
                                           linear_position_transform);
    light_object_program.SetUniformMatrix4("light_transform",
                                           linear_light_transform);

    // Draw light object
    glBindVertexArray(vao[2]);
    glDrawArrays(GL_POINTS, 0, 1);

    // Wait for next GLFW event to draw a new frame (no idle animation)
    glfwSwapBuffers(window);
    glfwWaitEvents();
  }

  glfwTerminate();
  exit(EXIT_SUCCESS);
}