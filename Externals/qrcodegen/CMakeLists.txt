# Nayuki's QR Code generator (C, MIT), for the Lylat QR sign-in dialog. A library of its own,
# added before the root CMakeLists.txt sets -std=c++1y for every later directory: Apple's clang
# refuses a C++ standard flag on a C file.
add_library(qrcodegen STATIC qrcodegen.c)
target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
