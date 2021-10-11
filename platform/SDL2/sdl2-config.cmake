set(SDL2_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")

# Support both 32 and 64 bit builds
if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
  set(SDL2_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/lib/x64/SDL2.lib;${CMAKE_CURRENT_LIST_DIR}/lib/x64/SDL2main.lib")
else ()
  set(SDL2_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/lib/x86/SDL2.lib;${CMAKE_CURRENT_LIST_DIR}/lib/x86/SDL2main.lib")
endif ()

string(STRIP "${SDL2_LIBRARY}" SDL2_LIBRARY)
