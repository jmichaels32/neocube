#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include "lib/util.h"

// Settings to change the way the cube animates
float theta_increment = 0.03, phi_increment = 0.03, alpha_increment = 0.02;
float cube_x = 1, cube_y = 0;
int width = 20;
int camera_distance = 100;

// Global variables required for functionality
char *ascii_array;
float *depth_array;
int screen_width, screen_height;
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
void twoD_points(int x, int y, int z, char ascii) {
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

        // Calculate iteration of graphics
        for (float x = -width; x < width; x += 0.3) {
            for (float y = -width; y < width; y += 0.3) {
                twoD_points(x, y, -width, '#');
                twoD_points(width, y, x, '-');
                twoD_points(-width, y, -x, '/');
                twoD_points(-x, y, width, ';');
                twoD_points(x, -width, -y, '%');
                twoD_points(x, width, y, '?');
            }
        } 

        // Render graphics
        printf("\x1b[2H");
        for (int index = 0; index < screen_width * screen_height; ++index) {
            if (index % screen_width != 0) {
                putchar(ascii_array[index]);
            } else {
                putchar(10);
            }
        }

        theta += theta_increment;
        phi += phi_increment;
        alpha += alpha_increment;
        //sleep_ms(30);
    } 

    // Clean memory up
    free(ascii_array);
    free(depth_array);
    return 0;
}






