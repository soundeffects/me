# Compilation for macOS
shadow_mapping: shadow_mapping.cpp
	# Using homebrew to install headers for GLUT and GLEW. Using homebrew's library for GLUT,
	# while substituting Apple's pre-installed GLUT framework instead of homebrew's library
	# for GLUT (which seems to be broken). FWe include files from a local install of 
	# cyCodeBase and the c++ std library, because cyCodeBase depends on it. Finally, we use
	# Apple's pre-installed OpenGL framework.
	gcc -std=c++11 \
	-I /opt/homebrew/Cellar/glfw/3.4/include -L /opt/homebrew/Cellar/glfw/3.4/lib -l GLFW \
	-I /opt/homebrew/Cellar/glew/2.2.0_1/include -L /opt/homebrew/Cellar/glew/2.2.0_1/lib -l GLEW \
	-I ../cyCodeBase/ \
	-framework OpenGL \
	-lc++ \
	shadow_mapping.cpp -o shadow_mapping
