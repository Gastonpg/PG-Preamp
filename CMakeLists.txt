cmake_minimum_required(VERSION 3.25.0)

project(PGPreamp
    VERSION 0.1.0
    DESCRIPTION "PG Preamp - minimal VST3 audio effect"
)

add_subdirectory(external)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(PGPreamp
    source/version.h
    source/pgpreampcids.h
    source/pgpreampprocessor.h
    source/pgpreampprocessor.cpp
    source/pgpreampcontroller.h
    source/pgpreampcontroller.cpp
    source/pgpreampentry.cpp
)

smtg_target_configure_version_file(PGPreamp)

target_compile_features(PGPreamp PRIVATE cxx_std_17)
target_link_libraries(PGPreamp PRIVATE sdk)
