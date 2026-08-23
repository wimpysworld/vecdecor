deco_geometry = static_library('deco-geometry',
        'deco-geometry.cpp',
        pic: true)

deco_geometry_dep = declare_dependency(
        include_directories: include_directories('.'),
        link_with: deco_geometry)

deco_button_renderer = static_library('deco-button-renderer',
        'deco-button-renderer.cpp',
        dependencies: [wayfire, cairo, librsvg, deco_geometry_dep],
        pic: true)

deco_button_renderer_dep = declare_dependency(
        include_directories: include_directories('.'),
        dependencies: [wayfire, cairo, librsvg, deco_geometry_dep],
        link_with: deco_button_renderer)

vecdecor = shared_module('vecdecor',
        ['decoration.cpp', 'deco-subsurface.cpp', 'deco-button.cpp', 'deco-layout.cpp', 'deco-theme.cpp'],
        dependencies: [wayfire, deco_geometry_dep, deco_button_renderer_dep],
        install: true,
        install_dir: join_paths(get_option('libdir'), 'wayfire'))
