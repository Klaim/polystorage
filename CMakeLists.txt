cmake_minimum_required( VERSION 3.4 )

find_package( Boost REQUIRED )

add_executable( concept_polymorphism_storage
    main.cpp
)

target_include_directories( concept_polymorphism_storage
    PUBLIC
        ${Boost_INCLUDE_DIRS}
)

