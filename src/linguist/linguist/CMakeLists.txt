# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

#####################################################################
## linguist App:
#####################################################################

qt_internal_add_app(linguist
    SOURCES
        ../shared/numerus.cpp
        ../shared/po.cpp
        ../shared/qm.cpp
        ../shared/qph.cpp
        ../shared/simtexth.cpp ../shared/simtexth.h
        ../shared/translator.cpp ../shared/translator.h
        ../shared/translatormessage.cpp ../shared/translatormessage.h
        ../shared/ts.cpp
        ../shared/xliff.cpp
        ../shared/xmlparser.cpp ../shared/xmlparser.h
        batchtranslation.ui
        batchtranslationdialog.cpp batchtranslationdialog.h
        errorsview.cpp errorsview.h
        finddialog.cpp finddialog.h finddialog.ui
        formpreviewview.cpp formpreviewview.h
        globals.cpp globals.h
        main.cpp
        mainwindow.cpp mainwindow.h mainwindow.ui
        messageeditor.cpp messageeditor.h
        messageeditorwidgets.cpp messageeditorwidgets.h
        messagehighlighter.cpp messagehighlighter.h
        messagemodel.cpp messagemodel.h
        phrase.cpp phrase.h
        phrasebookbox.cpp phrasebookbox.h phrasebookbox.ui
        phrasemodel.cpp phrasemodel.h
        phraseview.cpp phraseview.h
        recentfiles.cpp recentfiles.h
        sourcecodeview.cpp sourcecodeview.h
        statistics.cpp statistics.h statistics.ui
        translatedialog.cpp translatedialog.h translatedialog.ui
        translationsettings.ui
        translationsettingsdialog.cpp translationsettingsdialog.h
    DEFINES
        QFORMINTERNAL_NAMESPACE
        QT_KEYWORDS
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
    INCLUDE_DIRECTORIES
        ../shared
    LIBRARIES
        Qt::CorePrivate
        Qt::Gui
        Qt::GuiPrivate
        Qt::UiToolsPrivate
        Qt::Widgets
)

qt_internal_extend_target(linguist CONDITION QT_FEATURE_printsupport
    SOURCES
        printout.cpp printout.h
    LIBRARIES
        Qt::PrintSupport
)

qt_add_ui(linguist
    SOURCES
        batchtranslation.ui
        finddialog.ui
        mainwindow.ui
        phrasebookbox.ui
        statistics.ui
        translatedialog.ui
        translationsettings.ui
)

# Resources:
set(linguist_resource_files
    "images/appicon.png"
    "images/down.png"
    "images/editdelete.png"
    "images/icons/linguist-128-32.png"
    "images/mac/accelerator.png"
    "images/mac/book.png"
    "images/mac/done.png"
    "images/mac/doneandnext.png"
    "images/mac/editcopy.png"
    "images/mac/editcut.png"
    "images/mac/editpaste.png"
    "images/mac/fileopen.png"
    "images/mac/filesave.png"
    "images/mac/next.png"
    "images/mac/nextunfinished.png"
    "images/mac/phrase.png"
    "images/mac/prev.png"
    "images/mac/prevunfinished.png"
    "images/mac/print.png"
    "images/mac/punctuation.png"
    "images/mac/redo.png"
    "images/mac/searchfind.png"
    "images/mac/surroundingwhitespace.png"
    "images/mac/undo.png"
    "images/mac/validateplacemarkers.png"
    "images/mac/whatsthis.png"
    "images/minus.png"
    "images/plus.png"
    "images/up.png"
    "images/win/accelerator.png"
    "images/win/book.png"
    "images/win/done.png"
    "images/win/doneandnext.png"
    "images/win/editcopy.png"
    "images/win/editcut.png"
    "images/win/editpaste.png"
    "images/win/fileopen.png"
    "images/win/filesave.png"
    "images/win/next.png"
    "images/win/nextunfinished.png"
    "images/win/phrase.png"
    "images/win/prev.png"
    "images/win/prevunfinished.png"
    "images/win/print.png"
    "images/win/punctuation.png"
    "images/win/redo.png"
    "images/win/searchfind.png"
    "images/win/surroundingwhitespace.png"
    "images/win/undo.png"
    "images/win/validateplacemarkers.png"
    "images/win/whatsthis.png"
)

qt_internal_add_resource(linguist "linguist"
    PREFIX
        "/"
    FILES
        ${linguist_resource_files}
)

set_target_properties(linguist PROPERTIES
    QT_TARGET_DESCRIPTION "Qt Linguist"
)

file(GLOB phrasebooks_files "${CMAKE_CURRENT_SOURCE_DIR}/../phrasebooks/*")
set(phrasebooks_install_dir "${INSTALL_DATADIR}/phrasebooks")
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.31")
    cmake_path(SET phrasebooks_install_dir NORMALIZE "${phrasebooks_install_dir}")
endif()
qt_install(FILES ${phrasebooks_files} DESTINATION "${phrasebooks_install_dir}")

if(WIN32)
    set_target_properties(linguist PROPERTIES
        QT_TARGET_RC_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/linguist.ico"
    )
endif()

if(WIN32)
    set_target_properties(linguist PROPERTIES
        QT_TARGET_VERSION "${PROJECT_VERSION}.0"
    )
endif()

if(UNIX)
    set_target_properties(linguist PROPERTIES
        QT_TARGET_VERSION "${PROJECT_VERSION}"
    )
endif()

if(QT_INSTALL_XDG_DESKTOP_ENTRIES)
    if(UNIX AND NOT APPLE)
        qt_path_join(xdg_install_dir ${QT_INSTALL_DIR} ${CMAKE_INSTALL_DATAROOTDIR})

        qt_install(FILES linguist.desktop
            DESTINATION "${xdg_install_dir}/applications"
        )
        qt_install(FILES linguist.metainfo.xml
            RENAME io.qt.Linguist.metainfo.xml
            DESTINATION "${xdg_install_dir}/metainfo"
        )
        qt_install(FILES images/icons/linguist-128-32.png
            RENAME linguist.png
            DESTINATION "${xdg_install_dir}/icons/hicolor/128x128/apps"
        )
    endif()
endif()

if(APPLE)
    set_target_properties(linguist PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info_mac.plist"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "linguist.icns"
        OUTPUT_NAME "Linguist"
    )
    set_source_files_properties(linguist.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(linguist PRIVATE
        linguist.icns
    )
else()
    set_property(SOURCE main.cpp PROPERTY SKIP_AUTOMOC ON)
endif()
qt_internal_add_docs(linguist
    doc/qtlinguist.qdocconf
)

