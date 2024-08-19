#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include "lib/util.h"

// Settings to change the way the cube animates
float theta_increment = 0.06, phi_increment = 0.03, alpha_increment = 0.02;
float cube_x_velocity = 0.3, cube_y_velocity = 0.3;
int width = 20;
int camera_distance = 100;

// Global variables required for functionality
char *ascii_array;
float *depth_array;
int *color_array;
int screen_width, screen_height;
int x_velocity_factor = 1, y_velocity_factor = 1;
float cube_x = 0, cube_y = 0;
float theta = 0, phi = 0, alpha = 0;

// Return the rotated three dimensional coordinates
float threeD_x(int x, int y, int z) {
    return y * sin(theta) * sin(phi) * cos(alpha) - 
            z * cos(theta) * sin(phi) * cos(alpha) +
            y * cos(theta) * sin(alpha) + 
            z * sin(theta) * sin(alpha) + 
            x * cos(phi) * cos(alpha); 
}

float threeD_y(int x, int y, int z) {
    return y * cos(theta) * cos(alpha) +
            z * sin(theta) * cos(alpha) -
            y * sin(theta) * sin(phi) * sin(alpha) + 
            z * cos(theta) * sin(phi) * sin(alpha) - 
            x * cos(phi) * sin(alpha);
}

float threeD_z(int x, int y, int z) {
    return z * cos(theta) * cos(phi) -
            y * sin(theta) * cos(phi) + 
            x * sin(phi);
}

// Store the projected (2 dimensional) coordinates from three dimensional space
void twoD_points(int x, int y, int z, char ascii, int color) {
    float threeDpoint_x = threeD_x(x, y, z);
    float threeDpoint_y = threeD_y(x, y, z);
    float threeDpoint_z = threeD_z(x, y, z) + camera_distance;
    
    float scaling_factor = 40/threeDpoint_z;
    int twoDpoint_x = (int)(screen_width / 2 + scaling_factor * threeDpoint_x + cube_x);
    int twoDpoint_y = (int)(screen_height / 2 + scaling_factor * threeDpoint_y + cube_y); 

    int index = twoDpoint_x + twoDpoint_y * screen_width;
    // Make sure point is within the viewing area
    if (index >= 0 && index < screen_width * screen_height) {
        // Only replace if position is closer
        if (scaling_factor > depth_array[index]) {
            depth_array[index] = scaling_factor;
            ascii_array[index] = ascii;
            color_array[index] = color;
        }
    }
}

int main() {
    // Parse window size
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    screen_width = w.ws_col, screen_height = w.ws_row - 2;

    // Prepare arrays
    ascii_array = (char*)malloc(screen_width * screen_height * sizeof(char));
    depth_array = (float*)malloc(screen_width * screen_height * sizeof(float));
    color_array = (int*)malloc(screen_width * screen_height * sizeof(int));

    // Make sure malloc allocated correctly
    if (ascii_array == NULL || depth_array == NULL) {
        fprintf(stderr, "Memory allocation failed\n"); 
        return 0;
    }

    // Clear printing area
    printf("\x1b[2J");
   
    // Render loop 
    while (1) {
        // Clear memory
        memset(ascii_array, ' ', screen_width * screen_height);
        memset(depth_array, 0, screen_width * screen_height * 4);
        memset(color_array, 7, screen_width * screen_height * 4);

        //    _
        //  _|2|___
        // |1|3|5|6|
        //  ‾|4|‾‾‾
        //    ‾
        // Calculate iteration of graphics
        for (float x = -width; x < width; x += 0.3) {
            for (float y = -width; y < width; y += 0.3) {
                twoD_points(x, y, -width, '#', 4);   // 1
                twoD_points(x, width, y, '`', 4);    // 2
                twoD_points(-width, y, -x, '*', 6);  // 3
                twoD_points(x, -width, -y, '.', 4);  // 4
                twoD_points(-x, y, width, ';', 4);   // 5
                twoD_points(width, y, x, '-', 6);    // 6
            }
        } 

        // Render graphics
        printf("\x1b[2H");
        printf("\x1b[?25l");
        for (int index = 0; index < screen_width * screen_height; ++index) {
            if (index % screen_width != 0) {
                printf("\033[1;3%dm", color_array[index]);
                putchar(ascii_array[index]);
            } else {
                putchar(10);
            }
        }

        if (cube_x >= ( screen_width - width ) / 2 || cube_x <= - 1 * (( screen_width - width ) / 2)) {
            x_velocity_factor *= -1;
        }
        if (cube_y >= ( screen_height - width ) / 2 || cube_y <= - 1 * (( screen_height - width ) / 2)) {
            y_velocity_factor *= -1;
        }

        theta += theta_increment;
        phi += phi_increment;
        alpha += alpha_increment;
        cube_x += x_velocity_factor * cube_x_velocity;
        cube_y += y_velocity_factor * cube_y_velocity;
        //sleep_ms(30);
    } 

    // Clean memory up
    free(ascii_array);
    free(depth_array);
    free(color_array);
    return 0;
}






