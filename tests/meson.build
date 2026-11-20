synth = static_library('synth', 'synth.c', include_directories : inc)

test_parse = executable('test_parse', 'test_parse.c',
  dependencies : [audvd_dep, m_dep], link_with : synth)
test('parse', test_parse,
  env : ['AUDVD_TEST_DISC=' + get_option('test_disc')])

fuzz_smoke = executable('fuzz_smoke', 'fuzz_ifo.c',
  dependencies : audvd_dep, link_with : synth)
test('fuzz-smoke', fuzz_smoke, args : ['3000'], timeout : 120)

if get_option('fuzzer')
  executable('fuzz_ifo', 'fuzz_ifo.c',
    dependencies : audvd_dep, link_with : synth,
    c_args : ['-DAUDVD_LIBFUZZER', '-fsanitize=fuzzer'],
    link_args : ['-fsanitize=fuzzer'])
endif
