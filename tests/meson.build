geometry_test = executable(
    'test-geometry',
    'test-geometry.cpp',
    dependencies: deco_geometry_dep,
)

options_test = executable(
    'test-options',
    'test-options.cpp',
    include_directories: include_directories('../src'),
)

button_renderer_test_support = static_library(
    'button-renderer-test-support',
    '../src/deco-button-renderer.cpp',
    cpp_args: '-DVECDECOR_BUTTON_RENDERER_NO_WAYFIRE',
    dependencies: [cairo, librsvg, deco_geometry_dep],
)

button_renderer_test_support_dep = declare_dependency(
    include_directories: include_directories('../src'),
    dependencies: [cairo, librsvg, deco_geometry_dep],
    link_with: button_renderer_test_support,
)

button_renderer_test = executable(
    'test-button-renderer',
    'test-button-renderer.cpp',
    dependencies: button_renderer_test_support_dep,
)

python = find_program('python3', required: false)

test('geometry-contract', geometry_test)
test('option-callback-contract', options_test)
test(
    'button-renderer',
    button_renderer_test,
    args: join_paths(meson.current_source_dir(), '..', 'assets', 'buttons'),
)
if python.found()
    test(
        'metadata-contract',
        python,
        args: [
            files('test-metadata.py'),
            join_paths(meson.current_source_dir(), '..', 'metadata', 'vecdecor.xml'),
            join_paths(meson.current_source_dir(), '..', 'assets', 'buttons'),
        ],
    )
endif
