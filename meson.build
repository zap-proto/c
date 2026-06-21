project('zap-c', ['c','cpp'], meson_version: '>=1.0.0', default_options : ['c_std=c99', 'cpp_std=c++17'])

common_c_args = []
common_cpp_args = ['-std=c++17']
libzap_c_args = []
libzap_src = [
  'lib' / 'zap-malloc.c',
  'lib' / 'zap-stream.c',
  'lib' / 'zap.c',
]

libzap = library('zap', libzap_src,
    c_args : common_c_args + libzap_c_args,
    dependencies: [],
    implicit_include_directories: false,
    include_directories: include_directories(['compiler', 'lib'])
)

libzap_dep = declare_dependency(
    link_with: libzap,
    dependencies: [],
    include_directories: include_directories(['compiler', 'lib'])
)

zapc_src = [
  'compiler' / 'zapc-c.c',
  'compiler' / 'schema.zap.c',
  'compiler' / 'str.c'
]

zapc_c = executable('zapc-c', zapc_src,
      include_directories: include_directories(['lib']),
      dependencies: [libzap_dep],
      c_args : common_c_args,
)

zap_test_src = [
  'tests' / 'zap-test.cpp',
  'tests' / 'zap-stream-test.cpp',
  'tests' / 'example-test.cpp',
  'tests' / 'addressbook.zap.c',
  'compiler' / 'test.zap.c',
  'compiler' / 'schema-test.cpp',
  'compiler' / 'schema.zap.c'
]

if get_option('enable_tests') and not meson.is_subproject()

  thread_dep = dependency('threads')

  gtest_proj = subproject('gtest')
  gtest_dep = gtest_proj.get_variable('gtest_dep')
  gmock_dep = gtest_proj.get_variable('gmock_dep')

  # Sanitizer runtimes are pulled in by the compiler driver via the
  # -fsanitize flags that meson's b_sanitize option adds to both compile
  # and link steps. No explicit find_library() is needed (and Apple clang
  # ships no findable libasan/libubsan), so b_sanitize alone is portable.
  test_dependencies = [libzap_dep, gtest_dep, gmock_dep, thread_dep]

  exe = executable('zap-test', zap_test_src,
      include_directories: include_directories(['lib', 'tests', 'compiler']),
      dependencies: test_dependencies,
      c_args : common_c_args,
      cpp_args : common_cpp_args,
      install: false,
      install_rpath: '',
      implicit_include_directories: false
    )
  test('zap-test', exe)

endif
