# SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(YaST VERSION 24.0.144 LANGUAGES C)

# Default build type for single-config generators only.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif ()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Bundle runtime assets (layouts, flags, icons) into the executable via GResource
pkg_get_variable(GLIB_COMPILE_RESOURCES gio-2.0 glib_compile_resources)
if (NOT GLIB_COMPILE_RESOURCES)
	find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
endif ()

set(RESOURCE_PREFIX "/com/philarmstead/yast")
set(GRESOURCE_XML "${CMAKE_CURRENT_BINARY_DIR}/resources.gresource.xml")
set(GRESOURCE_C "${CMAKE_CURRENT_BINARY_DIR}/resources.c")

file(GLOB LAYOUT_FILES CONFIGURE_DEPENDS RELATIVE "${REPO_ROOT}"
	"${REPO_ROOT}/layouts/*.ui"
	"${REPO_ROOT}/layouts/*.css"
)
file(GLOB ASSET_ICONS CONFIGURE_DEPENDS RELATIVE "${REPO_ROOT}"
	"${REPO_ROOT}/assets/icons/*"
)
file(GLOB ASSET_FLAGS CONFIGURE_DEPENDS RELATIVE "${REPO_ROOT}"
	"${REPO_ROOT}/assets/flags/*.png"
)
set(RESOURCE_FILES ${LAYOUT_FILES} ${ASSET_ICONS} ${ASSET_FLAGS})

# GtkApplication automatically searches "<resource base path>/icons" for themed icons. That path
# is treated as a theme root in its own right, so it takes "<size>/<context>/" directly with no
# "hicolor/" segment, unlike the on-disk theme installed further down.
set(APP_ID "com.philarmstead.yast")
set(APP_ICON_SVG "assets/branding/yast.svg")

set(GRESOURCE_ENTRIES "")
foreach (res_file IN LISTS RESOURCE_FILES)
	# Text assets compress well; PNG flags are already compressed.
	set(compress "")
	if (res_file MATCHES "\\.(ui|css|svg)$")
		set(compress " compressed=\"true\"")
	endif ()
	string(APPEND GRESOURCE_ENTRIES "    <file${compress}>${res_file}</file>\n")
endforeach ()
string(APPEND GRESOURCE_ENTRIES
	"    <file alias=\"icons/scalable/apps/${APP_ID}.svg\" compressed=\"true\">${APP_ICON_SVG}</file>\n")

file(WRITE "${GRESOURCE_XML}"
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<gresources>
  <gresource prefix=\"${RESOURCE_PREFIX}\">
${GRESOURCE_ENTRIES}  </gresource>
</gresources>
")

add_custom_command(
	OUTPUT "${GRESOURCE_C}"
	COMMAND ${GLIB_COMPILE_RESOURCES}
	--target=${GRESOURCE_C}
	--sourcedir=${REPO_ROOT}
	--generate-source
	${GRESOURCE_XML}
	DEPENDS ${GRESOURCE_XML} ${RESOURCE_FILES} ${APP_ICON_SVG}
	WORKING_DIRECTORY ${REPO_ROOT}
	COMMENT "Compiling bundled resources"
	VERBATIM
)

# Windows embeds the icon in the executable itself, which is what Explorer, the taskbar and
# Alt-Tab read. The first icon resource by ID is the one the shell picks up.
if (WIN32)
	enable_language(RC)
	set(WIN_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
	file(WRITE "${WIN_ICON_RC}" "1 ICON \"${REPO_ROOT}/assets/branding/yast.ico\"\n")
else ()
	set(WIN_ICON_RC "")
endif ()

# Exactly one platform backend is compiled. Selecting it here rather than wrapping each file in
# `#if defined(ARCH_*)` means an unimplemented or misnamed entry point is an unresolved-symbol
# error at link time, instead of a translation unit that silently compiles to nothing.
# APPLE must be tested before UNIX, because macOS satisfies both.
if (WIN32)
	set(PLATFORM_SOURCES src/platform/windows.c)
elseif (APPLE)
	set(PLATFORM_SOURCES src/platform/macos.c)
elseif (UNIX)
	set(PLATFORM_SOURCES src/platform/linux.c)
else ()
	message(FATAL_ERROR
		"No platform backend exists for '${CMAKE_SYSTEM_NAME}'. Add one under src/platform/ and "
		"wire it up here."
	)
endif ()

add_executable(yast
	${GRESOURCE_C}
	${WIN_ICON_RC}
	${PLATFORM_SOURCES}
	src/main.c
	src/app/callbacks.c
	src/app/cache.c
	src/app/options.c
	src/app/injury-names.c
	src/app/my-club.c
	src/app/player-snapshot.c
	src/app/player-table.c
	src/app/player.c
	src/app/search.c
	src/app/search-handler.c
	src/app/shortlist.c
	src/app/ui.c
	src/app/callbacks/best-eleven.c
	src/app/callbacks/club-search.c
	src/app/callbacks/filters.c
	src/app/callbacks/nationality-search.c
	src/app/callbacks/player.c
	src/app/helpers/attributes.c
	src/app/helpers/date.c
	src/app/helpers/file-watcher.c
	src/app/helpers/formatter.c
	src/app/helpers/game.c
	src/app/helpers/icons.c
	src/app/helpers/ratings.c
	src/app/helpers/text.c
	src/app/helpers/vector-shared-pointer.c
	src/app/ui/best-eleven.c
	src/app/ui/player-info.c
	src/app/ui/player-list.c
	src/app/ui/settings.c
	src/app/ui/shortlist.c
	src/app/ui/similar-players.c
	src/app/ui/squad-depth.c
	src/app/ui/upgrade-finder.c
	src/core/logger.c
)

# Prevent a console/terminal window in Release builds.
if (WIN32)
	# Windows: link as a GUI subsystem app so no console is allocated.
	target_link_options(yast PRIVATE $<$<CONFIG:Release>:-mwindows>)
elseif (APPLE)
	# macOS resolves an application's icon from its bundle, so a bare executable always falls back
	# to the generic one. Bundling unconditionally therefore keeps the icon working in every
	# configuration, and also stops a Terminal window opening on launch.
	# Note: MACOSX_BUNDLE is read at configure time and does not support generator expressions.
	set_target_properties(yast PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_ICON_FILE "yast"
		MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
		MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_ID}"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
	)
	# Finder and the Dock read the icon from the bundle's Resources directory.
	target_sources(yast PRIVATE "${REPO_ROOT}/assets/branding/yast.icns")
	set_source_files_properties("${REPO_ROOT}/assets/branding/yast.icns"
		PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
	)
endif ()

# Allow including headers using absolute paths relative to the project `src/` directory,
# e.g. #include "platform/platform.h"
target_include_directories(yast PRIVATE
	${PROJECT_SOURCE_DIR}/src
)

# Use the project name for the public executable while keeping a lowercase CMake target.
set_target_properties(yast PROPERTIES
	OUTPUT_NAME "${PROJECT_NAME}"
)

target_compile_features(yast PRIVATE c_std_99)

target_compile_options(yast PRIVATE
	$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
	-Wall
	-Wextra
	-Wpedantic
	-Wshadow
	-Wconversion
	-Wsign-conversion
	-Wstrict-prototypes
	-Wmissing-prototypes
	-Wno-missing-field-initializers
	>
	$<$<C_COMPILER_ID:MSVC>:
	/W4
	>
)

# Link-time optimisation, so the compiler can inline across translation units. Guarded because
# not every toolchain ships a working plugin, and a missing one is a hard error otherwise.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C)
if (IPO_SUPPORTED)
	set_property(TARGET yast PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else ()
	message(STATUS "Link-time optimisation unavailable, continuing without it: ${IPO_ERROR}")
endif ()

# Drop the debug symbol table from Release builds; on MinGW it is the single largest contributor
# to the binary, dwarfing the code itself. Only that table goes, never the dynamic export tables
# GtkBuilder needs to resolve signal handlers by name at runtime (-rdynamic on Linux,
# __declspec(dllexport) via G_MODULE_EXPORT on Windows).
#
# Note this is deliberately not paired with --gc-sections: nothing references those handlers
# statically, so section GC discards them and the UI silently stops responding.
if (APPLE)
	# ld64 has no -s; -x drops the local symbols and leaves the external ones intact.
	target_link_options(yast PRIVATE $<$<CONFIG:Release>:-Wl,-x>)
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_link_options(yast PRIVATE $<$<CONFIG:Release>:-s>)
endif ()

target_compile_definitions(yast PRIVATE
	$<$<PLATFORM_ID:Windows>:ARCH_WIN>
	$<$<PLATFORM_ID:Linux>:ARCH_LINUX>
	$<$<PLATFORM_ID:Darwin>:ARCH_MACOS>
	$<$<CONFIG:Debug>:DEBUG>
	APP_NAME="${PROJECT_NAME}"
	APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
	APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
	APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
	_GNU_SOURCE
)

target_link_libraries(yast PRIVATE PkgConfig::GTK4)

# libm is a separate library on glibc and is not always pulled in transitively by GTK
find_library(MATH_LIBRARY m)
if (MATH_LIBRARY)
	target_link_libraries(yast PRIVATE ${MATH_LIBRARY})
endif ()

# Export runtime symbols on Linux so GtkBuilder can find G_MODULE_EXPORT callbacks
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# -rdynamic makes all symbols available to the dynamic linker for dlsym/lookup
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

	# Linux desktop integration: launchers, window managers and file managers resolve the icon by
	# name out of the hicolor theme, so it has to be installed there as well as bundled in the binary.
	include(GNUInstallDirs)
	install(TARGETS yast RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(FILES "${REPO_ROOT}/assets/branding/${APP_ID}.desktop"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
	)
	install(FILES "${REPO_ROOT}/${APP_ICON_SVG}"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
		RENAME ${APP_ID}.svg
	)
	foreach (size IN ITEMS 16 24 32 48 64 128 256 512)
		install(FILES "${REPO_ROOT}/assets/branding/png/${size}.png"
			DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
			RENAME ${APP_ID}.png
		)
	endforeach ()
endif ()

add_custom_target(info
	COMMAND ${CMAKE_COMMAND} -E echo "Generator: ${CMAKE_GENERATOR}"
	COMMAND ${CMAKE_COMMAND} -E echo "Config: $<CONFIG>"
	COMMAND ${CMAKE_COMMAND} -E echo "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
	COMMAND ${CMAKE_COMMAND} -E echo "Target: $<TARGET_FILE:yast>"
)

add_custom_target(run
	COMMAND $<TARGET_FILE:yast>
	DEPENDS yast
	WORKING_DIRECTORY $<TARGET_FILE_DIR:yast>
	USES_TERMINAL
)

# A Windows build links GTK4 dynamically against the MinGW prefix, so the executable on its own
# only starts on a machine that already has that prefix on PATH. `package` gathers the runtime
# closure into a self-contained directory and zips it for distribution.
if (WIN32)
	pkg_get_variable(GTK4_PREFIX gtk4 prefix)
	pkg_get_variable(GLIB_COMPILE_SCHEMAS gio-2.0 glib_compile_schemas)
	if (NOT GLIB_COMPILE_SCHEMAS)
		find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas)
	endif ()

	# file(GET_RUNTIME_DEPENDENCIES) shells out to objdump to read each PE import table.
	if (NOT CMAKE_OBJDUMP)
		find_program(CMAKE_OBJDUMP NAMES objdump llvm-objdump REQUIRED)
	endif ()

	set(PACKAGE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-windows-x64")
	set(PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}")

	add_custom_target(package
		COMMAND ${CMAKE_COMMAND}
		-D "EXE=$<TARGET_FILE:yast>"
		-D "DEST=${PACKAGE_DIR}"
		-D "GTK_PREFIX=${GTK4_PREFIX}"
		-D "OBJDUMP=${CMAKE_OBJDUMP}"
		-D "GLIB_COMPILE_SCHEMAS=${GLIB_COMPILE_SCHEMAS}"
		-P "${REPO_ROOT}/cmake/package-windows.cmake"
		# Archived from the parent directory so the zip holds "<name>/..." rather than the
		# build machine's absolute path.
		COMMAND ${CMAKE_COMMAND} -E tar cf "${PACKAGE_NAME}.zip" --format=zip -- "${PACKAGE_NAME}"
		WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		DEPENDS yast
		COMMENT "Packaging ${PACKAGE_NAME}.zip"
		VERBATIM
	)

	# The installer is an optional extra on top of the zip, so a missing NSIS only costs that one
	# artefact rather than failing the whole target.
	find_program(MAKENSIS makensis)
	if (MAKENSIS)
		set(APP_NAME "${PROJECT_NAME}")
		set(APP_EXE "${PROJECT_NAME}.exe")
		set(APP_VERSION "${PROJECT_VERSION}")
		# VIProductVersion rejects anything that is not exactly four numeric components.
		set(APP_VERSION_QUAD
			"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
		set(APP_PUBLISHER "Phil Armstead")

		# NSIS needs backslash-separated paths; CMake carries them with forward slashes.
		file(TO_NATIVE_PATH "${PACKAGE_DIR}" APP_STAGE_DIR)
		file(TO_NATIVE_PATH "${PACKAGE_DIR}-setup.exe" APP_OUTFILE)
		file(TO_NATIVE_PATH "${REPO_ROOT}/assets/branding/yast.ico" APP_ICON)
		file(TO_NATIVE_PATH "${REPO_ROOT}/COPYING" APP_LICENSE)

		# @ONLY so that NSIS's own ${...} references survive substitution untouched. Generating a
		# complete script means makensis is invoked with no options at all, which avoids the
		# makensis option prefix differing between its Windows and POSIX builds, shell quoting of
		# values containing spaces, and MSYS2's rewriting of arguments that look like paths.
		set(INSTALLER_NSI "${CMAKE_CURRENT_BINARY_DIR}/installer.nsi")
		configure_file("${REPO_ROOT}/cmake/installer.nsi.in" "${INSTALLER_NSI}" @ONLY)

		add_custom_command(TARGET package POST_BUILD
			COMMAND ${MAKENSIS} "${INSTALLER_NSI}"
			COMMENT "Building ${PACKAGE_NAME}-setup.exe"
			VERBATIM
		)
	else ()
		# Reported at build time rather than only at configure time, where it scrolls past
		# unnoticed and leaves a missing setup.exe looking like a silent failure.
		add_custom_command(TARGET package POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E echo
			"makensis not found: built the zip only, no setup.exe. Install NSIS with"
			"'pacman -S mingw-w64-x86_64-nsis', then re-run cmake to configure."
			VERBATIM
		)
	endif ()
endif ()
