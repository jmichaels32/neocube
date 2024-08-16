// Argument parsing
#include <argp.h>

// Program Description
const char *argp_program_version = "neocube 1.0";
const char *argp_program_bug_address = "<jmichaels135@gmail.com>";
static char doc[] = "Terminal ASCII shapes";
static char args_doc[] = "";

static struct argp_option options[] = {
    {"width", 'w', "WIDTH", 0, "Specify the width of the cube"},
    {"drawwidth", 'W', "WIDTH", 0, "Specify the width of the drawing box"},
    {"drawheight", 'H', "HEIGHT", 0, "Specify the height of the drawing box"},
    {"cameradist", 'c', "CAMERA", 0, "Specify the object's distance from the camera"},
    {"angle1", 'A', "ANGLE", 0, "Specify the first angle of rotation"},
    {"angle2", 'B', "ANGLE", 0, "Specify the second angle of rotation"},
    {"angle3", 'C', "ANGLE", 0, "Specify the third angle of rotation"},
    {"shape", 's', "SHAPE", 0, "Specify the drawn shape"},
    { 0 }
}

struct arguments {
    float width;
    int drawwidth;
    int drawheight;
    int cameradist;
    float angle1;
    float angle2;
    float angle3;
    char shape[];
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    switch (key) {
    case 'w': arguments->width = 1; break;
    case 'W': arguments->drawwidth = 1; break;
    case 'H': arguments->drawheight = 1; break;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc } ;

// Settings that can change the way the cube appears
float width = 30; // Width of cube 
int draw_width = 100, draw_height = 50; // Size of drawing 
int camera_dist = 100; // Distance from camera (Z dimension)
float increment_speed = 0.6; // How quickly we iterate through the cube width
                            // i.e. sets the resolution of the cube
float A = 0.05, B = 0.05, C = 0.01;

int main(int argc, int *argv[]) {
    for (int i 
}
