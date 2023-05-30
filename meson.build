project('librdmapp', ['c', 'cpp'],
    version: '1.0.0',
    default_options: [
    # 'werror=true',
    'warning_level=3',
    'c_std=c11',
    'cpp_std=c++20',
])



add_project_arguments([
    '-Wconversion',
    # '-Wno-missing-field-initializers',
    '-Wno-pedantic',
], language: ['c', 'cpp'])


project_mains = []
project_headers = []
project_sources = []

project_includes = [
    include_directories('./')
]


thread_dep = dependency('threads', required : true)


cpp_compiler = meson.get_compiler('cpp')

numa_dep = cpp_compiler.find_library('numa', has_headers: ['numa.h'], required: true)

rdma_libs = [
    cpp_compiler.find_library('rdmacm', has_headers: ['rdma/rdma_cma.h'], required: true),
    cpp_compiler.find_library('ibverbs', has_headers: ['infiniband/verbs.h'], required: true),
]
rdma_dep = declare_dependency(dependencies: rdma_libs)


subdir('rdmapp')


project_deps = [
    thread_dep,
    numa_dep,
    rdma_dep,
]


librdmapp = shared_library('rdmapp',
    project_sources,
    include_directories : project_includes,
    dependencies: project_deps,
    install : true
)


librdmapp_dep = declare_dependency(
    include_directories : project_includes,
    link_with : librdmapp
)



if not meson.is_subproject()
    subdir('tests')

#   test('all_tests',
#     executable(
#       'run_tests',
#       files(project_test_files),
#       dependencies : [project_dep, test_dep],
#       install : false
#     )
#   )


    fs = import('fs')

    foreach prog_src : project_mains
        prog_name = fs.stem(prog_src)

        link_args = []
    
        main = executable(f'@prog_name@',
            prog_src,
            include_directories: project_includes,
            link_args: link_args,
            link_with: [librdmapp],
            dependencies: project_deps
        )

        test(f'@prog_name@', main)
    endforeach
endif