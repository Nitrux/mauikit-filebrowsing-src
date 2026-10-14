project('mauikit-filebrowsing', 'cpp', version: '4.0.5', meson_version: '>=1.0.0', default_options: ['cpp_std=c++17', 'buildtype=release'])

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.get_id() == 'gcc' and cpp_compiler.has_argument('-Wno-sfinae-incomplete')
    add_project_arguments('-Wno-sfinae-incomplete', language: 'cpp')
endif
qt6 = import('qt6')
qt = dependency('qt6', version: '>=6.4', modules: ['Core', 'DBus', 'Quick', 'Qml', 'Sql', 'Xml', 'Concurrent', 'Network', 'Widgets'])
kf6_include_prefix = get_option('prefix') / 'include' / 'KF6'
kf6_system_include = get_option('prefix') / 'include'
kf6_core = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KCoreAddons'),
    dependencies: [cpp_compiler.find_library('KF6CoreAddons')],
)
kf6_i18n = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KI18n'),
    dependencies: [cpp_compiler.find_library('KF6I18n')],
)
mauikit = dependency('MauiKit4', version: '>=4.0.5')
kio_core = cpp_compiler.find_library('KF6KIOCore', required: false)
kio_filewidgets = cpp_compiler.find_library('KF6KIOFileWidgets', required: false)
kio_gui = cpp_compiler.find_library('KF6KIOGui', required: false)
kconfig_core = cpp_compiler.find_library('KF6ConfigCore', required: false)
solid = cpp_compiler.find_library('KF6Solid', required: false)
kbookmarks = cpp_compiler.find_library('KF6Bookmarks', required: false)
kio_found = kio_core.found() and kio_filewidgets.found() and kio_gui.found() and kbookmarks.found() and kconfig_core.found() and solid.found()
kio_include_dirs = include_directories(
    kf6_system_include,
    kf6_include_prefix / 'KCompletion',
    kf6_include_prefix / 'KConfig',
    kf6_include_prefix / 'KConfigCore',
    kf6_include_prefix / 'KConfigGui',
    kf6_include_prefix / 'KIO',
    kf6_include_prefix / 'KIOCore',
    kf6_include_prefix / 'KIOGui',
    kf6_include_prefix / 'KIOWidgets',
    kf6_include_prefix / 'KIOFileWidgets',
    kf6_include_prefix / 'KBookmarks',
    kf6_include_prefix / 'KJobWidgets',
    kf6_include_prefix / 'KService',
    kf6_include_prefix / 'KWidgetsAddons',
    kf6_include_prefix / 'KWindowSystem',
    kf6_include_prefix / 'Solid',
)
cpp_args = host_machine.cpu_family() == 'x86_64' ? ['-march=x86-64-v3'] : []
deps = [qt, kf6_core, kf6_i18n, mauikit]
moc_args = []
if kio_found
    deps += [declare_dependency(include_directories: kio_include_dirs, dependencies: [kio_core, kio_filewidgets, kio_gui, kbookmarks, kconfig_core, solid])]
    cpp_args += ['-DKIO_AVAILABLE']
    cpp_args += ['-Wno-deprecated-declarations']
    moc_args += ['-DKIO_AVAILABLE']
endif
filebrowsing_export_header = configure_file(
    input: 'meson-content.in',
    output: 'filebrowsing_export.h',
    configuration: {'CONTENT': ('#pragma once\n#define FILEBROWSING_EXPORT __attribute__((visibility("default")))\n')},
)
filebrowsing_version_header = configure_file(
    input: 'meson-content.in',
    output: 'filebrowsing_version.h',
    configuration: {'CONTENT': ('#pragma once\n#define FileBrowsing_VERSION_STRING "' + meson.project_version() + '"\n#define GIT_BRANCH "Stable"\n#define GIT_COMMIT_HASH "' + meson.project_version() + '"\n')},
)
filebrowsing_moc = qt6.preprocess(moc_headers: ['src/code/fm.h', 'src/code/fileoperation.h', 'src/code/fmstatic.h', 'src/code/fmlist.h', 'src/code/placeslist.h', 'src/code/downloader.h', 'src/code/fileloader.h', 'src/code/tagging.h', 'src/code/tagdb.h', 'src/code/syncing.h', 'src/code/libwebdavclient/lib/WebDAVClient.hpp', 'src/code/libwebdavclient/lib/utils/NetworkHelper.hpp', 'src/code/libwebdavclient/lib/utils/WebDAVReply.hpp'], moc_extra_arguments: moc_args, dependencies: [qt])
lib = shared_library('MauiKitFileBrowsing4', [filebrowsing_export_header, filebrowsing_version_header] + files('src/code/fm.cpp', 'src/code/fileoperation.cpp', 'src/code/fmstatic.cpp', 'src/code/fmlist.cpp', 'src/code/placeslist.cpp', 'src/code/downloader.cpp', 'src/code/fileloader.cpp', 'src/code/tagging.cpp', 'src/code/tagdb.cpp', 'src/code/moduleinfo.cpp', 'src/code/syncing.cpp', 'src/code/libwebdavclient/lib/WebDAVClient.cpp', 'src/code/libwebdavclient/lib/dto/WebDAVItem.cpp', 'src/code/libwebdavclient/lib/utils/Environment.cpp', 'src/code/libwebdavclient/lib/utils/NetworkHelper.cpp', 'src/code/libwebdavclient/lib/utils/WebDAVReply.cpp', 'src/code/libwebdavclient/lib/utils/XMLHelper.cpp') + filebrowsing_moc + qt6.preprocess(qresources: ['src/assets.qrc', 'src/code/tagging.qrc']), dependencies: deps, include_directories: include_directories('src/code', 'src/code/libwebdavclient/lib', 'src/code/libwebdavclient/lib/utils', 'src/code/libwebdavclient/lib/dto'), cpp_args: cpp_args, version: '4.0.5', soversion: '4', install: true)
plugin = shared_module('MauiKitFileBrowsing4plugin', ['src/code/filebrowsing_plugin.cpp', 'src/code/thumbnailer.cpp', 'src/code/tagslist.cpp', 'src/code/openwithmodel.cpp'] + qt6.preprocess(moc_headers: ['src/code/filebrowsing_plugin.h', 'src/code/tagslist.h', 'src/code/openwithmodel.h'], moc_extra_arguments: moc_args, dependencies: [qt]), link_with: lib, dependencies: deps, include_directories: include_directories('src/code'), cpp_args: cpp_args, install: true, install_dir: get_option('libdir') / 'qt6/qml/org/mauikit/filebrowsing')
qml_dir = get_option('libdir') / 'qt6/qml/org/mauikit/filebrowsing'
install_data(configure_file(
    input: 'meson-content.in',
    output: 'qmldir',
    configuration: {'CONTENT': ('module org.mauikit.filebrowsing 1.3\nplugin MauiKitFileBrowsing4plugin\n')}
), install_dir: qml_dir)
install_subdir('src/controls.6', install_dir: qml_dir, strip_directory: true)
install_headers(files('src/code/fm.h', 'src/code/fileoperation.h', 'src/code/fmstatic.h', 'src/code/fmlist.h', 'src/code/placeslist.h', 'src/code/downloader.h', 'src/code/fileloader.h', 'src/code/tagging.h', 'src/code/tagdb.h', 'src/code/moduleinfo.h') + [filebrowsing_export_header, filebrowsing_version_header], subdir: 'MauiKit4/FileBrowsing')
pkg = import('pkgconfig')
pkg.generate(lib, name: 'MauiKit File Browsing 4', filebase: 'MauiKitFileBrowsing4', version: meson.project_version(), description: 'MauiKit file browsing library', requires: 'MauiKit4')

cmake_targets = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitFileBrowsing4Targets.cmake',
    configuration: {'CONTENT': (('''get_filename_component(_MAUIKIT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET MauiKitFileBrowsing4)
  add_library(MauiKitFileBrowsing4 SHARED IMPORTED)
  set_target_properties(MauiKitFileBrowsing4 PROPERTIES
    IMPORTED_LOCATION "${_MAUIKIT_PREFIX}/@LIBDIR@/libMauiKitFileBrowsing4.so.4.0.5"
    INTERFACE_INCLUDE_DIRECTORIES "${_MAUIKIT_PREFIX}/include/MauiKit4/FileBrowsing")
endif()
if(NOT TARGET MauiKit4::FileBrowsing)
  add_library(MauiKit4::FileBrowsing ALIAS MauiKitFileBrowsing4)
endif()
if(NOT TARGET MauiKit::FileBrowsing)
  add_library(MauiKit::FileBrowsing ALIAS MauiKitFileBrowsing4)
endif()
''')).replace('@LIBDIR@', (get_option('libdir')))}
)
cmake_config = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitFileBrowsing4Config.cmake',
    configuration: {'CONTENT': ('''include(CMakeFindDependencyMacro)
find_dependency(Qt6 6.4 COMPONENTS Core)
include("${CMAKE_CURRENT_LIST_DIR}/MauiKitFileBrowsing4Targets.cmake")
set(MauiKitFileBrowsing4_FOUND TRUE)
''')}
)
cmake_version = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitFileBrowsing4ConfigVersion.cmake',
    configuration: {'CONTENT': (('''set(PACKAGE_VERSION "@VERSION@")
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT TRUE)
endif()
''')).replace('@VERSION@', (meson.project_version()))}
)
install_data([cmake_targets, cmake_config, cmake_version], install_dir: get_option('libdir') / 'cmake' / 'MauiKitFileBrowsing4')
