if get_option('tests')
  cmocka = dependency('cmocka', required: true)

  test_deps = [cmocka]
  test_inc = include_directories('../include')

  # Mock stubs for client functions used by rules.c
  mock_sources = files('mocks/client_stubs.c')

  test_events = executable('test_events',
    sources: ['unit/test_events.c'],
    dependencies: test_deps,
    include_directories: test_inc,
    link_with: swl_testable)

  test_config = executable('test_config',
    sources: ['unit/test_config.c'],
    dependencies: test_deps,
    include_directories: test_inc,
    link_with: swl_testable)

  test_layout = executable('test_layout',
    sources: ['unit/test_layout.c'],
    dependencies: test_deps,
    include_directories: test_inc,
    link_with: swl_testable,
    link_args: ['-lm'])

  test_rules = executable('test_rules',
    sources: ['unit/test_rules.c'] + mock_sources,
    dependencies: test_deps,
    include_directories: test_inc,
    link_with: swl_testable)

  test('events', test_events)
  test('config', test_config)
  test('layout', test_layout)
  test('rules', test_rules)
endif
