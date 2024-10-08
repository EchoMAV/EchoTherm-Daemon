#--------------------------------------------------------------------------------------------------------------------------#
#Project configuration
#--------------------------------------------------------------------------------------------------------------------------#
cmake_minimum_required(VERSION 3.16)
project(echothermd DESCRIPTION "EchoTherm Daemon")


find_package(Boost REQUIRED COMPONENTS system program_options CONFIG) 

#--------------------------------------------------------------------------------------------------------------------------#
#Executable configuration
#--------------------------------------------------------------------------------------------------------------------------#
add_executable(${PROJECT_NAME}
	src/echothermd.cpp
	src/EchoThermCamera.cpp
)

target_compile_features(${PROJECT_NAME} 
	PRIVATE cxx_std_17
)

target_link_libraries(${PROJECT_NAME}
	seekcamera
	Boost::system
	Boost::program_options
)

include(CMakePrintHelpers)
cmake_print_variables(CMAKE_SYSTEM_PROCESSOR)
target_link_directories(${PROJECT_NAME} 
	PRIVATE lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
)

target_include_directories(${PROJECT_NAME} 
	PRIVATE include
)

add_executable(echotherm
	src/echotherm.cpp
)

target_compile_features(echotherm 
	PRIVATE cxx_std_17
)

target_link_libraries(echotherm
	seekcamera
	Boost::system
	Boost::program_options
)


target_link_directories(echotherm
	PRIVATE lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
)

target_include_directories(echotherm 
	PRIVATE include
)


#--------------------------------------------------------------------------------------------------------------------------#
#Install
#--------------------------------------------------------------------------------------------------------------------------#
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(TARGETS echotherm DESTINATION bin)