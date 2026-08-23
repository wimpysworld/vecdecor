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

button_state_test = executable(
    'test-button-state',
    [
        'test-button-state.cpp',
        '../src/deco-button-state.cpp',
    ],
    dependencies: [wayfire, deco_geometry_dep],
)

layout_state_test = executable(
    'test-layout-state',
    [
        'test-layout-state.cpp',
        '../src/deco-button-state.cpp',
        '../src/deco-layout-model.cpp',
    ],
    dependencies: [wayfire, deco_geometry_dep],
)

shade_state_test = executable(
    'test-shade-state',
    [
        'test-shade-state.cpp',
        '../src/shade-state.cpp',
    ],
    include_directories: include_directories('../src'),
    dependencies: wayfire,
)

background_renderer_test = executable(
    'test-background-renderer',
    [
        'test-background-renderer.cpp',
        '../src/deco-background-renderer.cpp',
    ],
    dependencies: [cairo, deco_geometry_dep],
)

python = find_program('python3', required: false)

test('geometry-contract', geometry_test)
test('option-callback-contract', options_test)
test(
    'button-renderer',
    button_renderer_test,
    args: join_paths(meson.current_source_dir(), '..', 'assets', 'buttons'),
)
test('button-state', button_state_test)
test('layout-state', layout_state_test)
test('shade-state', shade_state_test)
test('background-renderer', background_renderer_test)
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
    test(
        'source-contract',
        python,
        args: [
            files('test-source-contract.py'),
            join_paths(meson.current_source_dir(), '..', 'src'),
        ],
    )
endif
