// Silence deprecation warnings on Apple's distribution of OpenGL
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif

// immintrin.h does not compile on ARM machines
#ifdef __aarch64__
#define CY_NO_IMMINTRIN_H
#endif

#include "lodepng.h"
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
#define OPENGL_MAJOR 4
#define OPENGL_MINOR 1
#define SENSITIVITY 1
#define PROJECT_NAME "Tessellation"

// Global app state
static cy::GLSLProgram shadow_program, plane_program, plane_wireframe_program,
    light_object_program;
static double prev_mouse_x;
static double prev_mouse_y;
static float camera_rotation_x;
static float camera_rotation_y;
static float camera_distance;
static float light_rotation_x;
static float light_rotation_y;
static float light_distance;
static float aspect_ratio;
static bool wireframe_mode;
static int tessellation_level;

// Shaders
static const char *plane_vert_src = R"VS(
in vec3 position;
out vec3 ctrl_position;

void main() {
  ctrl_position = position;
}
)VS";

static const char *plane_tess_ctrl_src = R"TCS(
layout(vertices = 4) out;
in vec3 ctrl_position[];
uniform float tessellation_level;
out vec3 eval_position[];

void main() {
  // Set tessellation levels
  gl_TessLevelInner[0] = tessellation_level;
  gl_TessLevelInner[1] = tessellation_level;
  gl_TessLevelOuter[gl_InvocationID] = tessellation_level;

  // Pass position
  eval_position[gl_InvocationID] = ctrl_position[gl_InvocationID];
}
)TCS";

static const char *plane_tess_eval_src = R"TES(
layout(quads, equal_spacing, ccw) in;
in vec3 eval_position[];
uniform mat4 position_transform;
uniform mat4 shadow_transform;
uniform mat4 light_transform;
uniform mat4 texture_transform;
uniform mat4 camera_transform;
uniform sampler2D displacement_texture;
out vec3 geo_light_direction;
out vec3 geo_view_direction;
out vec4 geo_light_view_position;
out vec2 geo_texture_coordinate;

void main() {
  // Interpolate position
  vec3 a = mix(eval_position[0], eval_position[1], gl_TessCoord.x);
  vec3 b = mix(eval_position[3], eval_position[2], gl_TessCoord.x);
  vec3 position = mix(a, b, gl_TessCoord.y);

  // Get texture coordinate for displacement
  vec2 texture_coordinate = (texture_transform * vec4(position, 1)).xz;

  // Transform and displace position
  vec3 displacement =
    vec3(0, texture(displacement_texture, texture_coordinate).x * 16.0, 0);
  gl_Position = position_transform * vec4(position + displacement, 1);

  // For context vectors for the frag shader, we need the following objects
  vec3 light_position = vec3(light_transform * vec4(0, 0, 0, 1));
  vec3 camera_position = vec3(camera_transform * vec4(0, 0, 0, 1));

  // Vertex data for fragments
  geo_light_direction = light_position - position;
  geo_view_direction = camera_position - position;
  geo_light_view_position = shadow_transform * vec4(position, 1);
  geo_texture_coordinate = texture_coordinate;
}
)TES";

static const char *plane_geo_src = R"GS(
layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;
in vec3 geo_light_direction[];
in vec3 geo_view_direction[];
in vec4 geo_light_view_position[];
in vec2 geo_texture_coordinate[];
out vec3 frag_light_direction;
out vec3 frag_view_direction;
out vec4 frag_light_view_position;
out vec2 frag_texture_coordinate;

void create_vertex(int data_index) {
  gl_Position = gl_in[data_index].gl_Position;
  frag_light_direction = geo_light_direction[data_index];
  frag_view_direction = geo_view_direction[data_index];
  frag_light_view_position = geo_light_view_position[data_index];
  frag_texture_coordinate = geo_texture_coordinate[data_index];
  EmitVertex();
}

void main() {
  create_vertex(0);
  create_vertex(1);
  create_vertex(2);
}
)GS";

static const char *plane_frag_src = R"FS(
in vec3 frag_light_direction;
in vec3 frag_view_direction;
in vec4 frag_light_view_position;
in vec2 frag_texture_coordinate;
uniform sampler2DShadow light_depth_texture;
uniform sampler2D normal_texture;
out vec4 color;

void main() {
  // Find context vectors
  vec3 light_direction = normalize(frag_light_direction);
  vec3 view_direction = normalize(frag_view_direction);
  
  // Adjust normal from texture into 3d space
  vec3 normal = vec3(texture(normal_texture, frag_texture_coordinate));
  normal = normalize(vec3(normal.x - 0.5, normal.z - 0.5, 0.5 - normal.y));

  // Find half vector for blinn shading
  vec3 half_vector = normalize(light_direction + view_direction);
  
  // Diffuse component
  float geometry_term = max(0.0, dot(normal, light_direction));
  vec4 diffuse_color = vec4(0.5, 0.5, 0.5, 1.0);
  vec4 diffuse_component = diffuse_color * geometry_term;

  // Specular component
  float blinn_term = max(0.0, dot(normal, half_vector));
  vec4 specular_color = vec4(1);
  vec4 specular_component = specular_color * pow(blinn_term, 30.0);

  // Composite final color
  color = diffuse_component + specular_component;

  // Check shadow depth
  color *= textureProj(light_depth_texture, frag_light_view_position);
}
)FS";

static const char *plane_simple_tess_eval_src = R"TES(
layout(quads, equal_spacing, ccw) in;
in vec3 eval_position[];
uniform mat4 position_transform;
uniform mat4 texture_transform;
uniform sampler2D displacement_texture;

void main() {
  vec3 a = mix(eval_position[0], eval_position[1], gl_TessCoord.x);
  vec3 b = mix(eval_position[3], eval_position[2], gl_TessCoord.x);
  vec3 position = mix(a, b, gl_TessCoord.y);
  vec2 texture_coordinate = (texture_transform * vec4(position, 1)).xz;
  vec3 displacement =
    vec3(0, texture(displacement_texture, texture_coordinate).x * 16.0, 0);
  gl_Position = position_transform * vec4(position + displacement, 1);
}
)TES";

static const char *plane_simple_geo_src = R"GS(
layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

void main() {
  gl_Position = gl_in[0].gl_Position;
  EmitVertex();
  gl_Position = gl_in[1].gl_Position;
  EmitVertex();
  gl_Position = gl_in[2].gl_Position;
  EmitVertex();
}
)GS";

static const char *plane_wireframe_geo_src = R"GS(
layout (triangles) in;
layout (line_strip, max_vertices = 4) out;

void main() {
  gl_Position = gl_in[0].gl_Position;
  EmitVertex();
  gl_Position = gl_in[1].gl_Position;
  EmitVertex();
  gl_Position = gl_in[2].gl_Position;
  EmitVertex();
  gl_Position = gl_in[0].gl_Position;
  EmitVertex();
}
)GS";

static const char *plane_wireframe_frag_src = R"FS(
out vec4 color;

void main() {
    color = vec4(0, 1, 1, 1);
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

static const char *simple_frag_src = R"FS(
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
  shadow_program.BuildSources(plane_vert_src, simple_frag_src,
                              plane_simple_geo_src, plane_tess_ctrl_src,
                              plane_simple_tess_eval_src, version);
  plane_program.BuildSources(plane_vert_src, plane_frag_src, plane_geo_src,
                             plane_tess_ctrl_src, plane_tess_eval_src, version);
  plane_wireframe_program.BuildSources(
      plane_vert_src, plane_wireframe_frag_src, plane_wireframe_geo_src,
      plane_tess_ctrl_src, plane_simple_tess_eval_src, version);
  light_object_program.BuildSources(light_vert_src, simple_frag_src, NULL, NULL,
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
    case GLFW_KEY_SPACE:
      wireframe_mode = !wireframe_mode;
      break;
    case GLFW_KEY_RIGHT:
      tessellation_level *= 2;
      if (tessellation_level > 64) {
        tessellation_level = 64;
      }
      break;
    case GLFW_KEY_LEFT:
      tessellation_level /= 2;
      if (tessellation_level < 1) {
        tessellation_level = 1;
      }
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
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Expected one or two arguments, which should be paths to "
                    "two png files. Terminating.\n");
    exit(EXIT_FAILURE);
  }

  // Use arg to check mesh validity
  std::vector<unsigned char> normal_image, displacement_image;
  unsigned texture_width, texture_height;
  if (lodepng::decode(normal_image, texture_width, texture_height, argv[1]) ||
      (argc == 3 && lodepng::decode(displacement_image, texture_width,
                                    texture_height, argv[2]))) {
    fprintf(stderr, "Error while loading .png texture files. Terminating.\n");
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
  glLineWidth(6.0f);
  glPatchParameteri(GL_PATCH_VERTICES, 4);

  // Initialize VAO/VBO's
  GLuint vao[2];
  glGenVertexArrays(2, vao);
  GLuint vbo[2];
  glGenBuffers(2, vbo);

  // Setup plane VAO
  glBindVertexArray(vao[0]);

  // Position buffer
  const float plane_vertices[] = {-60.0, 0.0, 60.0,  60.0,  0.0, 60.0,
                                  60.0,  0.0, -60.0, -60.0, 0.0, -60.0};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in plane shader
  plane_program.Bind();
  plane_program.SetAttribBuffer("position", vbo[0], 3);
  plane_program.SetUniform("light_depth_texture", 0);
  plane_program.SetUniform("normal_texture", 1);
  plane_program.SetUniform("displacement_texture", 2);

  // Link buffers in wireframe shader
  plane_wireframe_program.Bind();
  plane_wireframe_program.SetAttribBuffer("position", vbo[0], 3);
  plane_wireframe_program.SetUniform("displacement_texture", 2);

  // Link buffers in shadow shader
  shadow_program.Bind();
  shadow_program.SetAttribBuffer("position", vbo[0], 3);
  shadow_program.SetUniform("displacement_texture", 2);

  // Setup light object VAO
  glBindVertexArray(vao[1]);

  // Position buffer
  const float light_vertex[] = {0.0, 0.0, 0.0};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(light_vertex), light_vertex,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in object shader
  light_object_program.Bind();
  light_object_program.SetAttribBuffer("position", vbo[1], 3);

  // Create depth texture
  cy::GLRenderDepth2D light_depth_texture;
  light_depth_texture.Initialize(true, 1024, 1024);
  light_depth_texture.BindTexture(0);
  light_depth_texture.SetTextureFilteringMode(GL_LINEAR, GL_LINEAR);

  // Create normal and displacement textures
  cy::GLTexture2D normal_texture, displacement_texture;
  normal_texture.Bind(1);
  normal_texture.Initialize();
  normal_texture.SetImage(&normal_image[0], 4, texture_width, texture_height);
  normal_texture.SetFilteringMode(GL_LINEAR, GL_LINEAR);

  displacement_texture.Bind(2);
  displacement_texture.Initialize();
  displacement_texture.SetFilteringMode(GL_LINEAR, GL_LINEAR);
  // Only copy displacement texture if it exists
  if (argc == 3) {
    displacement_texture.SetImage(&displacement_image[0], 4, texture_width,
                                  texture_height);
  }

  camera_distance = 100.0;
  light_distance = 80.0;
  camera_rotation_x = M_PI / 2;
  light_rotation_x = M_PI / 4;
  wireframe_mode = false;
  tessellation_level = 1;

  // Render loop
  while (!glfwWindowShouldClose(window)) {
    // Clear frame
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Define matrices
    cy::Matrix4f position_transform, light_transform, light_view_transform,
        shadow_transform, texture_transform, camera_transform;

    // Transform into camera space
    position_transform =
        cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.5,
                                  200.0) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -camera_distance}) *
        cy::Matrix4f::RotationX(camera_rotation_x) *
        cy::Matrix4f::RotationY(camera_rotation_y);

    // Transform the light position
    light_transform =
        cy::Matrix4f::RotationY(-light_rotation_y) *
        cy::Matrix4f::RotationX(-light_rotation_x) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, light_distance});

    camera_transform =
        cy::Matrix4f::RotationY(-camera_rotation_y) *
        cy::Matrix4f::RotationX(-camera_rotation_x) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, camera_distance});

    // Transform into light camera space
    light_view_transform =
        cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.5,
                                  200.0) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -light_distance}) *
        cy::Matrix4f::RotationX(light_rotation_x) *
        cy::Matrix4f::RotationY(light_rotation_y);

    // Transform into light camera space, and then into shadow texture space
    shadow_transform = cy::Matrix4f::Translation(cy::Vec3f{0.5, 0.5, 0.499}) *
                       cy::Matrix4f::Scale(0.5) * light_view_transform;

    texture_transform = cy::Matrix4f::Translation(cy::Vec3f{0.5, 0.5, 0.5}) *
                        cy::Matrix4f::Scale(1.0 / 120.0);

    // Linearize uniform values
    float linear_position_transform[16], linear_light_transform[16],
        linear_light_view_transform[16], linear_shadow_transform[16],
        linear_texture_transform[16], linear_camera_transform[16];
    position_transform.Get(linear_position_transform);
    light_transform.Get(linear_light_transform);
    camera_transform.Get(linear_camera_transform);
    light_view_transform.Get(linear_light_view_transform);
    shadow_transform.Get(linear_shadow_transform);
    texture_transform.Get(linear_texture_transform);

    // Light depth pass
    light_depth_texture.Bind();
    glClear(GL_DEPTH_BUFFER_BIT);

    // Setup plane shader
    shadow_program.Bind();
    shadow_program.SetUniformMatrix4("position_transform",
                                     linear_light_view_transform);
    shadow_program.SetUniformMatrix4("texture_transform",
                                     linear_texture_transform);
    shadow_program.SetUniform("tessellation_level",
                              static_cast<float>(tessellation_level));

    // Draw plane
    glBindVertexArray(vao[0]);
    glDrawArrays(GL_PATCHES, 0, 4);

    // Now move on to the normal render pass
    light_depth_texture.Unbind();

    // Draw plane
    plane_program.Bind();
    plane_program.SetUniformMatrix4("position_transform",
                                    linear_position_transform);
    plane_program.SetUniformMatrix4("light_transform", linear_light_transform);
    plane_program.SetUniformMatrix4("camera_transform",
                                    linear_camera_transform);
    plane_program.SetUniformMatrix4("shadow_transform",
                                    linear_shadow_transform);
    plane_program.SetUniformMatrix4("texture_transform",
                                    linear_texture_transform);
    plane_program.SetUniform("tessellation_level",
                             static_cast<float>(tessellation_level));

    glDrawArrays(GL_PATCHES, 0, 4);

    // Setup light object shader
    light_object_program.Bind();
    light_object_program.SetUniformMatrix4("position_transform",
                                           linear_position_transform);
    light_object_program.SetUniformMatrix4("light_transform",
                                           linear_light_transform);

    // Draw light object
    glBindVertexArray(vao[1]);
    glDrawArrays(GL_POINTS, 0, 1);

    if (wireframe_mode) {
      // Setup wireframe shader
      plane_wireframe_program.Bind();
      plane_wireframe_program.SetUniformMatrix4("position_transform",
                                                linear_position_transform);
      plane_wireframe_program.SetUniformMatrix4("texture_transform",
                                                linear_texture_transform);
      plane_wireframe_program.SetUniform(
          "tessellation_level", static_cast<float>(tessellation_level));

      // Draw wireframe
      glDisable(GL_DEPTH_TEST);
      glBindVertexArray(vao[0]);
      glDrawArrays(GL_PATCHES, 0, 4);
      glEnable(GL_DEPTH_TEST);
    }

    // Wait for next GLFW event to draw a new frame (no idle animation)
    glfwSwapBuffers(window);
    glfwWaitEvents();
  }

  glfwTerminate();
  exit(EXIT_SUCCESS);
}