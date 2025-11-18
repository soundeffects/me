// Silence deprecation warnings on macOS
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GL/freeglut.h>
#include <cmath>

// Math Functions

float color_fader(int time, int offset, int period) {
    int periodic_time = (time + offset) % period;
    int phase_section = 2 * period / 3;
    if (0 < periodic_time && periodic_time <= phase_section) {
        float period_completion = static_cast<float>(periodic_time) / static_cast<float>(period);
        float radians = (3 * M_PI / 2) * period_completion;
        return sin(radians);
    } else {
        return 0.0;
    }
}


// Glut Handlers
// Note: remember to call glutPostRedisplay after any change to state is made

void display_handling() {
    glClear(GL_COLOR_BUFFER_BIT); // Note: OR with GL_DEPTH_BUFFER_BIT if using a depth buffer
    glutSwapBuffers();
}

void keyboard_handling(unsigned char key, int x, int y) {
    switch (key) {
        case 27: // Escape
            // glutLeaveMainLoop does not appear to be defined on macOS.
            // exit will not break anything, so we will use this instead
            // of glutLeaveMainLoop
            exit(0);
            break;
    }
}

void resize_handling(int x, int y) {
    glutPostRedisplay();
}

void idle_handling() {
    // Animate the background color
    int time = glutGet(GLUT_ELAPSED_TIME);
    
    glClearColor(
        color_fader(time, 0, 4500),
        color_fader(time, 1500, 4500),
        color_fader(time, 3000, 4500),
        0
    );
    
    glutPostRedisplay();
}

// void special_handling(int key, int x, int y); Note: glutGetModifiers() returns any modifier keys held down
// void mouse_handling(int button, int state, int x, int y);
// void drag_handling(int x, int y);
// void hover_handling(int x, int y);


// Glut Init

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitWindowSize(1920, 1080);
    glutInitWindowPosition(0, 0);
    glutInitDisplayMode(GLUT_DOUBLE);
    
    glutCreateWindow("Hello World!");
    glutDisplayFunc(display_handling);
    glutKeyboardFunc(keyboard_handling);
    glutReshapeFunc(resize_handling);
    glutIdleFunc(idle_handling);
    // glutSpecialFunc(special_handling);
    // glutMouseFunc(mouse_handling);
    // glutMotionFunc(drag_handling);
    // glutPassiveMotionFunc(hover_handling);
    
    glClearColor(0, 0, 0, 0);
    
    glutMainLoop();
    
    return 0;
}