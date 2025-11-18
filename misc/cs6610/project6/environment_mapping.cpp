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
#include <lodepng.h>
#include <stdio.h>
#include <stdlib.h>

// Definitions
#define OPENGL_MAJOR 3
#define OPENGL_MINOR 3
#define SENSITIVITY 1
#define PROJECT_NAME "Environment Mapping"

// Global app state
static cy::GLSLProgram object_program, background_program, plane_program;
static double prev_mouse_x, prev_mouse_y;
static float camera_rotation_x, camera_rotation_y, camera_distance,
    aspect_ratio;
static int width, height;

// Shaders
static const char *object_vert_src = R"VS(
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

static const char *object_frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
uniform mat3 world_space_transform;
uniform samplerCube cubemap;
out vec4 color;

void main() {
    // Find context vectors
    vec3 normal = normalize(fragment_normal);
    vec3 position = normalize(fragment_position);
    vec3 light_direction = world_space_transform * normalize(vec3(1.0, 1.0, -1.0));
    vec3 view_direction = -position;
    vec3 reflection_direction = world_space_transform * reflect(position, normal);
    vec3 half_vector = normalize(light_direction + view_direction);
    
    // Ambient component
    vec4 ambient_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 ambient_component = ambient_color * 0.05;
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = vec4(0.3, 0.3, 0.3, 1.0);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Specular component
    float blinn_term = max(0.0, dot(normal, half_vector));
    vec4 specular_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 specular_component = specular_color * pow(blinn_term, 70.0);
    
    // Reflective component
    vec4 reflective_color = texture(cubemap, reflection_direction);
    vec4 reflective_component = reflective_color * 1.0;
    
    // Composite final color
    color = ambient_component + diffuse_component + specular_component + reflective_component;
}
)FS";

static const char *background_vert_src = R"VS(
in vec3 position;
uniform mat3 direction_transform;
out vec3 fragment_direction;

void main() {
    // Send direction to fragments
    fragment_direction = direction_transform * position;
    
    // Render fragments
    gl_Position = vec4(position, 1);
}
)VS";

static const char *background_frag_src = R"FS(
in vec3 fragment_direction;
uniform samplerCube cubemap;
out vec4 color;

void main() {
    color = texture(cubemap, fragment_direction);
}
)FS";

static const char *plane_vert_src = R"VS(
in vec3 position;
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
    fragment_normal = normal_transform * vec3(0.0, 1.0, 0.0);
}
)VS";

static const char *plane_frag_src = R"FS(
in vec3 fragment_position;
in vec3 fragment_normal;
uniform sampler2D reflection_texture;
uniform samplerCube cubemap;
uniform mat3 world_space_transform;
uniform int screen_width;
uniform int screen_height;
out vec4 color;

void main() {
    // Find context vectors
    vec3 normal = normalize(fragment_normal);
    vec3 position = normalize(fragment_position);
    vec3 light_direction = world_space_transform * normalize(vec3(1.0, 1.0, -1.0));
    vec3 view_direction = -position;
    vec3 reflection_direction = world_space_transform * reflect(position, normal);
    vec3 half_vector = normalize(light_direction + view_direction);
    
    // Ambient component
    vec4 ambient_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 ambient_component = ambient_color * 0.05;
    
    // Diffuse component
    float geometry_term = max(0.0, dot(normal, light_direction));
    vec4 diffuse_color = vec4(0.3, 0.3, 0.3, 1.0);
    vec4 diffuse_component = diffuse_color * geometry_term;
    
    // Specular component
    float blinn_term = max(0.0, dot(normal, half_vector));
    vec4 specular_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 specular_component = specular_color * pow(blinn_term, 70.0);
    
    // Reflective component
    vec4 reflective_color = texture(cubemap, reflection_direction);
    vec4 reflective_component = reflective_color * 0.7;
    
    // Reflection bounce component
    vec4 reflection_bounce_color = texture(reflection_texture, vec2(gl_FragCoord.x / screen_width, gl_FragCoord.y / screen_height));
    vec4 reflection_bounce_component = reflection_bounce_color * 0.5;

    // Couldn't figure out how to clear the texture with a zero alpha, so that
    // alpha blending would work properly. This is a hacky way to do it: if the
    // color is completely black, then we store alpha as zero.
    float r = ceil(reflection_bounce_color.r);
    float g = ceil(reflection_bounce_color.g);
    float b = ceil(reflection_bounce_color.b);
    float alpha = max(r, max(g, b));
    
    // Composite final color
    color = (alpha * reflection_bounce_component) + ((1.0 - alpha) * reflective_component) + ambient_component + diffuse_component + specular_component;
}
)FS";

static void compile_shaders() {
  // Prepend the GLSL version string
  char version[20];
  snprintf(version, 20, "#version %d%d0 core\n", OPENGL_MAJOR, OPENGL_MINOR);
  object_program.BuildSources(object_vert_src, object_frag_src, NULL, NULL,
                              NULL, version);
  background_program.BuildSources(background_vert_src, background_frag_src,
                                  NULL, NULL, NULL, version);
  plane_program.BuildSources(plane_vert_src, plane_frag_src, NULL, NULL, NULL,
                             version);
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
    camera_rotation_x += dy * (SENSITIVITY / 100.0);
    camera_rotation_y += dx * (SENSITIVITY / 100.0);

    if (camera_rotation_x > M_PI / 2) {
      camera_rotation_x = M_PI / 2;
    } else if (camera_rotation_x < -M_PI / 2) {
      camera_rotation_x = -M_PI / 2;
    }
  } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
    camera_distance += dy * (SENSITIVITY / 10.0);

    if (camera_distance < 0.5) {
      camera_distance = 0.5;
    }
  }

  // Keep track of cursor location for future callbacks
  prev_mouse_x = x;
  prev_mouse_y = y;
}

static void resize_callback(GLFWwindow *window, int new_width, int new_height) {
  width = new_width;
  height = new_height;
  glViewport(0, 0, width, height);
  aspect_ratio = width / (float)height;
  plane_program.SetUniform("screen_width", width);
  plane_program.SetUniform("screen_height", height);
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
  cy::TriMesh mesh;
  if (!mesh.LoadFromFileObj(argv[1])) {
    fprintf(stderr, "Error while loading .obj file. Terminating.\n");
    exit(EXIT_FAILURE);
  }

  // Load cubemap textures
  std::vector<unsigned char> cubemap_textures[6];
  unsigned cubemap_width, cubemap_height;
  if (lodepng::decode(cubemap_textures[0], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_posx.png") ||
      lodepng::decode(cubemap_textures[1], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_negx.png") ||
      lodepng::decode(cubemap_textures[2], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_posy.png") ||
      lodepng::decode(cubemap_textures[3], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_negy.png") ||
      lodepng::decode(cubemap_textures[4], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_posz.png") ||
      lodepng::decode(cubemap_textures[5], cubemap_width, cubemap_height,
                      "./cubemap/cubemap_negz.png")) {
    fprintf(stderr, "Error while loading cubemap textures. Terminating.\n");
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
  glfwGetFramebufferSize(window, &width, &height);
  glViewport(0, 0, width, height);
  aspect_ratio = width / (float)height;
  glfwSetFramebufferSizeCallback(window, resize_callback);

  // Init OpenGL
  glfwMakeContextCurrent(window);
  glewInit();
  glfwSwapInterval(1);
  glEnable(GL_DEPTH_TEST);
  glClearColor(0., 0., 0., 0.);
  compile_shaders();

  // Create cubemap
  cy::GLTextureCubeMap cubemap;
  cubemap.Initialize();
  for (int i = 0; i < 6; i++) {
    cubemap.SetImageRGBA((cy::GLTextureCubeMap::Side)i, &cubemap_textures[i][0],
                         cubemap_width, cubemap_height);
  }
  cubemap.Bind(0);
  cubemap.BuildMipmaps();
  cubemap.SetFilteringMode(GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR);
  cubemap.SetAnisotropy(4.0);
  cubemap.SetSeamless();

  // Link cubemap in shaders
  object_program.Bind();
  object_program.SetUniform("cubemap", 0);
  background_program.Bind();
  background_program.SetUniform("cubemap", 0);
  plane_program.Bind();
  plane_program.SetUniform("cubemap", 0);

  // Generate VAO's and VBO's
  GLuint vao[3];
  glGenVertexArrays(3, vao);
  GLuint vbo[4];
  glGenBuffers(4, vbo);

  // Object VAO
  glBindVertexArray(vao[0]);

  // Position buffer
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NV(), &mesh.V(0),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Mesh normals averaged, so that one position maps to one normal
  mesh.ComputeNormals();

  // Normal buffer
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glEnableVertexAttribArray(1);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * mesh.NVN(), &mesh.VN(0),
               GL_STATIC_DRAW);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffers in object shader
  object_program.Bind();
  object_program.SetAttribBuffer("position", vbo[0], 3);
  object_program.SetAttribBuffer("normal", vbo[1], 3);

  // Background VAO
  glBindVertexArray(vao[1]);

  // Position buffer
  const float background_vertices[] = {-1.0,  3.0,  0.999, 3.0,  -1.0,
                                       0.999, -1.0, -1.0,  0.999};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[2]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(background_vertices),
               background_vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffer in background shader
  background_program.Bind();
  background_program.SetAttribBuffer("position", vbo[2], 3);

  // Plane VAO
  glBindVertexArray(vao[2]);

  // Position Buffer
  const float plane_vertices[] = {-30.0, 0.0, 30.0,  30.0,  0.0, 30.0,
                                  30.0,  0.0, -30.0, -30.0, 0.0, 30.0,
                                  30.0,  0.0, -30.0, -30.0, 0.0, -30.0};
  glBindBuffer(GL_ARRAY_BUFFER, vbo[3]);
  glEnableVertexAttribArray(0);
  glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

  // Link buffer in plane shader
  plane_program.Bind();
  plane_program.SetAttribBuffer("position", vbo[3], 3);

  // Setup reflection render buffer
  cy::GLRenderTexture2D render_buffer;
  render_buffer.BindTexture(1);
  plane_program.SetUniform("reflection_texture", 1);
  plane_program.SetUniform("screen_width", width);
  plane_program.SetUniform("screen_height", height);

  // Find indices
  GLuint indices[3 * mesh.NF()];
  for (int i = 0; i < mesh.NF(); i++) {
    cy::TriMesh::TriFace face = mesh.F(i), texture_face = mesh.FT(i);
    for (int face_index = 0; face_index < 3; face_index++) {
      indices[3 * i + face_index] = face.v[face_index];
    }
  }

  // Transfer indices to buffer object
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), &indices[0],
               GL_STATIC_DRAW);

  // Center mesh and place camera at appropriate position
  mesh.ComputeBoundingBox();
  cy::Vec3f min = mesh.GetBoundMin(), max = mesh.GetBoundMax(),
            mesh_center = (min + max) / 2;
  float bounds_diagonal = min.Length() + max.Length();
  camera_distance = bounds_diagonal / 2.0;

  // Render loop
  while (!glfwWindowShouldClose(window)) {
    // Clear frame
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Transform matrix is a series of transformations into camera space
    cy::Matrix4f position_transform =
        cy::Matrix4f::Perspective((2.0 / 5.0) * M_PI, aspect_ratio, 0.05,
                                  100.0) *
        cy::Matrix4f::Translation(cy::Vec3f{0.0, 0.0, -camera_distance}) *
        cy::Matrix4f::RotationX(camera_rotation_x) *
        cy::Matrix4f::RotationY(camera_rotation_y) *
        cy::Matrix4f::Translation(-mesh_center);

    // Rotation around the x axis to transform the object into "reflection
    // space"
    cy::Matrix4f reflection_transform =
        position_transform * cy::Matrix4f::RotationX(M_PI);

    // Inverse transform from camera space, so that reflection rays sample
    // accurately from the world space
    cy::Matrix3f world_space_transform =
        position_transform.GetSubMatrix3().GetInverse();

    // Use the transpose of inverse trick to get the normal transformation
    cy::Matrix3f normal_transform = world_space_transform.GetTranspose();

    // Do the same transformations for the reflected space
    cy::Matrix3f reflection_space_transform =
        reflection_transform.GetSubMatrix3().GetInverse();
    cy::Matrix3f reflection_normal_transform =
        reflection_space_transform.GetTranspose();

    // Rotate reflection space around y axis as well. Otherwise, the reflection
    // will be showing the opposite side of the teapot.
    reflection_space_transform =
        cy::Matrix3f::RotationY(M_PI) * reflection_space_transform;

    // Linearize transformation matrices
    float linear_position_transform[16], linear_reflection_transform[16],
        linear_world_space_transform[9], linear_reflection_space_transform[9],
        linear_normal_transform[9], linear_reflection_normal_transform[9];
    position_transform.Get(linear_position_transform);
    reflection_transform.Get(linear_reflection_transform);
    world_space_transform.Get(linear_world_space_transform);
    reflection_space_transform.Get(linear_reflection_space_transform);
    normal_transform.Get(linear_normal_transform);
    reflection_normal_transform.Get(linear_reflection_normal_transform);

    // Render background phase
    glDepthMask(GL_FALSE);

    // Draw background triangle without transform
    background_program.Bind();
    background_program.SetUniformMatrix3("direction_transform",
                                         linear_world_space_transform);
    glBindVertexArray(vao[1]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Render foreground phase
    glDepthMask(GL_TRUE);

    // Draw Object
    object_program.Bind();
    object_program.SetUniformMatrix4("position_transform",
                                     linear_position_transform);
    object_program.SetUniformMatrix3("world_space_transform",
                                     linear_world_space_transform);
    object_program.SetUniformMatrix3("normal_transform",
                                     linear_normal_transform);
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);

    // Refresh render buffer
    render_buffer.Delete();
    render_buffer.Initialize(true, 3, width, height);

    // Draw Reflection
    render_buffer.Bind();
    glClear(GL_COLOR_BUFFER_BIT);
    object_program.SetUniformMatrix4("position_transform",
                                     linear_reflection_transform);
    object_program.SetUniformMatrix3("world_space_transform",
                                     linear_reflection_space_transform);
    object_program.SetUniformMatrix3("normal_transform",
                                     linear_reflection_normal_transform);
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glDrawElements(GL_TRIANGLES, 3 * mesh.NF(), GL_UNSIGNED_INT, nullptr);
    render_buffer.Unbind();
    render_buffer.BuildTextureMipmaps();
    render_buffer.SetTextureFilteringMode(GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR);
    render_buffer.SetTextureAnisotropy(4.0);

    // Draw Plane
    plane_program.Bind();
    plane_program.SetUniformMatrix4("position_transform",
                                    linear_position_transform);
    plane_program.SetUniformMatrix3("world_space_transform",
                                    linear_world_space_transform);
    plane_program.SetUniformMatrix3("normal_transform",
                                    linear_normal_transform);
    glBindVertexArray(vao[2]);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Wait for next GLFW event to draw a new frame (no idle animation)
    glfwSwapBuffers(window);
    glfwWaitEvents();
  }

  glfwTerminate();
  exit(EXIT_SUCCESS);
}