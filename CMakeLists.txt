cmake_minimum_required(VERSION 3.25.0)

project(PGPreamp
    VERSION 0.5.0
    DESCRIPTION "PG Preamp - analog-inspired VST3 preamp and equalizer"
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
    resource/pgpreamp.uidesc
    resource/knob_sheet.png
    resource/rack_panel.png
)

smtg_target_configure_version_file(PGPreamp)
smtg_target_add_plugin_resources(PGPreamp
    RESOURCES
    resource/pgpreamp.uidesc
    resource/knob_sheet.png
    resource/rack_panel.png
)

target_compile_features(PGPreamp PRIVATE cxx_std_17)
target_link_libraries(PGPreamp PRIVATE sdk vstgui_support)
