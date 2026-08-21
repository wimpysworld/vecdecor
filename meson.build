project(
    'vecdecor',
    'c',
    'cpp',
    version: '0.11.0',
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

wayfire = dependency('wayfire', version: ['>=0.11.0', '<0.12.0'])
glm    = dependency('glm')

subdir('src')
subdir('metadata')
subdir('locale')
subdir('tests')
