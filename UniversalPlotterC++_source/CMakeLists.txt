cmake_minimum_required(VERSION 3.16)
project(UniversalPlotter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

qt_standard_project_setup()

add_executable(UniversalPlotter
    third_party/qcustomplot/qcustomplot.cpp
    src/main.cpp
    src/MainWindow.cpp
    src/DataLoader.cpp
    src/CurveFit.cpp
    src/ExprEval.cpp
    src/Plot3DWidget.cpp
    src/Latex3DExportDialog.cpp
    src/Latex3DExportDialog.h
    src/Plot3DWidget.h
    src/DraggableVLine.cpp
    src/LatexExport.cpp
    src/LatexExportDialog.cpp
    src/ColorPickerWidget.cpp
    src/DataEntryDialog.cpp
    src/DataEntryDialog.h
    src/NoWheelComboFilter.h
)

target_include_directories(UniversalPlotter PRIVATE
    third_party/qcustomplot
    src
)

target_link_libraries(UniversalPlotter PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
)

# MinGW's assembler uses the classic COFF object format by default, which
# caps out around 32k sections. A Debug build of qcustomplot.cpp (heavy
# templates + full debug info, no optimization) blows past that limit and
# fails with "too many sections" / "file too big". -mbig-obj switches to
# the extended COFF format that removes that cap.
if (MINGW)
    set_source_files_properties(third_party/qcustomplot/qcustomplot.cpp
        PROPERTIES COMPILE_OPTIONS "-Wa,-mbig-obj")
endif()
