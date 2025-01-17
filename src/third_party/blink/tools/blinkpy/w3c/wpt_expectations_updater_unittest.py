# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import copy
import json

from blinkpy.common.host_mock import MockHost
from blinkpy.common.net.git_cl import TryJobStatus
from blinkpy.common.net.git_cl_mock import MockGitCL
from blinkpy.common.net.results_fetcher import Build
from blinkpy.common.net.results_fetcher_mock import (
    MockTestResultsFetcher, BuilderStep)
from blinkpy.common.net.web_test_results import WebTestResult, WebTestResults
from blinkpy.common.path_finder import RELATIVE_WEB_TESTS
from blinkpy.common.system.executive import ScriptError
from blinkpy.common.system.log_testing import LoggingTestCase

from blinkpy.w3c.wpt_expectations_updater import (
    WPTExpectationsUpdater, SimpleTestResult, DesktopConfig)
from blinkpy.w3c.wpt_manifest import (
    WPTManifest, BASE_MANIFEST_NAME, MANIFEST_NAME)

from blinkpy.web_tests.builder_list import BuilderList
from blinkpy.web_tests.port.android import PRODUCTS_TO_EXPECTATION_FILE_PATHS
from blinkpy.web_tests.port.factory_mock import MockPortFactory

MOCK_WEB_TESTS = '/mock-checkout/' + RELATIVE_WEB_TESTS

class WPTExpectationsUpdaterTest(LoggingTestCase):
    def mock_host(self):
        """Returns a mock host with fake values set up for testing."""
        host = MockHost()
        host.port_factory = MockPortFactory(host)
        host.executive._output = ''

        # Set up a fake list of try builders.
        host.builders = BuilderList({
            'MOCK Try Mac10.10': {
                'port_name': 'test-mac-mac10.10',
                'specifiers': ['Mac10.10', 'Release'],
                'is_try_builder': True,
            },
            'MOCK Try Mac10.11': {
                'port_name': 'test-mac-mac10.11',
                'specifiers': ['Mac10.11', 'Release'],
                'is_try_builder': True,
            },
            'MOCK Try Trusty': {
                'port_name': 'test-linux-trusty',
                'specifiers': ['Trusty', 'Release'],
                'master': 'tryserver.blink',
                'has_webdriver_tests': True,
                'is_try_builder': True,
            },
            'MOCK Try Precise': {
                'port_name': 'test-linux-precise',
                'specifiers': ['Precise', 'Release'],
                'is_try_builder': True,
            },
            'MOCK Try Win10': {
                'port_name': 'test-win-win10',
                'specifiers': ['Win10', 'Release'],
                'is_try_builder': True,
            },
            'MOCK Try Win7': {
                'port_name': 'test-win-win7',
                'specifiers': ['Win7', 'Release'],
                'is_try_builder': True,
            },
        })

        # Write a dummy manifest file, describing what tests exist.
        host.filesystem.write_text_file(
            host.port_factory.get().web_tests_dir() + '/external/' +
            BASE_MANIFEST_NAME,
            json.dumps({
                'items': {
                    'reftest': {
                        'reftest.html': [
                            'abcdef123',
                            [None, [['/reftest-ref.html', '==']], {}]
                        ]
                    },
                    'testharness': {
                        'test/path.html': ['abcdef123', [None, {}]],
                        'test/zzzz.html': ['ghijkl456', [None, {}]],
                        'fake/some_test.html': [
                            'ghijkl456', ['fake/some_test.html?HelloWorld', {}]],
                        'fake/file/deleted_path.html': [
                            'ghijkl456', [None, {}]],
                        'test/task.js': [
                            'mnpqrs789',
                            ['test/task.html', {}],
                            ['test/task2.html', {}]],
                    },
                    'manual': {
                        'x-manual.html': ['abcdef123', [None, {}]],
                    },
                },
            }))

        for path in PRODUCTS_TO_EXPECTATION_FILE_PATHS.values():
            host.filesystem.write_text_file(path, '')
        return host

    def test_run_single_platform_failure(self):
        """Tests the main run method in a case where one test fails on one platform."""
        host = self.mock_host()

        # Fill in an initial value for TestExpectations
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(expectations_path,
                                        WPTExpectationsUpdater.MARKER_COMMENT + '\n')

        # Set up fake try job results.
        updater = WPTExpectationsUpdater(host)
        updater.git_cl = MockGitCL(
            updater.host, {
                Build('MOCK Try Mac10.10', 333):
                TryJobStatus('COMPLETED', 'FAILURE'),
                Build('MOCK Try Mac10.11', 111):
                TryJobStatus('COMPLETED', 'SUCCESS'),
                Build('MOCK Try Trusty', 222):
                TryJobStatus('COMPLETED', 'SUCCESS'),
                Build('MOCK Try Precise', 333):
                TryJobStatus('COMPLETED', 'SUCCESS'),
                Build('MOCK Try Win10', 444):
                TryJobStatus('COMPLETED', 'SUCCESS'),
                Build('MOCK Try Win7', 555):
                TryJobStatus('COMPLETED', 'SUCCESS'),
            })

        # Set up failing results for one try bot. It shouldn't matter what
        # results are for the other builders since we shouldn't need to even
        # fetch results, since the try job status already tells us that all
        # of the tests passed.
        host.results_fetcher.set_results(
            Build('MOCK Try Mac10.10', 333),
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'test': {
                                'path.html': {
                                    'expected': 'PASS',
                                    'actual': 'TIMEOUT',
                                    'is_unexpected': True,
                                }
                            }
                        }
                    }
                }
            }))
        self.assertEqual(0, updater.run())

        # Results are only fetched for failing builds.
        self.assertEqual(host.results_fetcher.fetched_builds,
                         [BuilderStep(Build('MOCK Try Mac10.10', 333),
                                      'blink_web_tests (with patch)')])

        self.assertEqual(
            host.filesystem.read_text_file(expectations_path),
            '# ====== New tests from wpt-importer added here ======\n'
            'crbug.com/626703 [ Mac10.10 ] external/wpt/test/path.html [ Timeout ]\n'
        )

    def test_cmd_arg_include_unexpected_pass_raieses_exception(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(expectations_path,
                                        WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        updater = WPTExpectationsUpdater(host, args=['--include-unexpected-pass'])
        with self.assertRaises(AssertionError) as ctx:
            updater.run()
        self.assertIn('--include-unexpected-pass', str(ctx.exception))

    def test_get_failing_results_dict_only_passing_results(self):
        host = self.mock_host()
        host.results_fetcher.set_results(
            Build('MOCK Try Mac10.10', 123),
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'x': {
                                'passing-test.html': {
                                    'expected': 'PASS',
                                    'actual': 'PASS',
                                },
                            },
                        },
                    },
                },
            }))
        updater = WPTExpectationsUpdater(host)
        self.assertFalse(
            updater.get_failing_results_dicts(Build('MOCK Try Mac10.10', 123)))

    def test_get_failing_results_dict_unexpected_pass(self):
        host = self.mock_host()
        host.results_fetcher.set_results(
            Build('MOCK Try Mac10.10', 123),
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'x': {
                                'passing-test.html': {
                                    'expected': 'FAIL TIMEOUT',
                                    'actual': 'PASS',
                                    'is_unexpected': True,
                                },
                            },
                        },
                    },
                },
            }))
        updater = WPTExpectationsUpdater(host)
        self.assertFalse(
            updater.get_failing_results_dicts(Build('MOCK Try Mac10.10', 123)))

    def test_get_failing_results_dict_no_results(self):
        host = self.mock_host()
        host.results_fetcher = MockTestResultsFetcher()
        host.results_fetcher.set_results(Build('MOCK Try Mac10.10', 123), None)
        updater = WPTExpectationsUpdater(host)
        self.assertFalse(
            updater.get_failing_results_dicts(Build('MOCK Try Mac10.10', 123)))

    def test_get_failing_results_dict_some_failing_results(self):
        host = self.mock_host()
        host.results_fetcher.set_results(
            Build('MOCK Try Mac10.10', 123),
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'x': {
                                'failing-test.html': {
                                    'expected': 'PASS',
                                    'actual': 'IMAGE',
                                    'is_unexpected': True,
                                },
                            },
                        },
                    },
                },
            }))
        updater = WPTExpectationsUpdater(host)
        results = updater.get_failing_results_dicts(
            Build('MOCK Try Mac10.10', 123))
        self.assertEqual(
            results, [{
                'external/wpt/x/failing-test.html': {
                    DesktopConfig(port_name='test-mac-mac10.10'):
                    SimpleTestResult(
                        actual='IMAGE',
                        expected='PASS',
                        bug='crbug.com/626703',
                    ),
                },
            }])

    def test_get_failing_results_dict_non_wpt_test(self):
        host = self.mock_host()
        host.results_fetcher.set_results(
            Build('MOCK Try Mac10.10', 123),
            WebTestResults({
                'tests': {
                    'x': {
                        'failing-test.html': {
                            'expected': 'PASS',
                            'actual': 'IMAGE',
                            'is_unexpected': True,
                        },
                    },
                },
            }))
        updater = WPTExpectationsUpdater(host)
        results_dict = updater.get_failing_results_dicts(
            Build('MOCK Try Mac10.10', 123))
        self.assertEqual(results_dict, [])

    def test_get_failing_results_dict_webdriver_failing_results_(self):
        host = self.mock_host()
        host.results_fetcher.set_results(
            Build('MOCK Try Trusty', 123),
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'x': {
                                'failing-test.html': {
                                    'expected': 'PASS',
                                    'actual': 'IMAGE',
                                    'is_unexpected': True,
                                },
                            },
                        },
                    },
                },
            }))

        host.results_fetcher.set_webdriver_test_results(
            Build('MOCK Try Trusty', 123), "tryserver.blink",
            WebTestResults({
                'tests': {
                    'external': {
                        'wpt': {
                            'y': {
                                'webdriver-fail.html': {
                                    'expected': 'PASS',
                                    'actual': 'FAIL',
                                    'is_unexpected': True,
                                },
                            },
                        },
                    },
                },
            }))
        updater = WPTExpectationsUpdater(host)
        results = updater.get_failing_results_dicts(
            Build('MOCK Try Trusty', 123))
        self.assertEqual(len(results), 2)
        self.assertEqual(
            results, [{
                'external/wpt/x/failing-test.html': {
                    DesktopConfig('test-linux-trusty'):
                    SimpleTestResult(
                        actual='IMAGE',
                        expected='PASS',
                        bug='crbug.com/626703',
                    ),
                }}, {
                'external/wpt/y/webdriver-fail.html': {
                    DesktopConfig('test-linux-trusty'):
                    SimpleTestResult(
                        actual='FAIL',
                        expected='PASS',
                        bug='crbug.com/626703',
                    ),
                },
            }])

    def test_merge_same_valued_keys_all_match(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertEqual(
            updater.merge_same_valued_keys({
                'one': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
                'two': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
            }), {('two', 'one'): {
                     'expected': 'FAIL',
                     'actual': 'PASS'
                 }})

    def test_merge_same_valued_keys_one_mismatch(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertEqual(
            updater.merge_same_valued_keys({
                'one': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
                'two': {
                    'expected': 'FAIL',
                    'actual': 'TIMEOUT'
                },
                'three': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
            }), {
                ('three', 'one'): {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
                ('two',): {
                    'expected': 'FAIL',
                    'actual': 'TIMEOUT'
                },
            })

    def test_get_expectations(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        # Positional arguments of SimpleTestResult: (expected, actual, bug)
        self.assertEqual(
            updater.get_expectations(SimpleTestResult('FAIL', 'PASS', 'bug')),
            {'Pass'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('FAIL', 'PASS PASS', 'bug')), {'Pass'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('FAIL', 'TIMEOUT', 'bug')), {'Timeout'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('FAIL', 'TIMEOUT TIMEOUT', 'bug')),
            {'Timeout'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('TIMEOUT', 'PASS', 'bug')), {'Pass'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('TIMEOUT', 'PASS PASS', 'bug')), {'Pass'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'TEXT PASS', 'bug')),
            {'Pass', 'Failure'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'TIMEOUT CRASH TEXT', 'bug')),
            {'Crash', 'Failure', 'Timeout'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('SLOW CRASH FAIL TIMEOUT', 'PASS', 'bug')),
            {'Pass'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'IMAGE+TEXT IMAGE IMAGE', 'bug')),
            {'Failure'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'MISSING', 'bug')), {'Skip'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'MISSING MISSING', 'bug')), {'Skip'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'TIMEOUT', 'bug'),
                test_name='foo/bar-manual.html'), {'Skip'})
        self.assertEqual(
            updater.get_expectations(
                SimpleTestResult('PASS', 'FAIL', 'bug'),
                test_name='external/wpt/webdriver/foo/a'), {'Failure'})

    def test_create_line_dict_old_tests(self):
        # In this example, there are two failures that are not in wpt.
        updater = WPTExpectationsUpdater(self.mock_host())
        results = {
            'fake/test/path.html': {
                tuple([DesktopConfig(port_name='one')]):
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
                tuple([DesktopConfig(port_name='two')]):
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
            }
        }
        self.assertEqual(updater.create_line_dict(results), {})

    def test_create_line_dict_new_tests(self):
        # In this example, there are three unexpected results for wpt tests.
        # The new test expectation lines are sorted by test, and then specifier.
        updater = WPTExpectationsUpdater(self.mock_host())
        results = {
            'external/wpt/test/zzzz.html': {
                tuple([DesktopConfig(port_name='test-mac-mac10.10')]):
                SimpleTestResult(
                    expected='PASS', actual='TEXT', bug='crbug.com/test'),
            },
            'virtual/foo/external/wpt/test/zzzz.html': {
                tuple([DesktopConfig(port_name='test-linux-trusty')]):
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
                tuple([DesktopConfig(port_name='test-mac-mac10.11')]):
                SimpleTestResult(
                    expected='FAIL', actual='TIMEOUT', bug='crbug.com/test'),
            },
            'unrelated/test.html': {
                tuple([DesktopConfig(port_name='test-linux-trusty')]):
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
            },
        }
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/test/zzzz.html': [
                    'crbug.com/test [ Mac10.10 ] external/wpt/test/zzzz.html [ Failure ]'
                ],
                'virtual/foo/external/wpt/test/zzzz.html': [
                    'crbug.com/test [ Trusty ] virtual/foo/external/wpt/test/zzzz.html [ Pass ]',
                    'crbug.com/test [ Mac10.11 ] virtual/foo/external/wpt/test/zzzz.html [ Timeout ]',
                ],
            })

    def test_create_line_dict_with_manual_tests(self):
        # In this example, there are two manual tests that should be skipped.
        updater = WPTExpectationsUpdater(self.mock_host())
        results = {
            'virtual/foo/external/wpt/test/aa-manual.html': {
                tuple([DesktopConfig(port_name='test-linux-trusty')]):
                SimpleTestResult(
                    expected='PASS', actual='TIMEOUT', bug='crbug.com/test'),
                tuple([DesktopConfig(port_name='test-mac-mac10.11')]):
                SimpleTestResult(
                    expected='FAIL', actual='TIMEOUT', bug='crbug.com/test'),
            },
        }
        self.assertEqual(
            updater.create_line_dict(results), {
                'virtual/foo/external/wpt/test/aa-manual.html': [
                    'crbug.com/test [ Trusty ] virtual/foo/external/wpt/test/aa-manual.html [ Skip ]',
                    'crbug.com/test [ Mac10.11 ] virtual/foo/external/wpt/test/aa-manual.html [ Skip ]',
                ],
            })

    def test_create_line_dict_with_asterisks(self):
        # Literal asterisks in test names need to be escaped in expectations.
        updater = WPTExpectationsUpdater(self.mock_host())
        results = {
            'external/wpt/html/dom/interfaces.https.html?exclude=(Document.*|HTML.*)':
            {
                tuple([DesktopConfig(port_name='test-linux-trusty')]):
                SimpleTestResult(
                    expected='PASS', actual='FAIL', bug='crbug.com/test'),
            },
        }
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/html/dom/interfaces.https.html?exclude=(Document.*|HTML.*)':
                [
                    'crbug.com/test [ Trusty ] external/wpt/html/dom/interfaces.https.html?exclude=(Document.\*|HTML.\*) [ Failure ]',
                ],
            })

    def test_normalized_specifiers(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertEqual(
            updater.normalized_specifiers('x/y.html', [DesktopConfig(port_name='test-mac-mac10.10')]),
            ['Mac10.10'])
        self.assertEqual(updater.normalized_specifiers('x/y.html', []), [''])

        self.assertEqual(
            updater.normalized_specifiers(
                'x/y.html', [DesktopConfig(port_name='test-mac-mac10.10'),
                             DesktopConfig(port_name='test-win-win7')]),
            ['Mac10.10', 'Win7'])

    def test_skipped_specifiers_when_test_is_skip(self):
        host = self.mock_host()
        expectations_path = '/test.checkout/wtests/NeverFixTests'
        host.filesystem.write_text_file(
            expectations_path,
            ('# tags: [ Linux ]\n'
             '# results: [ Skip ]\n'
             'crbug.com/111 [ Linux ] external/wpt/test.html [ Skip ]\n'))
        host.filesystem.write_text_file(
            '/test.checkout/wtests/external/wpt/test.html', '')
        updater = WPTExpectationsUpdater(host)
        self.assertEqual(
            updater.skipped_specifiers('external/wpt/test.html'),
            ['Precise', 'Trusty'])

    def test_specifiers_can_extend_to_all_platforms(self):
        host = self.mock_host()
        expectations_path = '/test.checkout/wtests/NeverFixTests'
        host.filesystem.write_text_file(
            expectations_path,
            ('# tags: [ Linux ]\n'
             '# results: [ Skip ]\n'
             'crbug.com/111 [ Linux ] external/wpt/test.html [ Skip ]\n'))
        host.filesystem.write_text_file(
            '/test.checkout/wtests/external/wpt/test.html', '')
        updater = WPTExpectationsUpdater(host)
        self.assertTrue(
            updater.specifiers_can_extend_to_all_platforms(
                ['Mac10.10', 'Mac10.11', 'Win7', 'Win10'],
                'external/wpt/test.html'))
        self.assertFalse(
            updater.specifiers_can_extend_to_all_platforms(
                ['Mac10.10', 'Win7', 'Win10'], 'external/wpt/test.html'))

    def test_simplify_specifiers(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        macros = {
            'mac': ['Mac10.10', 'mac10.11'],
            'win': ['Win7', 'win10'],
            'Linux': ['Trusty'],
        }
        self.assertEqual(
            updater.simplify_specifiers(['mac10.10', 'mac10.11'], macros),
            ['Mac'])
        self.assertEqual(
            updater.simplify_specifiers(['Mac10.10', 'Mac10.11', 'Trusty'],
                                        macros), ['Linux', 'Mac'])
        self.assertEqual(
            updater.simplify_specifiers(
                ['Mac10.10', 'Mac10.11', 'Trusty', 'Win7', 'Win10'], macros),
            [])
        self.assertEqual(
            updater.simplify_specifiers(['Mac', 'Win', 'Linux'], macros), [])

    def test_simplify_specifiers_uses_specifiers_in_builder_list(self):
        # Even if there are extra specifiers in the macro dictionary, we can simplify specifier
        # lists if they contain all of the specifiers the are represented in the builder list.
        # This way specifier simplification can still be done while a new platform is being added.
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        macros = {
            'mac': ['Mac10.10', 'mac10.11', 'mac10.14'],
            'win': ['Win7', 'win10'],
            'Linux': ['Trusty'],
        }
        self.assertEqual(
            updater.simplify_specifiers(['mac10.10', 'mac10.11'], macros),
            ['Mac'])
        self.assertEqual(
            updater.simplify_specifiers(
                ['Mac10.10', 'Mac10.11', 'Trusty', 'Win7', 'Win10'], macros),
            [])

    def test_simplify_specifiers_port_not_tested_by_trybots(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        macros = {
            'mac': ['Mac10.10', 'mac10.11'],
            'win': ['win10'],
            'foo': ['bar'],
        }
        self.assertEqual(
            updater.simplify_specifiers(['mac10.10', 'mac10.11'], macros),
            ['Mac'])

    def test_normalized_specifiers_with_skipped_test(self):
        host = self.mock_host()
        expectations_path = '/test.checkout/wtests/NeverFixTests'
        host.filesystem.write_text_file(
            expectations_path,
            ('# tags: [ Linux Mac10.11 ]\n'
             '# results: [ Skip ]\n'
             'crbug.com/111 [ Linux ] external/wpt/test.html [ Skip ]\n'
             'crbug.com/111 [ Mac10.11 ] external/wpt/test.html [ Skip ]\n'))
        host.filesystem.write_text_file(
            '/test.checkout/wtests/external/wpt/test.html', '')
        updater = WPTExpectationsUpdater(host)
        self.assertEqual(
            updater.normalized_specifiers(
                'external/wpt/test.html',
                [DesktopConfig(port_name='test-mac-mac10.10'),
                 DesktopConfig(port_name='test-win-win7'),
                 DesktopConfig(port_name='test-win-win10')]),
            [''])
        self.assertEqual(
            updater.normalized_specifiers('external/wpt/test.html',
                                          [DesktopConfig(port_name='test-win-win7'),
                                           DesktopConfig(port_name='test-win-win10')]),
            ['Win'])
        self.assertEqual(
            updater.normalized_specifiers('external/wpt/another.html',
                                          [DesktopConfig('test-win-win7'),
                                           DesktopConfig('test-win-win10')]),
            ['Win'])

    def test_merge_dicts_with_differing_status_is_merged(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        # Both dicts here have the key "one", and the value is not equal. The
        # actual fields get joined together.
        result = updater.merge_dicts(
            {
                'external/wpt/test/path.html': {
                    'one': SimpleTestResult('FAIL', 'PASS', 'bug'),
                    'two': SimpleTestResult('FAIL', 'TIMEOUT', 'bug'),
                    'three': SimpleTestResult('FAIL', 'PASS', 'bug'),
                },
            }, {
                'external/wpt/test/path.html': {
                    'one': SimpleTestResult('FAIL', 'TIMEOUT', 'bug'),
                }
            })
        expected_result = {
            'external/wpt/test/path.html': {
                'one': SimpleTestResult('FAIL', 'PASS TIMEOUT', 'bug'),
                'two': SimpleTestResult('FAIL', 'TIMEOUT', 'bug'),
                'three': SimpleTestResult('FAIL', 'PASS', 'bug'),
            },
        }
        self.assertEqual(result, expected_result)

    def test_merge_dicts_merges_second_dict_into_first(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        one = {
            'fake/test/path.html': {
                'one': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
                'two': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
            }
        }
        two = {
            'external/wpt/test/path.html': {
                'one': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
                'two': {
                    'expected': 'FAIL',
                    'actual': 'TIMEOUT'
                },
                'three': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
            }
        }
        three = {
            'external/wpt/test/path.html': {
                'four': {
                    'expected': 'FAIL',
                    'actual': 'PASS'
                },
            }
        }

        output = updater.merge_dicts(one, three)
        self.assertEqual(output, one)
        output = updater.merge_dicts(two, three)
        self.assertEqual(output, two)

    def test_generate_failing_results_dict(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        web_test_list = WebTestResults(
            {
                "tests": {
                    "external/wpt/test/name.html": {
                        "expected": "bar",
                        "actual": "foo",
                        "is_unexpected": True,
                        "has_stderr": True
                    }
                }
            },
            step_name='blink_web_tests (with patch)')

        updater.port_name = lambda b: b.builder_name
        self.assertEqual(
            updater.generate_failing_results_dict(
                Build(builder_name='test-mac-mac10.10', build_number=1), web_test_list),
            {
                'external/wpt/test/name.html': {
                    DesktopConfig(port_name='test-mac-mac10.10'):
                    SimpleTestResult(
                        expected='bar',
                        actual='foo',
                        bug='crbug.com/626703',
                    )
                }
            })

    def test_no_expectations_to_write(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        test_expectations = {'external/wpt/test/test1.html': {}}
        exp_dict = updater.write_to_test_expectations(test_expectations)
        self.assertEqual(exp_dict, {})
        logs = ''.join(self.logMessages()).lower()
        self.assertIn(
            ('no lines to write to testexpectations,'
             ' webdriverexpectations or neverfixtests.'),
            logs)

    def test_cleanup_outside_affected_expectations_in_cl(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(
            expectations_path,
            '# tags: [ Linux ]\n' +
            '# results: [ Pass Failure ]\n' +
            WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
            '[ linux ] external/wpt/fake/some_test.html?HelloWorld [ Failure ]\n' +
            'external/wpt/fake/file/non_existent_file.html [ Pass ]\n' +
            'external/wpt/fake/file/deleted_path.html [ Pass ]\n')
        updater = WPTExpectationsUpdater(
            host, ['--clean-up-test-expectations-only'])
        updater.port.tests = lambda: {
            'external/wpt/fake/new.html?HelloWorld'}

        def _git_command_return_val(cmd):
            if '--diff-filter=D' in cmd:
                return 'external/wpt/fake/file/deleted_path.html'
            if '--diff-filter=R' in cmd:
                return 'C external/wpt/fake/some_test.html external/wpt/fake/new.html'
            return ''

        updater.git.run = _git_command_return_val
        updater._relative_to_web_test_dir = lambda test_path: test_path
        updater.run()

        value = host.filesystem.read_text_file(expectations_path)
        self.assertMultiLineEqual(
            value, ('# tags: [ Linux ]\n' +
                    '# results: [ Pass Failure ]\n' +
                    WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
                    '[ linux ] external/wpt/fake/new.html?HelloWorld [ Failure ]\n'))

    def test_clean_expectations_for_deleted_test_harness(self):
        host = self.mock_host()
        port = host.port_factory.get()
        expectations_path = \
            port.path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(
            expectations_path,
            '# tags: [ Win Linux ]\n' +
            '# results: [ Pass Failure ]\n' +
            WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
            '[ linux ] wpt_internal/test/task.html [ Failure ]\n' +
            '[ win ] wpt_internal/test/task2.html [ Failure ]\n' +
            '[ linux ] external/wpt/test/task.html [ Failure ]\n' +
            'external/wpt/test/task2.html [ Pass ]\n')

        def _git_command_return_val(cmd):
            if '--diff-filter=D' in cmd:
                return '\n'.join(['external/wpt/test/task.js',
                                  'wpt_internal/test/task.js'])
            return ''

        wpt_manifest = port.wpt_manifest('external/wpt')
        host.filesystem.maybe_make_directory(
            port.web_tests_dir(), 'wpt_internal')
        host.filesystem.copyfile(
            host.filesystem.join(port.web_tests_dir(),
                                 'external', 'wpt', MANIFEST_NAME),
            host.filesystem.join(port.web_tests_dir(), 'wpt_internal',
                                 MANIFEST_NAME))
        wpt_internal_manifest = WPTManifest(host, host.filesystem.join(
            port.web_tests_dir(), 'wpt_internal', MANIFEST_NAME))

        updater = WPTExpectationsUpdater(
            host,
            ['--clean-up-affected-tests-only',
             '--clean-up-test-expectations-only'],
            [wpt_manifest, wpt_internal_manifest])
        updater.git.run = _git_command_return_val
        updater._relative_to_web_test_dir = lambda test_path: test_path
        updater.cleanup_test_expectations_files()

        test_expectations = {'external/wpt/fake/file/path.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='PASS', expected='', bug='crbug.com/123')}}
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater.write_to_test_expectations(test_expectations)
        value = host.filesystem.read_text_file(expectations_path)
        self.assertMultiLineEqual(
            value, ('# tags: [ Win Linux ]\n' +
                    '# results: [ Pass Failure ]\n\n' +
                    WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
                    'crbug.com/123 [ Trusty ] external/wpt/fake/file/path.html [ Pass ]'))
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

    def test_write_to_test_expectations_and_cleanup_expectations(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(
            expectations_path,
            '# tags: [ Linux ]\n' +
            '# results: [ Pass Failure ]\n' +
            WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
            '[ linux ] external/wpt/fake/some_test.html?HelloWorld [ Failure ]\n' +
            'external/wpt/fake/file/deleted_path.html [ Pass ]\n')
        updater = WPTExpectationsUpdater(
            host, ['--clean-up-affected-tests-only',
                   '--clean-up-test-expectations-only'])

        def _git_command_return_val(cmd):
            if '--diff-filter=D' in cmd:
                return 'external/wpt/fake/file/deleted_path.html'
            if '--diff-filter=R' in cmd:
                return 'C external/wpt/fake/some_test.html external/wpt/fake/new.html'
            return ''

        updater.git.run = _git_command_return_val
        updater._relative_to_web_test_dir = lambda test_path: test_path
        updater.cleanup_test_expectations_files()

        test_expectations = {'external/wpt/fake/file/path.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='PASS', expected='', bug='crbug.com/123')}}
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater.write_to_test_expectations(test_expectations)
        value = host.filesystem.read_text_file(expectations_path)
        self.assertMultiLineEqual(
            value, ('# tags: [ Linux ]\n' +
                    '# results: [ Pass Failure ]\n' +
                    WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
                    'crbug.com/123 [ Trusty ] external/wpt/fake/file/path.html [ Pass ]\n' +
                    '[ linux ] external/wpt/fake/new.html?HelloWorld [ Failure ]\n'))
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

    def test_clean_up_affected_tests_arg_raises_exception(self):
        host = self.mock_host()
        with self.assertRaises(AssertionError) as ctx:
            updater = WPTExpectationsUpdater(
                host, ['--clean-up-affected-tests-only'])
            updater.run()
        self.assertIn('Cannot use --clean-up-affected-tests-only',
                      str(ctx.exception))

    def test_clean_up_affected_tests_arg_does_not_raise_exception(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(
            host, ['--clean-up-affected-tests-only',
                   '--clean-up-test-expectations'])

    def test_write_to_test_expectations_with_marker_comment(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        host.filesystem.write_text_file(expectations_path,
                                        WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        updater = WPTExpectationsUpdater(host)
        test_expectations = {'external/wpt/fake/file/path.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='PASS', expected='', bug='crbug.com/123')}}
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater.write_to_test_expectations(test_expectations)
        value = host.filesystem.read_text_file(expectations_path)

        self.assertMultiLineEqual(
            value, (WPTExpectationsUpdater.MARKER_COMMENT + '\n'
                    'crbug.com/123 [ Trusty ] external/wpt/fake/file/path.html [ Pass ]\n'))
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

    def test_write_to_test_expectations_with_webdriver_lines(self):
        host = self.mock_host()

        webdriver_expectations_path = \
            host.port_factory.get().path_to_webdriver_expectations_file()
        host.filesystem.write_text_file(webdriver_expectations_path,
                                        WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        updater = WPTExpectationsUpdater(host)

        test_expectations = {'external/wpt/webdriver/fake/file/path.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='PASS', expected='', bug='crbug.com/123')}}

        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        expectations_value_origin = host.filesystem.read_text_file(
            expectations_path)

        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater.write_to_test_expectations(test_expectations)
        value = host.filesystem.read_text_file(webdriver_expectations_path)

        self.assertMultiLineEqual(value, (
            WPTExpectationsUpdater.MARKER_COMMENT + '\n'
            'crbug.com/123 [ Trusty ] external/wpt/webdriver/fake/file/path.html [ Pass ]\n'
        ))

        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

        expectations_value = host.filesystem.read_text_file(expectations_path)
        self.assertMultiLineEqual(expectations_value,
                                  expectations_value_origin)

    def test_write_to_test_expectations_with_no_marker_comment(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        raw_exps = '# tags: [ Trusty ]\n# results: [ Pass Failure ]\n'
        host.filesystem.write_text_file(
            expectations_path,
            raw_exps + '\n' +
            'crbug.com/111 [ Trusty ] foo/bar.html [ Failure ]\n')
        updater = WPTExpectationsUpdater(host)
        test_expectations = {'external/wpt/fake/file/path.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='PASS', expected='', bug='crbug.com/123')}}
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater.write_to_test_expectations(test_expectations)
        value = host.filesystem.read_text_file(expectations_path)

        self.assertMultiLineEqual(
            value, (raw_exps + '\n' +
                    'crbug.com/111 [ Trusty ] foo/bar.html [ Failure ]\n'
                    '\n' + WPTExpectationsUpdater.MARKER_COMMENT + '\n'
                    'crbug.com/123 [ Trusty ] external/wpt/fake/file/path.html [ Pass ]'))
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

    def test_write_to_test_expectations_with_marker_and_no_lines(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        raw_exps = '# tags: [ Trusty ]\n# results: [ Pass ]\n'
        host.filesystem.write_text_file(
            expectations_path,
            raw_exps + '\n' +
            WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
            'crbug.com/123 [ Trusty ] fake/file/path.html [ Pass ]\n')
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        skip_value_origin = host.filesystem.read_text_file(skip_path)

        updater = WPTExpectationsUpdater(host)
        updater.write_to_test_expectations({})

        value = host.filesystem.read_text_file(expectations_path)
        self.assertMultiLineEqual(
            value,
            raw_exps + '\n' +
            WPTExpectationsUpdater.MARKER_COMMENT + '\n' +
            'crbug.com/123 [ Trusty ] fake/file/path.html [ Pass ]\n')
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(skip_value, skip_value_origin)

    def test_write_to_test_expectations_with_manual_tests_and_newline(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        raw_exps = '# tags: [ Trusty ]\n# results: [ Skip ]\n'
        test_expectations = {'external/wpt/fake/file/path-manual.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='TIMEOUT', expected={}, bug='')}}
        host.filesystem.write_text_file(expectations_path,
                                        WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        host.filesystem.write_text_file(
            skip_path,
            raw_exps +
            '\n[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]\n')
        updater = WPTExpectationsUpdater(host)

        updater.write_to_test_expectations(test_expectations)

        expectations_value = host.filesystem.read_text_file(expectations_path)
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(expectations_value, WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        self.assertMultiLineEqual(
            skip_value,
            raw_exps + '\n'
            '[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]\n'
            '[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]\n')

    def test_write_to_test_expectations_without_newline(self):
        host = self.mock_host()
        expectations_path = \
            host.port_factory.get().path_to_generic_test_expectations_file()
        skip_path = host.port_factory.get().path_to_never_fix_tests_file()
        test_expectations = {'external/wpt/fake/file/path-manual.html': {
            tuple([DesktopConfig(port_name='test-linux-trusty')]):
            SimpleTestResult(actual='TIMEOUT', expected={}, bug='')}}
        raw_exps = '# tags: [ Trusty ]\n# results: [ Skip ]\n'
        host.filesystem.write_text_file(
            expectations_path,
            WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        host.filesystem.write_text_file(
            skip_path,
            raw_exps + '\n[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]')
        updater = WPTExpectationsUpdater(host)

        updater.write_to_test_expectations(test_expectations)

        expectations_value = host.filesystem.read_text_file(expectations_path)
        skip_value = host.filesystem.read_text_file(skip_path)
        self.assertMultiLineEqual(expectations_value, WPTExpectationsUpdater.MARKER_COMMENT + '\n')
        self.assertMultiLineEqual(
            skip_value,
            raw_exps + '\n[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]\n'
            '[ Trusty ] external/wpt/fake/file/path-manual.html [ Skip ]\n')

    def test_is_reference_test_given_testharness_test(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertFalse(updater.is_reference_test('test/path.html'))

    def test_is_reference_test_given_reference_test(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertTrue(updater.is_reference_test('external/wpt/reftest.html'))

    def test_is_reference_test_given_non_existent_file(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        self.assertFalse(updater.is_reference_test('foo/bar.html'))

    def test_get_test_to_rebaseline_returns_only_tests_with_failures(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        two = {
            'external/wpt/test/path.html': {
                'one':
                SimpleTestResult(expected='FAIL', actual='PASS', bug='bug'),
                'two':
                SimpleTestResult(expected='FAIL', actual='TIMEOUT', bug='bug'),
                'three':
                SimpleTestResult(expected='FAIL', actual='PASS', bug='bug'),
            }
        }
        tests_to_rebaseline, _ = updater.get_tests_to_rebaseline(two)
        self.assertEqual(tests_to_rebaseline, ['external/wpt/test/path.html'])

    def test_get_test_to_rebaseline_does_not_return_ref_tests(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        two = {
            'external/wpt/reftest.html': {
                'one':
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
                'two':
                SimpleTestResult(
                    expected='FAIL', actual='TIMEOUT', bug='crbug.com/test'),
                'three':
                SimpleTestResult(
                    expected='FAIL', actual='PASS', bug='crbug.com/test'),
            }
        }
        tests_to_rebaseline, _ = updater.get_tests_to_rebaseline(two)
        self.assertEqual(tests_to_rebaseline, [])

    def test_get_tests_to_rebaseline_returns_updated_dict(self):
        host = self.mock_host()
        results = {
            'external/wpt/test/path.html': {
                'one':
                SimpleTestResult(expected='PASS', actual='TEXT', bug='bug'),
                'two':
                SimpleTestResult(expected='PASS', actual='TIMEOUT', bug='bug'),
            },
        }
        results_copy = copy.deepcopy(results)
        updater = WPTExpectationsUpdater(host)
        tests_to_rebaseline, modified_test_results = updater.get_tests_to_rebaseline(
            results)
        self.assertEqual(tests_to_rebaseline, ['external/wpt/test/path.html'])
        # The record for the builder with a timeout is kept, but not with a text mismatch,
        # since that should be covered by downloading a new baseline.
        self.assertEqual(
            modified_test_results, {
                'external/wpt/test/path.html': {
                    'two':
                    SimpleTestResult(
                        expected='PASS', actual='TIMEOUT', bug='bug'),
                },
            })
        # The original dict isn't modified.
        self.assertEqual(results, results_copy)

    def test_get_tests_to_rebaseline_also_returns_slow_tests(self):
        host = self.mock_host()
        results = {
            'external/wpt/test/path.html': {
                'one':
                SimpleTestResult(expected='SLOW', actual='TEXT', bug='bug'),
                'two':
                SimpleTestResult(expected='SLOW', actual='TIMEOUT', bug='bug'),
            },
        }
        results_copy = copy.deepcopy(results)
        updater = WPTExpectationsUpdater(host)
        tests_to_rebaseline, modified_test_results = updater.get_tests_to_rebaseline(
            results)
        self.assertEqual(tests_to_rebaseline, ['external/wpt/test/path.html'])
        # The record for the builder with a timeout is kept, but not with a text mismatch,
        # since that should be covered by downloading a new baseline.
        self.assertEqual(
            modified_test_results, {
                'external/wpt/test/path.html': {
                    'two':
                    SimpleTestResult(
                        expected='SLOW', actual='TIMEOUT', bug='bug'),
                },
            })
        # The original dict isn't modified.
        self.assertEqual(results, results_copy)

    def test_get_tests_to_rebaseline_handles_retries(self):
        host = self.mock_host()
        results = {
            'external/wpt/test/foo.html': {
                'bot':
                SimpleTestResult(
                    expected='PASS', actual='TEXT TEXT', bug='bug'),
            },
            'external/wpt/test/bar.html': {
                'bot':
                SimpleTestResult(
                    expected='PASS', actual='TIMEOUT TIMEOUT', bug='bug'),
            },
        }
        updater = WPTExpectationsUpdater(host)
        tests_to_rebaseline, modified_test_results = updater.get_tests_to_rebaseline(
            results)
        self.assertEqual(tests_to_rebaseline, ['external/wpt/test/foo.html'])
        self.assertEqual(
            modified_test_results, {
                'external/wpt/test/foo.html': {},
                'external/wpt/test/bar.html': {
                    'bot':
                    SimpleTestResult(
                        expected='PASS', actual='TIMEOUT TIMEOUT', bug='bug'),
                },
            })

    def test_run_no_issue_number(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        updater.git_cl = MockGitCL(updater.host, issue_number='None')
        with self.assertRaises(ScriptError) as e:
            updater.run()
        self.assertEqual(e.exception.message, 'No issue on current branch.')

    def test_run_no_try_results(self):
        updater = WPTExpectationsUpdater(self.mock_host())
        updater.git_cl = MockGitCL(updater.host, {})
        with self.assertRaises(ScriptError) as e:
            updater.run()
        self.assertEqual(e.exception.message,
                         'No try job information was collected.')

    def test_new_manual_tests_get_skip_expectation(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        results = {
            'external/wpt/x-manual.html': {
                (
                    DesktopConfig('test-linux-precise'),
                    DesktopConfig('test-linux-trusty'),
                    DesktopConfig('test-mac-mac10.10'),
                    DesktopConfig('test-mac-mac10.11'),
                    DesktopConfig('test-win-win7'),
                    DesktopConfig('test-win-win10'),
                ):
                SimpleTestResult(
                    expected='PASS', actual='MISSING', bug='crbug.com/test')
            }
        }
        tests_to_rebaseline, _ = updater.get_tests_to_rebaseline(results)
        self.assertEqual(tests_to_rebaseline, [])
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/x-manual.html':
                ['crbug.com/test external/wpt/x-manual.html [ Skip ]']
            })

    def test_one_platform_has_no_results(self):
        # In this example, there is a failure that has been observed on
        # Linux and one Mac port, but the other Mac port has no results at all.
        # The specifiers are "filled in" and the failure is assumed to apply
        # to all Mac platforms.
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        results = {
            'external/wpt/x.html': {
                (
                    DesktopConfig(port_name='test-linux-precise'),
                    DesktopConfig(port_name='test-linux-trusty'),
                    DesktopConfig(port_name='test-mac-mac10.11'),
                ):
                SimpleTestResult(
                    expected='PASS', actual='TEXT', bug='crbug.com/test')
            }
        }
        updater.configs_with_no_results = [
            DesktopConfig(port_name='test-mac-mac10.10')]
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/x.html': [
                    'crbug.com/test [ Linux ] external/wpt/x.html [ Failure ]',
                    'crbug.com/test [ Mac ] external/wpt/x.html [ Failure ]',
                ]
            })

    def test_same_platform_one_without_results(self):
        # In this example, there are two configs using the same platform
        # (Mac10.10), and one of them has no results while the other one does.
        # The specifiers are "filled in" and the failure is assumed to apply
        # to the platform with missing results.
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        results = {
            'external/wpt/x.html': {
                (
                    DesktopConfig(port_name='test-linux-precise'),
                    DesktopConfig(port_name='test-linux-trusty'),
                    DesktopConfig(port_name='test-mac-mac10.10'),
                ):
                SimpleTestResult(expected='PASS',
                                 actual='TEXT',
                                 bug='crbug.com/test')
            }
        }
        updater.configs_with_no_results = [
            DesktopConfig(port_name='test-mac-mac10.10')
        ]
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/x.html': [
                    'crbug.com/test [ Linux ] external/wpt/x.html [ Failure ]',
                    'crbug.com/test [ Mac10.10 ] external/wpt/x.html [ Failure ]',
                ]
            })

    def test_cleanup_all_deleted_tests_in_expectations_files(self):
        host = MockHost()
        port = host.port_factory.get()
        host.filesystem.files[MOCK_WEB_TESTS + 'TestExpectations'] = (
            '# results: [ Failure ]\n'
            'external/wpt/some/test/a.html?hello%20world [ Failure ]\n'
            'some/test/b.html [ Failure ]\n'
            '# This line should be deleted\n'
            'some/test/c.html [ Failure ]\n'
            '# line below should exist in new file\n'
            'some/test/d.html [ Failure ]\n')
        host.filesystem.files[MOCK_WEB_TESTS + 'VirtualTestSuites'] = '[]'
        host.filesystem.files[MOCK_WEB_TESTS + 'new/a.html'] = ''
        host.filesystem.files[MOCK_WEB_TESTS + 'new/b.html'] = ''
        host.filesystem.files[
            host.filesystem.join(
                port.web_tests_dir(), 'some', 'test', 'd.html')] = ''
        # TODO(rmhasan): Remove creation of Android files within
        # tests.
        for path in PRODUCTS_TO_EXPECTATION_FILE_PATHS.values():
            host.filesystem.write_text_file(path, '')

        updater = WPTExpectationsUpdater(host)

        def _git_command_return_val(cmd):
            if '--diff-filter=D' in cmd:
                return 'some/test/b.html'
            return ''

        updater.git.run = _git_command_return_val
        updater._relative_to_web_test_dir = lambda test_path: test_path
        updater.cleanup_test_expectations_files()
        self.assertMultiLineEqual(
            host.filesystem.read_text_file(MOCK_WEB_TESTS +
                                           'TestExpectations'),
            ('# results: [ Failure ]\n'
             '# line below should exist in new file\n'
             'some/test/d.html [ Failure ]\n'))

    def test_cleanup_all_test_expectations_files(self):
        host = MockHost()
        host.filesystem.files[MOCK_WEB_TESTS + 'TestExpectations'] = (
            '# results: [ Failure ]\n'
            'some/test/a.html [ Failure ]\n'
            'some/test/b.html [ Failure ]\n'
            'ignore/globs/* [ Failure ]\n'
            'some/test/c\*.html [ Failure ]\n'
            # default test case, line below should exist in new file
            'some/test/d.html [ Failure ]\n')
        host.filesystem.files[MOCK_WEB_TESTS + 'WebDriverExpectations'] = (
            '# results: [ Failure ]\n'
            'external/wpt/webdriver/some/test/a\*.html>>foo\* [ Failure ]\n'
            'external/wpt/webdriver/some/test/a\*.html>>bar [ Failure ]\n'
            'external/wpt/webdriver/some/test/b.html>>foo [ Failure ]\n'
            'external/wpt/webdriver/some/test/c.html>>a [ Failure ]\n'
            # default test case, line below should exist in new file
            'external/wpt/webdriver/some/test/d.html>>foo [ Failure ]\n')
        host.filesystem.files[MOCK_WEB_TESTS + 'VirtualTestSuites'] = '[]'
        host.filesystem.files[MOCK_WEB_TESTS + 'new/a.html'] = ''
        host.filesystem.files[MOCK_WEB_TESTS + 'new/b.html'] = ''

        # TODO(rmhasan): Remove creation of Android files within
        # tests.
        for path in PRODUCTS_TO_EXPECTATION_FILE_PATHS.values():
            host.filesystem.write_text_file(path, '')

        updater = WPTExpectationsUpdater(
            host, ['--clean-up-test-expectations-only',
                   '--clean-up-affected-tests-only'])
        deleted_files = [
            'some/test/b.html', 'external/wpt/webdriver/some/test/b.html'
        ]
        renamed_file_pairs = {
            'some/test/a.html': 'new/a.html',
            'some/test/c*.html': 'new/c*.html',
            'external/wpt/webdriver/some/test/a*.html': 'old/a*.html',
            'external/wpt/webdriver/some/test/c.html': 'old/c.html',
        }
        updater._list_deleted_files = lambda: deleted_files
        updater._list_renamed_files = lambda: renamed_file_pairs
        updater.cleanup_test_expectations_files()
        self.assertMultiLineEqual(
            host.filesystem.read_text_file(MOCK_WEB_TESTS +
                                           'TestExpectations'),
            ('# results: [ Failure ]\n'
             'new/a.html [ Failure ]\n'
             'ignore/globs/* [ Failure ]\n'
             'new/c\*.html [ Failure ]\n'
             'some/test/d.html [ Failure ]\n'))
        self.assertMultiLineEqual(
            host.filesystem.read_text_file(MOCK_WEB_TESTS +
                                           'WebDriverExpectations'),
            ('# results: [ Failure ]\n'
             'old/a\*.html>>foo\* [ Failure ]\n'
             'old/a\*.html>>bar [ Failure ]\n'
             'old/c.html>>a [ Failure ]\n'
             'external/wpt/webdriver/some/test/d.html>>foo [ Failure ]\n'))

    def test_merging_platforms_if_possible(self):
        host = self.mock_host()
        updater = WPTExpectationsUpdater(host)
        results = {
            'external/wpt/x.html': {
                (
                    DesktopConfig('test-linux-precise'),
                    DesktopConfig('test-linux-trusty'),
                    DesktopConfig('test-mac-mac10.10'),
                    DesktopConfig('test-mac-mac10.11'),
                    DesktopConfig('test-win-win7'),
                ):
                SimpleTestResult(
                    expected='PASS', actual='TEXT', bug='crbug.com/test')
            }
        }
        self.assertEqual(
            updater.create_line_dict(results), {
                'external/wpt/x.html': [
                    'crbug.com/test [ Linux ] external/wpt/x.html [ Failure ]',
                    'crbug.com/test [ Mac ] external/wpt/x.html [ Failure ]',
                    'crbug.com/test [ Win7 ] external/wpt/x.html [ Failure ]',
                ]
            })
