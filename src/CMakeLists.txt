include(${QT_USE_FILE})

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

QT4_ADD_RESOURCES ( MIRALL_RC_SRC ../mirall.qrc)

set(mirall_UI
mirall/folderwizardsourcepage.ui
mirall/folderwizardtargetpage.ui
mirall/folderwizardnetworkpage.ui
mirall/folderwizardowncloudpage.ui
mirall/statusdialog.ui
mirall/owncloudwizardselecttypepage.ui
mirall/createanowncloudpage.ui
mirall/owncloudftpaccesspage.ui
mirall/owncloudwizardresultpage.ui
mirall/owncloudcredentialspage.ui
mirall/sslerrorsdialog.ui
)

qt4_wrap_ui(mirall_UI_SRCS ${mirall_UI})

set(mirall_SRCS
mirall/application.cpp
mirall/fileutils.cpp
mirall/folder.cpp
mirall/folderwatcher.cpp
mirall/folderwizard.cpp
mirall/gitfolder.cpp
mirall/networklocation.cpp
mirall/temporarydir.cpp
mirall/syncresult.cpp
mirall/unisonfolder.cpp
mirall/statusdialog.cpp
mirall/owncloudwizard.cpp
mirall/owncloudsetupwizard.cpp
mirall/owncloudinfo.cpp
mirall/theme.cpp
mirall/miralltheme.cpp
mirall/owncloudtheme.cpp
mirall/folderman.cpp
mirall/mirallconfigfile.cpp
mirall/updatedetector.cpp
mirall/occinfo.cpp
mirall/sslerrordialog.cpp

)

set(mirall_HEADERS
    mirall/application.h
    mirall/folder.h
    mirall/folderman.h
    mirall/folderwatcher.h
    mirall/folderwizard.h
    mirall/gitfolder.h
    mirall/owncloudfolder.h
    mirall/owncloudinfo.h
    mirall/owncloudsetupwizard.h
    mirall/owncloudwizard.h
    mirall/statusdialog.h
    mirall/unisonfolder.h
    mirall/theme.h
    mirall/csyncthread.h
    mirall/updatedetector.h
    mirall/sslerrordialog.h
)

if( UNIX AND NOT APPLE)
    if(NOT USE_INOTIFY)
        set(USE_INOTIFY ON)
    endif()
endif()

IF( USE_INOTIFY )
    add_definitions( -DUSE_INOTIFY )
    set(mirall_SRCS ${mirall_SRCS} mirall/inotify.cpp)
    set(mirall_HEADERS ${mirall_HEADERS} mirall/inotify.h)
ENDIF(UNIX)

# Disabled the csync found check. Csync required for now.
set(mirall_SRCS
    ${mirall_SRCS}
    mirall/csyncfolder.cpp
    mirall/owncloudfolder.cpp
    mirall/csyncthread.cpp
  )
include_directories(${CSYNC_INCLUDE_DIR}/csync ${CSYNC_INCLUDE_DIR})

set(mirall_HEADERS
    ${mirall_HEADERS}
    mirall/csyncfolder.h
    mirall/owncloudfolder.h)

qt4_wrap_cpp(mirallMoc ${mirall_HEADERS})

qt4_add_translation(mirall_I18N ${TRANSLATIONS})

add_library(mirall_static STATIC ${mirall_SRCS} ${mirall_UI_SRCS} ${mirallMoc} ${mirall_HEADERS} ${MIRALL_RC_SRC} ${mirall_I18N})
target_link_libraries(mirall_static ${QT_LIBRARIES})

add_executable( mirall main.cpp )
set( final_src ${mirall_HEADERS}
	${MIRALL_RC_SRC} ${mirall_SRCS} ${mirall_UI_SRCS} ${mirallMoc} ${mirall_HEADERS} ${mirall_I18N})

# add executable icon on windows and osx
include( AddAppIconMacro )
kde4_add_app_icon( final_src "${CMAKE_CURRENT_SOURCE_DIR}/../resources/mirall-*.png" )

add_executable(owncloud WIN32 main.cpp ${final_src})
# add_executable(owncloud  main.cpp ${final_src})

SET(BIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
SET_TARGET_PROPERTIES( mirall PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY  ${BIN_OUTPUT_DIRECTORY} )
SET_TARGET_PROPERTIES( owncloud PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY  ${BIN_OUTPUT_DIRECTORY} )

target_link_libraries(mirall mirall_static)

target_link_libraries(owncloud ${QT_LIBRARIES} )

target_link_libraries(mirall ${CSYNC_LIBRARY})
target_link_libraries(owncloud ${CSYNC_LIBRARY})

install(TARGETS mirall
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib )
install(TARGETS owncloud
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib )

install(FILES mirall.png DESTINATION share/icons/hicolor/48x48/apps
)

