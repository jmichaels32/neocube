#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <time.h>

#include "lib/util.h"

#define MAX_SHAPES 3
#define MAX_FACES 8
#define LIFESPAN_MIN 300
#define LIFESPAN_MAX 800
#define CAMERA_DISTANCE 100
#define PROJECTION_SCALE 40.0f

typedef enum {
    SHAPE_CUBE,
    SHAPE_TORUS,
    SHAPE_PYRAMID,
    SHAPE_OCTAHEDRON,
    SHAPE_COUNT
} ShapeType;

typedef struct {
    float sin_theta, cos_theta;
    float sin_phi, cos_phi;
    float sin_alpha, cos_alpha;
} Trig;

typedef enum {
    PHASE_ENTERING,
    PHASE_BOUNCING,
    PHASE_EXITING,
    PHASE_DEAD
} LifePhase;

typedef struct {
    ShapeType type;
    float x, y;
    float vx, vy;
    float theta, phi, alpha;
    float d_theta, d_phi, d_alpha;
    int half_width;
    float base_hue;        // shape's base hue (0-360), slowly cycles
    float hue_speed;       // how fast base_hue drifts
    int lifespan;
    int age;
    LifePhase phase;
    Trig trig;
} Shape;

// Screen buffers — color is now packed RGB (R<<16 | G<<8 | B)
char *ascii_array;
float *depth_array;
int *color_array;
int screen_width, screen_height;

// Output buffer
char *frame_buf;
int frame_buf_size;
int frame_buf_len;

Shape shapes[MAX_SHAPES];

void cleanup(int sig) {
    (void)sig;
    free(ascii_array);
    free(depth_array);
    free(color_array);
    free(frame_buf);
    printf("\x1b[?25h\x1b[0m");
    exit(0);
}

// --- Buffered output ---

void buf_reset(void) {
    frame_buf_len = 0;
}

void buf_append(const char *data, int len) {
    if (frame_buf_len + len < frame_buf_size) {
        memcpy(frame_buf + frame_buf_len, data, len);
        frame_buf_len += len;
    }
}

void buf_append_char(char c) {
    if (frame_buf_len < frame_buf_size - 1)
        frame_buf[frame_buf_len++] = c;
}

void buf_flush(void) {
    write(STDOUT_FILENO, frame_buf, frame_buf_len);
}

// --- Random helpers ---

float rand_float(float min, float max) {
    return min + (float)rand() / (float)RAND_MAX * (max - min);
}

int rand_range(int min, int max) {
    return min + rand() % (max - min + 1);
}

// --- HSV to packed RGB ---

int hsv_to_rgb(float h, float s, float v) {
    // h: 0-360, s: 0-1, v: 0-1
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;

    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else               { r = c; g = 0; b = x; }

    int ri = (int)((r + m) * 255);
    int gi = (int)((g + m) * 255);
    int bi = (int)((b + m) * 255);
    return (ri << 16) | (gi << 8) | bi;
}

// Compute gradient color for a point on a face.
// face_index offsets the hue slightly so each face is distinct but harmonious.
// gradient_t (0-1) creates a smooth brightness/hue shift across the face.
int face_gradient_color(const Shape *s, int face_index, float gradient_t) {
    // Each face is offset by 15-25 degrees — close enough to feel cohesive
    float hue = s->base_hue + face_index * 20.0f;
    // Gradient shifts hue slightly (±10 degrees) and varies brightness
    hue += (gradient_t - 0.5f) * 20.0f;
    float val = 0.55f + gradient_t * 0.4f;   // brightness from 0.55 to 0.95
    float sat = 0.6f + (1.0f - gradient_t) * 0.3f; // saturation from 0.6 to 0.9
    return hsv_to_rgb(hue, sat, val);
}

// --- Trig cache ---

void fill_trig(Shape *s) {
    s->trig.sin_theta = sinf(s->theta);
    s->trig.cos_theta = cosf(s->theta);
    s->trig.sin_phi = sinf(s->phi);
    s->trig.cos_phi = cosf(s->phi);
    s->trig.sin_alpha = sinf(s->alpha);
    s->trig.cos_alpha = cosf(s->alpha);
}

// --- 3D rotation and projection ---

float rotate_x(float x, float y, float z, const Trig *t) {
    return y * t->sin_theta * t->sin_phi * t->cos_alpha -
           z * t->cos_theta * t->sin_phi * t->cos_alpha +
           y * t->cos_theta * t->sin_alpha +
           z * t->sin_theta * t->sin_alpha +
           x * t->cos_phi * t->cos_alpha;
}

float rotate_y(float x, float y, float z, const Trig *t) {
    return y * t->cos_theta * t->cos_alpha +
           z * t->sin_theta * t->cos_alpha -
           y * t->sin_theta * t->sin_phi * t->sin_alpha +
           z * t->cos_theta * t->sin_phi * t->sin_alpha -
           x * t->cos_phi * t->sin_alpha;
}

float rotate_z(float x, float y, float z, const Trig *t) {
    return z * t->cos_theta * t->cos_phi -
           y * t->sin_theta * t->cos_phi +
           x * t->sin_phi;
}

void project_point(const Shape *s, float x, float y, float z, char ascii, int color) {
    float rx = rotate_x(x, y, z, &s->trig);
    float ry = rotate_y(x, y, z, &s->trig);
    float rz = rotate_z(x, y, z, &s->trig) + CAMERA_DISTANCE;

    if (rz <= 1.0f) return;

    float sf = PROJECTION_SCALE / rz;
    int px = (int)(screen_width / 2 + sf * rx + s->x);
    int py = (int)(screen_height / 2 + sf * ry + s->y);

    if (px < 0 || px >= screen_width || py < 0 || py >= screen_height) return;

    int index = px + py * screen_width;
    if (sf > depth_array[index]) {
        depth_array[index] = sf;
        ascii_array[index] = ascii;
        color_array[index] = color;
    }
}

// --- Triangle fill with gradient ---

void fill_triangle(const Shape *s, float v0[3], float v1[3], float v2[3],
                   char ascii, int face_index, float step) {
    for (float u = 0; u <= 1.0f; u += step) {
        for (float v = 0; v <= 1.0f - u; v += step) {
            float w = 1.0f - u - v;
            float x = w * v0[0] + u * v1[0] + v * v2[0];
            float y = w * v0[1] + u * v1[1] + v * v2[1];
            float z = w * v0[2] + u * v1[2] + v * v2[2];
            // Use barycentric u as gradient parameter
            int color = face_gradient_color(s, face_index, u);
            project_point(s, x, y, z, ascii, color);
        }
    }
}

// --- Shape renderers ---

void render_cube(Shape *s) {
    float hw = s->half_width;
    float step = 0.5f;
    float inv = 1.0f / (2.0f * hw);

    for (float i = -hw; i < hw; i += step) {
        float ti = (i + hw) * inv;  // 0 to 1 across face
        for (float j = -hw; j < hw; j += step) {
            float tj = (j + hw) * inv;
            float grad = (ti + tj) * 0.5f;  // diagonal gradient
            project_point(s, i, j, -hw, '#', face_gradient_color(s, 0, grad));
            project_point(s, i, hw, j, '`',  face_gradient_color(s, 1, grad));
            project_point(s, -hw, j, -i, '*', face_gradient_color(s, 2, grad));
            project_point(s, i, -hw, -j, '.', face_gradient_color(s, 3, grad));
            project_point(s, -i, j, hw, ';',  face_gradient_color(s, 4, grad));
            project_point(s, hw, j, i, '-',   face_gradient_color(s, 5, grad));
        }
    }
}

void render_torus(Shape *s) {
    float R = s->half_width * 0.65f;
    float r = s->half_width * 0.35f;
    float inv_2pi = 1.0f / (2.0f * M_PI);

    for (float u = 0; u < 2 * M_PI; u += 0.07f) {
        float cos_u = cosf(u), sin_u = sinf(u);
        float tu = u * inv_2pi;  // 0 to 1 around the ring
        for (float v = 0; v < 2 * M_PI; v += 0.03f) {
            float cos_v = cosf(v), sin_v = sinf(v);
            float x = (R + r * cos_v) * cos_u;
            float y = (R + r * cos_v) * sin_u;
            float z = r * sin_v;
            float tv = v * inv_2pi;  // 0 to 1 around the tube
            // Blend ring position and tube position for a 2D gradient feel
            float grad = (tu + tv) * 0.5f;
            int face = (int)(v / (M_PI / 2)) % 4;
            char chars[] = "@#%&";
            project_point(s, x, y, z, chars[face], face_gradient_color(s, face, grad));
        }
    }
}

void render_pyramid(Shape *s) {
    float hw = s->half_width;
    float step = 0.5f;
    float tri_step = 0.02f;
    float inv = 1.0f / (2.0f * hw);

    // Apex and base corners
    float apex[3] = {0, -hw, 0};
    float b0[3] = {-hw, hw, -hw};
    float b1[3] = { hw, hw, -hw};
    float b2[3] = { hw, hw,  hw};
    float b3[3] = {-hw, hw,  hw};

    // Base (grid fill with gradient)
    for (float i = -hw; i < hw; i += step) {
        float ti = (i + hw) * inv;
        for (float j = -hw; j < hw; j += step) {
            float tj = (j + hw) * inv;
            project_point(s, i, hw, j, '_', face_gradient_color(s, 0, (ti + tj) * 0.5f));
        }
    }

    // Four triangular faces
    char face_chars[] = {'^', '/', '\\', '|'};
    float *corners[] = {b0, b1, b2, b3};
    for (int f = 0; f < 4; f++) {
        fill_triangle(s, apex, corners[f], corners[(f + 1) % 4],
                      face_chars[f], f + 1, tri_step);
    }
}

void render_octahedron(Shape *s) {
    float hw = s->half_width;
    float tri_step = 0.018f;

    float verts[6][3] = {
        { hw, 0, 0}, {-hw, 0, 0},
        {0,  hw, 0}, {0, -hw, 0},
        {0, 0,  hw}, {0, 0, -hw}
    };

    int faces[8][3] = {
        {0, 2, 4}, {0, 2, 5}, {0, 3, 4}, {0, 3, 5},
        {1, 2, 4}, {1, 2, 5}, {1, 3, 4}, {1, 3, 5}
    };
    char chars[] = "#*+=<>%&";

    for (int f = 0; f < 8; f++) {
        fill_triangle(s, verts[faces[f][0]], verts[faces[f][1]], verts[faces[f][2]],
                      chars[f], f, tri_step);
    }
}

void render_shape(Shape *s) {
    switch (s->type) {
        case SHAPE_CUBE:        render_cube(s); break;
        case SHAPE_TORUS:       render_torus(s); break;
        case SHAPE_PYRAMID:     render_pyramid(s); break;
        case SHAPE_OCTAHEDRON:  render_octahedron(s); break;
        default: break;
    }
}

// --- Shape lifecycle ---

void shape_init(Shape *s) {
    s->type = rand() % SHAPE_COUNT;
    s->half_width = rand_range(screen_height / 3, screen_height / 2);

    int edge = rand() % 4;
    float projected_extent = (PROJECTION_SCALE / CAMERA_DISTANCE) * s->half_width + 5;
    float spawn_x = screen_width / 2.0f + projected_extent;
    float spawn_y = screen_height / 2.0f + projected_extent;
    switch (edge) {
        case 0:
            s->x = -spawn_x;
            s->y = rand_float(-screen_height / 4.0f, screen_height / 4.0f);
            s->vx = rand_float(0.4f, 0.8f);
            s->vy = rand_float(-0.2f, 0.2f);
            break;
        case 1:
            s->x = spawn_x;
            s->y = rand_float(-screen_height / 4.0f, screen_height / 4.0f);
            s->vx = -rand_float(0.4f, 0.8f);
            s->vy = rand_float(-0.2f, 0.2f);
            break;
        case 2:
            s->x = rand_float(-screen_width / 4.0f, screen_width / 4.0f);
            s->y = -spawn_y;
            s->vx = rand_float(-0.2f, 0.2f);
            s->vy = rand_float(0.4f, 0.8f);
            break;
        case 3:
            s->x = rand_float(-screen_width / 4.0f, screen_width / 4.0f);
            s->y = spawn_y;
            s->vx = rand_float(-0.2f, 0.2f);
            s->vy = -rand_float(0.4f, 0.8f);
            break;
    }

    s->theta = rand_float(0, 2 * M_PI);
    s->phi = rand_float(0, 2 * M_PI);
    s->alpha = rand_float(0, 2 * M_PI);
    s->d_theta = rand_float(0.03f, 0.12f);
    s->d_phi = rand_float(0.03f, 0.08f);
    s->d_alpha = rand_float(0.03f, 0.10f);

    s->base_hue = rand_float(0, 360);
    s->hue_speed = rand_float(0.05f, 0.2f);

    s->lifespan = rand_range(LIFESPAN_MIN, LIFESPAN_MAX);
    s->age = 0;
    s->phase = PHASE_ENTERING;
}

int shape_is_offscreen(Shape *s) {
    float ext = (PROJECTION_SCALE / CAMERA_DISTANCE) * s->half_width + 10;
    return (s->x < -(screen_width / 2.0f + ext) ||
            s->x > (screen_width / 2.0f + ext) ||
            s->y < -(screen_height / 2.0f + ext) ||
            s->y > (screen_height / 2.0f + ext));
}

int shape_is_onscreen(Shape *s) {
    float ext = (PROJECTION_SCALE / CAMERA_DISTANCE) * s->half_width;
    float edge_x = screen_width / 2.0f - ext;
    float edge_y = screen_height / 2.0f - ext;
    return (s->x > -edge_x && s->x < edge_x &&
            s->y > -edge_y && s->y < edge_y);
}

void shape_update(Shape *s) {
    if (s->phase == PHASE_DEAD) return;

    s->theta += s->d_theta;
    s->phi += s->d_phi;
    s->alpha += s->d_alpha;

    s->x += s->vx;
    s->y += s->vy;

    // Gentle hue drift
    s->base_hue += s->hue_speed;

    float ext = (PROJECTION_SCALE / CAMERA_DISTANCE) * s->half_width;
    float edge_x = screen_width / 2.0f - ext;
    float edge_y = screen_height / 2.0f - ext;

    switch (s->phase) {
        case PHASE_ENTERING:
            if (shape_is_onscreen(s))
                s->phase = PHASE_BOUNCING;
            break;

        case PHASE_BOUNCING:
            if (s->x > edge_x || s->x < -edge_x) s->vx *= -1;
            if (s->y > edge_y || s->y < -edge_y) s->vy *= -1;

            s->age++;
            if (s->age >= s->lifespan)
                s->phase = PHASE_EXITING;
            break;

        case PHASE_EXITING:
            if (shape_is_offscreen(s))
                s->phase = PHASE_DEAD;
            break;

        case PHASE_DEAD:
            break;
    }
}

// --- Main ---

int main(void) {
    srand(time(NULL));

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    screen_width = w.ws_col;
    screen_height = w.ws_row - 2;

    int total = screen_width * screen_height;
    ascii_array = malloc(total * sizeof(char));
    depth_array = malloc(total * sizeof(float));
    color_array = malloc(total * sizeof(int));
    // True color escapes are up to 19 bytes each + 1 char = 20 bytes per cell
    frame_buf_size = total * 22 + 256;
    frame_buf = malloc(frame_buf_size);

    if (!ascii_array || !depth_array || !color_array || !frame_buf) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    signal(SIGINT, cleanup);

    for (int i = 0; i < MAX_SHAPES; i++) {
        shape_init(&shapes[i]);
        shapes[i].lifespan = LIFESPAN_MIN + i * ((LIFESPAN_MAX - LIFESPAN_MIN) / MAX_SHAPES);
    }

    printf("\x1b[2J");

    while (1) {
        memset(ascii_array, ' ', total);
        memset(depth_array, 0, total * sizeof(float));
        memset(color_array, 0, total * sizeof(int));

        for (int i = 0; i < MAX_SHAPES; i++) {
            if (shapes[i].phase == PHASE_DEAD)
                shape_init(&shapes[i]);
            fill_trig(&shapes[i]);
            render_shape(&shapes[i]);
            shape_update(&shapes[i]);
        }

        // Build frame buffer with true color (24-bit) escapes
        buf_reset();
        buf_append("\x1b[H\x1b[?25l", 11);

        int prev_color = -1;
        for (int idx = 0; idx < total; idx++) {
            if (idx % screen_width == 0) {
                buf_append_char('\n');
                prev_color = -1;
            } else {
                if (ascii_array[idx] != ' ') {
                    if (color_array[idx] != prev_color) {
                        int c = color_array[idx];
                        int r = (c >> 16) & 0xFF;
                        int g = (c >> 8) & 0xFF;
                        int b = c & 0xFF;
                        char esc[24];
                        int n = snprintf(esc, sizeof(esc), "\033[38;2;%d;%d;%dm", r, g, b);
                        buf_append(esc, n);
                        prev_color = color_array[idx];
                    }
                } else if (prev_color != -1) {
                    buf_append("\033[0m", 4);
                    prev_color = -1;
                }
                buf_append_char(ascii_array[idx]);
            }
        }

        buf_flush();
        sleep_ms(50);
    }
}
