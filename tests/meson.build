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

python = find_program('python3')

test('geometry-contract', geometry_test)
test('option-callback-contract', options_test)
test(
    'metadata-contract',
    python,
    args: [
        files('test-metadata.py'),
        join_paths(meson.current_source_dir(), '..', 'metadata', 'vecdecor.xml'),
    ],
)
