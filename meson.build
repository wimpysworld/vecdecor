project(
    'vecdecor',
    'c',
    'cpp',
    version: '0.10.0',
    license: 'MIT',
    meson_version: '>=0.59.0',
    default_options: [
        'cpp_std=c++17',
        'c_std=c11',
    ],
)

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic','-fPIC'], language:'cpp')

wayfire = dependency('wayfire', version: ['>=0.10.1', '<0.11.0'])
cairo = dependency('cairo')
librsvg = dependency('librsvg-2.0')

vecdecor_data_dir = get_option('datadir')
if not vecdecor_data_dir.startswith('/')
    vecdecor_data_dir = join_paths(get_option('prefix'), vecdecor_data_dir)
endif
vecdecor_asset_dir = join_paths(vecdecor_data_dir, 'vecdecor', 'buttons')
vecdecor_config = configuration_data()
vecdecor_config.set_quoted('VECDECOR_ASSET_DIR', vecdecor_asset_dir)
vecdecor_config_header = configure_file(
    input: 'src/vecdecor-config.h.in',
    output: 'vecdecor-config.h',
    configuration: vecdecor_config,
)

subdir('src')
subdir('assets')
subdir('metadata')
subdir('locale')
subdir('tests')
