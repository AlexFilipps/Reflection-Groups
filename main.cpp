#include <iostream>
#include <cmath>
#include <vector>

// GLEW
#define GLEW_STATIC
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

//GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//shader header
#include "shader.h"
#include "compute_shader.h"

//CALLBACK FUNCTIONS
void error_callback(int, const char*);
void window_close_callback(GLFWwindow*);
void key_callback(GLFWwindow*, int, int, int, int);

//helper functions for data generation
void generate_next_locations(int*, int);


//window size
const unsigned int window_width = 800;
const unsigned int window_height = 800;


//display settings
const bool DISPLAY_MIRRORS = true;
const unsigned int DISPLAY_MODE = 0;
//options for display mode: 
//0: displays reflected points
//1: displays reflected points with a trailing effect when the cursor is moved
//2: displays a density map of the reflected points


//definitions for the mirrors (defined as slope and y-offset pairs)
//these definitions are only used for the displaying of the mirrors if DISPLAY_MIRRORS is set to true. The defining of the mirrors used in actual reflection calculations is done later.

//NOTE: these definitions should match the ones used for calculations used later 
const float mirrors[] = {
	1.0f, 0.0f,		//ex y=1x+0
	-2.0f, 0.0f,
	0.0f, 0.5f
};

//total number of mirrors (maximum of 8 supported)
const unsigned int num_mirrors = sizeof(mirrors) / sizeof(mirrors[0]) / 2;

//max number of reflections a point can go through
const int max_depth = 12;

//define some vertices and indices which will be used to display fully rendered textures to our window
float window_vertices[] = {
	1.0f,  1.0f, 0.0f,		1.0f, 1.0f,   // top right
	1.0f, -1.0f, 0.0f,		1.0f, 0.0f,   // bottom right
	-1.0f, -1.0f, 0.0f,		0.0f, 0.0f,   // bottom left
	-1.0f,  1.0f, 0.0f,		0.0f, 1.0f    // top left 
};
unsigned int window_indices[] = {
		0, 1, 3, // first triangle
		1, 2, 3  // second triangle
};





int main() {
	//---------------------------------------------------------------------------------------------------
	//GLFW AND GLEW INITILIZATION. THIS INCLUDES SETTING UP OUR WINDOW AND LINKING CALLBACK FUNCTIONS.
	//---------------------------------------------------------------------------------------------------
	//link our error callback function
	glfwSetErrorCallback(error_callback);

	//initialize glfw
	if (!glfwInit()) {
		std::cout << "ERROR: Could not initialize glfw!\n";
	}

	//create the window
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Window", NULL, NULL);

	//link the window callback functions
	glfwSetWindowCloseCallback(window, window_close_callback);
	glfwSetKeyCallback(window, key_callback);

	//set our window as current
	glfwMakeContextCurrent(window);

	//initiaize GLEW (must be done after we have set a current context)
	if (glewInit() != GLEW_OK) {
		std::cout << "ERROR: Could not initialize glew!\n";
	}

	//set up the GL viewport
	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	glViewport(0, 0, w, h);

	//set our update interval (argument is number of frames per update)
	glfwSwapInterval(1);


	//---------------------------------------------------------------------------------------------------
	//SHADER PROGRAM CREATION
	//---------------------------------------------------------------------------------------------------
	//texture_shader is used when displaying rendered textures to the window
	Shader texture_shader("texture_vert_shader.vs", "texture_frag_shader.fs");

	//line_shader is used when DISPLAY_MIRRORS is set to true and is responsible for drawing mirrors to the window
	Shader line_shader("line_vert_shader.vs", "line_frag_shader.fs");

	//the main compute shader used to compute reflected points
	ComputeShader refl_shader_com("reflection_solver.computes");

	//compute shader used in texture calculations when display mode is set to 0
	ComputeShader point_shader("point_solver.computes");

	//compute shader used in texture calculations when display mode is set to 1
	ComputeShader trail_shader("trail_solver.computes");

	//compute shader used in texture calculations when display mode is set to 2
	ComputeShader density_shader("density_solver.computes");



	//---------------------------------------------------------------------------------------------------
	//DATA GENERATION
	//---------------------------------------------------------------------------------------------------
	//using our mirrors defined above, generate the points used to display it:
	
	//the array used to hold our mirror points
	float lines[num_mirrors * 4];

	for (int i = 0; i < num_mirrors; i++) {
		lines[i * 4] = 1.0f;
		lines[i * 4 + 1] = mirrors[i * 2] * 1 + mirrors[i * 2 + 1];
		lines[i * 4 + 2] = -1.0f;
		lines[i * 4 + 3] = mirrors[i * 2] * -1 + mirrors[i * 2 + 1];
	}

	//Generate a list of numbers corresponding to the locations of points in our final image:

	/*
	Each number corresponds to the reflections a point undergos before reaching its final location. To get the reflections defined by the point's location number, 
	convert the number to base 'b' where b is the number of mirrors in our scene. Each digit in the number then corresponds to a reflection across its associated mirror.
	EX. A location number 5 with two mirrors in the scene would give us the binary number 101 meaning we reflect across mirror 1 then mirror 0 and then mirror 1 again.

	When generating location numbers we skip those that result in a sequence of 2 of the same digits as a reflection done twice in a row results in the original starting location.
	Location numbers which are negative imply one extra reflection across the 0 mirror at the end.
	*/

	unsigned int total_points;
	if (num_mirrors == 2) {
		total_points = (2 * max_depth) + 1;
	}
	else {
		total_points = (num_mirrors * ((pow(num_mirrors - 1, max_depth) - 1) / (num_mirrors - 2))) + 1;
	}
	//std::cout << "total points: " << total_points << std::endl;

	int *vertices = new int[total_points];
	
	//generate base point and single reflections
	vertices[0] = num_mirrors + 1;
	for (int i = 0; i < num_mirrors; i++) {
		vertices[i + 1] = i;
	}

	//generate all reflections with depth greater than one, up to the set max_depth
	for (int i = 0; i < max_depth - 1; i++) {
		generate_next_locations(vertices, i+1);
	}
	//std::cout << sizeof(vertices) << std::endl;



	//compute work groups for our reflection compute shaders
	//this should generate valid group sizes regardless of hardware up to around 2^25 points
	int group_x = 1;
	int group_y = 1;
	int group_z = 1;

	int group_index = 0;
	while ((group_x * group_y * group_z * 128) < total_points) {
		if ((group_index % 3) == 0) {
			group_x *= 2;
		}
		else if ((group_index % 3) == 1) {
			group_y *= 2;
		}
		else if ((group_index % 3) == 2) {
			group_z *= 2;
		}

		group_index++;
	}



	//---------------------------------------------------------------------------------------------------
	//VBO AND VAO SETUP
	//---------------------------------------------------------------------------------------------------
	//generate vbos and store their ids
	unsigned int VBO_lines;
	glGenBuffers(1, &VBO_lines);
	unsigned int VBO_texture;
	glGenBuffers(1, &VBO_texture);

	//generate vaos and store their ids
	unsigned int VAO_lines;
	glGenVertexArrays(1, &VAO_lines);
	unsigned int VAO_texture;
	glGenVertexArrays(1, &VAO_texture);

	//generate ebos and store their ids
	unsigned int EBO_texture;
	glGenBuffers(1, &EBO_texture);

	//bind the line vao and assign the line data to the line vbo
	glBindVertexArray(VAO_lines);
	glBindBuffer(GL_ARRAY_BUFFER, VBO_lines);
	glBufferData(GL_ARRAY_BUFFER, sizeof(lines), lines, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	//bind the texture vao and assign the some basic data to the texture vbo and ebo. This data is always the same and is just used for drawing a computed texture to the window
	glBindVertexArray(VAO_texture);
	glBindBuffer(GL_ARRAY_BUFFER, VBO_texture);
	glBufferData(GL_ARRAY_BUFFER, sizeof(window_vertices), window_vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_texture);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(window_indices), window_indices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);




	//---------------------------------------------------------------------------------------------------
	//SSBO AND TEXTURE SETUP
	//---------------------------------------------------------------------------------------------------

	//We use a couple of SSBOs for the purpose of sending data to and between our different compute shaders. 
	//We also initialize a texture which our final image can be saved to after gpu computation is done.

	//reflection_indices is used to store the data generated in the vertices array. It is used as input for the first stage of our compute shader.
	GLuint reflection_indices;
	glGenBuffers(1, &reflection_indices);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, reflection_indices);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int) * total_points, vertices, GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, reflection_indices);

	//points represents a 2-d grid of pixels where reflected points land. It is an itermediate step in our computation for our final output image.
	GLuint points;
	glGenBuffers(1, &points);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, points);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(unsigned int) * window_height * window_width, nullptr, GL_DYNAMIC_COPY);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points);

	//output_texture is where our final image is constructed before being displayed to the window. It is used to get the final output of our compute shaders.
	GLuint output_texture;
	glGenTextures(1, &output_texture);
	glBindTexture(GL_TEXTURE_2D, output_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	float borderColor[] = { 1.0f, 1.0f, 0.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, output_texture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_FLOAT, NULL);
	glBindImageTexture(0, output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);



	//---------------------------------------------------------------------------------------------------
	//SHADER UNIFORM SETUP
	//---------------------------------------------------------------------------------------------------
	//Here we set up a lot of the preliminary data that our shaders use.

	//norm_vecs is how the shaders are able to calculate reflection. 
	//Each vector represents one mirror, where the x and y are the 2-d normal vector to our mirror, and z is our mirrors y-offset.
	//NOTE: A maximum of 8 mirrors are supported, and the data here should match-up with the mirrors defined earlier if DISPLAY_MIRRORS is true.
	glm::vec3 norm_vecs[] = {
		glm::vec3(0.7071068f, -0.7071068f, 0.0f),
		glm::vec3(-0.8944272f, -0.4472136f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.5f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 0.0f)

	};


	//UNIFORM SETTING
	//-----------------------------------------------------------------------------------------------------------------------------------------------------------
	glProgramUniform1ui(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "display_mode"), DISPLAY_MODE);
	glProgramUniform1ui(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "total_points"), total_points);
	glProgramUniform1ui(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "num_mirrors"), num_mirrors);
	glProgramUniform1ui(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "window_width"), window_width);
	glProgramUniform1ui(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "window_height"), window_height);
	glProgramUniform3fv(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "norm_vecs"), 8, glm::value_ptr(norm_vecs[0]));

	glProgramUniform1ui(density_shader.programID, glGetUniformLocation(density_shader.programID, "window_width"), window_width);
	glProgramUniform1ui(density_shader.programID, glGetUniformLocation(density_shader.programID, "window_height"), window_height);

	glProgramUniform1ui(trail_shader.programID, glGetUniformLocation(trail_shader.programID, "window_width"), window_width);
	glProgramUniform1ui(trail_shader.programID, glGetUniformLocation(trail_shader.programID, "window_height"), window_height);

	glProgramUniform1ui(point_shader.programID, glGetUniformLocation(point_shader.programID, "window_width"), window_width);
	glProgramUniform1ui(point_shader.programID, glGetUniformLocation(point_shader.programID, "window_height"), window_height);
	//-----------------------------------------------------------------------------------------------------------------------------------------------------------




	//used to store cursor position
	double xpos, ypos;

	//zero out the point SSBO
	GLuint zero = 0;
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED, GL_UNSIGNED_INT, &zero);

	//---------------------------------------------------------------------------------------------------
	//MAIN PROGRAM LOOP
	//---------------------------------------------------------------------------------------------------
	while (!glfwWindowShouldClose(window)) {
	
		//decide which shader program to use based on DISPLAY_MODE, and dispatch our compute shaders
		if (DISPLAY_MODE == 0) { //point shader
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points);
			glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED, GL_UNSIGNED_INT, &zero);

			refl_shader_com.use();
			glDispatchCompute(group_x, group_y, group_z);

			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

			point_shader.use();
			glDispatchCompute(window_width, window_height, 1);

			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
		}
		else if (DISPLAY_MODE == 1) { // trail shader
			refl_shader_com.use();
			glDispatchCompute(group_x, group_y, group_z);

			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

			trail_shader.use();
			glDispatchCompute(window_width, window_height, 1);

			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
		}
		else if (DISPLAY_MODE == 2) { //density shader
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points);
			glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED, GL_UNSIGNED_INT, &zero);

			refl_shader_com.use();
			glDispatchCompute(group_x, group_y, group_z);

			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

			density_shader.use();
			glDispatchCompute(window_width, window_height, 1);

			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

		}






		//display the computed image to the window
		texture_shader.use();
		glBindTexture(GL_TEXTURE_2D, output_texture);
		glBindVertexArray(VAO_texture);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		//display our mirrors as lines if DISPLAY_MIRRORS is enabled
		if (DISPLAY_MIRRORS) {
			line_shader.use();
			glBindVertexArray(VAO_lines);
			glDrawArrays(GL_LINES, 0, (sizeof(lines) / sizeof(float)) / 2);
		}



		//get the cursor position and set our initial point to be its location in the window
		if (glfwGetWindowAttrib(window, GLFW_HOVERED)) {
			glfwGetCursorPos(window, &xpos, &ypos);
			//std::cout << "position: (" << (xpos - window_width / 2) / (window_width / 2) << ", " << ((window_height - ypos) - window_height / 2) / (window_height / 2) << ")\n";
			glProgramUniform2f(refl_shader_com.programID, glGetUniformLocation(refl_shader_com.programID, "cursor"), (xpos - window_width / 2) / (window_width / 2), ((window_height - ypos) - window_height / 2) / (window_height / 2));
		}


		//check for events which occured since the last update
		glfwPollEvents();
		//swap our frame buffers
		glfwSwapBuffers(window);
	}

	delete[] vertices;

	//std::vector<int> storage(total_points);
	//std::vector<int> storage(window_width * window_height);

	//glGetNamedBufferSubData(reflection_indices, 0, sizeof(int) * total_points, storage.data());
	//glGetNamedBufferSubData(points, 0, sizeof(unsigned int) * window_width * window_height, storage.data());

	//for (int i = 0; i < window_width * window_height; i++) {
	//	if (storage[i] != 0) {
	//		std::cout << "non zero found at index: " << i << " and it was: " << storage[i] << std::endl;
	//		break;
	//	}
	//	//std::cout << storage[i] << std::endl;
	//}
	//std::cout << storage[total_points - 1] << std::endl;



	//termintate glfw and exit
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}


//This function is used to generate the values of the vertices array.
//Specifications on what data it generates can be found before its call from main.
void generate_next_locations(int *locations, int depth) {

	int oldPointOffset;
	int newPointOffset;
	if (num_mirrors == 2) {
		oldPointOffset = (2 * (depth - 1)) + 1;
		newPointOffset = (2 * depth) + 1;
	}
	else {
		oldPointOffset = (num_mirrors * ((pow(num_mirrors - 1, depth - 1) - 1) / (num_mirrors - 2))) + 1;
		newPointOffset = (num_mirrors * ((pow(num_mirrors - 1, depth) - 1) / (num_mirrors - 2))) + 1;
	}1;


	int lastRefl;
	for (int i = 0; i < num_mirrors * pow(num_mirrors - 1, depth - 1); i++) {

		if (locations[i + oldPointOffset] < 0) {
			lastRefl = 0;
		}
		else {
			lastRefl = (int)locations[i + oldPointOffset] / pow(num_mirrors, depth - 1);
		}

		for (int j = 0; j < num_mirrors; j++) {

			if (j != lastRefl) {
				if (j == 0) {
					locations[newPointOffset] = locations[i + oldPointOffset] * -1;
				}
				else {
					if (locations[i + oldPointOffset] < 0) {
						locations[newPointOffset] = (locations[i + oldPointOffset] * -1) + (pow(num_mirrors, depth) * j);
					}
					else {
						locations[newPointOffset] = locations[i + oldPointOffset] + (pow(num_mirrors, depth) * j);
					}
				}
				newPointOffset++;
			}

		}
	}
}






//basic error logging
void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

//callback to handle window closing
void window_close_callback(GLFWwindow* window) {
	std::cout << "Window will now close.\n";
}

//callback to handle key presses
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		window_close_callback(window);
	}
}
