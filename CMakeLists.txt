cmake_minimum_required(VERSION 4.0)
project(MatrixP C)

set(CMAKE_C_STANDARD 23)

add_executable(MatrixP
        main.c
        mp_chunk.h
        mp_page.h
        mp_pool.h
        mp_matrix.h
        mp_chunk.c
        mp_page.c
        mp_pool.c
        mp_matrix.c
)
