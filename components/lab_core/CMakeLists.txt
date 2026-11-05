idf_component_register(SRCS "core.c" "wire.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
