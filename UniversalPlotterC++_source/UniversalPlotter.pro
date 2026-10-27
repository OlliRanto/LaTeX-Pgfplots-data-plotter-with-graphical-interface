QT       += core gui widgets printsupport

TARGET = UniversalPlotter
TEMPLATE = app
CONFIG += c++17

# MinGW's assembler uses the classic COFF object format by default, capped
# at ~32k sections. A Debug build of qcustomplot.cpp (heavy templates, full
# debug info, no optimization) blows past that and fails with "too many
# sections" / "file too big". -mbig-obj switches to extended COFF, which
# removes the cap. Harmless to apply to every file, so it's simplest to set
# it for the whole target rather than just qcustomplot.cpp.
win32-g++ {
    QMAKE_CXXFLAGS += -Wa,-mbig-obj
}

INCLUDEPATH += \
    third_party/qcustomplot \
    src

SOURCES += \
    third_party/qcustomplot/qcustomplot.cpp \
    src/main.cpp \
    src/MainWindow.cpp \
    src/DataLoader.cpp \
    src/CurveFit.cpp \
    src/ExprEval.cpp \
    src/Plot3DWidget.cpp \
    src/Latex3DExportDialog.cpp \
    src/DraggableVLine.cpp \
    src/LatexExport.cpp \
    src/LatexExportDialog.cpp \
    src/ColorPickerWidget.cpp \
    src/DataEntryDialog.cpp

HEADERS += \
    third_party/qcustomplot/qcustomplot.h \
    third_party/qcustomplot/config.h \
    src/MainWindow.h \
    src/Dataset.h \
    src/DataLoader.h \
    src/CurveFit.h \
    src/ExprEval.h \
    src/Plot3DWidget.h \
    src/Latex3DExportDialog.h \
    src/DraggableVLine.h \
    src/LatexExport.h \
    src/LatexExportDialog.h \
    src/LatexColors.h \
    src/ColorSchemes.h \
    src/ColorPickerWidget.h \
    src/DataEntryDialog.h \
    src/NoWheelComboFilter.h

win32: RC_ICONS = resources/plot.ico
